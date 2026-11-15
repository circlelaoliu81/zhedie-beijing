#include "fb_save.h"

#include <stddef.h>
#include <string.h>

/* "ZDBZ" —— 字节序在 C3（小端）上固定，换个平台也只是 magic 不匹配 */
#define FB_SAVE_MAGIC 0x5A44425Au

/* 落盘布局：头部 + 状态。字段顺序与填充由编译器决定，
 * 同一固件内读写必然一致；跨版本靠 version 拦，不做跨版本迁移。 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t len;      /* 整个结构体长度，用来挡住长度漂移 */
    uint32_t crc;      /* 覆盖 crc 字段之后的全部字节 */
    uint32_t seed;
    uint8_t bg_sel;
    uint8_t line_sel;
    uint8_t home_sel;
    uint8_t pad;
    fb_state_t st;
} fb_save_blob_t;

#define FB_CRC_OFFSET (offsetof(fb_save_blob_t, crc) + sizeof(uint32_t))

/* CRC-32（IEEE 802.3，反射多项式 0xEDB88320）。
 * 逐位算而不是查表：256×4 = 1KB 表在 8MB Flash 上无所谓，
 * 但这段代码是纯验证逻辑，能少一份数据就少一处出错的地方。 */
uint32_t fb_save_crc32(const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

static uint32_t blob_crc(const fb_save_blob_t *b) {
    return fb_save_crc32((const uint8_t *)b + FB_CRC_OFFSET,
                         sizeof(*b) - FB_CRC_OFFSET);
}

size_t fb_save_pack(const fb_app_t *app, void *buf, size_t cap) {
    if (!app || !buf) return 0;
    if (cap < sizeof(fb_save_blob_t)) return 0;
    /* 已经结束的局不存：读回来是个死档，不如让玩家重开 */
    if (app->st.ending != FB_END_NONE) return 0;

    fb_save_blob_t b;
    /* 先清零，把结构体填充字节也定下来 —— 否则两次 pack 的 crc 会不同 */
    memset(&b, 0, sizeof(b));
    b.magic = FB_SAVE_MAGIC;
    b.version = FB_SAVE_VERSION;
    b.len = (uint16_t)sizeof(b);
    b.seed = app->seed;
    b.bg_sel = app->bg_sel;
    b.line_sel = app->line_sel;
    b.home_sel = app->home_sel;
    b.st = app->st;
    b.crc = blob_crc(&b);

    memcpy(buf, &b, sizeof(b));
    return sizeof(b);
}

/* 读回来的数一个都不能信：坏档、旧档、甚至半截写入都可能落进这里。
 * 全部先夹到合法区间，剩下的不合法字段一律回默认值。 */
static void sanitize(fb_state_t *st) {
    if (st->day < 1) st->day = 1;
    if (st->day > FB_TOTAL_DAYS) st->day = FB_TOTAL_DAYS;
    if (st->health < 0) st->health = 0;
    if (st->health > 100) st->health = 100;
    if (st->stamina < 0) st->stamina = 0;
    if (st->stamina > 100) st->stamina = 100;
    if (st->reputation < 0) st->reputation = 0;
    if (st->reputation > 100) st->reputation = 100;
    if (st->hours_used > FB_DAY_HOURS) st->hours_used = FB_DAY_HOURS;
    if (st->spot >= FB_SPOT_COUNT) st->spot = 0;
    if (st->drowsy > FB_DROWSY_CRITICAL) st->drowsy = FB_DROWSY_CRITICAL;
    if (st->sleep_debt > FB_SLEEP_DEBT_MAX) st->sleep_debt = FB_SLEEP_DEBT_MAX;
    if (st->ending >= FB_END_DEAD) st->ending = FB_END_NONE;
    if (st->bg >= 3) st->bg = 0;

    /* 库存重算总数，不信任存档里的 inv_total */
    uint16_t total = 0;
    for (uint8_t i = 0; i < FB_KIND_COUNT; ++i) total = (uint16_t)(total + st->inv[i]);
    st->inv_total = (uint8_t)(total > 255 ? 255 : total);
}

bool fb_save_unpack(fb_app_t *app, const void *buf, size_t len) {
    if (!app || !buf) return false;
    if (len < sizeof(fb_save_blob_t)) return false;

    fb_save_blob_t b;
    memcpy(&b, buf, sizeof(b));
    if (b.magic != FB_SAVE_MAGIC || b.version != FB_SAVE_VERSION) return false;
    if (b.len != sizeof(b)) return false;
    if (b.crc != blob_crc(&b)) return false;
    if (b.st.ending != FB_END_NONE) return false;

    /* 用 init 建一份干净状态（含需求矩阵/新闻），再整体覆盖 */
    fb_app_init(app, b.seed);
    app->st = b.st;
    sanitize(&app->st);
    app->seed = b.seed;
    app->bg_sel = b.bg_sel < 3 ? b.bg_sel : 0;
    app->line_sel = b.line_sel < FB_LINE_COUNT ? b.line_sel : 0;
    app->home_sel = b.home_sel < FB_MENU_COUNT ? b.home_sel : 0;
    app->toast[0] = '\0';
    /* 不直接跳到主面板：停在封面，让玩家自己点「继续上次（第 N 天）」。
     * 开机就替玩家做决定，和擅自改他存档是一回事。 */
    app->sel = 0;
    app->scr = FB_SCR_INTRO;
    app->have_save = true;
    app->save_day = app->st.day;
    app->dirty = true;
    return true;
}
