#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""折叠北京 · 发布用 UI 拼图生成器

把 preview/*.png（240x320，由 fb_preview.py 从当前固件视图渲染）放大拼成
1152x1536 的 2x2 界面图，供社区发布使用。

为什么不用实机照片：手上没有拍摄条件，而这套图是固件视图直接渲染的，
与真机像素一致；图脚注必须写明「非实机拍摄」，不能冒充实拍。

用法:
    python3 tools/xm/fb_preview.py --dump /tmp/fb_dump.json --out preview
    python3 tools/xm/make_ui_sheet.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parents[2]
PREVIEW = REPO / "preview"
OUT = REPO / "assets" / "publish"
FONT = "/System/Library/Fonts/Hiragino Sans GB.ttc"

W, H = 1152, 1536
BG = (27, 27, 31)
INK = (232, 230, 226)
SUB = (170, 166, 160)
FOOT = (140, 136, 130)
ACCENT = (224, 122, 63)
CELL = (480, 640)

SHEETS = [
    {
        "name": "ui_sheet_1.png",
        "title": "折叠北京 · 游戏界面（一）",
        "cells": [
            ("02_home", "主面板：一天 24 小时的账"),
            ("03c_ride_due", "选行当：网约车按天租，界面直接标还剩几天"),
            ("07_gig", "日结零工：路边等活，当天结钱"),
            ("05_market_ready", "摆摊选点：不同地方不同行情"),
        ],
    },
    {
        "name": "ui_sheet_2.png",
        "title": "折叠北京 · 游戏界面（二）",
        "cells": [
            ("16_live", "直播：连播涨粉，粉丝到了才有商单"),
            ("15_finance", "借钱与存款，都有日息"),
            ("09_report", "收工结算：当天收支与身体代价"),
            ("10b_event3", "随机事件：每个选项都能选到"),
        ],
    },
]


def font(size):
    return ImageFont.truetype(FONT, size)


def build(sheet):
    canvas = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(canvas)

    d.text((66, 28), sheet["title"], font=font(30), fill=INK)
    d.rectangle([66, 74, 176, 77], fill=ACCENT)

    x0, gap_x = 66, 60
    y0, gap_y = 102, 74
    for i, (page, caption) in enumerate(sheet["cells"]):
        col, row = i % 2, i // 2
        x = x0 + col * (CELL[0] + gap_x)
        y = y0 + row * (CELL[1] + gap_y)

        src = Image.open(PREVIEW / f"{page}.png").convert("RGB")
        # 最近邻放大：游戏 UI 是硬边像素图，插值会把 1px 边框糊掉
        shot = src.resize(CELL, Image.NEAREST)
        canvas.paste(shot, (x, y))
        d.rectangle([x - 1, y - 1, x + CELL[0], y + CELL[1]], outline=(58, 58, 66))
        d.text((x, y + CELL[1] + 10), caption, font=font(21), fill=SUB)

    d.text((66, H - 46), "界面示意 · 由固件视图渲染，非实机拍摄", font=font(20), fill=FOOT)
    path = OUT / sheet["name"]
    canvas.save(path, optimize=True)
    print(f"{path.name}  {canvas.size[0]}x{canvas.size[1]}  {path.stat().st_size // 1024} KB")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for sheet in SHEETS:
        build(sheet)


if __name__ == "__main__":
    main()
