-- GPU rig check rows by bone (run_crowd_room100.lua --gpu-rig-verify N writes rig.bin, rig.txt, rig-blend.txt).
-- rig.bin: the bank's rows then the rig's, 12 floats a row (3x4, translation in column 3, cm); rig.txt: "actor bone node".
-- Each rig row M_r is split against the bank's M_b in bind space, Q = L_b^-1 L_r: its angle (deg), its scale/shear
-- (max |Q^T Q - I|), and what no rotation about any pivot explains (cm), so a wrong joint angle, a wrong offset and a
-- different scale composition (shear) differ.
-- The rows are float: an angle under about 0.1 deg with no translation is rounding. Palette alias rows (rank one) are
-- not split. The pattern is a Lua pattern ("^cf_", "hand").
--   Tools\lua\lua.exe Tools\crowd_rig_errors.lua <report dir or rig prefix> [bone pattern] [--actors] [--rows N]
local arg = arg
local base = arg[1] or error("usage: crowd_rig_errors.lua <dir|prefix> [pattern] [--actors] [--rows N]")
local pattern, by_actor, top_rows = nil, false, 0
local i = 2
while arg[i] do
  if arg[i] == "--actors" then by_actor = true
  elseif arg[i] == "--rows" then i = i + 1; top_rows = tonumber(arg[i])
  else pattern = arg[i] end
  i = i + 1
end
local prefix = base:match("[/\\]rig$") and base or (base .. "/rig")
local function read(path, mode) local f = assert(io.open(path, mode or "rb")); local s = f:read("a"); f:close(); return s end

