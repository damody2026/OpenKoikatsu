//! Optional PhysX back end for DynamicBone chains.
//!
//! The host (OKPhysXSecondary plugin) registers a solver through
//! `ok_set_physx_dynamic_solver`. When present, the per-update particle
//! integration (UpdateParticles1/2 in dbone.rs and the ver02 body chains) is
//! delegated to it. Rust keeps all pose logic: it hands over the particles'
//! current/previous positions, the animated "goal" offset of every particle and
//! the world capsules of the colliders, and receives the simulated positions
//! back. Bone rotations are still derived by `apply_particles`, so the PhysX
//! result drives the same skeleton the original solver did.
//!
//! The call is stateless on the Rust side (all history lives in pos/prev), so
//! cloned or reset chains stay correct; `key` only lets the host cache bodies.
use std::cell::Cell;
use std::sync::atomic::{AtomicPtr, AtomicU64, Ordering};

pub const FLAG_ROOT: u32 = 1;
/// Set on the root of body chains (breasts, hips): squeezed soft tissue, audited separately from hair/cloth.
pub const FLAG_BODY: u32 = 2;
/// Set on the root of chains worn over the clothes (hair, accessories): they collide with the clothed body surface.
pub const FLAG_OUTER: u32 = 8;
/// Set on the root when the chain was already solved by the built-in solver: the host only audits it.
pub const FLAG_AUDIT_ONLY: u32 = 4;
/// Set on the root of skin chains of the body mesh itself (an uncensor body's crotch/anus pivots): the clothes are not
/// skinned to these bones, so pushing them out of the rough body proxies drives the skin through tights and pantyhose.
/// The game gives them no body colliders either; the host keeps them off the body capsules.
pub const FLAG_SKIN: u32 = 16;
/// Set on the root of tails (`PhysicsComponent::Tail`): the host makes them springier (OKPhysXSecondary.TailBounce).
pub const FLAG_TAIL: u32 = 32;
/// Root flag bits 8..11: the host solver this chain asks for, plus one (0 = not chosen: the host uses PhysX). The host
/// follows it while OKPhysXSecondary.Backend is -1 (per component); 0..6 there force one solver on every chain.
pub const FLAG_SOLVER_SHIFT: u32 = 8;
pub const FLAG_SOLVER_MASK: u32 = 15 << FLAG_SOLVER_SHIFT;

/// Host solvers a component can ask for (OKPhysXSecondary.Backend numbers).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Solver { PhysX = 0, Kawaii = 1, Chaos = 2, Jolt = 4, MuJoCo = 5, DynamicBone = 6 }
impl Solver {
    pub const ALL: [Solver; 6] = [Solver::PhysX, Solver::Kawaii, Solver::Chaos, Solver::Jolt, Solver::MuJoCo, Solver::DynamicBone];
    /// Name used in OK cards (`okPh`).
    pub fn name(self) -> &'static str {
        match self { Solver::PhysX => "physx", Solver::Kawaii => "kawaii", Solver::Chaos => "chaos", Solver::Jolt => "jolt", Solver::MuJoCo => "mujoco", Solver::DynamicBone => "dynbone" }
    }
    pub fn from_name(name: &str) -> Option<Solver> { Solver::ALL.into_iter().find(|s| s.name().eq_ignore_ascii_case(name.trim())) }
    fn flags(self) -> u32 { (self as u32 + 1) << FLAG_SOLVER_SHIFT }
}

