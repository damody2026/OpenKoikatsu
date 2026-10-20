#include "OKSecondaryDrive.h"
#include "Async/Mutex.h"
#include "Async/UniqueLock.h"

// Comparison backend: compiled only when OK_SECONDARY_COMPARISON_BACKENDS (not Shipping, OKPhysXSecondary.Build.cs);
// otherwise the stubs at the end keep the dispatch working (backends 4/5 then run PhysX, OKSupportedBackend).
#if OK_SECONDARY_COMPARISON_BACKENDS
// Jolt Physics v5.6.0 (MIT, Jorrit Rouwe), static library staged by Tools/build_physics_backends.lua.
THIRD_PARTY_INCLUDES_START
// UE builds with /Zp8; the library was built with the default /Zp16. Without this, derived classes pack their first
// members into the base class tail padding differently (Jolt: SoftBodyMotionProperties::mVertices 8 bytes off).
#pragma pack(push, 16)
#pragma push_macro("check")
#undef check
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
#pragma pop_macro("check")
#pragma pack(pop)
THIRD_PARTY_INCLUDES_END

static TAutoConsoleVariable<int32> CVarJoltIterations(
	TEXT("OKPhysXSecondary.JoltIterations"), 4, TEXT("Soft body solver iterations per substep of the Jolt backend."), ECVF_Default);