local names, actors, nodes = {}, {}, {}
for line in read(prefix .. ".txt", "r"):gmatch("[^\r\n]+") do
  local a, n, node = line:match("^(%d+) (%S+) (%-?%d+)$")
  actors[#actors + 1] = tonumber(a); names[#names + 1] = n; nodes[#nodes + 1] = tonumber(node)
end
local blends = {}
local bf = io.open(prefix .. "-blend.txt", "r")
if bf then for line in bf:lines() do blends[#blends + 1] = tonumber(line) end; bf:close() end
local bin = read(prefix .. ".bin")
local rows = #names
assert(#bin == rows * 96, ("rig.bin %d bytes, %d rows named"):format(#bin, rows))
local function row(half, r)
  local m, at = {}, half * rows * 48 + (r - 1) * 48 + 1
  for k = 1, 12 do m[k], at = string.unpack("<f", bin, at) end
  return m
end
local function L(m, r, c) return m[(r - 1) * 4 + c] end

local function inv3(m)
  local a, b, c = L(m,1,1), L(m,1,2), L(m,1,3)
  local d, e, f = L(m,2,1), L(m,2,2), L(m,2,3)
  local g, h, k = L(m,3,1), L(m,3,2), L(m,3,3)
  local det = a*(e*k-f*h) - b*(d*k-f*g) + c*(d*h-e*g)
  if det == 0 then return nil end
  return {{(e*k-f*h)/det, (c*h-b*k)/det, (b*f-c*e)/det},
          {(f*g-d*k)/det, (a*k-c*g)/det, (c*d-a*f)/det},
          {(d*h-e*g)/det, (b*g-a*h)/det, (a*e-b*d)/det}}
end

-- One row: translation difference (cm); the rig's linear part against the bank's in bind space, Q = L_b^-1 L_r (a root's
-- non-uniform scale cancels): its angle (deg) and scale/shear (max |Q^T Q - I|); the part of the difference no rotation
-- about any pivot gives (cm, along Q's axis); and the pivot (component space, cm).
local function split(mb, mr)
  local dt = 0
  for r = 1, 3 do dt = dt + (L(mr,r,4) - L(mb,r,4))^2 end
  dt = math.sqrt(dt)
  local ib = inv3(mb)
  if not ib then return dt, 0, 0, dt end
  local Q = {}
  for r = 1, 3 do Q[r] = {} for c = 1, 3 do local s = 0 for k = 1, 3 do s = s + ib[r][k] * L(mr,k,c) end Q[r][c] = s end end
  local shear = 0
  for a = 1, 3 do for b = 1, 3 do
    local s = 0 for k = 1, 3 do s = s + Q[k][a] * Q[k][b] end
    shear = math.max(shear, math.abs(s - (a == b and 1 or 0)))
  end end
  local tr = Q[1][1] + Q[2][2] + Q[3][3]
  local angle = math.deg(math.acos(math.max(-1, math.min(1, (tr - 1) / 2))))
  -- e(p) = L_b (Q - I) p + (t_r - t_b): the pivot p0 solves (Q - I) p0 = -L_b^-1 (t_r - t_b), least squares (damped).
  local d = {}
  for r = 1, 3 do d[r] = 0 for k = 1, 3 do d[r] = d[r] - ib[r][k] * (L(mr,k,4) - L(mb,k,4)) end end
  local A = {}
  for r = 1, 3 do A[r] = {} for c = 1, 3 do A[r][c] = Q[r][c] - (r == c and 1 or 0) end end
  local N, y = {}, {}
  local scale = 0
  for r = 1, 3 do for c = 1, 3 do scale = math.max(scale, math.abs(A[r][c])) end end
  for r = 1, 3 do
    N[r] = {} y[r] = 0
    for c = 1, 3 do local s = 0 for k = 1, 3 do s = s + A[k][r] * A[k][c] end N[r][c] = s + (r == c and 1e-6 * scale * scale or 0) end
    for k = 1, 3 do y[r] = y[r] + A[k][r] * d[k] end
  end
  local Ni = scale > 0 and inv3({N[1][1],N[1][2],N[1][3],0, N[2][1],N[2][2],N[2][3],0, N[3][1],N[3][2],N[3][3],0})
  local residual, pivot = dt, nil
  if Ni then
    local x = {}
    for r = 1, 3 do x[r] = Ni[r][1]*y[1] + Ni[r][2]*y[2] + Ni[r][3]*y[3] end
    local v = {}
    for r = 1, 3 do v[r] = A[r][1]*x[1] + A[r][2]*x[2] + A[r][3]*x[3] - d[r] end
    local res = 0
    for r = 1, 3 do local w = L(mb,r,1)*v[1] + L(mb,r,2)*v[2] + L(mb,r,3)*v[3]; res = res + w*w end
    residual = math.sqrt(res)
    pivot = {}
    for r = 1, 3 do pivot[r] = L(mb,r,1)*x[1] + L(mb,r,2)*x[2] + L(mb,r,3)*x[3] + L(mb,r,4) end
  end
  return dt, angle, shear, residual, pivot
end

local groups, order = {}, {}
local all = {n = 0, sum = 0, ten = 0}
local list = {}
for r = 1, rows do
  local w = blends[actors[r] + 1]
  if not (w and w > 1e-4 and w < 1 - 1e-4) then
    local dt, angle, shear, residual, pivot = split(row(0, r), row(1, r))
    all.n = all.n + 1; all.sum = all.sum + dt; if dt >= 10 then all.ten = all.ten + 1 end
    local key = by_actor and (names[r] .. "@" .. actors[r]) or names[r]
    if not pattern or names[r]:find(pattern) then
      local g = groups[key]
      if not g then g = {n = 0, dt = 0, max = 0, angle = 0, maxangle = 0, shear = 0, residual = 0, node = nodes[r]}; groups[key] = g; order[#order + 1] = key end
      g.n = g.n + 1; g.dt = g.dt + dt; g.max = math.max(g.max, dt); g.angle = g.angle + angle; g.maxangle = math.max(g.maxangle, angle)
      g.shear = math.max(g.shear, shear); g.residual = g.residual + residual
      list[#list + 1] = {r = r, dt = dt, angle = angle, residual = residual, pivot = pivot}
    end
  end
end
print(("%d rows compared: mean %.4f cm, >=10 cm %.3f%%"):format(all.n, all.sum / math.max(1, all.n), 100 * all.ten / math.max(1, all.n)))
if pattern then
  local n, sum, ten, over = #list, 0, 0, 0
  for _, e in ipairs(list) do sum = sum + e.dt; if e.dt >= 10 then ten = ten + 1 end; if e.dt >= 0.1 then over = over + 1 end end
  print(("%d rows match %q: mean %.4f cm, >=0.1 cm %d, >=10 cm %.3f%%"):format(n, pattern, sum / math.max(1, n), over, 100 * ten / math.max(1, n)))
end
table.sort(order, function(a, b) return groups[a].dt / groups[a].n > groups[b].dt / groups[b].n end)
print(("%-40s %5s %6s %9s %9s %8s %8s %8s %9s"):format("bone", "rows", "node", "mean cm", "max cm", "mean deg", "max deg", "shear", "resid cm"))
for k, key in ipairs(order) do
  local g = groups[key]
  if k > 80 then break end
  print(("%-40s %5d %6d %9.4f %9.4f %8.4f %8.4f %8.5f %9.4f"):format(key, g.n, g.node, g.dt / g.n, g.max, g.angle / g.n, g.maxangle, g.shear, g.residual / g.n))
end
if top_rows > 0 then
  table.sort(list, function(a, b) return a.dt > b.dt end)
  for k = 1, math.min(top_rows, #list) do
    local e = list[k]
    local p = e.pivot and ("pivot %.1f %.1f %.1f"):format(e.pivot[1], e.pivot[2], e.pivot[3]) or ""
    print(("row %6d actor %2d %-32s %8.4f cm %8.4f deg resid %8.4f %s"):format(e.r - 1, actors[e.r], names[e.r], e.dt, e.angle, e.residual, p))
  end
end
