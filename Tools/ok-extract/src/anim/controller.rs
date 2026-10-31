//! Explicit RuntimeAnimatorController state loading. Never search another
//! controller when a state or a PPtr cannot be resolved.
use super::*;
use std::sync::Arc;

pub(super) const SOURCE_PREFIX: &str = "states:";

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize)]
pub(super) struct StateTiming {
    pub speed: f32,
    pub cycle: f32,
    pub looping: bool,
}

impl StateTiming {
    pub fn clip_time(self, elapsed: f32, duration: f32) -> f32 {
        if elapsed == f32::MAX { return duration; }
        let time = elapsed * self.speed + self.cycle * duration;
        if self.looping { time.rem_euclid(duration) } else { time.clamp(0.0, duration) }
    }
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub(super) struct StateEntry {
    pub label: String,
    pub clip: ClipEntry,
    pub timing: StateTiming,
    pub motion: Option<MotionNode>,
    pub parameters: Vec<FloatParameter>,
    pub float_names: Vec<String>,
    pub float_defaults: Vec<FloatParameter>,
    pub(super) short_name_id: u32,
    pub full_path_id: i32,
    pub studio_layer: Option<usize>,
    // Keep the authored multiplier parameter separate from the already
    // default-multiplied clock used by legacy raw state sampling.
    pub speed_parameter: Option<FloatParameter>,
    pub layer: Option<OverrideLayer>,
    pub active_layers: Vec<StateEntry>,
    trigger_rules: Vec<TriggerRule>,
    controller_id: i64,
    state_index: usize,
}

impl StateEntry {
    pub fn parameterized_speed(&self, values: &HashMap<String,f32>) -> f32 {
        match &self.speed_parameter {
            Some(p) => self.timing.speed * values.get(&p.name).copied().unwrap_or(p.default) / p.default,
            None => self.timing.speed,
        }
    }
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub(super) struct OverrideLayer {
    pub index: usize,
    pub base_label: String,
    base_state_index: usize,
    pub mask: Vec<(u32, f32)>,
    pub synchronized: bool,
    pub additive: bool,
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
struct TriggerRule { name:String, phase:Option<f32> }

// AnyState rules precede current-state rules. Keep unsupported matching
// events as blockers: a partial reset would not reproduce the controller.
fn trigger_rules(state:&UnityValue,sm:&UnityValue,ctrl:&UnityValue,tos:&HashMap<i64,String>,index:usize)->Vec<TriggerRule> {
    let triggers:HashSet<_>=uv_get(ctrl,"m_Values").and_then(|v|array(v,"m_ValueArray")).unwrap_or(&[]).iter()
        .filter(|v|integer(v,"m_Type")==Some(9)).filter_map(|v|integer(v,"m_ID")).collect();
    let mut result=Vec::new();
    for (any,rules) in [(true,array(sm,"m_AnyStateTransitionConstantArray")),(false,array(state,"m_TransitionConstantArray"))] {
        for rule in rules.unwrap_or(&[]) {
            let conditions=array(rule,"m_ConditionConstantArray").unwrap_or(&[]);
            for condition in conditions {
                let Some(id)=integer(condition,"m_EventID").filter(|id|triggers.contains(id)) else {continue;};
                let Some(name)=tos.get(&id) else {continue;};
                let phase=number(rule,"m_TransitionOffset").filter(|v|v.is_finite());
                let supported=conditions.len()==1&&integer(condition,"m_ConditionMode")==Some(1)
                    &&integer(rule,"m_DestinationState")==Some(index as i64)
                    &&number(rule,"m_TransitionDuration")==Some(0.)&&!flag(rule,"m_HasExitTime")
                    &&(!any||flag(rule,"m_CanTransitionToSelf"));
                result.push(TriggerRule{name:name.clone(),phase:if supported {phase}else{None}});
            }
        }
    }
    result
}

pub(super) fn trigger_layers(entry:&StateEntry,name:&str)->Option<Vec<(usize,f32)>> {
    // This API covers immediate self re-entry of explicitly selected layers.
    // Base-state changes and pending/conditional transitions need a scheduler.
    if entry.active_layers.is_empty()||entry.trigger_rules.iter().any(|r|r.name==name) {return None;}
    let mut layers=Vec::new();
    for child in &entry.active_layers {
        if let Some(rule)=child.trigger_rules.iter().find(|r|r.name==name) {
            let layer=child.layer.as_ref()?;
            if layer.synchronized {return None;}
            layers.push((layer.index,rule.phase?));
        }
    }
    (!layers.is_empty()).then_some(layers)
}

pub(super) fn trigger_names(entry:&StateEntry)->Vec<String> {
    let mut names:Vec<_>=entry.active_layers.iter().flat_map(|e|e.trigger_rules.iter().map(|r|r.name.clone())).collect();
    names.sort();names.dedup();names.retain(|name|trigger_layers(entry,name).is_some());names
}

pub(super) fn plain_base_pair(a:&StateEntry,b:&StateEntry)->bool {
    a.controller_id==b.controller_id&&[a,b].iter().all(|e|e.layer.is_none()&&e.active_layers.is_empty()&&e.studio_layer.is_none())
}

pub(super) fn independent_layer_pair(a:&StateEntry,b:&StateEntry)->bool {
    let (Some(x),Some(y))=(&a.layer,&b.layer) else{return false;};
    a.controller_id==b.controller_id&&a.active_layers.is_empty()&&b.active_layers.is_empty()
        &&x.index==y.index&&x.index>0&&x.base_label==y.base_label&&x.base_state_index==y.base_state_index
        &&!x.synchronized&&!y.synchronized&&x.additive==y.additive&&x.mask==y.mask
}

#[cfg(test)]
mod trigger_tests {
    use super::*;
    fn obj(fields:Vec<(&str,UnityValue)>)->UnityValue {UnityValue::Object(fields.into_iter().map(|(k,v)|(k.into(),v)).collect())}
    fn rule(duration:f64,exit:bool,destination:i64,can_self:bool,phase:f64,mode:i64)->UnityValue {
        obj(vec![("m_ConditionConstantArray",UnityValue::Array(vec![obj(vec![("m_EventID",UnityValue::Integer(11)),("m_ConditionMode",UnityValue::Integer(mode))])])),
            ("m_DestinationState",UnityValue::Integer(destination)),("m_TransitionDuration",UnityValue::Float(duration)),
            ("m_HasExitTime",UnityValue::Bool(exit)),("m_CanTransitionToSelf",UnityValue::Bool(can_self)),("m_TransitionOffset",UnityValue::Float(phase))])
    }
    #[test]
    fn trigger_metadata_keeps_priority_and_unsupported_guards() {
        let tos=HashMap::from([(11,"Pulse".to_string())]);
        let controller=|kind|obj(vec![("m_Values",obj(vec![("m_ValueArray",UnityValue::Array(vec![obj(vec![("m_ID",UnityValue::Integer(11)),("m_Type",UnityValue::Integer(kind))])]))]))]);
        let state=obj(vec![("m_TransitionConstantArray",UnityValue::Array(vec![rule(0.,false,4,true,0.8,1)]))]);
        let sm=|r|obj(vec![("m_AnyStateTransitionConstantArray",UnityValue::Array(vec![r]))]);
        let r=trigger_rules(&state,&sm(rule(0.,false,4,true,0.2,1)),&controller(9),&tos,4);
        assert_eq!(r.len(),2);assert_eq!(r[0].phase,Some(0.2));assert_eq!(r[1].phase,Some(0.8));
        for bad in [rule(0.1,false,4,true,0.,1),rule(0.,true,4,true,0.,1),rule(0.,false,5,true,0.,1),
            rule(0.,false,4,false,0.,1),rule(0.,false,4,true,f64::NAN,1),rule(0.,false,4,true,0.,2)] {
            let r=trigger_rules(&state,&sm(bad),&controller(9),&tos,4);
            assert_eq!(r[0].phase,None,"unsupported AnyState must block lower-priority rule");
        }
        assert!(trigger_rules(&state,&sm(rule(0.,false,4,true,0.,1)),&controller(1),&tos,4).is_empty(),"a float named like a trigger is not a trigger");
    }
}

// Runtime layer control, distinct from the source controller float namespace.
pub(super) const LAYER_WEIGHT_PARAMETER: &str = "OK.LayerWeight";

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub(crate) struct FloatParameter {
    pub id: u32, pub name: String, pub default: f32,
    pub min: f32, pub max: f32,
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub(super) enum MotionNode {
    // Unity stores the reciprocal of the authored child timeScale here.
    // Keep it with the motion when an override replaces the clip asset.
    Clip { clip: ClipEntry, cycle: f32, duration_factor: f32 },
    OneD { parameter: u32, thresholds: Vec<f32>, children: Vec<MotionNode> },
    Cartesian { parameters: [u32;2], positions: Vec<[f32;2]>,
        pairs: Vec<[f32;2]>, inverse: Vec<f32>, neighbors: Vec<Vec<usize>>, children: Vec<MotionNode> },
}

impl MotionNode {
    fn valid_additive_reference(&self)->bool {
        match self {Self::Clip{clip,..}=>clip.additive_reference_valid,
            Self::OneD{children,..}|Self::Cartesian{children,..}=>children.iter().all(Self::valid_additive_reference)}
    }
    /// The values the tree's blend nodes place children at along parameter `id` (unsorted, may repeat).
    pub fn positions_of(&self, id: u32, out: &mut Vec<f32>) {
        match self {
            Self::Clip{..} => {}
            Self::OneD{parameter,thresholds,children} => {
                if *parameter==id {out.extend_from_slice(thresholds);}
                for c in children {c.positions_of(id,out);}
            }
            Self::Cartesian{parameters:ids,positions,children,..} => {
                for (axis,p) in ids.iter().enumerate() {if *p==id {out.extend(positions.iter().map(|q|q[axis]));}}
                for c in children {c.positions_of(id,out);}
            }
        }
    }
    pub fn leaves(&self, parameters: &HashMap<u32,f32>, weight: f32, out: &mut Vec<(ClipEntry,f32,f32,f32)>) {
        match self {
            Self::Clip{clip,cycle,duration_factor} => out.push((clip.clone(),weight,*cycle,*duration_factor)),
            Self::OneD{parameter,thresholds,children} => {
                let value=parameters[parameter];
                let upper=thresholds.partition_point(|t|*t<value);
                if upper==0 { children[0].leaves(parameters,weight,out); }
                else if upper==thresholds.len() { children[upper-1].leaves(parameters,weight,out); }
                else {
                    let blend=(value-thresholds[upper-1])/(thresholds[upper]-thresholds[upper-1]);
                    if blend<1. {children[upper-1].leaves(parameters,weight*(1.-blend),out);}
                    if blend>0. {children[upper].leaves(parameters,weight*blend,out);}
                }
            }
            Self::Cartesian{parameters:ids,positions,pairs,inverse,neighbors,children} => {
                let p=[parameters[&ids[0]],parameters[&ids[1]]];
                let weights=cartesian_weights(p,positions,pairs,inverse,neighbors);
                for (child,w) in children.iter().zip(weights) {
                    if w>0. {child.leaves(parameters,weight*w,out);}
                }
            }
        }
    }
}

fn cartesian_weights(p:[f32;2],positions:&[[f32;2]],pairs:&[[f32;2]],inverse:&[f32],neighbors:&[Vec<usize>])->Vec<f32> {
    let n=positions.len();
    let mut weights=Vec::with_capacity(n);
    for(i,position) in positions.iter().enumerate() {
        let offset=[p[0]-position[0],p[1]-position[1]];
        let mut value=1f32;
        for &j in &neighbors[i] {
            let k=i*n+j;
            value=value.min(1.+(offset[0]*pairs[k][0]+offset[1]*pairs[k][1])*inverse[k]);
        }
        weights.push(value.max(0.));
    }
    let sum: f32=weights.iter().sum();
    if sum>0. {for w in &mut weights {*w/=sum;}}
    weights
}

fn parse_motion(state: &UnityValue, clips: &[Option<i64>], assets: &HashMap<i64,ClipEntry>,motion_set:usize) -> Result<MotionNode,&'static str> {
    let indices=array(state,"m_BlendTreeConstantIndexArray").ok_or("missing motion-set indices")?;
    let index=indices.get(motion_set).and_then(|v|v.as_i64()).ok_or("missing motion-set index")?;
    // An unset synchronized motion uses the base state's motion.
    let index=if index<0 {indices.first().and_then(|v|v.as_i64()).ok_or("missing base motion-set index")?} else {index};
    let tree=array(state,"m_BlendTreeConstantArray").and_then(|a|a.get(index as usize)).ok_or("invalid blend-tree index")?;
    let nodes=array(tree,"m_NodeArray").ok_or("missing motion nodes")?;
    fn visit(index:usize,nodes:&[UnityValue],clips:&[Option<i64>],assets:&HashMap<i64,ClipEntry>,stack:&mut HashSet<usize>) -> Result<MotionNode,&'static str> {
        if !stack.insert(index)||stack.len()>64 {return Err("cyclic/deep motion tree");}
        let node=nodes.get(index).map(unwrap_data).ok_or("invalid child node")?;
        if flag(node,"m_Mirror") {return Err("mirrored motion");}
        let children=array(node,"m_ChildIndices").ok_or("missing child indices")?;
        let result=if children.is_empty() {
            let duration_factor=number(node,"m_Duration").filter(|d|d.is_finite()&&*d>0.)
                .ok_or("invalid/nonpositive motion duration")?;
            let id=integer(node,"m_ClipID").ok_or("missing clip index")?;
            let clip=clips.get(id as usize).and_then(|id|*id).and_then(|id|assets.get(&id)).ok_or("external/missing clip")?.clone();
            let cycle=number(node,"m_CycleOffset").unwrap_or(0.);
            if !cycle.is_finite() {return Err("invalid cycle offset");}
            MotionNode::Clip{clip,cycle,duration_factor}
        } else {
            let parameter=integer(node,"m_BlendEventID").ok_or("missing blend parameter")? as u32;
            let kind=integer(node,"m_BlendType").ok_or("missing blend type")?;
            if kind!=0&&kind!=3 {return Err("unsupported blend type");}
            let parsed=children.iter().map(|v|v.as_i64().ok_or("invalid child index").and_then(|i|visit(i as usize,nodes,clips,assets,stack))).collect::<Result<Vec<_>,_>>()?;
            if kind==0 {
                let thresholds=array(uv_get(node,"m_Blend1dData").ok_or("missing 1D data")?,"m_ChildThresholdArray")
                .ok_or("missing thresholds")?.iter().map(|v|v.as_f64().map(|v|v as f32)).collect::<Option<Vec<_>>>().ok_or("invalid thresholds")?;
                if thresholds.len()!=children.len()||thresholds.is_empty()||thresholds.iter().any(|v|!v.is_finite())||thresholds.windows(2).any(|v|v[0]>=v[1]) {return Err("unordered/duplicate thresholds");}
                MotionNode::OneD{parameter,thresholds,children:parsed}
            } else {
                let data=uv_get(node,"m_Blend2dData").ok_or("missing 2D data")?;
                fn vectors(data:&UnityValue,key:&str)->Result<Vec<[f32;2]>,&'static str> {
                    array(data,key).ok_or("missing 2D vectors")?.iter().map(|v| {
                        let p=[number(v,"x").ok_or("invalid 2D x")?,number(v,"y").ok_or("invalid 2D y")?];
                        if p.iter().any(|v|!v.is_finite()) {return Err("nonfinite 2D vector");} Ok(p)
                    }).collect()
                }
                let positions=vectors(data,"m_ChildPositionArray")?;
                let pairs=vectors(data,"m_ChildPairVectorArray")?;
                let inverse=array(data,"m_ChildPairAvgMagInvArray").ok_or("missing pair inverse")?.iter()
                    .map(|v|v.as_f64().map(|v|v as f32)).collect::<Option<Vec<_>>>().ok_or("invalid pair inverse")?;
                let n=children.len();
                if positions.len()!=n||pairs.len()!=n*n||inverse.len()!=n*n {return Err("invalid Cartesian dimensions");}
                let neighbors=array(data,"m_ChildNeighborListArray").ok_or("missing neighbors")?.iter()
                    .map(|v|array(v,"m_NeighborArray").ok_or("missing neighbor row").and_then(|a|a.iter()
                        .map(|v|v.as_i64().and_then(|i|usize::try_from(i).ok()).ok_or("invalid neighbor index"))
                        .collect::<Result<Vec<_>,_>>())).collect::<Result<Vec<_>,_>>()?;
                if neighbors.len()!=n {return Err("invalid neighbor dimensions");}
                for(i,row) in neighbors.iter().enumerate() {
                    if row.is_empty()||row.iter().copied().collect::<HashSet<_>>().len()!=row.len() {return Err("invalid neighbor row");}
                    for &j in row {
                        if j>=n||j==i||!inverse[i*n+j].is_finite()||inverse[i*n+j]<=0.||pairs[i*n+j]==[0.,0.] {return Err("invalid Cartesian pair");}
                    }
                }
                MotionNode::Cartesian{parameters:[parameter,integer(node,"m_BlendEventYID").ok_or("missing Y parameter")? as u32],positions,pairs,inverse,neighbors,children:parsed}
            }
        };
        stack.remove(&index);Ok(result)
    }
    visit(0,nodes,clips,assets,&mut HashSet::new())
}

fn motion_parameters(motion:&MotionNode,ctrl:&UnityValue,tos:&HashMap<i64,String>) -> Result<Vec<FloatParameter>,&'static str> {
    let mut ranges=HashMap::<u32,(f32,f32)>::new();
    fn collect(node:&MotionNode,ranges:&mut HashMap<u32,(f32,f32)>) {
        fn add(ranges:&mut HashMap<u32,(f32,f32)>,id:u32,lo:f32,hi:f32) {
            ranges.entry(id).and_modify(|v|{v.0=v.0.min(lo);v.1=v.1.max(hi);}).or_insert((lo,hi));
        }
        match node {
            MotionNode::OneD{parameter,thresholds,children}=>{
                add(ranges,*parameter,thresholds[0],*thresholds.last().unwrap());
                for child in children {collect(child,ranges);}
            }
            MotionNode::Cartesian{parameters,positions,children,..}=>{
                for axis in 0..2 {add(ranges,parameters[axis],positions.iter().map(|p|p[axis]).fold(f32::INFINITY,f32::min),positions.iter().map(|p|p[axis]).fold(f32::NEG_INFINITY,f32::max));}
                for child in children {collect(child,ranges);}
            }
            MotionNode::Clip{..}=>{}
        }
    }
    collect(motion,&mut ranges);
    let mut result=Vec::new();
    for(id,(min,max)) in ranges {
        let default=default_float(ctrl,id as i64).filter(|v|v.is_finite()).ok_or("invalid float parameter default")?;
        result.push(FloatParameter{id,name:tos.get(&(id as i64)).ok_or("missing parameter name")?.clone(),default,min,max});
    }
    result.sort_by(|a,b|a.name.cmp(&b.name));Ok(result)
}

