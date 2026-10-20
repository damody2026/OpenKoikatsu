local ok = require "ok"
local json, P = ok.json, ok.path
local rename = require "rename_native_resources"
local migrate = rename.migrate

local T = ok.test.suite("RenameNativeResourcesTests")

local function fixture()
  local folder = P.join(ok.root, "Saved/Tests/NativeResourceNames", require("build_native_resources365").uuid_hex())
  local pack = P.join(folder, "pack")
  P.mkdir(P.join(pack, "bundles"))
  local payload = "original resource payload"
  local sha = ok.hash.sha256(payload)
  local source = P.join(pack, "bundles", sha)
  P.write_bytes(source, payload)
  local rows = json.array({})
  for _, p in ipairs({ "abdata/studio/sound/bgm/bgm_27.unity3d", "abdata/chara/co_top_00.unity3d" }) do
    rows[#rows + 1] = json.object({ { "path", p }, { "sha256", sha }, { "bytes", #payload } })
  end
  P.write_text(P.join(pack, "catalog.json"), json.encode(json.object({ { "schema", 1 }, { "kind", "native-base-bundles" }, { "entries", rows } })))
  return pack, P.join(folder, "catalog.before.json"), source, payload
end

T:case("test_legacy_shared_blob_becomes_readable_links", function(t)
  local pack, backup, source, payload = fixture()
  local inode = P.file_id(source).index
  local original = P.read_bytes(P.join(pack, "catalog.json"))
  migrate(pack, backup)
  local rows = json.decode(P.read_bytes(P.join(pack, "catalog.json"))).entries
  t:eq(P.read_bytes(backup), original)
  t:falsy(P.exists(source))
  for _, row in ipairs(rows) do
    local file = P.join(pack, "bundles", row.file)
    t:eq(row.file, (row.path:gsub("/", "@")))
    t:eq(P.read_bytes(file), payload)
    t:eq(P.file_id(file).index, inode)
  end
  t:truthy(P.samefile(P.join(pack, "bundles", rows[1].file), P.join(pack, "bundles", rows[2].file)))
end)

T:case("test_failed_publication_restores_old_names", function(t)
  local pack, backup, source, payload = fixture()
  local original = P.read_bytes(P.join(pack, "catalog.json"))
  local real_link = rename.link
  local calls = {}
  rename.link = function(a, b)
    calls[#calls + 1] = b
    if #calls == 2 then error("OSError: injected link failure", 0) end
    return real_link(a, b)
  end
  local okv, err = pcall(migrate, pack, backup)
  rename.link = real_link
  t:falsy(okv)
  t:truthy(tostring(err):find("injected link failure", 1, true), "error " .. tostring(err))
  t:eq(P.read_bytes(P.join(pack, "catalog.json")), original)
  t:eq(P.read_bytes(source), payload)
  t:eq(P.iterdir(P.join(pack, "bundles")), { source })
end)

T:run()
