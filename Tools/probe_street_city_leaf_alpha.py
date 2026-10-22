import unreal,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import OUT,read,write
rows=[]
for aid in ['camphor','cherry']:
 folder='/Game/UniversityCity/StreetScenery/Foliage/';tex=unreal.load_asset(folder+aid+'_RGBA');mat=unreal.load_asset(folder+'M_'+aid+'_Cutout');node=unreal.MaterialEditingLibrary.get_material_property_input_node(mat,unreal.MaterialProperty.MP_OPACITY_MASK)
 task=unreal.AssetExportTask();task.set_editor_property('object',tex);task.set_editor_property('filename',str(OUT/(aid+'-native-alpha.tga')));task.set_editor_property('automated',True);task.set_editor_property('prompt',False);task.set_editor_property('exporter',unreal.TextureExporterTGA());ok=unreal.Exporter.run_asset_export_task(task)
 row=dict(id=aid,exported=ok,clip=mat.get_editor_property('opacity_mask_clip_value'),mask_texture=node.get_editor_property('texture').get_path_name(),mask_output=unreal.MaterialEditingLibrary.get_material_property_input_node_output_name(mat,unreal.MaterialProperty.MP_OPACITY_MASK),texture_alpha_kept=not tex.get_editor_property('compression_no_alpha'))
 q=next(q for q in read(OUT/'native-install.json')['assets'] if q['id']==aid);mesh=unreal.load_asset(q['asset']);sections=[]
 for i in range(len(mesh.get_editor_property('static_materials'))):
  try:
   result=unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh,0,i);uvs=list(result[3]);sections.append(dict(slot=str(mesh.get_editor_property('static_materials')[i].get_editor_property('material_slot_name')),material=mesh.get_material(i).get_path_name(),triangles=len(result[1])//3,uv_min=[min((p.x for p in uvs),default=0),min((p.y for p in uvs),default=0)],uv_max=[max((p.x for p in uvs),default=0),max((p.y for p in uvs),default=0)],uv_first=[[p.x,p.y] for p in uvs[:6]]))
  except Exception as e:sections.append(str(e))
 row['sections']=sections;rows.append(row)
write(OUT/'native-alpha-probe.json',dict(status='passed',rows=rows))
unreal.SystemLibrary.quit_editor()
