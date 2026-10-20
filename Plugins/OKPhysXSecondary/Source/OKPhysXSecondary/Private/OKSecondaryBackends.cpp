#include "OKSecondaryBackends.h"
#include "Async/Mutex.h"
#include "Async/UniqueLock.h"
#include "HAL/IConsoleManager.h"

// KawaiiPhysics (MIT, pafuhana1213) is used as-is through its own per-step functions.
#include "AnimNode_KawaiiPhysics.h"

#include "Chaos/PBDEvolution.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/PBDAnimDriveConstraint.h"
#include "Chaos/Capsule.h"
#include "Chaos/Sphere.h"
#include "Chaos/ImplicitObject.h"

// Tuning knobs shared by the Jolt/MuJoCo backends (read by FOKParticleDrive / OKAltRadiusScale every frame).
static TAutoConsoleVariable<float> CVarAltDriveScale(
	TEXT("OKPhysXSecondary.AltDriveScale"), 1.f, TEXT("Jolt/MuJoCo: goal spring stiffness x this."), ECVF_Default);
static TAutoConsoleVariable<float> CVarAltDampingScale(
	TEXT("OKPhysXSecondary.AltDampingScale"), 1.f, TEXT("Jolt/MuJoCo: DynamicBone damping x this."), ECVF_Default);
static TAutoConsoleVariable<float> CVarAltLimitScale(
	TEXT("OKPhysXSecondary.AltLimitScale"), 1.f, TEXT("Jolt/MuJoCo: stiffness of the soft 'stay near the goal' limit x this."), ECVF_Default);
static TAutoConsoleVariable<float> CVarAltRadiusScale(
	TEXT("OKPhysXSecondary.AltRadiusScale"), 1.f, TEXT("Jolt/MuJoCo: particle collision radius x this."), ECVF_Default);

static TAutoConsoleVariable<int32> CVarChaosIterations(
	TEXT("OKPhysXSecondary.ChaosIterations"), 4, TEXT("Constraint iterations per substep of the Chaos backend."), ECVF_Default);

namespace
{
	FORCEINLINE FVector3f F3(const float* P) { return FVector3f(P[0], P[1], P[2]); }
	FORCEINLINE void Put3(float* Out, const FVector3f& V) { Out[0] = V.X; Out[1] = V.Y; Out[2] = V.Z; }

