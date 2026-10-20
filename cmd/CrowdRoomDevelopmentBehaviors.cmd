@echo off
"%~dp0..\Tools\lua\lua.exe" "%~dp0..\Tools\run_crowd_behavior_development.lua" %*
if errorlevel 1 pause
