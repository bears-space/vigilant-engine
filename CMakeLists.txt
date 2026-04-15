cmake_minimum_required(VERSION 3.16)

#Treats warnings as errors
add_compile_options("-Werror")

# Allow CI to supply prebuilt web assets while keeping local builds automatic.
option(VE_BUILD_WEB "Build the Vigilant UI with npm when the embedded HTML is missing" ON)
set(VE_VIGILANT_HTML "" CACHE FILEPATH "Path to a prebuilt vigilant.html file for firmware builds")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(vigilant-engine)
