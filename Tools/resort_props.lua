local mode=arg[1] or "build"
assert(mode=="build" or mode=="verify" or mode=="serve","build | verify | serve")
local p=assert(io.popen("cd"));local root=assert(p:read("*l"));p:close()
local function run(s) local ok,why,code=os.execute(s);assert(ok,"command failed: "..s.." ("..tostring(code)..")") end
if mode=="build" then run("cargo build --offline --release --manifest-path Tools/resort-props/Cargo.toml") end
local exe='"'..root..'/Tools/resort-props/target/release/resort-props.exe"'
run('"'..exe..' '..mode..' "'..root..'""')
if mode=="build" then run('"'..exe..' verify "'..root..'""') end
