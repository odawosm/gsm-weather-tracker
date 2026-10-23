# Module tests

One sketch per module. Run them in order and only move on when the current one passes. Each needs only its own module wired (pin map: `../claude-code-brief.md`).
Serial Monitor: **9600 baud** for all. For 07 also set line ending to **Both NL & CR**.

## Libraries (Arduino IDE → Library Manager)

- DHT sensor library (Adafruit)
- Adafruit Unified Sensor
- Adafruit GFX Library
- Adafruit SSD1306

`Wire` and `SoftwareSerial` come with the IDE.

## Order and what a pass looks like

| # | Sketch | Needs | Pass |
|---|---|---|---|
| 01 | `01_blink_serial` | USB only | L LED blinks, `Tick 1, 2, 3...` |
| 02 | `02_i2c_scanner` | OLED | `Found 0x3C <- SSD1306 OLED` (`0x27`/`0x3F` if the LCD backpack is on) |
| 03 | `03_oled` | OLED | `PASS: display.begin() OK`, title + counter + moving bar on screen |
| 04 | `04_dht11` | DHT11 | `PASS Temp: 24.0 C Hum: 60 %`, humidity rises when you breathe on it |
| 05 | `05_ldr` | LDR + 10k | Raw value falls when covered, rises under a torch |
| 06 | `06_rain` | Rain sensor | `dry` (~900–1023) when dry; drops to damp/rain with a wet finger, D0 shows `WET` |
| 07 | `07_sim800l_passthrough` | SIM800L + LM2596 @ 4.0V | `-> Module responding.`, supply `good`, then typed AT commands get answers |
| 08 | `08_sim800l_send_sms` | SIM800L + **SIM** | `PASS: SMS sent.` and the phone receives "Weather station test OK" |
| 09 | `09_sim800l_receive_sms` | SIM800L + **SIM** | Text from an authorized phone prints `SMS from +2547...: ...` and you get `Got it: ...` back |
| 10 | `10_all_sensors_oled` | DHT11, LDR, rain, OLED | Readings on OLED, a JSON line every 5 s, `free_ram` reported (note it before adding GSM code) |

## Before the SIM is inserted

- **07 works without a SIM.** Expect `AT` → OK, `AT+CSQ` and `AT+CBC` to answer, and `-> No SIM detected (expected...)` / `Not registered (expected: no SIM)` / `No operator (expected: no SIM)`. That still counts as a pass: it proves power, wiring and serial. Check the `AT+CBC` voltage reads ~4000 mV.
- **08 and 09 need the SIM.** Without one they stop at `FAIL: SIM not ready ...`, which is the correct behaviour.
- Inserting the SIM: **power off the SIM800L first**. Check the SIM in a phone first (active, SMS credit, PIN lock **disabled**). Then rerun 07: `SIM ready`, `Registered on home network`, operator `Safaricom`, NET LED blinking slowly (~every 3 s).
- In 08 set `PHONE`, and in 09 set `AUTHORIZED`, to your number in `+2547...` format.

## SIM800L safety

Measure the LM2596 output at 4.0V with a multimeter **before** connecting the SIM800L, and never power it from the Arduino 5V/3.3V pins. LM2596 OUT− must join Arduino GND.
