@echo off

setlocal
rem Ten-person test of CrowdRoom100.cmd: the first ten cards of
rem Content\CharacterCards\CrowdAPNG\selection.json, listed in
rem Saved\Crowd\CrowdRoom10\selection.json. Each card's .lowpoly.okmap
rem (Instant Meshes remesh, one pass at the default share) replaces its
rem source meshes; hair is left to the room's own edge-collapse reduction.
rem Character lighting follows the camera view direction (--view-light).

"%~dp0Tools\lua\lua.exe" "%~dp0Tools\run_crowd_runtime_apng_development.lua" --low-poly --view-light --executable "%~dp0Saved\Crowd\CrowdRoom100\receipt.json" --selection-file "%~dp0Saved\Crowd\CrowdRoom10\selection.json" %*

set "taskExitCode=%errorlevel%"
if not "%taskExitCode%"=="0" pause
exit /b %taskExitCode%