struct ControllerAsset {
    name: String,
    override_controller: bool,
    props: UnityValue,
}

fn array<'a>(value: &'a UnityValue, key: &str) -> Option<&'a [UnityValue]> {
    match uv_get(unwrap_data(value), key)? {
        UnityValue::Array(items) => Some(items),
        _ => None,
    }
}

fn integer(value: &UnityValue, key: &str) -> Option<i64> {
    uv_get(unwrap_data(value), key)?.as_i64()
}

fn number(value: &UnityValue, key: &str) -> Option<f32> {
    uv_get(unwrap_data(value), key)?.as_f64().map(|x| x as f32)
}

fn flag(value: &UnityValue, key: &str) -> bool {
    uv_get(unwrap_data(value), key).and_then(|x| x.as_bool()).unwrap_or(false)
}

// Keep the index of every pointer, including missing/external ones. Removing a
// pointer shifts subsequent m_ClipID values and can select a different asset.
fn base_clips(asset: &ControllerAsset) -> Option<Vec<Option<i64>>> {
    Some(array(&asset.props, "m_AnimationClips")?.iter().map(local_clip_pointer).collect())
}

fn resolve_controller(
    assets: &HashMap<i64, ControllerAsset>, id: i64,
) -> Result<(i64, Vec<Option<i64>>), &'static str> {
    let asset = assets.get(&id).ok_or("missing controller PPtr")?;
    if !asset.override_controller {
        return Ok((id, base_clips(asset).ok_or("missing clip array")?));
    }
    let root_id = uv_get(&asset.props, "m_Controller").and_then(local_clip_pointer)
        .ok_or("external/missing base controller")?;
    let root = assets.get(&root_id).ok_or("missing base controller")?;
    if root.override_controller { return Err("nested override controller"); }
    let originals = base_clips(root).ok_or("missing clip array")?;
    let mut clips = originals.clone();
    for pair in array(&asset.props, "m_Clips").ok_or("missing override array")? {
        let original = uv_get(pair, "m_OriginalClip").and_then(local_clip_pointer)
            .ok_or("external/missing original clip")?;
        let replacement = uv_get(pair, "m_OverrideClip").ok_or("missing override pointer")?;
        // A null override is explicitly an unchanged base clip, not clip zero.
        let is_null = integer(replacement, "m_FileID") == Some(0)
            && integer(replacement, "m_PathID").or_else(|| integer(replacement, "m_PathId")) == Some(0);
        let replacement = if is_null { Some(original) } else { local_clip_pointer(replacement) };
        for (i, base) in originals.iter().enumerate() {
            if *base == Some(original) { clips[i] = replacement; }
        }
    }
    Ok((root_id, clips))
}

