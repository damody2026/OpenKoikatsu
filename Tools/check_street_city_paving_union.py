"""Exercise clipping at slopes, mixed-height corners and independent levels."""
import json,hashlib
from pathlib import Path
from street_city_paving_union import PavingUnion,area
ROOT=Path('C:/portable/OpenKoikatsu');results=[]
def square(x,y,slope=0,z=20):return [[a,b,z+slope*a] for a,b in [(x,y),(x+2,y),(x+2,y+2),(x,y+2)]]
def case(name,first,second,expected):
 u=PavingUnion();u.take(first);pieces=u.take(second);value=sum(area(p) for p in pieces);ok=abs(value-expected)<1e-7;results.append(dict(name=name,remaining_area_m2=value,expected_area_m2=expected,passed=ok));assert ok,results[-1]
for slope in [0,.08,-.2]:case('translated overlap at slope '+str(slope),square(550,735,slope),square(551,736,slope),3)
case('near coplanar independent coefficients',square(550,735),square(550,735,z=20.0001),0)
case('separate elevated road retained',square(550,735),square(550,735,z=23),4)
case('touching edge retained',square(0,0),square(2,0),4)
case('mixed-height quad flat triangle clipped',square(0,0),[[1,0,20],[3,0,20],[3,2,20],[1,2,20.1]],3.5)
source=ROOT/'Tools/street_city_paving_union.py';result=dict(status='passed',source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),cases=results)
(ROOT/'output/street-social-city-production/campus-v1/paving-union-checks.json').write_text(json.dumps(result,indent=2),'utf8');print(len(results),'paving cases passed')
