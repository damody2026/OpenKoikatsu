//! MoreAccessories slots retain their original XML order and resolver identity.
use super::*;
fn data(v:&mut Value)->Option<&mut Value>{match v{Value::Array(a)=>a.get_mut(1),v=>Some(v)}}
fn xml(v:&mut Value)->Option<&mut Value>{let Value::Map(m)=v else{return None};for(k,v)in m{if k.as_str().is_some_and(|s|s.eq_ignore_ascii_case("moreAccessories")){let Value::Map(m)=data(v)? else{return None};return m.iter_mut().find(|(k,_)|k.as_str()==Some("additionalAccessories")).map(|(_,v)|v);}}None}
pub(super) fn append(blocks:&mut BTreeMap<String,Vec<u8>>,outfit:usize)->Result<(),String>{
 let mut v=if let Some(b)=blocks.get("KKEx"){decode(b)?}else{Value::Map(Vec::new())};
 if xml(&mut v).is_none(){let Value::Map(m)=&mut v else{return Err("模組資料格式錯誤".into())};if m.iter().any(|(k,_)|k.as_str().is_some_and(|s|s.eq_ignore_ascii_case("moreAccessories"))){return Err("既有額外飾品資料格式無法安全擴充".into())}m.push(("moreAccessories".into(),Value::Array(vec![1.into(),Value::Map(vec![("additionalAccessories".into(),"<additionalAccessories version=\"2.0.21.1\"></additionalAccessories>".into())])])));}
 let value=xml(&mut v).ok_or("無法建立額外飾品資料")?;let s=value.as_str().ok_or("額外飾品 XML 格式錯誤")?;let doc=roxmltree::Document::parse(s).map_err(|e|e.to_string())?;
 let set=doc.descendants().find(|n|n.has_tag_name("accessorySet")&&n.attribute("type").and_then(|v|v.parse::<usize>().ok())==Some(outfit));
 let mut s=s.to_string();if let Some(n)=set{if n.children().filter(|n|n.has_tag_name("accessory")).count()>=4096{return Err("額外飾品欄位超過安全上限".into())}let range=n.range();let raw=&s[range.clone()];if raw.trim_end().ends_with("/>"){let end=range.end-raw.len()+raw.rfind("/>").ok_or("服裝節點格式錯誤")?;s.replace_range(end..range.end,"><accessory type=\"120\" /></accessorySet>");}else{let pos=range.start+raw.rfind("</").ok_or("服裝節點格式錯誤")?;s.insert_str(pos,"<accessory type=\"120\" />");}}else{let root=doc.root_element();let range=root.range();let raw=&s[range.clone()];let node=format!("<accessorySet type=\"{outfit}\"><accessory type=\"120\" /></accessorySet>");if raw.trim_end().ends_with("/>"){let end=range.start+raw.rfind("/>").ok_or("額外飾品根節點格式錯誤")?;s.replace_range(end..range.end,&format!(">{node}</additionalAccessories>"));}else{let pos=range.start+raw.rfind("</").ok_or("額外飾品根節點格式錯誤")?;s.insert_str(pos,&node);}}
 *value=s.into();blocks.insert("KKEx".into(),encode(&v)?);Ok(())
}
pub(super) fn targets_for(blocks:&BTreeMap<String,Vec<u8>>,outfit:usize)->Result<Vec<Target>,String>{
 let mut out=targets();let Some(b)=blocks.get("KKEx")else{return Ok(out)};let mut v=decode(b)?;let Some(s)=xml(&mut v).and_then(|v|v.as_str())else{return Ok(out)};
 let doc=roxmltree::Document::parse(s).map_err(|e|e.to_string())?;let count=doc.descendants().filter(|n|n.has_tag_name("accessorySet")&&n.attribute("type").and_then(|v|v.parse::<usize>().ok())==Some(outfit)).flat_map(|n|n.children().filter(|n|n.has_tag_name("accessory"))).count();
 if count>4096{return Err("額外飾品欄位超過安全上限".into())}
 for i in 20..20+count{for template in targets().into_iter().filter(|t|t.section=="accessory"&&t.path=="parts.0.id"){let mut t=template;t.key=format!("accessory{i}_{}",t.category);t.label=t.label.replacen("配件欄 1：",&format!("額外配件欄 {}：",i+1),1);t.block="KKEx".into();t.section="accessory_extra".into();t.path=format!("parts.{i}.id");t.property=format!("accessory{i}.ChaFileAccessory.PartsInfo.id");out.push(t);}}Ok(out)
}
pub(super) fn patch(bytes:&[u8],t:&Target,item:Option<&Item>,outfit:usize)->Result<Vec<u8>,String>{
 let mut v=decode(bytes)?;let value=xml(&mut v).ok_or("缺少額外飾品資料")?;let s=value.as_str().ok_or("額外飾品 XML 格式錯誤")?;let doc=roxmltree::Document::parse(s).map_err(|e|e.to_string())?;
 let index=t.path.split('.').nth(1).and_then(|s|s.parse::<usize>().ok()).and_then(|i|i.checked_sub(20)).ok_or("額外飾品欄位錯誤")?;
 let node=doc.descendants().find(|n|n.has_tag_name("accessorySet")&&n.attribute("type").and_then(|v|v.parse::<usize>().ok())==Some(outfit)).ok_or("缺少本套服裝額外飾品")?.children().filter(|n|n.has_tag_name("accessory")).nth(index).ok_or("額外飾品欄位不存在")?;
 let mut attrs:BTreeMap<String,String>=node.attributes().map(|a|(a.name().into(),a.value().into())).collect();
 attrs.insert("type".into(),item.map(|i|i.entry.category_no).unwrap_or(120).to_string());attrs.insert("id".into(),item.map(|i|i.entry.id).unwrap_or(0).to_string());
 attrs.entry("parentKey".into()).or_insert_with(||item.map(|i|i.entry.parent.clone()).unwrap_or_default());attrs.entry("hideCategory".into()).or_insert("0".into());attrs.entry("noShake".into()).or_insert("false".into());
 for g in 0..2{for k in 0..3{for axis in ["x","y","z"]{attrs.entry(format!("addMove{g}{k}{axis}")).or_insert(if k==2{"1"}else{"0"}.into());}}}
 for c in 0..4{for channel in ["r","g","b","a"]{attrs.entry(format!("color{c}{channel}")).or_insert("1".into());}}
 let mut replacement="<accessory".to_string();for(k,v)in attrs{replacement.push_str(&format!(" {k}=\"{}\"",v.replace('&',"&amp;").replace('"',"&quot;").replace('<',"&lt;")));}replacement.push_str(" />");let range=node.range();let mut s=s.to_string();s.replace_range(range,&replacement);*value=s.into();encode(&v)
}
#[cfg(test)]mod tests{use super::*;
 #[test]fn new_extra_slots_preserve_order_and_other_outfits(){let mut b=BTreeMap::new();append(&mut b,0).unwrap();append(&mut b,1).unwrap();append(&mut b,0).unwrap();assert_eq!(targets_for(&b,0).unwrap().len(),targets().len()+20);assert_eq!(targets_for(&b,1).unwrap().len(),targets().len()+10);assert_eq!(targets_for(&b,2).unwrap().len(),targets().len());}
 #[test]fn extra_slot_selection_keeps_outfits_unknown_fields_and_stable_indices(){
 let fixture="<additionalAccessories><accessorySet type=\"0\"><accessory type=\"120\" unknown=\"keep\" /></accessorySet><accessorySet type=\"1\"><accessory type=\"120\" /></accessorySet></additionalAccessories>";
 let raw=encode(&Value::Map(vec![("moreAccessories".into(),Value::Array(vec![1.into(),Value::Map(vec![("additionalAccessories".into(),fixture.into())])]))])).unwrap();let blocks=BTreeMap::from([("KKEx".into(),raw.clone())]);let all=targets_for(&blocks,0).unwrap();assert_eq!(all.len(),targets().len()+10);let t=all.iter().find(|t|t.key=="accessory20_122").unwrap();let item=Item{key:"fixture".into(),guid:"test.mod".into(),source:"test.zipmod".into(),entry:crate::list::ListEntry{category_no:122,id:123,parent:"a_n_headtop".into(),..Default::default()}};let changed=patch(&raw,t,Some(&item),0).unwrap();let mut v=decode(&changed).unwrap();let text=xml(&mut v).unwrap().as_str().unwrap();assert!(text.contains("unknown=\"keep\""));assert!(text.contains("addMove02x=\"1\""));assert!(text.contains("type=\"122\""));assert!(text.contains("id=\"123\""));assert!(text.contains("parentKey=\"a_n_headtop\""));assert!(text.contains("<accessorySet type=\"1\"><accessory type=\"120\" /></accessorySet>"));assert_eq!(targets_for(&BTreeMap::from([("KKEx".into(),changed)]),0).unwrap().len(),all.len());
 }
}
