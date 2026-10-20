"""Shared building selection, geometry contract and asset-library boundaries."""
import json,copy
from functools import lru_cache
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
LIBRARY=ROOT/'output/street-social-city-production/S01-modular-components-v1'
@lru_cache(maxsize=2)
def _manifest_snapshot(mtime):
    return json.loads((ROOT/'Docs/StreetSocialCity/production/assembly-manifest.json').read_text('utf-8'))
def load_manifest():
    return _manifest_snapshot((ROOT/'Docs/StreetSocialCity/production/assembly-manifest.json').stat().st_mtime_ns)
def context(bid='S01'):
    m=load_manifest()
    b=next(q for q in m['layouts'] if q.get('kind')=='building' and q['building']==bid)
    if not b.get('small_shop_design'):raise ValueError('Special building requires its own layout adapter: '+bid)
    b=copy.deepcopy(b);world_yaw=0
    entry=next(q for q in b['small_shop_design']['floors'][0]['doors'] if q['source']=='building_entrance')
    if entry['outward_side']=='north':
        world_yaw=180
        sides={'north':'south','south':'north','east':'west','west':'east'}
        def rotate(value,key=''):
            if isinstance(value,dict):return {k:rotate(v,k) for k,v in value.items()}
            if isinstance(value,list):
                if ('bounds_m' in key) and len(value)==4 and all(isinstance(v,(int,float)) for v in value):return [-value[2],-value[3],-value[0],-value[1]]
                if key in ('center_m','local_position_m') and len(value) in (2,3):return [-value[0],-value[1]]+value[2:]
                return [rotate(v,key) for v in value]
            if key in ('side','outward_side') and value in sides:return sides[value]
            return value
        b['small_shop_design']['floors']=rotate(b['small_shop_design']['floors'])
    out=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building'
    f=b['small_shop_design']['floors'][0]
    stair=next(c['bounds_m'] for c in f['cells'] if c['kind']=='stairs')
    lift=next(q for q in f['doors'] if q['kind']=='sliding')['center_m'];doorx=lift[0]-.1
    return dict(id=bid,building=b,out=out,width=b['footprint_m'][0],depth=b['footprint_m'][1],world_yaw_deg=world_yaw,stair_center=[(stair[0]+stair[2])/2,(stair[1]+stair[3])/2],stair_far=stair[3]-.2,lift_door=[doorx,lift[1]],cab=[doorx-1.075,lift[1]],lobby=[lift[0]+1.2,f['cells'][1]['bounds_m'][1]+1],entry=next(q['center_m'] for q in f['doors'] if q['source']=='building_entrance'))
def s02_placements():
    """Reviewed dimensions, semantic equipment and positions, not scale-of-S01."""
    r=[]
    def put(f,a,n,p,yaw=0,scale=(1,1,1),actions=()):r.append(dict(floor=f,asset=a,name=n,pos=p,yaw=yaw,scale=scale,actions=actions))
    put(1,'steel-counter','order-counter',(-3.2,-6.95,0),90,scale=(1,.75,1),actions=['order','pay'])
    put(1,'pos','cash-register',(-3.2,-6.95,.9),90,actions=['pay'])
    put(1,'steel-counter','pickup-counter',(-3.2,-5.55,0),90,scale=(1,.75,1),actions=['pickup'])
    put(1,'steel-counter','mix-counter',(1.95,-6.6,0),scale=(3,1,1),actions=['mix','seal'])
    put(1,'cup-sealer','cup-sealer',(.65,-6.6,.9),180,actions=['seal'])
    put(1,'tea-dispenser','tea-dispenser',(2,-6.6,.9),180,actions=['dispense_tea'])
    put(1,'syrup-doser','syrup-doser',(3,-6.6,.9),180,actions=['measure_syrup'])
    put(1,'steel-counter','boiler-counter',(.65,-3.4,0),actions=['brew_tea'])
    put(1,'tea-dispenser','tea-brewer',(.65,-3.4,.9),actions=['brew_tea'])
    put(1,'double-sink','wash-station',(2.05,-3.4,0),actions=['wash_equipment'])
    put(1,'handwash','staff-handwash',(3.4,-3.4,0),actions=['wash_hands'])
    put(1,'ice-maker','ice-maker',(3.9,-5.1,0),-90,actions=['collect_ice'])
    put(1,'sofa','waiting-bench',(-3.7,-3.35,0),actions=['sit','wait'])
    put(1,'menu-board','menu',(1.95,-6.6,2.25),actions=['read_menu'])
    put(2,'double-sink','prep-wash',(2.1,-3.4,3.8),actions=['wash_ingredients'])
    put(2,'steel-table','packing-table',(1.3,-6.2,3.8),scale=(2,1.25,1),actions=['pack','prepare'])
    # The 1.9m plan depth reserves working space; it is not a 1.9m-deep body.
    put(2,'fridge','ingredients-fridge',(3.85,-6.85,3.8),180,scale=(.9/.7,1,1.9/1.8),actions=['store_cold'])
    for i,y in enumerate([-6.95,-6,-5.05]):
        put(2,'bookshelf',f'dry-rack-{i}',(-4.15,y,3.8),90,scale=(.9,1,.95),actions=['store_ingredients'])
        for row in range(5):
            for j in range(3):put(2,'ingredient-box',f'ingredient-{i}-{row}-{j}',(-4.15,-7.22+i*.95+j*.24,3.8+.027*.95+row*.395*.95),actions=['take_ingredient'])
    put(3,'bed','nap-bed',(2.9,-3.25,7.6),actions=['sleep','nap'])
    put(3,'wardrobe','staff-locker',(.9,-1.85,7.6),scale=(1.5/.7,.7/1.5,1),actions=['store_clothes'])
    put(3,'sofa','staff-sofa',(-3.7,-6.95,7.6),actions=['sit','rest'])
    put(3,'table','staff-desk',(-3.7,-5.7,7.6),scale=(1.25,1,1),actions=['read','work'])
    put(3,'chair','staff-chair',(-3.7,-4.9,7.6),180,actions=['sit'])
    # The basin remains open, not embedded through a full solid countertop.
    put(3,'steel-counter','pantry-left',(.65,-7,7.6),actions=['prepare_food'])
    put(3,'handwash','pantry-basin',(1.65,-7,7.6),180,actions=['wash'])
    put(3,'fridge','staff-fridge',(3.8,-7,7.6),180,actions=['store_food'])
    put(3,'shower','staff-shower',(-3.95,2.75,7.6),actions=['shower'])
    for i,x in enumerate([1.35,2.55]):put(1,'liftpanel',f'menu-suspension-{i}',(x,-6.6,2.9),scale=(.015/.62,.015/.04,.6/2.4))
    return r
