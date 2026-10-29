# Validation record

Validated October 28, 2026 with the installed ESP-IDF **5.5.6**, its Python 3.13
environment, Xtensa GCC 14.2.0, CMake 3.30.2 and Ninja 1.12.1. All profiles target
**esp32**, use **8 MB** flash, custom `partitions.csv` and project version **0.1.0**.
The ESP-IDF environment included `ESP_ROM_ELF_DIR` from the installed tools.

No physical device was flashed, monitored or otherwise accessed. No Wi-Fi,
ThingsBoard or real sensor connectivity was claimed. Non-secret build fixtures
ensure that Wi-Fi/TLS/MQTT code is compiled and linked, even without real
credentials. The linked network ELF contains `esp_wifi_connect`,
`esp_mqtt_client_enqueue`, `esp_crt_bundle_attach` and `esp_netif_sntp_init`.

## ESP-IDF build results

| Configuration | Exit code | Application bytes | Application partition free |
| --- | --- | --- | --- |
| Mock sensing + mock battery + complete networking | 0 | 962944 (`0xeb180`) | 54% of 2 MB |
| UART sensing + unavailable battery + complete networking | 0 | 983088 (`0xf0030`) | 53% of 2 MB |
| Accelerometer boundary + unavailable battery + complete networking | 0 | 962608 (`0xeb030`) | 54% of 2 MB |

All three ended with `Project build complete.` No compiler warnings, compiler
errors or CMake warnings appear in the final build logs. The mock/network profile
was cleaned with `fullclean` immediately before the final build; the other two
used fresh build directories. The initial safe-default build also passed.

Bootloader in all profiles: `0x6590` bytes, with `0xa70` bytes (9%) free before
the partition table. The event partition is 128 KB at `0x210000`.

Reproduce in an exported ESP-IDF shell (PowerShell syntax):

```powershell
idf.py -B build-network "-DSDKCONFIG=sdkconfig.local.network" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ci" "-DIDF_TARGET=esp32" fullclean build
idf.py -B build-uart "-DSDKCONFIG=sdkconfig.local.uart" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ci;tests/sdkconfig.uart" "-DIDF_TARGET=esp32" build
idf.py -B build-accelerometer "-DSDKCONFIG=sdkconfig.local.accelerometer" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ci;tests/sdkconfig.accelerometer" "-DIDF_TARGET=esp32" build
```

`fullclean` applies only to its selected build directory. Existing local sdkconfig
files override defaults, so use fresh filenames when changing profiles. The CI
SSID/password/token values are deliberately nonfunctional test fixtures. These
build profiles must be reconfigured with real local credentials before use.

Final local logs, intentionally ignored by Git:

- `build-network-validation.log`
- `build-uart-validation.log`
- `build-accelerometer-validation.log`

## Host tests

Command: `python tests/run_host_tests.py`. Exit code **0**. Both executables compile
with GCC and `-std=c11 -Wall -Wextra -Werror` against the actual application C
sources. **All 11 groups passed.**

| Group | Verified behavior |
| --- | --- |
| State changes | Initial baseline, duplicate suppression, captured timestamps, recovered state after drained-queue reboot |
| Persistent capacity/replay | 128 records, no overwrite on full store, ordered replay, slot reuse, sequence and boot counters |
| Storage failure/power loss | Failed event/checkpoint writes retain history; reboot after checkpoint but before deletion preserves state/event |
| Unsynchronized events | Same-boot backdating persists; previous-boot unresolved records remain stored and cannot block newer valid history |
| Invalid records | Incompatible record length fails initialization without erase |
| Sensor backpressure | RAM queue is bounded, submission is nonblocking, rejected states can be retried |
| UART parser | Exact state names, CRLF, byte-at-a-time input, embedded NUL rejection, overlong-line recovery |
| MQTT reconnect | Wi-Fi/SNTP startup gates, no duplicate application enqueue on reconnect, unmatched ACK ignored, matching PUBACK required |
| MQTT retries | Enqueue failure, outbox expiry, ACK arriving before enqueue returns, repeated NVS removal failure without republishing |
| Heartbeats | Offline discard, newest-sample overwrite, QoS0 failed send never retried |
| Callback overflow | Rebuild MQTT client and replay while preserving unsent records |

The NVS fake models atomic individual key writes, not real flash pages, garbage
collection or brownouts. Callback tests exercise deterministic interleavings,
not actual multithreaded scheduling. Radio, DHCP, DNS, SNTP transport, broker TLS
trust, credentials, sensor wiring and ThingsBoard ingestion require subsequent
hardware testing. See the README for those procedures and known limitations.

## Change inventory

- Updated: `CMakeLists.txt`, `main/CMakeLists.txt`, `README.md`,
  `sdkconfig.defaults`, `sdkconfig.ci`.
- Added: `.gitignore`, `partitions.csv`, `main/Kconfig.projbuild`,
  `main/app_main.c`, `docs/validation.md`.
- Added `.c`/`.h` module pairs under `main/`: `flow_types`, `flow_state`,
  `event_storage`, `wifi_manager`, `time_sync`, `mqtt_manager`, `heartbeat`,
  `battery`, `sensor_mock`, `sensor_uart`, `sensor_line_parser`,
  `sensor_accelerometer`.
- Added: `tests/run_host_tests.py`, `tests/sdkconfig.uart`,
  `tests/sdkconfig.accelerometer`, `tests/host/test_pipeline.c`,
  `tests/host/test_mqtt.c`, and minimal test-only API headers under
  `tests/host/include/`.
- Removed obsolete example files: `main/hello_world_main.c`,
  `pytest_hello_world.py`.
- Adjusted ignored local `sdkconfig` to use the custom partition table and MQTT
  deletion notifications. Build-generated local configurations and outputs are
  ignored. Git repository metadata and the existing flash-test artifact remain.
