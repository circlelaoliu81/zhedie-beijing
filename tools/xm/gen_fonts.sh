#!/usr/bin/env bash
# gen_fonts.sh —— 用 lv_font_conv 按字符集逐尺寸生成 4 套中文字体子集（bpp 4，压缩）。
# 必须在构建固件前运行；校验脚本 check_assets.py 会确认 4 个字体 .c 存在。
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
FONT="$REPO/managed_components/lvgl__lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf"
CHARSET="$REPO/assets/data/charset.txt"
OUTDIR="$REPO/assets/fonts"
LFC="/tmp/xuanmen-fonttools/node_modules/.bin/lv_font_conv"

if [ ! -f "$LFC" ]; then
  echo "未找到 lv_font_conv（/tmp 软链可能因重启失效）。请重新执行 env-xuanmen.sh 或重装字体工具。" >&2
  exit 1
fi
if [ ! -f "$FONT" ]; then
  echo "未找到基础字体 $FONT" >&2
  exit 1
fi
if [ ! -f "$CHARSET" ]; then
  echo "未找到字符集 $CHARSET，请先运行 gen_charset.py" >&2
  exit 1
fi

mkdir -p "$OUTDIR"

python3 - "$FONT" "$CHARSET" "$OUTDIR" "$LFC" <<'PY'
import sys, subprocess, os, json, hashlib
font, charset, outdir, lfc = sys.argv[1:5]
# 注意：charset.txt 按码位升序，首个字符就是空格 U+0020。
# 用 strip() 会把空格吃掉，导致 4 套字体都缺 U+0020 —— 设备上空格直接消失
# （LVGL 查不到字形 → glyph width 0），「上键 选上一项」会渲染成「上键选上一项」。
# 只去掉文件末尾换行，保留前导空格。
sym = open(charset, encoding="utf-8").read().rstrip("\n")
print("字符集字符数: %d" % len(sym))
files = []
for sz in (12, 16, 20, 24):
    out = os.path.join(outdir, "xm_font_%d.c" % sz)
    subprocess.run(["node", lfc, "--size", str(sz), "--bpp", "4",
                    "--font", font, "--symbols", sym, "--format", "lvgl",
                    "-o", out], check=True)
    szb = os.path.getsize(out)
    # 修正 lv_font_conv 生成的 include：本项目 LVGL 头位于组件根目录（lvgl.h），
    # 没有 lvgl/ 子目录，故强制使用 #include "lvgl.h"（由组件 include 路径解析）。
    with open(out, encoding="utf-8") as fh:
        data = fh.read()
    pat = ('#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n'
           '#else\n#include "lvgl/lvgl.h"\n#endif\n')
    if pat in data:
        data = data.replace(pat, '#include "lvgl.h"\n')
        with open(out, "w", encoding="utf-8") as fh:
            fh.write(data)
    print("  生成 xm_font_%d.c : %d KiB" % (sz, szb // 1024))
    files.append({"size": sz, "bpp": 4, "file": "xm_font_%d.c" % sz,
                  "bytes": szb,
                  "sha256": hashlib.sha256(open(out, "rb").read()).hexdigest()})
# 清单必须跟着字符集走：门禁靠它判断"改了文案有没有忘记重生成字体"。
# 之前这份清单是基线遗留的，字符数对不上也没人发现。
manifest = {
    "generator": "lv_font_conv",
    "source_font": {
        "name": "Source Han Sans SC (Normal)",
        "path": "managed_components/lvgl__lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf",
        "license": "SIL Open Font License 1.1",
    },
    "charset": {
        "file": "../data/charset.txt",
        "chars": len(sym),
        "sha256": hashlib.sha256(sym.encode("utf-8")).hexdigest(),
    },
    "bpp": 4,
    "format": "lvgl",
    "files": files,
}
with open(os.path.join(outdir, "xm_font_manifest.json"), "w", encoding="utf-8") as fh:
    json.dump(manifest, fh, ensure_ascii=False, indent=2)
    fh.write("\n")
print("字体子集生成完成。")
PY
