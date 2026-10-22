import subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
for script in ['run_street_city_streetscape_render.py','run_street_city_scene_integration.py','finish_street_city_streetscape.py']:
 subprocess.run([sys.executable,str(root/'Tools'/script)],cwd=root,check=True)
