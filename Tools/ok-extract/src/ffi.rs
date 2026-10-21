//! C ABI + function-pointer tables (unreal-rust style).
#[allow(unused_imports)] use crate::gamefs::GamePath as _;

use std::ffi::{c_char, CStr};
use std::os::raw::c_int;
use std::ptr;
use std::sync::Mutex;

use crate::session::OkSession;

// Borrowed by one blocking call only. No pointer is retained in cloneable sessions.
#[no_mangle]
pub unsafe extern "C" fn ok_clothing_transition(root:*const c_char,card:*const c_char,outfit:i32,previous:*const u8,kind:i32,out:*mut u8,err:*mut c_char,cap:i32)->i32 {
    let result=(|| {
        let (Some(root),Some(card))=(cstr_opt(root),cstr_opt(card)) else{return Err("clothing root/card missing".into());};
        if previous.is_null()||out.is_null()||outfit<0{return Err("invalid clothing transition buffer".into());}
        let states: [u8;9]=std::slice::from_raw_parts(previous,9).try_into().unwrap();
        crate::clothing_rules::load(std::path::Path::new(&root),std::path::Path::new(&card),outfit)?.transition(states,kind)
    })();
    match result {Ok(states)=>{ptr::copy_nonoverlapping(states.as_ptr(),out,9);0},Err(error)=>{write_err(err,cap,&error);-1}}
}

/// OK clothes (.okclothes) for the editor. op 0: import a coordinate card (a = card, b unused); 1: export outfit `outfit`
/// of character card `a`; 2: wear .okclothes `b` as outfit `outfit` of card `a`, writing the new card to `out`.
/// Returns 0 and a one-line summary in `msg`, or -1 and the error.
#[no_mangle]
pub unsafe extern "C" fn ok_clothes_file(op:i32,a:*const c_char,b:*const c_char,outfit:i32,out:*const c_char,msg:*mut c_char,cap:i32)->i32 {
    let result=std::panic::catch_unwind(||{
        let (Some(a),Some(out))=(cstr_opt(a),cstr_opt(out)) else{return Err("okclothes: path missing".to_string());};
        let (a,out)=(std::path::Path::new(&a),std::path::Path::new(&out));
        let outfit=usize::try_from(outfit).map_err(|_|"okclothes: invalid outfit".to_string());
        match op {
            0=>crate::okclothes::file::import(a,out),
            1=>crate::okclothes::file::export(a,outfit?,out),
            2=>{let b=cstr_opt(b).ok_or("okclothes: clothes path missing")?;let summary=crate::okclothes::file::wear_file(a,std::path::Path::new(&b),outfit?,out)?;
                // An OK card wearing new clothes is made again, so its models list and pack cover them.
                if crate::oklib::is_library_card(out) {remake_card_in_place(out)?;}
                Ok(summary)}
            _=>Err(format!("okclothes: unknown op {op}")),
        }
    }).unwrap_or_else(|_|Err("okclothes: panic".into()));
    match result{Ok(summary)=>{write_err(msg,cap,&summary);0},Err(e)=>{write_err(msg,cap,&e);-1}}
}

/// The installation original cards and their zipmods come from: OK_GAME_ROOT (may be absent: then only the base
/// library and the card's own folder provide resources).
fn installed_game_root()->std::path::PathBuf {
    std::env::var_os("OK_GAME_ROOT").map(std::path::PathBuf::from).filter(|g|g.is_dir()).unwrap_or_default()
}
/// Make `card` (an edited OK card) again in place from the configured base library.
fn remake_card_in_place(card:&std::path::Path)->Result<(),String> {
    let lib=crate::oklib::configured_library().ok_or("OK base library not configured")?;
    let (_,_,bytes,_)=crate::oklib::remake_ok_card(&lib,&installed_game_root(),card)?;
    std::fs::write(card,bytes).map_err(|e|format!("{}: {e}",card.display()))
}

/// Convert a card to an OK card (`out`): an original KKS card with its zipmods (OK_GAME_ROOT, or `game`), or an
/// edited OK card made again. Uses the configured base library. Returns 0 and "digest parts pack-bytes" in `msg`.
#[no_mangle]
pub unsafe extern "C" fn ok_okcard_make(game:*const c_char,card:*const c_char,out:*const c_char,msg:*mut c_char,cap:i32)->i32 {
    okcard_make_impl(game,card,out,None,msg,cap)
}
/// Scene-selected outfit; preserve all original coordinate indices.
#[no_mangle]
pub unsafe extern "C" fn ok_okcard_make_outfit(game:*const c_char,card:*const c_char,out:*const c_char,outfit:i32,msg:*mut c_char,cap:i32)->i32 {
    okcard_make_impl(game,card,out,Some(outfit),msg,cap)
}
unsafe fn okcard_make_impl(game:*const c_char,card:*const c_char,out:*const c_char,outfit:Option<i32>,msg:*mut c_char,cap:i32)->i32 {
    let result=std::panic::catch_unwind(||{
        let (Some(card),Some(out))=(cstr_opt(card),cstr_opt(out)) else{return Err("okcard: path missing".to_string());};
        let lib=crate::oklib::configured_library().ok_or("OK base library not configured")?;
        let game=cstr_opt(game).filter(|g|!g.is_empty()).map(std::path::PathBuf::from).unwrap_or_else(installed_game_root);
        let (digest,parts,bytes,pack)=match outfit {
            Some(index)=>crate::oklib::remake_ok_card_for_outfit(&lib,&game,std::path::Path::new(&card),index)?,
            None=>crate::oklib::remake_ok_card(&lib,&game,std::path::Path::new(&card))?,
        };
        std::fs::write(&out,&bytes).map_err(|e|format!("{out}: {e}"))?;
        Ok(format!("{digest} parts={parts} pack={pack}"))
    }).unwrap_or_else(|_|Err("okcard: panic".into()));
    match result{Ok(summary)=>{write_err(msg,cap,&summary);0},Err(e)=>{write_err(msg,cap,&e);-1}}
}

#[no_mangle]
pub unsafe extern "C" fn ok_appearance_request(root:*const c_char,request:*const c_char,report:*const c_char,err:*mut c_char,cap:i32)->i32 {
    static LOCK:Mutex<()>=Mutex::new(());
    let result=std::panic::catch_unwind(||{
        let _guard=LOCK.lock().map_err(|_|"圖片選擇工作鎖定失敗".to_string())?;
        let (Some(root),Some(request),Some(report))=(cstr_opt(root),cstr_opt(request),cstr_opt(report)) else{return Err("圖片選擇請求缺少路徑".into());};
        let result=crate::appearance::request(std::path::Path::new(&root),std::path::Path::new(&request))?;
        std::fs::write(report,serde_json::to_vec(&result).map_err(|e|e.to_string())?).map_err(|e|e.to_string())
    }).unwrap_or_else(|_|Err("圖片選擇資源處理失敗".into()));
    match result{Ok(())=>0,Err(e)=>{write_err(err,cap,&e);-1}}
}

#[no_mangle]
pub unsafe extern "C" fn ok_head_texture_changes(target:*mut OkSession,source:*const OkSession,indices:*mut i32,capacity:i32,apply:i32,err:*mut c_char,cap:i32)->i32 {
    let result=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||{
        if target.is_null()||source.is_null()||std::ptr::eq(target as *const OkSession,source){return Err("invalid appearance sessions".to_string());}
        let target=&mut *target;let source=&*source;let changed=target.head_texture_changes(source)?;
        if capacity<changed.len() as i32||(!changed.is_empty()&&indices.is_null()){return Err("appearance index buffer is too small".into());}
        for(i,index)in changed.iter().enumerate(){*indices.add(i)=*index as i32;}
        if apply!=0 {target.copy_head_textures(source,&changed);}
        Ok(changed.len() as i32)
    })).unwrap_or_else(|_|Err("appearance texture transaction failed".into()));
    match result {Ok(count)=>count,Err(error)=>{write_err(err,cap,&error);-1}}
}

pub type PrivateSkinCallback = unsafe extern "C" fn(*mut std::ffi::c_void,i32,i32,i32,*const f32,*const f32,*const f32);
#[derive(Clone,Copy)]
pub(crate) struct PrivateSkinObserver {pub callback:PrivateSkinCallback,pub context:usize,pub role:i32}
impl PrivateSkinObserver {
    pub(crate) fn emit(self,index:usize,part:&crate::session::SessionPart) {
        if part.positions.len()!=part.normals.len(){return;}
        unsafe {(self.callback)(self.context as *mut std::ffi::c_void,self.role,index as i32,part.positions.len() as i32,
            part.positions.as_ptr().cast(),part.normals.as_ptr().cast(),
            if part.rest_tangents.len()!=part.positions.len(){ptr::null()}else{part.rest_tangents.as_ptr().cast()});}
    }
}

#[no_mangle]
pub unsafe extern "C" fn ok_head_look_error(s:*mut OkSession,out:*mut f32)->i32 {
    // Last head-look check: out[0] = face vs limited desired direction, out[1] = desired vs target (degrees).
    let Some(s)=sess_ref(s) else{return -1;};
    if out.is_null() {return -1;}
    let Some((residual,excess))=s.head_look.last_error else{return 1;};
    *out=residual;*out.add(1)=excess;0
}
#[no_mangle]
pub unsafe extern "C" fn ok_set_head_look(s:*mut OkSession,x:f32,y:f32,z:f32,enabled:i32)->i32 {
    let Some(s)=sess_ref(s) else{return -1;};
    if enabled==0 {s.head_look.reset();return 0;}
    if ![x,y,z].iter().all(|v|v.is_finite()) || !s.body_skel.as_ref().is_some_and(|sk|sk.tfs.values().any(|t|t.name=="cf_j_head")) {return -1;}
    s.head_look.target=Some([x,y,z]);0
}
/// Studio FK: `names` is `count` NUL-terminated bone names back to back, `euler` 3 floats each (Unity local Euler
/// degrees). count 0 clears FK. Applied from the next pose frame on.
#[no_mangle]
pub unsafe extern "C" fn ok_session_set_fk(s:*mut OkSession,names:*const c_char,euler:*const f32,count:i32)->i32 {
    let Some(s)=sess_ref(s) else{return -1;};
    if count<0 || count>0 && (names.is_null()||euler.is_null()) {return -1;}
    let mut fk=Vec::with_capacity(count as usize);
    let mut p=names;
    for i in 0..count as usize {
        let name=std::ffi::CStr::from_ptr(p);
        let e=std::slice::from_raw_parts(euler.add(i*3),3);
        if !e.iter().all(|v|v.is_finite()) {return -1;}
        fk.push((name.to_string_lossy().into_owned(),[e[0],e[1],e[2]]));
        p=p.add(name.to_bytes().len()+1);
    }
    s.user_fk=fk;0
}
/// Studio IK: builds the solver from the shared resources root (once per session). 0 ok, 1 error (message in `err`).
#[no_mangle]
pub unsafe extern "C" fn ok_session_prepare_studio_ik(s:*mut OkSession,root:*const c_char,err:*mut c_char,cap:i32)->i32 {
    let Some(s)=sess_ref(s) else{return -1;};
    let root=cstr_opt(root).unwrap_or_default();
    let result=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||s.prepare_studio_ik(std::path::Path::new(&root)))).unwrap_or_else(|_|Err("IK setup panicked".into()));
    match result { Ok(())=>0, Err(e)=>{write_err(err,cap,&e);1} }
}
/// Studio IK targets: `count` effector indexes (0..8) with 3 floats each (skeleton space, Unity metres); 0 clears.
#[no_mangle]
pub unsafe extern "C" fn ok_session_set_studio_ik(s:*mut OkSession,effectors:*const i32,positions:*const f32,count:i32)->i32 {
    let Some(s)=sess_ref(s) else{return -1;};
    if count<0 || count>0 && (effectors.is_null()||positions.is_null()) {return -1;}
    let mut targets=Vec::with_capacity(count as usize);
    for i in 0..count as usize {
        let e=*effectors.add(i);
        let p=std::slice::from_raw_parts(positions.add(i*3),3);
        if !(0..crate::session::STUDIO_IK_TARGETS as i32).contains(&e) || !p.iter().all(|v|v.is_finite()) {return -1;}
        targets.push((e as usize,[p[0],p[1],p[2]]));
    }
    s.studio_ik=targets;s.studio_ik_rotations.clear();0
}
/// Full Studio IK targets. Rotations are Unity skeleton-space quaternions; disabled targets retain position-only semantics.
#[no_mangle]
pub unsafe extern "C" fn ok_session_set_studio_ik_pose(
    s: *mut OkSession,
    effectors: *const i32,
    positions: *const f32,
    rotations: *const f32,
    enabled: *const u8,
    count: i32,
) -> i32 {
    let Some(s) = sess_ref(s) else {
        return -1;
    };
    if !(0..=crate::session::STUDIO_IK_TARGETS as i32).contains(&count)
        || count > 0
            && (effectors.is_null()
                || positions.is_null()
                || rotations.is_null()
                || enabled.is_null())
    {
        return -1;
    }
    let mut targets = Vec::new();
    let mut angles = Vec::new();
    for i in 0..count as usize {
        let e = *effectors.add(i);
        let p = std::slice::from_raw_parts(positions.add(i * 3), 3);
        let q = std::slice::from_raw_parts(rotations.add(i * 4), 4);
        if !(0..crate::session::STUDIO_IK_TARGETS as i32).contains(&e)
            || !p.iter().all(|v| v.is_finite())
        {
            return -1;
        }
        targets.push((e as usize, [p[0], p[1], p[2]]));
        if *enabled.add(i) != 0 {
            let n = q.iter().map(|v| v * v).sum::<f32>();
            if !(5..=8).contains(&e)
                || !q.iter().all(|v| v.is_finite())
                || !n.is_finite()
                || n < 1e-12
            {
                return -1;
            }
            angles.push((e as usize, crate::finalik::qnorm([q[0], q[1], q[2], q[3]])));
        }
    }
    s.studio_ik = targets;
    s.studio_ik_rotations = angles;
    0
}
/// Saved NeckLookControllerVer2 FIX locals (native aBones order), count 0 clears; validates before mutation.
#[no_mangle]
pub unsafe extern "C" fn ok_session_set_studio_fixed_neck(
    s: *mut OkSession,
    rotations: *const f32,
    count: i32,
) -> i32 {
    let Some(s) = sess_ref(s) else {
        return -1;
    };
    if count == 0 {
        s.head_look.fixed = None;
        return 0;
    }
    if count != 2 || rotations.is_null() {
        return -1;
    }
    let mut result = [[0.; 4]; 2];
    for (i, q) in result.iter_mut().enumerate() {
        q.copy_from_slice(std::slice::from_raw_parts(rotations.add(i * 4), 4));
        let n = q.iter().map(|v| v * v).sum::<f32>();
        if !q.iter().all(|v| v.is_finite()) || !n.is_finite() || n < 1e-12 {
            return -1;
        }
        *q = crate::finalik::qnorm(*q);
    }
    s.head_look.fixed = Some(result);
    0
}

/// Skeleton-space position (Unity metres) of an IK effector's bone in the current pose; -1 before prepare.
#[no_mangle]
pub unsafe extern "C" fn ok_session_ik_effector_position(s:*mut OkSession,effector:i32,out:*mut f32)->i32 {
    let Some(s)=sess_ref(s) else{return -1;};
    if out.is_null()||effector<0 {return -1;}
    let Some(p)=s.ik_effector_position(effector as usize) else{return -1;};
    std::ptr::copy_nonoverlapping(p.as_ptr(),out,3);0
}
/// Local rotation (Unity Euler degrees) of a body bone in the current pose; -1 when there is no such bone.
#[no_mangle]
pub unsafe extern "C" fn ok_session_bone_euler(s:*mut OkSession,name:*const c_char,out:*mut f32)->i32 {
    let Some(s)=sess_ref(s) else{return -1;};
    if name.is_null()||out.is_null() {return -1;}
    let Some(e)=s.body_bone_euler(&std::ffi::CStr::from_ptr(name).to_string_lossy()) else{return -1;};
    std::ptr::copy_nonoverlapping(e.as_ptr(),out,3);0
}
#[no_mangle]
pub unsafe extern "C" fn ok_session_set_physx_secondary(s:*mut OkSession,enabled:i32)->i32 {
    // PhysX secondary dynamics for this character (gallery menu, one-to-one, paired playback; off for crowd and street).
    let Some(s)=sess_ref(s) else{return -1;};
    s.physx_secondary=enabled!=0;0
}
#[no_mangle]
pub unsafe extern "C" fn ok_static_interaction_frame(s:*mut OkSession,dt:f32)->i32 {
    let Some(s)=sess_ref(s) else{return -1;};
    if !dt.is_finite()||dt<0. {return -1;}
    s.refresh_static_interaction(dt);0
}

// Body reactions use shared authored data and per-session state. Preparation
// is explicit and transactional; the frame path performs no file IO.
#[no_mangle]
pub unsafe extern "C" fn ok_body_reaction_prepare(s:*mut OkSession,root:*const c_char,json:*const u8,len:i32,err:*mut c_char,cap:i32)->i32 {
    let result=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||{
        let s=sess_ref(s).ok_or("missing session")?;
        let root=cstr_opt(root).ok_or("missing shared resource root")?;
        if json.is_null()||len<=0||len>1024*1024{return Err("invalid reaction definition buffer".to_string());}
        let value=serde_json::from_slice::<unity_asset_core::UnityValue>(std::slice::from_raw_parts(json,len as usize)).map_err(|e|e.to_string())?;
        let definition=crate::hit_reaction::Definition::parse(&value)?;
        let solver=crate::h_playback::body_reaction_solver(s,std::path::Path::new(&root))?;
        s.body_interaction=Some(crate::hit_reaction::Interaction{reaction:crate::hit_reaction::Runtime::new(definition),weight:1.,release_mask:0,solver});
        Ok(())
    })).unwrap_or_else(|_|Err("body reaction preparation failed".into()));
    match result{Ok(())=>0,Err(e)=>{write_err(err,cap,&e);-1}}
}
#[no_mangle]
pub unsafe extern "C" fn ok_body_reaction_hit(s:*mut OkSession,id:i32,forces:*const f32,count:i32,weight:f32)->i32 {
    if forces.is_null()||count<=0||count>32||!weight.is_finite()||!(0.0..=1.0).contains(&weight){return -1;}
    let Some(interaction)=sess_ref(s).and_then(|s|s.body_interaction.as_mut())else{return -1;};
    let forces=std::slice::from_raw_parts(forces.cast::<[f32;3]>(),count as usize);
    if !interaction.reaction.hit(id,forces){return -1;}
    interaction.weight=weight;0
}
#[no_mangle]
pub unsafe extern "C" fn ok_body_reaction_clear(s:*mut OkSession)->i32 {
    let Some(s)=sess_ref(s)else{return -1;};
    if let Some(i)=&mut s.body_interaction{i.reaction.clear();i.release_mask=0;}
    0
}
#[no_mangle]
pub unsafe extern "C" fn ok_body_reaction_hit_with_release(s:*mut OkSession,id:i32,forces:*const f32,count:i32,weight:f32,release_mask:u8)->i32{
    if forces.is_null()||count<=0||count>32||release_mask>15||!weight.is_finite()||!(0.0..=1.0).contains(&weight){return -1;}
    let Some(session)=sess_ref(s)else{return -1;};
    let source=std::slice::from_raw_parts(forces.cast::<[f32;3]>(),count as usize);
    let Some(i)=session.body_interaction.as_mut()else{return -1;};
    if !i.reaction.hit(id,source){return -1;}
    i.weight=weight;i.release_mask=release_mask;0
}
#[no_mangle]
pub unsafe extern "C" fn ok_body_reaction_is_playing(s:*mut OkSession)->i32 {
    sess_ref(s).is_some_and(|s|s.body_interaction.as_ref().is_some_and(|i|i.reaction.is_playing())) as i32
}

