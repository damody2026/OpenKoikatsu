"""Finish current static scene validation, then emit real native review images."""
import sys,subprocess
from street_city_streetscape import ROOT,BASE,OUT,read,sha
from finish_street_city_scene_integration import native
def python(script):subprocess.run([sys.executable,str(ROOT/'Tools'/script)],cwd=ROOT,check=True)
scene=read(BASE/'scene-integration-v1/acceptance.json');assert scene['status']=='passed' and scene['map_sha256']==sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')
assert read(OUT/'layout-acceptance.json')['status']=='passed'
native('audit_street_city_remaining_native.py','remaining-collision',BASE/'world-completion-v1/native-collision-acceptance.json')
native('audit_street_city_functional_unreal.py','functional-interiors',BASE/'functional-interiors-v1/native-acceptance.json')
native('audit_street_city_streetscape_unreal.py','street-audit',OUT/'native-acceptance.json')
try:
 render=read(OUT/'native-render.json');current=render['status']=='rendered_requires_visual_review' and render['map_sha256']==scene['map_sha256'] and render['renderer_sha256']==sha(ROOT/'Tools/render_street_city_streetscape_unreal.py') and render['delivery_sha256']==sha(OUT/'delivery.json') and render['process_returncode']==0 and all(sha(q['image'])==q['sha256'] for q in render['images'])
except (OSError,ValueError,KeyError):current=False
if not current:python('run_street_city_streetscape_render.py')
python('finalize_street_city_world_walls.py');python('publish_street_city_functional_interiors.py')
print('READY FOR CURRENT NATIVE VISUAL REVIEW',flush=True)
