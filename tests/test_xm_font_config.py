#!/usr/bin/env python3
"""字体压缩格式 ↔ LVGL 解压开关一致性门禁。

真机事故（2026-11-14）：assets/fonts/xm_font_*.c 由 lv_font_conv 生成为压缩位图
（.bitmap_format = 1），但 CONFIG_LV_USE_FONT_COMPRESSED 未开启。LVGL 9.5 的
lv_font_get_bitmap_fmt_txt() 在压缩分支直接 return NULL 且只发一条 LV_LOG_WARN，
导致所有中文 label 取不到位图：整屏只剩米黄底和红框，一个字都不画。
日志里 0 panic / 0 Error / flush 直方图全是底色，静态测试却全绿。

本检查把「字体数据格式」与「编译期开关」绑在一起，纯 host 静态即可拦截，
避免再靠真机目视才发现。
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
KEY = "CONFIG_LV_USE_FONT_COMPRESSED"


def bitmap_formats():
    fonts = sorted((ROOT / "assets" / "fonts").glob("xm_font_*.c"))
    if not fonts:
        sys.exit("FAIL: 找不到 assets/fonts/xm_font_*.c")
    result = {}
    for path in fonts:
        match = re.search(r"\.bitmap_format\s*=\s*(\d+)",
                          path.read_text(encoding="utf-8"))
        if not match:
            sys.exit(f"FAIL: {path.name} 找不到 .bitmap_format")
        value = int(match.group(1))
        if value not in (0, 1, 2):
            sys.exit(f"FAIL: {path.name} .bitmap_format={value} 非法")
        result[path.name] = value
    return result


def config_value(path):
    if not path.exists():
        return "MISSING"
    text = path.read_text(encoding="utf-8")
    match = re.search(rf"^{re.escape(KEY)}=(\S+)", text, re.M)
    if match:
        return match.group(1)
    if re.search(rf"^# {re.escape(KEY)} is not set", text, re.M):
        return "n"
    return "ABSENT"


def main():
    formats = bitmap_formats()
    compressed = {name: value for name, value in formats.items() if value != 0}
    print("字体位图格式: " + ", ".join(f"{n}={v}" for n, v in formats.items()))

    for cfg in ("sdkconfig.defaults", "sdkconfig"):
        value = config_value(ROOT / cfg)
        print(f"{cfg}: {KEY}={value}")
        if compressed and value != "y":
            sys.exit(
                f"FAIL: {cfg} 的 {KEY}={value}，但 {len(compressed)} 套字体是压缩位图。"
                "LVGL 9.5 会 return NULL，真机所有中文不显示。"
            )

    if compressed:
        print(f"PASS: {len(formats)} 套字体 / 压缩 {len(compressed)} 套，两份配置均已开启解压")
    else:
        print(f"PASS: {len(formats)} 套字体均为 PLAIN，无需解压开关")


if __name__ == "__main__":
    main()
