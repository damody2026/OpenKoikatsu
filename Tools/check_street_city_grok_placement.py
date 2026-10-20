"""Fast real-geometry logo placement check before expensive export and rendering."""
import bpy,sys,json,argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from street_city_building_context import context
from street_city_large_shop_contract import context as large_context
from street_city_shop_wall_styles import profile,OUT as WALL,sha
from street_city_grok_brand_placement import select
p=argparse.ArgumentParser();p.add_argument('--building',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);bid=a.building
C=large_context(bid) if bid in ('S18','S19') else context(bid);source=WALL/'before'/bid/f'{bid}-complete-2k.blend';bpy.ops.wm.open_mainfile(filepath=str(source))
r=select(bpy.context.scene,C['building'],profile(bid));r.update(status='passed_source_geometry',building=bid,source_sha256=sha(source),selector_sha256=sha(ROOT/'Tools/street_city_grok_brand_placement.py'))
out=ROOT/'output/street-social-city-production/shop-brand-grok-v2/placements';out.mkdir(exist_ok=True);(out/(bid+'.json')).write_text(json.dumps(r,indent=2),'utf8');print(bid,'VISIBLE LOGO PATCH',r['logo_bounds_yz_m'],flush=True)
