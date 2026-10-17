# Switch source mapping and device loss

Implemented for ESP8266, 17 October 2026. ESP32 work remains paused. The implementation notes below supersede the original design retained later in this document.

## Implemented assembly map

Edit `nativeSources` in [BoardPinMap.h](BoardPinMap.h) before compiling for the actual wiring. List only physically exposed native connectors, in their permanent REST/setup order. Each entry has a resource ID, allowed capability flags, reserved flag (false), connector label and optional startup-active-low flag. IDs 0..16 mean GPIO; 256 means A0. Change the `std::array` size when editing the list; size zero is supported.

```cpp
{3, SourceMapping::DigitalIn | SourceMapping::DigitalOut | SourceMapping::Pwm,
 false, "RX header / GPIO3"},
```

Compile-time checks reject duplicate resources, flash pins, I2C/UART conflicts and unsupported functions. ESP8266 A0 is ADC-only; it cannot perform PWM or digital I/O. ESP-01 normally exposes no A0. Input-capable connectors default to high-impedance input until configured. Output-only connectors use their assembly startup polarity to initialize inactive.

The supplied ESP-01 profile exposes GPIO3, reserving GPIO2/0 for SDA/SCL and GPIO1 for TX-only logging. The ESP-12 example lists GPIO12/13/14 and A0. Confirm connector wiring and electrical limits before use.

## Implemented enumeration and drivers

Native connectors occupy the first IDs in `/api/v1/switch/0`. Each confirmed ADS1015/ADS1115 profile adds four resident read-only ADC channels after them. Configurable external channels follow. Setting the configurable count to zero cannot remove residents. Device disappearance never removes or renumbers channels. Explicitly adding an ADC profile shifts configurable IDs and requires setup confirmation; changing the assembly map also requires clients to update IDs.

Limits: 36 total channels, 16 configurable channels and eight I2C profiles. Configurable capacity shrinks if residents require more space. Sources are unique physical identities, independent of discovery order; duplicate allocation is rejected. Native resident sources cannot be rebound.

Discovery lists ACK addresses without guessing chip identity. Setup confirms PCF8574/PCF8574A (0x20..0x27/0x38..0x3f), ADS1015/ADS1115 (0x48..0x4b), or MCP4725 (0x60..0x67). Actual address availability depends on the fitted part.

[MappedSources.h](MappedSources.h) integrates the [SourceMapping.h](SourceMapping.h) model with existing handlers and ownership validation:

- GPIO binary input/output and PWM follow connector capabilities. PWM uses integer codes 0..1024.
- A0 uses `analogRead(A0)/1023`, followed by scale/offset. Configure scale for the actual divider; the bare ADC range is 1 V.
- PCF writes maintain a whole-port shadow, preserve other channels and release inputs high. Setup requires a safe byte for unassigned pins; mapped outputs use their inactive polarity.
- ADS conversion-ready and complete signed reads are checked. Gain is +/-6.144 V, steps 0.003 V (ADS1015) and 0.0001875 V (ADS1115), with per-channel calibration. Electrical inputs must still respect the chip supply.
- MCP4725 uses integer codes 0..4095 and volatile register writes, never EEPROM writes. Reported output is the successful command cache, not analog feedback.

Transport failures invalidate the device generation. Missing/recovering/stale/invalid values produce errors without fabricated zeroes. Metadata and assignments remain available offline. ACK alone does not mean ready: automatic recovery initializes PCF outputs inactive, DAC zero and validates ADC communication before accepting commands. Previous commands are never replayed. Native outputs initialize before WiFi startup. Status/MQTT use mapped identity and validity.

## Implemented setup and storage

MQTT configuration update: setup now includes broker hostname/IPv4 address (up to 63 characters) and port (1..65535). `POST /setup/mqtt`, like the other setup UI routes, does not require ClientID. Direct REST API settings retain the ownership guard. Saving updates live variables and requests a disconnect/reconnect without reboot; the form includes current settings. Firmware boot and retries use the saved endpoint. Credentials/topics retain their existing configuration.

