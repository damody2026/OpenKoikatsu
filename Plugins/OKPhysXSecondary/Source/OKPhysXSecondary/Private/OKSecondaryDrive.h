#pragma once
#include "OKSecondaryBackends.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"

// The PhysX backend's per-particle forces, for the solvers that only offer particles + length constraints (Jolt
// soft bodies, MuJoCo point masses): a goal spring towards parent + GoalOffset damped relative to the parent, the soft "stay near the
// goal" limit, DynamicBone damping and gravity. Same CVars and formulas as PrepareJob, applied as a velocity change
// before each substep; damping is implicit so large gains stay stable at the substep size.
struct FOKParticleDrive
{
	TArray<int32> Parent;
	TArray<float> K, C, LimitK, Extent, Damp;
	TArray<FVector3f> GoalOffset;
	FVector3f Gravity = FVector3f::ZeroVector;

	static float Var(const TCHAR* Name, float Default)
	{
		const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
		return V ? V->GetFloat() : Default;
	}

	void Init(const FOKBackendChain& Chain)
	{
		const FOKPxChainStep& S = *Chain.Step;
		const int32 Count = int32(S.ParticleCount);
		const float RefStep = FMath::Max(Chain.RefStep, 1e-4f);
		const float Sub = Chain.Dt / FMath::Max(Chain.Substeps, 1);
		const OKPhysXSecondary::FSoftness Softness = OKPhysXSecondary::Softness(S.Particles[0].Flags);
		const float PullScale = Softness.Pull, RangeScale = Softness.Range, DampScale = Softness.Damp;
		const float Zeta = Var(TEXT("OKPhysXSecondary.DriveDamping"), 1.f) * Softness.Zeta;
		const float Limit = Var(TEXT("OKPhysXSecondary.LimitStiffness"), 4000.f) * PullScale;
		// Tuning of the Jolt/MuJoCo backends only (PhysX keeps its own values).
		const float DriveScale = FMath::Max(Var(TEXT("OKPhysXSecondary.AltDriveScale"), 1.f), 0.f);
		const float DampingScale = FMath::Max(Var(TEXT("OKPhysXSecondary.AltDampingScale"), 1.f), 0.f);
		const float LimitScale = FMath::Max(Var(TEXT("OKPhysXSecondary.AltLimitScale"), 1.f), 0.f);
		Parent.SetNumUninitialized(Count);
		K.SetNumUninitialized(Count); C.SetNumUninitialized(Count); LimitK.SetNumUninitialized(Count);
		Extent.SetNumUninitialized(Count); Damp.SetNumUninitialized(Count); GoalOffset.SetNumUninitialized(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			const FOKPxParticle& P = S.Particles[i];
			Parent[i] = P.Parent;
			K[i] = P.Elasticity * PullScale * DriveScale / (RefStep * RefStep);
			C[i] = 2.f * Zeta * FMath::Sqrt(FMath::Max(K[i], 0.f));
			LimitK[i] = Limit * LimitScale;
			Extent[i] = FMath::Max(P.RestLen * (1.f - P.Stiffness) * 2.f * RangeScale, 1e-4f);
			Damp[i] = FMath::Min(P.Damping * DampScale * DampingScale / RefStep, 0.9f / FMath::Max(Sub, 1e-5f));
			GoalOffset[i] = FVector3f(P.GoalOffset[0], P.GoalOffset[1], P.GoalOffset[2]);
		}
		Gravity = Chain.Gravity;
	}

	/** New velocity of particle i (not a root) after Dt, from positions X and velocities V of the whole chain. */
	FVector3f Velocity(int32 i, const FVector3f* X, const FVector3f* V, float Dt) const
	{
		const int32 p = Parent[i];
		const FVector3f Rel = V[i] - V[p];
		FVector3f Accel = Gravity + K[i] * (X[p] + GoalOffset[i] - X[i]);
		float Implicit = C[i] + Damp[i]; // coefficients of the damping terms (on V[i]), solved implicitly
		FVector3f Explicit = C[i] * V[p];
		const FVector3f Off = X[i] - (X[p] + GoalOffset[i]);
		const float Dist = Off.Size();
		if (Dist > Extent[i])
		{
			const FVector3f Dir = Off / Dist;
			const float LimitC = 2.f * FMath::Sqrt(LimitK[i]);
			Accel -= Dir * (LimitK[i] * (Dist - Extent[i]) + LimitC * FVector3f::DotProduct(Rel, Dir));
		}
		return (V[i] + Dt * (Accel + Explicit)) / (1.f + Dt * Implicit);
	}
};

/** Collision radius of the Jolt/MuJoCo particles relative to the DynamicBone radius. */
FORCEINLINE float OKAltRadiusScale() { return FMath::Max(FOKParticleDrive::Var(TEXT("OKPhysXSecondary.AltRadiusScale"), 1.f), 0.f); }

// Root particle path over one frame: from where the solver left it to the animated position (teleports snap).
FORCEINLINE FVector3f OKRootFrom(const FVector3f& Last, const FVector3f& To, bool bFresh)
{
	return (bFresh || FVector3f::DistSquared(Last, To) > 1.f) ? To : Last;
}
