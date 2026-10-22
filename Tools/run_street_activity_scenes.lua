-- Resume-safe pipeline: each stage requires a fresh success receipt.
local requested=({install='art_install',audit='art_audit'})[arg[1]] or arg[1]
local valid={import_assets=true,art_materials=true,art_install=true,art_audit=true,render=true}
assert(not requested or valid[requested],'Unknown resume stage')
local active=not requested
for _,mode in ipairs({'import_assets','art_materials','art_install','art_audit','render'})do
 if mode==requested then active=true end
 if active then
 print('ACTIVITY STAGE '..mode)
 assert(os.execute('Tools\\lua\\lua.exe Tools/street_city_activity_scenes.lua '..mode),mode..' failed')
 end
end
