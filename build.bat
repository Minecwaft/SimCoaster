@echo off
if /I "%~3"=="--keep-going" goto keep_going
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %errorlevel%

:keep_going
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" "%~1" "%~2" -KeepGoing
exit /b %errorlevel%