fn default_float(controller: &UnityValue, id: i64) -> Option<f32> {
    if id == 0 { return Some(1.0); }
    let values = uv_get(controller, "m_Values")?;
    let param = array(values, "m_ValueArray")?.iter()
        .find(|v| integer(v, "m_ID") == Some(id) && integer(v, "m_Type") == Some(1))?;
    let index = integer(param, "m_Index")?;
    if index < 0 { return None; }
    array(uv_get(controller, "m_DefaultValues")?, "m_FloatValues")?
        .get(index as usize)?.as_f64().map(|x| x as f32)
}

fn speed_parameter(state:&UnityValue,ctrl:&UnityValue,tos:&HashMap<i64,String>) -> Result<Option<FloatParameter>,&'static str> {
    let id=integer(state,"m_SpeedParamID").unwrap_or(0);
    if id==0 {return Ok(None);}
    let default=default_float(ctrl,id).filter(|v|v.is_finite()&&*v>0.).ok_or("invalid speed parameter default")?;
    // Controller float parameters do not serialize UI ranges. The slider is
    // a positive preview range; setters validate the effective clock instead.
    Ok(Some(FloatParameter{id:id as u32,name:tos.get(&id).ok_or("missing speed parameter name")?.clone(),
        default,min:0.01,max:(default*2.).max(2.)}))
}

