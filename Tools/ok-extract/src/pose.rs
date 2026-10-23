use unity_asset_binary::asset::SerializedFile;
use unity_asset_core::UnityValue;
use unity_asset_decode::object::UnityObject;

use crate::unity::ExtractedMesh;

pub type Mat4 = [[f32; 4]; 4];

pub fn ident() -> Mat4 {
    [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]
}

fn mat_mul(a: &Mat4, b: &Mat4) -> Mat4 {
    let mut o = ident();
    for i in 0..4 {
        for j in 0..4 {
            o[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    o
}

pub(crate) fn mat_vec(m: &Mat4, p: [f32; 3]) -> [f32; 3] {
    [
        m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2] + m[0][3],
        m[1][0] * p[0] + m[1][1] * p[1] + m[1][2] * p[2] + m[1][3],
        m[2][0] * p[0] + m[2][1] * p[1] + m[2][2] * p[2] + m[2][3],
    ]
}

pub(crate) fn quat_mul(a: [f32; 4], b: [f32; 4]) -> [f32; 4] {
    [
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
    ]
}

pub(crate) fn quat_axis_angle(axis: [f32; 3], rad: f32) -> [f32; 4] {
    let n = (axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]).sqrt().max(1e-8);
    let s = (rad * 0.5).sin();
    [axis[0] / n * s, axis[1] / n * s, axis[2] / n * s, (rad * 0.5).cos()]
}

pub(crate) fn trs(pos: [f32; 3], rot: [f32; 4], scale: [f32; 3]) -> Mat4 {
    let (x, y, z, w) = (rot[0], rot[1], rot[2], rot[3]);
    let (x2, y2, z2) = (x + x, y + y, z + z);
    let (xx, yy, zz) = (x * x2, y * y2, z * z2);
    let (xy, xz, yz) = (x * y2, x * z2, y * z2);
    let (wx, wy, wz) = (w * x2, w * y2, w * z2);
    let (sx, sy, sz) = (scale[0], scale[1], scale[2]);
    [
        [(1.0 - (yy + zz)) * sx, (xy - wz) * sy, (xz + wy) * sz, pos[0]],
        [(xy + wz) * sx, (1.0 - (xx + zz)) * sy, (yz - wx) * sz, pos[1]],
        [(xz - wy) * sx, (yz + wx) * sy, (1.0 - (xx + yy)) * sz, pos[2]],
        [0.0, 0.0, 0.0, 1.0],
    ]
}

fn vf(v: &UnityValue) -> f32 {
    v.as_f64().unwrap_or(0.0) as f32
}

fn vec3(v: &UnityValue) -> [f32; 3] {
    match v {
        UnityValue::Object(o) => [
            o.get("x").map(vf).unwrap_or(0.0),
            o.get("y").map(vf).unwrap_or(0.0),
            o.get("z").map(vf).unwrap_or(0.0),
        ],
        UnityValue::Array(a) if a.len() >= 3 => [vf(&a[0]), vf(&a[1]), vf(&a[2])],
        _ => [0.0, 0.0, 0.0],
    }
}

fn quat(v: &UnityValue) -> [f32; 4] {
    match v {
        UnityValue::Object(o) => [
            o.get("x").map(vf).unwrap_or(0.0),
            o.get("y").map(vf).unwrap_or(0.0),
            o.get("z").map(vf).unwrap_or(0.0),
            o.get("w").map(vf).unwrap_or(1.0),
        ],
        UnityValue::Array(a) if a.len() >= 4 => [vf(&a[0]), vf(&a[1]), vf(&a[2]), vf(&a[3])],
        _ => [0.0, 0.0, 0.0, 1.0],
    }
}

fn pptr(v: &UnityValue) -> Option<i64> {
    match v {
        UnityValue::Object(o) => o
            .get("m_PathID")
            .or_else(|| o.get("m_PathId"))
            .and_then(|x| x.as_i64()),
        UnityValue::Integer(i) => Some(*i),
        _ => None,
    }
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct Tf {
    pub name: String,
    pub parent: Option<i64>,
    /// Unity Transform sibling index from the parent's serialized m_Children.
    /// None is reserved for synthetic/legacy fixtures without hierarchy data.
    #[serde(default)]
    pub sibling_index: Option<u32>,
    pub pos: [f32; 3],
    pub rot: [f32; 4],
    pub scale: [f32; 3],
}

fn mat_inv(m: &Mat4) -> Option<Mat4> {
    let mut a = *m;
    let mut inv = ident();
    for i in 0..4 {
        let mut piv = i;
        let mut best = a[i][i].abs();
        for r in i + 1..4 {
            if a[r][i].abs() > best {
                best = a[r][i].abs();
                piv = r;
            }
        }
        if best < 1e-8 {
            return None;
        }
        if piv != i {
            a.swap(i, piv);
            inv.swap(i, piv);
        }
        let d = a[i][i];
        for c in 0..4 {
            a[i][c] /= d;
            inv[i][c] /= d;
        }
        for r in 0..4 {
            if r == i {
                continue;
            }
            let f = a[r][i];
            for c in 0..4 {
                a[r][c] -= f * a[i][c];
                inv[r][c] -= f * inv[i][c];
            }
        }
    }
    Some(inv)
}

#[derive(Clone, serde::Serialize, serde::Deserialize)]
pub struct Skeleton {
    pub names: Vec<String>,
    pub worlds: Vec<Mat4>,
    pub rest: Vec<Mat4>,
    /// Bones that belong to the SkinnedMeshRenderer (bind-pose aligned).
    pub smr_count: usize,
    #[serde(serialize_with="crate::ordered::map")]
    pub tfs: std::collections::HashMap<i64, Tf>,
    pub bone_ids: Vec<i64>,
    /// Mesh path id this skeleton was built from (bind order source).
    pub source_mesh: i64,
}

impl std::fmt::Debug for Skeleton {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Skeleton")
            .field("names", &self.names)
            .field("smr_count", &self.smr_count)
            .field("source_mesh", &self.source_mesh)
            .finish()
    }
}

// Thread-owned value snapshots for layered animation. Duplicate-name lookup
// observes HashMap iteration order, so reuse requires identical ID order too.
thread_local! {static POSE_SNAPSHOTS:std::cell::RefCell<Vec<Skeleton>>=const{std::cell::RefCell::new(Vec::new())};}
pub(crate) struct SkeletonSnapshot(Option<Skeleton>);
impl SkeletonSnapshot {
    pub(crate) fn new(source:&Skeleton)->Self {
        let saved=POSE_SNAPSHOTS.with(|pool|{let mut pool=pool.borrow_mut();pool.iter().rposition(|s|s.tfs.len()==source.tfs.len()&&s.tfs.keys().zip(source.tfs.keys()).all(|(a,b)|a==b)).map(|i|pool.swap_remove(i))});
        let mut saved=saved.unwrap_or_else(||source.clone());saved.restore_from(source);Self(Some(saved))
    }
}
impl std::ops::Deref for SkeletonSnapshot {type Target=Skeleton;fn deref(&self)->&Skeleton{self.0.as_ref().unwrap()}}
impl std::ops::DerefMut for SkeletonSnapshot {fn deref_mut(&mut self)->&mut Skeleton{self.0.as_mut().unwrap()}}
impl Drop for SkeletonSnapshot {fn drop(&mut self){if let Some(saved)=self.0.take(){if saved.tfs.len()<=4096{POSE_SNAPSHOTS.with(|pool|{let mut pool=pool.borrow_mut();if pool.len()<4{pool.push(saved);}});}}}}

struct RigNamePlan {
    source:i64,order:Vec<(i64,String)>,
    first:std::sync::Arc<std::collections::HashMap<String,i64>>,
    last:std::sync::Arc<std::collections::HashMap<String,i64>>,
}
thread_local!{static RIG_NAMES:std::cell::RefCell<Vec<RigNamePlan>>=const{std::cell::RefCell::new(Vec::new())};}
pub(crate) fn rig_name_index(skel:&Skeleton,last:bool)->std::sync::Arc<std::collections::HashMap<String,i64>>{
    RIG_NAMES.with(|cache|{let mut cache=cache.borrow_mut();
        let found=cache.iter().position(|p|p.source==skel.source_mesh&&p.order.len()==skel.tfs.len()&&p.order.iter().zip(skel.tfs.iter()).all(|((id,name),(actual,tf))|id==actual&&name==&tf.name));
        let plan=if let Some(index)=found{cache.remove(index)}else{
            let mut first=std::collections::HashMap::new();let mut tail=std::collections::HashMap::new();
            for(id,tf)in &skel.tfs{first.entry(tf.name.clone()).or_insert(*id);tail.insert(tf.name.clone(),*id);}
            RigNamePlan{source:skel.source_mesh,order:skel.tfs.iter().map(|(id,tf)|(*id,tf.name.clone())).collect(),first:std::sync::Arc::new(first),last:std::sync::Arc::new(tail)}
        };
        let result=if last{plan.last.clone()}else{plan.first.clone()};if cache.len()>=32{cache.remove(0);}cache.push(plan);result
    })
}

impl Skeleton {
    /// Restore an exact baseline without discarding fixed-topology allocations.
    /// Runtime rigs may add/remove transforms; those retain full clone semantics.
    pub(crate) fn restore_from(&mut self,source:&Self) {
        let ordered=self.tfs.len()==source.tfs.len()&&self.tfs.keys().zip(source.tfs.keys()).all(|(a,b)|a==b);
        if !ordered&&(self.tfs.len()!=source.tfs.len()||source.tfs.keys().any(|id|!self.tfs.contains_key(id))){*self=source.clone();return;}
        self.names.clone_from(&source.names);self.worlds.clone_from(&source.worlds);self.rest.clone_from(&source.rest);
        self.bone_ids.clone_from(&source.bone_ids);self.smr_count=source.smr_count;self.source_mesh=source.source_mesh;
        let restore=|target:&mut Tf,tf:&Tf|{target.name.clone_from(&tf.name);target.parent=tf.parent;target.sibling_index=tf.sibling_index;target.pos=tf.pos;target.rot=tf.rot;target.scale=tf.scale;};
        if ordered {for((_,target),(_,tf))in self.tfs.iter_mut().zip(&source.tfs){restore(target,tf);}}
        else {for(id,tf)in &source.tfs {restore(self.tfs.get_mut(id).unwrap(),tf);}}
    }
    /// SetParent(false) keeps local TRS and appends to the new sibling list.
    /// Reparenting to the same parent leaves its sibling index unchanged.
    pub(crate) fn reparent(&mut self,id:i64,parent:Option<i64>,after:u32) {
        if self.tfs.get(&id).is_some_and(|tf|tf.parent==parent) {return;}
        let next=self.tfs.iter().filter(|(child,tf)|**child!=id&&tf.parent==parent)
            .map(|(_,tf)|tf.sibling_index.map_or(0,|index|index+1)).max().unwrap_or(0).max(after);
        if let Some(tf)=self.tfs.get_mut(&id) {tf.parent=parent;tf.sibling_index=Some(next);}
    }

    /// The body mesh SMR omits a few collider transforms carried by
    /// p_cf_body_bone. Add those nodes by name before applying ShapeBody, so
    /// the physics colliders inherit the same card morphs as the visible body.
    pub fn merge_missing_tree_transforms(&mut self, source: &Skeleton) -> usize {
        let mut by_name: std::collections::HashMap<String, i64> = self.tfs.iter()
            .map(|(id, tf)| (tf.name.clone(), *id)).collect();
        let mut pending: Vec<(i64, Tf)> = source.tfs.iter()
            .filter(|(_, tf)| !by_name.contains_key(&tf.name))
            .map(|(id, tf)| (*id, tf.clone())).collect();
        pending.sort_by(|a, b| a.1.name.cmp(&b.1.name));
        let mut added = 0;
        let mut synthetic_id = -1i64;
        while !pending.is_empty() {
            let before = pending.len();
            pending.retain(|(_, tf)| {
                if by_name.contains_key(&tf.name) { return false; }
                let parent = tf.parent.and_then(|id| source.tfs.get(&id))
                    .and_then(|parent| by_name.get(&parent.name).copied());
                if tf.parent.is_some() && parent.is_none() { return true; }
                while self.tfs.contains_key(&synthetic_id) { synthetic_id -= 1; }
                let id = synthetic_id;
                synthetic_id -= 1;
                let mut inserted = tf.clone();
                inserted.parent = parent;
                by_name.insert(inserted.name.clone(), id);
                self.tfs.insert(id, inserted);
                added += 1;
                false
            });
            if pending.len() == before { break; }
        }
        added
    }

    pub fn rebuild_worlds(&mut self) {
        // ExpressionBone rebuilds several times per frame. Keep the existing
        // name allocations; topology/order/name changes still replace values.
        self.names.resize_with(self.bone_ids.len(), String::new);
        self.worlds.resize(self.bone_ids.len(), ident());
        for (index, id) in self.bone_ids.iter().enumerate() {
            if let Some(tf) = self.tfs.get(id) { self.names[index].clone_from(&tf.name); }
            else { self.names[index].clear(); }
        }
        WORLD_PLANS.with(|plans| {
            let mut plans=plans.borrow_mut();
            let index=plans.iter().rposition(|p|p.matches(self));
            let mut plan=match index {Some(i)=>Some(plans.remove(i)),None=>WorldPlan::compile(self)};
            if let Some(ref mut plan)=plan {
                // The verified inventory order gives dense addresses without
                // retaining references across mutations or hashing each bone.
                for ((_,tf),node) in self.tfs.iter().zip(&plan.inputs){if let Some(i)=node{
                    let bits=[tf.pos[0].to_bits(),tf.pos[1].to_bits(),tf.pos[2].to_bits(),tf.rot[0].to_bits(),tf.rot[1].to_bits(),tf.rot[2].to_bits(),tf.rot[3].to_bits(),tf.scale[0].to_bits(),tf.scale[1].to_bits(),tf.scale[2].to_bits()];
                    plan.dirty[*i]=plan.local_inputs[*i]!=Some(bits);
                    if plan.dirty[*i]{plan.locals[*i]=trs(tf.pos,tf.rot,tf.scale);plan.local_inputs[*i]=Some(bits);}
                }}
                for i in 0..plan.nodes.len(){
                    let node=&plan.nodes[i];let local=plan.locals[i];
                    if let WorldParent::Node(parent)=node.parent {plan.dirty[i]|=plan.dirty[parent];}
                    if plan.dirty[i]{plan.matrices[i]=match node.parent {WorldParent::Local=>local,WorldParent::Missing=>mat_mul(&ident(),&local),WorldParent::Node(parent)=>mat_mul(&plan.matrices[parent],&local)};}
                }
                for (index,node)in plan.outputs.iter().enumerate(){self.worlds[index]=node.map_or(ident(),|i|plan.matrices[i]);}
            } else {
                let mut cache=std::collections::HashMap::with_capacity(self.tfs.len());
                for(index,id)in self.bone_ids.iter().enumerate(){self.worlds[index]=world_of(*id,&self.tfs,&mut cache);}
            }
            if let Some(plan)=plan {plans.push(plan);if plans.len()>128 {plans.remove(0);}}
        });
    }


    /// Reorder `bone_ids` to the given bone-name order (e.g. the runtime SMR
    /// bone order from the game) and recompute rest/worlds.
    pub fn set_bone_order(&mut self, names: &[String]) {
        let mut order: Vec<i64> = Vec::new();
        for n in names {
            if let Some(i) = self.names.iter().position(|x| x == n) {
                let id = self.bone_ids[i];
                if !order.contains(&id) {
                    order.push(id);
                }
                continue;
            }
            // HashMap iter order is not stable; pick the lowest path_id when
            // a prefab reuses a bone name (should be rare).
            let mut hits: Vec<i64> = self
                .tfs
                .iter()
                .filter(|(_, t)| &t.name == n)
                .map(|(id, _)| *id)
                .collect();
            hits.sort();
            if let Some(id) = hits.first() {
                if !order.contains(id) {
                    order.push(*id);
                }
            }
        }
        if order.len() * 2 < names.len() {
            return;
        }
        let mut cache = std::collections::HashMap::new();
        let rest: Vec<Mat4> = order
            .iter()
            .map(|id| world_of(*id, &self.tfs, &mut cache))
            .collect();
        self.bone_ids = order;
        self.rest = rest;
        self.rebuild_worlds();
    }

    /// StudioEditor `RebindSkinning` binds every head SMR against the full
    /// `p_cf_head_bone` name map, not just the face SMR's 45 bones. Append any
    /// remaining transforms so iris `cf_J_hitomi_tx_*` (and sclera extras)
    /// resolve by name instead of falling back to a parent with a different
    /// rest/bind. Original SMR indices stay at the front (`smr_count` unchanged).
    pub fn include_tree_bones(&mut self) {
        let have: std::collections::HashSet<i64> = self.bone_ids.iter().copied().collect();
        let mut extra: Vec<(String, i64)> = self
            .tfs
            .iter()
            .filter(|(id, t)| !have.contains(id) && !t.name.is_empty())
            .map(|(id, t)| (t.name.clone(), *id))
            .collect();
        extra.sort();
        if extra.is_empty() {
            return;
        }
        for (_, id) in extra {
            self.bone_ids.push(id);
        }
        let mut cache = std::collections::HashMap::new();
        self.rest = self
            .bone_ids
            .iter()
            .map(|id| world_of(*id, &self.tfs, &mut cache))
            .collect();
        self.rebuild_worlds();
    }

    /// Copy transforms this skeleton does not already name, then append only
    /// those new names. Parents go in before children. Existing indices stay
    /// at the front, so a body mesh already weighted to the old list is not
    /// reshuffled. The male body renderer omits the penis chain.
    /// Lowest transform id with this name (deterministic among duplicates).
    fn tf_id_named(&self, name: &str) -> Option<i64> {
        self.tfs.iter().filter(|(_, tf)| tf.name.eq_ignore_ascii_case(name)).map(|(id, _)| *id).min()
    }

    pub fn append_missing_tree_bones(&mut self, source: &Skeleton) -> usize {
        self.merge_missing_tree_transforms(source);
        let mut present: std::collections::HashSet<String> =
            self.names.iter().map(|name| name.to_ascii_lowercase()).collect();
        let mut pending: Vec<String> = Vec::new();
        // Deterministic order (transform id), never HashMap iteration order:
        // the appended indices are part of the skinned result.
        let mut source_ids: Vec<i64> = source.tfs.keys().copied().collect();
        source_ids.sort_unstable();
        for tf in source_ids.iter().filter_map(|id| source.tfs.get(id)) {
            if tf.name.is_empty() || present.contains(&tf.name.to_ascii_lowercase()) {
                continue;
            }
            if pending.iter().any(|name| name.eq_ignore_ascii_case(&tf.name)) {
                continue;
            }
            pending.push(tf.name.clone());
        }
        let before = self.bone_ids.len();
        for _ in 0..64 {
            if pending.is_empty() {
                break;
            }
            let batch = std::mem::take(&mut pending);
            let mut progressed = false;
            for name in batch {
                let ready = match self.tf_id_named(&name)
                    .and_then(|id| self.tfs.get(&id))
                    .and_then(|tf| tf.parent)
                {
                    None => true,
                    Some(parent) => self
                        .tfs
                        .get(&parent)
                        .map(|tf| present.contains(&tf.name.to_ascii_lowercase()))
                        .unwrap_or(false),
                };
                if !ready {
                    pending.push(name);
                    continue;
                }
                progressed = true;
                if let Some(id) = self.tf_id_named(&name) {
                    if !self.bone_ids.contains(&id) {
                        self.bone_ids.push(id);
                        present.insert(name.to_ascii_lowercase());
                    }
                }
            }
            if !progressed {
                break;
            }
        }
        for name in pending {
            if let Some(id) = self.tf_id_named(&name) {
                if !self.bone_ids.contains(&id) {
                    self.bone_ids.push(id);
                }
            }
        }
        let added = self.bone_ids.len() - before;
        if added > 0 {
            let mut cache = std::collections::HashMap::new();
            self.rest = self
                .bone_ids
                .iter()
                .map(|id| world_of(*id, &self.tfs, &mut cache))
                .collect();
            self.rebuild_worlds();
        }
        added
    }

    pub fn apply_stand_pose(&mut self) {
        let ids: Vec<i64> = self.tfs.keys().copied().collect();
        let mut parent_worlds = std::collections::HashMap::new();
        for id in &ids {
            let _ = world_of(*id, &self.tfs, &mut parent_worlds);
        }
        for (id, tf) in self.tfs.iter_mut() {
            let Some(deg) = pose_deg_for(&tf.name) else {
                continue;
            };
            let parent_world = tf
                .parent
                .filter(|p| *p != 0 && *p != *id)
                .and_then(|p| parent_worlds.get(&p).copied());
            apply_world_z(tf, parent_world.as_ref(), deg);
        }
        self.rebuild_worlds();
    }

    pub fn apply_local(
        &mut self,
        name: &str,
        pos: [f32; 3],
        euler_deg: [f32; 3],
        scl: [f32; 3],
        set_pos: [bool; 3],
        set_rot: bool,
        set_scl: [bool; 3],
    ) {
        self.apply_local_in(name, pos, euler_deg, scl, set_pos, set_rot, set_scl, None);
    }

    /// InitShapeBody writes onto p_cf_body_bone only (ChaControl.objBodyBone).
    pub fn apply_local_in(
        &mut self,
        name: &str,
        pos: [f32; 3],
        euler_deg: [f32; 3],
        scl: [f32; 3],
        set_pos: [bool; 3],
        set_rot: bool,
        set_scl: [bool; 3],
        only: Option<&std::collections::HashSet<i64>>,
    ) {
        for (id, tf) in self.tfs.iter_mut() {
            if tf.name != name {
                continue;
            }
            if let Some(allow) = only {
                if !allow.contains(id) {
                    continue;
                }
            }
            if set_pos[0] {
                tf.pos[0] = pos[0];
            }
            if set_pos[1] {
                tf.pos[1] = pos[1];
            }
            if set_pos[2] {
                tf.pos[2] = pos[2];
            }
            if set_rot {
                tf.rot = euler_deg_to_quat(euler_deg);
            }
            if set_scl[0] {
                tf.scale[0] = scl[0];
            }
            if set_scl[1] {
                tf.scale[1] = scl[1];
            }
            if set_scl[2] {
                tf.scale[2] = scl[2];
            }
        }
    }
}

/// `ChaControl.CreateBodyBone` rewrites these reference nodes when
/// `hiPoly == false`, before `InitShapeBody` (ChaControl.cs:5414-5489).
/// Studio T-pose dumps for all three HANDOFF2 cards have this layout.
pub fn apply_low_poly_body_references(sk: &mut Skeleton) {
    let mut pos = |name: &str, value: [f32; 3], axes: [bool; 3]| {
        sk.apply_local(name, value, [0.0; 3], [1.0; 3], axes, false, [false; 3]);
    };
    pos("a_n_bust", [0.0, 0.14, 0.0], [false, true, false]);
    pos("a_n_bust_f", [0.0, 0.0, 0.04], [false, false, true]);
    pos("a_n_ind_L", [-0.0162, -0.0024, 0.0006], [true; 3]);
    pos("a_n_ind_R", [0.0162, 0.0024, -0.0006], [true; 3]);
    pos("a_n_mid_L", [-0.016, -0.0028, 0.0], [true; 3]);
    pos("a_n_mid_R", [0.016, 0.0028, 0.0], [true; 3]);
    pos("a_n_ring_L", [-0.018, -0.0018, -0.0005], [true; 3]);
    pos("a_n_ring_R", [0.018, 0.0018, 0.0005], [true; 3]);
    pos("a_n_nip_L", [0.0, 0.0, 0.05], [true; 3]);
    pos("a_n_nip_R", [0.0, 0.0, 0.05], [true; 3]);
    for (name, euler) in [
        ("a_n_back_L", [0.0, 180.0, 0.0]),
        ("a_n_back_R", [0.0, 180.0, 0.0]),
        ("a_n_bust_f", [-35.0, 0.0, 0.0]),
        ("a_n_ind_L", [0.0, 0.0, 0.0]),
        ("a_n_ind_R", [180.0, 0.0, 0.0]),
        ("a_n_mid_R", [180.0, 0.0, 0.0]),
        ("a_n_ring_R", [180.0, 0.0, 0.0]),
        ("a_n_nip_L", [0.0, 0.0, 0.0]),
        ("a_n_nip_R", [0.0, 0.0, 0.0]),
    ] {
        sk.apply_local(name, [0.0; 3], euler, [1.0; 3], [false; 3], true, [false; 3]);
    }
    sk.rebuild_worlds();
}

/// Inverse of `euler_deg_to_quat` (Unity's Z, X, Y order): Unity Euler degrees of a local rotation.
pub fn quat_to_unity_euler(q: [f32; 4]) -> [f32; 3] { quat_to_euler_deg(2, q) }

pub fn euler_deg_to_quat(e: [f32; 3]) -> [f32; 4] {
    let qx = quat_axis_angle([1.0, 0.0, 0.0], e[0].to_radians());
    let qy = quat_axis_angle([0.0, 1.0, 0.0], e[1].to_radians());
    let qz = quat_axis_angle([0.0, 0.0, 1.0], e[2].to_radians());
    quat_mul(qy, quat_mul(qx, qz))
}

/// Rotation-order codes for Illusion's ConvertRotation:
/// 0 xyz, 1 xzy, 2 yxz, 3 yzx, 4 zxy, 5 zyx.
fn euler_order_to_quat(order: u8, x: f32, y: f32, z: f32) -> [f32; 4] {
    let qx = quat_axis_angle([1.0, 0.0, 0.0], x.to_radians());
    let qy = quat_axis_angle([0.0, 1.0, 0.0], y.to_radians());
    let qz = quat_axis_angle([0.0, 0.0, 1.0], z.to_radians());
    match order {
        0 => quat_mul(qx, quat_mul(qy, qz)),
        1 => quat_mul(qx, quat_mul(qz, qy)),
        2 => quat_mul(qy, quat_mul(qx, qz)),
        3 => quat_mul(qy, quat_mul(qz, qx)),
        4 => quat_mul(qz, quat_mul(qx, qy)),
        _ => quat_mul(qz, quat_mul(qy, qx)),
    }
}

/// Port of ConvertRotation.ConvertDegreeFromQuaternion for the orders the
/// ExpressionBone correction uses (3 = yzx, 4 = zxy, 5 = zyx), plus Unity's
/// Quaternion.Euler order (2 = yxz) for SetLocalRotationX.
fn quat_to_euler_deg(order: u8, q: [f32; 4]) -> [f32; 3] {
    let m = trs([0.0, 0.0, 0.0], q, [1.0, 1.0, 1.0]);
    let g = |r: usize, c: usize| m[r][c];
    let (x, y, z) = match order {
        2 => {
            let sx = (-g(1, 2)).clamp(-1.0, 1.0);
            let x = sx.asin();
            let (y, z) = if sx.abs() > 0.9999 {
                ((-g(2, 0)).atan2(g(0, 0)), 0.0)
            } else {
                (g(0, 2).atan2(g(2, 2)), g(1, 0).atan2(g(1, 1)))
            };
            (x, y, z)
        }
        3 => {
            let m10 = g(1, 0);
            let z = m10.clamp(-1.0, 1.0).asin();
            let (x, y) = if m10 > 0.9999 {
                (0.0, g(0, 2).atan2(g(2, 2)))
            } else {
                ((-g(1, 2)).atan2(g(1, 1)), (-g(2, 0)).atan2(g(0, 0)))
            };
            (x, y, z)
        }
        4 => {
            let m21 = g(2, 1);
            let x = m21.clamp(-1.0, 1.0).asin();
            let (y, z) = if m21 > 0.9999 {
                (0.0, g(1, 0).atan2(g(0, 0)))
            } else {
                ((-g(2, 0)).atan2(g(2, 2)), (-g(0, 1)).atan2(g(1, 1)))
            };
            (x, y, z)
        }
        _ => {
            let num = -g(2, 0);
            let y = num.clamp(-1.0, 1.0).asin();
            let (x, z) = if num > 0.9999 {
                (0.0, (-g(0, 1)).atan2(g(1, 1)))
            } else {
                (g(2, 1).atan2(g(2, 2)), g(1, 0).atan2(g(0, 0)))
            };
            (x, y, z)
        }
    };
    [x.to_degrees(), y.to_degrees(), z.to_degrees()]
}

fn translation_mat(x: f32, y: f32, z: f32) -> Mat4 {
    let mut m = ident();
    m[0][3] = x;
    m[1][3] = y;
    m[2][3] = z;
    m
}

/// Port of the game's `ExpressionBone.LateUpdate` corrections for the arms.
/// The game's skinned deform bones `cf_d_*` are re-driven every frame from the
/// animated `cf_j_*` bones to stop the T-pose-bind shoulder/elbow collapse.
pub fn apply_expression_bone_arms(skel: &mut Skeleton) {
    apply_expression_bone_arms_impl(skel,true,false);
}
fn apply_expression_bone_arms_impl(skel:&mut Skeleton,rebuild:bool,complete_snapshot:bool){
    let name2id=rig_name_index(skel,false);
    let mut world: std::collections::HashMap<i64, Mat4> = std::collections::HashMap::new();
    let needed=["cf_j_shoulder_L","cf_j_shoulder_R","cf_j_forearm01_L","cf_j_forearm01_R"];
    let ids:Vec<_>=if complete_snapshot{skel.tfs.keys().copied().collect()}else{needed.iter().filter_map(|name|name2id.get(*name).copied()).collect()};
    for id in &ids {
        let matrix=world_of(*id,&skel.tfs,&mut world);world.insert(*id,matrix);
    }
    let get = |n: &str| name2id.get(n).copied();

    // ---- Shoulder L/R: re-place cf_d_shoulder02_* from the forearm goal ----
    for &(side, is_left) in &[("L", true), ("R", false)] {
        let arm = format!("cf_j_arm00_{side}");
        let dsh = format!("cf_d_shoulder02_{side}");
        let fore = format!("cf_j_forearm01_{side}");
        let sho = format!("cf_j_shoulder_{side}");
        let (Some(i_arm), Some(i_dsh), Some(i_fore), Some(i_sho)) =
            (get(&arm), get(&dsh), get(&fore), get(&sho))
        else {
            continue;
        };
        let goal = mat_mul(&world[&i_sho], &translation_mat(0.0, 0.3, 0.0));
        let fp = [
            world[&i_fore][0][3],
            world[&i_fore][1][3],
            world[&i_fore][2][3],
        ];
        let Some(ginv) = mat_inv(&goal) else { continue };
        let v = mat_vec(&ginv, fp);
        let t2pos = skel.tfs.get(&i_arm).map(|t| t.pos).unwrap_or([0.0; 3]);
        let t3rot = skel.tfs.get(&i_dsh).map(|t| t.rot).unwrap_or([0.0, 0.0, 0.0, 1.0]);
        if std::env::var("OK_EBDBG").is_ok() {
            eprintln!("ok-extract: EBDBG {side} v={v:?} t2pos={t2pos:?}");
        }
        let dy = v[1].abs();
        let dz = v[2].abs();
        let num3 = if dy <= 0.3 { (0.3 - dy) * 250.0 } else { 0.0 };
        if let Some(tf) = skel.tfs.get_mut(&i_dsh) {
            if is_left {
                if dy <= 0.3 {
                    tf.pos[0] = t2pos[0] - (0.3 - dy) * 0.03 - dz * 0.03;
                    tf.pos[1] = (0.3 - dy) * 0.2;
                } else {
                    tf.pos[0] = t2pos[0] - dz * 0.03;
                    tf.pos[1] = 0.0;
                }
                let mut e = quat_to_euler_deg(3, t3rot);
                e[1] = v[2] * 50.0;
                e[2] = (-num3).min(0.0);
                tf.rot = euler_order_to_quat(3, e[0], e[1], e[2]);
                tf.pos[2] = -v[2] * 0.06;
            } else {
                let num6 = if dy <= 0.3 { (0.3 - dy) * -250.0 } else { 0.0 };
                if dy <= 0.3 {
                    tf.pos[0] = t2pos[0] + (0.3 - dy) * 0.03 + dz * 0.03;
                    tf.pos[1] = (0.3 - dy) * 0.2;
                } else {
                    tf.pos[0] = t2pos[0] + dz * 0.03;
                    tf.pos[1] = 0.0;
                }
                let mut e = quat_to_euler_deg(3, t3rot);
                e[1] = -v[2] * 50.0;
                e[2] = 0.0f32.max(-num6);
                tf.rot = euler_order_to_quat(3, e[0], e[1], e[2]);
                tf.pos[2] = -v[2] * 0.06;
            }
        }
        // ---- Upperarm: twist cf_d_arm01/02/03_* from cf_j_arm00_* ----
        let arm_rot = skel.tfs.get(&i_arm).map(|t| t.rot).unwrap_or([0.0, 0.0, 0.0, 1.0]);
        let x = quat_to_euler_deg(5, arm_rot)[0];
        for (bone, coef) in [
            (format!("cf_d_arm01_{side}"), 0.6f32),
            (format!("cf_d_arm02_{side}"), 0.5),
            (format!("cf_d_arm03_{side}"), 0.125),
        ] {
            if let Some(id) = get(&bone) {
                let rot = skel.tfs.get(&id).map(|t| t.rot).unwrap_or([0.0, 0.0, 0.0, 1.0]);
                let mut e = quat_to_euler_deg(5, rot);
                e[0] = -x * coef;
                if let Some(tf) = skel.tfs.get_mut(&id) {
                    tf.rot = euler_order_to_quat(5, e[0], e[1], e[2]);
                }
            }
        }
    }
    if rebuild { skel.rebuild_worlds(); }
}

/// Snapshot the local TRS (baseline) of every named bone, before ABM applies.
/// `ChaControl.Update` applies these high-poly female arm/hand corrections
/// every frame when Character.enableCorrectArmSize/HandSize are enabled.
/// They precede ABMX's relative bone modifiers in the captured T-pose.
pub fn apply_hi_poly_limb_size_corrections(skel: &mut Skeleton) {
    apply_scene_limb_size_corrections(skel, true, true);
}

/// ChaControl.LateUpdateForce overwrites these RefObjKey transforms after the
/// Animator. The scene controls Character.enableCorrectArmSize/HandSize.
pub fn apply_scene_limb_size_corrections(skel: &mut Skeleton, correct_arm: bool, correct_hand: bool) {
    apply_scene_limb_size_corrections_locals(skel, correct_arm, correct_hand);
    skel.rebuild_worlds();
}

/// `apply_scene_limb_size_corrections` without publishing the world matrices.
pub fn apply_scene_limb_size_corrections_locals(skel: &mut Skeleton, correct_arm: bool, correct_hand: bool) {
    let arm_x = if correct_arm { 0.92 } else { 1.0 };
    let hand_yz = if correct_hand { 0.9 } else { 1.0 };
    for tf in skel.tfs.values_mut() {
        match tf.name.as_str() {
            "cf_j_forearm01_L" | "cf_j_forearm01_R" => tf.scale = [arm_x, 1.0, 1.0],
            "cf_j_hand_L" | "cf_j_hand_R" => tf.scale = [1.0, hand_yz, hand_yz],
            _ => {}
        }
    }
}

pub fn capture_baseline(
    skel: &Skeleton,
    names: &[String],
) -> std::collections::HashMap<i64, ([f32; 3], [f32; 4], [f32; 3])> {
    capture_baseline_names(skel,names.iter().map(String::as_str))
}
pub(crate) fn capture_abm_baseline(skel:&Skeleton,abm:&[crate::abmx::AbmBone])->std::collections::HashMap<i64,([f32;3],[f32;4],[f32;3])>{
    capture_baseline_names(skel,abm.iter().map(|b|b.name.as_str()))
}
fn capture_baseline_names<'a>(skel:&Skeleton,names:impl Iterator<Item=&'a str>)->std::collections::HashMap<i64,([f32;3],[f32;4],[f32;3])>{
    let mut out = std::collections::HashMap::new();
    let selected:std::collections::HashSet<&str>=names.collect();
    if selected.is_empty() {
        return out;
    }
    for (id, tf) in &skel.tfs {
        if selected.contains(tf.name.as_str()) {
            out.insert(*id, (tf.pos, tf.rot, tf.scale));
        }
    }
    out
}

