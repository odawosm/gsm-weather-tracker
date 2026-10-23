# SMS weather station

Arduino Uno weather station (DHT11, LDR, rain sensor, SSD1306 OLED) that answers SMS commands through a SIM800L, plus a local dashboard comparing its readings with Open-Meteo. EEIC/7301 project.

**New to the project? Start with [`PROJECT_GUIDE.md`](PROJECT_GUIDE.md)** (also `PROJECT_GUIDE.pdf`), a plain-language guide to what it does, how it was tested and how to run it. The full technical report, with the theory, circuit explanations and IEEE references, is [`report/PROJECT_REPORT.pdf`](report/PROJECT_REPORT.pdf) (rebuild it with `python report/build.py`).

Background, hardware and wiring details: [`sms-weather-station-notes.md`](sms-weather-station-notes.md). Work order: [`claude-code-brief.md`](claude-code-brief.md).

## Wiring summary

| Arduino | Connected to |
|---|---|
| D2 | DHT11 DATA |
| D3 | Rain sensor D0 |
| D4 | Rain sensor VCC (powered only while reading) |
| D7 | SIM800L TXD (via level shifter, or direct) |
| D8 | SIM800L RXD (via level shifter, or 1kΩ series + 2kΩ to GND) |
| A0 | LDR divider midpoint (LDR to 5V, 10kΩ to GND) |
| A1 | Rain sensor A0 |
| A4 / A5 | OLED SDA / SCL |
| GND | All grounds, **including LM2596 OUT−** |

SIM800L power: LM2596 set to **4.0 V** (measure before connecting), 1000 µF capacitor at the module. Never from the Arduino's 5V/3.3V.

## Build order

1. **Module tests:** [`tests/`](tests/README.md). Run 01 → 10 in order, one module at a time.
2. **Firmware:** `firmware/weather_station/`. Set phone numbers and thresholds in `config.h`, then upload.
3. **Dashboard:** run `./run.sh` (Linux/macOS) or double-click `run.bat` (Windows) from this folder. Add `--fake` to run without hardware. Closing the browser tab stops it again. It sets up Python the first time, starts the app and opens http://localhost:5000. Details: [`dashboard/`](dashboard/README.md). On Windows, `run.bat` can also install Python and load the station program; see the next section.

## Set up on Windows

`run.bat` sets up everything on a fresh Windows 10 or 11 PC. You don't need to install Python or the Arduino IDE first.

**You need:** an internet connection for the first run, the project folder copied to the PC, and the Arduino with its USB cable.

1. **Copy the project folder** to the PC, e.g. to `Documents\GSM Weather Tracker`. If it came as a `.zip`, right-click it, choose **Extract All**, then open the extracted folder.
2. **Plug the Arduino into a USB port.**

    - A genuine Uno needs no driver.
    - A clone with a **CH340** chip (common on cheap boards) needs the CH340 driver once. Install it from the board seller or <https://www.wch-ic.com/downloads/CH341SER_EXE.html>.
    - To check: in Device Manager, under **Ports (COM & LPT)**, it should show as **USB-SERIAL CH340 (COMx)** or **Arduino Uno (COMx)**.

3. **Double-click `run.bat`.**

    - If Windows shows "Windows protected your PC", click **More info**, then **Run anyway**. It's a plain text script; open it in Notepad first if you want to read it.
    - The first run does this by itself:
        - **Python:** if it's missing, it installs Python 3.12 for your user only with `winget` (no admin password, about 30 MB).
        - **Dashboard packages:** it creates `dashboard\.venv` and installs the dashboard's packages (Flask, pyserial, requests).
        - **Start:** it starts the dashboard and opens <http://localhost:5000> in the browser.
    - Later runs skip the set-up and start in a few seconds.