/// What a DynamicBone chain belongs to; each component has its own default solver (Docs/PER_COMPONENT_PHYSICS_PLAN.md).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PhysicsComponent { Hair, Body, Clothes, Accessory, Tail }
impl PhysicsComponent {
    /// From a session part role (`hair_f`, `clothes_bot`, `accessory`...).
    pub fn from_role(role: &str) -> PhysicsComponent {
        if role.starts_with("hair") { PhysicsComponent::Hair }
        else if role.starts_with("clothes") { PhysicsComponent::Clothes }
        else { PhysicsComponent::Accessory }
    }
    /// Hair, breasts and hips keep the game's own DynamicBone (with collision added); everything else uses PhysX.
    pub fn default_solver(self) -> Solver {
        match self { PhysicsComponent::Hair | PhysicsComponent::Body => Solver::DynamicBone, _ => Solver::PhysX }
    }
    /// One accessory chain, from where the accessory is attached (`a_n_waist_b`...), the mesh name and the chain's bones.
    /// Anything worn on the head (wigs, pigtails, ears, earrings) is hair; elsewhere a chain named like a tail
    /// (tail, sippo/shippo, 尻尾, しっぽ, 尾巴/weiba, 尾) is a tail; the rest is an ordinary accessory.
    pub fn of_accessory_chain(attach: &str, mesh: &str, bones: &[&str]) -> PhysicsComponent {
        let attach = attach.to_ascii_lowercase();
        if ["a_n_head", "a_n_hair", "a_n_earrings", "a_n_megane", "a_n_nose", "a_n_mouth", "cf_j_faceup", "cf_j_head"].iter().any(|p| attach.starts_with(p)) {
            return PhysicsComponent::Hair;
        }
        // Hair hung elsewhere (a long strand attached at the back: KK_222816 cf_hair_s_05_00 on a_n_back_L) is still hair.
        let hair = |name: &str| { let n = name.to_lowercase(); ["hair", "kami", "髪", "头发", "頭髮"].iter().any(|w| n.contains(w)) };
        if hair(mesh) || bones.iter().any(|b| hair(b)) { return PhysicsComponent::Hair; }
        let tail = |name: &str| {
            let n = name.to_lowercase();
            ["tail", "sippo", "shippo", "尻尾", "しっぽ", "シッポ", "尾巴", "weiba", "尾"].iter().any(|w| n.contains(w))
                && !["twintail", "ponytail", "hair", "kami", "髪"].iter().any(|w| n.contains(w))
        };
        if bones.iter().any(|b| tail(b)) || tail(mesh) { PhysicsComponent::Tail } else { PhysicsComponent::Accessory }
    }
}


/// Root flags asking the host for `solver` (see FLAG_SOLVER_SHIFT).
pub(crate) fn solver_flags(solver: Solver) -> u32 { solver.flags() }

thread_local! { static CARD_SOLVER: Cell<Option<Solver>> = const { Cell::new(None) }; }
/// Restores the previous card choice when dropped.
pub(crate) struct CardSolverGuard(Option<Solver>);
impl Drop for CardSolverGuard { fn drop(&mut self) { CARD_SOLVER.with(|s| s.set(self.0)); } }
/// The solver the card names for the part being updated on this thread (None: each chain uses its component default).
pub(crate) fn enter_card_solver(solver: Option<Solver>) -> CardSolverGuard { CardSolverGuard(CARD_SOLVER.with(|s| s.replace(solver))) }
pub(crate) fn card_solver() -> Option<Solver> { CARD_SOLVER.with(|s| s.get()) }

#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct OkPxParticle {
    pub parent: i32,
    pub flags: u32,
    pub radius: f32,
    pub damping: f32,
    pub elasticity: f32,
    pub stiffness: f32,
    pub rest_len: f32,
    pub min_fraction: f32,
    pub max_fraction: f32,
    pub pos: [f32; 3],
    pub prev: [f32; 3],
    /// World-space vector from the simulated parent to this particle's goal.
    pub goal_offset: [f32; 3],
    /// DynamicBone FreezeAxis plane normal through the simulated parent (zero = free).
    pub freeze_normal: [f32; 3],
    /// Thickness of the mesh around the bone ending here (0 = unknown): other characters' bodies are met at
    /// max(radius, body_radius). The own body keeps the authored radius (hair rests on it).
    pub body_radius: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct OkPxCapsule {
    pub a: [f32; 3],
    pub b: [f32; 3],
    pub radius: f32,
}

#[repr(C)]
pub struct OkPxChainStep {
    pub key: u64,
    pub particle_count: u32,
    pub collider_count: u32,
    pub particles: *mut OkPxParticle,
    pub colliders: *const OkPxCapsule,
    /// Verlet gravity displacement per reference step (world units).
    pub gravity_step: [f32; 3],
    pub dt: f32,
    /// Duration of one original update step (1 / update_rate).
    pub reference_step: f32,
    pub velocity_scale: f32,
    /// Character the chain belongs to (0 = none). Chains of one group collide with
    /// the group's body capsules and with each other.
    pub group: u64,
}

