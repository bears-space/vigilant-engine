cmake_minimum_required(VERSION 3.20)
project(vigilant_engine_unit_tests LANGUAGES C)

enable_testing()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# This is a standalone host project: never include the firmware's CMakeLists.txt.
# A local Unity checkout (including ESP-IDF's components/unity/unity) works offline.
set(VE_UNITY_SOURCE_DIR "" CACHE PATH "Unity checkout containing src/unity.c")
option(VE_TEST_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
if(NOT VE_UNITY_SOURCE_DIR)
    include(FetchContent)
    FetchContent_Declare(unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
        GIT_TAG cbcd08fa7de711053a3deec6339ee89cad5d2697 # v2.6.1
    )
    FetchContent_MakeAvailable(unity)
else()
    if(NOT EXISTS "${VE_UNITY_SOURCE_DIR}/src/unity.c")
        message(FATAL_ERROR "VE_UNITY_SOURCE_DIR must contain src/unity.c")
    endif()
    add_library(unity STATIC "${VE_UNITY_SOURCE_DIR}/src/unity.c")
    target_include_directories(unity PUBLIC "${VE_UNITY_SOURCE_DIR}/src")
endif()

add_executable(test_i2c
    test_i2c.c
    fakes/fake_i2c.c
    ../../components/vigilant_engine/src/i2c.c
)
target_include_directories(test_i2c PRIVATE
    fakes
    ../../components/vigilant_engine/include
)
target_link_libraries(test_i2c PRIVATE unity)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_i2c PRIVATE -Wall -Wextra -Werror)
endif()
if(VE_TEST_SANITIZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "VE_TEST_SANITIZERS requires GCC or Clang")
    endif()
    foreach(test_target IN ITEMS test_i2c unity)
        target_compile_options(${test_target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    endforeach()
    target_link_options(test_i2c PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME vigilant_engine.i2c COMMAND test_i2c)
set_tests_properties(vigilant_engine.i2c PROPERTIES TIMEOUT 10)
