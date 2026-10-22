"""Seated centre-target sightline check; explicit avatar assumptions, not a code certification."""
import json
from street_city_functional_revision import OUT,read,sha
def main():
 a=read(OUT/'adoption.json');checks=[]
 for row in a['buildings']:
  r=read(row['receipt'])
  for room in r['classrooms']:
   if not room['tiered']:continue
   display=next(q for q in r['additions'] if q['owner']==room['room'] and q['kind']=='teaching_display')
   targets=[('exposed blackboard wing' if p['finish']=='board' else 'projection fabric centre',p['pos'][1],p['pos'][2]+p['size'][2]/2) for p in display['parts'] if p['finish'] in ('board','screen')]
   rows=[dict(y=2.4-.35,tier=0),dict(y=.2-.35,tier=.24),dict(y=-2-.35,tier=.48)]
   for index,seat in enumerate(rows):
    for title,y,z in targets:
     for front in rows[:index]:
      t=(front['y']-seat['y'])/(y-seat['y']);line=seat['tier']+1.15+(z-seat['tier']-1.15)*t;clearance=line-(front['tier']+1.35);checks.append(dict(room=room['room'],target=title,eye_row=index,front_y=front['y'],clearance_m=clearance,passed=clearance>=.05))
 issues=[q for q in checks if not q['passed']];result=dict(status='failed' if issues else 'passed',checks=len(checks),minimum_clearance_m=min(q['clearance_m'] for q in checks),seated_eye_height_above_tier_m=1.15,seated_head_top_above_tier_m=1.35,adoption_sha256=sha(OUT/'adoption.json'),scope='Vertical clearance to the exposed blackboard wing at mid-height and projection fabric centre above nearer row heads under stated seated avatar assumptions. The lowered screen intentionally occludes the board centre. Does not claim every edge or every avatar size.',issues=issues,results=checks)
 (OUT/'sightline-acceptance.json').write_text(json.dumps(result,indent=2),'utf8');assert not issues;print('LECTURE CENTRE SIGHTLINES',len(checks),result['minimum_clearance_m'],flush=True)
if __name__=='__main__':main()