pub type SolveFn = unsafe extern "C" fn(*const OkPxChainStep) -> i32;
pub type ReleaseFn = unsafe extern "C" fn(u64);
/// One simulated body part of the full-body ragdoll. `pos`/`rot` are the animated pose on
/// input (rot = x,y,z,w) and the simulated pose on output; `tip` is the far end of the part.
#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct OkPxBone {
    pub parent: i32,
    pub radius: f32,
    pub pos: [f32; 3],
    pub rot: [f32; 4],
    pub tip: [f32; 3],
}
pub type RagdollFn = unsafe extern "C" fn(u64, *mut OkPxBone, u32, f32) -> i32;
pub type SolveBatchFn = unsafe extern "C" fn(u64, *mut OkPxChainStep, u32) -> i32;
pub type AuditFn = unsafe extern "C" fn() -> i32;
pub type HeadTiltFn = unsafe extern "C" fn() -> f32;
pub type PublishBodyFn = unsafe extern "C" fn(u64, *const OkPxCapsule, u32);

static SOLVER: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());
static RELEASE: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());
static PUBLISH: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());
static RAGDOLL: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());
static SOLVE_BATCH: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());
static AUDIT: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());
static HEAD_TILT: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());
static NEXT_KEY: AtomicU64 = AtomicU64::new(1);

/// Registers (or clears with null) the PhysX chain solver of this module copy.
#[no_mangle]
pub extern "C" fn ok_set_physx_dynamic_solver(solve: Option<SolveFn>, release: Option<ReleaseFn>, publish_body: Option<PublishBodyFn>, ragdoll: Option<RagdollFn>, head_tilt: Option<HeadTiltFn>, solve_batch: Option<SolveBatchFn>, audit: Option<AuditFn>) {
    AUDIT.store(audit.map_or(std::ptr::null_mut(), |f| f as *mut ()), Ordering::Release);
    SOLVE_BATCH.store(solve_batch.map_or(std::ptr::null_mut(), |f| f as *mut ()), Ordering::Release);
    HEAD_TILT.store(head_tilt.map_or(std::ptr::null_mut(), |f| f as *mut ()), Ordering::Release);
    RAGDOLL.store(ragdoll.map_or(std::ptr::null_mut(), |f| f as *mut ()), Ordering::Release);
    PUBLISH.store(publish_body.map_or(std::ptr::null_mut(), |f| f as *mut ()), Ordering::Release);
    SOLVER.store(solve.map_or(std::ptr::null_mut(), |f| f as *mut ()), Ordering::Release);
    RELEASE.store(release.map_or(std::ptr::null_mut(), |f| f as *mut ()), Ordering::Release);
}

pub(crate) fn solver() -> Option<SolveFn> {
    let p = SOLVER.load(Ordering::Acquire);
    // SAFETY: only `ok_set_physx_dynamic_solver` stores here, with a SolveFn.
    if p.is_null() { None } else { Some(unsafe { std::mem::transmute::<*mut (), SolveFn>(p) }) }
}

/// Host cache id of one chain. Cloning allocates a fresh id: the host body set
/// is derived from pos/prev on every call, so no history needs to be shared.
#[derive(Debug)]
pub(crate) struct PxKey(u64);
impl PxKey {
    pub(crate) fn new() -> Self { PxKey(NEXT_KEY.fetch_add(1, Ordering::Relaxed)) }
    pub(crate) fn id(&self) -> u64 { self.0 }
}
impl Default for PxKey { fn default() -> Self { Self::new() } }
impl Clone for PxKey { fn clone(&self) -> Self { Self::new() } }
impl Drop for PxKey {
    fn drop(&mut self) {
        let p = RELEASE.load(Ordering::Acquire);
        if !p.is_null() {
            // SAFETY: stored from a ReleaseFn by `ok_set_physx_dynamic_solver`.
            unsafe { std::mem::transmute::<*mut (), ReleaseFn>(p)(self.0) }
        }
    }
}

