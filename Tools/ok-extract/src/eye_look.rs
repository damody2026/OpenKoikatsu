//! EyeLookCalc: auxiliary EyeTarget transforms feed iris UV/FBS, not face skinning.
use crate::finalik::*;
use crate::pose::{Mat4,Skeleton};
#[derive(Clone,serde::Serialize,serde::Deserialize,Debug)]
pub(crate) struct EyeRef {pub name:String,pub lr:i32}
#[derive(Clone,serde::Serialize,serde::Deserialize,Debug)]
pub(crate) struct Pattern {pub kind:i32,pub values:[f32;12]}
#[derive(Clone,serde::Serialize,serde::Deserialize,Debug)]
pub(crate) struct Config {
    pub root:String,pub center:String,pub eyes:Vec<EyeRef>,pub patterns:Vec<Pattern>,
    pub look:V3,pub up:V3,pub correct:bool,pub separation:f32,pub away:f32,pub control_distance:f32,
    #[serde(default)] pub init:Option<Init>,
}
#[derive(Clone,serde::Serialize,serde::Deserialize,Debug)]
pub(crate) struct Init {pub root:Quat,pub parents:[Quat;2],pub original:[Quat;2]}

#[cfg(test)]
mod tests {
    use super::*;
    fn floats<const N:usize>(v:&serde_json::Value)->[f32;N] {std::array::from_fn(|i|v[i].as_f64().expect("native float") as f32)}
    fn halves<const N:usize>(v:&serde_json::Value)->[[f32;N];2] {std::array::from_fn(|e|std::array::from_fn(|i|v[e*N+i].as_f64().unwrap() as f32))}
    #[test]
    #[ignore="game oracle: reads the original KKS installation (set OK_TEST_GAME_ROOT)"]
    fn native_eye_look_frames() {
        let root=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Saved/Tests/EyeLook285");
        let mut files=0;let mut frames=0;let mut max=0f32;let mut maxima=std::collections::BTreeMap::<String,f32>::new();
        let only=std::env::var("OK_EYE_LOOK_TEST_CARD").ok();
        for entry in crate::gamefs::read_dir(root).unwrap() {
            let path=entry.unwrap().path();if !path.to_string_lossy().ends_with(".eye-look.json") {continue;}
            if only.as_ref().is_some_and(|name|!path.file_name().unwrap().to_string_lossy().starts_with(name)) {continue;}
            let trace:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(&path).unwrap()).unwrap();
            let c:Config=serde_json::from_value(trace["config"].clone()).expect("complete native config");
            let mut runtime=Runtime{reference:halves(&trace["reference"]),up:halves(&trace["up"]),original:halves(&trace["original"]),angles:[[0.;2];2],
                dir_up:halves(&trace["up"]),fixed:halves(&trace["original"]),rates:[0.;3],target:[0.;3]};
            assert_eq!(trace["weighted_eye_slots"].as_i64(),Some(0),"native mesh binding changed: {}",path.display());
            let native_frames=trace["frames"].as_array().expect("nonempty native frames");assert_eq!(native_frames.len(),144);
            for (frame,f) in native_frames.iter().enumerate() {
                let m=floats::<16>(&f["center"]);
                let basis=Basis{root:floats(&f["root"]),rotation:floats(&f["rotation"]),center:std::array::from_fn(|i|std::array::from_fn(|j|m[i*4+j])),
                    center_rotation:floats(&f["center_rotation"]),positions:halves(&f["positions"]),parents:halves(&f["parents"])};
                let mut control=floats(&f["control"]);
                assert!(runtime.step(&c,&basis,floats(&f["input"]),f["pattern"].as_u64().unwrap() as usize,f["dt"].as_f64().unwrap() as f32,f["enabled"].as_bool().unwrap(),Some(&mut control)));
                for (key,actual) in [("angles",runtime.angles.concat()),("rates",runtime.rates.to_vec()),("dir_up",runtime.dir_up.concat()),("target",runtime.target.to_vec())] {
                    for (i,v) in actual.iter().enumerate() {let expected=f[key][i].as_f64().unwrap() as f32;let delta=(v-expected).abs();max=max.max(delta);
                        let tolerance=match key {"angles"=>0.002,"rates"=>0.00005,"dir_up"=>0.00001,_=>0.000001};
                        assert!(delta<tolerance,"{} frame{frame} {key}[{i}] rust={v} native={expected} error={delta}",path.display());}
                    let error=actual.iter().enumerate().map(|(i,v)|(v-f[key][i].as_f64().unwrap() as f32).abs()).fold(0f32,f32::max);
                    maxima.entry(key.into()).and_modify(|v|*v=v.max(error)).or_insert(error);
                }
                let rotations: [[f32;4];2]=halves(&f["rotations"]);
                for i in 0..2 {let dot=qdot(runtime.fixed[i],rotations[i]);
                    for j in 0..4 {let delta=(runtime.fixed[i][j]*sign(dot)-rotations[i][j]).abs();
                        assert!(delta<2e-5,"native quaternion {frame} {i}/{j} delta={delta}");
                        maxima.entry("rotation_components".into()).and_modify(|v|*v=v.max(delta)).or_insert(delta);
                    }
                }
                frames+=1;
            }
            let rig=crate::unity::parse_bundle_cached(crate::gamefs::read(crate::test_support::game_path("/abdata/studio/base/00.unity3d")).unwrap());
            let loaded=rig.eye_look_config("p_cf_head_bone").expect("source reference metadata");
            assert_eq!(serde_json::to_value(loaded).unwrap(),serde_json::to_value(&c).unwrap(),"actual source metadata");
            files+=1;
        }
        assert_eq!(files,if only.is_some(){1}else{5});assert_eq!(frames,files*144);println!("native EyeLookCalc {files} cards {frames} frames max_scalar_error={max}");
        println!("native field maxima={maxima:?}");
    }
    #[test]
    #[ignore = "loads five actual cards; run release with --ignored"]
    fn native_eye_look_session_basis() {
        let root=std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Saved/Tests/EyeLook285");
        let game=&crate::test_support::game_path("");
        let mut checked=0;
        for (card,stem) in [("[Card] March_7th_keiyuu","_Card__March_7th_keiyuu"),("Alice_keiyuu","Alice_keiyuu"),
            ("Hayase_yuuka_keiyuu_V1_02 For KKS","Hayase_yuuka_keiyuu_V1_02_For_KKS"),("KK_091207","KK_091207"),("KK_384283","KK_384283")] {
            let trace:serde_json::Value=serde_json::from_slice(&crate::gamefs::read(root.join(format!("{stem}EyeLook285.json.eye-look.json"))).unwrap()).unwrap();
            let path=game.join(format!("UserData/chara/female/{card}.png"));
            let mut session=crate::session::open(game,Some(&path),0).unwrap();
            session.apply_anim_frame(0.4,0.);
            let config=session.eye_look_config.as_ref().unwrap();
            let runtime=Runtime::from_session(&session,config).expect("actual Awake eye transforms");
            for (key,actual) in [("reference",runtime.reference.concat()),("up",runtime.up.concat()),("original",runtime.original.concat())] {
                for (i,v) in actual.iter().enumerate() {assert!((v-trace[key][i].as_f64().unwrap() as f32).abs()<1e-5,"{card} Init {key}{i} = {v}");}
            }
            let basis=Basis::from_session(&session,config).expect("world target pose basis");
            let frame=&trace["frames"][0];let native_matrix=floats::<16>(&frame["center"]);
            let native_center=std::array::from_fn(|i|std::array::from_fn(|j|native_matrix[i*4+j]));
            let native_positions: [V3;2]=halves(&frame["positions"]);
            let native_parents: [Quat;2]=halves(&frame["parents"]);
            for i in 0..2 {
                let actual=point(&crate::pose::mat_inv_pub(&basis.center),basis.positions[i]);
                let expected=point(&crate::pose::mat_inv_pub(&native_center),native_positions[i]);
                assert!(vlen(vsub(actual,expected))<2e-5,"{card} eye{i} center-local geometry actual={actual:?} native={expected:?}");
                let actual=qmul(qinv(basis.rotation),basis.parents[i]);
                let expected=qmul(qinv(floats(&frame["rotation"])),native_parents[i]);
                assert!((qdot(actual,expected).abs()-1.).abs()<2e-6,"{card} eye{i} relative parent rotation");
            }
            let rates=session.step_eye_look(vadd(basis.root,qrot(basis.rotation,[0.5,-0.2,2.])),1,1./60.,true).unwrap();
            assert!(rates.iter().all(|r|r.is_finite()&&r.abs()<=1.));
            checked+=1;
        }
        assert_eq!(checked,5);println!("native EyeLookCalc Awake references and posed world basis: {checked} cards");
    }
}
#[derive(Clone,Debug)]
pub(crate) struct Basis {pub root:V3,pub rotation:Quat,pub center:Mat4,pub center_rotation:Quat,pub positions:[V3;2],pub parents:[Quat;2]}
#[derive(Clone,Debug)]
pub(crate) struct Runtime {
    pub reference:[V3;2],pub up:[V3;2],pub original:[Quat;2],pub angles:[[f32;2];2],
    pub dir_up:[V3;2],pub fixed:[Quat;2],pub rates:[f32;3],pub target:V3,
}
fn clamp(x:f32,a:f32,b:f32)->f32 {if x<a {a}else if x>b {b}else{x}}
fn lerp(a:f32,b:f32,t:f32)->f32 {a+(b-a)*clamp(t,0.,1.)}
fn inverse(a:f32,b:f32,x:f32)->f32 {if a==b {0.}else{clamp((x-a)/(b-a),0.,1.)}}
fn sign(x:f32)->f32 {if x<0. {-1.}else{1.}}
fn angle(a:V3,b:V3)->f32 {let den=(vdot(a,a)*vdot(b,b)).sqrt();if den<1e-15 {0.}else{clamp(vdot(a,b)/den,-1.,1.).acos().to_degrees()}}
fn around(a:V3,b:V3,axis:V3)->f32 {let a=vsub(a,vproject(a,axis));let b=vsub(b,vproject(b,axis));angle(a,b)*sign(vdot(axis,vcross(a,b)))}
fn point(m:&Mat4,p:V3)->V3 {std::array::from_fn(|i|m[i][0]*p[0]+m[i][1]*p[1]+m[i][2]*p[2]+m[i][3])}
fn vector(m:&Mat4,p:V3)->V3 {std::array::from_fn(|i|m[i][0]*p[0]+m[i][1]*p[1]+m[i][2]*p[2])}
fn towards(a:V3,b:V3,d:f32)->V3 {let v=vsub(b,a);let len=vlen(v);if len<=d||len==0. {b}else{vadd(a,vmul(v,d/len))}}
fn rotation(sk:&Skeleton,name:&str)->Option<Quat> {
    let mut id=*sk.tfs.iter().filter(|(_,t)|t.name==name).map(|(id,_)|id).min()?;
    let mut q=IDENTITY;
    for _ in 0..sk.tfs.len() {let t=sk.tfs.get(&id)?;q=qmul(t.rot,q);match t.parent {Some(p) if sk.tfs.contains_key(&p)=>id=p,_=>return Some(qnorm(q))}}
    None
}
impl Basis {
    pub fn from_session(s:&crate::session::OkSession,c:&Config)->Option<Self> {
        let h=s.head_skel.as_ref()?;
        let b=s.body_skel.as_ref()?;
        let attach=crate::head_onto_body(Some(b),Some(h));
        let attach_rotation=qmul(rotation(b,"cf_s_head")?,qinv(rotation(h,&c.root)?));
        let world=|name:&str|h.world_tf(name).map(|m|crate::pose::mat_mul_pub(&attach,&m));
        let wr=|name:&str|rotation(h,name).map(|q|qmul(attach_rotation,q));
        let root=world(&c.root)?;
        let center=if c.center.is_empty() {root}else{world(&c.center)?};
        let mut positions=[[0.;3];2];let mut parents=[IDENTITY;2];
        for (i,e) in c.eyes.iter().enumerate() {
            positions[i]=crate::pose::mat_pos(&world(&e.name)?);
            let t=h.tfs.values().find(|t|t.name==e.name)?;
            parents[i]=wr(&h.tfs.get(&t.parent?)?.name)?;
        }
        Some(Self{root:crate::pose::mat_pos(&root),rotation:wr(&c.root)?,center,center_rotation:wr(if c.center.is_empty(){&c.root}else{&c.center})?,positions,parents})
    }
}
impl Runtime {
    pub fn new(c:&Config,rotation:Quat,parents:[Quat;2],original:[Quat;2])->Self {
        let reference=parents.map(|q|qrot(qinv(q),qrot(rotation,vnorm(c.look))));
        let up=parents.map(|q|qrot(qinv(q),qrot(rotation,vnorm(c.up))));
        Self{reference,up,original,angles:[[0.;2];2],dir_up:up,fixed:original,rates:[0.;3],target:[0.;3]}
    }
    pub fn init_from_skeleton(h:&Skeleton,c:&Config)->Option<Init> {
        let mut parents=[IDENTITY;2];let mut original=[IDENTITY;2];
        for (i,e) in c.eyes.iter().enumerate() {let t=h.tfs.values().find(|t|t.name==e.name)?;
            parents[i]=rotation(h,&h.tfs.get(&t.parent?)?.name)?;original[i]=t.rot;}
        Some(Init {root:rotation(h,&c.root)?,parents,original})
    }
    pub fn from_session(s:&crate::session::OkSession,c:&Config)->Option<Self> {
        let init=c.init.clone().or_else(||Self::init_from_skeleton(s.head_rest.as_ref()?,c))?;
        Some(Self::new(c,init.root,init.parents,init.original))
    }
    pub fn step(&mut self,c:&Config,b:&Basis,mut target:V3,pattern:usize,dt:f32,enabled:bool,mut control:Option<&mut V3>)->bool {
        let Some(p)=c.patterns.get(pattern) else{return false;};
        if !dt.is_finite()||dt<0.||target.iter().any(|v|!v.is_finite()) {return false;}
        if !enabled||dt==0. {return true;}
        let s=p.values;let mut kind=p.kind;
        if kind!=0 {
            let mut v=vsub(target,b.root);
            if kind!=1&&vlen(v)<s[7] {v=vmul(vnorm(v),s[7]);target=vadd(b.root,v);}
            let forward=qrot(b.rotation,[0.,0.,1.]);
            if angle([v[0],forward[1],v[2]],forward)>s[10]||angle([forward[0],v[1],v[2]],forward)>s[11] {kind=3;}
            if kind==3 {target=vadd(b.root,vmul(qrot(qmul(b.rotation,qangle_axis(5.,[1.,0.,0.])),[0.,0.,1.]),s[9]));}
            if let Some(t)=control.as_deref_mut() {if kind==4 {target=towards(b.root,*t,s[9]);}*t=towards(b.root,target,c.control_distance);}
            let mut targets=[target;2];
            if c.correct&&!c.center.is_empty()&&(kind==1||kind==3) {
                let mut local=point(&crate::pose::mat_inv_pub(&b.center),target);local[2]=local[2].max(0.5);
                let corrected=point(&b.center,local);
                let normal=vnorm(vsub(corrected,crate::pose::mat_pos(&b.center)));
                let horizontal=vnorm(vcross(vnorm(vcross([0.,1.,0.],normal)),[0.,1.,0.]));
                let local_rotation=qmul(qinv(b.center_rotation),qlook_rotation(horizontal,[0.,1.,0.]));
                for i in 0..2 {targets[i]=vadd(corrected,vector(&b.center,qrot(local_rotation,[if i==0 {-c.separation}else{c.separation},0.,0.])));}
            }
            let mut shared=-1.;
            for i in 0..2 {
                let dir=qrot(qinv(b.parents[i]),vnorm(vsub(if kind==1||kind==3 {targets[i]}else{target},b.positions[i])));
                let mut h=around(self.reference[i],dir,self.up[i]);
                let mut v=around(vsub(dir,vproject(dir,self.up[i])),dir,vcross(self.up[i],dir));
                let bend=|f:f32| ((f.abs()-s[0]).max(0.)*s[1].abs()).max(f.abs()-s[2])*sign(f)*sign(s[1]);
                h=bend(h);v=bend(v);
                let (min,max)=if c.eyes[i].lr==1 {(-s[4],-s[3])}else{(s[3],s[4])};
                h=clamp(h,min,max);v=clamp(v,s[5],s[6]);
                if kind==2 {
                    if shared==-1. {
                        let mut previous=lerp(-1.,1.,inverse(-s[4],-s[3],self.angles[i][0]));
                        let proposed=lerp(-1.,1.,inverse(-s[4],-s[3],h));let diff=previous-proposed;
                        if diff.abs()<c.away {
                            previous=if diff<0. {if proposed< -c.away {proposed+c.away}else{proposed-c.away}}
                                else if diff==0. {proposed+c.away}else if proposed>c.away {proposed-c.away}else{proposed+c.away};
                            shared=inverse(-1.,1.,previous);h=lerp(-s[4],-s[3],shared);
                        } else {shared=inverse(-1.,1.,previous);h=self.angles[i][0];}
                    } else {h=lerp(-s[4],-s[3],shared);}
                    v=-v;
                }
                self.angles[i]=[lerp(self.angles[i][0],h,dt*s[8]),lerp(self.angles[i][1],v,dt*s[8])];
                let mut normal=qrot(qmul(qangle_axis(self.angles[i][0],self.up[i]),qangle_axis(self.angles[i][1],vcross(self.up[i],self.reference[i]))),self.reference[i]);
                let (normalized,tangent)=vortho_normalize(normal,self.up[i]);normal=normalized;
                self.dir_up[i]=vslerp(self.dir_up[i],tangent,clamp(dt*5.,0.,1.));
                let (normalized,up)=vortho_normalize(normal,self.dir_up[i]);normal=normalized;self.dir_up[i]=up;
                self.fixed[i]=qmul(qmul(qlook_rotation(normal,self.dir_up[i]),qinv(qlook_rotation(self.reference[i],self.up[i]))),self.original[i]);
            }
            self.target=target;
        }
        for i in 0..2 {let (min,max)=if c.eyes[i].lr==1 {(-s[4],-s[3])}else{(s[3],s[4])};self.rates[i]=lerp(-1.,1.,inverse(min,max,self.angles[i][0]));}
        let v=self.angles[0][1];let (positive,negative)=if s[6]<=s[5] {(s[5],s[6])}else{(s[6],s[5])};
        self.rates[2]=if v>=0. {-inverse(0.,positive,v)}else{inverse(0.,negative,v)};
        true
    }
}
