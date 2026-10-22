"""Technical periodicization and approximate companion maps, not AI PBR measurements."""
import json,numpy as np
from pathlib import Path
from PIL import Image,ImageFilter
from street_city_world_wall_art import OUT,catalogue,sha
from street_city_grok_brand_materials import periodic
def main():
 rows=[]
 for q in catalogue():
  folder=OUT/'assets'/q['id'];receipt=folder/'generation.json'
  if not receipt.exists():continue
  r=json.loads(receipt.read_text('utf8'))
  if r['status']!='generated_review_pending':continue
  source=folder/'concept.jpg';assert sha(source)==r['image_sha256'];assert sha(r['grok_original_image_file'])==r['grok_original_image_sha256'];
  from recover_street_city_store_concepts import completed
  proof=completed(folder,r['prompt_sha256']);assert proof and proof['source'].resolve()==Path(r['grok_original_image_file']).resolve()
  if not r.get('completion_evidence'):
   r.update(completion_evidence='newline_normalized_prompt_and_completed_image_tool_receipt',prompt_match_rule='Exact file SHA and CLI user query equal after CRLF normalization and trailing line-ending removal only',tool_event_log=str(proof['event_log']),tool_event_log_sha256=sha(proof['event_log']),tool_call_id=proof['tool_call_id']);receipt.write_text(json.dumps(r,ensure_ascii=False,indent=2),'utf8')
  dest=OUT/'materials'/q['id'];dest.mkdir(parents=True,exist_ok=True);meta=dest/'pbr.json'
  identity=sha(source)+sha(__file__)
  if meta.exists():
   old=json.loads(meta.read_text('utf8'))
   if old.get('identity')==identity and all(sha(v['path'])==v['sha256'] for v in old['maps'].values()):rows.append(old);continue
  with Image.open(source) as im:resolution=list(im.size);im=im.convert('RGB').resize((2048,2048),Image.Resampling.LANCZOS)
  dest=dest/'revisions'/identity[:16];dest.mkdir(parents=True,exist_ok=True)
  a=periodic(np.asarray(im,dtype=float),band=64).astype('uint8');Image.fromarray(a).save(dest/'basecolor-2k.png')
  # Height only suggests very fine surface texture. It is not a physical scan.
  gray=np.asarray(Image.fromarray(a).convert('L').filter(ImageFilter.GaussianBlur(1.5)),dtype=float)/255
  height=(gray-np.asarray(Image.fromarray(a).convert('L').filter(ImageFilter.GaussianBlur(12)),dtype=float)/255)*.00065
  dx=(np.roll(height,-1,1)-np.roll(height,1,1))/(4/2048);dy=(np.roll(height,-1,0)-np.roll(height,1,0))/(4/2048)
  n=np.stack([-dx,dy,np.ones_like(dx)],2);n/=np.linalg.norm(n,axis=2)[:,:,None];Image.fromarray(np.clip((n*.5+.5)*255,0,255).astype('uint8')).save(dest/'normal-2k.png')
  rough=np.clip(q['roughness']*255+(gray-gray.mean())*12,0,255).astype('uint8');rough=periodic(np.repeat(rough[:,:,None],3,2).astype(float),band=64).astype('uint8')[:,:,0];Image.fromarray(rough).save(dest/'roughness-2k.png')
  maps={k:dict(path=str(dest/(k+'-2k.png')),sha256=sha(dest/(k+'-2k.png'))) for k in ['basecolor','normal','roughness']}
  row=dict(**q,status='baked_requires_visual_review',identity=identity,grok_image_sha256=sha(source),original_resolution=resolution,output_resolution=[2048,2048],maps=maps,maximum_edge_difference=int(max(abs(a[0].astype(int)-a[-1].astype(int)).max(),abs(a[:,0].astype(int)-a[:,-1].astype(int)).max())),companions='Approximate technical maps derived from albedo; not independently generated or measured PBR')
  assert row['maximum_edge_difference']==0;meta.write_text(json.dumps(row,ensure_ascii=False,indent=2),'utf8');rows.append(row)
 (OUT/'materials.json').write_text(json.dumps(dict(status='ready_for_review' if len(rows)==90 else 'partial',images=rows),ensure_ascii=False,indent=2),'utf8');print('Baked',len(rows),flush=True)
if __name__=='__main__':main()
