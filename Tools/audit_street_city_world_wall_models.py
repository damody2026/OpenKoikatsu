"""Independent GLB count, surface-moment, metric-bounds and image-budget acceptance."""
import json,time,hashlib,io
import numpy as np
from PIL import Image
from street_city_world_wall_art import OUT,sha
from build_street_city_language_models import read
SCRIPT=sha(__file__);IMAGES=set()
def stats(path):
 doc,raw=read(path);arrays={};triangles=0;lo=np.full(3,np.inf);hi=-lo;area=0.;moment=np.zeros(3)
 def acc(i):
  if i in arrays:return arrays[i]
  a=doc['accessors'][i];v=doc['bufferViews'][a['bufferView']];dt={5126:'<f4',5125:'<u4',5123:'<u2',5121:'u1'}[a['componentType']];width={'VEC3':3,'VEC2':2,'SCALAR':1,'VEC4':4}[a['type']];item=np.dtype(dt).itemsize;offset=v.get('byteOffset',0)+a.get('byteOffset',0);stride=v.get('byteStride',item*width);result=np.ndarray((a['count'],width),dtype=dt,buffer=raw,offset=offset,strides=(stride,item));arrays[i]=result;return result
 def node(i,parent):
  nonlocal triangles,lo,hi,area,moment
  q=doc['nodes'][i]
  if 'matrix' in q:m=np.array(q['matrix']).reshape((4,4),order='F')
  else:
   x,y,z,w=q.get('rotation',[0,0,0,1]);r=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]);m=np.eye(4);m[:3,:3]=r@np.diag(q.get('scale',[1,1,1]));m[:3,3]=q.get('translation',[0,0,0])
  m=parent@m
  if 'mesh' in q:
   for p in doc['meshes'][q['mesh']]['primitives']:
    assert p.get('mode',4)==4;v=acc(p['attributes']['POSITION']).astype(float)@m[:3,:3].T+m[:3,3];lo=np.minimum(lo,v.min(0));hi=np.maximum(hi,v.max(0));ix=acc(p['indices']).reshape(-1) if 'indices' in p else np.arange(len(v));assert len(ix)%3==0;triangles+=len(ix)//3;pts=v[ix.reshape(-1,3)];a=np.linalg.norm(np.cross(pts[:,1]-pts[:,0],pts[:,2]-pts[:,0]),axis=1)*.5;area+=a.sum();moment+=(pts.mean(1)*a[:,None]).sum(0)
  for child in q.get('children',[]):node(child,m)
 for i in doc['scenes'][doc.get('scene',0)]['nodes']:node(i,np.eye(4))
 for image in doc.get('images',[]):
  view=doc['bufferViews'][image['bufferView']];start=view.get('byteOffset',0);data=raw[start:start+view['byteLength']];digest=hashlib.sha256(data).hexdigest()
  if digest not in IMAGES:
   im=Image.open(io.BytesIO(data));assert max(im.size)<=2048,(path,image.get('name'),im.size);im.verify();IMAGES.add(digest)
 return dict(triangles=triangles,bounds=[lo.tolist(),hi.tolist()],area=float(area),area_centroid=(moment/area).tolist())
def main():
 folder=OUT/'geometry-audits';folder.mkdir(exist_ok=True);start=time.monotonic()
 while True:
  rows=[]
  for path in (OUT/'buildings').glob('*/model.json'):
   r=json.loads(path.read_text('utf8'));bid=r['building'];dest=folder/(bid+'.json');identity=hashlib.sha256((SCRIPT+sha(r['source'])+sha(r['model'])).encode()).hexdigest()
   if dest.exists():
    cached=json.loads(dest.read_text('utf8'))
    if cached.get('identity')==identity and cached['status']=='passed':rows.append(cached);continue
   old=stats(r['source']);new=stats(r['model']);assert old['triangles']==new['triangles']==r['triangles'],bid;assert np.max(np.abs(np.array(old['bounds'])-new['bounds']))<=.0001,(bid,'bounds');assert abs(old['area']-new['area'])<=max(.0001,old['area']*.000001),(bid,'surface area');assert np.max(np.abs(np.array(old['area_centroid'])-new['area_centroid']))<=.0001,(bid,'surface moment');assert r['geometry_before']==r['geometry_after']
   row=dict(status='passed',building=bid,identity=identity,source_sha256=sha(r['source']),model_sha256=sha(r['model']),triangles=new['triangles'],maximum_metric_bounds_difference_m=float(np.max(np.abs(np.array(old['bounds'])-new['bounds']))),source_surface_area_m2=old['area'],new_surface_area_m2=new['area'],maximum_image_dimension=2048,verifier_sha256=SCRIPT);dest.write_text(json.dumps(row,indent=2),'utf8');rows.append(row);print('Independent wall GLB geometry',bid,'passed',flush=True)
  report=dict(status='passed' if len(rows)==122 else 'in_progress',buildings=len(rows),results=rows,geometry='No mesh source changes; independent original/revised GLB world-space triangle counts, bounds within0.1mm, surface area and area-weighted centroid, all textures <=2K')
  (OUT/'model-acceptance.json').write_text(json.dumps(report,indent=2),'utf8')
  if len(rows)==122:return
  assert time.monotonic()-start<7200;time.sleep(15)
if __name__=='__main__':main()
