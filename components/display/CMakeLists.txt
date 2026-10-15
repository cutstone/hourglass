idf_component_register(SRCS "display.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES st7305 esp_driver_spi)