/// Apply ABM/ABMX bone modifiers (port of `BoneModifier.Apply`):
///   scale = baseline * mod.scale
///   rot   = baselineRot * Euler(mod.rot)
///   pos   = baselinePos * mod.length + mod.pos
pub fn apply_abm(
    skel: &mut Skeleton,
    abm: &[crate::abmx::AbmBone],
    coord: usize,
    baseline: &std::collections::HashMap<i64, ([f32; 3], [f32; 4], [f32; 3])>,
) {
    if abm.is_empty() || baseline.is_empty() {
        return;
    }
    let name2id=rig_name_index(skel,false);
    let mut applied = 0usize;
    if std::env::var("OK_ATTACH").is_ok() || std::env::var("OK_DIAG").is_ok() {
        for b in abm {
            let d = b.for_coord(coord);
            eprintln!(
                "ok-extract: ABMENTRY name={} loc={} ncoord={} empty={} data={:?}",
                b.name, b.location, b.coord.len(), d.is_empty(), d
            );
        }
    }
    for b in abm {
        let d = b.for_coord(coord);
        if d.is_empty() {
            continue;
        }
        let Some(&id) = name2id.get(&b.name) else {
            continue;
        };
        let Some(&(bp, br, bs)) = baseline.get(&id) else {
            continue;
        };
        let rot = quat_mul(br, euler_deg_to_quat(d.rot));
        let pos = [
            bp[0] * d.length + d.pos[0],
            bp[1] * d.length + d.pos[1],
            bp[2] * d.length + d.pos[2],
        ];
        let scl = [bs[0] * d.scale[0], bs[1] * d.scale[1], bs[2] * d.scale[2]];
        if let Some(tf) = skel.tfs.get_mut(&id) {
            tf.pos = pos;
            tf.rot = rot;
            tf.scale = scl;
        }
        applied += 1;
    }
    skel.rebuild_worlds();
    if std::env::var("OK_SHAPE_DIAGNOSTICS").ok().as_deref()==Some("1") {eprintln!("ok-extract: ABM applied to {applied} bones");}
}

fn gt_pos(s: &Skeleton, id: i64) -> [f32; 3] {
    s.tfs.get(&id).map(|t| t.pos).unwrap_or([0.0; 3])
}
fn gt_rot(s: &Skeleton, id: i64) -> [f32; 4] {
    s.tfs.get(&id).map(|t| t.rot).unwrap_or([0.0, 0.0, 0.0, 1.0])
}
fn gt_scale(s: &Skeleton, id: i64) -> [f32; 3] {
    s.tfs.get(&id).map(|t| t.scale).unwrap_or([1.0; 3])
}
fn st_pos(s: &mut Skeleton, id: i64, p: [f32; 3]) {
    if let Some(t) = s.tfs.get_mut(&id) {
        t.pos = p;
    }
}
fn st_rot(s: &mut Skeleton, id: i64, r: [f32; 4]) {
    if let Some(t) = s.tfs.get_mut(&id) {
        t.rot = r;
    }
}
fn st_scale(s: &mut Skeleton, id: i64, sc: [f32; 3]) {
    if let Some(t) = s.tfs.get_mut(&id) {
        t.scale = sc;
    }
}

// Unity Transform.rotation composes quaternions through parents, even when
// nonuniform card/ABMX scale makes the world matrix unsuitable for extraction.
pub(crate) fn world_rotation_of(id: i64, tfs: &std::collections::HashMap<i64, Tf>) -> [f32; 4] {
    let mut chain = Vec::new();
    let mut current = Some(id);
    for _ in 0..128 {
        let Some(bone) = current else { break };
        let Some(tf) = tfs.get(&bone) else { break };
        chain.push(tf.rot);
        current = tf.parent.filter(|p| *p != 0 && *p != bone);
    }
    chain.into_iter().rev().fold([0.0, 0.0, 0.0, 1.0], quat_mul)
}

/// `ExpressionBone.LateUpdate` ElboL/R. Each deform bone is optional in the
/// game, and its local z offset uses sin((forearm_y - 1) * 0.006), not a
/// linearized angle multiplied by sin(0.006).
pub fn apply_expression_bone_elbows(skel: &mut Skeleton) {
    apply_expression_bone_elbows_impl(skel,true);
}
fn apply_expression_bone_elbows_impl(skel:&mut Skeleton,rebuild:bool){
    let by_name=rig_name_index(skel,true);
    for (side, is_left) in [("L", true), ("R", false)] {
        let find = |stem: &str| by_name.get(&format!("{stem}_{side}")).copied();
        let Some(fore) = find("cf_j_forearm01") else { continue };
        let y = quat_to_euler_deg(4, gt_rot(skel, fore))[1];
        if let Some(elbow) = find("cf_s_elbo") {
            let mut e = quat_to_euler_deg(4, gt_rot(skel, elbow));
            e[1] = -y * 0.5;
            st_rot(skel, elbow, euler_order_to_quat(4, e[0], e[1], e[2]));
            let mut p = gt_pos(skel, elbow);
            p[0] = (y * 0.015).sin() * 0.022;
            let bend = ((y - 1.0) * 0.006).sin() * 0.18;
            p[2] = (-0.25 + if is_left { bend } else { -bend }) * 0.1;
            st_pos(skel, elbow, p);
        }
        if let Some(back) = find("cf_s_elboback") {
            let mut e = quat_to_euler_deg(4, gt_rot(skel, back));
            e[1] = -y * 0.5;
            st_rot(skel, back, euler_order_to_quat(4, e[0], e[1], e[2]));
            let mut p = gt_pos(skel, back);
            p[0] = -(y * 0.0039).tan() * 1.2;
            p[2] = if is_left {
                (0.3 - (-y * 0.009).sin() * 7.6 - (y * 0.0022).tan() * 16.0) * 0.1
            } else {
                (0.3 - (y * 0.009).sin() * 7.6 + (y * 0.0022).tan() * 16.0) * 0.1
            };
            st_pos(skel, back, p);
        }
    }
    if rebuild { skel.rebuild_worlds(); }
}

