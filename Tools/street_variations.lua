-- Portable street asset variants; never modifies StreetSocialCity/.
local mode = arg[1] or 'serve'
assert(mode == 'build' or mode == 'serve' or mode == 'verify' or mode == 'publish', 'build | serve | verify | publish')
local root = assert(io.popen('cd')):read('*l'):gsub('\\', '/')
local exe = root .. '/Tools/street-variation/target/release/street-variation.exe'
local function run(command)
  if command:sub(1, 1) == '"' then command = '"' .. command .. '"' end
  local ok, _, code = os.execute(command)
  assert(ok, 'Command failed: ' .. tostring(code))
end
if mode == 'build' then
  local dependency = io.open(root .. '/Tools/street-variation/viewer-deps/node_modules/three/build/three.module.js', 'rb')
  if dependency then dependency:close() else
    run('npm.cmd ci --prefix "' .. root .. '/Tools/street-variation/viewer-deps" --ignore-scripts --no-audit --no-fund')
  end
  run('cargo build --offline --release --manifest-path "' .. root .. '/Tools/street-variation/Cargo.toml"')
end
run('"' .. exe .. '" ' .. mode .. ' "' .. root .. '"')
