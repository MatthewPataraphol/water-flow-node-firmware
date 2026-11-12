"""Compile/run actual pipeline C code with deterministic NVS/FreeRTOS fakes."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = shutil.which(os.environ.get("CC", "gcc"))
if not compiler:
    raise SystemExit("Set CC to a GCC-compatible host C compiler or install gcc.")
with tempfile.TemporaryDirectory(prefix="flow-node-tests-") as directory:
    suites = {
        "test_pipeline": ("event_storage.c", "flow_state.c", "flow_types.c", "sensor_line_parser.c"),
        "test_mqtt": ("mqtt_manager.c", "flow_types.c"),
    }
    def run_suite(name, sources, defines=(), test_source=None):
        output = Path(directory) / (name + (".exe" if os.name == "nt" else ""))
        subprocess.run([
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(root / "tests/host/include"), "-I", str(root / "main"),
            *defines,
            str(root / "tests/host" / ((test_source or name) + ".c")),
            *[str(root / "main" / source) for source in sources],
            "-o", str(output),
        ], check=True)
        subprocess.run([str(output)], check=True)

    for name, sources in suites.items():
        run_suite(name, sources)

    run_suite("test_uart_pins", ("sensor_uart.c",), ("-DCONFIG_IDF_TARGET_ESP32=1",))

    auth_profiles = {
        "personal": (),
        "personal_limits": ("TEST_WIFI_PERSONAL_LIMITS",),
        "empty_ssid": ("TEST_WIFI_EMPTY_SSID",),
        "long_ssid": ("TEST_WIFI_LONG_SSID",),
        "enterprise": ("TEST_WIFI_ENTERPRISE",),
        "enterprise_identity": ("TEST_WIFI_ENTERPRISE", "TEST_WIFI_IDENTITY_OVERRIDE"),
        "enterprise_no_username": ("TEST_WIFI_ENTERPRISE", "TEST_WIFI_INVALID_USERNAME"),
        "enterprise_no_password": ("TEST_WIFI_ENTERPRISE", "TEST_WIFI_EMPTY_PASSWORD"),
    }
    for profile, options in auth_profiles.items():
        run_suite(
            "wifi_auth_" + profile, ("wifi_auth.c",),
            ("-include", str(root / "tests/host/include/wifi_auth_test_config.h"),
             *("-D" + option + "=1" for option in options)),
            test_source="test_wifi_auth",
        )
