-- Runs one StreetSocialCity editor stage through Tools/run_street_city_unreal_task.py inside UnrealEditor
-- (Assemble / ImportShops / PrepareShops / Campus / InteriorAuditCollision headless with -NullRHI, Render / InteriorAudit
-- with -RenderOffscreen) and checks the task's status file in
-- output/street-social-city-production/world-ground-v1/unreal-<stage>-task.json.
-- ImportShops first prepares the 288 shop floor assets in 8 PrepareShops batches of 12 shops, unless the previous import
-- or the batch receipts show every source unchanged.
--   Tools\lua\lua.exe Tools\run_street_city_unreal.lua [--stage <stage>] [--engine-root <UE>] [--interior-after]
--     [--shop-batch-start 1] [--shop-batch-end 12]
local ok = require "ok"
local path, json, hash = ok.path, ok.json, ok.hash

local args = ok.args.parser("Run a StreetSocialCity Unreal stage")
  :opt("--stage", { default = "Assemble", choices = { "Assemble", "Render", "InteriorAudit", "ImportShops", "PrepareShops", "Campus", "InteriorAuditCollision" } })
  :opt("--engine-root", { default = "" }):flag("--interior-after")
  :opt("--shop-batch-start", { type = "int", default = 1 }):opt("--shop-batch-end", { type = "int", default = 12 })
  :parse(arg)
local root = ok.root
local engine = args.engine_root ~= "" and args.engine_root or ok.engine_root()
local output = root .. "/output/street-social-city-production/world-ground-v1"
path.mkdir(output, true)

local function sha(p) return hash.sha256_file(p):lower() end
local function read_json(p) return json.decode(path.read_text(p)) end

local function run_stage(stage, batch_start, batch_end)
  local lower = stage:lower()
  local log = output .. "/unreal-" .. lower .. "-editor.log"
  local status_path = output .. "/unreal-" .. lower .. "-task.json"
  path.write_text(status_path, json.encode({ status = "launching", stage = stage, startedUtc = ok.time.now_utc_z() }))
  local cmd = { engine .. "/Engine/Binaries/Win64/UnrealEditor.exe", root .. "/StreetSocialCity/StreetSocialCity.uproject",
    "-ExecutePythonScript=" .. root .. "/Tools/run_street_city_unreal_task.py", "-CityStage=" .. stage,
    "-UserDir=E:/StreetSocialCityProduction/unreal-userdata", "-unattended", "-nosplash", "-nosound", "-nop4", "-abslog=" .. log }
  if args.interior_after then cmd[#cmd + 1] = "-CityInteriorAfter" end
  if stage == "PrepareShops" then
    cmd[#cmd + 1] = "-CityShopStart=" .. batch_start; cmd[#cmd + 1] = "-CityShopEnd=" .. batch_end
  end
  local headless = { Assemble = true, ImportShops = true, PrepareShops = true, Campus = true, InteriorAuditCollision = true }
  cmd[#cmd + 1] = headless[stage] and "-NullRHI" or "-RenderOffscreen"
  -- Temp files and the DDC on the production drive.
  local scratch = "E:/StreetSocialCityProduction/unreal-scratch"
  path.mkdir(scratch, true)
  local env = { TEMP = scratch, TMP = scratch, ["UE-LocalDataCachePath"] = "E:/StreetSocialCityProduction/unreal-ddc" }
  local saved = {}
  for k, v in pairs(env) do saved[k] = ok.fs.getenv(k) or false; ok.fs.setenv(k, v) end
  local child = ok.proc.popen(cmd, { cwd = root, stdout = "NUL", stderr = "NUL", hide = true })
  for k, v in pairs(saved) do ok.fs.setenv(k, v or nil) end
  local pid = child:pid()
  local code = child:wait()
  path.write_text(output .. "/unreal-" .. lower .. "-process.json",
    json.encode({ stage = stage, processId = pid, exitCode = code, endedUtc = ok.time.now_utc_z() }))
  if not path.is_file(status_path) then error("Unreal exited before the script started: " .. log, 0) end
  local status = read_json(status_path)
  if code ~= 0 or status.status ~= "succeeded" then
    error(("Unreal %s failed (exit %s, script %s): %s"):format(stage, tostring(code), tostring(status.status), log), 0)
  end
  print("Unreal " .. stage .. " verified")
end

-- ImportShops: are the 288 prepared shop floor assets current?
local function shops_need_preparation()
  local previous = output .. "/unreal-shops-import.json"
  if path.is_file(previous) then
    local prior = read_json(previous)
    if prior.status == "passed" and #(prior.floors or {}) == 288 then
      for _, floor in ipairs(prior.floors) do
        if not path.is_file(floor.source) or sha(floor.source) ~= floor.source_sha256 then goto prepare_check end
      end
      return false
    end
  end
  ::prepare_check::
  local prepared = 0
  local prepare_sha = sha(root .. "/Tools/prepare_street_city_unreal_shop_assets.py")
  for first = 1, 96, 12 do
    local last = math.min(96, first + 11)
    local receipt = ("%s/unreal-shops-prepare-%02d-%02d.json"):format(output, first, last)
    if not path.is_file(receipt) then break end
    local batch = read_json(receipt)
    if batch.status ~= "passed" or batch.source_sha256 ~= prepare_sha or #(batch.floors or {}) ~= 36 then break end
    for _, floor in ipairs(batch.floors) do
      local building, number = floor.id:match("^(.-)_F(.*)$")
      local input = ("%s/output/street-social-city-production/%s-modular-components-v1/assembly-building/%s-F%s-complete-2k.glb"):format(root, building, building, number)
      local asset = root .. "/StreetSocialCity/Content/" .. floor.asset:match("^([^.]*)"):gsub("^/Game/", "") .. ".uasset"
      if path.is_file(input) and path.is_file(asset) and sha(input) == floor.source_sha256 then prepared = prepared + 1 end
    end
  end
  if prepared == 288 then
    print("Reusing 288 exact-source prepared assets; full native map binding and collision still required")
    return false
  end
  return true
end

if args.stage == "ImportShops" then
  local task = output .. "/unreal-importshops-task.json"
  path.write_text(task, json.encode({ status = "preparing_assets", stage = args.stage, startedUtc = ok.time.now_utc_z() }))
  if os.getenv("STREET_CITY_GROK_PREWARM") == "1" then
    local warm = root .. "/output/street-social-city-production/shop-brand-grok-v2/native-prewarm.json"
    local deadline = ok.time.clock() + 2 * 3600
    while true do
      if path.is_file(warm) then
        local w = read_json(warm)
        if w.status == "passed_asset_preparation_only" and #(w.completed_batches or {}) == 8 then break end
      end
      if ok.time.clock() > deadline then error("Asset prewarm did not finish within the bounded wait", 0) end
      ok.time.sleep(5)
    end
  end
  if shops_need_preparation() then
    local okv, err = pcall(function()
      for first = 1, 96, 12 do run_stage("PrepareShops", first, math.min(96, first + 11)) end
    end)
    if not okv then
      path.write_text(task, json.encode({ status = "failed", stage = args.stage, error = tostring(err) }))
      error(err, 0)
    end
  end
end
run_stage(args.stage, args.shop_batch_start, args.shop_batch_end)