fn add_parameter(parameters:&mut Vec<FloatParameter>,parameter:&FloatParameter) {
    if let Some(p)=parameters.iter_mut().find(|p|p.id==parameter.id) {
        p.min=p.min.min(parameter.min);p.max=p.max.max(parameter.max);
    } else {parameters.push(parameter.clone());}
    parameters.sort_by(|a,b|a.name.cmp(&b.name));
}

fn state_entry(
    state: &UnityValue, ctrl: &UnityValue, clips: &[Option<i64>],
    clip_assets: &HashMap<i64, ClipEntry>,
    motion_set: usize,
) -> Result<(ClipEntry, StateTiming, Option<MotionNode>), &'static str> {
    let motion=parse_motion(state,clips,clip_assets,motion_set)?;
    if flag(state, "m_Mirror")
        || integer(state, "m_MirrorParamID").unwrap_or(0) != 0 {
        return Err("mirrored motion");
    }
    if flag(state, "m_IKOnFeet") { return Err("foot IK"); }
    if ["m_TimeParamID", "m_CycleOffsetParamID"].iter()
        .any(|key| integer(state, key).unwrap_or(0) != 0) { return Err("animated state time/offset"); }
    fn first(node:&MotionNode)->&ClipEntry {
        match node {MotionNode::Clip{clip,..}=>clip,MotionNode::OneD{children,..}|MotionNode::Cartesian{children,..}=>first(&children[0])}
    }
    let clip=first(&motion).clone();
    let speed = number(state, "m_Speed").ok_or("missing state speed")?
        * default_float(ctrl, integer(state, "m_SpeedParamID").unwrap_or(0))
            .ok_or("missing speed parameter default")?;
    // A scaled single clip needs the same phase evaluator as a BlendTree.
    // Its leaf cycle offset must not be folded into the state and applied twice.
    let raw_single=matches!(&motion,MotionNode::Clip{duration_factor,..} if *duration_factor==1.);
    let cycle = number(state, "m_CycleOffset").unwrap_or(0.0)
        + if raw_single {match &motion {MotionNode::Clip{cycle,..}=>*cycle,_=>0.}} else {0.};
    if !speed.is_finite() || speed <= 0.0 || !cycle.is_finite() { return Err("invalid/nonpositive state clock"); }
    let motion=if raw_single {None} else {Some(motion)};
    Ok((clip, StateTiming { speed, cycle, looping: flag(state, "m_Loop") },motion))
}

pub(super) fn additive_clip_ids(serialized:&unity_asset_binary::asset::SerializedFile)->HashSet<i64> {
    let mut assets=HashMap::new();
    for info in &serialized.objects {
        if !matches!(info.type_id,91|221){continue;}
        if let Ok(obj)=UnityObject::from_serialized_file(serialized,info) {
            assets.insert(info.path_id,ControllerAsset{name:obj.name().unwrap_or_default(),override_controller:info.type_id==221,
                props:UnityValue::Object(obj.class.properties().clone())});
        }
    }
    let mut result=HashSet::new();
    for id in assets.keys() {
        if let Ok((base,clips))=resolve_controller(&assets,*id) {
            let additive=uv_get(&assets[&base].props,"m_Controller").and_then(|ctrl|array(ctrl,"m_LayerArray"))
                .is_some_and(|layers|layers.iter().any(|layer|integer(layer,"(int&)m_LayerBlendingMode")==Some(1)));
            if additive {result.extend(clips.into_iter().flatten());}
        }
    }
    result
}

