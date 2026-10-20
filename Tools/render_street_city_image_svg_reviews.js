/* Rasterize every store's useful plan viewport with an independent SVG engine. */
const fs=require('fs'),path=require('path'),crypto=require('crypto'),sharp=require('sharp');
const root=path.resolve(__dirname,'..'),prod=path.join(root,'Docs/StreetSocialCity/production');
const out=path.join(root,'output/street-social-city-production/image-to-svg/vector-previews');
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
async function main(){
 fs.mkdirSync(out,{recursive:true});const register=JSON.parse(fs.readFileSync(path.join(prod,'image-to-svg-register.json'),'utf8'));const results=[];
 for(const r of register.stores){
  const suffix=r.id.startsWith('M1-')?'-detail.svg':'-F01.svg';const input=path.join(prod,'store-planning-svg',r.id+suffix);const output=path.join(out,r.id+'.png');
  await sharp(input).resize({width:500}).png().toFile(output);const stats=await sharp(output).stats();
  if(stats.channels.slice(0,3).every(c=>c.stdev<4))throw new Error(r.id+': blank or failed render');
  results.push({id:r.id,svg_file:path.relative(root,input).replaceAll('\\','/'),svg_sha256:hash(input),preview_file:path.relative(root,output).replaceAll('\\','/'),preview_sha256:hash(output)});
 }
 const pages=[];
 for(let start=0;start<results.length;start+=16){
  const batch=results.slice(start,start+16),layers=[];
  for(let i=0;i<batch.length;i++){
   const row=Math.floor(i/4),col=i%4;const thumb=await sharp(path.join(root,batch[i].preview_file)).resize({width:490,height:550,fit:'inside'}).png().toBuffer();
   layers.push({input:thumb,left:col*500+5,top:row*600+40});
   const label=Buffer.from(`<svg width="490" height="34"><text x="10" y="26" font-size="25" font-family="sans-serif" fill="#203c45">${batch[i].id}</text></svg>`);
   layers.push({input:label,left:col*500+5,top:row*600+3});
  }
  const target=path.join(out,`page-${String(start/16+1).padStart(2,'0')}.jpg`);
  await sharp({create:{width:2000,height:600*Math.ceil(batch.length/4),channels:3,background:'#f2f1e9'}}).composite(layers).jpeg({quality:94}).toFile(target);
  pages.push({file:path.relative(root,target).replaceAll('\\','/'),sha256:hash(target),ids:batch.map(r=>r.id)});
 }
 fs.writeFileSync(path.join(out,'render-receipt.json'),JSON.stringify({status:'passed',stores:results.length,source_register_sha256:hash(path.join(prod,'image-to-svg-register.json')),records:results,pages}));
 console.log(JSON.stringify({status:'passed',stores:results.length,pages:pages.length}));
}
main().catch(e=>{console.error(e);process.exitCode=1});
