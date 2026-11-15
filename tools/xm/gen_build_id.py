#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_build_id.py - 生成 main/generated/xm_build_id.h 的 8 个字符串宏，供 main.c 与关于页/预览 manifest 共用。

宏（均为字符串，匹配 main.c 的 ESP_LOGI %s 顺序）：
  XM_APP_VERSION       应用版本
  XM_BUILD_ID          构建标识（git 短哈希；无 git 则用时间戳）
  XM_DATA_EPOCH        数据起始日（黄历覆盖起点）
  XM_IDF_VERSION       ESP-IDF 版本
  XM_LVGL_VERSION      LVGL 版本
  XM_FONTCONV_VERSION  lv_font_conv 版本
  XM_CHARSET_SHA256    字符集 sha256（assets/data/charset.sha256）
  XM_DATA_SHA256       数据表 .c 拼接 sha256（黄历/易经）
"""
import os
import sys
import subprocess
import hashlib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GEN = os.path.join(REPO, "main", "generated")
ASSETS = os.path.join(REPO, "assets", "data")

APP_VERSION = "1.0.0"
DATA_EPOCH = "2026-01-01"
IDF_VERSION = "v5.5.3"
LVGL_VERSION = "9.5.0"
FONTCONV_VERSION = "1.5.3"


def git_build_id():
    """生成构建标识：git 短哈希，脏工作区追加 -dirty。

    脏判定必须用 `git status --porcelain`，不能只用 `git diff --quiet`：
    后者只看「已跟踪文件的工作区 vs 索引」，而本分支的玄门主体
    （main/core/、main/generated/、main/xm_*.c、assets/）全是未跟踪文件，
    只用 git diff 会把「几百个改动文件」误判成干净。
    """
    try:
        rev = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"], cwd=REPO, stderr=subprocess.DEVNULL
        ).decode().strip()
        status = subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=REPO, stderr=subprocess.DEVNULL
        ).decode().strip()
        dirty = len(status) > 0
        return rev + ("-dirty" if dirty else "")
    except Exception:
        import datetime
        return "dev-" + datetime.datetime.utcnow().strftime("%Y%m%d")


def read_charset_sha():
    p = os.path.join(ASSETS, "charset.sha256")
    if os.path.exists(p):
        with open(p, "r", encoding="utf-8") as f:
            return f.read().strip().split()[0]
    return "unknown"


def data_sha256():
    files = [
        "xm_huangli_data.c", "xm_yijing_data.c",
    ]
    h = hashlib.sha256()
    for fn in files:
        fp = os.path.join(GEN, fn)
        if not os.path.exists(fp):
            h.update(fn.encode("utf-8"))
            continue
        with open(fp, "rb") as f:
            h.update(f.read())
    return h.hexdigest()


def main():
    build_id = git_build_id()
    charset_sha = read_charset_sha()
    data_sha = data_sha256()

    os.makedirs(GEN, exist_ok=True)
    out = os.path.join(GEN, "xm_build_id.h")
    with open(out, "w", encoding="utf-8") as f:
        f.write("/* 自动生成：请勿手改。玄门构建标识（字符串宏）。 */\n")
        f.write("#pragma once\n\n")
        f.write('#define XM_APP_VERSION     "%s"\n' % APP_VERSION)
        f.write('#define XM_BUILD_ID        "%s"\n' % build_id)
        f.write('#define XM_DATA_EPOCH      "%s"\n' % DATA_EPOCH)
        f.write('#define XM_IDF_VERSION     "%s"\n' % IDF_VERSION)
        f.write('#define XM_LVGL_VERSION    "%s"\n' % LVGL_VERSION)
        f.write('#define XM_FONTCONV_VERSION "%s"\n' % FONTCONV_VERSION)
        f.write('#define XM_CHARSET_SHA256  "%s"\n' % charset_sha)
        f.write('#define XM_DATA_SHA256     "%s"\n' % data_sha)
    print("xm_build_id.h 生成成功：")
    print("  APP_VERSION     : %s" % APP_VERSION)
    print("  BUILD_ID        : %s" % build_id)
    print("  DATA_EPOCH      : %s" % DATA_EPOCH)
    print("  IDF_VERSION     : %s" % IDF_VERSION)
    print("  LVGL_VERSION    : %s" % LVGL_VERSION)
    print("  FONTCONV_VERSION: %s" % FONTCONV_VERSION)
    print("  CHARSET_SHA256  : %s" % charset_sha)
    print("  DATA_SHA256     : %s" % data_sha)
    print("  产物            : %s" % out)


if __name__ == "__main__":
    main()