/// Runs one chain step on the host solver. Returns false when no solver is
/// registered or it declined, leaving `particles` untouched.
pub(crate) fn solve(
    key: &PxKey, particles: &mut [OkPxParticle], colliders: &[OkPxCapsule],
    gravity_step: [f32; 3], dt: f32, reference_step: f32, velocity_scale: f32,
) -> bool {
    let Some(f) = solver() else { return false };
    if particles.len() < 2 || !allowed() { return false; }
    mark_layer(particles);
    let step = OkPxChainStep {
        key: key.id(), particle_count: particles.len() as u32, collider_count: colliders.len() as u32,
        particles: particles.as_mut_ptr(), colliders: colliders.as_ptr(),
        gravity_step, dt, reference_step, velocity_scale, group: current_group(),
    };
    // SAFETY: pointers are valid for the call; the host only writes pos/prev.
    unsafe { f(&step) != 0 }
}

thread_local! { static GROUP: Cell<u64> = const { Cell::new(0) }; }
/// Whether the character being updated on this thread uses the host solver at all. Set per session
/// (`ok_session_set_physx_secondary`): the gallery physics menu, one-to-one interaction and paired playback turn it
/// on. Crowd and street NPCs leave it off and keep the built-in solver even while OKPhysXSecondary.Enabled is on.
thread_local! { static ALLOWED: Cell<bool> = const { Cell::new(false) }; }
fn allowed() -> bool {
    // Diagnostic: OK_PHYSX_ALL_CHARACTERS=1 lets every character use the host solver (OK_Demo's global behaviour).
    static ALL: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    ALLOWED.with(|a| a.get()) || *ALL.get_or_init(|| std::env::var("OK_PHYSX_ALL_CHARACTERS").as_deref() == Ok("1"))
}
/// Writes the host's current weight (1..10) and hardness (1..10) of hair/cloth/accessory chains
/// (OKPhysXSecondary.Weight / OKPhysXSecondary.Hardness). Callable from any thread.
pub type TuningFn = unsafe extern "C" fn(*mut f32, *mut f32);
static TUNING: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());

/// Registered by the host (OKPhysXSecondary::RegisterTuning): the built-in Verlet then follows the same weight and
/// hardness as the host solvers, so every solver can be compared under the same settings. Every module that links this
/// library has its own copy of this state, so each one registers.
#[no_mangle]
pub extern "C" fn ok_set_secondary_tuning_source(get: Option<TuningFn>) {
    TUNING.store(get.map_or(std::ptr::null_mut(), |f| f as *mut ()), Ordering::Release);
}

/// Built-in Verlet version of the host's softness and weight (same formulas as PrepareJob in OKPhysXSecondary.cpp).
#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct VerletTuning {
    /// Elasticity x this (pull toward the animated pose).
    pub pull: f32,
    /// Stiffness range (how far from the goal a particle may stray) x this.
    pub range: f32,
    /// Damping x this.
    pub damp: f32,
    /// Extra downward acceleration (m/s^2, skeleton -Y) on top of the chain's own gravity.
    pub gravity: f32,
}
impl VerletTuning {
    pub const NONE: VerletTuning = VerletTuning { pull: 1.0, range: 1.0, damp: 1.0, gravity: 0.0 };
    /// Extra gravity displacement for one reference step of `step` seconds.
    pub fn gravity_step(&self, step: f32) -> [f32; 3] { [0.0, -self.gravity * step * step, 0.0] }
}
/// The tuning for hair/cloth/accessory chains of the character updated on this thread; identity unless this session
/// allows the host solver (gallery, one-to-one). Crowd and street NPCs stay as authored.
pub(crate) fn verlet_tuning() -> VerletTuning {
    let get = TUNING.load(Ordering::Acquire);
    if get.is_null() || !allowed() { return VerletTuning::NONE; }
    let (mut weight, mut hardness) = (1.0f32, 10.0f32);
    // SAFETY: stored from a TuningFn by `ok_set_secondary_tuning_source`; both pointers are valid for the call.
    unsafe { std::mem::transmute::<*mut (), TuningFn>(get)(&mut weight, &mut hardness) };
    let weight = if weight.is_finite() { weight.clamp(1.0, 10.0) } else { 1.0 };
    let hardness = if hardness.is_finite() { hardness.clamp(1.0, 10.0) } else { 10.0 };
    let soft = (10.0 - hardness) / 9.0;
    VerletTuning { pull: 1.0 - 0.9 * soft, range: 1.0 + 2.0 * soft, damp: 1.0 - 0.6 * soft, gravity: (weight - 1.0) / 9.0 * 9.81 }
}

