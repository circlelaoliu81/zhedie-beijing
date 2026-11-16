#include "fb_idle.h"

void fb_idle_init(fb_idle_t *idle, uint32_t now_ms) {
    idle->last_active_ms = now_ms;
    idle->asleep = false;
}

bool fb_idle_is_asleep(const fb_idle_t *idle) { return idle->asleep; }

bool fb_idle_on_key(fb_idle_t *idle, uint32_t now_ms) {
    idle->last_active_ms = now_ms;
    if (!idle->asleep) return false;
    idle->asleep = false;
    return true;
}

bool fb_idle_poll(fb_idle_t *idle, uint32_t now_ms) {
    if (idle->asleep) return false;
    /* 用有符号差值比较：esp_timer 的毫秒计数约 49 天回绕一次，
     * 直接比大小会在回绕点把「刚按过」判成「已经很久没按」。 */
    if ((int32_t)(now_ms - idle->last_active_ms) < (int32_t)FB_IDLE_TIMEOUT_MS) {
        return false;
    }
    idle->asleep = true;
    return true;
}
