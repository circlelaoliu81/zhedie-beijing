#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_xm_tianshi_bitmap.py —— 天师白描位图的双端一致性门禁（零第三方依赖）。

固件画 main/generated/xm_tianshi_img_data.c 里的 RGB565 常量，
host 预览贴 assets/art/*.png，另有同像素的 .rgb565 字节镜像。
两者只要分叉，用户在预览里看到的画就不是真机上的画。

门禁跑在 ESP-IDF 自带的精简 Python（没有 numpy、没有 PIL），所以这里
只用标准库：PNG 头用 struct 读 IHDR，像素用 .rgb565 原始字节。

  1. 头文件 W/H 宏 == manifest == PNG IHDR 尺寸 == C 数组长度 == bin 长度
  2. C 数组每个 uint16 == .rgb565 对应字节
  3. 解码回 RGB 后所有像素都必须落在「米纸底 ↔ 浓墨」连续墨阶上
  4. PNG / bin / RGB565 的 sha256 与 manifest 一致
"""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "assets" / "art"
GEN = ROOT / "main" / "generated"
HEADER = GEN / "xm_tianshi_img.h"
SOURCE = GEN / "xm_tianshi_img_data.c"
MANIFEST = ART / "manifest.json"

PAPER = (0xF2, 0xEB, 0xDC)
INK = (0x2B, 0x2A, 0x27)


def png_size(path):
    """只读 PNG 的 IHDR，不解码像素，不需要 PIL。"""
    d = path.read_bytes()
    if d[:8] != b"\x89PNG\r\n\x1a\n" or d[12:16] != b"IHDR":
        raise ValueError("不是合法 PNG：%s" % path)
    return struct.unpack(">II", d[16:24])


def rgb565_to_rgb(v):
    """5/6/5 还原成 8/8/8，用于墨阶校验。"""
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def on_ink_ramp(rgb):
    """像素必须落在 PAPER..INK 的线性墨阶上，允许 RGB565 量化 ±6 误差。"""
    span = [PAPER[i] - INK[i] for i in range(3)]
    ts = [(rgb[i] - INK[i]) / span[i] for i in range(3) if span[i]]
    t = sum(ts) / len(ts)
    for i in range(3):
        if abs(rgb[i] - (INK[i] + t * span[i])) > 6.0:
            return False
    return True


def parse_header():
    txt = HEADER.read_text(encoding="utf-8")
    out = {}
    for name, w, h in re.findall(
            r"#define\s+(\w+)_W\s+(\d+)u\s*\n#define\s+\1_H\s+(\d+)u", txt):
        out[name.lower()] = (int(w), int(h))
    return out


def parse_source():
    txt = SOURCE.read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(
            r"const uint16_t\s+(\w+)_rgb565\[[^\]]+\]\s*=\s*\{(.*?)\};",
            txt, re.S):
        out[m.group(1)] = [int(v, 16)
                           for v in re.findall(r"0x([0-9A-Fa-f]{4})", m.group(2))]
    return out


def main():
    if not (HEADER.exists() and SOURCE.exists() and MANIFEST.exists()):
        print("FAIL: 位图资产不完整（缺 h / c / manifest）")
        return 1
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    dims = parse_header()
    arrays = parse_source()

    for name, meta in manifest["images"].items():
        w, h = meta["w"], meta["h"]
        if dims.get(name) != (w, h):
            print("FAIL: %s 头文件尺寸 %s != manifest %s" % (name, dims.get(name), (w, h)))
            return 1
        png = ART / (name + ".png")
        binf = ART / (name + ".rgb565")
        if not png.exists() or not binf.exists():
            print("FAIL: 位图产物缺失（PNG 或 .rgb565）: %s" % name)
            return 1
        if hashlib.sha256(png.read_bytes()).hexdigest() != meta["png_sha256"]:
            print("FAIL: %s PNG 与 manifest 哈希不符" % name)
            return 1
        try:
            if png_size(png) != (w, h):
                print("FAIL: %s PNG IHDR 尺寸不符" % name)
                return 1
        except ValueError as e:
            print("FAIL: %s" % e)
            return 1

        raw = binf.read_bytes()
        if hashlib.sha256(raw).hexdigest() not in (meta["bin_sha256"],
                                                   meta["rgb565_sha256"]):
            print("FAIL: %s 字节镜像与 manifest 哈希不符" % name)
            return 1
        if len(raw) != w * h * 2:
            print("FAIL: %s 字节镜像长度 %d != %d" % (name, len(raw), w * h * 2))
            return 1
        pixels = [raw[i] | (raw[i + 1] << 8) for i in range(0, len(raw), 2)]
        vals = arrays.get(name, [])
        if len(vals) != w * h:
            print("FAIL: %s C 数组长度 %d != %d" % (name, len(vals), w * h))
            return 1
        for i, (a, b) in enumerate(zip(vals, pixels)):
            if a != b:
                print("FAIL: %s 第 %d 像素 C=0x%04X 镜像=0x%04X（固件与预览分叉）"
                      % (name, i, a, b))
                return 1
        bad = sum(1 for v in pixels if not on_ink_ramp(rgb565_to_rgb(v)))
        if bad:
            print("FAIL: %s 有 %d 个像素不在纸↔墨连续墨阶上" % (name, bad))
            return 1
        ink = sum(1 for v in pixels if sum(rgb565_to_rgb(v)) < 330)
        print("  %s %dx%d  墨迹像素=%d  双端逐像素一致: PASS"
              % (name, w, h, ink))

    print("test_xm_tianshi_bitmap: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
