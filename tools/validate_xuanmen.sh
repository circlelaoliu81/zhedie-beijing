#!/usr/bin/env bash
# validate_xuanmen.sh —— 玄门专属静态契约校验（validate.sh --static 末尾调用）。
# 核对生成资产的关键导出符号与构建标识宏均存在，缺失即 FAIL。
set -uo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

fail() { echo "FAIL: $1"; exit 1; }

echo "== 玄门静态校验 =="

# 数据表导出符号
grep -q "const uint8_t xm_huangli_blob\[\]"      main/generated/xm_huangli_data.c || fail "黄历 blob 符号缺失"
grep -q "const xm_gua_entry_t xm_yijing\[64\]"   main/generated/xm_yijing_data.c || fail "易经条目符号缺失"

# 四套字体符号
for sz in 12 16 20 24; do
  grep -q "const lv_font_t xm_font_${sz} =" "assets/fonts/xm_font_${sz}.c" \
    || fail "字体 xm_font_${sz} 符号缺失"
done

# 构建标识 8 宏
for m in XM_APP_VERSION XM_BUILD_ID XM_DATA_EPOCH XM_IDF_VERSION \
         XM_LVGL_VERSION XM_FONTCONV_VERSION XM_CHARSET_SHA256 XM_DATA_SHA256; do
  grep -q "#define ${m}" main/generated/xm_build_id.h || fail "构建标识宏 ${m} 缺失"
done

# 字符集清单存在且非空
[ -s assets/data/charset.txt ] || fail "字符集清单为空或缺失"

# ---- 真机栈预算（host 测试测不出来的那类缺陷）----------------------------
# ESP32-C3 的 main 任务栈只有几 KB，而 host 测试跑在 8MB 栈上：
# xm_vm_render 曾因栈上放了一份 7.1KB 的 xm_page_state_t，单函数吃掉 7280B，
# 四轮 host 复验全 PASS，真机首屏却 Stack protection fault 无限重启。
# 这里改用编译器的 -fstack-usage 报告直接卡阈值，与主机栈大小解耦。
#
# 负向对照：把 main/core/xm_vm.c 里 `static const xm_page_state_t fallback`
# 的 static 去掉，重跑本校验必须 FAIL 并报「xm_vm_render 7280B > 预算 1024B」。
STACK_BUDGET=1024
SU_DIR="$(mktemp -d /tmp/xuanmen-stack-usage.XXXXXX)"
trap 'rm -rf "${SU_DIR}"' EXIT
for src in main/core/xm_vm.c main/xm_pages.c main/core/xm_nav.c \
           main/core/xm_time.c main/core/xm_store.c main/core/xm_liuyao.c \
           main/core/xm_huangli.c main/core/xm_yijing.c main/core/xm_layout.c \
           main/core/xm_fontcov.c; do
  "${CC:-cc}" -std=c11 -O2 -fstack-usage -Imain -Imain/core -Imain/generated \
      -c "${src}" -o "${SU_DIR}/$(basename "${src%.c}").o" \
      || fail "栈用量报告生成失败：${src}"
done
python3 - "${SU_DIR}" "${STACK_BUDGET}" <<'PY' || fail "玄门栈预算超限"
import glob, os, sys
out, budget = sys.argv[1], int(sys.argv[2])
rows = []
for path in glob.glob(os.path.join(out, "*.su")):
    for line in open(path, encoding="utf-8"):
        cols = line.rstrip("\n").split("\t")
        if len(cols) >= 2 and cols[1].isdigit():
            rows.append((int(cols[1]), cols[0]))
if not rows:
    print("FAIL: 未读到任何栈用量记录，门禁形同虚设")
    sys.exit(1)
over = [r for r in rows if r[0] > budget]
for n, loc in sorted(over, reverse=True):
    print(f"FAIL: {loc} 栈用量 {n}B > 预算 {budget}B")
if over:
    print(f"共 {len(over)} 个函数超预算；真机 main 任务栈放不下")
    sys.exit(1)
mx = max(rows)
print(f"玄门栈预算校验：PASS（{len(rows)} 个函数，最大 {mx[0]}B / 预算 {budget}B）")
PY

echo "玄门静态校验：PASS"
exit 0
