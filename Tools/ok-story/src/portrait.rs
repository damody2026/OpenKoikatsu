//! The live portrait at the bottom left: a second, orthographic camera on the speaker's own model, framed from the
//! chest and turned only with the body's heading, so head and neck movement stays in the picture.
use crate::io::{ActorFacts, PortraitOut};
use crate::math::{self, Quat, V3};
use crate::script::PortraitAdjust;
use crate::tuning::{Layout, PortraitTuning};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

/// The framing measured once per character, on its first portrait frame.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Reference {
    pub pivot: String,
    /// Head minus pivot, in the heading frame then (cm).
    pub head_offset: V3,
    /// The hips' rotation and the facing then.
    pub hips: [f64; 4],
    pub facing: V3,
    pub scale: f64,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Portrait {
    /// Who is shown, who is wanted next.
    pub shown: String,
    pub wanted: String,
    pub alpha: f64,
    pub references: BTreeMap<String, Reference>,
    /// Last heading per character (a heading that cannot be read keeps it).
    pub yaw: BTreeMap<String, f64>,
    pub frame: u64,
}

const PIVOTS: [&str; 4] = ["spine03", "spine02", "hips", "head"];

fn heading_frame(yaw_deg: f64) -> Quat {
    Quat::yaw_deg(yaw_deg)
}

fn yaw_of(v: V3) -> f64 {
    v[1].atan2(v[0]).to_degrees()
}

impl Portrait {
    /// The speaker to show (empty: nobody). The change fades out first.
    pub fn want(&mut self, who: &str) {
        self.wanted = who.to_string();
    }

    pub fn tick(&mut self, t: &PortraitTuning, dt: f64) {
        if self.shown != self.wanted {
            if self.shown.is_empty() || self.alpha <= 0.0 {
                self.shown = self.wanted.clone();
                self.alpha = 0.0;
            } else {
                self.alpha = (self.alpha - dt / t.fade_out.max(1e-3)).max(0.0);
                return;
            }
        }
        if !self.shown.is_empty() {
            self.alpha = (self.alpha + dt / t.fade_in.max(1e-3)).min(1.0);
        }
    }

    fn reference(&mut self, who: &str, a: &ActorFacts) -> Option<Reference> {
        if let Some(r) = self.references.get(who) {
            return Some(r.clone());
        }
        let head = a.bone("head")?.pos;
        let pivot = PIVOTS.iter().find(|p| a.bone(p).is_some())?;
        let facing = a.facing();
        let frame = heading_frame(yaw_of(facing));
        let r = Reference {
            pivot: pivot.to_string(),
            head_offset: frame.inverse().rotate(math::sub(head, a.bone(pivot)?.pos)),
            hips: a.bone("hips").map(|h| h.rot).unwrap_or(a.root.rot),
            facing,
            scale: a.root.scale.abs().max(1e-6),
        };
        self.references.insert(who.to_string(), r.clone());
        Some(r)
    }

    /// The capture camera for the shown character this frame.
    pub fn out(
        &mut self,
        t: &PortraitTuning,
        layout: &Layout,
        adjust: &PortraitAdjust,
        actors: &BTreeMap<String, ActorFacts>,
        viewport: [f64; 2],
    ) -> PortraitOut {
        self.frame += 1;
        let rect = [0.0, 1.0 - layout.portrait_height, layout.portrait_width, layout.portrait_height];
        let (vw, vh) = (viewport[0].max(1.0), viewport[1].max(1.0));
        let px = [rect[2] * vw, rect[3] * vh];
        // The render target follows the portrait's pixels, at most the tuned size.
        let fit = (t.target[0] as f64 / px[0]).min(t.target[1] as f64 / px[1]).min(1.0);
        let target = [(px[0] * fit).round().max(16.0) as u32, (px[1] * fit).round().max(16.0) as u32];
        let mut out = PortraitOut {
            rect,
            fade_from: layout.portrait_fade_from,
            target,
            alpha: self.alpha,
            who: self.shown.clone(),
            ..Default::default()
        };
        let who = self.shown.clone();
        let Some(a) = actors.get(&who).filter(|a| a.ready) else { return out };
        let Some(r) = self.reference(&who, a) else { return out };
        let (Some(pivot), true) = (a.bone(&r.pivot), self.alpha > 0.0) else { return out };
        // Heading: how far the hips turned since the reference, applied to the facing then.
        let hips_now = a.bone("hips").map(|h| h.q()).unwrap_or(a.root.q());
        let turn = hips_now.times(Quat::from_array(r.hips).inverse());
        let f = turn.rotate(r.facing);
        let yaw = if f[0] * f[0] + f[1] * f[1] > 1e-6 { yaw_of(f) } else { *self.yaw.get(&who).unwrap_or(&yaw_of(r.facing)) };
        self.yaw.insert(who.clone(), yaw);
        let frame = heading_frame(yaw);
        let s = a.root.scale.abs().max(1e-6) / r.scale;
        let target = math::add(pivot.pos, frame.rotate(math::scale(r.head_offset, s)));
        let k = a.root.scale.abs().max(1e-6);
        // Heading frame: X ahead (where she faces), Y her right, Z up.
        let eye = math::add(target, frame.rotate([t.forward * k, t.side * k, t.up * k]));
        let aim = math::add(target, [0.0, 0.0, t.aim_up * k]);
        let rot = Quat::look(math::sub(aim, eye), [0.0, 0.0, 1.0]);
        let zoom = if adjust.scale > 0.0 { adjust.scale.clamp(0.25, 3.0) } else { 1.0 };
        let width = t.width * k / zoom;
        let height = width * target_h_over_w(target_px(&out));
        // Place the target at the anchor of the frame (x from the left, y from the bottom), moved by the author's offset.
        let ax = t.anchor[0] * width + adjust.offset_x.clamp(-100.0, 100.0) / 100.0 * width;
        let ay = t.anchor[1] * height + adjust.offset_y.clamp(-100.0, 100.0) / 100.0 * height;
        let right = rot.rotate([0.0, 1.0, 0.0]);
        let up = rot.rotate([0.0, 0.0, 1.0]);
        let pos = math::add(eye, math::add(math::scale(right, width * 0.5 - ax), math::scale(up, height * 0.5 - ay)));
        out.visible = true;
        out.pos = pos;
        out.rot = rot.to_array();
        out.ortho_width = width;
        out.capture = self.frame.is_multiple_of(t.every.max(1) as u64);
        out
    }
}

