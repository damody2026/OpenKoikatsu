return [====[
def mall_routes_state():
 world=unreal.EditorLevelLibrary.get_editor_world();actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};geometry=read(BASE/'mall-v1/geometry.json');rows=[]
 for i,q in enumerate(geometry['routes']):
  a=actors['Mall_M1_F'+str(q['floor']).zfill(2)];origin=a.get_actor_location();floor_z=next(f['z_m'] for f in geometry['contract']['floors'] if f['floor']==q['floor']);base=[origin.y/100,origin.x/100,origin.z/100+floor_z];aa=[base[0]+q['start'][0],base[1]+q['start'][1],base[2]+1];bb=[base[0]+q['end'][0],base[1]+q['end'][1],base[2]+1];hit=hit_data(unreal.SystemLibrary.capsule_trace_single(world,point(aa),point(bb),40,90,unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True));rows.append(dict(id=i,floor=q['floor'],name=q['name'],clear=not (hit and hit[0]),blocker=hit[9].get_actor_label() if hit and hit[0] and hit[9] else None))
 return rows

def previous_destination_state():
 rows=[]
 for a in SUB.get_all_level_actors():
  if 'CityDestinations-v2' not in [str(t) for t in a.tags]:continue
  q=dict(actor=a.get_actor_label(),transform=transform_data(a),hidden=a.get_editor_property('hidden'))
  c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
  if c:q.update(mesh=c.static_mesh.get_path_name(),instances=instance_hash(c),count=c.get_instance_count(),materials=[c.get_material(i).get_path_name() for i in range(c.get_num_materials())],collision=str(c.get_collision_profile_name()),visible=c.is_visible())
  elif isinstance(a,unreal.PointLight):q.update(intensity=a.point_light_component.get_editor_property('intensity'))
  rows.append(q)
 return sorted(rows,key=lambda q:q['actor'])

def dest_probe():
 assert DESTBATCH==3;actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};floors=[]
 for no in range(1,11):
  a=actors['Mall_M1_F'+str(no).zfill(2)];p=a.get_actor_location();floors.append(dict(floor=no,actor=a.get_actor_label(),position_m=[p.y/100,p.x/100,p.z/100],floor_height_m=4.2,need='one unique themed public-concourse artwork',new_v2_landmarks=0))
 old=read(BASE/'city-destinations-v2/install.json');assert len(old['instances'])==30 and not any(q.get('floor') for q in old['instances']);routes=mall_routes_state();preserved=previous_destination_state();result=dict(status='passed',floors=floors,mall_routes=routes,preserved_v2=preserved,previous_models=30,previous_zones=old['unique_zones'],map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'))
 assert not (DESTOUT/'install.json').exists(),'Do not replace pre-install gap baseline after install';write(DESTOUT/'gap-probe.json',result);write(OUT/'dest_probe.json',dict(status='passed',mall_floors=10,mall_routes=len(routes),preserved_previous_actors=len(preserved)))

def destination_mall_position(q,mesh,actors,placed):
 no=q['floor'];a=actors['Mall_M1_F'+str(no).zfill(2)];o=a.get_actor_location();geometry=read(BASE/'mall-v1/geometry.json');floor_z=next(f['z_m'] for f in geometry['contract']['floors'] if f['floor']==no);ox,oy,z=o.y/100,o.x/100,o.z/100+floor_z;bb=mesh.get_bounding_box();radius=max(.5,math.hypot(bb.max.x-bb.min.x,bb.max.y-bb.min.y)/200);height=(bb.max.z-bb.min.z)/100;assert height+.35<3.9;world=unreal.EditorLevelLibrary.get_editor_world();routes=[r for r in geometry['routes'] if r['floor']==no];attempts=[];sign=-1 if no%2==0 else 1
 for x,y in [(sign*x,y) for x in [35,45,25,55,-35,-45,-25,-55] for y in [7,-7,10,-10,4,-4]]:
  reason=None
  for r in routes:
   aa,bb=r['start'],r['end'];dx,dy=bb[0]-aa[0],bb[1]-aa[1];t=max(0,min(1,((x-aa[0])*dx+(y-aa[1])*dy)/max(1e-8,dx*dx+dy*dy)))
   if math.hypot(x-aa[0]-t*dx,y-aa[1]-t*dy)<radius+r['width']/2+1:reason='reserved public aisle';break
  p=[ox+x,oy+y,z];heights=[]
  if not reason:
   for dx,dy in [(0,0),(radius*.7,0),(-radius*.7,0),(0,radius*.7),(0,-radius*.7)]:
    h=hit_data(unreal.SystemLibrary.line_trace_single(world,point([p[0]+dx,p[1]+dy,z+.9]),point([p[0]+dx,p[1]+dy,z-.6]),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True))
    if not h or not h[0] or not h[9] or h[9].get_actor_label()!=a.get_actor_label():reason='not supported by intended mall floor';break
    heights.append(h[4].z/100)
   if not reason and (max(heights)-min(heights)>.04 or abs(heights[0]-z)>.35):reason='not clear finished floor'
  if not reason:
   centre=point([p[0],p[1],heights[0]+height/2+.1]);h=hit_data(unreal.SystemLibrary.box_trace_single(world,centre,centre,unreal.Vector(radius*90,radius*90,height*50),unreal.Rotator(pitch=0,yaw=0,roll=0),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True))
   if h and h[0]:reason='existing mall furniture or structure'
  if not reason:return [p[0],p[1],heights[0]+.01],radius,attempts
  attempts.append(dict(local=[x,y],reason=reason))
 raise AssertionError(('No clear mall display site',q['id'],no,attempts))

def destination_facility_position(q,mesh,actors,placed):
 facility=next(r for r in read(BASE/'world-completion-v1/outdoor/programme.json')['nodes'] if r['id']==q['facility']);native_name='World_'+q['facility'].replace('-','_');w,d=facility['size']
 if facility.get('existing_asset'):
  native_name='Outdoor_'+facility['existing_asset'].replace('-','_');physical=next(r for r in read(BASE/'campus-v1/outdoor/programme.json')['facilities'] if r['id']==facility['existing_asset']);w,d=physical['size']
 native=actors[native_name];o=native.get_actor_location();cx,cy,z=o.y/100,o.x/100,o.z/100;bb=mesh.get_bounding_box();radius=max(.6,math.hypot(bb.max.x-bb.min.x,bb.max.y-bb.min.y)/200);height=(bb.max.z-bb.min.z)/100;world=unreal.EditorLevelLibrary.get_editor_world();attempts=[]
 offsets=[(sx,sy) for sx in [-w*.40,w*.40,0] for sy in [d*.485,-d*.485]] if facility.get('existing_asset') else [(sx,sy) for sx in [-w*.28,w*.28,0,-w*.15,w*.15] for sy in [d*.25,-d*.25,0]]
 for x,y in offsets:
  p=[cx+x,cy+y,z];reason=None;heights=[]
  if any(math.hypot(p[0]-r['position_m'][0],p[1]-r['position_m'][1])<radius+r['radius_m']+2 for r in placed):reason='new landmark clearance'
  if not reason:
   for dx,dy in [(0,0),(radius*.7,0),(-radius*.7,0),(0,radius*.7),(0,-radius*.7)]:
    h=hit_data(unreal.SystemLibrary.line_trace_single(world,point([p[0]+dx,p[1]+dy,z+1]),point([p[0]+dx,p[1]+dy,z-1]),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True))
    if not h or not h[0]:reason='no public facility floor';break
    a=h[9];label=a.get_actor_label() if a else ''
    if label.startswith(('Destination_','Activity_','Resort_','Street_')):reason='existing furnishing';break
    heights.append(h[4].z/100)
   if not reason and (max(heights)-min(heights)>.08 or abs(heights[0]-z)>.35):reason='outside level facility platform'
  if not reason:
   centre=point([p[0],p[1],heights[0]+height/2+.1]);h=hit_data(unreal.SystemLibrary.box_trace_single(world,centre,centre,unreal.Vector(radius*85,radius*85,height*50),unreal.Rotator(pitch=0,yaw=0,roll=0),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True))
   if h and h[0]:reason='existing facility equipment'
  if not reason:return [p[0],p[1],heights[0]+.01],radius,attempts
  attempts.append(dict(offset=[x,y],reason=reason))
 raise AssertionError(('No clear supported public facility site',q['id'],q['facility'],attempts))

def expansion_audit():
 baseline=read(DESTOUT/'gap-probe.json');receipt=read(DESTOUT/'install.json');current=previous_destination_state();assert current==baseline['preserved_v2'],'Previous thirty landmarks or garden companions changed';before={r['id']:r for r in baseline['mall_routes']};after=mall_routes_state();regressions=[r for r in after if before[r['id']]['clear'] and not r['clear']];assert not regressions,str(regressions);floors={i:sum(q.get('floor')==i for q in receipt['instances']) for i in range(1,11)};assert all(n>=1 for n in floors.values()),floors
 actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};geometry=read(BASE/'mall-v1/geometry.json');supports=[];world=unreal.EditorLevelLibrary.get_editor_world()
 for q in receipt['instances']:
  if not q.get('floor'):continue
  a=actors['Mall_M1_F'+str(q['floor']).zfill(2)];o=a.get_actor_location();expected=o.z/100+next(f['z_m'] for f in geometry['contract']['floors'] if f['floor']==q['floor']);p=q['position_m'];assert abs(p[2]-expected)<.15,(q['id'],'Wrong floor height',p[2],expected)
  h=hit_data(unreal.SystemLibrary.line_trace_single(world,point([p[0],p[1],p[2]+.2]),point([p[0],p[1],p[2]-.3]),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[actors[q['actor']]],unreal.DrawDebugTrace.NONE,True));assert h and h[0] and h[9] and h[9].get_actor_label()==a.get_actor_label(),(q['id'],'Floor support changed');supports.append(dict(id=q['id'],floor=q['floor'],measured_ground_m=h[4].z/100,expected_m=expected))
 result=dict(status='passed',mall_floors=floors,mall_floor_supports=supports,mall_routes=after,new_route_blockers=regressions,previous_actors_preserved=len(current),previous_models_preserved=30,models_new=40,map_sha256=receipt['map_sha256']);write(DESTOUT/'expansion-audit.json',result)

