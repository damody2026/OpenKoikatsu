//! Typed, card-derived maker properties. Unknown MessagePack members survive edits.
use super::*;
mod plugins;
mod physics;

fn label(key: &str) -> String {
 let mod_labels=[("AccessoryDynamicBoneData","飾品動態骨骼資料"),("AreolaDepth","乳暈深度"),("CenterNipples","乳頭置中"),("Damping","阻尼"),("Depth","深度"),("Elasticity","彈性"),("EnablePushup","啟用內衣塑形"),("Firmness","緊實度"),("FlattenNipples","壓平乳頭"),("HorizontalAngle","水平角度"),("HorizontalPosition","水平位置"),("Lift","托高"),("NippleDepth","乳頭深度"),("NippleWidth","乳頭寬度"),("PushTogether","向內集中"),("Pushup_BodyData","身體塑形資料"),("Pushup_BraData","胸罩塑形資料"),("Pushup_TopData","上衣塑形資料"),("Radius","半徑"),("Roundness","圓潤度"),("Size","大小"),("Softness","柔軟度"),("Squeeze","擠壓"),("Stiffness","剛性"),("UseAdvanced","使用進階設定"),("VerticalAngle","垂直角度"),("VerticalPosition","垂直位置"),("Weight","重量")];
 if let Some((_,v))=mod_labels.iter().find(|(k,_)|*k==key){return v.to_string();}
 let extra=[("areolaSize","乳暈大小"),("bustSoftness","胸部柔軟度"),("bustWeight","胸部重量"),("cheekGlossPower","臉頰光澤強度"),("detailPower","細節強度"),("doubleTooth","虎牙"),("drawAddLine","附加線條"),("eyelineUpWeight","上眼線粗細"),("gradBlend","漸層混合"),("gradOffsetY","漸層垂直位移"),("gradScale","漸層縮放"),("hideBraOpt","隱藏胸罩選項"),("hideOpt","隱藏選項"),("hideShortsOpt","隱藏內褲選項"),("hlDownX","下高光水平位置"),("hlDownY","下高光垂直位置"),("hlUpX","上高光水平位置"),("hlUpY","上高光垂直位置"),("lipGlossPower","嘴唇光澤強度"),("moleLayout","痣配置"),("nailGlossPower","指甲光澤強度"),("nipGlossPower","乳頭光澤強度"),("offset","位移"),("paintLayout","彩繪配置"),("pupilHeight","瞳孔高度"),("pupilWidth","瞳孔寬度"),("pupilX","瞳孔水平位置"),("pupilY","瞳孔垂直位置"),("rotate","旋轉"),("skinGlossPower","皮膚光澤強度"),("tiling","紋理重複"),("MaterialColorPropertyList","材質顏色覆寫"),("MaterialFloatPropertyList","材質數值覆寫"),("Color","顏色"),("Value","設定值"),("ColorOriginal","原始顏色"),("ValueOriginal","原始數值"),("additionalAccessories","額外飾品"),("CoordinateHairAccessories","本套服裝的髮飾"),("HairAccessories","髮飾"),("ColorMatch","跟隨髮色"),("HairGloss","頭髮光澤"),("OutlineColor","輪廓色"),("AccessoryColor","飾品顏色"),("GlossColor","光澤色"),("HairLength","頭髮長度")];
 if let Some((_,v))=extra.iter().find(|(k,_)|*k==key){return v.to_string();}
 let pairs=[("face","臉部"),("body","身體"),("hair","頭髮"),("clothes","衣物"),("accessory","飾品"),("makeup","化妝"),("parts","部位"),("pupil","瞳孔"),("baseMakeup","基礎化妝"),("baseColor","主要顏色"),("subColor","次要顏色"),("startColor","髮根顏色"),("endColor","髮梢顏色"),("outlineColor","輪廓顏色"),("glossColor","光澤顏色"),("acsColor","髮飾顏色"),("eyebrowColor","眉毛顏色"),("eyelineColor","眼線顏色"),("whiteBaseColor","眼白主要顏色"),("whiteSubColor","眼白次要顏色"),("hlUpColor","上高光顏色"),("hlDownColor","下高光顏色"),("moleColor","痣顏色"),("lipLineColor","唇線顏色"),("skinMainColor","皮膚主要顏色"),("skinSubColor","皮膚次要顏色"),("nipColor","乳頭顏色"),("underhairColor","陰毛顏色"),("nailColor","指甲顏色"),("sunburnColor","曬痕顏色"),("cheekColor","腮紅顏色"),("eyeshadowColor","眼影顏色"),("lipColor","嘴唇顏色"),("paintColor","彩繪顏色"),("colorInfo","顏色層"),("color","顏色"),("patternColor","花紋顏色"),("pattern","花紋"),("parentKey","綁定骨架"),("addMove","變形組"),("noShake","停止擺動"),("pos","位置"),("rot","旋轉"),("scl","縮放"),("length","長度"),("x","橫軸"),("y","縱軸"),("z","深度"),("r","紅"),("g","綠"),("b","藍"),("a","透明度")];
 pairs.iter().find(|(k,_)|*k==key).map(|(_,v)|v.to_string()).unwrap_or_else(||key.parse::<usize>().map(|n|format!("{}",n+1)).unwrap_or_else(|_|format!("進階設定（{key}）")))
}