thread_local! { static CHAIN_SOLVER: Cell<Option<Solver>> = const { Cell::new(None) }; }
/// Restores the previous solver when dropped.
pub(crate) struct SolverGuard(Option<Solver>);
impl Drop for SolverGuard { fn drop(&mut self) { CHAIN_SOLVER.with(|s| s.set(self.0)); } }
/// Chains solved on this thread while the guard lives ask the host for `solver`.
pub(crate) fn enter_solver(solver: Solver) -> SolverGuard { SolverGuard(CHAIN_SOLVER.with(|s| s.replace(Some(solver)))) }

thread_local! { static OUTER: Cell<bool> = const { Cell::new(false) }; }
/// Restores the previous layer when dropped.
pub(crate) struct OuterGuard(bool);
impl Drop for OuterGuard { fn drop(&mut self) { OUTER.with(|o| o.set(self.0)); } }
/// Chains solved on this thread while the guard lives are worn over the clothes (hair, accessories).
pub(crate) fn enter_outer(outer: bool) -> OuterGuard { OuterGuard(OUTER.with(|o| o.replace(outer))) }
fn mark_layer(particles: &mut [OkPxParticle]) {
    let Some(root) = particles.first_mut() else { return };
    if OUTER.with(|o| o.get()) { root.flags |= FLAG_OUTER; }
    // A chain that already names its solver (classified per chain, dbone.rs) keeps it.
    if root.flags & FLAG_SOLVER_MASK == 0 {
        if let Some(solver) = CHAIN_SOLVER.with(|s| s.get()) { root.flags |= solver.flags(); }
    }
}

/// Restores the previous group (and its host-solver permission) when dropped.
pub(crate) struct GroupGuard(u64, bool);
impl Drop for GroupGuard { fn drop(&mut self) { GROUP.with(|g| g.set(self.0)); ALLOWED.with(|a| a.set(self.1)); } }
/// Marks the chains solved on this thread as belonging to one character; `physx` lets them use the host solver.
pub(crate) fn enter_group(id: u64, physx: bool) -> GroupGuard {
    GroupGuard(GROUP.with(|g| g.replace(id)), ALLOWED.with(|a| a.replace(physx)))
}
pub(crate) fn current_group() -> u64 { GROUP.with(|g| g.get()) }

/// Replaces the group's body capsules (self-collision proxies) on the host.
pub(crate) fn publish_body(group: u64, capsules: &[OkPxCapsule]) {
    let p = PUBLISH.load(Ordering::Acquire);
    if p.is_null() || group == 0 || !allowed() { return; }
    // SAFETY: stored from a PublishBodyFn by `ok_set_physx_dynamic_solver`.
    unsafe { std::mem::transmute::<*mut (), PublishBodyFn>(p)(group, capsules.as_ptr(), capsules.len() as u32) }
}

/// True while the host wants the full-body ragdoll for this character.
pub(crate) fn ragdoll_enabled(group: u64) -> bool {
    let p = RAGDOLL.load(Ordering::Acquire);
    if p.is_null() || group == 0 || !allowed() { return false; }
    // SAFETY: stored from a RagdollFn; a zero-length call only reports the switch.
    unsafe { std::mem::transmute::<*mut (), RagdollFn>(p)(group, std::ptr::null_mut(), 0, 0.0) != 0 }
}