	// ------------------------------------------------------------------------------------------------- KawaiiPhysics
	// FAnimNode_KawaiiPhysics outside an anim graph: its bones, settings and colliders are filled from the chain and its
	// protected per-step functions run in the same order as its SimulateOnce (simulate all, collide all, restore length).
	struct FOKKawaiiNode : public FAnimNode_KawaiiPhysics
	{
		void Step(FOKBackendChain& C)
		{
			FOKPxChainStep& S = *C.Step;
			const int32 Count = int32(S.ParticleCount);
			ModifyBones.SetNum(Count);
			// OKPhysXSecondary.Hardness: the same pull/damping scales as PhysX (Kawaii has no stray-range limit to widen).
			const OKPhysXSecondary::FSoftness Softness = OKPhysXSecondary::Softness(S.Particles[0].Flags);
			for (int32 i = 0; i < Count; ++i)
			{
				const FOKPxParticle& P = S.Particles[i];
				FKawaiiPhysicsModifyBone& Bone = ModifyBones[i];
				Bone.Index = i;
				Bone.ParentIndex = P.Parent;
				Bone.PhysicsSettings.Damping = FMath::Clamp(P.Damping * Softness.Damp, 0.f, 1.f);
				Bone.PhysicsSettings.Stiffness = FMath::Clamp(P.Elasticity * Softness.Pull, 0.f, 1.f); // pull to the animated pose
				Bone.PhysicsSettings.WorldDampingLocation = 1.f; // character motion arrives through the pose itself
				Bone.PhysicsSettings.WorldDampingRotation = 1.f;
				Bone.PhysicsSettings.Radius = FMath::Max(P.Radius, 0.f);
				Bone.Location = FVector(F3(P.Pos));
				Bone.PrevLocation = FVector(F3(P.Prev));
				Bone.PoseLocation = FVector(C.Anim[i]);
			}
			CapsuleLimits.Reset();
			SphericalLimits.Reset();
			bUseLegacyGravity = false;
			const FVector StepGravity(C.Gravity); // GravityInSimSpace is private: applied below as ComputeVerletStepVelocity does
			TargetFramerate = FMath::Max(1, FMath::RoundToInt(1.f / C.RefStep));

			const int32 Substeps = FMath::Max(1, C.Substeps);
			const float Sub = C.Dt / Substeps;
			DeltaTime = Sub; // GetStepDeltaTime() outside substepping
			const FVector RootFrom = ModifyBones[0].Location, RootTo = FVector(C.Anim[0]);
			float PrevDt = C.RefStep / FMath::Max(S.VelocityScale, 1e-3f); // Prev was written one reference step back
			for (int32 Step = 0; Step < Substeps; ++Step)
			{
				const float Blend = float(Step + 1) / Substeps;
				// Colliders where they are at this substep (Kawaii itself only sees the current pose).
				CapsuleLimits.Reset();
				SphericalLimits.Reset();
				for (int32 k = 0; k < C.Colliders.Num(); ++k)
				{
					const FVector A = FVector(FMath::Lerp(F3(C.FromColliders[k].A), F3(C.Colliders[k].A), Blend));
					const FVector B = FVector(FMath::Lerp(F3(C.FromColliders[k].B), F3(C.Colliders[k].B), Blend));
					const float Length = float((B - A).Size());
					if (Length < 1e-4f)
					{
						FSphericalLimit& Sphere = SphericalLimits.AddDefaulted_GetRef();
						Sphere.Location = A;
						Sphere.Radius = C.Colliders[k].Radius;
						Sphere.LimitType = ESphericalLimitType::Outer;
					}
					else
					{
						FCapsuleLimit& Capsule = CapsuleLimits.AddDefaulted_GetRef();
						Capsule.Location = (A + B) * 0.5;
						Capsule.Rotation = FQuat::FindBetweenNormals(FVector::UpVector, (B - A) / Length); // capsule axis = Z
						Capsule.Length = Length;
						Capsule.Radius = C.Colliders[k].Radius;
					}
				}
				FKawaiiPhysicsModifyBone& Root = ModifyBones[0];
				Root.PrevLocation = Root.Location;
				Root.Location = FMath::Lerp(RootFrom, RootTo, Blend); // kinematic follow (SimulateOnce does the same)
				const float Exponent = TargetFramerate * Sub;
				for (int32 i = 1; i < Count; ++i)
				{
					FKawaiiPhysicsModifyBone& Bone = ModifyBones[i];
					// ComputeVerletStepVelocity (its DeltaTimeOld is private): same formula.
					FVector Velocity = (Bone.Location - Bone.PrevLocation) / FMath::Max(PrevDt, KINDA_SMALL_NUMBER);
					Bone.PrevLocation = Bone.Location;
					Velocity *= (1.0f - Bone.PhysicsSettings.Damping);
					IntegrateVerletStepPosition(Bone, Velocity);
					// Gravity as DynamicBone applies it: a displacement per reference step (g*RefStep^2), spread over the
					// substeps. Kawaii's own velocity gravity (g*Sub per substep) sags by g*Sub/pull, which vanishes as the
					// substeps get smaller, so the weight setting did nothing on this backend.
					Bone.Location += StepGravity * (Sub * C.RefStep);
					ApplyStiffnessPull(Bone, ModifyBones[Bone.ParentIndex], Exponent);
				}
				for (int32 i = 1; i < Count; ++i)
				{
					AdjustBySphereCollision(ModifyBones[i], SphericalLimits);
					AdjustByCapsuleCollision(ModifyBones[i], CapsuleLimits);
				}
				for (int32 i = 1; i < Count; ++i)
				{
					FKawaiiPhysicsModifyBone& Bone = ModifyBones[i];
					const FKawaiiPhysicsModifyBone& Parent = ModifyBones[Bone.ParentIndex];
					const float BoneLength = float((Bone.PoseLocation - Parent.PoseLocation).Size());
					Bone.Location = (Bone.Location - Parent.Location).GetSafeNormal() * BoneLength + Parent.Location;
				}
				PrevDt = Sub;
			}
			const float Back = C.RefStep / FMath::Max(S.VelocityScale, 1e-3f);
			for (int32 i = 1; i < Count; ++i)
			{
				FOKPxParticle& P = S.Particles[i];
				const FKawaiiPhysicsModifyBone& Bone = ModifyBones[i];
				const FVector Velocity = (Bone.Location - Bone.PrevLocation) / FMath::Max(Sub, KINDA_SMALL_NUMBER);
				Put3(P.Pos, FVector3f(Bone.Location));
				Put3(P.Prev, FVector3f(Bone.Location - Velocity * Back));
			}
		}
	};

