-- Builds the character editor MCP servers (Rust, Tools/character_mcp/rust) and copies
-- ok-character-mcp.exe and ok-pair-mcp.exe into Tools/character_mcp/bin.
--   Tools\lua\lua.exe Tools\character_mcp\build.lua [--test]   (--test runs `cargo test --locked` first)
local ok = require "ok"
local path = ok.path

local function main()
  local p = ok.args.parser("Build the character editor MCP servers")
  p:flag("--test")
  local a = p:parse(arg)
  local here = ok.script_dir
  local rust = path.join(here, "rust")
  if a.test then
    if not ok.proc.run({ "cargo", "test", "--locked" }, { cwd = rust }).ok then error("Rust tests failed", 0) end
  end
  if not ok.proc.run({ "cargo", "build", "--release", "--locked" }, { cwd = rust }).ok then error("Rust MCP build failed", 0) end
  local output_directory = path.join(here, "bin")
  path.mkdir(output_directory)
  for _, name in ipairs({ "ok-character-mcp.exe", "ok-pair-mcp.exe" }) do
    local dst = path.join(output_directory, name)
    path.copy(path.join(rust, "target/release", name), dst)
    print((dst:gsub("/", "\\")))
  end
end

ok.main(main)
