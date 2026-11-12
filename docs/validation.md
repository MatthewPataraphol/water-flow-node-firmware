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

## Wi-Fi authentication update (November 10, 2026)

Added compile-time selection of WPA/WPA2 Personal or WPA2-Enterprise
PEAP/MSCHAPv2. Personal remains the default and keeps the existing SSID/password
symbols. No real campus account credentials are present in source, documentation,
configuration templates or tests. Build configurations/output remain ignored.

New menuconfig settings under **Water flow node**:

| Symbol | Purpose |
| --- | --- |
| `CONFIG_FLOW_WIFI_AUTH_PERSONAL` | Default Personal choice |
| `CONFIG_FLOW_WIFI_AUTH_ENTERPRISE` | Enterprise choice; enables required SDK supplicant/TLS support |
| `CONFIG_FLOW_WIFI_ENTERPRISE_USERNAME` | Inner account username; empty default |
| `CONFIG_FLOW_WIFI_ENTERPRISE_IDENTITY` | Optional outer identity; blank uses username |
| `CONFIG_FLOW_WIFI_ENTERPRISE_PASSWORD` | Account password; empty default |
| `CONFIG_FLOW_WIFI_ENTERPRISE_SERVER_NAME` | Optional certificate hostname; empty default |

`CONFIG_FLOW_WIFI_SSID` is shared. `CONFIG_FLOW_WIFI_PASSWORD` is visible in
Personal mode. Switching modes requires rebuilding/restarting firmware; Kconfig
can remove hidden mode-specific settings when saving the other mode.

The Enterprise setup uses `esp_eap_client.h` from the explicit `wpa_supplicant`
component dependency. Exact APIs:

- `esp_eap_client_set_eap_methods(ESP_EAP_TYPE_PEAP)`
- `esp_eap_client_set_identity()`
- `esp_eap_client_set_username()`
- `esp_eap_client_set_password()`
- `esp_eap_client_use_default_cert_bundle(true)`
- `esp_eap_client_set_domain_name()` (configured hostname, otherwise `NULL`)
- `esp_wifi_sta_enterprise_enable()`
- `esp_wifi_sta_enterprise_disable()` on setup failure to clear partial settings

The installed IDF 5.5.6 supplicant defaults PEAP's inner authentication to
MSCHAPv2 (`components/wpa_supplicant/src/eap_peer/eap.c`). The TTLS phase-2 API
is not used to configure PEAP. The station remains `WIFI_MODE_STA`; Enterprise
sets `WIFI_AUTH_WPA2_ENTERPRISE` as the scan threshold and keeps the PSK/password
field empty. EAP setup runs after `esp_wifi_set_config()` and before
`esp_wifi_start()`. Both modes retain `WIFI_STORAGE_RAM`, existing reconnection,
IP events, RSSI APIs and SNTP startup. The public Wi-Fi API, MQTT, SNTP, flow/NVS,
heartbeat and mock-sensor implementations are unchanged.

Trust checks use the built-in CA bundle. A configured name adds hostname
validation; with a blank name only the CA chain is checked. ESP-IDF's hostname
API is not an Android-style domain-suffix configuration. Obtain the correct
authentication-server name/trust requirements from network IT. This update
does not certify a live PAL3.0 connection. No board was flashed or accessed.

`python tests/run_host_tests.py` passed the original **11 pipeline/MQTT groups**
and **8 authentication profiles**, all compiled with `-Wall -Wextra -Werror`.
The new tests check Personal behavior and maximum fields, empty/oversize SSIDs,
Enterprise identity fallback/override, a password longer than the PSK field,
empty credential rejection, trust API setup, and cleanup after every failing
EAP setup step. These fakes do not perform an actual EAP/TLS handshake.

Both ESP32 builds completed with exit **0**, `Project build complete.`, and no
compiler or CMake warnings in their final logs:

| Wi-Fi mode | Application bytes | Application partition free |
| --- | --- | --- |
| WPA/WPA2 Personal | 963120 (`0xeb230`) | 54% of 2 MB |
| WPA2-Enterprise | 998048 (`0xf3aa0`) | 52% of 2 MB |

Inspection of `wifi_auth_apply()` in the Personal ELF confirmed it returns
`ESP_OK` without EAP calls. The Enterprise ELF includes all eight EAP setup and
cleanup APIs listed above. Both profiles retain mock sensing and the full
ThingsBoard TLS implementation; no physical connectivity was tested.

Builds use ESP-IDF 5.5.6, target `esp32`, 8 MB flash, the existing partition
layout and synthetic compile-only credentials. Host commit-memory pressure
required sequential builds with `IDF_PY_BUILD_JOBS=1` after parallel attempts
ran out of host memory. Current local configurations are `sdkconfig.local.wifi-personal`
and `sdkconfig.local.wifi-enterprise`; the Enterprise fixture defaults are in
ignored `sdkconfig.local.wifi-enterprise.defaults`. Real local `sdkconfig` is
not replaced by these validation profiles.

Commands in the exported ESP-IDF shell (PowerShell; the local Enterprise defaults
file contains synthetic fixtures and is intentionally not in Git):

```powershell
$env:IDF_PY_BUILD_JOBS = "1"
idf.py -B build-wifi-personal "-DSDKCONFIG=sdkconfig.local.wifi-personal" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ci" "-DIDF_TARGET=esp32" build
idf.py -B build-wifi-enterprise "-DSDKCONFIG=sdkconfig.local.wifi-enterprise" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ci;tests/sdkconfig.enterprise;sdkconfig.local.wifi-enterprise.defaults" "-DIDF_TARGET=esp32" build
```

Final ignored logs are `build-wifi-personal-validation.log` and
`build-wifi-enterprise-validation.log`. To configure a real account, use local
menuconfig as documented in the README, rather than modifying portable fixtures.

Changed files: `main/wifi_auth.c`, `main/wifi_auth.h`, `main/wifi_manager.c`,
`main/CMakeLists.txt`, `main/Kconfig.projbuild`, `README.md`, `docs/validation.md`,
`tests/run_host_tests.py`, `tests/host/test_wifi_auth.c`,
`tests/host/include/esp_wifi.h`, `tests/host/include/esp_eap_client.h`,
`tests/host/include/wifi_auth_test_config.h`, and `tests/sdkconfig.enterprise`.