// Additive APIs: existing function tables and portable snapshot layouts remain
// unchanged. Query with null/zero, then copy into caller-owned storage.
pub(crate) unsafe fn copy_resource(bytes: &[u8], out: *mut u8, cap: i32) -> i32 {
    if bytes.len()>i32::MAX as usize || cap<0 {return -1;}
    if out.is_null() && cap==0 {return bytes.len() as i32;}
    if out.is_null() || (cap as usize)<bytes.len() {return -1;}
    ptr::copy_nonoverlapping(bytes.as_ptr(),out,bytes.len());bytes.len() as i32
}
#[no_mangle]
pub unsafe extern "C" fn ok_session_animation_snapshot(s:*mut OkSession,out:*mut u8,cap:i32)->i32 {
    let Some(s)=sess_ref(s) else{return -1;};
    match s.to_animation_bytes() {Ok(bytes)=>copy_resource(&bytes,out,cap),Err(_)=>-1}
}
/// Full appearance clone for an in-process stage. Animation-only snapshots omit
/// textures; using one here would silently lose native material inputs.
#[no_mangle]
pub unsafe extern "C" fn ok_session_clone_interaction(source:*const OkSession)->*mut OkSession {
    let Some(source)=source.as_ref() else{return ptr::null_mut();};
    match source.to_preview_bytes().and_then(|bytes|OkSession::from_preview_bytes(&bytes)) {
        Ok(session)=>Box::into_raw(Box::new(session)),Err(_)=>ptr::null_mut()
    }
}
#[no_mangle]
pub unsafe extern "C" fn ok_session_open_animation_snapshot(bytes:*const u8,len:i32)->*mut OkSession {
    if bytes.is_null() || len<=0 || len as u64>crate::portable::MAX_BYTES {return ptr::null_mut();}
    match OkSession::from_animation_bytes(std::slice::from_raw_parts(bytes,len as usize)) {
        Ok(s)=>Box::into_raw(Box::new(s)),Err(_)=>ptr::null_mut()
    }
}
#[no_mangle]
pub unsafe extern "C" fn ok_session_export_shared_clip(s:*mut OkSession,key:*const c_char,out:*mut u8,cap:i32)->i32 {
    let Some(s)=sess_ref(s) else{return -1;};let Some(key)=cstr_opt(key) else{return -1;};
    match crate::anim::export_shared_clip(&s.game_root,&key) {Ok(bytes)=>copy_resource(&bytes,out,cap),Err(_)=>-1}
}
#[no_mangle]
pub unsafe extern "C" fn ok_export_shared_clip(root:*const c_char,key:*const c_char,out:*mut u8,cap:i32)->i32 {
    let (Some(root),Some(key))=(cstr_opt(root),cstr_opt(key)) else{return -1;};
    match crate::anim::export_shared_clip(std::path::Path::new(&root),&key) {Ok(bytes)=>copy_resource(&bytes,out,cap),Err(_)=>-1}
}
#[no_mangle]
pub unsafe extern "C" fn ok_shared_clip_duration(bytes:*const u8,len:i32)->f32 {
    if bytes.is_null() || len<=0 || len>128*1024*1024 {return 0.;}
    let Ok((root,key))=crate::anim::install_shared_clip(std::slice::from_raw_parts(bytes,len as usize)) else{return 0.;};
    crate::anim::clip_duration(&root,&key)
}
#[no_mangle]
pub extern "C" fn ok_begin_animation_preparation(){crate::anim::begin_animation_preparation();}
#[no_mangle]
pub extern "C" fn ok_end_animation_preparation(){crate::anim::end_animation_preparation();}
#[no_mangle]
pub unsafe extern "C" fn ok_session_install_shared_clip(s:*mut OkSession,bytes:*const u8,len:i32)->i32 {
    let Some(s)=sess_ref(s) else{return 1;};if bytes.is_null() || len<=0 || len>128*1024*1024 {return 1;}
    let Ok((root,key))=crate::anim::install_shared_clip(std::slice::from_raw_parts(bytes,len as usize)) else{return 1;};
    s.game_root=root;s.anim_clip=key;s.anim_parameters.clear();s.anim_scene_mode=0;s.anim_time=0.;
    // A shared-curve selection has the same lifetime as a native clip switch:
    // keep the character's live private rig and particles across actions.
    s.reset_anim_clock();s.pv_mask=15;0
}

/// Offline bank/source initialization starts fresh private accessory particles.
/// Live clip selection keeps its existing continuous state through the API above.
#[no_mangle]
pub unsafe extern "C" fn ok_session_prepare_shared_clip(s:*mut OkSession,bytes:*const u8,len:i32)->i32 {
    let status=ok_session_install_shared_clip(s,bytes,len);if status!=0 {return status;}
    let Some(session)=sess_ref(s) else{return 1;};session.reset_dynamic_accessories();0
}

#[repr(C)]
pub struct OkSkinPaletteView {
    pub positions:*const f32,
    pub normals:*const f32,
    pub tangents:*const f32,
    pub weights:*const f32,
    pub bone_idx:*const u16,
    pub n_verts:i32,
}

#[no_mangle]
pub unsafe extern "C" fn ok_part_skin_palette(s:*mut OkSession,index:i32,out:*mut OkSkinPaletteView,matrices:*mut f32,capacity:i32)->i32 {
    if index<0||capacity<0{return -1;}
    let Some(s)=sess_ref(s) else{return -1;};
    let Some(part)=s.parts.get(index as usize) else{return -1;};
    let (positions,normals,tangents,weights,bone_idx,palette)=if let Some(runtime)=&part.runtime_accessory {
        let body=runtime.skin_body(s.body_skel.as_ref(),s.clothes_dynamic_runtime.as_ref().map(|(_,state)|&state.skel));
        let Some((mesh,palette))=runtime.export_skin_palette(body,s.head_full_posed.as_ref().or(s.head_skel.as_ref())) else{return -1;};
        (&mesh.rest_positions,&mesh.rest_normals,&mesh.tangents,&mesh.weights,&mesh.bone_idx,palette)
    }else{
        let skel=match part.skel_kind {1=>s.body_skel.as_ref(),2=>s.head_skel.as_ref(),3=>s.head_full_posed.as_ref(),_=>None};
        let Some(skel)=skel else{return -1;};
        let count=if part.bind_poses.is_empty(){skel.worlds.len().min(skel.rest.len())}else{part.bind_poses.len().min(skel.worlds.len())};
        let binds=if part.bind_poses.len()>=count{part.bind_poses[..count].to_vec()}else{crate::pose::binds_from_rest(&skel.rest[..count])};
        let mut extra=if part.skel_kind==2||part.skel_kind==3{crate::head_onto_body(s.body_skel.as_ref(),Some(skel))}else{crate::pose::ident()};
        if let Some(name)=part.parent_bone.as_deref(){let Some(parent)=crate::bone_world(s.body_skel.as_ref(),s.head_skel.as_ref(),name) else{return -1;};extra=crate::pose::mat_mul_pub(&parent,&extra);}
        let mut palette:Vec<_>=skel.worlds[..count].iter().zip(&binds).map(|(world,bind)|crate::pose::mat_mul_pub(&extra,&crate::pose::mat_mul_pub(world,bind))).collect();
        if part.weights.iter().any(|w|w.iter().sum::<f32>()<=1e-5){palette.push(extra);}
        (&part.rest_positions,&part.rest_normals,&part.rest_tangents,&part.weights,&part.bone_idx,palette)
    };
    let n=positions.len();
    if n==0||n>i32::MAX as usize||palette.is_empty()||palette.len()>i32::MAX as usize||weights.len()!=n||bone_idx.len()!=n
        ||palette.iter().flatten().flatten().any(|v|!v.is_finite()) {return -1;}
    for (weights,bones) in weights.iter().zip(bone_idx){
        // Mod SMRs can contain signed influences. Preserve them; the exact
        // importer uses a blended affine palette for each signed vertex.
        if weights.iter().any(|w|!w.is_finite()){return -1;}
        if weights.iter().zip(bones).any(|(w,b)|w.abs()>=1e-6&&*b as usize>=palette.len()){return -1;}
    }
    let count=palette.len() as i32;
    if matrices.is_null(){return if capacity==0{count}else{-1};}
    if out.is_null()||capacity<count{return -1;}
    for (i,m) in palette.iter().enumerate(){ptr::copy_nonoverlapping(m.as_ptr().cast::<f32>(),matrices.add(i*16),16);}
    ptr::write(out,OkSkinPaletteView{
        positions:positions.as_ptr().cast(),normals:if normals.len()==n{normals.as_ptr().cast()}else{ptr::null()},
        tangents:if tangents.len()==n{tangents.as_ptr().cast()}else{ptr::null()},
        weights:weights.as_ptr().cast(),bone_idx:bone_idx.as_ptr().cast(),n_verts:n as i32,
    });count
}

#[no_mangle]
pub unsafe extern "C" fn ok_bone_world_matrix(s:*mut OkSession,name:*const c_char,out:*mut f32)->i32 {
    if name.is_null()||out.is_null(){return -1;}
    let Some(s)=sess_ref(s) else{return -1;};let Ok(name)=CStr::from_ptr(name).to_str() else{return -1;};
    let Some(m)=crate::bone_world(s.body_skel.as_ref(),s.head_skel.as_ref(),name) else{return -1;};
    if m.iter().flatten().any(|v|!v.is_finite()){return -1;}
    ptr::copy_nonoverlapping(m.as_ptr().cast::<f32>(),out,16);0
}

/// Private renderer stages only; legacy flattened palette ABI is unchanged.
/// Last slot is the unweighted identity fallback, not an authored skin bone.
#[no_mangle]
pub unsafe extern "C" fn ok_part_private_skin_stages(s:*mut OkSession,index:i32,out:*mut OkSkinPaletteView,matrices:*mut f32,capacity:i32,post:*mut f32)->i32 {
    private_skin_stages(s,index,0,out,matrices,capacity,post)
}

/// Process-local source revision. No borrowed pointer or session address is a
/// persistent identity. Caller copies source streams before mutating session.
#[no_mangle]
pub unsafe extern "C" fn ok_part_private_skin_source_revision(s:*mut OkSession,index:i32)->u64 {
    if index<0{return 0;}
    let Some(s)=sess_ref(s) else{return 0;};
    let Some(runtime)=s.parts.get(index as usize).and_then(|p|p.runtime_accessory.as_ref()) else{return 0;};
    // Ownership is an immutable source/layout token. Asking for it must not
    // rebuild this frame's body name map, world transforms and skin palette.
    // The staged publication below still validates every current coefficient
    // before copying any output, including nonfinite-pose rejection.
    runtime.current_private_source_revision().unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn ok_part_private_skin_stages_revision(s:*mut OkSession,index:i32,revision:u64,out:*mut OkSkinPaletteView,matrices:*mut f32,capacity:i32,post:*mut f32)->i32 {
    if revision==0{return -1;}
    private_skin_stages(s,index,revision,out,matrices,capacity,post)
}

/// Per-call renderer ownership. Validate every source before changing output;
/// ordinary skin/query APIs continue to publish full CPU geometry.
#[no_mangle]
pub unsafe extern "C" fn ok_skin_display_except_private(s:*mut OkSession,indices:*const i32,revisions:*const u64,count:i32)->i32 {
    if s.is_null()||count<0||count>65535||(count>0&&(indices.is_null()||revisions.is_null())){return 1;}
    let result=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||{
        let Some(excluded)=private_render_exclusions(&*s,indices,revisions,count) else{return false;};
        (&mut *s).skin_display_except(&excluded);true
    }));if matches!(result,Ok(true)){0}else{1}
}
unsafe fn private_render_exclusions(s:&OkSession,indices:*const i32,revisions:*const u64,count:i32)->Option<Vec<usize>>{
    if count<0||count>65535||(count>0&&(indices.is_null()||revisions.is_null())){return None;}
    let mut excluded=Vec::with_capacity(count as usize);
    for n in 0..count as usize{let i=*indices.add(n);let revision=*revisions.add(n);if i<0||revision==0{return None;}
        let runtime=s.parts.get(i as usize)?.runtime_accessory.as_ref()?;
        if runtime.current_private_source_revision()!=Some(revision){return None;}excluded.push(i as usize);
    }
    excluded.sort_unstable();if excluded.windows(2).any(|w|w[0]==w[1]){return None;}Some(excluded)
}
#[no_mangle]
pub unsafe extern "C" fn ok_query_private_skin_geometry(s:*mut OkSession,index:i32)->i32 {
    if s.is_null()||index<0{return 1;}
    let result=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||(&mut *s).query_private_geometry(index as usize)));
    if matches!(result,Ok(true)){0}else{1}
}
#[no_mangle]
pub unsafe extern "C" fn ok_query_private_skin_geometry_batch(s:*mut OkSession,indices:*const i32,count:i32)->i32 {
    if s.is_null()||count<0||count>65535||(count>0&&indices.is_null()){return 1;}
    let result=std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let input=if count==0{&[][..]}else{std::slice::from_raw_parts(indices,count as usize)};
        if input.iter().any(|i|*i<0){return false;}let indices=input.iter().map(|i|*i as usize).collect::<Vec<_>>();
        (&mut *s).query_private_geometries(&indices)
    }));if matches!(result,Ok(true)){0}else{1}
}

unsafe fn private_skin_stages(s:*mut OkSession,index:i32,expected:u64,out:*mut OkSkinPaletteView,matrices:*mut f32,capacity:i32,post:*mut f32)->i32 {
    if index<0||capacity<0{return -1;}
    let Some(s)=sess_ref(s) else{return -1;};
    let Some(runtime)=s.parts.get(index as usize).and_then(|p|p.runtime_accessory.as_ref()) else{return -1;};
    if expected!=0&&!runtime.gpu_stream_policy_supported(){return -1;}
    let body=runtime.skin_body(s.body_skel.as_ref(),s.clothes_dynamic_runtime.as_ref().map(|(_,state)|&state.skel));
    let Some((mesh,palette,correction))=runtime.export_skin_stages(body,s.head_full_posed.as_ref().or(s.head_skel.as_ref())) else{return -1;};
    let n=mesh.rest_positions.len();
    let Some(revision)=runtime.private_source_revision(palette.len()) else{return -1;};
    if (expected!=0&&revision!=expected)||palette.iter().flatten().flatten().chain(correction.iter().flatten()).any(|v|!v.is_finite()){return -1;}
    let count=palette.len() as i32;
    if matrices.is_null(){return if capacity==0{count}else{-1};}
    if out.is_null()||post.is_null()||capacity<count{return -1;}
    for (i,m) in palette.iter().enumerate(){ptr::copy_nonoverlapping(m.as_ptr().cast::<f32>(),matrices.add(i*16),16);}
    ptr::copy_nonoverlapping(correction.as_ptr().cast::<f32>(),post,16);
    ptr::write(out,OkSkinPaletteView{
        positions:mesh.rest_positions.as_ptr().cast(),normals:if mesh.rest_normals.len()==n{mesh.rest_normals.as_ptr().cast()}else{ptr::null()},
        tangents:if mesh.tangents.len()==n{mesh.tangents.as_ptr().cast()}else{ptr::null()},
        weights:mesh.weights.as_ptr().cast(),bone_idx:mesh.bone_idx.as_ptr().cast(),n_verts:n as i32,
    });count
}

#[no_mangle]
pub unsafe extern "C" fn ok_private_skin_stages_batch(s:*const OkSession,parts:*const i32,revisions:*const u64,counts:*const i32,count:i32,matrices:*mut f32,matrix_count:i32,posts:*mut f32)->i32 {
    if s.is_null()||count<=0||count>65535||parts.is_null()||revisions.is_null()||counts.is_null()||matrices.is_null()||posts.is_null()||matrix_count<=0{return -1;}
    let result=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||{
        let session=&*s;
        let parts=std::slice::from_raw_parts(parts,count as usize);
        let revisions=std::slice::from_raw_parts(revisions,count as usize);
        let counts=std::slice::from_raw_parts(counts,count as usize);
        let mut seen=std::collections::HashSet::new();let mut total=0usize;
        for ((part,revision),size) in parts.iter().zip(revisions).zip(counts){
            if *part<0||*revision==0||!(2..=65535).contains(size)||!seen.insert(*part){return false;}
            let Some(next)=total.checked_add(*size as usize)else{return false;};total=next;
        }
        if total!=matrix_count as usize||total>8_388_608{return false;}
        let policy=crate::session::AccessoryFramePolicy::read();
        if !policy.supports_private_gpu(){return false;}
        let head=session.head_full_posed.as_ref().or(session.head_skel.as_ref());
        let prepare_trace=crate::native_profile::Scope::new(34);
        let mut parents=std::collections::HashMap::new();
        let mut body_indices=std::collections::HashMap::new();
        let mut inputs=Vec::with_capacity(count as usize);
        for part in parts {
            let Some(runtime)=session.parts.get(*part as usize).and_then(|part|part.runtime_accessory.as_ref())else{return false;};
            let body=runtime.skin_body(session.body_skel.as_ref(),session.clothes_dynamic_runtime.as_ref().map(|(_,state)|&state.skel));
            // Validate each immutable body inventory once per batch. Workers
            // retain their own live world caches and the same duplicate policy.
            let body_names=body.map(|body|body_indices.entry(body as *const _ as usize)
                .or_insert_with(||crate::pose::rig_name_index(body,false)).clone());
            let parent=if let Some(key)=runtime.private_skin_parent_key(){
                // Body source can differ for coordinated clothes. Pointer keys
                // exist only for this immutable call, never across frames.
                let identity=(body.map_or(0,|body|body as *const _ as usize),key);
                *parents.entry(identity).or_insert_with(||crate::bone_world(body,head,key))
            }else{Some(crate::pose::ident())};
            let Some(parent)=parent else{return false;};
            inputs.push((runtime,body,parent,body_names));
        }
        drop(prepare_trace);
        let read=|i:usize|{
            let _trace=crate::native_profile::Scope::new(35);
            let (runtime,body,parent,body_names)=&inputs[i];
            let (_,palette,post)=runtime.export_skin_stages_for_parent_indexed(*body,policy,*parent,body_names.as_deref())?;
            if palette.len()!=counts[i]as usize||runtime.private_source_revision(palette.len())?!=revisions[i]
                ||palette.iter().flatten().flatten().chain(post.iter().flatten()).any(|v|!v.is_finite()){return None;}
            Some((palette,post))
        };
        let stages=if count>=4{use rayon::prelude::*;crate::session::native_parallel_work(||(0..count as usize).into_par_iter().map(read).collect::<Vec<_>>())}
            else{(0..count as usize).map(read).collect::<Vec<_>>()};
        if stages.iter().any(Option::is_none){return false;}
        let _trace=crate::native_profile::Scope::new(36);
        let mut offset=0;
        for(i,stage)in stages.into_iter().enumerate(){let (palette,post)=stage.unwrap();
            for matrix in &palette{ptr::copy_nonoverlapping(matrix.as_ptr().cast::<f32>(),matrices.add(offset*16),16);offset+=1;}
            ptr::copy_nonoverlapping(post.as_ptr().cast::<f32>(),posts.add(i*16),16);
        }true
    }));if matches!(result,Ok(true)){0}else{-1}
}

#[no_mangle]
pub unsafe extern "C" fn ok_part_skin_source_hash(s:*mut OkSession,index:i32,out:*mut u8)->i32 {
    if index<0||out.is_null(){return -1;}
    let Some(s)=sess_ref(s) else{return -1;};let Some(part)=s.parts.get(index as usize) else{return -1;};
    let bytes=if let Some(r)=&part.runtime_accessory {
        let mesh=r.source_mesh();
        bincode::serialize(&(&mesh.rest_positions,&mesh.weights,&mesh.bone_idx,&mesh.bind_poses,&mesh.bone_names))
    }else{bincode::serialize(&(&part.rest_positions,&part.weights,&part.bone_idx,&part.bind_poses,&part.rigid_follow_bone))};
    let Ok(bytes)=bytes else{return -1;};use sha2::{Digest,Sha256};let hash=Sha256::digest(bytes);ptr::copy_nonoverlapping(hash.as_ptr(),out,32);0
}
#[no_mangle]
pub unsafe extern "C" fn ok_part_skin_source_hash_v2(s:*mut OkSession,index:i32,out:*mut u8)->i32 {
    if index<0||out.is_null(){return -1;}
    let Some(session)=sess_ref(s) else{return -1;};
    let Some(part)=session.parts.get(index as usize) else{return -1;};
    let mut original=[0u8;32];
    if ok_part_skin_source_hash(s,index,original.as_mut_ptr())!=0{return -1;}
    let contract=match &part.runtime_accessory {Some(r)=>r.skin_contract(),None=>bincode::serialize(&(&part.parent_bone,part.skel_kind))};
    let Ok(contract)=contract else{return -1;};
    use sha2::{Digest,Sha256};let mut digest=Sha256::new();
    digest.update(b"OpenKoikatsu.skin-source.v2\0");digest.update(original);digest.update(contract);
    let hash=digest.finalize();ptr::copy_nonoverlapping(hash.as_ptr(),out,32);0
}
#[no_mangle]
pub unsafe extern "C" fn ok_part_raw_morph(s:*mut OkSession,index:i32,morph:i32,name:*mut *const c_char,indices:*mut u32,deltas:*mut f32,capacity:i32)->i32 {
    if index<0||morph<0||capacity<0{return -1;}
    let Some(s)=sess_ref(s) else{return -1;};let Some((label,values))=s.parts.get(index as usize).and_then(|p|p.blend_shapes.get(morph as usize)) else{return -1;};
    if values.len()>i32::MAX as usize||values.iter().any(|(_,v)|v.iter().any(|x|!x.is_finite())){return -1;}
    let count=values.len() as i32;if capacity==0&&indices.is_null()&&deltas.is_null(){return count;}
    if name.is_null()||indices.is_null()||deltas.is_null()||capacity<count{return -1;}
    for (i,(index,value)) in values.iter().enumerate(){ptr::write(indices.add(i),*index);ptr::copy_nonoverlapping(value.as_ptr(),deltas.add(i*3),3);}
    ptr::write(name,label.as_ptr());count
}

