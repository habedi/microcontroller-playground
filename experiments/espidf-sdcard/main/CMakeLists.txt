idf_component_register(
    SRCS "main.c" "editor.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_gpio fatfs sdmmc console esp_wifi esp_netif esp_event nvs_flash lwip littlefs
)
