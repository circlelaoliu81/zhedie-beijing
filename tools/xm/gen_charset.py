#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_charset.py - 汇总玄门全部上屏字符集，供 lv_font_conv 逐尺寸裁剪中文字体子集。

字符来源（并集，确保不缺字、不崩）：
  1. main/**/*.c *.h 中的字符串字面量（UI 文本 xm_strings.h、xm_pages.c 提示语等）。
  2. 黄历：gen_huangli.xm_hl_charset()（字段串 + 宜/忌/吉神/凶煞术语 + 固定查表）。
  3. 易经：gua64.tsv（名/卦辞/大象/白话/宜/忌/时机）+ yaoci384.tsv（384 爻辞）。
  4. 六爻解卦：gua64_detail.tsv 抽取的总论/事业/财富/爱情/健康/家宅上屏文本。
  5. 必备 ASCII 可打印区（日期、数字、标点、拉丁）。

输出：
  assets/data/charset.txt      去重后逐字符（UTF-8，含 ASCII）
  assets/data/charset.sha256  清单 sha256
"""
import os
import re
import sys
import hashlib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MAIN = os.path.join(REPO, "main")
SRC_DIR = os.environ.get(
    "XM_YIJING_SRC",
    "/Volumes/Extreme SSD/老刘的APP/玄门布衣/app/src/main/assets/corpus/yijing",
)
GUA64 = os.path.join(SRC_DIR, "gua64.tsv")
YAOCI = os.path.join(SRC_DIR, "yaoci384.tsv")

STR_LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')


def chars_from_jiegua():
    """六十四卦查表字符串池（卦辞/爻辞/宜忌时机/6 维度解卦）的全部上屏字符。
    直接复用 gen_yijing.build() 生成的字符串池字节并解码，保证字库与实际
    写入卦表的文本一字不差 —— 无论文本来自 tsv 还是内置补丁（如「习坎」），
    都能覆盖；否则 LVGL 查不到字形会静默跳过，卦面/解卦会缺字读不通。
    这也是对 test_xm_ui_charset.py 数据池覆盖门禁的源头保证。"""
    chars = set()
    try:
        import gen_yijing as yj
    except ImportError:
        return chars
    _marks, _meta, pool, _size = yj.build()
    for chunk in pool.split(b"\x00"):
        if not chunk:
            continue
        for ch in chunk.decode("utf-8"):
            if ch not in "\t\n\r":
                chars.add(ch)
    return chars


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
                # 仅还原 \" 与 \\ 两种转义，其余（含 UTF-8 CJK）原样保留
                s = s.replace('\\\\', '\\').replace('\\"', '"')
                chars.update(s)
    return chars


def chars_from_yijing():
    """易经字符：gua64 仅取 卦名/卦辞/大象/白话（跳過 卦符列），yaoci 取 384 爻辞。"""
    chars = set()
    with open(GUA64, "r", encoding="utf-8") as f:
        for line in f:
            if not line or line.startswith("#"):
                continue
            cols = line.rstrip("\n").split("\t")
            for idx, c in enumerate(cols):
                if idx == 1:  # 跳过 卦符 列（hexagram 符号，设备用线条绘制，不需字形）
                    continue
                for ch in c:
                    if ch not in "\t\n\r":
                        chars.add(ch)
    with open(YAOCI, "r", encoding="utf-8") as f:
        for line in f:
            if not line or line.startswith("#"):
                continue
            cols = line.rstrip("\n").split("\t")
            for c in cols[2:3]:  # 仅爻辞列
                for ch in c:
                    if ch not in "\t\n\r":
                        chars.add(ch)
    return chars


def main():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import gen_huangli as hl

    chars = set()
    chars |= chars_from_source_files()
    chars |= chars_from_yijing()
    chars |= chars_from_jiegua()
    chars |= hl.xm_hl_charset()
    # 必备 ASCII 可打印区
    for c in range(0x20, 0x7F):
        chars.add(chr(c))

    # 排序以便稳定输出：ASCII 在前，CJK 在后
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
    sha_path = os.path.join(out_dir, "charset.sha256")
    with open(sha_path, "w", encoding="utf-8") as f:
        f.write(sha + "\n")

    cjk = sum(1 for c in ordered if ord(c) > 0x2000)
    print("字符集生成成功：")
    print("  总字符数 : %d（ASCII %d，CJK/符号 %d）" % (len(ordered), len(ordered) - cjk, cjk))
    print("  sha256   : %s" % sha)
    print("  产物     : %s" % txt_path)
    print("            %s" % sha_path)


if __name__ == "__main__":
    main()
