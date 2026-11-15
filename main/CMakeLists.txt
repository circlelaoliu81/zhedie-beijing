idf_component_register(
    SRCS "main.c"
         "xm_app.c"
         "xm_input_task.c"
         "xm_ui.c"
         "xm_pages.c"
         "xm_widgets.c"
         "xm_status.c"
         "xm_fonts.c"
         "xm_battery.c"
         "core/xm_huangli.c"
         "core/xm_yijing.c"
         "core/xm_liuyao.c"
         "core/xm_nav.c"
         "core/xm_time.c"
         "core/xm_store.c"
         "core/xm_layout.c"
         "core/xm_fontcov.c"
         "core/xm_vm.c"
         "generated/xm_huangli_data.c"
         "generated/xm_yijing_data.c"
         "generated/xm_tianshi_img_data.c"
    INCLUDE_DIRS "." "core" "generated"
    REQUIRES bsp nvs_flash esp_timer esp_hw_support
)

target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/xm_font_12.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/xm_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/xm_font_20.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/xm_font_24.c"
)
