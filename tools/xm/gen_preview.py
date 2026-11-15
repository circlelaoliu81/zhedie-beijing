#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_preview.py —— 把 host harness（tools/xm/xm_vm_dump）导出的 xm_view_t JSON
逐页渲染为 240×320 PNG，并生成离线 index.html 预览画廊。

渲染严格复刻设备端 main/xm_ui.c + main/xm_widgets.c 的绘制语义：
  · 背景 XM_COLOR_PAPER = 0xF2EBDC
  · boxes：fill→实心矩形；border→1px 内描边（同 color 字段）
  · strokes：两点直线，width 像素，color
  · lines：标签框 (x, y, w, size+6)，宽度内水平对齐（0 左 /1 中 /2 右），
           文本按宽度换行后裁剪在 size+6 高度内（与 LVGL 固定高度标签一致）
颜色字段一律 0xRRGGBB。
用法：
  python3 tools/xm/gen_preview.py [--dump /tmp/xm_dump.json] [--out DIR] [--harness /tmp/xm_vm_dump]
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
ART_DIR = ROOT / "assets" / "art"
PAPER = (0xF2, 0xEB, 0xDC)
W, H = 240, 320
FONT_PATH = "/System/Library/Fonts/Hiragino Sans GB.ttc"

PAGE_TITLES = {
    "DISCLAIMER": "免责声明", "KEYGUIDE": "三键操作", "TIME": "授时", "HOME": "首页", "HL_A": "黄历·历法宜忌",
    "HL_B": "黄历·神煞冲煞", "HL_C": "黄历·十二时辰", "LY_CAST": "六爻·摇卦",
    "LY_RESULT": "六爻·卦象", "LY_HISTORY": "六爻·历史", "SETTINGS": "设置", "ABOUT": "关于",
}


def c(hexint):
    return ((hexint >> 16) & 0xFF, (hexint >> 8) & 0xFF, hexint & 0xFF)


_font_cache = {}


def font(size):
    if size not in _font_cache:
        _font_cache[size] = ImageFont.truetype(FONT_PATH, size)
    return _font_cache[size]


