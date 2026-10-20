@echo off
setlocal
rem Separate original street: one male and five player-selected APNG female NPCs (Tools\run_street_social_development.lua).
"%~dp0Tools\lua\lua.exe" "%~dp0Tools\run_street_social_development.lua" %*
set "taskExitCode=%errorlevel%"
if not "%taskExitCode%"=="0" pause
exit /b %taskExitCode%
