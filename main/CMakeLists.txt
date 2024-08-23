idf_component_register(SRCS "BLE.c" "screen.c" "BLE_AT2.c" "ota.c" "battery.c" "blink.c" "BLE_AT2.c" "fan.c" "flame_thrower.c" "wifi.c" "mqtt.c" "openlid.c" "http_log.c" "screen.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash bt driver mqtt esp_http_client esp_adc esp_https_ota)
