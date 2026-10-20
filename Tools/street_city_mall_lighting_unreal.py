"""Managed retail luminaires; actual Unreal lights, not render-only lights."""
import json,math
from pathlib import Path
import unreal
from street_city_mall_boutiques import profiles,OUT
from street_city_localization import sha

def targets(actors):
 for p in profiles().values():
  a,b,c,d=p['bounds'];floor=actors[f'Mall_M1_F{p["floor"]:02d}'];z=(p['floor']-1)*4.2+3.80
  for i,(x,y) in enumerate([(a+5,b+5),(c-5,d-5)]):
   point=floor.get_actor_transform().transform_location(unreal.Vector(x*100,-y*100,z*100))
   yield p,f'MallLight_{p["id"]}_{i+1}',point

def install(actors,sub):
 for p,name,point in targets(actors):
  actor=actors.get(name)
  if actor is None:actor=sub.spawn_actor_from_class(unreal.RectLight,point,unreal.Rotator(pitch=-90,yaw=0,roll=0));actor.set_actor_label(name)
  comp=actor.get_component_by_class(unreal.RectLightComponent);assert comp
  comp.set_mobility(unreal.ComponentMobility.MOVABLE)
  actor.set_actor_location_and_rotation(point,unreal.Rotator(pitch=-90,yaw=0,roll=0),False,True)
  comp.set_intensity_units(unreal.LightUnits.LUMENS);comp.set_intensity(60000)
  comp.set_attenuation_radius(1400);comp.set_source_width(240);comp.set_source_height(40)
  comp.set_use_temperature(True);comp.set_temperature(p['lighting_kelvin'])
  comp.set_editor_property('cast_shadows',True)
  actor.set_folder_path('Mall/M1/Lighting/F'+str(p['floor']));actor.set_editor_property('tags',['StreetCityManaged','MallBoutiqueLighting',p['id']])

def audit(actors):
 rows=[]
 for p,name,point in targets(actors):
  actor=actors[name];comp=actor.get_component_by_class(unreal.RectLightComponent);actual=actor.get_actor_location()
  assert math.dist([actual.x,actual.y,actual.z],[point.x,point.y,point.z])<.1,(name,'Light position changed')
  assert abs(actor.get_actor_rotation().pitch+90)<.01
  assert comp.get_editor_property('intensity_units')==unreal.LightUnits.LUMENS
  assert abs(comp.get_editor_property('intensity')-60000)<.01 and abs(comp.get_editor_property('temperature')-p['lighting_kelvin'])<.01
  assert comp.get_editor_property('use_temperature') and comp.get_editor_property('cast_shadows')
  rows.append(dict(actor=name,tenant=p['id'],temperature_kelvin=p['lighting_kelvin'],intensity_lumens=60000,source_size_cm=[240,40],attenuation_cm=1400))
 assert len(rows)==160
 (OUT/'native-lighting.json').write_text(json.dumps(dict(status='passed',actors=rows,count=160,installer_sha256=sha(__file__),scope='game lighting values, not a certified lux calculation'),indent=2),'utf8')
