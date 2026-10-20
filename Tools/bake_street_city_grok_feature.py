"""Host-Python image work; Blender deliberately has no Pillow dependency."""
import argparse,json
from pathlib import Path
from street_city_shop_wall_styles import profile
from street_city_grok_brand_materials import bake
p=argparse.ArgumentParser();p.add_argument('--building',required=True);p.add_argument('--output',required=True);p.add_argument('--placement',required=True);a=p.parse_args()
bake(a.building,Path(a.output),profile(a.building),json.loads(Path(a.placement).read_text('utf8')))
