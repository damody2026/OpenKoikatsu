-- Prints the Unreal Engine root this checkout builds and runs with (forward slashes, no trailing slash).
-- Lookup: --engine, then ok.engine_root() (OK_ENGINE_ROOT, Tools/engine.local.json, default; see Tools/lua/ok/init.lua).
-- Lua scripts call ok.engine_root() directly; .cmd/.bat launchers read this script's last output line:
--   for /f "usebackq delims=" %%E in (`"%~dp0Tools\lua\lua.exe" "%~dp0Tools\ok_engine.lua"`) do set "OK_ENGINE=%%E"
--   Tools\lua\lua.exe Tools\ok_engine.lua [--engine <root>] [--print]
local ok = require "ok"
local args = ok.args.parser("Print the Unreal Engine root"):opt("--engine", { default = "" }):flag("--print"):parse(arg)
local root = args.engine ~= "" and (args.engine:gsub("\\", "/"):gsub("/+$", "")) or ok.engine_root()
if args.print then
  local editor = root .. "/Engine/Binaries/Win64/UnrealEditor.exe"
  print("engine: " .. root)
  print("editor: " .. editor .. (ok.path.is_file(editor) and " (found)" or " (missing)"))
end
print(root)
