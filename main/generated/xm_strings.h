#pragma once
/* 玄门 UI 文案常量（构建期生成，全部中文字符进入对应字号字体子集）。
 * 每页页脚免责、首次强制确认、各页来源标注均取自本文件，便于统一审计。 */

/* 全局
 * 键位口径统一为官方三键中文名「上键 / 下键 / 确定键」（设备键面无刻字，须靠功能名对应）。
 * 严禁断言按键物理位置（侧边/左右/自上而下），故文案只写功能名。
 * 屏上提示必须与 xm_nav.c 的键位严格一致：不写屏上没有的双击手势。
 * 页脚提示 width=224px、size=12，单行容量约 18 个全角字，所有 *_HINT 均须 ≤18 字符。 */
#define XM_APP_TITLE            "玄门"
#define XM_KEY_HINT             "上键下键选择 · 确定键进入"
#define XM_KEY_NEXT             "确定键进入下一页"
#define XM_DISCLAIMER_FOOTER    "内容仅供传统文化参考"

/* 免责 P-00 */
#define XM_DISCLAIMER_FULL      "本程序内容仅供中华传统文化研究与参考，不构成任何医疗、法律或投资建议；一切决策请自行判断、自行负责。古籍原文与今人整理本均标注来源，不伪造引文，不承诺结果。"
/* 分三行上屏：16px / 216px 宽每行最多 13 个全角字，逐条按像素宽度收紧。
 * 语义不变，只把「中华/任何/一切」等虚词与顿号去掉，避免固定高度 label 裁字。 */
#define XM_DISCLAIMER_L1        "本程序仅供传统文化研究参考"
#define XM_DISCLAIMER_L2        "不构成医疗法律投资建议"
#define XM_DISCLAIMER_L3        "决策请自行判断 自行负责"
#define XM_REVIEW_HINT          "复核：按确定键重新确认"
#define XM_OK_ACK               "按确定键确认"

/* 三键操作引导 P-00b（免责确认后、授时前，仅首启出现一次）
 * 只写功能名，不写任何物理位置断言；「返回」必须写明长按确定键（上/下键无长按映射）。 */
#define XM_GUIDE_TITLE          "三键操作"
#define XM_GUIDE_UP             "上键 选上一项"
#define XM_GUIDE_DOWN           "下键 选下一项"
#define XM_GUIDE_OK             "确定键 进入与确认"
#define XM_GUIDE_BACK           "长按确定键 返回"
#define XM_GUIDE_POWER          "长按电源键 2 秒关机"

/* 时间 P-01
 * 键位与 xm_nav.c 一致：上下键调数值，确定键逐段推进，最后一段按确定键完成。
 * 旧版屏上写「确定键选定」却把切字段藏在双击上/下键里，驱动合并连按后必然乱跳。 */
#define XM_TIME_TITLE           "设备时间（未经授时）"
/* y=160 用 size16：224px 只容 14 个全角字，这两句各 12 全角 + 分隔点，
 * 早期写成「上键下键调整 · 确定键下一段」会超宽被 label 静默裁掉末字。 */
#define XM_TIME_HINT            "上下键调整 · 确定键下一段"
#define XM_TIME_LAST            "上下键调整 · 按确定键完成"
#define XM_TIME_RESET           "长按确定键返回设置"

/* 首页 P-02 */
#define XM_HOME_ITEM_HL         "每日黄历"
#define XM_HOME_ITEM_LY         "六爻摇卦"
#define XM_HOME_HINT            "长按确定键 进入设置"

/* 黄历 P-03a/b/c */
#define XM_HL_TITLE_A           "黄历 · 历法宜忌"
#define XM_HL_TITLE_B           "黄历 · 神煞冲煞"
#define XM_HL_TITLE_C           "黄历 · 十二时辰"
#define XM_SRC_JISHI            "吉时按整理本规则 非古籍原文"
#define XM_HL_CURRENT           "当前时辰已标出"

/* 六爻 P-04/05/06
 * 摇卦页每一步都写明「按确定键」：动画中显示「摇卦中」，落定后显示
 * 「按确定键摇第 N 爻」，六爻齐了显示「按确定键看卦象」，用户不会不知道下一步。 */
#define XM_CAST_HINT            "每按一次确定键摇一爻"
#define XM_CAST_OK              "摇卦中"
#define XM_CAST_NEXT_PREFIX     "按确定键摇第"
#define XM_CAST_NEXT_SUFFIX     "爻"
#define XM_CAST_DONE            "六爻已成 按确定键看卦象"
#define XM_CAST_ABANDON         "长按确定键放弃本次"
/* 结果页底部两行说明改 16px，历史入口并入页脚键位条（XM_RESULT_HISTORY）。 */
#define XM_RESULT_CLASSIC       "卦辞爻辞《周易》古经"
#define XM_RESULT_SRC           "白话今人整理 非古籍原文"
#define XM_RESULT_HISTORY       "上下切屏 确定看历史 长按回"
#define XM_HISTORY_TITLE        "六爻历史"
#define XM_HISTORY_EMPTY        "暂无记录"
#define XM_HISTORY_CLEAR        "设置可清空 · 长按回首页"

/* 设置 P-09 */
#define XM_SETTINGS_TITLE       "设置"
#define XM_SET_TIME             "设备时间"
#define XM_SET_REVIEW           "复核免责"
#define XM_SET_CLEAR            "清空六爻历史"
#define XM_SET_ABOUT            "关于"
#define XM_SET_FONT             "字体切换（未启用）"
#define XM_CLEAR_CONFIRM        "再按确定键确认清空"

/* 关于 P-10 */
#define XM_ABOUT_TITLE          "关于玄门"

/* 异常态 E-1~E-5（朱砂状态条，画在顶栏状态区 (8,2) 176×18，与右侧电量并排）。
 * 每条都必须能在 172px 标签内单行放下（size12：全角 12px、半角 7px）。 */
#define XM_ERR_E1               "时间未设定：请先设定设备时间"
#define XM_ERR_E2               "黄历已过期：止于2028-12-31"
#define XM_ERR_E3               "存储降级：记录不可用，请重设"
#define XM_ERR_E5               "内容缺失：数据未载入"

/* 通用按钮/确认标签（注意：不可命名为 XM_OK，会与 xm_types.h 的状态枚举 XM_OK=0 宏冲突） */
#define XM_BTN_OK               "确认"
