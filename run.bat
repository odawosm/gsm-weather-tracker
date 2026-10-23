@echo off
rem One command for the whole project on Windows. Double-click it, or run it from a Command Prompt in this folder.
rem   run.bat            Arduino plugged in (close the Arduino IDE Serial Monitor first)
rem   run.bat --fake     no hardware: fake readings
rem   run.bat --upload   first load the station program onto the Arduino, then start the dashboard
rem First run: installs Python if it's missing (winget, for this user only), then the dashboard's packages.
rem --upload the first time: downloads arduino-cli into .tools\ plus the Uno support and libraries.
rem Later runs only start things. Stop with Ctrl+C, or close the browser tab.
setlocal
set "ROOT=%~dp0"
cd /d "%ROOT%dashboard"

for %%a in (%*) do (
  if /i "%%a"=="--fake" set FAKE=1
  if /i "%%a"=="--upload" set UPLOAD=1
)

if not exist .venv\Scripts\python.exe call :make_venv
if not exist .venv\Scripts\python.exe call :install_python
if not exist .venv\Scripts\python.exe call :make_venv
if not exist .venv\Scripts\python.exe (
  echo Python 3 could not be set up automatically.
  echo Install it from https://www.python.org/downloads/ and tick "Add python.exe to PATH", then run this again.
  pause
  exit /b 1
)

rem Quick when everything is already installed (no download needed).
.venv\Scripts\python -m pip install -q --disable-pip-version-check -r requirements.txt
if errorlevel 1 (
  echo Installing the Python packages failed. Check the internet connection and try again.
  pause
  exit /b 1
)

if defined UPLOAD (
  call :upload
  if errorlevel 1 (
    echo Loading the station program failed: see the messages above.
    pause
    exit /b 1
  )
)

.venv\Scripts\python -u app.py
if errorlevel 1 pause
exit /b


:make_venv
rem The "python" that ships with Windows is only a Microsoft Store shortcut: the import check skips it.
py -3 -c "import sys" >nul 2>nul && py -3 -m venv .venv
if exist .venv\Scripts\python.exe exit /b 0
python -c "import sys" >nul 2>nul && python -m venv .venv
if exist .venv\Scripts\python.exe exit /b 0
rem Just installed by winget: not on this window's PATH yet, so use its folder directly.
for /d %%d in ("%LOCALAPPDATA%\Programs\Python\Python3*") do (
  if not exist .venv\Scripts\python.exe if exist "%%d\python.exe" "%%d\python.exe" -m venv .venv
)
exit /b 0


:install_python
where winget >nul 2>nul || exit /b 0
echo Python 3 is not installed. Installing it now for this user (one time, about 30 MB)...
winget install -e --id Python.Python.3.12 --scope user --silent --accept-package-agreements --accept-source-agreements
exit /b 0


:upload
set "CLI=%ROOT%.tools\arduino-cli.exe"
if exist "%CLI%" goto have_cli
echo Downloading arduino-cli (one time)...
if not exist "%ROOT%.tools" mkdir "%ROOT%.tools"
curl -L -s -S -o "%ROOT%.tools\cli.zip" https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip || exit /b 1
tar -xf "%ROOT%.tools\cli.zip" -C "%ROOT%.tools" || exit /b 1
del "%ROOT%.tools\cli.zip"
:have_cli
echo Checking the Arduino board support and libraries (the first time downloads about 150 MB)...
"%CLI%" core update-index >nul || exit /b 1
"%CLI%" core install arduino:avr || exit /b 1
"%CLI%" lib install U8g2 "DHT sensor library" || exit /b 1

rem A genuine Uno is listed as arduino:avr:uno; clones (CH340 chip) only as a COM port, so fall back to the first one.
set "PORT="
for /f "tokens=1" %%p in ('call "%CLI%" board list ^| findstr /i "arduino:avr:uno"') do if not defined PORT set "PORT=%%p"
if not defined PORT for /f "tokens=1" %%p in ('call "%CLI%" board list ^| findstr /b "COM"') do if not defined PORT set "PORT=%%p"
if not defined PORT (
  echo No Arduino found. Plug it in with the USB cable and run this again.
  exit /b 1
)
echo Loading the station program onto the Arduino on %PORT%...
"%CLI%" compile --fqbn arduino:avr:uno --upload -p %PORT% "%ROOT%firmware\weather_station" || exit /b 1
exit /b 0
