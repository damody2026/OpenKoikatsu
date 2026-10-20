use super::*;

fn unpack(b:&[u8])->Option<Value>{if b.len()>16*1024*1024{return None;}let mut c=Cursor::new(b);let v=rmpv::decode::read_value(&mut c).ok()?;((c.position() as usize==b.len())&&(v.is_map()||v.is_array())).then_some(v)}
fn relevant(name:&str)->bool{let n=name.to_ascii_lowercase();["materialeditor","hairaccessorycustomizer","hairshadowcolor","pushup","dynamicboneeditor","moreaccessories","accessorystates","overlay"].iter().any(|s|n.contains(s))}
fn title(name:&str)->&'static str{let n=name.to_ascii_lowercase();if n.contains("moreaccessories"){"額外飾品"}else if n.contains("materialeditor"){"材質編輯器"}else if n.contains("hairaccessorycustomizer"){"髮飾進階設定"}else if n.contains("pushup"){"內衣塑形"}else if n.contains("dynamicboneeditor"){"骨骼物理"}else if n.contains("hairshadowcolor"){"頭髮陰影色"}else if n.contains("accessorystates"){"飾品狀態"}else{"覆蓋貼圖"}}
fn xml_rows(xml:&str,outfit:usize)->Result<Vec<(String,String,String,std::ops::Range<usize>)>,String>{
 let doc=roxmltree::Document::parse(xml).map_err(|e|e.to_string())?;let mut out=Vec::new();let mut slot=20;
 for node in doc.descendants().filter(|n|n.has_tag_name("accessory")&&n.ancestors().any(|a|a.has_tag_name("accessorySet")&&a.attribute("type").and_then(|s|s.parse::<usize>().ok())==Some(outfit))){
  for a in node.attributes(){let n=a.name();let label=if n=="parentKey"{"綁定骨架".into()}else if n=="noShake"{"停止擺動".into()}else if let Some(k)=n.strip_prefix("color"){if k.len()!=2{continue;}let c=match &k[1..]{"r"=>"紅","g"=>"綠","b"=>"藍","a"=>"透明度",_=>continue};format!("顏色層 {}／{c}",k[..1].parse::<usize>().unwrap_or(0)+1)}else if let Some(k)=n.strip_prefix("addMove"){if k.len()!=3{continue;}let group=k[..1].parse::<usize>().map_err(|_|"變形組錯誤")?;let ty=k[1..2].parse::<usize>().map_err(|_|"變形種類錯誤")?;if group>1||ty>2{continue;}let axis=match &k[2..]{"x"=>"橫軸","y"=>"縱軸","z"=>"深度",_=>continue};format!("第 {} 組／{}／{axis}",group+1,["位置（公分）","旋轉（度）","縮放"][ty])}else{continue;};
   out.push((format!("{slot}.{n}"),format!("飾品欄 {}／{label}",slot+1),a.value().to_string(),a.range_value()));
  }slot+=1;
 }Ok(out)
}
fn visit(v:&Value,path:&str,labels:&str,color:bool,outfit:usize,out:&mut Vec<J>){
 match v{
  // MaterialEditor keeps every outfit's clothes (ObjectType 1) and accessory (2) entries in one list: only the edited
  // outfit's can change the preview, so the others are not offered (an edit there showed no change at all).
  Value::Map(m) if {let int=|key:&str|m.iter().find(|(k,_)|k.as_str()==Some(key)).and_then(|(_,v)|v.as_i64());matches!(int("ObjectType"),Some(1|2))&&int("CoordinateIndex").is_some_and(|c|c!=outfit as i64)}=>{},
  Value::Map(m)=>{let mut context=labels.to_string();for(key,title)in [("MaterialName","材質"),("RendererName","網格"),("Property","屬性")]{if let Some(s)=m.iter().find(|(k,_)|k.as_str()==Some(key)).and_then(|(_,v)|v.as_str()){context.push_str(&format!("／{title} {s}"));}}for(i,(k,v))in m.iter().enumerate(){let name=k.as_str().map(str::to_string).unwrap_or_else(||k.to_string());if name.to_ascii_lowercase().contains("texture")||name.ends_with("Original") {continue;}visit(v,&format!("{path}.m{i}"),&format!("{context}／{}",label(&name)),color||name.to_ascii_lowercase().contains("color"),outfit,out)}},
  Value::Array(a)=>for(i,v)in a.iter().enumerate(){visit(v,&format!("{path}.a{i}"),&format!("{labels}／{}",if color&&a.len()==4{["紅","綠","藍","透明度"][i].into()}else{format!("{}",i+1)}),color,outfit,out)},
  Value::Binary(b)=>if let Some(v)=unpack(b){visit(&v,&format!("{path}.packed"),labels,color,outfit,out)},
  Value::String(s)=>if let Some(xml)=s.as_str().filter(|s|s.trim_start().starts_with('<')){if let Ok(rows)=xml_rows(xml,outfit){for(key,label,raw,_)in rows {let (kind,value)=if key.ends_with("parentKey"){("parent",json!(raw))}else if key.ends_with("noShake"){("bool",json!(raw.eq_ignore_ascii_case("true")))}else{let Ok(n)=raw.parse::<f64>()else{continue};(if key.contains("color"){"color"}else{"number"},json!(n))};out.push(json!({"key":format!("{path}.xml.{key}"),"label":format!("{labels}／{label}"),"kind":kind,"value":value}));}}},
  Value::F32(n)=>out.push(json!({"key":path,"label":labels,"kind":if color{"color"}else{"number"},"value":n})),
  Value::F64(n)=>out.push(json!({"key":path,"label":labels,"kind":if color{"color"}else{"number"},"value":n})),
  Value::Boolean(n)=>out.push(json!({"key":path,"label":labels,"kind":"bool","value":n})),
  _=>{}
 }
}
pub(super)fn catalog(blocks:&BTreeMap<String,Vec<u8>>,outfit:usize,out:&mut Vec<J>)->Result<(),String>{
 if let Some(b)=blocks.get("KKEx"){let v=decode(b)?;if let Some(m)=v.as_map(){for(i,(k,v))in m.iter().enumerate(){if let Some(name)=k.as_str().filter(|n|relevant(n)){visit(v,&format!("plugins.m{i}"),title(name),false,outfit,out)}}}}Ok(())
}
fn replace(v:&mut Value,path:&str,value:&Value,outfit:usize)->Result<(),String>{
 if path.is_empty(){*v=value.clone();return Ok(());}
 let (head,rest)=path.split_once('.').unwrap_or((path,""));
 if head=="packed" {let b=v.as_slice().ok_or("模組二進位格式錯誤")?;let mut decoded=unpack(b).ok_or("模組資料無法解碼")?;replace(&mut decoded,rest,value,outfit)?;*v=Value::Binary(encode(&decoded)?);return Ok(());}
 if head=="xml" {let xml=v.as_str().ok_or("模組 XML 格式錯誤")?;let rows=xml_rows(xml,outfit)?;let (_,_,_,range)=rows.into_iter().find(|r|r.0==rest).ok_or("找不到額外飾品欄位")?;let raw=match value {Value::String(s)=>s.as_str().ok_or("骨架字串錯誤")?.to_string(),Value::Boolean(b)=>b.to_string(),_=>value.to_string()};let escaped=raw.replace('&',"&amp;").replace('<',"&lt;").replace('"',"&quot;").replace('\'',"&apos;");let mut xml=xml.to_string();xml.replace_range(range,&escaped);*v=xml.into();return Ok(());}
 let (kind,index)=head.split_at(1);let index=index.parse::<usize>().map_err(|_|"模組路徑錯誤")?;
 let child=match v {Value::Map(m) if kind=="m"=>m.get_mut(index).map(|(_,v)|v),Value::Array(a) if kind=="a"=>a.get_mut(index),_=>None}.ok_or("模組欄位不存在")?;replace(child,rest,value,outfit)
}
pub(super)fn patch(blocks:&mut BTreeMap<String,Vec<u8>>,changes:&BTreeMap<String,Value>,outfit:usize)->Result<(),String>{
 if !changes.keys().any(|k|k.starts_with("plugins.")){return Ok(());}
 let mut v=decode(blocks.get("KKEx").ok_or("缺少模組資料")?)?;for(k,val)in changes{if let Some(path)=k.strip_prefix("plugins."){replace(&mut v,path,val,outfit)?;}}blocks.insert("KKEx".into(),encode(&v)?);Ok(())
}
#[cfg(test)]mod tests{use super::*;
 #[test]fn material_editor_rows_of_other_outfits_are_not_offered(){
  let entry=|ot:i64,coord:i64,mat:&str|Value::Map(vec![("ObjectType".into(),ot.into()),("CoordinateIndex".into(),coord.into()),("Slot".into(),0.into()),("MaterialName".into(),mat.into()),("Property".into(),"ShadowColor".into()),("Value".into(),Value::Array(vec![Value::F32(0.5),Value::F32(0.5),Value::F32(0.5),Value::F32(1.)]))]);
  let colors=Value::Array(vec![entry(2,4,"other_outfit"),entry(2,0,"this_outfit"),entry(1,3,"other_clothes"),entry(3,4,"hair_any_outfit")]);
  let data=Value::Map(vec![("MaterialColorPropertyList".into(),Value::Binary(encode(&colors).unwrap()))]);
  let root=Value::Map(vec![("KK_Plugins.MaterialEditor".into(),Value::Array(vec![1.into(),data]))]);let blocks=BTreeMap::from([("KKEx".into(),encode(&root).unwrap())]);
  let mut props=Vec::new();catalog(&blocks,0,&mut props).unwrap();
  let labels:Vec<&str>=props.iter().map(|p|p["label"].as_str().unwrap()).collect();
  assert!(labels.iter().any(|l|l.contains("this_outfit"))&&labels.iter().any(|l|l.contains("hair_any_outfit")));
  assert!(!labels.iter().any(|l|l.contains("other_outfit")||l.contains("other_clothes")));
  // Kept rows keep their list index, so an edit still lands on its own entry.
  assert!(props.iter().filter(|p|p["label"].as_str().unwrap().contains("this_outfit")).all(|p|p["key"].as_str().unwrap().contains(".packed.a1.")));
 }
 #[test]fn packed_material_colors_keep_unknown_fields_and_texture_bytes(){
  let colors=Value::Array(vec![Value::Map(vec![("Color".into(),Value::Array(vec![Value::F32(0.2),Value::F32(0.3),Value::F32(0.4),Value::F32(1.)])),("Unknown".into(),"keep".into())])]);
  let data=Value::Map(vec![("MaterialColorPropertyList".into(),Value::Binary(encode(&colors).unwrap())),("MaterialTexturePropertyList".into(),Value::Binary(vec![0,1,2,3]))]);
  let root=Value::Map(vec![("KK_Plugins.MaterialEditor".into(),Value::Array(vec![1.into(),data]))]);let mut blocks=BTreeMap::from([("KKEx".into(),encode(&root).unwrap())]);let mut props=Vec::new();catalog(&blocks,0,&mut props).unwrap();assert_eq!(props.len(),4);let key=props[0]["key"].as_str().unwrap().to_string();patch(&mut blocks,&BTreeMap::from([(key,Value::F32(0.7))]),0).unwrap();
  let next=decode(&blocks["KKEx"]).unwrap();let plugin=&next.as_map().unwrap()[0].1.as_array().unwrap()[1];let m=plugin.as_map().unwrap();assert_eq!(m[1].1,Value::Binary(vec![0,1,2,3]));let decoded=decode(m[0].1.as_slice().unwrap()).unwrap();assert_eq!(decoded.as_array().unwrap()[0].as_map().unwrap()[1].1,Value::from("keep"));assert_eq!(decoded.as_array().unwrap()[0].as_map().unwrap()[0].1.as_array().unwrap()[0],Value::F32(0.7));
 }
 #[test]fn extra_accessory_xml_preserves_unknown_attributes_and_other_outfits(){
  let xml="<root><accessorySet type=\"0\"><accessory parentKey=\"a_n_head\" color0r=\"0.5\" addMove01x=\"0\" unknown=\"keep\" /></accessorySet><accessorySet type=\"1\"><accessory color0r=\"0.9\" /></accessorySet></root>";
  let mut v=Value::from(xml);replace(&mut v,"xml.20.color0r",&Value::F32(0.25),0).unwrap();replace(&mut v,"xml.20.addMove01x",&Value::F32(90.),0).unwrap();let s=v.as_str().unwrap();assert!(s.contains("color0r=\"0.25\""));assert!(s.contains("unknown=\"keep\""));assert!(s.contains("color0r=\"0.9\""));assert!(s.contains("addMove01x=\"90\""));
 }
}
