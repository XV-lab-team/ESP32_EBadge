set(src_dirs
            LED
            LCD)

set(include_dirs
            LED
            LCD)

set(requires
            driver
            esp_lcd
            esp_lcd_st77916)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})           
component_compile_options(-ffast-math -O3 -Wno-error=format=-Who-format)                        
