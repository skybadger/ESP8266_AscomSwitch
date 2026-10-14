# ESP8266 ASCOM Switch: build, review and implementation log

Review date: 14 October 2026. Target: ESP-01 with 1 MiB flash, Arduino framework. This is a source review and build baseline, not an Alpaca certification. Existing user modifications were preserved; shared files in `../libraries` were not edited.

## Step 1 — PlatformIO build baseline (implemented)

Actions:

- Added `platformio.ini`, targeting `esp01_1m`, Espressif8266 platform 4.2.1 and Arduino core 3.1.2.
- Kept the sketch in the project root. The source filter includes the sketch and PlatformIO's generated `.ino.cpp`, excluding unrelated files.
- Added `../libraries` as a library storage directory and the header-only ASCOM_rest include directory explicitly. Used `chain+` dependency discovery.
- Replaced local ArduinoJson 5 object/array/serialization calls with the existing ASCOM_rest compatibility macros. The checked-out shared ArduinoJson is 7.0.0. Nested temporary objects are copied into their destination arrays before their documents leave scope.
- Replaced the unused legacy `Time.h` dependency with standard `time.h`; timekeeping uses ESP8266 `configTime` and the C time functions.
- Added `build.ps1`, which finds PlatformIO on PATH or in the standard VS Code installation, checks exit codes, and supports build/upload/monitor/clean.
- Added generated-file exclusions in `.gitignore`.
- Excluded WebSockets, which RemoteDebug's manifest pulls in despite WebSocket support being disabled. Telnet debugging remains enabled.

Dependencies are drawn from the existing sibling folder: ASCOM_rest (including ArduinoJsonCompat), PubSubClient 2.8, EEPROMAnything with the project's string helpers, RemoteDebug 4.0.1, ArduinoJson 7.0.0, PCF8574 0.3.2, Adafruit ADS1X15 1.0.1, and any detected transitive dependencies. EEPROM, Wire, WiFi, UDP, web server and HTTP updater are supplied by the ESP8266 Arduino core. Do not replace the old ADS library with a recent version without adapting its header and initialization API. Shared library versions are not locked by this build: retain the sibling checkout for reproducibility.

From PowerShell in this directory:

```powershell
.\build.ps1
.\build.ps1 -Action upload -Port COM5
.\build.ps1 -Action monitor -Port COM5
# Build and upload, then attach the monitor:
.\build.ps1 -Action run -Port COM5
```

Equivalent portable commands:

```text
pio run -e esp01_1m
pio run -e esp01_1m --target upload --upload-port COM5
pio device monitor --port COM5 --baud 115200
```

Firmware runs on the physical ESP8266 after reset; PlatformIO does not emulate its peripherals. Use a 3.3 V UART and adequate 3.3 V power. Hold GPIO0 low during reset for serial programming; return it high and reset for normal boot. This board uses GPIO2=SDA and GPIO0=SCL, so attached circuitry must allow the required boot levels. Stop any existing serial monitor before uploading. After upload, the `run` action opens the monitor; manually reset into normal boot if the programmer does not handle it. Application logs are 115200 baud; ROM boot messages use a different baud rate.

Check actual flash size before choosing this target: older 512 KiB ESP-01 modules need a separate configuration and may not fit. A serial build fitting 1 MiB does not prove an OTA update fits alongside the running image. Evaluate free sketch space and the new image size before using `/update`.

Set WiFi/MQTT credentials through the existing private `SkybadgerStrings.h` in the sibling library; do not commit credentials or print that file in build logs. The sketch waits indefinitely for WiFi, so missing credentials can look like a failed boot.

Validation record: initial sandbox execution failed because PlatformIO needs its external package cache. Subsequent builds used the installed toolchain. The first source filter excluded the generated C++ translation unit and reported “Nothing to build”; this was corrected. Compilation then identified remaining v5 `set<double>` calls and unwanted WebSockets compilation; both were corrected. The final `platformio run -e esp01_1m` compiled, linked and generated `.pio/build/esp01_1m/firmware.bin` successfully in 46.55 seconds. Static RAM: 46,020 / 81,920 bytes (56.2%). Flash: 399,619 / 761,840 bytes (52.5%). These figures do not include runtime heap consumption. The PowerShell script passed parser validation. No hardware upload was performed.

