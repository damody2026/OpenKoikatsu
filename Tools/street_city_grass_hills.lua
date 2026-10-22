-- Native terrain material pass; reuse the established editor launcher.
local f=assert(io.open('Tools/street_city_tree_upgrade.lua','rb'));local s=f:read('*a');f:close()
local function replace(pattern,value)local n;s,n=s:gsub(pattern,value);assert(n==1,'Launcher contract changed: '..pattern)end
replace("local series=os.getenv%('STREET_CITY_TREE_SERIES'%) or 'v1'","local series='v2'")
replace("local out=root%.%.'/output/street%-social%-city%-production/tree%-upgrade%-'%.%.series","local out=root..'/output/street-social-city-production/grass-hills-v1'")
replace('tree_probe=true,','grass_prepare=true,grass_install=true,grass_audit=true,grass_render=true,grass_merge=true,grass_current=true,tree_probe=true,')
replace("dofile%('Tools/street_city_tree_upgrade_payload.lua'%)","dofile('Tools/street_city_tree_upgrade_payload.lua')..dofile('Tools/street_city_grass_hills_payload.lua')")
replace("MODE in %['tree_before','tree_after','tree_preview','tree_compare'%]","MODE in ['tree_before','tree_after','tree_preview','tree_compare','grass_render','grass_merge']")
replace("'tree_asset_audit'%]:assert","'tree_asset_audit','grass_prepare']:assert")
replace("if MODE not in %['tree_import'","WORLD_LOAD_SHA=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')\nif MODE not in ['tree_import'")
assert(load(s,'grass-hills-launcher'))()
