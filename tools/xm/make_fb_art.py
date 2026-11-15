#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""折叠北京 · 像素美术生成器。

产物走两条已被社区验证的路径（见 docs/reference/shinku-chen/
lossless-sprite-compositing-without-psram.zh_CN.md）：

  * 场景图（无透明）→ RGB565 裸数组，EMBED_FILES 进 Flash，LVGL 直读，
    不占 RAM、不按帧分配。整屏图必须走这条路：无 PSRAM 板子上
    240x320 RGB565 画布（153,600 B）申请不到，Flash 直读则零分配。
  * 图标（要压在任何底色上）→ RGB565A8（RGB565 平面 + A8 alpha 平面），
    3 字节/像素。纯色块拼图标会撞 box 容量上限，且做不出边缘。

风格约束（对齐官方 ui_pixel 与社区已上架玩法）：
  高饱和、硬边缘、无抗锯齿；硬阴影偏移而非柔和投影。
  折叠北京用「三层城市」母题：上层霓虹紫、中层钠灯橙、底层水泥青。

用法：
  python3 tools/xm/make_fb_art.py            # 全量生成
  python3 tools/xm/make_fb_art.py --check    # 只校验已有产物
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[2]
ART = REPO / "assets" / "art"
GEN = REPO / "main" / "generated"

BANNER_W, BANNER_H = 240, 104
FULL_W, FULL_H = 240, 320
ICON = 16

# ---------------------------------------------------------------- 调色板
# 三层城市的色阶：上层越冷越亮，底层越暗越脏。
C = {
    "void":     (0x07, 0x06, 0x0C),   # 最深的缝
    "sky0":     (0x0C, 0x0A, 0x18),   # 夜空底
    "sky1":     (0x1A, 0x10, 0x30),   # 夜空顶（紫）
    "sky2":     (0x2E, 0x1B, 0x4A),   # 高处更紫
    "far":      (0x17, 0x0F, 0x28),   # 远层楼
    "mid":      (0x21, 0x15, 0x36),   # 中层楼
    "near":     (0x2A, 0x1D, 0x40),   # 近层楼
    "concrete": (0x1C, 0x25, 0x30),   # 水泥
    "concrete2": (0x2C, 0x3A, 0x4A),
    "concrete3": (0x3F, 0x4E, 0x60),
    "ground":   (0x0E, 0x13, 0x19),
    "road":     (0x16, 0x1C, 0x26),
    "neon_p":   (0xFF, 0x4D, 0x6D),   # 霓虹粉
    "neon_c":   (0x2E, 0xE6, 0xC8),   # 霓虹青
    "neon_v":   (0x9B, 0x6B, 0xFF),   # 霓虹紫
    "warm":     (0xFF, 0xB0, 0x3A),   # 钠灯橙
    "warm_hi":  (0xFF, 0xD9, 0x8A),   # 灯泡黄
    "brick":    (0x7A, 0x52, 0x30),
    "tape":     (0xC9, 0xA4, 0x6A),
    "white":    (0xF2, 0xF0, 0xE8),
    "grey":     (0x8A, 0x93, 0xA3),
    "red":      (0xE2, 0x3B, 0x3B),
    "green":    (0x3F, 0xBF, 0x7F),
    "skin":     (0xE0, 0xA8, 0x78),
}


def mix(a, b, t):
    """线性插值两个颜色，t 为 0..1。用于渐层与光晕。"""
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def rrect(d, x, y, w, h, c):
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=c)


def vgrad(d, x, y, w, h, top, bottom):
    """竖直渐变。565 只有 32/64 级，逐行插值足够，不做抖动以免小屏显噪。"""
    for i in range(h):
        d.rectangle([x, y + i, x + w - 1, y + i], fill=mix(top, bottom, i / max(1, h - 1)))


def glow(d, cx, cy, r, c, base, strength=0.55):
    """硬边缘同心光晕。避开 Radiance 类柔和投影，保持像素感。"""
    for i in range(r, 0, -1):
        t = (1 - i / r) * strength
        d.ellipse([cx - i, cy - i, cx + i, cy + i], fill=mix(base, c, t))


def windows(d, x, y, w, h, base, lit_ratio, seed, color=None):
    """在楼体上点窗。用确定性 LCG，保证每次生成一致（资源 sha 才稳定）。"""
    s = seed
    color = color or C["warm_hi"]
    for yy in range(y + 3, y + h - 2, 5):
        for xx in range(x + 3, x + w - 3, 5):
            s = (s * 1103515245 + 12345) & 0x7FFFFFFF
            if (s >> 16) % 1000 < lit_ratio * 1000:
                cc = color if ((s >> 8) % 10) > 2 else C["warm"]
                rrect(d, xx, yy, 2, 2, cc)


def outline_rect(d, x, y, w, h, fill, edge, t=1):
    """带描边的实心块。像素画的可读性几乎全靠描边，裸色块在 240px 宽下会糊成一片。"""
    rrect(d, x - t, y - t, w + 2 * t, h + 2 * t, edge)
    rrect(d, x, y, w, h, fill)


