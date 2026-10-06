# Changelog

## V1.10.3

- Refined the empirical battery percentage curve using the second overnight discharge test.
- Full charge is now held at 100% down to approximately 4.08 V to avoid an immediate percentage drop after unplugging.
- Added denser high-SOC anchor points at 95% and 90% for a smoother top-end indication.
- Lower-end anchors were also adjusted from the second full-discharge run.

## V1.10.2

- Replaced the generic LiPo voltage-to-percentage table with the empirical discharge curve measured on the actual CARAC remote.
- New anchors span approximately 4.15 V at 100% down to 2.96 V at 0%.
- Battery percentage continues to use linear interpolation between measured anchor points.
- Charging detection and the persistent battery logger remain unchanged.

## V1.10.1

- Battery logger now keeps running independently of Wi-Fi connectivity.
- Logger state is persisted in LittleFS so an unexpected shutdown or reboot does not invalidate the discharge test.
- If the remote restarts while a test was armed, logging resumes automatically.
- Each CSV row is flushed to flash immediately to minimize data loss if the battery dies.
- Added a boot-segment column so samples recorded across a reboot can be distinguished.
- Stopping or clearing the logger explicitly disarms automatic resume.

## V1.10.0

- Added a temporary battery-discharge logger stored in LittleFS.
- Logger records one CSV sample every 60 seconds: uptime, elapsed time, raw ADC, calibrated voltage, current percentage and charging state.
- Added Web controls to start, stop, clear and download the battery log.
- Automatic Light Sleep is inhibited while the battery logger is active so a full overnight discharge can be recorded.
- Added automatic charging detection based on a filtered voltage rise.
- While charging, the percentage is hidden and the UI reports **EN CHARGE**.
- Charging state is cleared when filtered voltage drops by at least 0.01 V from the latest charging peak.
- Added battery charging/logger fields to the /status API.

## V1.9.0

- Added configurable automatic Light Sleep.
- Sleep can be disabled or configured in minutes/hours from the remote Web UI.
- D5 / GPIO14 is used as the wake source; no additional wiring is required.
- The wake-up button press is immediately sent as PLAY / PAUSE.
- Rescue AP mode prevents automatic sleep so network recovery remains accessible.
- Existing V1.8.0 network configuration is migrated automatically.
- OTA and ArduinoOTA remain available while the remote is awake.


## V1.8.0

- Added DHCP / static IPv4 selection.
- Added configurable static IP, gateway, subnet mask, DNS1 and DNS2.
- Added configurable hostname and mDNS.
- Added network status: RSSI, channel, BSSID, MAC, IP, gateway, subnet and DNS.
- Added configurable rescue AP SSID and password.
- Preserved automatic fallback AP and captive portal.
- Added migration from the V1.7.x EEPROM Wi-Fi structure.
- Preserved browser OTA and ArduinoOTA.
- Preserved battery monitoring with calibration factor 1.114.
- Preserved short press PLAY / PAUSE and 1.5 s long press RESET.
- Removed personal Wi-Fi credentials from compiled defaults for the public repository.

## V1.7.1

- Battery calibration corrected from 3.68 V to a 4.10 V multimeter reference.
- Calibration factor set to 1.114.

## V1.7.0

- Added browser OTA.
- Added ArduinoOTA.
- Added automatic rescue Wi-Fi AP.
- Added persistent Wi-Fi configuration.
- Added standalone remote Web interface.

## V1.6.x

- Initial integration of the physical ESP8266 remote with the CARAC TIMER control interface.
