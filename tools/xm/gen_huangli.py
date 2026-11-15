#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_huangli.py —— 构建期生成 XMHL v1 黄历查表（2026-01-01 ~ 2028-12-31，1096 天）。

输出：
  main/generated/xm_huangli_data.h   声明 blob + 固定查表（建除/宿/方/纳音/甲子）
  main/generated/xm_huangli_data.c   实现 blob（const uint8_t[]）+ 固定查表

二进制格式（严格匹配 main/core/xm_huangli.c 解析器）：
  [0..32)    header: magic"XMHL"|ver u16|day_count u16|base_days u32|str_pool_off u32|
                      str_pool_bytes u32|rec_off u32|rec_size u16|list_pool_off u16|crc32 u32
  [str_pool_off ...)  str_pool: u16 count | u16*count offsets | 以 NUL 结尾的字符串
  [list_pool_off ...) list_pool: u16 term_count | u16*term_count dict(str idx) | 各日列表字节流
  [rec_off ...)       records: 1096 × 64B（详见 xm_huangli.c xm_hl_lookup 读取布局）

布局约束（xm_hl_init 校验）：32 == str_pool_off < list_pool_off < rec_off <= size。
数据来源：lunar_python（与玄门布衣 lunar-java 同源历法库），完全离线、可复现。
"""
import os
import sys
import zlib
from datetime import date, datetime, timedelta

try:
    from lunar_python import Lunar  # 安装在移动硬盘 _toolchain/pylibs
except ImportError:
    sys.path.insert(0, "/Volumes/Extreme SSD/_toolchain/pylibs")
    from lunar_python import Lunar

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# ---- 固定查表（与 C 端 xm_huangli.c 解析后的取值一一对应）----
ZHIXING = ["建", "除", "满", "平", "定", "执", "破", "危", "成", "收", "开", "闭"]
XU = ["角", "亢", "氐", "房", "心", "尾", "箕", "斗", "牛", "女", "虚", "危", "室", "壁",
      "奎", "娄", "胃", "昴", "毕", "觜", "参", "井", "鬼", "柳", "星", "张", "翼", "轸"]
# 二十八宿吉凶（传统《禽星》口诀整理本）：1=吉 0=凶
XU_LUCK = [0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0]
NAYIN = ["海中金", "炉中火", "大林木", "路旁土", "剑锋金", "山头火", "涧下水", "城头土", "白蜡金",
         "杨柳木", "泉中水", "屋上土", "霹雳火", "松柏木", "长流水", "沙中金", "山下火", "平地木",
         "壁上土", "金箔金", "覆灯火", "天河水", "大驿土", "钗钏金", "桑柘木", "大溪水", "沙中土",
         "天上火", "石榴木", "大海水"]
JIAZI = ["甲子", "乙丑", "丙寅", "丁卯", "戊辰", "己巳", "庚午", "辛未", "壬申", "癸酉", "甲戌", "乙亥",
         "丙子", "丁丑", "戊寅", "己卯", "庚辰", "辛巳", "壬午", "癸未", "甲申", "乙酉", "丙戌", "丁亥",
         "戊子", "己丑", "庚寅", "辛卯", "壬辰", "癸巳", "甲午", "乙未", "丙申", "丁酉", "戊戌", "己亥",
         "庚子", "辛丑", "壬寅", "癸卯", "甲辰", "乙巳", "丙午", "丁未", "戊申", "己酉", "庚戌", "辛亥",
         "壬子", "癸丑", "甲寅", "乙卯", "丙辰", "丁巳", "戊午", "己未", "庚申", "辛酉", "壬戌", "癸亥"]
# 八方位（device 端 dir_table[8] 顺序；生成端与 C 端必须一致）
DIR8 = ["北", "东北", "东", "东南", "南", "西南", "西", "西北"]
GUA2DIR = {"坎": 0, "艮": 1, "震": 2, "巽": 3, "离": 4, "坤": 5, "兑": 6, "乾": 7}


def days_from_civil(y, m, d):
    """与 main/core/xm_time.c xm_days_from_civil 完全相同的 Hinnant 算法。"""
    yy = y - (1 if m <= 2 else 0)
    era = (yy if yy >= 0 else yy - 399) // 400
    yoe = yy - era * 400
    mp = m + (-3 if m > 2 else 9)
    doy = (153 * mp + 2) // 5 + d - 1
    doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
    return era * 146097 + doe - 719468


def enc_u16(v):
    return bytes([v & 0xFF, (v >> 8) & 0xFF])


def enc_u32(v):
    return bytes([v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF])


def build():
    start = date(2026, 1, 1)
    end = date(2028, 12, 31)
    base_days = days_from_civil(2026, 1, 1)
    assert base_days == 20454, "XM_HL_BASE_DAYS 常量与 days_from_civil 不一致: %d" % base_days

    day_count = (end - start).days + 1
    assert day_count == 1096, "天数应为 1096，实际 %d" % day_count

    # 主字符串池（字段串 + 术语串 去重后统一编号）
    strings = []          # 有序唯一字符串
    str_idx = {}          # 字符串 -> 池中编号

    def add_str(s):
        if s is None:
            return 0xFFFF
        s = str(s)
        if s not in str_idx:
            str_idx[s] = len(strings)
            strings.append(s)
        return str_idx[s]

    records = []          # 每日 64 字节 record（先占坑，list 字节流稍后回填）
    term_freq = {}       # 宜/忌/吉神/凶煞 术语词频（取前 254 进字典，其余转义）

    per_day_lists = []    # 每日的 4 个术语列表（list of list[str]）

    for n in range(day_count):
        d = start + timedelta(days=n)
        lun = Lunar.fromDate(datetime(d.year, d.month, d.day))
        month = lun.getMonth()
        leap = month < 0
        mc = lun.getMonthInChinese()
        lunar_m = (("闰" + mc) if leap else (mc + "月"))
        lunar_y = lun.getYearInChinese()
        lunar_d = lun.getDayInChinese()
        gz_y = lun.getYearInGanZhi()
        gz_m = lun.getMonthInGanZhi()
        gz_d = lun.getDayInGanZhi()
        tianshen = lun.getDayTianShen()
        zx = ZHIXING.index(lun.getZhiXing()) if lun.getZhiXing() in ZHIXING else 0
        xiu_name = lun.getXiu()
        xiu_idx = XU.index(xiu_name) if xiu_name in XU else 0
        xiu_luck = XU_LUCK[xiu_idx]
        xiu_raw = (xiu_luck << 7) | xiu_idx
        chong = str(lun.getDayChongDesc())
        sha_name = str(lun.getDaySha())
        if sha_name in DIR8:
            sha = DIR8.index(sha_name)
        elif sha_name in GUA2DIR:
            sha = GUA2DIR[sha_name]
        else:
            sha = 7
        pg = lun.getPengZuGan()
        pz = lun.getPengZuZhi()
        nayin = lun.getDayNaYin()
        nayin_idx = NAYIN.index(nayin) if nayin in NAYIN else 0

        def dir_of(name):
            if name in DIR8:
                return DIR8.index(name)
            if name in GUA2DIR:
                return GUA2DIR[name]
            return 7

        pos_xi = dir_of(str(lun.getDayPositionXi()))
        pos_fu = dir_of(str(lun.getDayPositionFu()))
        pos_cai = dir_of(str(lun.getDayPositionCai()))

        # 十二时辰：取前 12（lunar_python 实测返回 13 项，第 13 为次日子时，丢弃）
        times = lun.getTimes()[:12]
        assert len(times) == 12, "时辰应取 12，实际 %d" % len(times)
        hour_gz = []
        hour_luck = 0
        for i, t in enumerate(times):
            gz = t.getGanZhi()
            hour_gz.append(JIAZI.index(gz) if gz in JIAZI else 0)
            if str(t.getTianShenLuck()) == "吉":
                hour_luck |= (1 << i)

        # 宜/忌/吉神/凶煞（术语，累计词频；取前 254 进字典，其余 0xFF 转义）
        yi = list(lun.getDayYi())
        ji = list(lun.getDayJi())
        jishen = list(lun.getDayJiShen() if hasattr(lun, "getDayJiShen") else [])
        xiongsha = list(lun.getDayXiongSha() if hasattr(lun, "getDayXiongSha") else [])
        for lst in (yi, ji, jishen, xiongsha):
            for t in lst:
                ts = str(t)
                term_freq[ts] = term_freq.get(ts, 0) + 1

        per_day_lists.append((yi, ji, jishen, xiongsha))

        rec = bytearray(64)
        rec[0:2] = enc_u16(add_str(lunar_y))
        rec[2:4] = enc_u16(add_str(lunar_m))
        rec[4:6] = enc_u16(add_str(lunar_d))
        rec[6] = 1 if leap else 0
        rec[7:9] = enc_u16(add_str(gz_y))
        rec[9:11] = enc_u16(add_str(gz_m))
        rec[11:13] = enc_u16(add_str(gz_d))
        rec[25:27] = enc_u16(add_str(tianshen))
        rec[27] = zx
        rec[28] = xiu_raw
        rec[29:31] = enc_u16(add_str(chong))
        rec[31] = sha
        rec[32:34] = enc_u16(add_str(pg))
        rec[34:36] = enc_u16(add_str(pz))
        # 布局：pengzu_zhi 占 34..35（u16 字符串索引），其后单字节/时辰字段整体右移 1 字节，
        # 避免 pos_xi 与 pengzu_zhi 高字节在 35 处重叠（曾导致 877 天 lookup 失败）。
        rec[36] = pos_xi
        rec[37] = pos_fu
        rec[38] = pos_cai
        rec[39] = nayin_idx
        for i in range(12):
            rec[40 + i] = hour_gz[i]
        rec[52:54] = enc_u16(hour_luck & 0xFFFF)
        rec[54] = 0  # hour_idx_seed（未使用，保留）
        records.append(rec)

    # ---- 术语字典：按词频取前 254 进字典（1 字节 code），其余用 0xFF + 2 字节索引转义 ----
    sorted_terms = sorted(term_freq.items(), key=lambda kv: (-kv[1], kv[0]))
    dict_terms = [t for t, _ in sorted_terms[:254]]
    term_code = {t: code for code, t in enumerate(dict_terms)}
    term_count = len(term_code)

    # 所有术语串（字典项 + 转义项）必须先在字符串池取得稳定编号，故此处入池
    for t in sorted(term_freq.keys()):
        add_str(t)

    # ---- 字符串池（字段串 + 术语串 全部编号后统一序列化）----
    count = len(strings)
    assert count < 0xFFFF, "字符串过多"
    pool_offsets = bytearray()
    pool_bytes = bytearray()
    pool_offsets += enc_u16(count)
    pos = 2 + 2 * count
    for s in strings:
        pool_offsets += enc_u16(pos)
        sb = s.encode("utf-8")
        pool_bytes += sb + b"\x00"
        pos += len(sb) + 1
    str_pool = pool_offsets + pool_bytes
    str_pool_size = len(str_pool)

    # ---- 列表字节流 ----
    list_pool = bytearray()
    list_pool += enc_u16(term_count)  # term_count（字典项数；解码端按 code<term_count 判定）
    for t in dict_terms:
        list_pool += enc_u16(add_str(t))  # 字典项 -> 字符串池编号（已入池，返回既有编号）

    list_offsets = []  # 每列表流的起始（相对 list_pool）
    for n in range(day_count):
        yi, ji, jishen, xiongsha = per_day_lists[n]
        for lst in (yi, ji, jishen, xiongsha):
            off = len(list_pool)
            list_offsets.append(off)
            for t in lst:
                t = str(t)
                if t in term_code:
                    list_pool.append(term_code[t])
                else:
                    list_pool.append(0xFF)
                    list_pool += enc_u16(add_str(t))
            # 回填该列表在 record 中的 list_off(u16) 与 count(u8)
            r = records[n]
            # 计算本列表在当日 record 中的字段位置
            list_no = (len(list_offsets) - 1) % 4  # 0=yi 1=ji 2=jishen 3=xiongsha
            field_base = [13, 16, 19, 22][list_no]
            r[field_base:field_base + 2] = enc_u16(off)
            r[field_base + 2] = len(lst)

    # ---- 守卫：record 各字段字节区间不得重叠（曾因 pengzu_zhi[34:36] 与 pos_xi[35] 重叠，
    #      导致 877 天 lookup 读到越界索引而整日失败）。此处断言防复发；55..63 为保留未用字节。 ----
    _LAYOUT = [
        ("lunar_y", 0, 2), ("lunar_m", 2, 4), ("lunar_d", 4, 6), ("lunar_leap", 6, 7),
        ("gz_y", 7, 9), ("gz_m", 9, 11), ("gz_d", 11, 13),
        ("yi_off", 13, 15), ("yi_count", 15, 16), ("ji_off", 16, 18), ("ji_count", 18, 19),
        ("jishen_off", 19, 21), ("jishen_count", 21, 22), ("xiongsha_off", 22, 24),
        ("xiongsha_count", 24, 25), ("tianshen", 25, 27), ("zhixing", 27, 28),
        ("xiu_raw", 28, 29), ("chong", 29, 31), ("sha", 31, 32),
        ("pengzu_gan", 32, 34), ("pengzu_zhi", 34, 36),
        ("pos_xi", 36, 37), ("pos_fu", 37, 38), ("pos_cai", 38, 39), ("nayin", 39, 40),
        ("hour_gz", 40, 52), ("hour_luck", 52, 54), ("hour_idx_seed", 54, 55),
    ]
    _owner = [None] * 64
    for _nm, _s, _e in _LAYOUT:
        assert 0 <= _s < _e <= 64, "字段 %s 区间非法 [%d,%d)" % (_nm, _s, _e)
        for _b in range(_s, _e):
            assert _owner[_b] is None, "字段 %s 与 %s 在字节 %d 重叠" % (_nm, _owner[_b], _b)
            _owner[_b] = _nm

    list_pool_size = len(list_pool)
    assert list_pool_size < 0xFFFF, "列表池过大，偏移溢出 u16"

    # ---- 记录区 ----
    rec_off = 32 + str_pool_size + list_pool_size
    rec_bytes = b"".join(bytes(r) for r in records)
    assert len(rec_bytes) == day_count * 64

    # ---- header ----
    header = bytearray()
    header += b"XMHL"
    header += enc_u16(1)                       # version
    header += enc_u16(day_count)               # day_count
    header += enc_u32(base_days)               # base_days
    header += enc_u32(32)                       # str_pool_off
    header += enc_u32(str_pool_size)            # str_pool_bytes
    header += enc_u32(rec_off)                  # rec_off
    header += enc_u16(64)                       # rec_size
    header += enc_u16(32 + str_pool_size)       # list_pool_off
    header += enc_u32(0)                        # crc32（占位，稍后回填）

    blob = bytes(header) + bytes(str_pool) + bytes(list_pool) + rec_bytes
    crc = zlib.crc32(blob[32:]) & 0xFFFFFFFF
    blob = blob[:28] + enc_u32(crc) + blob[32:]

    # ---- 校验：自洽回读（首末日 + 闰月日 + 跨年）----
    _self_check(blob, start, end)

    return blob, count, str_pool_size, list_pool_size, day_count, base_days, dict_terms, term_count


def xm_hl_charset():
    """返回黄历全部上屏字符集合（字段串 + 宜/忌/吉神/凶煞术语 + 固定查表），供字体子集裁剪。"""
    start = date(2026, 1, 1)
    end = date(2028, 12, 31)
    day_count = (end - start).days + 1
    chars = set()

    def add_chars(s):
        if s:
            chars.update(str(s))

    # 固定查表字符
    for t in ZHIXING + XU + NAYIN + DIR8 + JIAZI:
        add_chars(t)

    for n in range(day_count):
        d = start + timedelta(days=n)
        lun = Lunar.fromDate(datetime(d.year, d.month, d.day))
        mc = lun.getMonthInChinese()
        leap = lun.getMonth() < 0
        add_chars(lun.getYearInChinese())
        add_chars((("闰" + mc) if leap else (mc + "月")))
        add_chars(lun.getDayInChinese())
        add_chars(lun.getYearInGanZhi())
        add_chars(lun.getMonthInGanZhi())
        add_chars(lun.getDayInGanZhi())
        add_chars(lun.getDayTianShen())
        add_chars(lun.getDayChongDesc())
        add_chars(lun.getDaySha())
        add_chars(lun.getPengZuGan())
        add_chars(lun.getPengZuZhi())
        add_chars(lun.getDayNaYin())
        add_chars(lun.getDayPositionXi())
        add_chars(lun.getDayPositionFu())
        add_chars(lun.getDayPositionCai())
        for t in lun.getDayYi():
            add_chars(t)
        for t in lun.getDayJi():
            add_chars(t)
        for t in (lun.getDayJiShen() if hasattr(lun, "getDayJiShen") else []):
            add_chars(t)
        for t in (lun.getDayXiongSha() if hasattr(lun, "getDayXiongSha") else []):
            add_chars(t)
        for tm in lun.getTimes()[:12]:
            add_chars(tm.getGanZhi())
    return chars


def _self_check(blob, start, end):
    import struct
    assert blob[0:4] == b"XMHL"
    magic, ver, dc, bd, spo, spb, ro, rs, lpo, crc = struct.unpack_from("<4sHHIIIIHHI", blob, 0)
    assert magic == b"XMHL"
    assert ver == 1 and dc == 1096 and bd == 20454 and spo == 32 and rs == 64
    assert ro + dc * rs <= len(blob)
    assert lpo > spo and lpo < ro and spo + spb <= len(blob)
    import zlib as _z
    assert (_z.crc32(blob[32:]) & 0xFFFFFFFF) == crc
    # 抽查首尾两天能定位记录
    for n in (0, dc - 1):
        d = start + __import__("datetime").timedelta(days=n)
        days = days_from_civil(d.year, d.month, d.day)
        idx = days - bd
        assert 0 <= idx < dc
        rec = blob[ro + idx * rs: ro + idx * rs + rs]
        assert len(rec) == 64


def emit_c(blob, count, str_pool_size, list_pool_size, day_count, base_days, dict_terms, term_count):
    out_dir = os.path.join(REPO, "main", "generated")
    os.makedirs(out_dir, exist_ok=True)
    # header
    h_path = os.path.join(out_dir, "xm_huangli_data.h")
    with open(h_path, "w", encoding="utf-8") as f:
        f.write("/* 自动生成：请勿手改。XMHL v1 黄历查表声明。 */\n")
        f.write("#pragma once\n#include \"xm_types.h\"\n\n")
        f.write("/* 黄历数据 blob（const，置于 Flash .rodata，不占内部 SRAM）。 */\n")
        f.write("extern const uint8_t xm_huangli_blob[];\n")
        f.write("extern const uint32_t xm_huangli_blob_size;\n\n")
        f.write("/* 固定查表：建除十二神 / 二十八宿 / 八方位 / 三十纳音 / 六十甲子。 */\n")
        f.write("extern const char *const xm_zhixing_table[12];\n")
        f.write("extern const char *const xm_xiu_table[28];\n")
        f.write("extern const char *const xm_dir_table[8];\n")
        f.write("extern const char *const xm_nayin_table[30];\n")
        f.write("extern const char *const xm_jiazi_table[60];\n")
    # source
    c_path = os.path.join(out_dir, "xm_huangli_data.c")
    with open(c_path, "w", encoding="utf-8") as f:
        f.write("/* 自动生成：请勿手改。XMHL v1 黄历查表（1096 天）。 */\n")
        f.write("#include \"xm_huangli_data.h\"\n\n")
        f.write("const uint8_t xm_huangli_blob[] = {\n")
        for i in range(0, len(blob), 16):
            chunk = blob[i:i + 16]
            f.write("    " + ", ".join("0x%02X" % b for b in chunk) + ",\n")
        f.write("};\n")
        f.write("const uint32_t xm_huangli_blob_size = %d;\n\n" % len(blob))
        f.write("const char *const xm_zhixing_table[12] = {\n")
        f.write("    " + ", ".join('"%s"' % t for t in ZHIXING) + "\n};\n")
        f.write("const char *const xm_xiu_table[28] = {\n")
        f.write("    " + ", ".join('"%s"' % t for t in XU) + "\n};\n")
        f.write("const char *const xm_dir_table[8] = {\n")
        f.write("    " + ", ".join('"%s"' % t for t in DIR8) + "\n};\n")
        f.write("const char *const xm_nayin_table[30] = {\n")
        f.write("    " + ", ".join('"%s"' % t for t in NAYIN) + "\n};\n")
        f.write("const char *const xm_jiazi_table[60] = {\n")
        f.write("    " + ", ".join('"%s"' % t for t in JIAZI) + "\n};\n")
    return c_path, h_path


def main():
    blob, count, spb, lpb, dc, bd, dict_terms, term_count = build()
    c_path, h_path = emit_c(blob, count, spb, lpb, dc, bd, dict_terms, term_count)
    total = len(blob)
    print("XMHL v1 生成成功：")
    print("  blob 总字节 : %d (%.1f KiB)" % (total, total / 1024.0))
    print("  str_pool    : %d 字节, %d 个字符串" % (spb, count))
    print("  list_pool   : %d 字节, 字典 %d 项（其余 0xFF 转义）" % (lpb, term_count))
    print("  记录区      : %d × 64 = %d 字节" % (dc, dc * 64))
    print("  预算 ≤130KiB: %s" % ("OK" if total <= 130 * 1024 else "超限!"))
    print("  产物        : %s" % c_path)
    print("               %s" % h_path)


if __name__ == "__main__":
    main()
