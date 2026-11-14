idf_component_register(
    SRCS "zigbee_router.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash spiffs esp_timer
)

