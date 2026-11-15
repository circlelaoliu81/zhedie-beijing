#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""折叠北京 · 主机预览渲染器

把 fb_vm_dump 导出的 xm_view_t JSON 逐页渲染为 240x320 PNG，并拼一张总览图。
绘制语义对齐设备端 main/xm_ui.c：
  boxes 先画（fill=实心 / border=描边），lines 后画（在框之上）。
文本宽度按设备口径计算：CJK = size，ASCII = size/2+1，与 xm_layout/xm_vm 一致。

用法: python3 tools/xm/fb_preview.py --dump /tmp/fb_dump.json --out preview
"""
import argparse
import json
import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

W, H = 240, 320
FONT_PATH = "/System/Library/Fonts/Hiragino Sans GB.ttc"
ART = Path(__file__).resolve().parents[2] / "assets" / "art"
TITLES = {
    "01_intro": "封面", "02_home": "主面板", "03_lines": "选行当",
    "04_market": "摆摊进货", "05_market_ready": "备好货",
    "06_home_after_work": "干完一份回主面板", "07_gig": "日结零工",
    "08_home_multi": "同一天第二份工", "09_report": "收工结算",
    "10_event": "随机事件", "11_next_home": "次日",
    "12_home_exhausted": "熬夜到快垮", "13_about": "玩法说明",
    "14_ending": "结局",
    "15_finance": "财务存取", "16_live": "直播连播",
    "17_intro_resume": "有存档的封面",
}
_cache = {}


def font(size):
    if size not in _cache:
        _cache[size] = ImageFont.truetype(FONT_PATH, size)
    return _cache[size]


def rgb(v):
    return ((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)


def dev_width(text, size):
    return sum((size // 2 + 1) if ord(ch) < 0x80 else size for ch in text)


def draw_text(draw, x, y, w, text, size, color, align):
    """按设备口径压缩横向尺度后绘制，保证与真机字宽一致。"""
    if not text:
        return
    f = font(size)
    host_w = draw.textlength(text, font=f)
    want = dev_width(text, size)
    if host_w <= 0:
        return
    ratio = want / host_w
    if align == 1:
        x = x + (w - want) / 2
    elif align == 2:
        x = x + (w - want)
    if abs(ratio - 1.0) < 0.02:
        draw.text((x, y), text, font=f, fill=color)
        return
    # 横向缩放：先画到透明层再 resize
    pad = 4
    layer = Image.new("RGBA", (max(1, int(host_w) + pad * 2), size + pad * 2), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    ld.text((pad, pad), text, font=f, fill=color)
    layer = layer.resize((max(1, int(want) + pad * 2), size + pad * 2), Image.LANCZOS)
    draw._image.paste(layer, (int(x) - pad, int(y) - pad), layer)


def render(page, out_path):
    img = Image.new("RGB", (W, H), (0x12, 0x15, 0x1C))
    draw = ImageDraw.Draw(img)
    draw._image = img

    # 层级必须与固件 xm_ui.c 一致：image → icons → boxes → lines
    im = page.get("image") or {}
    if im.get("name"):
        src = ART / (im["name"] + ".png")
        if src.exists():
            art = Image.open(src).convert("RGB")
            if (art.width, art.height) != (im["w"], im["h"]):
                art = art.resize((im["w"], im["h"]), Image.NEAREST)
            img.paste(art, (im["x"], im["y"]))

    for ic in page.get("icons", []):
        if not ic.get("name"):
            continue
        src = ART / (ic["name"] + ".png")
        if not src.exists():
            continue
        sprite = Image.open(src).convert("RGBA")
        if (sprite.width, sprite.height) != (ic["w"], ic["h"]):
            sprite = sprite.resize((ic["w"], ic["h"]), Image.NEAREST)
        img.paste(sprite, (ic["x"], ic["y"]), sprite)

    for b in page.get("boxes", []):
        x, y, w, h = b["x"], b["y"], b["w"], b["h"]
        col = rgb(b["color"])
        if b["fill"]:
            draw.rectangle([x, y, x + w - 1, y + h - 1], fill=col)
        elif b["border"]:
            draw.rectangle([x, y, x + w - 1, y + h - 1], outline=col)
    for ln in page.get("lines", []):
        draw_text(draw, ln["x"], ln["y"], ln["w"], ln["text"],
                  ln["size"], rgb(ln["color"]), ln["align"])
    img.save(out_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default="/tmp/fb_dump.json")
    ap.add_argument("--out", default="preview")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    pages = [json.loads(l) for l in Path(args.dump).read_text(encoding="utf-8").splitlines() if l.strip()]
    files = []
    for p in pages:
        fp = out / (p["name"] + ".png")
        render(p, fp)
        files.append((p["name"], fp))
    # 总览拼图：每页加标题，5 列 2 行
    cols = 5
    gap, cap = 12, 22
    rows = (len(files) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (W + gap) + gap, rows * (H + cap + gap) + gap), (0x1A, 0x1E, 0x26))
    sd = ImageDraw.Draw(sheet)
    for i, (name, fp) in enumerate(files):
        r, c = divmod(i, cols)
        x = gap + c * (W + gap)
        y = gap + r * (H + cap + gap)
        sd.text((x + 2, y), TITLES.get(name, name), font=font(16), fill=(0xEDE6, 0xE6, 0xD8)[0:1] + (0xE6, 0xD8))
        sheet.paste(Image.open(fp), (x, y + cap))
    sheet_fp = out / "_all_pages.png"
    sheet.save(sheet_fp)
    print("渲染 %d 页 -> %s" % (len(files), out.resolve()))
    print("总览: %s (%dx%d)" % (sheet_fp, sheet.width, sheet.height))


if __name__ == "__main__":
    main()