	UE::FMutex GKawaiiLock;
	TMap<uint64, TSharedPtr<FOKKawaiiNode>> GKawaiiNodes;

	// --------------------------------------------------------------------------------------------------------- Chaos
	// One Chaos::Softs::FPBDEvolution per DynamicBone set: particles (roots kinematic), bone-length springs, an anim
	// drive towards the animated pose, collision particles for the body capsules.
	struct FOKChaosBatch
	{
		UE::FMutex Lock;
		TUniquePtr<Chaos::Softs::FPBDEvolution> Evolution;
		TUniquePtr<Chaos::Softs::FPBDSpringConstraints> Springs;
		TUniquePtr<Chaos::Softs::FPBDAnimDriveConstraint> Drive;
		TArray<int32> Signature; // particle counts and parents of every chain: rebuild when it changes
		TArray<int32> Offsets;
		int32 ParticleCount = 0;
		int32 CollisionOffset = INDEX_NONE;
		int32 CollisionCapacity = 0;
		TArray<Chaos::Softs::FSolverVec3> AnimPositions, AnimVelocities;
		TArray<float> DriveStiffness, DriveDamping;
		TArray<Chaos::Softs::FSolverVec3> KinematicFrom, KinematicTo; // root particles, per particle index
		TArray<FVector4f> CollisionShape; // (radius, length) last geometry per collision slot
		TArray<FTransform3f> CollisionFrom, CollisionTo;
		bool bFresh = true; // just built: roots start where they are told
		double LastStep = 0.; // a gap (another solver ran) restarts the roots from where they are told
		float StepDuration = 1.f;
		float StepStart = 0.f;

