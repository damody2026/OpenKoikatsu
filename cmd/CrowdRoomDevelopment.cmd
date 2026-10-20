@echo off
"%~dp0..\Tools\lua\lua.exe" "%~dp0..\Tools\run_crowd_optimized.lua" %*
if errorlevel 1 pause