4. **Only if the Arduino doesn't have the station program yet,** or you changed `firmware\weather_station\config.h`:

    1. Open the project folder in File Explorer.
    2. Type `cmd` in the address bar and press Enter.
    3. Run `run.bat --upload`.

    The first time, this downloads `arduino-cli` into `.tools\` plus the Uno support and the U8g2 and DHT libraries (about 150 MB). Then it finds the Arduino's COM port, compiles the program, loads it onto the board, and starts the dashboard. Later uploads take under a minute.

5. **Phone SMS backup (optional):** enter the phone's details in the dashboard's **SMS phone** card. The phone must be on the same Wi-Fi as the PC. See [`dashboard/README.md`](dashboard/README.md#backup-sms-through-an-android-phone).

**Other options:**

- **No hardware:** `run.bat --fake` shows made-up readings, so it works without the Arduino.
- **Combined:** the options can be combined, e.g. `run.bat --upload --fake`.

**Stopping:** close the browser tab, or press Ctrl+C in the black window. While the phone backup is on, closing the tab doesn't stop it; use Ctrl+C.

**If something goes wrong:**

- **"Python 3 could not be set up automatically":** `winget` is missing on older Windows 10. Install Python from <https://www.python.org/downloads/> with **"Add python.exe to PATH"** ticked, then run `run.bat` again.
- **"No Arduino found":** check the USB cable (some only charge and don't carry data) and the CH340 driver (step 2).
- **"Access is denied" / port busy:** another program has the Arduino's port open, such as the Arduino IDE's Serial Monitor or a second dashboard window. Close it and try again.
- **Start again from scratch:** delete `dashboard\.venv`, and `.tools\` if it exists, then run `run.bat` again.

## Libraries (Arduino IDE → Library Manager)

- DHT sensor library (Adafruit) + Adafruit Unified Sensor
- U8g2 (firmware OLED; text-only U8x8 mode to save RAM)
- Adafruit GFX + Adafruit SSD1306 (tests 03 and 10 only)

## Firmware

- Reads the sensors every 5 s and shows them on the OLED (one weather page plus uptime; GSM details are only in the `STATUS` SMS reply).
- Prints one JSON line per reading over USB at 9600 baud: `{"t":24.5,"h":61,"light":72,"rain":"dry","rain_raw":987,"csq":18}`. SMS events are printed as `{"sms":"in|out|ignored|fail",...}`.
- **SMS commands** from numbers in `AUTHORIZED`:
    - `WEATHER`: full report with trend, plus the online rain forecast that the dashboard sends over USB every 5 min (left out if the laptop has been disconnected for more than 90 min)
    - `STATUS`: signal, uptime, free RAM
    - `HELP`: list of commands
- **Alerts:** rain starting, temperature crossing the high/low limits, a sharp light drop. Each type is sent at most once per 30 min.
- **Hourly report:** optional, set `HOURLY_REPORT 1` in `config.h`.
- **Without a working SIM800L:** keeps running and retries the module every 30 s.

**Backup SMS:** if the SIM800L isn't available, the dashboard can handle the same commands and alerts through an Android phone running "SMS Gateway for Android". It's off until set up. The phone's address, login and approved numbers are entered and changed in the dashboard's SMS phone card, which also shows whether the phone is connected. See [`dashboard/README.md`](dashboard/README.md#backup-sms-through-an-android-phone).

## Status (2026-10-21)

- Tests 01–06 and 10 pass on the hardware. Firmware runs on the hardware with all sensors and the OLED. The OLED shows only the weather page now.
- **Rain sensor:** it has been reading about 630 when it should be about 1010 dry. This needs checking before it's calibrated to three levels (dry / light rain / heavy rain).
- **The first SIM800L is faulty** (TXD output dead). Tests 07–09 and the SMS features are waiting for a replacement module.
- Dashboard checked with `test_dashboard.py`, fake data and live Open-Meteo data, and run with the real Arduino. The forecast line reaches the Arduino.
- **Phone SMS backup:** the dashboard connects to a real phone running SMS Gateway for Android. Testing with real texts is next.
