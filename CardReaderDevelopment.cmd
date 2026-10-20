@echo off
setlocal
rem Card reader in UnrealEditor -game (Tools\run_card_reader_development.lua). Rebuilds OpenKoikatsuEditor first when
rem the shared engine no longer matches this checkout's build.
"%~dp0Tools\lua\lua.exe" "%~dp0Tools\run_card_reader_development.lua" %*
set "taskExitCode=%errorlevel%"
if not "%taskExitCode%"=="0" pause
exit /b %taskExitCode%
