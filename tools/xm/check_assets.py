#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_assets.py - 固件构建前的资产门禁（validate.sh --firmware 在 idf.py build 前调用）。

校验：
  1. 两张数据表 .c/.h 存在：黄历/易经 + xm_build_id.h
  2. 四套字体子集 assets/fonts/xm_font_{12,16,20,24}.c 存在
  3. 字符集清单 assets/data/charset.txt 存在
  4. 体量预算：黄历 blob <= 130KiB；四字体合计 <= 6MiB（应用分区 0x7f0000≈7.94MiB）

任一缺失或超限即退出码 1，阻断构建；全部通过打印 PASS 并退出 0。
"""
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

DATA_FILES = [
    "main/generated/xm_huangli_data.c", "main/generated/xm_huangli_data.h",
    "main/generated/xm_yijing_data.c", "main/generated/xm_yijing_data.h",
    "main/generated/xm_build_id.h",
]
FONT_FILES = ["assets/fonts/xm_font_%d.c" % s for s in (12, 16, 20, 24)]
CHARSET = "assets/data/charset.txt"

HL_BLOB_BUDGET = 130 * 1024        # 黄历 blob 预算
FONT_TOTAL_BUDGET = 6 * 1024 * 1024  # 四字体合计预算（远低于 7.94MiB 应用分区）


def fail(msg):
    print("FAIL: %s" % msg)
    sys.exit(1)


def main():
    missing = [f for f in DATA_FILES + FONT_FILES + [CHARSET] if not os.path.exists(os.path.join(REPO, f))]
    if missing:
        for m in missing:
            print("  缺失: %s" % m)
        fail("存在缺失的生成资产，请先运行 tools/xm/gen_*.py 与 gen_fonts.sh")

    # 黄历 blob 体量（解析 xm_huangli_data.c 中的 const uint8_t xm_huangli_blob[] 字节数）
    hl_c = os.path.join(REPO, "main/generated/xm_huangli_data.c")
    hl_bytes = os.path.getsize(hl_c)
    # blob 实际大小在 header 中，但 .c 文件本身就是 blob 的 hex 表达；用 .c 文件大小近似上界即可
    if hl_bytes > HL_BLOB_BUDGET * 2:  # hex 表达约为 2x，留余量
        # 直接估算：每行 16 字节 ×2字符 + 逗号 ≈ 每个字节约 5 字符；改为解析 hex 行
        pass

    # 精确解析 blob 字节数：统计 "0x.." 出现次数
    cnt = 0
    with open(hl_c, "r", encoding="utf-8") as f:
        for line in f:
            cnt += line.count("0x")
    if cnt == 0:
        fail("黄历 blob 未找到字节流")
    if cnt > HL_BLOB_BUDGET:
        fail("黄历 blob 字节数 %d 超过预算 %d" % (cnt, HL_BLOB_BUDGET))
    print("  黄历 blob 字节数 : %d (预算 <= %d)" % (cnt, HL_BLOB_BUDGET))

    # 四字体合计
    tot = 0
    for fn in FONT_FILES:
        sz = os.path.getsize(os.path.join(REPO, fn))
        tot += sz
        print("  字体 %s : %d KiB" % (os.path.basename(fn), sz // 1024))
    if tot > FONT_TOTAL_BUDGET:
        fail("四字体合计 %d 超过预算 %d" % (tot, FONT_TOTAL_BUDGET))
    print("  四字体合计      : %d KiB (预算 <= %d KiB)" % (tot // 1024, FONT_TOTAL_BUDGET // 1024))

    print("资产门禁：PASS")
    sys.exit(0)


if __name__ == "__main__":
    main()
