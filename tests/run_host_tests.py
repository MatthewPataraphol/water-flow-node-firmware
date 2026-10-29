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
    for name, sources in suites.items():
        output = Path(directory) / (name + (".exe" if os.name == "nt" else ""))
        subprocess.run([
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(root / "tests/host/include"), "-I", str(root / "main"),
            str(root / "tests/host" / (name + ".c")),
            *[str(root / "main" / source) for source in sources],
            "-o", str(output),
        ], check=True)
        subprocess.run([str(output)], check=True)