/// Mutate one decoded section at a time; retain section prefixes and trailing bytes.
fn sections(blocks: &mut BTreeMap<String,Vec<u8>>, outfit: usize, mut f: impl FnMut(&str,&mut Value)->Result<(),String>) -> Result<(),String> {
 let raw=blocks.get("Custom").ok_or("缺少外觀區塊")?.clone();
 if let Some(mut v)=decode(&raw).ok().filter(Value::is_map) {
  for name in ["face","body","hair"] {f(name,at(&mut v,name)?)?;}
  blocks.insert("Custom".into(),encode(&v)?);
 }
 else {
  let mut pos=0;let mut out=Vec::new();let mut i=0;
  while pos<raw.len() {let n=u32::from_le_bytes(raw.get(pos..pos+4).ok_or("外觀長度截斷")?.try_into().unwrap()) as usize;pos+=4;let mut v=decode(raw.get(pos..pos+n).ok_or("外觀資料截斷")?)?;pos+=n;
   if i==0 && at(&mut v,"face").is_ok(){for name in ["face","body","hair"]{f(name,at(&mut v,name)?)?;}}
   else {let name=["face","body","hair"].get(i).ok_or("未知外觀區段")?;f(name,&mut v)?;}
   let b=encode(&v)?;out.extend((b.len() as u32).to_le_bytes());out.extend(b);i+=1;
  }blocks.insert("Custom".into(),out);
 }
 let mut v=decode(blocks.get("Coordinate").ok_or("缺少服裝區塊")?)?;
 let slot=match &mut v {Value::Array(a)=>a.get_mut(outfit).ok_or("服裝索引超出範圍")?,v=>v};
 let Value::Binary(bin)=slot else{return Err("服裝資料格式錯誤".into())};let mut pos=0;let mut out=Vec::new();
 for section in ["clothes","accessory","makeup"] {if section=="makeup" {out.push(*bin.get(pos).ok_or("化妝旗標截斷")?);pos+=1;}
  let n=u32::from_le_bytes(bin.get(pos..pos+4).ok_or("服裝長度截斷")?.try_into().unwrap()) as usize;pos+=4;
  if n==0 {out.extend(0u32.to_le_bytes());continue;}
  let mut data=decode(bin.get(pos..pos+n).ok_or("服裝資料截斷")?)?;pos+=n;f(section,&mut data)?;let b=encode(&data)?;out.extend((b.len() as u32).to_le_bytes());out.extend(b);
 }out.extend(&bin[pos..]);*bin=out;blocks.insert("Coordinate".into(),encode(&v)?);Ok(())
}

