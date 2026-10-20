// Combine deterministic camera renders without changing their geometry.
const fs=require('fs'),path=require('path'),crypto=require('crypto'),sharp=require('sharp');
const root=path.resolve(__dirname,'..'),base=path.join(root,'output/street-social-city-production/shop-references');
const plan=JSON.parse(fs.readFileSync(path.join(root,'Docs/StreetSocialCity/production/effective-city-plan.json'),'utf8'));
const names=Object.fromEntries(plan.buildings.map(b=>[b.id,b.name]));
const styles={S01:'a warm Taiwanese cafe bookstore, pale oak bookcases, ivory plaster, restrained dark teal accents, clear glass, charcoal metal',S02:'a contemporary Taiwanese bubble tea shop, cream plaster, sage green accents, stainless service surfaces, pale oak, restrained botanical branding without text',S03:'a Taiwanese bakery and patisserie, warm ivory plaster, honey oak, muted terracotta accents, brass details, pastry display glass'};
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
(async()=>{
for(const bid of process.argv.slice(2)){
 for(const key of ['exterior','F01','F02','F03']){
  const dir=path.join(base,bid,key),meta=JSON.parse(fs.readFileSync(path.join(dir,'control.json'),'utf8'));
  const controls=meta.views.map(v=>path.join(root,v.file));
  for(let i=0;i<controls.length;i++)if(hash(controls[i])!==meta.views[i].sha256)throw new Error('Changed camera control '+controls[i]);
  const sheet=path.join(dir,'control-three-views.png');
  await sharp({create:{width:2880,height:960,channels:3,background:'#303030'}}).composite(controls.map((input,i)=>({input,left:i*960,top:0}))).png().toFile(sheet);
  const rooms=plan.rooms.filter(r=>r.building===bid&&r.floor===meta.floor);
  const common=`Use image_edit exactly ONCE on the local image control-three-views.png. Do not generate from text. The output is ONE wide three-panel perspective sheet: front-left, rear-right, high-top, exactly the same camera framing, panel order, footprint and orientation as the input. Building ${bid}, ${names[bid]}, footprint ${meta.building_footprint_m[0]}m east-west by ${meta.building_footprint_m[1]}m north-south, THREE storeys. Keep 8% framing margin and the whole footprint visible in EVERY panel. Style: ${styles[bid]||'contemporary Taiwanese university city, ivory plaster, pale oak, restrained teal'}. Stylized realistic 3D game design reference, coherent PBR finish, no people, vehicles, text, annotations or perspective labels. All three panels must depict the SAME geometry, object counts and material placements. `;
  const detail=key==='exterior'?`EXTERIOR MASSING REFERENCE. Exact building height ${meta.building_height_m}m and 3 storeys. Retain the flat rectangular roof silhouette and the exact 2.4m-wide entrance apertures; south-facing main entrance is offset west of centre, never move it to the centre. Preserve the building volume and floor-band elevations. Add coherent facade finish, storefront glazing and upper-floor window styling INSIDE the fixed wall faces; window arrangement is a preliminary facade proposal, not an approved construction schedule. No extra floors, balconies, towers, dormers, attached annexes, pavement platform, canopy outside the footprint or roof furniture. Entrance voids must stay open, no steps or raised threshold. `:
  `COMPLETE FLOOR ${meta.floor} CUTAWAY. This is the whole ${meta.building_footprint_m[0]}x${meta.building_footprint_m[1]}m floor, not only the main room. Floor height ${meta.floor_height_m}m. Programme: ${rooms.map(r=>r.name+'; furnishings '+r.required_props.join(', ')).join(' | ')}. Preserve ALL structural wall footprints, private partitions, kitchen/bathroom/storage locations, elevator, stair flights, shaft/slab openings, and every object envelope position and count. Teal boxes are approved furniture/equipment envelopes: refine the objects only within those envelopes, never add or relocate furniture groups. Retain the exact 2m clear paths. Open zones must stay fully connected: NEVER add walls, doors, frames or lintels across the open display/reception/dining boundaries. Foreground exterior walls are deliberately cut away for inspection, not missing from the actual building; retain the exact same cutaway in each corresponding panel. Stair and elevator geometry must stay intact; do not replace them with furniture. No decorative objects outside existing envelopes. Upper accommodation rooms must retain their private bedroom/bathroom partitions and beds. `;
  const prompt=common+detail+'Only image_edit and reading the specified control image are authorized. Do not run shell commands, edit project files, invoke other services, submit to Tripo, or spawn subagents. Return ONLY the absolute generated image file path and stop after this single image_edit call.';
  fs.writeFileSync(path.join(dir,'grok-prompt.txt'),prompt,'utf8');
  meta.control_sheet_file=path.relative(root,sheet).replaceAll('\\','/');meta.control_sheet_sha256=hash(sheet);meta.prompt_sha256=crypto.createHash('sha256').update(prompt).digest('hex');
  fs.writeFileSync(path.join(dir,'reference-job.json'),JSON.stringify(meta,null,2),'utf8');
 }
 console.log(bid+': four control sheets and bounded Grok prompts prepared');
}
})().catch(e=>{console.error(e);process.exit(1)});
