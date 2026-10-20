@echo off
setlocal
"%~dp0..\Tools\lua\lua.exe" "%~dp0..\Tools\run_crowd_grouped_development.lua" %*
if errorlevel 1 pause
