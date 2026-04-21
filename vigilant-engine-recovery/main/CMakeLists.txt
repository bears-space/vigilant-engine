get_filename_component(VE_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(VE_RECOVERY_HTML "${VE_REPO_ROOT}/build/static/recovery/index.html")

idf_component_register(
    SRCS "recovery_app.c"
    INCLUDE_DIRS "."
    EMBED_FILES 
        "${VE_RECOVERY_HTML}"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server app_update esp_partition
)

if(TARGET ve_web)
  add_dependencies(${COMPONENT_LIB} ve_web)
endif()
