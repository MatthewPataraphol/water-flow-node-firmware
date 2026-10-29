# Water flow node firmware

Purdue EPICS non-invasive water-flow monitor. The ESP32 manages sensor state,
historical events, Wi-Fi and ThingsBoard Cloud. Sensing technologies report only
`FLOW` or `NO_FLOW`; cloud code is independent of their measurement algorithm.

Uses **ESP-IDF v5.5.6**, C and the supplied FreeRTOS. Target: **esp32**, Adafruit
ESP32 Feather V2, ESP32-PICO-V3-02, 8 MB flash, 2 MB PSRAM. This firmware fits
internal RAM and does not require PSRAM initialization. Battery sensing, the final
MSP430 UART protocol and accelerometer classification are unfinished hardware
interfaces. **Default mock mode generates test data.**

## Architecture and files

```text
mock / UART / future SPI classifier
            |
   reading queue (32, RAM)
            |
   state task: baseline + transition filtering
            |
   persistent NVS event store (128 records)
            |
   telemetry worker --> MQTT QoS 1 --> ThingsBoard
            ^               |
            +--- remove only after PUBACK

heartbeat task --> newest-only RAM mailbox --> MQTT QoS 0
Wi-Fi reconnect task --> IP --> SNTP --> verified TLS MQTT
```

| Files under `main/` (matching `.h` for each `.c`, except `app_main.c`) | Responsibility |
| --- | --- |
| `app_main.c` | Ordered startup, persistent boot identity, visible failures |
| `flow_types.c` | Sensor-independent enum and versioned event type |
| `flow_state.c` | Nonblocking submission, baseline, transition filtering, persistence retry |
| `event_storage.c` | Mutex-protected NVS queue, state recovery, timestamp resolution, PUBACK removal |
| `wifi_manager.c` | Wi-Fi station, reconnect attempts every 5 seconds, IP and RSSI |
| `time_sync.c` | SNTP after IP acquisition, validated epoch/monotonic clock anchor |
| `mqtt_manager.c` | TLS, automatic reconnect, one in-flight event, heartbeat sends |
| `heartbeat.c` | Health collection every 300 seconds by default |
| `battery.c` | Explicit mock or unavailable status; future hardware boundary |
| `sensor_mock.c` | Periodic synthetic transitions |
| `sensor_uart.c`, `sensor_line_parser.c` | UART1 transport and replaceable temporary parser |
| `sensor_accelerometer.c` | Future SPI/classifier boundary; returns `ESP_ERR_NOT_SUPPORTED` |
| `Kconfig.projbuild`, `CMakeLists.txt` | Application settings and component dependencies |

Root `CMakeLists.txt` defines `water_flow_node`; `sdkconfig.defaults` provides safe
defaults; `partitions.csv` reserves historical NVS separately from Wi-Fi NVS.
`sdkconfig.ci` has deliberately nonfunctional network build fixtures. `tests/`
contains host fault/recovery tests and sensor build profiles. The hello-world
source, README and example test have been replaced. Existing Git metadata and
local flash-test artifacts are preserved.

Sensor/state tasks never call networking APIs. One telemetry worker owns MQTT
connection and in-flight state; callbacks pass small notifications through a
queue, avoiding callback/enqueue mutex deadlocks and fast-PUBACK races. Storage
has its own mutex. Network failures do not restart the board. Heartbeat sends can
block the telemetry worker for a bounded socket timeout while sensing continues.

## Configure and build