#[repr(C)]
pub struct OkMeshView {
    pub role: *const c_char,
    pub name: *const c_char,
    pub positions: *const f32,
    pub n_verts: i32,
    pub normals: *const f32,
    pub indices: *const u32,
    pub n_idx: i32,
    pub uvs: *const f32,
    pub weights: *const f32,
    pub bone_idx: *const u16,
    pub png: *const u8,
    pub png_len: i32,
    pub alpha_mask: *const u8,
    pub alpha_mask_len: i32,
    pub hair_gloss: *const u8,
    pub hair_gloss_len: i32,
    pub detail_mask: *const u8,
    pub detail_mask_len: i32,
    pub line_mask: *const u8,
    pub line_mask_len: i32,
    pub normal_map: *const u8,
    pub normal_map_len: i32,
    pub normal_mask: *const u8,
    pub normal_mask_len: i32,
    pub ramp_png: *const u8,
    pub ramp_png_len: i32,
    pub shadow_depth: f32,
    pub line_width: f32,
    pub line_depth: f32,
    pub kano_ver_shift: f32,
    pub blend: *const c_char,
    pub unlit: i32,
    pub uv2: *const f32,
    pub uv4: *const f32,
    pub overtex_png: *const u8,
    pub overtex_png_len: i32,
    pub overcolor: [f32; 4],
    pub shadow_color: [f32; 4],
    pub line_color: [f32; 4],
    pub shadow_extend: f32,
    pub shadow_extend_another: f32,
    pub main_tex_st: [f32; 4],
    pub alpha_mask_st: [f32; 4],
    pub normal_map_st: [f32; 4],
    pub normal_mask_st: [f32; 4],
    pub detail_mask_st: [f32; 4],
    pub line_mask_st: [f32; 4],
    pub normal_detail_scale: f32,
    pub face_normal: f32,
    pub face_shadow: f32,
    pub line_tex_on: f32,
    pub hair_gloss_st: [f32; 4],
    pub spec_height: f32,
    pub specular_power: f32,
    pub specular_power_nail: f32,
    pub specular_color: [f32; 4],
    pub no_tex_specular: f32,
    pub metallic: f32,
    pub smoothness: f32,
    pub shader_kind: i32,
    pub rest_positions: *const f32,
    pub parent_bone: *const c_char,
    pub rigid_follow_bone: *const c_char,
    pub skel_kind: i32,
    pub rest_tangents: *const f32,
    pub reference_normals: *const f32,
    pub uv3: *const f32,
    pub vertex_colors: *const u8,
    pub overtex2_png: *const u8,
    pub overtex2_png_len: i32,
    pub overcolor2: [f32; 4],
    pub nip_size: f32,
    pub nip_specular: f32,
    pub nip_toggle: f32,
    pub tex1_mask: f32,
    pub texcoord2_st: [f32; 4],
    pub texcoord3_st: [f32; 4],
    pub normal_detail: *const u8,
    pub normal_detail_len: i32,
    pub normal_detail_st: [f32; 4],
    pub render_visible: i32,
    pub item_draw: i32,
    pub item_colors: [[f32; 4]; 4],
    pub color_mask: *const u8,
    pub color_mask_len: i32,
    pub color_mask_st: [f32; 4],
    pub skin_line_width: f32,
    pub eye_draw:i32,
    pub eye_textures:[*const u8;2],
    pub eye_texture_lens:[i32;2],
    pub eye_colors:[[f32;4];2],
    pub eye_rotations:[f32;3],
    pub eye_color:[f32;4],
    pub eye_highlight:f32,
    pub eye_sample_uvs:*const f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct OkUnrealBindings {
    pub log: Option<unsafe extern "C" fn(i32, *const c_char)>,
}

#[repr(C)]
pub struct OkRustBindings {
    pub session_open: unsafe extern "C" fn(
        *const c_char,
        *const c_char,
        i32,
        *mut *mut OkSession,
        *mut c_char,
        i32,
    ) -> i32,
    pub session_free: unsafe extern "C" fn(*mut OkSession),
    pub part_count: unsafe extern "C" fn(*mut OkSession) -> i32,
    pub part_at: unsafe extern "C" fn(*mut OkSession, i32, *mut OkMeshView) -> i32,
    pub apply_shape: unsafe extern "C" fn(*mut OkSession, *const f32, i32, *const f32, i32) -> i32,
    pub skin: unsafe extern "C" fn(*mut OkSession) -> i32,
    pub shape_body: unsafe extern "C" fn(*mut OkSession, *mut f32, i32) -> i32,
    pub shape_face: unsafe extern "C" fn(*mut OkSession, *mut f32, i32) -> i32,
    pub coord_count: unsafe extern "C" fn(*mut OkSession) -> i32,
    pub skel_bone_count: unsafe extern "C" fn(*mut OkSession) -> i32,
    pub skel_bone_at: unsafe extern "C" fn(*mut OkSession, i32, *mut OkBoneView) -> i32,
    pub morph_count: unsafe extern "C" fn(*mut OkSession, i32) -> i32,
    pub morph_at: unsafe extern "C" fn(*mut OkSession, i32, i32, *mut OkMorphView) -> i32,
    pub mouth_pair: unsafe extern "C" fn(*mut OkSession, i32, i32, *mut i32, *mut i32) -> i32,
    pub mouth_pattern_count: unsafe extern "C" fn(*mut OkSession, i32) -> i32,
    pub apply_anim_time: unsafe extern "C" fn(*mut OkSession, f32) -> i32,
    pub anim_duration: unsafe extern "C" fn(*mut OkSession) -> f32,
    pub clip_count: unsafe extern "C" fn(*mut OkSession) -> i32,
    pub clip_name: unsafe extern "C" fn(*mut OkSession, i32, *mut c_char, i32) -> i32,
    pub set_anim_clip: unsafe extern "C" fn(*mut OkSession, *const c_char) -> i32,
    pub clip_source_count: unsafe extern "C" fn(*mut OkSession) -> i32,
    pub clip_source_name: unsafe extern "C" fn(*mut OkSession, i32, *mut c_char, i32) -> i32,
    pub clip_count_for_source: unsafe extern "C" fn(*mut OkSession, *const c_char) -> i32,
    pub clip_name_for_source: unsafe extern "C" fn(*mut OkSession, *const c_char, i32, *mut c_char, i32) -> i32,
    pub set_anim_scene_mode: unsafe extern "C" fn(*mut OkSession, i32) -> i32,
    pub apply_anim_frame: unsafe extern "C" fn(*mut OkSession, f32, f32) -> i32,
    pub anim_looping: unsafe extern "C" fn(*mut OkSession) -> i32,
    pub hand_shape_count: unsafe extern "C" fn(*mut OkSession) -> i32,
    pub set_hand_shape: unsafe extern "C" fn(*mut OkSession, i32, i32, i32, i32, f32) -> i32,
    pub face_pattern_count: unsafe extern "C" fn(*mut OkSession, i32, i32) -> i32,
    pub face_pair: unsafe extern "C" fn(*mut OkSession, i32, i32, i32, *mut i32, *mut i32) -> i32,
    pub face_baseline_weight: unsafe extern "C" fn(*mut OkSession, i32, i32) -> f32,
    pub face_default_rate: unsafe extern "C" fn(*mut OkSession, i32) -> f32,
    pub face_runtime_setting: unsafe extern "C" fn(*mut OkSession, i32) -> f32,
    pub eye_material_setting: unsafe extern "C" fn(*mut OkSession, i32, i32) -> f32,
    pub set_eye_gaze: unsafe extern "C" fn(*mut OkSession, f32, f32, f32) -> i32,
    pub eye_look_pattern_count: unsafe extern "C" fn(*mut OkSession) -> i32,
    pub step_eye_look: unsafe extern "C" fn(*mut OkSession,f32,f32,f32,i32,f32,i32,*mut f32) -> i32,
    pub external_body_pose_at: unsafe extern "C" fn(*mut OkSession,i32,*mut OkBodyPose) -> i32,
    pub apply_external_body_frame: unsafe extern "C" fn(*mut OkSession,f32,f32,*const OkBodyPose,i32) -> i32,
    pub anim_float_count: unsafe extern "C" fn(*mut OkSession) -> i32,
    pub anim_float_at: unsafe extern "C" fn(*mut OkSession,i32,*mut c_char,i32,*mut f32) -> i32,
    pub set_anim_float: unsafe extern "C" fn(*mut OkSession,*const c_char,f32) -> i32,
    pub set_studio_animation: unsafe extern "C" fn(*mut OkSession,*const c_char,i32,f32,f32,i32,f32) -> i32,
    pub set_studio_animation_profile: unsafe extern "C" fn(*mut OkSession,*const c_char,i32,f32,f32,i32,f32,*const c_char,*const c_char) -> i32,
    pub studio_yure_flag: unsafe extern "C" fn(*mut OkSession,i32) -> i32,
    pub set_studio_animation_pv_profile: unsafe extern "C" fn(*mut OkSession,*const c_char,i32,f32,f32,i32,f32,*const c_char,*const c_char,i32) -> i32,
    pub set_studio_pv: unsafe extern "C" fn(*mut OkSession,i32,i32) -> i32,
    pub studio_pv_flag: unsafe extern "C" fn(*mut OkSession,i32) -> i32,
    pub studio_pv_bone: unsafe extern "C" fn(*mut OkSession,i32,i32) -> i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct OkBodyPose {
    pub bone: i32,
    pub position: [f32;3],
    pub rotation: [f32;4],
    pub scale: [f32;3],
}

pub(crate) unsafe extern "C" fn external_body_pose_at(s:*mut OkSession,index:i32,out:*mut OkBodyPose) -> i32 {
    if out.is_null() || index < 0 { return 1; }
    let Some(sess)=sess_ref(s) else { return 1; };
    if sess.body_shaped.is_none() {
        let body=sess.shape_body.clone();let face=sess.shape_face.clone();
        sess.apply_shape(&body,&face);
    }
    let export=sess.ensure_skel_export();
    let Some(b)=export.bones.get(index as usize) else { return 1; };
    if b.pose_locked { return 1; }
    let name=b.name.clone();
    let Some(tf)=sess.body_shaped.as_ref().and_then(|sk|sk.tfs.values().find(|tf|tf.name.eq_ignore_ascii_case(&name))) else { return 1; };
    *out=OkBodyPose{bone:index,position:tf.pos,rotation:tf.rot,scale:tf.scale};
    0
}

pub(crate) unsafe extern "C" fn apply_external_body_frame(s:*mut OkSession,t:f32,dt:f32,poses:*const OkBodyPose,count:i32) -> i32 {
    if count < 0 || count > 65536 || (count>0 && poses.is_null()) { return 1; }
    let Some(sess)=sess_ref(s) else { return 1; };
    let frame=if count==0 { &[] } else { std::slice::from_raw_parts(poses,count as usize) };
    if sess.apply_external_body_frame(t,dt,frame) { 0 } else { 1 }
}

#[repr(C)]
pub struct OkMorphView {
    pub name: *const c_char,
    pub indices: *const u32,
    pub deltas: *const f32,
    pub n: i32,
}

#[repr(C)]
pub struct OkBoneView {
    pub name: *const c_char,
    pub parent: i32,
    pub rest_pos: [f32; 3],
    pub rest_rot: [f32; 4],
    pub rest_scl: [f32; 3],
    pub pose_pos: [f32; 3],
    pub pose_rot: [f32; 4],
    pub pose_scl: [f32; 3],
    pub layer: i32,
}

static UNREAL: Mutex<OkUnrealBindings> = Mutex::new(OkUnrealBindings { log: None });

pub(crate) fn ffi_log(level: i32, msg: &str) {
    if let Ok(b) = UNREAL.lock() {
        if let Some(log) = b.log {
            if let Ok(c) = std::ffi::CString::new(msg) {
                unsafe { log(level, c.as_ptr()) };
            }
        }
    }
}

pub(crate) unsafe fn cstr_opt(p: *const c_char) -> Option<String> {
    if p.is_null() {
        return None;
    }
    CStr::from_ptr(p).to_str().ok().map(|s| s.to_string())
}

pub(crate) fn write_err(buf: *mut c_char, cap: i32, msg: &str) {
    crate::write_err(buf, cap as c_int, msg);
}

#[no_mangle]
pub unsafe extern "C" fn ok_animation_pairs(s:*mut OkSession,out:*mut c_char,cap:i32)->i32 {
    let Some(session)=sess_ref(s) else {return -1;};
    let Ok(Ok(json))=std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| serde_json::to_string(&crate::h_playback::pairs(&session.game_root)))) else {return -1;};
    if out.is_null()||cap<=0 {return json.len() as i32+1;}
    if cap as usize<=json.len() {return -1;}
    write_err(out,cap,&json);0
}

/// Whether opening this OK card (outfit, clothes states; null states = all worn) is served by the open cache:
/// 1 cached, 0 not cached, -1 not an OK library card (no open cache) or no base library configured.
#[no_mangle]
pub unsafe extern "C" fn ok_session_cache_ready(card:*const c_char,outfit:i32,clothes:*const u8)->i32 {
    let Some(card)=cstr_opt(card) else{return -1;};
    let card=std::path::PathBuf::from(card);
    let Some(lib)=crate::oklib::configured_library() else{return -1;};
    // OK cards and original KKS cards (library + zipmods) share the open cache; a portable okPv card does not.
    if !crate::oklib::is_library_card(&card)&&std::fs::read(&card).ok().and_then(|b|crate::portable::png_snapshot(&b).ok()).is_none_or(|s|s.1.is_some()){return -1;}
    let states=if clothes.is_null(){[0u8;9]}else{let mut s=[0u8;9];std::ptr::copy_nonoverlapping(clothes,s.as_mut_ptr(),9);s};
    match crate::oklib_session_cache_key_states(&lib,&card,outfit,states) {Some(key)=>i32::from(crate::oklib::session_cache_has(&key)),None=>-1}
}

/// Called between independent scene loads, outside open/prepare jobs.
#[no_mangle]
pub extern "C" fn ok_release_source_caches() {
    crate::unity::release_bundle_caches();
    if let Some(store) = crate::oklib::active() { store.forget_models(); }
}

/// 1: this thread's opens are background work that gives way to foreground opens (stops at a stage boundary with an
/// error while one runs); 0: foreground (default).
#[no_mangle]
pub extern "C" fn ok_set_thread_background(on:i32) { crate::set_thread_background(on!=0); }

/// Builds the per-process memos card opens share for this game folder / OK base library (see `prewarm`).
/// 0 on success. Meant for a background thread at reader start.
#[no_mangle]
pub unsafe extern "C" fn ok_prewarm(root:*const c_char)->i32 {
    // An empty root: the configured OK base library (what OK cards open from).
    let root=match cstr_opt(root).filter(|r|!r.is_empty()) {Some(r)=>std::path::PathBuf::from(r),None=>match crate::oklib::configured_library(){Some(l)=>l,None=>return -1}};
    match std::panic::catch_unwind(||crate::prewarm(&root)) {Ok(Ok(()))=>0,_=>1}
}

// Common authored resources have their own root; menu queries must never
// replace the live session's shared-clip root or reset its clock.
#[no_mangle]
pub unsafe extern "C" fn ok_common_animation_pairs(root:*const c_char,out:*mut c_char,cap:i32)->i32 {
    let Some(root)=cstr_opt(root) else{return -1;};
    let Ok(Ok(json))=std::panic::catch_unwind(||serde_json::to_string(&crate::h_playback::pairs(std::path::Path::new(&root)))) else{return -1;};
    if out.is_null()||cap<=0{return json.len() as i32+1;}if cap as usize<=json.len(){return -1;}write_err(out,cap,&json);0
}
#[no_mangle]
pub unsafe extern "C" fn ok_common_interaction_poses(root:*const c_char,out:*mut c_char,cap:i32)->i32 {
    let Some(root)=cstr_opt(root)else{return -1;};
    let Ok(Ok(json))=std::panic::catch_unwind(||serde_json::to_string(&crate::h_playback::interaction_poses(std::path::Path::new(&root))))else{return -1;};
    if out.is_null()||cap<=0{return json.len() as i32+1;}if cap as usize<=json.len(){return -1;}write_err(out,cap,&json);0
}
#[no_mangle]
pub unsafe extern "C" fn ok_common_animation_pair_clip(root:*const c_char,key:*const c_char,layer:i32,out:*mut c_char,cap:i32)->i32 {
    let (Some(root),Some(key))=(cstr_opt(root),cstr_opt(key)) else{return -1;};
    let Ok(Some(clip))=std::panic::catch_unwind(||crate::anim::native_pair_clip(std::path::Path::new(&root),&key,layer)) else{return -1;};
    if out.is_null()||cap<=0{return clip.len()as i32+1;}if cap as usize<=clip.len(){return -1;}write_err(out,cap,&clip);0
}
#[no_mangle]
pub unsafe extern "C" fn ok_select_common_animation(s:*mut OkSession,root:*const c_char,key:*const c_char)->i32 {
    let (Some(s),Some(root),Some(key))=(sess_ref(s),cstr_opt(root),cstr_opt(key)) else{return 1;};
    let root=std::path::PathBuf::from(root);
    // The real file system: under a running base library, gamefs replays paths inside it (the animation root is not recorded).
    if !std::path::Path::is_file(&root.join("manifest.json"))||!crate::anim::selectable_clip(&root,&key){return 1;}
    let previous=s.game_root.clone();s.game_root=root;
    let code=set_anim_clip(s,std::ffi::CString::new(key).unwrap().as_ptr());
    if code!=0{s.game_root=previous;}code
}

#[no_mangle]
pub unsafe extern "C" fn ok_animation_pair_clip(s:*mut OkSession,key:*const c_char,layer:i32,out:*mut c_char,cap:i32)->i32{
    let Some(session)=sess_ref(s)else{return -1;};let Some(key)=cstr_opt(key)else{return -1;};
    let Ok(Some(clip))=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||crate::anim::native_pair_clip(&session.game_root,&key,layer)))else{return -1;};
    if out.is_null()||cap<=0{return clip.len()as i32+1;}if cap as usize<=clip.len(){return -1;}write_err(out,cap,&clip);0
}

#[no_mangle]
pub unsafe extern "C" fn ok_animation_pair_frame(female:*mut OkSession,male:*mut OkSession,time:f32,dt:f32,state:*const c_char,female_motion:*const c_char,male_motion:*const c_char,err:*mut c_char,cap:i32)->i32 {
    animation_pair_frame_impl(female,male,time,dt,state,female_motion,male_motion,err,cap,None,(&[],&[]))
}
#[no_mangle]
pub unsafe extern "C" fn ok_animation_pair_frame_gpu(female:*mut OkSession,male:*mut OkSession,time:f32,dt:f32,state:*const c_char,female_motion:*const c_char,male_motion:*const c_char,
    female_parts:*const i32,female_revisions:*const u64,female_count:i32,male_parts:*const i32,male_revisions:*const u64,male_count:i32,err:*mut c_char,cap:i32)->i32 {
    if female.is_null()||male.is_null()||female==male{write_err(err,cap,"invalid paired handles");return 1;}
    let ownership=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||{
        Some((private_render_exclusions(&*female,female_parts,female_revisions,female_count)?,
            private_render_exclusions(&*male,male_parts,male_revisions,male_count)?))
    }));
    // Code 2 is exclusively a pre-frame ownership rejection: caller may safely
    // run the ordinary solver once. Other errors must never advance it twice.
    let Ok(Some((f,m)))=ownership else{write_err(err,cap,"GPU renderer ownership changed");return 2;};
    animation_pair_frame_impl(female,male,time,dt,state,female_motion,male_motion,err,cap,None,(&f,&m))
}

