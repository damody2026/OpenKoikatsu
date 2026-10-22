"""Purpose-specific board furniture; curved beds and sanitaryware use approved assets."""
from pathlib import Path
from street_city_campus_recipes import recipe as common,p,desk,chair,shift,cabinet
from street_city_campus_sports import recipe as sports
ROOT=Path(__file__).resolve().parents[1]/'output/street-social-city-production'

def component_folder(aid):
 if aid=='lod-toilet-compact':return ROOT/'world-completion-v1/components/toilet/remesh-lod'
 if aid=='tripo-basin-economy':return ROOT/'world-completion-v1/components/basin/remesh-lod'
 if aid=='tripo-fish':return ROOT/'world-completion-v1/components/fish/remesh'
 if aid=='product-bed':return ROOT/'world-completion-v1/components/bed/remesh-lod'
 if aid.startswith('product-'):
  from street_city_mall import component_folders
  return component_folders()[aid]
 return ROOT/'campus-v1/components'/aid/'remesh'

def recipe(aid,size):
 key=aid.removeprefix('COMMON-CITY-');w,d,h=size
 if key=='desk':return desk(w,d,.75)
 if key=='lounge':return sports('池畔渡假躺椅',size)
 if key=='treadmill':return sports('跑步機',size)
 if key=='weights':return sports('重量訓練站',size)
 if key in ('tub','spa_basin'):
  # Deliberately rectangular built-in stone bath, hollow interior and separate water.
  t=.12
  return [p((w,d,t),(0,0,0),'tile')]+[p((w,t,h-t),(0,y,t),'tile') for y in [-d/2+t/2,d/2-t/2]]+[p((t,d-2*t,h-t),(x,0,t),'tile') for x in [-w/2+t/2,w/2-t/2]]+[p((w-2*t,d-2*t,.008),(0,0,h-.2),'water')]+[p((.03,.03,.45),(w/2-.2,d/2-.16,h),'steel'),p((.03,.3,.03),(w/2-.2,d/2-.3,h+.4),'steel')]
 if key=='kitchen':return common('COMMON-CITY-cookline',size)
 if key in ('parcel','secure'):return cabinet(w,d,h)
 if key=='stage':return [p((w,d,h),(0,0,0),'wood')]
 if key=='tank':
  return [p((w,d,.35),(0,0,0),'black'),p((w,d,.1),(0,0,h-.1),'black')]+[p((w,.04,h-.45),(0,y,.35),'glass') for y in [-d/2+.02,d/2-.02]]+[p((.04,d,h-.45),(x,0,.35),'glass') for x in [-w/2+.02,w/2-.02]]+[p((w-.1,d-.1,.008),(0,0,h-.25),'water')]+[p((.05,.05,h),(x,y,0),'steel') for x in [-w/2+.025,w/2-.025] for y in [-d/2+.025,d/2-.025]]
 return common(aid,size)
