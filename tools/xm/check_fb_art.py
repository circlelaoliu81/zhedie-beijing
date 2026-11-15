#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""美术产物的格式门禁。

主机预览画的是 PNG，走的是 PIL 的 RGBA 合成路径 —— 它证明不了设备端能正确
解出 RGB565 / RGB565A8。这里按 LVGL 的**实际读取语义**把二进制解回来，再和
源 PNG 逐像素比对：

  RGB565    : 每像素 2 字节小端，行内无 padding。
  RGB565A8  : RGB565 平面在前（stride = w*2），A8 平面紧随其后（stride = w）。
              取 alpha 的偏移必须是 stride*h —— LVGL 在
              lv_draw_sw_img.c 里就是按 img_stride * src_h 定位的。

任何一项对不上，设备上都会是花屏或黑块，而不是报错。
"""
import json
import sys
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
ART = REPO / "assets" / "art"


def rgb565_to_rgb(v):
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def quantize_rgb(img):
    """把源图按 565 量化再解回 24 位。

    PNG 里存的是原始 24 位色，而设备拿到的是 565 —— 直接逐字节比必然不同。
    让参考图走一遍同样的量化，两边的差异就只可能来自编码或布局错误。
    """
    out = bytearray()
    px = img.convert("RGB").tobytes()
    for i in range(0, len(px), 3):
        r, g, b = px[i], px[i + 1], px[i + 2]
        out += bytes(rgb565_to_rgb(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)))
    return bytes(out)


def decode_scene(raw, w, h):
    if len(raw) != w * h * 2:
        raise ValueError(f"长度 {len(raw)} != {w*h*2}")
    out = bytearray(w * h * 3)
    for i in range(w * h):
        lo, hi = raw[i * 2], raw[i * 2 + 1]
        r, g, b = rgb565_to_rgb(lo | (hi << 8))
        out[i * 3], out[i * 3 + 1], out[i * 3 + 2] = r, g, b
    return bytes(out)


def decode_icon(raw, w, h):
    stride = w * 2
    if len(raw) != stride * h + w * h:
        raise ValueError(f"长度 {len(raw)} != {stride*h + w*h}")
    rgb = raw[: stride * h]
    alpha = raw[stride * h:]          # 与 LVGL 的 stride*h 偏移一致
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            i = y * w + x
            lo, hi = rgb[i * 2], rgb[i * 2 + 1]
            r, g, b = rgb565_to_rgb(lo | (hi << 8))
            out[i * 4], out[i * 4 + 1], out[i * 4 + 2] = r, g, b
            out[i * 4 + 3] = alpha[i]
    return bytes(out)


def main():
    manifest = json.loads((ART / "fb_art_manifest.json").read_text(encoding="utf-8"))["assets"]
    bad = 0
    for name, m in manifest.items():
        w, h, kind = m["w"], m["h"], m["kind"]
        if kind == "scene":
            raw = (ART / f"{name}.rgb565").read_bytes()
            dec = Image.frombytes("RGB", (w, h), decode_scene(raw, w, h))
            ref = Image.open(ART / f"{name}.png").convert("RGB")
        else:
            raw = (ART / f"{name}.rgb565a8").read_bytes()
            dec = Image.frombytes("RGBA", (w, h), decode_icon(raw, w, h))
            ref = Image.open(ART / f"{name}.png").convert("RGBA")

        if kind == "scene":
            diff = sum(1 for a, b in zip(dec.tobytes(), quantize_rgb(ref)) if a != b)
        else:
            # 图标：alpha>0 的像素按量化后的 RGB 比，alpha 平面逐位比。
            # 全透明处的 RGB 在 565 往返中不保证保真，且不参与合成。
            refq = quantize_rgb(ref)
            rp = ref.tobytes()
            dp = dec.tobytes()
            diff = 0
            for i in range(w * h):
                if rp[i * 4 + 3] == 0:
                    if dp[i * 4 + 3] != 0:
                        diff += 1
                    continue
                if dp[i * 4 + 3] != rp[i * 4 + 3]:
                    diff += 1
                    continue
                if dp[i * 4:i * 4 + 3] != refq[i * 3:i * 3 + 3]:
                    diff += 1
        status = "OK " if diff == 0 else "BAD"
        if diff:
            bad += 1
        print(f"  {status} {name:16s} {w}x{h} {kind:5s} bytes={len(raw):>6} diff={diff}")
    if bad:
        print(f"\n格式门禁 FAIL：{bad} 项解码不一致")
        return 1
    print(f"\n格式门禁 PASS：{len(manifest)} 项均可按 LVGL 语义无损还原")
    return 0


if __name__ == "__main__":
    sys.exit(main())
