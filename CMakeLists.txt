cmake_minimum_required(VERSION 3.16)

#Treats warnings as errors
add_compile_options("-Werror")

# Allow CI to supply prebuilt web assets while keeping local builds automatic.
option(VE_BUILD_WEB "Build the Vigilant UI with npm when the embedded HTML is missing" ON)
set(VE_VIGILANT_HTML "" CACHE FILEPATH "Path to a prebuilt vigilant.html file for firmware builds")

if(NOT DEFINED ENV{IDF_PATH} OR "$ENV{IDF_PATH}" STREQUAL "")
  message(FATAL_ERROR
    "IDF_PATH is not set. Open the project from an exported ESP-IDF shell or use the VS Code ESP-IDF extension. "
    "Generic CMake Tools launches do not load the ESP-IDF environment automatically.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
# ESP-IDF evaluates component CMake files once before idf_component_register(); use
# build properties so component logic can still read these values during that pass.
idf_build_set_property(VE_BUILD_WEB "${VE_BUILD_WEB}")
idf_build_set_property(VE_VIGILANT_HTML "${VE_VIGILANT_HTML}")
project(vigilant-engine)
