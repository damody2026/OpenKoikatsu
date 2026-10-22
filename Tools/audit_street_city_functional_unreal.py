"""Reopen the saved complete world and ray-test the revised functional components."""
import unreal,sys,json,math,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_functional_revision import ROOT,OUT,read,sha
MAP='/Game/UniversityCity/Maps/UniversityCity';checks=[];issues=[];SOURCE_SHA=sha(__file__)
def check(ok,name,**data):
 q=dict(passed=bool(ok),name=name,**data);checks.append(q)
 if not ok:issues.append(q)
def main():
 adoption=read(OUT/'adoption.json');inventory=read(OUT.parent/'scene-integration-v1/inventory.json');world=unreal.EditorLevelLibrary.get_editor_world();assert world.get_path_name().split('.')[0]==MAP;actors={o.get_actor_label():o for o in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()};iv={r['id']:r for r in inventory['rows']}
 for building in adoption['buildings']:
  r=read(building['receipt']);bid=r['building'];first=r['floors'][0]['id'];base=iv[first]['position_cm']
  def pt(p):return unreal.Vector(base[0]+p[1]*100,base[1]+p[0]*100,base[2]+p[2]*100)
  def ray(start,end):
   hit=unreal.SystemLibrary.line_trace_single(world,pt(start),pt(end),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True)
   return hit if hit is not None and hit.to_tuple()[0] else None
  for f in r['floors']:
   comp=actors[f['id']].static_mesh_component;check(unreal.EditorAssetLibrary.get_metadata_tag(comp.static_mesh,'StreetCitySourceSHA256')==f['source_sha256'],'native revised source '+f['id']);check(bool(comp.get_editor_property('cast_shadow')),'native shadow casting '+f['id'])
  for q in r['opaque_bays']:
   x,y,z=q['position_m'];axis=0 if q['size_m'][0]<q['size_m'][1] else 1;start=[x,y,z+1.45];end=start[:];start[axis]-=.4;end[axis]+=.4;hit=ray(start,end);check(hit is not None,'native opaque facade collision '+q['name'])
   comp=actors[next(f['id'] for f in r['floors'] if f['floor']==q['floor'])].static_mesh_component
   if q is r['opaque_bays'][0]:
    checked=0
    for i,slot in enumerate(comp.static_mesh.get_editor_property('static_materials')):
     mat=comp.get_material(i)
     source_name=' '.join(str(slot.get_editor_property(k)) for k in ['imported_material_slot_name','material_slot_name']).lower()
     if any(key in source_name for key in ['limestone','rainscreen']):checked+=1;check(mat.get_editor_property('blend_mode')==unreal.BlendMode.BLEND_OPAQUE,'native opaque exterior shader '+mat.get_name())
    check(checked>0,'actual exterior shader slots inspected')
  for room in r['classrooms']:
   check(room['room']+'-teaching-board-screen' in {q['name'] for q in r['additions']},'certified native teaching display '+room['room'])
   display=next(q for q in r['additions'] if q['owner']==room['room'] and q['kind']=='teaching_display');ox,oy,oz=display['position_m'];angle=math.radians(display['yaw']);cc,ss=math.cos(angle),math.sin(angle)
   def local(xx,yy,zz):return [ox+cc*xx-ss*yy,oy+ss*xx+cc*yy,oz+zz]
   board=next(q for q in display['parts'] if q['finish']=='board');fabric=next(q for q in display['parts'] if q['finish']=='screen');by=board['pos'][1];sy=fabric['pos'][1]
   check(ray(local(0,sy-.05,1.8),local(0,sy+.05,1.8)) is not None,'native lowered central projection fabric '+room['room'])
   check(ray(local(0,by-.05,1.8),local(0,by+.05,1.8)) is not None,'native continuous board behind screen '+room['room'])
   for xx in [-board['size'][0]/2+.4,board['size'][0]/2-.4]:check(ray(local(xx,by-.05,1.8),local(xx,by+.05,1.8)) is not None,'native exposed blackboard wing '+room['room']+f' {xx}')
   if not room['tiered']:continue
   rec=next(q for q in r['additions'] if q['owner']==room['room'] and q['kind']=='lecture_tiers');x,y,z=rec['position_m'];t=math.radians(rec['yaw']);c,s=math.cos(t),math.sin(t)
   for yy,hh in [(-2.85,.12),(-2.55,.24),(-2.25,.36),(-1.65,.48),(-1.05,.36),(-.025,.24),(1,.12)]:
    for xx in [-.8,0,.8]:
     a=[x+c*xx-s*yy,y+s*xx+c*yy,z+hh+.035];b=[*a[:2],z+hh-.035];hit=ray(a,b);check(hit is not None,'native lecture tread '+room['room']+f' {xx}/{yy}');check(ray(a,[*a[:2],z+hh+2.2]) is None,'native lecture headroom '+room['room']+f' {xx}/{yy}')
   for xx in [-room['clear_size_m'][0]/2+1.05,room['clear_size_m'][0]/2-1.05]:
    for yy in [-3.5,-2,-.2,1.6,3.3]:
     a=[x+c*xx-s*yy,y+s*xx+c*yy,z+.03];check(ray(a,[*a[:2],z-.03]) is not None,'native flat bypass support '+room['room']+f' {xx}/{yy}');check(ray(a,[*a[:2],z+2.2]) is None,'native flat bypass clear '+room['room']+f' {xx}/{yy}')
 result=dict(status='failed' if issues else 'passed',checks=len(checks),issues=issues,results=checks,map=MAP,map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'),source_sha256=SOURCE_SHA,adoption_sha256=sha(OUT/'adoption.json'),scope='Actual reopened native model identities, shadow flags, opaque shaders and facade collision, lowered projection fabric and continuous blackboard wings, lecture treads, headroom and flat bypass routes')
 (OUT/'native-acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8');assert not issues,str(issues[:10])
assert unreal.EditorLevelLibrary.load_level(MAP)
unreal.AutomationLibrary.finish_loading_before_screenshot()
unreal.EditorPythonScripting.set_keep_python_script_alive(True);frames=0
def tick(dt):
 global frames
 frames+=1
 if frames<60:return
 unreal.unregister_slate_post_tick_callback(handle)
 try:main()
 except Exception:(OUT/'native-error.txt').write_text(traceback.format_exc(),'utf8');unreal.log_error(traceback.format_exc())
 finally:unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