/// `ExpressionBone.LateUpdate` ForearmL/R and HandL/R. The wrist geometry
/// uses world transforms; the forearm twist uses local hand rotation.
pub fn apply_expression_bone_hands(skel: &mut Skeleton) {
    apply_expression_bone_hands_impl(skel,true);
}
fn apply_expression_bone_hands_impl(skel:&mut Skeleton,rebuild:bool){
    let by_name=rig_name_index(skel,true);
    let mut world_cache = std::collections::HashMap::new();
    for (side, offset) in [("L", [-0.01, 0.01, 0.0]), ("R", [0.01, 0.01, 0.0])] {
        let find = |stem: &str| by_name.get(&format!("{stem}_{side}")).copied();
        let Some(hand) = find("cf_j_hand") else {
            continue;
        };
        if let (Some(fore), Some(mid)) = (find("cf_j_forearm01"), find("cf_j_middle01")) {
            let fore_world = world_of(fore, &skel.tfs, &mut world_cache);
            let hand_world = world_of(hand, &skel.tfs, &mut world_cache);
            let mid_world = world_of(mid, &skel.tfs, &mut world_cache);
            let hand_pos = [hand_world[0][3], hand_world[1][3], hand_world[2][3]];
            let mid_pos = [mid_world[0][3], mid_world[1][3], mid_world[2][3]];
            let fore_rot = world_rotation_of(fore, &skel.tfs);
            let hand_rot = world_rotation_of(hand, &skel.tfs);
            let calc = trs(hand_pos, fore_rot, [1.0; 3]);
            if let Some(calc_inv) = mat_inv(&calc) {
                let finger = mat_vec(&calc_inv, mid_pos);
                let f = finger[1].max(0.0) * 100.0 * std::f32::consts::PI / 180.0;
                if let Some(dhand) = find("cf_d_hand") {
                    let mut pos = gt_pos(skel, dhand);
                    pos[0] = if side == "L" {
                        -0.25 + f.sin() * 15.0
                    } else {
                        0.25 - f.sin() * 15.0
                    } * 0.1;
                    pos[1] = f.sin() * 1.5;
                    st_pos(skel, dhand, pos);
                }
                let moved_offset = mat_vec(&trs([0.0; 3], hand_rot, [1.0; 3]), offset);
                let moved = [
                    hand_pos[0] + moved_offset[0],
                    hand_pos[1] + moved_offset[1],
                    hand_pos[2] + moved_offset[2],
                ];
                if let (Some(fore_inv), Some(wrist)) = (mat_inv(&fore_world), find("cf_d_wrist")) {
                    let mut local = mat_vec(&fore_inv, moved);
                    local[0] = 0.0;
                    let len = (local[1] * local[1] + local[2] * local[2]).sqrt();
                    let angle = if len > 1e-8 {
                        (local[1] / len).clamp(-1.0, 1.0).acos().to_degrees()
                            * if local[2] < 0.0 { -1.0 } else { 1.0 }
                    } else {
                        0.0
                    };
                    // TransformRotationEx.SetLocalRotationX reads localRotation.eulerAngles
                    // and assigns Quaternion.Euler, which uses Unity's y-x-z order.
                    let mut euler = quat_to_euler_deg(2, gt_rot(skel, wrist));
                    euler[0] = angle;
                    st_rot(skel, wrist, euler_deg_to_quat(euler));
                }
            }
        }
        if let Some(dfore) = find("cf_d_forearm02") {
            let x = quat_to_euler_deg(5, gt_rot(skel, hand))[0];
            let mut euler = quat_to_euler_deg(5, gt_rot(skel, dfore));
            euler[0] = x * 0.45;
            st_rot(
                skel,
                dfore,
                euler_order_to_quat(5, euler[0], euler[1], euler[2]),
            );
        }
    }
    if rebuild { skel.rebuild_worlds(); }
}

/// The game's independent Bust and BustColDB branches update the breast
/// deform bone and each collision helper from local spine/bust transforms.
pub fn apply_expression_bone_bust(skel: &mut Skeleton) {
    apply_expression_bone_bust_impl(skel,true);
}
fn apply_expression_bone_bust_impl(skel:&mut Skeleton,rebuild:bool){
    let by_name=rig_name_index(skel,true);
    if let (Some(&spine), Some(&deform)) =
        (by_name.get("cf_j_spine03"), by_name.get("cf_d_bust00"))
    {
        let spine_euler = quat_to_euler_deg(5, gt_rot(skel, spine));
        st_rot(skel, deform, euler_order_to_quat(5,
            -spine_euler[0] * 0.35, -spine_euler[1] * 0.35,
            -spine_euler[2] * 0.35));
        let mut pos = gt_pos(skel, deform);
        pos[1] = (-0.081 + spine_euler[0] * 0.003) * 0.1;
        pos[2] = spine_euler[0] * 0.0004;
        st_pos(skel, deform, pos);
    }
    for side in ["L", "R"] {
        let bust = by_name.get(&format!("cf_j_bust02_{side}")).copied();
        let hit = by_name.get(&format!("cf_d_hit_bust_{side}")).copied();
        if let (Some(bust), Some(hit)) = (bust, hit) {
            let pos = gt_pos(skel, bust);
            st_pos(skel, hit, [-pos[0] * 0.5, -pos[1] * 0.5, -pos[2] * 0.25]);
            let scale = gt_scale(skel, bust);
            st_scale(skel, hit, [1.5 - scale[0] * 0.5,
                1.5 - scale[1] * 0.5, 1.5 - scale[2] * 0.5]);
        }
    }
    if rebuild { skel.rebuild_worlds(); }
}

/// Independent Thigh/Knee/Calf branches in `ExpressionBone.LateUpdate`.
/// Thigh twist reads cf_j_thigh00, while knee reads cf_j_leg01 and calf
/// reads cf_j_leg03; they do not depend on the leg-position helper branch.
pub fn apply_expression_bone_leg_twists(skel: &mut Skeleton) {
    apply_expression_bone_leg_twists_impl(skel,true);
}
fn apply_expression_bone_leg_twists_impl(skel:&mut Skeleton,rebuild:bool){
    let by_name=rig_name_index(skel,true);
    for (side, is_left) in [("L", true), ("R", false)] {
        let find = |stem: &str| by_name.get(&format!("{stem}_{side}")).copied();
        if let Some(thigh) = find("cf_j_thigh00") {
            let y = quat_to_euler_deg(5, gt_rot(skel, thigh))[1];
            for (stem, coef) in [("cf_d_thigh01", 0.75), ("cf_d_thigh02", 0.5),
                ("cf_d_thigh03", 0.25)] {
                if let Some(deform) = find(stem) {
                    let mut e = quat_to_euler_deg(5, gt_rot(skel, deform));
                    e[1] = -y * coef;
                    st_rot(skel, deform, euler_order_to_quat(5, e[0], e[1], e[2]));
                }
            }
        }
        if let Some(knee) = find("cf_j_leg01") {
            let x = quat_to_euler_deg(5, gt_rot(skel, knee))[0];
            // BoneName[9/12] is cf_s_kneeB; names do not describe the
            // formula's front/back order. Keep the source dictionary indices.
            if let Some(front) = find("cf_s_kneeB") {
                let mut e = quat_to_euler_deg(5, gt_rot(skel, front));
                e[0] = -x * 0.5;
                e[2] = x * if is_left { 0.1 } else { -0.1 };
                st_rot(skel, front, euler_order_to_quat(5, e[0], e[1], e[2]));
                let mut p = gt_pos(skel, front);
                p[1] = (-x * 0.048 + (x * 0.01).sin() * 1.1) * 0.1;
                p[2] = (-0.6 - (x * 0.0115).sin() * 4.0 + (x * 0.0032).tan() * 5.0) * 0.1;
                st_pos(skel, front, p);
            }
            if let Some(back) = find("cf_d_kneeF") {
                let mut e = quat_to_euler_deg(5, gt_rot(skel, back));
                e[0] = -x * 0.5;
                st_rot(skel, back, euler_order_to_quat(5, e[0], e[1], e[2]));
                let mut p = gt_pos(skel, back);
                p[1] = ((x * 0.012).sin() * 1.8 - (x * 0.00335).tan() * 4.5) * 0.1;
                p[2] = (0.4 + x * 0.0125 - (x * 0.0032).tan() * 4.5) * 0.1;
                st_pos(skel, back, p);
            }
        }
        if let (Some(calf), Some(deform)) = (find("cf_j_leg03"), find("cf_d_leg02")) {
            let y = quat_to_euler_deg(5, gt_rot(skel, calf))[1];
            let mut e = quat_to_euler_deg(5, gt_rot(skel, deform));
            e[1] = y * 0.5;
            st_rot(skel, deform, euler_order_to_quat(5, e[0], e[1], e[2]));
        }
    }
    if rebuild { skel.rebuild_worlds(); }
}

/// `ExpressionBone.LateUpdate` AcsNull. BoneName index 61 is cf_j_head
/// (source rotation) and index 60 is a_n_neck (accessory attachment target).
pub fn apply_expression_bone_acs_null(skel: &mut Skeleton) {
    apply_expression_bone_acs_null_impl(skel,true);
}
fn apply_expression_bone_acs_null_impl(skel:&mut Skeleton,rebuild:bool){
    let head = skel.tfs.iter().find(|(_, tf)| tf.name == "cf_j_head").map(|(id, _)| *id);
    let neck = skel.tfs.iter().find(|(_, tf)| tf.name == "a_n_neck").map(|(id, _)| *id);
    if let (Some(head), Some(neck)) = (head, neck) {
        let e = quat_to_euler_deg(5, gt_rot(skel, head));
        let mut p = gt_pos(skel, neck);
        p[0] = e[2] * 0.0002;
        p[2] = (-0.08 - e[0] * 0.002) * 0.1;
        st_pos(skel, neck, p);
        if rebuild { skel.rebuild_worlds(); }
    }
}

/// Default enabled branches of the Studio ExpressionBone component.
/// The scene factory decides whether this component exists at all.
pub fn apply_expression_bone(skel: &mut Skeleton) {
    // Branches read local transforms (or world_of snapshots from those locals),
    // never the display palette. Publish the identical final palette once.
    apply_expression_bone_arms_impl(skel,false,false);
    apply_expression_bone_elbows_impl(skel,false);
    apply_expression_bone_hands_impl(skel,false);
    apply_expression_bone_leg_positions_impl(skel,false,false);
    apply_expression_bone_leg_twists_impl(skel,false);
    apply_expression_bone_bust_impl(skel,false);
    apply_expression_bone_acs_null_impl(skel,false);
    skel.rebuild_worlds();
}

/// LegL/R use a calculation Transform with thigh WORLD position and waist
/// WORLD rotation, then the helpers created by ExpressionBone.Initialize.
pub fn apply_expression_bone_leg_positions(skel: &mut Skeleton) {
    apply_expression_bone_leg_positions_impl(skel,true,false);
}
fn apply_expression_bone_leg_positions_impl(skel:&mut Skeleton,rebuild:bool,complete_snapshot:bool){
    use std::collections::HashMap;
    let name2id=rig_name_index(skel,false);
    let mut world: HashMap<i64, Mat4> = HashMap::new();
    let needed=["cf_j_waist02","cf_j_thigh00_L","cf_j_thigh00_R","cf_j_leg01_L","cf_j_leg01_R"];
    let ids:Vec<_>=if complete_snapshot{skel.tfs.keys().copied().collect()}else{needed.iter().filter_map(|name|name2id.get(*name).copied()).collect()};
    for id in &ids {
        let matrix=world_of(*id,&skel.tfs,&mut world);world.insert(*id,matrix);
    }
    let get = |n: &str| name2id.get(n).copied();
    let inv_pt = |m: &Mat4, p: [f32; 3]| mat_inv(m).map(|inv| mat_vec(&inv, p)).unwrap_or([0.0; 3]);
    let wpos = |id: i64| [world[&id][0][3], world[&id][1][3], world[&id][2][3]];

    // ---- Legs ----
    if let (Some(i_nh), Some(i_waist)) = (get("cf_n_height"), get("cf_j_waist02")) {
        let nheight = gt_scale(skel, i_nh)[1].max(1e-6);
        for &(side, is_left) in &[("L", true), ("R", false)] {
            let (Some(i_thigh), Some(i_leg01)) =
                (get(&format!("cf_j_thigh00_{side}")), get(&format!("cf_j_leg01_{side}")))
            else {
                continue;
            };
            let gx = if is_left { -0.083 } else { 0.083 };
            let goal = mat_mul(&world[&i_waist], &translation_mat(gx, 0.4, -0.008));
            let calc = trs(wpos(i_thigh), world_rotation_of(i_waist, &skel.tfs), [1.0; 3]);
            let v1 = inv_pt(&calc, wpos(i_leg01));
            let v1 = [v1[0] / nheight * 10.0, v1[1] / nheight * 10.0, v1[2] / nheight * 10.0];
            let num = if v1[1] >= 0.0 { v1[1].abs() } else { 0.0 };
            let ig = mat_inv(&goal).map(|m| mat_vec(&m, wpos(i_leg01))).unwrap_or([0.0; 3]);
            let v = [ig[0] * -10.0, ig[1] * -10.0, ig[2] * -10.0];
            if let Some(i) = get(&format!("cf_s_leg_{side}")) {
                let (px, py, pz, xr, yr, zr) = if is_left {
                    (
                        (1.0 + v[0] * 0.2 + ((v[2] * 1.1).abs() - (8.849 - v[1])) * 0.06) * -0.1,
                        (1.0 + (8.849 - v[1]) * 0.4 + v[0] * 0.2 + num * 0.6) * 0.1,
                        (-v[2] * 0.2 + v[0] * 0.05 - num * 0.4) * 0.1,
                        v[2] * 6.0 - v[0] * 2.0 - num * 5.0,
                        -v[0] * 12.0 - v[2] * 3.0,
                        -v[0],
                    )
                } else {
                    (
                        (-1.0 + v[0] * 0.2 - ((v[2] * 1.1).abs() - (8.849 - v[1])) * 0.06) * -0.1,
                        (1.0 + (8.849 - v[1]) * 0.4 - v[0] * 0.2 + num * 0.6) * 0.1,
                        (-v[2] * 0.2 - v[0] * 0.05 - num * 0.4) * 0.1,
                        v[2] * 6.0 + v[0] * 2.0 - num * 5.0,
                        -v[0] * 12.0 + v[2] * 3.0,
                        -v[0],
                    )
                };
                let mut p = gt_pos(skel, i);
                p[0] = px;
                p[1] = py;
                p[2] = pz;
                st_pos(skel, i, p);
                st_rot(skel, i, euler_order_to_quat(5, xr, yr, zr));
                let mut sc = gt_scale(skel, i);
                sc[0] = 1.0 + v[2].abs() * 0.2;
                sc[1] = 1.0 + num * 0.4;
                sc[2] = 1.0 + v[2].abs() * 0.3 + num * 0.3;
                st_scale(skel, i, sc);
            }
            if let Some(i) = get(&format!("cf_d_siri_{side}")) {
                let (px, py, pz) = if is_left {
                    (
                        (-0.7 + v[0] * 0.01 - (v[0] * 0.02).abs() - (v[2] * 0.005).abs()) * 0.1,
                        ((8.849 - v[1]) * 0.01 + (v[2] * 0.02).abs() + v[0] * 0.04) * 0.1,
                        (-(8.849 - v[1]) * 0.03 - (v[2] - num * 3.0) * 0.03 - v[0].abs() * 0.02)
                            * 0.1,
                    )
                } else {
                    (
                        (0.7 + v[0] * 0.01 + (v[0] * 0.02).abs() + (v[2] * 0.005).abs()) * 0.1,
                        ((8.849 - v[1]) * 0.01 + (v[2] * 0.02).abs() - v[0] * 0.04) * 0.1,
                        (-(8.849 - v[1]) * 0.03 - (v[2] - num * 3.0) * 0.03 - v[0].abs() * 0.02)
                            * 0.1,
                    )
                };
                let mut p = gt_pos(skel, i);
                p[0] = px;
                p[1] = py;
                p[2] = pz;
                st_pos(skel, i, p);
                let mut sc = gt_scale(skel, i);
                sc[0] = 1.0 + v[2].abs() * 0.02;
                sc[1] = 1.0 + num.abs() * 0.01;
                st_scale(skel, i, sc);
            }
        }
    }

    if rebuild { skel.rebuild_worlds(); }
}

#[cfg(test)]
#[test]
#[ignore="explicit read-only portable body and native animation fixtures"]
fn expression_sparse_snapshot_and_single_publish_match_legacy(){
    let path=std::env::var("OK_TEST_LEGACY_PORTABLE").unwrap();
    let session=crate::portable::load(std::path::Path::new(&path)).unwrap();
    let base=session.body_shaped.as_ref().or(session.body_skel.as_ref()).unwrap();
    let game=std::path::PathBuf::from(std::env::var("OK_TEST_GAME_ROOT").unwrap());
    let key="states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01";
    let mut old_ns=0u128;let mut new_ns=0u128;
    for frame in 0..240 {
        let mut sampled=base.clone();
        crate::anim::crossfade::sample(&game,&mut sampled,key,frame as f32/120.,&std::collections::HashMap::new(),Default::default());
        let mut old=sampled.clone();let mut new=sampled;
        // The original branch sequence: whole-rig snapshots and publication
        // after each correction, using the unchanged correction formulas.
        let start=std::time::Instant::now();
        apply_expression_bone_arms_impl(&mut old,true,true);
        apply_expression_bone_elbows(&mut old);apply_expression_bone_hands(&mut old);
        apply_expression_bone_leg_positions_impl(&mut old,true,true);
        apply_expression_bone_leg_twists(&mut old);apply_expression_bone_bust(&mut old);apply_expression_bone_acs_null(&mut old);
        old_ns+=start.elapsed().as_nanos();
        let start=std::time::Instant::now();apply_expression_bone(&mut new);new_ns+=start.elapsed().as_nanos();
        assert_eq!(old.worlds,new.worlds,"world palette at {frame}");assert_eq!(old.names,new.names);
        for(id,tf)in &old.tfs{let actual=&new.tfs[id];assert_eq!(tf.pos,actual.pos,"position {id} at {frame}");assert_eq!(tf.rot,actual.rot,"rotation {id} at {frame}");assert_eq!(tf.scale,actual.scale,"scale {id} at {frame}");}
    }
    eprintln!("OK expression exact frames=240 old_mean_ms={:.6} new_mean_ms={:.6}",old_ns as f64/240e6,new_ns as f64/240e6);
}

/// Local TRS matrix (pos, euler degrees, scale).
pub fn local_trs(pos: [f32; 3], euler_deg: [f32; 3], scl: [f32; 3]) -> Mat4 {
    trs(pos, euler_deg_to_quat(euler_deg), scl)
}

fn quat_conj(q: [f32; 4]) -> [f32; 4] {
    [-q[0], -q[1], -q[2], q[3]]
}

fn quat_from_mat(m: &Mat4) -> [f32; 4] {
    let m00 = m[0][0];
    let m11 = m[1][1];
    let m22 = m[2][2];
    let trace = m00 + m11 + m22;
    if trace > 0.0 {
        let s = (trace + 1.0).sqrt() * 2.0;
        [
            (m[2][1] - m[1][2]) / s,
            (m[0][2] - m[2][0]) / s,
            (m[1][0] - m[0][1]) / s,
            0.25 * s,
        ]
    } else if m00 > m11 && m00 > m22 {
        let s = (1.0 + m00 - m11 - m22).sqrt() * 2.0;
        [
            0.25 * s,
            (m[0][1] + m[1][0]) / s,
            (m[0][2] + m[2][0]) / s,
            (m[2][1] - m[1][2]) / s,
        ]
    } else if m11 > m22 {
        let s = (1.0 + m11 - m00 - m22).sqrt() * 2.0;
        [
            (m[0][1] + m[1][0]) / s,
            0.25 * s,
            (m[1][2] + m[2][1]) / s,
            (m[0][2] - m[2][0]) / s,
        ]
    } else {
        let s = (1.0 + m22 - m00 - m11).sqrt() * 2.0;
        [
            (m[0][2] + m[2][0]) / s,
            (m[1][2] + m[2][1]) / s,
            0.25 * s,
            (m[1][0] - m[0][1]) / s,
        ]
    }
}

fn apply_world_z(tf: &mut Tf, parent_world: Option<&Mat4>, deg: f32) {
    let r = quat_axis_angle([0.0, 0.0, 1.0], deg.to_radians());
    let pq = parent_world.map(quat_from_mat).unwrap_or([0.0, 0.0, 0.0, 1.0]);
    let extra_local = quat_mul(quat_mul(quat_conj(pq), r), pq);
    tf.rot = quat_mul(extra_local, tf.rot);
}

fn pose_deg_for(name: &str) -> Option<f32> {
    let n = name.to_ascii_lowercase();
    let left = n.contains("_l") || n.ends_with("l");
    let right = n.contains("_r") || n.ends_with("r");
    if !left && !right {
        return None;
    }
    let sign = if left { 1.0 } else { -1.0 };
    // World-Z: +Z rotation sends +X toward -Y (down). Left arm is -X so opposite sign.
    if n.contains("arm00") || n.contains("forearm") || n.contains("hand") || n.contains("wrist") {
        Some(sign * 80.0)
    } else if n.contains("shoulder") {
        Some(sign * 42.0)
    } else if n.contains("elbo") || n.contains("arm01") || n.contains("arm02") || n.contains("arm03") {
        Some(sign * 70.0)
    } else {
        None
    }
}

impl Skeleton {
    pub fn world(&self, name: &str) -> Option<Mat4> {
        let i = self
            .names
            .iter()
            .position(|n| n.eq_ignore_ascii_case(name))?;
        Some(self.worlds[i])
    }

    pub fn index_of(&self, name: &str) -> Option<usize> {
        self.names.iter().position(|n| n.eq_ignore_ascii_case(name))
    }

