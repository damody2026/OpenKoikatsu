//! Maker image selectors. Source identities never collapse across zipmods.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use std::{collections::BTreeMap,fs,io::Cursor,path::{Path,PathBuf}};
use serde::{Serialize,Deserialize};
use serde_json::{Value as J,json};
use rmpv::Value;
use sha2::{Digest,Sha256};
mod properties;
mod extra;
pub(crate) mod live_head;
pub(crate) mod timings;
mod live_head_ffi;
pub(crate) mod catalog_watch;
#[cfg(test)]
mod single_slot_tests;
#[derive(Clone,Serialize,Deserialize)]
struct Item {key:String,guid:String,source:String,entry:crate::list::ListEntry}
#[derive(Clone,Serialize,Deserialize)]
pub struct Target {pub key:String,pub label:String,pub category:i32,block:String,section:String,path:String,property:String}
fn target(key:&str,label:&str,cat:i32,section:&str,path:&str)->Target {
 Target{key:key.into(),label:label.into(),category:cat,block:"Custom".into(),section:section.into(),path:path.into(),property:format!("{section}.{path}")}
}
pub fn targets()->Vec<Target>{
 let mut t=Vec::new();
 for (key,label,cat,section,path) in [
 ("head","頭型",100,"face","headId"),("face_detail","臉部皮膚細節",400,"face","detailId"),
 ("eyebrow","眉毛種類",406,"face","eyebrowId"),("eyeline_up","上眼線種類",412,"face","eyelineUpId"),
 ("eyeline_down","下眼線種類",413,"face","eyelineDownId"),("eye_white","眼白種類",407,"face","whiteId"),
 ("eye_hi_up","上眼睛高光",410,"face","hlUpId"),("eye_hi_down","下眼睛高光",411,"face","hlDownId"),
 ("nose","鼻子種類",414,"face","noseId"),("lipline","唇線種類",404,"face","lipLineId"),
 ("mole","痣的種類",415,"face","moleId"),("eyeshadow","眼影種類",401,"face","baseMakeup.eyeshadowId"),
 ("cheek","腮紅種類",402,"face","baseMakeup.cheekId"),("lip","唇彩種類",403,"face","baseMakeup.lipId"),
 ("body_detail","身體皮膚細節",420,"body","detailId"),("sunburn","曬痕種類",422,"body","sunburnId"),
 ("nipple","乳頭種類",423,"body","nipId"),("underhair","陰毛種類",424,"body","underhairId"),
 ("hair_gloss","頭髮光澤種類",433,"hair","glossId")]{t.push(target(key,label,cat,section,path));}
 for (i,label,cat) in [(0,"後髮",101),(1,"前髮",102),(2,"側髮",103),(3,"附加頭髮",104)]{
  t.push(target(&format!("hair{i}"),label,cat,"hair",&format!("parts.{i}.id")));}
 for i in 0..2 {
  let side=if i==0{"左"}else{"右"};
  t.push(target(&format!("pupil{i}"),&format!("{side}瞳孔種類"),408,"face",&format!("pupil.{i}.id")));
  t.push(target(&format!("gradation{i}"),&format!("{side}瞳孔漸層"),409,"face",&format!("pupil.{i}.gradMaskId")));
  t.push(target(&format!("facepaint{i}"),&format!("臉部彩繪 {}",i+1),405,"face",&format!("baseMakeup.paintId.{i}")));
  t.push(target(&format!("bodypaint{i}"),&format!("身體彩繪 {}",i+1),421,"body",&format!("paintId.{i}")));
 }
 for (i,label,cat) in [(0,"上衣",105),(1,"下裝",106),(2,"胸罩",107),(3,"內褲",108),(4,"手套",109),(5,"褲襪",110),(6,"襪子",111),(7,"室內鞋",112),(8,"室外鞋",112)]{
  let mut c=target(&format!("clothes{i}"),label,cat,"clothes",&format!("parts.{i}.id"));c.block="Coordinate".into();t.push(c);
  for channel in 0..4 {let mut c=target(&format!("pattern{i}_{channel}"),&format!("{label}花紋 {}",channel+1),430,"clothes",&format!("parts.{i}.colorInfo.{channel}.pattern"));c.block="Coordinate".into();t.push(c);}
  for emblem in 0..2 {let mut c=target(&format!("emblem{i}_{emblem}"),&format!("{label}徽章 {}",emblem+1),431,"clothes",&format!("parts.{i}.{}",if emblem==0{"emblemeId"}else{"emblemeId2"}));c.block="Coordinate".into();t.push(c);}
 }
 for (i,label,cat) in [(0,"水手服上衣",200),(1,"水手服領口",201),(2,"水手服裝飾",202),(0,"外套上衣",210),(1,"外套領口",211),(2,"外套裝飾",212)]{
  let mut c=target(&format!("sub{cat}"),label,cat,"clothes",&format!("subPartsId.{i}"));c.block="Coordinate".into();t.push(c);
 }
 // Every original accessory slot can select every original accessory category.
 for i in 0..20 {for (cat,label) in [(121,"髮飾"),(122,"頭飾"),(123,"臉部配件"),(124,"頸部配件"),(125,"身體配件"),(126,"腰部配件"),(127,"腿部配件"),(128,"手臂配件"),(129,"手部配件"),(130,"私密部位配件")]{
  let mut c=target(&format!("accessory{i}_{cat}"),&format!("配件欄 {}：{label}",i+1),cat,"accessory",&format!("parts.{i}.id"));c.block="Coordinate".into();t.push(c);
 }}
 for v in &mut t {
  let parts=v.path.split('.').collect::<Vec<_>>();
  v.property=if v.section=="face" {
   if parts[0]=="pupil"{format!("ChaFileFace.{}{}",if parts[2]=="id"{"Pupil"}else{"PupilGradient"},parts[1].parse::<usize>().unwrap()+1)}
   else if parts[0]=="baseMakeup"{if parts[1]=="paintId"{format!("ChaFileMakeup.PaintID{}",parts[2].parse::<usize>().unwrap()+1)}else{format!("ChaFileMakeup.{}",parts[1])}}
   else{format!("ChaFileFace.{}",v.path)}
  }else if v.section=="body" {
   if parts[0]=="paintId"{format!("ChaFileBody.PaintID{}",parts[1].parse::<usize>().unwrap()+1)}else{format!("ChaFileBody.{}",v.path)}
  }else if v.section=="hair"{if parts[0]=="parts"{format!("ChaFileHair.{}",["HairBack","HairFront","HairSide","HairOption"][parts[1].parse::<usize>().unwrap()])}else{format!("ChaFileHair.{}",v.path)}}
  else if v.section=="accessory"{format!("accessory{}.ChaFileAccessory.PartsInfo.id",parts[1])}
  else if parts[0]=="subPartsId"{format!("ChaFileClothes.Clothes{}Sub{}",if v.category<210{"Sailor"}else{"Jacket"},["A","B","C"][parts[1].parse::<usize>().unwrap()])}
  else {let prefix=["ClothesTop","ClothesBot","ClothesBra","ClothesShorts","ClothesGloves","ClothesPants","ClothesSocks","ClothesShoesInner","ClothesShoesOuter"][parts[1].parse::<usize>().unwrap()];
   format!("ChaFileClothes.{prefix}{}",if parts[2]=="id"{String::new()}else if parts[2]=="colorInfo"{format!("Pattern{}",parts[3])}else if parts[2]=="emblemeId"{"Emblem".into()}else{"Emblem2".into()})};
 }t
}
fn hash(data:&[u8])->String{format!("{:x}",Sha256::digest(data))}
fn signature(root:&Path)->String{
 fn walk(p:&Path,out:&mut Vec<String>){if let Ok(rd)=crate::gamefs::read_dir(p){for e in rd.flatten(){let p=e.path();if p.ok_is_dir(){walk(&p,out)}else if let Ok(m)=e.metadata(){if p.extension().is_some_and(|e|e=="zipmod"||e=="unity3d"){out.push(format!("{}:{}:{:?}",p.display(),m.len(),m.modified().ok()));}}}}}
 let mut paths=Vec::new();walk(&root.join("abdata"),&mut paths);walk(&root.join("mods"),&mut paths);paths.sort();hash(paths.join("\n").as_bytes())
}
fn retain_active_sources(items:&mut Vec<Item>,index:&std::collections::HashMap<String,PathBuf>){
 let normal=|p:&str|p.replace('\\',"/").to_ascii_lowercase();
 items.retain(|item|item.guid.is_empty()||index.get(&item.guid).or_else(||index.iter().find(|(g,_)|g.eq_ignore_ascii_case(&item.guid)).map(|(_,p)|p)).is_some_and(|path|normal(&item.source)==normal(&path.to_string_lossy())));
}
fn active_catalog(root:&Path,mut items:Vec<Item>)->Vec<Item>{
 let wanted=items.iter().filter(|i|!i.guid.is_empty()).map(|i|i.guid.clone()).collect();
 let index=crate::mods::find_guids(&root.join("mods"),Some(&wanted));retain_active_sources(&mut items,&index);items
}
fn catalog_disk(root:&Path,cache:&Path,sig:&str)->Result<Vec<Item>,String>{
 fs::create_dir_all(cache).map_err(|e|e.to_string())?;
 let path=cache.join(format!("catalog-v1-{sig}.json"));
 if let Ok(b)=crate::gamefs::read(&path){if let Ok(v)=serde_json::from_slice(&b){return Ok(active_catalog(root,v));}}
 let mut items=Vec::new();let allowed=targets().iter().map(|t|t.category).collect::<std::collections::HashSet<_>>();
 let mut add=|cat:crate::list::ChaListCatalog,guid:String,source:String|{for entry in cat.by_id.into_values(){if !allowed.contains(&entry.category_no){continue;}
  let key=hash(format!("{sig}|{source}|{}|{}",entry.category_no,entry.id).as_bytes());items.push(Item{key,guid:guid.clone(),source:source.clone(),entry});}};
 add(crate::list::load_characustom_dir(&root.join("abdata/list/characustom")),String::new(),String::new());
 let mut zips=crate::mods::all_zipmods(&root.join("mods"));zips.sort();
 for zip in zips {let guid=crate::mods::guids_from_zipmod(&zip).into_iter().next().unwrap_or_default();add(crate::list::catalog_from_zipmod(&zip),guid,zip.to_string_lossy().into_owned());}
 items.sort_by(|a,b|(a.entry.category_no,&a.source,a.entry.id).cmp(&(b.entry.category_no,&b.source,b.entry.id)));
 fs::write(path,serde_json::to_vec(&items).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;Ok(active_catalog(root,items))
}
struct CachedCatalog {root:PathBuf,watch:catalog_watch::Watch,items:std::sync::Arc<Vec<Item>>}
fn catalog(root:&Path,cache:&Path)->Result<std::sync::Arc<Vec<Item>>,String>{
 static CURRENT:std::sync::OnceLock<std::sync::Mutex<Option<CachedCatalog>>>=std::sync::OnceLock::new();
 let mut current=CURRENT.get_or_init(||std::sync::Mutex::new(None)).lock().map_err(|_|"外觀目錄快取鎖定失敗")?;
 let canonical=root.canonicalize().map_err(|e|e.to_string())?;
 if let Some(entry)=current.as_mut(){if entry.root==canonical&&!entry.watch.changed(){let items=entry.items.clone();drop(current);crate::mods::prepare_strip_all_migrations(root);return Ok(items);}}
 let mut watch=catalog_watch::Watch::new(&canonical);
 for _ in 0..3 {
  let sig=signature(root);let items=std::sync::Arc::new(catalog_disk(root,cache,&sig)?);
  if !watch.available()||!watch.changed(){*current=Some(CachedCatalog{root:canonical,watch,items:items.clone()});drop(current);crate::mods::prepare_strip_all_migrations(root);return Ok(items);}
 }
 Err("素材目錄持續變更，請等檔案更新完成後再試".into())
}
fn thumb(root:&Path,cache:&Path,item:&Item)->Result<Option<PathBuf>,String>{
 let folder=cache.join("thumbnails");fs::create_dir_all(&folder).map_err(|e|e.to_string())?;let path=folder.join(format!("{}.png",item.key));if path.ok_exists(){return Ok(Some(path));}
 let ab=item.entry.info("ThumbAB");let tex=item.entry.info("ThumbTex");if ab.is_empty()||tex.is_empty()||ab=="0"||tex=="0"{return Ok(None);}
 let zip=if item.source.is_empty(){None}else{Some(item.source.as_str())};
 let bytes=zip.and_then(|z|crate::mods::zipmod_bundle_named(Path::new(z),&ab)).or_else(||crate::gamefs::read(root.join("abdata").join(&ab)).ok().map(crate::unity::SourceBytes::from));
 let png=crate::sideloader_png(zip,root,&ab,&[&tex]).or_else(||bytes.and_then(|b|crate::unity::sprite_png_from_bytes(b.to_vec(),&tex).or_else(||crate::unity::exact_png_from_bytes(b.to_vec(),&tex).map(crate::flip_runtime_blit_v))));
 if let Some(png)=png {fs::write(&path,png).map_err(|e|e.to_string())?;return Ok(Some(path));}Ok(None)
}
fn decode(bytes:&[u8])->Result<Value,String>{rmpv::decode::read_value(&mut Cursor::new(bytes)).map_err(|e|e.to_string())}
fn encode(v:&Value)->Result<Vec<u8>,String>{let mut b=Vec::new();rmpv::encode::write_value(&mut b,v).map_err(|e|e.to_string())?;Ok(b)}
fn at<'a>(v:&'a mut Value,path:&str)->Result<&'a mut Value,String>{
 if path.is_empty(){return Ok(v);}let (key,rest)=path.split_once('.').unwrap_or((path,""));
 let child=match v {Value::Map(m)=>m.iter_mut().find(|(k,_)|k.as_str()==Some(key)).map(|(_,v)|v),Value::Array(a)=>key.parse::<usize>().ok().and_then(|i|a.get_mut(i)),_=>None}.ok_or_else(||format!("卡片缺少外觀欄位：{key}"))?;
 at(child,rest)
}
fn set(v:&mut Value,key:&str,value:Value)->Result<(),String>{let Value::Map(m)=v else{return Err("外觀資料不是欄位表".into());};if let Some((_,v))=m.iter_mut().find(|(k,_)|k.as_str()==Some(key)){*v=value;}else{m.push((key.into(),value));}Ok(())}
pub(crate) fn blocks_from_card(bytes:&[u8])->Result<BTreeMap<String,Vec<u8>>,String>{
 let (end,_)=crate::portable::png_snapshot(bytes)?;let extra=&bytes[end+12..];let mut c=Cursor::new(extra);crate::card::read_i32_le(&mut c)?;crate::card::read_dotnet_string(&mut c)?;crate::card::read_dotnet_string(&mut c)?;
 let n=crate::card::read_i32_le(&mut c)?;if n<0{return Err("預覽資料長度錯誤".into());}c.set_position(c.position()+n as u64);let n=crate::card::read_i32_le(&mut c)?;if n<0{return Err("區塊表長度錯誤".into());}let p=c.position() as usize;let table=decode(extra.get(p..p+n as usize).ok_or("區塊表截斷")?)?;c.set_position((p+n as usize) as u64);crate::card::read_i64_le(&mut c)?;let base=c.position() as usize;
 crate::card::lst_blocks(&table).into_iter().map(|(name,pos,len)|{if pos<0||len<0{return Err("區塊位置錯誤".into());}Ok((name,extra.get(base+pos as usize..base+pos as usize+len as usize).ok_or("區塊截斷")?.to_vec()))}).collect()
}
fn appearance_fingerprint(blocks:&BTreeMap<String,Vec<u8>>)->Result<String,String>{
 fn clean(v:&mut Value){match v{Value::Map(m)=>{m.retain(|(k,_)|!matches!(k.as_str(),Some("shapeValueFace"|"shapeValueBody"|"bustSoftness"|"bustWeight")));for(_,v)in m.iter_mut(){clean(v)}m.sort_by(|(a,_),(b,_)|a.to_string().cmp(&b.to_string()));},Value::Array(a)=>for v in a{clean(v)},_=>{}}}
 let raw=blocks.get("Custom").ok_or("缺少身體資料")?;let mut values=Vec::new();
 if let Ok(mut v)=decode(raw){if v.is_map(){if v.as_map().is_some_and(|m|m.iter().any(|(k,_)|k.as_str()==Some("face"))){for key in ["face","body","hair"]{let mut section=at(&mut v,key)?.clone();clean(&mut section);values.push(section);}}else{clean(&mut v);values.push(v);}}}
 if values.is_empty(){let mut p=0;while p<raw.len(){let n=u32::from_le_bytes(raw.get(p..p+4).ok_or("身體資料截斷")?.try_into().unwrap()) as usize;p+=4;let mut v=decode(raw.get(p..p+n).ok_or("身體資料截斷")?)?;p+=n;if values.is_empty()&&v.as_map().is_some_and(|m|m.iter().any(|(k,_)|k.as_str()==Some("face"))){let mut sections=Vec::new();for key in ["face","body","hair"]{let mut section=at(&mut v,key)?.clone();clean(&mut section);sections.push(section);}values=sections;}else{clean(&mut v);values.push(v);}}}
 let mut bytes=encode(&Value::Array(values))?;bytes.extend(blocks.get("Coordinate").ok_or("缺少服裝資料")?);let mut parameter=decode(blocks.get("Parameter").ok_or("缺少角色識別資料")?)?;for key in ["lastname","firstname","sex"]{bytes.extend(encode(at(&mut parameter,key)?)?);}Ok(hash(&bytes))
}
/// Index only small appearance blocks. Skip PNG pixels and KKEx texture blobs
/// with file seeks; installed libraries can contain hundreds of GB of cards.
fn appearance_blocks_from_file(path:&Path)->Result<BTreeMap<String,Vec<u8>>,String>{
 use std::io::{Read,Seek,SeekFrom};
 let mut file=fs::File::open(path).map_err(|e|e.to_string())?;
 fn read(file:&mut fs::File,n:usize)->Result<Vec<u8>,String>{if n>32*1024*1024{return Err("索引區塊過大".into());}let mut b=vec![0;n];file.read_exact(&mut b).map_err(|e|e.to_string())?;Ok(b)}
 let magic=read(&mut file,8)?;if magic!=b"\x89PNG\r\n\x1a\n"{return Err("不是角色 PNG".into());}
 loop{let h=read(&mut file,8)?;let n=u32::from_be_bytes(h[..4].try_into().unwrap()) as u64;file.seek(SeekFrom::Current((n+4) as i64)).map_err(|e|e.to_string())?;if &h[4..]==b"IEND"{break;}}
 read(&mut file,4)?;for _ in 0..2{let mut n=0u32;let mut shift=0;loop{let b=read(&mut file,1)?[0];if shift>28{return Err("字串長度錯誤".into());}n|=((b&127) as u32)<<shift;shift+=7;if b<128{break;}}file.seek(SeekFrom::Current(n as i64)).map_err(|e|e.to_string())?;}
 let n=u32::from_le_bytes(read(&mut file,4)?.try_into().unwrap());file.seek(SeekFrom::Current(n as i64)).map_err(|e|e.to_string())?;
 let n=u32::from_le_bytes(read(&mut file,4)?.try_into().unwrap()) as usize;let table=decode(&read(&mut file,n)?)?;read(&mut file,8)?;let base=file.stream_position().map_err(|e|e.to_string())?;
 let mut blocks=BTreeMap::new();for(name,pos,len)in crate::card::lst_blocks(&table){if !matches!(name.as_str(),"Custom"|"Coordinate"|"Parameter"){continue;}if pos<0||len<0{return Err("外觀區塊位置錯誤".into());}file.seek(SeekFrom::Start(base+pos as u64)).map_err(|e|e.to_string())?;blocks.insert(name,read(&mut file,len as usize)?);}Ok(blocks)
}
const PROVENANCE:&str="OpenKoikatsu.AppearanceSource";
/// Old lean PNGs omitted KKEx. Recover only an exact, unambiguous original;
/// guessing from a name or a shared numeric mod ID would change untouched parts.
fn original_metadata(root:&Path,cache:&Path,bytes:&[u8])->Result<Option<Vec<u8>>,String>{
 let portable=crate::portable::png_snapshot(bytes)?.1.is_some();
 let current=blocks_from_card(bytes)?;
 if current.get("KKEx").and_then(|b|decode(b).ok()).is_some_and(|v|v.as_map().is_some_and(|m|m.iter().any(|(k,_)|k.as_str()==Some(PROVENANCE)))){return Ok(None);}
 let fingerprint=appearance_fingerprint(&current)?;
 if !portable{let mut data=current.get("KKEx").map(|b|decode(b)).transpose()?.unwrap_or(Value::Map(Vec::new()));set(&mut data,PROVENANCE,Value::Array(vec![1.into(),Value::Map(vec![("fingerprint".into(),fingerprint.into())])]))?;return Ok(Some(encode(&data)?));}
 fn walk(p:&Path,files:&mut Vec<PathBuf>){if let Ok(rd)=crate::gamefs::read_dir(p){for e in rd.flatten(){let p=e.path();if p.ok_is_dir(){walk(&p,files)}else if p.extension().is_some_and(|e|e.eq_ignore_ascii_case("png")){files.push(p);}}}}
 let mut files=Vec::new();walk(&root.join("UserData/chara"),&mut files);files.sort();
 let stamp=hash(files.iter().filter_map(|p|crate::gamefs::metadata(p).ok().map(|m|format!("{}:{}:{:?}",p.display(),m.len(),m.modified().ok()))).collect::<Vec<_>>().join("\n").as_bytes());
 let index_path=cache.join(format!("source-index-v3-{stamp}.json"));
 let index:BTreeMap<String,Vec<PathBuf>>=if let Ok(b)=crate::gamefs::read(&index_path){serde_json::from_slice(&b).map_err(|e|e.to_string())?}else{let mut index=BTreeMap::<String,Vec<PathBuf>>::new();for p in files{if let Ok(b)=appearance_blocks_from_file(&p){if let Ok(key)=appearance_fingerprint(&b){index.entry(key).or_default().push(p);}}}let tmp=index_path.with_extension(format!("{}.tmp",std::process::id()));fs::write(&tmp,serde_json::to_vec(&index).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;if let Err(e)=fs::rename(&tmp,&index_path){if !index_path.ok_exists(){return Err(e.to_string());}let _=fs::remove_file(tmp);}index};
 let paths=index.get(&fingerprint).ok_or("舊版獨立卡缺少素材來源，找不到外觀資料完全相符的原始卡；已保留目前角色")?;
 let mut candidates=BTreeMap::new();for p in paths{let data=crate::gamefs::read(p).map_err(|e|e.to_string())?;let blocks=blocks_from_card(&data)?;if appearance_fingerprint(&blocks)?!=fingerprint{continue;}let kkex=blocks.get("KKEx").cloned().unwrap_or_else(||encode(&Value::Map(Vec::new())).unwrap());candidates.entry(hash(&kkex)).or_insert((p,kkex));}
 if candidates.len()!=1{return Err("舊版獨立卡符合多個不同模組來源，無法安全決定素材；已保留目前角色".into());}
 let (_, (path,raw))=candidates.into_iter().next().unwrap();let mut merged=decode(&raw)?;
 // Legacy APNG conversion normalized editable bone/pregnancy state. Missing
 // runtime modifiers mean empty, not permission to re-enable the source card's
 // old modifiers. Restore appearance sources while retaining the current
 // card's exact editor state (including intentionally absent plugin records).
 if let Value::Map(entries)=&mut merged{entries.retain(|(key,_)|!matches!(key.as_str(),Some("KKABMPlugin.ABMData"|"KK_PregnancyPlus")));}
 if let Some(b)=current.get("KKEx"){if let Value::Map(m)=decode(b)?{for(k,v)in m{if let Some(k)=k.as_str(){set(&mut merged,k,v)?;}}}}
 set(&mut merged,PROVENANCE,Value::Array(vec![1.into(),Value::Map(vec![("source".into(),path.to_string_lossy().to_string().into()),("fingerprint".into(),fingerprint.into())])]))?;Ok(Some(encode(&merged)?))
}
fn custom(raw:&[u8],t:&Target,id:i32)->Result<Vec<u8>,String>{
 if let Ok(mut v)=decode(raw){if at(&mut v,&t.section).is_ok(){*at(&mut v,&format!("{}.{}",t.section,t.path))?=id.into();return encode(&v);}}
 let index=match t.section.as_str(){"face"=>0,"body"=>1,"hair"=>2,_=>return Err("不支援此外觀區段".into())};
 let mut pos=0;let mut out=Vec::new();let mut i=0;let mut changed=false;
 while pos<raw.len(){let end=pos.checked_add(4).ok_or("外觀資料長度溢位")?;let n=u32::from_le_bytes(raw.get(pos..end).ok_or("外觀資料截斷")?.try_into().unwrap()) as usize;pos=end;
  let b=raw.get(pos..pos+n).ok_or("外觀資料截斷")?;let mut v=decode(b)?;
  // Some Custom blocks wrap one face/body/hair map rather than three maps.
  if i==0&&at(&mut v,&t.section).is_ok(){*at(&mut v,&format!("{}.{}",t.section,t.path))?=id.into();changed=true;}
  else if i==index{*at(&mut v,&t.path)?=id.into();changed=true;}
  let b=encode(&v)?;out.extend((b.len() as u32).to_le_bytes());out.extend(b);pos+=n;i+=1;
 }if !changed{return Err("外觀資料區段不足".into());}Ok(out)
}
fn coordinate(raw:&[u8],t:&Target,id:i32,outfit:usize,reset:bool)->Result<Vec<u8>,String>{
 let mut v=decode(raw)?;let slot=match &mut v {Value::Array(a)=>a.get_mut(outfit).ok_or("服裝索引超出範圍")?,v=>v};
 let Value::Binary(bin)=slot else{return Err("服裝資料格式不支援".into());};let mut pos=0;let mut out=Vec::new();
 for section in ["clothes","accessory","makeup"]{
  if section=="makeup" {out.push(*bin.get(pos).ok_or("服裝化妝資料截斷")?);pos+=1;}
  let n=u32::from_le_bytes(bin.get(pos..pos+4).ok_or("服裝資料截斷")?.try_into().unwrap()) as usize;pos+=4;
  let bytes=bin.get(pos..pos+n).ok_or("服裝資料截斷")?;let mut data=decode(bytes)?;
  if section==t.section {*at(&mut data,&t.path)?=id.into();if section=="accessory"{let slot=t.path.split('.').nth(1).ok_or("配件欄位缺少索引")?;*at(&mut data,&format!("parts.{slot}.type"))?=if reset{120}else{t.category}.into();}}
  let b=encode(&data)?;out.extend((b.len() as u32).to_le_bytes());out.extend(b);pos+=n;
 }out.extend(&bin[pos..]);*bin=out;encode(&v)
}
fn resolver(kkex:&[u8],property:&str,item:Option<&Item>)->Result<Vec<u8>,String>{
 let mut v=if kkex.is_empty(){Value::Map(Vec::new())}else{decode(kkex)?};
 resolver_value(&mut v,property,item)?;encode(&v)
}
fn resolver_value(v:&mut Value,property:&str,item:Option<&Item>)->Result<(),String>{
 let key=if !v.as_map().is_some_and(|m|m.iter().any(|(k,_)|k.as_str()==Some("com.bepis.sideloader.universalautoresolver")))&&v.as_map().is_some_and(|m|m.iter().any(|(k,_)|k.as_str()==Some("EC.Core.Sideloader.UniversalAutoResolver"))){"EC.Core.Sideloader.UniversalAutoResolver"}else{"com.bepis.sideloader.universalautoresolver"};
 
 if !v.as_map().is_some_and(|m|m.iter().any(|(k,_)|k.as_str()==Some(key))){set(v,key,Value::Array(vec![1.into(),Value::Map(vec![("info".into(),Value::Array(Vec::new()))])]))?;}
 // Resolver keys contain dots; use the literal top-level map key.
 let Value::Map(map)=v else{return Err("模組資料格式不支援".into());};
 let plugin=map.iter_mut().find(|(k,_)|k.as_str()==Some(key)).map(|(_,v)|v).unwrap();
 let data=match plugin{Value::Array(a)=>a.get_mut(1).ok_or("解析器資料不足")?,v if v.as_map().is_some_and(|m|m.iter().any(|(k,_)|k.as_str()==Some("info")))=>v,Value::Map(m)=>m.iter_mut().find(|(k,_)|k.as_str()==Some("1")||k.as_i64()==Some(1)).map(|(_,v)|v).ok_or("解析器資料不足")?,v=>v};
 let info=at(data,"info")?;let Value::Array(list)=info else{return Err("解析器清單格式不支援".into());};
 list.retain(|v|{let decoded=if let Value::Binary(b)=v{decode(b).ok()}else{Some(v.clone())};!decoded.and_then(|mut v|at(&mut v,"Property").ok().and_then(|v|v.as_str().map(str::to_owned))).is_some_and(|p|p==property)});
 if let Some(item)=item.filter(|i|!i.guid.is_empty()) {let row=Value::Map(vec![("ModID".into(),item.guid.clone().into()),("Slot".into(),item.entry.id.into()),("LocalSlot".into(),item.entry.id.into()),("Property".into(),property.into()),("CategoryNo".into(),item.entry.category_no.into()),("Name".into(),item.entry.name.clone().into())]);list.push(Value::Binary(encode(&row)?));}
 Ok(())
}
/// A type selection replaces the selected slot's authored texture. A saved
/// MaterialEditor MainTex must not silently keep drawing the previous type.
/// Geometry slot changes follow MaterialEditor's own maker reset scopes.
fn clear_material_conflicts(bytes:&[u8],t:&Target,outfit:usize)->Result<Vec<u8>,String>{
 let mut root=decode(bytes)?;clear_material_conflicts_value(&mut root,t,outfit)?;encode(&root)
}
fn clear_material_conflicts_value(root:&mut Value,t:&Target,outfit:usize)->Result<(),String>{
 let Value::Map(map)=root else{return Err("模組資料格式錯誤".into());};
 for(key,plugin)in map{if !key.as_str().is_some_and(|k|k.to_ascii_lowercase().contains("materialeditor")){continue;}
  let data=match plugin{Value::Array(a)=>a.get_mut(1),v=>Some(v)};let Some(Value::Map(data))=data else{continue;};
  for(key,value)in data{let name=key.as_str().unwrap_or("");if !(name.ends_with("PropertyList")||matches!(name,"MaterialShaderList"|"MaterialCopyList")){continue;}
   let Value::Binary(raw)=value else{continue;};let mut list=decode(raw)?;let Value::Array(rows)=&mut list else{continue;};
   rows.retain(|row|{
    let get=|key:&str|row.as_map().and_then(|m|m.iter().find(|(k,_)|k.as_str()==Some(key)).map(|(_,v)|v));
    let kind=get("ObjectType").and_then(Value::as_i64).unwrap_or(-1);let coord=get("CoordinateIndex").and_then(Value::as_i64).unwrap_or(0);let slot=get("Slot").and_then(Value::as_i64).unwrap_or(0);
    let material=get("MaterialName").or_else(||get("RendererName")).and_then(Value::as_str).unwrap_or("").to_ascii_lowercase();let prop=get("Property").and_then(Value::as_str).unwrap_or("").trim_start_matches('_').to_ascii_lowercase();
    let index=t.path.split('.').nth(1).and_then(|s|s.parse::<i64>().ok()).unwrap_or(0);
    let geometry=matches!(t.category,100..=112|121..=130|200..=212);
    let scoped=if t.section=="clothes"{kind==1&&coord==outfit as i64&&slot==if t.path.starts_with("subPartsId"){0}else{index}}
     else if matches!(t.section.as_str(),"accessory"|"accessory_extra"){kind==2&&coord==outfit as i64&&slot==index}
     else if t.section=="hair"{kind==3&&(t.category==433||slot==index)}
     else if kind==4{match t.category{
      100=>["face","head","hitomi","sirome","mayuge","eyeline","nose","mole","tooth","teeth","tongue"].iter().any(|s|material.contains(s)),
      406=>material.contains("mayuge")||material.contains("eyebrow"),
      412=>material.contains("eyeline")&&!material.contains("down")&&!material.contains("_low"),413=>material.contains("eyeline")&&(material.contains("down")||material.contains("_low")),
      407=>material.contains("sirome")||material.contains("white"),408..=411=>material.contains("hitomi")||material.contains("pupil"),
      414=>material.contains("nose")||material.contains("face"),415=>material.contains("mole")||material.contains("face"),
      400..=405=>material.contains("face"),420..=423=>material.contains("body")||material.contains("nip"),424=>material.contains("underhair"),_=>false}}
     else{false};
    if !scoped{return true;}if geometry{return false;}
    if name!="MaterialTexturePropertyList"{return true;}
    !match t.category{410=>matches!(prop.as_str(),"overtex"|"overtex1"|"eyehighlight"|"eyehiuptex"),411=>matches!(prop.as_str(),"overtex2"|"eyehighlight2"|"eyehidowntex"),433=>prop.contains("hairgloss"),430|431=>prop.starts_with("maintex"),408|409=>matches!(prop.as_str(),"maintex"|"gradmask"|"gradationmask"),400|420=>matches!(prop.as_str(),"maintex"|"detailmask"|"normalmask"),414=>matches!(prop.as_str(),"maintex"|"detailmask"|"normalmap"),_=>matches!(prop.as_str(),"maintex"|"overtex"|"overtex2")}
   });*raw=encode(&list)?;
  }
 }Ok(())
}
// Sparse overlays retain borrowed image/plugin buffers until the single final
// write. Map positions preserve duplicate keys and authored entry ordering.
enum ExtendedPatch {
 Replace(Value),
 Map(Vec<(usize,ExtendedPatch)>,Vec<(Value,Value)>),
 Element(usize,Box<ExtendedPatch>),
}
fn overlay_extended<'a>(base:&rmpv::ValueRef<'a>,patch:&'a ExtendedPatch)->Result<rmpv::ValueRef<'a>,String>{
 use rmpv::ValueRef as R;
 Ok(match patch{
  ExtendedPatch::Replace(value)=>value.as_ref(),
  ExtendedPatch::Map(changes,append)=>{
   let R::Map(entries)=base else{return Err("extended map overlay shape mismatch".into());};
   if changes.iter().any(|(i,_)|*i>=entries.len()){return Err("extended map overlay index out of range".into());}
   let mut result=Vec::with_capacity(entries.len()+append.len());
   for(i,(key,value))in entries.iter().enumerate(){result.push((key.clone(),match changes.iter().find(|(index,_)|*index==i){Some((_,p))=>overlay_extended(value,p)?,None=>value.clone()}));}
   result.extend(append.iter().map(|(k,v)|(k.as_ref(),v.as_ref())));R::Map(result)
  },
  ExtendedPatch::Element(index,change)=>{
   let R::Array(entries)=base else{return Err("extended array overlay shape mismatch".into());};
   if *index>=entries.len(){return Err("extended array overlay index out of range".into());}
   let mut result=entries.clone();result[*index]=overlay_extended(&entries[*index],change)?;R::Array(result)
  },
 })
}
fn patch_extended_data(bytes:&[u8],property:&str,item:Option<&Item>,t:&Target,outfit:usize)->Result<Vec<u8>,String>{
 use rmpv::ValueRef as R;
 let root=if bytes.is_empty(){R::Map(Vec::new())}else{rmpv::decode::read_value_ref(&mut Cursor::new(bytes)).map_err(|e|e.to_string())?};
 let R::Map(entries)=&root else{return Err("模組資料格式錯誤".into());};
 let mut sparse=Vec::new();let mut locations=Vec::new();let mut material_fields=BTreeMap::new();
 for(index,(key,value))in entries.iter().enumerate(){
  let name=match key{R::String(name)=>name.as_str().unwrap_or(""),_=>""};
  if matches!(name,"com.bepis.sideloader.universalautoresolver"|"EC.Core.Sideloader.UniversalAutoResolver"){
   sparse.push((key.to_owned(),value.to_owned()));locations.push(index);
  }else if name.to_ascii_lowercase().contains("materialeditor"){
   let body=match value{R::Array(values)=>values.get(1),v=>Some(v)};
   let Some(R::Map(fields))=body else{continue;};
   let mut selected=Vec::new();let mut indices=Vec::new();
   for(i,(key,value))in fields.iter().enumerate(){
    let name=match key{R::String(name)=>name.as_str().unwrap_or(""),_=>""};
    if (name.ends_with("PropertyList")||matches!(name,"MaterialShaderList"|"MaterialCopyList"))&&matches!(value,R::Binary(_)){
     selected.push((key.to_owned(),value.to_owned()));indices.push(i);
    }
   }
   material_fields.insert(index,indices);sparse.push((key.to_owned(),Value::Map(selected)));locations.push(index);
  }
 }
 let mut edited=Value::Map(sparse);resolver_value(&mut edited,property,item)?;clear_material_conflicts_value(&mut edited,t,outfit)?;
 let Value::Map(edited)=edited else{unreachable!()};let mut changes=Vec::new();let mut append=Vec::new();
 for(i,(key,value))in edited.into_iter().enumerate(){
  let Some(&index)=locations.get(i) else{append.push((key,value));continue;};
  let patch=if let Some(indices)=material_fields.get(&index){
   let Value::Map(fields)=value else{return Err("material overlay shape mismatch".into());};
   let change=ExtendedPatch::Map(indices.iter().copied().zip(fields.into_iter().map(|(_,v)|ExtendedPatch::Replace(v))).collect(),Vec::new());
   if matches!(&entries[index].1,R::Array(_)){ExtendedPatch::Element(1,Box::new(change))}else{change}
  }else{ExtendedPatch::Replace(value)};
  changes.push((index,patch));
 }
 let patch=ExtendedPatch::Map(changes,append);let merged=overlay_extended(&root,&patch)?;
 let mut output=Vec::with_capacity(bytes.len().saturating_add(1024));
 rmpv::encode::write_value_ref(&mut output,&merged).map_err(|e|e.to_string())?;Ok(output)
}
/// Rebuild the block directory, preserving every unrelated block and unknown key.
fn patch(original:&[u8],t:&Target,item:Option<&Item>,outfit:usize)->Result<Vec<u8>,String>{
 patch_with_metadata(original,t,item,outfit,None)
}
fn patch_with_metadata(original:&[u8],t:&Target,item:Option<&Item>,outfit:usize,metadata:Option<Vec<u8>>)->Result<Vec<u8>,String>{
 use std::borrow::Cow;
 let (iend,snapshot)=crate::portable::png_snapshot(original)?;let mut png=original[..iend+12].to_vec();
 if let Some(span)=snapshot {png.drain(span.start-8..span.end+4);}
 let extra=&original[iend+12..];let mut cur=Cursor::new(extra);crate::card::read_i32_le(&mut cur)?;crate::card::read_dotnet_string(&mut cur)?;crate::card::read_dotnet_string(&mut cur)?;
 let n=crate::card::read_i32_le(&mut cur)?;if n<0{return Err("臉部預覽長度錯誤".into());}cur.set_position(cur.position()+n as u64);
 let prefix=cur.position() as usize;let n=crate::card::read_i32_le(&mut cur)?;if n<0{return Err("區塊表長度錯誤".into());}let start=cur.position() as usize;let mut table=decode(extra.get(start..start+n as usize).ok_or("區塊表截斷")?)?;
 cur.set_position((start+n as usize) as u64);crate::card::read_i64_le(&mut cur)?;let base=cur.position() as usize;
 let mut blocks:BTreeMap<String,Cow<'_,[u8]>>=BTreeMap::new();for (name,pos,len) in crate::card::lst_blocks(&table){if pos<0||len<0{return Err("區塊位置錯誤".into());}blocks.insert(name,Cow::Borrowed(extra.get(base+pos as usize..base+pos as usize+len as usize).ok_or("區塊截斷")?));}
 if let Some(metadata)=metadata{blocks.insert("KKEx".into(),Cow::Owned(metadata));}
 let mut effective=t.clone();
 if t.path.starts_with("baseMakeup.") {if let Some(raw)=blocks.get("Coordinate") {let value=decode(raw)?;let slot=match &value{Value::Array(a)=>a.get(outfit),v=>Some(v)};if let Some(Value::Binary(b))=slot{let mut pos=0;for _ in 0..2{let n=u32::from_le_bytes(b.get(pos..pos+4).ok_or("服裝資料截斷")?.try_into().unwrap()) as usize;pos+=4+n;}if b.get(pos).copied().unwrap_or(0)!=0{effective.block="Coordinate".into();effective.section="makeup".into();effective.path=t.path.trim_start_matches("baseMakeup.").into();}}}}
 let t=&effective;
 let id=item.map(|i|i.entry.id).unwrap_or(0);let raw=blocks.get(&t.block).ok_or("角色缺少外觀資料")?;
 let changed=if t.section=="accessory_extra"{extra::patch(raw,t,item,outfit)?}else if t.block=="Custom"{custom(raw,t,id)?}else{coordinate(raw,t,id,outfit,item.is_none())?};blocks.insert(t.block.clone(),Cow::Owned(changed));
 let property=if t.block=="Custom"{t.property.clone()}else{format!("outfit{outfit}.{}",t.property)};
 let kkex=patch_extended_data(blocks.get("KKEx").map(|b|b.as_ref()).unwrap_or(&[]),&property,item,t,outfit)?;blocks.insert("KKEx".into(),Cow::Owned(kkex));
 let original_rows=at(&mut table,"lstInfo")?.as_array().ok_or("區塊表格式錯誤")?.clone();
 let mut rows=Vec::new();let mut payload_len=0usize;
 for (name,bytes) in &blocks {let mut row=original_rows.iter().find(|v|v.as_map().is_some_and(|m|m.iter().any(|(k,v)|k.as_str()==Some("name")&&v.as_str()==Some(name)))).cloned().unwrap_or_else(||Value::Map(vec![("name".into(),name.clone().into()),("version".into(),"3".into())]));set(&mut row,"pos",(payload_len as i64).into())?;set(&mut row,"size",(bytes.len() as i64).into())?;rows.push(row);payload_len=payload_len.checked_add(bytes.len()).ok_or("區塊大小溢出")?;}
 set(&mut table,"lstInfo",Value::Array(rows))?;let header=encode(&table)?;
 png.reserve(prefix+header.len()+12+payload_len);
 png.extend(&extra[..prefix]);png.extend((header.len() as i32).to_le_bytes());png.extend(header);png.extend((payload_len as i64).to_le_bytes());for(_,bytes)in blocks{png.extend(bytes.as_ref());}Ok(png)
}
fn text<'a>(r:&'a J,key:&str)->Result<&'a str,String>{r[key].as_str().ok_or_else(||format!("缺少請求欄位：{key}"))}
pub(crate) fn write_blocks(original:&[u8],blocks:BTreeMap<String,Vec<u8>>)->Result<Vec<u8>,String>{
 let (iend,snapshot)=crate::portable::png_snapshot(original)?;let mut png=original[..iend+12].to_vec();if let Some(span)=snapshot{png.drain(span.start-8..span.end+4);}
 let extra=&original[iend+12..];let mut c=Cursor::new(extra);crate::card::read_i32_le(&mut c)?;crate::card::read_dotnet_string(&mut c)?;crate::card::read_dotnet_string(&mut c)?;let n=crate::card::read_i32_le(&mut c)?;if n<0{return Err("預覽長度錯誤".into());}c.set_position(c.position()+n as u64);let prefix=c.position() as usize;let n=crate::card::read_i32_le(&mut c)?;if n<0{return Err("區塊表長度錯誤".into());}let start=c.position() as usize;let mut table=decode(extra.get(start..start+n as usize).ok_or("區塊表截斷")?)?;
 let original_rows=at(&mut table,"lstInfo")?.as_array().ok_or("區塊表格式錯誤")?.clone();let mut rows=Vec::new();let mut payload=Vec::new();
 for(name,bytes)in blocks{let mut row=original_rows.iter().find(|v|v.as_map().is_some_and(|m|m.iter().any(|(k,v)|k.as_str()==Some("name")&&v.as_str()==Some(&name)))).cloned().unwrap_or_else(||Value::Map(vec![("name".into(),name.clone().into()),("version".into(),"3".into())]));set(&mut row,"pos",(payload.len() as i64).into())?;set(&mut row,"size",(bytes.len() as i64).into())?;rows.push(row);payload.extend(bytes);}
 set(&mut table,"lstInfo",Value::Array(rows))?;let header=encode(&table)?;png.extend(&extra[..prefix]);png.extend((header.len() as i32).to_le_bytes());png.extend(header);png.extend((payload.len() as i64).to_le_bytes());png.extend(payload);Ok(png)
}
fn export_edited(root:&Path,r:&J,raw:&[u8],output:&Path)->Result<J,String>{
   // Legacy extraction still temporarily selects process-wide renderer
   // options. All appearance checkpoint/request exports share this guard;
   // independent contexts must never restore one another's environment.
   static EXPORT_LOCK:std::sync::Mutex<()>=std::sync::Mutex::new(());
   let _export=EXPORT_LOCK.lock().map_err(|_|"appearance export lock poisoned".to_string())?;
   // An OK card is edited as the original card data it carries: the preview opens it from the OK base library plus the
   // installation's zipmods (no legacy okPv snapshot is built), and saving makes it an OK card again (ok_okcard_make).
   if crate::oklib::png_has_chunk(raw,crate::oklib::CARD_CHUNK)&&crate::oklib::configured_library().is_some(){
       fs::write(output,crate::oklib::strip_ok_chunks(raw)?).map_err(|e|e.to_string())?;
       return Ok(json!({"valid":true,"output":output,"ok_card_source":true}));
   }
   let started=std::time::Instant::now();
   let input=output.with_extension("source.png");let mut f=fs::OpenOptions::new().create_new(true).write(true).open(&input).map_err(|e|e.to_string())?;use std::io::Write;f.write_all(&raw).map_err(|e|e.to_string())?;drop(f);
   let source_written=started.elapsed().as_secs_f64()*1000.;
   struct Restore(Vec<(&'static str,Option<std::ffi::OsString>)>);impl Drop for Restore{fn drop(&mut self){for(k,v)in &self.0{if let Some(v)=v{std::env::set_var(k,v)}else{std::env::remove_var(k)}}}}
   let _restore=Restore(["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"].into_iter().map(|k|(k,std::env::var_os(k))).collect());
   let _clothes=crate::unity::request_clothes_states([0;9]);
   let resources=Path::new(text(&r,"resources")?);
   // Scope follows the data registry, not the visible target's naming prefix.
   // Patterns, emblems and sailor/jacket subparts are Coordinate edits too.
   let coordinate_only=r["action"]=="accessory_slot_add" || (r["action"]=="apply" &&
       r["target"].as_str().is_some_and(|key|key.starts_with("accessory")||
           targets().iter().any(|t|t.key==key&&t.block=="Coordinate")));
   let unchanged=if r["action"]=="accessory_slot_add"{crate::portable::reuse_card_snapshot(raw,Path::new(text(&r,"card")?),output)?}else{None};
   let single_slot=if r["action"]=="apply"{r["target"].as_str().and_then(|t|t.strip_prefix("accessory"))
       .and_then(|t|t.split('_').next()).and_then(|t|t.parse::<usize>().ok())}else{None};
   let delta=if unchanged.is_none(){if let Some(slot)=single_slot{
       crate::portable::shared_edited_accessory(root,&input,Path::new(text(&r,"card")?),resources,output,r["outfit"].as_u64().unwrap_or(0) as usize,slot)?
   }else{None}}else{None};
   let head_texture=if unchanged.is_none()&&delta.is_none()&&r["head_categories"].is_array(){
       let categories=r["head_categories"].as_array().unwrap().iter().map(|v|v.as_i64().and_then(|c|i32::try_from(c).ok()).ok_or("invalid head checkpoint category")).collect::<Result<Vec<_>,_>>()?;
       crate::portable::shared_edited_head_textures(root,&input,Path::new(text(&r,"card")?),resources,output,&categories)?
   }else if unchanged.is_none()&&delta.is_none()&&r["action"]=="apply"{
       targets().into_iter().find(|t|Some(t.key.as_str())==r["target"].as_str()).map(|t|
           crate::portable::shared_edited_head_texture(root,&input,Path::new(text(&r,"card")?),resources,output,t.category)).transpose()?.flatten()
   }else{None};
   let reused=if unchanged.is_some(){unchanged}else if delta.is_some(){delta}else if head_texture.is_some(){head_texture}else if coordinate_only {crate::portable::shared_edited_outfit(root,&input,Path::new(text(&r,"card")?),resources,output,r["outfit"].as_u64().unwrap_or(0) as usize)?}else{None};
   let built=match reused{Some(b)=>b,None=>crate::portable::shared(root,&input,resources,output)?};
   let preview_built=started.elapsed().as_secs_f64()*1000.;
   // shared exports lean metadata; retain the full edited original card payload.
   let portable=crate::gamefs::read(output).map_err(|e|e.to_string())?;let (end,_)=crate::portable::png_snapshot(&portable)?;
   let (source_end,_)=crate::portable::png_snapshot(&raw)?;
   let mut complete=portable[..end+12].to_vec();complete.extend(&raw[source_end+12..]);
   // The rebuilt preview PNG does not carry the source's chunks: keep its per-component physics (okPh).
   if crate::oklib::png_chunk(raw,crate::okphysics::CHUNK).is_some(){complete=crate::okphysics::write_card(&complete,&crate::okphysics::CardPhysics::read_card(raw))?;}
   let source_hash=hash(&raw);let apng_hash=hash(&complete);
   fs::write(output,complete).map_err(|e|e.to_string())?;
   let card_written=started.elapsed().as_secs_f64()*1000.;
   let receipt=json!({"source":input,"source_sha256":source_hash,"apng_sha256":apng_hash,
       "source_embedded":true,"game_root":root,"outfit_count":built["outfit_count"],
       "outfits_rebuilt":built.get("outfits_rebuilt").cloned().unwrap_or_else(||built["outfit_count"].clone()),
       "unchanged_outfits_reused":built.get("unchanged_outfits_reused").cloned().unwrap_or(json!(0)),
       "single_slot":built.get("single_slot"),"parts_rebuilt":built.get("parts_rebuilt"),"parts_reused":built.get("parts_reused"),
       "single_slot_timings_ms":built.get("single_slot_timings_ms"),
       "head_texture_outfits_updated":built.get("head_texture_outfits_updated"),
       "head_texture_parts_updated":built.get("head_texture_parts_updated"),
       "geometry_rebuilt":built.get("geometry_rebuilt"),
       "timings_ms":{"source_write":source_written,"preview_build":preview_built-source_written,
           "metadata_hash_write":card_written-preview_built,"through_card_write":card_written}});
   fs::write(output.with_extension("verified.json"),serde_json::to_vec_pretty(&receipt).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
 Ok(json!({"valid":true,"output":output}))
}
pub fn request(root:&Path,path:&Path)->Result<J,String>{
 let r:J=serde_json::from_slice(&crate::gamefs::read(path).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
 if matches!(text(&r,"action")?,"properties"|"properties_apply"|"accessory_slot_add"){
  let original=crate::gamefs::read(text(&r,"card")?).map_err(|e|e.to_string())?;let outfit=r["outfit"].as_u64().unwrap_or(0) as usize;
  let cache=Path::new(text(&r,"cache")?);let metadata=original_metadata(root,cache,&original)?;let mut blocks=blocks_from_card(&original)?;if let Some(metadata)=metadata{blocks.insert("KKEx".into(),metadata);}
  if text(&r,"action")?=="accessory_slot_add"{extra::append(&mut blocks,outfit)?;}
  let original=write_blocks(&original,blocks)?;
  if text(&r,"action")?=="properties"{let parents:J=serde_json::from_str(include_str!("../data/editor_accessory_parents.json")).map_err(|e|e.to_string())?;return Ok(json!({"valid":true,"properties":properties::catalog_properties(&original,outfit)?,"parents":parents}));}
  let output=Path::new(text(&r,"output")?);if output.ok_exists(){return Err("外觀輸出已存在".into());}
  let raw=if text(&r,"action")?=="accessory_slot_add"{original}else{properties::patch_properties(&original,outfit,r["edits"].as_array().ok_or("缺少修改清單")?)?};
  return export_edited(root,&r,&raw,output);
 }
 let cache=Path::new(text(&r,"cache")?);let items=catalog(root,cache)?;
 match text(&r,"action")? {
  "catalog"=>{let t=if let Some(card)=r["card"].as_str(){let original=crate::gamefs::read(card).map_err(|e|e.to_string())?;let mut blocks=blocks_from_card(&original)?;if let Some(metadata)=original_metadata(root,cache,&original)?{blocks.insert("KKEx".into(),metadata);}extra::targets_for(&blocks,r["outfit"].as_u64().unwrap_or(0) as usize)?}else{targets()};Ok(json!({"valid":true,"targets":t,"items":items.as_ref()}))},
  "thumbnails"=>{let keys=r["keys"].as_array().ok_or("缺少縮圖清單")?;if keys.len()>48{return Err("縮圖分頁超過上限".into());}let mut out=Vec::new();for k in keys {let key=k.as_str().ok_or("縮圖識別碼錯誤")?;if let Some(i)=items.iter().find(|i|i.key==key){out.push(json!({"key":key,"path":thumb(root,cache,i)?}));}}Ok(json!({"valid":true,"thumbnails":out}))},
  "verify"=>{
   let original=crate::gamefs::read(text(&r,"card")?).map_err(|e|e.to_string())?;
   let blocks=blocks_from_card(&original)?;let outfit=r["outfit"].as_u64().unwrap_or(0) as usize;
   let t=extra::targets_for(&blocks,outfit)?.into_iter().find(|t|Some(t.key.as_str())==r["target"].as_str()).ok_or("找不到外觀種類")?;
   let key=text(&r,"item")?;let item=if key.is_empty(){None}else{Some(items.iter().find(|i|i.key==key&&i.entry.category_no==t.category).ok_or("找不到此外觀素材")?)};
   // Reapplying a saved selection must be idempotent in both the real field
   // and its source resolver/material overrides. No preview/resource export.
   let expected=blocks_from_card(&patch(&original,&t,item,outfit)?)?;
   if blocks!=expected{return Err("保存卡片的外觀欄位或來源識別與選取素材不符".into());}
   Ok(json!({"valid":true,"target":t.key,"item":key,"saved_field_and_source_verified":true}))
  },
  "apply"=>{let target=text(&r,"target")?;
   let original=crate::gamefs::read(text(&r,"card")?).map_err(|e|e.to_string())?;let metadata=original_metadata(root,cache,&original)?;let mut blocks=blocks_from_card(&original)?;if let Some(b)=&metadata{blocks.insert("KKEx".into(),b.clone());}let t=extra::targets_for(&blocks,r["outfit"].as_u64().unwrap_or(0) as usize)?.into_iter().find(|t|t.key==target).ok_or("找不到外觀種類")?;
   let key=text(&r,"item")?;let item=if key.is_empty(){None}else{Some(items.iter().find(|i|i.key==key&&i.entry.category_no==t.category).ok_or("找不到此外觀素材")?)};
   if item.is_some_and(|i|!i.source.is_empty()&&i.guid.is_empty()){return Err("此模組沒有可辨識的識別碼，無法安全套用素材".into());}
   let output=Path::new(text(&r,"output")?);if output.ok_exists(){return Err("外觀輸出已存在".into());}
   let raw=patch_with_metadata(&original,&t,item,r["outfit"].as_u64().unwrap_or(0) as usize,metadata)?;
   export_edited(root,&r,&raw,output)?;
   Ok(json!({"valid":true,"output":output,"target":target,"item":key}))},
  _=>Err("不支援的圖片選擇請求".into())
 }
}

#[cfg(test)]mod tests{
 use super::*;
 #[test]
 fn head_texture_commit_preserves_live_state_and_rejects_geometry(){
  let path=Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C001.png");if !path.ok_exists(){return;}
  let mut source=crate::oklib::open_test_card(path,0).unwrap();
  let mut live=crate::session::OkSession::from_cache_bytes(&source.to_cache_bytes().unwrap()).unwrap();
  let index=source.parts.iter().position(|p|p.name.to_ascii_lowercase().contains("mayuge")).unwrap();
  source.parts[index].png=Some(vec![11,22,33]);
  live.anim_time=42.;let uv=live.parts[index].uvs[0];live.parts[index].uvs[0]=[0.123,0.456];
  let indices=live.head_texture_changes(&source).unwrap();assert_eq!(indices,vec![index]);
  live.copy_head_textures(&source,&indices);assert_eq!(live.parts[index].png,source.parts[index].png);
  assert_eq!(live.anim_time,42.);assert_eq!(live.parts[index].uvs[0],[0.123,0.456]);assert_ne!(uv,live.parts[index].uvs[0]);
  source.parts[index].rest_positions[0][0]+=0.1;
  assert!(live.head_texture_changes(&source).is_err());
  assert_eq!(live.parts[index].png,Some(vec![11,22,33]));
 }
 #[test]
 fn head_texture_rejects_transparency_change_on_opaque_main_alpha(){
  let path=Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C001.png");
  let mut live=crate::oklib::open_test_card(path,0).unwrap();
  let index=live.parts.iter().position(|p|p.role=="head"&&p.shader_kind==11).unwrap();
  let opaque=crate::unity::solid_png([1.;4],1,1).unwrap();
  let transparent=crate::unity::solid_png([1.,1.,1.,0.],1,1).unwrap();
  live.parts[index].png=Some(opaque.clone());
  live.parts[index].source_material.as_mut().unwrap().floats.insert("_OKNativeAlphaOpaque".into(),1.);
  let mut source=crate::session::OkSession::from_cache_bytes(&live.to_cache_bytes().unwrap()).unwrap();
  source.parts[index].png=Some(transparent.clone());
  assert!(live.head_texture_changes(&source).is_err());
  let mut delta=crate::session::HeadTextureDelta{part_count:live.parts.len(),shape_body:live.shape_body.clone(),shape_face:live.shape_face.clone(),
   parts:vec![crate::session::HeadTextureDeltaPart{index,name:live.parts[index].name.clone(),shader_kind:11,png:transparent,
    runtime_textures:live.parts[index].runtime_textures.clone(),eye_textures:None}]};
  assert!(live.apply_head_texture_delta(&delta).is_err());assert_eq!(live.parts[index].png.as_ref(),Some(&opaque));
  delta.parts[0].png=crate::unity::solid_png([0.4,0.6,0.8,1.],1,1).unwrap();
  live.apply_head_texture_delta(&delta).unwrap();
  assert_eq!(live.parts[index].shader_textures.get("_MainTex"),Some(&delta.parts[0].png));
 }
 #[test]
 #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
 fn restoring_legacy_appearance_sources_preserves_current_editor_modifiers(){
  let root=&crate::test_support::game_path("");
  let path=Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C001.png");if !root.ok_exists()||!path.ok_exists(){return;}
  let raw=crate::gamefs::read(path).unwrap();let before=blocks_from_card(&raw).unwrap();
  let current=crate::editor_mods::decode(before.get("KKEx").map(Vec::as_slice).unwrap_or(&[])).unwrap();
  let restored=original_metadata(root,Path::new("C:/portable/OpenKoikatsu/Saved/AppearanceEditor/cache"),&raw).unwrap().unwrap();
  let after=crate::editor_mods::decode(&restored).unwrap();
  assert_eq!(serde_json::to_value(&current).unwrap(),serde_json::to_value(&after).unwrap());
  assert!(decode(&restored).unwrap().as_map().unwrap().len()>before.get("KKEx").and_then(|v|decode(v).ok()).and_then(|v|v.as_map().map(Vec::len)).unwrap_or(0));
 }
 #[test]
 #[ignore="installed legacy APNG display regression"]
 fn legacy_texture_edit_keeps_display_geometry(){
  let project=Path::new("C:/portable/OpenKoikatsu");let game=&crate::test_support::game_path("");
  let base=project.join("Content/CharacterCards/CrowdAPNG/C001.png");let original=crate::gamefs::read(&base).unwrap();
  let metadata=original_metadata(game,&project.join("Saved/AppearanceEditor/cache"),&original).unwrap();
  let t=targets().into_iter().find(|t|t.key=="eyebrow").unwrap();
  let item=Item{key:"audit".into(),guid:String::new(),source:String::new(),entry:crate::list::ListEntry{id:7,category_no:406,..Default::default()}};
  let raw=patch_with_metadata(&original,&t,Some(&item),0,metadata).unwrap();
  let dir=project.join("Saved/Tests/AppearanceSelectors20261010").join(format!("geometry-{}",std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));fs::create_dir_all(&dir).unwrap();
  let output=dir.join("eyebrow.png");let request=json!({"action":"apply","card":base,"resources":project.join("Content/IndependentResources/v1"),"target":"eyebrow"});export_edited(game,&request,&raw,&output).unwrap();
  let count=crate::card::parse_card_bytes_mode(&raw,0,false).unwrap().coordinate_count;
  for outfit in 0..count{
   let before=crate::portable::load_outfit(&base,outfit as i32).unwrap();let after=crate::portable::load_outfit(&output,outfit as i32).unwrap();
   assert_eq!(before.parts.len(),after.parts.len());
   for(a,b)in before.parts.iter().zip(&after.parts){
    assert_eq!(a.name,b.name);assert_eq!(a.rest_positions,b.rest_positions,"rest outfit={outfit} part={}",a.name);
    assert_eq!(a.positions,b.positions,"display outfit={outfit} part={}",a.name);
    assert_eq!(a.render_visible,b.render_visible);
   }
  }
  fs::write(dir.join("acceptance.json"),serde_json::to_vec_pretty(&json!({"valid":true,"outfits":count,"posed_geometry_preserved":true})).unwrap()).unwrap();
  eprintln!("legacy display geometry parity report {}",dir.display());
 }
 #[test]
 fn extended_overlay_matches_full_tree_for_every_target(){
  let row=|material:&str,property:&str|Value::Map(vec![("ObjectType".into(),4.into()),("MaterialName".into(),material.into()),("Property".into(),property.into())]);
  let lists=encode(&Value::Array(vec![row("cf_m_hitomi_00","_overtex1"),row("cf_m_hitomi_00","_overtex2"),row("cf_m_mayuge_00","_MainTex"),row("cf_m_nose_00","_DetailMask")])).unwrap();
  let data=Value::Map(vec![("TextureDictionary".into(),Value::Binary(vec![73;1024*1024])),
   ("MaterialTexturePropertyList".into(),Value::Binary(lists.clone())),
   ("MaterialTexturePropertyList".into(),Value::Binary(encode(&Value::Array(vec![row("cf_m_sirome_00","_MainTex")])).unwrap())),
   ("unknown".into(),Value::Ext(7,vec![9;1024]))]);
  let root=Value::Map(vec![("opaque.plugin".into(),Value::Binary(vec![51;1024*1024])),
   ("KK_Plugins.MaterialEditor".into(),Value::Array(vec![42.into(),data.clone(),"retained extra".into()])),
   ("Other.MaterialEditor".into(),data),
   ("EC.Core.Sideloader.UniversalAutoResolver".into(),Value::Array(vec![1.into(),Value::Map(vec![("info".into(),Value::Array(Vec::new()))])]))]);
  let bytes=encode(&root).unwrap();
  for target in targets(){
   let mut expected=decode(&bytes).unwrap();resolver_value(&mut expected,&target.property,None).unwrap();clear_material_conflicts_value(&mut expected,&target,2).unwrap();
   assert_eq!(patch_extended_data(&bytes,&target.property,None,&target,2).unwrap(),encode(&expected).unwrap(),"{}",target.key);
  }
  let target=targets().remove(2);let raw=encode(&Value::Map(vec![("opaque".into(),Value::Binary(vec![1;1024]))])).unwrap();
  let mut expected=decode(&raw).unwrap();resolver_value(&mut expected,&target.property,None).unwrap();
  assert_eq!(patch_extended_data(&raw,&target.property,None,&target,0).unwrap(),encode(&expected).unwrap());
  assert_eq!(patch_extended_data(&[],&target.property,None,&target,0).unwrap(),resolver(&[],&target.property,None).unwrap());
 }
 #[test]
 fn upper_highlight_type_clears_native_overtex1_without_removing_lower(){
  let row=|property:&str|Value::Map(vec![("ObjectType".into(),4.into()),("MaterialName".into(),"cf_m_hitomi_00".into()),("Property".into(),property.into())]);
  let rows=Value::Array(vec![row("_overtex1"),row("_overtex2"),row("_MainTex")]);
  let plugin=Value::Map(vec![("MaterialTexturePropertyList".into(),Value::Binary(encode(&rows).unwrap()))]);
  let root=Value::Map(vec![("KK_Plugins.MaterialEditor".into(),Value::Array(vec![1.into(),plugin]))]);
  let t=targets().into_iter().find(|t|t.key=="eye_hi_up").unwrap();
  let mut result=decode(&clear_material_conflicts(&encode(&root).unwrap(),&t,0).unwrap()).unwrap();
  let Value::Map(map)=&mut result else{panic!()};let Value::Array(plugin)=&mut map[0].1 else{panic!()};
  let rows=decode(at(&mut plugin[1],"MaterialTexturePropertyList").unwrap().as_slice().unwrap()).unwrap();
  assert_eq!(rows.as_array().unwrap().len(),2);
  for name in ["_overtex2","_MainTex"]{assert!(rows.as_array().unwrap().iter().any(|v|v.as_map().unwrap().iter().any(|(k,v)|k.as_str()==Some("Property")&&v.as_str()==Some(name))));}
 }
 #[test]
 fn eye_gradient_lookup_does_not_use_the_other_eyes_mod(){
  let path=Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C001.png");if !path.ok_exists(){return;}
  let mut card=crate::card::parse_card_bytes_mode(&crate::gamefs::read(path).unwrap(),0,false).unwrap();
  card.resolves=vec![crate::card::ResolveInfo{mod_id:"other.eye.mod".into(),slot:999,local_slot:1,property:"ChaFileFace.PupilGradient2".into(),category_no:409,name:"right only".into()}];
  let mut vanilla=crate::list::ChaListCatalog::default();vanilla.insert_all(vec![crate::list::ListEntry{category_no:409,id:1,name:"vanilla left".into(),..Default::default()}]);
  let resolved=crate::face_detail_entry_for_property(&vanilla,&Default::default(),&card,&Default::default(),409,1,Some("ChaFileFace.PupilGradient1")).unwrap();
  assert_eq!(resolved.name,"vanilla left");
 }
 #[test]
 fn every_image_selector_patches_and_resets_its_own_field(){
  let path=Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C001.png");
  if !path.ok_exists(){return;}
  let original=crate::gamefs::read(path).unwrap();let mut failures=Vec::new();let mut checked=0;
  for t in targets(){
   let item=Item{key:"audit".into(),guid:String::new(),source:String::new(),entry:crate::list::ListEntry{id:17,category_no:t.category,..Default::default()}};
   for selected in [Some(&item),None]{
    match patch(&original,&t,selected,0){
     Err(error)=>failures.push(format!("{} reset={}: {error}",t.key,selected.is_none())),
     Ok(bytes)=>{
      if let Err(error)=crate::card::parse_card_bytes_mode(&bytes,0,false){failures.push(format!("{}: {error}",t.key));}
      let repeated=patch(&bytes,&t,selected,0).unwrap();
      assert_eq!(blocks_from_card(&bytes).unwrap(),blocks_from_card(&repeated).unwrap(),"saved verification is not idempotent: {}",t.key);
      checked+=1;
     }
    }
   }
  }
  eprintln!("image selector field transactions checked={checked}");
  assert!(failures.is_empty(),"{}",failures.join("\n"));
 }
 #[test]
 #[ignore="installed catalog sequential ten-selection metadata audit; not renderer acceptance"]
 fn installed_catalog_ten_per_type_sequential_metadata(){
  let project=Path::new("C:/portable/OpenKoikatsu");let root=&crate::test_support::game_path("");
  let base=project.join("Content/CharacterCards/CrowdAPNG/C001.png");
  let cache=project.join("Saved/AppearanceEditor/cache");
  let original=crate::gamefs::read(&base).unwrap();let mut blocks=blocks_from_card(&original).unwrap();
  if let Some(metadata)=original_metadata(root,&cache,&original).unwrap(){blocks.insert("KKEx".into(),metadata);}
  // Carry the result into the next click and next type, exactly as the editor
  // does. This catches resolver/coordinate interactions across transactions.
  // Resource extraction and render timing are explicitly outside this audit.
  let mut current=write_blocks(&original,blocks).unwrap();
  let types=extra::targets_for(&blocks_from_card(&current).unwrap(),0).unwrap();
  let items=catalog(root,&cache).unwrap();let mut rows=Vec::new();let mut count=0;
  let output=project.join("Saved/Tests/AppearanceSelectors20261010/ten-per-type-metadata.json");
  for (type_index,t) in types.iter().enumerate(){
   let selected=items.iter().filter(|i|i.entry.category_no==t.category).take(10).collect::<Vec<_>>();
   let available=items.iter().filter(|i|i.entry.category_no==t.category).count();
   let mut cases=Vec::new();
   for (selection_index,item) in selected.iter().enumerate(){
    assert!(item.source.is_empty()||!item.guid.is_empty(),"unselectable source type={} item={}",t.key,item.key);
    let started=std::time::Instant::now();
    current=patch(&current,t,Some(item),0).unwrap_or_else(|e|panic!("{} / {}: {e}",t.key,item.key));
    crate::card::parse_card_bytes_mode(&current,0,false).unwrap();
    let actual=blocks_from_card(&current).unwrap();
    let expected=blocks_from_card(&patch(&current,t,Some(item),0).unwrap()).unwrap();
    assert_eq!(actual,expected,"saved source mismatch: {} / {}",t.key,item.key);
    cases.push(json!({"selection_index":selection_index,"item":item.key,"source":item.source,
     "saved_field_and_source_verified":true,"metadata_transaction_ms":started.elapsed().as_secs_f64()*1000.}));count+=1;
   }
   rows.push(json!({"target_index":type_index,"target":t.key,"category":t.category,"available":available,"cases":cases}));
   fs::write(&output,serde_json::to_vec_pretty(&json!({"data_layer_only":true,"renderer_verified":false,
    "complete":type_index+1==types.len(),"target_count":types.len(),"case_count":count,"types":rows})).unwrap()).unwrap();
  }
  eprintln!("sequential catalog metadata targets={} cases={count}; no renderer/timing acceptance claimed",types.len());
 }
 #[test]
 #[ignore="installed native eye highlight and RT parity"]
 fn eye_highlight_delta_matches_native_source(){
  let project=Path::new("C:/portable/OpenKoikatsu");let game=&crate::test_support::game_path("");
  let base=project.join("Content/CharacterCards/CrowdAPNG/C001.png");let original=crate::gamefs::read(&base).unwrap();
  let metadata=original_metadata(game,&project.join("Saved/AppearanceEditor/cache"),&original).unwrap();
  let t=targets().into_iter().find(|t|t.key=="eye_hi_down").unwrap();
  let item=Item{key:"audit".into(),guid:String::new(),source:String::new(),entry:crate::list::ListEntry{id:5,category_no:411,..Default::default()}};
  let raw=patch_with_metadata(&original,&t,Some(&item),0,metadata).unwrap();
  let dir=project.join("Saved/Tests/AppearanceSelectors20261010").join(format!("eye-native-{}",std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));fs::create_dir_all(&dir).unwrap();
  let output=dir.join("delta.png");let resources=project.join("Content/IndependentResources/v1");
  let request=json!({"action":"apply","card":base,"resources":resources,"target":"eye_hi_down"});
  export_edited(game,&request,&raw,&output).unwrap();
  let receipt:J=serde_json::from_slice(&crate::gamefs::read(output.with_extension("verified.json")).unwrap()).unwrap();
  assert_eq!(receipt["geometry_rebuilt"],false,"eye delta fell back to full rebuild");
  let source=dir.join("delta.source.png");let full=dir.join("full.png");crate::portable::shared(game,&source,&resources,&full).unwrap();
  let count=crate::card::parse_card_bytes_mode(&raw,0,false).unwrap().coordinate_count;
  for outfit in 0..count {
   let a=crate::portable::load_outfit(&output,outfit as i32).unwrap();let b=crate::portable::load_outfit(&full,outfit as i32).unwrap();
   let eyes=a.parts.iter().filter(|p|p.name.to_ascii_lowercase().contains("hitomi")&&p.name.contains("02")).collect::<Vec<_>>();
   assert!(!eyes.is_empty());
   for eye in eyes {let native=b.parts.iter().find(|p|p.name==eye.name).unwrap();
    assert_eq!(eye.png,native.png,"eye RT mismatch outfit={outfit} part={}",eye.name);
    assert_eq!(eye.eye_draw.as_ref().map(|e|&e.textures),native.eye_draw.as_ref().map(|e|&e.textures),"eye highlights mismatch outfit={outfit} part={}",eye.name);
    assert_eq!(eye.eye_draw.as_ref().map(|e|&e.raw_uvs),native.eye_draw.as_ref().map(|e|&e.raw_uvs),"eye UV changed outfit={outfit} part={}",eye.name);
   }
  }
  fs::write(dir.join("acceptance.json"),serde_json::to_vec_pretty(&json!({"valid":true,"outfits":count,"native_eye_rt_and_highlights_match":true,"receipt":receipt})).unwrap()).unwrap();
  eprintln!("eye highlight native parity report {}",dir.display());
 }
 #[test]
 #[ignore="installed source/APNG parity; set OK_APPEARANCE_NATIVE_BASELINE"]
 fn head_texture_delta_matches_full_source_for_every_outfit(){
  let game=&crate::test_support::game_path("");
  let project=Path::new("C:/portable/OpenKoikatsu");
  let base=project.join("Saved/AppearanceEditor/F2CC7DA14D83469B0813EFBEBA4A4AD2.input.png");
  let native=PathBuf::from(std::env::var("OK_APPEARANCE_NATIVE_BASELINE").unwrap());
  let original=crate::gamefs::read(&base).unwrap();let cache=project.join("Saved/AppearanceEditor/cache");
  let t=targets().into_iter().find(|t|t.key=="eyebrow").unwrap();
  let metadata=original_metadata(game,&cache,&original).unwrap();
  let item=Item{key:"audit".into(),guid:String::new(),source:String::new(),entry:crate::list::ListEntry{id:7,category_no:406,..Default::default()}};
  let raw=patch_with_metadata(&original,&t,Some(&item),0,metadata).unwrap();
  let dir=project.join("Saved/Tests/AppearanceSelectors20261010").join(format!("delta-{}",std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));fs::create_dir_all(&dir).unwrap();
  let output=dir.join("eyebrow.png");let request=json!({"action":"apply","card":base,"resources":project.join("Content/IndependentResources/v1"),"target":"eyebrow"});
  let started=std::time::Instant::now();export_edited(game,&request,&raw,&output).unwrap();
  let receipt:J=serde_json::from_slice(&crate::gamefs::read(output.with_extension("verified.json")).unwrap()).unwrap();
  assert_eq!(receipt["outfits_rebuilt"],0,"fast path not exercised");
  let info=crate::card::parse_card_bytes_mode(&raw,0,false).unwrap();
  let mut rows=Vec::new();
  for outfit in 0..info.coordinate_count{
   let before=crate::portable::load_outfit(&base,outfit as i32).unwrap();
   let delta=crate::portable::load_outfit(&output,outfit as i32).unwrap();
   let full=crate::portable::load_outfit(&native,outfit as i32).unwrap();
   assert_eq!(delta.parts.len(),before.parts.len());
   let mut changed=0;
   for(a,b)in before.parts.iter().zip(&delta.parts){
    assert_eq!(a.name,b.name);assert_eq!(a.rest_positions,b.rest_positions);assert_eq!(a.weights,b.weights);assert_eq!(a.bind_poses,b.bind_poses);
    if a.name.to_ascii_lowercase().contains("mayuge"){
     let expected=full.parts.iter().find(|p|p.name==a.name).unwrap();assert_eq!(b.png,expected.png,"native parity outfit={outfit} part={}",b.name);
     assert_ne!(a.png,b.png);changed+=1;
    }else{assert_eq!(a.png,b.png,"unrelated texture changed: {}",a.name);}
   }
   assert!(changed>0);rows.push(json!({"outfit":outfit,"native_texture_parity":true,"unrelated_geometry_and_textures_preserved":true}));
  }
  fs::write(dir.join("acceptance.json"),serde_json::to_vec_pretty(&json!({"valid":true,"elapsed_seconds":started.elapsed().as_secs_f64(),"receipt":receipt,"outfits":rows})).unwrap()).unwrap();
  eprintln!("head texture parity report {}",dir.display());
 }
 #[test]#[ignore="local normal-hair position diagnosis; requires OK_TEST_EDITED_CARD"]
 fn diagnose_unedited_wig_bounds(){
  for name in ["C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C000.png".to_string(),std::env::var("OK_TEST_EDITED_CARD").unwrap()]{
   let mut s=crate::portable::load_outfit(Path::new(&name),0).unwrap();let anchor=crate::bone_world(s.body_skel.as_ref(),s.head_skel.as_ref(),"a_n_headtop");eprintln!("ANCHOR {anchor:?}");for index in 0..s.parts.len(){if s.parts[index].name.contains("爱莉希雅"){let mut hash=[0u8;32];unsafe{crate::ffi::ok_part_skin_source_hash_v2(&mut s,index as i32,hash.as_mut_ptr());}eprintln!("WIG_SOURCE {}",hash.iter().map(|b|format!("{b:02x}")).collect::<String>());}}let info=crate::card::parse_card(Path::new(&name)).unwrap();eprintln!("CARD {name} parent0={} parent1={} abmx={}",info.accessory[0].parent_key,info.accessory[1].parent_key,s.abm.len());
   for p in s.parts.iter().filter(|p|p.name.contains("爱莉希雅")){for(label,positions)in [("rest",&p.rest_positions),("posed",&p.positions)]{let mut lo=[f32::INFINITY;3];let mut hi=[f32::NEG_INFINITY;3];for v in positions{for k in 0..3{lo[k]=lo[k].min(v[k]);hi[k]=hi[k].max(v[k]);}}eprintln!("WIG {} {label} {:?} {:?} parent={:?}",p.name,lo,hi,p.parent_bone);}}
  }
 }
 #[test]#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]fn empty_extra_slot_reuses_every_outfit_and_keeps_wig_binding(){
  let base=Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C000.png");if !base.ok_exists(){return;}
  let original=crate::gamefs::read(base).unwrap();let mut blocks=blocks_from_card(&original).unwrap();
  let root=&crate::test_support::game_path("");let cache=Path::new("C:/portable/OpenKoikatsu/Saved/AppearanceEditor/cache");
  if let Some(metadata)=original_metadata(root,cache,&original).unwrap(){blocks.insert("KKEx".into(),metadata);}
  let before_raw=write_blocks(&original,blocks.clone()).unwrap();let before=crate::card::parse_card_bytes_mode(&before_raw,0,false).unwrap();
  extra::append(&mut blocks,0).unwrap();let raw=write_blocks(&original,blocks).unwrap();
  let stamp=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
  let output=PathBuf::from(format!("C:/portable/OpenKoikatsu/Saved/Tests/editor-empty-slot-{stamp}.png"));
  let report=crate::portable::reuse_card_snapshot(&raw,base,&output).unwrap().unwrap();assert_eq!(report["outfits_rebuilt"],0);
  let bytes=crate::gamefs::read(&output).unwrap();let(_,a)=crate::portable::png_snapshot(&original).unwrap();let(_,b)=crate::portable::png_snapshot(&bytes).unwrap();assert_eq!(&original[a.unwrap()],&bytes[b.unwrap()]);
  let after=crate::card::parse_card_bytes_mode(&bytes,0,false).unwrap();assert_eq!(after.accessory.len(),before.accessory.len()+1);assert_eq!(after.accessory[0].parent_key,before.accessory[0].parent_key);for(a,b)in after.accessory[0].add_move.iter().zip(&before.accessory[0].add_move){assert_eq!(a.pos,b.pos);assert_eq!(a.rot,b.rot);assert_eq!(a.scale,b.scale);}assert_eq!(after.shape_body,before.shape_body);assert_eq!(after.coordinate_count,before.coordinate_count);
  for index in 1..before.coordinate_count{let unchanged=crate::card::parse_card_bytes_mode(&bytes,index as i32,false).unwrap();let old=crate::card::parse_card_bytes_mode(&before_raw,index as i32,false).unwrap();assert_eq!(unchanged.accessory.len(),old.accessory.len());}
 }
 #[test]fn duplicate_guid_catalog_uses_the_runtime_resolver_source(){
  let item=|guid:&str,path:&str|Item{key:path.into(),guid:guid.into(),source:path.into(),entry:Default::default()};let mut items=vec![item("",""),item("mod.test","C:/Mods/active.zipmod"),item("mod.test","C:/Mods/inactive.zipmod"),item("mod.other","C:/Mods/other.zipmod")];
  let index=std::collections::HashMap::from([("mod.test".into(),PathBuf::from("c:/mods/active.zipmod")),("mod.other".into(),PathBuf::from("C:/Mods/other.zipmod"))]);retain_active_sources(&mut items,&index);assert_eq!(items.len(),3);assert!(!items.iter().any(|i|i.source.contains("inactive")));assert!(items.iter().any(|i|i.guid=="mod.other"));
 }
 #[test]fn material_reset_scopes_keep_other_slots_and_highlights(){
  let row=|material:&str,prop:&str|Value::Map(vec![("ObjectType".into(),4.into()),("MaterialName".into(),material.into()),("Property".into(),prop.into())]);
  let rows=Value::Array(vec![row("cf_m_eyeline_00_up","MainTex"),row("cf_m_eyeline_00_down","MainTex"),row("cf_m_eyeline_low","MainTex"),row("cf_m_body","MainTex"),row("cf_m_eyeline_00_up","OverTex")]);
  let data=Value::Map(vec![("MaterialTexturePropertyList".into(),Value::Binary(encode(&rows).unwrap()))]);let root=Value::Map(vec![("KK_Plugins.MaterialEditor".into(),Value::Array(vec![1.into(),data]))]);
  let t=targets().into_iter().find(|t|t.key=="eyeline_up").unwrap();let mut decoded=decode(&clear_material_conflicts(&encode(&root).unwrap(),&t,0).unwrap()).unwrap();let Value::Map(map)=&mut decoded else{panic!()};let Value::Array(plugin)=&mut map[0].1 else{panic!()};let raw=at(&mut plugin[1],"MaterialTexturePropertyList").unwrap().as_slice().unwrap();let result=decode(raw).unwrap();assert_eq!(result.as_array().unwrap().len(),3);for name in ["cf_m_eyeline_00_down","cf_m_eyeline_low","cf_m_body"]{assert!(result.as_array().unwrap().iter().any(|v|v.as_map().unwrap().iter().any(|(k,v)|k.as_str()==Some("MaterialName")&&v.as_str()==Some(name))));}
 }
 #[test]fn fused_extended_patch_preserves_serialized_result_for_all_targets(){
  let row=|material:&str,prop:&str|Value::Map(vec![("ObjectType".into(),4.into()),("MaterialName".into(),material.into()),("Property".into(),prop.into())]);
  let properties=Value::Binary(encode(&Value::Array(vec![row("cf_m_eyeline_00_up","MainTex"),row("cf_m_hitomi","OverTex1"),row("cf_m_hitomi","OverTex2"),row("cf_m_body","MainTex")])).unwrap());
  let root=encode(&Value::Map(vec![("unknown.plugin".into(),Value::Binary(vec![7;65537])),("KK_Plugins.MaterialEditor".into(),Value::Array(vec![1.into(),Value::Map(vec![("MaterialTexturePropertyList".into(),properties),("opaqueTextureDictionary".into(),Value::Binary(vec![9;131073]))])]))])).unwrap();
  for t in targets(){
   let mut entry=crate::list::ListEntry::default();entry.id=17;entry.category_no=t.category;entry.name="source".into();
   let item=Item{key:"test".into(),guid:"test.mod".into(),source:"test.zipmod".into(),entry};
   let property=if t.block=="Custom"{t.property.clone()}else{format!("outfit2.{}",t.property)};
   for selection in [None,Some(&item)]{
    let old=clear_material_conflicts(&resolver(&root,&property,selection).unwrap(),&t,2).unwrap();
    let fused=patch_extended_data(&root,&property,selection,&t,2).unwrap();
    assert_eq!(fused,old,"{} changed serialized metadata",t.key);
   }
  }
 }
 #[test]fn registry_unique_and_covers_original_surfaces(){let t=targets();let keys=t.iter().map(|t|&t.key).collect::<std::collections::HashSet<_>>();assert_eq!(keys.len(),t.len());for category in [100,101,102,103,104,105,112,121,130,200,212,400,401,406,408,409,412,413,420,421,422,423,424,430,431,433]{assert!(t.iter().any(|t|t.category==category));}}
 #[test]fn every_target_has_real_card_field(){let p=Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C000.png");if !p.ok_exists(){return;}let original=crate::gamefs::read(p).unwrap();let mut errors=Vec::new();for t in targets(){if let Err(e)=patch(&original,&t,None,0){errors.push(format!("{}: {e}",t.key));}}assert!(errors.is_empty(),"{}",errors.join("\n"));}
 #[test]fn resolver_preserves_other_mods_and_unknown_data(){for key in ["com.bepis.sideloader.universalautoresolver","EC.Core.Sideloader.UniversalAutoResolver"]{let row=Value::Map(vec![("Property".into(),"untouched.property".into()),("ModID".into(),"other.mod".into())]);let v=Value::Map(vec![("unknown.plugin".into(),Value::Binary(vec![1,2,3])),(key.into(),Value::Array(vec![1.into(),Value::Map(vec![("info".into(),Value::Array(vec![Value::Binary(encode(&row).unwrap())]))])]))]);let result=decode(&resolver(&encode(&v).unwrap(),"ChaFileFace.eyebrowId",None).unwrap()).unwrap();assert_eq!(v,result);}}
 #[test]#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]fn sprite_atlas_reads_distinct_original_thumbnails(){let p=&crate::test_support::game_path("/abdata/chara/thumb/mt_eyebrow_00.unity3d");if !p.ok_exists(){return;}let bytes=crate::gamefs::read(p).unwrap();let a=crate::unity::sprite_png_from_bytes(bytes.clone(),"thumb_mayuge_000").expect("first sprite");let b=crate::unity::sprite_png_from_bytes(bytes,"thumb_mayuge_001").expect("second sprite");assert_ne!(a,b);let image=crate::teximg::load_from_memory(&a).unwrap();assert!(image.width()>=100&&image.height()>=100);}
 #[test]fn wrapped_custom_body_and_accessory_reset(){let t=targets().into_iter().find(|t|t.key=="body_detail").unwrap();let v=Value::Map(vec![("body".into(),Value::Map(vec![("detailId".into(),9.into())]))]);let b=encode(&v).unwrap();let mut wrapped=(b.len() as u32).to_le_bytes().to_vec();wrapped.extend(b);let result=custom(&wrapped,&t,2).unwrap();let mut decoded=decode(&result[4..]).unwrap();assert_eq!(at(&mut decoded,"body.detailId").unwrap().as_i64(),Some(2));
 let t=targets().into_iter().find(|t|t.key=="accessory0_123").unwrap();let c=Value::Map(Vec::new());let a=Value::Map(vec![("parts".into(),Value::Array(vec![Value::Map(vec![("id".into(),88.into()),("type".into(),123.into()),("unknown".into(),"keep".into())])]))]);let mut raw=Vec::new();for v in [&c,&a]{let b=encode(v).unwrap();raw.extend((b.len() as u32).to_le_bytes());raw.extend(b);}raw.push(0);let b=encode(&c).unwrap();raw.extend((b.len() as u32).to_le_bytes());raw.extend(b);let result=coordinate(&encode(&Value::Array(vec![Value::Binary(raw)])).unwrap(),&t,0,0,true).unwrap();let v=decode(&result).unwrap();let b=v.as_array().unwrap()[0].as_slice().unwrap();let n=u32::from_le_bytes(b[..4].try_into().unwrap()) as usize;let p=4+n;let n=u32::from_le_bytes(b[p..p+4].try_into().unwrap()) as usize;let mut a=decode(&b[p+4..p+4+n]).unwrap();assert_eq!(at(&mut a,"parts.0.type").unwrap().as_i64(),Some(120));assert_eq!(at(&mut a,"parts.0.unknown").unwrap().as_str(),Some("keep"));}
 #[test]fn legacy_portable_restores_exact_original_mod_metadata(){let root=&crate::test_support::game_path("");let p=Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C000.png");if !root.ok_exists()||!p.ok_exists(){return;}let bytes=crate::gamefs::read(p).unwrap();let cache=Path::new("C:/portable/OpenKoikatsu/Saved/AppearanceEditor/cache");fs::create_dir_all(cache).unwrap();let metadata=original_metadata(root,cache,&bytes).unwrap().expect("legacy metadata restored");let t=targets().into_iter().find(|t|t.key=="eyebrow").unwrap();let result=patch_with_metadata(&bytes,&t,None,0,Some(metadata)).unwrap();let card=crate::card::parse_card_bytes_mode(&result,0,false).unwrap();assert!(card.resolves.len()>10,"original mod references missing");assert!(!card.me_tex.keys().any(|k|k.3.contains("mayuge")&&k.4=="maintex"),"old eyebrow texture override survived");assert!(card.me_tex.keys().any(|k|k.3.contains("body")),"unrelated body textures were removed");assert_eq!(card.eyebrow_id,0);let data=blocks_from_card(&result).unwrap();let kkex=decode(&data["KKEx"]).unwrap();assert!(kkex.as_map().unwrap().iter().any(|(k,_)|k.as_str()==Some(PROVENANCE)));}
 #[test]fn patch_preserves_other_blocks_and_values(){let path=Path::new("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C000.png");if !path.ok_exists(){return;}let original=crate::gamefs::read(path).unwrap();let before=crate::card::parse_card_bytes_mode(&original,0,false).unwrap();let t=targets().into_iter().find(|t|t.key=="eyebrow").unwrap();let item=Item{key:"test".into(),guid:String::new(),source:String::new(),entry:crate::list::ListEntry{id:3,category_no:406,..Default::default()}};let patched=patch(&original,&t,Some(&item),0).unwrap();let after=crate::card::parse_card_bytes_mode(&patched,0,false).unwrap();assert_eq!(after.eyebrow_id,3);assert_eq!(after.shape_face,before.shape_face);assert_eq!(after.shape_body,before.shape_body);assert_eq!(after.clothes_ids,before.clothes_ids);assert_eq!(after.hair_ids,before.hair_ids);assert_eq!(after.pupil_ids,before.pupil_ids);}
}
