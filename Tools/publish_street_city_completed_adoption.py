"""Publish independently approved source and placement rows into the persistent world registry."""
import json
from street_city_remaining_contract import context,digest,OUT,IDS
from street_city_remesh_gate import sha
from street_city_scene_inventory import build

def read(p):return json.loads(p.read_text('utf8'))
def main():
 prepared=read(OUT/'native-prepared-remaining-all.json');outside=read(OUT/'outdoor/native-prepared.json');ground=read(OUT/'ground/native-prepared.json')
 assert all(q['status']=='passed' for q in [prepared,outside,ground]);rows=[]
 for q in prepared['rows']:
  c=context(q['building']);report=read(c['out']/'assembly-report.json');acc=read(c['out']/'acceptance.json');assert acc['status']=='passed' and report['contract_sha256']==digest(c);assert sha(q['source'])==q['source_sha256'];x,y,z=c['building']['world_position_m'];rows.append(dict(q,position_cm=[y*100,x*100,z*100],placement_contract_sha256=digest(c)))
 assert {q['building'] for q in rows}==set(IDS) and len([q for q in rows if q['kind']=='city_building'])==sum(len(context(b)['building']['floors']) for b in IDS)
 for q in outside['rows']:
  assert sha(q['source'])==q['source_sha256'];rows.append(q)
 for q in ground['rows']:assert sha(q['source'])==q['source_sha256']
 receipt=dict(status='passed',scope='approved current model sources and metric placements; native scene acceptance is separate',rows=rows,overrides=ground['rows'],building_programmes={b:digest(context(b)) for b in IDS},outdoor_acceptance_sha256=sha(OUT/'outdoor/acceptance.json'),ground_acceptance_sha256=sha(OUT/'ground/acceptance.json'))
 (OUT/'adopted-sources.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),'utf8');build()

if __name__=='__main__':main()
