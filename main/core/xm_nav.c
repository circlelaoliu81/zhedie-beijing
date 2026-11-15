#include "xm_nav.h"

#include <string.h>
#include "xm_time.h"

/* 摇卦动画时长与帧步长：输入任务 100ms 一次 tick，720ms 约 7 帧。
 * 每帧都由 nav_tick 置 render，屏幕上看得到「三枚钱摇动 → 落定成爻」。 */
#define XM_CAST_ANIM_US   720000LL
#define XM_CAST_FRAME_US  100000LL
#define XM_CAST_FRAME_MAX 7u

void xm_nav_init(xm_nav_t *nav) {
    if (!nav) return;
    memset(nav, 0, sizeof(*nav));
    nav->page = XM_PAGE_DISCLAIMER;
    nav->disclaimer_acked = false;
    nav->time_confirmed = false;
    nav->draft_date.year = 2026;
    nav->draft_date.month = 11;
    nav->draft_date.day = 13;
    nav->draft_date.hour = 10;
    nav->draft_date.minute = 30;
    /* 从「年」开始调，按确定键依次推进到「分」，与屏上提示一致。 */
    nav->time_cursor = 0;
    nav->pending_page = XM_PAGE_HOME;
    nav->err_flags = XM_ERR_TIME_UNCALIBRATED_BIT;
}

void xm_nav_push(xm_nav_t *nav, xm_page_t page) {
    if (!nav || nav->depth >= 8) return;
    nav->stack[nav->depth++] = page;
}

xm_page_t xm_nav_pop(xm_nav_t *nav) {
    if (!nav || nav->depth == 0) return XM_PAGE_HOME;
    return nav->stack[--nav->depth];
}

static void xm_nav_adjust_date(xm_nav_t *nav, int delta) {
    const uint8_t cursor = nav->time_cursor;
    if (cursor == 0) {
        int year = (int)nav->draft_date.year + delta;
        if (year < 2026) year = 2026;
        if (year > 2035) year = 2035;
        nav->draft_date.year = (uint16_t)year;
    } else if (cursor == 1) {
        int month = (int)nav->draft_date.month + delta;
        if (month < 1) month = 12;
        if (month > 12) month = 1;
        nav->draft_date.month = (uint8_t)month;
    } else if (cursor == 2) {
        const uint8_t dim = xm_time_days_in_month(nav->draft_date.year, nav->draft_date.month);
        int day = (int)nav->draft_date.day + delta;
        if (day < 1) day = dim;
        if (day > dim) day = 1;
        nav->draft_date.day = (uint8_t)day;
    } else if (cursor == 3) {
        int hour = (int)nav->draft_date.hour + delta;
        if (hour < 0) hour = 23;
        if (hour > 23) hour = 0;
        nav->draft_date.hour = (uint8_t)hour;
    } else {
        int minute = (int)nav->draft_date.minute + delta;
        if (minute < 0) minute = 59;
        if (minute > 59) minute = 0;
        nav->draft_date.minute = (uint8_t)minute;
    }
}

static void xm_nav_result_init(xm_nav_result_t *out, const xm_nav_t *nav) {
    memset(out, 0, sizeof(*out));
    out->page = nav->page;
    out->action = XM_NAV_ACTION_NONE;
}

static void xm_nav_abandon_cast(xm_nav_t *nav, int64_t now_us) {
    nav->casting = false;
    nav->cast_count = 0;
    nav->cast_phase = 0u;
    nav->cast_frame = 0u;
    nav->cast_anim_us = 0;
    nav->notice = true;
    nav->notice_until_us = now_us + 1500000LL;
    nav->pending_page = XM_PAGE_HOME;
}

