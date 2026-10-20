use std::path::Path;
fn batch_shared(game:&Path,list:&Path,resources:&Path,output:&Path)->Result<serde_json::Value,String>{
    batch_shared_range(game,list,resources,output,0,usize::MAX,None)
}
pub fn batch_shared_range(game:&Path,list:&Path,resources:&Path,output:&Path,start:usize,end:usize,publish:Option<&Path>)->Result<serde_json::Value,String>{
    use sha2::{Digest,Sha256};
    std::fs::create_dir_all(output).map_err(|e|e.to_string())?;
    let input=std::fs::read_to_string(list).map_err(|e|e.to_string())?;
    let cards:Vec<_>=input.lines().filter(|s|!s.trim().is_empty()).collect();
    let mut records=Vec::new();
    for (index,card) in cards.iter().enumerate().skip(start).take(end.saturating_sub(start)){
        let source=Path::new(card);let bytes=std::fs::read(source).map_err(|e|e.to_string())?;
        let hash=format!("{:x}",Sha256::digest(&bytes));
        let png=output.join(format!("C{index:03}.png"));let receipt=output.join(format!("C{index:03}.conversion.json"));
        if png.exists(){
            let saved:serde_json::Value=serde_json::from_slice(&std::fs::read(&receipt).map_err(|_|"Existing card has no completed conversion receipt; preserve it and repair before resuming")?).map_err(|e|e.to_string())?;
            if saved["source_sha256"]!=hash{return Err(format!("source changed at index {index}; choose a fresh output"));}
            records.push(saved);continue;
        }
        eprintln!("APNG portable preparation {}/{} {}",index+1,cards.len(),card);
        let mut report=ok_extract::portable::shared(game,source,resources,&png)?;
        if let Some(root)=publish {
            std::fs::create_dir_all(root.join("assets")).map_err(|e|e.to_string())?;
            for asset in std::fs::read_dir(resources.join("assets")).map_err(|e|e.to_string())? {
                let asset=asset.map_err(|e|e.to_string())?;let target=root.join("assets").join(asset.file_name());
                if !target.exists(){if let Err(error)=std::fs::hard_link(asset.path(),&target){if error.kind()!=std::io::ErrorKind::AlreadyExists{return Err(error.to_string());}}}
            }
            report["worker_resources"]=serde_json::json!(resources);report["builtin_directory"]=serde_json::json!(root);
        }
        report["source"]=serde_json::json!(card);report["index"]=serde_json::json!(index);report["source_sha256"]=serde_json::json!(hash);
        std::fs::write(&receipt,serde_json::to_vec_pretty(&report).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
        records.push(report);
    }
    Ok(serde_json::json!({"selected":cards.len(),"range_start":start,"range_end":end.min(cards.len()),"prepared":records.len(),"complete":true,"cards":records}))
}
fn main() {
    let args: Vec<String> = std::env::args().collect();
    let result = match args.get(1).map(String::as_str) {
        Some("upgrade-prepared-clothing-masks") if args.len()==4 => ok_extract::portable::upgrade_prepared_clothing_masks(Path::new(&args[2]),Path::new(&args[3])),
        Some("repair-clothing-mask-library") if args.len()==5 => ok_extract::portable::repair_clothing_mask_library(Path::new(&args[2]),Path::new(&args[3]),Path::new(&args[4])),
        Some("repair-clothing-masks") if args.len()==5 => ok_extract::portable::repair_clothing_masks(Path::new(&args[2]),Path::new(&args[3]),Path::new(&args[4])),
        Some("prepare-reader-library") if args.len()==4 => ok_extract::portable::prepare_reader_library(Path::new(&args[2]),Path::new(&args[3])),
        Some("requirements") if args.len()==3 => ok_extract::standalone_card_requirements(Path::new(&args[2]),0),
        Some("export") if args.len()==5 => ok_extract::portable::export(Path::new(&args[2]),Path::new(&args[3]),Path::new(&args[4])),
        Some("resolve-editor-demo") if args.len()==5 => (|| {
            let mut rows:Vec<serde_json::Value>=serde_json::from_slice(&std::fs::read(&args[3]).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
            let mut labels=std::collections::HashMap::new();
            let mut missing=Vec::new();
            for row in &mut rows {
                let source=row["source"].as_str().ok_or("Missing source")?.to_string();
                let names=labels.entry(source.clone()).or_insert_with(||ok_extract::clip_names_for_source(Path::new(&args[2]),&source));
                let suffix=format!(".{}",row["state"].as_str().ok_or("Missing state")?);
                let mut matches=names.iter().filter(|name|name.ends_with(&suffix)).collect::<Vec<_>>();
                if matches.is_empty() {
                    // Older demo lists omit the height prefix used by newer
                    // H controllers. Resolve only their authored medium state.
                    let medium=format!(".M_{}",row["state"].as_str().unwrap());
                    matches=names.iter().filter(|name|name.ends_with(&medium)).collect();
                    if matches.len()==1 {row["state_alias"]=medium.into();}
                }
                if matches.len()==1 {row["key"]=format!("{}|{}",source,matches[0]).into();}
                else {row["unresolved_candidates"]=serde_json::json!(names);missing.push(row["id"].clone());}
            }
            let result=serde_json::json!({"schema":1,"demos":rows,"missing":missing,"valid":missing.is_empty()});
            std::fs::write(&args[4],serde_json::to_vec_pretty(&result).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
            if !missing.is_empty(){return Err(format!("Unresolved demos: {}",missing.len()));}
            Ok(serde_json::json!({"valid":true,"demos":rows.len(),"controllers":labels.len()}))
        })(),
        Some("verify-editor-demo") if args.len()==5 => (|| {
            let catalog:serde_json::Value=serde_json::from_slice(&std::fs::read(&args[3]).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
            let demos=catalog["demos"].as_array().ok_or("Missing demos")?;
            let mut failed=Vec::new();
            let keys=demos.iter().map(|row|row["key"].as_str().map(str::to_owned).ok_or("Missing resolved key")).collect::<Result<Vec<_>,_>>()?;
            for (row,ready) in demos.iter().zip(ok_extract::prepare_animation_batch(Path::new(&args[2]),&keys)) {
                if !ready {failed.push(row["id"].clone());}
            }
            let report=serde_json::json!({"valid":failed.is_empty(),"prepared":demos.len()-failed.len(),"failed":failed});
            std::fs::write(&args[4],serde_json::to_vec_pretty(&report).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
            if !failed.is_empty(){return Err(format!("Failed to prepare {} demos",failed.len()));}
            Ok(report)
        })(),
        Some("prepare-clothing-state") if args.len()==5 => (|| {
            let outfit=args[3].parse::<i32>().map_err(|_|"invalid outfit")?;
            let states=args[4].split(',').map(|v|v.parse::<u8>().map_err(|_|"invalid clothing state")).collect::<Result<Vec<_>,_>>()?;
            if outfit<0||states.len()!=9||states.iter().any(|v|*v>3){return Err("outfit and nine clothing states are required".into());}
            for key in ["OK_GPU_SKIN","OK_SKELMESH","OK_NOPOSE","OK_UE_ANIM","OK_HEAD_GPUSKIN"]{std::env::set_var(key,"1");}
            std::env::set_var("OK_CLOTHES_STATES",&args[4]);
            let session=ok_extract::portable::load_outfit(Path::new(&args[2]),outfit)?;
            let parts=session.parts.iter().filter(|p|p.role.starts_with("clothes")).map(|p|serde_json::json!({"role":p.role,"name":p.name,"visible":p.render_visible,"triangles":p.indices.len()/3})).collect::<Vec<_>>();
            Ok(serde_json::json!({"valid":true,"card":args[2],"outfit":outfit,"requested_states":states,"clothing_parts":parts}))
        })(),
        Some("verify") if args.len()==3 => ok_extract::portable::verify(Path::new(&args[2])),
        Some("embed") if args.len()==5 => ok_extract::portable::embed(Path::new(&args[2]),Path::new(&args[3]),Path::new(&args[4])),
        Some("shared") if args.len()==6 => ok_extract::portable::shared(Path::new(&args[2]),Path::new(&args[3]),Path::new(&args[4]),Path::new(&args[5])),
        Some("batch-shared") if args.len()==6 => batch_shared(Path::new(&args[2]),Path::new(&args[3]),Path::new(&args[4]),Path::new(&args[5])),
        _ => Err("usage: ok-preview-pack export GAME CARD OUTPUT | verify PACK_OR_PNG | embed CARD PACK OUTPUT.png | shared GAME CARD BUILTIN_DIRECTORY OUTPUT.png".into()),
    };
    match result { Ok(report)=>println!("{}",serde_json::to_string_pretty(&report).unwrap()),
        Err(error)=>{eprintln!("{error}");std::process::exit(1);} }
}