		void Build(TArrayView<FOKBackendChain> Chains)
		{
			using namespace Chaos::Softs;
			Evolution = MakeUnique<FPBDEvolution>(FSolverParticles(), FSolverCollisionParticles(), TArray<Chaos::TVec3<int32>>(),
				FMath::Max(1, CVarChaosIterations.GetValueOnAnyThread()), (FSolverReal)0., (FSolverReal)0., (FSolverReal)0., (FSolverReal)0.);
			Offsets.Reset();
			ParticleCount = 0;
			TArray<Chaos::TVec2<int32>> Edges;
			for (int32 c = 0; c < Chains.Num(); ++c)
			{
				const FOKPxChainStep& S = *Chains[c].Step;
				const int32 Offset = Evolution->AddParticleRange(int32(S.ParticleCount), uint32(c), true);
				Offsets.Add(Offset);
				for (uint32 i = 1; i < S.ParticleCount; ++i) Edges.Add(Chaos::TVec2<int32>(Offset + S.Particles[i].Parent, Offset + int32(i)));
				ParticleCount += int32(S.ParticleCount);
			}
			FSolverParticles& Particles = Evolution->Particles();
			for (int32 i = 0; i < ParticleCount; ++i) { Particles.M(i) = 1.f; Particles.InvM(i) = 1.f; }
			// The springs take their rest lengths from the positions at construction: start from the animated pose.
			for (int32 c = 0; c < Chains.Num(); ++c)
			{
				for (int32 i = 0; i < Chains[c].Anim.Num(); ++i)
				{
					Particles.X(Offsets[c] + i) = FSolverVec3(Chains[c].Anim[i]);
					Particles.P(Offsets[c] + i) = Particles.X(Offsets[c] + i);
				}
			}
			AnimPositions.SetNumZeroed(ParticleCount);
			AnimVelocities.SetNumZeroed(ParticleCount);
			DriveStiffness.SetNumZeroed(ParticleCount);
			DriveDamping.Init(0.f, ParticleCount);
			KinematicFrom.SetNumZeroed(ParticleCount);
			KinematicTo.SetNumZeroed(ParticleCount);
			TArray<float> SpringMultipliers;
			Springs = MakeUnique<FPBDSpringConstraints>(Particles, 0, ParticleCount, Edges, TConstArrayView<float>(SpringMultipliers), FSolverVec2(1.f, 1.f));
			Drive = MakeUnique<FPBDAnimDriveConstraint>(0, ParticleCount, AnimPositions, AnimPositions, AnimVelocities,
				TConstArrayView<float>(DriveStiffness), TConstArrayView<float>(DriveDamping));
			Drive->SetProperties(FSolverVec2(0.f, 1.f), FSolverVec2(0.f, 1.f));
			const int32 InitIndex = Evolution->AddConstraintInitRange(1, true);
			Evolution->ConstraintInits()[InitIndex] = [this](FSolverParticles&, const FSolverReal Dt)
			{
				const int32 Iterations = Evolution->GetIterations();
				Springs->ApplyProperties(Dt, Iterations);
				Drive->ApplyProperties(Dt, Iterations);
			};
			const int32 RuleIndex = Evolution->AddConstraintRuleRange(1, true);
			Evolution->ConstraintRules()[RuleIndex] = [this](FSolverParticles& InParticles, const FSolverReal Dt)
			{
				Drive->Apply(InParticles, Dt);
				Springs->Apply(InParticles, Dt);
			};
			Evolution->SetKinematicUpdateFunction([this](FSolverParticles& InParticles, const FSolverReal SubDt, const FSolverReal LocalTime, const int32 Index)
			{
				// Called before the evolution advances its time (collision particles after): target the end of the substep.
				const float Alpha = FMath::Clamp((float(LocalTime + SubDt) - StepStart) / FMath::Max(StepDuration, 1e-6f), 0.f, 1.f);
				const FSolverVec3 X = KinematicFrom[Index] + (KinematicTo[Index] - KinematicFrom[Index]) * Alpha;
				InParticles.P(Index) = X;
				InParticles.X(Index) = X;
			});
			Evolution->SetCollisionKinematicUpdateFunction([this](FSolverCollisionParticles& InParticles, const FSolverReal, const FSolverReal LocalTime, const int32 Index)
			{
				const int32 Slot = Index - CollisionOffset;
				if (!CollisionFrom.IsValidIndex(Slot)) return;
				const float Alpha = FMath::Clamp((float(LocalTime) - StepStart) / FMath::Max(StepDuration, 1e-6f), 0.f, 1.f);
				FTransform3f T;
				T.Blend(CollisionFrom[Slot], CollisionTo[Slot], Alpha);
				InParticles.SetX(Index, T.GetLocation());
				InParticles.SetR(Index, T.GetRotation());
			});
			CollisionOffset = INDEX_NONE;
			CollisionCapacity = 0;
			CollisionShape.Reset();
			CollisionFrom.Reset();
			CollisionTo.Reset();
			bFresh = true;
		}

