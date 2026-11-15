#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_yijing.py —— 构建期生成 XMYJ v1 六十四卦查表。

输出：
  main/generated/xm_yijing_data.h   声明 magic / version / 64 条目 / 字符串池
  main/generated/xm_yijing_data.c   实现（const 数据，置于 Flash .rodata）

条目布局（严格匹配 main/core/xm_yijing.h 的 xm_gua_entry_t，已 XM_PACKED）：
  mark u8 | off[19] u16 | len[19] u8 | rsv u8  = 59 字节

字段顺序（xm_yj_field_t）：
  0 NAME 卦名 | 1 GUACI 卦辞 | 2 XIANG 大象传 | 3 PLAIN 白话核心
  | 4..9 YAO_1..YAO_6 初/二/三/四/五/上 爻辞
  | 10 YI 宜 | 11 JI 忌 | 12 TIMING 时机（传统占断）
  | 13 OVERALL 总论（综合解读）| 14 CAREER 事业 | 15 WEALTH 财富
  | 16 LOVE 爱情 | 17 HEALTH 健康 | 18 HOME 家宅（分维度解卦）

卦码 D6：初爻最低位（mark bit5=初，bit0=上）；索引 = 初*32+二*16+三*8+四*4+五*2+上*1。
  XM_GUA_IDX(mark) = mark & 0x3F，故 entry[mark].mark == mark 即满足 xm_yijing_init 的校验。
  爻辞的"九/六"直接给出阴阳，比上下卦更权威，用它反推 mark，避免 D7（上下卦列序）陷阱。

数据来源：
  - 玄门布衣 corpus/yijing/gua64.tsv（名/卦辞/大象/白话/宜/忌/时机）+ yaoci384.tsv（384 爻辞）。
  - 玄门布衣 corpus/yijing/gua64_detail.tsv（64 卦 × 多学派分维度解读，含傅佩荣解卦手册
    的 事业/财运/婚恋/身体·疾病/家宅 等维度）。本生成器从中抽取 6 个生活维度解读，
    作为用户真正要的「解卦」内容（就卦象讲事业/财富/爱情/健康等维度）。
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC_DIR = os.environ.get(
    "XM_YIJING_SRC",
    "/Volumes/Extreme SSD/老刘的APP/玄门布衣/app/src/main/assets/corpus/yijing",
)
GUA64 = os.path.join(SRC_DIR, "gua64.tsv")
YAOCI = os.path.join(SRC_DIR, "yaoci384.tsv")
DETAIL = os.path.join(SRC_DIR, "gua64_detail.tsv")

POS_OF = {"初": 1, "二": 2, "三": 3, "四": 4, "五": 5, "上": 6}

# 八卦象字 -> 自然象（用于由 上卦/下卦 反推 gua64_detail.tsv 的卦全名）。
BAGUA_IMG = {
    "乾": "天", "坤": "地", "坎": "水", "离": "火",
    "震": "雷", "巽": "风", "艮": "山", "兑": "泽",
}

# gua64.tsv 的「卦名」（单字/简名）-> gua64_detail.tsv 的「卦全名」。
# 注：gua64.tsv 的「上卦」列实际存的是卦符符号（䷀ 等）而非卦象字，无法据此反推；
# 周易 64 卦名与全名是固定标准映射，直接硬编码并与 detail 文件 64 个全名严格校验。
FULL_NAME = {
    "乾": "乾为天", "坤": "坤为地", "屯": "水雷屯", "蒙": "山水蒙",
    "需": "水天需", "讼": "天水讼", "师": "地水师", "比": "水地比",
    "小畜": "风天小畜", "履": "天泽履", "泰": "地天泰", "否": "天地否",
    "同人": "天火同人", "大有": "火天大有", "谦": "地山谦", "豫": "雷地豫",
    "随": "泽雷随", "蛊": "山风蛊", "临": "地泽临", "观": "风地观",
    "噬嗑": "火雷噬嗑", "贲": "山火贲", "剥": "山地剥", "复": "地雷复",
    "无妄": "天雷无妄", "大畜": "山天大畜", "颐": "山雷颐", "大过": "泽风大过",
    "坎": "坎为水", "习坎": "坎为水", "离": "离为火", "咸": "泽山咸", "恒": "雷风恒",
    "遁": "天山遁", "大壮": "雷天大壮", "晋": "火地晋", "明夷": "地火明夷",
    "家人": "风火家人", "睽": "火泽睽", "蹇": "水山蹇", "解": "雷水解",
    "损": "山泽损", "益": "风雷益", "夬": "泽天夬", "姤": "天风姤",
    "萃": "泽地萃", "升": "地风升", "困": "泽水困", "井": "水风井",
    "革": "泽火革", "鼎": "火风鼎", "震": "震为雷", "艮": "艮为山",
    "渐": "风山渐", "归妹": "雷泽归妹", "丰": "雷火丰", "旅": "火山旅",
    "巽": "巽为风", "兑": "兑为泽", "涣": "风水涣", "节": "水泽节",
    "中孚": "风泽中孚", "小过": "雷山小过", "既济": "水火既济", "未济": "火水未济",
}

