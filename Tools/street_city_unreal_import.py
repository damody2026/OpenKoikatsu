"""Installed-API synchronous glTF importer with metric, material and hash gates."""
import unreal,json,hashlib,re
from pathlib import Path
from street_city_material_identity import material_identities
ROOT=Path('C:/portable/OpenKoikatsu');OUT=ROOT/'output/street-social-city-production/world-ground-v1'
KEEP=[]
COMPILED_MATERIALS=set()
CANONICAL_MATERIALS={}
MATERIAL_BINDINGS=[]

def ensure_mesh_materials(mesh):
 slots=mesh.get_editor_property('static_materials');folder=mesh.get_path_name().rsplit('/',1)[0];lookup=None;changed=False;bound=[]
 identities=material_identities(unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCitySourceFile'))
 for index,slot in enumerate(slots):
  material=slot.get_editor_property('material_interface')
  if material is None:
   if lookup is None:
    lookup={}
    for path in unreal.EditorAssetLibrary.list_assets(folder,recursive=False):
     asset=unreal.EditorAssetLibrary.load_asset(path)
     if isinstance(asset,unreal.MaterialInterface):lookup[asset.get_name()]=asset
   names=[str(slot.get_editor_property(k)) for k in ['imported_material_slot_name','material_slot_name']]
   material=next((lookup[n] for n in names if n in lookup),None)
   assert material is not None,('Missing material slot',mesh.get_path_name(),index,names,list(lookup))
   slots[index].set_editor_property('material_interface',material);changed=True
  names=[str(slot.get_editor_property(k)) for k in ['imported_material_slot_name','material_slot_name']]+[material.get_name()]
  normalized=[re.sub(r'[^A-Za-z0-9_]', '_',n) for n in names]
  identity=next((identities[n] for n in normalized if n in identities),None)
  assert identity is not None,('Cannot prove material content identity',mesh.get_path_name(),names,list(identities))
  canonical=CANONICAL_MATERIALS.setdefault(identity,material)
  bound.append(canonical)
  MATERIAL_BINDINGS.append(dict(mesh=mesh.get_path_name(),slot=index,source_identity=identity,material=canonical.get_path_name()))
 if changed:
  mesh.set_editor_property('static_materials',slots)
  deps=json.loads(unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCityDependencies'))
  deps=sorted(set(deps+[str(v.get_editor_property('material_interface').get_path_name()) for v in mesh.get_editor_property('static_materials')]))
  unreal.EditorAssetLibrary.set_metadata_tag(mesh,'StreetCityDependencies',json.dumps(deps));assert unreal.EditorAssetLibrary.save_loaded_asset(mesh)
 assert all(v.get_editor_property('material_interface') is not None for v in mesh.get_editor_property('static_materials'))
 return bound
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def import_mesh(path,folder,name):
 digest=sha(path);asset_path=folder+'/'+name;existing=unreal.EditorAssetLibrary.load_asset(asset_path) if unreal.EditorAssetLibrary.does_asset_exist(asset_path) else None
 if isinstance(existing,unreal.StaticMesh) and unreal.EditorAssetLibrary.get_metadata_tag(existing,'StreetCitySourceSHA256')==digest:
  deps=unreal.EditorAssetLibrary.get_metadata_tag(existing,'StreetCityDependencies')
  if deps and json.loads(deps) and all(unreal.EditorAssetLibrary.does_asset_exist(p) for p in json.loads(deps)) and all(v.get_editor_property('material_interface') is not None for v in existing.get_editor_property('static_materials')):return existing
 # Reimport retains old pipeline settings, sometimes omitting all materials.
 # Version destinations by source content so changed PBR inputs get a fresh import.
 folder=folder+'/Source_'+digest[:16];asset_path=folder+'/'+name
 existing=unreal.EditorAssetLibrary.load_asset(asset_path) if unreal.EditorAssetLibrary.does_asset_exist(asset_path) else None
 if isinstance(existing,unreal.StaticMesh) and unreal.EditorAssetLibrary.get_metadata_tag(existing,'StreetCitySourceSHA256')==digest:
  deps=unreal.EditorAssetLibrary.get_metadata_tag(existing,'StreetCityDependencies')
  if deps and json.loads(deps) and all(unreal.EditorAssetLibrary.does_asset_exist(p) for p in json.loads(deps)) and all(v.get_editor_property('material_interface') is not None for v in existing.get_editor_property('static_materials')):return existing
 pipeline=unreal.InterchangeGenericAssetsPipeline();KEEP.append(pipeline);pipeline.set_editor_property('asset_name',name);pipeline.set_editor_property('use_source_name_for_asset',False);pipeline.get_editor_property('common_meshes_properties').set_editor_property('bake_meshes',True)
 material_pipeline=pipeline.get_editor_property('material_pipeline');material_pipeline.set_editor_property('import_materials',True);material_pipeline.set_editor_property('reuse_existing_materials',False);material_pipeline.set_editor_property('create_new_materials',True)
 mesh_pipeline=pipeline.get_editor_property('mesh_pipeline');mesh_pipeline.set_editor_property('combine_static_meshes_behavior',unreal.InterchangeCombineStaticMeshesBehavior.ALL);mesh_pipeline.set_editor_property('import_skeletal_meshes',False);mesh_pipeline.set_editor_property('build_nanite',False);mesh_pipeline.set_editor_property('generate_lightmap_u_vs',False)
 params=unreal.ImportAssetParameters();params.set_editor_property('is_automated',True);params.set_editor_property('replace_existing',True);params.set_editor_property('destination_name',name);params.set_editor_property('override_pipelines',[unreal.SoftObjectPath(pipeline.get_path_name())]);manager=unreal.InterchangeManager.get_interchange_manager_scripted();source=unreal.InterchangeManager.create_source_data(str(path));objects=manager.import_asset(folder,source,params);meshes=[o for o in objects or [] if isinstance(o,unreal.StaticMesh)];assert len(meshes)==1,(str(path),'expected one combined mesh',len(meshes));mesh=meshes[0]
 body=mesh.get_editor_property('body_setup');assert body;body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE);unreal.EditorAssetLibrary.set_metadata_tag(mesh,'StreetCitySourceSHA256',digest);unreal.EditorAssetLibrary.set_metadata_tag(mesh,'StreetCitySourceFile',str(path));dependencies=[]
 for asset in objects:
  if isinstance(asset,unreal.Texture2D):assert max(asset.blueprint_get_size_x(),asset.blueprint_get_size_y())<=2048
  if asset!=mesh:
   assert unreal.EditorAssetLibrary.save_loaded_asset(asset);dependencies.append(asset.get_path_name())
 unreal.EditorAssetLibrary.set_metadata_tag(mesh,'StreetCityDependencies',json.dumps(dependencies));assert unreal.EditorAssetLibrary.save_loaded_asset(mesh);KEEP.clear();return mesh
def xyz(v):return [float(v.x),float(v.y),float(v.z)]
def floor_inputs():
 audit=json.loads((ROOT/'output/street-social-city-production/shop-street-v1/final-audit.json').read_text('utf-8'));world=json.loads((ROOT/'output/street-social-city-production/shop-street-v1/world-assembly.json').read_text('utf-8'));assert audit['status']=='passed' and audit['passed']==96;by_id={b['id']:b for b in audit['buildings']}
 for b in world['buildings']:
  for floor in (1,2,3):
   path=Path(b['source']).parent/f'{b["building"]}-F{floor:02d}-complete-2k.glb';assert sha(path)==by_id[b['building']]['files'][path.name]
   yield dict(id=f'{b["building"]}_F{floor:02d}',building=b['building'],floor=floor,source=path,position_m=b['position_m'],yaw_deg=90-b['rotation_z_deg'],footprint_m=b['footprint_m'],expected_triangles=b['floor_triangles'][str(floor)])
