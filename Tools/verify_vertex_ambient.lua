-- Compare the native compiled forward VS against the runtime L2 formula.
-- No card inputs or translated shader are used by the D3D11 oracle.
local ok=require 'ok'
local cg=require 'shader_program_codegen'
local p,json=ok.path,ok.json
local parser=ok.args.parser('Native vertex ambient oracle')
parser:opt('--tag',{required=true});parser:opt('--map',{default='20'})
local a=parser:parse(arg)
assert(a.tag:match('^[%w_%-]+$'),'Invalid evidence tag')
local out=p.join(ok.root,'Saved/Tests',a.tag);assert(not p.exists(out),'Fresh evidence tag required');p.mkdir(out)
local native=p.join(ok.root,'Tools/shader_probe/target/release/vertex_ambient_oracle.exe')
assert(p.is_file(native),'Build Tools/shader_probe first')
local catalog=ok.read_json(p.join(ok.root,'Content/IndependentResources/base_v1/studio/map_environments.json'))
local probe
for _,map in ipairs(catalog.maps) do if map.map==tonumber(a.map) then probe=map.probe end end
assert(probe,'Map environment missing')
local higher=json.array({})
for _,c in ipairs(probe) do higher[#higher+1]=json.array({c[5],c[6],3*c[7],c[8]}) end
higher[4]=json.array({probe[1][9],probe[2][9],probe[3][9],1})
local specs={
 ['Koikano/hair_main_sun']={pass=2,normal=1,ambient=4},
 ['Koikano/hair_main_sun_front']={pass=2,normal=1,ambient=4},
 ['Koikano/main_skin']={pass=2,normal=2,ambient=5},
 ['Koikano/main_clothes_opaque']={pass=2,normal=1,ambient=4},
 ['Koikano/main_clothes_alpha']={pass=1,normal=1,ambient=4},
 ['Koikano/main_clothes_item']={pass=2,normal=1,ambient=5},
}
local results=json.array({})
local bundle=p.join(ok.game_root(true),'kks_decomp/plugins/KKS_Plugins/KKS_MaterialEditor/KK_Plugins.Resources.KKS_Shaders.unity3d')
for _,object in ipairs(cg.shader_trees(bundle)) do
 local tree=object.tree;local name=tree.m_ParsedForm.m_Name;local spec=specs[name]
 if spec then
  local pass=tree.m_ParsedForm.m_SubShaders[1].m_Passes[spec.pass];local names=cg.names(pass.m_NameIndices)
  local program
  for _,sp in ipairs(pass.progVertex.m_SubPrograms) do
   local directional=false;for _,k in ipairs(sp.m_GlobalKeywordIndices or {}) do directional=directional or names[k]=='DIRECTIONAL' end
   if sp.m_GpuProgramType==15 and directional then program=sp;break end
  end
  assert(program,'Native directional vertex program absent: '..name)
  local raw=cg.platform_raw(tree,4);local blob=cg.dxbc_in(cg.sub_program_at(raw,program.m_BlobIndex,12).m_ProgramCode,'index')
  local label=name:gsub('/','_');local shader=out..'/'..label..'.vs.dxbc';p.write_bytes(shader,blob)
  local asm=cg.dumpbin(shader);p.write_text(shader..'.asm',asm)
  local buffers=json.array({})
  for _,cb in ipairs(program.m_ConstantBuffers) do
   local values=json.array({});for i=1,cb.m_Size//4 do values[i]=0 end
   for _,u in ipairs(cb.m_MatrixParams) do
    local key=names[u.m_NameIndex]
    if key=='unity_ObjectToWorld' or key=='unity_WorldToObject' or key=='unity_MatrixVP' then
     for i=0,3 do values[u.m_Index//4+i*4+i+1]=1 end
    end
   end
   for _,u in ipairs(cb.m_VectorParams) do
    local key=names[u.m_NameIndex];local index=({unity_SHBr=1,unity_SHBg=2,unity_SHBb=3,unity_SHC=4})[key]
    if index then for c=1,4 do values[u.m_Index//4+c]=higher[index][c] end end
   end
   buffers[#buffers+1]=values
  end
  local config={shader=shader,buffers=buffers,higher=higher,normal_semantic=spec.normal,ambient_semantic=spec.ambient}
  local file=out..'/'..label..'.json';p.write_text(file,json.encode(config,{indent=2})..'\n')
  local report=out..'/'..label..'.result.json'
  ok.proc.run({native,file,report},{check=true})
  local result=ok.read_json(report);assert(result.passed,'Native vertex oracle failed: '..name)
  result.native_dxbc_sha256=cg.sha256(blob);results[#results+1]=result
 end
end
assert(#results==6,'Expected six native shader families')
p.write_text(out..'/results.json',json.encode({passed=true,map=tonumber(a.map),families=results},{indent=2})..'\n')
print(out)