# 源 corpus（玄门布衣 gua64.tsv）个别卦的「宜/忌/时机」整列缺失，
# 在此做内置补丁兜底（不改外部 corpus，保证六十四卦解卦字段齐全）。
# 内容依据该卦《大象传》/传统占断撰写，非杜撰。
GUA64_PATCH = {
    "习坎": {
        "yi": "固守诚信 循序渐进 待时而动",
        "ji": "冒进逞强 轻信盲从 孤注一掷",
        "timing": "险境重重 守正可脱困 需耐心等待",
    },
}

# 从 gua64_detail.tsv 抽取的分维度标签 -> 内部维度键。
# 每条取最长匹配段（同一卦多学派重复出现时留最完整的一句），并裁剪首尾标点。
DIM_PATTERNS = {
    "overall": [r"[吉凶平中][：:]\s*([^。；;]{2,80})"],
    "career":  [r"事业[：:]\s*([^。；;]{2,80})"],
    "wealth":  [r"财运[：:]\s*([^。；;]{2,80})"],
    "love":    [r"婚恋[：:]\s*([^。；;]{2,80})"],
    "health":  [r"身体[：:]\s*([^。；;]{2,80})", r"疾病[：:]\s*([^。；;]{2,80})"],
    "home":    [r"家宅[：:]\s*([^。；;]{2,80})"],
}
# 健康维度优先取「身体」，其次「疾病」。
HEALTH_PREFER = 0


def _trim(t):
    return t.strip(" \t　。；;，、：:．. \n\r")


def enc_u16(v):
    return bytes([v & 0xFF, (v >> 8) & 0xFF])


def build_full_name(shang, xia):
    """由 上卦/下卦 的卦象字反推 gua64_detail.tsv 的卦全名。
    纯卦（上==下）形如「乾为天」「坤为地」；异卦形如「水雷屯」「山水蒙」。"""
    if shang == xia:
        return shang + "为" + BAGUA_IMG.get(shang, shang)
    return BAGUA_IMG.get(shang, "") + BAGUA_IMG.get(xia, "")