    /// Attach points like `a_n_hair_pony` / `cf_J_FaceUp_ty` live on the
    /// transform hierarchy, not necessarily in the SMR bone list.
    pub fn world_tf(&self, name: &str) -> Option<Mat4> {
        if let Some(m) = self.world(name) {
            return Some(m);
        }
        let id = self
            .tfs
            .iter()
            .find(|(_, t)| t.name.eq_ignore_ascii_case(name))
            .map(|(id, _)| *id)?;
        let mut cache = std::collections::HashMap::new();
        Some(world_of(id, &self.tfs, &mut cache))
    }

    pub fn root_world(&self) -> Mat4 {
        for id in &self.bone_ids {
            if let Some(tf) = self.tfs.get(id) {
                if tf.parent.filter(|p| *p != 0 && *p != *id).is_none() {
                    let mut cache = std::collections::HashMap::new();
                    return world_of(*id, &self.tfs, &mut cache);
                }
            }
        }
        for (id, tf) in &self.tfs {
            if tf.parent.filter(|p| *p != 0 && *p != *id).is_none() {
                let mut cache = std::collections::HashMap::new();
                return world_of(*id, &self.tfs, &mut cache);
            }
        }
        ident()
    }
}

pub fn mat_mul_pub(a: &Mat4, b: &Mat4) -> Mat4 {
    mat_mul(a, b)
}

pub fn mat_inv_pub(m: &Mat4) -> Mat4 {
    mat_inv(m).unwrap_or_else(ident)
}

/// Inverse basis for direction transforms. Elimination and pivoting retain the
/// full source matrix, but the unused inverse translation column is not solved.
/// Each retained column has exactly the same arithmetic as `mat_inv`.
pub(crate) fn mat_inv_basis(m: &Mat4) -> Mat4 {
    let mut a = *m;
    let mut inv = [[1.0,0.0,0.0],[0.0,1.0,0.0],[0.0,0.0,1.0],[0.0,0.0,0.0]];
    for i in 0..4 {
        let mut piv=i;
        let mut best=a[i][i].abs();
        for r in i+1..4 { if a[r][i].abs()>best {best=a[r][i].abs();piv=r;} }
        if best<1e-8 {return ident();}
        if piv!=i {a.swap(i,piv);inv.swap(i,piv);}
        let d=a[i][i];
        for c in 0..4 {a[i][c]/=d;}
        for c in 0..3 {inv[i][c]/=d;}
        for r in 0..4 {
            if r==i {continue;}
            let f=a[r][i];
            for c in 0..4 {a[r][c]-=f*a[i][c];}
            for c in 0..3 {inv[r][c]-=f*inv[i][c];}
        }
    }
    let mut out=ident();
    for r in 0..4 {out[r][..3].copy_from_slice(&inv[r]);}
    out
}

#[test]
fn inverse_basis_matches_full_elimination_bits() {
    let mut seed=0x5e12_cac1u32;
    let mut random=||{seed=seed.wrapping_mul(1664525).wrapping_add(1013904223);(seed as i32 as f32)/2147483648.0};
    let check=|m:Mat4|{
        let reference=mat_inv_pub(&m);let actual=mat_inv_basis(&m);
        for r in 0..4 {for c in 0..3 {assert_eq!(actual[r][c].to_bits(),reference[r][c].to_bits(),"{m:?} [{r}][{c}]");}}
    };
    for i in 0..4096 {
        let mut m=ident();
        for r in 0..4 {for c in 0..4 {m[r][c]=random()*16.0;}}
        if i%2==0 {m[3]=[0.0,0.0,0.0,1.0];}
        if i%7==0 {m[1]=m[0];}
        if i%11==0 {m[2][2]*=1e-10;}
        if i%13==0 {m[0][3]*=1e12;}
        check(m);
    }
    let mut m=ident();m[0][0]=-0.0;check(m);
    m[0][0]=f32::INFINITY;check(m);
    m[0][0]=f32::NAN;check(m);
}

#[test]
#[ignore="release-only inverse direction hot path A/B"]
fn inverse_basis_hot_path_benchmark() {
    let matrices:Vec<_>=(0..1024).map(|i|trs([i as f32*0.01,-2.0,1.0],euler_deg_to_quat([i as f32*0.13,24.0,-17.0]),[1.2,0.7,-0.8])).collect();
    for basis in [false,true,false,true]{
        let start=std::time::Instant::now();let mut sum=0.0;
        for i in 0..262144 {let m=std::hint::black_box(&matrices[i%matrices.len()]);let inverse=if basis {mat_inv_basis(m)}else{mat_inv_pub(m)};sum+=std::hint::black_box(inverse[0][0]);}
        eprintln!("INVERSE BASIS basis={basis} ms={:.3} checksum={sum}",start.elapsed().as_secs_f64()*1000.0);
    }
}

pub(crate) fn mat_inverse_checked(m:&Mat4)->Option<Mat4> {mat_inv(m)}

/// `SetParent(parent, worldPositionStays: false)` with identity local:
/// child world = parent world.
pub fn under_parent(parent: &Mat4, child_root: &Mat4) -> Mat4 {
    mat_mul(parent, &mat_inv_pub(child_root))
}

pub fn world_scale_pub(m: &Mat4) -> [f32; 3] {
    let col = |j: usize| (m[0][j] * m[0][j] + m[1][j] * m[1][j] + m[2][j] * m[2][j]).sqrt();
    [col(0), col(1), col(2)]
}

/// Card addMove matrices have positive scale and no shear.
pub(crate) fn replace_local_trs(tf: &mut Tf, local: &Mat4) {
    let scale = world_scale_pub(local);
    let mut rotation = *local;
    for axis in 0..3 { for row in 0..3 { rotation[row][axis] /= scale[axis]; } }
    tf.pos = mat_pos(local);
    tf.rot = quat_from_mat(&rotation);
    tf.scale = scale;
}

pub fn apply_mat(pos: &mut [[f32; 3]], m: &Mat4) {    for p in pos {
        *p = mat_vec(m, *p);
    }
}

pub fn mat_pos(m: &Mat4) -> [f32; 3] {
    [m[0][3], m[1][3], m[2][3]]
}

/// `OK_ACCDIAG=<substr>` gates the accessory skinning diagnostic. Empty/unset
/// disables it; a value matches when the mesh name contains it (case-folded).
pub fn accdiag_matches(name: &str) -> bool {
    match std::env::var("OK_ACCDIAG") {
        Ok(v) if !v.is_empty() => name.to_ascii_lowercase().contains(&v.to_ascii_lowercase()),
        _ => false,
    }
}

#[cfg(test)]
#[test]
fn rig_name_index_preserves_duplicate_policy_and_inventory_changes(){
    use std::collections::HashMap;use std::sync::Arc;
    let mut tfs=HashMap::new();for (id,name,parent) in [(1,"duplicate",None),(2,"duplicate",Some(1)),(3,"unique",Some(1)),(0,"zero",None)]{tfs.insert(id,Tf{sibling_index:None,name:name.into(),parent,pos:[id as f32,2.,-1.],rot:[0.,0.,0.,1.],scale:[1.,2.,0.5]});}
    let mut s=Skeleton{names:vec![],worlds:vec![],rest:vec![],smr_count:4,tfs,bone_ids:vec![1,2,3,0],source_mesh:17};
    for frame in 0..40 {
        let mut first=HashMap::new();let mut last=HashMap::new();for(id,tf)in &s.tfs{first.entry(tf.name.clone()).or_insert(*id);last.insert(tf.name.clone(),*id);}
        assert_eq!(*rig_name_index(&s,false),first);assert_eq!(*rig_name_index(&s,true),last);
        let names=vec!["duplicate".to_owned(),"unique".to_owned(),"missing".to_owned(),"duplicate".to_owned()];
        let baseline:HashMap<_,_>=s.tfs.iter().filter(|(_,tf)|names.iter().any(|name|name==&tf.name)).map(|(id,tf)|(*id,(tf.pos,tf.rot,tf.scale))).collect();
        assert_eq!(capture_baseline(&s,&names),baseline);
        let abm:Vec<_>=names.into_iter().map(|name|crate::abmx::AbmBone{name,location:0,coord:vec![]}).collect();assert_eq!(capture_abm_baseline(&s,&abm),baseline);
        let retained=rig_name_index(&s,false);s.tfs.get_mut(&1).unwrap().pos[0]+=0.125;assert!(Arc::ptr_eq(&retained,&rig_name_index(&s,false)));
        let mut cache=HashMap::new();let expected:HashMap<_,_>=s.tfs.keys().map(|id|(*id,world_of(*id,&s.tfs,&mut cache))).collect();let mut merged=HashMap::new();for id in s.tfs.keys(){let matrix=world_of(*id,&s.tfs,&mut merged);merged.insert(*id,matrix);}for id in s.tfs.keys(){assert_eq!(expected[id],merged[id]);}
        if frame%3==0{s.tfs.get_mut(&3).unwrap().name=format!("renamed{frame}");}else{s.source_mesh+=1;}
    }
    for source in 0..50{s.source_mesh=source;rig_name_index(&s,false);}RIG_NAMES.with(|cache|assert!(cache.borrow().len()<=32));
}

#[cfg(test)]
#[test]
fn repeated_world_rebuild_retains_names_and_exact_hierarchy() {
    let mut tfs = std::collections::HashMap::new();
    for (id, parent, name) in [(1, None, "root_transform"), (2, Some(1), "child_transform")] {
        tfs.insert(id, Tf { sibling_index: None, name: name.into(), parent,
            pos: [id as f32, 2., -3.], rot: [0., 0., 0., 1.], scale: [1., 2., 0.5] });
    }
    let mut s = Skeleton { names: vec![], worlds: vec![], rest: vec![], smr_count: 2,
        tfs, bone_ids: vec![1, 2, 77], source_mesh: 0 };
    s.rebuild_worlds();
    let pointers = [s.names[0].as_ptr(), s.names[1].as_ptr()];
    for frame in 0..64 {
        s.tfs.get_mut(&1).unwrap().pos[0] = frame as f32 * 0.125;
        s.tfs.get_mut(&2).unwrap().rot = euler_order_to_quat(4, frame as f32, 17., -9.);
        s.rebuild_worlds();
        assert_eq!([s.names[0].as_ptr(), s.names[1].as_ptr()], pointers);
        for (index, id) in s.bone_ids.iter().enumerate() {
            assert_eq!(s.worlds[index], world_of_id(*id, &s.tfs));
        }
    }
    s.tfs.get_mut(&2).unwrap().name = "renamed".into();
    s.bone_ids = vec![2, 77, 1]; s.rebuild_worlds();
    assert_eq!(s.names, ["renamed", "", "root_transform"]);
    s.tfs.remove(&1); s.bone_ids.truncate(2); s.rebuild_worlds();
    assert_eq!(s.names.len(), 2); assert_eq!(s.worlds.len(), 2);
    assert_eq!(s.worlds[0], world_of_id(2, &s.tfs));
    assert_eq!(s.worlds[1], ident());
    // Same IDs/source, different parent links cannot reuse the old schedule.
    s.tfs.insert(1,Tf{sibling_index:None,name:"other".into(),parent:Some(2),pos:[-2.,3.,1.],rot:[0.,0.,0.,1.],scale:[-1.,0.5,2.]});
    s.tfs.get_mut(&2).unwrap().parent=Some(0);s.bone_ids=vec![1,2,77];s.rebuild_worlds();
    for(index,id)in s.bone_ids.iter().enumerate(){assert_eq!(s.worlds[index],world_of_id(*id,&s.tfs));}
    s.tfs.get_mut(&1).unwrap().parent=Some(1);s.rebuild_worlds();
    assert_eq!(s.worlds[0],world_of_id(1,&s.tfs));
    s.tfs.reserve(2048);s.rebuild_worlds();
    for(index,id)in s.bone_ids.iter().enumerate(){assert_eq!(s.worlds[index],world_of_id(*id,&s.tfs));}
    let inventory=s.tfs.iter().map(|(id,tf)|(*id,tf.clone())).collect::<Vec<_>>();s.tfs.clear();
    for(id,tf)in inventory.into_iter().rev(){s.tfs.insert(id,tf);}s.rebuild_worlds();
    for(index,id)in s.bone_ids.iter().enumerate(){assert_eq!(s.worlds[index],world_of_id(*id,&s.tfs));}
    for source in 0..140 {s.source_mesh=source;s.rebuild_worlds();}
    WORLD_PLANS.with(|plans|assert!(plans.borrow().len()<=128));
}

#[cfg(test)]
#[test]
fn pooled_animation_snapshots_preserve_values_order_and_nested_isolation(){
    POSE_SNAPSHOTS.with(|pool|pool.borrow_mut().clear());
    let mut source=Skeleton{names:vec![],worlds:vec![],rest:vec![],smr_count:2,tfs:std::collections::HashMap::new(),bone_ids:vec![1,2],source_mesh:42};
    for id in 1..=2{source.tfs.insert(id,Tf{name:"duplicate".into(),parent:(id==2).then_some(1),sibling_index:Some(id as u32),pos:[id as f32,0.,0.],rot:[0.,0.,0.,1.],scale:[1.;3]});}source.rebuild_worlds();source.rest=source.worlds.clone();
    let check=|snapshot:&Skeleton,expected:&Skeleton|{assert_eq!(snapshot.tfs.keys().collect::<Vec<_>>(),expected.tfs.keys().collect::<Vec<_>>());assert_eq!(snapshot.names,expected.names);assert_eq!(snapshot.worlds,expected.worlds);assert_eq!(snapshot.rest,expected.rest);assert_eq!(snapshot.bone_ids,expected.bone_ids);for(id,t)in &expected.tfs{let a=&snapshot.tfs[id];assert_eq!((&a.name,a.parent,a.sibling_index,a.pos,a.rot,a.scale),(&t.name,t.parent,t.sibling_index,t.pos,t.rot,t.scale));}};
    for frame in 0..32 {source.tfs.get_mut(&1).unwrap().pos[1]=frame as f32;source.rebuild_worlds();let mut outer=SkeletonSnapshot::new(&source);check(&outer,&source);let inner=SkeletonSnapshot::new(&outer);outer.tfs.get_mut(&1).unwrap().pos[1]=-1.;check(&inner,&source);}
    source.tfs.remove(&2);source.bone_ids=vec![1];source.tfs.get_mut(&1).unwrap().name="renamed".into();source.rebuild_worlds();check(&SkeletonSnapshot::new(&source),&source);
    let held=(0..9).map(|_|SkeletonSnapshot::new(&source)).collect::<Vec<_>>();drop(held);POSE_SNAPSHOTS.with(|pool|assert!(pool.borrow().len()<=4));
}

// Derived topology belongs to the sampling thread, never to serialized cards.
// Validate every parent and output ID: source_mesh alone is not a rig identity.
// Each publication still validates the complete live topology. Bit-identical
// locals and unchanged parents yield the same matrix through the same formula;
// reuse those values without skipping animation or physics updates.
enum WorldParent { Local, Missing, Node(usize) }
struct WorldNode { parent:WorldParent }
struct WorldPlan {
    source_mesh:i64, topology:Vec<(i64,Option<i64>)>, bone_ids:Vec<i64>,
    nodes:Vec<WorldNode>, inputs:Vec<Option<usize>>, outputs:Vec<Option<usize>>, locals:Vec<Mat4>, matrices:Vec<Mat4>,
    local_inputs:Vec<Option<[u32;10]>>,dirty:Vec<bool>,
}
thread_local! {static WORLD_PLANS:std::cell::RefCell<Vec<WorldPlan>>=const {std::cell::RefCell::new(Vec::new())};}
impl WorldPlan {
    fn matches(&self,s:&Skeleton)->bool {
        self.source_mesh==s.source_mesh&&self.topology.len()==s.tfs.len()&&self.bone_ids==s.bone_ids&&
        self.topology.iter().zip(&s.tfs).all(|((id,parent),(actual,tf))|id==actual&&tf.parent==*parent)
    }
    fn compile(s:&Skeleton)->Option<Self> {
        fn visit(id:i64,s:&Skeleton,seen:&mut std::collections::HashMap<i64,usize>,active:&mut std::collections::HashSet<i64>,nodes:&mut Vec<WorldNode>)->Result<Option<usize>,()> {
            if let Some(index)=seen.get(&id){return Ok(Some(*index));}
            let Some(tf)=s.tfs.get(&id)else{return Ok(None)};
            if !active.insert(id){return Err(());}
            let parent=match tf.parent {Some(p)if p!=0&&p!=id=>match visit(p,s,seen,active,nodes)? {Some(i)=>WorldParent::Node(i),None=>WorldParent::Missing},_=>WorldParent::Local};
            active.remove(&id);let index=nodes.len();nodes.push(WorldNode{parent});seen.insert(id,index);Ok(Some(index))
        }
        let mut seen=std::collections::HashMap::new();let mut active=std::collections::HashSet::new();let mut nodes=Vec::new();let mut outputs=Vec::new();
        for id in &s.bone_ids {outputs.push(visit(*id,s,&mut seen,&mut active,&mut nodes).ok()?);}
        let matrices=vec![ident();nodes.len()];let locals=matrices.clone();let inputs=s.tfs.keys().map(|id|seen.get(id).copied()).collect();
        let local_inputs=vec![None;nodes.len()];let dirty=vec![true;nodes.len()];
        Some(Self{source_mesh:s.source_mesh,topology:s.tfs.iter().map(|(id,tf)|(*id,tf.parent)).collect(),bone_ids:s.bone_ids.clone(),nodes,inputs,outputs,locals,matrices,local_inputs,dirty})
    }
}

#[cfg(test)]
#[test]
fn incremental_world_publication_matches_recursive_bits_after_live_changes(){
    let mut s=Skeleton{names:vec![],worlds:vec![],rest:vec![],smr_count:4,tfs:std::collections::HashMap::new(),bone_ids:vec![1,2,3,99],source_mesh:-1918};
    for id in 1..=3 {s.tfs.insert(id,Tf{name:format!("bone{id}"),parent:if id==1{None}else{Some(id-1)},sibling_index:None,pos:[0.1,0.2,0.3],rot:[0.1,0.2,0.3,0.9],scale:[-0.7,0.0,1.3]});}
    for frame in 0..24 {
        match frame {
            2=>s.tfs.get_mut(&2).unwrap().pos[0]=-0.0,
            3=>s.tfs.get_mut(&2).unwrap().pos[0]=0.0,
            5=>s.tfs.get_mut(&1).unwrap().scale=[1.2,0.8,-0.9],
            7=>s.tfs.get_mut(&3).unwrap().parent=Some(1),
            9=>s.tfs.get_mut(&2).unwrap().parent=Some(100),
            11=>{let mut added=s.tfs[&1].clone();added.name="new".into();added.parent=None;s.tfs.insert(100,added);},
            13=>s.bone_ids.reverse(),
            15=>s.tfs.get_mut(&3).unwrap().name="renamed".into(),
            17=>{s.tfs.remove(&100);},
            19=>s.tfs.get_mut(&1).unwrap().rot=[0.,0.,0.,1.],
            _=>{}
        }
        s.rebuild_worlds();let mut oracle=std::collections::HashMap::new();
        for (index,id) in s.bone_ids.iter().enumerate(){let expected=world_of(*id,&s.tfs,&mut oracle);
            assert_eq!(s.worlds[index].map(|row|row.map(f32::to_bits)),expected.map(|row|row.map(f32::to_bits)),"frame {frame} id {id}");
            assert_eq!(s.names[index],s.tfs.get(id).map_or("",|tf|tf.name.as_str()));
        }
    }
}

pub fn world_of_id(id: i64, tfs: &std::collections::HashMap<i64, Tf>) -> Mat4 {
    let mut cache = std::collections::HashMap::new();
    world_of(id, tfs, &mut cache)
}

pub(crate) fn world_of(id: i64, tfs: &std::collections::HashMap<i64, Tf>, cache: &mut std::collections::HashMap<i64, Mat4>) -> Mat4 {
    if let Some(m) = cache.get(&id) {
        return *m;
    }
    let Some(tf) = tfs.get(&id) else {
        return ident();
    };
    let local = trs(tf.pos, tf.rot, tf.scale);
    let world = if let Some(p) = tf.parent {
        if p != 0 && p != id {
            mat_mul(&world_of(p, tfs, cache), &local)
        } else {
            local
        }
    } else {
        local
    };
    cache.insert(id, world);
    world
}

pub(crate) struct SkelGraph {
    asset_roots: std::collections::HashMap<String, Vec<i64>>,
    tfs: std::collections::HashMap<i64, Tf>,
    smr_bones: std::collections::HashMap<i64, Vec<Vec<i64>>>,
    go_name: std::collections::HashMap<i64, String>,
    go_tf: std::collections::HashMap<i64, i64>,
    // A Mesh asset can be referenced by several prefab renderer objects.
    // Keep every instance; its transform is selected in the prefab context.
    mesh_go: std::collections::HashMap<i64, Vec<i64>>,
}

