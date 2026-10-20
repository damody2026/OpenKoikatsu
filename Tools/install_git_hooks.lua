-- Installs this checkout's git hooks: pre-commit runs Tools/check_no_cards.lua (no character cards in commits).
--
--   Tools\lua\lua.exe Tools\install_git_hooks.lua

local p = assert(io.popen("git rev-parse --git-path hooks", "r"))
local hooks = p:read("l"); p:close()
local f = assert(io.open(hooks .. "/pre-commit", "wb"))
f:write('#!/bin/sh\nexec "$(git rev-parse --show-toplevel)/Tools/lua/lua.exe" Tools/check_no_cards.lua\n')
f:close()
io.write("installed ", hooks, "/pre-commit\n")
