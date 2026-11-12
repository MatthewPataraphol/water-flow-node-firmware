idf_component_register(
    SRCS "app_main.c" "flow_state.c" "flow_types.c" "event_storage.c"
         "wifi_manager.c" "wifi_auth.c" "time_sync.c" "mqtt_manager.c" "heartbeat.c"
         "battery.c" "sensor_mock.c" "sensor_uart.c" "sensor_line_parser.c"
         "sensor_accelerometer.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi wpa_supplicant esp_event esp_netif nvs_flash mqtt mbedtls
                  esp_timer esp_driver_uart esp_driver_gpio
)
