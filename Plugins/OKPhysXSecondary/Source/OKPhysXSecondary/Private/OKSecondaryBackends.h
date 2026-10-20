#pragma once
#include "CoreMinimal.h"
#include "OKPhysXSecondaryAbi.h"
#include "OKPhysXSecondary.h"

// Alternative solvers for the same DynamicBone chains, for side-by-side comparison with PhysX. They receive exactly what
// PhysX receives (particles, goals, the body/partner colliders already filtered and shrunk to the animated clearance) and
// write positions back into the step; the audit is shared. No PhysX headers here (UE/Chaos headers clash with them).
struct FOKBackendChain
{
	FOKPxChainStep* Step = nullptr;
	TArray<FOKPxCapsule> Colliders;   // current pose
	TArray<FOKPxCapsule> FromColliders; // previous pose (for sweeping across substeps)
	TArray<FVector3f> Anim;           // animated pose of every particle
	float Dt = 0.f;
	float RefStep = 1.f / 60.f;
	FVector3f Gravity = FVector3f::ZeroVector; // acceleration
	int32 Substeps = 1;
	/**
	 * Optional (OKPhysXSecondary.AltSubstepCollision): PhysX's own per-substep contact handling (soft skin layer + exact
	 * push-out, per-particle collider radii) on the solver's positions/velocities. Call after each substep with
	 * Alpha = (step + 1) / Substeps; X and V hold ParticleCount entries and are modified in place.
	 */
	TFunction<void(float Alpha, int32 Substeps, FVector3f* X, FVector3f* V)> SubstepContact;
};

/** KawaiiPhysics (v1.21) per-bone algorithm: verlet + stiffness pull + capsule/sphere collision + length restore. */
void OKKawaiiStep(TArrayView<FOKBackendChain> Chains);
/** Chaos::Softs::FPBDEvolution (the Chaos Cloth PBD solver): springs for bone length, anim drive, collision particles. */
void OKChaosStep(uint64 BatchKey, TArrayView<FOKBackendChain> Chains);
/** Jolt Physics v5.6.0 soft bodies (XPBD edges) colliding with kinematic capsules. */
void OKJoltStep(uint64 BatchKey, TArrayView<FOKBackendChain> Chains);
/** MuJoCo 3.15.0: slide-joint point masses, length-limited spatial tendons, mocap capsules. False if mujoco.dll is missing. */
bool OKMuJoCoStep(uint64 BatchKey, TArrayView<FOKBackendChain> Chains);
/** The game's own DynamicBone update (Verlet, elasticity, stiffness range, freeze plane, exact length); collision by the shared exact pass. */
void OKDynamicBoneStep(TArrayView<FOKBackendChain> Chains);
void OKJoltRelease(uint64 Key);
void OKMuJoCoRelease(uint64 Key);
void OKBackendsRelease(uint64 Key);
