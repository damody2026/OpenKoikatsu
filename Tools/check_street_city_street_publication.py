from html.parser import HTMLParser
from urllib.parse import urlparse,unquote
from urllib.request import url2pathname
from pathlib import Path
from street_city_streetscape import ROOT,BASE,OUT,read,write,sha
class Links(HTMLParser):
 def __init__(self):super().__init__();self.paths=[]
 def handle_starttag(self,tag,attrs):
  for key,value in attrs:
   if key in ('href','src') and value:
    url=urlparse(value)
    if url.scheme in ('http','https'):continue
    self.paths.append(Path(url2pathname(url.path)) if url.scheme=='file' else OUT/unquote(url.path))
parser=Links();parser.feed((OUT/'index.html').read_text('utf8'));missing=[str(p) for p in parser.paths if not p.exists()];assert not missing,missing
a=read(OUT/'acceptance.json');assert a['status']=='passed' and a['map_sha256']==sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')
assert a['native_acceptance_sha256']==sha(OUT/'native-acceptance.json') and a['native_render_sha256']==sha(OUT/'native-render.json')
assert a['new_tripo_assets']<=100 and a['tree_lod_triangles']==[1992,912,336]
write(OUT/'publication-acceptance.json',dict(status='passed',local_links_checked=len(parser.paths),missing=missing,map_sha256=a['map_sha256'],page_sha256=sha(OUT/'index.html'),acceptance_sha256=sha(OUT/'acceptance.json')))
print('PASSED current map, receipts and',len(parser.paths),'local publication links')
