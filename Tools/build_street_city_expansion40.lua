package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local base='output/street-social-city-production/city-destinations-v3'
assert(os.execute('if not exist "'..base..'" mkdir "'..base..'"'))
local rows={
 {'晶蘭迎賓雕塑','mall-F01',2.1,'Crystal orchid luxury sculpture, three sweeping ivory porcelain orchid blossoms on slender champagne bronze stems, polished dark stone integral plinth. Premium perfume and jewellery hall centrepiece.'},
 {'絲帶時裝雕塑','mall-F02',2.3,'Haute couture silk ribbon sculpture, flowing rose gold and blush enamel ribbons twisting into an elegant dress-like silhouette, compact polished limestone plinth. Luxury womenswear art.'},
 {'紳士風帆雕塑','mall-F03',2.2,'Gentleman sail sculpture, graceful folded navy blue enamel and brushed platinum sail forms rising from dark walnut base, restrained premium masculine design, no letters.'},
 {'珍珠結飾雕塑','mall-F04',1.8,'Pearl knot sculpture, interwoven satin gold loops with three large lustrous ivory pearls, elegant handbag and shoe hall art on a low oval marble base.'},
 {'花瓣陶藝裝置','mall-F05',1.9,'Contemporary folded petal ceramic sculpture, interlocking cream terracotta and sage glazed organic bowls forming a flowering composition, rich tactile detail, compact stable stone foot.'},
 {'書頁之樹','mall-F06',2.2,'Tree of pages sculpture, bronze tree trunk with an open canopy of delicately curved ivory book pages, blank pages without writing, organic elegant shape on a small walnut plinth.'},
 {'未來光環裝置','mall-F07',2.3,'Futuristic orbit sculpture, three offset polished aluminium torus rings enclosing an iridescent blue ceramic sphere, precise sleek digital lifestyle art with a stable charcoal pedestal.'},
 {'童話木馬','mall-F08',2.0,'Whimsical luxury carousel horse sculpture, pearl white horse with pastel mint saddle and delicate golden floral decorations, standing on a stable sculpted oval base, friendly family photo landmark.'},
 {'鎏金鳳凰雕塑','mall-F09',2.3,'Golden phoenix entertainment hall sculpture, graceful bronze bird with raised filigree wings and flowing curled tail, deep red enamel feather highlights, compact stable black stone base.'},
 {'茶韻白瓷雕塑','mall-F10',1.8,'Refined porcelain tea ceremony sculpture, a sculptural ivory teapot pouring an abstract curved bronze ribbon into a tea cup, small camellia blossom details, elegant integral stone base.'},
 {'竹影盆景','west-dorm',2.4,'Sculptural Japanese black pine bonsai, thick twisting weathered trunk and layered dense green needle pads, mature realistic botanical form in a wide low charcoal ceramic planter.'},
 {'小熊貓家庭雕塑','west-dorm',1.6,'Friendly red panda parent and cub garden statue, parent seated while cub curls against its striped tail, natural warm rust and cream painted resin, stable integrated mossy stone base.'},
 {'森鹿迎賓','forest-lookout',2.3,'Elegant woodland deer garden sculpture with branching antlers entwined in small green leaves, warm bronze patina, relaxed standing pose on a low weathered stone base.'},
 {'蕨葉迎賓拱','campus-northeast',2.8,'Elegant botanical fern arch, two tall curved bronze fern fronds meeting overhead with dense green fern leaves and small white flowers, open walk-through centre and sturdy feet.'},
 {'白鷺水岸雕塑','east-wetland',2.0,'Graceful white heron sculpture standing among sculpted reeds, slender neck and folded wings in ivory ceramic with subtle gold beak, integral natural stone foot.'},
 {'貓頭鷹閱讀樹','research-garden',2.1,'Charming bronze owl perched on a sculptural twisting tree stump with a blank open book at its base, detailed feathers, warm academic garden character, stable wide foot.'},
 {'翠鳥小景','campus-fishing',1.8,'Vivid kingfisher garden sculpture with turquoise and rust plumage perched on a flowing bronze reed above an abstract ripple base, realistic bird anatomy and refined ceramic finish.'},
 {'玉蘭白瓷樹','international-garden',2.4,'White magnolia sculptural tree, elegant branching bronze stems bearing large ivory porcelain magnolia blossoms and sparse sage leaves, compact stable dark stone base.'},
 {'奔跑者雕塑','stadium-entry',2.6,'Contemporary runner sculpture, dynamic flowing brushed bronze ribbons forming an athletic human figure mid stride, abstract face without personal identity, sturdy integrated stone plinth.'},
 {'小提琴曲線長椅','practice-garden',1.5,'Sculptural violin shaped park bench, flowing warm bent timber body with dark bronze supports and elegant carved violin scroll at one end, comfortable human scale seating.'},
 {'音符鋼琴藝術','culture-west',2.0,'Public art piano sculpture, elegant compact grand piano silhouette in deep teal enamel with sweeping bronze music-note shaped support and decorative ivory keys, no words.'},
 {'祥龍花燈','nightmarket',2.6,'Festive Taiwanese dragon lantern sculpture, friendly flowing golden dragon with coral red scales curling around a sturdy bronze column, ornate floral motifs, stable pedestal, no writing.'},
 {'山茶壺裝置','shopping-east',2.0,'Oversized elegant celadon tea pot sculpture with a sculpted camellia blossom on the lid and curved bronze handle, tactile glazed ceramic finish, low stable round foot.'},
 {'草莓花語座椅','shopping-west',1.4,'Playful strawberry garden photo seat, giant ripe red strawberry with realistic seeds and fresh green leafy crown, sculpted comfortable cream bench inset along one side, sturdy broad base.'},
 {'野餐小狗雕塑','residential-north',1.5,'Friendly golden retriever puppy sculpture carrying a small wicker picnic basket with daisies, warm honey resin finish and detailed soft fur, stable sitting pose.'},
 {'燕子木雕拱','residential-south',2.7,'Hand carved warm timber garden arch with two graceful swallow birds and flowing leaf carvings, open walk-through centre, tasteful craft detail, sturdy broad side feet.'},
 {'鴛鴦雙鳥','campus-watergarden',1.4,'Pair of colourful mandarin duck public sculptures resting beside each other on a low sculpted ripple base, detailed orange sail feathers and jade blue ceramic glazes.'},
 {'山茶花心雕塑','hotel-east',2.1,'Luxury camellia blossom sculpture, layered pale blush porcelain petals with a warm gold centre on curving bronze stems, tasteful spa resort garden art on compact limestone base.'},
 {'魚躍月門','riverside-south',2.7,'Sculptural circular moon gate with two elegant bronze koi fish flowing around its rim, turquoise ceramic wave accents, open centre for photography, sturdy stone feet.'},
 {'巨型海螺','south-bay',2.1,'Oversized natural spiral conch shell public sculpture, creamy pearlescent surface with warm coral inner lip, realistic graceful spiral ribs, stable low hidden base.'},
 {'海龜迎賓','marine-garden',1.3,'Friendly sea turtle coastal sculpture, jade green shell with subtle turquoise mosaic scutes and realistic flippers, gentle face, resting stable on a low sand-coloured stone base.'},
 {'珊瑚扇藝術','east-coast',2.4,'Broad elegant sea fan coral public sculpture in warm coral pink and brushed bronze, branching organic fan with robust sculptural thickness, stable limestone pedestal.'},
 {'露營浣熊','camping-garden',1.7,'Friendly raccoon garden sculpture wearing a small canvas camping backpack and holding a metal camping mug, grey brown textured resin fur, upright stable seated pose.'},
 {'森林木雕圖騰','forest-trail',2.8,'Hand carved woodland totem sculpture with stacked owl squirrel and leaf motifs in weathered warm cedar, elegant organic trunk silhouette, broad sturdy stone foot, no words.'},
 {'古銅羅盤','lookout-east',2.0,'Large freestanding nautical compass rose sculpture in weathered bronze and dark teal enamel, intricate radial star points and a tilted circular ring, no text or letters, stable stone pedestal.'},
 {'招福貓石燈','shrine-garden',1.5,'Charming traditional Japanese lucky cat stone lantern sculpture, seated smiling cat raising one paw above a small warm lantern chamber, weathered granite with subtle bronze bell, no writing.'},
 {'野花陶球','campus-southcourt',1.7,'Contemporary garden sculpture of three intersecting glazed ceramic spheres with raised wildflower petal reliefs, dusty yellow sage and ivory palette, stable organic integrated base.'},
 {'流光緞帶長椅','administration-garden',1.3,'Elegant public bench made of two sweeping brushed bronze ribbons flowing into a comfortable curved timber seat, sophisticated warm sculptural design, sturdy ground supports.'},
 {'星光迎賓燈雕','station-south',2.7,'Welcoming tourism sculpture of a large hollow five point bronze star framing a suspended amber glass lantern, refined fluted column and stable dark stone foot, no words.'},
 {'鳳凰花盆景','shopping-gateway',2.5,'Spectacular mature flamboyant bonsai tree, twisting thick trunk, broad layered canopy of vivid red orange flowers and small green leaves, elegant wide shallow terracotta planter.'}
}
local centres={{250,890,20},{250,890,20},{112.5,987.5,129.83},{760,950,20},{842.5,630,12.32},{450,540,20},{757.5,852.5,19.73},{755,580,20},{1270,772,12},{1005,750,20},{550,335,12},{620,355,12},{430,402,12},{420,322,12},{80,390,12},{175,300,12},{780,850,20},{750,240,12},{600,180,12},{1040,340,12},{1055,295,12},{1040,400,12},{990,470,12},{990,850,20},{1000,540,20},{300,606,20},{620,705,20},{700,715,20},{160,160,12},{220,500,12}}
local assets={}
for i,r in ipairs(rows)do
 local q={id=string.format('E%03d',i),name=r[1],zone=r[2],height_m=r[3],triangle_cap=i<=10 and 10000 or 12000,prompt=r[4]..' One isolated complete realistic PBR game prop, no ground plane, no surrounding scene, no people, no words or letters. 2K textures, moderate geometry, sturdy construction.'}
 if i<=10 then q.floor=i;q.search_center_m={690,467,12.06+(i-1)*4.2};q.placement='mall_public_concourse';q.companions=false
 else q.search_center_m=centres[i-10];q.placement='outdoor';q.two_sided=(i==11 or i==14 or i==18 or i==40)
  if i==13 then q.facility='PK-0111' elseif i==19 then q.facility='PK-0114' elseif i==35 then q.facility='PK-0117';q.search_center_m={990,850,20.055} elseif i==36 then q.facility='PK-0111';q.search_center_m={85,1033,137.12} end
 end
 assert(#q.prompt<=1000);assets[#assets+1]=q
end
local plan={version='20261021-expansion40',minimum_new_tripo_models=40,preserve_previous_models=30,wall_policy='preserve_original_material',recapture=70,assets=assets,allocation={mall=10,outdoor=30},gap_inventory={
 {area='百貨十層公共區',evidence='Previous30 destinations are outdoor-only; mall programme has ten themed floors with existing stores and merchandise',need='One distinct premium centrepiece per floor',allocation=10},
 {area='校園與西側宿舍',evidence='Previous30 campus landmarks clustered at central and eastern gardens; west dormitory and research edge have no new centrepiece',need='Botanical, academic and friendly garden features'},
 {area='山頂、運動場、森林與海岸',evidence='Existing outdoor programme has twenty facilities; previous30 only covers a subset',need='Add distinctive landmarks to existing supported public facilities and trails'},
 {area='住宅、商店街、夜市與交通站',evidence='Existing entrance dressing is mostly functional furniture; main artworks remain sparse away from prior nineteen pockets',need='Friendly craft, food, animal and visitor welcome features'}
}}
local f=assert(io.open(base..'/plan.json','wb'));f:write(json.encode(plan));f:close();print('40 new models planned: 10 mall floors + 30 outdoor; capture all70 after installation')
