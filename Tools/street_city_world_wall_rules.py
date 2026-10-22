"""Shared Blender and Unreal semantic assignment. Text/logos are never overwritten."""
import json,re
from street_city_world_wall_art import ROOT,OUT,CITY,CAMPUS
from street_city_shop_wall_styles import GROUPS
def family(bid):return next(k for k,v in GROUPS.items() if int(bid[1:]) in v)
def identity(label):
 if label.startswith('City_') or label.startswith('Campus_'):return label.split('_')[1]
 if label.startswith('Mall_'):return 'M1'
 return label.split('_')[0]
def floor_no(label):return int(label.rsplit('_F',1)[1])
def target(bid,name,no=1):
 n=name.lower().replace('_',' ')
 if any(t in n for t in ['identity','sign','print','brand','feature','tripo material']):return None
 if bid in CITY:
  if n.startswith('campus paint') or 'upper washable plaster' in n or 'warm washable paint' in n:return 'interior'
  if any(t in n for t in ['honed limestone','ivory rainscreen','charcoal stone plinth']):return 'facade'
  if 'sanitary porcelain' in n:return 'wet'
 elif bid in CAMPUS:
  if n.startswith('campus paint') or 'warm washable paint' in n:return 'interior'
  if bid=='SP' and n=='campus tile':return 'wet'
 elif re.fullmatch(r'S\d+',bid):
  if ('wall facade' in n or 'wall quiet' in n):return 'facade'
 elif bid=='M1':
  if any(t in n for t in ['upper washable plaster','honed limestone','charcoal stone plinth']):return 'interior'
  if 'sanitary porcelain' in n:return 'wet'
 return None
def maps_for(bid,no,role):
 if bid in CITY:
  inside='wet' if bid=='H1' and no==2 else 'lobby' if no==1 or bid=='CV' and no==3 else 'quiet'
  if role=='wet':inside='wet'
  return 'city/'+bid+'/'+inside,'city/'+bid+'/facade'
 if bid in CAMPUS:return 'campus/'+bid+'/interior','campus/'+bid+'/facade'
 if bid=='M1':
  inside='wet' if role=='wet' else 'restaurant' if no>=8 else 'gallery' if no in (2,3,4) else 'public'
  return 'mall/M1/'+inside,'mall/M1/public'
 key='shops/'+family(bid)+'/facade';return key,key
def bounds():
 rows=json.loads((ROOT/'Docs/StreetSocialCity/production/building-floor-audit.json').read_text('utf8'))['buildings'];return {q['id']:q['footprint_m'] for q in rows}
def read_materials():
 rows=json.loads((OUT/'materials.json').read_text('utf8'))['images'];review=json.loads((OUT/'visual-material-review.json').read_text('utf8'));assert review['status']=='passed'
 for q in rows:assert review['approved_image_sha256'].get(q['id'])==q['grok_image_sha256'],q['id']+' changed after visual review'
 return {q['id']:q for q in rows}