pub(crate) fn build_skel_graph(serialized: &SerializedFile) -> SkelGraph {
    let mut go_name: std::collections::HashMap<i64, String> = std::collections::HashMap::new();
    let mut tfs: std::collections::HashMap<i64, Tf> = std::collections::HashMap::new();
    let mut smr_bones: std::collections::HashMap<i64, Vec<Vec<i64>>> = std::collections::HashMap::new();
    let mut go_tf: std::collections::HashMap<i64, i64> = std::collections::HashMap::new();
    let mut mesh_go: std::collections::HashMap<i64, Vec<i64>> = std::collections::HashMap::new();
    for info in &serialized.objects {
        // Only these types are used: skip reading (decompressing, parsing) textures and meshes.
        if !matches!(info.type_id, 1 | 4 | 33 | 137) { continue; }
        let Ok(obj) = UnityObject::from_serialized_file(serialized, info) else {
            continue;
        };
        match info.type_id {
            1 => {
                go_name.insert(info.path_id, obj.name().unwrap_or_default());
            }
            4 => {
                let props = obj.class.properties();
                let go = props.get("m_GameObject").and_then(pptr);
                if let Some(g) = go {
                    go_tf.insert(g, info.path_id);
                }
                let name = go.and_then(|g| go_name.get(&g).cloned()).unwrap_or_default();
                tfs.insert(
                    info.path_id,
                    Tf { sibling_index: None,
                        name,
                        parent: props.get("m_Father").and_then(pptr).filter(|p| *p != 0),
                        pos: props.get("m_LocalPosition").map(vec3).unwrap_or([0.0; 3]),
                        rot: props
                            .get("m_LocalRotation")
                            .map(quat)
                            .unwrap_or([0.0, 0.0, 0.0, 1.0]),
                        scale: props.get("m_LocalScale").map(vec3).unwrap_or([1.0; 3]),
                    },
                );
            }
            33 | 137 => {
                let props = obj.class.properties();
                let go = props.get("m_GameObject").and_then(pptr);
                let Some(mesh) = props.get("m_Mesh").and_then(pptr) else {
                    continue;
                };
                if let Some(g) = go {
                    mesh_go.entry(mesh).or_default().push(g);
                }
                let mut bones = Vec::new();
                if let Some(UnityValue::Array(arr)) = props.get("m_Bones") {
                    for b in arr {
                        if let Some(id) = pptr(b) {
                            bones.push(id);
                        }
                    }
                }
                smr_bones.entry(mesh).or_default().push(bones);
            }
            _ => {}
        }
    }
    for info in &serialized.objects {
        if info.type_id != 4 {
            continue;
        }
        let Ok(obj) = UnityObject::from_serialized_file(serialized, info) else {
            continue;
        };
        let props=obj.class.properties();
        if let Some(UnityValue::Array(children))=props.get("m_Children") {
            for (index,child) in children.iter().enumerate() {
                if let Some(tf)=pptr(child).and_then(|id|tfs.get_mut(&id)) {
                    if tf.parent==Some(info.path_id) {tf.sibling_index=Some(index as u32);}
                }
            }
        }
        if let Some(g) = props.get("m_GameObject").and_then(pptr) {
            if let Some(tf) = tfs.get_mut(&info.path_id) {
                if let Some(n) = go_name.get(&g) {
                    tf.name = n.clone();
                }
            }
        }
    }
    SkelGraph {
        asset_roots: crate::unity::prefab_asset_roots(serialized),
        tfs,
        smr_bones,
        go_name,
        go_tf,
        mesh_go,
    }
}

#[test]
#[ignore = "requires local game oo_base serialized Transform hierarchy"]
fn source_sibling_serialized_hierarchy_control() {
    let bundle=crate::unity::load_bundle_path_unbounded(&crate::test_support::game_path("/abdata/chara/oo_base.unity3d")).unwrap();
    let mut checked=0;let mut non_id_order=0;
    for file in &bundle.files {
        let Ok(bytes)=bundle.extract_file_data(file) else {continue;};
        let Ok(serialized)=unity_asset_binary::asset::parse_serialized_file(bytes) else {continue;};
        let graph=build_skel_graph(&serialized);
        for info in serialized.objects_of_type(4) {
            let obj=UnityObject::from_serialized_file(&serialized,info).unwrap();
            let Some(UnityValue::Array(children))=obj.class.properties().get("m_Children") else {continue;};
            let expected:Vec<_>=children.iter().filter_map(pptr).filter(|id|graph.tfs.contains_key(id)).collect();
            for (index,id) in expected.iter().enumerate() {assert_eq!(graph.tfs[id].sibling_index,Some(index as u32));}
            let mut actual:Vec<_>=graph.tfs.iter().filter(|(_,tf)|tf.parent==Some(info.path_id)).map(|(id,_)|*id).collect();
            actual.sort_by_key(|id|(graph.tfs[id].sibling_index.unwrap_or(u32::MAX),*id));
            assert_eq!(actual,expected);
            let mut id_order=expected.clone();id_order.sort_unstable();
            if id_order!=expected {non_id_order+=1;}
            checked+=expected.len();
        }
        let cached:std::collections::HashMap<i64,Tf>=bincode::deserialize(&bincode::serialize(&graph.tfs).unwrap()).unwrap();
        for (id,tf) in &graph.tfs {assert_eq!(cached[id].sibling_index,tf.sibling_index);}
    }
    assert!(checked>100&&non_id_order>0,"fixture must exercise real ordering differences");
    eprintln!("serialized hierarchy: {checked} child references, {non_id_order} parents differ from PathID sort; cache roundtrip pass");
}

/// Selected prefab renderer's matrix, including the authored root transform.
/// A shared Mesh resolves its renderer inside the requested prefab. Skinned meshes
/// already carry this in their bind poses; non-skinned accessories (accessory
/// hair, bangles) need it or they render at the parent origin (e.g. an
/// accessory hair arc becomes a halo above the head).
pub fn mesh_prefab_locals(
    serialized: &SerializedFile,
    main_data: &str,
) -> std::collections::HashMap<i64, Mat4> {
    mesh_prefab_locals_in(&build_skel_graph(serialized), main_data)
}
/// Same over a transform graph built once per serialized file.
pub(crate) fn mesh_prefab_locals_in(
    graph: &SkelGraph,
    main_data: &str,
) -> std::collections::HashMap<i64, Mat4> {
    let root_tf = prefab_root_tfs(&graph,main_data).first().copied();
    let root_go = root_tf.and_then(|tf|graph.go_tf.iter().find(|(_,id)|**id==tf).map(|(go,_)|*go));
    let root_name = root_go
        .and_then(|id| graph.go_name.get(&id).cloned())
        .unwrap_or_default();
    let Some(root_go) = root_go else {
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!("ok-extract: prefab_locals main_data={main_data} root=none");
        }
        return Default::default();
    };
    if std::env::var("OK_DIAG").is_ok() {
        eprintln!(
            "ok-extract: prefab_locals main_data={main_data} root='{root_name}' mesh_go={}",
            graph.mesh_go.len()
        );
    }
    let Some(_root_tf) = graph.go_tf.get(&root_go).copied() else {
        if std::env::var("OK_DIAG").is_ok() {
            eprintln!("ok-extract: prefab_locals main_data={main_data} root='{root_name}' no_root_tf");
        }
        return Default::default();
    };
    let selected = prefab_mesh_gameobjects(&graph, main_data);
    let mut out = std::collections::HashMap::new();
    for (mesh, go) in &selected {
        let Some(tf) = graph.go_tf.get(go).copied() else {
            continue;
        };
        // Mesh GameObject world in prefab space (includes the prefab root's own
        // local transform R). The game instantiates the prefab root under the
        // accessory parent with that same R, so world = parent * local * M.
        // Returning root-relative (inv(R)*M) would drop R and, for packs whose
        // accessor root carries the scale (e.g. AA2 HEXA headbands), place the
        // mesh at ~1x instead of the authored size.
        let world = world_of_id(tf, &graph.tfs);
        out.insert(*mesh, world);
    }
    if std::env::var("OK_DIAG").is_ok() {
        eprintln!(
            "ok-extract: prefab_locals main_data={main_data} root='{root_name}' mesh_go={} out={}",
            graph.mesh_go.len(),
            out.len()
        );
        for (mesh, go) in &selected {
            let gon = graph.go_name.get(go).cloned().unwrap_or_default();
            let gl = gon.to_ascii_lowercase();
            if !(gl.contains("headband") || gl.contains("kami") || gl.contains("bangle")) {
                continue;
            }
            let mut chain = Vec::new();
            let mut id = graph.go_tf.get(go).copied();
            while let Some(t) = id {
                let Some(tf) = graph.tfs.get(&t) else { break };
                chain.push(format!(
                    "{}({:.4},{:.4},{:.4})",
                    tf.name, tf.scale[0], tf.scale[1], tf.scale[2]
                ));
                id = tf.parent;
            }
            eprintln!("ok-extract: prefabchain mesh={mesh} {gon}: {}", chain.join(" <- "));
        }
    }
    out
}

/// ChaControl.UpdateAccessoryMoveFromInfo writes addMove onto the prefab's
/// `N_move` / `N_move2` child (ChaControl.cs:2570-2579):
///   move.localPosition = addMove[j,0]*0.01; move.localEulerAngles = addMove[j,1];
///   move.localScale = addMove[j,2];
/// Returns (parentWorld, selfWorld) of that node in prefab space, so the mesh
/// transform becomes `parentWorld * newLocal * inv(selfWorld) * meshWorld`.
pub fn prefab_node_world(
    serialized: &SerializedFile,
    main_data: &str,
    node_name: &str,
) -> Option<(Mat4, Mat4)> {
    prefab_node_world_in(&build_skel_graph(serialized), main_data, node_name)
}
/// Same over a transform graph built once per serialized file.
pub(crate) fn prefab_node_world_in(
    graph: &SkelGraph,
    main_data: &str,
    node_name: &str,
) -> Option<(Mat4, Mat4)> {
    let roots = prefab_root_tfs(&graph, main_data);
    if roots.is_empty() { return None; }
    let under_root = |tf: i64| {
        let mut cur = Some(tf);
        let mut guard = 0;
        while let Some(c) = cur {
            if roots.binary_search(&c).is_ok() {
                return true;
            }
            guard += 1;
            if guard > 4096 {
                break;
            }
            cur = graph
                .tfs
                .get(&c)
                .and_then(|t| t.parent)
                .filter(|p| *p != 0 && *p != c);
        }
        false
    };
    let mut hits: Vec<i64> = graph
        .go_name
        .iter()
        .filter(|(_, n)| n.eq_ignore_ascii_case(node_name))
        .filter_map(|(go, _)| graph.go_tf.get(go).copied())
        .filter(|tf| under_root(*tf))
        .collect();
    hits.sort_unstable();
    let node_tf = *hits.first()?;
    let self_world = world_of_id(node_tf, &graph.tfs);
    let parent_world = graph
        .tfs
        .get(&node_tf)
        .and_then(|t| t.parent)
        .filter(|p| *p != 0 && *p != node_tf)
        .map(|p| world_of_id(p, &graph.tfs))
        .unwrap_or_else(ident);
    Some((parent_world, self_world))
}

fn prefab_root_tfs(graph: &SkelGraph, main_data: &str) -> Vec<i64> {
    let want = main_data.trim_start_matches("p_").to_ascii_lowercase();
    let full = main_data.to_ascii_lowercase();
    let mut roots: Vec<i64> = if let Some(gos)=graph.asset_roots.get(&full) {
        gos.iter().filter_map(|go|graph.go_tf.get(go).copied()).collect()
    } else {graph.go_name.iter()
        .filter(|(_, name)| {
            let name = name.to_ascii_lowercase();
            name == full || name == want
        })
        .filter_map(|(go, _)| graph.go_tf.get(go).copied())
        .collect()};
    roots.sort_unstable();
    roots.dedup();
    roots
}

fn transform_under_roots(graph: &SkelGraph, mut tf: i64, roots: &[i64]) -> bool {
    for _ in 0..4096 {
        if roots.binary_search(&tf).is_ok() { return true; }
        let Some(parent) = graph.tfs.get(&tf).and_then(|t| t.parent)
            .filter(|parent| *parent != 0 && *parent != tf) else { return false; };
        tf = parent;
    }
    false
}

/// One transform of a prefab (local to its parent; the root keeps its own authored local, as the game instantiates it).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabNode { pub name: String, pub parent: i32, pub pos: [f32; 3], pub rot: [f32; 4], pub scale: [f32; 3] }

/// A Light component of a prefab (Unity LightType: 0 spot, 1 directional, 2 point).
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabLight { pub node: i32, pub kind: i32, pub color: [f32; 4], pub intensity: f32, pub range: f32, pub spot_angle: f32, pub enabled: bool }

/// A prefab's transform tree (parents before children), the nodes each mesh is drawn at (several when GameObjects
/// share a Mesh; sorted by mesh, then node) and its lights:
/// Studio item child points (ItemList 子の接続先), rigid item FK and lamp items.
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct PrefabHierarchy { pub nodes: Vec<PrefabNode>, pub mesh_nodes: Vec<(i64, i32)>, pub lights: Vec<PrefabLight> }

impl PrefabHierarchy {
    /// Prefab-space matrix of every node.
    pub fn worlds(&self) -> Vec<Mat4> { self.worlds_with(&|_| None) }
    /// `worlds` with some nodes' local rotation replaced (item FK: node index -> Unity quaternion).
    pub fn worlds_with(&self, rot: &dyn Fn(usize) -> Option<[f32; 4]>) -> Vec<Mat4> {
        let mut out: Vec<Mat4> = Vec::with_capacity(self.nodes.len());
        for (i, n) in self.nodes.iter().enumerate() {
            let local = trs(n.pos, rot(i).unwrap_or(n.rot), n.scale);
            let world = if n.parent >= 0 && (n.parent as usize) < out.len() { mat_mul(&out[n.parent as usize], &local) } else { local };
            out.push(world);
        }
        out
    }
    pub fn find(&self, name: &str) -> Option<usize> { self.nodes.iter().position(|n| n.name.eq_ignore_ascii_case(name)) }
    /// Prefab-space position, rotation and (axis-wise) scale of a node.
    pub fn node_world(&self, index: usize) -> Option<([f32; 3], [f32; 4], [f32; 3])> {
        let pos = mat_pos(self.worlds().get(index)?);
        let (mut rot, mut scale, mut i) = ([0., 0., 0., 1.], [1f32; 3], index as i32);
        for _ in 0..self.nodes.len() {
            let Some(n) = self.nodes.get(i as usize) else { break; };
            rot = quat_mul(n.rot, rot);
            for a in 0..3 { scale[a] *= n.scale[a]; }
            i = n.parent;
            if i < 0 { break; }
        }
        Some((pos, rot, scale))
    }
}

/// The hierarchy of `main_data` and its GameObject -> node map (empty when the prefab is not in this file).
pub(crate) fn prefab_hierarchy_in(graph: &SkelGraph, main_data: &str) -> (PrefabHierarchy, std::collections::HashMap<i64, i32>) {
    let mut h = PrefabHierarchy::default();
    let mut go_node = std::collections::HashMap::new();
    let Some(root) = prefab_root_tfs(graph, main_data).first().copied() else { return (h, go_node); };
    let mut children: std::collections::HashMap<i64, Vec<(u32, i64)>> = std::collections::HashMap::new();
    for (id, tf) in &graph.tfs { if let Some(p) = tf.parent { children.entry(p).or_default().push((tf.sibling_index.unwrap_or(u32::MAX), *id)); } }
    for v in children.values_mut() { v.sort_unstable(); }
    let mut tf_node = std::collections::HashMap::new();
    let mut queue = std::collections::VecDeque::from([(root, -1i32)]);
    while let Some((id, parent)) = queue.pop_front() {
        let Some(tf) = graph.tfs.get(&id) else { continue; };
        if h.nodes.len() >= 65_536 { break; }
        let index = h.nodes.len() as i32;
        h.nodes.push(PrefabNode { name: tf.name.clone(), parent, pos: tf.pos, rot: tf.rot, scale: tf.scale });
        tf_node.insert(id, index);
        for (_, c) in children.get(&id).into_iter().flatten() { queue.push_back((*c, index)); }
    }
    for (go, tf) in &graph.go_tf { if let Some(n) = tf_node.get(tf) { go_node.insert(*go, *n); } }
    // Every renderer instance of a mesh (a shared Mesh drawn by several GameObjects is one entry per GameObject).
    let roots = prefab_root_tfs(graph, main_data);
    let mut mesh_nodes: Vec<(i64, i32)> = Vec::new();
    for (mesh, objects) in &graph.mesh_go {
        for go in objects {
            if !graph.go_tf.get(go).is_some_and(|tf| transform_under_roots(graph, *tf, &roots)) { continue; }
            if let Some(n) = go_node.get(go) { mesh_nodes.push((*mesh, *n)); }
        }
    }
    mesh_nodes.sort_unstable();
    mesh_nodes.dedup();
    h.mesh_nodes = mesh_nodes;
    (h, go_node)
}

fn prefab_mesh_gameobjects(graph: &SkelGraph, main_data: &str)
    -> std::collections::HashMap<i64, i64>
{
    let roots = prefab_root_tfs(graph, main_data);
    if roots.is_empty() { return Default::default(); }
    graph.mesh_go.iter().filter_map(|(mesh, objects)| {
        let mut candidates: Vec<_> = objects.iter().copied().filter(|go|
            graph.go_tf.get(go).is_some_and(|tf| transform_under_roots(graph, *tf, &roots)))
            .collect();
        candidates.sort_unstable(); candidates.dedup();
        if candidates.len() > 1 {
            eprintln!("ok-extract: multiple renderer instances for mesh={mesh} prefab={main_data}; using first selected instance");
        }
        candidates.first().map(|go| (*mesh, *go))
    }).collect()
}

#[test]
fn shared_mesh_gameobjects_are_selected_within_prefab() {
    let mut graph = SkelGraph {
        asset_roots: [("blue".into(), vec![10]), ("studio".into(), vec![20])].into(),
        tfs: Default::default(), smr_bones: Default::default(), go_name: Default::default(),
        go_tf: [(10, 100), (11, 110), (20, 200), (21, 210), (30, 300)].into(),
        mesh_go: [(410, vec![21, 30, 11]), (411, vec![30])].into(),
    };
    for (id, parent, x) in [(100,None,1.), (110,Some(100),2.),
        (200,None,10.), (210,Some(200),20.), (300,None,100.)] {
        graph.tfs.insert(id, Tf { sibling_index: None, name: id.to_string(), parent,
            pos: [x,0.,0.], rot: [0.,0.,0.,1.], scale: [1.;3] });
    }
    assert_eq!(prefab_mesh_gameobjects(&graph,"blue"), [(410,11)].into());
    assert_eq!(prefab_mesh_gameobjects(&graph,"studio"), [(410,21)].into());
    graph.mesh_go.get_mut(&410).unwrap().reverse();
    assert_eq!(prefab_mesh_gameobjects(&graph,"blue"), [(410,11)].into());
    assert_eq!(world_of_id(110,&graph.tfs)[0][3],3.);
    assert_eq!(world_of_id(210,&graph.tfs)[0][3],30.);
    assert!(prefab_mesh_gameobjects(&graph,"missing").is_empty());
}

#[cfg(test)]
#[test]
fn duplicate_prefab_names_keep_all_transform_roots() {
    let graph = SkelGraph {
        asset_roots: Default::default(),
        tfs: Default::default(),
        smr_bones: Default::default(),
        go_name: [(30, "p_AS01".to_string()), (10, "AS01".to_string()),
                  (20, "p_AS01".to_string()), (40, "unrelated".to_string())].into(),
        go_tf: [(30, 300), (10, 100), (20, 200), (40, 400)].into(),
        mesh_go: Default::default(),
    };
    assert_eq!(prefab_root_tfs(&graph, "p_AS01"), vec![100, 200, 300]);
}

#[cfg(test)]
#[test]
fn expression_hand_uses_world_position_and_rotation_under_scaled_parent() {
    let mut tfs = std::collections::HashMap::new();
    let root_rot = quat_axis_angle([0.0, 0.0, 1.0], std::f32::consts::FRAC_PI_2);
    for (id, name, parent, pos, rot, scale) in [
        (1, "root", None, [5.0, 6.0, 7.0], root_rot, [2.0, 1.0, 1.0]),
        (2, "cf_j_forearm01_L", Some(1), [0.0; 3], [0.0, 0.0, 0.0, 1.0], [1.0; 3]),
        (3, "cf_j_hand_L", Some(2), [0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0], [1.0; 3]),
        (4, "cf_j_middle01_L", Some(3), [0.0, 0.1, 0.0], [0.0, 0.0, 0.0, 1.0], [1.0; 3]),
        (5, "cf_d_hand_L", Some(2), [0.0; 3], [0.0, 0.0, 0.0, 1.0], [1.0; 3]),
        (6, "cf_d_wrist_L", Some(2), [0.0; 3], [0.0, 0.0, 0.0, 1.0], [1.0; 3]),
        (7, "cf_d_forearm02_L", Some(2), [0.0; 3], [0.0, 0.0, 0.0, 1.0], [1.0; 3]),
    ] {
        tfs.insert(id, Tf { sibling_index: None, name: name.into(), parent, pos, rot, scale });
    }
    let mut skel = Skeleton { names: Vec::new(), worlds: Vec::new(), rest: Vec::new(),
        smr_count: 7, tfs, bone_ids: (1..=7).collect(), source_mesh: 0 };
    skel.tfs.get_mut(&6).unwrap().rot = euler_deg_to_quat([15.0, 25.0, 35.0]);
    apply_expression_bone_hands(&mut skel);
    let expected_y = (0.1f32 * 100.0 * std::f32::consts::PI / 180.0).sin() * 1.5;
    let hand = gt_pos(&skel, 5);
    assert!((hand[1] - expected_y).abs() < 1e-4, "hand deformation must use world points: {hand:?}");
    assert!((hand[0] - (-0.025 + expected_y)).abs() < 1e-4);
    let wrist_euler = quat_to_euler_deg(2, gt_rot(&skel, 6));
    assert!(wrist_euler.iter().all(|v| v.is_finite()));
    assert!((wrist_euler[1] - 25.0).abs() < 1e-3 && (wrist_euler[2] - 35.0).abs() < 1e-3,
        "SetLocalRotationX must preserve the Unity Euler Y/Z axes: {wrist_euler:?}");
    // Unity's HandL branch is independent of ForearmL's middle-finger bone.
    skel.tfs.remove(&4);
    skel.tfs.get_mut(&3).unwrap().rot = quat_axis_angle([1.0, 0.0, 0.0], 20.0f32.to_radians());
    apply_expression_bone_hands(&mut skel);
    let twist_x = quat_to_euler_deg(5, gt_rot(&skel, 7))[0];
    assert!((twist_x - 9.0).abs() < 1e-3, "hand twist must survive a missing middle bone: {twist_x}");
}

