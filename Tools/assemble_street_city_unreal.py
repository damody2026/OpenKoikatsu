"""Build and verify the actual Unreal map from approved shop/ground assets."""
import sys,json,math,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_unreal_import import *
from street_city_remesh_gate import verify
ground=json.loads((OUT/'ground-acceptance.json').read_text('utf-8'));shops=json.loads((OUT/'unreal-shops-import.json').read_text('utf-8'));assert ground['status']=='passed' and ground['chunk_count']==16 and shops['status']=='passed' and len(shops['floors'])==288
MAP='/Game/UniversityCity/Maps/UniversityCity';checks=[];errors=[];placed=[];actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
def check(ok,name,**data):
 checks.append(dict(name=name,passed=bool(ok),**data))
 if not ok:errors.append(name)
try:
 unreal.EditorAssetLibrary.make_directory('/Game/UniversityCity/Maps')
 if unreal.EditorAssetLibrary.does_asset_exist(MAP):assert unreal.EditorLevelLibrary.load_level(MAP)
 else:assert unreal.EditorLevelLibrary.new_level(MAP)
 current={a.get_actor_label():a for a in actors.get_all_level_actors()}
 world=unreal.EditorLevelLibrary.get_editor_world()
 probe={}
 for label in ['S01_F03','S25_F03','S26_F03','S27_F03']:
  if label not in current:continue
  c=current[label].static_mesh_component
  probe[label]=dict(overrides=[str(v) for v in c.get_editor_property('override_materials')],materials=[str(c.get_material(i)) for i in range(c.get_num_materials())])
 (OUT/'unreal-material-before-reset.json').write_text(json.dumps(probe,indent=2),'utf-8')
 def spawn(cls,location,rotation=None):
  actor=actors.spawn_actor_from_class(cls,location,rotation or unreal.Rotator());assert actor is not None,cls;return actor
 def place(mesh,label,location,yaw,water=False):
  bound_materials=ensure_mesh_materials(mesh)
  check(all(v.get_editor_property('material_interface') is not None for v in mesh.get_editor_property('static_materials')),label+' bound material slots')
  actor=current.get(label)
  if actor is None:actor=actors.spawn_actor_from_object(mesh,unreal.Vector(*location),unreal.Rotator(pitch=0,yaw=yaw,roll=0));assert actor is not None,'Run assembly with full Editor -ExecutePythonScript, not -run=pythonscript';actor.set_actor_label(label)
  else:actor.static_mesh_component.set_static_mesh(mesh);actor.set_actor_location_and_rotation(unreal.Vector(*location),unreal.Rotator(pitch=0,yaw=yaw,roll=0),False,True)
  actor.static_mesh_component.set_editor_property('override_materials',bound_materials)
  check(all(actor.static_mesh_component.get_material(i)==m for i,m in enumerate(bound_materials)),label+' canonical scene material binding')
  actor.set_editor_property('tags',['StreetCityManaged',label]);actor.static_mesh_component.set_collision_profile_name('NoCollision' if water else 'BlockAll');actor.set_folder_path('Water' if water else 'Terrain' if label.startswith('G') else 'Shops/'+label.split('_')[0]);return actor
 for row in shops['floors']:
  mesh=unreal.EditorAssetLibrary.load_asset(row['asset'])
  if any(v.get_editor_property('material_interface') is None for v in mesh.get_editor_property('static_materials')):
   mesh=import_mesh(row['source'],row['asset'].rsplit('/',1)[0]+'/MaterialRepair',row['id']);row['asset']=mesh.get_path_name()
   (OUT/'unreal-shops-import.json').write_text(json.dumps(shops,indent=2),'utf-8')
  check(isinstance(mesh,unreal.StaticMesh),row['id']+' asset reload');check(unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCitySourceSHA256')==row['source_sha256']==sha(row['source']),row['id']+' source identity');deps=json.loads(unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCityDependencies'));check(all(unreal.EditorAssetLibrary.does_asset_exist(p) for p in deps),row['id']+' persisted material/texture dependencies');x,y,z=row['position_m'];actor=place(mesh,row['id'],[y*100,x*100,z*100],row['yaw_deg']);origin,extent=actor.get_actor_bounds(False);check(abs(extent.x*2-row['footprint_m'][1]*100)<.5 and abs(extent.y*2-row['footprint_m'][0]*100)<.5,row['id']+' world metric bounds');check(mesh.get_editor_property('body_setup').get_editor_property('collision_trace_flag')==unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE,row['id']+' exact interior collision');placed.append(dict(id=row['id'],kind='shop_floor',asset=mesh.get_path_name(),position_cm=xyz(actor.get_actor_location()),yaw_deg=row['yaw_deg']))
 for chunk in ground['chunks']:
  x,y,z=chunk['world_origin_m'];receipt=verify(Path(chunk['ground']['remesh_report']).parent,chunk['ground']['sha256']);mesh=import_mesh(receipt['path'],'/Game/UniversityCity/Ground',chunk['id']);actor=place(mesh,chunk['id'],[y*100,x*100,0],90);origin,extent=actor.get_actor_bounds(False);check(abs(extent.x*2-27500)<.5 and abs(extent.y*2-27500)<.5,chunk['id']+' actual 275m Unreal bounds');placed.append(dict(id=chunk['id'],kind='ground',asset=mesh.get_path_name(),position_cm=xyz(actor.get_actor_location()),yaw_deg=90))
  if chunk.get('water'):
   receipt=verify(Path(chunk['water']['remesh_report']).parent,chunk['water']['sha256']);mesh=import_mesh(receipt['path'],'/Game/UniversityCity/Water',chunk['id']+'_Water');actor=place(mesh,chunk['id']+'_Water',[y*100,x*100,0],90,True);placed.append(dict(id=chunk['id']+'_Water',kind='water',asset=mesh.get_path_name(),position_cm=xyz(actor.get_actor_location()),yaw_deg=90))
  unreal.log(chunk['id']+' assembled into Unreal map')
 world=unreal.EditorLevelLibrary.get_editor_world()
 def trace(start,end):
  result=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(*start),unreal.Vector(*end),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True)
  if result is None:return None
  hit=next((v for v in result if isinstance(v,unreal.HitResult)),None) if isinstance(result,tuple) else result
  return hit if hit is not None and hit.to_tuple()[0] else None
 def hit_location(hit):return xyz(hit.to_tuple()[4])
 for chunk in ground['chunks']:
  x,y,_=chunk['world_origin_m'];hit=trace([(y+137.5)*100,(x+137.5)*100,20000],[(y+137.5)*100,(x+137.5)*100,-10000]);check(hit is not None,chunk['id']+' actual Unreal ground trace')
  if hit is not None:check(abs(hit_location(hit)[2]-chunk['center_height_m']*100)<.5,chunk['id']+' collision height agrees with remeshed GLB',hit_cm=hit_location(hit)[2])
 portals=json.loads((OUT/'chunk-sources.json').read_text('utf-8'))['portals']
 for portal in portals:
  x,y,z=portal['door_world_m'];dx,dy=portal['outward'];x+=dx;y+=dy
  for lane in (-.8,0,.8):
   xx=x-dy*lane;yy=y+dx*lane;hit=trace([yy*100,xx*100,(z+1)*100],[yy*100,xx*100,(z-1)*100]);check(hit is not None and abs(hit_location(hit)[2]-z*100)<.5,portal['building']+f' Unreal entrance support {lane}')
 # Verify actual collision across main and ordinary door apertures; elevator
 # motion remains in source animations and is not claimed as runtime-connected.
 source_world=json.loads((ROOT/'output/street-social-city-production/shop-street-v1/world-assembly.json').read_text('utf-8'))
 for b in source_world['buildings']:
  r=json.loads((Path(b['source']).parent/'assembly-report.json').read_text('utf-8'));angle=math.radians(b['rotation_z_deg']);co,si=math.cos(angle),math.sin(angle);cx,cy,cz=b['position_m']
  for door in r['doors']:
   if door['kind']=='sliding':continue
   if 'position_m' in door:
    px,py,pz=door['position_m'];yaw=math.radians(door['yaw_deg']);tx,ty=math.cos(yaw),math.sin(yaw)
   else:px,py=door['center_m'];pz=r['lift']['stops'][door['floor']-1];tx,ty=(1,0) if door['axis']==0 else (0,1)
   nx,ny=-ty,tx
   def worldpoint(offset):
    xx,yy=px+nx*offset,py+ny*offset;east=co*xx-si*yy+cx;north=si*xx+co*yy+cy;return [north*100,east*100,(cz+pz+1)*100]
   check(trace(worldpoint(-.45),worldpoint(.45)) is None,b['building']+'/'+door['id']+' actual Unreal door aperture')
 # Reproducible daylight and a usable initial viewport/player position.
 sun=current.get('CitySun') or spawn(unreal.DirectionalLight,unreal.Vector(55000,55000,50000),unreal.Rotator(pitch=-50,yaw=-35,roll=0));sun.set_actor_label('CitySun');sun.root_component.set_mobility(unreal.ComponentMobility.MOVABLE);sun.light_component.set_intensity(6);sun.light_component.set_editor_property('atmosphere_sun_light',True)
 sky=current.get('CitySkyLight') or spawn(unreal.SkyLight,unreal.Vector(55000,55000,50000));sky.set_actor_label('CitySkyLight');sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE);sky.light_component.set_intensity(1.5);sky.light_component.set_editor_property('real_time_capture',False);sky.light_component.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP);sky.light_component.set_editor_property('cubemap',unreal.EditorAssetLibrary.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'));sky.light_component.recapture_sky()
 if 'CityAtmosphere' not in current:atmosphere=spawn(unreal.SkyAtmosphere,unreal.Vector());atmosphere.set_actor_label('CityAtmosphere')
 if 'StreetPlayerStart' not in current:
  start=spawn(unreal.PlayerStart,unreal.Vector(46000,21760,1400),unreal.Rotator(pitch=0,yaw=0,roll=0));start.set_actor_label('StreetPlayerStart')
 unreal.EditorLevelLibrary.set_level_viewport_camera_info(unreal.Vector(42000,18500,6000),unreal.Rotator(pitch=-20,yaw=40,roll=0));check(unreal.EditorLevelLibrary.save_current_level(),'Unreal map saved');check(len([p for p in placed if p['kind']=='shop_floor'])==288,'all 288 shop floors placed');check(len([p for p in placed if p['kind']=='ground'])==16,'all 16 chunks placed')
 result=dict(status='passed' if not errors else 'failed',map=MAP,project=str(ROOT/'StreetSocialCity/StreetSocialCity.uproject'),checks=len(checks),errors=errors,actors=placed,results=checks,scope='Actual Unreal static environment, persisted materials/textures, metric placement and collision queries; door/elevator runtime controllers are not connected')
 (OUT/'unreal-material-bindings.json').write_text(json.dumps(dict(status='passed',unique_materials=len(CANONICAL_MATERIALS),bindings=MATERIAL_BINDINGS),indent=2),'utf-8')
 (OUT/'unreal-acceptance.json').write_text(json.dumps(result,indent=2),'utf-8');unreal.log('City assembly '+result['status']+' '+str(len(checks)))
 if errors:raise RuntimeError(str(errors[:15]))
except Exception:
 (OUT/'unreal-assembly-error.txt').write_text(traceback.format_exc(),'utf-8');raise
finally:
 if '-CityAutoExit' in unreal.SystemLibrary.get_command_line():unreal.SystemLibrary.quit_editor()
