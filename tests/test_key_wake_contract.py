#!/usr/bin/env python3
"""Static contracts for the Xuanmen application entry (main/main.c).

玄门刻意简化上游 FoloToy 基线：移除 DEMOS 菜单、ui_pixel 外壳与深睡唤醒
（关于页明示“不启用深睡、默认常亮”）。因此原先针对 main.c 中 on_key /
key_guard_consume 的深睡唤醒契约已随该功能一并移除，此处改为守护玄门入口
实际必须满足的生命周期契约：
  · 初始化应用状态、渲染首屏、启动输入任务；
  · 明确不调用 esp_deep_sleep_start（保持常亮，避免误入睡死）；
  · 启动时打印可追溯构建标识；首屏渲染在 LVGL 锁内完成。
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", source)
    if not match:
        raise AssertionError(f"function not found: {name}")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1:index]
    raise AssertionError(f"function is unterminated: {name}")


class MainEntryContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.main = read("main/main.c")

    def test_app_main_exists(self) -> None:
        self.assertTrue(re.search(r"\bvoid\s+app_main\s*\(\s*void\s*\)", self.main),
                        "main.c 必须提供 app_main(void) 入口")

    def test_app_main_wires_lifecycle(self) -> None:
        body = function_body(self.main, "app_main")
        # 初始化 → 渲染首屏 → 启动输入任务，三者缺一不可。
        self.assertIn("xm_app_init(", body)
        self.assertIn("xm_app_render(", body)
        self.assertIn("xm_ui_apply_locked(", body)
        self.assertIn("xm_input_task_start(", body)

    def test_deep_sleep_is_intentionally_absent(self) -> None:
        # 玄门默认常亮，不进入深睡；入口若出现深睡调用即为回归。
        self.assertNotIn("esp_deep_sleep_start", self.main)
        self.assertNotIn("bsp_audio_prepare_deep_sleep", self.main)

    def test_build_identity_is_logged(self) -> None:
        self.assertIn("xm_log_build_identity(", self.main)
        self.assertRegex(self.main, r"XM_BUILD_ID")

    def test_first_screen_render_is_locked(self) -> None:
        # 首屏必须在 LVGL 锁内渲染并应用，避免与输入任务并发改 UI。
        body = function_body(self.main, "app_main")
        self.assertLess(body.index("bsp_lvgl_lock("),
                        body.index("xm_ui_apply_locked("))


if __name__ == "__main__":
    unittest.main()
