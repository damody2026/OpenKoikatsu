-- Pre-commit guard: character cards never go into the repository (AGENTS.md "版控只放共用資源").
--
--   Tools\lua\lua.exe Tools\check_no_cards.lua [--all]
--
-- Checks the staged files (added / copied / modified / renamed), or with --all every tracked file. A file is rejected
-- when it lives in a card folder, or when an image carries card data: an original card ("KoiKatuChara..."), an OK card
-- (okPv / okLc chunk), or a Studio scene that embeds characters. Installed as .git/hooks/pre-commit by
-- Tools/install_git_hooks.lua.

local CARD_DIRS = { "^Content/CharacterCards/", "^UserData/chara/", "_cards/", "^UserData/Studio/scene/" }
local IMAGE = { png = true, apng = true }

local function lines(cmd)
  local p = assert(io.popen(cmd, "r"))
  local s = p:read("a"); p:close()
  local out = {}
  for f in s:gmatch("[^%z]+") do out[#out + 1] = f end
  return out
end

local function card_kind(path)
  local f = io.open(path, "rb")
  if not f then return nil end
  local s = f:read("a"); f:close()
  -- An LFS pointer in the work tree has no card data to look at; the real content is checked where it exists.
  if s:sub(1, 8) ~= "\137PNG\r\n\26\n" then return nil end
  if s:find("okPv", 1, true) or s:find("okLc", 1, true) then return "OK 卡" end
  local chara = s:find("KoiKatuChara", 1, true) or s:find("KoiKatsuChara", 1, true)
  if s:find("KStudio", 1, true) then return chara and "含角色的 Studio 場景" or nil end
  if chara then return "角色卡" end
  return nil
end

local all = arg[1] == "--all"
local files = all and lines("git ls-files -z")
  or lines("git diff --cached --name-only --diff-filter=ACMR -z")
local bad = {}
for _, f in ipairs(files) do
  local why
  for _, d in ipairs(CARD_DIRS) do
    if f:find(d) and IMAGE[(f:match("%.([%w]+)$") or ""):lower()] then why = "角色卡資料夾"; break end
  end
  if not why and IMAGE[(f:match("%.([%w]+)$") or ""):lower()] then why = card_kind(f) end
  if why then bad[#bad + 1] = ("  %s（%s）"):format(f, why) end
end

if #bad > 0 then
  io.stderr:write("不能提交角色卡（私有卡與測試卡只留在本機）：\n", table.concat(bad, "\n"), "\n",
    "用 git restore --staged <檔案> 取消暫存，必要時加進 .gitignore。\n")
  os.exit(1)
end

-- Text sources must be UTF-8. Reads the staged blob (also right when a commit goes through a temporary index whose
-- content differs from the work tree): a line written in the console code page once landed in STREET_ERRORS as Big5.
local TEXT = { md = true, toml = true, lua = true, rs = true, cpp = true, h = true, inl = true, json = true, cs = true }
local not_utf8 = {}
if not all then
  for _, f in ipairs(files) do
    if TEXT[(f:match("%.([%w]+)$") or ""):lower()] then
      local p = assert(io.popen('git show ":' .. f .. '"', "r"))
      local s = p:read("a"); p:close()
      if s and not utf8.len(s) then not_utf8[#not_utf8 + 1] = "  " .. f end
    end
  end
end
if #not_utf8 > 0 then
  io.stderr:write("這些文字檔不是 UTF-8（多半是用主控台字碼頁寫入的），請轉成 UTF-8 再提交：\n",
    table.concat(not_utf8, "\n"), "\n")
  os.exit(1)
end
if all then io.write(("checked %d tracked files, no cards\n"):format(#files)) end
