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
CTest reports one executable containing 22 Unity cases; run
`./build/tests-host/test_i2c` to see each case and the Unity summary.
Failures return a nonzero exit status to CTest and CI.

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

## Coverage and boundaries

The suite exercises the public API in
`components/vigilant_engine/src/i2c.c`:

- Bus configuration, repeated initialization, failed initialization and retry.
- Scanning addresses `0x03` through `0x77`, cached results, the 16-device limit,
  and bounded output buffers.
- Device address and speed configuration, addition and removal failures.
- Register reads, register-prefixed writes, single-byte helpers, zero-length
  transfers, invalid arguments, and driver error propagation.
- WHOAMI success, mismatch, and read failure.
- Deinitialization, cache clearing, and failed deletion retries.

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

Add a `static void test_...(void)` function to `tests/host/test_i2c.c`, assert the
expected public behavior with Unity macros, and register it with `RUN_TEST` in
`main`. Use `fake_i2c` to set driver results and inspect calls. Do not copy the
production implementation into tests.

For another module, add a separate executable and `add_test` entry to
`tests/host/CMakeLists.txt`, compiling that module's production source with only
the fakes it needs. Keep module-specific fixtures in that executable.

## Tests running on ESP32

ESP-IDF also provides on-device Unity tests with `TEST_CASE`, a component `test`
directory, and a test application. This host suite uses upstream Unity's
`RUN_TEST`; it is not an on-device test app. For tests involving real peripherals,
follow [ESP-IDF's unit testing guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/unit-tests.html)
and use the actual ESP-IDF drivers rather than the host fake headers.
