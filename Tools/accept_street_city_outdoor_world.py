"""Check the final exported scene rather than assuming source transforms survived."""
import bpy,sys,json
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_outdoor_contract import OUT
from street_city_remesh_gate import sha
a=json.loads((OUT/'acceptance.json').read_text('utf8'));assert a['status']=='passed';bpy.ops.wm.read_factory_settings(use_empty=True);model=OUT.parent/'campus-complete-2k.glb';bpy.ops.import_scene.gltf(filepath=str(model));bpy.context.view_layer.update();results=[]
for q in a['assets']:
 objects=[o for o in bpy.context.scene.objects if o.type=='MESH' and o.get('outdoor_id')==q['id']];n=0
 for o in objects:o.data.calc_loop_triangles();n+=len(o.data.loop_triangles)
 results.append(dict(name=q['id']+' actual exported triangle inventory',passed=n==q['actual_triangles'],triangles=n))
 for o in objects:
  root=o
  while root.parent:root=root.parent
  error=max(abs(root.matrix_world.translation[k]-q['origin'][k]) for k in range(3));results.append(dict(name=q['id']+' actual root position '+o.name,passed=error<.001,error_m=error))
result=dict(status='passed' if all(q['passed'] for q in results) else 'failed',checks=len(results),results=results,issues=[q for q in results if not q['passed']],model_sha256=sha(model),source_sha256=sha(Path(__file__)))
(OUT/'world-acceptance.json').write_text(json.dumps(result,indent=2),'utf8');print(result['status'],len(results),result['issues'][:10]);assert not result['issues']
