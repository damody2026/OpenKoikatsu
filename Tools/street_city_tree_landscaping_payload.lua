return [====[
import zipfile,ast,collections
LANDTAG='TreeLandscaping-v3'
def auxiliary(m):return '/tree_trunk_collision/' in m or '/tree_grate/' in m
def snapshot():
 rows=[]
 for a,c in component_index().values():
  r=mesh_state(a,c,True);r['tree']=bool(tree_kind(c.static_mesh));r['auxiliary']=auxiliary(r['mesh']);rows.append(r)
 return rows
def land_probe():
 assert not (OUT/'baseline.json').exists(),'Do not replace original landscaping baseline'
 rows=snapshot();write(OUT/'baseline.json',dict(status='passed',rows=rows,map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')))
 world=unreal.EditorLevelLibrary.get_editor_world();ignore=[a for a,c in component_index().values() if tree_kind(c.static_mesh) or auxiliary(c.static_mesh.get_path_name())];samples=[]
 for r in rows:
  if not r['tree'] or not r['visible'] or r['hidden']:continue
  for t in r.get('instances',[r['actor_transform']]):
   p=t['location'];h=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(p[0],p[1],p[2]+120),unreal.Vector(p[0],p[1],p[2]-500),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ignore,unreal.DrawDebugTrace.NONE,True)
   v=h.to_tuple() if h else None
   if v and v[0]:
    c=v[10];samples.append(dict(actor=r['actor'],position=p,hit_actor=v[9].get_actor_label() if v[9] else None,hit_component=c.get_name() if c else None,mesh=c.static_mesh.get_path_name() if isinstance(c,unreal.StaticMeshComponent) and c.static_mesh else None,hit=[v[4].x,v[4].y,v[4].z],tuple_types=[type(x).__name__ for x in v],tuple_tail=[str(x) for x in v[11:]],materials=[c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())] if isinstance(c,unreal.StaticMeshComponent) else []))
   else:samples.append(dict(actor=r['actor'],position=p,hit_actor=None))
 write(OUT/'land_probe.json',dict(status='passed',samples=samples,tree_count=len(samples),components=len(rows)))

def land_context():
 global FIELD,ROADS,BUILDINGS,WORLD,IGNORE,OCCUPIED,WALKWAYS
 with zipfile.ZipFile(BASE/'world-ground-v1/world-field.npz') as z:
  FIELD={}
  for name in ['height_map','kind_map','water_map']:
   raw=z.read(name+'.npy');start=10 if raw[6]==1 else 12;length=int.from_bytes(raw[8:start],'little');h=ast.literal_eval(raw[start:start+length].decode().strip());fmt={'|u1':'B','<f4':'f','<f8':'d'}[h['descr']];FIELD[name]=(raw[start+length:],fmt,struct.calcsize(fmt))
 ROADS=[]
 for r in read(BASE/'world-ground-v1/ground-plan.json')['roads']:
  for a,b in zip(r['points_map_m'],r['points_map_m'][1:]):ROADS.append((a[0],1100-a[1],b[0],1100-b[1],r['width_m']/2,r['kind']))
 BUILDINGS=read(BASE/'city-art-v1/plan.json')['buildings'];WORLD=unreal.EditorLevelLibrary.get_editor_world();IGNORE=list({a for a,c in component_index().values() if tree_kind(c.static_mesh) or auxiliary(c.static_mesh.get_path_name()) or a.get_actor_label()=='LandscapeV3_SoilPlanters'});OCCUPIED=[];WALKWAYS=[]
 for a,c in component_index().values():
  if '/tactile/' not in c.static_mesh.get_path_name() or not c.is_visible() or c.get_editor_property('hidden_in_game'):continue
  for i in range(c.get_instance_count()):
   p=c.get_instance_transform(i,True).translation;WALKWAYS.append((p.y/100,p.x/100))
 # Keep trunks away from art, playing fields, outdoor stages and fixed furnishings.
 for a,c in component_index().values():
  if not c.is_visible() or c.get_editor_property('hidden_in_game') or a in IGNORE:continue
  label=a.get_actor_label()
  if label.startswith(('Activity_','Destination','CampusOutdoor','World_','CityDestination','CityArt_','Resort_')) and not re.fullmatch(r'G\d+',label):
   box=c.static_mesh.get_bounding_box();extent=(box.max-box.min)*.5;center=(box.max+box.min)*.5
   transforms=[c.get_instance_transform(i,True) for i in range(c.get_instance_count())] if isinstance(c,unreal.InstancedStaticMeshComponent) else [c.get_world_transform()]
   for t in transforms:
    origin=unreal.MathLibrary.transform_location(t,center);s=t.scale3d;radius=math.hypot(extent.x*s.x,extent.y*s.y)/100
    if radius>80:continue
    OCCUPIED.append([origin.y/100,origin.x/100,radius,radius,label])

def field(name,x,y):
 raw,fmt,size=FIELD[name];i=max(0,min(1100,round(x)));j=max(0,min(1100,round(1100-y)));return struct.unpack_from('<'+fmt,raw,(j*1101+i)*size)[0]
def route_reason(x,y,margin=1.2):
 if any((x-a)**2+(y-b)**2<2.7**2 for a,b in WALKWAYS):return 'actual tactile / sidewalk clearance'
 for ax,ay,bx,by,w,kind in ROADS:
  dx,dy=bx-ax,by-ay;t=max(0,min(1,((x-ax)*dx+(y-ay)*dy)/max(1e-6,dx*dx+dy*dy)))
  if (x-ax-t*dx)**2+(y-ay-t*dy)**2<(w+margin)**2:return kind+' reserved route'
 return None
def ground(x,y,z=None):
 if x<8 or y<8 or x>1092 or y>1092:return None,'outside planting bounds'
 if field('water_map',x,y):return None,'water'
 why=route_reason(x,y)
 if why:return None,why
 for b in BUILDINGS.values():
  px,py,_=b['position_m'];w,d=b['footprint_m']
  if abs(x-px)<w/2+3 and abs(y-py)<d/2+3:return None,'building and entrance buffer'
 h=field('height_map',x,y) if z is None else z
 hit=unreal.SystemLibrary.line_trace_single(WORLD,point([x,y,h+2]),point([x,y,h-6]),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,IGNORE,unreal.DrawDebugTrace.NONE,True);v=hit.to_tuple() if hit else None
 if not v or not v[0]:return None,'no surface'
 a,c=v[9],v[10];label=a.get_actor_label() if a else '';slot='';section=-1
 if isinstance(c,unreal.StaticMeshComponent):
  result=c.get_material_from_collision_face_index(v[15]);material,section=result if isinstance(result,tuple) else (result,-1)
  slots=c.static_mesh.get_editor_property('static_materials')
  # Section is not a material slot: resolve the material itself and original slot names.
  names=[str(s.get_editor_property('imported_material_slot_name')) for i,s in enumerate(slots) if c.get_material(i)==material or c.static_mesh.get_material(i)==material];slot='|'.join(names)
 if label.startswith('Street_planter_square_'):return dict(position=[x,y,v[4].z/100],surface='existing soil planter',actor=label,slot=slot),None
 # Pavement is allowed only as support for a new soil-filled raised planter, never a bare trunk.
 # The route / entrance checks above reserve the full pedestrian width before this branch.
 if label.startswith('Street_paving_'):return dict(position=[x,y,v[4].z/100],surface='needs planter',actor=label,slot=slot),None
 if not re.fullmatch(r'G\d+',label):return None,'surface occupied: '+label
 natural=any(k in slot.lower() for k in ['grass','forest','riverbank','sand'])
 urban='urban' in slot.lower()
 if not natural and not urban:return None,'paved surface: '+slot
 if abs(v[7].z)<.92:return None,'slope too steep'
 return dict(position=[x,y,v[4].z/100],surface='soil' if natural else 'needs planter',actor=label,slot=slot),None

def from_tf(t):return unreal.Transform(location=unreal.Vector(*t['location']),rotation=unreal.Quat(*t['rotation']).rotator(),scale=unreal.Vector(*t['scale']))
def new_cluster(label,mesh,materials,transforms,visible=True,collision='NoCollision'):
 actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};a=actors.get(label)
 if not a:
  bp=EAL.load_asset('/Game/UniversityCity/StreetScenery/BP_StreetCluster');a=SUB.spawn_actor_from_class(bp.generated_class(),unreal.Vector());a.set_actor_label(label)
 else:assert LANDTAG in [str(t) for t in a.tags]
 a.tags=[unreal.Name(LANDTAG)];a.set_folder_path('UniversityCity/TreeLandscapingV3');c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);name=c.get_name();c=set_tree_binding(a,name,mesh,materials);c.clear_instances();c.set_visibility(visible,True);c.set_hidden_in_game(not visible,True);c.set_collision_profile_name(collision);c.set_cull_distances(45000,65000)
 for t in transforms:c.add_instance(from_tf(t),True)
 return a,c