xm_status_t xm_nav_dispatch(xm_nav_t *nav, xm_input_t input, int64_t now_us,
                            xm_nav_result_t *out) {
    if (!nav || !out || input == XM_IN_NONE) return XM_ERR_INVALID_ARG;
    xm_nav_result_init(out, nav);
    if (!nav->disclaimer_acked && !nav->review_mode && nav->page != XM_PAGE_DISCLAIMER) {
        nav->page = XM_PAGE_DISCLAIMER;
        nav->depth = 0;
        out->page = nav->page;
        out->action = XM_NAV_ACTION_RENDER;
        return XM_OK;
    }
    switch (nav->page) {
        case XM_PAGE_DISCLAIMER:
            if (input == XM_IN_OK_CLICK) {
                if (nav->review_mode) {
                    nav->review_mode = false;
                    nav->page = XM_PAGE_SETTINGS;
                } else {
                    nav->disclaimer_acked = true;
                    /* 首启先看一页三键操作说明，再由引导页进授时页。 */
                    nav->page = XM_PAGE_KEYGUIDE;
                    out->persist_ack = true;
                }
                out->action = XM_NAV_ACTION_RENDER;
            }
            break;
        case XM_PAGE_KEYGUIDE:
            /* 引导页只有一个出口：按确定键进入授时页。 */
            if (input == XM_IN_OK_CLICK) {
                nav->page = XM_PAGE_TIME;
                out->action = XM_NAV_ACTION_RENDER;
            }
            break;
        case XM_PAGE_TIME:
            /* 键位与屏上提示严格一致：
             *   上/下键 = 调整「当前高亮字段」的数值
             *   确定键   = 选定本段并进下一段；在「分」上按确定键 = 完成设定
             *   长按确定键 = 从设置页进来时返回设置（首启无上一页，不许跳过授时）
             * 旧版把「切字段」绑在双击上/下键，屏上却写「确定键选定」；而驱动会把
             * 快速连按合并成 DOUBLE，用户连按调数值时字段反而乱跳 —— 表现为授时无法操作。 */
            if (input == XM_IN_UP_CLICK) {
                xm_nav_adjust_date(nav, 1);
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_DOWN_CLICK) {
                xm_nav_adjust_date(nav, -1);
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_OK_CLICK) {
                if (nav->time_cursor < 4u) {
                    nav->time_cursor++;
                } else {
                    nav->time_confirmed = true;
                    nav->page = XM_PAGE_HOME;
                    nav->depth = 0;
                    out->persist_time = true;
                }
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_OK_LONG && nav->depth > 0u) {
                nav->time_confirmed = false;
                nav->page = xm_nav_pop(nav);
                out->action = XM_NAV_ACTION_RENDER;
            }
            break;
        case XM_PAGE_HOME:
            /* 首页只有两项（黄历 / 六爻）：上下键在 2 项上等价，都是切换。 */
            if (input == XM_IN_UP_CLICK || input == XM_IN_DOWN_CLICK) {
                nav->sel = (uint8_t)((nav->sel + 1u) & 1u);
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_OK_CLICK) {
                xm_nav_push(nav, XM_PAGE_HOME);
                if (nav->sel == 0) {
                    nav->page = XM_PAGE_HL_A;
                } else {
                    nav->page = XM_PAGE_LY_CAST;
                    nav->casting = true;
                    nav->cast_count = 0;
                    nav->cast_phase = 0u;
                    nav->cast_frame = 0u;
                }
                nav->sub = 0;
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_OK_LONG) {
                xm_nav_push(nav, XM_PAGE_HOME);
                nav->page = XM_PAGE_SETTINGS;
                nav->sel = 0;
                out->action = XM_NAV_ACTION_RENDER;
            }
            break;
        case XM_PAGE_HL_A:
        case XM_PAGE_HL_B:
        case XM_PAGE_HL_C:
            if (input == XM_IN_UP_CLICK || input == XM_IN_DOWN_CLICK) {
                /* 不绕圈：两端夹紧。官方 three-key-reader-interaction 指出会绕圈的列表
                 * 会被用户描述成「上下键反了」。sub 只取 0..2，按增量取模后再夹紧。 */
                const int next = (int)nav->sub + (input == XM_IN_UP_CLICK ? -1 : 1);
                nav->sub = (uint8_t)(next < 0 ? 0 : (next > 2 ? 2 : next));
                nav->page = (xm_page_t)(XM_PAGE_HL_A + nav->sub);
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_OK_LONG) {
                nav->page = xm_nav_pop(nav);
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_OK_CLICK) {
                out->action = XM_NAV_ACTION_RENDER;
            }
            break;
        case XM_PAGE_LY_CAST:
            if (input == XM_IN_OK_LONG) {
                xm_nav_abandon_cast(nav, now_us);
                out->action = XM_NAV_ACTION_RENDER;
                break;
            }
            if (nav->cast_phase == 1u) {
                /* 摇动中：吃掉确定键，避免连按把六爻一次跳过；动画结束自然会重绘。 */
                break;
            }
            if (input == XM_IN_OK_CLICK) {
                if (nav->cast_count < 6u) {
                    /* 一次确定键 = 摇一爻，先播动画，爻线要等落定才出现。 */
                    nav->cast_count++;
                    out->cast_step = true;
                    nav->cast_phase = 1u;
                    nav->cast_frame = 0u;
                    nav->cast_anim_us = now_us;
                } else {
                    /* 六爻已成，屏幕提示「按确定键看卦象」，这一步由用户明确按下。 */
                    nav->casting = false;
                    nav->page = XM_PAGE_LY_RESULT;
                    nav->sub = 0;
                    out->finish_cast = true;
                }
                out->action = XM_NAV_ACTION_RENDER;
            }
            break;
        case XM_PAGE_LY_RESULT:
            /* 结果页拆成九屏（卦象/动爻/总论/事业/财富/爱情/健康/家宅/占断），
             * 上/下键循环切换，让用户把卦面、动爻、分维度解卦全部看完；
             * 确定键进历史（保持「确定=进入」语义），长按回首页。 */
            if (input == XM_IN_UP_CLICK) {
                nav->sub = (uint8_t)((nav->sub + 8u) % 9u);
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_DOWN_CLICK) {
                nav->sub = (uint8_t)((nav->sub + 1u) % 9u);
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_OK_CLICK) {
                xm_nav_push(nav, XM_PAGE_LY_RESULT);
                nav->page = XM_PAGE_LY_HISTORY;
                nav->sel = 0;
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_OK_LONG) {
                nav->page = xm_nav_pop(nav);
                out->action = XM_NAV_ACTION_RENDER;
            }
            break;
        case XM_PAGE_LY_HISTORY:
            if (input == XM_IN_UP_CLICK || input == XM_IN_DOWN_CLICK) {
                nav->sel = (uint8_t)((nav->sel + (input == XM_IN_UP_CLICK ? 7u : 1u)) & 7u);
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_OK_LONG) {
                nav->page = xm_nav_pop(nav);
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_OK_CLICK) {
                out->action = XM_NAV_ACTION_RENDER;
            }
            break;
        case XM_PAGE_SETTINGS:
            if (input == XM_IN_UP_CLICK || input == XM_IN_DOWN_CLICK) {
                nav->sel = (uint8_t)((nav->sel + (input == XM_IN_UP_CLICK ? 4u : 1u)) % 5u);
                nav->confirm_clear = false;
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_OK_CLICK) {
                if (nav->sel == 0) {
                    xm_nav_push(nav, XM_PAGE_SETTINGS);
                    nav->page = XM_PAGE_TIME;
                    nav->time_confirmed = false;
                    nav->time_cursor = 0u;
                } else if (nav->sel == 1) {
                    nav->review_mode = true;
                    nav->page = XM_PAGE_DISCLAIMER;
                } else if (nav->sel == 2) {
                    if (!nav->confirm_clear) {
                        nav->confirm_clear = true;
                    } else {
                        nav->confirm_clear = false;
                        out->clear_hist = true;
                    }
                } else if (nav->sel == 3) {
                    xm_nav_push(nav, XM_PAGE_SETTINGS);
                    nav->page = XM_PAGE_ABOUT;
                    nav->sub = 0;
                }
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_OK_LONG) {
                nav->page = xm_nav_pop(nav);
                out->action = XM_NAV_ACTION_RENDER;
            }
            break;
        case XM_PAGE_ABOUT:
            /* 关于页是静态整页：上下键只吃掉按键不误触，长按确定键返回。 */
            if (input == XM_IN_UP_CLICK || input == XM_IN_DOWN_CLICK) {
                out->action = XM_NAV_ACTION_RENDER;
            } else if (input == XM_IN_OK_LONG) {
                nav->page = xm_nav_pop(nav);
                out->action = XM_NAV_ACTION_RENDER;
            }
            break;
        default:
            break;
    }
    out->page = nav->page;
    return XM_OK;
}

