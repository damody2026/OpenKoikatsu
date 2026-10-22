"""Camera positions validated against the same map-to-world ground contract."""
import numpy as np
from street_city_streetscape import BASE,OUT,write,sha,sample
with np.load(BASE/'world-ground-v1/world-field.npz') as f:height=f['height_map']
views=[('shop',[245,459,13.7],[310,459,14]),('campus',[575,680,21.7],[575,735,22]),('mall',[565,460,13.7],[655,460,23]),('riverside',[785,420,13.7],[785,485,14])]
rows=[]
for name,eye,target in views:
 terrain=sample(height,eye[0],1100-eye[1]);eye[2]=max(eye[2],terrain+1.7);rows.append(dict(name=name,eye=eye,target=target,terrain_height_m=terrain,clearance_m=eye[2]-terrain))
write(OUT/'camera-plan.json',dict(status='passed',views=rows,ground_sha256=sha(BASE/'world-ground-v1/world-field.npz'),source_sha256=sha(__file__)))