fn walk(v:&Value,path:&str,labels:&str,color:bool,out:&mut Vec<J>) {
 let key=path.rsplit('.').next().unwrap_or("");
 if matches!(key,"shapeValueBody"|"shapeValueFace"|"version") {return;}
 if key=="addMove" {if let Some(a)=v.as_array(){
  if let Some(flat)=a.get(2).and_then(Value::as_array).filter(|a|a.len()==6){for(i,vector)in flat.iter().enumerate(){let base=format!("{labels}／第 {} 組／{}",i/3+1,["位置（公分）","旋轉（度）","縮放"][i%3]);if let Some(axis)=vector.as_array(){for(j,v)in axis.iter().take(3).enumerate(){walk(v,&format!("{path}.2.{i}.{j}"),&format!("{base}／{}",["橫軸","縱軸","深度"][j]),false,out)}}}return;}
 }}
 let color=color||(key!="colorInfo"&&key.to_ascii_lowercase().contains("color"));
 match v {
  Value::Map(m)=>for(k,v)in m {if let Some(k)=k.as_str(){walk(v,&format!("{path}.{k}"),&format!("{labels}／{}",label(k)),color,out)}},
  Value::Array(a)=>for(i,v)in a.iter().enumerate(){let component=if color&&a.len()==4{["紅","綠","藍","透明度"][i].to_string()}else{label(&i.to_string())};walk(v,&format!("{path}.{i}"),&format!("{labels}／{component}"),color,out)},
  Value::F32(n)=>out.push(json!({"key":path,"label":labels,"kind":if color{"color"}else{"number"},"value":n})),
  Value::F64(n)=>out.push(json!({"key":path,"label":labels,"kind":if color{"color"}else{"number"},"value":n})),
  Value::Boolean(n)=>out.push(json!({"key":path,"label":labels,"kind":"bool","value":n})),
  Value::String(s) if key=="parentKey"=>out.push(json!({"key":path,"label":labels,"kind":"parent","value":s.as_str()})),
  _=>{}
 }
}
pub(super) fn catalog_properties(original:&[u8],outfit:usize)->Result<Vec<J>,String>{
 let mut blocks=blocks_from_card(original)?;let mut props=physics::catalog(original,outfit)?;sections(&mut blocks,outfit,|name,v|{walk(v,name,&label(name),false,&mut props);Ok(())})?;plugins::catalog(&blocks,outfit,&mut props)?;Ok(props)
}
pub(super) fn patch_properties(original:&[u8],outfit:usize,edits:&[J])->Result<Vec<u8>,String>{
 if edits.is_empty()||edits.len()>8192{return Err("修改清單數量錯誤".into());}
 let (edits,physics_edits)=physics::take_edits(edits);
 if edits.is_empty(){return physics::apply(original,original,outfit,&physics_edits);}
 let edits=&edits[..];
 let allowed=catalog_properties(original,outfit)?;let mut changes=BTreeMap::new();
 for edit in edits {let key=text(edit,"key")?;let p=allowed.iter().find(|p|p["key"]==key).ok_or("找不到可編輯欄位")?;
  if changes.contains_key(key){return Err("修改清單含重複欄位".into());}
  let value=match p["kind"].as_str().unwrap_or("") {
   "bool"=>Value::Boolean(edit["value"].as_bool().ok_or("布林值格式錯誤")?),
   "parent"=>{let s=edit["value"].as_str().ok_or("骨架名稱格式錯誤")?;if s.len()>256||s.chars().any(char::is_control){return Err("骨架名稱不合法".into());}s.into()},
   kind=>{let n=edit["value"].as_f64().ok_or("數值格式錯誤")?;if !n.is_finite()||n.abs()>1e6||(kind=="color"&&!(0.0..=1.0).contains(&n)){return Err("數值超出可用範圍".into());}Value::F32(n as f32)}
  };changes.insert(key.to_string(),value);
 }
 let mut blocks=blocks_from_card(original)?;
 sections(&mut blocks,outfit,|name,v|{for(key,value)in &changes {if let Some(path)=key.strip_prefix(&format!("{name}.")){*at(v,path)?=value.clone();}}Ok(())})?;
 plugins::patch(&mut blocks,&changes,outfit)?;
 let card=super::write_blocks(original,blocks)?;
 if physics_edits.is_empty(){Ok(card)}else{physics::apply(original,&card,outfit,&physics_edits)}
}

