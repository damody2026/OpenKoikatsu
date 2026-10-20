//! Saved Studio FIX locals and bounded additive head tracking, applied before dynamics.
use crate::finalik::*;
use crate::pose::Skeleton;

#[derive(Default)]
pub(crate) struct HeadLook {
    pub target: Option<V3>,
    /// Native NeckLookControllerVer2 aBones order: neck, head; independent of camera tracking.
    pub fixed: Option<[Quat;2]>,
    yaw: f32,
    pitch: f32,
    /// Last check (degrees): face to the limited desired direction, and desired to the target (beyond the limits).
    pub last_error: Option<(f32, f32)>,
}

impl HeadLook {
    pub fn apply(&mut self, skeleton: &mut Skeleton, dt: f32) {
        if let Some(rotations)=self.fixed {
            for (name,rotation) in ["cf_j_neck","cf_j_head"].into_iter().zip(rotations) {
                if let Some(tf)=skeleton.tfs.values_mut().find(|tf|tf.name.eq_ignore_ascii_case(name)) {tf.rot=rotation;}
            }
            skeleton.rebuild_worlds();
            self.last_error=None;
            return;
        }
        let Some(target) = self.target else { return; };
        let mut state = [self.yaw, self.pitch];
        self.last_error = crate::dbone::head_look_world(skeleton, target, &mut state, dt);
        if self.last_error.is_some() { self.yaw = state[0]; self.pitch = state[1]; }
    }
    pub fn reset(&mut self) { self.target = None; self.yaw = 0.; self.pitch = 0.; self.last_error = None; }
}

fn angles(local: V3) -> Option<(f32,f32)> {
    if !local.iter().all(|v| v.is_finite()) || vlen(local) < 0.05 { return None; }
    Some((local[0].atan2(local[2]).to_degrees().clamp(-65.,65.),
        local[1].atan2((local[0]*local[0]+local[2]*local[2]).sqrt()).to_degrees().clamp(-35.,35.)))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn bounded_targets() {
        assert_eq!(angles([0.,0.,1.]), Some((0.,0.)));
        assert_eq!(angles([100.,100.,0.]), Some((65.,35.)));
        assert!(angles([0.;3]).is_none());
        assert!(angles([f32::NAN,0.,1.]).is_none());
        assert!(angles([0.,0.,-1.]).unwrap().0.abs() <= 65.);
    }
    #[test]
    fn fresh_pose_tracking_is_bounded_and_disable_restores_animation() {
        use crate::pose::{Tf,ident};
        // head_look_world measures the body frame from the chest (spine03) and the shoulders (arm00_L/R).
        let bone=|name:&str,parent:Option<i64>,pos:[f32;3]|Tf{name:name.into(),parent,sibling_index:None,pos,rot:[0.,0.,0.,1.],scale:[1.;3]};
        let mut skeleton=Skeleton {names:vec![],worlds:vec![],rest:vec![],smr_count:6,
            tfs:[(1,bone("cf_j_neck",Some(4),[0.,0.1,0.])),(2,bone("cf_j_head",Some(1),[0.,0.1,0.])),(3,bone("hair_anchor",Some(2),[0.,0.,0.2])),
                (4,bone("cf_j_spine03",None,[0.;3])),(5,bone("cf_j_arm00_L",Some(4),[-0.2,0.05,0.])),(6,bone("cf_j_arm00_R",Some(4),[0.2,0.05,0.]))].into(),
            bone_ids:vec![1,2,3,4,5,6],source_mesh:0};
        skeleton.rebuild_worlds();skeleton.rest=vec![ident();6];let base=skeleton.clone();
        let mut look=HeadLook::default();look.target=Some([1.,0.4,1.]);
        for _ in 0..240 {skeleton=base.clone();look.apply(&mut skeleton,1./120.);}
        let forward=qrot(qmul(skeleton.tfs[&1].rot,skeleton.tfs[&2].rot),[0.,0.,1.]);
        assert!(forward[0]>0.6 && forward[2]>0.6,"head did not face target: {forward:?}");
        assert_ne!(skeleton.worlds[2],base.worlds[2],"attachment did not follow head");
        let held=skeleton.worlds.clone();for _ in 0..240 {skeleton=base.clone();look.apply(&mut skeleton,1./120.);}
        assert!(skeleton.worlds.iter().flatten().flatten().zip(held.iter().flatten().flatten()).all(|(a,b)|(a-b).abs()<1e-5),"tracking accumulated rotations");
        look.reset();skeleton=base.clone();look.apply(&mut skeleton,1./120.);assert_eq!(skeleton.worlds,base.worlds);
        look.target=Some([0.,0.,-10.]);skeleton=base;look.apply(&mut skeleton,0.);
        assert!(skeleton.worlds.iter().flatten().flatten().all(|v|v.is_finite()));assert!(look.yaw.abs()<=65.&&look.pitch.abs()<=35.);
    }
}
