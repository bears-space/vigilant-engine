idf_component_register(
    SRCS "recovery_app.c"
    INCLUDE_DIRS "."
    EMBED_FILES 
        "static/index.html"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server app_update esp_partition
)

if(TARGET ve_web)
  add_dependencies(${COMPONENT_LIB} ve_web)
endif()
