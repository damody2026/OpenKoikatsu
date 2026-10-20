-- argparse-like command line parsing.
--   local p = ok.args.parser("Build things")
--   p:arg("input")                                 -- positional (required)
--   p:arg("extra", {nargs="*"})                    -- remaining positionals
--   p:opt("--output", {default="out.json"})        -- string option
--   p:opt("--count", {type="int", default=3})
--   p:opt("--scale", {type="float"})
--   p:flag("--verbose")                            -- store_true
--   p:opt("--tag", {multiple=true})                -- action="append"
--   p:opt("--mode", {choices={"a","b"}, required=true})
--   local a = p:parse(arg)    -- a.output, a.count, a.verbose, a.input ...
-- Names map "--some-name" to a.some_name. Unknown options are errors.
local M = {}
local Parser = {}
Parser.__index = Parser

function M.parser(description) return setmetatable({ description = description, opts = {}, pos = {}, byname = {} }, Parser) end

local function dest_of(name) return (name:gsub("^%-+", ""):gsub("%-", "_")) end

function Parser:arg(name, spec)
  spec = spec or {}; spec.name = name; spec.dest = spec.dest or dest_of(name)
  self.pos[#self.pos + 1] = spec
  return self
end
function Parser:opt(...)
  local names, spec = { ... }, {}
  if type(names[#names]) == "table" then spec = table.remove(names) end
  spec.names = names
  local long = names[1]
  for _, n in ipairs(names) do if n:sub(1, 2) == "--" then long = n break end end
  spec.dest = spec.dest or dest_of(long)
  self.opts[#self.opts + 1] = spec
  for _, n in ipairs(names) do self.byname[n] = spec end
  return self
end
function Parser:flag(...)
  local names, spec = { ... }, {}
  if type(names[#names]) == "table" then spec = table.remove(names) end
  spec.flag = true
  names[#names + 1] = spec
  return self:opt(table.unpack(names))
end

function Parser:usage()
  local parts = { "usage: " .. ((arg and arg[0]) or "script") }
  for _, o in ipairs(self.opts) do parts[#parts + 1] = (o.required and "" or "[") .. o.names[1] .. (o.flag and "" or " " .. o.dest:upper()) .. (o.required and "" or "]") end
  for _, p in ipairs(self.pos) do parts[#parts + 1] = (p.nargs == "*" or p.nargs == "?") and ("[" .. p.name .. "]") or p.name end
  local lines = { table.concat(parts, " ") }
  if self.description then lines[#lines + 1] = "\n" .. self.description end
  for _, o in ipairs(self.opts) do lines[#lines + 1] = "  " .. table.concat(o.names, ", ") .. (o.help and ("  " .. o.help) or "") end
  return table.concat(lines, "\n")
end

function Parser:fail(msg)
  io.stderr:write(self:usage(), "\nerror: ", msg, "\n")
  os.exit(2)
end

local function convert(self, spec, v, label)
  local t = spec.type
  if t == "int" then
    local n = math.tointeger(tonumber(v)); if n == nil then self:fail(label .. ": invalid int value: " .. v) end; v = n
  elseif t == "float" then
    local n = tonumber(v); if n == nil then self:fail(label .. ": invalid float value: " .. v) end; v = n + 0.0
  elseif type(t) == "function" then v = t(v) end
  if spec.choices then
    local okv = false
    for _, c in ipairs(spec.choices) do if c == v then okv = true end end
    if not okv then self:fail(label .. ": invalid choice: " .. tostring(v)) end
  end
  return v
end

-- parse(list) where list is the script's `arg` table (1-based) or any array.
function Parser:parse(list)
  list = list or arg or {}
  local out, positional, i, only_pos = {}, {}, 1, false
  for _, o in ipairs(self.opts) do
    if o.flag then out[o.dest] = o.default or false
    elseif o.multiple then out[o.dest] = o.default or {}
    else out[o.dest] = o.default end
  end
  while i <= #list do
    local a = list[i]
    if only_pos or a == "-" or a:sub(1, 1) ~= "-" or tonumber(a) then positional[#positional + 1] = a
    elseif a == "--" then only_pos = true
    elseif a == "-h" or a == "--help" then print(self:usage()); os.exit(0)
    else
      local name, inline = a:match("^(%-%-[^=]+)=(.*)$")
      name = name or a
      local spec = self.byname[name]
      if not spec then self:fail("unrecognized arguments: " .. a) end
      if spec.flag then out[spec.dest] = not spec.store_false
      else
        local v = inline
        if spec.nargs == "+" or spec.nargs == "*" then
          local vals = {}
          if v then vals[1] = convert(self, spec, v, name) end
          while list[i + 1] and (list[i + 1]:sub(1, 1) ~= "-" or tonumber(list[i + 1])) do
            i = i + 1; vals[#vals + 1] = convert(self, spec, list[i], name)
          end
          out[spec.dest] = vals
        else
          if v == nil then i = i + 1; v = list[i]; if v == nil then self:fail(name .. ": expected one argument") end end
          v = convert(self, spec, v, name)
          if spec.multiple then local t = out[spec.dest]; t[#t + 1] = v else out[spec.dest] = v end
        end
      end
    end
    i = i + 1
  end
  local k = 1
  for idx, p in ipairs(self.pos) do
    if p.nargs == "*" or p.nargs == "+" then
      local rest = {}
      local reserve = #self.pos - idx
      while k <= #positional - reserve do rest[#rest + 1] = convert(self, p, positional[k], p.name); k = k + 1 end
      if p.nargs == "+" and #rest == 0 then self:fail("the following arguments are required: " .. p.name) end
      out[p.dest] = rest
    elseif positional[k] ~= nil then out[p.dest] = convert(self, p, positional[k], p.name); k = k + 1
    elseif p.nargs == "?" then out[p.dest] = p.default
    else self:fail("the following arguments are required: " .. p.name) end
  end
  if k <= #positional then self:fail("unrecognized arguments: " .. table.concat(positional, " ", k)) end
  for _, o in ipairs(self.opts) do
    if o.required and out[o.dest] == nil then self:fail("the following arguments are required: " .. o.names[1]) end
  end
  return out
end

-- Quick helpers for scripts that only test sys.argv membership.
function M.has(list, flag) for _, a in ipairs(list or arg) do if a == flag then return true end end return false end
function M.value(list, name, default)
  list = list or arg
  for i, a in ipairs(list) do
    if a == name then return list[i + 1] end
    local v = a:match("^" .. name:gsub("%-", "%%-") .. "=(.*)$"); if v then return v end
  end
  return default
end

return M
