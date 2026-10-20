-- Version-checked, reversible patch for the installed binary-only UECP shell.
--
-- Only the automatic news/personal-notice popup dispatcher is disabled. The
-- plugin's main UI, manual What's New overlay, authentication and other tools
-- remain intact. Unknown binaries are refused, never patched by guessed offsets.
local ok = require "ok"
local json, P = ok.json, ok.path

local ORIGINAL_SHA = "9496db41e98ad6e9a936a641a3d104c41a8ab9cfbfb05b3e91fdae48ad69b686"
local FUNCTION_RVA = 0x9DB70
local BEFORE = "\x48\x89\x5c\x24\x10"
local AFTER = "\xc3\x90\x90\x90\x90" -- void callback: return before allocating or dispatching a modal

local function fail(msg) error("ValueError: " .. msg, 0) end
local function win(p) return (P.norm(p):gsub("/", "\\")) end
-- sha256 of a large byte string through the Rust hasher.
local function sha(data)
  local tmp = P.temp(".bin"); P.write_bytes(tmp, data)
  local digest = ok.hash.file(tmp, "sha256"); P.remove(tmp, true)
  return digest
end

-- Returns a 0-based file offset.
local function offset_of(data, rva)
  local pe = string.unpack("<I4", data, 0x3c + 1)
  if data:sub(1, 2) ~= "MZ" or data:sub(pe + 1, pe + 4) ~= "PE\0\0" then fail("Expected a PE DLL") end
  local count = string.unpack("<I2", data, pe + 6 + 1)
  local size = string.unpack("<I2", data, pe + 20 + 1)
  for i = 0, count - 1 do
    local pos = pe + 24 + size + i * 40
    local virtual = string.unpack("<I4", data, pos + 12 + 1)
    local raw_size, raw = string.unpack("<I4I4", data, pos + 16 + 1)
    if virtual <= rva and rva < virtual + raw_size then return raw + rva - virtual end
  end
  fail("Callback RVA does not exist")
end

local function splice(data, offset, bytes) return data:sub(1, offset) .. bytes .. data:sub(offset + #bytes + 1) end

local function main()
  local p = ok.args.parser()
  p:flag("--restore")
  p:flag("--verify")
  local args = p:parse(arg)
  local root = P.parent(ok.script_dir)
  local dll = P.join(root, "Plugins/BpGeneratorUltimate/Binaries/Win64/UnrealEditor-UECPShell.dll")
  local directory = P.join(root, "Saved/PluginPatches/BpGeneratorUltimate-NoStartupNews")
  local backup = P.join(directory, "UnrealEditor-UECPShell.original.dll")
  local data = P.read_bytes(dll)
  local offset = offset_of(data, FUNCTION_RVA)
  local patched = splice(data, offset, BEFORE)
  if sha(patched) ~= ORIGINAL_SHA then fail("Unsupported or independently modified DLL; no changes made") end
  local cur = data:sub(offset + 1, offset + #BEFORE)
  if cur ~= BEFORE and cur ~= AFTER then fail("Unexpected callback bytes; no changes made") end
  if args.verify then
    if data:sub(offset + 1, offset + #AFTER) ~= AFTER then fail("Startup news callback is still enabled") end
    print("Verified: automatic news popup dispatcher returns without creating a window")
    return
  end
  P.mkdir(directory)
  if not P.exists(backup) then P.write_bytes(backup, patched) end
  if ok.hash.file(backup, "sha256") ~= ORIGINAL_SHA then fail("Original backup hash mismatch") end
  local result = patched
  if not args.restore then result = splice(patched, offset, AFTER) end
  if data ~= result then P.write_bytes(dll, result) end
  local changed = 0
  for i = 1, #AFTER do if patched:byte(offset + i) ~= result:byte(offset + i) then changed = changed + 1 end end
  local report = json.object({
    { "disabled", not args.restore }, { "dll", win(dll) }, { "backup", win(backup) },
    { "original_sha256", ORIGINAL_SHA }, { "current_sha256", sha(result) },
    { "function_rva", ("0x%x"):format(FUNCTION_RVA) }, { "file_offset", ("0x%x"):format(offset) },
    { "scope", "automatic news and personal-notice popup dispatcher" },
    { "changed_bytes", changed } })
  P.write_text(P.join(directory, "receipt.json"), json.encode(report, { indent = 2 }))
  print(json.encode(report, { indent = 2 }))
end

ok.main(main)
