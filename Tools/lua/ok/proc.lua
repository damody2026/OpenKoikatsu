-- Subprocesses (subprocess.run-like). Argument lists are quoted per the MSVC
-- argv convention and started with CreateProcessW (okfs.run) - no cmd.exe, so
-- & | < > ^ % in arguments are passed literally. Use {shell=true} with a
-- string to get cmd.exe semantics.
local fs = require "okfs"
local path = require "ok.path"
local M = {}

-- Quote one argument per MSVC argv rules.
function M.quote(a)
  a = tostring(a)
  if a ~= "" and not a:find('[%s"]') then return a end
  local out, bs = { '"' }, 0
  for c in a:gmatch(".") do
    if c == "\\" then bs = bs + 1
    elseif c == '"' then out[#out + 1] = string.rep("\\", bs * 2 + 1) .. '"'; bs = 0
    else out[#out + 1] = string.rep("\\", bs) .. c; bs = 0 end
  end
  out[#out + 1] = string.rep("\\", bs * 2) .. '"'
  return table.concat(out)
end

function M.cmdline(args)
  if type(args) == "string" then return args end
  local q = {}
  for i, a in ipairs(args) do q[i] = M.quote(a) end
  return table.concat(q, " ")
end

-- Batch files and builtins need cmd.exe; CreateProcess cannot start them directly.
local function needs_cmd(first)
  first = tostring(first or ""):lower()
  return first:match("%.bat$") or first:match("%.cmd$") or first == "echo" or first == "dir" or first == "type" or first == "copy" or first == "del" or first == "mklink"
end

local function full_line(args, opts)
  if type(args) == "string" then
    if opts.shell == false then return args end
    return 'cmd.exe /D /S /C "' .. args .. '"'
  end
  local line = M.cmdline(args)
  if needs_cmd(args[1]) then return 'cmd.exe /D /S /C "' .. line .. '"' end
  return line
end

-- run(args, opts) -> {code=int, ok=bool, stdout=string|nil, stderr=string|nil}
-- opts: capture (stdout+stderr strings), stdout_only (capture stdout, stderr passes
--       through), merge (stderr into stdout), check (error on nonzero), cwd,
--       input (string fed to stdin), env {NAME=value|false}, text (default true:
--       CRLF->LF in captured output), stdout_file / stderr_file, append, quiet,
--       timeout (seconds), hide (no console window)
-- A string `args` runs through cmd.exe (shell semantics).
function M.run(args, opts)
  opts = opts or {}
  local o = { cwd = opts.cwd and path.norm(opts.cwd):gsub("/", "\\") or nil, append = opts.append, hide = opts.hide, timeout = opts.timeout }
  local tmp_in, tmp_out, tmp_err
  if opts.input then tmp_in = path.temp(".in"); path.write_bytes(tmp_in, opts.input); o.stdin = tmp_in end
  if opts.capture or opts.stdout_only then
    tmp_out = path.temp(".out"); o.stdout = tmp_out
    if opts.merge then o.stderr = "stdout" elseif opts.capture then tmp_err = path.temp(".err"); o.stderr = tmp_err end
  else
    if opts.stdout_file then o.stdout = path.norm(opts.stdout_file) end
    if opts.stderr_file then o.stderr = path.norm(opts.stderr_file) elseif opts.merge then o.stderr = "stdout" end
    if opts.quiet then o.stdout = o.stdout or "NUL"; o.stderr = o.stderr or "NUL" end
  end
  local saved = {}
  if opts.env then
    for k, v in pairs(opts.env) do saved[k] = fs.getenv(k) or false; fs.setenv(k, v or nil) end
  end
  local code, err = fs.run(full_line(args, opts), o)
  for k, v in pairs(saved) do fs.setenv(k, v or nil) end
  local function slurp(p)
    if not p then return nil end
    local okr, s = pcall(path.read_bytes, p)
    fs.remove(p)
    s = okr and s or ""
    if opts.text ~= false then s = s:gsub("\r\n", "\n") end
    return s
  end
  local res = { stdout = slurp(tmp_out), stderr = slurp(tmp_err) }
  if tmp_in then fs.remove(tmp_in) end
  if code == nil then
    res.code, res.ok, res.error = -1, false, err
    if opts.check then error(("cannot run %s: %s"):format(M.cmdline(args), tostring(err)), 2) end
    return res
  end
  res.code, res.ok = code, code == 0
  if opts.check and not res.ok then
    error(("command failed (%d): %s\n%s"):format(res.code, M.cmdline(args), res.stderr or ""), 2)
  end
  return res
end

-- Capture stdout of a command, erroring on failure (subprocess.check_output).
function M.output(args, opts)
  opts = opts or {}
  opts.capture = true; opts.check = opts.check ~= false
  return M.run(args, opts).stdout
end

-- Start a command without waiting (Popen). Returns the process id.
-- opts: cwd, hide, stdout_file, stderr_file, merge, env {NAME=value|false}
function M.spawn(args, opts)
  opts = opts or {}
  local o = { wait = false, cwd = opts.cwd and path.norm(opts.cwd):gsub("/", "\\") or nil, hide = opts.hide,
    stdout = opts.stdout_file and path.norm(opts.stdout_file) or nil, stderr = opts.stderr_file and path.norm(opts.stderr_file) or (opts.merge and "stdout" or nil) }
  -- env {NAME=value|false}: set for the child only (inherited at creation), as in run().
  local saved = {}
  for k, v in pairs(opts.env or {}) do saved[k] = fs.getenv(k) or false; fs.setenv(k, v or nil) end
  local pid, err = fs.run(full_line(args, opts), o)
  for k, v in pairs(saved) do fs.setenv(k, v or nil) end
  if not pid then error(("cannot start %s: %s"):format(M.cmdline(args), tostring(err)), 2) end
  return pid
end

-- Interactive child with stdin/stdout pipes (subprocess.Popen(stdin=PIPE, stdout=PIPE)).
--   local p = ok.proc.popen({exe, "--stdio"}, {stderr = "stdout" | path | nil, cwd = dir})
--   ok.proc.popen({exe}, {stdout = "NUL", stderr = "NUL", hide = true})  -- managed child, no pipes read
--   p:write(line .. "\n"); local reply, err = p:read_line(5)   -- err "timeout" | "eof"
--   p:read(n [, timeout]) / p:read("a"); p:close_stdin(); p:wait([timeout]); p:poll(); p:kill(); p:pid()
function M.popen(args, opts)
  opts = opts or {}
  local function target(v) if v == nil or v == "stdout" or v == "NUL" then return v end return path.norm(v) end
  local o = { cwd = opts.cwd and path.norm(opts.cwd):gsub("/", "\\") or nil, stdout = target(opts.stdout), stderr = target(opts.stderr), hide = opts.hide }
  local p, err = fs.popen2(full_line(args, opts), o)
  if not p then error(("cannot start %s: %s"):format(M.cmdline(args), tostring(err)), 2) end
  return p
end

-- Run the repository's lua.exe on another Lua script.
function M.lua(script, args, opts)
  local ok = require "ok"
  local list = { ok.lua_exe, script }
  for _, a in ipairs(args or {}) do list[#list + 1] = a end
  return M.run(list, opts)
end

-- Locate an executable on PATH (shutil.which).
function M.which(name)
  if path.is_file(name) then return path.norm(name) end
  local exts = { "", ".exe", ".cmd", ".bat", ".com" }
  for dir in (fs.getenv("PATH") or ""):gmatch("[^;]+") do
    for _, e in ipairs(exts) do
      local p = path.join(dir, name .. e)
      if path.is_file(p) then return p end
    end
  end
  return nil
end

return M
