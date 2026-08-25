set(src_dirs
            LED
            LCD
            EXIO
            KEY
            SDMMC
            SDMMC_FAT)

set(include_dirs
            LED
            LCD
            EXIO
            KEY
            SDMMC
            SDMMC_FAT)

set(requires
            driver
            esp_lcd
            esp_lcd_st77916
            log
            freertos
            sdmmc
            fatfs
            esp_driver_sdmmc)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})           
component_compile_options(-ffast-math -O3 -Wno-error=format=-Who-format)                        
