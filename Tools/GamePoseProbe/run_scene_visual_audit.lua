-- Read-only original Studio scene inputs and camera captures.
-- Build Tools/GamePoseProbe/build.lua first. Uses a temporary installed probe.
local ok = require "ok"
local fs = require "okfs"
local path = ok.path
local parser = ok.args.parser("Native Studio scene shader and post-process audit")
parser:opt("--scenes", { nargs = "+", default = {} })
parser:flag("--empty")
parser:opt("--tag", { required = true })
parser:opt("--gpu-probe")
parser:opt("--mesh")
parser:opt("--pass", { default = "1" })
parser:flag("--frame-mesh")
parser:flag("--mesh-snapshot")
parser:flag("--input-streams")
parser:flag("--stages")
parser:flag("--lightmaps")
parser:flag("--normals")
parser:flag("--shadow-mask")
-- Every property of the materials whose shader name contains one of these |-separated words (or all), as `prop` rows
-- of Scene<n>.scene.tsv.
parser:opt("--material-props")
parser:opt("--hide")
-- The --hide list from a UTF-8 text file, one path part per line (paths with non-ASCII names).
parser:opt("--hide-file")
-- Renderers (path parts, comma-separated) hidden for the --shadow-mask render only.
parser:opt("--depth-hide")
parser:opt("--white")
parser:opt("--mesh-dump")
local a = parser:parse(arg)
assert(not a.mesh_snapshot or a.gpu_probe, "Mesh snapshots need --gpu-probe and --mesh")
assert(not a.input_streams or a.gpu_probe, "Input streams need --gpu-probe and --mesh")
if a.gpu_probe then assert(a.mesh and path.is_file(a.gpu_probe), "GPU capture needs --mesh and a built probe DLL") end
assert(a.tag:match("^[%w_%-]+$"), "Invalid audit tag")
local game = ok.game_root()
local output = path.join(ok.root, "Saved/Tests", a.tag)
assert(not path.exists(output), "Use a fresh audit tag")
local scenes = {}
if a.empty then scenes[1] = "@empty" end
for i, scene in ipairs(a.scenes) do
    local resolved = path.abs(scene)
    assert(path.is_file(resolved), "Missing scene: " .. resolved)
    scenes[#scenes + 1] = resolved
end
assert(#scenes > 0, "Supply --scenes or --empty")
local installed = path.join(game, "BepInEx/plugins/OpenKoikatsu.GamePoseProbe.dll")
assert(not path.exists(installed), "Probe already installed; retain its owner's plugin")
for _, process in ipairs(ok.kit.call({ "proc-list", "CharaStudio.exe" }) or {}) do
    error("Existing Studio process must retain its plugins: " .. tostring(process.pid))
end
path.mkdir(output)
path.copy(path.join(ok.root, "Tools/GamePoseProbe/OpenKoikatsu.GamePoseProbe.dll"), installed)
local owned
local success, err = xpcall(function()
    for i, scene in ipairs(scenes) do
        -- Native fixture modes must not leak in from another probe invocation.
        local env = ok.proc.run({ "cmd.exe", "/D", "/C", "set", "OK_" }, { capture = true })
        for line in (env.stdout or ""):gmatch("[^\n]+") do
            local name = line:match("^([^=]+)=")
            if name then fs.setenv(name, nil) end
        end
        local prefix = path.join(output, "Scene" .. i)
        fs.setenv("OK_PROBE_STUDIO_SCENE", scene)
        fs.setenv("OK_GAME_POSE_OUTPUT", prefix)
        if a.stages then fs.setenv("OK_PROBE_CAPTURE_STAGES", "1") end
        if a.lightmaps then fs.setenv("OK_PROBE_LIGHTMAPS", "1") end
        if a.normals then fs.setenv("OK_PROBE_NORMALS", "1") end
        if a.shadow_mask then fs.setenv("OK_PROBE_SHADOW_MASK", "1") end
        if a.material_props then fs.setenv("OK_PROBE_MATERIAL_PROPS", a.material_props) end
        local hide = a.hide
        if a.hide_file then
            local parts = {}
            for line in path.read_text(a.hide_file):gmatch("[^\r\n]+") do parts[#parts + 1] = line end
            if hide then table.insert(parts, 1, hide) end
            hide = table.concat(parts, ",")
        end
        if hide then fs.setenv("OK_PROBE_HIDE", hide) end
        if a.depth_hide then fs.setenv("OK_PROBE_DEPTH_HIDE", a.depth_hide) end
        if a.white then fs.setenv("OK_PROBE_WHITE", a.white) end
        if a.mesh_dump then fs.setenv("OK_PROBE_MESH", a.mesh_dump) end
        if a.gpu_probe then
            fs.setenv("OK_PROBE_GPU_LIBRARY", path.abs(a.gpu_probe))
            fs.setenv("OK_PROBE_GPU_MESH", a.mesh)
            fs.setenv("OK_PROBE_GPU_PASS", a.pass)
            if a.frame_mesh then fs.setenv("OK_PROBE_GPU_FRAME", "1") end
            if a.mesh_snapshot then fs.setenv("OK_PROBE_GPU_MESH_SNAPSHOT", "1") end
            if a.input_streams then fs.setenv("OK_PROBE_GPU_INPUT_STREAMS", "1") end
        end
        local arguments = { path.join(game, "CharaStudio.exe"), "-windowed",
            "-screen-width", "1280", "-screen-height", "720" }
        if a.gpu_probe then arguments[#arguments + 1] = "-force-d3d11" end
        owned = ok.proc.popen(arguments,
            { hide = true, stdout = "NUL", stderr = "NUL" })
        path.write_text(prefix .. ".pid", tostring(owned:pid()))
        local deadline = ok.time.time() + 180
        while not path.exists(prefix) do
            assert(not path.exists(prefix .. ".error.txt"), "Native scene load failed: " .. scene)
            assert(not owned:poll(), "Native exited before scene audit completion")
            assert(ok.time.time() < deadline, "Native scene audit timeout: " .. scene)
            ok.time.sleep(1)
        end
        assert(path.exists(prefix .. ".scene.tsv") and path.exists(prefix .. ".png"), "Incomplete scene audit")
        assert(path.read_text(prefix):find("^PASS:"), "Native scene audit returned an error")
        if a.gpu_probe then assert(ok.read_json(prefix .. ".gpu.json").passed, "GPU constant-buffer capture failed") end
        if owned:wait(15) == nil then owned:kill(); owned:wait() end
        print("Native scene audit complete: " .. scene .. " -> " .. prefix)
        io.stdout:flush()
        owned = nil
    end
end, debug.traceback)
if owned and not owned:poll() then owned:kill(); owned:wait() end
if path.exists(installed) then path.rename(installed, path.join(output, "probe.last-run.dll")) end
if not success then error(err) end
