idf_component_register(SRCS "display.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd esp_driver_spi
                       PRIV_REQUIRES kevincoooool__esp_lcd_st7305)
