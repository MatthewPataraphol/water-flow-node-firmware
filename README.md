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
| `wifi_auth.c` | Personal or PEAP/MSCHAPv2 Enterprise station configuration and credential setup |
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
| Wi-Fi authentication mode | **WPA/WPA2 Personal** by default; select Enterprise for account-based Wi-Fi |
| Wi-Fi SSID | Empty; enter the selected network's 2.4 GHz SSID locally |
| Wi-Fi password | Personal mode only; enter the hotspot/home Wi-Fi password locally |
| Enterprise username / password | Enterprise mode only; empty local account credentials |
| Enterprise outer identity override | Optional; blank uses the Enterprise username |
| Enterprise authentication-server certificate hostname | Optional; obtain the matching server hostname from network IT |
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

### Hotspot/home Wi-Fi and Purdue PAL3.0

For a home router or phone hotspot, run `idf.py menuconfig`, open **Water flow
node -> Wi-Fi authentication mode**, and select **WPA/WPA2 Personal**. Enter
**Wi-Fi SSID** and **Wi-Fi password**, save, exit and run `idf.py build`.
The existing symbols `CONFIG_FLOW_WIFI_SSID` and `CONFIG_FLOW_WIFI_PASSWORD` keep
their meaning. An existing local configuration defaults to Personal mode.

To switch from a hotspot to PAL3.0:

1. Run `idf.py menuconfig` from an exported ESP-IDF 5.5.6 shell.
2. Open **Water flow node -> Wi-Fi authentication mode** and select
   **WPA2-Enterprise (PEAP/MSCHAPv2)**.
3. Set **Wi-Fi SSID** to `PAL3.0`. This remains a shared configurable setting;
   no Purdue SSID is compiled into source or portable defaults.
4. Enter your own **Enterprise username** and **Enterprise password** locally.
   PAL3.0 uses Purdue **Career Account** credentials. Leave **Enterprise outer
   identity override** empty to use the same username for outer identity and
   inner authentication, unless network IT specifies otherwise.
5. Recommended: set **Enterprise authentication-server certificate hostname**
   to the RADIUS/EAP server hostname confirmed by Purdue IT. ESP-IDF checks the
   certificate hostname; do not assume an Android domain-suffix value is an
   exact authentication-server hostname.
6. Save, exit and run `idf.py build`. Flash manually only when ready. Startup
   logs `Wi-Fi authentication mode: WPA2-Enterprise`; after IP acquisition the
   same SNTP, MQTT/TLS, sensing, buffering and heartbeat pipeline continues.

Purdue's [published PAL3.0 configuration guidance](https://kcc.business.purdue.edu/Faq/Item/803)
specifies PEAP/MSCHAPv2 and Career Account credentials. Confirm current server
certificate/trust and device-access requirements with Purdue IT; a successful
firmware build does not establish compatibility with a live campus network.
The ESP32 requires a 2.4 GHz access point. This implementation supports
password-based PEAP/MSCHAPv2, rather than client-certificate EAP-TLS.

Enterprise mode always enables the ESP-IDF CA certificate bundle for the EAP
server chain. A configured server hostname adds name validation; leaving it
blank checks the CA chain without binding it to the intended authentication
server, and logs a warning. A private/unbundled CA requires configuring the
ESP-IDF trust bundle with network IT. EAP certificate date checks follow the
SDK's TLS configuration; joining Wi-Fi precedes the first SNTP synchronization.
MQTT still waits for valid SNTP time and verifies its own server certificate.
No certificate-verification bypass or automatic Personal fallback is added.

To return to phone-hotspot testing, select **WPA/WPA2 Personal**, change the
SSID back to the hotspot name, re-enter **Wi-Fi password**, save and rebuild.
Switching modes takes effect after installing/restarting the rebuilt firmware.
Hidden mode-specific settings may be removed when Kconfig saves the other mode.

**Never commit Career Account credentials**, including outer identities, to
source, `sdkconfig.defaults`, test profiles, documentation or Git. New Enterprise
settings are stored in the same ignored local `sdkconfig`; its backups and
firmware/build output also need protection. Passwords, usernames, identities,
access tokens and SSIDs are not included in the application's auth-mode logs.

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
directory and local sdkconfig for each. The UART fixture selects Feather V2 RX
GPIO7 and disabled TX (`-1`). Normal builds do not load CI fixtures.

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

**UART:** select UART and configure RX, optional TX and baud. On the
[Adafruit ESP32 Feather V2](https://learn.adafruit.com/adafruit-esp32-feather-v2/pinouts),
the header labeled **RX is GPIO7**, and **TX is GPIO8**. Recommended one-way
connection with another ESP32 (3.3 V logic):

```text
Other ESP32 TX  --->  Feather V2 RX / GPIO7
Other ESP32 GND --->  Feather V2 GND
```

Leave Feather TX unconnected for this one-way input. In `idf.py menuconfig`, open
**Water flow node** and set:

- **Sensor source**: `UART: temporary FLOW/NO_FLOW line protocol`
- **UART1 RX GPIO**: `7` (`CONFIG_FLOW_UART_RX_GPIO=7`)
- **UART1 optional TX GPIO**: `-1` (`CONFIG_FLOW_UART_TX_GPIO=-1`)
- **UART baud rate**: `115200` (`CONFIG_FLOW_UART_BAUD=115200`)

UART1, 115200 baud, 8N1 and no flow control remain unchanged. RX still defaults
to unconfigured (`-1`); TX defaults to disabled (`-1`). If a return path is later
needed, GPIO8 is the labeled Feather TX pin and may be configured explicitly.

Validation reads the ESP32 package ID and permits GPIO7/8 only on the detected
**ESP32-PICO-V3-02**. On that package, GPIO6/11 (flash), GPIO9/10 (PSRAM), and
unavailable/internal GPIO16/17/18/23 remain blocked even with PSRAM software
disabled. GPIO1/3 remain blocked for the console. Other/unknown ESP32 packages
retain the conservative GPIO6-11 block and GPIO16/17 protection. See Espressif's
[PICO series pin mapping, sections 2.2.3 and 2.4](https://www.espressif.com/sites/default/files/documentation/esp32-pico-v3-02_datasheet_en.pdf).
RX must also pass the ESP-IDF valid-GPIO check; enabled TX must be output-capable
and different from RX. Verify strapping pins/peripheral conflicts for other pin
choices. The final TI electrical interface has not been defined.

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

Tests compile the actual state worker, NVS store, parser, MQTT worker and Wi-Fi
authentication layer with `-Wall -Wextra -Werror` and deterministic API fakes.
Eleven pipeline/MQTT groups
cover baseline/deduplication, timestamps, FIFO/full queue, reboot/counters, storage
failure and checkpoint/deletion power-loss boundaries, unresolved/corrupt records,
backpressure, parser recovery, reconnect/in-flight handling, publication failure,
outbox expiry, fast PUBACK, failed acknowledged-event removal, callback overflow,
and disposable heartbeats. Eight additional authentication profiles cover Personal
mode and field limits, empty/oversize SSIDs, Enterprise credentials and identity
fallback/override, empty credentials and cleanup after each EAP setup failure.
Four UART pin-validation groups additionally check PICO GPIO7/8, protected
memory/console/unavailable pins, GPIO capability masks, RX/TX overlap and disabled
TX, and conservative behavior on other/unknown package IDs.
These use synthetic inputs without any real network/account credentials.
Fakes do not emulate flash GC, real concurrent
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