fn entries_in_source(source: &ClipBundleSource, owner:Option<&str>) -> Vec<StateEntry> {
    let Some(files)=preparation_files(source,None) else{return Vec::new();};
    let clip_entries = cached_bundle_entries(source);
    let mut entries = Vec::new();
    let mut skipped = std::collections::BTreeMap::<&str, usize>::new();
    for (file_name,serialized) in files.iter() {
        let clip_assets: HashMap<i64, ClipEntry> = clip_entries.iter()
            .filter(|c| c.asset_file == *file_name).map(|c| (c.path_id, c.clone())).collect();
        let mut assets = HashMap::new();
        for info in &serialized.objects {
            if info.type_id != 91 && info.type_id != 221 { continue; }
            let Ok(obj) = UnityObject::from_serialized_file(&serialized, info) else { continue; };
            assets.insert(info.path_id, ControllerAsset { name: obj.name().unwrap_or_default(),
                override_controller: info.type_id == 221,
                props: UnityValue::Object(obj.class.properties().clone()) });
        }
        for (&id, asset) in &assets {
            if owner.is_some_and(|name|name!=asset.name){continue;}
            let (base_id, clips) = match resolve_controller(&assets, id) {
                Ok(value) => value,
                Err(reason) => { *skipped.entry(reason).or_default() += 1; continue; }
            };
            let base = &assets[&base_id];
            let Some(ctrl) = uv_get(&base.props, "m_Controller") else { continue; };
            let Some(layers) = array(ctrl, "m_LayerArray") else { continue; };
            // Unity's base layer weight is always one, even if serialized as
            // zero. Extra layers with zero default weight contribute no pose.
            // Active extra layers require masks and additive composition.
            if layers.is_empty() || layers.iter().skip(1)
                .any(|l| number(l, "m_DefaultWeight") != Some(0.0)) {
                *skipped.entry("layer composition").or_default() += 1; continue;
            }
            if flag(&layers[0], "m_IKPass") {
                *skipped.entry("layer IK").or_default() += 1; continue;
            }
            let motion_set = integer(&layers[0], "m_StateMachineMotionSetIndex")
                .or_else(|| integer(&layers[0], "m_StateMachineSynchronizedLayerIndex")).unwrap_or(0);
            if motion_set != 0 {
                *skipped.entry("synchronized base motion set").or_default() += 1; continue;
            }
            if uv_get(unwrap_data(&layers[0]), "m_SkeletonMask").and_then(|m| array(m, "m_Data"))
                .is_some_and(|a| a.iter().any(|entry| number(entry, "m_Weight") != Some(1.0))) {
                *skipped.entry("base skeleton mask").or_default() += 1; continue;
            }
            let Some(sm_index) = integer(&layers[0], "m_StateMachineIndex") else { continue; };
            let Some(sm) = array(ctrl, "m_StateMachineArray").and_then(|a| a.get(sm_index as usize)) else { continue; };
            let Some(states) = array(sm, "m_StateConstantArray") else { continue; };
            let tos: HashMap<i64, String> = match uv_get(&base.props, "m_TOS") {
                Some(UnityValue::Array(a)) => a.iter().filter_map(|pair| match pair {
                    UnityValue::Array(p) if p.len() == 2 => Some((p[0].as_i64()?, p[1].as_str()?.to_string())),
                    UnityValue::Object(p) => Some((p.get("first")?.as_i64()?, p.get("second")?.as_str()?.to_string())),
                    _ => None,
                }).collect(),
                _ => HashMap::new(),
            };
            for (state_index, state) in states.iter().enumerate() {
                let state = unwrap_data(state);
                let name = integer(state, "m_FullPathID").and_then(|id| tos.get(&id));
                let Some(name) = name else { *skipped.entry("missing full state path").or_default() += 1; continue; };
                match state_entry(state, ctrl, &clips, &clip_assets,0) {
                    Ok((clip, timing, motion)) => {
                        let mut parameters=match motion.as_ref().map(|m|motion_parameters(m,ctrl,&tos)).transpose() {
                            Ok(p)=>p.unwrap_or_default(),Err(reason)=>{*skipped.entry(reason).or_default()+=1;continue;}
                        };
                        let speed_parameter=match speed_parameter(state,ctrl,&tos) {
                            Ok(p)=>p,Err(reason)=>{*skipped.entry(reason).or_default()+=1;continue;}
                        };
                        if let Some(p)=&speed_parameter {add_parameter(&mut parameters,p);}
                        let motion=if speed_parameter.is_some() {Some(motion.unwrap_or_else(||MotionNode::Clip{clip:clip.clone(),cycle:0.,duration_factor:1.}))} else {motion};
                        let float_defaults:Vec<_>=uv_get(ctrl,"m_Values").and_then(|v|array(v,"m_ValueArray")).unwrap_or(&[]).iter()
                            .filter(|p|integer(p,"m_Type")==Some(1)).filter_map(|p| {
                                let id=integer(p,"m_ID")?;let name=tos.get(&id)?.clone();let default=default_float(ctrl,id)?;
                                default.is_finite().then_some(FloatParameter{id:id as u32,name,default,min:0.,max:1.})
                            }).collect();
                        let float_names=float_defaults.iter().map(|p|p.name.clone()).collect();
                        entries.push(StateEntry {label: format!("{} / {name}", asset.name),clip,timing,motion,parameters,float_names,float_defaults,
                            short_name_id:integer(state,"m_NameID").unwrap_or(0) as u32,full_path_id:integer(state,"m_FullPathID").unwrap_or(0) as i32,
                            studio_layer:None,speed_parameter,layer:None,active_layers:Vec::new(),trigger_rules:trigger_rules(state,sm,ctrl,&tos,state_index),controller_id:id,state_index});
                    },
                    Err(reason) => *skipped.entry(reason).or_default() += 1,
                }
            }
            // Explicit layer preview starts the same controller's
            // authored base default and enables only the selected layer.
            let default_index=integer(sm,"m_DefaultState").and_then(|i|usize::try_from(i).ok());
            let base_entry=default_index.and_then(|index|entries.iter().find(|e|e.controller_id==id&&e.state_index==index&&e.layer.is_none()).cloned());
            let Some(base_entry)=base_entry else {continue;};
            for(layer_index,layer) in layers.iter().enumerate().skip(1) {
                let motion_set=integer(layer,"m_StateMachineMotionSetIndex").or_else(||integer(layer,"m_StateMachineSynchronizedLayerIndex")).unwrap_or(0);
                let mode=integer(layer,"(int&)m_LayerBlendingMode");
                let additive=mode==Some(1);
                if flag(layer,"m_IKPass")||!matches!(mode,Some(0|1))||motion_set<0||flag(layer,"m_SyncedLayerAffectsTiming")
                    ||(motion_set>0&&integer(layer,"m_StateMachineIndex")!=Some(sm_index)) {
                    *skipped.entry("unsupported explicit layer mode").or_default()+=1;continue;
                }
                let Some(mask_data)=uv_get(unwrap_data(layer),"m_SkeletonMask").and_then(|m|array(m,"m_Data")) else {
                    *skipped.entry("missing layer mask").or_default()+=1;continue;
                };
                let mask=mask_data.iter()
                    .map(|v|Some((integer(v,"m_PathHash")? as u32,number(v,"m_Weight")?)))
                    .collect::<Option<Vec<_>>>();
                let Some(mask)=mask.filter(|m|m.iter().all(|(_,w)|w.is_finite()&&(0.0..=1.0).contains(w))) else {
                    *skipped.entry("invalid layer mask").or_default()+=1;continue;
                };
                let layer_states=integer(layer,"m_StateMachineIndex").and_then(|i|array(ctrl,"m_StateMachineArray")?.get(i as usize))
                    .and_then(|m|array(m,"m_StateConstantArray")).unwrap_or(&[]);
                for(state_index,state) in layer_states.iter().enumerate() {
                    let state=unwrap_data(state);
                    let Some(name)=integer(state,"m_FullPathID").and_then(|h|tos.get(&h)) else {continue;};
                    match state_entry(state,ctrl,&clips,&clip_assets,motion_set as usize) {
                        Ok((clip,timing,motion))=>{
                            // Single-clip layers also need composition, rather
                            // than the unmasked raw-clip shortcut.
                            let motion=motion.unwrap_or_else(||MotionNode::Clip{clip:clip.clone(),cycle:0.,duration_factor:1.});
                            if additive&&!motion.valid_additive_reference() {
                                *skipped.entry("invalid additive reference").or_default()+=1;continue;
                            }
                            let mut parameters=match motion_parameters(&motion,ctrl,&tos) {
                                Ok(p)=>p,Err(reason)=>{*skipped.entry(reason).or_default()+=1;continue;}
                            };
                            let speed_parameter=match speed_parameter(state,ctrl,&tos) {
                                Ok(p)=>p,Err(reason)=>{*skipped.entry(reason).or_default()+=1;continue;}
                            };
                            if let Some(p)=&speed_parameter {add_parameter(&mut parameters,p);}
                            for p in &base_entry.parameters {
                                if let Some(existing)=parameters.iter_mut().find(|v|v.id==p.id) {existing.min=existing.min.min(p.min);existing.max=existing.max.max(p.max);}
                                else {parameters.push(p.clone());}
                            }
                            if base_entry.float_names.iter().any(|n|n==LAYER_WEIGHT_PARAMETER) || parameters.iter().any(|p|p.id==u32::MAX) {
                                *skipped.entry("reserved layer control collision").or_default()+=1;continue;
                            }
                            parameters.push(FloatParameter{id:u32::MAX,name:LAYER_WEIGHT_PARAMETER.into(),default:1.,min:0.,max:1.});
                            parameters.sort_by(|a,b|a.name.cmp(&b.name));
                            entries.push(StateEntry{label:format!("{} / {name}",asset.name),clip,timing,motion:Some(motion),parameters,
                                float_names:base_entry.float_names.clone(),float_defaults:base_entry.float_defaults.clone(),
                                short_name_id:integer(state,"m_NameID").unwrap_or(0) as u32,full_path_id:integer(state,"m_FullPathID").unwrap_or(0) as i32,studio_layer:None,
                                speed_parameter,layer:Some(OverrideLayer{index:layer_index,base_label:base_entry.label.clone(),base_state_index:base_entry.state_index,mask:mask.clone(),synchronized:motion_set>0,additive}),active_layers:Vec::new(),trigger_rules:integer(layer,"m_StateMachineIndex").and_then(|i|array(ctrl,"m_StateMachineArray")?.get(i as usize)).map(|m|trigger_rules(state,m,ctrl,&tos,state_index)).unwrap_or_default(),controller_id:id,state_index});
                        }
                        Err(reason)=>*skipped.entry(reason).or_default()+=1,
                    }
                }
            }
        }
    }
    entries.sort_by(|a, b| a.label.cmp(&b.label).then(a.clip.asset_file.cmp(&b.clip.asset_file))
        .then(a.controller_id.cmp(&b.controller_id)).then(a.state_index.cmp(&b.state_index)));
    let mut used: HashSet<String> = entries.iter().map(|e| e.label.clone()).collect();
    let mut seen = HashSet::new();
    for entry in &mut entries {
        if seen.insert(entry.label.clone()) { continue; }
        let base = entry.label.clone();
        let mut suffix = 2;
        loop {
            let label = format!("{base} ({suffix})");
            if used.insert(label.clone()) { entry.label = label; break; }
            suffix += 1;
        }
    }
    // Duplicate controller/state labels receive suffixes above. Resolve the
    // layer's base through file-scoped identity, never its pre-suffix label.
    let base_labels:HashMap<_,_>=entries.iter().filter(|e|e.layer.is_none())
        .map(|e|((e.clip.asset_file.clone(),e.controller_id,e.state_index),e.label.clone())).collect();
    for entry in &mut entries {
        if let Some(layer)=&mut entry.layer {
            layer.base_label=base_labels[&(entry.clip.asset_file.clone(),entry.controller_id,layer.base_state_index)].clone();
        }
    }
    entries.sort_by(|a, b| a.label.cmp(&b.label));
    eprintln!("ok-extract: controller states {} supported={} skipped={skipped:?}", source.key(), entries.len());
    entries
}

