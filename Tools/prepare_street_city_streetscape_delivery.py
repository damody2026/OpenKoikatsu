import math,json
from collections import Counter
from street_city_streetscape import ROOT,BASE,OUT,read,write,sha
a=read(OUT/'asset-acceptance.json');assert a['status']=='passed';plan=read(OUT/'plan.json');legacy=read(OUT/'legacy-trees.json');assets={q['id']:q for q in a['assets']};instances=plan['instances']+legacy['instances'];groups={}
for tree in list(instances):
 if tree['asset'] in ('camphor','cherry'):instances.append(dict(tree,id=tree['id']+'-trunk',asset='tree-trunk-collision'))
for q in instances:
 x,y,z=q['position_m'];aid=q['asset'];gx,gy=math.floor(x/125),math.floor(y/125);label=f'Street_{aid.replace("-","_")}_{gx:02d}_{gy:02d}';group=groups.setdefault(label,dict(label=label,asset=aid,origin_m=[gx*125,gy*125,0],instances=[]));group['instances'].append(q)
for q in groups.values():
 aid=q['asset'];q['collision']='NoCollision' if aid in ('camphor','cherry','flowers','bed-flowers','tree-grate','drain','parking-mark','tactile','lane-dash','crosswalk') else 'BlockAll';q['visible']=aid!='tree-trunk-collision';q['cull_end_m']=650 if aid in ('camphor','cherry') else 220 if aid.startswith('lamp') else 100 if aid in ('flowers','bed-flowers','bicycle','scooter') else 180;q['cull_start_m']=int(q['cull_end_m']*.8)
counts=Counter(q['asset'] for q in instances);total=sum(assets[q['asset']]['lods'][0]['triangles'] for q in instances if q['asset']!='tree-trunk-collision');trees=sum(counts[k] for k in ('camphor','cherry'))
report=dict(status='accepted_assets_requires_native',source_files={str(p):sha(p) for p in [OUT/'plan.json',OUT/'asset-acceptance.json',OUT/'legacy-trees.json']},assets=list(assets.values()),groups=list(groups.values()),counts=dict(counts),visible_instances=sum(len(g['instances']) for g in groups.values() if g['visible']),tree_instances=trees,regional_trees_reused=448,facility_trees_reused=len(legacy['instances'])-448,legacy_replacements=legacy['clusters'],facility_sources=a['facility_sources'],all_visible_at_lod0_triangles=total,tree_all_lod0_triangles=sum(assets[k]['lods'][0]['triangles']*counts[k] for k in ['camphor','cherry']),tree_all_lod2_triangles=sum(assets[k]['lods'][2]['triangles']*counts[k] for k in ['camphor','cherry']),new_tripo_assets=a['new_tripo_assets'],generation_limit=100,reference=plan['reference'])
assert len(a['new_tripo_assets'])<=100;write(OUT/'delivery.json',report);print('DELIVERY',len(groups),'instance clusters;',trees,'trees;',total,'global worst-case triangles, before distance culling')