def city_layer(d, y0, h, base, lit, seed, gaps=()):
    """一层楼的剪影：不连续的天际线 + 窗。gaps 让楼体断开，做出错位折叠感。"""
    s = seed
    x = 0
    while x < BANNER_W:
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        bw = 14 + (s >> 14) % 26
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        bh = max(8, h - (s >> 16) % (h // 2 + 1))
        skip = any(g[0] <= x <= g[1] for g in gaps)
        if not skip:
            rrect(d, x, y0 + h - bh, bw, bh, base)
            windows(d, x, y0 + h - bh, bw, bh, base, lit, s)
        x += bw + 1


# ---------------------------------------------------------------- 场景
def scene_intro_room(d, w, h, S):
    """出租屋：一只行李箱、一扇亮着别人家灯的窗。

    只放两个主体，各自给足尺寸和描边 —— 之前版本把床、包、门都塞进来，
    结果每样都只有十几个像素，糊成一片灰。
    """
    rrect(d, 0, 0, w, h, (0x1A, 0x21, 0x30))
    fy = S(66)
    rrect(d, 0, fy, w, h - fy, C["ground"])
    rrect(d, 0, fy, w, S(2), mix(C["ground"], C["concrete3"], 0.45))

    # 窗：右侧大窗，占约四成宽，是画面唯一光源
    wx, wy, ww, wh = S(126), S(12), S(92), S(52)
    rrect(d, wx, wy, ww, wh, C["void"])
    # 窗外两栋楼 + 稀疏但醒目的大窗（4x5，不是 2x2 噪点）
    for bx, bh, lit in ((S(132), S(34), 3), (S(178), S(42), 2)):
        bw = S(26)
        by = wy + wh - bh
        rrect(d, bx, by, bw, bh, (0x15, 0x1B, 0x2E))
        s0 = bx
        for i in range(lit):
            rrect(d, bx + S(4) + (i % 2) * S(12), by + S(5) + (i // 2) * S(13), S(7), S(9),
                  C["warm_hi"])
    d.pieslice([S(196), wy + S(4), S(212), wy + S(20)], 0, 360, fill=C["warm_hi"])
    # 窗框：十字窗棂 + 4px 粗边
    rrect(d, wx, wy + wh // 2 - S(1), ww, S(3), (0x2C, 0x36, 0x46))
    rrect(d, wx + ww // 2 - S(1), wy, S(3), wh, (0x2C, 0x36, 0x46))
    # 窗框只能画四条边。之前画成实心矩形，把窗外的楼和灯全盖住了。
    _e = S(4)
    rrect(d, wx - _e, wy - _e, ww + 2 * _e, _e, (0x3F, 0x4E, 0x60))
    rrect(d, wx - _e, wy + wh, ww + 2 * _e, _e, (0x3F, 0x4E, 0x60))
    rrect(d, wx - _e, wy - _e, _e, wh + 2 * _e, (0x3F, 0x4E, 0x60))
    rrect(d, wx + ww, wy - _e, _e, wh + 2 * _e, (0x3F, 0x4E, 0x60))
    rrect(d, wx - S(5), wy - S(5), ww + S(10), S(5), (0x3F, 0x4E, 0x60))   # 窗台板

    # 行李箱：左侧主体，明确轮廓 + 提手 + 两条横带 + 轮
    lx, ly, lw, lh = S(20), S(30), S(62), S(56)
    outline_rect(d, lx, ly, lw, lh, (0x7A, 0x46, 0x40), (0x2A, 0x16, 0x18), S(2))
    for by in (ly + S(14), ly + S(34)):
        rrect(d, lx + S(2), by, lw - S(4), S(5), (0x9B, 0x5E, 0x52))
    rrect(d, lx + lw // 2 - S(7), ly - S(6), S(14), S(4), (0x2A, 0x16, 0x18))   # 提手
    rrect(d, lx + lw // 2 - S(5), ly - S(5), S(10), S(2), (0x7A, 0x46, 0x40))
    for wxr in (lx + S(8), lx + lw - S(14)):
        d.ellipse([wxr, ly + lh, wxr + S(8), ly + lh + S(8)], fill=(0x14, 0x18, 0x1F))
    # 窗光落在箱子侧面，把主体和地面分开
    rrect(d, lx + lw - S(6), ly + S(4), S(5), lh - S(8), (0x8E, 0x56, 0x4E))

    # 地上：一张散落的纸，暗示刚被裁
    rrect(d, S(96), S(88), S(26), S(11), (0xC8, 0xC6, 0xBA))
    rrect(d, S(100), S(91), S(16), S(1), (0x8A, 0x88, 0x80))
    rrect(d, S(100), S(94), S(12), S(1), (0x8A, 0x88, 0x80))


def scene_home_street(d, w, h, S):
    """街角：三层城市在头顶错位叠着，这是「折叠」的主视觉。"""
    vgrad(d, 0, 0, w, h, C["sky1"], C["sky0"])
    for i in range(24):
        s = (i * 1103515245 + 12345) & 0x7FFFFFFF
        d.point(((s >> 7) % w, (s >> 3) % S(24)), fill=C["grey"])
    # 三层：远→近，逐层更亮更密
    city_layer(d, S(16), S(22), C["far"], 0.30, 11, gaps=[(S(70), S(90))])
    rrect(d, 0, S(38), w, S(1), mix(C["neon_c"], C["void"], 0.55))   # 折叠缝
    city_layer(d, S(34), S(24), C["mid"], 0.42, 27)
    rrect(d, 0, S(58), w, S(1), mix(C["neon_p"], C["void"], 0.55))
    city_layer(d, S(54), S(28), C["near"], 0.55, 43)
    # 路灯
    lpx = S(30)
    rrect(d, lpx, S(40), S(3), S(48), C["concrete2"])
    rrect(d, lpx - S(2), S(38), S(10), S(3), C["concrete3"])
    glow(d, lpx + S(3), S(44), S(13), C["warm"], C["near"], 0.5)
    rrect(d, lpx + S(1), S(43), S(5), S(3), C["warm_hi"])
    # 右侧店铺招牌：竖排霓虹
    sx = S(196)
    rrect(d, sx, S(30), S(11), S(34), mix(C["neon_p"], C["void"], 0.25))
    for i in range(3):
        rrect(d, sx + S(3), S(34) + i * S(10), S(5), S(6), C["neon_p"])
    rrect(d, sx - S(2), S(28), S(15), S(2), C["concrete3"])
    # 地面与灯下反光
    gy = S(78)
    rrect(d, 0, gy, w, h - gy, C["ground"])
    rrect(d, 0, gy, w, S(1), C["concrete"])
    rrect(d, lpx - S(8), gy + S(2), S(22), h - gy - S(2), mix(C["ground"], C["warm"], 0.16))
    rrect(d, sx - S(6), gy + S(2), S(20), h - gy - S(2), mix(C["ground"], C["neon_p"], 0.14))


def scene_market_stall(d, w, h, S):
    """夜市摊：一辆小吃车、一串灯泡、两口锅。最该有烟火气的一页。"""
    vgrad(d, 0, 0, w, h, C["sky1"], C["sky0"])
    city_layer(d, S(10), S(24), C["far"], 0.26, 61)
    gy = S(76)
    rrect(d, 0, gy, w, h - gy, C["ground"])
    # 车体
    cx0, cw = S(56), S(128)
    rrect(d, cx0, S(50), cw, S(26), C["concrete2"])
    rrect(d, cx0, S(50), cw, S(3), C["concrete3"])
    rrect(d, cx0 + S(4), S(57), S(26), S(15), mix(C["concrete2"], C["void"], 0.35))  # 柜门
    # 台面与锅
    rrect(d, cx0 - S(3), S(46), cw + S(6), S(5), C["concrete3"])
    rrect(d, S(96), S(41), S(26), S(6), C["void"])          # 锅
    rrect(d, S(96), S(41), S(26), S(2), C["concrete3"])
    # 蒸汽：上升点阵，越高越稀
    for i in range(5):
        rrect(d, S(104) + (i % 3) * S(4), S(37) - i * S(3), S(3), S(2),
              mix(C["grey"], C["sky0"], i / 5))
    # 遮阳篷：红白条纹 + 波浪下沿（摊位最强识别点）
    ay = S(30)
    for i in range(0, cw + S(6), S(8)):
        col = C["neon_p"] if (i // S(8)) % 2 == 0 else C["white"]
        rrect(d, cx0 - S(3) + i, ay, min(S(8), cw + S(6) - i), S(13), col)
    for i in range(0, cw + S(6), S(8)):
        d.pieslice([cx0 - S(3) + i, ay + S(13) - S(4), cx0 - S(3) + i + S(8), ay + S(13) + S(4)],
                   0, 180, fill=C["neon_p"] if (i // S(8)) % 2 == 0 else C["white"])
    # 灯泡串 + 光晕
    rrect(d, S(20), S(24), S(200), S(1), C["concrete2"])
    for bx in (S(70), S(120), S(170)):
        glow(d, bx, S(28), S(8), C["warm"], C["sky1"], 0.5)
        rrect(d, bx - S(2), S(25), S(4), S(5), C["warm_hi"])
    # 轮子
    for wx in (cx0 + S(12), cx0 + cw - S(12)):
        d.ellipse([wx - S(6), gy - S(6), wx + S(6), gy + S(6)], fill=C["void"])
        d.ellipse([wx - S(2), gy - S(2), wx + S(2), gy + S(2)], fill=C["concrete3"])
    # 两侧等着的路人（剪影，暗示有生意可做）
    for px, ph in ((S(12), S(22)), (S(210), S(24))):
        rrect(d, px, gy - ph, S(10), ph, C["void"])
        d.ellipse([px, gy - ph - S(9), px + S(10), gy - ph + S(1)], fill=C["void"])
    # 摊位暖光落地
    rrect(d, cx0, gy + S(2), cw, h - gy - S(2), mix(C["ground"], C["warm"], 0.20))


def scene_delivery_road(d, w, h, S):
    """夜路：骑手放大到占画面近一半，路面用两条明确透视线收进远处。

    上一版两侧楼群撒满 2x2 窗点，整屏变成紫色噪点墙，骑手反而看不出来。
    """
    vgrad(d, 0, 0, w, S(54), mix(C["sky2"], C["void"], 0.5), C["sky0"])
    # 两侧只留纯剪影块，靠"越来越矮"表达纵深，不撒窗点
    for side, sx0, sw in ((-1, 0, S(72)), (1, S(168), S(72))):
        for i in range(3):
            bw = sw - i * S(12)
            bh = S(40) - i * S(6)
            bx = sx0 if side < 0 else S(240) - bw
            rrect(d, bx, S(54) - bh, bw, bh, mix(C["far"], C["void"], 0.25))
    # 远处只点 4 个亮点，做"还有人在路上"的暗示
    for lx, ly in ((S(112), S(20)), (S(120), S(14)), (S(126), S(22)), (S(132), S(16))):
        rrect(d, lx, ly, S(3), S(4), C["warm_hi"])
    # 路面
    d.polygon([(S(72), S(54)), (S(168), S(54)), (w, h), (0, h)], fill=C["road"])
    # 两侧路缘（白线，透视收紧）
    d.polygon([(S(72), S(54)), (S(76), S(54)), (S(52), h), (S(28), h)],
              fill=mix(C["grey"], C["road"], 0.35))
    d.polygon([(S(164), S(54)), (S(168), S(54)), (S(212), h), (S(188), h)],
              fill=mix(C["grey"], C["road"], 0.35))
    # 中央虚线三段，越远越短
    for i, (t, dh) in enumerate(((0.06, 6), (0.34, 9), (0.62, 13))):
        y = S(54) + t * (h - S(54))
        hw = S(2) + t * S(3)
        rrect(d, S(120) - int(hw) // 2, int(y), max(2, int(hw)), S(dh * 2), (0x9A, 0x92, 0x7E))

    # 骑手：从观众背后看，能认出来的只有四件事 —— 头盔圆、肩、背上的大箱子、两个尾灯。
    bx = S(120)
    # 车轮（半露）与车体
    rrect(d, bx - S(30), S(84), S(60), S(7), (0x14, 0x18, 0x1F))
    for wx in (bx - S(26), bx + S(14)):
        d.ellipse([wx, S(84), wx + S(12), S(96)], fill=(0x0A, 0x0D, 0x12))
    # 肩 + 躯干（上宽下窄，才是人的轮廓）
    d.polygon([(bx - S(24), S(62)), (bx + S(24), S(62)), (bx + S(18), S(86)), (bx - S(18), S(86))],
              fill=(0x2C, 0x3A, 0x4A))
    # 外卖箱：占躯干大半，是"外卖员"的唯一强符号
    outline_rect(d, bx - S(22), S(44), S(44), S(36), (0x3F, 0x4E, 0x60), C["void"], S(1))
    rrect(d, bx - S(22), S(52), S(44), S(5), C["neon_p"])
    rrect(d, bx - S(16), S(62), S(32), S(12), mix((0x3F, 0x4E, 0x60), C["void"], 0.3))
    # 头盔：压在箱子上面，一个干净的圆
    d.ellipse([bx - S(13), S(24), bx + S(13), S(50)], fill=(0x22, 0x2C, 0x3A))
    d.pieslice([bx - S(13), S(22), bx + S(13), S(46)], 180, 360, fill=(0x3E, 0x4E, 0x62))
    rrect(d, bx - S(13), S(40), S(26), S(3), (0x14, 0x18, 0x1F))
    # 尾灯：全片唯一高饱和红，带光晕
    for tx in (bx - S(26), bx + S(20)):
        glow(d, tx + S(3), S(82), S(8), C["red"], C["road"], 0.55)
        rrect(d, tx, S(79), S(6), S(5), C["red"])
    # 车头灯照向观众的方向（很淡，别做成一块棕色）
    d.polygon([(bx - S(10), S(88)), (bx + S(10), S(88)), (bx + S(30), h), (bx - S(30), h)],
              fill=mix(C["road"], C["warm"], 0.10))


def scene_gig_warehouse(d, w, h, S):
    """仓库日结：货架、纸箱、一盏吊灯。体力活的压迫感。"""
    rrect(d, 0, 0, w, h, C["concrete"])
    for y in range(0, S(80), 12):
        d.line([(0, y), (w, y)], fill=mix(C["concrete"], C["void"], 0.3))
    gy = S(80)
    rrect(d, 0, gy, w, h - gy, C["ground"])
    # 吊灯 + 光锥
    rrect(d, S(118), 0, S(2), S(10), C["concrete3"])
    d.polygon([(S(110), S(10)), (S(128), S(10)), (S(126), S(17)), (S(112), S(17))], fill=C["concrete3"])
    d.polygon([(S(112), S(17)), (S(126), S(17)), (S(160), gy), (S(78), gy)],
              fill=mix(C["concrete"], C["warm"], 0.13))
    rrect(d, S(116), S(16), S(6), S(2), C["warm_hi"])
    # 货架
    for px in (S(16), S(104), S(192)):
        rrect(d, px, S(20), S(5), S(60), C["concrete2"])
    for by in (S(28), S(50), S(72)):
        rrect(d, S(14), by, S(216), S(4), C["concrete3"])
    # 纸箱（错落堆，不能整齐 —— 整齐就没有打工感）
    boxes = [(20, 34, 22, 15), (46, 38, 18, 11), (108, 30, 24, 17), (136, 34, 20, 13),
             (196, 40, 22, 9), (24, 56, 26, 15), (112, 55, 30, 16), (150, 58, 18, 12),
             (198, 60, 20, 11), (60, 66, 22, 13), (120, 74, 24, 15)]
    for bx, by, bw, bh in boxes:
        rrect(d, S(bx), S(by), S(bw), S(bh), C["brick"])
        rrect(d, S(bx), S(by), S(bw), S(2), mix(C["brick"], C["white"], 0.2))
        rrect(d, S(bx + bw // 2 - 2), S(by), S(4), S(bh), C["tape"])
    # 弯腰搬箱的人影：头 + 前倾的躯干 + 抱着箱子的手，三个块就够了
    d.ellipse([S(158), S(46), S(174), S(62)], fill=(0x10, 0x14, 0x1B))
    d.polygon([(S(158), S(60)), (S(176), S(60)), (S(180), S(80)), (S(154), S(80))],
              fill=(0x10, 0x14, 0x1B))
    rrect(d, S(150), S(62), S(34), S(18), C["brick"])
    rrect(d, S(150), S(62), S(34), S(2), mix(C["brick"], C["white"], 0.25))


def scene_ride_car(d, w, h, S):
    """开车：整块挡风玻璃 + 下方露出方向盘上半环。

    上一版把方向盘圆心放在画面之外，只剩一道几乎看不见的弧。
    这里把玻璃压到画面上 2/3，方向盘圆心贴底边，保证半环完整入画。
    """
    rrect(d, 0, 0, w, h, C["void"])
    gh = S(62)
    # 挡风玻璃：夜空 + 左右近处楼剪影（按住不放，比撒窗点清楚）
    vgrad(d, 0, 0, w, gh, mix(C["sky2"], C["void"], 0.35), C["sky0"])
    # 两侧近楼压住画面，中间留出"路"，才有驾驶视野
    rrect(d, 0, 0, S(52), gh, (0x10, 0x14, 0x1F))
    rrect(d, S(188), 0, S(52), gh, (0x10, 0x14, 0x1F))
    for bx, bw, bh in ((S(2), S(26), S(44)), (S(28), S(18), S(34)),
                       (S(190), S(24), S(48)), (S(214), S(24), S(30))):
        rrect(d, bx, gh - bh, bw, bh, (0x0A, 0x0D, 0x16))
        for i in range(3):
            rrect(d, bx + S(4) + (i % 2) * S(9), gh - bh + S(5) + (i // 2) * S(13), S(5), S(7),
                  C["warm_hi"])
    # 远处路灯排，把"路"的纵深做出来
    for i in range(4):
        lx = S(60) + i * S(34)
        rrect(d, lx, S(28) - i * 2, S(2), gh - S(28) + i * 2, (0x1C, 0x25, 0x30))
        rrect(d, lx - S(2), S(26) - i * 2, S(7), S(2), C["warm"])
    # 对面车灯：两对，带光晕，构成"对面来车"的直觉
    for hx in (S(78), S(158)):
        glow(d, hx, S(42), S(10), C["white"], C["sky0"], 0.5)
        rrect(d, hx - S(4), S(39), S(9), S(5), C["white"])
    # 雨刷（静止，一根，压在玻璃上）
    d.line([(S(14), gh - S(4)), (S(104), S(8))], fill=(0x0A, 0x0D, 0x12), width=max(2, S(3)))
    # 玻璃边框
    rrect(d, 0, gh - S(2), w, S(3), (0x2C, 0x36, 0x46))
    # 仪表台
    rrect(d, 0, gh, w, h - gh, (0x14, 0x18, 0x1F))
    # 方向盘：圆心贴底，半环完整可见
    dcx, dcy, dr = w // 2, h + S(4), S(54)
    d.arc([dcx - dr, dcy - dr, dcx + dr, dcy + dr], 185, 355,
          fill=(0x08, 0x0A, 0x0E), width=max(4, S(9)))
    d.arc([dcx - dr + S(12), dcy - dr + S(12), dcx + dr - S(12), dcy + dr - S(12)],
          200, 340, fill=(0x08, 0x0A, 0x0E), width=max(3, S(5)))
    # 两个发光仪表：青=速度、橙=油量，替代看不懂的细线
    rrect(d, S(18), h - S(12), S(30), S(8), mix(C["neon_c"], C["void"], 0.45))
    rrect(d, S(192), h - S(12), S(30), S(8), mix(C["warm"], C["void"], 0.45))
    # 后视镜
    rrect(d, dcx - S(18), 0, S(36), S(8), (0x2C, 0x36, 0x46))
    rrect(d, dcx - S(15), S(2), S(30), S(4), (0x0C, 0x10, 0x1A))


def scene_live_room(d, w, h, S):
    """直播间：环形灯放到最大，主播半身在右侧。"""
    rrect(d, 0, 0, w, h, (0x1E, 0x18, 0x30))
    # 顶部串灯（6 个点，稀疏才有氛围，密了像噪点）
    for i in range(6):
        bx = S(16) + i * S(40)
        d.line([(bx, 0), (bx, S(7))], fill=(0x3F, 0x4E, 0x60))
        rrect(d, bx - S(2), S(7), S(4), S(5), C["warm_hi"])
    # 环形灯：大环，白环粗 6，内部微亮
    rcx, rcy, rr = S(74), S(52), S(32)
    glow(d, rcx, rcy, rr + S(12), C["white"], (0x1E, 0x18, 0x30), 0.26)
    d.ellipse([rcx - rr, rcy - rr, rcx + rr, rcy + rr],
              outline=C["white"], width=max(3, S(6)))
    d.ellipse([rcx - rr + S(6), rcy - rr + S(6), rcx + rr - S(6), rcy + rr - S(6)],
              fill=mix((0x1E, 0x18, 0x30), C["white"], 0.12))
    # 手机：竖在环中心，屏幕亮青，是"在直播"的唯一硬证据
    outline_rect(d, rcx - S(11), rcy - S(20), S(22), S(38), (0x0A, 0x0D, 0x14), C["void"], S(1))
    rrect(d, rcx - S(8), rcy - S(17), S(16), S(30), mix(C["neon_c"], (0x0A, 0x0D, 0x14), 0.5))
    rrect(d, rcx - S(8), rcy - S(17), S(16), S(3), C["neon_c"])
    # 支架
    rrect(d, rcx - S(2), rcy + rr, S(4), h - rcy - rr, (0x2C, 0x36, 0x46))
    # 主播：右侧半身，头 + 肩，描边压住背景
    hx = S(150)
    # 头：先脸，再只盖上半的头发。之前 pieslice 直接把整张脸涂黑成"碗"。
    d.ellipse([hx + S(8), S(24), hx + S(40), S(56)], fill=C["skin"])
    d.pieslice([hx + S(6), S(21), hx + S(42), S(39)], 180, 360, fill=(0x1A, 0x14, 0x22))
    # 眼睛：两颗 3px 深点，是"这是个人"的最小信息量
    rrect(d, hx + S(15), S(42), S(4), S(3), (0x14, 0x10, 0x18))
    rrect(d, hx + S(29), S(42), S(4), S(3), (0x14, 0x10, 0x18))
    rrect(d, hx + S(21), S(49), S(8), S(2), (0xC0, 0x70, 0x60))
    d.polygon([(hx - S(14), h), (hx + S(2), S(56)), (hx + S(46), S(56)), (hx + S(62), h)],
              fill=(0x2A, 0x34, 0x48))
    rrect(d, hx + S(2), S(56), S(44), S(4), C["neon_p"])   # 衣领
    # 地面暖光
    rrect(d, 0, h - S(10), w, S(10), mix((0x1E, 0x18, 0x30), C["neon_c"], 0.10))


def scene_report_night(d, w, h, S):
    """收工：三层城市加月亮。一天结束的空镜。"""
    vgrad(d, 0, 0, w, h, C["sky2"], C["sky0"])
    for i in range(30):
        s = (i * 2654435761 + 7) & 0x7FFFFFFF
        d.point(((s >> 5) % w, (s >> 13) % S(30)), fill=C["white"])
    # 月亮 + 环
    mx, my = S(190), S(22)
    glow(d, mx, my, S(16), C["white"], C["sky2"], 0.28)
    d.ellipse([mx - S(9), my - S(9), mx + S(9), my + S(9)], fill=C["warm_hi"])
    d.ellipse([mx - S(5), my - S(6), mx + S(1), my + S(0)], fill=mix(C["warm_hi"], C["grey"], 0.25))
    city_layer(d, S(40), S(22), C["far"], 0.28, 101, gaps=[(S(120), S(132))])
    rrect(d, 0, S(60), w, S(1), mix(C["neon_c"], C["void"], 0.6))
    city_layer(d, S(56), S(26), C["mid"], 0.40, 113)
    gy = S(80)
    rrect(d, 0, gy, w, h - gy, C["ground"])
    # 路面倒影：稀疏三条长光带。之前按 12px 密铺像栅栏。
    for bx, bw, col in ((S(34), S(52), C["warm"]), (S(114), S(30), C["neon_c"]),
                        (S(176), S(42), C["neon_p"])):
        rrect(d, bx, gy + S(3), bw, S(2), mix(C["ground"], col, 0.40))
        rrect(d, bx + S(8), gy + S(7), bw - S(16), S(1), mix(C["ground"], col, 0.26))


def scene_event_rain(d, w, h, S):
    """随机事件：雨夜撑伞的人。不确定感。"""
    vgrad(d, 0, 0, w, h, mix(C["sky2"], C["void"], 0.55), C["void"])
    city_layer(d, S(12), S(26), C["far"], 0.18, 131)
    # 雨丝
    for i in range(90):
        s = (i * 1103515245 + 12345) & 0x7FFFFFFF
        x = (s >> 7) % w
        y = (s >> 3) % h
        d.line([(x, y), (x - S(2), y + S(6))], fill=mix(C["grey"], C["sky0"], 0.55))
    gy = S(78)
    rrect(d, 0, gy, w, h - gy, C["ground"])
    # 伞 + 人
    pcx = S(120)
    d.pieslice([pcx - S(34), S(26), pcx + S(34), S(74)], 180, 360, fill=C["neon_p"])
    for i in range(0, S(68), S(9)):
        d.line([(pcx - S(34) + i, S(50)), (pcx, S(28))], fill=mix(C["neon_p"], C["void"], 0.3))
    rrect(d, pcx - S(1), S(48), S(3), S(16), C["concrete3"])
    rrect(d, pcx - S(11), S(62), S(22), S(26), (0x1C, 0x25, 0x30))
    rrect(d, pcx - S(11), S(62), S(4), S(26), (0x2C, 0x3A, 0x4A))
    d.ellipse([pcx - S(8), S(50), pcx + S(8), S(66)], fill=(0x24, 0x2E, 0x3C))
    rrect(d, pcx - S(6), S(70), S(4), S(16), (0x14, 0x18, 0x1F))
    rrect(d, pcx + S(2), S(70), S(4), S(16), (0x14, 0x18, 0x1F))
    # 水洼
    rrect(d, pcx - S(30), gy + S(12), S(60), S(4), mix(C["ground"], C["neon_p"], 0.20))


def scene_ending_dawn(d, w, h, S):
    """结局：地平线亮起来。全片唯一暖色主导的画面。"""
    horizon = S(96)
    vgrad(d, 0, 0, w, horizon, C["sky2"], C["warm"])
    vgrad(d, 0, horizon - S(26), w, S(26), mix(C["sky2"], C["warm"], 0.55), C["warm_hi"])
    # 太阳
    sunx, suny = S(120), horizon
    glow(d, sunx, suny, S(46), C["warm_hi"], C["warm"], 0.42)
    d.pieslice([sunx - S(27), suny - S(27), sunx + S(27), suny + S(27)], 180, 360, fill=C["warm_hi"])
    # 城市剪影压在光里
    for bx, bw, bh in ((0, 26, 14), (26, 18, 22), (44, 30, 10), (74, 22, 18),
                       (96, 34, 8), (130, 20, 20), (150, 28, 12), (178, 22, 24),
                       (200, 40, 9)):
        rrect(d, S(bx), horizon - S(bh), S(bw), S(bh), C["void"])
        for i in range(2):
            if bh > 14:
                rrect(d, S(bx + 4) + i * S(9), horizon - S(bh) + S(4), S(4), S(5),
                      mix(C["void"], C["warm"], 0.25))
    rrect(d, 0, horizon, w, h - horizon, mix(C["void"], C["warm"], 0.10))
    # 飞鸟
    for bx, by in ((S(60), S(34)), (S(78), S(28)), (S(168), S(40))):
        d.line([(bx, by), (bx + S(4), by - S(3))], fill=C["void"])
        d.line([(bx + S(4), by - S(3)), (bx + S(8), by)], fill=C["void"])


SCENES = {
    "intro_room": (scene_intro_room, BANNER_W, BANNER_H),
    "home_street": (scene_home_street, BANNER_W, BANNER_H),
    "market_stall": (scene_market_stall, BANNER_W, BANNER_H),
    "delivery_road": (scene_delivery_road, BANNER_W, BANNER_H),
    "gig_warehouse": (scene_gig_warehouse, BANNER_W, BANNER_H),
    "ride_car": (scene_ride_car, BANNER_W, BANNER_H),
    "live_room": (scene_live_room, BANNER_W, BANNER_H),
    "report_night": (scene_report_night, BANNER_W, BANNER_H),
    "event_rain": (scene_event_rain, BANNER_W, BANNER_H),
    "ending_dawn": (scene_ending_dawn, BANNER_W, BANNER_H),
}


# ---------------------------------------------------------------- 图标
# 用字符图定义 16x16：可读、可改、不会画歪。
#   . 透明   # 主色   + 高光   - 阴影
ICON_COLORS = {
    "cash": C["warm_hi"],
    "heart": C["red"],
    "bolt": C["warm"],
    "star": (0xB4, 0x8CFF, 0xFF) if False else C["warm_hi"],
    "cal": C["neon_c"],
    "work": C["warm"],
    "rest": C["neon_v"],
    "card": C["neon_c"],
    "box": C["brick"],
    "door": C["grey"],
    "info": C["grey"],
    "clock": C["white"],
}

# 图标全部用几何精确绘制。16px 下手写字符图很容易出现奇偶行错位，
# 形状一旦不正，16 个像素的错误就占满整张图。
def _star_points(cx, cy, ro, ri, n=5):
    pts = []
    for i in range(n * 2):
        r = ro if i % 2 == 0 else ri
        a = math.radians(-90 + i * 180 / n)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def build_icon(name):
    """返回 16x16 RGBA 图标。统一 1px 外边距，保证在任何底上都有呼吸。"""
    base = ICON_COLORS[name]
    hi = mix(base, (255, 255, 255), 0.42)
    lo = mix(base, (0, 0, 0), 0.55)
    img = Image.new("RGBA", (ICON, ICON), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    O = base + (255,)

    if name == "cash":
        # 古钱：外圆 + 方孔，是中国玩家 3 毫秒能认出的"钱"
        d.ellipse([1, 1, 14, 14], fill=lo + (255,))
        d.ellipse([2, 2, 13, 13], fill=O)
        d.ellipse([3, 3, 9, 9], fill=hi + (255,))
        d.rectangle([6, 6, 9, 9], fill=(0x12, 0x15, 0x1C, 255))
    elif name == "heart":
        d.ellipse([2, 2, 8, 8], fill=O)
        d.ellipse([7, 2, 13, 8], fill=O)
        d.polygon([(2, 5), (13, 5), (8, 14)], fill=O)
        d.ellipse([3, 3, 6, 5], fill=hi + (255,))
    elif name == "bolt":
        d.polygon([(9, 1), (3, 9), (7, 9), (6, 14), (13, 6), (9, 6)], fill=O)
        d.polygon([(9, 1), (6, 6), (9, 6)], fill=hi + (255,))
    elif name == "star":
        d.polygon(_star_points(8, 8.5, 7.2, 3.0), fill=O)
        d.polygon(_star_points(8, 8.5, 4.2, 1.6), fill=hi + (255,))
    elif name == "cal":
        # 日历：顶部深色条 + 两排实心格。格子必须 2x2 实心，1px 点会消失。
        d.rectangle([1, 3, 14, 14], fill=lo + (255,))
        d.rectangle([2, 4, 13, 13], fill=O)
        d.rectangle([2, 4, 13, 7], fill=lo + (255,))
        d.rectangle([4, 1, 5, 5], fill=hi + (255,))
        d.rectangle([10, 1, 11, 5], fill=hi + (255,))
        for yy in (9, 12):
            for xx in (4, 7, 10):
                d.rectangle([xx, yy, xx + 1, yy + 1], fill=(0x12, 0x15, 0x1C, 255))
    elif name == "work":
        # 工具箱：提手 + 箱体 + 锁扣。锤子在 16px 下会退化成 "T" 形路牌。
        d.rectangle([5, 1, 10, 2], fill=lo + (255,))
        d.rectangle([5, 3, 6, 4], fill=lo + (255,))
        d.rectangle([9, 3, 10, 4], fill=lo + (255,))
        d.rectangle([1, 5, 14, 14], fill=lo + (255,))
        d.rectangle([2, 6, 13, 13], fill=O)
        d.rectangle([2, 6, 13, 7], fill=hi + (255,))
        d.rectangle([7, 8, 8, 11], fill=(0x12, 0x15, 0x1C, 255))
    elif name == "rest":
        # 月牙：大圆减偏移圆，边缘干净
        moon = Image.new("L", (ICON, ICON), 0)
        md = ImageDraw.Draw(moon)
        md.ellipse([1, 1, 14, 14], fill=255)
        md.ellipse([5, 0, 17, 13], fill=0)
        img.paste(Image.new("RGBA", (ICON, ICON), O), (0, 0), moon)
    elif name == "card":
        # 银行卡：顶部深磁条 + 右下芯片。带中心圆的版本看起来像显示器。
        d.rectangle([0, 3, 15, 12], fill=lo + (255,))
        d.rectangle([1, 4, 14, 11], fill=O)
        d.rectangle([1, 5, 14, 7], fill=lo + (255,))
        d.rectangle([9, 9, 12, 10], fill=hi + (255,))
        d.rectangle([2, 9, 4, 10], fill=(0xFF, 0xFF, 0xFF, 255))
    elif name == "box":
        d.polygon([(2, 5), (8, 2), (13, 5)], fill=lo + (255,))
        d.rectangle([1, 5, 14, 14], fill=lo + (255,))
        d.rectangle([2, 6, 13, 13], fill=O)
        d.rectangle([2, 6, 13, 8], fill=hi + (255,))
        d.rectangle([3, 10, 5, 11], fill=lo + (255,))
    elif name == "door":
        d.rectangle([3, 1, 12, 14], fill=lo + (255,))
        d.rectangle([4, 2, 11, 13], fill=O)
        d.rectangle([4, 2, 11, 3], fill=hi + (255,))
        d.point((10, 8), fill=(0x12, 0x15, 0x1C, 255))
    elif name == "info":
        d.rectangle([6, 2, 9, 5], fill=O)      # i 的点（4px 才看得见）
        d.rectangle([6, 7, 9, 13], fill=O)     # i 的竖
        d.rectangle([5, 7, 10, 8], fill=hi + (255,))
    elif name == "clock":
        d.ellipse([1, 1, 14, 14], fill=lo + (255,))
        d.ellipse([2, 2, 13, 13], fill=O)
        d.ellipse([3, 3, 12, 12], fill=mix(base, (0, 0, 0), 0.35) + (255,))
        d.line([(8, 8), (8, 4)], fill=hi + (255,), width=2)
        d.line([(8, 8), (11, 9)], fill=hi + (255,), width=2)
    else:
        raise ValueError(name)
    return img


def rgb565_bytes(img):
    """LVGL LV_COLOR_FORMAT_RGB565：每像素 2 字节小端，行内连续无 padding。"""
    w, h = img.size
    out = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b = img.getpixel((x, y))[:3]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out += bytes((v & 0xFF, v >> 8))
    return bytes(out)


def rgb565a8_bytes(img):
    """LVGL LV_COLOR_FORMAT_RGB565A8：先 RGB565 平面（stride=w*2），再 A8 平面（stride=w）。"""
    w, h = img.size
    rgb = img.convert("RGB")
    plane = rgb565_bytes(rgb)
    alpha = bytes(img.getchannel("A").tobytes())
    return plane + alpha


def emit_scene(name, img):
    w, h = img.size
    raw = rgb565_bytes(img)
    (ART / f"{name}.rgb565").write_bytes(raw)
    img.save(ART / f"{name}.png")
    return {"kind": "scene", "w": w, "h": h, "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest()}


def emit_icon(name, img):
    raw = rgb565a8_bytes(img)
    (ART / f"{name}.rgb565a8").write_bytes(raw)
    img.save(ART / f"{name}.png")
    return {"kind": "icon", "w": ICON, "h": ICON, "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest()}


HEADER = """// 本文件由 tools/xm/make_fb_art.py 生成，请勿手改。
// 场景图为 RGB565（Flash 直读、零 RAM）；图标为 RGB565A8（RGB565 平面 + A8 平面）。
#pragma once

#include <stdint.h>
"""


def emit_header(manifest):
    lines = [HEADER]
    lines.append("/* 场景图：240x104 横幅（封面/结局另用 240x320 全屏）。 */\n")
    for name, m in manifest.items():
        if m["kind"] != "scene":
            continue
        up = name.upper()
        lines.append(f"#define FB_SCENE_{up}_W {m['w']}u")
        lines.append(f"#define FB_SCENE_{up}_H {m['h']}u")
        lines.append(f"#define FB_SCENE_{up}_BYTES {m['bytes']}u")
        lines.append(f"extern const uint16_t *const fb_scene_{name};\n")
    lines.append("/* 图标：16x16，RGB565 平面在前、A8 平面紧随其后。 */\n")
    lines.append("#define FB_ICON_SIZE 16u\n")
    for name, m in manifest.items():
        if m["kind"] != "icon":
            continue
        up = name.upper()
        lines.append(f"#define FB_ICON_{up}_W {m['w']}u")
        lines.append(f"#define FB_ICON_{up}_H {m['h']}u")
        lines.append(f"#define FB_ICON_{up}_BYTES {m['bytes']}u")
        lines.append(f"extern const uint8_t *const fb_icon_{name};\n")
    idx = ["/* 图标索引：视图模型只带 id，不搬指针。 */",
           "typedef enum {", "    FB_ICO_NONE = 0,"]
    for name in manifest:
        if manifest[name]["kind"] == "icon":
            idx.append(f"    FB_ICO_{name.upper()},")
    idx.extend(["    FB_ICO_COUNT", "} fb_icon_id_t;", ""])
    lines.append("\n".join(idx))
    lines.append("/* 嵌入长度自检：由 fb_art_data.c 实现，启动时调一次。 */")
    lines.append("#include <stdbool.h>")
    lines.append("bool fb_art_selftest(uint32_t *bad_count);\n")
    (GEN / "fb_art.h").write_text("\n".join(lines) + "\n", encoding="utf-8")


def emit_bind(manifest):
    """把 EMBED_FILES 生成的裸符号绑成有类型的指针，并提供长度自检。

    ESP-IDF 的 EMBED_FILES 只产出 _binary_<文件名>_start / _end 这类裸符号，
    文件名里的非字母数字会被替换成下划线。应用层不应该直接引用它们 ——
    这里做唯一一处绑定，顺便用 _end - _start 校验真实长度。
    """
    out = ["// 本文件由 tools/xm/make_fb_art.py 生成，请勿手改。",
           "// 符号绑定与长度自检：EMBED_FILES 只产出裸符号，这里做唯一一处类型化。",
           "#include <stdbool.h>",
           "",
           "#include \"fb_art.h\"",
           "",
           "#define BIND_SCENE(name)                                                  \\\n"
           "    extern const uint8_t _binary_##name##_rgb565_start[];                   \\\n"
           "    extern const uint8_t _binary_##name##_rgb565_end[];                     \\\n"
           "    const uint16_t *const fb_scene_##name =                                \\\n"
           "        (const uint16_t *)_binary_##name##_rgb565_start;",
           "#define BIND_ICON(name)                                                   \\\n"
           "    extern const uint8_t _binary_##name##_rgb565a8_start[];                 \\\n"
           "    extern const uint8_t _binary_##name##_rgb565a8_end[];                   \\\n"
           "    const uint8_t *const fb_icon_##name =                                  \\\n"
           "        _binary_##name##_rgb565a8_start;",
           ""]
    for name, m in manifest.items():
        out.append(f"BIND_{'SCENE' if m['kind'] == 'scene' else 'ICON'}({name})")
    out.append("")
    out.append("""/* 资源自检：字节数与清单不一致就说明嵌入的文件和生成器对不上。
 * 这类错误在设备上表现成花屏或黑块而不是报错，所以必须主动跑一次。
 * 用 uintptr_t 相减：跨对象指针相减在标准里是 UB，链接器层面才有意义。 */
bool fb_art_selftest(uint32_t *bad_count) {
    bool ok = true;
    if (bad_count) *bad_count = 0;
""")
    for name, m in manifest.items():
        if m["kind"] == "scene":
            sym = f"_binary_{name}_rgb565_end"
        else:
            sym = f"_binary_{name}_rgb565a8_end"
        start = f"_binary_{name}_{'rgb565' if m['kind'] == 'scene' else 'rgb565a8'}_start"
        macro = f"FB_{'SCENE' if m['kind'] == 'scene' else 'ICON'}_{name.upper()}_BYTES"
        out.append(f"    if ((uintptr_t){sym} - (uintptr_t){start} != (uintptr_t){macro}) {{")
        out.append(f"        ok = false;")
        out.append(f"        if (bad_count) ++(*bad_count);")
        out.append(f"    }}")
    out.append("    return ok;")
    out.append("}")
    (GEN / "fb_art_data.c").write_text("\n".join(out) + "\n", encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="只校验产物与清单是否一致")
    args = ap.parse_args()

    ART.mkdir(parents=True, exist_ok=True)
    GEN.mkdir(parents=True, exist_ok=True)

    manifest_path = ART / "fb_art_manifest.json"
    manifest = {}
    total = 0

    if not args.check:
        for name, (fn, w, h) in SCENES.items():
            img = Image.new("RGB", (w, h), C["sky0"])
            d = ImageDraw.Draw(img)
            # S() 让 240 宽的设计能在任意目标尺寸上等比缩放，便于以后出海报图
            fn(d, w, h, lambda v, sw=w: max(1, int(round(v * sw / BANNER_W))))
            manifest[name] = emit_scene(name, img)
            total += manifest[name]["bytes"]
        for name in ICON_COLORS:
            manifest[name] = emit_icon(name, build_icon(name))
            total += manifest[name]["bytes"]

        manifest_path.write_text(
            json.dumps({"generator": "tools/xm/make_fb_art.py",
                        "palette": "folded-beijing/three-tier",
                        "assets": manifest}, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
        emit_header(manifest)
        emit_bind(manifest)
    else:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))["assets"]
        bad = 0
        for name, m in manifest.items():
            ext = "rgb565a8" if m["kind"] == "icon" else "rgb565"
            p = ART / f"{name}.{ext}"
            if not p.exists():
                print(f"  缺失 {p.name}")
                bad += 1
                continue
            sha = hashlib.sha256(p.read_bytes()).hexdigest()
            if sha != m["sha256"]:
                print(f"  漂移 {p.name}")
                bad += 1
        if bad:
            print(f"校验失败：{bad} 项不一致")
            return 1
        print(f"校验通过：{len(manifest)} 项")
        return 0

    scenes = [k for k, v in manifest.items() if v["kind"] == "scene"]
    icons = [k for k, v in manifest.items() if v["kind"] == "icon"]
    print(f"美术生成完成：{len(scenes)} 张场景 + {len(icons)} 个图标")
    print(f"  合计 {total} 字节（{total / 1024:.1f} KB）")
    for name in scenes:
        m = manifest[name]
        print(f"  scene {name:16s} {m['w']}x{m['h']}  {m['bytes']:>6} B")
    print(f"  图标 共 {sum(manifest[i]['bytes'] for i in icons)} B")
    print(f"  清单 {manifest_path}")
    print(f"  头文件 {GEN / 'fb_art.h'}")
    print(f"  绑定   {GEN / 'fb_art_data.c'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
