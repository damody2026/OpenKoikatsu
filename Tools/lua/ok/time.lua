-- Time helpers (time.time, perf_counter, sleep, datetime formatting).
local fs = require "okfs"
local M = {}

M.time = fs.time            -- unix seconds (float)
M.clock = fs.clock          -- monotonic seconds (perf_counter)
M.perf_counter = fs.clock
M.sleep = fs.sleep          -- seconds (float)

-- Local ISO timestamp like datetime.now().isoformat(timespec="seconds").
function M.now_iso(utc)
  local t = os.time()
  if utc then return os.date("!%Y-%m-%dT%H:%M:%S", t) .. "+00:00" end
  local off = os.difftime(t, os.time(os.date("!*t", t)))
  local sign = off < 0 and "-" or "+"
  off = math.abs(off)
  return os.date("%Y-%m-%dT%H:%M:%S", t) .. string.format("%s%02d:%02d", sign, off // 3600, (off % 3600) // 60)
end
-- datetime.now().isoformat() without offset (naive local time, seconds).
function M.now_local() return os.date("%Y-%m-%dT%H:%M:%S") end
-- UTC "Z" timestamp.
function M.now_utc_z() return os.date("!%Y-%m-%dT%H:%M:%SZ") end
function M.today() return os.date("%Y-%m-%d") end
function M.stamp() return os.date("%Y%m%d-%H%M%S") end

-- Parse "YYYY-MM-DD" into a unix day number and back (UTC calendar math).
local function days_from_civil(y, m, d)
  y = m <= 2 and y - 1 or y
  local era = (y >= 0 and y or y - 399) // 400
  local yoe = y - era * 400
  local doy = (153 * (m + (m > 2 and -3 or 9)) + 2) // 5 + d - 1
  local doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
  return era * 146097 + doe - 719468
end
local function civil_from_days(z)
  z = z + 719468
  local era = (z >= 0 and z or z - 146096) // 146097
  local doe = z - era * 146097
  local yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
  local y = yoe + era * 400
  local doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
  local mp = (5 * doy + 2) // 153
  local d = doy - (153 * mp + 2) // 5 + 1
  local m = mp + (mp < 10 and 3 or -9)
  return m <= 2 and y + 1 or y, m, d
end
function M.date_to_days(s) local y, m, d = s:match("^(%d+)-(%d+)-(%d+)"); return days_from_civil(tonumber(y), tonumber(m), tonumber(d)) end
function M.days_to_date(n) local y, m, d = civil_from_days(n); return string.format("%04d-%02d-%02d", y, m, d) end
function M.add_days(s, n) return M.days_to_date(M.date_to_days(s) + n) end
-- 0 = Monday ... 6 = Sunday (date.weekday()).
function M.weekday(s) return (M.date_to_days(s) + 3) % 7 end
M.days_from_civil, M.civil_from_days = days_from_civil, civil_from_days

return M