def land_install():
 baseline=read(OUT/'baseline.json');assert not (OUT/'land_install.json').exists(),'Already installed; audit instead of duplicating'
 mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';assert sha(mapfile)==baseline['map_sha256'],'World changed after inventory';shutil.copy2(mapfile,OUT/'UniversityCity-before-landscaping.umap');land_context()
 live=component_index();imports=read(BASE/'tree-upgrade-v2/tree_import.json')['assets'];bindings=read(BASE/'tree-upgrade-v2/install.json')['components'];approved=[];changes=[];counts=collections.Counter();positions=[];planters=[];replacements=0;group_rows=[]
 def vacant(p,gap):return all((p[0]-q[0])**2+(p[1]-q[1])**2>=gap*gap for q in positions)
 for r in baseline['rows']:
  if not r['tree'] or not r['visible'] or r['hidden']:continue
  key=(r['actor'],r['component']);a,c=live[key];rows=[];bind=bindings['|'.join(key)];variant=bind['variant'];mats=[EAL.load_asset(p) for p in r['materials']];mesh=c.static_mesh
  # Sparse palms are unsuitable shade trees along the shop street; use the existing full-canopy banyan.
  if bind['region']=='shopping' and variant in ['palm','columnar']:
   variant='banyan';source=next(v for v in bindings.values() if v['variant']==variant);mesh=EAL.load_asset(source['asset']);mats=[EAL.load_asset(p) for p in source['materials']];c=set_tree_binding(a,key[1],mesh,mats);replacements+=r.get('count',1)
  for i,t in enumerate(r.get('instances',[r['actor_transform']])):
   old=[t['location'][1]/100,t['location'][0]/100,t['location'][2]/100];site,reason=ground(*old);origin_reason=reason
   if site and not vacant(site['position'],3):site=None;reason='crowded trunks'
   if site is None:
    found=None
    for radius in [3,5,8,12,18,26,38,54,75]:
     for j in range(24):
      angle=(j*15+(i%3)*5)*math.pi/180;x=old[0]+radius*math.cos(angle);y=old[1]+radius*math.sin(angle)
      if not vacant([x,y],5):continue
      candidate,why=ground(x,y)
      if candidate and candidate['surface']=='soil':found=candidate;break
     if found:break
    site=found
   if site is None:
    changes.append(dict(actor=r['actor'],index=i,old=old,new=None,reason=reason));counts['retired_no_safe_site']+=1;continue
   p=site['position'][:]
   if site['surface']=='needs planter':planters.append(p[:]);p[2]+=.48;counts['new_soil_planters']+=1
   nt=dict(t,location=[p[1]*100,p[0]*100,p[2]*100]);positions.append(p[:2]);rows.append(nt);approved.append(dict(actor=r['actor'],index=len(rows)-1,position=p,support=site,variant=variant));counts['kept' if math.dist(old,p)<.12 else 'relocated']+=1
   if math.dist(old,p)>=.12:changes.append(dict(actor=r['actor'],index=i,old=old,new=p,reason=origin_reason or 'soil datum / planter correction'))
  c.clear_instances()
  for t in rows:c.add_instance(from_tf(t),True)
  group_rows.append(dict(actor=r['actor'],component=r['component'],count=len(rows),variant=variant))
  if len(group_rows)%25==0:unreal.log('LANDSCAPING_PROGRESS '+str(len(group_rows))+' '+str(dict(counts)))
 # Existing grates are decorative overlays rather than soil openings. Retire them and all old collider groups.
 for r in baseline['rows']:
  if not r['auxiliary']:continue
  a,c=live[(r['actor'],r['component'])];c.clear_instances() if isinstance(c,unreal.InstancedStaticMeshComponent) else None;c.set_visibility(False,True);c.set_hidden_in_game(True,True);c.set_collision_profile_name('NoCollision')
 # Add a mixed grove only to verified open campus grass, avoiding play facilities and furnishings.
 extra={};seed=__import__('random').Random(20261022)
 for y in range(610,963,8):
  for x in range(205,780,8):
   p=[x+seed.uniform(-1.4,1.4),y+seed.uniform(-1.4,1.4)]
   if not vacant(p,7):continue
   if any(abs(p[0]-a)<w+3 and abs(p[1]-b)<d+3 for a,b,w,d,label in OCCUPIED):continue
   site,why=ground(*p)
   if not site or site['surface']!='soil':continue
   variant=seed.choice(['camphor-a','camphor-b','banyan','terminalia','golden-flower','white-flower']);scale=seed.uniform(.82,1.05);rot=unreal.Rotator(pitch=0,yaw=seed.uniform(0,360),roll=0).quaternion();t=dict(location=vec(point(site['position'])),rotation=[rot.x,rot.y,rot.z,rot.w],scale=[scale]*3);extra.setdefault(variant,[]).append(t);positions.append(p);approved.append(dict(actor='LandscapeV3_Campus_'+variant,index=len(extra[variant])-1,position=site['position'],support=site,variant=variant));counts['campus_added']+=1
   if counts['campus_added']>=220:break
  if counts['campus_added']>=220:break
 for variant,ts in extra.items():
  binding=next(v for v in bindings.values() if v['variant']==variant);a,c=new_cluster('LandscapeV3_Campus_'+variant,EAL.load_asset(binding['asset']),[EAL.load_asset(p) for p in binding['materials']],ts)
 # Reuse the remeshed soil-filled square planter, with its real opening and correct metric bounds.
 sample=next(r for r in baseline['rows'] if '/planter_square/' in r['mesh']);mesh=EAL.load_asset(sample['mesh']);box=mesh.get_bounding_box();size=box.max-box.min;ts=[]
 for p in planters:
  sc=[200/size.x,200/size.y,48/size.z];t=dict(location=[p[1]*100-(box.min.x+box.max.x)*.5*sc[0],p[0]*100-(box.min.y+box.max.y)*.5*sc[1],p[2]*100-box.min.z*sc[2]],rotation=[0,0,0,1],scale=sc);ts.append(t)
 if ts:new_cluster('LandscapeV3_SoilPlanters',mesh,[EAL.load_asset(p) for p in sample['materials']],ts,True,'BlockAll')
 trunk=next(r for r in baseline['rows'] if '/tree_trunk_collision/' in r['mesh']);mesh=EAL.load_asset(trunk['mesh']);ts=[]
 for q in approved:
  p=q['position'];ts.append(dict(location=[p[1]*100,p[0]*100,p[2]*100],rotation=[0,0,0,1],scale=[1,1,1]))
 new_cluster('LandscapeV3_TrunkCollision',mesh,[EAL.load_asset(p) for p in trunk['materials']],ts,False,'BlockAll')
 assert counts['campus_added']>=100,dict(counts)
 assert sha(mapfile)==baseline['map_sha256'],'Another editor changed the world during landscaping; do not overwrite'
 saved=False
 for attempt in range(4):
  if unreal.EditorLevelLibrary.save_current_level():saved=True;break
  time.sleep(4)
 assert saved,'Map remains locked by another editor; original file retained'
 write(OUT/'land_install.json',dict(status='passed',counts=dict(counts),shade_tree_replacements=replacements,trees=approved,changes=changes,groups=group_rows,visible_instances=len(approved),map_sha256=sha(mapfile)))