#[cfg(test)] mod tests {
 use super::*;
 #[test]#[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]fn lean_card_property_request_recovers_original_mod_controls(){
  let root=&crate::test_support::game_path("");let card="C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C000.png";
  let base=catalog_properties(&crate::gamefs::read(card).unwrap(),0).unwrap();
  let path=std::env::temp_dir().join(format!("ok-properties-{}-{}.json",std::process::id(),std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
  fs::write(&path,serde_json::to_vec(&json!({"action":"properties","card":card,"outfit":0,"cache":"C:/portable/OpenKoikatsu/Saved/AppearanceEditor/cache"})).unwrap()).unwrap();
  let result=super::super::request(root,&path).unwrap();let props=result["properties"].as_array().unwrap();assert!(props.len()>base.len());assert!(props.iter().any(|p|p["key"].as_str().is_some_and(|k|k.starts_with("plugins."))));assert_eq!(result["parents"].as_array().unwrap().len(),55);assert!(props.iter().all(|p|!p["label"].as_str().unwrap().contains("進階設定（")));
  fs::write(format!("C:/portable/OpenKoikatsu/Saved/Tests/EditorPropertiesRecoveredManifest-{}.json",std::process::id()),serde_json::to_vec_pretty(&result).unwrap()).unwrap();let _=fs::remove_file(path);
 }
 #[test]fn properties_real_card_roundtrip_and_untouched_sections(){
  let original=crate::gamefs::read("C:/portable/OpenKoikatsu/Content/CharacterCards/CrowdAPNG/C000.png").unwrap();
  let props=catalog_properties(&original,0).unwrap();assert!(props.len()>300);
  assert!(props.iter().any(|p|p["key"]=="clothes.parts.0.colorInfo.0.tiling.0"&&p["kind"]=="number"));
  for key in ["hair.parts.0.baseColor.0","face.eyelineColor.0","face.pupil.0.baseColor.0","accessory.parts.0.parentKey"]{assert!(props.iter().any(|p|p["key"]==key),"missing {key}");}
  let edits=vec![json!({"key":"hair.parts.0.baseColor.0","value":0.123}),json!({"key":"face.eyelineColor.0","value":0.234}),json!({"key":"accessory.parts.0.parentKey","value":"a_n_head"})];
  let changed=patch_properties(&original,0,&edits).unwrap();let result=catalog_properties(&changed,0).unwrap();
  for p in &props {let next=result.iter().find(|n|n["key"]==p["key"]).unwrap();if let Some(e)=edits.iter().find(|e|e["key"]==p["key"]){if let Some(v)=e["value"].as_f64(){assert!((next["value"].as_f64().unwrap()-v).abs()<1e-6)}else{assert_eq!(next["value"],e["value"]);}}else{assert_eq!(next,p);}}
  let before=blocks_from_card(&original).unwrap();let after=blocks_from_card(&changed).unwrap();for(name,bytes)in before{if !matches!(name.as_str(),"Custom"|"Coordinate"){assert_eq!(after[&name],bytes);}}
  assert!(patch_properties(&original,0,&[json!({"key":"hair.parts.0.id","value":9})]).is_err());
  assert!(patch_properties(&original,0,&[json!({"key":"hair.parts.0.baseColor.0","value":2})]).is_err());
 }
}