Remaining compiler warnings include unused variables, JavaScript string line continuations, a shared-library multi-line comment, RemoteDebug redefining MAX_TIME_INACTIVE (the sketch's attempt to disable inactivity timeout is overridden), an ADC debug `%d` receiving a floating-point argument, and shared `getTimeAsString2` formatting a 64-bit `time_t` with `%li`. Fix the format mismatches early: they are runtime correctness issues, not merely cosmetic warnings. Build output also reports a broken manifest in the unrelated sibling I2C-Sensor-Lib_iLib folder, which is ignored and not a dependency.

## Alpaca v1 compliance review

### Format fixes — ADC accepted; timestamp under review (14 October 2026)

- `ESP8266_AscomSwitch.ino`: changed the ADC gain argument from `%d` to `%f`, labelled it as a gain factor, and explicitly cast floating-point printf arguments to `double`.
- `../libraries/ASCOM_rest/Skybadger_common_funcs.h`: replaced timestamp `sprintf(buf, "%li.%03i", now, ...)` with `snprintf(buf, sizeof(buf), "%lld.%03i", (long long) now, ...)`. This preserves 64-bit seconds and bounds the buffer write.
- Added `tests/test_formatting.py`: two Windows host tests exercise the actual format strings through the C runtime, covering ADC output plus zero, post-2038/post-uint32 timestamps and millisecond padding/truncation. Initial test runs exposed unavailable CRT exports; the test now uses the exported `msvcrt._snprintf` formatter.
- Validation: `python tests/test_formatting.py` passed both tests (four timestamp cases). `platformio run -e esp01_1m` compiled, linked and generated firmware successfully in 16.91 seconds. No `-Wformat` diagnostics remain. Flash: 399,635 bytes; static RAM: 46,020 bytes. Other pre-existing warnings remain. No on-device execution was performed.
- Workflow requested by the user: present each change for acceptance/rejection; commit only after acceptance; handle high-priority findings in table order. This format change is the first review checkpoint. The shared header belongs to the separate `libraries/ASCOM_rest` Git repository; accepted changes must be committed in their respective repositories without including unrelated existing modifications.
- Proposed commit message: `Fix ADC debug and 64-bit timestamp printf formats` (project), and `Fix bounded formatting of 64-bit epoch timestamps` (ASCOM_rest).

Follow-up: the user accepted the ADC logging change and requested a recompile/review of the timestamp. The actual Xtensa compiler passed `tests/esp8266_time_types.cpp` with `-Wformat=2 -Werror=format`: `sizeof(long)==4`, `sizeof(long long)==8`, `sizeof(time_t)==8`, and time_t retains values beyond 32 bits. The installed Arduino core also provides `lltoa`, `ulltoa` and String constructors for long long. A 32-bit CPU does not preclude compiler-supported 64-bit integer operations. The timestamp build passed again (15.08 seconds, unchanged size) and host format tests passed; execution on an ESP8266 has not been tested. Retain the bounded `%lld` conversion for this pinned core rather than truncating time_t. For a demonstrably limited formatter on an older core, use a checked unsigned-long conversion (maximum epoch seconds 4294967295, ending in 2106), or the core's 64-bit String conversion; neither is necessary for this compiler diagnostic. The timestamp change remains uncommitted pending user review. An isolated ADC test (`python tests/test_adc_format.py`) passed.

### High priority 1 — enforce CanWrite (accepted)

The user confirmed that each high-priority fix still needs its own acceptance checkpoint. Added a read-only PUT guard inside the validated Id branch in both `handlerDriver0SwitchState` and `handlerDriver0SwitchValue`. It returns HTTP 200 with NotImplemented (0x400) and a descriptive message, serializes the response, then returns before relay/PWM access or cached-value mutation. Read operations and SetSwitchName are unaffected. This is a change to value/state write permission, independent of connection-state corrections planned later.

Validation: ESP-01 compile/link succeeded (47.77 seconds); flash 399,915 bytes, static RAM 46,052 bytes. Both guards were reviewed against the hardware-write/cache-mutation paths. Live HTTP and hardware tests require a connected device and have not been run. The user accepted this item by selecting option 2. Commit message: `Reject state and value writes to read-only Alpaca switches`. Timestamp acceptance remains pending.

Committed as `a6e725d`. Only the accepted guard insertions and this log were staged; existing source edits and the pending timestamp change were left out.

### High priority 2 — numeric relay values (accepted)

- `handlerDriver0SwitchValue` now returns the logical cached relay state as a number for both Relay_NO and Relay_NC, instead of rejecting GetSwitchValue.
- Both SetSwitch and relay SetSwitchValue call `setRelayOutput`, backed by the new `RelayOutput.h`. The backend accepts only exactly 0 or 1, preserves the existing global reverseRelayLogic convention, bounds the pin to the eight outputs of the single PCF8574, and updates the logical cache only after a successful bus write. It returns descriptive errors for invalid values/pins and I2C failures.
- Relay numeric PUT parsing uses strtod with complete-consumption and range-error checks so blank text, partial numbers and overflow/underflow cannot become unintended relay commands. General request/boolean parsing remains the separate later high-priority item. The existing CanWrite guards continue to reject read-only setters before the backend is called.
- `tests/relay_output_checks.cpp` evaluates the actual templated backend at compile time against a mock expander, using the ESP8266 compiler: active-high and active-low 0/1 transitions, negative/fractional/above-one values, NaN, infinity, pin 8 rejection and cache preservation after an I2C error. All nine static assertions passed with `-Wall -Wextra -Werror`.
- Full PlatformIO compile/link passed in 47.23 seconds, with no format diagnostics. Flash: 400,491 bytes; static RAM: 46,068 bytes. Test objects are compiled separately and are excluded from the production source filter. No live HTTP or physical relay test was performed. The existing expander address still needs hardware verification, as already recorded in the review.
- Before: numeric relay reads/writes return unsupported-operation errors. After: a valid `Value=1` uses the same output backend as `State=true`, and GetSwitchValue returns numeric 1; `Value=0` corresponds to State=false. Invalid values fail without writing or changing the cache.
- The user accepted this fix. Commit message: `Support numeric relay values through a shared output backend`.

Committed as `e49a554`. Only this fix's source hunks, `RelayOutput.h`, target checks and the build log were staged.

### High priority 3 — PWM boolean operations (accepted)

- PWM GetSwitch now assigns a bool to the JSON Value: false at the configured minimum, true otherwise. This follows the [documented GetSwitch rule](https://ascom-standards.org/newdocs/switch.html#Switch.GetSwitch), rather than returning the duty count or using a midpoint threshold.
- PWM SetSwitch validates true/false text (case insensitive), maps false to min and true to max, and drives PWM through `setPwmOutput`. Numeric SetSwitchValue uses the same backend. Read-only guards still execute before either setter.
- New `PwmOutput.h` validates integer min/max within the existing 0..1024 duty-count scale, min < max, pin bounds MINPIN..MAXPIN, and finite integer values within those limits before output/cache changes. Fractional duty counts are rejected instead of being silently truncated to an integer hardware value. This does not change the broader pin-allocation policy; reserved I2C/UART pin validation and arbitrary step alignment remain planned work.
- The backend explicitly calls analogWriteRange(1024) before analogWrite, so the application's duty counts do not depend on whether a setup request previously initialized the range. The installed Arduino core defaults to 255. The Arduino PWM API provides no success status, so cached state is the commanded state after the void write call, rather than independently measured physical duty.
- Added `tests/pwm_output_checks.cpp`: 16 target compile-time assertions exercise the real backend with a mock writer, including false at a nonzero min, true just above min, true at max, both boolean writes, invalid/unassigned pins, out-of-range/fractional/non-finite values and invalid endpoints leaving output/range/cache untouched. The first NaN check exposed a constant-expression limitation in GCC; explicit NaN rejection now precedes relational checks. All 16 assertions passed with `-Wall -Wextra -Werror`, and all nine relay regression assertions passed again.
- Final PlatformIO compile/link passed in 18.27 seconds with no format diagnostics: flash 401,211 bytes; static RAM 46,196 bytes. The previous build was repeated after the explicit NaN guard was added. Live HTTP and PWM waveform tests were not performed.
- Example with min=100 and max=900: SetSwitch(true) writes duty 900; SetSwitch(false) writes duty 100; GetSwitch is false at 100 and true at 101. Numeric writes use the same 0..1024 hardware range and reflect their actual integer duty count in the cache.
- The user accepted fix 3. Commit message: `Correct PWM boolean state and share validated output writes`. The timestamp fix is still pending separately.

Scope: Alpaca transport `/api/v1`, its management/discovery services, and the advertised synchronous SwitchV2 interface. API version 1 and Switch interface version 2 are different numbers. Later asynchronous Switch interfaces are outside this baseline.

References: [Alpaca API reference, document 1.0](https://ascom-standards.org/ASCOM%20Alpaca%20API%20Reference.pdf), [official API schemas](https://ascom-standards.org/api/), [SwitchV2 interface](https://ascom-standards.org/Help/Developer/html/T_ASCOM_DeviceInterface_ISwitchV2.htm), [SetSwitch contract](https://ascom-standards.org/Help/Developer/html/M_ASCOM_DeviceInterface_ISwitchV2_SetSwitch.htm), [PlatformIO ESP-01 target](https://docs.platformio.org/en/stable/boards/espressif8266/esp01_1m.html), and [external libraries option](https://docs.platformio.org/en/stable/projectconf/sections/env/options/library/lib_extra_dirs.html).

The device registers the common and synchronous SwitchV2 endpoint names and largely uses GET/PUT correctly. It is **not currently compliant**. Findings below are based on this checkout, including the shared headers, rather than assumptions from the README.

| Priority | Location | Finding and recommended correction |
| --- | --- | --- |
| High | `handlerDriver0SwitchState`, `handlerDriver0SwitchValue` | Neither setter checks `writeable`. Reject value/state writes when CanWrite is false with ASCOM NotImplemented (0x400), before hardware access or cache changes. |
| High | `handlerDriver0SwitchValue` | Relay GetSwitchValue/SetSwitchValue are rejected. Support numeric 0/1 for binary channels through the same backend used by SetSwitch. |
| High | `handlerDriver0SwitchState` | PWM GetSwitch returns a number, and SetSwitch converts a boolean string to an integer without driving hardware. Always return a JSON boolean; map true/false to max/min and use the common output setter. Verify the interface's GetSwitch threshold rule. |
| High | Switch handlers | Known calls frequently return HTTP 400 with Alpaca JSON for invalid device arguments. Use HTTP 200 plus the applicable ASCOM error for understood operations; reserve transport errors for malformed/unrecognized requests and return the specified text form. The missing-Id early exit in SwitchState sends an empty message because serialization is skipped. |
| High | Switch handlers | `toInt`/`toFloat` accept malformed text as zero or partial values. Strictly parse complete input, finite decimal values and boolean tokens; validate IDs before indexing and values before mutation. Apply one consistent case-insensitive argument resolver to transaction fields as well as Id/State/Value. |
| High | `ASCOM_rest/Skybadger_common_funcs.h`, `ASCOMAPICommon_rest.h` | Shared common validation rejects zero IDs and requires both fields; the switch handlers instead silently use zero. Review optional ID handling against the schema and accept legal uint32 values, including zero. `connected=0` also collides with NOT_CONNECTED. Separate connection state from client ID. |
| High | Shared common handler and `Webrelay_common.h` | `InterfaceVersion` is a pointer to string "2", but its handler casts that pointer to `int32`. Return numeric 2 explicitly or convert safely. |
| High | `ASCOM_rest/AlpacaManagement.h` | `/management/apiversions` casts the InterfaceVersion pointer and conflates device interface and API versions. Return `[1]`. Discovery reads unbounded packet length into 256 bytes and checks only 14 signature bytes without validating version/length. Bound reads and validate the full discovery header. |
| Medium | Sketch route registration | `setup/api/v1/switch/0/setup` lacks its leading slash and has the wrong chooser setup shape. Register `/setup/v1/switch/0/setup`; retain legacy aliases only where needed. Custom getswitchtype/setswitchtype are extensions, not standard Switch members. |
| Medium | `initSwitch`, setup forms | Newly added channels have min=max=0. Enforce min<max, positive step and realizable ranges. SwitchStep is a value increment, not a count of levels; correct the introductory comment and validate PWM step alignment. |
| Medium | Switch handlers and connection handler | Switch calls bypass common connection validation despite the README claim. Adopt coherent hardware connection behavior; do not use ClientID as an implicit exclusive lock unless it is a documented custom policy. Return NotConnected for operations requiring an unavailable connection. |
| Medium | Management/metadata | Prototype UniqueID is identical across devices. Assign persistent distinct identifiers. Review driver version formatting, transaction-ID echoing in management, and unsigned server transaction counter rollover. Extra `ClientID`/`Method` response fields are not the main failure; required types and values are. |
| Medium | DAC branch | Unsupported DAC paths alter cached state before returning failure, and unknown types can fall through as success. Reject unsupported channel types during setup and avoid changes on failed commands. |

## Additional firmware findings

- A single PCF8574 has eight outputs, but MAXSWITCH allows 16 and writes use the API ID as the expander pin. Its address is set to decimal 160, outside the 7-bit address space; confirm the real schematic and scanned address (typically 0x20–0x27 or 0x38–0x3f). Do not silently change an address without identifying the hardware.
- Debug startup deliberately toggles relay 0. Remove this test from operational firmware and define safe power-up states. `setPins` tests `pin <= MINPIN` where a lower-bound test is expected, and calls analogWrite for relay entries that can have pin=-1. Validate allowed pins against I2C/UART/boot use and actual hardware mapping.
- `handlerDriver0SetupNumSwitches` rejects zero but permits negative values. EEPROM loading trusts the saved count before allocation and reads; a magic byte is insufficient corruption detection. `reSize` leaks the old pointer array when changing size and does not handle allocation failures. Reload cleanup uses the new count and does not free nested strings. Prefer bounded fixed storage.
- ADC presence is forced true. The gain search stops when a reading saturates rather than retaining the last usable gain. Store signed raw readings, retain a gain with headroom, detect I2C faults, and verify resistor ratios and ADS1015 full-scale values against hardware measurements.
- Acquisition uses `<= lastChannel`, publishing uses `< lastChannel`: with lastChannel=3 four inputs are read but only three reported. Labels and scale-factor comments disagree; debug output divides by scale while published output multiplies. Replace these with one hardware channel descriptor table and one conversion function.
- WiFi blocks startup indefinitely; synchronous MQTT reconnects and ADC delays affect HTTP responsiveness. Initialize safe outputs before network waits, use bounded reconnect backoff, and sample one ADC channel per loop cycle. Keep bus access out of timer callbacks (the existing callbacks correctly set flags).
- Stream large setup pages or keep static text in flash to reduce String heap fragmentation. Verify runtime free heap, JSON allocation failure handling and MQTT packet capacity after adding resident channels. Restrict configuration/update access appropriately for the deployment; current HTTP updater is initialized without credentials.

## Resident read-only ADC design

Expose voltage inputs as sensor channels within SwitchV2. There is no separate generic “ADC” Alpaca device type, and an ObservingConditions device has no standard rail-voltage property. Keep sampling independent of configured writable outputs.

Recommended enumeration with a fixed board profile containing A physically wired ADC channels:

| Alpaca IDs | Source | Count |
| --- | --- | --- |
| 0 through A-1 | Resident voltage channels | Fixed for this board profile |
| A through A+N-1 | User-configured relay/PWM outputs | N may be zero |

`MaxSwitch = A + configuredOutputCount`. For A=3, zero outputs still exposes three sensors; four outputs exposes seven channels. Use fixed sensor IDs to preserve client bindings as outputs are added/removed. Existing relay IDs shift by A: migrate scripts, MQTT commands if applicable and ASCOM client channel mappings before deployment. Appending sensors would preserve old output IDs but would move sensor IDs whenever N changes. An alternative is a separate fixed sensor-only Switch device at `/api/v1/switch/1`, but that requires removing the shared code's one-device assumptions; defer unless maintaining old mappings is essential.

Introduce a channel resolver returning kind and backend index. Never send an Alpaca ID directly to `switchDevice.write` after adding resident sensors. Keep resident descriptors outside the resizable output array and outside its EEPROM records. Compile-time board profiles select the physically fitted inputs and calibration; sensor presence in the API remains fixed even if the ADC is missing. Enumeration and metadata should still work when hardware is absent; value reads should return a descriptive driver error for missing/stale samples, never a fabricated zero.

Each voltage channel has CanWrite=false, name/description including volts, GetSwitchValue in volts, min=0, and a calibrated maximum derived from safe input/divider limits. Choose fixed gain per channel where practical. SwitchStep is volts per effective ADC count (ADS1015 gain LSB × divider ratio), rather than 1024 or 4096. With auto-ranging, document one stable externally reported resolution and quantize consistently; do not change metadata per sample. GetSwitch must return a boolean using the interface rule; it is not an independently configurable alarm. Reject both SetSwitch and SetSwitchValue before any mutation. SetSwitchName is a separate metadata operation; choose to support/persist sensor names or explicitly report unsupported renaming rather than treating CanWrite as a metadata permission.

Use one signed sample cache with acquisition timestamp, quality/presence, gain and converted voltage. Feed Alpaca, MQTT and `/status` from it. Sensor names, scaling and array bounds must come from the same descriptors. EEPROM stores output configuration and any explicit calibration overrides, not resident ADC readings. No resident-channel implementation is included in this build baseline.

## Incremental implementation plan

For each step below, append a dated record here containing changed files, behavior changes, build size, checks/results and remaining limitations. Each step should be a separately reviewable commit and leave a working build.

| Step / status | Actions | Acceptance checks |
| --- | --- | --- |
| 1 / implemented | Establish build and JSON compatibility; retain existing EEPROM/API behavior. | Compile/link for ESP-01; record RAM/flash and warnings. |
| 2 / planned | Fix physical address/mapping after schematic check; bound count and pins; remove startup relay pulse; repair resize/EEPROM validation and default ranges. | Build; blank/corrupt EEPROM and count 0/negative/max tests; bench boot with dummy loads and verify safe outputs. |
| 3 / planned | Centralize parameter parsing and response serialization; fix numeric interface/API versions, chooser setup path, discovery bounds/signature and unique identity. Patch shared ASCOM_rest in a separately reviewed library change or add scoped local overrides. | Build; zero/missing/mixed-case transaction fields; malformed IDs; valid/short/oversize/wrong-version discovery packets; management JSON types. |
| 4 / planned | Unify relay/PWM value and boolean operations; enforce CanWrite, connection state, min/max and valid steps; reject unsupported DAC without mutation. | Build; binary numeric round trips; PWM boolean min/max behavior; failed writes leave hardware/cache untouched; read-only setter errors; synchronous SwitchV2 Conform/ConformU run. |
| 5 / planned | Add board ADC descriptors and signed cached acquisition, real presence/error detection, deterministic gains and calibration. Align loops and MQTT/status labels. | Build; bench comparison against a meter at several voltages; unplugged ADC and saturation tests; confirm responsiveness and timestamp freshness. |
| 6 / planned | Add resident sensor IDs/resolver; make configured output count independent and permit zero; MaxSwitch combines both counts; protect sensors from setup resize/type edits. Migrate client IDs. | Build; N=0/1/max keeps sensor IDs present and stable; every advertised Id has metadata/value; writes to sensors cannot reach relay/PWM; reboot preserves output settings. |
| 7 / planned | Version and checksum EEPROM format; migrate legacy records without incorporating resident sensors; persist board calibration/name overrides if supported. | Build; legacy fixture migration, truncated/corrupt data fallback, repeated reboot round trips, bounded flash writes. |
| 8 / planned | Improve WiFi/MQTT backoff and acquisition scheduling; reduce heap allocation; control configuration/update access; assess OTA image headroom. | Build; broker/WiFi outage recovery, HTTP latency and heap soak tests, repeated discovery, full synchronous conformity run and physical-load validation. |

Do not claim compliance until the corrected firmware passes an appropriate SwitchV2 conformity run on the actual hardware. Newer Switch interfaces may require additional members; `/api/v1` alone does not select that interface version.
