//! Explicit Animator.CrossFade on the base or selected independent layers.
use super::*;

#[derive(Clone,Copy,Debug,Default,PartialEq)]
pub(crate) struct Clock {pub epoch:f32,pub phase:f32}
#[derive(Clone,Debug,PartialEq)]
pub(crate) struct CrossFade {
    pub from:String,pub to:String,pub from_clock:Clock,pub to_clock:Clock,
    pub start:f32,pub seconds:f32,pub source_length:f32,
    pub layer:Option<usize>,
}
fn plan(game:&Path,key:&str,values:&HashMap<String,f32>)->Option<(String,controller::StateEntry,Vec<(ClipEntry,f32,f32,f32)>,f32)> {
    let (prefix,label)=key.split_once('|')?;let source=prefix.strip_prefix(controller::SOURCE_PREFIX)?;
    let resolved=source_path(game,source)?;
    let mut e=controller::entries(&resolved).iter().find(|e|e.label==label)?.clone();
    if !controller::plain_base_pair(&e,&e) {return None;}
    if e.motion.is_none() {e.motion=Some(controller::MotionNode::Clip{clip:e.clip.clone(),cycle:0.,duration_factor:1.});}
    let p=motion_plan_entry(game,source,e,values)?;
    (p.1.timing.speed.is_finite()&&p.1.timing.speed>0.&&valid_animation_clock(game,key,values)).then_some(p)
}
pub(crate) fn declared_float(game:&Path,key:&str,name:&str)->bool {
    let Some((prefix,label))=key.split_once('|') else{return false;};
    let Some(source)=prefix.strip_prefix(controller::SOURCE_PREFIX).and_then(|s|source_path(game,s)) else{return false;};
    controller::entries(&source).iter().find(|e|e.label==label).is_some_and(|e|controller::plain_base_pair(e,e)&&e.float_names.iter().any(|n|n==name))
}
pub(crate) fn length(game:&Path,key:&str,values:&HashMap<String,f32>)->Option<f32> {
    let (_,e,_,duration)=plan(game,key,values)?;let length=duration/e.timing.speed;
    (length.is_finite()&&length>0.).then_some(length)
}
impl CrossFade {
    pub fn new(game:&Path,from:&str,to:&str,values:&HashMap<String,f32>,clock:Clock,start:f32,blend:f32,offset:f32)->Option<Self> {
        if ![start,blend,offset,clock.epoch,clock.phase].iter().all(|v|v.is_finite())||start<0.||blend<0. {return None;}
        let (a,ea,_,duration)=plan(game,from,values)?;let(b,eb,_,_)=plan(game,to,values)?;
        if a!=b||!controller::plain_base_pair(&ea,&eb) {return None;}
        let source_length=duration/ea.timing.speed;let seconds=(source_length as f64*blend as f64) as f32;
        if !source_length.is_finite()||source_length<=0.||!seconds.is_finite()||(blend>0.&&seconds<=0.) {return None;}
        Some(Self{from:from.into(),to:to.into(),from_clock:clock,to_clock:Clock{epoch:start,phase:offset},start,seconds,source_length,layer:None})
    }
    pub fn new_layer(game:&Path,from:&str,to:&str,index:usize,values:&HashMap<String,f32>,start:f32,blend:f32,offset:f32)->Option<Self> {
        if ![start,blend,offset].iter().all(|v|v.is_finite())||start<0.||blend<0.||index==0 {return None;}
        let resolve=|key:&str| {let (source,label)=key.split_once('|')?;let resolved=source_path(game,source.strip_prefix(controller::SOURCE_PREFIX)?)?;Some((resolved,label.to_string()))};
        let (ra,la)=resolve(from)?;let(rb,lb)=resolve(to)?;
        let (ClipBundleSource::Layers{controller:a,base_label:ba,..},ClipBundleSource::Layers{controller:b,base_label:bb,..})=(&ra,&rb) else{return None;};
        if a.key()!=b.key()||ba!=bb||la!=lb||!valid_animation_clock(game,from,values)||!valid_animation_clock(game,to,values) {return None;}
        let (source,ea,_,_)=motion_plan(game,from,values)?;let(_,eb,_,_)=motion_plan(game,to,values)?;
        if ea.active_layers.len()!=eb.active_layers.len() {return None;}
        let mut pair=None;
        for (x,y) in ea.active_layers.iter().zip(&eb.active_layers) {
            if x.layer.as_ref()?.index!=y.layer.as_ref()?.index {return None;}
            if x.layer.as_ref()?.index==index {if !controller::independent_layer_pair(x,y) {return None;}pair=Some((x,y));}
            else if x.label!=y.label {return None;}
        }
        let (x,y)=pair?;let(_,x,_,duration)=motion_plan_entry(game,&source,x.clone(),values)?;
        let(_,target,_,target_duration)=motion_plan_entry(game,&source,y.clone(),values)?;
        let source_length=duration/x.timing.speed;let target_length=target_duration/target.timing.speed;
        let seconds=(source_length as f64*blend as f64) as f32;
        let clock=Clock{epoch:values.get(&controller::layer_start_time_name(index)).copied().unwrap_or(0.),phase:values.get(&controller::layer_start_phase_name(index)).copied().unwrap_or(0.)};
        if ![source_length,target_length,seconds,clock.epoch,clock.phase].iter().all(|v|v.is_finite())||source_length<=0.||target_length<=0.||seconds<0.||(blend>0.&&seconds==0.)||clock.epoch>start {return None;}
        Some(Self{from:from.into(),to:to.into(),from_clock:clock,to_clock:Clock{epoch:start,phase:offset},start,seconds,source_length,layer:Some(index)})
    }
    pub fn weight(&self,t:f32)->f32 {if self.seconds==0. {1.}else{((t as f64-self.start as f64)/self.seconds as f64).clamp(0.,1.) as f32}}
    pub fn sample(&self,game:&Path,skel:&mut Skeleton,t:f32,values:&HashMap<String,f32>) {
        if let Some(index)=self.layer {
            sample_layers(game,skel,&self.to,t,values,&std::collections::BTreeMap::from([(index,self.clone())]));return;
        }
        let w=self.weight(t);
        if w==0. {sample(game,skel,&self.from,t,values,self.from_clock);return;}
        if w==1. {sample(game,skel,&self.to,t,values,self.to_clock);return;}
        let mut from=skel.clone();sample(game,&mut from,&self.from,t,values,self.from_clock);
        sample(game,skel,&self.to,t,values,self.to_clock);
        blend_poses(skel,&from,w);
    }
}
pub(crate) fn sample_layers(game:&Path,skel:&mut Skeleton,key:&str,t:f32,values:&HashMap<String,f32>,fades:&std::collections::BTreeMap<usize,CrossFade>) {
    let Some((source,entry,leaves,duration))=motion_plan(game,key,values) else{return;};
    let baseline=skel.clone();let mut base=entry.clone();base.active_layers.clear();
    apply_motion_pose(game,skel,&baseline,&baseline,&source,&base,leaves,duration,t,values,1.,0.,0.);
    for child in &entry.active_layers {
        let layer=child.layer.as_ref().unwrap();let accumulated=skel.clone();
        let weight=values.get(&controller::layer_weight_name(layer.index)).copied().unwrap_or(1.);
        let fade=fades.get(&layer.index);
        let clock=if let Some(f)=fade {if t<f.start {f.from_clock}else{f.to_clock}}
            else if layer.synchronized {Clock::default()}
            else {Clock{epoch:values.get(&controller::layer_start_time_name(layer.index)).copied().unwrap_or(0.),phase:values.get(&controller::layer_start_phase_name(layer.index)).copied().unwrap_or(0.)}};
        if t!=f32::MAX&&t<clock.epoch {continue;}
        let mut target=child.clone();
        if let Some(f)=fade.filter(|f|t<f.start) {
            let Some((_,root,_,_))=motion_plan(game,&f.from,values) else{return;};
            let Some(from)=root.active_layers.iter().find(|c|c.layer.as_ref().unwrap().index==layer.index) else{return;};
            target=from.clone();
        }
        let Some((_,target,leaves,duration))=motion_plan_entry(game,&source,target,values) else{return;};
        let Some(fade)=fade.filter(|f|t>=f.start) else {
            apply_motion_pose(game,skel,&baseline,&accumulated,&source,&target,leaves,duration,t,values,weight,clock.epoch,clock.phase);continue;
        };
        let mut incoming=baseline.clone();
        if !sample_motion_pose(game,&mut incoming,&baseline,&source,&target,leaves,duration,t,values,clock.epoch,clock.phase) {return;}
        let alpha=fade.weight(t);
        if alpha<1. {
            let Some((_,root,_,_))=motion_plan(game,&fade.from,values) else{return;};
            let Some(from)=root.active_layers.iter().find(|c|c.layer.as_ref().unwrap().index==layer.index) else{return;};
            let Some((_,from,leaves,duration))=motion_plan_entry(game,&source,from.clone(),values) else{return;};
            let mut outgoing=baseline.clone();
            if !sample_motion_pose(game,&mut outgoing,&baseline,&source,&from,leaves,duration,t,values,fade.from_clock.epoch,fade.from_clock.phase) {return;}
            blend_poses(&mut incoming,&outgoing,alpha);
        }
        *skel=incoming;compose_motion_layer(skel,&accumulated,&target,weight);
    }
}
fn blend_poses(skel:&mut Skeleton,from:&Skeleton,w:f32) {
        for(&id,tf) in &mut skel.tfs {
            let a=&from.tfs[&id];
            for i in 0..3 {tf.pos[i]=a.pos[i]*(1.-w)+tf.pos[i]*w;tf.scale[i]=a.scale[i]*(1.-w)+tf.scale[i]*w;}
            let sign=if a.rot.iter().zip(tf.rot).map(|(a,b)|a*b).sum::<f32>()<0. {-1.}else{1.};
            tf.rot=norm_quat(std::array::from_fn(|i|a.rot[i]*(1.-w)+tf.rot[i]*w*sign));
        }
        skel.rebuild_worlds();
}
pub(crate) fn sample(game:&Path,skel:&mut Skeleton,key:&str,t:f32,values:&HashMap<String,f32>,clock:Clock) {
    if clock==Clock::default() {apply_clip_at_parameters(game,skel,key,t,values);return;}
    let Some((source,e,leaves,duration))=plan(game,key,values) else{return;};
    let baseline=skel.clone();apply_motion_pose(game,skel,&baseline,&baseline,&source,&e,leaves,duration,t,values,1.,clock.epoch,clock.phase);
}
