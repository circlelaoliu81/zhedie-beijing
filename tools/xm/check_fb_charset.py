#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""字体覆盖门禁：确认所有可能上屏的字符都在 assets/data/charset.txt 里。

两条来源都要过：
  1. main/**/*.c *.h 的字符串字面量（文案源头）
  2. fb_vm_dump 导出的实际渲染文本（真实上屏内容）

任一字缺失即非零退出。缺字在设备上会渲染成方块/空白，而主机预览看不出来，
所以这一步必须自动跑，不能靠肉眼。
"""
import hashlib
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "main"
STR_LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')


def charset():
    return set((ROOT / "assets/data/charset.txt").read_text(encoding="utf-8").rstrip("\n"))


def from_sources():
    chars = set()
    for root, _d, files in os.walk(MAIN):
        for fn in files:
            if not fn.endswith((".c", ".h")):
                continue
            txt = (Path(root) / fn).read_text(encoding="utf-8", errors="replace")
            for m in STR_LIT.finditer(txt):
                s = m.group(1).replace('\\\\', '\\').replace('\\"', '"')
                # 格式占位符（%s/%d/%%）不是上屏字符
                s = re.sub(r"%(?:[-+ #0]*\d*(?:\.\d+)?[diouxXeEfgGcspr%])", "", s)
                chars.update(s)
    return chars


def from_dump(path):
    chars = set()
    p = Path(path)
    if not p.exists():
        return chars
    for line in p.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        page = json.loads(line)
        for ln in page.get("lines", []):
            chars.update(ln.get("text", ""))
    return chars


def font_manifest():
    p = ROOT / "assets/fonts/xm_font_manifest.json"
    if not p.exists():
        return None
    return json.loads(p.read_text(encoding="utf-8"))


def main():
    dump = sys.argv[1] if len(sys.argv) > 1 else "/tmp/fb_dump.json"
    cs = charset()

    # 先验"字符集 -> 字体"这一环：字体是用某一份 charset.txt 生成的，
    # 清单里记着那份的 sha256。两份对不上就说明改了文案没重生成字体，
    # 此时下面的覆盖检查照样会 PASS（它只比 charset），但设备上会缺字。
    man = font_manifest()
    if man is None:
        print("缺少 assets/fonts/xm_font_manifest.json，无法确认字体与字符集同源")
        return 1
    cur_sha = hashlib.sha256(
        (ROOT / "assets/data/charset.txt").read_text(encoding="utf-8").rstrip("\n").encode("utf-8")
    ).hexdigest()
    m_sha = man.get("charset", {}).get("sha256", "")
    m_chars = man.get("charset", {}).get("chars", -1)
    if m_sha != cur_sha or m_chars != len(cs):
        print("字体与字符集不同源：清单 %d 字 / %s，当前 %d 字 / %s"
              % (m_chars, m_sha[:12], len(cs), cur_sha[:12]))
        print("请重新运行 tools/xm/gen_fonts.sh")
        return 1
    need = from_sources() | from_dump(dump)
    need.discard("\n")
    need.discard("\t")
    need.discard("\r")
    missing = sorted(c for c in need if c not in cs)
    print("字库字符数: %d" % len(cs))
    print("待检查字符数: %d" % len(need))
    if missing:
        print("缺失 %d 个字：" % len(missing))
        print("  " + " ".join("%s(U+%04X)" % (c, ord(c)) for c in missing))
        return 1
    print("覆盖检查 PASS：全部上屏字符均已在字库内")
    return 0


if __name__ == "__main__":
    sys.exit(main())
