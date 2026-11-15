#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_charset.py - 汇总折叠北京全部上屏字符集，供 lv_font_conv 逐尺寸裁剪中文字体子集。

折叠北京是纯文案驱动的小游戏：所有上屏文本都写在 main/**/*.c 的字符串字面量里
（fb_app.c 的页面模板 + fb_game.c 的事件/结局/结局文案），没有外部 tsv 数据源。
因此字符集 = 源码字面量 ∪ 必备 ASCII 可打印区。

输出：
  assets/data/charset.txt      去重后逐字符（UTF-8，含 ASCII）
  assets/data/charset.sha256   清单 sha256
"""
import hashlib
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MAIN = os.path.join(REPO, "main")
STR_LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')


def chars_from_source_files():
    """提取 main/** 字符串字面量中的真实 UTF-8 字符（CJK 直接书写，不做 unicode_escape）。"""
    chars = set()
    for root, _dirs, files in os.walk(MAIN):
        for fn in files:
            if not (fn.endswith(".c") or fn.endswith(".h")):
                continue
            path = os.path.join(root, fn)
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                text = f.read()
            for m in STR_LIT.finditer(text):
                s = m.group(1)
                s = s.replace('\\\\', '\\').replace('\\"', '"')
                chars.update(s)
    return chars


def main():
    chars = chars_from_source_files()
    # 必备 ASCII 可打印区（金额、天数、百分比、键位提示）
    for c in range(0x20, 0x7F):
        chars.add(chr(c))

    def sort_key(c):
        o = ord(c)
        return (0, o) if o < 128 else (1, o)

    ordered = sorted(chars, key=sort_key)
    out_dir = os.path.join(REPO, "assets", "data")
    os.makedirs(out_dir, exist_ok=True)
    txt_path = os.path.join(out_dir, "charset.txt")
    with open(txt_path, "w", encoding="utf-8") as f:
        f.write("".join(ordered))
        f.write("\n")
    data = "".join(ordered).encode("utf-8")
    sha = hashlib.sha256(data).hexdigest()
    with open(os.path.join(out_dir, "charset.sha256"), "w", encoding="utf-8") as f:
        f.write(sha + "\n")

    cjk = sum(1 for c in ordered if ord(c) > 0x2000)
    print("字符集生成成功：")
    print("  总字符数 : %d（ASCII/拉丁 %d，CJK/符号 %d）" % (len(ordered), len(ordered) - cjk, cjk))
    print("  sha256   : %s" % sha)
    print("  产物     : %s" % txt_path)


if __name__ == "__main__":
    main()