Storage is now version 4, 4,088 bytes including the MQTT fields appended after the version-3 layout. Valid version-3 records retain their mappings and use the existing compile-time broker and port 1883 until saved. A failed EEPROM commit returns an error explaining that the settings are active only in RAM. The version-3 description below records the initial mapping implementation.

[MappingUi.h](MappingUi.h) and [MappingPageScript.h](MappingPageScript.h) expose source, allowed mode, polarity, range, calibration, write policy and presence. Both browser and server enforce capabilities. Occupied resources and stale inventory submissions are rejected. Reconfiguration quiesces the old output before release; offline assignments are retained.

[MappingStorage.h](MappingStorage.h) uses a bounded 4,022-byte version-3 record within 4,096-byte EEPROM emulation, with CRC and an assembly signature. Cached samples and old output commands are not persisted. Flash failures are reported as RAM-only changes. Corrupt/incompatible records fall back to safe defaults.

Legacy `*` records migrate bounded names/settings behind the native prefix. Allowed legacy native PWM assignments transfer to their native resident. Old expander assignments remain unbound until the actual chip/address is confirmed; decimal address 160 is not guessed into another device.

## Validation

There are 34 target-compiler assertions and 61 host runtime checks against actual mapping/storage/setup code with fake GPIO/Wire/EEPROM. Cases include conflicts, capability restrictions, missing devices, both ADC variants, DAC volatile writes, inactive recovery, resident insertion, zero external count, migration, CRC and flash failure. Pin-map tests compile valid profiles and reject seven invalid cases. Browser checks exercise generated forms at 320, 390, 768 and 1440 pixels.

Physical checks remain: wiring/polarity, calibration, cold boot/reboot levels, unplug/replug and bus faults with dummy loads, persistence, network responsiveness and Alpaca conformity. Chip power-up behavior before firmware takes control requires bench verification.

## Original design record (historical)

The following proposals describe the earlier design stage. References below to unintegrated code, explicit recovery approval and future steps are historical; automatic inactive recovery and the integrated behavior above are implemented.

## Separate identity, configuration and health

An Alpaca switch ID identifies a logical channel. It must never double as a GPIO number, PCF8574 pin or discovery-array position. Use:

`switch ID -> persistent channel binding -> physical source key -> runtime device/driver`

Keep three bounded tables, rather than allocating/removing switch entries on every bus scan:

| Table | Stored fields | Lifetime |
| --- | --- | --- |
| Logical channel configuration | Name, description, SourceMapping::Binding, units, min/max/step, calibration, startup policy, resident flag | Persistent; indexed by stable Alpaca ID |
| Hardware/device configuration | Driver profile, stable bus ID, 7-bit address, channel count/capabilities, safe startup port image/output defaults | Persistent; retained when absent |
| Runtime registry and value cache | Device health, last probe/operation/error times, failure count, generation; per-channel value/time/validity; per-PCF whole-port shadow | RAM only; not written to EEPROM on each probe/sample |

`Binding` separates source from mode, user write permission and active-low polarity. Retain descriptive names such as Relay_NO, but do not use them as physical driver identities or infer active polarity from them.

Reference types are in [SourceMapping.h](SourceMapping.h): SourceRef, Binding, NativeResource, Device, ValueCache and PcfShadow. Logical channel names/ranges/calibration remain in a proposed replacement/extension of SwitchEntry; do not serialize the current pointer-bearing struct.

## Source keys and capabilities

| Source | Example stable UI key | Channel/resource meaning | Modes |
| --- | --- | --- | --- |
| Native pin | `native:r2` | Board resource 2, labelled GPIO2 by the board profile | Digital input/output or PWM, as permitted |
| Native ADC | `native:r256` | Illustrative board resource 256, labelled A0 | Read-only analogue input |
| PCF8574 | `i2c:0:20:5` | Bus 0, address 0x20, port P5 | Binary input/output |
| I2C ADC | `i2c:0:48:2` | Bus 0, address 0x48, driver channel 2 | Read-only analogue input |
| I2C DAC | `i2c:0:60:0` | Bus 0, address 0x60, driver channel 0 | Analogue output |

