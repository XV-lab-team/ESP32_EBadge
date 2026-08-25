# LVGL 9.5.0 自带 CJK 字库仍写已删除的 cache 字段，开 SOURCE_HAN_SANS_SC_16_CJK 会编不过。
# 构建时剥掉即可。界面主字库是 ui_font_zh_16.c（4bpp 子集），这份 CJK 只做 fallback。
set(_ui_cjk "${CMAKE_SOURCE_DIR}/managed_components/lvgl__lvgl/src/font/lv_font_source_han_sans_sc_16_cjk.c")
if(EXISTS "${_ui_cjk}")
    file(READ "${_ui_cjk}" _ui_cjk_src)
    if(_ui_cjk_src MATCHES "lv_font_fmt_txt_glyph_cache_t")
        string(REPLACE "static  lv_font_fmt_txt_glyph_cache_t cache;\r\n" "" _ui_cjk_src "${_ui_cjk_src}")
        string(REPLACE "static  lv_font_fmt_txt_glyph_cache_t cache;\n" "" _ui_cjk_src "${_ui_cjk_src}")
        string(REPLACE "    .cache = &cache\r\n" "" _ui_cjk_src "${_ui_cjk_src}")
        string(REPLACE "    .cache = &cache\n" "" _ui_cjk_src "${_ui_cjk_src}")
        file(WRITE "${_ui_cjk}" "${_ui_cjk_src}")
        message(STATUS "LVGL CJK 16: stripped stale font cache (LVGL 9.5)")
    endif()
endif()

idf_component_register(SRCS "main.c" "lcd_test.c" "lvgl_app.c" "ui_usb_mode.c" "ui_font_zh_16.c"
                            "led_script.c" "usb_msc.c"
                    INCLUDE_DIRS "."
                    REQUIRES BSP letter_shell esp_lvgl_port nvs_flash os_monitor esp_tinyusb usb)
