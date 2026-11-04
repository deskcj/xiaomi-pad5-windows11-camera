@echo off
setlocal EnableExtensions
cd /d "%~dp0"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup.ps1" -Mode Install
set "CameraSetupExit=%ERRORLEVEL%"
echo.
if "%CameraSetupExit%"=="3010" goto restart
if "%CameraSetupExit%"=="0" goto success
if "%CameraSetupExit%"=="2" goto incomplete
echo SETUP STOPPED. See the error above.
echo Logs: C:\ProgramData\XiaomiPad5Camera\Logs
goto finish
:incomplete
echo DRIVERS INSTALLED; CAMERA VERIFICATION INCOMPLETE.
echo Review the message above and save the complete newest Logs folder.
echo Both cameras have not passed the complete automated test yet.
echo Logs: C:\ProgramData\XiaomiPad5Camera\Logs
goto finish
:restart
echo RESTART REQUIRED.
echo In Windows select Restart, then run Setup.cmd again.
echo Keep this archive extracted. The installer will continue after the restart.
goto finish
:success
echo SUCCESS: both cameras produced images and closed normally.
:finish
pause
exit /b %CameraSetupExit%