Addresses and resource numbers are examples, not changes to the connected hardware. In the code, SourceRef carries kind/bus/address/channel; a UI key is its canonical physical-resource serialization. Driver kind is validated against the configured profile, not inferred solely from the key/address.

NativeResource is an explicit board allowlist. A dedicated ADC pad gets its own resource ID so A0 is never confused with GPIO0. If a physical GPIO has ADC and digital functions, both modes use the SAME resource ID and cannot be allocated independently. Only expose ADC resources actually supported and accessible on the selected module/board. The ESP8266's single ADC can be used for external A0 or VCC measurement; those configurations are not two independent simultaneous channels. GPIO availability also depends on I2C, UART and boot use. [ESP8266 reference](https://arduino-esp8266.readthedocs.io/en/3.1.2/reference.html)

Unique allocation compares physical resources, ignoring mode. Two switches cannot own GPIO2 as different modes, or the same PCF pin. The current sketch uses GPIO2/GPIO0 for I2C, so those pins must be reserved in its board profile, despite the illustrative GPIO2 key above. Different PCF pins on one device are independent allocations, but share its device health and port shadow. Reject duplicate bus/address profiles before accepting inventory records; two physical devices with the same address on the same bus cannot be distinguished or safely allocated by software. On a later multi-bus design, bus IDs must represent stable wiring, not discovery order.

`assignBinding()` validates capabilities, read-only input rules, channel bounds and conflicts before changing its binding array. A configured but missing device remains a valid configuration target; it is unavailable for I/O. Unknown addresses need an explicitly created/confirmed driver profile first. Resident ADC bindings should be preclaimed and locked against ordinary output-count/type edits.

## Initial detection and continued inventory

At startup:

1. Load and validate saved profiles/bindings, creating Unknown device records even for absent devices. Keep logical IDs and names unchanged.
2. Initialize Wire and probe configured devices plus an approved set of discovery addresses. Record an ACK as an address candidate, not a verified device type. Do not blindly issue register writes to an unknown part to guess what it is.
3. Match candidates to saved profiles, or show an unclassified candidate for the operator to identify. Where a chip supports a documented identity read, check it. PCF8574 has no serial-number identity handshake that can prove the same physical device has returned.
4. Expand each confirmed profile into its device-local channel resources: P0..P7 for PCF8574, the actual ADC/DAC driver's channel set for converters. Compute free resources from bindings, not from whichever devices happened to be discovered first.
5. Initialize approved devices to their configured safe whole-device state before enabling outputs. Never silently replay old cached relay or DAC commands after a device returns.

In loop(), service at most one due device/probe or acquisition phase per pass. A suggested configured-device probe interval is 1 second with a capped backoff to 5 seconds when absent; a full discovery rescan can be slower and operator-triggered. Do not scan the complete bus inside every HTTP request. A real read/write checks its own result even if the last probe succeeded. For reads, require the complete expected response length and successful driver status, not merely address ACK. Configure clock-stretch/transaction bounds using the pinned ESP8266 Wire implementation; its available API is setClockStretchLimit, not a portable promise that every core implements setWireTimeout. Bound recovery attempts and preserve HTTP/MQTT servicing when the bus is stuck.

The reference Device.probeDue()/observeProbe() implement elapsed-time scheduling and state updates; the loop supplies the chosen interval/backoff and one-device-per-pass budget. recordSample() checks complete sample length and finite scaled values before accepting an ADC cache update. Short reads invalidate the device/cache, rather than publishing a fabricated zero. The driver must additionally check its own conversion-ready/status and gain/configuration. For example, after selecting a due device and its bus:

```cpp
wire.beginTransmission(device.address);
const uint8_t status = wire.endTransmission();
device.observeProbe(millis(), status == 0, status);
```

No callback performs bus access in an ISR. The current synchronous application can keep bus work serialized in loop/handler context; later transports must preserve that serialization.

