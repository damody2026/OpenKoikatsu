//! Native single-garment transitions, kept separate from UpdateClothesStateAll.
#[allow(unused_imports)] use crate::gamefs::GamePath as _;
use std::{fs, path::Path};
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub(crate) struct Rules {
    types: [Option<u8>; 9],
    not_bot: bool,
    not_shorts: bool,
}
impl Rules {
    pub(crate) fn transition(&self, previous: [u8; 9], kind: i32) -> Result<[u8; 9], String> {
        if previous.iter().any(|v| *v > 3) { return Err("invalid clothing state".into()); }
        let mut states = crate::resolve_clothes_states(previous, self.types, self.not_bot, self.not_shorts);
        match kind {
            0..=8 => {
                let index=kind as usize; let value=(states[index]+1)%4;
                crate::set_clothes_state(&mut states,index,value,self.types[index],self.not_bot,self.not_shorts,[self.types[7],self.types[8]]);
            },
            100 => states=[0;9],
            101 | 102 => {
                let end=if kind==101 {4}else{9}; let value=if kind==101 {1}else{3};
                for index in 0..end {crate::set_clothes_state(&mut states,index,value,self.types[index],self.not_bot,self.not_shorts,[self.types[7],self.types[8]]);}
            },
            _ => return Err("unknown clothing action".into()),
        }
        Ok(states)
    }
}

fn native(game:&Path,path:&Path,outfit:i32)->Result<Rules,String> {
    let card=crate::card::parse_card_bytes_mode(&crate::gamefs::read(path).map_err(|e|e.to_string())?,outfit,false)?;
    let vanilla=crate::list::load_characustom_dir(&game.join("abdata/list/characustom"));
    let mods_dir=game.join("mods");let mods=crate::list::load_mods_catalog(&mods_dir);
    let wanted=card.resolves.iter().map(|r|r.mod_id.clone()).collect();
    let guids=crate::mods::find_guids(&mods_dir,Some(&wanted));
    let slots=crate::select_card_clothes(&card,outfit,&vanilla,&mods,&guids);
    let mut types=std::array::from_fn(|i|slots[i].as_ref().and_then(|(e,_,_)|e.info("StateType").trim().parse::<u8>().ok()));
    let not_bot=slots[0].as_ref().is_some_and(|(e,_,_)|e.info("Coordinate").trim()=="2");
    let not_shorts=slots[2].as_ref().is_some_and(|(e,_,_)|e.info("Coordinate").trim()=="2");
    let top_type=slots[0].as_ref().and_then(|(e,_,_)|e.info("Kind").parse().ok()).unwrap_or(0);
    let sub=crate::select_card_top_subparts(&card,outfit,top_type,&vanilla,&mods,&guids);
    let top_not_bra=slots[0].as_ref().is_some_and(|(e,_,_)|e.info("NotBra").trim()=="1");
    let sub_not_bra:[Option<bool>;2]=std::array::from_fn(|i|sub[i].as_ref().and_then(|(_,e,_,_)|(!crate::is_dummy_part(&e.main_data,Some(&e.name))).then(||e.info("NotBra").trim()=="1")));
    let not_bra=match top_type {1=>sub_not_bra[0].unwrap_or(top_not_bra),2 if sub_not_bra.iter().any(Option::is_some)=>sub_not_bra.iter().any(|v|*v==Some(true)),_=>top_not_bra};
    types=crate::adjust_clothes_state_types(types,crate::selected_clothes_reference_flags(game,&slots),not_bra,not_shorts);
    Ok(Rules{types,not_bot,not_shorts})
}

#[derive(Serialize,Deserialize)]
struct Prepared {schema:u32,resource_sha256:String,rules:Rules}
pub(crate) fn validate(bytes:&[u8],digest:&str)->Result<Rules,String>{
    if bytes.len()>4096{return Err("clothing rules exceed size limit".into());}
    let data:Prepared=serde_json::from_slice(bytes).map_err(|e|e.to_string())?;
    if data.schema!=1||data.resource_sha256!=digest||data.rules.types.iter().flatten().any(|v|*v>3){return Err("clothing rule metadata mismatch".into());}Ok(data.rules)
}
pub(crate) fn load(game:&Path,path:&Path,outfit:i32)->Result<Rules,String>{
    use sha2::{Digest,Sha256};
    let bytes=crate::gamefs::read(path).map_err(|e|e.to_string())?;
    if !bytes.starts_with(b"\x89PNG\r\n\x1a\n")||crate::portable::png_snapshot(&bytes)?.1.is_none(){
        let game=crate::oklib::root_for_card(game,Some(path));
        crate::oklib::ensure_library(&game)?;
        return native(&game,path,outfit);
    }
    let digest=crate::portable::clothing_resource_digest(&bytes)?;
    let cache=path.parent().ok_or("card directory missing")?.join(".okstates").join(&digest).join(format!("outfit{outfit}_rules.json"));
    if cache.ok_is_file(){return validate(&crate::gamefs::read(&cache).map_err(|e|e.to_string())?,&digest);}
    let receipt:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(path.with_extension("verified.json")).map_err(|_|"Clothing rules have not been prepared; original conversion provenance is required once")?).map_err(|e|e.to_string())?;
    if receipt["apng_sha256"].as_str()!=Some(format!("{:x}",Sha256::digest(&bytes)).as_str()){return Err("clothing rule receipt hash mismatch".into());}
    let (root,source)=crate::portable::clothing_source(path,&bytes,&receipt)?;
    let rules=native(&root,&source,outfit)?;
    crate::portable::publish_clothing_cache(&cache,&serde_json::to_vec_pretty(&Prepared{schema:1,resource_sha256:digest,rules:rules.clone()}).map_err(|e|e.to_string())?)?;
    Ok(rules)
}