/// Simulates the ragdoll for one frame; false when the host declined (pose untouched).
pub(crate) fn ragdoll_step(group: u64, bones: &mut [OkPxBone], dt: f32) -> bool {
    let p = RAGDOLL.load(Ordering::Acquire);
    if p.is_null() || group == 0 || bones.is_empty() { return false; }
    // SAFETY: the host reads and rewrites pos/rot of `bones` only.
    unsafe { std::mem::transmute::<*mut (), RagdollFn>(p)(group, bones.as_mut_ptr(), bones.len() as u32, dt) != 0 }
}

/// Stress test from the host: extra backward head tilt in degrees (0 = off). Every pose is then checked with the
/// head thrown back, the position where long hair is most likely to go through the back and shoulders.
pub(crate) fn debug_head_tilt() -> f32 {
    let p = HEAD_TILT.load(Ordering::Acquire);
    if p.is_null() { return 0.0; }
    // SAFETY: stored from a HeadTiltFn by `ok_set_physx_dynamic_solver`.
    unsafe { std::mem::transmute::<*mut (), HeadTiltFn>(p)() }
}

/// One chain's request for a batched solve (see `solve_batch`).
pub(crate) struct ChainInput {
    pub key: u64,
    pub particles: Vec<OkPxParticle>,
    pub colliders: Vec<OkPxCapsule>,
    pub gravity_step: [f32; 3],
    pub dt: f32,
    pub reference_step: f32,
    pub velocity_scale: f32,
}

/// True while the host's PhysX solver is switched on. Then PhysX owns every chain: the built-in Verlet solver never runs
/// (chains PhysX cannot express are held still instead).
pub(crate) fn enabled() -> bool {
    if !allowed() { return false; }
    let Some(f) = solver() else { return false };
    // SAFETY: a null step only asks whether the solver is on.
    unsafe { f(std::ptr::null()) != 0 }
}

pub(crate) fn batch_available() -> bool { allowed() && solver().is_some() && !SOLVE_BATCH.load(Ordering::Acquire).is_null() }

/// Solves every chain of one DynamicBone set in one host call: one PhysX scene per set (`batch`), one simulate per
/// substep for all its chains. All or nothing: false leaves every input untouched.
pub(crate) fn solve_batch(batch: &PxKey, inputs: &mut [&mut ChainInput]) -> bool {
    let p = SOLVE_BATCH.load(Ordering::Acquire);
    if p.is_null() || !allowed() || inputs.is_empty() || inputs.iter().any(|i| i.particles.len() < 2) { return false; }
    let group = current_group();
    for input in inputs.iter_mut() { mark_layer(&mut input.particles); }
    let mut steps: Vec<OkPxChainStep> = inputs.iter_mut().map(|i| OkPxChainStep {
        key: i.key, particle_count: i.particles.len() as u32, collider_count: i.colliders.len() as u32,
        particles: i.particles.as_mut_ptr(), colliders: i.colliders.as_ptr(),
        gravity_step: i.gravity_step, dt: i.dt, reference_step: i.reference_step, velocity_scale: i.velocity_scale, group,
    }).collect();
    // SAFETY: every pointer stays valid for the call; the host only writes particle pos/prev.
    unsafe { std::mem::transmute::<*mut (), SolveBatchFn>(p)(batch.id(), steps.as_mut_ptr(), steps.len() as u32) != 0 }
}

/// True while the host measures clipping/jitter (tests only).
pub(crate) fn audit_enabled() -> bool {
    if !allowed() { return false; }
    let p = AUDIT.load(Ordering::Acquire);
    // SAFETY: stored from an AuditFn by `ok_set_physx_dynamic_solver`.
    !p.is_null() && unsafe { std::mem::transmute::<*mut (), AuditFn>(p)() != 0 }
}

/// Sends chains the built-in solver already moved, so the host measures them with the same audit as PhysX.
pub(crate) fn audit_batch(batch: &PxKey, inputs: &mut [ChainInput]) {
    for input in inputs.iter_mut() { if let Some(root) = input.particles.first_mut() { root.flags |= FLAG_AUDIT_ONLY; } }
    let mut refs: Vec<&mut ChainInput> = inputs.iter_mut().collect();
    let _ = solve_batch(batch, &mut refs);
}

