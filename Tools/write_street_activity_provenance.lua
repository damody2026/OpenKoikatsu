package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local base='C:/portable/OpenKoikatsu/output/street-social-city-production/activity-scenes-v1'
local function read(p)local f=assert(io.open(p,'rb'));local v=json.decode(f:read('*a'));f:close();return v end
local rows={}
for id,job in pairs({bear='1b2d400d-7850-4015-b6d8-6544e2bea201',koi='9bdffd4f-bb5b-40d5-8f0b-84c3cc0ccea1'})do
 local source=read(base..'/components/'..id..'/source.json');local remesh=read(base..'/components/'..id..'/remesh/remesh-report.json')
 assert(remesh.status=='passed')
 rows[id]={provider='Tripo Model',input_mode='text-to-3D',job_url='https://studio.tripo3d.ai/workspace/generate/'..job,texture_resolution='2K',original_download=source.original_source,original_download_sha256=source.source_sha256,normalized_size_m=source.normalized_size,adopted_remesh_sha256=remesh.output_sha256,remesh_report=base..'/components/'..id..'/remesh/remesh-report.json',normal_finishing=remesh.normal_finishing,legacy_glb_route_extra_is_not_authoritative=true}
end
local f=assert(io.open(base..'/generation.json','wb'));f:write(json.encode({status='passed',models=rows,source_of_truth='Actual Model UI jobs, immutable download hashes and native adoption receipts'}));f:close()
