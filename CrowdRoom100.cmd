@echo off

setlocal
rem Native APNG room with the shared StreetSocial toon materials and display profile.
rem FXAA + 150%% supersampling; progressive GPU entry with 3-second fade-in.
rem GPU pose cache defaults to 4 GiB. WASD is available while characters load.
rem Default APNG cards and selection: Content\CharacterCards\CrowdAPNG.
rem Low models: each card's .lowpoly.okmap (Instant Meshes remesh, written by
rem ok-lowpoly or the APNG export) replaces its source meshes; skinning keeps the
rem card's own palette. Room-only host pinned by Saved\Crowd\CrowdRoom100\receipt.json.

"%~dp0Tools\lua\lua.exe" "%~dp0Tools\run_crowd_runtime_apng_development.lua" --low-poly --executable "%~dp0Saved\Crowd\CrowdRoom100\receipt.json" %*

set "taskExitCode=%errorlevel%"
if not "%taskExitCode%"=="0" pause
exit /b %taskExitCode%
