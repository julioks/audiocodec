@echo off
setlocal

set "LOCAL_CLI=%~dp0.tools\arduino-cli.exe"
set "IDE_CLI=%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
set "PROJECT_CONFIG=%~dp0arduino-cli.yaml"
set "ARDUINO_CLI="

if exist "%LOCAL_CLI%" (
  set "ARDUINO_CLI=%LOCAL_CLI%"
)

if not defined ARDUINO_CLI if exist "%IDE_CLI%" (
  set "ARDUINO_CLI=%IDE_CLI%"
)

if not defined ARDUINO_CLI (
  echo arduino-cli.exe was not found. Expected "%LOCAL_CLI%" or "%IDE_CLI%". 1>&2
  exit /b 1
)

if exist "%PROJECT_CONFIG%" (
  "%ARDUINO_CLI%" --config-file "%PROJECT_CONFIG%" %*
) else (
  "%ARDUINO_CLI%" %*
)

exit /b %ERRORLEVEL%