def parse_gua64():
    """卦名 -> {guaci, xiang, plain, yi, ji, timing}。
    列：0名 1符 2上卦 3下卦 4卦辞 5大象 6白话 7宜 8忌 9时机。
    同时返回 上卦/下卦 用于反推 detail 全名。"""
    out = {}
    meta = {}
    with open(GUA64, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            cols = line.split("\t")
            if len(cols) < 10:
                continue
            name = cols[0].strip()
            out[name] = {
                "guaci": cols[4].strip(),
                "xiang": cols[5].strip(),
                "plain": cols[6].strip(),
                "yi": cols[7].strip(),
                "ji": cols[8].strip(),
                "timing": cols[9].strip(),
            }
            meta[name] = (cols[2].strip(), cols[3].strip())
    # 应用内置补丁（源 corpus 缺失字段的兜底）。
    for name, patch in GUA64_PATCH.items():
        if name in out:
            for k, v in patch.items():
                if not out[name][k]:
                    out[name][k] = v
    return out, meta


def parse_yaoci():
    """卦名 -> {pos: (yaoci_text, yang_bool)}。列：0名 1爻名 2爻辞 3吉凶。"""
    out = {}
    with open(YAOCI, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            cols = line.split("\t")
            if len(cols) < 3:
                continue
            name = cols[0].strip()
            yaoming = cols[1].strip()
            yaoci = cols[2].strip()
            if len(yaoming) < 2:
                continue
            if yaoming in ("用九", "用六"):
                continue
            if yaoming.startswith("初"):
                pos = 1
            elif yaoming.startswith("上"):
                pos = 6
            else:
                pos_char = yaoming[1]
                pos = {"二": 2, "三": 3, "四": 4, "五": 5}.get(pos_char)
                if pos is None:
                    continue
            yang = ("九" in yaoming)  # 九=阳，六=阴
            out.setdefault(name, {})[pos] = (yaoci, yang)
    return out


def parse_detail():
    """全名 -> 该卦所有解读文本（col2 综合解读，多学派多行）。"""
    out = {}
    if not os.path.exists(DETAIL):
        return out
    with open(DETAIL, "r", encoding="utf-8") as f:
        f.readline()  # 跳过表头
        for line in f:
            cols = line.rstrip("\n").split("\t")
            if len(cols) < 3:
                continue
            name = cols[0].strip()
            out.setdefault(name, []).append(cols[2])
    return out


def _extract_one(texts):
    """在给定文本集合里抽取 6 个生活维度，返回 {key: text}（可能为空）。"""
    res = {}
    for key, pats in DIM_PATTERNS.items():
        best = ""
        for pat in pats:
            # 健康维度按 PREFER 顺序：先 body 后 disease，非空才用后面的
            if key == "health" and pat != DIM_PATTERNS["health"][HEALTH_PREFER] and best:
                continue
            for t in texts:
                for m in re.finditer(pat, t):
                    cand = _trim(m.group(1))
                    if len(cand) > len(best):
                        best = cand
        res[key] = best
    return res


def extract_dimensions(texts):
    """从一卦的全部解读文本里抽取 6 个生活维度，返回 {key: text}。
    优先取「通用解读行」（不含「变卦」的行，即整卦层面的邵雍/傅佩荣解读），
    避免抽到某一爻动变的零碎判语；通用行抽不到的维度再放宽到全部行兜底。
    overall 缺省时回退到首段通用解读（裁到 80 字）。"""
    general = [t for t in texts if "变卦" not in t]
    res = _extract_one(general)
    # 兜底：通用行没抽到的维度，再放宽到含变卦行的全部文本里找。
    if any(not res[k] for k in res):
        all_res = _extract_one(texts)
        for k in res:
            if not res[k] and all_res[k]:
                res[k] = all_res[k]
    # overall 兜底：没有「吉/凶/平/中：」段时，用首段综合解读裁前 80 字。
    if not res.get("overall") and texts:
        first = general[0] if general else texts[0]
        # 去掉开头的「卦名。」前缀（如「乾。 元亨利贞...」）
        m = re.match(r"^[^。；;]{1,8}[。；;]\s*(.*)$", first)
        body = m.group(1) if m else first
        res["overall"] = _trim(body)[:80]
    return res


def build():
    gua, meta = parse_gua64()
    yao = parse_yaoci()
    detail = parse_detail()
    assert len(gua) == 64, "gua64 应为 64 卦，实际 %d" % len(gua)
    assert len(yao) == 64, "yaoci 应覆盖 64 卦，实际 %d" % len(yao)

    # 建立 detail 全名 -> 维度解读 的索引（按硬编码的 卦名->全名 对照表）。
    detail_dims = {}
    unmatched = []
    for name in gua:
        full = FULL_NAME.get(name)
        if full and full in detail:
            detail_dims[name] = extract_dimensions(detail[full])
        else:
            unmatched.append((name, full))
    assert not unmatched, "以下卦未能在 gua64_detail.tsv 命中：%s" % unmatched

    # entry[mark] = {name, off[19], len[19]}；先收集文本，再统一进字符串池。
    entries = {}  # mark -> list of 19 字段文本
    marks_seen = set()
    for name in gua:
        yd = yao.get(name)
        assert yd and len(yd) == 6, "卦 %s 的 6 爻不全" % name
        mark = 0
        fields_yao = [None] * 6  # pos1..pos6
        for pos in range(1, 7):
            yaoci, yang = yd[pos]
            fields_yao[pos - 1] = yaoci
            mark |= ((1 if yang else 0) << (6 - pos))
        assert mark not in marks_seen, "卦码冲突：%s 与既有同 mark=%d" % (name, mark)
        marks_seen.add(mark)
        g = gua[name]
        d = detail_dims[name]
        fields = ([name, g["guaci"], g["xiang"], g["plain"]]
                  + fields_yao
                  + [g["yi"], g["ji"], g["timing"]]
                  + [d["overall"], d["career"], d["wealth"], d["love"],
                     d["health"], d["home"]])
        # 任何空字段补占位符：xm_gua_text 对 len==0 返回 NULL，而 UI 用 %s 打印，必须避免。
        fields = [(s if s else "（整理本暂缺）") for s in fields]
        assert len(fields) == 19, "字段数应为 19，实际 %d" % len(fields)
        entries[mark] = fields

    assert len(entries) == 64, "应生成 64 个互异卦码，实际 %d" % len(entries)

    # 字符串池（去重；每条字段 NUL 结尾，off 为池中真实字节偏移，len 不含 NUL）。
    str_off = {}   # 字符串 -> 池中字节偏移
    pool = bytearray()

    def add_str(s):
        if s not in str_off:
            str_off[s] = len(pool)
            pool.extend(s.encode("utf-8"))
            pool.append(0)
        return str_off[s]

    marks_sorted = sorted(entries.keys())
    entry_meta = {}
    for mark in marks_sorted:
        fields = entries[mark]
        off = []
        ln = []
        for s in fields:
            o = add_str(s)
            off.append(o)
            ln.append(len(s.encode("utf-8")))
        entry_meta[mark] = (off, ln)

    strpool_size = len(pool)
    assert strpool_size > 0
    return marks_sorted, entry_meta, bytes(pool), strpool_size


def emit_c(marks_sorted, entry_meta, pool, strpool_size):
    out_dir = os.path.join(REPO, "main", "generated")
    os.makedirs(out_dir, exist_ok=True)
    h_path = os.path.join(out_dir, "xm_yijing_data.h")
    with open(h_path, "w", encoding="utf-8") as f:
        f.write("/* 自动生成：请勿手改。XMYJ v1 六十四卦查表声明。 */\n")
        f.write("#pragma once\n#include \"xm_types.h\"\n#include \"xm_yijing.h\"\n\n")
        f.write("extern const uint8_t xm_yijing_magic[4];\n")
        f.write("extern const uint8_t xm_yijing_version;\n")
        f.write("extern const xm_gua_entry_t xm_yijing[64];\n")
        f.write("extern const uint8_t xm_yijing_strpool[];\n")
        f.write("extern const uint32_t xm_yijing_strpool_size;\n")

    c_path = os.path.join(out_dir, "xm_yijing_data.c")
    with open(c_path, "w", encoding="utf-8") as f:
        f.write("/* 自动生成：请勿手改。XMYJ v1 六十四卦查表（64 卦 × 19 字段）。 */\n")
        f.write("#include \"xm_yijing_data.h\"\n\n")
        f.write("const uint8_t xm_yijing_magic[4] = { 'X', 'M', 'Y', 'J' };\n")
        f.write("const uint8_t xm_yijing_version = 1;\n\n")

        f.write("const xm_gua_entry_t xm_yijing[64] = {\n")
        for mark in marks_sorted:
            off, ln = entry_meta[mark]
            rsv = 0
            off_list = ", ".join("%d" % o for o in off)
            len_list = ", ".join("%d" % l for l in ln)
            f.write("    { 0x%02X, { %s }, { %s }, %d },\n" % (mark, off_list, len_list, rsv))
        f.write("};\n\n")

        f.write("const uint8_t xm_yijing_strpool[] = {\n")
        for i in range(0, len(pool), 16):
            chunk = pool[i:i + 16]
            f.write("    " + ", ".join("0x%02X" % b for b in chunk) + ",\n")
        f.write("};\n")
        f.write("const uint32_t xm_yijing_strpool_size = %d;\n" % strpool_size)
    return c_path, h_path


def _self_check(marks_sorted, entry_meta, pool, strpool_size):
    # 校验：64 互异 mark 覆盖 0..63；每条 off+len+1 <= size；len>0
    assert sorted(marks_sorted) == list(range(64)), "卦码未覆盖 0..63"
    for mark in marks_sorted:
        off, ln = entry_meta[mark]
        assert len(off) == 19 and len(ln) == 19, "字段数不对 mark=%d" % mark
        for o, l in zip(off, ln):
            assert l > 0, "空字段"
            assert o + l + 1 <= strpool_size, "字符串越界 mark=%d" % mark
            assert pool[o + l] == 0, "字符串未 NUL 结尾"


def main():
    marks_sorted, entry_meta, pool, spb = build()
    _self_check(marks_sorted, entry_meta, pool, spb)
    c_path, h_path = emit_c(marks_sorted, entry_meta, pool, spb)
    print("XMYJ v1 生成成功：")
    print("  卦码覆盖  : 0..63（64 互异）")
    print("  字符串池  : %d 字节, %d 个字符串" % (spb, pool.count(0)))
    print("  条目区    : 64 × 59 = %d 字节" % (64 * 59))
    print("  产物      : %s" % c_path)
    print("             %s" % h_path)


if __name__ == "__main__":
    main()