## Device loss and recovery

States are Unknown, Missing, Recovering, Ready and Fault. An address NACK during a probe makes a device Missing. A failed actual operation makes it Fault immediately; do not allow several more potentially unsafe writes merely to debounce the UI indicator. Both invalidate its generation. Optional UI debounce must not delay I/O inhibition.

All channels belonging to the failed device become unavailable together. Their bindings, names, capabilities and IDs remain. Cached values carry a device-generation number; after loss/recovery, old values fail validation even if their timestamp is recent. ADC samples also expire by age. A PCF write failure can leave the applied output uncertain, so the old cache/shadow cannot be returned as a confirmed current state.

An ACK after loss changes the state to Recovering, not Ready. Recheck the driver profile, clear stale conversions, and initialize safe outputs. Default output recovery requires operator approval because an address may now contain a different device and a returning expander may have reset its outputs. An ADC profile may eventually permit automatic recovery after driver-specific validation and a new valid sample; output auto-recovery is a separate explicit policy, not a side effect of detection. Even then, do not replay old actuation commands.

Software cannot switch off an absent expander or guarantee a disconnected DAC has changed its analogue output. Outputs may retain their previous state, reset, or be electrically indeterminate. An uninterrupted ACK stream also cannot reliably detect a brief unobserved reset of a simple device. Loads needing guaranteed fail-off require appropriate hardware enable/watchdog/reset arrangements. PCF8574 ports power up high; the driver needs an explicit eight-bit safe-state image based on polarity, input masks and the actual board. Do not assume all-high means safe on every relay board. [PCF8574 documentation](https://www.ti.com/product/PCF8574)

## PCF8574 transaction code

The tested reference implementation includes two Wire-compatible functions:

```cpp
//After an ACK has put the configured device in Recovering:
Result result = recoverPcf8574(wire, device, configuredSafePortByte,
                               profileConfirmed, operatorApproved,
                               shadow, millis());

//After range/ownership/binding checks and successful recovery:
result = writePcf8574Bit(wire, device, binding.source.channel,
                        requestedLogicalState, binding.activeLow,
                        shadow, channelCache, millis());
```

The write modifies one bit in the device's confirmed whole-port shadow, preserving all other assigned pins and input-release bits. It checks both write-buffer acceptance and endTransmission status. On failure it records the error, advances the device generation, invalidates the shadow/current cache and returns IoFailure. On success it commits the new shadow and logical command cache. It never uses a PCF pin read to reconstruct the output latch: port levels and intended latch state differ for quasi-bidirectional inputs/externally driven pins.

The helper expects the correct Wire bus and a validated binding; the dispatcher chooses the bus from source.bus. It is a backend function, not an authorization boundary. It does not replace existing ClientID, CanWrite or numeric argument validation. ACK confirms the transaction, not a relay contact position. Detailed implementation: [SourceMapping.h](SourceMapping.h).

## Handler and setup changes

| Current function/path | Proposed change |
| --- | --- |
| initSwitch/copySwitch/reSize | Initialize/copy Binding and invalidate new runtime caches. Prefer fixed bounded channel slots. Output resizing cannot remove resident ADC entries. |
| readSwitchId / metadata handlers | Resolve only the logical channel for metadata. Names, ranges and CanWrite remain available offline. CanWrite describes capability AND user policy, not connectivity; ADCs stay false. |
| setRelayOutput / setPwmOutput / new setDacOutput | Replace Id-as-pin/global-device assumptions with resolveChannelSource(id, write=true), then dispatch by source kind and mode. Validate value/range/step first. Only a successful transaction updates cache. |
| SwitchState/SwitchValue GET | Resolve source, require appropriate connection policy, then read a valid cache/sample or perform a bounded driver read. Offline, stale or unknown output state produces an error, never a fabricated zero/false. Keep bool conversion and numeric units consistent with channel metadata. |
| SwitchState/SwitchValue PUT | Keep existing positive-ClientID lease, strict parsing and CanWrite checks. Resolve immediately before the transaction, call the backend, and map failure to an Alpaca error. Preserve all extra URL mappings. |
| handlerDriver0SetupSwitches / SwitchType | Stage a candidate configuration, validate source and exclusivity against a scratch binding table, then quiesce/release the old backend and initialize the new one. Publish/persist only an accepted configuration. Hardware changes cannot be perfectly rolled back: mark affected caches invalid if a reconfiguration fails partway through. |
| setPins / startup / MQTT output commands | Use the same dispatcher and board profile. Remove direct analogWrite and switchDevice.write shortcuts. A source in Recovering cannot bypass the guard through a setup or MQTT path. |
| setupFormBuilderDriver0Switches | Add Source, Mode, Polarity, Units/Range and availability fields. Source options come from the inventory plus retained offline bindings. Bindings use stable keys, never current dropdown positions. |
| setupFromEeprom / saveToEeprom | Introduce a versioned fixed-width schema and checksum; migrate old relay IDs to an explicitly confirmed PCF profile/pin. Persist configuration only, not discovery order, timestamps, failures, generation counters, samples or live driver pointers. |
| loop / handlerStatus | Schedule inventory/probe/acquisition work; expose source key, driver profile, Ready/Missing/Recovering/Fault, error code, last operation/probe time, sample age and last commanded value separately. |

For understood source I/O failures return HTTP 200 with a driver-specific Alpaca error, for example 0x500 and `I2C bus 0 device 0x20 unavailable; switch 3 remains assigned to P5`. Use distinct documented codes for stale samples and recovery-required state, and omit Value on error. Preserve the existing NotConnected response for a missing client/device session; hardware loss must not silently clear the owner lease. Read-only writes remain NotImplemented; invalid user-selected sources/ranges remain InvalidValue. Verify final error assignments during handler integration.

The setup dropdown might show `PCF8574 / bus 0 / 0x20 / P5 — assigned, offline` as the selected option while disabling it for allocation to other channels. New unclaimed online pins are selectable; claimed pins cannot be allocated twice. Unknown ACK-only addresses are shown in a separate device-profile confirmation section. Saving names or keeping the existing offline assignment remains possible; attempting actual output is rejected. Revalidate on the server even if the browser used an older inventory snapshot. A registry revision can detect stale setup submissions; compare while validating/committing, not just in JavaScript.

## Integration sequence and checks

1. Agree the model, fixed limits and loss/recovery policy; retain the reference code for review. No existing handler behavior changes in this step.
2. Introduce the versioned binding/device configuration and legacy migration; add stable source labels and read-only inventory/status presentation.
3. Introduce the registry, bounded probing and PCF shadow/recovery path; route existing binary operations through it and bench-test unplug/replug with dummy loads.
4. Add board-allowed GPIO input/output/PWM and native ADC modes. Add the chosen I2C ADC/DAC drivers, their value units and calibration, checking complete reads and write failures.
5. Enable the source selector/rebinding transaction and resident ADC enumeration. Verify zero configured outputs, duplicate allocation, all metadata offline, and reboot persistence.

The reference tests in [tests/source_mapping_checks.cpp](tests/source_mapping_checks.cpp) execute 34 constexpr assertions in the actual ESP8266 compiler. They cover native capabilities, A0/GPIO separation, reserved/invalid pins and modes, PCF bounds, all device states, duplicate physical allocation, absent configured/unknown devices, source lookup independent of registry index, read-only ADCs, explicit unbinding, ACK-not-readiness, safe recovery approval, whole-port bit preservation/polarity, address/data NACK, buffer failure, timeout, failed initialization, generation/freshness invalidation, sample completeness/nonfinite values and millis wraparound. These are simulated transaction tests, not live bus tests. The module is not included in the firmware yet, so they establish component behavior without claiming an integrated build or UI implementation. Diagnostic bytes 0xff, 0xfe and 0xfd denote local write-buffer, sample-length and scaled-data errors; other transaction error bytes are supplied by Wire/the driver.
