"""Bind campus delivery to current source, rendered, remeshed and native files."""
import hashlib,json,struct,sys
from datetime import datetime
from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import unquote,urlsplit
from xml.etree import ElementTree as ET
from street_city_campus_contract import IDS,OUT,context,digest
from street_city_remesh_gate import verify
from street_city_unreal_snapshot import verify as verify_shop_snapshot
from street_city_atomic import write_text

ROOT=Path('C:/portable/OpenKoikatsu');GROUND=OUT.parent/'world-ground-v1'
checks=[];issues=[];pinned={}
def sha(path):
 path=Path(path);value=hashlib.sha256(path.read_bytes()).hexdigest();pinned[str(path)]=value;return value
def read(path):
 path=Path(path);sha(path);return json.loads(path.read_text('utf-8-sig'))
def check(ok,name,**details):
 row=dict(name=name,passed=bool(ok),**details);checks.append(row)
 if not ok:issues.append(row)
def same(path,value,name):check(sha(path)==value,name)
def passed(data,name):
 check(data.get('status')=='passed' and not data.get('errors') and not data.get('issues'),name)
 check(all(q.get('passed') for q in data.get('results',[])),name+' individual results')
def glb(path):
 data=Path(path).read_bytes();check(len(data)>=20 and data[:4]==b'glTF' and struct.unpack_from('<I',data,4)[0]==2 and struct.unpack_from('<I',data,8)[0]==len(data),str(path)+' complete GLB');sha(path)
class Links(HTMLParser):
 def __init__(self):super().__init__();self.links=[]
 def handle_starttag(self,tag,attrs):
  for key,value in attrs:
   if key in ('href','src') and value:self.links.append(value)

