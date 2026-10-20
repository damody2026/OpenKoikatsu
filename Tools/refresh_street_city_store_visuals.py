"""One resumable entry: Grok images, actual image reading, native SVG, acceptance."""
import os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
os.environ['PYTHONIOENCODING']='utf-8'
for script,args in (
 ('build_street_city_store_review.py',[]),
 ('generate_street_city_store_concepts.py',['--workers','4','--defer-gallery']),
 ('derive_street_city_image_designs.py',['--workers','4']),
 ('build_street_city_image_svgs.py',[]),
 ('verify_street_city_image_svgs.py',[])):
 subprocess.run([sys.executable,str(ROOT/'Tools'/script),*args],cwd=ROOT,check=True)
