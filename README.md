# 玄门

`feature/xuanmen` 分支是 FoloToy AI Passport 上的离线掌上术数小工具「玄门」。它不沿用基线 demo 测试菜单，使用全新独立 UI、纯本地确定性数据表与三键交互。

## 设备与边界

- ESP32-C3 / 8 MB Flash / 无 PSRAM / 240×320 ST7789P3 / UP·DOWN·OK 三键。
- 纯离线：不联网、不配网、不采集个人信息、不调用云端模型。
- 设备时间由用户手动设定，`esp_timer` 单调推进；每次冷启动必须确认，断电期间时间不累计。
- 黄历数据有效期为 2026-01-01 ~ 2028-12-31；超出后明确提示更新固件，不外推。
- 吉时与白话断语标注为整理本规则，不冒充古籍原文；卦辞与爻辞标注《周易》古经。
- 每页固定显示「仅供传统文化参考 · 决策自负」。

## P0 功能

- 首次免责确认、三键操作引导、设备时间确认、双入口首页、设置与关于页。
- 每日黄历三子页：14 类字段、十二时辰与吉时标记。
- 六爻摇卦六次：本卦、动爻、之卦、卦辞、动爻爻辞、白话断语、大象传折叠与最近 8 条历史。
- 12/16/20/24 四档中文子集字体、fallback、逐码点覆盖测试与主机 UI 预览。
- 异常态 E-1 ~ E-5：时间未经授时、数据过期、历史暂不可用、电量隐藏、内容暂缺。

## 构建与门禁

激活移动硬盘上的 ESP-IDF 5.5.3 环境后，在仓库根目录运行：

```bash
source "/Volumes/Extreme SSD/_toolchain/env-xuanmen.sh"
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh
```

数据表、字符清单、字体子集与构建标识均由 `tools/xm/` 下的离线流水线生成，记录见 `assets/data/xm_assets_manifest.json`、`assets/fonts/xm_font_manifest.json` 与 `assets/data/FORMAT.txt`。

### 构建标识（build id）的发布口径

`tools/xm/gen_build_id.py` 取当前 git 短哈希，脏工作区追加 `-dirty`。脏判定用 `git status --porcelain`（覆盖未跟踪文件），不能改用 `git diff --quiet`——本项目主体文件曾长期处于未跟踪状态，只看 diff 会给出错误的「干净」信号。

**发布包以「功能提交」的哈希为 build id，纯元数据提交（刷新 build id、文档）不刷 build id。** 原因是 build id 写入文件、提交该文件又会推进 HEAD，形成自指循环：提交 → 跑生成器 → id 落后一格 → 再提交 → 无限。所以正确做法是让 id 固定指向包含全部功能代码的那笔提交，元数据提交不再改动它。

## 本次明确不实现

P1：梅花易数、择日、长按连续滚动、字号切换、空闲背光。P2：语音/TTS、BLE、深睡、横屏、云端解读、OTA 与历史导出。上述项目只作记录，不提供假入口。

未经用户明确同意，不执行任何烧录、USB/串口写入或设备数据清除操作。主机预览图与固件编译通过均不等于真机中文显示验收；未授权刷机时 C7/C8 记为 `Unverified`。