def main():
 write_text(OUT/'delivery-acceptance.json',json.dumps(dict(status='in_progress',started_at=datetime.now().astimezone().isoformat()),indent=2))
 from publish_street_city_campus import publish
 publish(require_all=True)
 if (OUT/'outdoor/acceptance.json').exists():
  from publish_street_city_outdoor import publish as publish_outdoor
  publish_outdoor()
 total=0;largest=0;dry_triangles=0
 for bid in IDS:
  c=context(bid);folder=c['out'];r=read(folder/'assembly-report.json');a=read(folder/'acceptance.json')
  passed(a,bid+' actual model acceptance');passed(r,bid+' approved assembly');check(r.get('approved'),bid+' approved flag')
  check(r['contract_sha256']==digest(c),bid+' exact current effective contract')
  same(folder/'acceptance.json',r['acceptance_sha256'],bid+' adopted acceptance version')
  same(ROOT/'Tools/accept_street_city_campus.py',a['verifier_source_sha256'],bid+' actual verifier version')
  same(folder/f'{bid}-complete-2k.glb',a['sha256'],bid+' whole-building model version');glb(folder/f'{bid}-complete-2k.glb');sha(folder/f'{bid}-complete-2k.blend')
  check(r['footprint_m']==c['building']['footprint_m'],bid+' exterior dimensions')
  for path,value in r['recipe_sources'].items():
   same(path,value,bid+' historical executed source '+Path(path).name)
   if Path(path).name!='street_city_campus_contract.py':same(ROOT/'Tools'/Path(path).name,value,bid+' current geometry generator '+Path(path).name)
  for aid,source in r['sources'].items():
   receipt=verify(Path(source['remesh_report']).parent,source['sha256']);check(receipt['tool_sha256']==source['tool_sha256'],bid+' Rust version '+aid);sha(source['remesh_report']);sha(receipt['path'])
  view=read(folder/'render-receipt.json');check(view['source_sha256']==a['sha256'],bid+' current model rendered');same(ROOT/'Tools/render_street_city_campus.py',view['renderer_sha256'],bid+' renderer version')
  bounds=view['interior_framing_ndc'];check(len(bounds)==4 and min(bounds)>=.019 and max(bounds)<=.981,bid+' complete interior framing')
  for name,value in view['images'].items():same(folder/name,value,bid+' actual rendered image '+name)
  programme=read(folder/'room-programme.json');check(programme['building']==bid,bid+' current room programme');structure=read(folder/'structure-plan.json')
  for f in c['building']['floors']:
   no=f['floor'];total+=1;n=a['floor_triangles'][str(no)];check(n==r['floor_triangles'][str(no)] and n+r['water_triangles'][str(no)]<=100000,bid+f' F{no:02d} complete floor face cap');largest=max(largest,n+r['water_triangles'][str(no)]);dry_triangles+=n
   glb(folder/f'{bid}-F{no:02d}-complete-2k.glb')
   path=folder/f'{bid}-F{no:02d}-plan.svg';root=ET.parse(path).getroot();sha(path);texts=[''.join(x.itertext()) for x in root.iter() if x.tag.endswith('text')];joined=' '.join(texts);W,D=c['building']['footprint_m']
   check(f'{W:g} × {D:g} m' in joined,bid+f' F{no:02d} exact exterior dimensions on plan')
   wall_count=sum(q['kind']=='wall' and q['pos'][2]<=f['z_m']+1<q['pos'][2]+q['size'][2] for q in structure['parts']);check(sum(x.tag.endswith('rect') and x.get('fill')=='#47535b' for x in root.iter())==wall_count,bid+f' F{no:02d} all actual cross-section walls drawn including double-height walls')
   floor_count=sum(q['kind']=='floor' and abs(q['pos'][2]+q['size'][2]-f['z_m'])<.001 for q in structure['parts']);check(sum(x.tag.endswith('rect') and x.get('fill')=='#f6f1e7' for x in root.iter())==floor_count,bid+f' F{no:02d} actual slab panels with courtyard and atrium voids')
   check(all(q['id'] in joined and q['name'] in joined for q in programme['rooms'] if q['floor']==no),bid+f' F{no:02d} all main room labels')
   for length in [1,5,10]:check(f'{length} m' in texts,bid+f' F{no:02d} {length}m scale')
   for core in f['vertical_circulation']:
    label={'stairs':'樓梯','lift':'電梯','restroom':'公共廁所','lobby':'梯廳'}.get(core['kind'],core['kind'])+str(core.get('core',''));check(label in joined,bid+f' F{no:02d} labelled '+label)
  for d in r['doors']:check(abs(d['width_m']-(2.4 if d['width_m']>1.5 else 1.2))<1e-6,bid+' declared door width '+d['id'])
 check(len(IDS)==17 and total==89,'complete school inventory')
 gallery=OUT/'index.html';parser=Links();parser.feed(gallery.read_text('utf8'));sha(gallery)
 for link in parser.links:
  url=urlsplit(link)
  if not url.scheme and not url.netloc and url.path:check((gallery.parent/unquote(url.path)).is_file(),'gallery local link '+link)
 for name in ['RESEARCH.md','RESEARCH-SUPPLEMENT.md','OFFICES.md','department-offices.json','research.json','tripo-generation-log.json']:sha(OUT/name)
 for path in [ROOT/'model3d.md',ROOT/'Docs/StreetSocialCity/ERRORS_AND_PREVENTION.md',ROOT/'StreetSocialCity/OpenCity.cmd',ROOT/'StreetSocialCity/StreetSocialCity.uproject']:sha(path)
 paving=read(OUT/'paving-union-checks.json');passed(paving,'graded paving subtraction checks');same(ROOT/'Tools/street_city_paving_union.py',paving['source_sha256'],'paving algorithm proof version')
 paving_geometry=read(OUT/'terrain-paving-acceptance.json');passed(paving_geometry,'actual certified paving preflight');same(ROOT/'Tools/accept_street_city_paving_geometry.py',paving_geometry['source_sha256'],'paving geometry verifier')
 for row in paving_geometry['sources']:same(row['path'],row['sha256'],row['id']+' actual paving tested version')
 overlap=read(OUT/'ground-overlap-acceptance.json');passed(overlap,'observed paving overlap regression');same(OUT/'campus-complete-2k.blend',overlap['model_sha256'],'observed paving current model');same(ROOT/'Tools/diagnose_street_city_ground_overlap.py',overlap['source_sha256'],'paving regression verifier')
 canonical=read(ROOT/'Docs/StreetSocialCity/production/campus-programme.json');check(canonical['status']=='passed' and len(canonical['buildings'])==17,'canonical current campus programme');check(len(canonical['rooms'])==len({q['id'] for q in canonical['rooms']})==680 and all(q['name'] and q['interactions'] and all(v>0 for v in q['clear_size_m']) for q in canonical['rooms']),'all 680 main spaces have current purpose/interaction plans')
 foundations=read(GROUND/'campus-foundations.json');check({r['id'] for r in foundations['buildings']}==set(IDS),'all building foundations')
 for row in foundations['buildings']:
  b=context(row['id'])['building'];check(row['footprint_m']==b['footprint_m'] and row['world_position_m']==b['world_position_m'],row['id']+' ground foundation dimensions/position')
 g=read(GROUND/'ground-acceptance.json');passed(g,'actual complete remeshed ground');check(g['chunk_count']==16,'all 16 ground chunks')
 for row in g['chunks']:
  for kind in ['ground','water']:
   if row.get(kind):verify(Path(row[kind]['remesh_report']).parent,row[kind]['sha256']);sha(row[kind]['remesh_report']);same(row[kind]['path'],row[kind]['sha256'],row['id']+' current '+kind)
 w=read(OUT/'world-acceptance.json');passed(w,'actual combined campus acceptance');check(w['building_count']==17 and w['floor_count']==89,'combined campus inventory')
 for name,value in w['files'].items():same(OUT/name,value,'combined accepted '+name)
 world_view=read(OUT/'world-render-receipt.json');same(ROOT/'Tools/render_street_city_campus.py',world_view['renderer_sha256'],'complete campus renderer version');same(OUT/'campus-overview.png',world_view['image_sha256'],'actual complete campus rendered image');check(world_view['files']==w['files'],'complete image exact accepted scene version')
 wa=read(OUT/'world-assembly.json');check(len(wa['buildings'])==17 and len(wa['terrain'])==(8 if wa.get('outdoor') else 6),'complete campus assembly provenance')
 if wa.get('outdoor'):
  oa=read(OUT/'outdoor/acceptance.json');passed(oa,'actual outdoor remesh geometry');same(ROOT/'Tools/accept_street_city_outdoor.py',oa['source_sha256'],'outdoor verifier version');same(ROOT/'Tools/build_street_city_outdoor.py',oa['builder_sha256'],'outdoor geometry builder');same(OUT/'outdoor/programme.json',oa['contract_sha256'],'outdoor programme')
  for row in wa['outdoor']:same(row['source'],row['sha256'],'outdoor scene source '+row['id'])
  from street_city_outdoor_gate import verify_outdoor
  for row in oa['assets']:verify_outdoor(row['id'],row['remesh']['sha256'])
  ou=read(OUT/'outdoor/unreal-acceptance.json');passed(ou,'actual native outdoor collision');same(ROOT/'Tools/street_city_unreal_outdoor.py',ou['source_sha256'],'native outdoor verifier');same(OUT/'outdoor/acceptance.json',ou['acceptance_sha256'],'native current outdoor geometry')
  for row in ou['saved_assets']:same(row['path'],row['sha256'],'current native outdoor asset')
  ow=read(OUT/'outdoor/world-acceptance.json');passed(ow,'actual combined outdoor placement');same(OUT/'campus-complete-2k.glb',ow['model_sha256'],'current outdoor combined geometry');same(ROOT/'Tools/accept_street_city_outdoor_world.py',ow['source_sha256'],'outdoor world verifier')
  assembly=read(OUT/'outdoor/assembly.json');same(ROOT/'Tools/assemble_street_city_outdoor.py',assembly['assembler_sha256'],'outdoor assembler version');same(OUT/'campus-complete-2k.glb',assembly['model_sha256'],'outdoor rendered current model');same(OUT/'campus-overview.png',assembly['image_sha256'],'outdoor rendered actual overview')
  for name,value in assembly['images'].items():same(OUT/'outdoor'/name,value,'outdoor actual preview '+name)
  gallery=OUT/'outdoor/index.html';parser=Links();parser.feed(gallery.read_text('utf8'));sha(gallery);sha(OUT/'outdoor/RESEARCH.md')
  for name in ['street_city_outdoor_contract.py','street_city_outdoor_gate.py','street_city_rectangular_paths.py','seal_street_city_outdoor.py','run_street_city_outdoor.py','finish_street_city_outdoor.py','publish_street_city_outdoor.py']:sha(ROOT/'Tools'/name)
  for link in parser.links:
   url=urlsplit(link)
   if not url.scheme and not url.netloc and url.path:
    target=gallery.parent/unquote(url.path);check(target.is_file(),'outdoor gallery local link '+link)
    if target.is_file():sha(target)
  for row in oa['assets']:
   plan=OUT/'outdoor'/row['id']/'plan.svg';root=ET.parse(plan).getroot();sha(plan);texts=[''.join(q.itertext()) for q in root.iter() if q.tag.endswith('text')];joined=' '.join(texts)
   for n in [1,5,10]:check(f'{n}m' in texts,row['id']+' outdoor metric ruler '+str(n))
   if row['id']=='connection':check('175m' in joined and '4m' in joined,'connection overall dimensions')
   else:check(f'{row["size"][0]:g}×{row["size"][1]:g}m' in joined,row['id']+' actual dimensions on SVG')
  root=ET.parse(OUT/'outdoor/world-plan.svg').getroot();meta=next(q for q in root if q.get('id')=='world-metre-contract');check(__import__('json').loads(meta.text)['world_size_m']==[1380,1100],'outdoor world SVG extent')
 for row in wa['buildings']+wa['terrain']+wa.get('water',[]):same(row['source'],row['sha256'],'current combined source '+str(row.get('building',row.get('id'))))
 glb(OUT/'campus-complete-2k.glb');sha(OUT/'campus-overview.png')
 u=read(OUT/'unreal-acceptance.json');passed(u,'actual current Unreal campus collision');check(u['floors']==89,'all native campus floors')
 same(ROOT/'Tools/assemble_street_city_unreal_campus.py',u['source_sha256'],'actual native importer/verifier version');same(GROUND/'ground-acceptance.json',u['ground_acceptance_sha256'],'actual native accepted terrain version')
 for row in u['saved_assets']:same(row['path'],row['sha256'],'saved native campus asset '+Path(row['path']).name)
 imports=read(GROUND/'unreal-campus-import.json');passed(imports,'current native campus import');check(len(imports['floors'])==89,'native import floor inventory')
 for row in imports['floors']:same(row['source'],row['source_sha256'],'current native source '+row['id'])
 old=read(GROUND/'unreal-interioraudit.json');passed(old,'preserved shops actual collision');check(verify_shop_snapshot(ROOT,old['saved_assets'])==291,'preserved shops native snapshot')
 sha(ROOT/'Tools/run_street_city_unreal.lua')
 for stage in ['Campus','InteriorAuditCollision']:
  process=read(GROUND/('unreal-'+stage.lower()+'-process.json'));task=read(GROUND/('unreal-'+stage.lower()+'-task.json'));check(process['stage']==stage and process['exitCode']==0 and task['status']=='succeeded','actual native process and script succeeded '+stage)
 # Pin all saved native dependencies, including material/texture/terrain assets.
 native=ROOT/'StreetSocialCity/Content/UniversityCity';native_files=list(native.rglob('*.uasset'))+list(native.rglob('*.umap'))
 check(len(native_files)>400,'saved native dependency inventory')
 for path in native_files:sha(path)
 check(sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')==next(r['sha256'] for r in u['saved_assets'] if r['path'].endswith('.umap')),'campus and shop audit share current saved map')
 result=dict(status='failed' if issues else 'passed',completed_at=datetime.now().astimezone().isoformat(),building_count=17,floor_count=total,main_room_count=len(canonical['rooms']),dry_triangles=dry_triangles,maximum_floor_triangles_including_water=largest,checks=len(checks),issues=issues,results=checks,source_sha256=sha(Path(__file__)),files=pinned,scope='Static modular school and current saved Unreal environment; game controllers not connected; NTUST programme reference, not measured replica')
 if wa.get('outdoor'):result.update(outdoor_asset_count=len(oa['assets']),outdoor_triangles=sum(q['actual_triangles'] for q in oa['assets']),outdoor_native_checks=ou['checks'])
 write_text(OUT/'delivery-acceptance.json',json.dumps(result,ensure_ascii=False,indent=2));print(result['status'],len(checks),'checks',len(issues),'issues',flush=True)
 assert not issues,str(issues[:12])
if __name__=='__main__':
 try:main()
 except Exception as error:
  import traceback
  write_text(OUT/'delivery-acceptance.json',json.dumps(dict(status='failed',checks=len(checks),issues=issues,error=traceback.format_exc()),ensure_ascii=False,indent=2))
  raise