#[cfg(test)]
#[test]
fn expression_elbow_uses_sine_of_whole_angle_and_independent_bones() {
    let mut tfs = std::collections::HashMap::new();
    let id = [0.0, 0.0, 0.0, 1.0];
    for (bone_id, name, rot) in [
        (1, "cf_j_forearm01_L", euler_order_to_quat(4, 0.0, 30.0, 0.0)),
        (2, "cf_s_elbo_L", id),
        (3, "cf_s_elboback_L", id),
        (4, "cf_j_forearm01_R", euler_order_to_quat(4, 0.0, 30.0, 0.0)),
        (5, "cf_s_elbo_R", id),
        (6, "cf_s_elboback_R", id),
    ] {
        tfs.insert(bone_id, Tf { sibling_index: None, name: name.into(), parent: None,
            pos: [0.0; 3], rot, scale: [1.0; 3] });
    }
    let mut skel = Skeleton { names: Vec::new(), worlds: Vec::new(), rest: Vec::new(),
        smr_count: 6, tfs, bone_ids: (1..=6).collect(), source_mesh: 0 };
    apply_expression_bone_elbows(&mut skel);
    let bend = ((30.0f32 - 1.0) * 0.006).sin() * 0.18;
    assert!((gt_pos(&skel, 2)[2] - (-0.25 + bend) * 0.1).abs() < 1e-5);
    assert!((gt_pos(&skel, 5)[2] - (-0.25 - bend) * 0.1).abs() < 1e-5);
    skel.tfs.remove(&2);
    skel.tfs.get_mut(&3).unwrap().pos = [0.0; 3];
    apply_expression_bone_elbows(&mut skel);
    assert!(gt_pos(&skel, 3)[2].abs() > 0.01,
        "back elbow must still update when front elbow is absent");
}

#[cfg(test)]
#[test]
fn expression_bust_updates_deform_and_available_collision_side() {
    let mut tfs = std::collections::HashMap::new();
    let bones = [
        (1, "cf_j_spine03", [0.0; 3], euler_order_to_quat(5, 20.0, 10.0, 5.0), [1.0; 3]),
        (2, "cf_d_bust00", [0.5, 0.0, 0.0], [0.0, 0.0, 0.0, 1.0], [1.0; 3]),
        (3, "cf_j_bust02_L", [0.2, 0.3, 0.4], [0.0, 0.0, 0.0, 1.0], [1.2, 0.8, 1.0]),
        (4, "cf_d_hit_bust_L", [0.0; 3], [0.0, 0.0, 0.0, 1.0], [1.0; 3]),
    ];
    for (id, name, pos, rot, scale) in bones {
        tfs.insert(id, Tf { sibling_index: None, name: name.into(), parent: None, pos, rot, scale });
    }
    let mut skel = Skeleton { names: Vec::new(), worlds: Vec::new(), rest: Vec::new(),
        smr_count: 4, tfs, bone_ids: (1..=4).collect(), source_mesh: 0 };
    apply_expression_bone_bust(&mut skel);
    let e = quat_to_euler_deg(5, gt_rot(&skel, 2));
    for (actual, expected) in e.into_iter().zip([-7.0, -3.5, -1.75]) {
        assert!((actual - expected).abs() < 1e-3, "bust rotation {e:?}");
    }
    let p = gt_pos(&skel, 2);
    assert!((p[0] - 0.5).abs() < 1e-6);
    assert!((p[1] + 0.0021).abs() < 1e-5);
    assert!((p[2] - 0.008).abs() < 1e-5);
    let hit = gt_pos(&skel, 4);
    let scale = gt_scale(&skel, 4);
    assert!((hit[0] + 0.1).abs() < 1e-6 && (hit[1] + 0.15).abs() < 1e-6 && (hit[2] + 0.1).abs() < 1e-6);
    assert!((scale[0] - 0.9).abs() < 1e-6 && (scale[1] - 1.1).abs() < 1e-6 && (scale[2] - 1.0).abs() < 1e-6);
}

#[cfg(test)]
#[test]
fn expression_leg_twists_read_their_own_source_bones() {
    let mut tfs = std::collections::HashMap::new();
    let identity = [0.0, 0.0, 0.0, 1.0];
    let bones = [
        (1, "cf_j_thigh00_L", euler_order_to_quat(5, 0.0, 40.0, 0.0)),
        (2, "cf_j_leg01_L", euler_order_to_quat(5, 30.0, -25.0, 0.0)),
        (3, "cf_j_leg03_L", euler_order_to_quat(5, 0.0, 20.0, 0.0)),
        (4, "cf_d_thigh01_L", identity),
        (5, "cf_d_thigh02_L", identity),
        (6, "cf_d_thigh03_L", identity),
        (7, "cf_d_kneeF_L", identity),
        (8, "cf_s_kneeB_L", identity),
        (9, "cf_d_leg02_L", identity),
    ];
    for (id, name, rot) in bones {
        tfs.insert(id, Tf { sibling_index: None, name: name.into(), parent: None,
            pos: [0.0; 3], rot, scale: [1.0; 3] });
    }
    let mut skel = Skeleton { names: Vec::new(), worlds: Vec::new(), rest: Vec::new(),
        smr_count: 9, tfs, bone_ids: (1..=9).collect(), source_mesh: 0 };
    apply_expression_bone_leg_twists(&mut skel);
    for (bone, expected) in [(4, -30.0), (5, -20.0), (6, -10.0)] {
        let actual = quat_to_euler_deg(5, gt_rot(&skel, bone))[1];
        assert!((actual - expected).abs() < 1e-3, "thigh deform {bone} y={actual}");
    }
    let knee = quat_to_euler_deg(5, gt_rot(&skel, 8));
    assert!((knee[0] + 15.0).abs() < 1e-3 && (knee[2] - 3.0).abs() < 1e-3);
    let front = gt_pos(&skel, 7);
    let back = gt_pos(&skel, 8);
    assert!((front[1] - ((0.36f32).sin()*1.8 - (0.1005f32).tan()*4.5)*0.1).abs() < 1e-6);
    assert!((back[1] - (-30.0*0.048 + (0.3f32).sin()*1.1)*0.1).abs() < 1e-6);
    assert!((quat_to_euler_deg(5, gt_rot(&skel, 9))[1] - 10.0).abs() < 1e-3);
    skel.tfs.remove(&2);
    skel.tfs.get_mut(&4).unwrap().rot = identity;
    apply_expression_bone_leg_twists(&mut skel);
    assert!((quat_to_euler_deg(5, gt_rot(&skel, 4))[1] + 30.0).abs() < 1e-3,
        "thigh twist must not require a knee source");
}

#[cfg(test)]
#[test]
fn expression_leg_calculation_is_invariant_under_character_world_motion() {
    let identity = [0.0, 0.0, 0.0, 1.0];
    let mut tfs = std::collections::HashMap::new();
    for (id, name, parent, pos, rot) in [
        (1, "cf_j_root", None, [0.0; 3], identity),
        (2, "cf_n_height", Some(1), [0.0; 3], identity),
        (3, "cf_j_waist02", Some(2), [0.0, 1.2, 0.0], identity),
        (4, "cf_j_thigh00_L", Some(3), [-0.08, 0.0, 0.0], euler_order_to_quat(5, 0.0, 0.0, 120.0)),
        (5, "cf_j_leg01_L", Some(4), [0.0, -0.5, 0.0], identity),
        (6, "cf_s_leg_L", Some(3), [0.0; 3], identity),
        (7, "cf_d_siri_L", Some(3), [0.0; 3], identity),
    ] {
        tfs.insert(id, Tf { sibling_index: None, name: name.into(), parent, pos, rot, scale: [1.0; 3] });
    }
    tfs.get_mut(&2).unwrap().scale = [0.84; 3];
    let neutral = Skeleton { names: Vec::new(), worlds: Vec::new(), rest: Vec::new(),
        smr_count: 7, tfs, bone_ids: (1..=7).collect(), source_mesh: 0 };
    let mut base = neutral.clone();
    let mut moved = neutral;
    moved.tfs.get_mut(&1).unwrap().pos = [0.3, 3.0, -0.8];
    moved.tfs.get_mut(&1).unwrap().rot = euler_order_to_quat(5, 13.0, 27.0, -10.0);
    apply_expression_bone_leg_positions(&mut base);
    apply_expression_bone_leg_positions(&mut moved);
    for id in [6, 7] {
        for (actual, expected) in moved.tfs[&id].pos.iter().zip(base.tfs[&id].pos) {
            assert!((actual - expected).abs() < 1e-5, "world motion changed helper {id}");
        }
        for (actual, expected) in moved.tfs[&id].scale.iter().zip(base.tfs[&id].scale) {
            assert!((actual - expected).abs() < 1e-5, "world motion changed helper scale {id}");
        }
    }
    assert!(base.tfs[&6].scale[1] > 1.9, "fixture must activate the raised-leg branch");
}

#[cfg(test)]
#[test]
fn expression_acs_null_moves_neck_anchor_from_head_rotation() {
    let mut tfs = std::collections::HashMap::new();
    tfs.insert(1, Tf { sibling_index: None, name: "cf_j_head".into(), parent: None,
        pos: [0.1, 0.2, 0.3], rot: euler_order_to_quat(5, 20.0, 0.0, 30.0), scale: [1.0; 3] });
    tfs.insert(2, Tf { sibling_index: None, name: "a_n_neck".into(), parent: None,
        pos: [0.0, 0.038, 0.0], rot: [0.0, 0.0, 0.0, 1.0], scale: [1.0; 3] });
    let mut skel = Skeleton { names: Vec::new(), worlds: Vec::new(), rest: Vec::new(),
        smr_count: 2, tfs, bone_ids: vec![1, 2], source_mesh: 0 };
    let head_before = gt_pos(&skel, 1);
    apply_expression_bone_acs_null(&mut skel);
    let neck = gt_pos(&skel, 2);
    assert!((neck[0] - 0.006).abs() < 1e-5, "neck x={neck:?}");
    assert!((neck[1] - 0.038).abs() < 1e-6);
    assert!((neck[2] + 0.012).abs() < 1e-5, "neck z={neck:?}");
    assert_eq!(gt_pos(&skel, 1), head_before, "AcsNull must not move the head");
}

/// The two accessory move nodes can be siblings. Only a node on a renderer's
/// own ancestor chain changes that renderer when ChaControl writes addMove.
/// Returning both prefab nodes for every mesh rotates/translates the left and
/// right pieces twice (for example the two fox ears in one accessory slot).
pub fn mesh_prefab_move_nodes(
    serialized: &SerializedFile,
    main_data: &str,
) -> std::collections::HashMap<i64, [Option<(Mat4, Mat4)>; 2]> {
    mesh_prefab_move_nodes_in(&build_skel_graph(serialized), main_data)
}
/// Same over a transform graph built once per serialized file.
pub(crate) fn mesh_prefab_move_nodes_in(
    graph: &SkelGraph,
    main_data: &str,
) -> std::collections::HashMap<i64, [Option<(Mat4, Mat4)>; 2]> {
    let roots = prefab_root_tfs(&graph, main_data);
    if roots.is_empty() { return Default::default(); }
    let mut out = std::collections::HashMap::new();
    for (mesh, go) in &prefab_mesh_gameobjects(&graph, main_data) {
        let mut moves = [None, None];
        let mut reached_root = false;
        let mut scan = |start: i64| {
            let mut cur = Some(start);
            for _ in 0..128 {
                let Some(id) = cur else { break };
                if roots.binary_search(&id).is_ok() { reached_root = true; break; }
                let Some(tf) = graph.tfs.get(&id) else { break };
                let idx = if tf.name.eq_ignore_ascii_case("N_move") {
                    Some(0)
                } else if tf.name.eq_ignore_ascii_case("N_move2") {
                    Some(1)
                } else { None };
                if let Some(idx) = idx {
                    let parent = tf.parent
                        .filter(|p| *p != 0 && *p != id)
                        .map(|p| world_of_id(p, &graph.tfs))
                        .unwrap_or_else(ident);
                    moves[idx] = Some((parent, world_of_id(id, &graph.tfs)));
                }
                cur = tf.parent.filter(|p| *p != 0 && *p != id);
            }
        };
        if let Some(&tf) = graph.go_tf.get(go) { scan(tf); }
        // An SMR's GameObject can sit beside N_move while its bones are below
        // that node. Follow the skinning bones as well as the renderer object.
        if let Some(groups) = graph.smr_bones.get(mesh) {
            for bone in groups.iter().flat_map(|g| g.iter()) {
                if transform_under_roots(&graph, *bone, &roots) { scan(*bone); }
            }
        }
        if reached_root { out.insert(*mesh, moves); }
    }
    out
}

fn skeleton_from_graph(graph: &SkelGraph, mesh_path_id: i64) -> Option<Skeleton> {
    let bones = graph
        .smr_bones
        .get(&mesh_path_id)?
        .iter()
        .max_by_key(|b| b.len())?
        .clone();
    if bones.is_empty() {
        return None;
    }
    let mut tfs = graph.tfs.clone();
    {
        let mut root = bones[0];
        loop {
            let Some(tf) = tfs.get(&root) else { break };
            match tf.parent {
                Some(p) if p != 0 && p != root => root = p,
                _ => break,
            }
        }
        let keep: std::collections::HashSet<i64> =
            subtree_ids(root, &tfs).into_iter().collect();
        tfs.retain(|id, _| keep.contains(id));
    }
    let mut rest_cache = std::collections::HashMap::new();
    let rest: Vec<Mat4> = bones.iter().map(|id| world_of(*id, &tfs, &mut rest_cache)).collect();
    let mut cache = std::collections::HashMap::new();
    let mut names = Vec::new();
    let mut worlds = Vec::new();
    for id in &bones {
        names.push(tfs.get(id).map(|t| t.name.clone()).unwrap_or_default());
        worlds.push(world_of(*id, &tfs, &mut cache));
    }
    let smr_count = names.len();
    Some(Skeleton {
        names,
        worlds,
        rest,
        smr_count,
        tfs,
        bone_ids: bones,
        source_mesh: mesh_path_id,
    })
}

/// All transforms of a serialized file as one skeleton, without pruning to an
/// SMR's subtree. The game's head prefab carries bones that no head mesh
/// references (cf_J_hairF_top / cf_J_hairB_top ...); hair prefabs are remapped
/// onto those bones (`LoadCharaFbxData` copyWeights), and they carry the head
/// shape scale, so hair must skin against the full head tree, not the 45-bone
/// face SMR skeleton.
pub fn skeleton_full_tree(serialized: &SerializedFile) -> Option<Skeleton> {
    let graph = build_skel_graph(serialized);
    if graph.tfs.is_empty() {
        return None;
    }
    let mut bone_ids: Vec<i64> = graph.tfs.keys().copied().collect();
    bone_ids.sort_unstable();
    let mut cache = std::collections::HashMap::new();
    let mut names = Vec::with_capacity(bone_ids.len());
    let mut worlds = Vec::with_capacity(bone_ids.len());
    for id in &bone_ids {
        names.push(graph.tfs.get(id).map(|t| t.name.clone()).unwrap_or_default());
        worlds.push(world_of(*id, &graph.tfs, &mut cache));
    }
    let rest = worlds.clone();
    Some(Skeleton {
        names,
        worlds,
        rest,
        smr_count: 0,
        tfs: graph.tfs,
        bone_ids,
        source_mesh: 0,
    })
}

/// Skeleton from an armature prefab's GameObject subtree: every Transform whose
/// ancestor chain reaches the GameObject named `prefab_name`.
///
/// StudioEditor instantiates the armature prefabs `p_cf_body_bone` /
/// `p_cf_head_bone` and rebinds the body/head/hair/accessory meshes to them
/// (`KoikatsuCharacterAssembler.cs:227-274`). Those prefabs have no SMR, so
/// `skeleton_from_graph` (which starts from an SMR bone list) cannot build them;
/// this walks the raw transform tree instead.
pub fn skeleton_for_prefab(serialized: &SerializedFile, prefab_name: &str) -> Option<Skeleton> {
    let graph = build_skel_graph(serialized);
    let root_go = graph
        .go_name
        .iter()
        .find(|(_, n)| n.eq_ignore_ascii_case(prefab_name))
        .map(|(id, _)| *id)?;
    let root_tf = graph.go_tf.get(&root_go).copied()?;
    let mut bone_ids: Vec<i64> = graph
        .tfs
        .keys()
        .copied()
        .filter(|id| {
            let mut cur = Some(*id);
            let mut guard = 0;
            while let Some(c) = cur {
                if c == root_tf {
                    return true;
                }
                guard += 1;
                if guard > 4096 {
                    break;
                }
                cur = graph
                    .tfs
                    .get(&c)
                    .and_then(|t| t.parent)
                    .filter(|p| *p != 0 && *p != c);
            }
            false
        })
        .collect();
    if bone_ids.is_empty() {
        return None;
    }
    bone_ids.sort_unstable();
    let mut cache = std::collections::HashMap::new();
    let mut names = Vec::with_capacity(bone_ids.len());
    let mut worlds = Vec::with_capacity(bone_ids.len());
    for id in &bone_ids {
        names.push(graph.tfs.get(id).map(|t| t.name.clone()).unwrap_or_default());
        worlds.push(world_of(*id, &graph.tfs, &mut cache));
    }
    let rest = worlds.clone();
    // Keep only the prefab subtree transforms; otherwise `include_tree_bones`
    // (and root walks) would pull in the whole serialized file's transform tree.
    let keep: std::collections::HashSet<i64> = bone_ids.iter().copied().collect();
    let tfs: std::collections::HashMap<i64, Tf> = graph
        .tfs
        .into_iter()
        .filter(|(id, _)| keep.contains(id))
        .collect();
    Some(Skeleton {
        names,
        worlds,
        rest,
        smr_count: 0,
        tfs,
        bone_ids,
        source_mesh: 0,
    })
}

pub fn skeleton_from_serialized(
    serialized: &SerializedFile,
    mesh_path_id: i64,
) -> Option<Skeleton> {
    skeleton_from_graph(&build_skel_graph(serialized), mesh_path_id)
}

pub fn skeletons_from_serialized(
    serialized: &SerializedFile,
    mesh_ids: &[i64],
) -> std::collections::HashMap<i64, Skeleton> {
    let graph = build_skel_graph(serialized);
    let mut out = std::collections::HashMap::new();
    for id in mesh_ids {
        if let Some(s) = skeleton_from_graph(&graph, *id) {
            out.insert(*id, s);
        }
    }
    out
}

pub fn subtree_ids_pub(root: i64, tfs: &std::collections::HashMap<i64, Tf>) -> Vec<i64> {
    subtree_ids(root, tfs)
}

fn subtree_ids(root: i64, tfs: &std::collections::HashMap<i64, Tf>) -> Vec<i64> {
    let mut kids: std::collections::HashMap<i64, Vec<i64>> = std::collections::HashMap::new();
    for (id, tf) in tfs {
        if let Some(p) = tf.parent {
            kids.entry(p).or_default().push(*id);
        }
    }
    let mut out = Vec::new();
    let mut stack = vec![root];
    while let Some(id) = stack.pop() {
        out.push(id);
        if let Some(cs) = kids.get(&id) {
            stack.extend(cs);
        }
    }
    out
}