unsafe fn animation_pair_frame_impl(female:*mut OkSession,male:*mut OkSession,time:f32,dt:f32,state:*const c_char,female_motion:*const c_char,male_motion:*const c_char,err:*mut c_char,cap:i32,observer:Option<PrivateSkinObserver>,excluded:(&[usize],&[usize]))->i32 {
    // Do not create aliased mutable references from a repeated handle.
    if female.is_null()||male.is_null()||female==male||!time.is_finite()||time<0.||!dt.is_finite()||dt<0. {write_err(err,cap,"invalid paired frame input");return 1;}
    let (Some(state),Some(fm),Some(mm))=(cstr_opt(state),cstr_opt(female_motion),cstr_opt(male_motion)) else {write_err(err,cap,"invalid UTF-8 pair identity");return 1;};
    let female=&mut *female;let male=&mut *male;
    if female.sex!=1||male.sex!=0||female.game_root!=male.game_root {write_err(err,cap,"pair needs opposite roles from the same game");return 1;}
    let result=std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let profile=std::env::var("OK_ANIM_PROFILE").ok().as_deref()==Some("1");let start=std::time::Instant::now();
        let setups={let _trace=crate::native_profile::Scope::new(0);crate::h_playback::validated_pair_setups(female,male,&state,&fm,&mm)?};
        let validated=start.elapsed();
        // Keep the established stage barriers/dispatch: an outer worker
        // transaction did not improve complete-frame measurements.
        let work=||{
        {let _trace=crate::native_profile::Scope::new(1);
        crate::h_playback::set_native_pair_parameters(female,male);
        crate::session::native_parallel_work(||rayon::join(
            ||{let _trace=crate::native_profile::Scope::new(14);female.sample_paired_frame(time)},
            ||{let _trace=crate::native_profile::Scope::new(14);male.sample_paired_frame(time)}));}
        let sampled=start.elapsed();
        let contacts={let _trace=crate::native_profile::Scope::new(2);crate::h_playback::apply_contacts_prepared_with_policy(female,male,&state,setups,false,true,dt)?};
        let contacted=start.elapsed();
        // Contact solving is a barrier: both sampled actors must be visible
        // to the coupled solver before either actor advances private physics.
        {let _trace=crate::native_profile::Scope::new(3);crate::session::native_parallel_work(||rayon::join(
            ||female.finish_paired_dynamics_filtered(dt,observer,excluded.0),||male.finish_paired_dynamics_filtered(dt,observer.map(|o|PrivateSkinObserver{role:1,..o}),excluded.1)));}
        if profile{eprintln!("OK animation pair validate_ms={:.3} sample_ms={:.3} contacts_ms={:.3} dynamics_ms={:.3}",validated.as_secs_f64()*1000.,(sampled-validated).as_secs_f64()*1000.,(contacted-sampled).as_secs_f64()*1000.,(start.elapsed()-contacted).as_secs_f64()*1000.);}
        Ok::<usize,String>(contacts)
        };
        let _trace=crate::native_profile::Scope::new(24);
        work()
    }));
    match result {Ok(Ok(_))=>0,Ok(Err(message))=>{write_err(err,cap,&message);1},Err(_)=>{write_err(err,cap,"paired playback failed");1}}
}

#[no_mangle]
pub unsafe extern "C" fn ok_animation_pair_frame_observed(female:*mut OkSession,male:*mut OkSession,time:f32,dt:f32,state:*const c_char,female_motion:*const c_char,male_motion:*const c_char,
    callback:PrivateSkinCallback,context:*mut std::ffi::c_void,err:*mut c_char,cap:i32)->i32 {
    let observer=PrivateSkinObserver{callback,context:context as usize,role:0};
    let result=animation_pair_frame_impl(female,male,time,dt,state,female_motion,male_motion,err,cap,Some(observer),(&[],&[]));
    if result!=0{return result;}
    // The successful paired-frame validation established distinct, non-null
    // handles. Capture their exclusive session borrows, not raw pointers,
    // across the scoped worker join (as in ok_animation_pair_frame).
    let female=&mut *female;let male=&mut *male;
    let result=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||{
        crate::session::native_parallel_work(||rayon::join(
            ||female.skin_all_observed(Some(observer)),
            ||male.skin_all_observed(Some(PrivateSkinObserver{role:1,..observer}))));
    }));
    if result.is_err(){write_err(err,cap,"observed paired skin failed");return 1;}0
}

#[no_mangle]
pub unsafe extern "C" fn ok_session_open_clothes(game:*const c_char,card:*const c_char,outfit:i32,states:*const u8,out:*mut *mut OkSession,err:*mut c_char,cap:i32)->i32 {
    if states.is_null()||out.is_null(){write_err(err,cap,"missing clothing state or output");return 1;}
    *out=std::ptr::null_mut();
    let values:[u8;9]=std::slice::from_raw_parts(states,9).try_into().unwrap();
    if values.iter().any(|v|*v>3){write_err(err,cap,"invalid clothing state");return 1;}
    let _scope=crate::unity::request_clothes_states(values);
    session_open(game,card,outfit,out,err,cap)
}

/// `session_open` for an OK Studio character: `status_json` is a `studio_status::StatusOverride` (tears, blush,
/// nipples, skin gloss, fluids, accessory slots); `states` (nullable) the clothing states as in `ok_session_open_clothes`.
#[no_mangle]
pub unsafe extern "C" fn ok_session_open_status(game:*const c_char,card:*const c_char,outfit:i32,states:*const u8,status_json:*const c_char,out:*mut *mut OkSession,err:*mut c_char,cap:i32)->i32 {
    if out.is_null(){write_err(err,cap,"missing output");return 1;}
    *out=std::ptr::null_mut();
    let status:crate::studio_status::StatusOverride=match serde_json::from_str(&cstr_opt(status_json).unwrap_or_else(||"{}".into())) {
        Ok(v)=>v, Err(e)=>{write_err(err,cap,&format!("character state: {e}"));return 1;}
    };
    let _status=crate::studio_status::request(status);
    if states.is_null() { return session_open(game,card,outfit,out,err,cap); }
    ok_session_open_clothes(game,card,outfit,states,out,err,cap)
}

pub(crate) unsafe extern "C" fn session_open(
    game_root_utf8: *const c_char,
    card_path_utf8: *const c_char,
    outfit: i32,
    out: *mut *mut OkSession,
    err_buf: *mut c_char,
    err_cap: i32,
) -> i32 {
    let run = crate::run_no_panic(|| {
        // A foreground open makes background opens (clothing variant warm-up) yield the card pack mount.
        let _foreground = crate::ForegroundOpen::begin();
        let game = cstr_opt(game_root_utf8).ok_or_else(|| "null game_root".to_string())?;
        let card = cstr_opt(card_path_utf8);
        // Original KKS card with a base library configured: the library plus the card's zipmods.
        if let Some(card) = card.as_deref() {
            if let Some(result) = crate::oklib::open_original_card(std::path::Path::new(&game), std::path::Path::new(card), outfit) {
                if result.is_ok() { ffi_log(0, &format!("ok-extract: {card}: {}", crate::oklib::LAST_OPEN.lock().unwrap())); }
                return result;
            }
        }
        let game = crate::oklib::root_for_card(std::path::Path::new(&game), card.as_deref().map(std::path::Path::new)).to_string_lossy().into_owned();
        let library_card = card.as_deref().is_some_and(|c| crate::oklib::is_library_card(std::path::Path::new(c)));
        if game.is_empty() {
            let path = card.as_deref().ok_or("missing portable pack path")?;
            return crate::portable::load_outfit(std::path::Path::new(path),outfit);
        }
        let sess = crate::session::open(
            std::path::Path::new(&game),
            card.as_deref().map(std::path::Path::new),
            outfit,
        )?;
        if library_card {
            let pack = card.as_deref().and_then(|c| std::fs::read(c).ok()).is_some_and(|b| crate::oklib::png_chunk(&b, crate::oklib::PACK_CHUNK).is_some());
            ffi_log(0, &format!("ok-extract: {}: OK card from OK data only (card pack {}, models {}, zipmod data used {})",
                card.as_deref().unwrap_or_default(), if pack { "mounted" } else { "none" },
                if pack { crate::oklib::LAST_PACK_MODELS.load(std::sync::atomic::Ordering::Relaxed) } else { 0 }, crate::gamefs::fallback_used()));
        }
        Ok(sess)
    });
    match run {
        Ok(sess) => {
            if out.is_null() {
                write_err(err_buf, err_cap, "null out");
                return 1;
            }
            unsafe { *out = Box::into_raw(Box::new(sess)) };
            0
        }
        Err(e) => {
            ffi_log(2, &e);
            write_err(err_buf, err_cap, &e);
            1
        }
    }
}

/// 1 when the session reads the game installation (game mode), 0 when it holds everything itself
/// (portable packs and sessions built from the OK base library).
#[no_mangle]
pub unsafe extern "C" fn ok_session_has_game_root(s: *mut OkSession) -> i32 {
    sess_ref(s).map_or(0, |s| (!s.game_root.as_os_str().is_empty()) as i32)
}

pub(crate) unsafe extern "C" fn session_free(s: *mut OkSession) {
    if !s.is_null() {
        drop(Box::from_raw(s));
    }
}

unsafe fn sess_ref<'a>(s: *mut OkSession) -> Option<&'a mut OkSession> {
    if s.is_null() {
        None
    } else {
        Some(&mut *s)
    }
}

pub(crate) unsafe extern "C" fn part_count(s: *mut OkSession) -> i32 {
    sess_ref(s).map(|x| x.parts.len() as i32).unwrap_or(0)
}

pub(crate) unsafe extern "C" fn part_at(s: *mut OkSession, i: i32, out: *mut OkMeshView) -> i32 {
    if out.is_null() {
        return 1;
    }
    let Some(sess) = sess_ref(s) else {
        return 1;
    };
    let idx = i as usize;
    let (rest_ptr, ref_normal_ptr, ref_tangent_ptr, export_idx) = match sess.skel_export.as_ref() {
        Some(ex) => {
            let rp = ex
                .part_rest
                .get(idx)
                .filter(|v| !v.is_empty())
                .map(|v| v.as_ptr() as *const f32)
                .unwrap_or(ptr::null());
            let rn = ex.part_ref_normals.get(idx).filter(|v| !v.is_empty())
                .map(|v| v.as_ptr() as *const f32).unwrap_or(ptr::null());
            let rt = ex.part_ref_tangents.get(idx).filter(|v| !v.is_empty())
                .map(|v| v.as_ptr() as *const f32).unwrap_or(ptr::null());
            let bi = ex
                .part_bone_idx
                .get(idx)
                .filter(|v| !v.is_empty())
                .map(|v| v.as_ptr() as *const u16);
            (rp, rn, rt, bi)
        }
        None => (ptr::null(), ptr::null(), ptr::null(), None),
    };
    let Some(p) = sess.parts.get(idx) else {
        return 1;
    };
    *out = OkMeshView {
        role: p.role_c.as_ptr(),
        name: p.name_c.as_ptr(),
        positions: p.positions.as_ptr() as *const f32,
        n_verts: p.positions.len() as i32,
        normals: if p.normals.is_empty() {
            ptr::null()
        } else {
            p.normals.as_ptr() as *const f32
        },
        indices: p.indices.as_ptr(),
        n_idx: p.indices.len() as i32,
        uvs: if p.uvs.is_empty() {
            ptr::null()
        } else {
            p.uvs.as_ptr() as *const f32
        },
        weights: if p.weights.is_empty() {
            ptr::null()
        } else {
            p.weights.as_ptr() as *const f32
        },
        bone_idx: export_idx.unwrap_or_else(|| {
            if p.bone_idx.is_empty() {
                ptr::null()
            } else {
                p.bone_idx.as_ptr() as *const u16
            }
        }),
        png: p.png.as_ref().map(|b| b.as_ptr()).unwrap_or(ptr::null()),
        png_len: p.png.as_ref().map(|b| b.len() as i32).unwrap_or(0),
        alpha_mask: p.alpha_mask.as_ref().map(|b| b.as_ptr()).unwrap_or(ptr::null()),
        alpha_mask_len: p.alpha_mask.as_ref().map(|b| b.len() as i32).unwrap_or(0),
        hair_gloss: p
            .hair_gloss
            .as_ref()
            .map(|b| b.as_ptr())
            .unwrap_or(ptr::null()),
        hair_gloss_len: p.hair_gloss.as_ref().map(|b| b.len() as i32).unwrap_or(0),
        detail_mask: p.detail_mask.as_ref().map(|b| b.as_ptr()).unwrap_or(ptr::null()),
        detail_mask_len: p.detail_mask.as_ref().map(|b| b.len() as i32).unwrap_or(0),
        line_mask: p.line_mask.as_ref().map(|b| b.as_ptr()).unwrap_or(ptr::null()),
        line_mask_len: p.line_mask.as_ref().map(|b| b.len() as i32).unwrap_or(0),
        normal_map: p.normal_map.as_ref().map(|b| b.as_ptr()).unwrap_or(ptr::null()),
        normal_map_len: p.normal_map.as_ref().map(|b| b.len() as i32).unwrap_or(0),
        normal_mask: p.normal_mask.as_ref().map(|b| b.as_ptr()).unwrap_or(ptr::null()),
        normal_mask_len: p.normal_mask.as_ref().map(|b| b.len() as i32).unwrap_or(0),
        ramp_png: sess.ramp_png.as_ref().map(|b| b.as_ptr()).unwrap_or(ptr::null()),
        ramp_png_len: sess.ramp_png.as_ref().map(|b| b.len() as i32).unwrap_or(0),
        shadow_depth: sess.shadow_depth,
        line_width: sess.line_width,
        line_depth: sess.line_depth,
        kano_ver_shift: sess.kano_ver_shift,
        blend: p.blend_c.as_ptr(),
        unlit: if p.unlit { 1 } else { 0 },
        uv2: if p.uv2.is_empty() {
            ptr::null()
        } else {
            p.uv2.as_ptr() as *const f32
        },
        uv4: if p.uv4.is_empty() {
            ptr::null()
        } else {
            p.uv4.as_ptr() as *const f32
        },
        overtex_png: p
            .overtex_png
            .as_ref()
            .map(|b| b.as_ptr())
            .unwrap_or(ptr::null()),
        overtex_png_len: p.overtex_png.as_ref().map(|b| b.len() as i32).unwrap_or(0),
        overcolor: p.draw_overlay_color(0),
        shadow_color: p.shadow_color,
        line_color: p.line_color,
        shadow_extend: p.shadow_extend,
        shadow_extend_another: p.shadow_extend_another,
        main_tex_st: p.main_tex_st,
        alpha_mask_st: p.alpha_mask_st,
        normal_map_st: p.normal_map_st,
        normal_mask_st: p.normal_mask_st,
        detail_mask_st: p.detail_mask_st,
        line_mask_st: p.line_mask_st,
        normal_detail_scale: p.normal_detail_scale,
        face_normal: p.face_normal,
        face_shadow: p.face_shadow,
        line_tex_on: p.line_tex_on,
        hair_gloss_st: p.hair_gloss_st,
        spec_height: p.spec_height,
        specular_power: p.specular_power,
        specular_power_nail: p.specular_power_nail,
        specular_color: p.specular_color,
        no_tex_specular: p.no_tex_specular,
        metallic: p.metallic,
        smoothness: p.smoothness,
        shader_kind: p.shader_kind as i32,
        rest_positions: rest_ptr,
        parent_bone: if p.parent_c.as_bytes().is_empty() {
            ptr::null()
        } else {
            p.parent_c.as_ptr()
        },
        rigid_follow_bone: if p.rigid_follow_c.as_bytes().is_empty() {
            ptr::null()
        } else {
            p.rigid_follow_c.as_ptr()
        },
        skel_kind: p.skel_kind as i32,
        rest_tangents: if p.skel_kind==4 && p.rest_tangents.len()==p.positions.len() {
            p.rest_tangents.as_ptr() as *const f32
        } else if !ref_tangent_ptr.is_null() {
            ref_tangent_ptr
        } else if p.rest_tangents.len() == p.rest_positions.len() {
            p.rest_tangents.as_ptr() as *const f32
        } else {
            ptr::null()
        },
        reference_normals: ref_normal_ptr,
        uv3: if p.uv3.len() == p.positions.len() { p.uv3.as_ptr() as *const f32 } else { ptr::null() },
        vertex_colors: if p.vertex_colors.len() == p.positions.len() { p.vertex_colors.as_ptr() as *const u8 } else { ptr::null() },
        overtex2_png: p.overtex2_png.as_ref().map(|b| b.as_ptr()).unwrap_or(ptr::null()),
        overtex2_png_len: p.overtex2_png.as_ref().map(|b| b.len() as i32).unwrap_or(0),
        overcolor2: p.draw_overlay_color(1),
        nip_size: p.nip_size,
        nip_specular: p.nip_specular,
        nip_toggle: p.nip_toggle,
        tex1_mask: p.tex1_mask,
        texcoord2_st: p.texcoord2_st,
        texcoord3_st: p.texcoord3_st,
        normal_detail: p.normal_detail.as_ref().map(|b| b.as_ptr()).unwrap_or(ptr::null()),
        normal_detail_len: p.normal_detail.as_ref().map(|b| b.len() as i32).unwrap_or(0),
        normal_detail_st: p.normal_detail_st,
        render_visible: p.render_visible as i32,
        item_draw: p.item_draw.as_ref().map(|m| m.color_count as i32).unwrap_or(0),
        item_colors: p.item_draw.as_ref().map(|m| m.colors).unwrap_or([[1.0; 4]; 4]),
        color_mask: p.item_draw.as_ref().and_then(|m| m.color_mask.as_ref()).map(|b| b.as_ptr()).unwrap_or(ptr::null()),
        color_mask_len: p.item_draw.as_ref().and_then(|m| m.color_mask.as_ref()).map(|b| b.len() as i32).unwrap_or(0),
        color_mask_st: p.item_draw.as_ref().map(|m| m.color_mask_st).unwrap_or([1.0, 1.0, 0.0, 0.0]),
        skin_line_width: p.skin_line_width,
        eye_draw:p.eye_draw.as_ref().map_or(0,|d|d.kind),
        eye_textures:std::array::from_fn(|i|p.eye_draw.as_ref().and_then(|d|d.textures[i].as_ref()).map_or(ptr::null(),|b|b.as_ptr())),
        eye_texture_lens:std::array::from_fn(|i|p.eye_draw.as_ref().and_then(|d|d.textures[i].as_ref()).map_or(0,|b|b.len() as i32)),
        eye_colors:p.eye_draw.as_ref().map_or([[1.;4];2],|d|d.colors),
        eye_rotations:p.eye_draw.as_ref().map_or([0.,0.5,0.5],|d|d.rotations),
        eye_color:p.eye_draw.as_ref().map_or([1.;4],|d|d.color),
        eye_highlight:p.eye_draw.as_ref().map_or(0.,|d|d.enabled),
        eye_sample_uvs:p.eye_draw.as_ref().map_or(ptr::null(),|d|d.sample_uvs.as_ptr() as *const f32),
    };
    0
}

pub(crate) unsafe extern "C" fn skel_bone_count(s: *mut OkSession) -> i32 {
    let Some(sess) = sess_ref(s) else {
        return 0;
    };
    sess.ensure_skel_export().bones.len() as i32
}

