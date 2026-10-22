-- Reuse the established native launcher, with separate immutable landscaping receipts.
local f=assert(io.open('Tools/street_city_tree_upgrade.lua','rb'));local s=f:read('*a');f:close()
s=s:gsub("local series=os.getenv%('STREET_CITY_TREE_SERIES'%) or 'v1'","local series='v2'")
s=s:gsub("local out=root%.%.'/output/street%-social%-city%-production/tree%-upgrade%-'%.%.series","local out=root..'/output/street-social-city-production/tree-landscaping-v3'")
s=s:gsub('tree_probe=true,','land_probe=true,land_install=true,land_repair=true,land_balance=true,land_audit=true,land_render=true,tree_probe=true,')
s=s:gsub("dofile%('Tools/street_city_tree_upgrade_payload.lua'%)","dofile('Tools/street_city_tree_upgrade_payload.lua')..dofile('Tools/street_city_tree_landscaping_payload.lua')")
s=s:gsub("'tree_compare'%]:return","'tree_compare','land_render']:return")
s=s:gsub("MODE in %['tree_before','tree_after','tree_preview','tree_compare'%]","MODE in ['tree_before','tree_after','tree_preview','tree_compare','land_render']")
assert(load(s,'tree-landscaping-launcher'))()