xm_status_t xm_nav_tick(xm_nav_t *nav, int64_t now_us, bool *render) {
    if (!nav || !render) return XM_ERR_INVALID_ARG;
    *render = false;
    /* 摇卦动画：每次 tick 推进一帧并要求重绘；到时落定本爻，屏上提示
     * 「按确定键摇下一爻」，用户始终知道该按哪个键、现在第几爻。 */
    if (nav->cast_phase == 1u) {
        const int64_t elapsed = now_us - nav->cast_anim_us;
        if (elapsed >= XM_CAST_ANIM_US) {
            nav->cast_phase = 0u;
            nav->cast_frame = 0u;
            *render = true;
        } else {
            uint8_t frame = (uint8_t)(elapsed / XM_CAST_FRAME_US);
            if (frame > XM_CAST_FRAME_MAX) frame = XM_CAST_FRAME_MAX;
            if (frame != nav->cast_frame) {
                nav->cast_frame = frame;
                *render = true;
            }
        }
    }
    if (nav->notice && now_us >= nav->notice_until_us) {
        nav->notice = false;
        nav->page = nav->pending_page;
        nav->depth = 0;
        *render = true;
    }
    return XM_OK;
}

void xm_nav_set_error(xm_nav_t *nav, uint8_t flags) {
    if (nav) nav->err_flags = flags;
}

uint8_t xm_nav_error_flags(const xm_nav_t *nav) {
    return nav ? nav->err_flags : 0u;
}