pub(crate) unsafe extern "C" fn skel_bone_at(
    s: *mut OkSession,
    i: i32,
    out: *mut OkBoneView,
) -> i32 {
    if out.is_null() {
        return 1;
    }
    let Some(sess) = sess_ref(s) else {
        return 1;
    };
    let ex = sess.ensure_skel_export();
    let Some(b) = ex.bones.get(i as usize) else {
        return 1;
    };
    *out = OkBoneView {
        name: b.name_c.as_ptr(),
        parent: b.parent,
        rest_pos: b.rest_pos,
        rest_rot: b.rest_rot,
        rest_scl: b.rest_scl,
        pose_pos: b.pose_pos,
        pose_rot: b.pose_rot,
        pose_scl: b.pose_scl,
        layer: b.layer as i32,
    };
    0
}

pub(crate) unsafe extern "C" fn morph_count(s: *mut OkSession, part: i32) -> i32 {
    sess_ref(s)
        .and_then(|x| x.parts.get(part as usize))
        .map(|p| p.blend_shapes.len() as i32)
        .unwrap_or(0)
}

pub(crate) unsafe extern "C" fn morph_at(
    s: *mut OkSession,
    part: i32,
    morph: i32,
    out: *mut OkMorphView,
) -> i32 {
    if out.is_null() {
        return 1;
    }
    if crate::profile::var("OK_GPU_SKIN").ok().as_deref()!=Some("1") {
        let transformed=sess_ref(s).and_then(|session| {
            let part=session.parts.get(part as usize)?;
            if part.role!="body" || part.skel_kind!=1{return None;}
            crate::skel_export::body_morph_to_pose(session,part,morph as usize)
        });
        if let Some(deltas)=transformed {
            if let Some(part)=sess_ref(s).and_then(|session|session.parts.get_mut(part as usize)) {
                if let Some(target)=part.morph_dxyz.get_mut(morph as usize){*target=deltas;}
            }
        }
    }
    let Some(session) = sess_ref(s) else { return 1; };
    let gpu = crate::profile::var("OK_GPU_SKIN").ok().as_deref() == Some("1");
    if gpu { session.ensure_skel_export(); }
    let Some(p) = session.parts.get(part as usize) else {
        return 1;
    };
    let i = morph as usize;
    let Some((name, _)) = p.blend_shapes.get(i) else {
        return 1;
    };
    let idx = p.morph_idx.get(i);
    let dxyz = if gpu { session.skel_export.as_ref().and_then(|e| e.part_morphs.get(part as usize)).and_then(|m| m.get(i)) } else { p.morph_dxyz.get(i) };
    let n = idx.map(|v| v.len()).unwrap_or(0) as i32;
    *out = OkMorphView {
        name: name.as_ptr(),
        indices: idx.map(|v| v.as_ptr()).unwrap_or(ptr::null()),
        deltas: dxyz.map(|v| v.as_ptr()).unwrap_or(ptr::null()),
        n,
    };
    0
}

fn face_patterns(p:&crate::session::SessionPart,control:i32)->Option<&[(i32,i32)]> {
    match control {0=>Some(&p.mouth_patterns),1=>Some(&p.eye_patterns),2=>Some(&p.eyebrow_patterns),_=>None}
}
pub(crate) unsafe extern "C" fn face_pattern_count(s:*mut OkSession,part:i32,control:i32)->i32 {
    sess_ref(s).and_then(|s|s.parts.get(part as usize)).and_then(|p|face_patterns(p,control)).map_or(0,|p|p.len() as i32)
}
pub(crate) unsafe extern "C" fn face_pair(s:*mut OkSession,part:i32,control:i32,pattern:i32,close:*mut i32,open:*mut i32)->i32 {
    if close.is_null() || open.is_null() {return 1;}
    let Some(&(c,o))=sess_ref(s).and_then(|s|s.parts.get(part as usize)).and_then(|p|face_patterns(p,control))
        .and_then(|p|p.get(pattern as usize)) else{return 1;};
    *close=c;*open=o;0
}
pub(crate) unsafe extern "C" fn face_baseline_weight(s:*mut OkSession,part:i32,morph:i32)->f32 {
    sess_ref(s).and_then(|s|s.parts.get(part as usize)).and_then(|p|p.face_baseline_weights.get(morph as usize)).copied().unwrap_or(0.)
}
pub(crate) unsafe extern "C" fn face_default_rate(s:*mut OkSession,control:i32)->f32 {
    if !(1..=2).contains(&control) {return 0.;}
    sess_ref(s).and_then(|s|s.parts.iter().find(|p|!p.eye_patterns.is_empty() || !p.eyebrow_patterns.is_empty()))
        .map_or(if control==1 {0.92} else {1.},|p|p.face_default_rates[control as usize-1])
}
pub(crate) unsafe extern "C" fn face_runtime_setting(s:*mut OkSession,setting:i32)->f32 {
    let defaults=[30.,0.15,1.,1.,0.,0.,-0.1,-0.1,0.1,0.3,0.1,0.];
    if !(0..12).contains(&setting) {return 0.;}
    sess_ref(s).and_then(|s|s.parts.iter().find(|p|!p.eye_patterns.is_empty() || !p.eyebrow_patterns.is_empty()))
        .map_or(defaults[setting as usize],|p|p.face_runtime_settings[setting as usize])
}

pub(crate) unsafe extern "C" fn eye_material_setting(s:*mut OkSession,part:i32,setting:i32)->f32 {
    let runtime=sess_ref(s).and_then(|s|s.parts.get(part as usize)).and_then(|p|p.iris_runtime.as_ref());
    if setting==-1 {return if runtime.is_some() {1.}else{0.};}
    runtime.and_then(|r|r.settings.get(setting as usize)).copied().unwrap_or(0.)
}
pub(crate) unsafe extern "C" fn set_eye_gaze(s:*mut OkSession,left:f32,right:f32,vertical:f32)->i32 {
    if !left.is_finite() || !right.is_finite() || !vertical.is_finite() {return 1;}
    let Some(session)=sess_ref(s) else{return 1;};
    for part in &mut session.parts {
        if let Some(runtime)=part.iris_runtime.as_ref() {
            let h=if runtime.settings[0]==1. {right}else{left};
            runtime.apply(&mut part.uvs,h.clamp(-1.,1.),vertical.clamp(-1.,1.));
            if let Some(draw)=part.eye_draw.as_mut() {
                draw.apply(runtime.st(h.clamp(-1.,1.),vertical.clamp(-1.,1.)),&runtime.raw_uvs,&mut part.uv2,&mut part.uv3);
            }
        }
    }
    0
}
unsafe extern "C" fn eye_look_pattern_count(s:*mut OkSession)->i32 {
    sess_ref(s).and_then(|s|s.eye_look_config.as_ref()).map_or(0,|c|c.patterns.len() as i32)
}
unsafe extern "C" fn step_eye_look(s:*mut OkSession,x:f32,y:f32,z:f32,pattern:i32,dt:f32,enabled:i32,out:*mut f32)->i32 {
    if out.is_null()||pattern<0 {return 1;}
    let Some(session)=sess_ref(s) else{return 1;};
    let Some(rates)=session.step_eye_look([x,y,z],pattern as usize,dt,enabled!=0) else{
        let detail=session.eye_look_config.as_ref().map(|c| {
            let names=std::iter::once(c.root.as_str()).chain(std::iter::once(c.center.as_str())).chain(c.eyes.iter().map(|e|e.name.as_str()));
            let references=names.map(|name| {
                let tf=session.head_skel.as_ref().and_then(|h|h.tfs.values().find(|t|t.name==name));
                format!("{name}:{} parent={}",tf.is_some(),tf.and_then(|t|t.parent).and_then(|id|session.head_skel.as_ref()?.tfs.get(&id)).map_or("missing",|p|p.name.as_str()))
            }).collect::<Vec<_>>();
            format!("basis={} init={} references={references:?}",crate::eye_look::Basis::from_session(session,c).is_some(),crate::eye_look::Runtime::from_session(session,c).is_some())
        });
        ffi_log(1,&format!("EyeLook source rejected pattern={pattern} dt={dt} head={} body={} body_head={} {detail:?}",session.head_skel.is_some(),session.body_skel.is_some(),session.body_skel.as_ref().is_some_and(|b|b.tfs.values().any(|t|t.name=="cf_s_head"))));
        return 1;
    };
    std::ptr::copy_nonoverlapping(rates.as_ptr(),out,3);
    set_eye_gaze(s,rates[0],rates[1],rates[2])
}

pub(crate) unsafe extern "C" fn mouth_pair(
    s: *mut OkSession, part: i32, pattern: i32, close: *mut i32, open: *mut i32,
) -> i32 {
    if close.is_null() || open.is_null() { return 1; }
    let Some(pair) = sess_ref(s)
        .and_then(|x| x.parts.get(part as usize))
        .and_then(|p| p.mouth_patterns.get(pattern as usize)) else { return 1; };
    *close = pair.0;
    *open = pair.1;
    0
}

pub(crate) unsafe extern "C" fn mouth_pattern_count(s: *mut OkSession, part: i32) -> i32 {
    sess_ref(s)
        .and_then(|x| x.parts.get(part as usize))
        .map(|p| p.mouth_patterns.len() as i32)
        .unwrap_or(0)
}

pub(crate) unsafe extern "C" fn apply_anim_time(s: *mut OkSession, t: f32) -> i32 {
    let Some(sess) = sess_ref(s) else {
        return 1;
    };
    sess.apply_anim_time(t);
    0
}

pub(crate) unsafe extern "C" fn apply_anim_frame(s: *mut OkSession, t: f32, dt: f32) -> i32 {
    if !t.is_finite() || !dt.is_finite() || dt < 0.0 { return 1; }
    let Some(sess) = sess_ref(s) else { return 1; };
    if std::env::var("OK_ANIM_PROFILE").ok().as_deref()==Some("1") {
        let start=std::time::Instant::now();sess.sample_paired_frame(t);let sampled=start.elapsed();
        sess.finish_paired_dynamics(dt);
        eprintln!("OK animation native sample_ms={:.3} dynamics_ms={:.3}",sampled.as_secs_f64()*1000.,start.elapsed().as_secs_f64()*1000.-sampled.as_secs_f64()*1000.);
    }else{sess.apply_anim_frame(t, dt);}
    0
}

pub(crate) unsafe extern "C" fn anim_duration(s: *mut OkSession) -> f32 {
    sess_ref(s).map(|x| x.anim_duration()).unwrap_or(1.0)
}

pub(crate) unsafe extern "C" fn anim_looping(s: *mut OkSession) -> i32 {
    sess_ref(s).map(|x| crate::anim::clip_looping(&x.game_root, &x.anim_clip) as i32).unwrap_or(0)
}

pub(crate) unsafe extern "C" fn hand_shape_count(s: *mut OkSession) -> i32 {
    sess_ref(s).map(|x| x.hand_shapes.count() as i32).unwrap_or(0)
}

pub(crate) unsafe extern "C" fn set_hand_shape(s: *mut OkSession, side: i32, enabled: i32, a: i32, b: i32, blend: f32) -> i32 {
    if !(0..=1).contains(&side) || !(0..=1).contains(&enabled) || a < 0 || b < 0 { return 1; }
    let Some(sess) = sess_ref(s) else { return 1; };
    if sess.set_hand_shape(side as usize, enabled != 0, a as usize, b as usize, blend) { 0 } else { 1 }
}

pub(crate) unsafe extern "C" fn clip_count(s: *mut OkSession) -> i32 {
    sess_ref(s).map(|x| crate::anim::clip_names(&x.game_root).len() as i32).unwrap_or(0)
}

pub(crate) unsafe extern "C" fn clip_name(s: *mut OkSession, index: i32, out: *mut c_char, cap: i32) -> i32 {
    if out.is_null() || cap <= 0 || index < 0 { return 1; }
    let Some(sess) = sess_ref(s) else { return 1; };
    let names = crate::anim::clip_names(&sess.game_root);
    let Some(name) = names.get(index as usize) else { return 1; };
    if name.len() >= cap as usize { return 1; }
    std::ptr::copy_nonoverlapping(name.as_ptr(), out as *mut u8, name.len());
    *out.add(name.len()) = 0;
    0
}

pub(crate) unsafe extern "C" fn set_anim_clip(s: *mut OkSession, name: *const c_char) -> i32 {
    if name.is_null() { return 1; }
    let Some(sess) = sess_ref(s) else { return 1; };
    let Ok(name) = std::ffi::CStr::from_ptr(name).to_str() else { return 1; };
    if !crate::anim::selectable_clip(&sess.game_root, name) { return 1; }
    sess.anim_clip = name.to_string();
    sess.anim_parameters.clear();
    if crate::anim::clip_expression_bone_mode(&sess.game_root,name,sess.anim_scene_mode) {
        // OCIChar.LoadAnime checks the full controller, preferring height1
        // even when only height is referenced by the selected state.
        if let Some(parameter)=crate::anim::studio_height_parameter(&sess.game_root,name) {
            sess.anim_parameters.insert(parameter,sess.shape_body.first().copied().unwrap_or(0.5));
        }
    }
    sess.anim_time = 0.0;
    sess.reset_anim_clock();
    sess.pv_mask=15;
    // Animator.Play/controller switching does not recreate or re-enable the
    // character's DynamicBone components. Keep their particle history; shape,
    // clothes and collider changes retain their explicit reset paths.
    0
}

pub(crate) unsafe extern "C" fn set_studio_animation(s:*mut OkSession,name:*const c_char,is_h:i32,
    height:f32,breast:f32,is_motion:i32,motion:f32)->i32 {
    set_studio_animation_profile(s,name,is_h,height,breast,is_motion,motion,c"".as_ptr(),c"".as_ptr())
}
pub(crate) unsafe extern "C" fn set_studio_animation_profile(s:*mut OkSession,name:*const c_char,is_h:i32,
    height:f32,breast:f32,is_motion:i32,motion:f32,yure_source:*const c_char,yure_owner:*const c_char)->i32 {
    set_studio_animation_pv_profile(s,name,is_h,height,breast,is_motion,motion,yure_source,yure_owner,15)
}
pub(crate) unsafe extern "C" fn set_studio_animation_pv_profile(s:*mut OkSession,name:*const c_char,is_h:i32,
    height:f32,breast:f32,is_motion:i32,motion:f32,yure_source:*const c_char,yure_owner:*const c_char,pv_mask:i32)->i32 {
    if !(0..=15).contains(&pv_mask) {return 1;}
    if name.is_null() || !(0..=1).contains(&is_h) || !(0..=1).contains(&is_motion) {return 1;}
    let Some(sess)=sess_ref(s) else {return 1;};
    let Ok(name)=std::ffi::CStr::from_ptr(name).to_str() else {return 1;};
    let profile=std::env::var("OK_MENU_PROFILE").ok().as_deref()==Some("1");let start=std::time::Instant::now();
    let Some(values)=crate::anim::studio_parameters(&sess.game_root,name,sess.shape_body.first().copied().unwrap_or(0.5),
        sess.shape_body.get(4).copied().unwrap_or(0.5),is_h==1,[height,breast],(is_motion==1).then_some(motion)) else {return 1;};
    let parameters_done=std::time::Instant::now();
    if yure_source.is_null()||yure_owner.is_null() {return 1;}
    let (Ok(source),Ok(owner))=(std::ffi::CStr::from_ptr(yure_source).to_str(),std::ffi::CStr::from_ptr(yure_owner).to_str()) else {return 1;};
    let policy=if is_h==1 {match crate::anim::yure::load(&sess.game_root,name,source,owner) {Ok(p)=>Some(p),Err(_)=>return 1,}} else {None};
    let yure_done=std::time::Instant::now();
    if let Some(policy)=policy {sess.set_yure_policy(policy);}
    sess.anim_clip=name.into();sess.anim_parameters=values;sess.anim_scene_mode=4;sess.anim_time=0.;sess.reset_anim_clock();
    sess.pv_mask=if is_h==1 {pv_mask as u8} else {15};
    if profile {eprintln!("ok-extract: Studio init parameters_ms={:.3} yure_ms={:.3} commit_ms={:.3}",parameters_done.duration_since(start).as_secs_f64()*1000.,yure_done.duration_since(parameters_done).as_secs_f64()*1000.,yure_done.elapsed().as_secs_f64()*1000.);}
    // OCIChar.LoadAnime changes Animator state/parameters, not accessory
    // component lifetime. Keep their warm rig/particles as set_anim_clip does.
    0
}

pub(crate) unsafe extern "C" fn set_studio_pv(s:*mut OkSession,mask:i32,fk:i32)->i32 {
    if !(0..=15).contains(&mask)||!(0..=1).contains(&fk) {return 1;}
    let Some(s)=sess_ref(s) else {return 1;};s.pv_mask=mask as u8;s.pv_fk=fk==1;0
}
pub(crate) unsafe extern "C" fn studio_pv_flag(s:*mut OkSession,index:i32)->i32 {
    let Some(s)=sess_ref(s) else {return -1;};
    if !(0..=3).contains(&index) {return -1;}((s.pv_mask&(1<<index))!=0&&!s.pv_fk&&s.user_fk.is_empty()) as i32
}
pub(crate) unsafe extern "C" fn studio_pv_bone(s:*mut OkSession,index:i32,source:i32)->i32 {
    let Some(s)=sess_ref(s) else {return -1;};if !(0..=3).contains(&index)||!(0..=1).contains(&source) {return -1;}
    s.ensure_skel_export();
    let Some(path)=s.pv_config.as_ref().and_then(|c|c.pairs.get(index as usize)).map(|p|&p[if source==1 {0}else{1}]) else {return -1;};
    let Some(sk)=s.body_skel.as_ref() else {return -1;};let Some(id)=crate::pv::resolve(sk,path) else {return -1;};
    s.skel_export.as_ref().and_then(|e|e.bones.iter().position(|b|b.name==sk.tfs[&id].name&&!b.pose_locked)).map_or(-1,|i|i as i32)
}

pub(crate) unsafe extern "C" fn anim_float_count(s:*mut OkSession)->i32 {
    let Some(sess)=sess_ref(s) else {return 0;};
    crate::anim::animation_float_parameters(&sess.game_root,&sess.anim_clip).len() as i32
}
pub(crate) unsafe extern "C" fn studio_yure_flag(s:*mut OkSession,index:i32)->i32 {
    let Some(s)=sess_ref(s) else {return -1;};let p=s.yure_policy.unwrap_or_default();
    match index {0..=3=>p.active[index as usize] as i32,4..=21=>p.shapes[(index as usize-4)/9][(index as usize-4)%9] as i32,_=>-1}
}
pub(crate) unsafe extern "C" fn anim_float_at(s:*mut OkSession,index:i32,out:*mut c_char,cap:i32,ranges:*mut f32)->i32 {
    if index<0||cap<=0||out.is_null()||ranges.is_null() {return 1;}
    let Some(sess)=sess_ref(s) else {return 1;};
    let entries=crate::anim::animation_float_parameters(&sess.game_root,&sess.anim_clip);
    let Some(p)=entries.get(index as usize) else {return 1;};
    if p.name.len()>=cap as usize {return 1;}
    std::ptr::copy_nonoverlapping(p.name.as_ptr(),out as *mut u8,p.name.len());*out.add(p.name.len())=0;
    *ranges=sess.anim_parameters.get(&p.name).copied().unwrap_or(p.default);*ranges.add(1)=p.min;*ranges.add(2)=p.max;0
}
pub(crate) unsafe extern "C" fn set_anim_float(s:*mut OkSession,name:*const c_char,value:f32)->i32 {
    if name.is_null() {return 1;}
    let Some(sess)=sess_ref(s) else {return 1;};
    let Ok(name)=std::ffi::CStr::from_ptr(name).to_str() else {return 1;};
    if sess.set_anim_float(name,value) {0} else {1}
}

// Additive exports keep the existing OkRustBindings allocation/layout intact.
#[repr(C)]
pub struct OkTextureSettings {
    pub filter: i32,
    pub wrap_u: i32,
    pub wrap_v: i32,
    pub srgb: i32,
    pub mip_policy: i32,
}

/// Additive ABI: draw pass availability survives session caching.
#[no_mangle]
pub unsafe extern "C" fn ok_part_outline_enabled(s: *mut OkSession, index: i32) -> i32 {
    if index < 0 { return -1; }
    let Some(s) = sess_ref(s) else { return -1; };
    s.parts.get(index as usize).map_or(-1, |p| i32::from(p.source_outline_enabled))
}

