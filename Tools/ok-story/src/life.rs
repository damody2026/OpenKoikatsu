//! Small signs of life on everyone on stage: blinks and glances on their own random clocks, kept apart so the cast
//! never blinks or looks away in unison (at most one blink and two gaze changes at a time).
use crate::io::ActorFacts;
use crate::math::{self, Quat, V3};
use crate::rng::Rng;
use crate::tuning::LifeTuning;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

/// Where someone's eyes go.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub enum Gaze {
    Camera,
    Animation,
    At(String),
    Point(V3),
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub enum Glance {
    None,
    /// Looking a little aside, until.
    Aside { point: V3, until: f64 },
    /// Looking at another character, until.
    At { who: String, until: f64 },
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Person {
    pub next_blink: f64,
    pub blink_until: f64,
    pub next_glance: f64,
    pub look_ready: f64,
    pub glance: Glance,
    pub glance_started: f64,
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct Life {
    /// The clock, running only while the story plays.
    pub time: f64,
    pub people: BTreeMap<String, Person>,
    pub last_gaze_start: f64,
}

/// What the life layer decided this tick.
#[derive(Debug, Default, PartialEq)]
pub struct Decisions {
    pub blinks: Vec<String>,
    /// Final gaze per present character.
    pub gaze: BTreeMap<String, Gaze>,
}

/// The facts of one tick for one person on stage.
pub struct Present<'a> {
    pub who: &'a str,
    pub base: Gaze,
    /// A glance would override what the story asked for: no.
    pub fixed: bool,
    /// The face shuts the eyes (no blink over it).
    pub closed: bool,
    pub facts: Option<&'a ActorFacts>,
}

impl Life {
    fn person(&mut self, t: &LifeTuning, rng: &mut Rng, who: &str) -> &mut Person {
        let time = self.time;
        self.people.entry(who.to_string()).or_insert_with(|| Person {
            next_blink: time + rng.range(t.blink_every),
            blink_until: 0.0,
            next_glance: time + rng.range(t.glance_every),
            look_ready: time + rng.range(t.look_cooldown) * 0.5,
            glance: Glance::None,
            glance_started: 0.0,
        })
    }

    /// One tick: `present` are the people on stage now (anyone else is forgotten, so a return waits afresh).
    pub fn tick(&mut self, t: &LifeTuning, rng: &mut Rng, dt: f64, present: &[Present], view: Option<V3>) -> Decisions {
        self.time += dt.max(0.0);
        let now = self.time;
        self.people.retain(|w, _| present.iter().any(|p| p.who == w));
        let mut out = Decisions::default();
        let blinking = |people: &BTreeMap<String, Person>| people.values().filter(|p| p.blink_until > now).count();
        let gazing = |people: &BTreeMap<String, Person>| people.values().filter(|p| p.glance != Glance::None).count();
        for p in present {
            self.person(t, rng, p.who);
        }
        // Glances that ended (or whose target left).
        for p in present {
            let person = self.people.get_mut(p.who).unwrap();
            let over = match &person.glance {
                Glance::None => false,
                Glance::Aside { until, .. } => *until <= now || p.fixed,
                Glance::At { who, until } => *until <= now || p.fixed || !present.iter().any(|q| q.who == who),
            };
            if over {
                person.glance = Glance::None;
                person.next_glance = now + rng.range(t.glance_every);
            }
        }
        // Blinks, fairly: whoever has waited longest first.
        let mut order: Vec<&Present> = present.iter().collect();
        order.sort_by(|a, b| self.people[a.who].next_blink.total_cmp(&self.people[b.who].next_blink));
        for p in order {
            let free = blinking(&self.people) < t.blink_max;
            let authored = p.facts.is_some_and(|f| f.authored_blink);
            let person = self.people.get_mut(p.who).unwrap();
            if person.next_blink > now {
                continue;
            }
            if authored || p.closed {
                person.next_blink = now + rng.range(t.blink_every);
            } else if !free {
                person.next_blink = now + rng.range(t.blink_retry);
            } else {
                person.blink_until = now + t.blink_length;
                person.next_blink = now + rng.range(t.blink_every);
                out.blinks.push(p.who.to_string());
            }
        }
        // New glances: the longest waiting first; at most gaze_max at once, starts staggered.
        let mut order: Vec<&Present> = present.iter().filter(|p| !p.fixed && p.facts.is_some_and(|f| f.ready && !f.authored_look)).collect();
        order.sort_by(|a, b| self.people[a.who].next_glance.total_cmp(&self.people[b.who].next_glance));
        for p in order {
            if self.people[p.who].glance != Glance::None || self.people[p.who].next_glance > now {
                continue;
            }
            if gazing(&self.people) >= t.gaze_max || now - self.last_gaze_start < t.gaze_stagger {
                let person = self.people.get_mut(p.who).unwrap();
                person.next_glance = now + t.gaze_stagger + rng.range(t.gaze_jitter);
                continue;
            }
            let me = p.facts.unwrap();
            let look_ready = self.people[p.who].look_ready <= now;
            let other = if look_ready && rng.unit() < 0.35 { pick_other(t, rng, me, p.who, present) } else { None };
            let glance = match other {
                Some(who) => {
                    self.people.get_mut(p.who).unwrap().look_ready = now + rng.range(t.look_cooldown);
                    Glance::At { who, until: now + rng.range(t.look_hold) }
                }
                None => {
                    let base = target_point(&p.base, me, present, view);
                    Glance::Aside { point: aside(t, rng, me.eyes(), base), until: now + rng.range(t.glance_hold) }
                }
            };
            let person = self.people.get_mut(p.who).unwrap();
            person.glance = glance;
            person.glance_started = now;
            self.last_gaze_start = now;
        }
        for p in present {
            let g = match &self.people[p.who].glance {
                Glance::None => p.base.clone(),
                Glance::Aside { point, .. } => Gaze::Point(*point),
                Glance::At { who, .. } => Gaze::At(who.clone()),
            };
            out.gaze.insert(p.who.to_string(), g);
        }
        out
    }
}

/// The world point a gaze aims at (None: the camera or the animation, not a point).
pub fn target_point(g: &Gaze, me: &ActorFacts, present: &[Present], view: Option<V3>) -> V3 {
    let ahead = math::add(me.eyes(), math::scale(me.facing(), 200.0));
    match g {
        Gaze::Point(p) => *p,
        Gaze::At(w) => present.iter().find(|p| p.who == w).and_then(|p| p.facts).map(|f| f.eyes()).unwrap_or(ahead),
        Gaze::Camera => view.unwrap_or(ahead),
        Gaze::Animation => ahead,
    }
}

/// A point a few degrees beside `base` as seen from `eye`, within the eyes' range.
fn aside(t: &LifeTuning, rng: &mut Rng, eye: V3, base: V3) -> V3 {
    let yaw = (rng.sign() * rng.range(t.glance_yaw)).clamp(-t.max_yaw, t.max_yaw);
    let pitch = (rng.sign() * rng.range(t.glance_pitch)).clamp(-t.max_pitch, t.max_pitch);
    let d = math::normalize(math::sub(base, eye));
    let flat = [d[0], d[1], 0.0];
    let side = math::normalize(math::cross([0.0, 0.0, 1.0], flat));
    let turned = Quat::axis_angle(side, -pitch.to_radians()).mul(Quat::yaw_deg(yaw)).rotate(d);
    math::add(eye, math::scale(turned, t.glance_distance))
}

/// Someone on stage in front of `me`, near enough to glance at.
fn pick_other(t: &LifeTuning, rng: &mut Rng, me: &ActorFacts, who: &str, present: &[Present]) -> Option<String> {
    let facing = me.facing();
    let candidates: Vec<&Present> = present
        .iter()
        .filter(|p| p.who != who)
        .filter(|p| {
            let Some(f) = p.facts.filter(|f| f.ready) else { return false };
            let to = math::sub(f.eyes(), me.eyes());
            let flat = [to[0], to[1], 0.0];
            let dist = math::len(flat);
            dist > 1.0 && dist <= t.look_range && math::dot(math::normalize(flat), facing) >= t.look_cone.to_radians().cos()
        })
        .collect();
    if candidates.is_empty() {
        None
    } else {
        Some(candidates[rng.below(candidates.len())].who.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::io::Xf;

    fn actor(x: f64) -> ActorFacts {
        let mut a = ActorFacts { ready: true, ..Default::default() };
        a.root = Xf { pos: [x, 0.0, 0.0], rot: Quat::IDENTITY.to_array(), scale: 1.0 };
        a.root.rot = Quat::yaw_deg(0.0).to_array();
        a
    }

    #[test]
    fn crowd_blinks_and_glances_apart() {
        let t = LifeTuning::default();
        let mut rng = Rng::new(7);
        let mut life = Life::default();
        let names: Vec<String> = (0..20).map(|i| format!("p{i}")).collect();
        let facts: Vec<ActorFacts> = (0..20).map(|i| actor(i as f64 * 60.0)).collect();
        let mut blinks: BTreeMap<String, usize> = BTreeMap::new();
        let mut glances: BTreeMap<String, usize> = BTreeMap::new();
        let mut previous: BTreeMap<String, Gaze> = BTreeMap::new();
        let dt = 1.0 / 60.0;
        for _ in 0..(5 * 60 * 60) {
            let present: Vec<Present> = names
                .iter()
                .zip(&facts)
                .map(|(w, f)| Present { who: w, base: Gaze::Camera, fixed: false, closed: false, facts: Some(f) })
                .collect();
            let d = life.tick(&t, &mut rng, dt, &present, Some([0.0, 500.0, 150.0]));
            for b in &d.blinks {
                *blinks.entry(b.clone()).or_default() += 1;
            }
            let now = life.time;
            assert!(life.people.values().filter(|p| p.blink_until > now).count() <= t.blink_max);
            assert!(life.people.values().filter(|p| p.glance != Glance::None).count() <= t.gaze_max);
            for (w, g) in &d.gaze {
                if *g != Gaze::Camera && previous.get(w) == Some(&Gaze::Camera) {
                    *glances.entry(w.clone()).or_default() += 1;
                }
                if let Gaze::Point(p) = g {
                    // Within the eyes' range of the camera direction.
                    let f = &facts[names.iter().position(|n| n == w).unwrap()];
                    let eye = f.eyes();
                    let a = math::normalize(math::sub(*p, eye));
                    let b = math::normalize(math::sub([0.0, 500.0, 150.0], eye));
                    let ang = math::dot(a, b).clamp(-1.0, 1.0).acos().to_degrees();
                    assert!(ang <= (t.max_yaw.powi(2) + t.max_pitch.powi(2)).sqrt() + 0.01, "{ang}");
                }
            }
            previous = d.gaze;
        }
        for w in &names {
            assert!(blinks.get(w).copied().unwrap_or(0) > 0, "{w} never blinked");
            assert!(glances.get(w).copied().unwrap_or(0) > 0, "{w} never glanced");
        }
    }

    #[test]
    fn same_seed_same_life() {
        let t = LifeTuning::default();
        let run = || {
            let mut rng = Rng::new(42);
            let mut life = Life::default();
            let f = [actor(0.0), actor(80.0)];
            let mut log = Vec::new();
            for _ in 0..3000 {
                let present = [
                    Present { who: "a", base: Gaze::Camera, fixed: false, closed: false, facts: Some(&f[0]) },
                    Present { who: "b", base: Gaze::Camera, fixed: false, closed: false, facts: Some(&f[1]) },
                ];
                let d = life.tick(&t, &mut rng, 0.02, &present, None);
                log.push(format!("{:?}{:?}", d.blinks, d.gaze));
            }
            log
        };
        assert_eq!(run(), run());
    }

    #[test]
    fn closed_eyes_and_fixed_gaze_are_left_alone() {
        let t = LifeTuning::default();
        let mut rng = Rng::new(1);
        let mut life = Life::default();
        let f = actor(0.0);
        for _ in 0..(60 * 60) {
            let present = [Present { who: "a", base: Gaze::At("x".into()), fixed: true, closed: true, facts: Some(&f) }];
            let d = life.tick(&t, &mut rng, 1.0 / 60.0, &present, None);
            assert!(d.blinks.is_empty());
            assert_eq!(d.gaze["a"], Gaze::At("x".into()));
        }
    }
}