pub fn animator_root_id(skel: &Skeleton) -> Option<i64> {
    animator_tree(skel).map(|(root,_)|root)
}
pub(crate) fn animator_tree(skel:&Skeleton)->Option<(i64,Vec<i64>)> {
    let roots:Vec<i64>=skel.tfs.iter().filter(|(_,t)|t.name=="cf_j_root"||t.name=="cf_J_N_FaceRoot"||t.name=="cf_J_FaceRoot").map(|(id,_)|*id).collect();
    if roots.is_empty(){return None;}
    let mut children:std::collections::HashMap<i64,Vec<i64>>=std::collections::HashMap::new();
    for(id,tf)in &skel.tfs {if let Some(parent)=tf.parent {children.entry(parent).or_default().push(*id);}}
    let mut best:Option<(i64,Vec<i64>)>=None;
    for root in roots {let mut tree=Vec::new();let mut stack=vec![root];while let Some(id)=stack.pop(){tree.push(id);if let Some(kids)=children.get(&id){stack.extend(kids);}}
        // Stable ascending sort followed by last selects the last equal size.
        if best.as_ref().is_none_or(|(_,previous)|tree.len()>=previous.len()){best=Some((root,tree));}
    }
    best
}
#[cfg(test)]
fn animator_root_id_oracle(skel: &Skeleton) -> Option<i64> {
    let mut roots: Vec<i64> = skel
        .tfs
        .iter()
        .filter(|(_, t)| t.name == "cf_j_root" || t.name == "cf_J_N_FaceRoot" || t.name == "cf_J_FaceRoot")
        .map(|(id, _)| *id)
        .collect();
    if roots.is_empty() {
        return None;
    }
    roots.sort_by_key(|r| subtree_ids(*r, &skel.tfs).len());
    roots.pop()
}
#[cfg(test)]
#[test]
fn animator_tree_and_assigned_publication_match_original_with_ties_and_reparenting(){
    let mut skel=Skeleton{names:vec![],worlds:vec![],rest:vec![],smr_count:6,tfs:std::collections::HashMap::new(),bone_ids:vec![2,3,6,7,10,99],source_mesh:-913};
    for(id,name,parent)in [(1,"cf_j_root",None),(2,"bone",Some(1)),(3,"bone",Some(1)),(5,"cf_J_N_FaceRoot",None),(6,"bone",Some(5)),(7,"leaf",Some(5)),(9,"cf_J_FaceRoot",None),(10,"bone",Some(9))]{
        skel.tfs.insert(id,Tf{name:name.into(),parent,sibling_index:None,pos:[id as f32*0.1,-0.2,0.3],rot:[0.1,0.2,0.3,0.9],scale:if id==3{[-0.7,0.0,1.3]}else{[0.8,1.2,0.9]}});
    }
    for parent in [Some(1),Some(5),Some(9),None]{
        skel.tfs.get_mut(&7).unwrap().parent=parent;
        assert_eq!(animator_root_id(&skel),animator_root_id_oracle(&skel));
        let mut oracle=skel.clone();let mut optimized=skel.clone();assigned_weights_remap_oracle(&mut oracle);assigned_weights_remap(&mut optimized);
        assert_eq!(oracle.bone_ids,optimized.bone_ids);assert_eq!(oracle.names,optimized.names);assert_eq!(oracle.rest,optimized.rest);assert_eq!(oracle.worlds,optimized.worlds);
    }
}

/// ChaControl.ChangeHead: `CommonLib.CopySameNameTransform(objHeadBone, objHead)`
/// (CommonLib.cs:52-70) iterates the head bone armature's (`p_cf_head_bone`)
/// transforms and copies `localPosition/localRotation/localScale` from any
/// same-named transform of the head mesh prefab (`p_cf_head_00`). The two
/// prefabs author the attach nodes differently (`a_n_head` is [0,0.1806,0.1260]
/// on the armature but [0,0.1475,0.1100] on the mesh), so without this the hair
/// accessories attach ~0.033 high / ~0.016 forward. Returns the number of
/// transforms overwritten.
pub fn copy_same_name_transform(dst: &mut Skeleton, src: &Skeleton) -> usize {
    use std::collections::HashMap;
    // FindAssist keys by name; pick the lowest path id when a name repeats so
    // the result is deterministic.
    let mut src_ids: Vec<i64> = src.tfs.keys().copied().collect();
    src_ids.sort_unstable();
    let mut src_by_name: HashMap<&str, &Tf> = HashMap::new();
    for id in src_ids {
        if let Some(tf) = src.tfs.get(&id) {
            src_by_name.entry(tf.name.as_str()).or_insert(tf);
        }
    }
    let mut changed = 0usize;
    let mut cache = HashMap::new();
    for id in dst.bone_ids.iter().copied().collect::<Vec<_>>() {
        let Some(s) = dst.tfs.get(&id).and_then(|t| src_by_name.get(t.name.as_str()).copied()) else {
            continue;
        };
        let Some(tf) = dst.tfs.get_mut(&id) else { continue };
        tf.pos = s.pos;
        tf.rot = s.rot;
        tf.scale = s.scale;
        changed += 1;
    }
    if changed > 0 {
        dst.rest = dst
            .bone_ids
            .iter()
            .map(|id| world_of(*id, &dst.tfs, &mut cache))
            .collect();
        dst.rebuild_worlds();
    }
    changed
}

/// ChaControl: aaWeightsBody.CreateBoneList(p_cf_body_bone) then
/// AssignedWeightsAndSetBounds(objBody, "cf_j_root") remaps SMR.bones by name
/// onto the animator skeleton and destroys the mesh prefab's own cf_j_root.
pub fn assigned_weights_remap(skel: &mut Skeleton) {
    assigned_weights_remap_impl(skel,true);
}
#[cfg(test)]
pub(crate) fn assigned_weights_remap_oracle(skel:&mut Skeleton){assigned_weights_remap_impl(skel,false);}
fn assigned_weights_remap_impl(skel:&mut Skeleton,reuse_worlds:bool) {
    let selected=if reuse_worlds {animator_tree(skel)}else{
    let mut roots: Vec<i64> = skel
        .tfs
        .iter()
        .filter(|(_, t)| t.name == "cf_j_root" || t.name == "cf_J_N_FaceRoot" || t.name == "cf_J_FaceRoot")
        .map(|(id, _)| *id)
        .collect();
    if roots.is_empty() {
        return;
    }
    roots.sort_by_key(|r| subtree_ids(*r, &skel.tfs).len());
    let root = *roots.last().unwrap();
    let tree = subtree_ids(root, &skel.tfs);
    Some((root,tree))};
    let Some((root,tree))=selected else{return;};
    let mut dict: std::collections::HashMap<String, i64> = std::collections::HashMap::new();
    for id in &tree {
        if let Some(tf) = skel.tfs.get(id) {
            dict.entry(tf.name.clone()).or_insert(*id);
        }
    }
    let mut remapped = 0usize;
    for id in &mut skel.bone_ids {
        let Some(tf) = skel.tfs.get(id) else { continue };
        if let Some(&new_id) = dict.get(&tf.name) {
            if new_id != *id {
                *id = new_id;
                remapped += 1;
            }
        }
    }
    let mut rest_cache = std::collections::HashMap::new();
    skel.rest = skel
        .bone_ids
        .iter()
        .map(|id| world_of(*id, &skel.tfs, &mut rest_cache))
        .collect();
    if reuse_worlds {
        skel.names.resize_with(skel.bone_ids.len(),String::new);
        for(index,id)in skel.bone_ids.iter().enumerate(){if let Some(tf)=skel.tfs.get(id){skel.names[index].clone_from(&tf.name);}else{skel.names[index].clear();}}
        skel.worlds.clone_from(&skel.rest);
    }else{skel.rebuild_worlds();}
    if remapped > 0 {
        eprintln!(
            "ok-extract: AssignedWeights root={} tree={} remapped={remapped} smr={}",
            skel.tfs.get(&root).map(|t| t.name.as_str()).unwrap_or("?"),
            tree.len(),
            skel.bone_ids.len()
        );
    }
}

/// Some illusion meshes contain coincident duplicate vertices whose bone
/// indices are swapped/implausible (a tiny minority). Those explode under
/// skinning. Detect vertices whose dominant bone's rest position is far from
/// the vertex's rest-skinned position and copy weights from the nearest sane
/// vertex (usually the coincident duplicate).
pub fn repair_weights(mesh: &mut ExtractedMesh, skel: &Skeleton) {
    let n = mesh.positions.len();
    if n == 0
        || mesh.weights.len() != n
        || mesh.bone_idx.len() != n
        || skel.rest.is_empty()
        || mesh.bind_poses.is_empty()
    {
        return;
    }
    let nb = skel.rest.len().min(mesh.bind_poses.len());
    if nb == 0 {
        return;
    }
    let Some(rest_pos) = skin_positions(mesh, &skel.rest[..nb], &mesh.bind_poses[..nb]) else {
        return;
    };
    if std::env::var("OK_DIAG").is_ok() {
        let mut maxd = 0f32;
        let mut sum = 0f64;
        let mut nbig = 0usize;
        for i in 0..n {
            let d = (0..3)
                .map(|k| (rest_pos[i][k] - mesh.positions[i][k]).powi(2))
                .sum::<f32>()
                .sqrt();
            maxd = maxd.max(d);
            sum += d as f64;
            if d > 0.05 {
                nbig += 1;
            }
        }
        eprintln!(
            "ok-extract: DIAG rest-vs-raw {} max={maxd:.4} mean={:.5} n>0.05={nbig}/{n}",
            mesh.name,
            sum / n as f64
        );
        for i in [0usize, n / 4, n / 2, (3 * n) / 4, n - 1] {
            eprintln!(
                "ok-extract: DIAG   v{i} raw={:?} rest={:?}",
                mesh.positions[i], rest_pos[i]
            );
        }
    }
    let bone_pos = |bi: usize| -> [f32; 3] {
        skel
            .rest
            .get(bi)
            .map(|m| [m[0][3], m[1][3], m[2][3]])
            .unwrap_or([0.0; 3])
    };
    let mut bad = vec![false; n];
    for i in 0..n {
        let w = mesh.weights[i];
        let b = mesh.bone_idx[i];
        let rp = rest_pos[i];
        let mut any = false;
        let mut far = false;
        for k in 0..4 {
            if w[k] <= 0.1 {
                continue;
            }
            any = true;
            let bp = bone_pos(b[k] as usize);
            let d = ((rp[0] - bp[0]).powi(2) + (rp[1] - bp[1]).powi(2) + (rp[2] - bp[2]).powi(2))
                .sqrt();
            if d > 0.35 {
                far = true;
            }
        }
        if !any || far {
            bad[i] = true;
        }
    }
    let bad_count = bad.iter().filter(|x| **x).count();
    if bad_count == 0 {
        return;
    }
    for i in 0..n {
        if !bad[i] {
            continue;
        }
        let mut best_d = f32::MAX;
        let mut best_j = usize::MAX;
        for j in 0..n {
            if bad[j] {
                continue;
            }
            let d = (rest_pos[i][0] - rest_pos[j][0]).powi(2)
                + (rest_pos[i][1] - rest_pos[j][1]).powi(2)
                + (rest_pos[i][2] - rest_pos[j][2]).powi(2);
            if d < best_d {
                best_d = d;
                best_j = j;
            }
        }
        if best_j != usize::MAX {
            mesh.weights[i] = mesh.weights[best_j];
            mesh.bone_idx[i] = mesh.bone_idx[best_j];
        }
    }
    eprintln!(
        "ok-extract: repaired {bad_count} outlier skin vertices in {}",
        mesh.name
    );
}

/// A correctly skinned vertex only blends bones that lie near it in the bind
/// pose. A small influence from a bone far from the vertex (relative to the
/// vertex's nearest influence) is a skin/decode artefact - e.g. a thigh bone
/// bleeding onto a hand vertex, which explodes when the arm is posed. Drop
/// such influences and renormalise, rather than copying a neighbour's weights.
pub fn prune_far_weights(mesh: &mut ExtractedMesh, skel: &Skeleton) {
    let n = mesh.positions.len();
    if n == 0
        || mesh.weights.len() != n
        || mesh.bone_idx.len() != n
        || skel.rest.is_empty()
        || mesh.bind_poses.is_empty()
    {
        return;
    }
    let nb = skel.rest.len().min(mesh.bind_poses.len());
    if nb == 0 {
        return;
    }
    let Some(rest_pos) = skin_positions(mesh, &skel.rest[..nb], &mesh.bind_poses[..nb]) else {
        return;
    };
    let mut pruned = 0usize;
    for i in 0..n {
        let rp = rest_pos[i];
        let b = mesh.bone_idx[i];
        let w = mesh.weights[i];
        let dist = |k: usize| -> f32 {
            skel
                .rest
                .get(b[k] as usize)
                .map(|m| {
                    ((rp[0] - m[0][3]).powi(2) + (rp[1] - m[1][3]).powi(2) + (rp[2] - m[2][3]).powi(2))
                        .sqrt()
                })
                .unwrap_or(f32::MAX)
        };
        let mut min_d = f32::MAX;
        for k in 0..4 {
            if w[k] > 0.02 {
                min_d = min_d.min(dist(k));
            }
        }
        let mut nw = w;
        let mut changed = false;
        for k in 0..4 {
            if nw[k] > 0.02 {
                let d = dist(k);
                if d > 0.20 && d > min_d + 0.12 {
                    nw[k] = 0.0;
                    changed = true;
                }
            }
        }
        if changed {
            let s: f32 = nw.iter().sum();
            if s > 1e-4 {
                for v in nw.iter_mut() {
                    *v /= s;
                }
                mesh.weights[i] = nw;
                pruned += 1;
            }
        }
    }
    if pruned > 0 {
        eprintln!("ok-extract: pruned far influences on {pruned} vertices in {}", mesh.name);
    }
}

fn add_weight(map: &mut Vec<(u16, f32)>, b: u16, w: f32) {
    if w <= 1e-5 {
        return;
    }
    for e in map.iter_mut() {
        if e.0 == b {
            e.1 += w;
            return;
        }
    }
    map.push((b, w));
}

/// Laplacian weight smoothing: blends each vertex's bone weights with its
/// neighbours' so shoulders / fingers deform smoothly instead of creasing.
pub fn smooth_weights(mesh: &mut ExtractedMesh, iters: usize) {
    if iters == 0 { return; }
    let n = mesh.positions.len();
    if n == 0 || mesh.weights.len() != n || mesh.bone_idx.len() != n {
        return;
    }
    let mut nbrs: Vec<Vec<u32>> = vec![Vec::new(); n];
    for tri in mesh.indices.chunks(3) {
        let (a, b, c) = (tri[0], tri[1], tri[2]);
        if (a as usize) >= n || (b as usize) >= n || (c as usize) >= n {
            continue;
        }
        nbrs[a as usize].push(b);
        nbrs[a as usize].push(c);
        nbrs[b as usize].push(a);
        nbrs[b as usize].push(c);
        nbrs[c as usize].push(a);
        nbrs[c as usize].push(b);
    }
    for _ in 0..iters {
        let mut new_w = vec![[0.0f32; 4]; n];
        let mut new_b = vec![[0u16; 4]; n];
        for i in 0..n {
            let mut map: Vec<(u16, f32)> = Vec::new();
            for k in 0..4 {
                add_weight(&mut map, mesh.bone_idx[i][k], mesh.weights[i][k] * 0.55);
            }
            let ns = &nbrs[i];
            if !ns.is_empty() {
                let f = 0.45 / ns.len() as f32;
                for &j in ns {
                    let jj = j as usize;
                    for k in 0..4 {
                        add_weight(&mut map, mesh.bone_idx[jj][k], mesh.weights[jj][k] * f);
                    }
                }
            }
            if map.is_empty() {
                new_w[i] = mesh.weights[i];
                new_b[i] = mesh.bone_idx[i];
                continue;
            }
            map.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal));
            map.truncate(4);
            let sum: f32 = map.iter().map(|e| e.1).sum();
            if sum <= 1e-6 {
                new_w[i] = mesh.weights[i];
                new_b[i] = mesh.bone_idx[i];
                continue;
            }
            for k in 0..4 {
                if k < map.len() {
                    new_b[i][k] = map[k].0;
                    new_w[i][k] = map[k].1 / sum;
                } else {
                    new_b[i][k] = 0;
                    new_w[i][k] = 0.0;
                }
            }
        }
        mesh.weights = new_w;
        mesh.bone_idx = new_b;
    }
}

/// Light Laplacian relaxation of the *posed* vertex positions. Removes small
/// self-intersection creases (shoulder/armpit) without changing the design.
pub fn relax_positions(pos: &mut [[f32; 3]], indices: &[u32], iters: usize, factor: f32) {
    let n = pos.len();
    if n == 0 {
        return;
    }
    for _ in 0..iters {
        let mut sum = vec![[0f64; 3]; n];
        let mut cnt = vec![0u32; n];
        for tri in indices.chunks(3) {
            let (a, b, c) = (tri[0] as usize, tri[1] as usize, tri[2] as usize);
            if a >= n || b >= n || c >= n {
                continue;
            }
            for &(u, v) in &[(a, b), (b, c), (c, a)] {
                sum[u][0] += pos[v][0] as f64;
                sum[u][1] += pos[v][1] as f64;
                sum[u][2] += pos[v][2] as f64;
                cnt[u] += 1;
                sum[v][0] += pos[u][0] as f64;
                sum[v][1] += pos[u][1] as f64;
                sum[v][2] += pos[u][2] as f64;
                cnt[v] += 1;
            }
        }
        let f0 = 1.0 - factor as f64;
        let f1 = factor as f64;
        for i in 0..n {
            if cnt[i] == 0 {
                continue;
            }
            let c = cnt[i] as f64;
            pos[i][0] = (pos[i][0] as f64 * f0 + sum[i][0] / c * f1) as f32;
            pos[i][1] = (pos[i][1] as f64 * f0 + sum[i][1] / c * f1) as f32;
            pos[i][2] = (pos[i][2] as f64 * f0 + sum[i][2] / c * f1) as f32;
        }
    }
}

/// After posing, a few vertices decode as blended between unrelated bones
/// (e.g. half fingertip, half leg) and fly off as spikes. A vertex whose
/// dominant bone is far from the vertex in the posed skeleton is suspect;
/// copy weights from the neighbour whose bone is closest to it.
pub fn repair_posed_spikes(
    mesh: &mut ExtractedMesh,
    posed: &[Mat4],
    binds: &[Mat4],
) -> Option<Vec<[f32; 3]>> {
    let n = mesh.positions.len();
    if n == 0 || mesh.indices.len() < 3 || posed.is_empty() {
        return None;
    }
    let nb = posed.len();
    let vdist = |a: [f32; 3], b: [f32; 3]| -> f32 {
        ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2) + (a[2] - b[2]).powi(2)).sqrt()
    };
    let mut pos = skin_positions(mesh, posed, binds)?;
    let mut fixed_total = 0usize;
    for _round in 0..4 {
        let mut spread = vec![0f32; n];
        let mut single_far = vec![false; n];
        for i in 0..n {
            let w = mesh.weights[i];
            let b = mesh.bone_idx[i];
            let mut ps: Vec<[f32; 3]> = Vec::new();
            let mut bad_idx = false;
            for k in 0..4 {
                if w[k] <= 0.1 {
                    continue;
                }
                let bi = b[k] as usize;
                if bi >= nb {
                    bad_idx = true;
                    continue;
                }
                ps.push([posed[bi][0][3], posed[bi][1][3], posed[bi][2][3]]);
            }
            if bad_idx || ps.is_empty() {
                single_far[i] = true;
                spread[i] = 10.0;
                continue;
            }
            let mut sp = 0f32;
            for a in 0..ps.len() {
                for c in (a + 1)..ps.len() {
                    sp = sp.max(vdist(ps[a], ps[c]));
                }
            }
            spread[i] = sp;
            if ps.len() == 1 && vdist(pos[i], ps[0]) > 0.4 {
                single_far[i] = true;
            }
        }
        let mut bad = vec![false; n];
        for i in 0..n {
            if single_far[i] || spread[i] > 0.5 {
                bad[i] = true;
            }
        }
        let bad_count = bad.iter().filter(|b| **b).count();
        if bad_count == 0 {
            break;
        }
        fixed_total += bad_count;
        // Reassign each bad vertex from the nearest *good* vertex in posed
        // space (a cluster of bad vertices may share a triangle, so triangle
        // neighbours alone are not enough).
        let good: Vec<usize> = (0..n).filter(|i| !bad[*i]).collect();
        if good.is_empty() {
            break;
        }
        for i in 0..n {
            if !bad[i] {
                continue;
            }
            let pi = pos[i];
            let mut best = usize::MAX;
            let mut bd = f32::MAX;
            for &j in &good {
                let d = vdist(pi, pos[j]);
                if d < bd {
                    bd = d;
                    best = j;
                }
            }
            if best != usize::MAX {
                mesh.weights[i] = mesh.weights[best];
                mesh.bone_idx[i] = mesh.bone_idx[best];
            }
        }
        pos = skin_positions(mesh, posed, binds)?;
    }
    // Final geometric snap: any vertex still touching an abnormally long edge
    // is collapsed onto its nearest neighbour so no spike survives.
    {
        let mut maxedge = vec![0f32; n];
        let mut best_len = vec![f32::MAX; n];
        let mut best_nb = vec![usize::MAX; n];
        let mut edge_lens: Vec<f32> = Vec::with_capacity(mesh.indices.len());
        for tri in mesh.indices.chunks(3) {
            let (a, b, c) = (tri[0] as usize, tri[1] as usize, tri[2] as usize);
            for &(u, v) in &[(a, b), (b, c), (c, a)] {
                if u >= n || v >= n || u == v {
                    continue;
                }
                let d = vdist(pos[u], pos[v]);
                edge_lens.push(d);
                if d > maxedge[u] {
                    maxedge[u] = d;
                }
                if d > maxedge[v] {
                    maxedge[v] = d;
                }
                if d < best_len[u] {
                    best_len[u] = d;
                    best_nb[u] = v;
                }
                if d < best_len[v] {
                    best_len[v] = d;
                    best_nb[v] = u;
                }
            }
        }
        if !edge_lens.is_empty() {
            // Only collapse genuine spikes: one edge far longer than the
            // vertex's other edges. Large-but-uniform triangles (shoulder /
            // armpit / fingers) are legitimate and must be left alone.
            let mut moved = 0usize;
            let snapshot = pos.clone();
            for i in 0..n {
                if maxedge[i] > 0.15
                    && best_nb[i] != usize::MAX
                    && maxedge[i] > 8.0 * best_len[i].max(1e-4)
                {
                    pos[i] = snapshot[best_nb[i]];
                    moved += 1;
                }
            }
            fixed_total += moved;
        }
    }
    if fixed_total > 0 {
        eprintln!(
            "ok-extract: spike repair {} vertices in {}",
            fixed_total, mesh.name
        );
    }
    Some(pos)
}