		void Step(TArrayView<FOKBackendChain> Chains)
		{
			using namespace Chaos::Softs;
			TArray<int32> Sig;
			for (const FOKBackendChain& C : Chains)
			{
				Sig.Add(int32(C.Step->ParticleCount));
				for (uint32 i = 0; i < C.Step->ParticleCount; ++i) Sig.Add(C.Step->Particles[i].Parent);
			}
			if (!Evolution || Sig != Signature) { Build(Chains); Signature = MoveTemp(Sig); }
			const double Now = FPlatformTime::Seconds();
			if (LastStep > 0. && Now - LastStep > 0.25) bFresh = true;
			LastStep = Now;

			FSolverParticles& Particles = Evolution->Particles();
			float Dt = 0.f;
			int32 Substeps = 1;
			for (int32 c = 0; c < Chains.Num(); ++c)
			{
				const FOKBackendChain& C = Chains[c];
				const FOKPxChainStep& S = *C.Step;
				Dt = FMath::Max(Dt, C.Dt);
				Substeps = FMath::Max(Substeps, C.Substeps);
				Evolution->SetGravity(FSolverVec3(C.Gravity), uint32(c));
				// OKPhysXSecondary.Hardness: the same pull/damping scales as PhysX.
				const OKPhysXSecondary::FSoftness Softness = OKPhysXSecondary::Softness(S.Particles[0].Flags);
				float Damping = 0.f, Radius = 0.f;
				for (uint32 i = 0; i < S.ParticleCount; ++i)
				{
					const int32 g = Offsets[c] + int32(i);
					const FOKPxParticle& P = S.Particles[i];
					AnimPositions[g] = FSolverVec3(C.Anim[i]);
					AnimVelocities[g] = FSolverVec3(0.f);
					if (P.Parent < 0)
					{
						Particles.InvM(g) = 0.f;
						Particles.PAndInvM(g).InvM = 0.f;
						KinematicTo[g] = FSolverVec3(F3(P.Pos));
						KinematicFrom[g] = bFresh ? KinematicTo[g] : FSolverVec3(Particles.X(g)); // swept from last frame's root
						continue;
					}
					Particles.InvM(g) = 1.f;
					Particles.PAndInvM(g).InvM = 1.f;
					Particles.X(g) = FSolverVec3(F3(P.Pos));
					Particles.P(g) = Particles.X(g);
					Particles.V(g) = FSolverVec3((F3(P.Pos) - F3(P.Prev)) * (S.VelocityScale / C.RefStep));
					DriveStiffness[g - 0] = FMath::Clamp(P.Elasticity * Softness.Pull, 0.f, 1.f);
					Damping = FMath::Max(Damping, P.Damping * Softness.Damp);
					Radius = FMath::Max(Radius, P.Radius);
				}
				// DynamicBone damping is per reference step; Chaos damps per substep.
				Evolution->SetDamping(FMath::Clamp(Damping * (C.Dt / FMath::Max(C.Substeps, 1)) / C.RefStep, 0.f, 1.f), uint32(c));
				Evolution->SetCollisionThickness(Radius, uint32(c));
			}

			// Collision particles: union of every chain's colliders (they are nearly the same body capsules).
			TArray<const FOKPxCapsule*> Shapes, FromShapes;
			for (const FOKBackendChain& C : Chains) for (int32 k = 0; k < C.Colliders.Num(); ++k) { Shapes.Add(&C.Colliders[k]); FromShapes.Add(&C.FromColliders[k]); }
			if (Shapes.Num() > CollisionCapacity)
			{
				const int32 Add = Shapes.Num() - CollisionCapacity;
				const int32 Offset = Evolution->AddCollisionParticleRange(Add, uint32(INDEX_NONE), true); // INDEX_NONE: collides with every chain's group (0 = chain 0 only)
				if (CollisionOffset == INDEX_NONE) CollisionOffset = Offset;
				CollisionCapacity += Add;
				CollisionShape.SetNum(CollisionCapacity);
				CollisionFrom.SetNum(CollisionCapacity);
				CollisionTo.SetNum(CollisionCapacity);
				for (int32 k = CollisionCapacity - Add; k < CollisionCapacity; ++k) CollisionShape[k] = FVector4f(-1.f);
			}
			FSolverCollisionParticles& Collisions = Evolution->CollisionParticles();
			for (int32 k = 0; k < CollisionCapacity; ++k)
			{
				const int32 Index = CollisionOffset + k;
				if (k >= Shapes.Num())
				{
					CollisionFrom[k] = CollisionTo[k] = FTransform3f(FVector3f(1e6f)); // parked
					continue;
				}
				auto Frame = [](const FOKPxCapsule& C, float& Length)
				{
					const FVector3f A = F3(C.A), B = F3(C.B);
					Length = (B - A).Size();
					const FQuat4f Q = Length > 1e-4f ? FQuat4f::FindBetweenNormals(FVector3f::UpVector, (B - A) / Length) : FQuat4f::Identity;
					return FTransform3f(Q, (A + B) * 0.5f);
				};
				float Length = 0.f, FromLength = 0.f;
				CollisionTo[k] = Frame(*Shapes[k], Length);
				CollisionFrom[k] = Frame(*FromShapes[k], FromLength);
				const float Radius = FMath::Max(Shapes[k]->Radius, 1e-4f);
				if (FMath::Abs(CollisionShape[k].X - Radius) > 1e-4f || FMath::Abs(CollisionShape[k].Y - Length) > 1e-4f)
				{
					const Chaos::FVec3 Half(0., 0., Length * 0.5);
					Collisions.SetGeometry(Index, Length > 1e-4f
						? Chaos::FImplicitObjectPtr(MakeImplicitObjectPtr<Chaos::FCapsule>(-Half, Half, Chaos::FReal(Radius)))
						: Chaos::FImplicitObjectPtr(MakeImplicitObjectPtr<Chaos::TSphere<Chaos::FReal, 3>>(Chaos::FVec3(0.), Chaos::FReal(Radius))));
					CollisionShape[k] = FVector4f(Radius, Length, 0.f, 0.f);
				}
			}

			bFresh = false;
			if (Dt <= 0.f) return;
			const float Sub = Dt / Substeps;
			StepStart = float(Evolution->GetTime());
			StepDuration = Dt;
			for (int32 Step = 0; Step < Substeps; ++Step) Evolution->AdvanceOneTimeStep(Sub);

			for (int32 c = 0; c < Chains.Num(); ++c)
			{
				FOKBackendChain& C = Chains[c];
				FOKPxChainStep& S = *C.Step;
				const float Back = C.RefStep / FMath::Max(S.VelocityScale, 1e-3f);
				for (uint32 i = 1; i < S.ParticleCount; ++i)
				{
					const int32 g = Offsets[c] + int32(i);
					const FVector3f X(Particles.X(g)), V(Particles.V(g));
					Put3(S.Particles[i].Pos, X);
					Put3(S.Particles[i].Prev, X - V * Back);
				}
			}
		}
	};

