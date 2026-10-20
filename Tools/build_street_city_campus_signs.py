"""Exterior campus lettering: semantic textures on certified remeshed boards."""
import copy,json,struct,html,os
from pathlib import Path
from street_city_localization import ROOT,OUT,LOCALES,path_for,sha,text
from street_city_campus_contract import IDS,context
from street_city_remesh_gate import verify
from build_street_city_language_models import read,write,convert
DEST=OUT/'campus-signs'

def main():
 DEST.mkdir(parents=True,exist_ok=True)
 proof=verify(ROOT/'output/street-social-city-production/campus-v1/components/unit-panel/remesh')
 rows=[]
 for bid in IDS:
  c=context(bid);doc,binary=read(proof['path']);doc=copy.deepcopy(doc)
  # Reuse the exact certified box topology. UVs alone are replaced so each
  # outward face reads upright; the rear is concealed against the facade.
  prim=doc['meshes'][0]['primitives'][0];pa=doc['accessors'][prim['attributes']['POSITION']];pv=doc['bufferViews'][pa['bufferView']]
  ua=doc['accessors'][prim['attributes']['TEXCOORD_0']];uv=doc['bufferViews'][ua['bufferView']];binary=bytearray(binary)
  for i in range(pa['count']):
   x,y,z=struct.unpack_from('<fff',binary,pv.get('byteOffset',0)+pa.get('byteOffset',0)+i*pv.get('byteStride',12))
   struct.pack_into('<ff',binary,uv.get('byteOffset',0)+ua.get('byteOffset',0)+i*uv.get('byteStride',8),x+.5,.5-y)
  binary=bytes(binary);image=path_for('building.'+bid,'zh-Hant').read_bytes();binary+=b'\0'*((-len(binary))%4)
  view=len(doc['bufferViews']);doc['bufferViews'].append(dict(buffer=0,byteOffset=len(binary),byteLength=len(image)));binary+=image
  doc['images']=[dict(name='sign-2k',bufferView=view,mimeType='image/png')];doc['textures']=[dict(source=0)]
  doc['materials']=[dict(name='Campus facade '+bid,extras=dict(city_text_id='building.'+bid),pbrMetallicRoughness=dict(baseColorTexture=dict(index=0),metallicFactor=.15,roughnessFactor=.6))]
  height=min(1.23,c['building']['floors'][0]['height_m']-2.95);assert height>=.35,(bid,'No clear facade band above entrance')
  width=height*2048/420
  doc['nodes'][0].update(name=bid+'-localized-facade-sign',translation=[0,height/2,0],scale=[width,height,.06])
  source=DEST/(bid+'-sign.glb');write(source,doc,binary);localized=convert((bid,source),DEST)
  positions=[]
  for e in c['building']['entrances']:
   side=e['facade_side'];nx,ny={'W':(-1,0),'E':(1,0),'N':(0,1),'S':(0,-1)}[side]
   x,y,_=e['position_m'];bottom=c['building']['floors'][0]['height_m']-.15-height
   assert bottom>=2.75
   positions.append(dict(id=e['id']+'-sign',position_m=[x+nx*.08,y+ny*.08,bottom],yaw_deg={'S':0,'N':180,'W':-90,'E':90}[side],entrance=e['id']))
  rows.append(dict(building=bid,key='building.'+bid,size_m=[width,.06,height],placements=positions,variants={l:dict(path=str(path_for('building.'+bid,l)),sha256=sha(path_for('building.'+bid,l)),text=text('building.'+bid,l)) for l in LOCALES},portable=localized,remesh=proof))
 report=dict(status='passed',buildings=rows,building_count=len(rows),sign_count=sum(len(r['placements']) for r in rows),locales=list(LOCALES),parameter='LanguageIndex',geometry_route='reused certified remeshed panel; metric scale and UV only')
 (DEST/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8')
 cards=[]
 for r in rows:
  imgs=''.join('<figure><figcaption>'+l+' · '+html.escape(v['text'])+'</figcaption><img src="'+Path(os.path.relpath(v['path'],DEST)).as_posix()+'?v='+v['sha256'][:12]+'"></figure>' for l,v in r['variants'].items())
  cards.append('<article><h2>'+r['building']+'</h2>'+imgs+'</article>')
 (DEST/'index.html').write_text('<!doctype html><meta charset="utf-8"><title>校園館名三語招牌</title><style>body{background:#192a30;color:#eee;font:18px sans-serif;margin:30px}article{border-bottom:1px solid #789;padding:20px}figure{display:inline-block;width:30%;margin:1%}img{width:100%}</style><h1>17 棟校園 · 34 面入口館名招牌</h1><p>同一招牌切換繁體、簡體、英文。沿用已通過 remesh 的板件；不改變建築尺寸與出入口。</p>'+''.join(cards),'utf8')

if __name__=='__main__':main()
