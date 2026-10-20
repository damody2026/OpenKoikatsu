// Assemble existing Grok views and exact control views without altering pixels.
const fs=require('fs'),path=require('path'),crypto=require('crypto'),sharp=require('sharp');
const root=path.resolve(__dirname,'..'),out=path.join(root,'Docs/StreetSocialCity/production'),base=path.join(root,'output/street-social-city-production/shop-references');
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8')),rel=p=>path.relative(root,p).replaceAll('\\','/');
const save=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2),'utf8');
const digest=sha(path.join(out,'assembly-manifest.json'));
const allowHistoricalStyle=process.argv.includes('--allow-historical-style');
const manifest=read(path.join(out,'assembly-manifest.json'));
const ledgerPath=path.join(out,'generation-ledger.json'),ledger=read(ledgerPath);
const viewIds=['front-left','rear-right','high-top'],labels=['前左透視','後右透視','高角度透視'];
const names={S01:'咖啡書屋',S02:'手搖飲店',S03:'烘焙甜點店'},records=[];
const dest=path.join(out,'shop-references');fs.mkdirSync(dest,{recursive:true});
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
async function board(files,target,title){
 const width=2880,height=1050;
 const caption=Buffer.from(`<svg width="2880" height="90"><rect width="2880" height="90" fill="#173f43"/><g fill="white" font-family="Microsoft JhengHei,sans-serif"><text x="24" y="30" font-size="22">${esc(title)}</text>${labels.map((l,i)=>`<text x="${i*960+24}" y="70" font-size="22">${l}</text>`).join('')}</g></svg>`);
 const images=await Promise.all(files.map(input=>sharp(input).resize(960,960,{fit:'contain',background:'#303030'}).toBuffer()));
 await sharp({create:{width,height,channels:3,background:'#303030'}}).composite([{input:caption,left:0,top:0},...images.map((input,i)=>({input,left:i*960,top:90}))]).jpeg({quality:94}).toFile(target);
}
(async()=>{
 for(const bid of Object.keys(names)){
  const b=manifest.layouts.find(q=>q.kind==='building'&&q.building===bid);
  const finished=read(path.join(base,bid,'dimension-locked/render-acceptance.json'));
  if(finished.source_manifest_sha256!==digest||finished.status!=='dimension_locked_illustrations_passed')throw Error('Stale dimension-locked render '+bid);
  for(const key of ['exterior','F01','F02','F03']){
   const dir=path.join(base,bid,key),meta=read(path.join(dir,'control.json'));
   if(meta.manifest_sha256!==digest)throw Error('Stale camera controls '+bid+'/'+key);
   const children=viewIds.map(v=>read(path.join(dir,v,'generation.json')));
   for(let i=0;i<children.length;i++){
    const q=children[i],job=read(path.join(dir,viewIds[i],'reference-job.json'));
    if(q.control_sheet_sha256!==job.control_sheet_sha256||q.prompt_sha256!==job.prompt_sha256||sha(path.join(root,q.image_file))!==q.image_sha256)throw Error('Changed generation input '+bid+'/'+key+'/'+viewIds[i]);
    if(q.manifest_sha256!==digest){
     if(!allowHistoricalStyle||q.manifest_sha256!==job.manifest_sha256||sha(path.join(root,q.control_sheet_file))!==q.control_sheet_sha256||sha(path.join(dir,viewIds[i],'grok-prompt.txt'))!==q.prompt_sha256)throw Error('Historical style provenance failed '+bid+'/'+key+'/'+viewIds[i]);
    }
    if(!q.visual_review||q.visual_review.image_sha256!==q.image_sha256)throw Error('Missing current visual review '+bid+'/'+key+'/'+viewIds[i]);
   }
   const oldFile=path.join(dir,'generation.json');
   if(fs.existsSync(oldFile)){
    const old=read(oldFile),oldImage=path.join(root,old.image_file),history=path.join(dir,'history');fs.mkdirSync(history,{recursive:true});
    const archiveProof=path.join(history,old.image_sha256.slice(0,12)+'-generation.json');
    if(!fs.existsSync(archiveProof))fs.copyFileSync(oldFile,archiveProof);
    if(fs.existsSync(oldImage)&&sha(oldImage)===old.image_sha256){
     const archiveImage=path.join(history,old.image_sha256.slice(0,12)+path.extname(oldImage));
     if(!fs.existsSync(archiveImage))fs.copyFileSync(oldImage,archiveImage);
     for(const q of ledger.building_references||[])if(q.image_sha256===old.image_sha256){q.image_file=rel(archiveImage);q.archived_proof_file=rel(archiveProof);}
    }
   }
   const title=`${bid} ${names[bid]}｜${key==='exterior'?'三層外觀':key+' 整層室內'}｜外框 ${b.footprint_m.join(' × ')}m`;
   const generated=path.join(dest,bid+'-'+key+'-grok.jpg'),control=path.join(dest,bid+'-'+key+'-control.jpg'),locked=path.join(dest,bid+'-'+key+'-locked.jpg');
   await board(children.map(q=>path.join(root,q.image_file)),generated,title+'｜歷史 Grok 風格草稿・非目前幾何');
   await board(meta.views.map(v=>path.join(root,v.file)),control,title+'｜精確尺寸控制幾何');
   const render=finished.records.find(q=>q.view_set===key);
   for(const view of render.views)if(sha(path.join(root,view.file))!==view.sha256)throw Error('Changed dimension-locked image');
   await board(render.views.map(q=>path.join(root,q.file)),locked,title+'｜Grok 材質方向・固定幾何重建');
   const issues=children.flatMap((q,i)=>(q.visual_review.limitations||[]).map(s=>labels[i]+'：'+s));
   const record={building:bid,view_set:key,status:'grok_style_draft_with_dimension_locked_reconstruction',manifest_sha256:digest,image_file:rel(generated),image_sha256:sha(generated),dimension_locked_sheet_file:rel(locked),dimension_locked_sheet_sha256:sha(locked),dimension_locked_render:render,control_sheet_file:rel(control),control_sheet_sha256:sha(control),views:children.map((q,i)=>({id:viewIds[i],...q})),visual_limitations:issues,geometry_approved:false,tripo_submitted:false,review_scope:'Raw Grok drafts retain their rejected geometry limitations. Consistent reconstruction uses the verified control model and illustrative equipment enclosed within source dimensions; not a Tripo model.'};
   save(oldFile,record);records.push(record);
   console.log(bid+'/'+key+': composed from three hash-verified reviewed views');
  }
 }
 const keys=new Set(records.map(q=>q.building+'/'+q.view_set));
 ledger.building_references=(ledger.building_references||[]).filter(q=>!keys.has(q.building+'/'+q.view_set)||q.status?.startsWith('rejected'));
 ledger.building_references.push(...records);save(ledgerPath,ledger);
 const cards=Object.keys(names).map(bid=>{
  const b=manifest.layouts.find(q=>q.kind==='building'&&q.building===bid),design=b.small_shop_design;
  return `<section id="${bid}"><h2>${bid} ${names[bid]} <small>${b.footprint_m.join(' × ')}m・3F</small></h2><p>${design.floors.map(f=>f.floor+'F '+esc(f.cells[0].name)).join(' ／ ')}</p><p><a href="floor-layouts/${bid}.svg">全部樓層 SVG</a> · ${[1,2,3].map(f=>`<a href="floor-layouts/${bid}-F0${f}.svg">${f}F 尺寸圖</a>`).join(' · ')}</p>${records.filter(q=>q.building===bid).map(q=>`<article><h3>${q.view_set==='exterior'?'外觀':q.view_set+' 整層室內'}</h3><p><a href="shop-references/${bid}-${q.view_set}-locked.jpg">開啟固定幾何三視圖</a> · <a href="shop-references/${bid}-${q.view_set}-grok.jpg">Grok 原始草稿</a> · <a href="shop-references/${bid}-${q.view_set}-control.jpg">開啟尺寸控制三視圖</a></p><img loading="lazy" src="shop-references/${bid}-${q.view_set}-locked.jpg" alt="${bid} ${q.view_set} 固定幾何三視圖"><details><summary>核對精確控制圖與視覺檢查紀錄</summary><img loading="lazy" src="shop-references/${bid}-${q.view_set}-control.jpg" alt="${bid} ${q.view_set} 尺寸控制三視圖"><p>${q.visual_limitations.length?q.visual_limitations.map(esc).join('<br>'):'已查看三張不同視角；尺寸與設備位置仍以 SVG 與控制 GLB 為準。'}</p></details></article>`).join('')}</section>`;
 }).join('');
 const page=`<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>S01–S03 台灣尺度店屋・Grok 透視三視圖</title><style>body{margin:0;background:#f3f1e8;color:#173f43;font:17px "Microsoft JhengHei",sans-serif}header,main{padding:24px 4vw}header{background:#173f43;color:white}header a{color:#cce6dc}main{max-width:1600px;margin:auto}h1{font-size:30px}h2{margin-top:40px}h3{margin:0}small{font-size:18px}p{line-height:1.7}a{color:#256d78}article{background:white;padding:20px;margin:20px 0;border:1px solid #c8d6cd}img{display:block;width:100%;height:auto;margin:12px 0}summary{cursor:pointer;padding:12px;background:#e2ece6}.status{padding:16px;background:#e0ede2}nav{display:flex;gap:20px;flex-wrap:wrap}</style><header><h1>三間台灣尺度店屋・透視三視圖</h1><p>三棟 × 外觀與三層室內 × 三個視角，共 36 張 Grok 草稿；依其材質方向重建 36 張同幾何圖，並列為 12 組。</p><nav>${Object.keys(names).map(b=>`<a href="#${b}">${b} ${names[b]}</a>`).join('')}<a href="../world-terrain.svg">世界地圖</a></nav></header><main><p class="status">外框與各層共用公尺尺度，含 0.2m 外牆、每層廁所、樓梯、電梯與至少 2m 公共通路；正門 2.4m、一般門 1.2m。Grok 圖為材質與設備造型參考，圖像細節不能用來取代尺寸圖及控制模型；逐圖限制列在下方核對紀錄。</p><p><a href="small-shop-redesign.md">尺度與台灣案例來源</a> · <a href="small-shop-acceptance.json">店屋配置驗收</a> · <a href="small-shop-reference-acceptance.json">36 張來源與視覺紀錄</a> · <a href="current-acceptance.md">整合驗收</a> · <a href="../ERRORS_AND_PREVENTION.md">錯誤與防範紀錄</a></p>${cards}<p>來源版本 ${digest}。目前為規畫、控制幾何及 Grok 示意圖；實際 Tripo 模型與遊戲導航另行驗收。</p></main></html>`;
 fs.writeFileSync(path.join(out,'small-shop-review.html'),page,'utf8');
 save(path.join(out,'small-shop-reference-acceptance.json'),{status:'dimension_locked_reconstruction_passed_raw_grok_geometry_not_approved',source_manifest_sha256:digest,buildings:3,view_sets:12,single_views:36,records,actual_AI_geometry_approved:false,runtime_navigation_verified:false});
})().catch(e=>{console.error(e);process.exit(1)});
