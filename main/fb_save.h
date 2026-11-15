#pragma once
/* 折叠北京 · 存档序列化（纯 C11，不依赖 ESP-IDF，可 host 测试）
 *
 * 只存「一局的可恢复状态」——fb_state_t 加三个 UI 光标。
 * 不存当前停在哪个页面：读档一律回到主面板，避免恢复到结算页/事件页
 * 这类半截状态（事件指针指向只读表，存不下来）。
 *
 * 头部带 magic + version + len + crc32，任一不符就当没有存档。
 * 宁可让玩家重开一局，也不能把坏档灌进引擎 —— 那种崩溃现象在设备上
 * 只会表现为「开机黑屏」，根本查不出来。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fb_app.h"

#define FB_SAVE_VERSION 1u
/* 给调用方开栈缓冲用的上界，留足余量（实际约 100 字节量级） */
#define FB_SAVE_MAX 256

/* 序列化当前进度。返回写入字节数；局已结束或缓冲不够返回 0。 */
size_t fb_save_pack(const fb_app_t *app, void *buf, size_t cap);

/* 反序列化。校验失败返回 false 且不改动 app。成功时 scr 置为主面板。 */
bool fb_save_unpack(fb_app_t *app, const void *buf, size_t len);

uint32_t fb_save_crc32(const void *data, size_t n);