fn target_px(o: &PortraitOut) -> [u32; 2] {
    o.target
}

fn target_h_over_w(t: [u32; 2]) -> f64 {
    t[1] as f64 / t[0].max(1) as f64
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::io::Xf;

    fn actor(yaw: f64, head_turn: f64, nod: f64) -> ActorFacts {
        let mut a = ActorFacts { ready: true, ..Default::default() };
        let q = Quat::yaw_deg(yaw);
        a.root = Xf { pos: [0.0, 0.0, 0.0], rot: q.to_array(), scale: 1.0 };
        a.bones.insert("hips".into(), Xf { pos: [0.0, 0.0, 90.0], rot: q.to_array(), scale: 1.0 });
        a.bones.insert("spine03".into(), Xf { pos: [0.0, 0.0, 125.0], rot: q.to_array(), scale: 1.0 });
        // Turning or nodding the head moves it a little around the neck.
        let neck = [0.0, 0.0, 140.0];
        let head_local = Quat::yaw_deg(head_turn).times(Quat::from_rotator(nod, 0.0, 0.0)).rotate(q.rotate([0.0, 3.0, 12.0]));
        a.bones.insert("head".into(), Xf { pos: math::add(neck, head_local), rot: q.to_array(), scale: 1.0 });
        a
    }

    fn shot(p: &mut Portrait, a: ActorFacts) -> PortraitOut {
        let mut actors = BTreeMap::new();
        actors.insert("ayu".to_string(), a);
        p.out(&PortraitTuning::default(), &Layout::default(), &PortraitAdjust::default(), &actors, [1920.0, 1080.0])
    }

    #[test]
    fn follows_the_body_not_the_head() {
        let t = PortraitTuning::default();
        let mut p = Portrait::default();
        p.want("ayu");
        p.tick(&t, 1.0);
        let base = shot(&mut p, actor(0.0, 0.0, 0.0));
        assert!(base.visible && base.alpha == 1.0);
        // Turning or nodding the head does not turn the camera.
        let head = shot(&mut p, actor(0.0, 30.0, 15.0));
        assert_eq!(head.rot, base.rot);
        // Turning the body does.
        let body = shot(&mut p, actor(40.0, 0.0, 0.0));
        let angle = Quat::from_array(body.rot).angle_to(Quat::from_array(base.rot)).to_degrees();
        assert!((angle - 40.0).abs() < 0.5, "{angle}");
        // The camera looks at her from the front (she faces +Y at yaw 0).
        assert!(base.pos[1] > 50.0, "{:?}", base.pos);
    }

    #[test]
    fn fades_between_speakers() {
        let t = PortraitTuning::default();
        let mut p = Portrait::default();
        p.want("ayu");
        p.tick(&t, 1.0);
        assert_eq!((p.shown.as_str(), p.alpha), ("ayu", 1.0));
        p.want("rin");
        p.tick(&t, t.fade_out / 2.0);
        assert_eq!(p.shown, "ayu");
        assert!(p.alpha < 1.0 && p.alpha > 0.0);
        p.tick(&t, t.fade_out);
        p.tick(&t, 0.0);
        assert_eq!(p.shown, "rin");
        p.tick(&t, t.fade_in);
        assert_eq!(p.alpha, 1.0);
        p.want("");
        p.tick(&t, 1.0);
        p.tick(&t, 1.0);
        assert_eq!(p.shown, "");
    }

    #[test]
    fn target_size_keeps_the_frame_aspect() {
        let mut p = Portrait::default();
        let out = shot(&mut p, actor(0.0, 0.0, 0.0));
        let aspect = out.target[0] as f64 / out.target[1] as f64;
        let want = 0.54 * 1920.0 / (0.68 * 1080.0);
        assert!((aspect - want).abs() < 0.01, "{aspect} {want}");
    }
}
