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
local a = parser:parse(arg)
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
        if a.gpu_probe then
            fs.setenv("OK_PROBE_GPU_LIBRARY", path.abs(a.gpu_probe))
            fs.setenv("OK_PROBE_GPU_MESH", a.mesh)
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
