"""Resume portable three-language derivatives while actual wall model builds finish."""
import json,time
from street_city_world_wall_art import OUT,sha
from build_street_city_language_models import convert,read,geometry_digest
def main():
 start=time.monotonic();folder=OUT/'models';folder.mkdir(exist_ok=True)
 while True:
  models=[]
  for path in (OUT/'buildings').glob('*/model.json'):
   r=json.loads(path.read_text('utf8'));bid=r['building'];existing=folder/(bid+'.json');row=None
   if existing.exists():
    candidate=json.loads(existing.read_text('utf8'))
    if candidate['source_sha256']==sha(r['model']) and sha(candidate['model'])==candidate['model_sha256']:row=candidate
   if row is None:row=convert((bid,r['model']),folder)
   old,oldbin=read(r['model']);new,newbin=read(row['model']);assert geometry_digest(old,oldbin)==geometry_digest(new,newbin);models.append(row)
  status='passed' if len(models)==122 else 'in_progress';(OUT/'portable-localization.json').write_text(json.dumps(dict(status=status,buildings=len(models),models=models,scope='Portable language variants on revised wall models; native geometry source assets remain unchanged'),ensure_ascii=False,indent=2),'utf8')
  if status=='passed':return
  assert time.monotonic()-start<7200,'Wall model build did not complete in two hours'
  time.sleep(15)
if __name__=='__main__':main()
