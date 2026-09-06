# Unit testing with Unity

Vigilant Engine has a standalone host test project using
[ThrowTheSwitch Unity](https://github.com/ThrowTheSwitch/Unity), the C unit testing
framework also used by ESP-IDF. It builds the real I2C wrapper source with a small
fake ESP-IDF driver, so the tests run on your computer without an ESP32, an I2C
device, ESP-IDF, Node.js, or the web/recovery firmware builds.

## Run the tests

Install a C compiler, CMake 3.20 or newer, Make (or Ninja), and Git.
On Arch Linux / EndeavourOS:

```sh
sudo pacman -S --needed base-devel cmake git
```

From the repository root:

```sh
cmake -S tests/host -B build/tests-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tests-host --parallel
ctest --test-dir build/tests-host --output-on-failure --no-tests=error
```

The first configure downloads Unity v2.6.1 pinned to commit
`cbcd08fa7de711053a3deec6339ee89cad5d2697`. Subsequent builds reuse it.
CTest runs the I2C test executable; run `./build/tests-host/test_i2c` directly
to see each Unity case and the summary. Failures return a nonzero exit status
to CTest and CI. The I2C test runner is intentionally empty so new cases can be
written from scratch.

For an offline build, point to an existing Unity checkout, or ESP-IDF's initialized
Unity submodule. The chosen checkout supplies the Unity version in this mode:

```sh
cmake -S tests/host -B build/tests-host \
  -DVE_UNITY_SOURCE_DIR="$IDF_PATH/components/unity/unity"
cmake --build build/tests-host --parallel
ctest --test-dir build/tests-host --output-on-failure --no-tests=error
```

Use the inner `unity` directory containing `src/unity.c`, not ESP-IDF's outer
Unity component directory.

## Scope and boundaries

The fake captures driver arguments and provides configurable responses. Its
headers live only in `tests/host/fakes`; they must never be included by firmware
targets. Each test resets both wrapper state and fake state.

These tests verify wrapper logic. They do not verify electrical behavior, actual
ESP-IDF driver execution, scheduling, Wi-Fi, HTTP, OTA, or status LEDs. The existing
firmware build workflow remains responsible for compiling against ESP-IDF.
The fake API subset follows ESP-IDF v6.0, matching the firmware CI version; review
it when changing the SDK or adding driver calls.

## Sanitizers and CI

On a GCC/Clang host with sanitizer runtimes installed:

```sh
cmake -S tests/host -B build/tests-host -DVE_TEST_SANITIZERS=ON
cmake --build build/tests-host --parallel
ctest --test-dir build/tests-host --output-on-failure --no-tests=error
```

The `Unity unit tests` GitHub Actions workflow runs this configuration on pushes
and pull requests, independently of frontend and firmware jobs.

## Add a test

Add a `static void test_...(void)` function to `tests/host/test_i2c.c`, then add
`RUN_TEST(test_...);` inside `main`. Arrange fake-driver inputs through
`fake_i2c`, call one public I2C behavior, and check its result and relevant
driver interaction with Unity assertions. For example, set
`fake_i2c.create_result` to control bus creation or inspect
`fake_i2c.create_calls` afterward. `setUp` and `tearDown` already isolate the
wrapper and fake state between cases. Do not copy the production implementation
into tests.

Prefer one behavior or scenario per test, not one test containing the entire I2C
API. A public function will commonly need several cases (success, invalid input,
driver failure, boundary behavior). Small helper functions such as
`i2c_read_reg8` can be tested separately when they have a distinct contract, but
you do not need duplicate tests for behavior already fully exercised through the
underlying multi-byte function.

For another module, add a separate executable and `add_test` entry to
`tests/host/CMakeLists.txt`, compiling that module's production source with only
the fakes it needs. Keep module-specific fixtures in that executable.

## Tests running on ESP32

ESP-IDF also provides on-device Unity tests with `TEST_CASE`, a component `test`
directory, and a test application. This host suite uses upstream Unity's
`RUN_TEST`; it is not an on-device test app. For tests involving real peripherals,
follow [ESP-IDF's unit testing guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/unit-tests.html)
and use the actual ESP-IDF drivers rather than the host fake headers.