static TAutoConsoleVariable<float> CVarJoltFriction(
	TEXT("OKPhysXSecondary.JoltFriction"), 0.2f, TEXT("Friction of the Jolt soft bodies against the body capsules."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarJoltCollisionSteps(
	TEXT("OKPhysXSecondary.JoltCollisionSteps"), 1, TEXT("PhysicsSystem::Update collision steps per substep of the Jolt backend."), ECVF_Default);
static TAutoConsoleVariable<float> CVarJoltCompliance(
	TEXT("OKPhysXSecondary.JoltCompliance"), 0.f, TEXT("Compliance (1/stiffness) of the Jolt bone-length edges; 0 = rigid. Changing it rebuilds the soft bodies."), ECVF_Default);

// Every chain is one Jolt soft body (vertices = particles, roots with inverse mass 0 moved by velocity, rigid edges for
// the bone lengths) colliding with its own kinematic capsules. Object layer 2c+1 = soft body of chain c, 2c+2 = its
// colliders, so each chain only meets the colliders PrepareJob chose for it. Goal spring, damping and gravity come from
// FOKParticleDrive as a velocity change before every substep (Jolt's own gravity/damping are off).
namespace
{
	FORCEINLINE JPH::Vec3 J3(const FVector3f& V) { return JPH::Vec3(V.X, V.Y, V.Z); }
	FORCEINLINE FVector3f U3(JPH::Vec3Arg V) { return FVector3f(V.GetX(), V.GetY(), V.GetZ()); }
	FORCEINLINE FVector3f F3(const float* P) { return FVector3f(P[0], P[1], P[2]); }
	FORCEINLINE void Put3(float* Out, const FVector3f& V) { Out[0] = V.X; Out[1] = V.Y; Out[2] = V.Z; }

	class FLayers final : public JPH::BroadPhaseLayerInterface
	{
	public:
		JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer Layer) const override { return JPH::BroadPhaseLayer(JPH::uint8(Layer & 1)); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer Layer) const override { return Layer.GetValue() ? "Soft" : "Colliders"; }
#endif
	};
	class FObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer Layer, JPH::BroadPhaseLayer Phase) const override { return (Layer & 1) != Phase.GetValue(); }
	};
	class FPairs final : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer A, JPH::ObjectLayer B) const override
		{
			return ((A & 1) && B == A + 1) || ((B & 1) && A == B + 1);
		}
	};

	UE::FMutex GJoltInitLock;
	bool GJoltReady = false;
	void EnsureJolt()
	{
		UE::TUniqueLock Guard(GJoltInitLock);
		if (GJoltReady) return;
		JPH::RegisterDefaultAllocator();
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();
		GJoltReady = true; // kept for the process lifetime
	}

	struct FJoltBatch
	{
		struct FChain
		{
			JPH::BodyID Soft;
			TArray<JPH::BodyID> Colliders;
			TArray<FVector2f> Dims; // (radius, length) of each collider's current shape
			FVector3f LastRoot = FVector3f::ZeroVector;
		};
		UE::FMutex Lock;
		FLayers Layers;
		FObjectVsBroadPhase ObjectVsBroadPhase;
		FPairs Pairs;
		TUniquePtr<JPH::TempAllocatorImpl> Temp;
		TUniquePtr<JPH::JobSystemSingleThreaded> Jobs;
		TUniquePtr<JPH::PhysicsSystem> System;
		TArray<FChain> Chains;
		TArray<int32> Signature;
		int32 Capacity = 0;
		float BuiltCompliance = -1.f;
		bool bFresh = true;
		double LastStep = 0.;

		~FJoltBatch() { Clear(); }

		void Clear()
		{
			if (!System) return;
			JPH::BodyInterface& Bodies = System->GetBodyInterfaceNoLock();
			for (FChain& C : Chains)
			{
				if (!C.Soft.IsInvalid()) { Bodies.RemoveBody(C.Soft); Bodies.DestroyBody(C.Soft); }
				for (const JPH::BodyID& Id : C.Colliders) { Bodies.RemoveBody(Id); Bodies.DestroyBody(Id); }
			}
			Chains.Reset();
		}

		static JPH::Ref<JPH::Shape> MakeShape(float Radius, float Length)
		{
			if (Length > 1e-4f) return new JPH::CapsuleShape(Length * 0.5f, Radius); // axis = Y
			return new JPH::SphereShape(Radius);
		}

		static void Frame(const FOKPxCapsule& C, FVector3f& Center, FQuat4f& Rotation, float& Length)
		{
			const FVector3f A = F3(C.A), B = F3(C.B);
			Length = (B - A).Size();
			Center = (A + B) * 0.5f;
			Rotation = Length > 1e-4f ? FQuat4f::FindBetweenNormals(FVector3f(0.f, 1.f, 0.f), (B - A) / Length) : FQuat4f::Identity;
		}

		void Build(TArrayView<FOKBackendChain> In)
		{
			Clear();
			BuiltCompliance = FMath::Max(CVarJoltCompliance.GetValueOnAnyThread(), 0.f);
			int32 Need = 0;
			for (const FOKBackendChain& C : In) Need += 1 + C.Colliders.Num();
			if (!System || Need > Capacity)
			{
				Capacity = int32(FMath::Max(1024u, FMath::RoundUpToPowerOfTwo(uint32(Need * 2))));
				System = MakeUnique<JPH::PhysicsSystem>();
				System->Init(JPH::uint(Capacity), 0, JPH::uint(Capacity * 4), JPH::uint(Capacity * 4), Layers, ObjectVsBroadPhase, Pairs);
				System->SetGravity(JPH::Vec3::sZero());
				Temp = MakeUnique<JPH::TempAllocatorImpl>(8 * 1024 * 1024);
				Jobs = MakeUnique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
			}
			JPH::BodyInterface& Bodies = System->GetBodyInterfaceNoLock();
			for (int32 c = 0; c < In.Num(); ++c)
			{
				const FOKBackendChain& C = In[c];
				const FOKPxChainStep& S = *C.Step;
				JPH::Ref<JPH::SoftBodySharedSettings> Shared = new JPH::SoftBodySharedSettings;
				float Radius = 0.f;
				for (uint32 i = 0; i < S.ParticleCount; ++i)
				{
					const FOKPxParticle& P = S.Particles[i];
					const FVector3f X = C.Anim[i];
					Shared->mVertices.push_back(JPH::SoftBodySharedSettings::Vertex(JPH::Float3(X.X, X.Y, X.Z), JPH::Float3(0, 0, 0), P.Parent < 0 ? 0.f : 1.f));
					if (P.Parent < 0) continue;
					JPH::SoftBodySharedSettings::Edge Edge(JPH::uint32(P.Parent), JPH::uint32(i), BuiltCompliance);
					Edge.mRestLength = P.RestLen > 1e-5f ? P.RestLen : FVector3f::Distance(C.Anim[P.Parent], X);
					Shared->mEdgeConstraints.push_back(Edge);
					Radius = FMath::Max(Radius, P.Radius);
				}
				Shared->Optimize();
				JPH::SoftBodyCreationSettings Settings(Shared, JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::ObjectLayer(2 * c + 1));
				Settings.mNumIterations = JPH::uint32(FMath::Max(1, CVarJoltIterations.GetValueOnAnyThread()));
				Settings.mLinearDamping = 0.f;
				Settings.mGravityFactor = 0.f;
				Settings.mVertexRadius = FMath::Max(Radius, 1e-4f);
				Settings.mUpdatePosition = false; // vertices stay in world space
				Settings.mAllowSleeping = false;
				Settings.mFriction = 0.2f;
				FChain& Chain = Chains.AddDefaulted_GetRef();
				Chain.Soft = Bodies.CreateAndAddSoftBody(Settings, JPH::EActivation::Activate);
				Chain.LastRoot = C.Anim[0];
			}
			bFresh = true;
		}

		void SyncColliders(int32 c, const FOKBackendChain& C)
		{
			FChain& Chain = Chains[c];
			JPH::BodyInterface& Bodies = System->GetBodyInterfaceNoLock();
			while (Chain.Colliders.Num() < C.Colliders.Num())
			{
				JPH::BodyCreationSettings Settings(MakeShape(0.01f, 0.f), JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic, JPH::ObjectLayer(2 * c + 2));
				Settings.mAllowSleeping = false;
				Chain.Colliders.Add(Bodies.CreateAndAddBody(Settings, JPH::EActivation::Activate));
				Chain.Dims.Add(FVector2f(0.01f, 0.f));
			}
			for (int32 k = 0; k < Chain.Colliders.Num(); ++k)
			{
				if (k >= C.Colliders.Num())
				{
					Bodies.SetPositionAndRotation(Chain.Colliders[k], JPH::RVec3(1e5f, 1e5f, 1e5f), JPH::Quat::sIdentity(), JPH::EActivation::DontActivate); // parked
					continue;
				}
				FVector3f Center; FQuat4f Rotation; float Length;
				Frame(C.Colliders[k], Center, Rotation, Length); // the shape follows the current pose
				const float Radius = FMath::Max(C.Colliders[k].Radius, 1e-4f);
				if (FMath::Abs(Chain.Dims[k].X - Radius) > 5e-4f || FMath::Abs(Chain.Dims[k].Y - Length) > 5e-4f)
				{
					Bodies.SetShape(Chain.Colliders[k], MakeShape(Radius, Length), false, JPH::EActivation::Activate);
					Chain.Dims[k] = FVector2f(Radius, Length);
				}
				FVector3f From; FQuat4f FromRotation; float FromLength;
				Frame(C.FromColliders[k], From, FromRotation, FromLength);
				Bodies.SetPositionAndRotation(Chain.Colliders[k], JPH::RVec3(J3(From)), JPH::Quat(FromRotation.X, FromRotation.Y, FromRotation.Z, FromRotation.W), JPH::EActivation::Activate);
			}
		}

		void Step(TArrayView<FOKBackendChain> In)
		{
			TArray<int32> Sig;
			for (const FOKBackendChain& C : In)
			{
				Sig.Add(int32(C.Step->ParticleCount));
				for (uint32 i = 0; i < C.Step->ParticleCount; ++i) Sig.Add(C.Step->Particles[i].Parent);
			}
			if (!System || Sig != Signature || FMath::Max(CVarJoltCompliance.GetValueOnAnyThread(), 0.f) != BuiltCompliance) { Build(In); Signature = MoveTemp(Sig); }
			const JPH::uint32 Iterations = JPH::uint32(FMath::Max(1, CVarJoltIterations.GetValueOnAnyThread()));
			const float Friction = FMath::Max(CVarJoltFriction.GetValueOnAnyThread(), 0.f);
			const float RadiusScale = OKAltRadiusScale();
			const double Now = FPlatformTime::Seconds();
			if (LastStep > 0. && Now - LastStep > 0.25) bFresh = true;
			LastStep = Now;

			float Dt = 0.f;
			int32 Substeps = 1;
			for (const FOKBackendChain& C : In) { Dt = FMath::Max(Dt, C.Dt); Substeps = FMath::Max(Substeps, C.Substeps); }
			if (Dt <= 0.f) return;
			const float Sub = Dt / Substeps;

			TArray<FOKParticleDrive> Drives;
			Drives.SetNum(In.Num());
			TArray<FVector3f> RootFrom, RootTo;
			const JPH::BodyLockInterfaceNoLock& Locks = System->GetBodyLockInterfaceNoLock();
			for (int32 c = 0; c < In.Num(); ++c)
			{
				const FOKBackendChain& C = In[c];
				const FOKPxChainStep& S = *C.Step;
				Drives[c].Init(C);
				SyncColliders(c, C);
				RootTo.Add(F3(S.Particles[0].Pos));
				RootFrom.Add(OKRootFrom(Chains[c].LastRoot, RootTo.Last(), bFresh));
				JPH::BodyLockWrite BodyLock(Locks, Chains[c].Soft);
				if (!BodyLock.Succeeded() || !BodyLock.GetBody().IsSoftBody()
					|| static_cast<JPH::SoftBodyMotionProperties*>(BodyLock.GetBody().GetMotionProperties())->GetVertices().size() != S.ParticleCount)
				{
					UE_LOG(LogTemp, Warning, TEXT("OKPhysXSecondary Jolt: chain %d body %u lock=%d soft=%d particles=%u vertices=%d, rebuilding"), c, Chains[c].Soft.GetIndexAndSequenceNumber(),
						BodyLock.Succeeded() ? 1 : 0, BodyLock.Succeeded() && BodyLock.GetBody().IsSoftBody() ? 1 : 0, S.ParticleCount,
						BodyLock.Succeeded() && BodyLock.GetBody().IsSoftBody() ? int32(static_cast<JPH::SoftBodyMotionProperties*>(BodyLock.GetBody().GetMotionProperties())->GetVertices().size()) : -1);
					Signature.Reset(); // rebuilt next frame
					return;
				}
				JPH::SoftBodyMotionProperties* Motion = static_cast<JPH::SoftBodyMotionProperties*>(BodyLock.GetBody().GetMotionProperties());
				float Radius = 0.f;
				for (uint32 i = 1; i < S.ParticleCount; ++i) Radius = FMath::Max(Radius, S.Particles[i].Radius);
				Motion->SetNumIterations(Iterations);
				Motion->SetVertexRadius(FMath::Max(Radius * RadiusScale, 1e-4f));
				BodyLock.GetBody().SetFriction(Friction);
				for (uint32 i = 0; i < S.ParticleCount; ++i)
				{
					const FOKPxParticle& P = S.Particles[i];
					JPH::SoftBodyVertex& V = Motion->GetVertex(i);
					if (P.Parent < 0) { V.mPosition = J3(RootFrom.Last()); continue; }
					V.mPosition = J3(F3(P.Pos));
					V.mVelocity = J3((F3(P.Pos) - F3(P.Prev)) * (S.VelocityScale / C.RefStep));
				}
			}
			bFresh = false;

			TArray<FVector3f> X, Vel;
			JPH::BodyInterface& Bodies = System->GetBodyInterfaceNoLock();
			for (int32 Step = 0; Step < Substeps; ++Step)
			{
				const float A0 = float(Step) / Substeps, A1 = float(Step + 1) / Substeps;
				for (int32 c = 0; c < In.Num(); ++c)
				{
					const FOKBackendChain& C = In[c];
					for (int32 k = 0; k < C.Colliders.Num(); ++k)
					{
						FVector3f From, To; FQuat4f FromQ, ToQ; float L;
						Frame(C.FromColliders[k], From, FromQ, L);
						Frame(C.Colliders[k], To, ToQ, L);
						const FQuat4f Q = FQuat4f::Slerp(FromQ, ToQ, A1);
						Bodies.MoveKinematic(Chains[c].Colliders[k], JPH::RVec3(J3(FMath::Lerp(From, To, A1))), JPH::Quat(Q.X, Q.Y, Q.Z, Q.W), Sub);
					}
					JPH::BodyLockWrite BodyLock(Locks, Chains[c].Soft);
					if (!BodyLock.Succeeded() || !BodyLock.GetBody().IsSoftBody()) continue;
					JPH::SoftBodyMotionProperties* Motion = static_cast<JPH::SoftBodyMotionProperties*>(BodyLock.GetBody().GetMotionProperties());
					const int32 Count = int32(C.Step->ParticleCount);
					X.SetNumUninitialized(Count);
					Vel.SetNumUninitialized(Count);
					for (int32 i = 0; i < Count; ++i) { X[i] = U3(Motion->GetVertex(i).mPosition); Vel[i] = U3(Motion->GetVertex(i).mVelocity); }
					const FVector3f R0 = FMath::Lerp(RootFrom[c], RootTo[c], A0), R1 = FMath::Lerp(RootFrom[c], RootTo[c], A1);
					for (int32 i = 0; i < Count; ++i) if (Drives[c].Parent[i] < 0) Vel[i] = (R1 - R0) / Sub;
					for (int32 i = 0; i < Count; ++i)
					{
						JPH::SoftBodyVertex& V = Motion->GetVertex(i);
						if (Drives[c].Parent[i] < 0) { V.mPosition = J3(R0); V.mVelocity = J3(Vel[i]); }
						else V.mVelocity = J3(Drives[c].Velocity(i, X.GetData(), Vel.GetData(), Sub));
					}
				}
				System->Update(Sub, FMath::Clamp(CVarJoltCollisionSteps.GetValueOnAnyThread(), 1, 8), Temp.Get(), Jobs.Get());
				for (int32 c = 0; c < In.Num(); ++c)
				{
					if (!In[c].SubstepContact) continue;
					JPH::BodyLockWrite BodyLock(Locks, Chains[c].Soft);
					if (!BodyLock.Succeeded() || !BodyLock.GetBody().IsSoftBody()) continue;
					JPH::SoftBodyMotionProperties* Motion = static_cast<JPH::SoftBodyMotionProperties*>(BodyLock.GetBody().GetMotionProperties());
					const int32 Count = int32(In[c].Step->ParticleCount);
					X.SetNumUninitialized(Count); Vel.SetNumUninitialized(Count);
					for (int32 i = 0; i < Count; ++i) { X[i] = U3(Motion->GetVertex(i).mPosition); Vel[i] = U3(Motion->GetVertex(i).mVelocity); }
					In[c].SubstepContact(A1, Substeps, X.GetData(), Vel.GetData());
					for (int32 i = 1; i < Count; ++i) { Motion->GetVertex(i).mPosition = J3(X[i]); Motion->GetVertex(i).mVelocity = J3(Vel[i]); }
				}
			}

			for (int32 c = 0; c < In.Num(); ++c)
			{
				FOKBackendChain& C = In[c];
				FOKPxChainStep& S = *C.Step;
				const float Back = C.RefStep / FMath::Max(S.VelocityScale, 1e-3f);
				JPH::BodyLockRead BodyLock(Locks, Chains[c].Soft);
				if (!BodyLock.Succeeded() || !BodyLock.GetBody().IsSoftBody()) continue;
				const JPH::SoftBodyMotionProperties* Motion = static_cast<const JPH::SoftBodyMotionProperties*>(BodyLock.GetBody().GetMotionProperties());
				for (uint32 i = 1; i < S.ParticleCount; ++i)
				{
					const FVector3f P = U3(Motion->GetVertex(i).mPosition), V = U3(Motion->GetVertex(i).mVelocity);
					Put3(S.Particles[i].Pos, P);
					Put3(S.Particles[i].Prev, P - V * Back);
				}
				Chains[c].LastRoot = RootTo[c];
			}
		}
	};

	UE::FMutex GJoltLock;
	TMap<uint64, TSharedPtr<FJoltBatch>> GJoltBatches;
}

void OKJoltStep(uint64 BatchKey, TArrayView<FOKBackendChain> Chains)
{
	EnsureJolt();
	TSharedPtr<FJoltBatch> Batch;
	{
		UE::TUniqueLock Guard(GJoltLock);
		TSharedPtr<FJoltBatch>& Slot = GJoltBatches.FindOrAdd(BatchKey);
		if (!Slot) Slot = MakeShared<FJoltBatch>();
		Batch = Slot;
	}
	UE::TUniqueLock Guard(Batch->Lock);
	Batch->Step(Chains);
}

void OKJoltRelease(uint64 Key)
{
	TSharedPtr<FJoltBatch> Batch;
	{
		UE::TUniqueLock Guard(GJoltLock);
		GJoltBatches.RemoveAndCopyValue(Key, Batch);
	}
	if (Batch) { UE::TUniqueLock Guard(Batch->Lock); Batch->Clear(); }
}
#else
void OKJoltStep(uint64, TArrayView<FOKBackendChain>) {}
void OKJoltRelease(uint64) {}
#endif
