set(src_dirs
            LED
            LCD
            EXIO)

set(include_dirs
            LED
            LCD
            EXIO)

set(requires
            driver
            esp_lcd
            esp_lcd_st77916
            log)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})           
component_compile_options(-ffast-math -O3 -Wno-error=format=-Who-format)                        