#[cfg(test)]mod tests {
    use super::*;
    #[test]fn explicit_requests_are_nested_thread_local_and_restore_on_unwind(){
        let previous=crate::unity::requested_clothes_states();
        let outer=crate::unity::request_clothes_states([1;9]);
        assert_eq!(crate::unity::requested_clothes_states(),[1;9]);
        let worker=std::thread::spawn(||{
            let _own=crate::unity::request_clothes_states([3;9]);
            assert_eq!(crate::unity::requested_clothes_states(),[3;9]);
        });worker.join().unwrap();
        assert_eq!(crate::unity::requested_clothes_states(),[1;9]);
        let _=std::panic::catch_unwind(||{let _nested=crate::unity::request_clothes_states([2;9]);assert_eq!(crate::unity::requested_clothes_states(),[2;9]);panic!("test scope unwind");});
        assert_eq!(crate::unity::requested_clothes_states(),[1;9]);
        drop(outer);assert_eq!(crate::unity::requested_clothes_states(),previous);
    }
    #[test]fn native_linked_and_independent_transitions(){
        let linked=Rules{types:[Some(0);9],not_bot:true,not_shorts:true};
        let half=linked.transition([0;9],0).unwrap();assert_eq!(&half[..2],&[1,0]);
        let off=linked.transition(half,0).unwrap();assert_eq!(&off[..2],&[3,3]);
        assert_eq!(&linked.transition(off,0).unwrap()[..2],&[0,0]);
        let off=linked.transition(linked.transition([0;9],2).unwrap(),2).unwrap();assert_eq!(&off[2..4],&[3,3]);
        let independent=Rules{types:[Some(2);9],not_bot:false,not_shorts:false};
        assert_eq!(&independent.transition([0;9],0).unwrap()[..2],&[1,0]);
        assert_eq!(linked.transition([0;9],102).unwrap(),[3;9]);
        assert_eq!(linked.transition([3;9],100).unwrap(),[0;9]);
        assert!(linked.transition([0;9],-1).is_err());
    }
    #[test]fn portable_rules_are_native_and_remain_available_offline(){
        let root=Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/CharacterCards/CrowdAPNG");let card=root.join("C000.png");
        if std::env::var_os("OK_BASE_LIBRARY").is_none(){std::env::set_var("OK_BASE_LIBRARY",Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Content/IndependentResources/base_v1"));}
        let rules=load(Path::new(""),&card,0).unwrap();assert!(rules.not_bot);
        let half=rules.transition([0;9],0).unwrap();let off=rules.transition(half,0).unwrap();assert_eq!(&off[..2],&[3,3]);
        // An OK card's rules come from the OK base library; the offline cache below belongs to legacy okPv cards.
        if crate::oklib::is_library_card(&card){return;}
        let digest=crate::portable::clothing_resource_digest(&crate::gamefs::read(&card).unwrap()).unwrap();
        let bytes=crate::gamefs::read(root.join(".okstates").join(&digest).join("outfit0_rules.json")).unwrap();
        let offline=validate(&bytes,&digest).unwrap();assert_eq!(offline.transition(half,0).unwrap(),off);
        assert!(validate(&bytes,"wrong resource").is_err());
        let unique=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
        let target=root.join(format!("../../../Saved/Tests/EditorClothingOffline_{unique}"));fs::create_dir_all(&target).unwrap();
        let saved=target.join("saved.png");fs::copy(&card,&saved).unwrap();
        assert!(crate::portable::copy_clothing_states(&card,&saved).unwrap()>0);
        fs::rename(saved.with_extension("verified.json"),target.join("provenance.saved.json")).unwrap();
        assert_eq!(load(Path::new("__no_original_game__"),&saved,0).unwrap().transition(half,0).unwrap(),off);
    }
}
