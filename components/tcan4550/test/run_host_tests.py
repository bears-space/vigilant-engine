#!/usr/bin/env python3
# SPDX-AI-Disclosure: ai-generated
# SPDX-AI-Model: gpt-6
# SPDX-AI-Provider: OpenAI
# SPDX-AI-Scope: Host regression-test build and execution runner.
# SPDX-AI-Date: 2026-10-13

"""Compile the actual port and TI core against a register-level SPI mock."""
import os
from pathlib import Path
import subprocess
import tempfile

component = Path(__file__).resolve().parents[1]
headers = [
    "esp_err.h", "esp_check.h", "esp_log.h", "esp_heap_caps.h",
    "driver/gpio.h", "driver/spi_master.h", "freertos/FreeRTOS.h",
    "freertos/semphr.h", "freertos/task.h",
]
with tempfile.TemporaryDirectory(prefix="tcan4550-test-") as directory:
    root = Path(directory)
    for header in headers:
        target = root / header
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#include "host_idf.h"\n')
    binary = root / "test_port"
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=gnu11", "-g",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I" + str(root), "-I" + str(component / "test"),
        "-I" + str(component / "include"), "-I" + str(component / "ti"),
        str(component / "test/test_port.c"), str(component / "tcan4550.c"),
        str(component / "tcan4550_spi.c"), str(component / "ti/TCAN4550.c"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
    # Check that both the compatibility API and TI declarations work in C++.
    smoke = root / "headers.cpp"
    smoke.write_text('#include "tcan4550.h"\n#include "tcan4550_ti.h"\n')
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-fsyntax-only",
        "-I" + str(root), "-I" + str(component / "test"),
        "-I" + str(component / "include"), "-I" + str(component / "ti"),
        str(smoke),
    ], check=True)