static CATALOGUES: OnceLock<Mutex<HashMap<String, Arc<Vec<StateEntry>>>>> = OnceLock::new();

pub(super) fn entries(source: &ClipBundleSource) -> Arc<Vec<StateEntry>> {
    let key = source.key();
    let cache = CATALOGUES.get_or_init(|| Mutex::new(HashMap::new()));
    if let Ok(guard) = cache.lock() {
        if let Some(entries) = guard.get(&key) { return entries.clone(); }
    }
    // The parsed graph is kept on disk too: parsing a controller bundle takes seconds, reading it back milliseconds.
    let root = source.resource_root();
    let disk = source.disk_key();
    let entries = Arc::new(match root.as_deref().and_then(|root| super::disk_cache::load::<Vec<StateEntry>>(root, "controller", &disk)) {
        Some(entries) => entries,
        None => {
            let entries = match source {
                ClipBundleSource::Studio{base,owner,overlay,studio_profile} => studio_entries(base,owner,overlay.as_ref(),*studio_profile),
                ClipBundleSource::Layers{controller,base_label,selected}=>multi_layer_entries(controller,base_label,selected),
                _ => entries_in_source(source,None),
            };
            if let Some(root) = root.as_deref() { if !entries.is_empty() { super::disk_cache::store(root, "controller", &disk, &entries); } }
            entries
        }
    });
    if let Ok(mut guard) = cache.lock() { guard.insert(key, entries.clone()); }
    entries
}

// An explicitly selected controller does not need the graphs of every other
// controller stored in the same file. Preserve file/owner identity in the cache.
fn owner_entries(source:&ClipBundleSource,owner:&str)->Arc<Vec<StateEntry>> {
    let base_key=source.key();let key=format!("owner:{}",serde_json::to_string(&(&base_key,owner)).unwrap());
    let cache=CATALOGUES.get_or_init(||Mutex::new(HashMap::new()));
    if let Ok(guard)=cache.lock(){
        if let Some(all)=guard.get(&base_key){return all.clone();}
        if let Some(graph)=guard.get(&key){return graph.clone();}
    }
    let graph=Arc::new(entries_in_source(source,Some(owner)));
    if let Ok(mut guard)=cache.lock(){return guard.entry(key).or_insert_with(||graph.clone()).clone();}
    graph
}

// RuntimeAnimatorController.animationClips, in source array order. The game
// deliberately assigns by clip name in Utils.Animator.SetupAnimatorOverrideController;
// this differs from an authored AnimatorOverrideController's PPtr mapping.
fn controller_clips(source:&ClipBundleSource,owner:&str)->Option<Vec<ClipEntry>> {
    let bundle=source.load()?;let all=cached_bundle_entries(source);let mut found=None;
    for file in &bundle.files {
        let ser=parse_serialized_file(bundle.extract_file_data(file).ok()?).ok()?;
        let mut assets=HashMap::new();
        for info in &ser.objects {
            if !matches!(info.type_id,91|221) {continue;}
            let obj=UnityObject::from_serialized_file(&ser,info).ok()?;
            assets.insert(info.path_id,ControllerAsset{name:obj.name().unwrap_or_default(),override_controller:info.type_id==221,
                props:UnityValue::Object(obj.class.properties().clone())});
        }
        for (&id,a) in assets.iter().filter(|(_,a)|a.name==owner) {
            if found.is_some() {return None;}
            let _=a;let (_,ids)=resolve_controller(&assets,id).ok()?;let mut clips=Vec::new();let mut seen=HashSet::new();
            for id in ids {let id=id?;if !seen.insert(id) {continue;}
                let mut clip=all.iter().find(|c|c.asset_file==file.name&&c.path_id==id)?.clone();
                // Runtime overlays may place a non-additive source clip on an
                // additive target layer; validate its actual reference here.
                clip.additive_reference_valid=clip_reference_is_valid(&ser,id);
                clips.push(clip);
            }
            found=Some(clips);
        }
    }
    found
}