def dest_verify():
 audit();dest_audit()
 if DESTBATCH==3:expansion_audit()
 write(OUT/'dest_verify.json',dict(status='passed',scope='Independent reopen; original city, previous30 and new batch; mall floor coverage and routes'))

def dest_capture():
 world=unreal.EditorLevelLibrary.get_editor_world();folder=DESTOUT/'renders';folder.mkdir(exist_ok=True);views=[];actors={a.get_actor_label():a for a in SUB.get_all_level_actors()};overrides=read(DESTOUT/'capture-overrides.json') if (DESTOUT/'capture-overrides.json').exists() else {}
 for batch in [2,3]:
  for q in read(BASE/('city-destinations-v'+str(batch))/'install.json')['instances']:
   a=actors[q['actor']];mesh=EAL.load_asset(q['asset']);bb=mesh.get_bounding_box();height=(bb.max.z-bb.min.z)/100;radius=q['radius_m'];p=q['position_m'];target=[p[0],p[1],p[2]+height*.48];candidates=[]
   # Keep the camera inside its floor, with real surrounding architecture visible.
   distance=max(2.5,radius*3.2,height*1.9);indoor=bool(q.get('floor'))
   if indoor:distance=min(distance,4.5)
   broadside=90 if (bb.max.x-bb.min.x)>(bb.max.y-bb.min.y)*1.25 else 0
   for turn in [0,45,-45,90,-90,135,-135,180]:
    angle=math.radians(q['yaw']+broadside+turn);eye=[p[0]+distance*math.cos(angle),p[1]+distance*math.sin(angle),p[2]+min(2.8,height*.75+.6) if indoor else p[2]+height*.8+1]
    h=hit_data(unreal.SystemLibrary.sphere_trace_single(world,point(eye),point(target),12,unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[a],unreal.DrawDebugTrace.NONE,True))
    if not h or not h[0]:candidates.append((abs(turn),eye))
   if q['id'] in overrides:eye=overrides[q['id']]['eye'];target=overrides[q['id']].get('target',target)
   else:
    assert candidates,('No unobstructed context camera',q['id']);eye=sorted(candidates,key=lambda r:r[0])[0][1]
   views.append(dict(id=q['id'],name=q['name'],zone=q['zone'],floor=q.get('floor'),eye=eye,target=target,actor=q['actor']))
 assert len(views)==70
 selection=set(PAYLOAD.get('capture_ids','').split(',')) if PAYLOAD.get('capture_ids') else set();previous=[]
 if selection:
  prior=read(DESTOUT/'render.json');assert prior['map_sha256']==sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap');previous=[r for r in prior['images'] if r['id'] not in selection];views=[r for r in views if r['id'] in selection];assert len(views)==len(selection)
 state=dict(i=0,phase='setup',wait=30,images=[],busy=False);refs=[]
 unreal.SystemLibrary.execute_console_command(world,'r.DepthOfFieldQuality 0')
 def tick(dt):
  if state['busy']:return
  if state['wait']>0:state['wait']-=1;return
  state['busy']=True
  try:
   if state['phase']=='setup':
    q=views[state['i']];p=point(q['eye']);rot=unreal.MathLibrary.find_look_at_rotation(p,point(q['target']));unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(p,rot);a=SUB.spawn_actor_from_class(unreal.SceneCapture2D,p,rot);a.set_actor_location(p,False,True);a.set_actor_rotation(rot,False);assert (a.get_actor_location()-p).length()<.01
    c=a.capture_component2d;c.set_editor_property('capture_every_frame',True);c.set_editor_property('always_persist_rendering_state',True);c.set_editor_property('capture_on_movement',False)
    # SceneCapture defaults disable Lumen independently of the actual project.
    # Match the saved project's renderer and retain temporal state during warmup.
    gi=unreal.SystemLibrary.get_console_variable_int_value('r.DynamicGlobalIlluminationMethod');reflection=unreal.SystemLibrary.get_console_variable_int_value('r.ReflectionMethod');pp=c.get_editor_property('post_process_settings')
    if gi==1:pp.set_editor_property('override_dynamic_global_illumination_method',True);pp.set_editor_property('dynamic_global_illumination_method',unreal.DynamicGlobalIlluminationMethod.LUMEN)
    if reflection==1:pp.set_editor_property('override_reflection_method',True);pp.set_editor_property('reflection_method',unreal.ReflectionMethod.LUMEN)
    c.set_editor_property('post_process_settings',pp);q['renderer']=dict(global_illumination=gi,reflection=reflection,temporal_warmup_frames=60)
    rt=unreal.RenderingLibrary.create_render_target2d(world,1440,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8);refs.append(rt);c.set_editor_property('texture_target',rt);c.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR);c.set_editor_property('fov_angle',65);state['current']=(q,a,c,rt);state['phase']='capture';state['wait']=60
   elif state['phase']=='capture':unreal.AutomationLibrary.finish_loading_before_screenshot();state['current'][2].set_editor_property('capture_every_frame',False);state['current'][2].capture_scene();state['phase']='export';state['wait']=10
   else:
    q,a,c,rt=state['current'];p=folder/(q['id']+'.png');unreal.RenderingLibrary.export_render_target(world,rt,str(folder),p.name);assert p.exists() and p.stat().st_size>5000;state['images'].append(dict(id=q['id'],name=q['name'],zone=q['zone'],floor=q.get('floor'),path=str(p),sha256=sha(p),camera=q));c.set_editor_property('capture_every_frame',False);c.set_editor_property('texture_target',None);SUB.destroy_actor(a);unreal.RenderingLibrary.release_render_target2d(rt);refs.remove(rt);state['i']+=1;state['phase']='setup';state['wait']=10;unreal.log('DEST CAPTURE '+q['id'])
    if state['i']==len(views):
     unreal.unregister_slate_post_tick_callback(destination_capture_handle);images=sorted(previous+state['images'],key=lambda r:r['id']);assert len(images)==70;result=dict(status='passed',images=images,count=70,map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'));write(DESTOUT/'render.json',result);write(OUT/'dest_capture.json',result);unreal.SystemLibrary.quit_editor()
  except BaseException:
   unreal.unregister_slate_post_tick_callback(destination_capture_handle);write(OUT/'dest_capture-error.json',dict(status='failed',error=traceback.format_exc()));unreal.SystemLibrary.quit_editor()
  finally:state['busy']=False
 global destination_capture_handle,DESTINATION_CAPTURE_REFS
 DESTINATION_CAPTURE_REFS=(state,refs);destination_capture_handle=unreal.register_slate_post_tick_callback(tick)
]====]