/// Shader keywords choose compile-time branches independently of float values.
#[no_mangle]
pub unsafe extern "C" fn ok_part_material_keyword(s:*mut OkSession,index:i32,keyword:*const c_char)->i32 {
    if index<0 || keyword.is_null() { return -1; }
    let Some(s)=sess_ref(s) else{return -1;};
    let Ok(name)=CStr::from_ptr(keyword).to_str() else{return -1;};
    let Some(mat)=s.parts.get(index as usize).and_then(|p|p.source_material.as_ref()) else{return -1;};
    i32::from(mat.keywords.iter().any(|k|k.eq_ignore_ascii_case(name)))
}

/// Additive source-material API: query exact gamma-space numeric values.
/// Absent properties return 1 without touching the caller's buffer.
#[no_mangle]
pub unsafe extern "C" fn ok_part_material_value(s:*mut OkSession,index:i32,property:*const c_char,out:*mut f32)->i32 {
    if index<0 || property.is_null() || out.is_null() {return 1;}
    let Some(s)=sess_ref(s) else{return 1;};
    let Ok(name)=CStr::from_ptr(property).to_str() else{return 1;};
    let Some(mat)=s.parts.get(index as usize).and_then(|p|p.source_material.as_ref()) else{return 1;};
    let value=mat.color(name).or_else(||mat.float(name).map(|v|[v,0.,0.,0.]))
        .or_else(||name.ends_with("_ST").then(|| {
            let defaults=[1.,1.,0.,0.];let axes=["x","y","z","w"];
            std::array::from_fn(|i|mat.float(&format!("{name}_{}",axes[i])).unwrap_or(defaults[i]))
        }));
    let Some(value)=value.filter(|v|v.iter().all(|v|v.is_finite())) else{return 1;};
    std::ptr::copy_nonoverlapping(value.as_ptr(),out,4);0
}

/// Complete material metadata, additive ABI; no PNG data or borrowed strings.
#[no_mangle]
pub unsafe extern "C" fn ok_part_material_metadata(s:*mut OkSession,index:i32,out:*mut u8,cap:i32)->i32 {
    if index<0 {return -1;}
    let Some(s)=sess_ref(s) else{return -1;};
    let Some(p)=s.parts.get(index as usize) else{return -1;};
    // source_property names the card slot an accessory came from (UE material overrides match on it).
    let value=serde_json::json!({"schema":1,"material":p.source_material,
        "shader_textures":p.shader_textures.keys().collect::<Vec<_>>(),"runtime_textures":p.runtime_textures,
        "source_property":p.source_property});
    let Ok(bytes)=serde_json::to_vec(&value) else{return -1;};
    copy_resource(&bytes,out,cap)
}

/// Borrowed PNG remains valid until the session is closed/reloaded.
/// Copy the original expression varying, independent of pupil/EyeLook sampling.
/// Count-only query uses null output; invalid data never modifies caller output.
#[no_mangle]
pub unsafe extern "C" fn ok_part_eye_expression_uvs(s:*mut OkSession,index:i32,out:*mut f32,capacity:i32)->i32 {
    if index<0 || capacity<0 {return -1;}
    let Some(p)=sess_ref(s).and_then(|s|s.parts.get(index as usize)) else {return -1;};
    let (Some(draw),Some(iris))=(p.eye_draw.as_ref(),p.iris_runtime.as_ref()) else {return -1;};
    let n=iris.raw_uvs.len();if n>i32::MAX as usize || n!=p.uvs.len() || iris.raw_uvs.iter().any(|uv|draw.expression_uv(*uv).is_none()) {return -1;}
    if out.is_null(){return n as i32;}if (capacity as usize)<n{return -1;}
    for (i,raw) in iris.raw_uvs.iter().enumerate(){let uv=draw.expression_uv(*raw).unwrap();ptr::copy_nonoverlapping(uv.as_ptr(),out.add(i*2),2);}
    n as i32
}

/// Borrowed PNG remains valid until the session is closed/reloaded.
#[no_mangle]
pub unsafe extern "C" fn ok_part_shader_texture(s:*mut OkSession,index:i32,property:*const c_char,out:*mut *const u8,len:*mut i32)->i32 {
    if index<0 || property.is_null() || out.is_null() || len.is_null(){return 1;}
    let Some(s)=sess_ref(s) else{return 1;};let Ok(name)=CStr::from_ptr(property).to_str() else{return 1;};
    let Some(png)=s.parts.get(index as usize).and_then(|p|p.shader_textures.get(name)) else{return 1;};
    if png.len()>i32::MAX as usize{return 1;}
    *out=png.as_ptr();*len=png.len() as i32;0
}

/// Returns 0 only when this property has explicit runtime provenance.
/// Unknown/static textures retain the caller's existing loading path.
#[no_mangle]
pub unsafe extern "C" fn ok_part_texture_settings(
    s: *mut OkSession, index: i32, property: *const c_char, out: *mut OkTextureSettings,
) -> i32 {
    if index < 0 || property.is_null() || out.is_null() { return 1; }
    let Some(s) = sess_ref(s) else { return 1; };
    let Ok(property) = CStr::from_ptr(property).to_str() else { return 1; };
    let Some(settings) = s.parts.get(index as usize)
        .and_then(|p| p.runtime_textures.get(property)) else { return 1; };
    *out = OkTextureSettings { filter: settings[0], wrap_u: settings[1],
        wrap_v: settings[2], srgb: settings[3], mip_policy: settings[4] };
    0
}

#[no_mangle]
pub unsafe extern "C" fn ok_animation_trigger_count(s:*mut OkSession)->i32 {
    sess_ref(s).map(|s|crate::anim::animation_trigger_names(&s.game_root,&s.anim_clip).len() as i32).unwrap_or(0)
}
#[no_mangle]
pub unsafe extern "C" fn ok_animation_trigger_at(s:*mut OkSession,index:i32,out:*mut c_char,cap:i32)->i32 {
    if index<0||out.is_null()||cap<=0 {return 1;}
    let Some(s)=sess_ref(s) else {return 1;};
    let names=crate::anim::animation_trigger_names(&s.game_root,&s.anim_clip);
    let Some(name)=names.get(index as usize).filter(|n|n.len()<cap as usize) else {return 1;};
    std::ptr::copy_nonoverlapping(name.as_ptr(),out as *mut u8,name.len());*out.add(name.len())=0;0
}
#[no_mangle]
pub unsafe extern "C" fn ok_set_animation_trigger(s:*mut OkSession,name:*const c_char)->i32 {
    if name.is_null() {return 1;}
    let Some(s)=sess_ref(s) else {return 1;};let Ok(name)=std::ffi::CStr::from_ptr(name).to_str() else {return 1;};
    if s.set_anim_trigger(name) {0}else{1}
}
#[no_mangle]
pub unsafe extern "C" fn ok_animation_crossfade(s:*mut OkSession,name:*const c_char,blend:f32,offset:f32)->i32 {
    if name.is_null(){return 1;}
    let Some(s)=sess_ref(s) else{return 1;};let Ok(name)=std::ffi::CStr::from_ptr(name).to_str() else{return 1;};
    if s.crossfade(name,blend,offset){0}else{1}
}
#[no_mangle]
pub unsafe extern "C" fn ok_animation_elapsed(s:*mut OkSession)->f32 {sess_ref(s).map(|s|s.anim_time).unwrap_or(0.)}
#[no_mangle]
pub unsafe extern "C" fn ok_animation_in_transition(s:*mut OkSession)->i32 {sess_ref(s).is_some_and(|s|s.is_anim_transitioning()) as i32}
#[no_mangle]
pub unsafe extern "C" fn ok_animation_transition_source_length(s:*mut OkSession)->f32 {
    sess_ref(s).and_then(|s|s.anim_crossfade.as_ref().or_else(||if s.anim_layer_crossfades.len()==1{s.anim_layer_crossfades.values().next()}else{None})).map(|f|f.source_length).unwrap_or(0.)
}
#[no_mangle]
pub unsafe extern "C" fn ok_animation_layer_in_transition(s:*mut OkSession,index:i32)->i32 {
    if index<0{return 0;}sess_ref(s).is_some_and(|s|s.anim_layer_transition(index as usize).is_some()) as i32
}
#[no_mangle]
pub unsafe extern "C" fn ok_animation_layer_transition_source_length(s:*mut OkSession,index:i32)->f32 {
    if index<0{return 0.;}sess_ref(s).and_then(|s|s.anim_layer_transition(index as usize)).map(|f|f.source_length).unwrap_or(0.)
}
#[no_mangle]
pub unsafe extern "C" fn ok_animation_crossfade_layer(s:*mut OkSession,index:i32,name:*const c_char,blend:f32,offset:f32)->i32 {
    if index<=0||name.is_null(){return 1;}
    let Some(s)=sess_ref(s) else{return 1;};let Ok(name)=std::ffi::CStr::from_ptr(name).to_str() else{return 1;};
    if s.crossfade_layer(name,index as usize,blend,offset){0}else{1}
}

pub(crate) unsafe extern "C" fn set_anim_scene_mode(s: *mut OkSession, mode: i32) -> i32 {
    if !(0..=4).contains(&mode) { return 1; }
    let Some(sess) = sess_ref(s) else { return 1; };
    sess.anim_scene_mode = mode;
    0
}

pub(crate) unsafe extern "C" fn clip_source_count(s: *mut OkSession) -> i32 {
    sess_ref(s).map(|x| crate::anim::clip_sources(&x.game_root).len() as i32).unwrap_or(0)
}

pub(crate) unsafe extern "C" fn clip_source_name(s: *mut OkSession, index: i32, out: *mut c_char, cap: i32) -> i32 {
    if out.is_null() || cap <= 0 || index < 0 { return 1; }
    let Some(sess) = sess_ref(s) else { return 1; };
    let sources = crate::anim::clip_sources(&sess.game_root);
    let Some(name) = sources.get(index as usize) else { return 1; };
    if name.len() >= cap as usize { return 1; }
    std::ptr::copy_nonoverlapping(name.as_ptr(), out as *mut u8, name.len());
    *out.add(name.len()) = 0;
    0
}

pub(crate) unsafe extern "C" fn clip_count_for_source(s: *mut OkSession, source: *const c_char) -> i32 {
    if source.is_null() { return 0; }
    let Some(sess) = sess_ref(s) else { return 0; };
    let Ok(source) = std::ffi::CStr::from_ptr(source).to_str() else { return 0; };
    crate::anim::clip_count_for_source(&sess.game_root, source) as i32
}

#[no_mangle]
pub unsafe extern "C" fn ok_session_animation_names(session:*mut OkSession,source:*const c_char,sources:i32,out:*mut u8,cap:i32)->i32 {
    let (Some(session),Some(source))=(sess_ref(session),cstr_opt(source)) else{return -1;};
    std::panic::catch_unwind(|| {
        let names=if sources==1 {crate::anim::clip_sources(&session.game_root)}else if source.is_empty(){crate::anim::clip_names(&session.game_root)}else{crate::anim::clip_names_for_source(&session.game_root,&source)};
        match serde_json::to_vec(&names){Ok(bytes)=>copy_resource(&bytes,out,cap),Err(_)=>-1}
    }).unwrap_or(-1)
}
// Bulk labels for workers: no live session pointer and no indexed FFI loop.
#[no_mangle]
pub unsafe extern "C" fn ok_animation_source_labels(root:*const c_char,source:*const c_char,out:*mut u8,cap:i32)->i32 {
    let (Some(root),Some(source))=(cstr_opt(root),cstr_opt(source)) else{return -1;};
    std::panic::catch_unwind(|| {
        let game=std::path::Path::new(&root);
        let labels=if source.is_empty(){crate::anim::clip_names(game)}else{crate::anim::clip_names_for_source(game,&source)};
        match serde_json::to_vec(&labels){Ok(bytes)=>copy_resource(&bytes,out,cap),Err(_)=>-1}
    }).unwrap_or(-1)
}
#[no_mangle]
pub unsafe extern "C" fn ok_prepare_studio_animation(root:*const c_char,key:*const c_char,source:*const c_char,owner:*const c_char)->i32 {
    let (Some(root),Some(key),Some(source),Some(owner))=(cstr_opt(root),cstr_opt(key),cstr_opt(source),cstr_opt(owner)) else{return 0;};
    std::panic::catch_unwind(|| {
        let game=std::path::Path::new(&root);
        if !crate::anim::prepare_animation_clip(game,&key)||!crate::anim::prepare_studio_yure(game,&key,&source,&owner) {return 0;}
        // Native Studio options start at zero; the controller's authored default
        // parameters may select a different blend leaf/duration. Prepare both
        // optional-motion variants through the same parameter validation path.
        for motion in [None,Some(0.)] {let _=crate::anim::studio_parameters(game,&key,0.5,0.5,true,[0.,0.],motion);}
        1
    }).unwrap_or(0)
}
// Worker-safe immutable preparation. No live session/UObject is referenced.
#[no_mangle]
pub unsafe extern "C" fn ok_prepare_animation_source(root:*const c_char,source:*const c_char)->i32 {
    let (Some(root),Some(source))=(cstr_opt(root),cstr_opt(source)) else {return 0;};
    std::panic::catch_unwind(|| crate::anim::clip_count_for_source(std::path::Path::new(&root),&source) as i32).unwrap_or(0)
}
#[no_mangle]
pub unsafe extern "C" fn ok_prepare_animation_clip(root:*const c_char,key:*const c_char)->i32 {
    let (Some(root),Some(key))=(cstr_opt(root),cstr_opt(key)) else {return 0;};
    std::panic::catch_unwind(|| {
        let game=std::path::Path::new(&root);
        i32::from(crate::anim::prepare_animation_clip(game,&key))
    }).unwrap_or(0)
}
#[no_mangle]
pub unsafe extern "C" fn ok_prepare_animation_pair(root:*const c_char,state:*const c_char,female_motion:*const c_char,male_motion:*const c_char)->i32 {
    let (Some(root),Some(state),Some(fm),Some(mm))=(cstr_opt(root),cstr_opt(state),cstr_opt(female_motion),cstr_opt(male_motion)) else {return 0;};
    std::panic::catch_unwind(|| i32::from(crate::h_playback::prepare_pair(std::path::Path::new(&root),&state,&fm,&mm).is_ok())).unwrap_or(0)
}
#[no_mangle]
pub unsafe extern "C" fn ok_animation_has_clip(s:*mut OkSession,key:*const c_char)->i32 {
    let (Some(session),Some(key))=(sess_ref(s),cstr_opt(key)) else {return 0;};
    i32::from(crate::anim::selectable_clip(&session.game_root,&key))
}

pub(crate) unsafe extern "C" fn clip_name_for_source(s: *mut OkSession, source: *const c_char, index: i32, out: *mut c_char, cap: i32) -> i32 {
    if source.is_null() || index < 0 { return 1; }
    let Some(sess) = sess_ref(s) else { return 1; };
    let Ok(source) = std::ffi::CStr::from_ptr(source).to_str() else { return 1; };
    let Some(name) = crate::anim::clip_name_for_source_at(&sess.game_root, source, index as usize) else { return 1; };
    // Query the exact UTF-8 allocation size (including the terminator). Source
    // labels can include multiple controller owners; never truncate identities.
    if out.is_null() && cap == 0 { return i32::try_from(name.len() + 1).unwrap_or(1); }
    if out.is_null() || cap <= 0 { return 1; }
    if name.len() >= cap as usize { return 1; }
    std::ptr::copy_nonoverlapping(name.as_ptr(), out as *mut u8, name.len());
    *out.add(name.len()) = 0;
    0
}

pub(crate) unsafe extern "C" fn apply_shape(
    s: *mut OkSession,
    body: *const f32,
    n_body: i32,
    face: *const f32,
    n_face: i32,
) -> i32 {
    let Some(sess) = sess_ref(s) else {
        return 1;
    };
    let body = if body.is_null() || n_body <= 0 {
        &[][..]
    } else {
        std::slice::from_raw_parts(body, n_body as usize)
    };
    let face = if face.is_null() || n_face <= 0 {
        &[][..]
    } else {
        std::slice::from_raw_parts(face, n_face as usize)
    };
    sess.apply_shape(body, face);
    0
}

#[no_mangle]
pub unsafe extern "C" fn ok_apply_shape_live(s:*mut OkSession,body:*const f32,n_body:i32,face:*const f32,n_face:i32)->i32 {
    let Some(sess)=sess_ref(s) else{return 1;};
    if body.is_null()||face.is_null()||n_body<0||n_face<0{return 1;}
    let body=std::slice::from_raw_parts(body,n_body as usize);let face=std::slice::from_raw_parts(face,n_face as usize);
    if body.len()!=sess.shape_body.len()||face.len()!=sess.shape_face.len()||body.iter().chain(face).any(|v|!v.is_finite()){return 1;}
    sess.apply_shape_live(body,face);0
}

#[no_mangle]
pub unsafe extern "C" fn ok_prepare_shape_live(s:*mut OkSession,body:*const f32,n_body:i32,face:*const f32,n_face:i32)->i32 {
    let Some(sess)=sess_ref(s) else{return 1;};
    if body.is_null()||face.is_null()||n_body<0||n_face<0{return 1;}
    let body=std::slice::from_raw_parts(body,n_body as usize);let face=std::slice::from_raw_parts(face,n_face as usize);
    if body.len()!=sess.shape_body.len()||face.len()!=sess.shape_face.len()||body.iter().chain(face).any(|v|!v.is_finite()){return 1;}
    sess.prepare_shape_live(body,face);0
}

pub(crate) unsafe extern "C" fn skin(s: *mut OkSession) -> i32 {
    let Some(sess) = sess_ref(s) else {
        return 1;
    };
    sess.skin_all();
    0
}

fn copy_f32(src: &[f32], dst: *mut f32, cap: i32) -> i32 {
    if dst.is_null() || cap <= 0 {
        return src.len() as i32;
    }
    let n = src.len().min(cap as usize);
    unsafe {
        std::ptr::copy_nonoverlapping(src.as_ptr(), dst, n);
    }
    src.len() as i32
}

pub(crate) unsafe extern "C" fn shape_body(s: *mut OkSession, out: *mut f32, cap: i32) -> i32 {
    sess_ref(s)
        .map(|x| copy_f32(&x.shape_body, out, cap))
        .unwrap_or(0)
}

pub(crate) unsafe extern "C" fn shape_face(s: *mut OkSession, out: *mut f32, cap: i32) -> i32 {
    sess_ref(s)
        .map(|x| copy_f32(&x.shape_face, out, cap))
        .unwrap_or(0)
}