fn studio_entries(base:&ClipBundleSource,owner:&str,overlay:Option<&(Box<ClipBundleSource>,String,String)>,studio_profile:bool)->Vec<StateEntry> {
    let all=owner_entries(base,owner);
    let mut graph:Vec<_>=all.iter().filter(|e|e.label.split_once(" / ").is_some_and(|(n,_)|n==owner)).cloned().collect();
    let identities:HashSet<_>=graph.iter().map(|e|(e.clip.asset_file.clone(),e.controller_id)).collect();
    if identities.len()!=1 {return Vec::new();}
    if let Some((source,over_owner,runtime_source))=overlay {
        let Some(clips)=controller_clips(source,over_owner) else {return Vec::new();};
        let Some(originals)=controller_clips(base,owner) else {return Vec::new();};
        // AnimatorOverrideController's string indexer addresses the first
        // original clip with that name. Other same-name PPtrs (e.g. breast
        // motion sets) must keep their original clips.
        let mut first=HashMap::new();
        for clip in originals {first.entry(clip.name).or_insert((clip.asset_file,clip.path_id));}
        let mut replacements=HashMap::new();
        for mut clip in clips {if let Some(original)=first.get(&clip.name) {
            clip.source=Some(runtime_source.clone());replacements.insert(original.clone(),clip);
        }}
        fn replace(m:&mut MotionNode,clips:&HashMap<(String,i64),ClipEntry>) {
            match m {
                MotionNode::Clip{clip,..}=>if let Some(over)=clips.get(&(clip.asset_file.clone(),clip.path_id)) {*clip=over.clone();},
                MotionNode::OneD{children,..}|MotionNode::Cartesian{children,..}=>for child in children {replace(child,clips);},
            }
        }
        for e in &mut graph {
            if let Some(m)=&mut e.motion {replace(m,&replacements);}
            if let Some(over)=replacements.get(&(e.clip.asset_file.clone(),e.clip.path_id)) {e.clip=over.clone();}
        }
    }
    // A runtime replacement brings its own reference metadata. Revalidate
    // after PPtr replacement so unsupported clips never become public states.
    graph.retain(|e|!e.layer.as_ref().is_some_and(|l|l.additive)||e.motion.as_ref().is_some_and(MotionNode::valid_additive_reference));
    let mut result=Vec::new();
    let context=if studio_profile {"Studio"} else {"Controller"};
    for base_state in graph.iter().filter(|e|e.layer.is_none()) {
        let (_,path)=base_state.label.split_once(" / ").unwrap();
        let base_label=format!("{owner} / [{context} layer 0] {path}");
        let mut entry=base_state.clone();entry.label=base_label.clone();entry.studio_layer=Some(0);
        add_studio_controls(&mut entry);result.push(entry);
        let mut by_layer=HashMap::<usize,Vec<&StateEntry>>::new();
        for e in graph.iter().filter(|e|e.layer.as_ref().is_some_and(|l|l.additive)||e.layer.is_some()&&e.short_name_id==base_state.short_name_id) {
            by_layer.entry(e.layer.as_ref().unwrap().index).or_default().push(e);
        }
        for (index,matches) in by_layer {
            if matches.len()!=1&&!matches.iter().all(|e|e.layer.as_ref().unwrap().additive) {continue;}
            for candidate in matches {
                let mut entry=candidate.clone();
                let addon=if entry.layer.as_ref().unwrap().additive {format!(" + {}",entry.label.split_once(" / ").unwrap().1)}else{String::new()};
                entry.label=format!("{owner} / [{context} layer {index}] {path}{addon}");entry.studio_layer=Some(index);
                let layer=entry.layer.as_mut().unwrap();layer.base_label=base_label.clone();layer.base_state_index=base_state.state_index;
                for p in &base_state.parameters {add_parameter(&mut entry.parameters,p);}
                add_studio_controls(&mut entry);result.push(entry);
            }
        }
    }
    result.sort_by(|a,b|a.label.cmp(&b.label));
    eprintln!("ok-extract: Studio graph base={} owner={owner} overlay={} supported={}",base.key(),overlay.map(|(_,n,_)|n.as_str()).unwrap_or("none"),result.len());
    result
}

