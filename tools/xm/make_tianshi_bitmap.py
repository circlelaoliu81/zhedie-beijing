#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_tianshi_bitmap.py —— 天师白描位图的唯一数据源生成器。

背景：早期用 135 条程序化直线段拼「天师像」，在 240×320 上只能是抽象简笔，
用户明确判为「太过于抽象十分难看」/「光头和尚」。线段池这条路已废弃。
现在改用一张传统白描母图，经固定裁切 / 对比归一化 / 三色量化，生成设备
分辨率位图；同一份像素同时导出给：
  · 固件   main/generated/xm_tianshi_img.c  （RGB565 常量，LVGL lv_image 直读 Flash）
  · 预览   assets/art/tianshi_*.png         （host gen_preview.py 贴到页面）
  · 门禁   assets/art/manifest.json         （尺寸 / 像素哈希）

输入：assets/art/tianshi_master.png —— 传统道教天师白描灰度母图（已裁切归档）
用法：python3 tools/xm/make_tianshi_bitmap.py
生成期依赖 numpy + Pillow（仅开发机需要）；固件与门禁都不依赖它们 ——
tests/test_xm_tianshi_bitmap.py 刻意只用标准库 + PIL，以适配 ESP-IDF 精简 Python。
"""
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "assets" / "art"
GEN = ROOT / "main" / "generated"
MASTER = ART / "tianshi_master.png"

# 墨阶两端：米纸底 ↔ 浓墨。设备是 RGB565，直接做连续插值保留白描的浓淡层次，
# 不硬阈值成三色 —— 二值化 + 膨胀会把长髯/衣纹糊成一整块黑（已验证很难看）。
PAPER = np.array((0xF2, 0xEB, 0xDC), dtype=np.float32)
INK = np.array((0x2B, 0x2A, 0x27), dtype=np.float32)

# 两张设备位图的定稿参数。crop 都是母图内的相对坐标（左上 x,y,右下 x,y），
# fit=cover 保持人物比例并按目标宽高比居中裁切，绝不拉伸变形。
# gamma >1 压深中间调，让细墨线在小屏上不发灰；不做形态学加粗。
SPECS = (
    # name,        w,   h,  crop(母图坐标),        gamma, pad_bottom
    ("tianshi_main", 200, 132, (0, 0, 904, 760), 1.75, 4),  # 免责页：半身像，冠/髯/袍/拂尘俱全
    ("tianshi_home", 160, 94, (90, 0, 814, 560), 1.75, 8),  # 首页：更聚焦头肩，底部留白不顶日期行
)


def to_rgb565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def build(master, w, h, crop, gamma, pad_bottom=0):
    """母图 → 设备分辨率连续墨阶位图（RGB numpy 数组）。

    关键：不做二值化、不做膨胀。细线降采样后本来就是灰阶，硬阈值会让
    长髯和衣纹挤成黑块；连续插值 + 轻锐化才能保留白描的笔触浓淡。
    """
    g = np.asarray(master.crop(crop).convert("L")).astype(np.float32)
    lo, hi = np.percentile(g, [1, 99])
    g = np.clip((g - lo) / (hi - lo) * 255.0, 0, 255).astype(np.uint8)
    gi = Image.fromarray(g)

    cw, ch = crop[2] - crop[0], crop[3] - crop[1]
    s = max(w / cw, h / ch)
    nw, nh = int(cw * s) + 1, int(ch * s) + 1
    gi = gi.resize((nw, nh), Image.LANCZOS)
    x = (nw - w) // 2
    y = (nh - h) // 2
    gi = gi.crop((x, y, x + w, y + h)).filter(
        ImageFilter.UnsharpMask(radius=0.6, percent=70, threshold=2)
    )
    a = np.asarray(gi).astype(np.float32) / 255.0
    # 母图背景不是纯白（米色纸 + 纸纹），归一化后亮度只有 0.9x 左右。若直接
    # 拿 a 当墨阶，纸底会被算成浅灰，贴到 F2EBDC 页面上就是一块明显的灰补丁
    # （首页预览已验证）。用四角小块的中位数代表真实纸面亮度重新白平衡，
    # 再把残留的极淡纸纹吸附到纯 PAPER，让位图与页面无缝衔接。
    edge = np.concatenate((a[:8, :8].ravel(), a[:8, -8:].ravel(),
                           a[-8:, :8].ravel(), a[-8:, -8:].ravel()))
    bg = float(np.median(edge))
    a = np.clip(a / max(bg, 1e-3), 0.0, 1.0)
    t = np.power(a, gamma)
    out = np.clip(INK + t[..., None] * (PAPER - INK), 0, 255)
    delta = np.abs(out - PAPER[None, None, :]).max(axis=2)
    out[delta <= 10.0] = PAPER
    # 底部留白：位图下缘紧贴「玄门」标题 / 今日干支行会显得顶得慌，
    # 留几行纯纸白让画面透气，同时不影响图像矩形与几何门禁。
    if pad_bottom > 0:
        out[h - pad_bottom:h, :, :] = PAPER
    return out.astype(np.uint8)


def main():
    if not MASTER.exists():
        raise SystemExit(f"缺少白描母图：{MASTER}")
    master = Image.open(MASTER).convert("RGB")
    GEN.mkdir(parents=True, exist_ok=True)
    ART.mkdir(parents=True, exist_ok=True)

    hpp = [
        "#pragma once",
        "/* 天师白描位图：LVGL lv_image 直读 Flash 的 RGB565 常量。",
        " * 像素源与 host 预览同源，由 make_tianshi_bitmap.py 保证一致。 */",
        "#include <stdint.h>",
        "",
    ]
    cpp = [
        "/* 由 tools/xm/make_tianshi_bitmap.py 生成，请勿手改。 */",
        '#include "xm_tianshi_img.h"',
        "",
    ]
    manifest = {"source": MASTER.name, "images": {}}

    for name, w, h, crop, gamma, pad_bottom in SPECS:
        arr = build(master, w, h, crop, gamma, pad_bottom)
        png = ART / f"{name}.png"
        Image.fromarray(arr).save(png, optimize=True)
        # 纯字节镜像：给跑在 ESP-IDF 精简 Python（无 numpy / 无 PIL）里的门禁
        # 做逐 uint16 比对用，避免门禁为了读 PNG 去引第三方依赖。
        raw565 = rgb565_bytes(arr)
        (ART / f"{name}.rgb565").write_bytes(raw565)
        raw = emit_c_string(name, arr)
        cpp.extend(raw.splitlines())
        cpp.append("")
        hpp.append(f"#define {name.upper()}_W {w}u")
        hpp.append(f"#define {name.upper()}_H {h}u")
        hpp.append(f"extern const uint16_t {name}_rgb565[{name.upper()}_W * {name.upper()}_H];")
        hpp.append("")
        manifest["images"][name] = {
            "w": w, "h": h,
            "crop": list(crop),
            "gamma": gamma,
            "pad_bottom": pad_bottom,
            "png_sha256": hashlib.sha256(png.read_bytes()).hexdigest(),
            "bin_sha256": hashlib.sha256(raw565).hexdigest(),
            "rgb565_sha256": hashlib.sha256(raw565).hexdigest(),
        }

    # 命名带 _data.c 后缀：validate.sh 的 `main/generated/xm_*_data.c` glob
    # 会自动把位图链进所有 host 契约测试，不需要逐行改编译命令。
    (GEN / "xm_tianshi_img.h").write_text("\n".join(hpp), encoding="utf-8")
    (GEN / "xm_tianshi_img_data.c").write_text("\n".join(cpp) + "\n", encoding="utf-8")
    (ART / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    for name, meta in manifest["images"].items():
        print(f"  {name}: {meta['w']}x{meta['h']}  rgb565={meta['rgb565_sha256'][:12]}…")
    print(f"生成完成：{GEN / 'xm_tianshi_img_data.c'}  {ART / 'manifest.json'}")


def rgb565_bytes(arr):
    flat = arr.reshape(-1, 3)
    out = bytearray()
    for px in flat:
        v = to_rgb565(tuple(int(x) for x in px))
        out.append(v & 0xFF)
        out.append(v >> 8)
    return bytes(out)


def emit_c_string(name, arr):
    """RGB565 常量数组；uint16 数组避免手写字节序，与 LVGL RGB565 内存布局一致。"""
    flat = arr.reshape(-1, 3)
    vals = [to_rgb565(tuple(int(v) for v in px)) for px in flat]
    lines = [f"const uint16_t {name}_rgb565[{name.upper()}_W * {name.upper()}_H] = {{"]
    for i in range(0, len(vals), 12):
        lines.append("    " + " ".join(f"0x{v:04X}," for v in vals[i:i + 12]))
    lines.append("};")
    return "\n".join(lines)


if __name__ == "__main__":
    main()
