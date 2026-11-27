idf_component_register(SRCS "echo.c"
                            "lab_protocol.c"
                            "lab_store.c"
                            "thingspeak.c"
                            "discord_bot.c"
                            "zigbee_gateway.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_http_client nvs_flash esp_wifi esp_netif esp_event esp_timer
                                  esp_coex ieee802154 esp_hw_support lwip)