fn add_studio_controls(entry:&mut StateEntry) {
    for names in [&["height1","height"][..],&["Breast1","Breast","breast"][..],&["motion"][..]] {
        if let Some(p)=names.iter().find_map(|n|entry.float_defaults.iter().find(|p|p.name==*n)) {
            add_parameter(&mut entry.parameters,p);
        }
    }
    // The common evaluator is also required for a single Studio clip whose
    // parameter list may include controller-wide options.
    if entry.motion.is_none() {entry.motion=Some(MotionNode::Clip{clip:entry.clip.clone(),cycle:0.,duration_factor:1.});}
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    #[ignore="installed shared bundle selective-owner graph equivalence"]
    fn selective_owner_graph_matches_full_catalogue() {
        let root=std::env::var("OK_TEST_GAME_ROOT").unwrap();let source=source_path(Path::new(&root),"studio/anime/00.unity3d").unwrap();
        let _scope=AnimationPreparationScope::new();
        let selected=entries_in_source(&source,Some("action_19"));assert_eq!(selected.len(),11);
        let all=entries_in_source(&source,None);
        let expected:Vec<_>=all.into_iter().filter(|e|e.label.starts_with("action_19 / ")).collect();
        assert_eq!(format!("{selected:?}"),format!("{expected:?}"));
    }

    fn object(fields: Vec<(&str, UnityValue)>) -> UnityValue {
        UnityValue::Object(fields.into_iter().map(|(k, v)| (k.to_string(), v)).collect())
    }

    fn pointer(file: i64, path: i64) -> UnityValue {
        object(vec![("m_FileID", UnityValue::Integer(file)), ("m_PathID", UnityValue::Integer(path))])
    }

    #[test]
    fn overrides_use_original_pointer_identity_and_preserve_null_and_external_indices() {
        let mut assets = HashMap::new();
        assets.insert(1, ControllerAsset { name: "base".into(), override_controller: false,
            props: object(vec![("m_AnimationClips", UnityValue::Array(vec![
                pointer(0, 11), pointer(1, 13), pointer(0, 12),
            ]))]) });
        assets.insert(2, ControllerAsset { name: "override".into(), override_controller: true,
            props: object(vec![("m_Controller", pointer(0, 1)), ("m_Clips", UnityValue::Array(vec![
                object(vec![("m_OriginalClip", pointer(0, 11)), ("m_OverrideClip", pointer(0, 21))]),
                object(vec![("m_OriginalClip", pointer(0, 12)), ("m_OverrideClip", pointer(0, 0))]),
            ]))]) });
        let (base, clips) = resolve_controller(&assets, 2).unwrap();
        assert_eq!(base, 1);
        assert_eq!(clips, vec![Some(21), None, Some(12)]);
        assert_eq!(resolve_controller(&assets, 1).unwrap().1, vec![Some(11), None, Some(12)],
            "an override must not mutate its base controller");
        assets.get_mut(&2).unwrap().props = object(vec![("m_Controller", pointer(2, 1))]);
        assert_eq!(resolve_controller(&assets, 2).unwrap_err(), "external/missing base controller");
    }

    #[test]
    fn state_clock_uses_speed_offset_and_loop_or_end_hold() {
        let mut clock = StateTiming { speed: 0.5, cycle: 0.25, looping: true };
        assert!((clock.clip_time(0.5, 2.0) - 0.75).abs() < 1e-6);
        assert!((clock.clip_time(3.5, 2.0) - 0.25).abs() < 1e-6);
        assert!((clock.clip_time(-1.5, 2.0) - 1.75).abs() < 1e-6);
        clock.looping = false;
        assert_eq!(clock.clip_time(8.0, 2.0), 2.0);
        assert_eq!(clock.clip_time(-8.0, 2.0), 0.0);
    }

    #[test]
    fn scaled_single_motion_preserves_offsets_and_rejects_invalid_duration() {
        let clip=ClipEntry{name:"source".into(),label:"source".into(),asset_file:"f".into(),path_id:1,owners:vec![],source:None,additive_reference_valid:true};
        let assets=HashMap::from([(1,clip)]);
        let state=|duration:f64|object(vec![
            ("m_BlendTreeConstantIndexArray",UnityValue::Array(vec![UnityValue::Integer(0)])),
            ("m_BlendTreeConstantArray",UnityValue::Array(vec![object(vec![("m_NodeArray",UnityValue::Array(vec![object(vec![
                ("m_ChildIndices",UnityValue::Array(vec![])),("m_ClipID",UnityValue::Integer(0)),
                ("m_Duration",UnityValue::Float(duration)),("m_CycleOffset",UnityValue::Float(0.2)),
            ])]))])])),("m_Speed",UnityValue::Float(1.5)),("m_CycleOffset",UnityValue::Float(0.1)),
        ]);
        let ctrl=object(vec![]);
        let (_,timing,motion)=state_entry(&state(0.5),&ctrl,&[Some(1)],&assets,0).unwrap();
        assert!((timing.cycle-0.1).abs()<1e-6);
        let mut leaves=Vec::new();motion.unwrap().leaves(&HashMap::new(),1.,&mut leaves);
        assert_eq!(leaves.len(),1);assert!((leaves[0].2-0.2).abs()<1e-6);assert_eq!(leaves[0].3,0.5);
        let (_,timing,motion)=state_entry(&state(1.),&ctrl,&[Some(1)],&assets,0).unwrap();
        assert!(motion.is_none());assert!((timing.cycle-0.3).abs()<1e-6);
        for invalid in [0.,-1.,f64::NAN,f64::INFINITY] {
            assert_eq!(state_entry(&state(invalid),&ctrl,&[Some(1)],&assets,0).unwrap_err(),"invalid/nonpositive motion duration");
        }
    }

    #[test]
    fn controller_composition_preserves_source_scene() {
        for (path,expected) in [("C:/game/abdata/h/anim/female/a.unity3d",ClipScene::AdultOrEsthetic),
            ("C:/game/abdata/action/animator/a.unity3d",ClipScene::Normal)] {
            let graph=|studio_profile|ClipBundleSource::Studio{base:Box::new(ClipBundleSource::File(PathBuf::from(path))),owner:"owner".into(),overlay:None,studio_profile};
            assert_eq!(scene_for_source(&graph(false)),expected);
            assert_eq!(scene_for_source(&graph(true)),ClipScene::Studio);
            assert_ne!(graph(false).key(),graph(true).key(),"different scene profiles must not share graph cache");
        }
    }
}

pub(super) fn layer_weight_name(index:usize)->String {format!("{LAYER_WEIGHT_PARAMETER}.{index}")}
pub(crate) fn layer_start_time_name(index:usize)->String {format!("OK.LayerStartTime.{index}")}
pub(crate) fn layer_start_phase_name(index:usize)->String {format!("OK.LayerStartPhase.{index}")}

fn multi_layer_entries(source:&ClipBundleSource,base_label:&str,selected:&[String])->Vec<StateEntry> {
    let graph=entries(source);
    let Some(base)=graph.iter().find(|e|e.label==base_label&&e.layer.is_none()) else {return Vec::new();};
    let mut result=base.clone();let mut indices=HashSet::new();
    for label in selected {
        let Some(entry)=graph.iter().find(|e|e.label==*label) else {return Vec::new();};
        let Some(layer)=&entry.layer else {return Vec::new();};
        if layer.base_label!=base_label||layer.index==0||layer.index>=u32::MAX as usize||!indices.insert(layer.index) {return Vec::new();}
        for p in entry.parameters.iter().filter(|p|p.name!=LAYER_WEIGHT_PARAMETER) {add_parameter(&mut result.parameters,p);}
        result.active_layers.push(entry.clone());
    }
    // Animator evaluates layer indices, independent of descriptor input order.
    result.active_layers.sort_by_key(|e|e.layer.as_ref().unwrap().index);
    // Allocate runtime IDs outside every authored parameter, in canonical
    // layer order. Fixed arithmetic lanes can collide with another layer's
    // control or an authored Unity hash.
    let mut used:HashSet<_>=base.float_defaults.iter().map(|p|p.id).chain(result.parameters.iter().map(|p|p.id)).collect();
    let mut next=u32::MAX;
    for e in &result.active_layers {
        let layer=e.layer.as_ref().unwrap();
        if [layer_weight_name(layer.index),layer_start_time_name(layer.index),layer_start_phase_name(layer.index)]
            .iter().any(|name|base.float_defaults.iter().any(|p|p.name==*name)) {return Vec::new();}
        let mut controls=vec![(layer_weight_name(layer.index),1.,0.,1.)];
        if !layer.synchronized {
            controls.push((layer_start_time_name(layer.index),0.,-10.,10.));
            controls.push((layer_start_phase_name(layer.index),0.,0.,1.));
        }
        for(name,default,min,max) in controls {
            if base.float_defaults.iter().any(|p|p.name==name) {return Vec::new();}
            while used.contains(&next) {let Some(n)=next.checked_sub(1) else {return Vec::new();};next=n;}
            let id=next;used.insert(id);
            add_parameter(&mut result.parameters,&FloatParameter{id,name,default,min,max});
        }
    }
    vec![result]
}