Install ESP-IDF **v5.5.6** with ESP32 tools and use its exported shell (on Windows,
the ESP-IDF PowerShell prompt or your installation's `export.ps1`). Use the
installation's Python, CMake and Ninja. From this repository:

```powershell
idf.py set-target esp32
idf.py menuconfig
idf.py build
```

Under **Water flow node**, configure:

| Setting | Default / action |
| --- | --- |
| Wi-Fi SSID / password | Empty; enter the site's 2.4 GHz credentials locally |
| ThingsBoard hostname | `mqtt.thingsboard.cloud` (hostname only) |
| ThingsBoard MQTT TLS port | `8883` |
| ThingsBoard device access token | Empty; enter your device's token locally |
| Heartbeat interval | `300` seconds |
| SNTP server | `pool.ntp.org`; change for an internal server if required |
| Sensor source | **MOCK**; choose UART or unfinished SPI boundary explicitly |
| Mock state interval | `30` seconds |
| Mock battery readings | Enabled for mock sensing: 3910 mV, 78%; disable for real operation |

Create/select a ThingsBoard Cloud device and obtain its access token. The token
is the MQTT username; no MQTT password is needed. TLS verifies broker hostname
and certificate using the ESP-IDF certificate bundle. MQTT starts after Wi-Fi
and valid SNTP time so certificate time validation can succeed. Permit DNS,
SNTP UDP/123 and MQTT TLS TCP/8883. The configured port must serve TLS; plaintext
MQTT is not enabled. See the [ThingsBoard Cloud MQTT connection guide](https://thingsboard.io/docs/paas/reference/mqtt-api/getting-connected/)
and [ESP-IDF v5.5 MQTT documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/protocols/mqtt.html).

For a local `sdkconfig` left over from hello-world, verify these settings because
defaults do not override existing selections:

- Serial flasher config: **8 MB** flash.
- Partition Table: **Custom partition table CSV**, `partitions.csv`.
- Component config / mbedTLS: **Certificate Bundle** enabled.
- Component config / ESP-MQTT: **Report deleted messages** enabled; outbox
  expiration defaults to 300000 ms.

`sdkconfig`, backups, local configs, `build*` directories and firmware artifacts
are ignored. **Never put real credentials in `sdkconfig.defaults`, `sdkconfig.ci`,
source, documentation or commits.** Menuconfig stores plaintext secrets locally
and compiles them into firmware; protect configuration and build artifacts.
Production provisioning, flash encryption and secure boot remain deployment work.

Clean build without deleting existing output:

```powershell
idf.py -B build-clean "-DIDF_TARGET=esp32" build
```

Build the fully linked network implementation with fake build-only credentials
using a separate local configuration:

```powershell
idf.py -B build-network "-DSDKCONFIG=sdkconfig.local.network" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ci" "-DIDF_TARGET=esp32" build
```

For UART/accelerometer compile checks, append `;tests/sdkconfig.uart` or
`;tests/sdkconfig.accelerometer` to that defaults list, with a distinct build
directory and local sdkconfig for each. The UART fixture's GPIO21 is a compile-test
value; verify board wiring before deployment. Normal builds do not load CI fixtures.

## Flash and monitor (after resolving the separate host issue)

No flashing or host/USB/MD5 workaround is part of this change. When ready, use your
actual port in an exported shell:

```powershell
idf.py -p COM_PORT flash
idf.py -p COM_PORT monitor
```

For alternate output, include the same `-B build-directory` option. Exit monitor
with `Ctrl+]`. The layout has a 2 MB factory app at `0x10000` and 128 KB
`flow_events` NVS at `0x210000`; no OTA slots. Preserve the history partition's
offset/size in future upgrades. Initialization errors never automatically erase NVS.

## State events and delivery

The first known observation establishes and persists a baseline, without flow
telemetry. Later actual changes create events:

```text
UNKNOWN -> NO_FLOW   baseline only
NO_FLOW -> NO_FLOW   no event
NO_FLOW -> FLOW      one event
FLOW    -> FLOW      no event
FLOW    -> NO_FLOW   one event
```

Last observed state survives reboot, including after the queue drains. Repeated
states after reboot do not create duplicate transitions. `UNKNOWN` is rejected;
unavailable sensing must not become `NO_FLOW`. A change while unpowered is detected
at the next observation; its actual change time while unpowered is unknown.

Topic `v1/devices/me/telemetry`, QoS 1, retain disabled:

```json
{"ts":1791295200000,"values":{"flow_state":"FLOW"}}
```

```json
{"ts":1791298800000,"values":{"flow_state":"NO_FLOW"}}
```

`ts` is Unix epoch milliseconds at observation, not reconnect/replay time. Every
transition commits before publication. The oldest timestamped event sends first.
Successful enqueue is **not** delivery: removal waits for `MQTT_EVENT_PUBLISHED`
(broker PUBACK). ESP-MQTT retransmits its in-flight QoS1 message on reconnect;
expired outbox messages retry from NVS. See [ESP-MQTT acknowledgement and retransmission behavior](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/protocols/mqtt.html).
PUBACK confirms broker receipt, not downstream rule-chain storage or dashboard
rendering. Delivery is **at least once**: an ACK/power-loss race can replay the
same timestamp/state. Downstream counting must tolerate duplicates.

### Persistence and timestamp limits

- Each event has its own versioned NVS blob, so a change does not rewrite the
  whole queue. A small state/sequence checkpoint commits before acknowledged
  deletion. A boot counter writes once per boot. NVS manages key-level recovery
  and wear.
- Before first SNTP sync, records contain a boot ID and monotonic observation
  time. Sync during that boot lets the firmware backdate and persist their epoch
  timestamp. No event publishes before valid sync in the current boot, even with
  a timestamp from an earlier boot. Once synced, the local monotonic clock keeps
  time through Wi-Fi outages; SNTP refreshes the anchor.
- **Without an RTC, reboot before first sync destroys the epoch/monotonic
  relationship for those unsynchronized records.** They remain stored, count as
  buffered and produce a startup error diagnostic. Replay skips them so later
  valid events proceed. No fabricated timestamps or silent deletion. An automatic
  export/recovery workflow is a TODO; an RTC or agreed unknown-time cloud policy
  is needed to close this limitation.
- Capacity is **128 persisted events**, a **32-reading RAM queue** and the state
  worker's pending reading. At capacity/on write failure, the worker retains its
  pending reading and retries each second. Producers receive backpressure and
  retry before later readings. Existing history is never overwritten. An
  indefinite outage cannot have an unbounded lossless queue: UART has a finite
  RX buffer and no flow control, so prolonged saturation can lose UART input.
  Final hardware needs source retry/flow control and an agreed overflow policy.
- Power loss can lose readings still in RAM before commit. Software cannot make
  failed flash durable. Unknown record versions or storage initialization errors
  stop startup without erasing history. Validate flash/GC and power-loss recovery
  on the physical board before field deployment.

## Heartbeats and battery

Every 5 minutes by default, independently of state changes, QoS0 health telemetry
uses the same topic with server-assigned time:

```json
{"battery_percent":78,"battery_voltage":3.91,"wifi_rssi":-55,"uptime_s":86400,"buffered_events":0,"battery_mock":true}
```

RSSI uses `esp_wifi_sta_get_ap_info()`. Uptime uses the ESP timer and resets on
reboot. `buffered_events` counts persisted unresolved and in-flight records until
acknowledged removal, excluding RAM readings awaiting commit. Mock battery data
has `battery_mock: true`. Disable mocking to report unavailable battery voltage
and percentage as `null`, with `battery_mock: false`. Unavailable RSSI is `null`.

Heartbeats never enter NVS or the MQTT outbox. Offline/failed heartbeats are
discarded; a one-element RAM mailbox holds only the newest sample until checked.
The first heartbeat is due one configured interval after boot.

## Sensors and mock testing

**Mock:** select MOCK in menuconfig. Startup warns that readings are synthetic.
It reports `NO_FLOW` immediately, then alternates states every configured interval.
For a new store, the initial observation is the baseline; the first event follows
one interval later. Use a separate ThingsBoard test device for synthetic data.

**UART:** select UART and configure a board-safe RX GPIO, optional TX and baud.
RX defaults to unconfigured (`-1`), so no pin is guessed. UART1 defaults to 115200
baud, 8N1, no flow control. Flash GPIO6-11, PSRAM GPIO16-17 and console GPIO1/3
are rejected. Verify the Feather V2 pinout, strapping pins, peripheral conflicts,
voltage levels and common ground before wiring. The TI electrical interface has
not been finalized.

Temporary protocol: exact, case-sensitive ASCII lines `FLOW` or `NO_FLOW`, each
terminated with an actual newline byte. CRLF also works. Unknown, binary and
overlong lines discard through newline; parsing spans reads. Duplicate valid
lines do not create events. Replace `sensor_line_parser.c` and transport handling
when the ultrasonic team agrees on framing, integrity checks, source timestamps
and acknowledgements.

**SPI accelerometer:** selecting this source logs that acquisition/classification
is unavailable while networking and stored replay continue. No IIS3DWB driver,
SPI configuration or classifier is fabricated. Implement the sensor module after
these interfaces are agreed.

Task-context producers use `flow_state_report(state)` or
`flow_state_report_at(state, monotonic_ms)` with `esp_timer_get_time() / 1000`.
On `ESP_ERR_TIMEOUT`, retain the state and original measurement time and retry
`flow_state_report_at()` before later data. `ESP_OK` means accepted into RAM,
not durable yet. ISR producers need an ISR-to-task handoff. New sensing methods
do not require cloud changes.

## Verification

With Python and a GCC-compatible host compiler:

```powershell
python tests/run_host_tests.py
```

Tests compile the actual state worker, NVS store, parser and MQTT worker with
`-Wall -Wextra -Werror` and deterministic FreeRTOS/NVS/MQTT fakes. Eleven groups
cover baseline/deduplication, timestamps, FIFO/full queue, reboot/counters, storage
failure and checkpoint/deletion power-loss boundaries, unresolved/corrupt records,
backpressure, parser recovery, reconnect/in-flight handling, publication failure,
outbox expiry, fast PUBACK, failed acknowledged-event removal, callback overflow,
and disposable heartbeats. Fakes do not emulate flash GC, real concurrent
scheduling, radio, TLS or ThingsBoard. See [validation results](docs/validation.md)
for exact ESP32 build commands, image sizes and test scope.

When physical flashing is available, test on a **test device**:

1. Observe assigned IP, time sync, MQTT connection and alternating ThingsBoard
   events; verify heartbeats and the mock battery marker.
2. With UART, send baseline, repeated `FLOW`, then `NO_FLOW`; check only changes.
3. After sync, disable Wi-Fi, generate transitions and restore it; verify original
   timestamps, ordered replay and queue drainage.
4. Reboot with timestamped events queued; verify replay/state recovery. Separately
   test boot-before-sync and the documented unresolved-time limitation.
5. Test broker loss, rejected credentials, outbox expiration and power cuts at
   storage/ACK boundaries. Check no reboot loop or history overwrite.

## Hardware and deployment TODOs

- Finalize MSP430/EVM430-FR6047 framing, electrical interface, pins, flow control,
  sensor availability and timestamp ownership.
- Implement SPI accelerometer acquisition and a validated flow classifier;
  define arbitration if ultrasonic and accelerometer sources run together.
- Define battery divider/monitor, ADC configuration, calibration and charge curve;
  implement the unavailable branch in `battery_read()`.
- Resolve pre-sync reboot timestamps with an RTC or agreed cloud policy; add a
  service/export workflow for retained unresolved records.
- Validate capacity, flash endurance, brownouts, clock drift and networking on the
  actual Feather V2; tune overflow behavior against the sensor's delivery contract.
- Define provisioning, token rotation and physical-device protection. OTA, deep
  sleep and long-term power optimization remain future work.
