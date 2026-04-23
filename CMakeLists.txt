cmake_minimum_required(VERSION 3.16)

#Treats warnings as errors
add_compile_options("-Werror")

# Allow CI to supply prebuilt web assets while keeping local builds automatic.
option(VE_BUILD_WEB "Build the Vigilant UI with npm when the embedded HTML is missing" ON)
set(VE_VIGILANT_HTML "" CACHE FILEPATH "Path to a prebuilt vigilant.html file for firmware builds")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
# ESP-IDF evaluates component CMake files once before idf_component_register(); use
# build properties so component logic can still read these values during that pass.
idf_build_set_property(VE_BUILD_WEB "${VE_BUILD_WEB}")
idf_build_set_property(VE_VIGILANT_HTML "${VE_VIGILANT_HTML}")
project(vigilant-engine)

idf_build_get_property(VE_BUILD_DIR BUILD_DIR)
idf_build_get_property(VE_PROJECT_BIN PROJECT_BIN)
idf_build_get_property(VE_PYTHON PYTHON)
idf_build_get_property(VE_IDF_TARGET IDF_TARGET)
idf_build_get_property(VE_IDF_PATH IDF_PATH)

partition_table_get_partition_info(VE_MAIN_APP_OFFSET "--partition-name ota_0" "offset")
partition_table_get_partition_info(VE_RECOVERY_APP_OFFSET "--partition-name factory" "offset")

if(NOT VE_MAIN_APP_OFFSET)
    message(FATAL_ERROR "Could not find ota_0 partition offset for main firmware flashing.")
endif()

if(NOT VE_RECOVERY_APP_OFFSET)
    message(FATAL_ERROR "Could not find factory partition offset for recovery firmware flashing.")
endif()

set(VE_RECOVERY_PROJECT_DIR "${CMAKE_SOURCE_DIR}/vigilant-engine-recovery")
set(VE_RECOVERY_BIN "${VE_RECOVERY_PROJECT_DIR}/build/vigilant-engine-recovery.bin")
set(VE_RECOVERY_PARTITIONS_CSV "${VE_RECOVERY_PROJECT_DIR}/partitions.csv")
get_filename_component(VE_PYTHON_BIN_DIR "${VE_PYTHON}" DIRECTORY)
get_filename_component(VE_PYTHON_ENV_PATH "${VE_PYTHON_BIN_DIR}" DIRECTORY)
get_filename_component(VE_TOOLCHAIN_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)

set(VE_ESP_IDF_VERSION "$ENV{ESP_IDF_VERSION}")
if(NOT VE_ESP_IDF_VERSION)
    set(VE_ESP_IDF_VERSION "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}")
endif()
set(VE_IDF_TOOLS_PATH "$ENV{IDF_TOOLS_PATH}")
if(NOT VE_IDF_TOOLS_PATH)
    set(VE_IDF_TOOLS_PATH "$ENV{HOME}/.espressif")
endif()
set(VE_ESP_ROM_ELF_DIR "$ENV{ESP_ROM_ELF_DIR}")

function(ve_remove_default_project_bin target_name)
    if(NOT TARGET ${target_name})
        return()
    endif()

    foreach(property_name IN ITEMS IMAGES ENCRYPTED_IMAGES NON_ENCRYPTED_IMAGES FLASH_FILE FLASH_ENTRY)
        get_property(property_values TARGET ${target_name} PROPERTY ${property_name})
        set(filtered_values)

        foreach(property_value IN LISTS property_values)
            string(FIND "${property_value}" "${VE_PROJECT_BIN}" project_bin_index)
            if(project_bin_index EQUAL -1)
                list(APPEND filtered_values "${property_value}")
            endif()
        endforeach()

        set_property(TARGET ${target_name} PROPERTY ${property_name} "${filtered_values}")
    endforeach()
endfunction()

ve_remove_default_project_bin(app-flash)
ve_remove_default_project_bin(flash)
ve_remove_default_project_bin(encrypted-app-flash)
ve_remove_default_project_bin(encrypted-flash)

esptool_py_flash_target_image(app-flash app "${VE_MAIN_APP_OFFSET}" "${VE_BUILD_DIR}/${VE_PROJECT_BIN}")
esptool_py_flash_target_image(flash recovery "${VE_RECOVERY_APP_OFFSET}" "${VE_RECOVERY_BIN}" ALWAYS_PLAINTEXT)
esptool_py_flash_target_image(flash app "${VE_MAIN_APP_OFFSET}" "${VE_BUILD_DIR}/${VE_PROJECT_BIN}")

add_custom_target(ve_recovery_build
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/partitions.csv"
            "${VE_RECOVERY_PARTITIONS_CSV}"
    COMMAND "${CMAKE_COMMAND}" -E env
            "IDF_PATH=${VE_IDF_PATH}"
            "IDF_TOOLS_PATH=${VE_IDF_TOOLS_PATH}"
            "IDF_PYTHON_ENV_PATH=${VE_PYTHON_ENV_PATH}"
            "ESP_IDF_VERSION=${VE_ESP_IDF_VERSION}"
            "ESP_ROM_ELF_DIR=${VE_ESP_ROM_ELF_DIR}"
            "PATH=${VE_TOOLCHAIN_BIN_DIR}:$ENV{PATH}"
            "${VE_PYTHON}" "${VE_IDF_PATH}/tools/idf.py"
            -C "${VE_RECOVERY_PROJECT_DIR}"
            -D "IDF_TARGET=${VE_IDF_TARGET}"
            -D "VE_BUILD_WEB=${VE_BUILD_WEB}"
            reconfigure build
    BYPRODUCTS "${VE_RECOVERY_BIN}" "${VE_RECOVERY_PARTITIONS_CSV}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM
)

add_dependencies(ve_recovery_build app)
add_dependencies(flash ve_recovery_build)
