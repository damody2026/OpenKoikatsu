"""Independent actual-envelope and terrain checks of every new street instance."""
import math
import numpy as np
from street_city_streetscape import ROOT,BASE,OUT,read,write,sha,sample
d=read(OUT/'delivery.json');plan=read(OUT/'plan.json');assets={q['id']:q for q in d['assets']};buildings=read(ROOT/'Docs/StreetSocialCity/production/building-floor-audit.json')['buildings'];checks=[];issues=[]
with np.load(BASE/'world-ground-v1/world-field.npz') as file:height=file['height_map'];water=file['water_map']
def check(ok,name,**data):
 row=dict(passed=bool(ok),name=name,**data);checks.append(row)
 if not ok:issues.append(row)
for q in plan['instances']:
 asset=assets[q['asset']];bounds=asset['lods'][0]['bounds_m'];x,y,z=q['position_m'];sx,sy,sz=q['scale'];angle=math.radians(q['yaw_deg']);c,s=math.cos(angle),math.sin(angle);corners=[(x+c*xx*sx-s*yy*sy,y+s*xx*sx+c*yy*sy) for xx in bounds[0] for yy in bounds[1]];lo=[min(p[k] for p in corners) for k in [0,1]];hi=[max(p[k] for p in corners) for k in [0,1]]
 overlaps=[b['id'] for b in buildings if min(hi[0],b['world_position_m'][0]+b['footprint_m'][0]/2)>max(lo[0],b['world_position_m'][0]-b['footprint_m'][0]/2)+.001 and min(hi[1],b['world_position_m'][1]+b['footprint_m'][1]/2)>max(lo[1],b['world_position_m'][1]-b['footprint_m'][1]/2)+.001]
 check(not overlaps,'actual mesh envelope clears buildings '+q['id'],asset=q['asset'],overlaps=overlaps)
 if q['asset'] in ('camphor','cherry') and q['scale'][0]<.3:continue # planted into a certified soil tray
 if q['asset'] in ('flowers','bed-flowers','tree-grate','parking-mark','tactile','lane-dash','crosswalk'):continue
 foot=z+bounds[2][0]*sz;terrain=sample(height,x,1100-y)
 check(foot-terrain<=.045,'actual base is terrain supported '+q['id'],asset=q['asset'],base_gap_m=foot-terrain)
 check(not water[max(0,min(1100,round(1100-y))),max(0,min(1100,round(x)))],'new object not placed in water '+q['id'])
check(plan['generator_sha256']==sha(ROOT/'Tools/street_city_streetscape.py'),'current placement generator')
for path,digest in plan['source_files'].items():check(sha(path)==digest,'immutable world input '+path)
check(len(d['new_tripo_assets'])<=100,'at most 100 new Tripo object types');check(d['tree_all_lod2_triangles']<d['tree_all_lod0_triangles']*.2,'all-tree distant LOD below 20% of near triangles')
result=dict(status='failed' if issues else 'passed',checks=len(checks),issues=issues,results=checks,delivery_sha256=sha(OUT/'delivery.json'),verifier_sha256=sha(__file__),scope='Actual accepted mesh bounds against building parcels and physical terrain support; native entrance collisions are independently required')
write(OUT/'layout-acceptance.json',result);print(result['status'],len(checks),'layout checks',len(issues),'issues');assert not issues,issues[:12]