	UE::FMutex GChaosLock;
	TMap<uint64, TSharedPtr<FOKChaosBatch>> GChaosBatches;
}

void OKKawaiiStep(TArrayView<FOKBackendChain> Chains)
{
	for (FOKBackendChain& C : Chains)
	{
		TSharedPtr<FOKKawaiiNode> Node;
		{
			UE::TUniqueLock Guard(GKawaiiLock);
			TSharedPtr<FOKKawaiiNode>& Slot = GKawaiiNodes.FindOrAdd(C.Step->Key);
			if (!Slot) Slot = MakeShared<FOKKawaiiNode>();
			Node = Slot;
		}
		Node->Step(C);
	}
}

void OKChaosStep(uint64 BatchKey, TArrayView<FOKBackendChain> Chains)
{
	TSharedPtr<FOKChaosBatch> Batch;
	{
		UE::TUniqueLock Guard(GChaosLock);
		TSharedPtr<FOKChaosBatch>& Slot = GChaosBatches.FindOrAdd(BatchKey);
		if (!Slot) Slot = MakeShared<FOKChaosBatch>();
		Batch = Slot;
	}
	UE::TUniqueLock Guard(Batch->Lock);
	Batch->Step(Chains);
}

// ----------------------------------------------------------------------------------------------------- DynamicBone
// The game's own DynamicBone update on the prepared inputs: Verlet with damping and gravity, elasticity toward the
// animated goal, the stiffness range, the freeze plane and the exact bone length, parents first, then collision with
// the shared contact handling. One step per frame, time-scaled to the reference step (at 60 fps exactly the game's
// update): fixed 60 Hz steps at a higher frame rate move the hair 0, 1 or 2 steps per frame and it visibly stutters.
// Pos/Prev hold the displacement of one reference step, as the other solvers' particles.
void OKDynamicBoneStep(TArrayView<FOKBackendChain> Chains)
{
	for (FOKBackendChain& C : Chains)
	{
		FOKPxChainStep& S = *C.Step;
		const int32 Count = int32(S.ParticleCount);
		if (Count < 2) continue;
		const float Ref = FMath::Max(C.RefStep, 1e-4f);
		// Fraction of a reference step this frame covers (a long hitch is clamped like DynamicBone's 3 steps).
		const float F = FMath::Clamp(C.Dt / Ref, 0.f, 3.f);
		const int32 Steps = F > 0.f ? 1 : 0;
		const OKPhysXSecondary::FSoftness Soft = OKPhysXSecondary::Softness(S.Particles[0].Flags);
		const float VelocityScale = FMath::Max(S.VelocityScale, 1e-3f);
		const FVector3f Gravity = C.Gravity * (C.Dt * C.Dt); // DynamicBone's force per step, over this frame's time
		// Per-step factors raised to the frame's fraction of a step: same decay per second at any frame rate.
		auto PerFrame = [F](float PerStep) { return 1.f - FMath::Pow(FMath::Clamp(1.f - PerStep, 0.f, 1.f), F); };
		TArray<FVector3f, TInlineAllocator<64>> X, Prev;
		X.SetNumUninitialized(Count); Prev.SetNumUninitialized(Count);
		for (int32 i = 0; i < Count; ++i) { X[i] = F3(S.Particles[i].Pos); Prev[i] = F3(S.Particles[i].Prev); }
		X[0] = C.Anim[0];
		// Elasticity, stiffness range, freeze plane and bone length of particle i against its parent (UpdateParticles2).
		auto Constrain = [&](int32 i, bool bPull)
		{
			const FOKPxParticle& P = S.Particles[i];
			const int32 Parent = P.Parent;
			const FVector3f Goal = X[Parent] + F3(P.GoalOffset);
			if (bPull) X[i] += (Goal - X[i]) * PerFrame(FMath::Clamp(P.Elasticity * Soft.Pull, 0.f, 1.f));
			if (P.Stiffness > 0.f)
			{
				const FVector3f D = Goal - X[i];
				const float Limit = P.RestLen * (1.f - P.Stiffness) * 2.f * Soft.Range;
				const float Len = D.Size();
				if (Len > Limit && Len > 1e-8f) X[i] += D * ((Len - Limit) / Len);
			}
			const FVector3f Normal = F3(P.FreezeNormal);
			if (Normal.SizeSquared() > 0.25f)
			{
				const FVector3f N = Normal.GetSafeNormal();
				X[i] -= N * FVector3f::DotProduct(X[i] - X[Parent], N);
			}
			const float Rest = FMath::Max(P.RestLen, 1e-5f);
			const float MinLen = Rest / FMath::Max(1.f - P.MinFraction, 0.05f), MaxLen = Rest / FMath::Max(1.f - P.MaxFraction, 0.05f);
			const FVector3f Arm = X[i] - X[Parent];
			const float Len = Arm.Size();
			if (Len > 1e-8f) X[i] = X[Parent] + Arm * (FMath::Clamp(Len, MinLen, MaxLen) / Len);
		};
		if (Steps > 0)
		{
			TArray<FVector3f, TInlineAllocator<64>> Start;
			Start.SetNumUninitialized(Count);
			for (int32 i = 1; i < Count; ++i)
			{
				Start[i] = X[i];
				// Velocity per reference step (Pos - Prev), kept over this frame's fraction with the step's damping.
				const FVector3f V = (X[i] - Prev[i]) * VelocityScale;
				const float Keep = FMath::Pow(1.f - FMath::Clamp(S.Particles[i].Damping * Soft.Damp, 0.f, 1.f), F);
				X[i] += V * (F * Keep) + Gravity;
			}
			for (int32 i = 1; i < Count; ++i) Constrain(i, true);
			// Collision as DynamicBone collides in UpdateParticles2, with the shared contact handling (soft skin layer and
			// exact push-out with per-particle radii, colliders swept over the frame). Velocities per second.
			TArray<FVector3f, TInlineAllocator<64>> V;
			V.SetNumUninitialized(Count);
			V[0] = FVector3f::ZeroVector;
			for (int32 i = 1; i < Count; ++i) V[i] = (X[i] - Start[i]) / C.Dt;
			if (C.SubstepContact) C.SubstepContact(1.f, 1, X.GetData(), V.GetData());
			// Back to one reference step of displacement.
			for (int32 i = 1; i < Count; ++i) Prev[i] = X[i] - V[i] * Ref;
		}
		// No time this frame (SkipUpdateParticles): the chain follows its moved parents within its limits.
		else for (int32 i = 1; i < Count; ++i) Constrain(i, false);
		for (int32 i = 1; i < Count; ++i) { Put3(S.Particles[i].Pos, X[i]); Put3(S.Particles[i].Prev, Prev[i]); }
	}
}

void OKBackendsRelease(uint64 Key)
{
	{
		UE::TUniqueLock Guard(GKawaiiLock);
		GKawaiiNodes.Remove(Key);
	}
	{
		UE::TUniqueLock Guard(GChaosLock);
		GChaosBatches.Remove(Key);
	}
	OKJoltRelease(Key);
	OKMuJoCoRelease(Key);
}
