set(requires esp-tls nvs_flash esp_netif esp_http_server)
idf_build_get_property(target IDF_TARGET)

if(${target} STREQUAL "linux")
    list(APPEND requires esp_stubs protocol_examples_common)
else()
    list(APPEND requires esp_wifi esp_eth)
endif()

idf_component_register(
    SRCS
        "src/http_server.c"
        "src/ota_http.c"
        "src/vigilant.c"
        "src/status_led.c"
        "src/websocket.c"
    INCLUDE_DIRS
        "include"
    EMBED_FILES
        "static/vigilant.html"
    REQUIRES
        ${requires}
        app_update
)