pub fn skin_positions(mesh: &ExtractedMesh, posed: &[Mat4], binds: &[Mat4]) -> Option<Vec<[f32; 3]>> {
    if mesh.positions.is_empty() || mesh.weights.is_empty() || mesh.bone_idx.is_empty() {
        return None;
    }
    let n = mesh.positions.len();
    let mut out = vec![[0.0f32; 3]; n];
    let nb = posed.len();
    let palette:Vec<Mat4>=posed.iter().enumerate().map(|(i,m)|if binds.len()==posed.len(){mat_mul(m,&binds[i])}else{*m}).collect();
    for i in 0..n {
        let w = mesh.weights.get(i).copied().unwrap_or([1.0, 0.0, 0.0, 0.0]);
        let b = mesh.bone_idx.get(i).copied().unwrap_or([0, 0, 0, 0]);
        let p = mesh.positions[i];
        let mut acc = [0.0f32; 3];
        let mut ws = 0.0f32;
        for k in 0..4 {
            let wi = w[k];
            if wi.abs() < 1e-6 {
                continue;
            }
            let bi = b[k] as usize;
            if bi >= nb {
                continue;
            }
            let pal = palette[bi];
            let t = mat_vec(&pal, p);
            acc[0] += t[0] * wi;
            acc[1] += t[1] * wi;
            acc[2] += t[2] * wi;
            ws += wi;
        }
        if ws > 1e-5 {
            out[i] = [acc[0] / ws, acc[1] / ws, acc[2] / ws];
        } else {
            out[i] = p;
        }
    }
    Some(out)
}

/// Rotate a normal by a matrix's 3x3 part (no translation, renormalized).
pub fn rot_normal(m: &Mat4, v: [f32; 3]) -> [f32; 3] {
    let t = [
        m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
        m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
        m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2],
    ];
    let l = (t[0] * t[0] + t[1] * t[1] + t[2] * t[2]).sqrt();
    if l > 1e-8 {
        [t[0] / l, t[1] / l, t[2] / l]
    } else {
        v
    }
}

/// Skin the mesh's stored normals with the same palette as `skin_positions`.
/// Unity ships correct normals (the face has interior cavity triangles whose
/// geometric normals can't be averaged), so these must come from the asset.
pub fn skin_normals(mesh: &ExtractedMesh, posed: &[Mat4], binds: &[Mat4]) -> Option<Vec<[f32; 3]>> {
    skin_directions(&mesh.normals,&mesh.weights,&mesh.bone_idx,posed,binds)
}

#[cfg(test)]
#[test]
fn cached_skin_palette_matches_per_vertex_reference() {
    let mut mesh=ExtractedMesh::default();
    mesh.positions=(0..257).map(|i|[i as f32*0.013,-0.12,i as f32*-0.007]).collect();
    mesh.normals=vec![[0.3,0.4,0.5];257];mesh.weights=vec![[0.1,0.2,0.3,0.4];257];
    mesh.bone_idx=vec![[0,1,2,3];257];
    mesh.weights[0]=[0.;4];mesh.bone_idx[1]=[500;4];mesh.weights[2]=[-0.1,0.2,0.3,0.4];
    let mut posed=vec![ident();4];let mut binds=vec![ident();4];
    for i in 0..4 {posed[i][0][0]=0.7+i as f32*0.1;posed[i][1][1]=1.3;posed[i][0][3]=i as f32*0.23;binds[i][1][3]=-0.6;}
    for binds in [&binds[..],&binds[..2]] {
        let actual=skin_positions(&mesh,&posed,binds).unwrap();
        let mut expected=Vec::new();let mut expected_normals=Vec::new();
        for i in 0..mesh.positions.len(){
            let mut sum=[0.;3];let mut normal=[0.;3];let mut weight=0.;
            for k in 0..4 {let w=mesh.weights[i][k];let b=mesh.bone_idx[i][k] as usize;if w.abs()<1e-6||b>=posed.len(){continue;}
                let m=if binds.len()==posed.len(){mat_mul(&posed[b],&binds[b])}else{posed[b]};let p=mat_vec(&m,mesh.positions[i]);
                let n=mesh.normals[i];let d=[m[0][0]*n[0]+m[0][1]*n[1]+m[0][2]*n[2],m[1][0]*n[0]+m[1][1]*n[1]+m[1][2]*n[2],m[2][0]*n[0]+m[2][1]*n[1]+m[2][2]*n[2]];
                for a in 0..3 {sum[a]+=p[a]*w;normal[a]+=d[a]*w;}weight+=w;
            }
            expected.push(if weight>1e-5{sum.map(|v|v/weight)}else{mesh.positions[i]});
            let n=if weight>1e-5{normal.map(|v|v/weight)}else{mesh.normals[i]};let length=(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]).sqrt();expected_normals.push(if length>1e-8{n.map(|v|v/length)}else{n});
        }
        assert_eq!(actual,expected);assert_eq!(skin_normals(&mesh,&posed,binds).unwrap(),expected_normals);
    }
}

pub(crate) fn skin_directions(directions:&[[f32;3]],weights:&[[f32;4]],bone_idx:&[[u16;4]],posed:&[Mat4],binds:&[Mat4])->Option<Vec<[f32;3]>> {
    if directions.is_empty() || weights.is_empty() || bone_idx.is_empty() {
        return None;
    }
    let n = directions.len();
    let mut out = vec![[0.0f32; 3]; n];
    let nb = posed.len();
    let palette:Vec<Mat4>=posed.iter().enumerate().map(|(i,m)|if binds.len()==posed.len(){mat_mul(m,&binds[i])}else{*m}).collect();
    for i in 0..n {
        let nrm = directions[i];
        let w = weights.get(i).copied().unwrap_or([1.0, 0.0, 0.0, 0.0]);
        let b = bone_idx.get(i).copied().unwrap_or([0, 0, 0, 0]);
        let mut acc = [0.0f32; 3];
        let mut ws = 0.0f32;
        for k in 0..4 {
            let wi = w[k];
            if wi.abs() < 1e-6 {
                continue;
            }
            let bi = b[k] as usize;
            if bi >= nb {
                continue;
            }
            let pal = palette[bi];
            let t = [
                pal[0][0] * nrm[0] + pal[0][1] * nrm[1] + pal[0][2] * nrm[2],
                pal[1][0] * nrm[0] + pal[1][1] * nrm[1] + pal[1][2] * nrm[2],
                pal[2][0] * nrm[0] + pal[2][1] * nrm[1] + pal[2][2] * nrm[2],
            ];
            acc[0] += t[0] * wi;
            acc[1] += t[1] * wi;
            acc[2] += t[2] * wi;
            ws += wi;
        }
        let v = if ws > 1e-5 {
            [acc[0] / ws, acc[1] / ws, acc[2] / ws]
        } else {
            nrm
        };
        let l = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt();
        out[i] = if l > 1e-8 {
            [v[0] / l, v[1] / l, v[2] / l]
        } else {
            [0.0, 0.0, 1.0]
        };
    }
    Some(out)
}

pub fn skin_tangents(mesh:&ExtractedMesh,posed:&[Mat4],binds:&[Mat4])->Vec<[f32;4]> {
    let directions:Vec<_>=mesh.tangents.iter().map(|t|[t[0],t[1],t[2]]).collect();
    skin_directions(&directions,&mesh.weights,&mesh.bone_idx,posed,binds)
        .map(|v|v.into_iter().zip(&mesh.tangents).map(|(d,t)|[d[0],d[1],d[2],t[3]]).collect())
        .unwrap_or_else(||mesh.tangents.clone())
}

/// Native LBS streams share one immutable palette and one influence walk.
/// Keep each stream's original float accumulation/division order exactly.
pub(crate) fn skin_streams(mesh:&ExtractedMesh,normals:&[[f32;3]],posed:&[Mat4],binds:&[Mat4])
    ->Option<(Vec<[f32;3]>,Vec<[f32;3]>,Vec<[f32;4]>)>{
    let count=mesh.positions.len();
    if count==0||mesh.weights.is_empty()||mesh.bone_idx.is_empty(){return None;}
    if normals.len()!=count||mesh.tangents.len()!=count {
        return Some((skin_positions(mesh,posed,binds)?,skin_directions(normals,&mesh.weights,&mesh.bone_idx,posed,binds).unwrap_or_else(||normals.to_vec()),skin_tangents(mesh,posed,binds)));
    }
    let palette:Vec<_>=posed.iter().enumerate().map(|(i,m)|if binds.len()==posed.len(){mat_mul(m,&binds[i])}else{*m}).collect();
    let mut positions=vec![[0.;3];count];let mut directions=vec![[0.;3];count];let mut tangents=vec![[0.;4];count];
    let normalize=|v:[f32;3]|{let length=(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]).sqrt();if length>1e-8{[v[0]/length,v[1]/length,v[2]/length]}else{[0.,0.,1.]}};
    for i in 0..count {
        let weights=mesh.weights.get(i).copied().unwrap_or([1.,0.,0.,0.]);
        let bones=mesh.bone_idx.get(i).copied().unwrap_or([0;4]);
        let point=mesh.positions[i];let normal=normals[i];let tangent=mesh.tangents[i];
        let mut p=[0.;3];let mut n=[0.;3];let mut t=[0.;3];let mut total=0.;
        for k in 0..4 {
            let weight=weights[k];let bone=bones[k] as usize;if weight.abs()<1e-6||bone>=palette.len(){continue;}
            let m=palette[bone];let position=mat_vec(&m,point);
            let direction=[m[0][0]*normal[0]+m[0][1]*normal[1]+m[0][2]*normal[2],m[1][0]*normal[0]+m[1][1]*normal[1]+m[1][2]*normal[2],m[2][0]*normal[0]+m[2][1]*normal[1]+m[2][2]*normal[2]];
            let handed=[m[0][0]*tangent[0]+m[0][1]*tangent[1]+m[0][2]*tangent[2],m[1][0]*tangent[0]+m[1][1]*tangent[1]+m[1][2]*tangent[2],m[2][0]*tangent[0]+m[2][1]*tangent[1]+m[2][2]*tangent[2]];
            for axis in 0..3{p[axis]+=position[axis]*weight;n[axis]+=direction[axis]*weight;t[axis]+=handed[axis]*weight;}total+=weight;
        }
        positions[i]=if total>1e-5{[p[0]/total,p[1]/total,p[2]/total]}else{point};
        directions[i]=normalize(if total>1e-5{[n[0]/total,n[1]/total,n[2]/total]}else{normal});
        let t=normalize(if total>1e-5{[t[0]/total,t[1]/total,t[2]/total]}else{[tangent[0],tangent[1],tangent[2]]});
        tangents[i]=[t[0],t[1],t[2],tangent[3]];
    }
    Some((positions,directions,tangents))
}

#[cfg(test)]
#[test]
fn combined_skin_streams_match_independent_reference_paths(){
    let mut mesh=ExtractedMesh::default();
    mesh.positions=(0..257).map(|i|[i as f32*0.013,-0.12,i as f32*-0.007]).collect();
    mesh.normals=vec![[0.3,0.4,0.5];257];mesh.normals[3]=[0.;3];
    mesh.tangents=vec![[0.5,-0.2,0.7,-1.];257];mesh.tangents[3]=[0.;4];
    mesh.weights=vec![[0.1,0.2,0.3,0.4];250];mesh.bone_idx=vec![[0,1,2,3];253];
    mesh.weights[0]=[0.;4];mesh.weights[2]=[-0.1,0.2,0.3,0.4];mesh.bone_idx[1]=[500;4];
    let mut posed=vec![ident();4];let mut binds=vec![ident();4];
    for i in 0..4{posed[i][0][0]=0.7+i as f32*0.1;posed[i][1][1]=1.3;posed[i][0][3]=i as f32*0.23;binds[i][1][3]=-0.6;}
    for posed in [&posed[..],&posed[..0]]{for binds in [&binds[..],&binds[..2]]{for n in [257,253,0]{for t in [257,253,0]{
        let mut source=mesh.clone();source.tangents.truncate(t);let normals=&source.normals[..n];
        let actual=skin_streams(&source,normals,posed,binds).unwrap();
        assert_eq!(actual.0,skin_positions(&source,posed,binds).unwrap());
        assert_eq!(actual.1,skin_directions(normals,&source.weights,&source.bone_idx,posed,binds).unwrap_or_else(||normals.to_vec()));
        assert_eq!(actual.2,skin_tangents(&source,posed,binds));
    }}}}
}

#[cfg(test)]
#[test]
fn accessory_tangent_uses_position_palette_and_preserves_handedness() {
    let mut mesh=ExtractedMesh::default();
    mesh.positions=vec![[1.,0.,0.],[0.,1.,0.]];
    mesh.normals=vec![[0.,0.,1.];2];
    mesh.tangents=vec![[1.,0.,0.,-1.],[0.,1.,0.,1.]];
    mesh.weights=vec![[1.,0.,0.,0.],[0.,1.,0.,0.]];
    mesh.bone_idx=vec![[0,0,0,0],[0,1,0,0]];
    let mut first=ident();
    first[0][0]=0.;first[0][1]=-1.;first[1][0]=1.;first[1][1]=0.;
    first[0][3]=7.;
    let mut second=ident();second[0][3]=3.;second[1][3]=4.;
    let mut bind=ident();bind[0][3]=-2.;
    let posed=[first,second];let binds=[bind,ident()];
    let tangent=skin_tangents(&mesh,&posed,&binds);
    let normal=skin_normals(&mesh,&posed,&binds).unwrap();
    let position=skin_positions(&mesh,&posed,&binds).unwrap();
    assert!((tangent[0][0]).abs()<1e-6 && (tangent[0][1]-1.).abs()<1e-6);
    assert_eq!(tangent[0][3],-1.);
    assert!((tangent[1][0]).abs()<1e-6 && (tangent[1][1]-1.).abs()<1e-6);
    assert_eq!(tangent[1][3],1.);
    assert!((position[0][0]-7.).abs()<1e-6 && (position[0][1]+1.).abs()<1e-6);
    assert!((position[1][0]-3.).abs()<1e-6 && (position[1][1]-5.).abs()<1e-6);
    assert_eq!(normal,vec![[0.,0.,1.];2]);
    assert_eq!(mesh.tangents[0],[1.,0.,0.,-1.],"authored rest tangent must remain unchanged");
    mesh.weights.clear();
    assert_eq!(skin_tangents(&mesh,&posed,&binds),mesh.tangents,
        "a mesh without skin weights keeps authored tangents");
}

/// Dual-quaternion skinning. Blends the rigid part of each bone's skin matrix
/// (as a dual quaternion) instead of the raw matrices, which removes the
/// candy-wrapper / shoulder collapse of linear blend skinning. A blended
/// uniform scale keeps the morph bulk (height etc.).
pub fn skin_positions_dqs(mesh: &ExtractedMesh, posed: &[Mat4], binds: &[Mat4]) -> Option<Vec<[f32; 3]>> {
    if mesh.positions.is_empty() || mesh.weights.is_empty() || mesh.bone_idx.is_empty() {
        return None;
    }
    let cross = |a: [f32; 3], b: [f32; 3]| {
        [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    };
    let add3 = |a: [f32; 3], b: [f32; 3]| [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
    let mul3 = |a: [f32; 3], s: f32| [a[0] * s, a[1] * s, a[2] * s];
    let n = mesh.positions.len();
    let mut out = vec![[0.0f32; 3]; n];
    let nb = posed.len();
    for i in 0..n {
        let w = mesh.weights.get(i).copied().unwrap_or([1.0, 0.0, 0.0, 0.0]);
        let b = mesh.bone_idx.get(i).copied().unwrap_or([0, 0, 0, 0]);
        let p = mesh.positions[i];
        let mut q = [0.0f32; 4];
        let mut dq = [0.0f32; 4];
        let mut ref_q: Option<[f32; 4]> = None;
        let mut scale = 0.0f32;
        let mut ws = 0.0f32;
        for k in 0..4 {
            let wi = w[k];
            if wi.abs() < 1e-6 {
                continue;
            }
            let bi = b[k] as usize;
            if bi >= nb {
                continue;
            }
            let pal = if binds.len() == posed.len() {
                mat_mul(&posed[bi], &binds[bi])
            } else {
                posed[bi]
            };
            let col = |j: usize| [pal[0][j], pal[1][j], pal[2][j]];
            let (c0, c1, c2) = (col(0), col(1), col(2));
            let len = |c: [f32; 3]| (c[0] * c[0] + c[1] * c[1] + c[2] * c[2]).sqrt().max(1e-9);
            let (l0, l1, l2) = (len(c0), len(c1), len(c2));
            let norm = |c: [f32; 3], l: f32| [c[0] / l, c[1] / l, c[2] / l];
            let (n0, n1, n2) = (norm(c0, l0), norm(c1, l1), norm(c2, l2));
            let rmat: Mat4 = [
                [n0[0], n1[0], n2[0], 0.0],
                [n0[1], n1[1], n2[1], 0.0],
                [n0[2], n1[2], n2[2], 0.0],
                [0.0, 0.0, 0.0, 1.0],
            ];
            let rq = quat_from_mat(&rmat);
            let sgn = match ref_q {
                Some(r0) => {
                    if rq[0] * r0[0] + rq[1] * r0[1] + rq[2] * r0[2] + rq[3] * r0[3] < 0.0 {
                        -1.0
                    } else {
                        1.0
                    }
                }
                None => {
                    ref_q = Some(rq);
                    1.0
                }
            };
            let t = [pal[0][3], pal[1][3], pal[2][3]];
            let dual = quat_mul([t[0], t[1], t[2], 0.0], rq);
            for j in 0..4 {
                q[j] += wi * sgn * rq[j];
                dq[j] += wi * sgn * 0.5 * dual[j];
            }
            scale += wi * (l0 * l1 * l2).cbrt();
            ws += wi;
        }
        if ws < 1e-5 {
            out[i] = p;
            continue;
        }
        for j in 0..4 {
            q[j] /= ws;
            dq[j] /= ws;
        }
        let qn = (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]).sqrt().max(1e-9);
        for j in 0..4 {
            q[j] /= qn;
            dq[j] /= qn;
        }
        let dd = q[0] * dq[0] + q[1] * dq[1] + q[2] * dq[2] + q[3] * dq[3];
        for j in 0..4 {
            dq[j] -= q[j] * dd;
        }
        let s = scale / ws;
        let v = [p[0] * s, p[1] * s, p[2] * s];
        let qv = [q[0], q[1], q[2]];
        let dv = [dq[0], dq[1], dq[2]];
        let c1 = cross(qv, add3(cross(qv, v), mul3(v, q[3])));
        let c2 = add3(mul3(dv, q[3]), mul3(qv, -dq[3]));
        let t2 = add3(c2, cross(qv, dv));
        out[i] = add3(v, add3(mul3(c1, 2.0), mul3(t2, 2.0)));
    }
    Some(out)
}

#[allow(dead_code)]
pub fn transform_positions(pos: &[[f32; 3]], m: &Mat4) -> Vec<[f32; 3]> {
    pos.iter().map(|p| mat_vec(m, *p)).collect()
}

pub fn rebuild_obj(mesh: &mut ExtractedMesh, pos: Vec<[f32; 3]>) {
    mesh.positions = pos;
    mesh.obj = crate::unity::mesh_to_obj(&mesh.name, &mesh.positions, &mesh.uvs, &mesh.indices);
}

pub fn aabb(pos: &[[f32; 3]]) -> ([f32; 3], [f32; 3]) {
    let mut mn = [f32::MAX; 3];
    let mut mx = [f32::MIN; 3];
    for p in pos {
        for i in 0..3 {
            mn[i] = mn[i].min(p[i]);
            mx[i] = mx[i].max(p[i]);
        }
    }
    (mn, mx)
}

pub fn translate(pos: &mut [[f32; 3]], d: [f32; 3]) {
    for p in pos {
        p[0] += d[0];
        p[1] += d[1];
        p[2] += d[2];
    }
}

pub fn binds_from_rest(rest: &[Mat4]) -> Vec<Mat4> {
    rest.iter().map(|m| mat_inv(m).unwrap_or_else(ident)).collect()
}

pub fn skin_bones<'a>(sk: &'a Skeleton) -> (&'a [Mat4], &'a [Mat4]) {
    let n = sk.smr_count.min(sk.worlds.len()).min(sk.rest.len());
    (&sk.worlds[..n], &sk.rest[..n])
}

pub fn bind_poses_from_object(object: &UnityObject) -> Vec<Mat4> {
    let mut out = Vec::new();
    let Some(UnityValue::Array(arr)) = object.class.properties().get("m_BindPose") else {
        return out;
    };
    for item in arr {
        let UnityValue::Object(o) = item else { continue };
        let e = |k: &str| o.get(k).map(vf).unwrap_or(0.0);
        out.push([
            [e("e00"), e("e01"), e("e02"), e("e03")],
            [e("e10"), e("e11"), e("e12"), e("e13")],
            [e("e20"), e("e21"), e("e22"), e("e23")],
            [e("e30"), e("e31"), e("e32"), e("e33")],
        ]);
    }
    out
}
