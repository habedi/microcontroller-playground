idf_component_register(
    SRCS "main.c" "editor.c" "path_util.c" "wget.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_gpio fatfs sdmmc console esp_wifi esp_netif esp_event nvs_flash lwip littlefs esp_psram app_update esp_hw_support retawq esp_http_client
)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=write")
