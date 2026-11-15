#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_xm_ui_charset.py —— 上屏文案 ↔ 字体子集 覆盖门禁。

白屏事故的根因是「字体是压缩位图但没开解压」，而更早的隐患是新增文案
用了字库里没有的汉字：LVGL 查不到字形会直接跳过该字，屏上静默缺字。
本测试把 xm_strings.h、运行时动态文案，以及 xm_yijing_data.c 字符串池里
所有上屏文本（卦辞/爻辞/6 维度解卦）逐字对到 xm_font_12.c 的 cmap，
缺一个字就 FAIL（不需要重新生成字体，避免重演「改文案→白屏」）。
数据池覆盖是后补的：卦表文本不经过 xm_strings.h，早期只查文案常量会漏字。

负向对照：把任意一条文案换成字库外的「龘」，本脚本必须 FAIL。
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(REPO, "assets", "fonts", "xm_font_12.c")
FONT_SIZES = (12, 16, 20, 24)
STRINGS = os.path.join(REPO, "main", "generated", "xm_strings.h")

# 运行时拼出来的文案（不在 xm_strings.h 的字面量里）。
DYNAMIC = [
    "第 1 / 6 爻", "第 6 / 6 爻", "第 1 / 5 段", "第 5 / 5 段",
    "摇卦中 第 3 / 6 爻", "按确定键摇第 4 爻", "六爻已成 按确定键看卦象",
]


def fail(msg):
    print("FAIL: " + msg)
    sys.exit(1)


def font_codepoints(size=12):
    path = os.path.join(REPO, "assets", "fonts", "xm_font_%d.c" % size)
    src = open(path, encoding="utf-8").read()
    cmap = re.search(r"cmaps\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not cmap:
        fail("找不到字体 cmaps 定义")
    starts = [int(v) for v in re.findall(r"\.range_start\s*=\s*(\d+)", cmap.group(1))]
    if not starts:
        fail("cmaps 里没有 range_start")
    sparse = re.search(r"static const uint16_t unicode_list_1\[\]\s*=\s*\{(.*?)\};",
                       src, re.S)
    if not sparse:
        fail("找不到 unicode_list_1（字体可能被改成非稀疏 cmap）")
    offsets = [int(v, 0) for v in
               re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", sparse.group(1))]
    codes = {starts[-1] + v for v in offsets}
    # FORMAT0_TINY 的第一段是连续 ASCII 32..126
    for cp in range(32, 127):
        codes.add(cp)
    return codes, len(offsets)


def ui_texts():
    src = open(STRINGS, encoding="utf-8").read()
    literals = re.findall(r'"((?:[^"\\]|\\.)*)"', src)
    return literals + DYNAMIC


def data_pool_texts():
    """从已生成的 xm_yijing_data.c 字符串池里还原所有上屏文本。
    卦辞/爻辞/解卦维度都是从数据池读出来直接上屏的，不在 xm_strings.h 里；
    旧门禁只查文案常量，漏掉了数据池汉字（曾导致 6 维度 225 字在设备上静默缺字）。
    这里按字节数组还原 UTF-8 字符串，确保数据池每个汉字都在字库里。"""
    path = os.path.join(REPO, "main", "generated", "xm_yijing_data.c")
    if not os.path.exists(path):
        fail("找不到 xm_yijing_data.c，无法核验卦表文本字库覆盖")
    src = open(path, encoding="utf-8").read()
    m = re.search(r"xm_yijing_strpool\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        fail("找不到 xm_yijing_strpool 字节数组")
    byte_vals = [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", m.group(1))]
    pool = bytes(byte_vals)
    texts = []
    for chunk in pool.split(b"\x00"):
        if not chunk:
            continue
        try:
            texts.append(chunk.decode("utf-8"))
        except UnicodeDecodeError:
            fail("字符串池存在非法 UTF-8 字节序列")
    return texts


def main():
    codes, total = font_codepoints()
    if total < 1000:
        fail("字体子集异常小（%d 字），可能没生成完整" % total)
    texts = ui_texts()
    if len(texts) < 20:
        fail("没读到足够的 UI 文案，门禁形同虚设")
    # 数据池文本（卦辞/爻辞/6 维度解卦）同样直接上屏，一并纳入覆盖检查。
    pool_texts = data_pool_texts()
    if len(pool_texts) < 100:
        fail("卦表字符串池条目过少（%d），门禁可能没读到数据" % len(pool_texts))
    all_texts = texts + pool_texts

    missing = {}
    for text in all_texts:
        for ch in text:
            if ord(ch) >= 0x80 and ord(ch) not in codes:
                missing.setdefault(ch, []).append(text)
    if missing:
        for ch, where in sorted(missing.items()):
            print("  缺字 %s U+%04X 出现在：%s" % (ch, ord(ch), where[0][:40]))
        fail("共 %d 个汉字不在字体子集里，设备上会静默不显示" % len(missing))

    # 自检：门禁本身必须能识别缺字，防止「永远 PASS」的假绿。
    if ord("龘") in codes:
        fail("自检失败：字库里居然有龘，无法证明本门禁有效")

    # 正文从 12px 提到 16px 后，四档字号必须共用同一份 charset：
    # 若某档少生成了字，放大后的正文会静默缺字（LVGL 查不到字形直接跳过）。
    for size in FONT_SIZES[1:]:
        other, n = font_codepoints(size)
        if other != codes:
            diff = sorted(codes - other)[:5]
            fail("xm_font_%d.c 与 12px 字库不一致（少 %d 字，例：%s）" %
                 (size, len(codes - other),
                  " ".join("U+%04X" % cp for cp in diff)))
    print("  字体子集 %d 字；%d 条 UI 文案 + %d 条卦表文本全部命中" %
          (total, len(texts), len(pool_texts)))
    print("  四档字号 12/16/20/24 的 cmap 逐字一致（放大字号不缺字）")
    print("  自检：字库外的「龘」可被识别（门禁有效）")
    print("UI 文案字库覆盖：PASS")
    sys.exit(0)


if __name__ == "__main__":
    main()