#[cfg(test)]
mod tests {
    use super::*;
    unsafe extern "C" fn always_on(_: *const OkPxChainStep) -> i32 { 1 }
    #[test]
    fn host_solver_only_for_characters_that_allow_it() {
        // Registration is process-wide; only threads inside an allowing group may use it.
        ok_set_physx_dynamic_solver(Some(always_on), None, None, None, None, None, None);
        assert!(!enabled(), "no group: built-in solver");
        {
            let _crowd = enter_group(1, false);
            assert!(!enabled(), "crowd / street NPC: built-in solver");
            {
                let _pair = enter_group(2, true);
                assert!(enabled(), "one-to-one interaction character: host solver");
            }
            assert!(!enabled(), "leaving the pair restores the outer character's setting");
        }
        let thread = std::thread::spawn(|| { let _pair = enter_group(3, true); enabled() });
        assert!(thread.join().unwrap(), "the permission is per thread");
        assert!(!enabled());
        ok_set_physx_dynamic_solver(None, None, None, None, None, None, None);
    }
}

#[cfg(test)]
mod component_tests {
    use super::PhysicsComponent::{self, *};

    fn of(attach: &str, mesh: &str, bones: &[&str]) -> PhysicsComponent { PhysicsComponent::of_accessory_chain(attach, mesh, bones) }

    #[test]
    fn tails_from_the_crowd_cards() {
        // Bone names, mesh names and attachments seen in Content/CharacterCards (C002, C010, C026, C047, C050, C051, C069).
        assert_eq!(of("a_n_waist_b", "acs_waist_cattail", &["N_j_sippo_02", "N_j_sippo_05"]), Tail);
        assert_eq!(of("a_n_waist", "tail", &["tail0", "tail4"]), Tail);
        assert_eq!(of("a_n_waist_b", "acs_aku01_sippo", &["j_01", "j_09"]), Tail);
        assert_eq!(of("a_n_waist_b", "Zhenzhen_Weiba", &["Skn_Tail_01"]), Tail);
        assert_eq!(of("a_n_waist_b", "W1_mesh", &["tail_M_02_jnt"]), Tail);
        assert_eq!(of("a_n_waist_f", "Tail 2_mesh", &["tail", "tail.005_end"]), Tail);
        assert_eq!(of("a_n_waist", "009_衣装_fashion_swimsuit_02", &["尻尾1", "尻尾12"]), Tail);
    }

    #[test]
    fn head_chains_are_hair_and_body_ribbons_are_accessories() {
        // Pigtails named "tail" on the head and wig hair stay hair (C010 dogears, C050 Yvonne_hair, C000 wig).
        assert_eq!(of("a_n_headtop", "dogears", &["TailL1", "TailL4"]), Hair);
        assert_eq!(of("a_n_headside", "Yvonne_hair", &["hair_twintail_L"]), Hair);
        assert_eq!(of("a_n_hair_twin_L", "x", &["j_01"]), Hair);
        assert_eq!(of("a_n_earrings_R", "Earring_mesh", &["Earring"]), Hair);
        assert_eq!(of("cf_J_FaceUp_ty", "bangs", &["j"]), Hair);
        assert_eq!(of("a_n_waist_b", "on_waist_wedding_back222", &["waist_j_base_r01"]), Accessory);
        assert_eq!(of("a_n_neck", "on_heart_necklace_base", &["acs_J_neck_01"]), Accessory);
        assert_eq!(of("a_n_waist_L", "acs_sao", &["j_1"]), Accessory);
        // The LOL Ahri cards: nine fox tails per card; a hair strand hung from the back.
        assert_eq!(of("a_n_waist_b", "o_acs_foxtail", &["joint0", "joint3"]), Tail);
        assert_eq!(of("a_n_back_L", "cf_hair_s_05_00", &["joint1", "joint7"]), Hair);
        assert_eq!(of("a_n_bust_f", "o_necktie_yure_long", &["j_acs_01"]), Accessory);
        assert_eq!(Tail.default_solver(), super::Solver::PhysX);
        assert_eq!(Hair.default_solver(), super::Solver::DynamicBone);
    }

}
