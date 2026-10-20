// Render the authoritative metre-based SVG for Grok's scene composition input.
const fs=require('fs'),path=require('path'),sharp=require('sharp'),crypto=require('crypto');
const root=path.resolve(__dirname,'..');
(async()=>{
 const records=JSON.parse(fs.readFileSync(path.join(root,'Docs/StreetSocialCity/production/scene-design-register.json'),'utf8')).scenes;
 const svg=path.join(root,'Docs/StreetSocialCity/world-terrain.svg');
 for(const r of records){
  const [x0,y0,x1,y1]=r.bounds_map_m;
  const dir=path.join(root,'output/street-social-city-production/scene-concepts',r.id);
  const target=path.join(dir,'control-map.png');
  await sharp(svg).extract({left:Math.round(100+x0*2),top:Math.round(215+y0*2),width:Math.round((x1-x0)*2),height:Math.round((y1-y0)*2)}).resize({width:1800}).png().toFile(target);
  const digest=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
  fs.writeFileSync(path.join(dir,'map-evidence.json'),JSON.stringify({source_svg_sha256:digest(svg),control_map_sha256:digest(target),bounds_map_m:r.bounds_map_m,design_sha256:r.design_sha256,prompt_sha256:r.prompt_sha256},null,2));
 }
 console.log('Prepared '+records.length+' spatial reference maps.');
})();