def land_audit():
 baseline=read(OUT/'baseline.json');installed=read(OUT/'land_install.json');land_context();live=component_index();checks=0;issues=[]
 for r in baseline['rows']:
  if r['tree'] or r['auxiliary']:continue
  a,c=live[(r['actor'],r['component'])];now=mesh_state(a,c,True)
  for k in ['tree','auxiliary']:now[k]=r[k]
  assert now==r,('Unrelated component modified',r['actor'],r['component']);checks+=1
 for q in installed['trees']:
  key=next(k for k in live if k[0]==q['actor']);a,c=live[key];t=c.get_instance_transform(q['index'],True);p=[t.translation.y/100,t.translation.x/100,t.translation.z/100];assert math.dist(p,q['position'])<.001
  support,why=ground(*q['support']['position']);assert support and not why,(q,why);assert support['surface']==q['support']['surface'];checks+=1
  if support['surface']=='needs planter':
   ignored=[a for a in IGNORE if a.get_actor_label()!='LandscapeV3_SoilPlanters'];hit=unreal.SystemLibrary.line_trace_single(WORLD,point([p[0],p[1],p[2]+.2]),point([p[0],p[1],p[2]-.3]),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ignored,unreal.DrawDebugTrace.NONE,True);v=hit.to_tuple() if hit else None
   assert v and v[0] and v[9].get_actor_label()=='LandscapeV3_SoilPlanters',('Missing real planter under trunk',q)
   assert abs(v[4].z/100-p[2])<.15,('Tree root not on planter soil',q,v[4].z/100);checks+=1
 assert live[('LandscapeV3_TrunkCollision','HierarchicalInstancedStaticMesh')][1].get_instance_count()==installed['visible_instances']
 planter_count=installed['counts'].get('new_soil_planters',0)
 if planter_count:assert live[('LandscapeV3_SoilPlanters','HierarchicalInstancedStaticMesh')][1].get_instance_count()==planter_count
 from street_city_remesh_gate import verify
 imports=read(BASE/'tree-upgrade-v2/tree_import.json')['assets']
 for aid,r in imports.items():
  m=EAL.load_asset(r['asset']);assert API.get_lod_count(m)==4
  for row in r['remesh']:verify(Path(row['remesh_report']).parent);checks+=1
 if installed.get('campus_distribution'):
  for band in installed['campus_distribution']:
   actual=sum(q['actor'].startswith('LandscapeV3_Campus_') and band['north_min']<=q['position'][1]<band['north_max'] for q in installed['trees']);assert actual==band['count'],(band,actual);checks+=1
 write(OUT/'land_audit.json',dict(status='passed',checks=checks,trees=installed['visible_instances'],counts=installed['counts'],map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')))

def land_repair():
 installed=read(OUT/'land_install.json');assert not installed.get('walkway_revision'),'Already corrected';land_context();mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';assert sha(mapfile)==installed['map_sha256'];shutil.copy2(mapfile,OUT/'UniversityCity-before-walkway-refinement.umap');write(OUT/'land_install-initial.json',installed);live=component_index();accepted=[];changes=[];bins={};positions=[];planters=[];removed=0
 for q in installed['trees']:
  s=q['support']['position'];good,reason=ground(*s)
  if good:positions.append(q['position'][:2])
 for q in installed['trees']:
  key=next(k for k in live if k[0]==q['actor']);a,c=live[key];t=tf(c.get_instance_transform(q['index'],True));site,reason=ground(*q['support']['position']);original_reason=reason;old=q['position'][:]
  if not site:
   for radius in [3,5,8,12,18,26,38,54,75]:
    for j in range(24):
     ang=j*math.pi/12;x=old[0]+radius*math.cos(ang);y=old[1]+radius*math.sin(ang)
     if any((x-u)**2+(y-v)**2<5**2 for u,v in positions):continue
     candidate,why=ground(x,y)
     if candidate and candidate['surface']=='soil':site=candidate;break
    if site:break
   if site:positions.append(site['position'][:2])
  if not site:changes.append(dict(actor=q['actor'],old=old,new=None,reason=original_reason));removed+=1;continue
  p=site['position'][:]
  if site['surface']=='needs planter':planters.append(p[:]);p[2]+=.48
  rows=bins.setdefault(key,[]);t['location']=[p[1]*100,p[0]*100,p[2]*100];q=dict(q,index=len(rows),position=p,support=site);rows.append(t);accepted.append(q)
  if math.dist(old,p)>.01:changes.append(dict(actor=q['actor'],old=old,new=p,reason=original_reason))
 for key,ts in bins.items():
  a,c=live[key];c.clear_instances()
  for t in ts:c.add_instance(from_tf(t),True)
 for key in {k for k in live if tree_kind(live[k][1].static_mesh) and live[k][1].is_visible() and not live[k][1].get_editor_property('hidden_in_game')}-set(bins):live[key][1].clear_instances()
 a,c=live[('LandscapeV3_SoilPlanters','HierarchicalInstancedStaticMesh')];mesh=c.static_mesh;mats=[c.get_material(i) for i in range(c.get_num_materials())];box=mesh.get_bounding_box();size=box.max-box.min;ts=[]
 for p in planters:
  sc=[200/size.x,200/size.y,48/size.z];ts.append(dict(location=[p[1]*100-(box.min.x+box.max.x)*.5*sc[0],p[0]*100-(box.min.y+box.max.y)*.5*sc[1],p[2]*100-box.min.z*sc[2]],rotation=[0,0,0,1],scale=sc))
 new_cluster('LandscapeV3_SoilPlanters',mesh,mats,ts,True,'BlockAll')
 a,c=live[('LandscapeV3_TrunkCollision','HierarchicalInstancedStaticMesh')];mesh=c.static_mesh;mats=[c.get_material(i) for i in range(c.get_num_materials())];ts=[dict(location=vec(point(q['position'])),rotation=[0,0,0,1],scale=[1,1,1]) for q in accepted];new_cluster('LandscapeV3_TrunkCollision',mesh,mats,ts,False,'BlockAll')
 assert sha(mapfile)==installed['map_sha256'],'Concurrent world modification';assert unreal.EditorLevelLibrary.save_current_level();installed.update(trees=accepted,visible_instances=len(accepted),walkway_revision=dict(changes=changes,relocated=len(changes)-removed,retired=removed,tactile_samples=len(WALKWAYS)),map_sha256=sha(mapfile));installed['counts']['new_soil_planters']=len(planters);write(OUT/'land_install.json',installed);write(OUT/'land_repair.json',dict(status='passed',changes=installed['walkway_revision'],visible_instances=len(accepted),map_sha256=sha(mapfile)))

def land_render():land_audit();capture_trees()

def land_balance():
 installed=read(OUT/'land_install.json');assert not installed.get('campus_distribution');land_context();mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';assert sha(mapfile)==installed['map_sha256'];shutil.copy2(mapfile,OUT/'UniversityCity-before-campus-balance.umap');live=component_index();seed=__import__('random').Random(20261023);positions=[q['position'][:2] for q in installed['trees'] if not q['actor'].startswith('LandscapeV3_Campus_')];chosen=[];bands=[]
 for lower,upper in [(610,690),(690,770),(770,860),(860,965)]:
  candidates=[[x+seed.uniform(-1,1),y+seed.uniform(-1,1)] for y in range(lower+4,upper-3,7) for x in range(208,779,7)];seed.shuffle(candidates);accepted=[]
  for p in candidates:
   if any((p[0]-x)**2+(p[1]-y)**2<7**2 for x,y in positions):continue
   if any(abs(p[0]-a)<w+3 and abs(p[1]-b)<d+3 for a,b,w,d,label in OCCUPIED):continue
   site,why=ground(*p)
   if not site or site['surface']!='soil':continue
   accepted.append(site);positions.append(p)
   if len(accepted)==55:break
  assert len(accepted)==55,('Insufficient safe campus sites',lower,len(accepted));chosen+=accepted;bands.append(dict(north_min=lower,north_max=upper,count=55))
 extra=[q for q in installed['trees'] if q['actor'].startswith('LandscapeV3_Campus_')];assert len(extra)==len(chosen)==220;seed.shuffle(chosen);bins={};changes=[]
 for q,site in zip(extra,chosen):
  key=next(k for k in live if k[0]==q['actor']);a,c=live[key];t=tf(c.get_instance_transform(q['index'],True));old=q['position'][:];q['position']=site['position'];q['support']=site;t['location']=vec(point(q['position']));ts=bins.setdefault(key,[]);q['index']=len(ts);ts.append(t);changes.append(dict(actor=q['actor'],old=old,new=q['position']))
 for key,ts in bins.items():
  a,c=live[key];c.clear_instances()
  for t in ts:c.add_instance(from_tf(t),True)
 a,c=live[('LandscapeV3_TrunkCollision','HierarchicalInstancedStaticMesh')];mesh=c.static_mesh;mats=[c.get_material(i) for i in range(c.get_num_materials())];ts=[dict(location=vec(point(q['position'])),rotation=[0,0,0,1],scale=[1,1,1]) for q in installed['trees']];new_cluster('LandscapeV3_TrunkCollision',mesh,mats,ts,False,'BlockAll')
 assert sha(mapfile)==installed['map_sha256'];assert unreal.EditorLevelLibrary.save_current_level();installed['campus_distribution']=bands;installed['map_sha256']=sha(mapfile);write(OUT/'land_install.json',installed);write(OUT/'land_balance.json',dict(status='passed',bands=bands,changes=changes,map_sha256=sha(mapfile)))
]====]
