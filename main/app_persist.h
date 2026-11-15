#pragma once
/* 折叠北京 · 存档落盘桥（设备层）
 *
 * fb_save.c 只管字节；这一层管「什么时候存、存到哪儿」。
 * 分开的理由：host 测试要能编 fb_save.c 而不拉进 NVS/ESP-IDF。
 */

#include <stdbool.h>

#include "fb_app.h"

/* 尝试把已有存档读进 app。成功返回 true，此时 app 已停在主面板。
 * 没有存档 / 存档损坏 / NVS 不可用一律返回 false，app 保持原样。 */
bool app_persist_resume(fb_app_t *app);

/* 状态机置了 save_dirty 就落盘一次；局已结束则删档。
 * 每次按键后调用，一天最多写两次，不担心 Flash 磨损。 */
void app_persist_tick(fb_app_t *app);

/* 主动删档（重开一局时用） */
void app_persist_clear(void);