pub(crate) unsafe extern "C" fn coord_count(s: *mut OkSession) -> i32 {
    sess_ref(s)
        .map(|x| x.coordinate_count as i32)
        .unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn ok_register_bindings(
    unreal: OkUnrealBindings,
    out_rust: *mut OkRustBindings,
) -> i32 {
    if out_rust.is_null() {
        return 1;
    }
    if let Ok(mut g) = UNREAL.lock() {
        *g = unreal;
    }
    *out_rust = OkRustBindings {
        session_open,
        session_free,
        part_count,
        part_at,
        apply_shape,
        skin,
        shape_body,
        shape_face,
        coord_count,
        skel_bone_count,
        skel_bone_at,
        morph_count,
        morph_at,
        mouth_pair,
        mouth_pattern_count,
        apply_anim_time,
        anim_duration,
        clip_count,
        clip_name,
        set_anim_clip,
        clip_source_count,
        clip_source_name,
        clip_count_for_source,
        clip_name_for_source,
        set_anim_scene_mode,
        apply_anim_frame,
        anim_looping,
        hand_shape_count,
        set_hand_shape,
        face_pattern_count,
        face_pair,
        face_baseline_weight,
        face_default_rate,
        face_runtime_setting,
        eye_material_setting,
        set_eye_gaze,
        eye_look_pattern_count,
        step_eye_look,
        external_body_pose_at,
        apply_external_body_frame,
        anim_float_count,
        anim_float_at,
        set_anim_float,
        set_studio_animation,
        set_studio_animation_profile,
        studio_yure_flag,
        set_studio_animation_pv_profile,
        set_studio_pv,
        studio_pv_flag,
        studio_pv_bone,
    };
    0
}

#[cfg(test)]
mod tests {
    #[test]
    fn common_animation_ffi_rejects_invalid_handles_and_roots() {
        unsafe {
            assert_eq!(ok_part_private_skin_stages(ptr::null_mut(),0,ptr::null_mut(),ptr::null_mut(),0,ptr::null_mut()),-1);
            assert_eq!(ok_part_private_skin_stages(ptr::null_mut(),-1,ptr::null_mut(),ptr::null_mut(),0,ptr::null_mut()),-1);
            assert_eq!(ok_part_private_skin_source_revision(ptr::null_mut(),0),0);
            assert_eq!(ok_part_private_skin_stages_revision(ptr::null_mut(),0,0,ptr::null_mut(),ptr::null_mut(),0,ptr::null_mut()),-1);
            assert_eq!(ok_part_private_skin_stages_revision(ptr::null_mut(),0,1,ptr::null_mut(),ptr::null_mut(),0,ptr::null_mut()),-1);
            assert_eq!(ok_common_animation_pairs(ptr::null(),ptr::null_mut(),0),-1);
            assert_eq!(ok_common_animation_pair_clip(ptr::null(),ptr::null(),0,ptr::null_mut(),0),-1);
            assert_eq!(ok_select_common_animation(ptr::null_mut(),ptr::null(),ptr::null()),1);
            let missing=std::ffi::CString::new("__missing_common_animation_root__").unwrap();
            let clip=std::ffi::CString::new("states:missing.unity3d|Idle").unwrap();
            assert_eq!(ok_common_animation_pair_clip(missing.as_ptr(),clip.as_ptr(),0,ptr::null_mut(),0),-1);
        }
    }
    #[test]
    #[ignore="explicit APNG and common curve inputs; no game-root dependency"]
    fn portable_shared_selection_preserves_private_particle_history() {
        let card=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let curves=std::env::var("OK_TEST_SHARED_CURVES").unwrap();
        let bytes=crate::gamefs::read(curves).unwrap();
        let mut session=crate::portable::load(std::path::Path::new(&card)).unwrap();
        unsafe {assert_eq!(ok_session_install_shared_clip(&mut session,bytes.as_ptr(),bytes.len() as i32),0);}
        for frame in 0..30 {session.apply_anim_frame(frame as f32/60.,1./60.);}
        let snapshot=|s:&OkSession|s.parts.iter().filter_map(|p|p.runtime_accessory.as_ref()
            .and_then(|r|r.dynamic_state_for_test()).map(|d|d.particle_snapshot())).collect::<Vec<_>>();
        let before=snapshot(&session);assert!(!before.is_empty());
        unsafe {assert_eq!(ok_session_install_shared_clip(&mut session,bytes.as_ptr(),bytes.len() as i32),0);}
        assert_eq!(before,snapshot(&session));
        assert!(crate::anim::is_shared_root(&session.game_root));
        eprintln!("APNG shared selection retained private particle histories rigs={}",before.len());
    }
    #[test]
    #[ignore="explicit APNG and shared curves; renderer exclusions and on-demand oracle"]
    fn private_gpu_display_exclusions_preserve_queries() {
        let card=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
        let curves=crate::gamefs::read(std::env::var("OK_TEST_SHARED_CURVES").unwrap()).unwrap();
        let mut session=crate::portable::load(std::path::Path::new(&card)).unwrap();
        let mut oracle=crate::portable::load(std::path::Path::new(&card)).unwrap();
        for s in [&mut session,&mut oracle]{unsafe{assert_eq!(ok_session_install_shared_clip(s,curves.as_ptr(),curves.len() as i32),0);}s.apply_anim_frame(0.,0.);s.skin_all();}
        let mut indices=Vec::new();let mut revisions=Vec::new();
        for i in 0..session.parts.len(){unsafe{let revision=ok_part_private_skin_source_revision(&mut session,i as i32);
            if revision!=0&&i%2==0{indices.push(i as i32);revisions.push(revision);}}}
        assert!(!indices.is_empty());
        let previous=session.parts.iter().map(|p|p.positions.clone()).collect::<Vec<_>>();
        session.apply_anim_frame(0.23,1./120.);
        oracle.apply_anim_frame(0.23,1./120.);oracle.skin_all();
        unsafe{assert_eq!(ok_skin_display_except_private(&mut session,indices.as_ptr(),revisions.as_ptr(),indices.len() as i32),0);}
        for (i,p) in session.parts.iter().enumerate(){
            if indices.contains(&(i as i32)){assert_eq!(p.positions,previous[i],"GPU-owned source must not publish CPU LBS");}
            else{assert_eq!(p.positions,oracle.parts[i].positions,"CPU-owned part must publish current pose");}
        }
        let particles=|s:&OkSession|s.parts.iter().filter_map(|p|p.runtime_accessory.as_ref().and_then(|r|r.dynamic_state_for_test()).map(|d|d.particle_snapshot())).collect::<Vec<_>>();
        let history=particles(&session);
        for &i in &indices{unsafe{assert_eq!(ok_query_private_skin_geometry(&mut session,i),0);}
            let p=&session.parts[i as usize];let wanted=&oracle.parts[i as usize];
            assert_eq!(p.positions,wanted.positions);assert_eq!(p.normals,wanted.normals);assert_eq!(p.rest_tangents,wanted.rest_tangents);
        }
        assert_eq!(history,particles(&session),"queries must not advance/reset particles");
        let stable=session.parts.iter().map(|p|p.positions.clone()).collect::<Vec<_>>();revisions[0]+=1;
        unsafe{assert_eq!(ok_skin_display_except_private(&mut session,indices.as_ptr(),revisions.as_ptr(),indices.len() as i32),1);
            assert_eq!(ok_skin_display_except_private(ptr::null_mut(),ptr::null(),ptr::null(),0),1);
            assert_eq!(ok_query_private_skin_geometry(ptr::null_mut(),0),1);}
        assert_eq!(stable,session.parts.iter().map(|p|p.positions.clone()).collect::<Vec<_>>());
        eprintln!("Private GPU display exclusions/query oracle parts={} current geometry exact; particles unchanged",indices.len());
    }
    #[test]
    fn animation_envelope_rejects_truncation_and_oversize() {
        assert!(OkSession::from_animation_bytes(b"OKAN0001").is_err());
        let mut bytes = b"OKAN0001".to_vec();
        bytes.extend((crate::portable::MAX_BYTES + 1).to_le_bytes());
        bytes.extend(0u32.to_le_bytes());
        assert!(OkSession::from_animation_bytes(&bytes).err().unwrap().contains("size limit"));
    }

    #[test]
    #[ignore = "installed KKS fixture; run alone"]
    fn compact_animation_snapshot_preserves_native_palettes_and_apng() {
        use std::path::Path;
        let root = &crate::test_support::game_path("");
        let card = root.join("UserData/chara/female/[Card] March_7th_keiyuu.png");
        let source = crate::session::open(root, Some(&card), 0).unwrap();
        let full = source.to_preview_bytes().unwrap();
        let compact = source.to_animation_bytes().unwrap();
        assert_eq!(full, source.to_preview_bytes().unwrap(), "animation export must not mutate portable/APNG data");
        let mut old = OkSession::from_animation_bytes(&full).unwrap();
        let mut new = OkSession::from_animation_bytes(&compact).unwrap();
        assert!(compact.len() < full.len());
        assert_eq!(old.parts.len(), new.parts.len());
        assert!(new.parts.iter().all(|p| p.png.is_none() && p.shader_textures.is_empty()));
        assert!(old.parts.iter().any(|p| p.png.is_some()));
        let mut damaged = compact.clone(); damaged[16] ^= 1;
        assert!(OkSession::from_animation_bytes(&damaged).err().unwrap().contains("checksum"));
        let clip = crate::anim::export_shared_clip(root, "studio/anime/00.unity3d|f_aruki_00_00_01").unwrap();
        unsafe {
            for s in [&mut old, &mut new] {
                assert_eq!(ok_session_install_shared_clip(s, clip.as_ptr(), clip.len() as i32), 0);
            }
            for frame in 0..=60 {
                let t = frame as f32 / 60.;
                for s in [&mut old, &mut new] { s.apply_anim_frame(t, if frame == 0 {0.} else {1./60.}); }
                assert_eq!(old.body_skel.as_ref().unwrap().worlds, new.body_skel.as_ref().unwrap().worlds);
                for index in 0..old.parts.len() as i32 {
                    let mut a = [0;32]; let mut b = [0;32];
                    assert_eq!(ok_part_skin_source_hash_v2(&mut old,index,a.as_mut_ptr()),0);
                    assert_eq!(ok_part_skin_source_hash_v2(&mut new,index,b.as_mut_ptr()),0);
                    assert_eq!(a,b);
                    let n = ok_part_skin_palette(&mut old,index,ptr::null_mut(),ptr::null_mut(),0);
                    assert_eq!(n,ok_part_skin_palette(&mut new,index,ptr::null_mut(),ptr::null_mut(),0));
                    if n > 0 {
                        let mut x=vec![0f32;n as usize*16]; let mut y=x.clone();
                        let mut view:OkSkinPaletteView=std::mem::zeroed();
                        assert_eq!(n,ok_part_skin_palette(&mut old,index,&mut view,x.as_mut_ptr(),n));
                        assert_eq!(n,ok_part_skin_palette(&mut new,index,&mut view,y.as_mut_ptr(),n));
                        assert_eq!(x,y,"frame={frame} part={index}");
                    }
                    assert_eq!(old.parts[index as usize].morph_idx,new.parts[index as usize].morph_idx);
                    assert_eq!(old.parts[index as usize].morph_dxyz,new.parts[index as usize].morph_dxyz);
                }
            }
        }
        eprintln!("Animation snapshot full={} compact={} native palettes matched 61 frames; APNG unchanged", full.len(), compact.len());
    }
    use super::*;
    use std::ffi::CString;
    use std::ptr;

    #[test]
    fn user_fk_disables_studio_pv_and_clear_restores_mask() {
        let mut s = crate::session::preview_session(&[], std::path::Path::new(""));
        s.pv_mask = 5;
        let name = CString::new("cf_j_hand_L").unwrap();
        let euler = [10.0f32, 20., 30.];
        unsafe {
            assert_eq!(studio_pv_flag(&mut s, 0), 1);
            assert_eq!(studio_pv_flag(&mut s, 1), 0);
            assert_eq!(ok_session_set_fk(&mut s, name.as_ptr(), euler.as_ptr(), 1), 0);
            for index in 0..4 { assert_eq!(studio_pv_flag(&mut s, index), 0); }
            assert_eq!(ok_session_set_fk(&mut s, ptr::null(), ptr::null(), 0), 0);
            for index in 0..4 { assert_eq!(studio_pv_flag(&mut s, index), (5 >> index) & 1); }
            assert_eq!(set_studio_pv(&mut s, 5, 1), 0);
            assert_eq!(studio_pv_flag(&mut s, 0), 0, "explicit host FK gate still applies");
        }
    }

    #[test]
    fn source_material_api_invalid_inputs_leave_output_untouched() {
        let name=CString::new("_Color").unwrap();let mut value=[7.;4];
        let mut png=std::ptr::dangling::<u8>();let mut size=91;
        unsafe {
            assert_eq!(ok_part_material_value(ptr::null_mut(),0,name.as_ptr(),value.as_mut_ptr()),1);
            assert_eq!(ok_part_material_value(ptr::null_mut(),-1,name.as_ptr(),value.as_mut_ptr()),1);
            assert_eq!(ok_part_shader_texture(ptr::null_mut(),0,name.as_ptr(),&mut png,&mut size),1);
        }
        assert_eq!(value,[7.;4]);assert_eq!(png,std::ptr::dangling::<u8>());assert_eq!(size,91);
    }

    #[test]
    fn register_null_out_fails() {
        let u = OkUnrealBindings { log: None };
        let rc = unsafe { ok_register_bindings(u, ptr::null_mut()) };
        assert_eq!(rc, 1);
    }

    #[test]
    fn session_open_missing_abdata_fails() {
        let mut rust = std::mem::MaybeUninit::<OkRustBindings>::uninit();
        let u = OkUnrealBindings { log: None };
        assert_eq!(unsafe { ok_register_bindings(u, rust.as_mut_ptr()) }, 0);
        let rust = unsafe { rust.assume_init() };
        let game = CString::new("C:/portable/OpenKoikatsu/Saved/no_such_game").unwrap();
        let mut sess: *mut OkSession = ptr::null_mut();
        let mut err = [0u8; 256];
        let rc = unsafe {
            (rust.session_open)(
                game.as_ptr(),
                ptr::null(),
                0,
                &mut sess,
                err.as_mut_ptr() as *mut c_char,
                err.len() as i32,
            )
        };
        assert_eq!(rc, 1);
        assert!(sess.is_null());
    }

    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn session_open_real_card_parts() {
        let game_p = &crate::test_support::game_path(r"");
        if !game_p.join("abdata").ok_is_dir() {
            return;
        }
        let card_p =
            &crate::test_support::game_path(r"\UserData\chara\female\KK_091207.png");
        if !card_p.ok_is_file() {
            return;
        }
        let mut rust = std::mem::MaybeUninit::<OkRustBindings>::uninit();
        let u = OkUnrealBindings { log: None };
        assert_eq!(unsafe { ok_register_bindings(u, rust.as_mut_ptr()) }, 0);
        let rust = unsafe { rust.assume_init() };
        let game = CString::new(game_p.to_string_lossy().as_ref()).unwrap();
        let card = CString::new(card_p.to_string_lossy().as_ref()).unwrap();
        let mut sess: *mut OkSession = ptr::null_mut();
        let mut err = [0u8; 512];
        let rc = unsafe {
            (rust.session_open)(
                game.as_ptr(),
                card.as_ptr(),
                0,
                &mut sess,
                err.as_mut_ptr() as *mut c_char,
                err.len() as i32,
            )
        };
        assert_eq!(rc, 0, "open failed");
        assert!(!sess.is_null());
        let n = unsafe { (rust.part_count)(sess) };
        assert!(n > 0, "part_count={n}");
        let mut view = std::mem::MaybeUninit::<OkMeshView>::zeroed();
        assert_eq!(unsafe { (rust.part_at)(sess, 0, view.as_mut_ptr()) }, 0);
        let view = unsafe { view.assume_init() };
        assert!(view.n_verts > 0);
        let n_body = unsafe { (rust.shape_body)(sess, ptr::null_mut(), 0) };
        let mut body = vec![0.0f32; n_body.max(1) as usize];
        unsafe { (rust.shape_body)(sess, body.as_mut_ptr(), body.len() as i32) };
        let n_face = unsafe { (rust.shape_face)(sess, ptr::null_mut(), 0) };
        let mut face = vec![0.0f32; n_face.max(1) as usize];
        unsafe { (rust.shape_face)(sess, face.as_mut_ptr(), face.len() as i32) };
        if !body.is_empty() {
            body[0] = 1.0;
        }
        assert_eq!(
            unsafe {
                (rust.apply_shape)(
                    sess,
                    body.as_ptr(),
                    body.len() as i32,
                    face.as_ptr(),
                    face.len() as i32,
                )
            },
            0
        );
        assert_eq!(unsafe { (rust.skin)(sess) }, 0);
        let mut view2 = std::mem::MaybeUninit::<OkMeshView>::zeroed();
        assert_eq!(unsafe { (rust.part_at)(sess, 0, view2.as_mut_ptr()) }, 0);
        let view2 = unsafe { view2.assume_init() };
        assert_eq!(view2.n_verts, view.n_verts);
        assert!(!view2.positions.is_null());
        unsafe { (rust.session_free)(sess) };
    }
}

#[no_mangle]
pub unsafe extern "C" fn ok_editor_decode_mods(data:*const u8,len:i32,out:*mut u8,cap:i32)->i32 {
    if len<0 || (len>0 && data.is_null()){return -1;}
    let bytes=if len==0 {&[][..]}else{std::slice::from_raw_parts(data,len as usize)};
    let Ok(mods)=crate::editor_mods::decode(bytes) else{return -1;};
    let Ok(json)=serde_json::to_vec(&mods) else{return -1;};copy_resource(&json,out,cap)
}
#[no_mangle]
pub unsafe extern "C" fn ok_editor_encode_mods(data:*const u8,len:i32,json:*const c_char,out:*mut u8,cap:i32)->i32 {
    if len<0 || (len>0 && data.is_null()) || json.is_null(){return -1;}
    let bytes=if len==0 {&[][..]}else{std::slice::from_raw_parts(data,len as usize)};
    let Ok(mods)=serde_json::from_slice::<crate::editor_mods::EditorMods>(CStr::from_ptr(json).to_bytes()) else{return -1;};
    let Ok(bytes)=crate::editor_mods::encode(bytes,&mods) else{return -1;};copy_resource(&bytes,out,cap)
}
#[no_mangle]
pub unsafe extern "C" fn ok_editor_apply_mods(s:*mut OkSession,json:*const c_char,coordinate:i32)->i32 {
    let Some(s)=sess_ref(s) else{return -1;};if json.is_null() || coordinate<0{return -1;}
    let Ok(mut mods)=serde_json::from_slice::<crate::editor_mods::EditorMods>(CStr::from_ptr(json).to_bytes()) else{return -1;};
    if !crate::editor_mods::valid(&mods){return -1;}
    for b in &mut mods.abm {let active=b.for_coord(coordinate as usize);b.coord=vec![active];}
    if s.abm!=mods.abm{s.abm=mods.abm;let body=s.shape_body.clone();let face=s.shape_face.clone();s.apply_shape(&body,&face);}let changed=crate::pregnancy::apply(s,&mods.pregnancy);if changed{2}else{0}
}

/// `ok_editor_apply_mods` and then the card's sliders (`body`/`face`; empty = the session's), as a card's load applies
/// them. The mods' bone modifiers rebuild the shape, so the two calls built it twice (first with the session's
/// values, ~0.12 s each). With no pregnancy edit to apply (that one reads the shaped body) the bone modifiers are set
/// and the shape is built once with the card's values; otherwise the two steps run as before. Returns as
/// `ok_editor_apply_mods` (editor_mods::apply_with_shape).
#[no_mangle]
pub unsafe extern "C" fn ok_editor_apply_mods_shape(s:*mut OkSession,json:*const c_char,coordinate:i32,body:*const f32,n_body:i32,face:*const f32,n_face:i32)->i32 {
    let Some(sess)=sess_ref(s) else{return -1;};if json.is_null()||coordinate<0{return -1;}
    let Ok(mut mods)=serde_json::from_slice::<crate::editor_mods::EditorMods>(CStr::from_ptr(json).to_bytes()) else{return -1;};
    if !crate::editor_mods::valid(&mods){return -1;}
    for b in &mut mods.abm {let active=b.for_coord(coordinate as usize);b.coord=vec![active];}
    let slice=|p:*const f32,n:i32,own:&Vec<f32>|->Vec<f32>{if p.is_null()||n<=0{own.clone()}else{std::slice::from_raw_parts(p,n as usize).to_vec()}};
    let body=slice(body,n_body,&sess.shape_body);let face=slice(face,n_face,&sess.shape_face);
    if body.iter().chain(&face).any(|v|!v.is_finite()){return -1;}
    crate::editor_mods::apply_with_shape(sess,&mods,&body,&face)
}

#[no_mangle]
pub unsafe extern "C" fn ok_editor_body_physics(s:*mut OkSession,softness:f32,weight:f32)->i32 {
 let Some(s)=sess_ref(s)else{return -1;};if !softness.is_finite()||!weight.is_finite(){return -1;}
 if s.bust_softness==softness&&s.bust_weight==weight{return 0;}
 s.bust_softness=softness;s.bust_weight=weight;let body=s.shape_body.clone();let face=s.shape_face.clone();s.apply_shape_live(&body,&face);0
}

/// Common contact recipe is evaluated between Animator helpers and physics.
/// This API owns no global state and cannot mutate another character's session.
#[no_mangle]
pub unsafe extern "C" fn ok_session_apply_contact_frame(s:*mut OkSession,t:f32,dt:f32,json:*const u8,len:i32)->i32 {
 let Some(s)=sess_ref(s)else{return -1;};
 if json.is_null()||len<=0||len>16384||!t.is_finite()||t<0.||!dt.is_finite()||dt<0.||dt>1.{return -1;}
 let Some(recipe)=crate::point_contact::Recipe::parse(std::slice::from_raw_parts(json,len as usize))else{return -1;};
 let duration=s.anim_duration();if !duration.is_finite()||duration<=0.{return -1;}
 s.sample_paired_frame(t);
 let Some(body)=s.body_skel.as_mut()else{return -1;};
 if !recipe.apply(body,(t/duration).clamp(0.,1.)){return -1;}
 s.finish_paired_dynamics(dt);0
}

/// Copy the live session for a low-poly companion export. Game thread only;
/// the returned handle owns no session state. Free with `ok_lowpoly_free`.
#[no_mangle]
pub unsafe extern "C" fn ok_lowpoly_capture(s:*mut OkSession,err:*mut c_char,cap:i32)->*mut std::ffi::c_void {
    let Some(s)=sess_ref(s)else{write_err(err,cap,"no session");return ptr::null_mut();};
    match crate::run_no_panic(std::panic::AssertUnwindSafe(||crate::lowpoly::capture(s))) {
        Ok(source)=>Box::into_raw(Box::new(source)) as *mut std::ffi::c_void,
        Err(e)=>{write_err(err,cap,&e);ptr::null_mut()}
    }
}

/// Remesh and write `<card>.lowpoly.glb` and `<card>.lowpoly.json`. Safe on a
/// worker thread; `budget<=0` uses the default whole-character budget.
#[no_mangle]
pub unsafe extern "C" fn ok_lowpoly_export(h:*const std::ffi::c_void,card:*const c_char,budget:i32,
    summary:*mut c_char,summary_cap:i32,err:*mut c_char,cap:i32)->i32 {
    if h.is_null()||card.is_null(){write_err(err,cap,"missing low-poly source or card path");return 1;}
    let source=&*(h as *const crate::lowpoly::Source);
    let Ok(card)=CStr::from_ptr(card).to_str()else{write_err(err,cap,"card path is not UTF-8");return 1;};
    let budget=if budget>0{budget as usize}else{crate::lowpoly::DEFAULT_TRIANGLE_BUDGET};
    let (glb,report)=crate::lowpoly::companion_paths(std::path::Path::new(card));
    match crate::run_no_panic(std::panic::AssertUnwindSafe(||crate::lowpoly::export(source,&glb,&report,budget))) {
        Ok(json)=>{
            let remeshed=json["parts"].as_array().map_or(0,|p|p.iter().filter(|r|r["method"]=="remeshed").count());
            write_err(summary,summary_cap,&format!("{} → {} triangles, {} bones, {} parts remeshed",
                json["source_triangles"],json["low_triangles"],json["bones"],remeshed));0
        }
        Err(e)=>{write_err(err,cap,&e);1}
    }
}

#[no_mangle]
pub unsafe extern "C" fn ok_lowpoly_free(h:*mut std::ffi::c_void) {
    if !h.is_null(){drop(Box::from_raw(h as *mut crate::lowpoly::Source));}
}

// ------------------------------------------------------------ OK model editor

fn okmodel_view(lib: *const c_char, id: *const c_char) -> Result<crate::okmodel_view::ModelView, String> {
    let lib = unsafe { cstr_opt(lib) }.ok_or("library path missing")?;
    let id = unsafe { cstr_opt(id) }.ok_or("model id missing")?;
    let id = crate::oklib::id_from_hex_pub(&id).ok_or("bad model id")?;
    crate::okmodel_view::ModelView::open(std::path::Path::new(&lib), id)
}
fn copy_text(text: &str, out: *mut u8, cap: i32) -> i32 {
    let bytes = text.as_bytes();
    if !out.is_null() && cap as usize >= bytes.len() { unsafe { ptr::copy_nonoverlapping(bytes.as_ptr(), out, bytes.len()); } }
    bytes.len() as i32
}
/// Model contents as JSON. Returns the byte length (call again with a buffer
/// at least that large); -1 on error (message in `out`).
#[no_mangle]
pub unsafe extern "C" fn ok_okmodel_info(lib: *const c_char, id: *const c_char, out: *mut u8, cap: i32) -> i32 {
    let root = cstr_opt(lib).unwrap_or_default();
    match crate::run_no_panic(|| crate::okmodel_view::with_library(std::path::Path::new(&root), || okmodel_view(lib, id).map(|v| v.contents().to_string()))) {
        Ok(json) => copy_text(&json, out, cap),
        Err(e) => { copy_text(&e, out, cap); -1 }
    }
}
/// One prefab of a model as a preview session (render with the normal session path).
#[no_mangle]
pub unsafe extern "C" fn ok_okmodel_open_session(lib: *const c_char, id: *const c_char, prefab: *const c_char, include_disabled: i32,
    out: *mut *mut OkSession, err: *mut c_char, cap: i32) -> i32 {
    if out.is_null() { return 1; }
    *out = ptr::null_mut();
    let prefab = cstr_opt(prefab).unwrap_or_default();
    let root = cstr_opt(lib).unwrap_or_default();
    match crate::run_no_panic(|| crate::okmodel_view::with_library(std::path::Path::new(&root), || okmodel_view(lib, id)?.prefab_session(&prefab, include_disabled != 0))) {
        Ok(s) => { *out = Box::into_raw(Box::new(s)); 0 }
        Err(e) => { write_err(err, cap, &e); 1 }
    }
}
/// `ok_okmodel_open_session` with Studio item FK: `fk_json` = [[bone, [x, y, z]], …] (local Unity Euler degrees).
#[no_mangle]
pub unsafe extern "C" fn ok_okmodel_open_session_fk(lib: *const c_char, id: *const c_char, prefab: *const c_char, include_disabled: i32,
    fk_json: *const c_char, out: *mut *mut OkSession, err: *mut c_char, cap: i32) -> i32 {
    if out.is_null() { return 1; }
    *out = ptr::null_mut();
    let prefab = cstr_opt(prefab).unwrap_or_default();
    let root = cstr_opt(lib).unwrap_or_default();
    let fk: Vec<(String, [f32; 3])> = match serde_json::from_str(&cstr_opt(fk_json).unwrap_or_else(|| "[]".into())) {
        Ok(v) => v, Err(e) => { write_err(err, cap, &format!("item FK: {e}")); return 1; }
    };
    match crate::run_no_panic(|| crate::okmodel_view::with_library(std::path::Path::new(&root), || okmodel_view(lib, id)?.prefab_session_fk(&prefab, include_disabled != 0, &fk))) {
        Ok(s) => { *out = Box::into_raw(Box::new(s)); 0 }
        Err(e) => { write_err(err, cap, &e); 1 }
    }
}
/// Bones a prefab's skinned meshes follow (item FK), JSON array of names; returns the byte length (larger than `cap`: nothing written).
#[no_mangle]
pub unsafe extern "C" fn ok_okmodel_fk_bones(lib: *const c_char, id: *const c_char, prefab: *const c_char, out: *mut u8, cap: i32) -> i32 {
    let prefab = cstr_opt(prefab).unwrap_or_default();
    let root = cstr_opt(lib).unwrap_or_default();
    let bones = crate::run_no_panic(|| crate::okmodel_view::with_library(std::path::Path::new(&root), || okmodel_view(lib, id).map(|v| v.fk_bones(&prefab)))).unwrap_or_default();
    copy_text(&serde_json::to_string(&bones).unwrap(), out, cap)
}
/// Studio item patterns of the OK base library as JSON [[id, name], ...] (`out` null / `cap` 0: the size).
#[no_mangle]
pub unsafe extern "C" fn ok_studio_patterns(lib: *const c_char, out: *mut u8, cap: i32) -> i32 {
    let root = cstr_opt(lib).unwrap_or_default();
    let list = crate::run_no_panic(|| crate::okmodel_view::with_library(std::path::Path::new(&root), || Ok(crate::studio_patterns(std::path::Path::new(&root))))).unwrap_or_default();
    crate::ffi::copy_resource(serde_json::to_string(&list).unwrap_or_default().as_bytes(), out, cap)
}
/// One Studio item pattern's mask texture (PNG) from the OK base library (`out` null / `cap` 0: the size; -1: none).
#[no_mangle]
pub unsafe extern "C" fn ok_studio_pattern_png(lib: *const c_char, id: i32, out: *mut u8, cap: i32) -> i32 {
    let root = cstr_opt(lib).unwrap_or_default();
    let png = crate::run_no_panic(|| crate::okmodel_view::with_library(std::path::Path::new(&root), || crate::studio_pattern_png(std::path::Path::new(&root), id).ok_or_else(|| "no pattern".to_string()))).ok();
    match png { Some(b) => crate::ffi::copy_resource(&b, out, cap), None => -1 }
}
/// PNG of texture `name` in game bundle `bundle` from the OK base library (e.g. a Studio filter LUT): the byte
/// count (copied when it fits `cap`), -1 when there is no such texture.
#[no_mangle]
pub unsafe extern "C" fn ok_studio_texture_png(lib: *const c_char, bundle: *const c_char, name: *const c_char, out: *mut u8, cap: i32) -> i32 {
    let (root, bundle, name) = (cstr_opt(lib).unwrap_or_default(), cstr_opt(bundle).unwrap_or_default(), cstr_opt(name).unwrap_or_default());
    let png = crate::run_no_panic(|| crate::okmodel_view::with_library(std::path::Path::new(&root), || crate::studio_items::texture_png(std::path::Path::new(&root), &bundle, &name).ok_or_else(|| "no texture".to_string()))).ok();
    match png { Some(b) => crate::ffi::copy_resource(&b, out, cap), None => -1 }
}
/// The eye set of eyes pattern `ptn` (cha_eyeset row, `eye_set::EyeSet` as JSON, plus "changes_parts") from the OK
/// base library's lists: the byte count (copied when it fits `cap`), -1 when the lists have no eye sets.
#[no_mangle]
pub unsafe extern "C" fn ok_eye_set_json(lib: *const c_char, ptn: i32, out: *mut u8, cap: i32) -> i32 {
    let root = cstr_opt(lib).unwrap_or_default();
    let json = crate::run_no_panic(|| crate::okmodel_view::with_library(std::path::Path::new(&root), || {
        let set = crate::eye_set::eye_set(std::path::Path::new(&root), ptn).ok_or_else(|| "no eye set".to_string())?;
        let mut v = serde_json::to_value(&set).map_err(|e| e.to_string())?;
        v["changes_parts"] = serde_json::Value::Bool(set.changes_parts());
        Ok(v.to_string())
    })).ok();
    match json { Some(s) => crate::ffi::copy_resource(s.as_bytes(), out, cap), None => -1 }
}
/// Prefab-space transform of a node of a library model's prefab (Unity: position metres, quaternion xyzw, scale):
/// `out` takes 10 floats. 0 found, 1 not found (or the model's hierarchy is not recorded: `--oklib studio-nodes`).
#[no_mangle]
pub unsafe extern "C" fn ok_okmodel_node_world(lib: *const c_char, id: *const c_char, prefab: *const c_char, node: *const c_char, out: *mut f32) -> i32 {
    if out.is_null() { return 1; }
    let (prefab, node, root) = (cstr_opt(prefab).unwrap_or_default(), cstr_opt(node).unwrap_or_default(), cstr_opt(lib).unwrap_or_default());
    let found = crate::run_no_panic(|| crate::okmodel_view::with_library(std::path::Path::new(&root), || okmodel_view(lib, id).map(|v| {
        let h = v.hierarchy(&prefab);
        h.find(&node).and_then(|i| h.node_world(i))
    }))).ok().flatten();
    let Some((p, r, s)) = found else { return 1; };
    let values = [p[0], p[1], p[2], r[0], r[1], r[2], r[3], s[0], s[1], s[2]];
    std::ptr::copy_nonoverlapping(values.as_ptr(), out, 10);
    0
}
/// Current edits (JSON) of a model.
#[no_mangle]
pub unsafe extern "C" fn ok_okmodel_edits_get(lib: *const c_char, id: *const c_char, out: *mut u8, cap: i32) -> i32 {
    let (Some(lib), Some(id)) = (cstr_opt(lib), cstr_opt(id).and_then(|i| crate::oklib::id_from_hex_pub(&i))) else { return -1 };
    copy_text(&serde_json::to_string_pretty(&crate::okmodel_edit::load(std::path::Path::new(&lib), &id)).unwrap(), out, cap)
}
/// Replace the edits of a model (JSON; an empty object removes them).
#[no_mangle]
pub unsafe extern "C" fn ok_okmodel_edits_set(lib: *const c_char, id: *const c_char, json: *const c_char, err: *mut c_char, cap: i32) -> i32 {
    let result = (|| {
        let lib = cstr_opt(lib).ok_or("library path missing")?;
        let id = cstr_opt(id).and_then(|i| crate::oklib::id_from_hex_pub(&i)).ok_or("bad model id")?;
        let edits: crate::okmodel_edit::ModelEdits = serde_json::from_str(&cstr_opt(json).ok_or("edits missing")?).map_err(|e| e.to_string())?;
        crate::okmodel_edit::save(std::path::Path::new(&lib), &id, edits)?;
        crate::unity::clear_bundle_cache();
        Ok::<_, String>(())
    })();
    match result { Ok(()) => 0, Err(e) => { write_err(err, cap, &e); 1 } }
}
/// The first prefab of a model with shown meshes (a scene item's default), written to `out`; 1 when there is none.
#[no_mangle]
pub unsafe extern "C" fn ok_okmodel_first_prefab(lib: *const c_char, id: *const c_char, out: *mut c_char, cap: i32) -> i32 {
    let root = cstr_opt(lib).unwrap_or_default();
    match crate::run_no_panic(|| crate::okmodel_view::with_library(std::path::Path::new(&root), || okmodel_view(lib, id)?.first_shown_prefab().ok_or_else(|| "the model has no prefab with meshes".to_string()))) {
        Ok(name) => { write_err(out, cap, &name); 0 }
        Err(e) => { write_err(out, cap, &e); 1 }
    }
}
/// Studio FK rotation handle: Unity local Euler degrees turned by `degrees` about the bone's own local axis
/// (0 X, 1 Y, 2 Z), written back as Unity Euler degrees (the order FK stores).
#[no_mangle]
pub unsafe extern "C" fn ok_unity_euler_rotate(euler: *const f32, axis: i32, degrees: f32, out: *mut f32) -> i32 {
    if euler.is_null() || out.is_null() || !(0..3).contains(&axis) || !degrees.is_finite() { return -1; }
    let e = std::slice::from_raw_parts(euler, 3);
    let mut unit = [0.0f32; 3];
    unit[axis as usize] = 1.0;
    let q = crate::pose::quat_mul(crate::pose::euler_deg_to_quat([e[0], e[1], e[2]]), crate::pose::quat_axis_angle(unit, degrees.to_radians()));
    let r = crate::pose::quat_to_unity_euler(q);
    std::ptr::copy_nonoverlapping(r.as_ptr(), out, 3);
    0
}
/// Timeline FK: the rotation `t` (0..1) of the way from Unity Euler degrees `a` to `b` (shortest arc), as Unity Euler degrees.
#[no_mangle]
pub unsafe extern "C" fn ok_unity_euler_slerp(a: *const f32, b: *const f32, t: f32, out: *mut f32) -> i32 {
    if a.is_null() || b.is_null() || out.is_null() || !t.is_finite() { return -1; }
    let (a, b) = (std::slice::from_raw_parts(a, 3), std::slice::from_raw_parts(b, 3));
    let qa = crate::pose::euler_deg_to_quat([a[0], a[1], a[2]]);
    let mut qb = crate::pose::euler_deg_to_quat([b[0], b[1], b[2]]);
    let mut dot: f32 = (0..4).map(|i| qa[i] * qb[i]).sum();
    if dot < 0.0 { qb = [-qb[0], -qb[1], -qb[2], -qb[3]]; dot = -dot; }
    let q = if dot > 0.9995 {
        let l: [f32; 4] = std::array::from_fn(|i| qa[i] + (qb[i] - qa[i]) * t);
        let n = l.iter().map(|v| v * v).sum::<f32>().sqrt().max(1e-9);
        l.map(|v| v / n)
    } else {
        let th = dot.clamp(-1.0, 1.0).acos();
        let (sa, sb) = (((1.0 - t) * th).sin() / th.sin(), (t * th).sin() / th.sin());
        std::array::from_fn(|i| qa[i] * sa + qb[i] * sb)
    };
    let e = crate::pose::quat_to_unity_euler(q);
    std::ptr::copy_nonoverlapping(e.as_ptr(), out, 3);
    0
}
/// KStudio scene import step 1 (`studio_scene::export`): JSON in `out` (needs a large buffer); 0 ok, 1 error.
#[no_mangle]
pub unsafe extern "C" fn ok_studio_scene_export(scene: *const c_char, out_dir: *const c_char, out: *mut c_char, cap: i32) -> i32 {
    let result = (|| {
        let scene = cstr_opt(scene).ok_or("scene path missing")?;
        let dir = cstr_opt(out_dir).ok_or("output folder missing")?;
        let json = crate::studio_scene::export(std::path::Path::new(&scene), std::path::Path::new(&dir))?;
        let text = serde_json::to_string(&json).map_err(|e| e.to_string())?;
        if text.len() + 1 > cap.max(0) as usize { return Err(format!("needs {} bytes", text.len() + 1)); }
        Ok(text)
    })();
    match result { Ok(json) => { write_err(out, cap, &json); 0 } Err(e) => { write_err(out, cap, &e); 1 } }
}
/// KK Studio pose file -> JSON (`studio_pose::Pose`) in `out`; 0 ok, 1 error (message in `out`).
#[no_mangle]
pub unsafe extern "C" fn ok_studio_pose_read(path: *const c_char, out: *mut c_char, cap: i32) -> i32 {
    let result = (|| {
        let path = cstr_opt(path).ok_or("pose path missing")?;
        let pose = crate::studio_pose::read(&std::fs::read(&path).map_err(|e| format!("{path}: {e}"))?)?;
        serde_json::to_string(&pose).map_err(|e| e.to_string())
    })();
    match result { Ok(json) => { write_err(out, cap, &json); 0 } Err(e) => { write_err(out, cap, &e); 1 } }
}
/// JSON (`studio_pose::Pose`) -> KK Studio pose file; 0 ok, 1 error (message in `err`).
#[no_mangle]
pub unsafe extern "C" fn ok_studio_pose_write(path: *const c_char, json: *const c_char, err: *mut c_char, cap: i32) -> i32 {
    let result = (|| {
        let path = cstr_opt(path).ok_or("pose path missing")?;
        let pose: crate::studio_pose::Pose = serde_json::from_str(&cstr_opt(json).ok_or("pose JSON missing")?).map_err(|e| e.to_string())?;
        if let Some(dir) = std::path::Path::new(&path).parent() { std::fs::create_dir_all(dir).map_err(|e| e.to_string())?; }
        std::fs::write(&path, crate::studio_pose::write(&pose)).map_err(|e| format!("{path}: {e}"))
    })();
    match result { Ok(()) => 0, Err(e) => { write_err(err, cap, &e); 1 } }
}
/// Save a model with its edits as a new library model; the new id (32 hex) is written to `out`.
#[no_mangle]
pub unsafe extern "C" fn ok_okmodel_save_as(lib: *const c_char, id: *const c_char, out: *mut c_char, cap: i32) -> i32 {
    let result = (|| {
        let lib = cstr_opt(lib).ok_or("library path missing")?;
        let id = cstr_opt(id).and_then(|i| crate::oklib::id_from_hex_pub(&i)).ok_or("bad model id")?;
        let new = crate::okmodel_edit::save_as(std::path::Path::new(&lib), &id)?;
        crate::unity::clear_bundle_cache();
        Ok::<_, String>(crate::oklib::id_hex(&new))
    })();
    match result { Ok(hex) => { write_err(out, cap, &hex); 0 } Err(e) => { write_err(out, cap, &e); 1 } }
}
/// PNG of a texture of a model (edits applied). Returns the length (see ok_okmodel_info).
#[no_mangle]
pub unsafe extern "C" fn ok_okmodel_texture_png(lib: *const c_char, id: *const c_char, name: *const c_char, out: *mut u8, cap: i32) -> i32 {
    let name = cstr_opt(name).unwrap_or_default();
    let root = cstr_opt(lib).unwrap_or_default();
    match crate::run_no_panic(|| crate::okmodel_view::with_library(std::path::Path::new(&root), || okmodel_view(lib, id)?.texture_png(&name).ok_or_else(|| "no such texture".to_string()))) {
        Ok(png) => { if !out.is_null() && cap as usize >= png.len() { ptr::copy_nonoverlapping(png.as_ptr(), out, png.len()); } png.len() as i32 }
        Err(_) => -1,
    }
}
