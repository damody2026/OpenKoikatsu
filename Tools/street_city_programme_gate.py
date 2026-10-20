"""A passed older model must not be relabelled as a changed current layout."""
import math,re
from street_city_retail_profiles import placements

def verify_current(C,report):
 from street_city_shop_wall_styles import verify_metadata
 verify_metadata(C['id'],report)
 if C['id'] in ('S01','S02') or not C['building'].get('small_shop_design'):return
 expected=placements(C)[0];actual={q['name']:q for q in report['instances']}
 names={f'F{q["floor"]:02d}-{q["name"]}' for q in expected}
 recorded={n for n in actual if re.match(r'F\d\d-(equipment-|residence-shower)',n)}
 assert names==recorded,(C['id'],'equipment version differs from current layout')
 for q in expected:
  name=f'F{q["floor"]:02d}-{q["name"]}';item=actual[name]
  assert item['asset']==q['asset'],(C['id'],name,'asset changed')
  assert all(abs(x-y)<1e-6 for x,y in zip(item['position_m'],q['pos'])),(C['id'],name,'position changed')
  assert all(abs(x-y)<1e-6 for x,y in zip(item['scale'],q['scale'])),(C['id'],name,'size changed')
  assert abs(item['yaw_rad']-math.radians(q['yaw']))<1e-6,(C['id'],name,'rotation changed')
