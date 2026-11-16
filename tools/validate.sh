#!/usr/bin/env bash
# 折叠北京 · 一键验证。
#
# 每一道门禁对应一类"编译通过但设备上是坏的"故障：
#   art-check   美术二进制长度/编码不对  -> 花屏、黑块
#   charset     字库缺字                -> 屏上空白方块
#   host-test   数值/导航回归            -> 玩法坏掉但能开机
#   layout      文本越界/重叠            -> 字被裁掉或压在一起
#   build       固件能链接、分区放得下    -> 刷不进去
#
# 用法：
#   bash tools/validate.sh          # 除构建以外的全部门禁
#   bash tools/validate.sh --build  # 再加上固件构建
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

DO_BUILD=0
[ "${1:-}" = "--build" ] && DO_BUILD=1

PASS=0
FAIL=0
step() { printf '\n=== %s ===\n' "$1"; }
ok()   { PASS=$((PASS + 1)); printf '  PASS %s\n' "$1"; }
bad()  { FAIL=$((FAIL + 1)); printf '  FAIL %s\n' "$1"; }

step "1/5 美术清单自校验"
if python3 tools/xm/make_fb_art.py --check; then ok "清单与产物 sha 一致"; else bad "美术产物漂移，重新跑 make_fb_art.py"; fi

step "2/5 美术二进制格式门禁"
if python3 tools/xm/check_fb_art.py >/tmp/fb_art_check.log 2>&1; then
  tail -1 /tmp/fb_art_check.log; ok "RGB565 / RGB565A8 可按 LVGL 语义还原"
else
  cat /tmp/fb_art_check.log; bad "美术二进制编码错误"
fi

step "3/5 字符集与字体覆盖"
python3 tools/xm/gen_charset.py | tail -3
# 视图 dump 既是预览输入，也是字体门禁的真实上屏文本来源
if gcc -std=c11 -Wall -Wextra -Werror -I main -I main/core -I main/generated \
      -o /tmp/fb_vm_dump tools/xm/fb_vm_dump.c tools/xm/fb_art_host_stub.c \
      main/fb_app.c main/fb_game.c main/core/xm_fontcov.c 2>/tmp/fb_dump_build.log; then
  /tmp/fb_vm_dump >/tmp/fb_dump.json
  if python3 tools/xm/check_fb_charset.py /tmp/fb_dump.json | tail -1; then
    ok "全部上屏字符都在字库内"
  else
    bad "字库缺字（重新跑 tools/xm/gen_fonts.sh）"
  fi
else
  cat /tmp/fb_dump_build.log; bad "视图 dump 编译失败"
fi

step "4/5 主机测试（引擎契约 + 页面布局）"
if gcc -std=c11 -Wall -Wextra -Werror -I main -o /tmp/t_game tests/test_fb_game.c \
      main/fb_game.c 2>/tmp/t1.log; then
  if /tmp/t_game >/tmp/t1.out 2>&1; then tail -1 /tmp/t1.out; ok "引擎契约"; else cat /tmp/t1.out; bad "引擎契约"; fi
else
  cat /tmp/t1.log; bad "引擎测试编译失败"
fi
if gcc -std=c11 -Wall -Wextra -I main -I main/core -I main/generated -o /tmp/t_pages \
      tests/test_fb_app_pages.c tools/xm/fb_art_host_stub.c main/fb_app.c \
      main/fb_game.c main/core/xm_fontcov.c 2>/tmp/t2.log; then
  if /tmp/t_pages >/tmp/t2.out 2>&1; then tail -1 /tmp/t2.out; ok "页面布局与导航"; else cat /tmp/t2.out; bad "页面布局与导航"; fi
else
  cat /tmp/t2.log; bad "页面测试编译失败"
fi
# 存档：往返一致 + 坏档必拒。这一项挂了等于玩家进度会无声消失。
if gcc -std=c11 -Wall -Wextra -Werror -I main -I main/core -I main/generated -o /tmp/t_save \
      tests/test_fb_save.c tools/xm/fb_art_host_stub.c main/fb_save.c main/fb_app.c \
      main/fb_game.c main/core/xm_fontcov.c 2>/tmp/t3.log; then
  if /tmp/t_save >/tmp/t3.out 2>&1; then tail -1 /tmp/t3.out; ok "存档往返与坏档拒绝"; else cat /tmp/t3.out; bad "存档往返与坏档拒绝"; fi
else
  cat /tmp/t3.log; bad "存档测试编译失败"
fi

# 空闲息屏：60 秒无操作关背光 + 第一下按键只唤醒。这一项挂了等于玩家盲按
# 一下就被送进动作页，或者屏幕永远不熄。
if gcc -std=c11 -Wall -Wextra -Werror -I main -o /tmp/t_idle tests/test_fb_idle.c \
      main/fb_idle.c 2>/tmp/t4.log; then
  if /tmp/t_idle >/tmp/t4.out 2>&1; then tail -1 /tmp/t4.out; ok "空闲息屏与唤醒"; else cat /tmp/t4.out; bad "空闲息屏与唤醒"; fi
else
  cat /tmp/t4.log; bad "息屏测试编译失败"
fi

# 租车月租：剩余天数、收车/续租文案必须排最前、房租不许挤掉车租。
# 这一项挂了等于玩家又要经历一次「网约车凭空消失」。
if gcc -std=c11 -Wall -Wextra -Werror -I main -o /tmp/t_rent tests/test_fb_rent.c \
      main/fb_game.c 2>/tmp/t4b.log; then
  if /tmp/t_rent >/tmp/t4b.out 2>&1; then tail -1 /tmp/t4b.out; ok "租车月租与收车可见性"; else cat /tmp/t4b.out; bad "租车月租与收车可见性"; fi
else
  cat /tmp/t4b.log; bad "租车测试编译失败"
fi

step "5/5 固件构建"
if [ "$DO_BUILD" = "1" ]; then
  # shellcheck disable=SC1091
  source "/Volumes/Extreme SSD/_toolchain/env-xuanmen.sh" >/dev/null 2>&1
  if idf.py build >/tmp/fb_build.log 2>&1; then
    grep -E "binary size" /tmp/fb_build.log | tail -1
    ok "固件构建"
  else
    grep -E "error" /tmp/fb_build.log | head -10; bad "固件构建"
  fi
else
  printf '  跳过（加 --build 启用）\n'
fi

printf '\n=============================\n'
printf '通过 %d 项，失败 %d 项\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
printf '全部门禁通过。预览：preview/_all_pages.png\n'
