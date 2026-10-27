@echo off

setlocal
rem The hundred-girl room: the StreetSocial runtime characters (same build, materials, GPU poses and shadows as the
rem street's heroines) wandering an empty room. Each girl stands about in one of her standing poses, now and then
rem walks somewhere else. WASD / Q,E move the camera, hold the right mouse button to look, Shift faster.
rem Cards: Content\CharacterCards\CrowdAPNG. Uses the newest Saved\StreetSocial\Build\*.stage.
rem   CrowdRoom100.cmd --benchmark 60    measure 60 s once everyone is drawn (report.json + screenshot), then quit

"%~dp0Tools\lua\lua.exe" "%~dp0Tools\run_crowd_room100.lua" %*

set "taskExitCode=%errorlevel%"
if not "%taskExitCode%"=="0" pause
exit /b %taskExitCode%
