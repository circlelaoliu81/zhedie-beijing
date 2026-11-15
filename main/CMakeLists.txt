# 折叠北京：应用组件。BSP 与 LVGL 复用官方基线，页面与状态机全部自研。
#
# 美术资源走 EMBED_FILES 直接嵌进 rodata：ESP32-C3 的 Flash 是 memory-mapped，
# LVGL 可以直读，因此 240x104 的场景图不占 RAM、不按帧分配。
# 这一点是无 PSRAM 板子的硬约束（整屏 RGB565 画布 153,600 B 申请不到）。
idf_component_register(
    SRCS "main.c"
         "fb_app.c"
         "fb_game.c"
         "fb_save.c"
         "app_persist.c"
         "xm_input_task.c"
         "xm_ui.c"
         "xm_widgets.c"
         "xm_status.c"
         "xm_fonts.c"
         "xm_battery.c"
         "core/xm_layout.c"
         "core/xm_fontcov.c"
         "generated/fb_art_data.c"
    INCLUDE_DIRS "." "core" "generated"
    REQUIRES bsp nvs_flash esp_timer esp_hw_support
    EMBED_FILES
        "../assets/art/intro_room.rgb565"
        "../assets/art/home_street.rgb565"
        "../assets/art/market_stall.rgb565"
        "../assets/art/delivery_road.rgb565"
        "../assets/art/gig_warehouse.rgb565"
        "../assets/art/ride_car.rgb565"
        "../assets/art/live_room.rgb565"
        "../assets/art/report_night.rgb565"
        "../assets/art/event_rain.rgb565"
        "../assets/art/ending_dawn.rgb565"
        "../assets/art/cash.rgb565a8"
        "../assets/art/heart.rgb565a8"
        "../assets/art/bolt.rgb565a8"
        "../assets/art/star.rgb565a8"
        "../assets/art/cal.rgb565a8"
        "../assets/art/work.rgb565a8"
        "../assets/art/rest.rgb565a8"
        "../assets/art/card.rgb565a8"
        "../assets/art/box.rgb565a8"
        "../assets/art/door.rgb565a8"
        "../assets/art/info.rgb565a8"
        "../assets/art/clock.rgb565a8"
)

target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/xm_font_12.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/xm_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/xm_font_20.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/xm_font_24.c"
)