def device_width(text, size):
    """设备端真实字宽口径：与 xm_layout.c / xm_vm.c 完全一致。
    CJK 宽 = size，ASCII 宽 = size/2 + 1。
    宿主 Hiragino 的半角字宽约 0.67em、设备子集（思源）是 0.5em，
    直接用宿主度量会高估约 20%，让「逐字段定宽」的版式在预览里看着错位。"""
    w = 0
    for ch in text:
        w += (size // 2 + 1) if ord(ch) < 0x80 else size
    return w


def draw_device_text(draw, xy, text, size, color, anchor):
    """按设备口径绘制一行字。

    宿主 Hiragino 的半角字宽约 0.67em，设备子集（思源）是 0.5em；直接用宿主
    字体画，同一串数字会比真机宽出约 20%，逐字段定宽的版式在预览里就会错位。
    这里先按真实字号渲染，再横向压缩到设备口径的宽度 —— 字高保持与真机一致，
    字宽也对齐真机，于是「段与段严丝合缝」在预览里能直接看出来。
    """
    if not text:
        return
    f = font(size)
    want = device_width(text, size)
    host_w = draw.textlength(text, font=f)
    if host_w <= 0 or want <= 0:
        draw.text(xy, text, font=f, fill=color, anchor=anchor)
        return
    ratio = want / host_w
    if ratio >= 0.995:
        draw.text(xy, text, font=f, fill=color, anchor=anchor)
        return
    # 渲染到离屏图后横向缩放；锚点保持不变。
    pad = 4
    w = int(host_w) + pad * 2
    h = int(size * 1.6) + pad * 2
    tmp = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    td = ImageDraw.Draw(tmp)
    td.text((pad, pad), text, font=f, fill=color + (255,), anchor="la")
    tw = max(1, int(round(w * ratio)))
    tmp = tmp.resize((tw, h), Image.LANCZOS)
    ax, ay = xy
    # 锚点的垂直分量：mm 表示以文本框中线为基准，la 渲染后要上移半行高。
    if len(anchor) > 1 and anchor[1] == "m":
        ay -= size * 0.55
    if anchor[0] == "m":
        ax -= tw / 2.0
    elif anchor[0] == "r":
        ax -= tw
    # 纵向：la/m/r 都以文本顶部为基准，pad 内缩即可。
    ay -= pad
    draw._image.alpha_composite(tmp, (int(round(ax)), int(round(ay)))) \
        if draw._image.mode == "RGBA" else draw._image.paste(
            tmp, (int(round(ax)), int(round(ay))), tmp)


def wrap_text(draw, text, f, max_w):
    """按像素宽度换行。CJK 逐字断行，ASCII 尽量按空格断。"""
    if not text:
        return [""]
    lines, cur = [], ""
    for ch in text:
        trial = cur + ch
        if draw.textlength(trial, font=f) <= max_w or not cur:
            cur = trial
        else:
            lines.append(cur)
            cur = ch
    if cur:
        lines.append(cur)
    return lines


def render_page(page):
    img = Image.new("RGB", (W, H), PAPER)
    draw = ImageDraw.Draw(img)

    # 天师白描底图垫最下层，顺序与固件 xm_ui.c 一致。
    # 这里贴的 PNG 与固件 RGB565 常量同源（门禁逐像素校验），尺寸不符直接报错，
    # 绝不静默少画一块让人误以为「真机也这样」。
    spec = page.get("image")
    if spec:
        art_path = ART_DIR / (spec["name"] + ".png")
        if not art_path.exists():
            raise RuntimeError("白描位图缺失：%s" % art_path)
        art = Image.open(art_path).convert("RGB")
        if art.size != (int(spec["w"]), int(spec["h"])):
            raise RuntimeError("白描位图尺寸不符：%s %s != %s" %
                               (art_path.name, art.size, (spec["w"], spec["h"])))
        img.paste(art, (int(spec["x"]), int(spec["y"])))

    for box in page.get("boxes", []):
        x, y, w, h = box["x"], box["y"], box["w"], box["h"]
        col = c(box["color"])
        if w <= 0 or h <= 0:
            continue
        x2, y2 = x + w - 1, y + h - 1
        r = min(int(box.get("radius", 0)), min(w, h) // 2)

        def paint(**kw):
            if r:
                draw.rounded_rectangle([x, y, x2, y2], radius=r, **kw)
            else:
                draw.rectangle([x, y, x2, y2], **kw)

        if box.get("fill"):
            paint(fill=col)
        if box.get("border"):
            paint(outline=col, width=1)

    for s in page.get("strokes", []):
        draw.line([s["x1"], s["y1"], s["x2"], s["y2"]],
                  fill=c(s["color"]), width=max(1, int(s.get("width", 1))))

    # VM 保证每个 line 的文案在其宽度内单行可容（长段落已由 VM 拆为多行 line），
    # 故此处按原样单行绘制，不换行、不裁切——所见即设备上屏内容，任何溢出即可见。
    #
    # 唯一例外：宿主字体（Hiragino）的半角字宽约 0.67em，而设备子集（思源）是 0.5em，
    # 同一串数字在宿主上会宽出约 20%。对「整行居中」的文案无影响，但授时页那种
    # 逐字段定宽的小标签会因此溢出。这里对「文本宽 > 标签宽」的行按比例缩字号，
    # 保证预览不裁字；这只是预览侧的近似，设备侧位置与字号仍由 VM 决定。
    for ln in page.get("lines", []):
        text = ln.get("text") or ""
        size = int(ln["size"])
        f = font(size)
        box_h = size + 6
        x, w = ln["x"], ln["w"]
        cy = ln["y"] + box_h / 2.0
        align = ln.get("align", 0)
        if align == 1:
            anchor, px = "mm", x + w / 2.0
        elif align == 2:
            anchor, px = "rm", x + w
        else:
            anchor, px = "lm", x
        if text and w > 0:
            draw_device_text(draw, (px, cy), text, size, c(ln["color"]), anchor)
        else:
            draw.text((px, cy), text, font=f, fill=c(ln["color"]), anchor=anchor)

    if page.get("overlay"):
        # 与设备端 main/xm_status.c 完全一致：状态条画在顶栏状态区 (8,2) 176×18，
        # 与右侧电量 (184,4,48,16) 并排；内部标签 (10,4) 172×14、字号 12、居中。
        # 此状态下 VM 不再输出「玄门」字样（见 xm_vm_common）。
        ov = page["overlay"]
        of = font(12)
        draw.rectangle([8, 2, 8 + 176 - 1, 2 + 18 - 1], fill=(0x9E, 0x3B, 0x32))
        draw.text((10 + 172 / 2.0, 4 + 14 / 2.0), ov, font=of,
                  fill=(0xF2, 0xEB, 0xDC), anchor="mm")

    return img


def contact_sheet(images, names, cols=5, pad=12, label_h=26):
    cw, ch = W, H
    rows = (len(images) + cols - 1) // cols
    sheet_w = cols * cw + (cols + 1) * pad
    sheet_h = rows * (ch + label_h) + (rows + 1) * pad
    sheet = Image.new("RGB", (sheet_w, sheet_h), (0x1E, 0x1E, 0x1E))
    d = ImageDraw.Draw(sheet)
    lf = font(14)
    for idx, (im, nm) in enumerate(zip(images, names)):
        r, cc = divmod(idx, cols)
        x = pad + cc * (cw + pad)
        y = pad + r * (ch + label_h + pad)
        sheet.paste(im, (x, y))
        title = PAGE_TITLES.get(nm, nm)
        d.text((x + cw / 2.0, y + ch + label_h / 2.0),
               "%s  (%s)" % (title, nm), font=lf, fill=(0xE8, 0xE8, 0xE8), anchor="mm")
    return sheet


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default="/tmp/xm_dump.json")
    ap.add_argument("--harness", default="/tmp/xm_vm_dump")
    ap.add_argument("--out", default="/Volumes/Extreme SSD/老刘的APP/玄门-项目文档/03-玄门UI预览")
    args = ap.parse_args()

    dump = args.dump
    if not os.path.exists(dump) and os.path.exists(args.harness):
        data = subprocess.check_output([args.harness])
        open(dump, "wb").write(data)
    if not os.path.exists(dump):
        print("找不到 dump JSON：%s（先运行 %s）" % (dump, args.harness))
        sys.exit(1)

    pages = json.load(open(dump, encoding="utf-8"))
    os.makedirs(args.out, exist_ok=True)

    images, names = [], []
    for p in pages:
        img = render_page(p)
        nm = p["name"]
        # 同名页（HOME 错误态）加后缀避免覆盖
        fn = nm
        if fn in names:
            fn = "%s_%d" % (nm, sum(1 for n in names if n.startswith(nm)))
        path = os.path.join(args.out, "%02d_%s.png" % (p["page"], fn))
        img.save(path)
        images.append(img)
        names.append(nm)
        print("  -> %s  (%s)" % (path, p.get("overlay") or "ok"))

    sheet = contact_sheet(images, names)
    sheet_path = os.path.join(args.out, "00_总览_contact_sheet.png")
    sheet.save(sheet_path)
    print("  => 总览图：%s (%dx%d)" % (sheet_path, sheet.size[0], sheet.size[1]))

    # index.html 画廊
    cards = []
    for p, img in zip(pages, images):
        nm = p["name"]
        title = PAGE_TITLES.get(nm, nm)
        fname = "%02d_%s.png" % (p["page"], nm)
        # 匹配实际文件名
        for ff in os.listdir(args.out):
            if ff.startswith("%02d_%s" % (p["page"], nm)) and ff.endswith(".png"):
                fname = ff
                break
        cards.append(
            '<figure><img src="%s" alt="%s"><figcaption>%02d · %s<br><span>%s</span></figcaption></figure>'
            % (fname, title, p["page"], title, nm))
    html = """<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<title>玄门 · 界面预览（240×320）</title>
<style>
body{margin:0;background:#1e1e1e;color:#e8e8e8;font-family:-apple-system,"PingFang SC",sans-serif;padding:24px}
h1{font-size:20px;font-weight:600;margin:0 0 4px}
.sub{color:#8a8a8a;font-size:13px;margin-bottom:20px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:20px}
figure{margin:0;background:#2a2a2a;border-radius:10px;padding:12px;text-align:center}
img{width:240px;height:320px;image-rendering:pixelated;border-radius:4px;display:block;margin:0 auto}
figcaption{font-size:13px;margin-top:10px;line-height:1.5}
figcaption span{color:#8a8a8a;font-size:11px}
</style></head><body>
<h1>玄门 · 界面预览</h1>
<div class="sub">由 xm_view_t（纯 C VM 真实输出）渲染 · 240×320 · 与设备同源绘制语义</div>
<div class="grid">
%s
</div></body></html>""" % "\n".join(cards)
    open(os.path.join(args.out, "index.html"), "w", encoding="utf-8").write(html)
    print("  => 画廊：%s/index.html" % args.out)

    # 摇卦动画属于动态交付物：必须给出连续分帧图 + 可播放的 GIF，
    # 不能只在文字里说「已实现动画」。数据来自 xm_vm_dump 的真实 view 输出。
    shake = [(nm, im) for nm, im in zip(names, images) if nm.startswith("LY_CAST_SHAKE")]
    tail = [(nm, im) for nm, im in zip(names, images)
            if nm.startswith("LY_CAST_SETTLED") or nm.startswith("LY_CAST_READY")]
    seq = shake + tail
    if seq:
        pad, label_h = 10, 24
        sheet_w = len(seq) * W + (len(seq) + 1) * pad
        sheet = Image.new("RGB", (sheet_w, H + label_h + 2 * pad), PAPER)
        sd = ImageDraw.Draw(sheet)
        for k, (nm, im) in enumerate(seq):
            x = pad + k * (W + pad)
            sheet.paste(im, (x, pad))
            sd.text((x + W / 2.0, pad + H + label_h / 2.0),
                    nm.replace("LY_CAST_", ""), font=font(12),
                    fill=(0x2B, 0x2A, 0x27), anchor="mm")
        sheet_path = os.path.join(args.out, "摇卦动画_连续分帧.png")
        sheet.save(sheet_path)
        gif_path = os.path.join(args.out, "摇卦动画_逐帧.gif")
        gif_frames = [im.convert("RGB").quantize(colors=64) for _, im in seq]
        gif_frames[0].save(gif_path, save_all=True, append_images=gif_frames[1:],
                           duration=[160] * len(gif_frames), loop=0)
        print("  => 动画分帧：%s" % sheet_path)
        print("  => 动画 GIF：%s（%d 帧）" % (gif_path, len(gif_frames)))

    # 授时上下键属于交互结果：把「按上键前 / 后」两帧真实 view 并排摆出来，
    # 不能只在文字里说「数字会变」。两帧都由 xm_vm_dump 的 xm_pages_render 导出。
    before = [(nm, im) for nm, im in zip(names, images)
              if nm.startswith("TIME_KEY_BEFORE")]
    after = [(nm, im) for nm, im in zip(names, images)
             if nm.startswith("TIME_KEY_AFTER")]
    if before and after:
        pad, label_h = 12, 28
        pair = [before[0], after[0]]
        sheet = Image.new("RGB", (2 * W + 3 * pad, H + label_h + 2 * pad), PAPER)
        sd = ImageDraw.Draw(sheet)
        labels = ["按上键前 · 2026-11-13 10:30", "按上键后 · 2027-03-07 09:45"]
        for k, (nm, im) in enumerate(pair):
            x = pad + k * (W + pad)
            sheet.paste(im, (x, pad))
            sd.text((x + W / 2.0, pad + H + label_h / 2.0), labels[k],
                    font=font(12), fill=(0x2B, 0x2A, 0x27), anchor="mm")
        path = os.path.join(args.out, "授时上下键_前后对照.png")
        sheet.save(path)
        print("  => 授时对照：%s" % path)

    print("完成：共 %d 页。" % len(pages))


if __name__ == "__main__":
    main()
