#include "OKPhysXSecondary.h"
#include "OKPhysXSecondaryAbi.h"
#include "OKSecondaryBackends.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include <atomic>
#include "Async/Mutex.h"
#include "Async/UniqueLock.h"
#include "Algo/StableSort.h"

// PhysX headers use member functions named check()/verify(), which UE defines as macros.
#pragma push_macro("check")
#pragma push_macro("verify")
#undef check
#undef verify
THIRD_PARTY_INCLUDES_START
#include "PxPhysicsAPI.h"
THIRD_PARTY_INCLUDES_END
#pragma pop_macro("verify")
#pragma pop_macro("check")

DEFINE_LOG_CATEGORY_STATIC(LogOKPhysX, Log, All);

static TAutoConsoleVariable<int32> CVarEnabled(
	TEXT("OKPhysXSecondary.Enabled"), 1,
	TEXT("1: hair/breast/skirt DynamicBone chains are simulated by PhysX. 0: built-in Verlet solver."), ECVF_Default);
static TAutoConsoleVariable<float> CVarSettleSeconds(
	TEXT("OKPhysXSecondary.SettleSeconds"), 0.5f, TEXT("After a pose/state switch, solve with SettleBackend for this many seconds first (pops hair that the new pose put inside the body back out). 0 = off."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarSettleBackend(
	TEXT("OKPhysXSecondary.SettleBackend"), 2, TEXT("Solver used while settling after a pose switch: 0 PhysX, 1 KawaiiPhysics, 2 Chaos PBD, 4 Jolt, 5 MuJoCo (3 was Bullet: removed, falls back to 0)."), ECVF_Default);
static std::atomic<double> GSettleUntil{0.};
static bool Settling() { return FPlatformTime::Seconds() < GSettleUntil.load(); }
// Bumped by OKPhysXSecondary.ResetState and whenever the solver changes: every chain restarts from its animated pose
// (particles on their goals at rest, no collider/frame history) and the other backends drop their worlds, so nothing a
// previous solver left behind carries over.
static std::atomic<int32> GResetEpoch{1};
static FAutoConsoleCommand GResetStateCommand(TEXT("OKPhysXSecondary.ResetState"), TEXT("Put every chain back on its animated pose and rebuild the solvers' state."),
	FConsoleCommandDelegate::CreateLambda([]() { ++GResetEpoch; }));
static FAutoConsoleCommand GSettleCommand(TEXT("OKPhysXSecondary.Settle"), TEXT("OKPhysXSecondary.Settle [seconds]: settle now (default SettleSeconds)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) { OKPhysXSecondary::Settle(Args.Num() ? FCString::Atof(*Args[0]) : -1.f); }));
static TAutoConsoleVariable<int32> CVarClearanceMode(
	TEXT("OKPhysXSecondary.ClearanceMode"), 2,
	TEXT("How body colliders make room for the animated pose. 0: one shrink per chain (its worst particle), 1: per particle, ")
	TEXT("2: per particle, but at most ClearanceMaxAllow inside any body the chain does not hang from (proxy roughness, not pose penetration). -1: no shrink."), ECVF_Default);
static TAutoConsoleVariable<float> CVarClearanceMaxAllow(
	TEXT("OKPhysXSecondary.ClearanceMaxAllow"), 0.01f, TEXT("ClearanceMode 2: largest depth (m) a particle may keep inside a body proxy it does not hang from."), ECVF_Default);
static TAutoConsoleVariable<float> CVarClearanceAttachMargin(
	TEXT("OKPhysXSecondary.ClearanceAttachMargin"), 0.08f, TEXT("ClearanceMode 2: a body within this distance (m) of the chain root counts as the one it hangs on / rests against, and keeps the animated-pose allowance."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarSkipRootBody(
	TEXT("OKPhysXSecondary.SkipRootBody"), 1, TEXT("1: hair/accessory chains do not collide with the own body proxy nearest their root (within ClearanceAttachMargin; bangs and the head); the card's own colliders handle it. 0: collide with every proxy."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarPartnerAttach(
	TEXT("OKPhysXSecondary.PartnerAttach"), 0, TEXT("ClearanceMode 2: 1 = a partner's body near the chain root also counts as the body it hangs on (keeps the animated-pose allowance; old behaviour). 0 = only the own body; partners are always avoided."), ECVF_Default);
static TAutoConsoleVariable<float> CVarWeight(
	TEXT("OKPhysXSecondary.Weight"), 1.f,
	TEXT("Hair/cloth/accessory weight 1..10: 1 = the card's own DynamicBone gravity, 10 = plus full real gravity (9.81 m/s^2, toward the ground ")
	TEXT("whatever the character's orientation). Breast/soft-tissue chains keep their own settings. Every solver, the built-in Verlet included."), ECVF_Default);
static TAutoConsoleVariable<float> CVarHardness(
	TEXT("OKPhysXSecondary.Hardness"), 10.f,
	TEXT("Hair/cloth/accessory hardness 1..10: 10 = as authored on the card, 1 = softest (pull back to the animated pose down to 1/10, ")
	TEXT("3x the swing range, less damping with some bounce). Breast/soft-tissue chains keep their own settings. Every solver, the built-in Verlet included (Kawaii: pull and damping only)."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarSelfCollision(
	TEXT("OKPhysXSecondary.SelfCollision"), 1, TEXT("Bit 1: chains collide with the character's body capsules. Bit 2: chains collide with each other."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarSkinBodyCollision(
	TEXT("OKPhysXSecondary.SkinBodyCollision"), 0, TEXT("1: skin chains of the body mesh (uncensor crotch/anus pivots) also collide with the body capsules. 0: only their own colliders, as in the game (no garment follows those bones)."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarBodySelfCollision(
	TEXT("OKPhysXSecondary.BodySelfCollision"), 0, TEXT("1: breast/hip chains (root flag 2) also collide with the own body capsules and the other chains. 0: only their own colliders and the partner's body, as the game's DynamicBone_Ver02 (long hair behind the hips and hands on the chest pushed them in and kept them shaking)."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarProjectionPasses(
	TEXT("OKPhysXSecondary.ProjectionPasses"), 3, TEXT("Exact position passes after the PhysX step (bone length, then push out of every collider)."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarWorldInertia(
	TEXT("OKPhysXSecondary.WorldInertia"), 1, TEXT("1: moving or turning a character in the world makes its hair and cloth lag and swing."), ECVF_Default);
static TAutoConsoleVariable<float> CVarDebugHeadTilt(
	TEXT("OKPhysXSecondary.DebugHeadTilt"), 0.f, TEXT("Clipping audit: throw every character's head back by this many degrees on top of the animation."), ECVF_Default);
static TAutoConsoleVariable<float> CVarDriveDamping(
	TEXT("OKPhysXSecondary.DriveDamping"), 1.f, TEXT("Damping ratio of the goal spring, relative to the parent (1 = critical)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarLimitStiffness(
	TEXT("OKPhysXSecondary.LimitStiffness"), 4000.f, TEXT("Stiffness (1/s^2) of the soft 'stay near the goal' limit; critically damped."), ECVF_Default);
static TAutoConsoleVariable<float> CVarLengthStiffness(
	TEXT("OKPhysXSecondary.LengthStiffness"), 100000.f, TEXT("Soft bone length: stiffness (1/s^2) of the spring outside the DynamicBone min/max range; critically damped. 0 = rigid joint."), ECVF_Default);
static TAutoConsoleVariable<float> CVarLengthSlack(
	TEXT("OKPhysXSecondary.LengthSlack"), 1.f, TEXT("Exact pass with a soft bone length: bones may end the frame this much longer/shorter than the DynamicBone range (1 = the range itself, as DynamicBone keeps the length each step; 1.1 let bangs on the head end 10% short and bare the forehead)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarContactStiffness(
	TEXT("OKPhysXSecondary.ContactStiffness"), 20000.f, TEXT("Soft contact: stiffness (1/s^2) of the skin layer around every body collider; critically damped. 0 = no soft layer."), ECVF_Default);
static TAutoConsoleVariable<float> CVarContactSkin(
	TEXT("OKPhysXSecondary.ContactSkin"), 0.006f, TEXT("Soft contact: thickness (m) of the skin layer where the contact spring acts, outside the collider."), ECVF_Default);
static TAutoConsoleVariable<float> CVarPlaneStiffness(
	TEXT("OKPhysXSecondary.PlaneStiffness"), 20000.f, TEXT("Soft freeze-axis plane: stiffness (1/s^2); critically damped. 0 = locked axis."), ECVF_Default);
static TAutoConsoleVariable<float> CVarContactLookahead(
	TEXT("OKPhysXSecondary.ContactLookahead"), 0.03f, TEXT("Soft contact: the skin layer grows by the critically damped stopping distance of a particle approaching the collider (v/sqrt(k)), up to this many metres."), ECVF_Default);
static TAutoConsoleVariable<float> CVarHardTolerance(
	TEXT("OKPhysXSecondary.HardTolerance"), 0.002f, TEXT("Exact (hard) correction only for errors beyond this many metres; smaller ones are left to the soft constraints."), ECVF_Default);
static TAutoConsoleVariable<float> CVarHardMaxStep(
	TEXT("OKPhysXSecondary.HardMaxStep"), 0.01f, TEXT("Largest exact (hard) correction of one particle per frame, metres: larger errors are eased out over several frames instead of jumping."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarMeshRadius(
	TEXT("OKPhysXSecondary.MeshRadius"), 1, TEXT("1: other characters' bodies meet a chain at its mesh thickness around each bone (FOKPxParticle::BodyRadius) when that is larger than the authored DynamicBone radius. 0: authored radius only."), ECVF_Default);
static TAutoConsoleVariable<float> CVarPushMemory(
	TEXT("OKPhysXSecondary.PushMemory"), 0.9f, TEXT("Share of last frame's exact-pass correction carried into the next frame (0..0.99): lets a deep contact on a chain pinned to its animated pose clear over a few frames. 0 = every frame starts from the simulation again."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarDebugHard(
	TEXT("OKPhysXSecondary.DebugHard"), 0, TEXT("Log the next N corrections of the final exact pass (why the soft constraints did not hold)."), ECVF_Default);
std::atomic<int32> GDebugHardLeft{0};
static FAutoConsoleVariableSink GDebugHardSink(FConsoleCommandDelegate::CreateLambda([]
{
	static int32 Last = 0;
	const int32 Value = CVarDebugHard.GetValueOnGameThread();
	if (Value != Last) { GDebugHardLeft = Value; Last = Value; }
}));
static bool DebugHardTake() { int32 Left = GDebugHardLeft.load(); while (Left > 0) { if (GDebugHardLeft.compare_exchange_weak(Left, Left - 1)) return true; } return false; }
static TAutoConsoleVariable<int32> CVarAltSafetyNet(
	TEXT("OKPhysXSecondary.AltSafetyNet"), 0, TEXT("1: the other backends (Kawaii/Chaos/Jolt/MuJoCo) also get PhysX's blow-up reset and final exact pass (HardTolerance/HardMaxStep/ProjectionPasses). 0: compared on their own collision handling only."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarAltSubstepCollision(
	TEXT("OKPhysXSecondary.AltSubstepCollision"), 0, TEXT("1: Jolt/MuJoCo also run PhysX's per-substep contact handling (soft skin layer + exact push-out with per-particle radii) after every substep, on top of their own contacts."), ECVF_Default);
static TAutoConsoleVariable<float> CVarAltSubstepScale(
	TEXT("OKPhysXSecondary.AltSubstepScale"), 1.f, TEXT("Substeps of the other backends = PhysX's substep count x this (1..32)."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarBackend(
	TEXT("OKPhysXSecondary.Backend"), -1, TEXT("Solver for the DynamicBone chains: -1 (default) each chain uses its component's solver (card setting, else hair/breasts/hips DynamicBone and the rest PhysX); force one for every chain: 0 PhysX 5.11, 1 KawaiiPhysics v1.21, 2 Chaos PBD (UE5.8), 4 Jolt 5.6, 5 MuJoCo 3.15, 6 the game's DynamicBone update with the shared exact collision pass. Same inputs and audit."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTailBounce(
	TEXT("OKPhysXSecondary.TailBounce"), 1.f, TEXT("How springy tails are (root flag 32, every solver): 0 = their authored DynamicBone settings, 1 = pull back to the pose x2.5 and damping x0.5 (they swing out and spring back instead of dragging), up to 2."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTailCompress(
	TEXT("OKPhysXSecondary.TailCompress"), 0.7f, TEXT("Shortest a tail bone may get under contact, as a fraction of its length (0.3..1; 1 = rigid as authored). A tail pressed against a body squashes and springs back instead of fighting the collision (shaking) or going through."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTailFluff(
	TEXT("OKPhysXSecondary.TailFluff"), 0.6f, TEXT("Part of a tail's mesh thickness that is solid against bodies, its own included (0..1). Fluffy tails are 10-15 cm thick around their bones: the rest may squash elastically against a body instead of the tail being shoved away."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarAudit(
	TEXT("OKPhysXSecondary.Audit"), 0, TEXT("1: measure clipping and jitter every step (OKPhysXSecondary::GetStats); costs CPU, for tests only."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarCrossCollision(
	TEXT("OKPhysXSecondary.CrossCollision"), 1, TEXT("1: chains also collide with the body capsules of the other characters (two-person scenes)."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarBodyMask(
	TEXT("OKPhysXSecondary.BodyMask"), -1, TEXT("Bit i enables body capsule i (arms L 0-4, R 5-9, torso 10, neck 11, pelvis 12, head 13) for chain collision."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarRagdollPassthrough(
	TEXT("OKPhysXSecondary.RagdollPassthrough"), 0, TEXT("Debug: ragdoll returns the animated pose unchanged (tests the write-back path)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarRagdollDebugTwist(
	TEXT("OKPhysXSecondary.RagdollDebugTwist"), 0.f, TEXT("Debug (with Passthrough): rotate every ragdoll part by this many degrees about world Y."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarRagdollPartCollision(
	TEXT("OKPhysXSecondary.RagdollPartCollision"), 1, TEXT("1: ragdoll parts collide with non-adjacent parts of the same body."), ECVF_Default);
static TAutoConsoleVariable<float> CVarRagdollFrequency(
	TEXT("OKPhysXSecondary.RagdollFrequency"), 25.f, TEXT("Natural frequency (rad/s) of the ragdoll joint drives at strength 1; gains scale with the inertia each joint moves."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarRagdoll(
	TEXT("OKPhysXSecondary.Ragdoll"), 0, TEXT("1: every character's body is simulated as a PhysX ragdoll driven toward the animation."), ECVF_Default);
static TAutoConsoleVariable<float> CVarRagdollStrength(
	TEXT("OKPhysXSecondary.RagdollStrength"), 1.f, TEXT("Joint drive strength toward the animation. 0 = limp, ~5 = holds the pose against gravity, higher = stiffer."), ECVF_Default);
static TAutoConsoleVariable<float> CVarRagdollRootStrength(
	TEXT("OKPhysXSecondary.RagdollRootStrength"), 1.f, TEXT("Strength of the hips anchor to the animated hips. 0 = free fall."), ECVF_Default);
static TAutoConsoleVariable<float> CVarRagdollFloor(
	TEXT("OKPhysXSecondary.RagdollFloor"), -1000.f, TEXT("Floor height in skeleton space (metres). -1000 = lowest animated foot when the ragdoll starts."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarMaxSubsteps(
	TEXT("OKPhysXSecondary.MaxSubsteps"), 4, TEXT("Upper bound of PhysX substeps per chain per frame."), ECVF_Default);
static TAutoConsoleVariable<float> CVarSubstep(
	TEXT("OKPhysXSecondary.Substep"), 1.f / 120.f, TEXT("Target PhysX substep length in seconds."), ECVF_Default);

using namespace physx;

namespace
{
	PxDefaultAllocator GAllocator;

	class FLogErrorCallback : public PxErrorCallback
	{
	public:
		void reportError(PxErrorCode::Enum Code, const char* Message, const char* File, int Line) override
		{
			UE_LOG(LogOKPhysX, Warning, TEXT("PhysX %d: %hs (%hs:%d)"), int32(Code), Message, File, Line);
		}
	} GErrorCallback;

	PxFoundation* GFoundation = nullptr;
	PxPhysics* GPhysics = nullptr;
	PxDefaultCpuDispatcher* GDispatcher = nullptr;
	void* GDllHandles[4] = {nullptr, nullptr, nullptr};

	constexpr PxU32 GroupParticle = 1;
	constexpr PxU32 GroupCollider = 2;

	// Chains are isolated scenes, so only particle-vs-collider contacts matter.
	PxFilterFlags FilterShader(PxFilterObjectAttributes, PxFilterData D0, PxFilterObjectAttributes, PxFilterData D1,
		PxPairFlags& PairFlags, const void*, PxU32)
	{
		if ((D0.word0 ^ D1.word0) != (GroupParticle | GroupCollider)) return PxFilterFlag::eSUPPRESS;
		PairFlags = PxPairFlag::eCONTACT_DEFAULT;
		return PxFilterFlag::eDEFAULT;
	}

	float SegmentDistance(const PxVec3& P, const PxVec3& A, const PxVec3& B)
	{
		const PxVec3 AB = B - A;
		const float L2 = AB.dot(AB);
		const float T = L2 > 1e-12f ? FMath::Clamp((P - A).dot(AB) / L2, 0.f, 1.f) : 0.f;
		return (P - (A + AB * T)).magnitude();
	}

	// Closest points between the bone P0->P1 (P0 = parent) and a collider axis Q0->Q1. U is the position along the bone
	// (0 parent, 1 particle). A long bone (a whole hair strand can be one 30+ cm bone) can pierce a body anywhere along
	// its length, so particles and bone middles alone miss it.
	float BoneClosest(const PxVec3& P0, const PxVec3& P1, const PxVec3& Q0, const PxVec3& Q1, float& U, PxVec3& OnBone, PxVec3& OnAxis)
	{
		const PxVec3 D1 = P1 - P0, D2 = Q1 - Q0, R = P0 - Q0;
		const float A = D1.dot(D1), E = D2.dot(D2), F = D2.dot(R);
		float S = 0.f, T = 0.f;
		if (A <= 1e-12f && E <= 1e-12f) { S = T = 0.f; }
		else if (A <= 1e-12f) { S = 0.f; T = FMath::Clamp(F / E, 0.f, 1.f); }
		else
		{
			const float C = D1.dot(R);
			if (E <= 1e-12f) { T = 0.f; S = FMath::Clamp(-C / A, 0.f, 1.f); }
			else
			{
				const float B = D1.dot(D2), Den = A * E - B * B;
				S = Den > 1e-12f ? FMath::Clamp((B * F - C * E) / Den, 0.f, 1.f) : 0.f;
				T = (B * S + F) / E;
				if (T < 0.f) { T = 0.f; S = FMath::Clamp(-C / A, 0.f, 1.f); }
				else if (T > 1.f) { T = 1.f; S = FMath::Clamp((B - C) / A, 0.f, 1.f); }
			}
		}
		U = S; OnBone = P0 + D1 * S; OnAxis = Q0 + D2 * T;
		return (OnBone - OnAxis).magnitude();
	}
	float BoneDistance(const PxVec3& P0, const PxVec3& P1, const PxVec3& Q0, const PxVec3& Q1)
	{
		float U; PxVec3 X, Y;
		return BoneClosest(P0, P1, Q0, Q1, U, X, Y);
	}

	FORCEINLINE FVector3f F3(const float* P) { return FVector3f(P[0], P[1], P[2]); }

	FORCEINLINE PxVec3 V(const float* P) { return PxVec3(P[0], P[1], P[2]); }
	FORCEINLINE void Put(float* Out, const PxVec3& P) { Out[0] = P.x; Out[1] = P.y; Out[2] = P.z; }

	struct FCollider
	{
		PxRigidDynamic* Body = nullptr;
		bool bSphere = false;
		bool bActive = false;
		PxTransform From = PxTransform(PxIdentity);
		PxTransform To = PxTransform(PxIdentity);
	};

	PxScene* CreateChainScene()
	{
		PxSceneDesc Desc(GPhysics->getTolerancesScale());
		Desc.gravity = PxVec3(0.f);
		Desc.cpuDispatcher = GDispatcher;
		Desc.filterShader = FilterShader;
		return GPhysics->createScene(Desc);
	}

	// One PhysX scene for every chain of one DynamicBone set (a hair piece, an outfit, the body): the set is stepped
	// with a single simulate() per substep instead of one per chain.
	struct FBatch
	{
		UE::FMutex Lock;
		PxScene* Scene = nullptr;
		~FBatch() { if (Scene) Scene->release(); }
	};

	struct FChain
	{
		UE::FMutex Lock;
		PxScene* Scene = nullptr;
		TArray<PxRigidDynamic*> Bodies;
		TArray<PxD6Joint*> Springs;
		TArray<PxDistanceJoint*> Lengths;
		TArray<PxD6Joint*> Planes; // FreezeAxis, created on demand
		TArray<int32> Parents;
		TArray<float> Radii;
		TArray<FVector4f> JointCache; // last K, limit extent, min, max length set on the joints (PhysX marks every set as dirty)
		TArray<float> DampingCache;
		TArray<FCollider> Colliders;
		TArray<PxRigidDynamic*> Proxies;
		TMap<uint64, float> BodyClear;    // per body capsule (group<<6|index): largest radius that keeps this chain's rest pose out of contact
		TArray<PxVec3> Push;              // per particle: last frame's exact-pass correction, carried into the next frame (see PushMemory)
		TSet<uint64> IgnoredProxy;        // other-chain particles that already touched this chain at rest
		TSet<uint64> SeenProxy;
		TArray<float> ProxyRadii;
		uint64 Group = 0;
		FMatrix44f LastFrame = FMatrix44f::Identity;
		bool bHasLastFrame = false;
		double LastSolveTime = 0.;  // when this chain was last solved here (a gap = another solver ran meanwhile)
		bool bStale = false;        // first step after such a gap: start from the current pose, not old history
		int32 ResetEpoch = 0;       // last GResetEpoch this chain was reset for

		TSharedPtr<struct FBatch> Batch; // shared scene owner when the chain is solved in a batch (null: own scene)

		~FChain();
		void ReleaseActors()
		{
			for (PxD6Joint* J : Springs) if (J) J->release();
			for (PxDistanceJoint* J : Lengths) if (J) J->release();
			for (PxD6Joint* J : Planes) if (J) J->release();
			for (PxRigidDynamic* B : Bodies) if (B) B->release();
			for (PxRigidDynamic* B : Proxies) if (B) B->release();
			for (FCollider& C : Colliders) if (C.Body) C.Body->release();
			Springs.Reset(); Lengths.Reset(); Planes.Reset(); Bodies.Reset(); Proxies.Reset(); Colliders.Reset();
		}

		bool Matches(const FOKPxChainStep& S) const
		{
			if (!Scene || Bodies.Num() != int32(S.ParticleCount)) return false;
			for (uint32 i = 0; i < S.ParticleCount; ++i) if (Parents[i] != S.Particles[i].Parent) return false;
			return true;
		}

		void Build(const FOKPxChainStep& S, PxScene* Shared)
		{
			if (Batch) ReleaseActors();
			else if (Scene) { Scene->release(); Scene = nullptr; }
			Bodies.Reset(); Springs.Reset(); Lengths.Reset(); Planes.Reset(); Parents.Reset(); Radii.Reset(); JointCache.Reset(); DampingCache.Reset(); Colliders.Reset(); Proxies.Reset(); ProxyRadii.Reset(); BodyClear.Reset(); ColliderSlots.Reset(); LastShapes.Reset(); IgnoredProxy.Reset(); SeenProxy.Reset();
			Scene = Shared ? Shared : CreateChainScene();
			PxMaterial* Material = GPhysics->createMaterial(0.4f, 0.4f, 0.f);
			for (uint32 i = 0; i < S.ParticleCount; ++i)
			{
				const FOKPxParticle& P = S.Particles[i];
				const float R = FMath::Max(P.Radius, 1e-4f);
				PxRigidDynamic* Body = GPhysics->createRigidDynamic(PxTransform(V(P.Pos)));
				PxShape* Shape = GPhysics->createShape(PxSphereGeometry(R), *Material, true);
				PxFilterData Data(GroupParticle, 0, 0, 0);
				Shape->setSimulationFilterData(Data);
				Body->attachShape(*Shape);
				Shape->release();
				Body->setMass(1.f);
				Body->setMassSpaceInertiaTensor(PxVec3(1.f));
				Body->setRigidDynamicLockFlags(PxRigidDynamicLockFlag::eLOCK_ANGULAR_X | PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y | PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z);
				Body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
				Body->setSleepThreshold(0.f);
				Body->setMaxLinearVelocity(1000.f);
				Body->setMaxDepenetrationVelocity(1.f); // a particle found inside a collider is eased out, not shot away
				if (P.Flags & 1u) Body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
				Scene->addActor(*Body);
				Bodies.Add(Body);
				Parents.Add(P.Parent);
				Radii.Add(R);
				if (P.Parent >= 0)
				{
					PxRigidDynamic* Parent = Bodies[P.Parent];
					PxD6Joint* Spring = PxD6JointCreate(*GPhysics, Parent, PxTransform(PxIdentity), Body, PxTransform(PxIdentity));
					Spring->setMotion(PxD6Axis::eX, PxD6Motion::eLIMITED);
					Spring->setMotion(PxD6Axis::eY, PxD6Motion::eLIMITED);
					Spring->setMotion(PxD6Axis::eZ, PxD6Motion::eLIMITED);
					Springs.Add(Spring);
					PxDistanceJoint* Length = PxDistanceJointCreate(*GPhysics, Parent, PxTransform(PxIdentity), Body, PxTransform(PxIdentity));
					Length->setDistanceJointFlags(PxDistanceJointFlag::eMIN_DISTANCE_ENABLED | PxDistanceJointFlag::eMAX_DISTANCE_ENABLED);
					// Default tolerance is 0.025 * toleranceScale.length (2.5 cm): longer than most hair bones, so the joint
					// never engaged and the final exact pass did all the length work.
					Length->setTolerance(0.f);
					Lengths.Add(Length);
				}
				else
				{
					Springs.Add(nullptr);
					Lengths.Add(nullptr);
				}
				Planes.Add(nullptr);
			}
			Material->release();
		}

		// Colliders keep their PhysX body by identity (Key): a body never jumps from one limb to another between frames,
		// which would hit the particles at an absurd speed. Moving colliders are swept across the substeps (Advance).
		TMap<uint64, int32> ColliderSlots;
		TMap<uint64, FOKPxCapsule> LastShapes; // colliders of the previous step, by identity (swept across substeps)
		// Last two results and animated poses per particle (jitter audit).
		TArray<PxVec3> SimHistory[2], AnimHistory[2], LastExcess;
		int32 HistoryFrames = 0;

		float SyncColliders(const TArray<FOKPxCapsule>& List, const TArray<uint64>& Keys)
		{
			float MaxMove = 0.f;
			PxMaterial* Material = nullptr;
			for (FCollider& Slot : Colliders) Slot.bActive = false;
			TSet<uint64> Seen;
			for (int32 i = 0; i < List.Num(); ++i)
			{
				const FOKPxCapsule& C = List[i];
				const PxVec3 A = V(C.A), B = V(C.B);
				const PxVec3 Axis = B - A;
				const float Length = Axis.magnitude();
				const float R = FMath::Max(C.Radius, 1e-4f);
				const bool bSphere = Length < 1e-4f;
				PxTransform Pose((A + B) * 0.5f);
				if (!bSphere)
				{
					// Capsule geometry runs along local +X.
					Pose.q = PxShortestRotation(PxVec3(1.f, 0.f, 0.f), Axis / Length);
				}
				Seen.Add(Keys[i]);
				int32* Found = ColliderSlots.Find(Keys[i]);
				bool bFresh = Found == nullptr;
				if (!Found)
				{
					// Reuse a parked slot or make a new one.
					int32 Free = INDEX_NONE;
					for (int32 k = 0; k < Colliders.Num() && Free == INDEX_NONE; ++k)
					{
						bool bOwned = false;
						for (const TPair<uint64, int32>& Pair : ColliderSlots) if (Pair.Value == k) { bOwned = true; break; }
						if (!bOwned) Free = k;
					}
					if (Free == INDEX_NONE) { Free = Colliders.Num(); Colliders.AddDefaulted(); }
					Found = &ColliderSlots.Add(Keys[i], Free);
				}
				FCollider& Slot = Colliders[*Found];
				if (Slot.Body && Slot.bSphere != bSphere)
				{
					Slot.Body->release();
					Slot.Body = nullptr;
				}
				if (!Slot.Body)
				{
					if (!Material) Material = GPhysics->createMaterial(0.4f, 0.4f, 0.f);
					Slot.bSphere = bSphere;
					Slot.Body = GPhysics->createRigidDynamic(Pose);
					PxShape* Shape = bSphere ? GPhysics->createShape(PxSphereGeometry(R), *Material, true)
						: GPhysics->createShape(PxCapsuleGeometry(R, Length * 0.5f), *Material, true);
					Shape->setSimulationFilterData(PxFilterData(GroupCollider, 0, 0, 0));
					Slot.Body->attachShape(*Shape);
					Shape->release();
					Slot.Body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
					Scene->addActor(*Slot.Body);
					bFresh = true;
				}
				else
				{
					PxShape* Shape = nullptr;
					Slot.Body->getShapes(&Shape, 1);
					if (bSphere) Shape->setGeometry(PxSphereGeometry(R));
					else Shape->setGeometry(PxCapsuleGeometry(R, Length * 0.5f));
				}
				Slot.bActive = true;
				if (bFresh)
				{
					// A collider that just appeared is placed, not swept in.
					Slot.Body->setGlobalPose(Pose);
					Slot.From = Pose;
					Slot.To = Pose;
				}
				else
				{
					Slot.From = Slot.Body->getGlobalPose();
					Slot.To = Pose;
					MaxMove = FMath::Max(MaxMove, (Slot.From.p - Pose.p).magnitude() + Slot.From.q.getAngle(Pose.q) * Length * 0.5f);
				}
			}
			// Colliders no longer listed are parked far away and their slot freed.
			for (auto It = ColliderSlots.CreateIterator(); It; ++It)
			{
				if (Seen.Contains(It.Key())) continue;
				FCollider& Slot = Colliders[It.Value()];
				if (Slot.Body) Slot.Body->setGlobalPose(PxTransform(PxVec3(1e6f)));
				It.RemoveCurrent();
			}
			if (Material) Material->release();
			return MaxMove;
		}

		void Advance(float Alpha)
		{
			for (FCollider& Slot : Colliders)
			{
				if (!Slot.bActive || !Slot.Body) continue;
				const PxVec3 P = Slot.From.p + (Slot.To.p - Slot.From.p) * Alpha;
				const PxQuat To = Slot.From.q.dot(Slot.To.q) < 0.f ? -Slot.To.q : Slot.To.q;
				const PxQuat Q = Slot.From.q * (1.f - Alpha) + To * Alpha;
				Slot.Body->setKinematicTarget(PxTransform(P, Q.getNormalized()));
			}
		}

		// Particles of the other chains of the character, frozen at their last result.
		void SyncProxies(const TArray<FVector4f>& Spheres)
		{
			PxMaterial* Material = nullptr;
			for (int32 i = 0; i < Spheres.Num(); ++i)
			{
				const PxTransform Pose(PxVec3(Spheres[i].X, Spheres[i].Y, Spheres[i].Z));
				const float R = FMath::Max(Spheres[i].W, 1e-4f);
				if (Proxies.Num() <= i)
				{
					if (!Material) Material = GPhysics->createMaterial(0.4f, 0.4f, 0.f);
					PxRigidDynamic* Body = GPhysics->createRigidDynamic(Pose);
					PxShape* Shape = GPhysics->createShape(PxSphereGeometry(R), *Material, true);
					Shape->setSimulationFilterData(PxFilterData(GroupCollider, 0, 0, 0));
					Body->attachShape(*Shape);
					Shape->release();
					Body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
					Scene->addActor(*Body);
					Proxies.Add(Body);
					ProxyRadii.Add(R);
					continue;
				}
				if (FMath::Abs(ProxyRadii[i] - R) > 1e-6f)
				{
					PxShape* Shape = nullptr;
					Proxies[i]->getShapes(&Shape, 1);
					Shape->setGeometry(PxSphereGeometry(R));
					ProxyRadii[i] = R;
				}
				Proxies[i]->setGlobalPose(Pose); // teleport: no velocity from stale snapshots
			}
			for (int32 i = Spheres.Num(); i < Proxies.Num(); ++i) Proxies[i]->setGlobalPose(PxTransform(PxVec3(1e6f)));
			if (Material) Material->release();
		}
	};

	// Clipping audit: how far hair/cloth particles ended up inside the body capsules they are meant to stay out of.
	std::atomic<int32> GMaxPenetrationBits{0};
	std::atomic<int64> GViolations{0};
	std::atomic<int64> GSteps{0};
	std::atomic<int32> GMinBodyGapBits{0x7f7fffff};
	std::atomic<int32> GMaxProjectionBits{0};
	std::atomic<int64> GResets{0};
	std::atomic<int64> GSolveCycles{0}; // CPU time inside Solve/Ragdoll, all threads
	std::atomic<int64> GPhaseCycles[4]; // prepare, simulate, substep collisions, finish
	struct FSolveTimer
	{
		const uint64 Start = FPlatformTime::Cycles64();
		~FSolveTimer() { GSolveCycles += int64(FPlatformTime::Cycles64() - Start); }
	};
	std::atomic<int64> GPops{0};
	// Per solver (0..7, +8 for body chains): chain steps, pops, jitter (audit).
	std::atomic<int64> GStepsBy[16], GPopsBy[16], GJitterBy[16];
	// Tails only (root flag 32): chain steps, pops, jitter, and the summed particle offset from the animated pose (um).
	std::atomic<int64> GTailSteps{0}, GTailPops{0}, GTailJitter{0}, GTailSwingMicrons{0}, GTailSwingSamples{0}, GTailMaxSwingMicrons{0};
	// Tails at their mesh thickness (what is seen): deepest particle/bone inside a full body proxy, steps beyond 2 cm, per body.
	std::atomic<int32> GTailDepthBits{0}; std::atomic<int64> GTailDeep{0}; std::atomic<int64> GTailDeepByBody[128];
	std::atomic<int32> GMaxSwingBits{0}; std::atomic<uint64> GMaxSwingWhere{0}; // largest particle offset from the animated pose, and (chain<<16 | particle)
	std::atomic<int32> GMaxStretchBits{0}; // largest bone length / rest length seen by the audit
	std::atomic<int32> GTrueDepthBits{0}; std::atomic<int64> GTrueViolations{0}; // against the full body proxies, attachment body excluded
	std::atomic<int64> GHardFixes{0};
	std::atomic<int64> GHardSubstep{0}; // safety-net projections inside the substeps
	std::atomic<int64> GHardLength{0}, GHardPoint{0}, GHardMid{0}; // which part of the exact pass moved it // particles the final exact (hard) pass still had to move: the soft constraints did not hold
	std::atomic<int64> GSwingMicrons{0}, GSwingSamples{0}, GDropMicrons{0}; // offset of the simulated particles from the animated pose
	std::atomic<int64> GJitter{0};
	std::atomic<int32> GMaxJerkBits{0}; // CPU time inside Solve/Ragdoll, all threads
	std::atomic<int32> GBodyPenetrationBits{0};
	std::atomic<int64> GBodyViolations{0};
	std::atomic<int64> GViolationsByBody[128];

	void AtomicMaxFloat(std::atomic<int32>& Bits, float Value)
	{
		int32 Old = Bits.load();
		for (;;)
		{
			float OldValue; FMemory::Memcpy(&OldValue, &Old, 4);
			if (!(Value > OldValue)) break;
			int32 New; FMemory::Memcpy(&New, &Value, 4);
			if (Bits.compare_exchange_weak(Old, New)) break;
		}
	}
	void AtomicMinFloat(std::atomic<int32>& Bits, float Value)
	{
		int32 Old = Bits.load();
		for (;;)
		{
			float OldValue; FMemory::Memcpy(&OldValue, &Old, 4);
			if (!(Value < OldValue)) break;
			int32 New; FMemory::Memcpy(&New, &Value, 4);
			if (Bits.compare_exchange_weak(Old, New)) break;
		}
	}

	struct FSnapshot { TArray<FVector4f> Spheres; };
	struct FGroup
	{
		UE::FMutex Lock;
		TArray<FOKPxCapsule> Body;
		TArray<FOKPxCapsule> Clothed; // same capsules grown to the clothes worn over them (empty: no clothes layer)
		TMap<uint64, FSnapshot> Chains;
		FMatrix44f Frame = FMatrix44f::Identity; // skeleton space (metres) -> world (metres)
		bool bHasFrame = false;
	};
	TMap<uint64, TSharedPtr<FGroup>> GGroups;
	UE::FMutex GGroupLock;


	constexpr PxU32 GroupRagdoll = 4;
	constexpr PxU32 GroupGround = 8;

	PxFilterFlags RagdollFilter(PxFilterObjectAttributes, PxFilterData D0, PxFilterObjectAttributes, PxFilterData D1,
		PxPairFlags& PairFlags, const void*, PxU32)
	{
		const PxU32 Kinds = D0.word0 | D1.word0;
		if (D0.word0 == GroupRagdoll && D1.word0 == GroupRagdoll)
		{
			// word1 = own bit, word2 = bits of parts that must not collide with it (neighbours).
			if ((D0.word2 & D1.word1) || (D1.word2 & D0.word1)) return PxFilterFlag::eSUPPRESS;
			if (!D0.word3) return PxFilterFlag::eSUPPRESS; // part-vs-part collision is opt-in
		}
		else if (Kinds != (GroupRagdoll | GroupGround)) return PxFilterFlag::eSUPPRESS;
		PairFlags = PxPairFlag::eCONTACT_DEFAULT;
		return PxFilterFlag::eDEFAULT;
	}

	FORCEINLINE PxQuat Q4(const float* R) { return PxQuat(R[0], R[1], R[2], R[3]).getNormalized(); }

	struct FRagdoll
	{
		UE::FMutex Lock;
		PxScene* Scene = nullptr;
		TArray<PxRigidDynamic*> Bodies;
		TArray<PxD6Joint*> Joints; // joint i connects part i to its parent (root: to the world)
		TArray<int32> Parents;
		PxRigidStatic* Ground = nullptr;
		TArray<float> Supported; // mass this joint has to carry: the part and everything below it
		TArray<float> JointInertia; // moment of inertia of that supported subtree about the joint
		float TotalMass = 1.f;
		TArray<PxQuat> RestFrame1; // child-local rotation that aligns it with the parent at creation

		~FRagdoll() { if (Scene) Scene->release(); }

		void Build(const FOKPxBone* Bones, uint32 Count)
		{
			if (Scene) { Scene->release(); Scene = nullptr; }
			Bodies.Reset(); Joints.Reset(); Parents.Reset(); RestFrame1.Reset();
			PxSceneDesc Desc(GPhysics->getTolerancesScale());
			Desc.gravity = PxVec3(0.f, -9.81f, 0.f);
			Desc.cpuDispatcher = GDispatcher;
			Desc.filterShader = RagdollFilter;
			Desc.solverType = PxSolverType::eTGS;
			Scene = GPhysics->createScene(Desc);
			PxMaterial* Material = GPhysics->createMaterial(0.6f, 0.6f, 0.f);

			Ground = PxCreatePlane(*GPhysics, PxPlane(0.f, 1.f, 0.f, 0.f), *Material);
			PxShape* GroundShape = nullptr;
			Ground->getShapes(&GroundShape, 1);
			GroundShape->setSimulationFilterData(PxFilterData(GroupGround, 0, 0, 0));
			Scene->addActor(*Ground);

			for (uint32 i = 0; i < Count; ++i)
			{
				const FOKPxBone& B = Bones[i];
				const PxQuat Q = Q4(B.Rot);
				const PxVec3 P = V(B.Pos);
				const PxVec3 Axis = V(B.Tip) - P;
				const float Length = FMath::Max(Axis.magnitude(), 0.02f);
				const PxVec3 LocalDir = Q.rotateInv(Axis.magnitude() > 1e-5f ? Axis / Axis.magnitude() : PxVec3(0.f, -1.f, 0.f));
				const float R = FMath::Min(FMath::Max(B.Radius, 0.01f), Length * 0.5f);
				PxRigidDynamic* Body = GPhysics->createRigidDynamic(PxTransform(P, Q));
				PxShape* Shape = GPhysics->createShape(PxCapsuleGeometry(R, FMath::Max(Length * 0.5f - R, 0.002f)), *Material, true);
				Shape->setLocalPose(PxTransform(LocalDir * (Length * 0.5f), PxShortestRotation(PxVec3(1.f, 0.f, 0.f), LocalDir)));
				PxU32 Mask = 0;
				for (uint32 j = 0; j < Count; ++j) if (Bones[j].Parent == int32(i) || int32(j) == B.Parent) Mask |= 1u << j;
				if (B.Parent >= 0) for (uint32 j = 0; j < Count; ++j) if (Bones[j].Parent == B.Parent) Mask |= 1u << j; // siblings overlap at the shared joint
				Shape->setSimulationFilterData(PxFilterData(GroupRagdoll, 1u << i, Mask, CVarRagdollPartCollision.GetValueOnAnyThread() ? 1u : 0u));
				Body->attachShape(*Shape);
				Shape->release();
				PxRigidBodyExt::updateMassAndInertia(*Body, 900.f);
				Body->setLinearDamping(0.4f);
				Body->setAngularDamping(1.5f);
				{
					// Thin capsules have tiny inertia: floor it so joint torques stay well conditioned.
					const PxVec3 Inertia = Body->getMassSpaceInertiaTensor();
					const float Floor = Body->getMass() * 0.012f;
					Body->setMassSpaceInertiaTensor(PxVec3(FMath::Max(Inertia.x, Floor), FMath::Max(Inertia.y, Floor), FMath::Max(Inertia.z, Floor)));
				}
				Body->setSolverIterationCounts(32, 8);
				Body->setMaxAngularVelocity(40.f);
				Body->setMaxDepenetrationVelocity(1.5f);
				Body->setSleepThreshold(0.f); // joint drive targets change without touching the body: it must never sleep
				Scene->addActor(*Body);
				Bodies.Add(Body);
				Parents.Add(B.Parent);

				PxD6Joint* Joint = nullptr;
				if (B.Parent >= 0)
				{
					const FOKPxBone& PB = Bones[B.Parent];
					const PxQuat QP = Q4(PB.Rot);
					Joint = PxD6JointCreate(*GPhysics, Bodies[B.Parent], PxTransform(QP.rotateInv(P - V(PB.Pos)), PxQuat(PxIdentity)),
						Body, PxTransform(PxVec3(0.f), Q.getConjugate() * QP));
					Joint->setAngularDriveConfig(PxD6AngularDriveConfig::eSLERP); // default is swing/twist, which ignores eSLERP drives
					Joint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLIMITED);
					Joint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLIMITED);
					Joint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLIMITED);
					Joint->setSwingLimit(PxJointLimitCone(PxPi * 0.97f, PxPi * 0.97f));
					Joint->setTwistLimit(PxJointAngularLimitPair(-PxPi * 0.95f, PxPi * 0.95f));
					RestFrame1.Add(Q.getConjugate() * QP);
				}
				else
				{
					// Hips: a soft anchor to the animated hips (world frame).
					Joint = PxD6JointCreate(*GPhysics, nullptr, PxTransform(P, Q), Body, PxTransform(PxIdentity));
					Joint->setAngularDriveConfig(PxD6AngularDriveConfig::eSLERP);
					Joint->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);
					Joint->setMotion(PxD6Axis::eY, PxD6Motion::eFREE);
					Joint->setMotion(PxD6Axis::eZ, PxD6Motion::eFREE);
					RestFrame1.Add(PxQuat(PxIdentity));
				}
				Joints.Add(Joint);
			}
			Supported.Init(0.f, Count);
			for (uint32 i = 0; i < Count; ++i) Supported[i] = Bodies[i]->getMass();
			for (int32 i = int32(Count) - 1; i > 0; --i) if (Parents[i] >= 0) Supported[Parents[i]] += Supported[i]; // parents come first
			TotalMass = Supported[0];
			// Inertia of everything a joint moves, about the joint itself (parallel axis over the subtree). Drive gains
			// derive from it, so every joint gets the same natural frequency regardless of body size or card proportions.
			JointInertia.Init(0.f, Count);
			for (uint32 j = 0; j < Count; ++j)
			{
				const PxVec3 JointPos = Bodies[j]->getGlobalPose().p;
				float I = 0.f;
				for (uint32 k = 0; k < Count; ++k)
				{
					int32 Up = int32(k);
					while (Up >= 0 && Up != int32(j)) Up = Parents[Up];
					if (Up != int32(j)) continue; // k is not below j
					const PxTransform Com = Bodies[k]->getGlobalPose().transform(Bodies[k]->getCMassLocalPose());
					const PxVec3 Diag = Bodies[k]->getMassSpaceInertiaTensor();
					I += Bodies[k]->getMass() * (Com.p - JointPos).magnitudeSquared() + (Diag.x + Diag.y + Diag.z) / 3.f;
				}
				JointInertia[j] = FMath::Max(I, 1e-4f);
			}
			Material->release();
		}
	};

	UE::FMutex GRagdollLock;
	TMap<uint64, TSharedPtr<FRagdoll>> GRagdolls;

	FChain::~FChain()
	{
		if (Batch)
		{
			UE::TUniqueLock Guard(Batch->Lock);
			ReleaseActors(); // the scene belongs to the batch
		}
		else if (Scene)
		{
			Scene->release(); // releases every actor and joint it still owns
		}
	}

	UE::FMutex GMapLock;
	TMap<uint64, TSharedPtr<FChain>> GChains;
	UE::FMutex GBatchLock;
	TMap<uint64, TSharedPtr<FBatch>> GBatches;

	struct FSolveJob
	{
		FOKPxChainStep* S = nullptr;
		TSharedPtr<FChain> Chain;
		bool bReady = false;
		bool bAuditOnly = false; // measure only (built-in solver result)
		bool bReset = false;     // this step restarts the chain from its animated pose (GResetEpoch)
		float RefStep = 0.f, Dt = 0.f;
		PxVec3 Accel = PxVec3(0.f);
		TArray<FOKPxCapsule> Shapes, AuditShapes, FromShapes;
		int32 AuthoredShapes = 0; // Shapes[0..AuthoredShapes) are the chain's own DynamicBone colliders
		bool bProjectOnly = false; // DynamicBone backend: contacts are exact projections (no skin layer, step cap or push memory)
		int32 Solver = 0; // backend solving this chain (SolveJobs)
		TArray<uint64> ShapeKeys, AuditKeys;
		// Collider radius per particle and shape (ParticleCount x Shapes); Shapes[k].Radius is the largest of them.
		TArray<float> PartRadius;
		TArray<float> FullRadius;   // unshrunk proxy radius per shape (true-penetration audit)
		TArray<uint8> Attached;     // the chain hangs from this shape (its root is in contact): excluded from that audit
		float Rad(int32 i, int32 k) const { return PartRadius.Num() ? PartRadius[i * Shapes.Num() + k] : Shapes[k].Radius; }
		TArray<PxVec3, TInlineAllocator<64>> AnimPos;
		TArray<FVector4f> Others;
		int32 Substeps = 1;
		PxVec3 RootTo = PxVec3(0.f), RootFrom = PxVec3(0.f);
	};
}

namespace OKPhysXSecondary
{
	static bool EnsurePhysics()
	{
		if (GPhysics) return true;
		static bool bTried = false;
		if (bTried) return false;
		bTried = true;
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OKPhysXSecondary"));
		if (!Plugin) return false;
		const FString Dir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries/ThirdParty/Win64"));
		FPlatformProcess::PushDllDirectory(*Dir);
		// Dependencies first so the delay-load of PhysX.dll resolves against the loaded images.
		const TCHAR* Names[4] = {TEXT("PhysXFoundation.dll"), TEXT("PhysXCommon.dll"), TEXT("PhysX.dll"), TEXT("PhysXCooking.dll")};
		for (int32 i = 0; i < 4; ++i)
		{
			GDllHandles[i] = FPlatformProcess::GetDllHandle(*FPaths::Combine(Dir, Names[i]));
			if (!GDllHandles[i])
			{
				UE_LOG(LogOKPhysX, Error, TEXT("failed to load %s"), Names[i]);
				FPlatformProcess::PopDllDirectory(*Dir);
				return false;
			}
		}
		FPlatformProcess::PopDllDirectory(*Dir);
		GFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, GAllocator, GErrorCallback);
		if (!GFoundation) return false;
		GPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *GFoundation, PxTolerancesScale(), false, nullptr);
		if (!GPhysics) return false;
		PxInitExtensions(*GPhysics, nullptr);
		GDispatcher = PxDefaultCpuDispatcherCreate(0);
		UE_LOG(LogOKPhysX, Display, TEXT("PhysX %d.%d secondary dynamics ready"), PX_PHYSICS_VERSION_MAJOR, PX_PHYSICS_VERSION_MINOR);
		return true;
	}

	void Register(void (*SetSolver)(int32 (*)(const void*), void (*)(uint64), void (*)(uint64, const void*, uint32), int32 (*)(uint64, void*, uint32, float), float (*)(), int32 (*)(uint64, void*, uint32), int32 (*)()))
	{
		FModuleManager::Get().LoadModuleChecked<IModuleInterface>(TEXT("OKPhysXSecondary"));
		if (SetSolver && EnsurePhysics()) SetSolver(&OKPhysXSecondary::Solve, &OKPhysXSecondary::Release, &OKPhysXSecondary::PublishBody, &OKPhysXSecondary::Ragdoll, &OKPhysXSecondary::HeadTilt, &OKPhysXSecondary::SolveBatch, &OKPhysXSecondary::AuditEnabled);
	}

	static void GetTuning(float* Weight, float* Hardness)
	{
		if (Weight) *Weight = FMath::Clamp(CVarWeight.GetValueOnAnyThread(), 1.f, 10.f);
		if (Hardness) *Hardness = FMath::Clamp(CVarHardness.GetValueOnAnyThread(), 1.f, 10.f);
	}

	void RegisterTuning(void (*SetSource)(void (*)(float*, float*)))
	{
		if (SetSource) SetSource(&GetTuning);
	}

	FSoftness Softness(uint32 RootFlags)
	{
		// Pull, range and damping scaled together, so one value reads as "softer". Breast/hip soft tissue keeps its own.
		const float Soft = (RootFlags & 2u) ? 0.f : (10.f - FMath::Clamp(CVarHardness.GetValueOnAnyThread(), 1.f, 10.f)) / 9.f;
		FSoftness Out;
		Out.Pull = 1.f - 0.9f * Soft;
		Out.Range = 1.f + 2.f * Soft;
		Out.Damp = 1.f - 0.6f * Soft;
		Out.Zeta = 1.f - 0.7f * Soft; // below critical damping when soft: overshoot a little and settle (a soft bounce)
		return Out;
	}

	// While settling, the chains are solved here whatever the chosen solver (also when it is the built-in Verlet).
	bool IsEnabled() { return (CVarEnabled.GetValueOnAnyThread() != 0 || Settling()) && GPhysics != nullptr; }
	void Settle(float Seconds)
	{
		const float Duration = Seconds < 0.f ? CVarSettleSeconds.GetValueOnAnyThread() : Seconds;
		if (Duration <= 0.f) return;
		GSettleUntil = FPlatformTime::Seconds() + Duration;
		UE_LOG(LogTemp, Display, TEXT("OKPhysXSecondary settle %.2f s with backend %d"), Duration, CVarSettleBackend.GetValueOnAnyThread());
	}

	void Release(uint64 Key)
	{
		OKBackendsRelease(Key);
		{
			UE::TUniqueLock BatchGuard(GBatchLock);
			GBatches.Remove(Key); // a set's batch scene lives on while its chains still reference it
		}
		TSharedPtr<FChain> Dead;
		{
			UE::TUniqueLock Guard(GMapLock);
			GChains.RemoveAndCopyValue(Key, Dead);
		}
		if (Dead)
		{
			UE::TUniqueLock Guard(Dead->Lock);
			if (Dead->Group)
			{
				TSharedPtr<FGroup> Group;
				{
					UE::TUniqueLock GroupGuard(GGroupLock);
					Group = GGroups.FindRef(Dead->Group);
				}
				if (Group) { UE::TUniqueLock GroupGuard(Group->Lock); Group->Chains.Remove(Key); }
			}
		}
	}

	int32 Ragdoll(uint64 GroupId, void* BonesPtr, uint32 Count, float Dt)
	{
		const FSolveTimer Timer;
		const bool bOn = IsEnabled() && CVarRagdoll.GetValueOnAnyThread() != 0;
		if (!bOn)
		{
			UE::TUniqueLock Guard(GRagdollLock);
			GRagdolls.Remove(GroupId); // back to the pure animation pose
			return 0;
		}
		if (Count == 0) return 1;
		if (Count > 32 || !(Dt > 0.f)) return 0;
		if (CVarRagdollPassthrough.GetValueOnAnyThread())
		{
			const float Twist = FMath::DegreesToRadians(CVarRagdollDebugTwist.GetValueOnAnyThread());
			if (Twist != 0.f)
			{
				const PxQuat Q(Twist, PxVec3(0.f, 1.f, 0.f));
				FOKPxBone* Bones = static_cast<FOKPxBone*>(BonesPtr);
				for (uint32 i = 0; i < Count; ++i)
				{
					const PxQuat R = Q * Q4(Bones[i].Rot);
					Bones[i].Rot[0] = R.x; Bones[i].Rot[1] = R.y; Bones[i].Rot[2] = R.z; Bones[i].Rot[3] = R.w;
				}
			}
			return 1;
		}
		FOKPxBone* Bones = static_cast<FOKPxBone*>(BonesPtr);
		TSharedPtr<FRagdoll> R;
		{
			UE::TUniqueLock Guard(GRagdollLock);
			TSharedPtr<FRagdoll>& Slot = GRagdolls.FindOrAdd(GroupId);
			if (!Slot) Slot = MakeShared<FRagdoll>();
			R = Slot;
		}
		UE::TUniqueLock Guard(R->Lock);
		bool bRebuild = !R->Scene || R->Bodies.Num() != int32(Count);
		for (uint32 i = 0; !bRebuild && i < Count; ++i) bRebuild = R->Parents[i] != Bones[i].Parent;
		// A stiff ragdoll that is far from its animation was left behind by a pose jump (clip switch,
		// teleport) or tangled up: start over from the animated pose instead of dragging it across.
		if (!bRebuild && CVarRagdollStrength.GetValueOnAnyThread() >= 0.5f)
		{
			float Deviation = 0.f;
			for (uint32 i = 0; i < Count; ++i) Deviation = FMath::Max(Deviation, (R->Bodies[i]->getGlobalPose().p - V(Bones[i].Pos)).magnitude());
			bRebuild = Deviation > 0.5f;
		}
		if (bRebuild) R->Build(Bones, Count);

		// Drive every joint toward the animation: the target is the child orientation relative to
		// the parent in joint space (frame1 was aligned to frame0 at creation).
		const float Strength = FMath::Max(CVarRagdollStrength.GetValueOnAnyThread(), 0.f);
		const float RootStrength = FMath::Max(CVarRagdollRootStrength.GetValueOnAnyThread(), 0.f);
		for (uint32 i = 0; i < Count; ++i)
		{
			PxD6Joint* Joint = R->Joints[i];
			const PxQuat QC = Q4(Bones[i].Rot);
			if (Bones[i].Parent >= 0)
			{
				const PxQuat QP = Q4(Bones[Bones[i].Parent].Rot);
				PxQuat Rel = (QP.getConjugate() * QC * R->RestFrame1[i]).getNormalized();
				if (Rel.w < 0.f) Rel = -Rel; // shortest way round
				Joint->setDrivePosition(PxTransform(PxVec3(0.f), Rel));
				{
					// Critically damped spring at a fixed natural frequency for the inertia this joint moves.
					const float Omega = CVarRagdollFrequency.GetValueOnAnyThread() * FMath::Sqrt(Strength);
					const float I = R->JointInertia[i];
					Joint->setDrive(PxD6Drive::eSLERP, PxD6JointDrive(I * Omega * Omega, 2.f * I * Omega, PX_MAX_F32, false));
				}
			}
			else
			{
				Joint->setLocalPose(PxJointActorIndex::eACTOR0, PxTransform(V(Bones[i].Pos), QC));
				const float RootOmega = CVarRagdollFrequency.GetValueOnAnyThread() * 0.7f * FMath::Sqrt(RootStrength);
				const PxD6JointDrive Linear(R->TotalMass * RootOmega * RootOmega, 2.f * R->TotalMass * RootOmega, PX_MAX_F32, false);
				Joint->setDrive(PxD6Drive::eX, Linear);
				Joint->setDrive(PxD6Drive::eY, Linear);
				Joint->setDrive(PxD6Drive::eZ, Linear);
				
			}
		}
		// The floor follows the animation (the lowest animated point), so poses that sit on a bed or
		// stand up never start inside the plane. RagdollFloor overrides it with a fixed height.
		float Floor = CVarRagdollFloor.GetValueOnAnyThread();
		if (Floor < -999.f)
		{
			Floor = FLT_MAX;
			for (uint32 i = 0; i < Count; ++i) Floor = FMath::Min(Floor, FMath::Min(Bones[i].Pos[1], Bones[i].Tip[1]) - 0.12f); // capsule radius below the bone axis
		}
		R->Ground->setGlobalPose(PxTransformFromPlaneEquation(PxPlane(0.f, 1.f, 0.f, -Floor))); // plane normal is the shape X axis
		const float StepDt = FMath::Min(Dt, 0.1f);
		const int32 Substeps = FMath::Clamp(FMath::CeilToInt(StepDt / (1.f / 240.f)), 1, 8);
		const float Sub = StepDt / Substeps;
		for (int32 Step = 0; Step < Substeps; ++Step)
		{
			R->Scene->simulate(Sub);
			R->Scene->fetchResults(true);
		}
		TArray<PxVec3> AnimPos;
		for (uint32 i = 0; i < Count; ++i) AnimPos.Add(V(Bones[i].Pos));
		float MaxError = 0.f, WorstAngle = 0.f;
		int32 WorstBone = -1;
		for (uint32 i = 0; i < Count; ++i)
		{
			const PxTransform Pose = R->Bodies[i]->getGlobalPose();
			const float Err = (Pose.p - V(Bones[i].Pos)).magnitude();
			if (Err > MaxError) { MaxError = Err; WorstBone = int32(i); WorstAngle = Pose.q.getAngle(Q4(Bones[i].Rot)) ; }
			if (!Pose.isValid()) { R->Scene->release(); R->Scene = nullptr; return 0; }
			Put(Bones[i].Pos, Pose.p);
			Bones[i].Rot[0] = Pose.q.x; Bones[i].Rot[1] = Pose.q.y; Bones[i].Rot[2] = Pose.q.z; Bones[i].Rot[3] = Pose.q.w;
		}
		static std::atomic<int64> Frames{0};
		if (Frames % 600 == 0 && MaxError > 0.3f)
		{
			FString All;
			for (uint32 i = 0; i < Count; ++i)
			{
				const PxTransform Pose = R->Bodies[i]->getGlobalPose();
				All += FString::Printf(TEXT(" [%u p=%d err=%.2f anim=(%.2f,%.2f,%.2f) sim=(%.2f,%.2f,%.2f) vel=%.1f]"), i, Bones[i].Parent,
					(Pose.p - AnimPos[i]).magnitude(), AnimPos[i].x, AnimPos[i].y, AnimPos[i].z, Pose.p.x, Pose.p.y, Pose.p.z, R->Bodies[i]->getLinearVelocity().magnitude());
			}
			UE_LOG(LogOKPhysX, Display, TEXT("ragdoll detail:%s"), *All);
		}
		if (++Frames % 600 == 1) UE_LOG(LogOKPhysX, Display, TEXT("ragdoll group=%llu parts=%u max tracking error=%.3f m (part %d, angle off %.2f rad, parent %d) dt=%.5f substeps=%d asleep=%d"), GroupId, Count, MaxError, WorstBone, WorstAngle, WorstBone >= 0 ? Bones[WorstBone].Parent : -1, Dt, Substeps, int32(R->Bodies[0]->isSleeping()));
		return 1;
	}

	void PublishBody(uint64 GroupId, const void* Capsules, uint32 Count)
	{
		if (!GroupId) return;
		TSharedPtr<FGroup> Group;
		{
			UE::TUniqueLock Guard(GGroupLock);
			TSharedPtr<FGroup>& Slot = GGroups.FindOrAdd(GroupId);
			if (!Slot) Slot = MakeShared<FGroup>();
			Group = Slot;
		}
		{
			UE::TUniqueLock Guard(Group->Lock);
			// Skin capsules, then after a negative-radius separator the clothed layer (same order).
			const FOKPxCapsule* All = static_cast<const FOKPxCapsule*>(Capsules);
			int32 Split = int32(Count);
			for (int32 i = 0; i < int32(Count); ++i) if (All[i].Radius < 0.f) { Split = i; break; }
			Group->Body.Reset();
			Group->Body.Append(All, Split);
			TArray<FOKPxCapsule> Clothed;
			if (Split < int32(Count) && int32(Count) - Split - 1 == Split) Clothed.Append(All + Split + 1, Split);
			// Report a changed clothes layer once (how much each capsule grew, cm).
			FString Grown;
			for (int32 i = 0; i < Clothed.Num(); ++i) Grown += FString::Printf(TEXT("%d:%.1f "), i, (Clothed[i].Radius - Group->Body[i].Radius) * 100.f);
			static FString LastGrown;
			static UE::FMutex LogLock;
			{
				UE::TUniqueLock LogGuard(LogLock);
				if (Split < int32(Count)) Grown += FString::Printf(TEXT("| skinMedian=%.3f skinVerts=%.0f clothVerts=%.0f"), All[Split].A[0], All[Split].A[1], All[Split].A[2]);
				if (Grown != LastGrown) { UE_LOG(LogOKPhysX, Display, TEXT("clothes layer group=%llu grown(cm)=%s"), GroupId, Grown.IsEmpty() ? TEXT("none") : *Grown); LastGrown = Grown; }
			}
			Group->Clothed = MoveTemp(Clothed);
		}
		{
			static std::atomic<int32> Publishes{0};
			if (Count > 12 && (Publishes++ % 900) == 0)
			{
				const FOKPxCapsule* Body = static_cast<const FOKPxCapsule*>(Capsules);
				const FOKPxCapsule& Pelvis = Body[12];
				const FOKPxCapsule& Head = Body[13];
				UE_LOG(LogOKPhysX, Display, TEXT("group %llu pelvis=(%.2f %.2f %.2f) head=(%.2f %.2f %.2f) capsules=%u"), GroupId,
					(Pelvis.A[0] + Pelvis.B[0]) * 0.5f, (Pelvis.A[1] + Pelvis.B[1]) * 0.5f, (Pelvis.A[2] + Pelvis.B[2]) * 0.5f, Head.A[0], Head.A[1], Head.A[2], Count);
			}
		}
		// Do two characters' bodies share one space? Their closest approach tells (touching poses give ~0).
		TArray<TSharedPtr<FGroup>> OtherGroups;
		{
			UE::TUniqueLock Guard(GGroupLock);
			for (const TPair<uint64, TSharedPtr<FGroup>>& Pair : GGroups) if (Pair.Key != GroupId) OtherGroups.Add(Pair.Value);
		}
		const FOKPxCapsule* Mine = static_cast<const FOKPxCapsule*>(Capsules);
		for (const TSharedPtr<FGroup>& Other : OtherGroups)
		{
			UE::TUniqueLock Guard(Other->Lock);
			for (const FOKPxCapsule& A : Other->Body)
			{
				for (uint32 i = 0; i < Count; ++i)
				{
					const FOKPxCapsule& B = Mine[i];
					// endpoint-to-segment approximation of the segment gap
					float Gap = FLT_MAX;
					Gap = FMath::Min(Gap, SegmentDistance(V(B.A), V(A.A), V(A.B)));
					Gap = FMath::Min(Gap, SegmentDistance(V(B.B), V(A.A), V(A.B)));
					Gap = FMath::Min(Gap, SegmentDistance(V(A.A), V(B.A), V(B.B)));
					Gap = FMath::Min(Gap, SegmentDistance(V(A.B), V(B.A), V(B.B)));
					AtomicMinFloat(GMinBodyGapBits, Gap - A.Radius - B.Radius);
				}
			}
		}
	}

	void SetGroupFrame(uint64 GroupId, const FMatrix44f& SkeletonToWorld)
	{
		if (!GroupId) return;
		TSharedPtr<FGroup> Group;
		{
			UE::TUniqueLock Guard(GGroupLock);
			TSharedPtr<FGroup>& Slot = GGroups.FindOrAdd(GroupId);
			if (!Slot) Slot = MakeShared<FGroup>();
			Group = Slot;
		}
		UE::TUniqueLock Guard(Group->Lock);
		if (!Group->bHasFrame || !Group->Frame.Equals(SkeletonToWorld, 1e-3f))
		{
			const FMatrix44f& M = SkeletonToWorld;
			UE_LOG(LogOKPhysX, Display, TEXT("group %llu frame rows x=(%.3f %.3f %.3f) y=(%.3f %.3f %.3f) z=(%.3f %.3f %.3f) t=(%.3f %.3f %.3f) det=%.3f"),
				GroupId, M.M[0][0], M.M[0][1], M.M[0][2], M.M[1][0], M.M[1][1], M.M[1][2], M.M[2][0], M.M[2][1], M.M[2][2], M.M[3][0], M.M[3][1], M.M[3][2], M.Determinant());
		}
		Group->Frame = SkeletonToWorld;
		Group->bHasFrame = true;
	}

	float HeadTilt() { return CVarDebugHeadTilt.GetValueOnAnyThread(); }
	int32 AuditEnabled() { return CVarAudit.GetValueOnAnyThread() != 0 ? 1 : 0; }

	void ResetStats()
	{
		GMaxPenetrationBits = 0; GViolations = 0; GSteps = 0; GMinBodyGapBits = 0x7f7fffff; GMaxProjectionBits = 0; GResets = 0; GBodyPenetrationBits = 0; GBodyViolations = 0; GSolveCycles = 0; GPops = 0; GMaxSwingBits = 0; GMaxSwingWhere = 0; GMaxStretchBits = 0; GTrueDepthBits = 0; GTrueViolations = 0; GHardFixes = 0; GHardSubstep = 0; GHardLength = 0; GHardPoint = 0; GHardMid = 0; GSwingMicrons = 0; GSwingSamples = 0; GDropMicrons = 0; GJitter = 0; for (std::atomic<int64>& Phase : GPhaseCycles) Phase = 0; GMaxJerkBits = 0; for (std::atomic<int64>& Count : GViolationsByBody) Count = 0; for (int32 k = 0; k < 16; ++k) { GStepsBy[k] = 0; GPopsBy[k] = 0; GJitterBy[k] = 0; } GTailSteps = 0; GTailPops = 0; GTailJitter = 0; GTailSwingMicrons = 0; GTailSwingSamples = 0; GTailMaxSwingMicrons = 0; GTailDepthBits = 0; GTailDeep = 0; for (std::atomic<int64>& Count : GTailDeepByBody) Count = 0;
	}

	FStats GetStats()
	{
		FStats Out;
		const int32 Max = GMaxPenetrationBits.load(), Gap = GMinBodyGapBits.load();
		FMemory::Memcpy(&Out.MaxPenetration, &Max, 4);
		FMemory::Memcpy(&Out.MinBodyGap, &Gap, 4);
		Out.Violations = GViolations.load();
		const int32 Projection = GMaxProjectionBits.load();
		FMemory::Memcpy(&Out.MaxProjection, &Projection, 4);
		Out.Resets = GResets.load();
		Out.SolveSeconds = FPlatformTime::ToSeconds64(GSolveCycles.load());
		Out.Pops = GPops.load();
		{ const int32 Bits = GMaxSwingBits.load(); FMemory::Memcpy(&Out.MaxSwing, &Bits, 4); Out.MaxSwingWhere = GMaxSwingWhere.load(); }
		{ const int32 Bits = GMaxStretchBits.load(); FMemory::Memcpy(&Out.MaxStretch, &Bits, 4); }
		Out.TrueViolations = GTrueViolations.load();
		{ const int32 Bits = GTrueDepthBits.load(); FMemory::Memcpy(&Out.TrueDepth, &Bits, 4); }
		Out.HardFixes = GHardFixes.load();
		Out.HardSplit = FString::Printf(TEXT("%lld/%lld/%lld sub %lld"), GHardLength.load(), GHardPoint.load(), GHardMid.load(), GHardSubstep.load());
		Out.MeanSwing = GSwingSamples.load() ? float(double(GSwingMicrons.load()) * 1e-6 / double(GSwingSamples.load())) : 0.f;
		Out.MeanDrop = GSwingSamples.load() ? float(double(GDropMicrons.load()) * 1e-6 / double(GSwingSamples.load())) : 0.f;
		for (int32 k = 0; k < 4; ++k) Out.PhaseSeconds[k] = FPlatformTime::ToSeconds64(GPhaseCycles[k].load());
		Out.Jitter = GJitter.load();
		if (const int64 Steps = GTailSteps.load()) Out.BySolver += FString::Printf(TEXT("tail:%lld/%lld/%lld swing=%.3f max=%.3f seen=%.3f/%lld "), Steps, GTailPops.load(), GTailJitter.load(), GTailSwingSamples.load() ? double(GTailSwingMicrons.load()) * 1e-6 / double(GTailSwingSamples.load()) : 0., double(GTailMaxSwingMicrons.load()) * 1e-6, [] { const int32 Bits = GTailDepthBits.load(); float D; FMemory::Memcpy(&D, &Bits, 4); return D; }(), GTailDeep.load());
		for (int32 k = 0; k < 128; ++k) if (const int64 Count = GTailDeepByBody[k].load()) Out.BySolver += FString::Printf(TEXT("%s%d:%lld "), k >= 100 ? TEXT("tp") : TEXT("t"), k >= 100 ? k - 100 : k, Count);
		for (int32 k = 0; k < 16; ++k) if (const int64 Steps = GStepsBy[k].load()) Out.BySolver += FString::Printf(TEXT("%s%d:%lld/%lld/%lld "), k >= 8 ? TEXT("b") : TEXT(""), k & 7, Steps, GPopsBy[k].load(), GJitterBy[k].load());
		const int32 Jerk = GMaxJerkBits.load();
		FMemory::Memcpy(&Out.MaxJerk, &Jerk, 4);
		const int32 BodyPenetration = GBodyPenetrationBits.load();
		FMemory::Memcpy(&Out.BodyPenetration, &BodyPenetration, 4);
		Out.BodyViolations = GBodyViolations.load();
		for (int32 k = 0; k < 128; ++k) if (const int64 Count = GViolationsByBody[k].load()) Out.ViolationsByBody += FString::Printf(TEXT("%s%d:%lld "), k >= 100 ? TEXT("p") : TEXT(""), k >= 100 ? k - 100 : k, Count);
		Out.Steps = GSteps.load();
		return Out;
	}

	// Everything about one chain up to the shared substeps: validation, inertia from world motion, colliders,
	// bodies and joints set from the Rust state. Locks the chain until FinishJob.
	bool PrepareJob(FSolveJob& J, const TSharedPtr<FBatch>& Batch, bool bPhysX)
	{
		FOKPxChainStep& S = *J.S;
		TSharedPtr<FChain>& Chain = J.Chain;
		float& RefStep = J.RefStep;
		float& Dt = J.Dt;
		PxVec3& Accel = J.Accel;
		TArray<FOKPxCapsule>& Shapes = J.Shapes;
		TArray<FOKPxCapsule>& AuditShapes = J.AuditShapes;
		TArray<FOKPxCapsule>& FromShapes = J.FromShapes;
		TArray<uint64>& ShapeKeys = J.ShapeKeys;
		TArray<float>& FullRadius = J.FullRadius;
		TArray<uint8>& Attached = J.Attached;
		J.PartRadius.Reset(); FullRadius.Reset(); Attached.Reset();
		TArray<uint64>& AuditKeys = J.AuditKeys;
		TArray<PxVec3, TInlineAllocator<64>>& AnimPos = J.AnimPos;
		TArray<FVector4f>& Others = J.Others;
		int32& Substeps = J.Substeps;
		PxVec3& RootTo = J.RootTo;
		PxVec3& RootFrom = J.RootFrom;
		(void)ShapeKeys; (void)Others; (void)FromShapes; (void)Substeps; (void)RootTo; (void)RootFrom; (void)Accel; (void)Dt;
		if (S.ParticleCount < 2 || !(S.Dt > 0.f) || !(S.ReferenceStep > 0.f)) return false;
		if (!(S.Particles[0].Flags & 1u)) return false;
		// A particle that is far from where its parent and goal put it (NaN, a pose jump, an earlier blow-up)
		// is reset onto its goal at rest instead of being dragged across by the joints.
		for (uint32 i = 1; i < S.ParticleCount && !J.bAuditOnly; ++i)
		{
			FOKPxParticle& P = S.Particles[i];
			const FOKPxParticle& Parent = S.Particles[P.Parent];
			const PxVec3 Goal = V(Parent.Pos) + V(P.GoalOffset);
			const PxVec3 Pos = V(P.Pos);
			if (!Pos.isFinite() || !V(P.Prev).isFinite() || (Pos - Goal).magnitude() > P.RestLen * 4.f + 0.3f)
			{
				Put(P.Pos, Goal);
				Put(P.Prev, Goal);
			}
		}

		{
			UE::TUniqueLock Guard(GMapLock);
			TSharedPtr<FChain>& Slot = GChains.FindOrAdd(S.Key);
			if (!Slot) Slot = MakeShared<FChain>();
			Chain = Slot;
		}
		Chain->Lock.Lock(); // released in FinishJob
		if (bPhysX && (!Chain->Matches(S) || Chain->Batch != Batch)) { Chain->Batch = Batch; Chain->Build(S, Batch ? Batch->Scene : nullptr); }

		// The character itself moved or turned in the world (actor movement, not animation): particles keep their
		// world position and velocity, i.e. they lag behind and swing like real hair and cloth.
		FMatrix44f GroupFrame = FMatrix44f::Identity;
		bool bGroupFrame = false;
		if (S.Group)
		{
			TSharedPtr<FGroup> FrameGroup;
			{
				UE::TUniqueLock FrameListGuard(GGroupLock);
				FrameGroup = GGroups.FindRef(S.Group);
			}
			if (FrameGroup)
			{
				UE::TUniqueLock FrameGuard(FrameGroup->Lock);
				GroupFrame = FrameGroup->Frame;
				bGroupFrame = FrameGroup->bHasFrame;
			}
		}
		{
			// Switching solvers (or a pause) leaves this chain's history behind: last colliders, last world frame, the
			// clearance smoothing and the root body. Using them after the gap swept colliders and the root across the
			// whole gap in one step and dragged the chain (torn underwear after Verlet -> PhysX/Kawaii/Chaos).
			const double Now = FPlatformTime::Seconds();
			Chain->bStale = Chain->LastSolveTime > 0. && Now - Chain->LastSolveTime > 0.25;
			if (!J.bAuditOnly) Chain->LastSolveTime = Now;
			const int32 Epoch = GResetEpoch.load();
			if (Chain->ResetEpoch != Epoch && !J.bAuditOnly)
			{
				// Particles back on their goals (parents come first), at rest.
				Chain->ResetEpoch = Epoch;
				J.bReset = true;
				Chain->bStale = true;
				for (uint32 i = 1; i < S.ParticleCount; ++i)
				{
					FOKPxParticle& P = S.Particles[i];
					if (P.Parent < 0 || P.Parent >= int32(i)) continue;
					const FOKPxParticle& Parent = S.Particles[P.Parent];
					for (int32 a = 0; a < 3; ++a) { P.Pos[a] = Parent.Pos[a] + P.GoalOffset[a]; P.Prev[a] = P.Pos[a]; }
				}
			}
			if (Chain->bStale)
			{
				Chain->LastShapes.Reset();
				Chain->bHasLastFrame = false;
				Chain->BodyClear.Reset();
				Chain->HistoryFrames = 0;
			}
		}
		if (bGroupFrame && Chain->bHasLastFrame && CVarWorldInertia.GetValueOnAnyThread() && !J.bAuditOnly)
		{
			const FMatrix44f ToNew = Chain->LastFrame * GroupFrame.Inverse(); // old local -> world -> new local (row vectors)
			const FVector3f Moved = ToNew.TransformPosition(FVector3f::ZeroVector);
			if (Moved.Length() < 1.f) // larger jumps are teleports, not motion
			{
				for (uint32 i = 1; i < S.ParticleCount; ++i)
				{
					FOKPxParticle& P = S.Particles[i];
					const FVector3f NewPos = ToNew.TransformPosition(F3(P.Pos));
					const FVector3f NewPrev = ToNew.TransformPosition(F3(P.Prev));
					P.Pos[0] = NewPos.X; P.Pos[1] = NewPos.Y; P.Pos[2] = NewPos.Z;
					P.Prev[0] = NewPrev.X; P.Prev[1] = NewPrev.Y; P.Prev[2] = NewPrev.Z;
				}
			}
		}
		Chain->LastFrame = GroupFrame;
		Chain->bHasLastFrame = bGroupFrame;
		static std::atomic<int64> Calls{0};
		const int64 N = ++Calls;
		if (N == 1 || N % 20000 == 0) UE_LOG(LogOKPhysX, Display, TEXT("PhysX solved %lld chain steps (chains=%d, particles=%u, colliders=%u)"), N, GChains.Num(), S.ParticleCount, S.ColliderCount);

		RefStep = S.ReferenceStep;
		Dt = FMath::Min(S.Dt, 0.1f);
		Accel = V(S.GravityStep) / (RefStep * RefStep);
		// Weight: real gravity on top of the card's (usually very light) DynamicBone gravity, toward the world ground.
		if (!(S.Particles[0].Flags & 2u))
		{
			const float Extra = (FMath::Clamp(CVarWeight.GetValueOnAnyThread(), 1.f, 10.f) - 1.f) / 9.f * 9.81f;
			if (Extra > 0.f)
			{
				// Skeleton space is the session's (Unity, Y up) unless the owner published where it sits in the world.
				FVector3f Down(0.f, -1.f, 0.f);
				if (bGroupFrame) Down = GroupFrame.Inverse().TransformVector(FVector3f(0.f, 0.f, -1.f)).GetSafeNormal();
				Accel += PxVec3(Down.X, Down.Y, Down.Z) * Extra;
			}
		}

		// Colliders = the chain's authored ones + the character's body capsules; the other
		// chains of the character join as frozen spheres. Only what can reach this chain counts.
		float Lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, Hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
		float Reach = 0.f;
		for (uint32 i = 0; i < S.ParticleCount; ++i)
		{
			const FOKPxParticle& P = S.Particles[i];
			Reach = FMath::Max(Reach, P.RestLen * 2.f + FMath::Max(P.Radius, P.BodyRadius));
			for (int32 a = 0; a < 3; ++a) { Lo[a] = FMath::Min(Lo[a], P.Pos[a]); Hi[a] = FMath::Max(Hi[a], P.Pos[a]); }
		}
		for (int32 a = 0; a < 3; ++a) { Lo[a] -= Reach; Hi[a] += Reach; }
		auto Overlaps = [&](const float* A, const float* B, float R)
		{
			for (int32 a = 0; a < 3; ++a) if (FMath::Max(A[a], B[a]) + R < Lo[a] || FMath::Min(A[a], B[a]) - R > Hi[a]) return false;
			return true;
		};
		Shapes.Reserve(int32(S.ColliderCount) + 16);
		for (uint32 i = 0; i < S.ColliderCount; ++i) { Shapes.Add(S.Colliders[i]); ShapeKeys.Add((1ull << 63) | i); }
		J.AuthoredShapes = int32(S.ColliderCount);
		// Animated pose of the chain: the root plus the goal offsets (what the animation alone would show).
		AnimPos.SetNum(S.ParticleCount);
		AnimPos[0] = V(S.Particles[0].Pos);
		for (uint32 i = 1; i < S.ParticleCount; ++i) AnimPos[i] = AnimPos[S.Particles[i].Parent] + V(S.Particles[i].GoalOffset);
		TArray<FOKPxCapsule> OwnBody;
		// Skin chains of the body mesh (crotch/anus pivots of an uncensor body): no garment is skinned to their bones, so
		// shoving them out of the rough proxies (own or partner's) pushes the skin through tights and pantyhose.
		const bool bSkinChain = (S.Particles[0].Flags & 16u) && !CVarSkinBodyCollision.GetValueOnAnyThread();
		const int32 SelfBits = bSkinChain ? 0 : CVarSelfCollision.GetValueOnAnyThread();
		// Breasts and hips: the game gives their DynamicBone_Ver02 chains no body colliders; the own proxies and the hair
		// strands hanging over them dented them and, as the hair swung, kept them shaking. The partner's body still counts.
		const bool bBodyChain = (S.Particles[0].Flags & 2u) && !CVarBodySelfCollision.GetValueOnAnyThread();
		if (S.Group) Chain->Group = S.Group; // released with its group even when it does not collide with it
		if (S.Group && SelfBits)
		{
			TSharedPtr<FGroup> Group;
			{
				UE::TUniqueLock GroupGuard(GGroupLock);
				Group = GGroups.FindRef(S.Group);
			}
			if (Group)
			{
				UE::TUniqueLock GroupGuard(Group->Lock);
				// Chains worn over the clothes (hair, accessories) rest on the clothed layer; clothes on the skin.
				const bool bOuter = (S.Particles[0].Flags & 8u) && Group->Clothed.Num() == Group->Body.Num();
				OwnBody = bOuter ? Group->Clothed : Group->Body; // never hold two group locks at once (two solver threads would deadlock)
				if ((SelfBits & 2) && !bBodyChain) for (const TPair<uint64, FSnapshot>& Pair : Group->Chains)
				{
					if (Pair.Key == S.Key) continue;
					for (int32 SphereIndex = 0; SphereIndex < Pair.Value.Spheres.Num(); ++SphereIndex)
					{
						const FVector4f& Sphere = Pair.Value.Spheres[SphereIndex];
						const uint64 Id = (Pair.Key << 8) ^ uint64(SphereIndex);
						if (!Chain->SeenProxy.Contains(Id))
						{
							// Particles already touching this chain at rest (layers, strands rooted together) never collide.
							Chain->SeenProxy.Add(Id);
							for (uint32 i = 1; i < S.ParticleCount; ++i)
							{
								if ((V(S.Particles[i].Pos) - PxVec3(Sphere.X, Sphere.Y, Sphere.Z)).magnitude() < Sphere.W + FMath::Max(S.Particles[i].Radius, 1e-4f) + 0.004f)
								{
									Chain->IgnoredProxy.Add(Id);
									break;
								}
							}
						}
						if (Chain->IgnoredProxy.Contains(Id)) continue;
						const float Center[3] = {Sphere.X, Sphere.Y, Sphere.Z};
						if (Overlaps(Center, Center, Sphere.W)) Others.Add(Sphere);
					}
				}
			}
			Chain->Group = S.Group;
		if (SelfBits & 1)
		{
			const int32 Mask = CVarBodyMask.GetValueOnAnyThread();
			TArray<TArray<float>> PerShape;
			auto AddBody = [&](uint64 BodyGroupId, const TArray<FOKPxCapsule>& Body)
			{
				// Hair and accessories grow out of the own body part nearest their root (bangs: the head). That part is the
				// card's authored colliders' job, as in the game: the rough proxy kept pushing the strands off the forehead
				// (soft layer and exact pass) and weak bangs stayed swept aside. Every other proxy still counts.
				int32 RootBody = INDEX_NONE;
				if (BodyGroupId == S.Group && (S.Particles[0].Flags & 8u) && CVarSkipRootBody.GetValueOnAnyThread())
				{
					float Nearest = CVarClearanceAttachMargin.GetValueOnAnyThread();
					for (int32 BodyIndex = 0; BodyIndex < Body.Num(); ++BodyIndex)
					{
						const FOKPxCapsule& C = Body[BodyIndex];
						if (C.Radius <= 0.f || C.Radius > 1.f) continue; // the separator and the floor sphere
						const float Gap = SegmentDistance(AnimPos[0], V(C.A), V(C.B)) - C.Radius;
						if (Gap < Nearest) { Nearest = Gap; RootBody = BodyIndex; }
					}
				}
				for (int32 BodyIndex = 0; BodyIndex < Body.Num(); ++BodyIndex)
				{
					FOKPxCapsule C = Body[BodyIndex];
					if (!(Mask & (1 << FMath::Min(BodyIndex, 30)))) continue;
					if (BodyIndex == RootBody) continue;
					if (!Overlaps(C.A, C.B, C.Radius)) continue; // out of reach: no clearance to compute
					// Clothes and hair rest on the skin and the body proxies are rough. Each frame the capsule is shrunk
					// to the clearance of the chain's *animated* pose, so only motion the animation would not produce
					// (swinging, sagging, long hair falling when the head is thrown back) is pushed out.
					// Clearance of the *animated* pose per particle (particle and its bone middle): the animation itself may put
					// it inside a rough proxy, and only motion beyond the animation is pushed out.
					const int32 Mode = CVarClearanceMode.GetValueOnAnyThread();
					const float Full = C.Radius;
					// "Attached": the body the chain hangs on or rests against at its root (hair: the head; a skirt: hips and thighs).
					// Only the character's own body: the animation knows nothing about the partner, so a tail rooted at the waist
					// with the partner's pelvis right behind it stood up through the partner's torso as "animated pose".
					const bool bOwnBody = BodyGroupId == S.Group || CVarPartnerAttach.GetValueOnAnyThread() != 0;
					const bool bAttached = bOwnBody && SegmentDistance(AnimPos[0], V(C.A), V(C.B)) < Full + FMath::Max(S.Particles[0].Radius, 0.f) + CVarClearanceAttachMargin.GetValueOnAnyThread();
					const float MaxAllow = FMath::Max(CVarClearanceMaxAllow.GetValueOnAnyThread(), 0.f);
					TArray<float, TInlineAllocator<64>> Per;
					Per.SetNumUninitialized(S.ParticleCount);
					Per[0] = Full;
					float Clear = FLT_MAX;
					for (uint32 i = 1; i < S.ParticleCount; ++i)
					{
						const float R = FMath::Max(S.Particles[i].Radius, 1e-4f);
						const float Own = FMath::Min(SegmentDistance(AnimPos[i], V(C.A), V(C.B)),
							BoneDistance(AnimPos[S.Particles[i].Parent], AnimPos[i], V(C.A), V(C.B))) - R;
						Clear = FMath::Min(Clear, Own);
						Per[i] = Own;
					}
					const uint64 SmoothKey = (BodyGroupId << 6) | uint64(BodyIndex & 63);
					float Largest = 0.f;
					for (uint32 i = 1; i < S.ParticleCount; ++i)
					{
						float Target = Mode < 0 ? Full : FMath::Min(Full, FMath::Max((Mode == 0 ? Clear : Per[i]) - 0.004f, 0.f));
						// Mode 2: a body the chain does not hang from keeps all but MaxAllow of its radius (proxy roughness):
						// a pose that sends hair deep into the chest does not switch the chest off.
						if (Mode == 2 && !bAttached) Target = FMath::Max(Target, Full - MaxAllow);
						// A collider that suddenly grows shoves resting hair in one frame (visible popping): it may shrink at
						// once (never inside the animated pose) but grows by at most 1 cm per step.
						const uint64 Key = Mode == 0 ? SmoothKey : (SmoothKey * 1099511628211ull) ^ (uint64(i) + 1);
						if (float* Last = Chain->BodyClear.Find(Key)) Target = FMath::Min(Target, *Last + 0.01f);
						Chain->BodyClear.Add(Key, Target);
						// Another character's body is met at the mesh's own thickness around the bone (authored radii are often
						// 0: a thick tail collided as a line and stood through the partner's torso).
						// Tails (flag 32) meet their own body at their thickness too (not the body they hang from), and only the
						// solid part of it (TailFluff): the fluff squashes elastically.
						const bool bTail = (S.Particles[0].Flags & 32u) != 0;
						if (CVarMeshRadius.GetValueOnAnyThread() && (BodyGroupId != S.Group || (bTail && !bAttached)))
							Target += FMath::Max(S.Particles[i].BodyRadius * (bTail ? FMath::Clamp(CVarTailFluff.GetValueOnAnyThread(), 0.f, 1.f) : 1.f) - FMath::Max(S.Particles[i].Radius, 0.f), 0.f);
						Per[i] = Target;
						Largest = FMath::Max(Largest, Target);
					}
					C.Radius = Largest;
					if (C.Radius > 0.003f && Overlaps(C.A, C.B, C.Radius))
					{
						Shapes.Add(C); ShapeKeys.Add(SmoothKey); AuditShapes.Add(C); AuditKeys.Add(BodyGroupId == S.Group ? uint64(BodyIndex) : 100ull + BodyIndex);
						FullRadius.Add(Full); Attached.Add(bAttached ? 1 : 0);
						PerShape.Add(TArray<float>(Per.GetData(), Per.Num()));
					}
				}
			};
			// Partners first, own body last: the exact pass resolves colliders in order, so when cloth is pinned between
			// the two bodies it ends outside its own body (the one it is draped on) rather than inside it.
			if (CVarCrossCollision.GetValueOnAnyThread())
			{
				TArray<TPair<uint64, TSharedPtr<FGroup>>> OtherGroups;
				{
					UE::TUniqueLock ListGuard(GGroupLock);
					for (const TPair<uint64, TSharedPtr<FGroup>>& Pair : GGroups) if (Pair.Key != S.Group) OtherGroups.Add(Pair);
				}
				for (const TPair<uint64, TSharedPtr<FGroup>>& Other : OtherGroups)
				{
					TArray<FOKPxCapsule> OtherBody;
					FMatrix44f OtherFrame = FMatrix44f::Identity;
					bool bOtherFrame = false;
					{
						UE::TUniqueLock OtherGuard(Other.Value->Lock);
						// The partner is met at the surface of their clothes.
						OtherBody = Other.Value->Clothed.Num() == Other.Value->Body.Num() ? Other.Value->Clothed : Other.Value->Body;
						OtherFrame = Other.Value->Frame;
						bOtherFrame = Other.Value->bHasFrame;
					}
					// A body without a world frame cannot be placed next to one that has it: a session that published
					// once and has no actor (a loader's temporary, a clothing preview) would otherwise sit at our origin
					// as a phantom partner that hair and cloth keep colliding with.
					if (bOtherFrame != bGroupFrame) continue;
					// Each character has its own skeleton space; bring the partner's body into ours via the world.
					if (bOtherFrame && bGroupFrame)
					{
						const FMatrix44f ToMine = OtherFrame * GroupFrame.Inverse();
						for (FOKPxCapsule& C : OtherBody)
						{
					const FVector3f A = ToMine.TransformPosition(F3(C.A)), B = ToMine.TransformPosition(F3(C.B));
					C.A[0] = A.X; C.A[1] = A.Y; C.A[2] = A.Z;
					C.B[0] = B.X; C.B[1] = B.Y; C.B[2] = B.Z;
						}
					}
					AddBody(Other.Key, OtherBody);
				}
			}
			if (!bBodyChain) AddBody(S.Group, OwnBody);
			// Authored chain colliders (added before the bodies) keep their own radius for every particle.
			const int32 K = Shapes.Num(), First = K - PerShape.Num();
			J.PartRadius.SetNumUninitialized(int32(S.ParticleCount) * K);
			for (int32 i = 0; i < int32(S.ParticleCount); ++i)
				for (int32 k = 0; k < K; ++k)
					J.PartRadius[i * K + k] = k < First ? Shapes[k].Radius : PerShape[k - First][i];
			TArray<float> Authored; for (int32 k = 0; k < First; ++k) Authored.Add(Shapes[k].Radius);
			J.FullRadius.Insert(Authored, 0);
			TArray<uint8> NotAttached; NotAttached.Init(0, First); J.Attached.Insert(NotAttached, 0);
		}
		}
		// Colliders are not PhysX bodies: PhysX contacts are soft and kinematic bodies cost a lot per chain. They are
		// resolved exactly after every substep (below), swept from their previous pose to the current one.
		FromShapes.Reserve(Shapes.Num());
		float ColliderMove = 0.f;
		for (int32 k = 0; k < Shapes.Num(); ++k)
		{
			const FOKPxCapsule* Last = Chain->LastShapes.Find(ShapeKeys[k]);
			FromShapes.Add(Last ? *Last : Shapes[k]);
			if (Last)
			{
				ColliderMove = FMath::Max(ColliderMove, FMath::Max((V(Last->A) - V(Shapes[k].A)).magnitude(), (V(Last->B) - V(Shapes[k].B)).magnitude()));
			}
		}
		Chain->LastShapes.Reset();
		for (int32 k = 0; k < Shapes.Num(); ++k) Chain->LastShapes.Add(ShapeKeys[k], Shapes[k]);
		if (bPhysX) Chain->SyncProxies(Others);
		// Fast heads/limbs: never let a kinematic collider or the root travel more than ~2 cm per substep.
		const PxVec3 RootDelta = bPhysX ? V(S.Particles[0].Pos) - Chain->Bodies[0]->getGlobalPose().p : V(S.Particles[0].Pos) - V(S.Particles[0].Prev);
		const float Travel = FMath::Max(ColliderMove, RootDelta.magnitude());
		const int32 MaxSub = FMath::Max(1, CVarMaxSubsteps.GetValueOnAnyThread());
		const int32 TimeSub = FMath::CeilToInt(Dt / FMath::Max(CVarSubstep.GetValueOnAnyThread(), 1e-4f));
		Substeps = FMath::Clamp(FMath::Max(TimeSub, FMath::CeilToInt(Travel / 0.03f)), 1, MaxSub);
		const float Sub = Dt / Substeps;
		RootTo = V(S.Particles[0].Pos);
		RootFrom = bPhysX ? Chain->Bodies[0]->getGlobalPose().p : V(S.Particles[0].Prev);
		if ((RootTo - RootFrom).magnitudeSquared() > 1.f || Chain->bStale) RootFrom = RootTo; // teleport or stale: do not drag the chain
		if (bPhysX) for (uint32 i = 0; i < S.ParticleCount; ++i)
		{
			const FOKPxParticle& P = S.Particles[i];
			PxRigidDynamic* Body = Chain->Bodies[i];
			if (P.Parent < 0)
			{
				Body->setGlobalPose(PxTransform(RootFrom));
				continue;
			}
			const float R = FMath::Max(P.Radius, 1e-4f);
			if (FMath::Abs(Chain->Radii[i] - R) > 1e-6f)
			{
				PxShape* Shape = nullptr;
				Body->getShapes(&Shape, 1);
				Shape->setGeometry(PxSphereGeometry(R));
				Chain->Radii[i] = R;
			}
			Body->setGlobalPose(PxTransform(V(P.Pos)));
			Body->setLinearVelocity((V(P.Pos) - V(P.Prev)) * (S.VelocityScale / RefStep));
			// Verlet damping 1-d per reference step as a continuous coefficient.
			if (Chain->JointCache.Num() != int32(S.ParticleCount)) { Chain->JointCache.Init(FVector4f(-1.f), S.ParticleCount); Chain->DampingCache.Init(-1.f, S.ParticleCount); }
			// Softness (not for soft-tissue chains): pull, range and damping scaled together, same for every solver.
			const OKPhysXSecondary::FSoftness Softness = OKPhysXSecondary::Softness(S.Particles[0].Flags);
			const float PullScale = Softness.Pull, RangeScale = Softness.Range, DampScale = Softness.Damp;
			const float LinearDamping = FMath::Min(P.Damping * DampScale / RefStep, 0.9f / Sub);
			if (Chain->DampingCache[i] != LinearDamping) { Body->setLinearDamping(LinearDamping); Chain->DampingCache[i] = LinearDamping; }

			// Elasticity moves a particle e*(goal-pos) per reference step -> acceleration e/step^2.
			const float K = P.Elasticity * PullScale / (RefStep * RefStep);
			PxD6Joint* Spring = Chain->Springs[i];
			Spring->setLocalPose(PxJointActorIndex::eACTOR0, PxTransform(V(P.GoalOffset)));
			// Damped relative to the parent (critically by default): body damping alone is relative to the world, so
			// the spring kept ringing against the moving parent at the frame rate (hair shaking).
			FVector4f& Cached = Chain->JointCache[i];
			if (Cached.X != K)
			{
				// Below critical damping when soft: the strand overshoots its goal a little and settles (a soft bounce).
				const float Zeta = CVarDriveDamping.GetValueOnAnyThread() * Softness.Zeta;
				const PxD6JointDrive Drive(K, 2.f * Zeta * FMath::Sqrt(FMath::Max(K, 0.f)), PX_MAX_F32, true);
				Spring->setDrive(PxD6Drive::eX, Drive);
				Spring->setDrive(PxD6Drive::eY, Drive);
				Spring->setDrive(PxD6Drive::eZ, Drive);
				Cached.X = K;
			}
			// Stiffness bounds how far the particle may stray from its goal (2*len*(1-stiffness)).
			// A soft limit: a hard one fights the hard bone-length joint (two rigid constraints from different anchors)
			// and the solver alternates between them every frame. Particles weigh 1 kg, so these gains are accelerations.
			const float LimitK = CVarLimitStiffness.GetValueOnAnyThread() * PullScale;
			const float Extent = FMath::Max(P.RestLen * (1.f - P.Stiffness) * 2.f * RangeScale, 1e-4f);
			if (FMath::Abs(Cached.Y - Extent) > 1e-5f)
			{
				Spring->setDistanceLimit(PxJointLinearLimit(Extent, PxSpring(LimitK, 2.f * FMath::Sqrt(LimitK))));
				Cached.Y = Extent;
			}

			const PxVec3 Normal = V(P.FreezeNormal);
			if (Normal.magnitudeSquared() > 0.25f)
			{
				if (!Chain->Planes[i])
				{
					PxD6Joint* Plane = PxD6JointCreate(*GPhysics, Chain->Bodies[P.Parent], PxTransform(PxIdentity), Body, PxTransform(PxIdentity));
					const float PlaneK = CVarPlaneStiffness.GetValueOnAnyThread();
					if (PlaneK > 0.f)
					{
						// Soft plane: a zero-width limit with a critically damped spring instead of a locked axis.
						Plane->setMotion(PxD6Axis::eX, PxD6Motion::eLIMITED);
						Plane->setLinearLimit(PxD6Axis::eX, PxJointLinearLimitPair(-1e-4f, 1e-4f, PxSpring(PlaneK, 2.f * FMath::Sqrt(PlaneK))));
					}
					else Plane->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
					Plane->setMotion(PxD6Axis::eY, PxD6Motion::eFREE);
					Plane->setMotion(PxD6Axis::eZ, PxD6Motion::eFREE);
					Chain->Planes[i] = Plane;
				}
				// Joint X axis = plane normal; both frames share the rotation, origin at the parent.
				const PxQuat Q = PxShortestRotation(PxVec3(1.f, 0.f, 0.f), Normal.getNormalized());
				Chain->Planes[i]->setLocalPose(PxJointActorIndex::eACTOR0, PxTransform(PxVec3(0.f), Q));
				Chain->Planes[i]->setLocalPose(PxJointActorIndex::eACTOR1, PxTransform(PxVec3(0.f), Q));
			}
			PxDistanceJoint* Length = Chain->Lengths[i];
			const float Rest = FMath::Max(P.RestLen, 1e-5f);
			const float MinLen = Rest / FMath::Max(1.f - P.MinFraction, 0.05f), MaxLen = Rest / FMath::Max(1.f - P.MaxFraction, 0.05f);
			const float LengthK = CVarLengthStiffness.GetValueOnAnyThread();
			if (!!(Length->getDistanceJointFlags() & PxDistanceJointFlag::eSPRING_ENABLED) != (LengthK > 0.f) || (LengthK > 0.f && Length->getStiffness() != LengthK))
			{
				// Soft bone length: outside [min, max] a critically damped spring pulls back instead of a rigid rod.
				PxDistanceJointFlags Flags = PxDistanceJointFlag::eMIN_DISTANCE_ENABLED | PxDistanceJointFlag::eMAX_DISTANCE_ENABLED;
				if (LengthK > 0.f) { Flags |= PxDistanceJointFlag::eSPRING_ENABLED; Length->setStiffness(LengthK); Length->setDamping(2.f * FMath::Sqrt(LengthK)); }
				Length->setDistanceJointFlags(Flags);
			}
			if (FMath::Abs(Cached.Z - MinLen) > 1e-6f) { Length->setMinDistance(MinLen); Cached.Z = MinLen; }
			if (FMath::Abs(Cached.W - MaxLen) > 1e-6f) { Length->setMaxDistance(MaxLen); Cached.W = MaxLen; }
		}

		return true;
	}

	// One substep of one chain around the shared simulate(): kinematic root and gravity before, exact collisions after.
	void BeforeSubstep(FSolveJob& J, float Alpha)
	{
		const FOKPxChainStep& S = *J.S;
		J.Chain->Bodies[0]->setKinematicTarget(PxTransform(J.RootFrom + (J.RootTo - J.RootFrom) * Alpha));
		for (uint32 i = 1; i < S.ParticleCount; ++i)
		{
			J.Chain->Bodies[i]->addForce(J.Accel, PxForceMode::eACCELERATION);
		}
	}

	// The contact handling of one substep on plain positions/velocities, shared by PhysX (AfterSubstep) and, with
	// OKPhysXSecondary.AltSubstepCollision, the other backends. Substeps = how many substeps this frame has.
	void SubstepContact(FSolveJob& J, float Alpha, int32 Substeps, TArrayView<PxVec3> Pos, TArrayView<PxVec3> Vel)
	{
		const FOKPxChainStep& S = *J.S;
		const TArray<FOKPxCapsule>& Shapes = J.Shapes;
		const TArray<FOKPxCapsule>& FromShapes = J.FromShapes;
		// Soft contact: a skin layer outside every collider where an implicit, critically damped spring pushes out
		// (relative to the collider's own motion). The layer grows with the approach speed by the spring's stopping
		// distance (v / sqrt(k)), so fast hair is slowed before it reaches the body instead of being projected out.
		// Bone middles get the same layer, shared by both ends of the bone. Only an error beyond the hard tolerance is
		// then projected, partly, and the soft layer finishes the job over the next substeps.
		// The DynamicBone backend collides as DynamicBone does: the particle is projected onto the surface at once. The
		// soft layer, the step cap and the push memory are for PhysX's springs; against DynamicBone's strong pull toward
		// the goal they made contact and pull fight every frame (jitter).
		const float ContactK = J.bProjectOnly ? 0.f : CVarContactStiffness.GetValueOnAnyThread();
		const float Skin = ContactK > 0.f ? FMath::Max(CVarContactSkin.GetValueOnAnyThread(), 0.f) : 0.f;
		const float Lookahead = FMath::Max(CVarContactLookahead.GetValueOnAnyThread(), 0.f);
		const float Tolerance = J.bProjectOnly ? 0.f : FMath::Max(CVarHardTolerance.GetValueOnAnyThread(), 0.f);
		const float MaxStep = J.bProjectOnly ? 1e9f : FMath::Max(CVarHardMaxStep.GetValueOnAnyThread(), 1e-4f) / FMath::Max(Substeps, 1);
		const float Sub = J.Dt / FMath::Max(Substeps, 1);
		if (!Shapes.Num() || S.ParticleCount < 2) return;

		struct FShapeNow { PxVec3 A, AB, Vel; float L2, Radius; };
		TArray<FShapeNow, TInlineAllocator<32>> Now;
		Now.SetNumUninitialized(Shapes.Num());
		for (int32 k = 0; k < Shapes.Num(); ++k)
		{
			const PxVec3 A = V(FromShapes[k].A) + (V(Shapes[k].A) - V(FromShapes[k].A)) * Alpha;
			const PxVec3 B = V(FromShapes[k].B) + (V(Shapes[k].B) - V(FromShapes[k].B)) * Alpha;
			Now[k] = { A, B - A, ((V(Shapes[k].A) - V(FromShapes[k].A)) + (V(Shapes[k].B) - V(FromShapes[k].B))) * (0.5f / FMath::Max(J.Dt, 1e-6f)), (B - A).dot(B - A), Shapes[k].Radius };
		}
		auto Nearest = [&Now](const PxVec3& X, int32 k, PxVec3& Normal, float& Dist)
		{
			const FShapeNow& C = Now[k];
			const float T = C.L2 > 1e-12f ? FMath::Clamp((X - C.A).dot(C.AB) / C.L2, 0.f, 1.f) : 0.f;
			const PxVec3 Out = X - (C.A + C.AB * T);
			Dist = Out.magnitude();
			Normal = Dist > 1e-6f ? Out / Dist : PxVec3(0.f);
		};

		const int32 Count = int32(S.ParticleCount);
		TArray<PxVec3, TInlineAllocator<64>> Dv;
		Dv.Init(PxVec3(0.f), Count);
		const float Omega = FMath::Sqrt(FMath::Max(ContactK, 0.f));
		const float Damp = 2.f * Omega;
		// Implicit damped spring: velocity change along the normal for a penetration Depth into the layer.
		auto Spring = [&](float Depth, float Vn) { return (ContactK * Depth - Damp * Vn) * Sub / (1.f + Damp * Sub + ContactK * Sub * Sub); };
		if (Skin > 0.f && Sub > 0.f)
		{
			for (int32 i = 1; i < Count; ++i)
			{
				const int32 Parent = S.Particles[i].Parent;
				const bool bParentFree = !(S.Particles[Parent].Flags & 1u) && Parent > 0;
				const float R = FMath::Max(S.Particles[i].Radius, 0.f);
				for (int32 k = 0; k < Now.Num(); ++k)
				{
					PxVec3 Normal; float Dist;
					Nearest(Pos[i], k, Normal, Dist);
					if (Dist > 1e-6f)
					{
						const float Vn = (Vel[i] - Now[k].Vel).dot(Normal);
						// The chain's own DynamicBone colliders act like DynamicBone's: on the particle sphere, no skin layer.
						const float Layer = k < J.AuthoredShapes ? 0.f : Skin + FMath::Min(FMath::Max(-Vn, 0.f) / FMath::Max(Omega, 1e-3f), Lookahead);
						const float Depth = J.Rad(i, k) + R + Layer - Dist;
						if (Depth > 0.f) { const float D = Spring(Depth, Vn); if (D > 0.f) Dv[i] += Normal * D; }
					}
					// The bone itself, at its point closest to the collider (the particle end is handled above). Not for the
					// chain's own colliders: strands lie along a head sphere, every bone is a chord through it, and pushing the
					// chords out flipped the bangs back (DynamicBone tests particles only).
					if (k < J.AuthoredShapes) continue;
					float U; PxVec3 OnBone, OnAxis;
					Dist = BoneClosest(Pos[Parent], Pos[i], Now[k].A, Now[k].A + Now[k].AB, U, OnBone, OnAxis);
					if (Dist > 1e-6f && U > 0.05f && U < 0.95f)
					{
						Normal = (OnBone - OnAxis) / Dist;
						const PxVec3 PointVel = (bParentFree ? Vel[Parent] : PxVec3(0.f)) * (1.f - U) + Vel[i] * U;
						const float Vn = (PointVel - Now[k].Vel).dot(Normal);
						const float Layer = Skin + FMath::Min(FMath::Max(-Vn, 0.f) / FMath::Max(Omega, 1e-3f), Lookahead);
						const float Depth = J.Rad(i, k) + R + Layer - Dist;
						if (Depth > 0.f)
						{
							const float D = Spring(Depth, Vn);
							if (D > 0.f)
							{
								// Moving both ends by D moves that point by D; with a kinematic parent the particle end
								// carries it by the lever 1/U.
								if (bParentFree) { Dv[i] += Normal * D; Dv[Parent] += Normal * D; }
								else Dv[i] += Normal * (D / FMath::Max(U, 0.2f));
							}
						}
					}
				}
			}
			for (int32 i = 1; i < Count; ++i)
			{
				if (Dv[i].magnitudeSquared() > 0.f) Vel[i] += Dv[i];
			}
		}
		// Safety net: a particle still deeper than the tolerance inside a collider is moved out (at most MaxStep per
		// substep) and loses only the velocity that drives it further in.
		for (int32 i = 1; i < Count; ++i)
		{
			const float R = FMath::Max(S.Particles[i].Radius, 0.f);
			PxVec3 P = Pos[i];
			for (int32 k = 0; k < Now.Num(); ++k)
			{
				PxVec3 Normal; float Dist;
				Nearest(P, k, Normal, Dist);
				const float Depth = J.Rad(i, k) + R - Dist;
				if (Depth <= Tolerance) continue;
				if (Dist <= 1e-6f)
				{
					Normal = P - Pos[S.Particles[i].Parent];
					Normal = Normal.magnitudeSquared() > 1e-12f ? Normal.getNormalized() : PxVec3(0.f, 1.f, 0.f);
				}
				P += Normal * FMath::Min(Depth - Tolerance, MaxStep);
			}
			if ((P - Pos[i]).magnitudeSquared() > 0.f)
			{
				const PxVec3 Normal = (P - Pos[i]).getNormalized();
				PxVec3 Velocity = Vel[i];
				const float Into = Velocity.dot(Normal);
				if (Into < 0.f) Velocity -= Normal * Into;
				Pos[i] = P;
				Vel[i] = Velocity;
				++GHardSubstep;
			}
		}
	}

	void AfterSubstep(FSolveJob& J, float Alpha)
	{
		const int32 Count = int32(J.S->ParticleCount);
		if (!J.Shapes.Num() || Count < 2) return;
		TArray<PxVec3, TInlineAllocator<64>> Pos, Vel, Pos0, Vel0;
		Pos.SetNumUninitialized(Count); Vel.SetNumUninitialized(Count);
		for (int32 i = 0; i < Count; ++i) { Pos[i] = J.Chain->Bodies[i]->getGlobalPose().p; Vel[i] = J.Chain->Bodies[i]->getLinearVelocity(); }
		Pos0 = Pos; Vel0 = Vel;
		SubstepContact(J, Alpha, J.Substeps, Pos, Vel);
		for (int32 i = 1; i < Count; ++i)
		{
			if (Pos[i] != Pos0[i]) J.Chain->Bodies[i]->setGlobalPose(PxTransform(Pos[i]));
			if (Vel[i] != Vel0[i]) J.Chain->Bodies[i]->setLinearVelocity(Vel[i]);
		}
	}

	// Results back to Rust: exact projection, audits, snapshot. Unlocks the chain.
	void FinishJob(FSolveJob& J, bool bPhysX, bool bSafety = false)
	{
		FOKPxChainStep& S = *J.S;
		TSharedPtr<FChain>& Chain = J.Chain;
		float& RefStep = J.RefStep;
		float& Dt = J.Dt;
		PxVec3& Accel = J.Accel;
		TArray<FOKPxCapsule>& Shapes = J.Shapes;
		TArray<FOKPxCapsule>& AuditShapes = J.AuditShapes;
		TArray<FOKPxCapsule>& FromShapes = J.FromShapes;
		TArray<uint64>& ShapeKeys = J.ShapeKeys;
		TArray<uint64>& AuditKeys = J.AuditKeys;
		TArray<PxVec3, TInlineAllocator<64>>& AnimPos = J.AnimPos;
		TArray<FVector4f>& Others = J.Others;
		int32& Substeps = J.Substeps;
		PxVec3& RootTo = J.RootTo;
		PxVec3& RootFrom = J.RootFrom;
		(void)ShapeKeys; (void)Others; (void)FromShapes; (void)Substeps; (void)RootTo; (void)RootFrom; (void)Accel; (void)Dt;
		if (bPhysX) for (uint32 i = 0; i < S.ParticleCount; ++i)
		{
			FOKPxParticle& P = S.Particles[i];
			if (P.Parent < 0) continue;
			PxRigidDynamic* Body = Chain->Bodies[i];
			const PxVec3 Pos = Body->getGlobalPose().p;
			const PxVec3 Vel = Body->getLinearVelocity();
			const FOKPxParticle& ParentOut = S.Particles[P.Parent];
			if (!Pos.isFinite() || !Vel.isFinite() || (Pos - V(ParentOut.Pos)).magnitude() > P.RestLen * 3.f + 0.2f)
			{
				// This particle blew up: put only it back on its animated goal at rest (neighbours keep their motion).
				const PxVec3 Goal = V(ParentOut.Pos) + V(P.GoalOffset);
				Put(P.Pos, Goal);
				Put(P.Prev, Goal);
				++GResets;
				continue;
			}
			Put(P.Pos, Pos);
			Put(P.Prev, Pos - Vel * (RefStep / FMath::Max(S.VelocityScale, 1e-3f)));
		}
		if (!bPhysX && bSafety) for (uint32 i = 0; i < S.ParticleCount; ++i)
		{
			// Same blow-up guard for the other backends, on the positions they wrote back.
			FOKPxParticle& P = S.Particles[i];
			if (P.Parent < 0) continue;
			const PxVec3 Pos = V(P.Pos), Prev = V(P.Prev);
			const FOKPxParticle& ParentOut = S.Particles[P.Parent];
			if (Pos.isFinite() && Prev.isFinite() && (Pos - V(ParentOut.Pos)).magnitude() <= P.RestLen * 3.f + 0.2f) continue;
			const PxVec3 Goal = V(ParentOut.Pos) + V(P.GoalOffset);
			Put(P.Pos, Goal);
			Put(P.Prev, Goal);
			++GResets;
		}
		// Contact solving is soft (contact offsets, joint/contact conflicts, fast kinematic bodies), so finish with an
		// exact position pass like DynamicBone's own collider step: push every particle out of each collider, then
		// restore the bone length towards the parent. Collision is applied last so the result never ends inside a
		// body; the velocity is kept by shifting the previous position along.
		if ((bPhysX || bSafety) && Shapes.Num()) // the other backends: only with OKPhysXSecondary.AltSafetyNet
		{
			// Safety net only: errors within the tolerance are the soft constraints' business, larger ones are eased out
			// by at most MaxStep per frame (no visible jump), bone middles are shared by both ends of the bone.
			const int32 Passes = FMath::Max(1, CVarProjectionPasses.GetValueOnAnyThread());
			const float Tolerance = J.bProjectOnly ? 0.f : FMath::Max(CVarHardTolerance.GetValueOnAnyThread(), 0.f);
			const float MaxStep = J.bProjectOnly ? 1e9f : FMath::Max(CVarHardMaxStep.GetValueOnAnyThread(), 1e-4f);
			const int32 Count = int32(S.ParticleCount);
			TArray<PxVec3, TInlineAllocator<64>> Start, Velocity;
			Start.SetNumUninitialized(Count); Velocity.SetNumUninitialized(Count);
			// MaxStep limits how far a *contact point* moves per frame. A long bone touching near its root moves its
			// particle end by the lever 1/U for the same contact motion, so that particle's own limit grows with it.
			TArray<float, TInlineAllocator<64>> Lever;
			Lever.Init(1.f, Count);
			// The cap holds per frame, but a chain pinned to its animated pose (elasticity/stiffness 1, a tail's base) is put
			// back every step, so a contact deeper than one MaxStep was never cleared (a tail stood through the partner).
			// Last frame's correction is carried over (decaying, so the chain returns once the contact is gone).
			TArray<PxVec3, TInlineAllocator<64>> Simulated;
			Simulated.SetNumUninitialized(Count);
			for (int32 i = 0; i < Count; ++i) Simulated[i] = V(S.Particles[i].Pos);
			const float Memory = J.bProjectOnly ? 0.f : FMath::Clamp(CVarPushMemory.GetValueOnAnyThread(), 0.f, 0.99f);
			if (Chain->Push.Num() == Count && Memory > 0.f && !Chain->bStale)
			{
				for (int32 i = 1; i < Count; ++i)
				{
					const PxVec3 Carry = Chain->Push[i] * Memory;
					Put(S.Particles[i].Pos, V(S.Particles[i].Pos) + Carry);
					Put(S.Particles[i].Prev, V(S.Particles[i].Prev) + Carry);
				}
			}
			for (int32 i = 0; i < Count; ++i) { Start[i] = V(S.Particles[i].Pos); Velocity[i] = Start[i] - V(S.Particles[i].Prev); }
			auto Capped = [MaxStep](const PxVec3& Move) { const float L = Move.magnitude(); return L > MaxStep ? Move * (MaxStep / L) : Move; };
			for (int32 Pass = 0; Pass < Passes; ++Pass)
			{
				for (int32 i = 1; i < Count; ++i)
				{
					FOKPxParticle& P = S.Particles[i];
					PxVec3 Pos = V(P.Pos);
					const int32 ParentIndex = P.Parent;
					const bool bParentFree = ParentIndex > 0 && !(S.Particles[ParentIndex].Flags & 1u);
					PxVec3 ParentPos = V(S.Particles[ParentIndex].Pos);
					// Bone length: only beyond the soft spring's working stretch (10 %) and the tolerance.
					const float Rest = FMath::Max(P.RestLen, 1e-5f);
					const float Slack = CVarLengthStiffness.GetValueOnAnyThread() > 0.f ? FMath::Max(CVarLengthSlack.GetValueOnAnyThread(), 1.f) : 1.f;
					const float MinLen = Rest / FMath::Max(1.f - P.MinFraction, 0.05f) / Slack, MaxLen = Rest / FMath::Max(1.f - P.MaxFraction, 0.05f) * Slack;
					const PxVec3 Arm = Pos - ParentPos;
					const float Len = Arm.magnitude();
					const float Target = FMath::Clamp(Len, MinLen, MaxLen);
					if (Len > 1e-6f && FMath::Abs(Target - Len) > Tolerance)
					{
						const float Fix = (Target - Len) + (Target > Len ? -Tolerance : Tolerance);
						Pos += Capped(Arm * (Fix / Len));
						if (Pass == 0) ++GHardLength;
						if (Pass == 0 && DebugHardTake()) UE_LOG(LogTemp, Display, TEXT("HardFix length key=%llu i=%d/%u parent=%d kin=%d len=%.4f rest=%.4f min=%.4f max=%.4f minF=%.3f maxF=%.3f flags=%u"),
							S.Key, i, S.ParticleCount, ParentIndex, !bParentFree, Len, Rest, MinLen, MaxLen, P.MinFraction, P.MaxFraction, P.Flags);
					}
					// Particle against every collider.
					const float R = FMath::Max(P.Radius, 0.f);
					for (int32 k = 0; k < Shapes.Num(); ++k)
					{
						const FOKPxCapsule& C = Shapes[k];
						const PxVec3 A = V(C.A), AB = V(C.B) - A;
						const float L2 = AB.dot(AB);
						const float T = L2 > 1e-12f ? FMath::Clamp((Pos - A).dot(AB) / L2, 0.f, 1.f) : 0.f;
						PxVec3 Out = Pos - (A + AB * T);
						const float Dist = Out.magnitude();
						const float Depth = J.Rad(i, k) + R - Dist;
						if (Depth <= Tolerance) continue;
						if (Dist < 1e-6f) { Out = Pos - ParentPos; if (Out.magnitudeSquared() < 1e-12f) Out = PxVec3(0.f, 1.f, 0.f); }
						Pos += Capped(Out.getNormalized() * (Depth - Tolerance));
						if (Pass == 0) ++GHardPoint;
						if (Pass == 0 && DebugHardTake()) UE_LOG(LogTemp, Display, TEXT("HardFix point key=%llu i=%d/%u parent=%d kin=%d depth=%.4f R=%.4f colliderR=%.4f collider=%d"),
							S.Key, i, S.ParticleCount, ParentIndex, !bParentFree, Depth, R, C.Radius, int32(&C - Shapes.GetData()));
					}
					// The bone, at its point closest to each collider: both ends move by that point's correction; with a
					// kinematic parent the particle end carries it by the lever 1/U. The chain's own DynamicBone colliders
					// test particles only, as DynamicBone does (see the substep contacts).
					for (int32 k = J.AuthoredShapes; k < Shapes.Num(); ++k)
					{
						const FOKPxCapsule& C = Shapes[k];
						const PxVec3 A = V(C.A), AB = V(C.B) - A;
						const float L2 = AB.dot(AB);
						float U; PxVec3 OnBone, OnAxis;
						const float Dist = BoneClosest(ParentPos, Pos, A, V(C.B), U, OnBone, OnAxis);
						const PxVec3 Out = OnBone - OnAxis;
						const float Depth = J.Rad(i, k) + R - Dist;
						if (Depth <= Tolerance || Dist < 1e-6f || U <= 0.05f) continue;
						const PxVec3 Move = Capped(Out * ((Depth - Tolerance) / Dist));
						if (bParentFree) { Pos += Move; ParentPos += Move; }
						else
						{
							// Turn the bone about its fixed parent so that point clears the collider: moving the particle by the
							// lever alone would stretch the bone (a skirt panel dragged out along a hand).
							const float BoneLen = (Pos - ParentPos).magnitude();
							const PxVec3 Turned = Pos + Move / FMath::Max(U, 0.2f) - ParentPos;
							if (BoneLen > 1e-6f && Turned.magnitudeSquared() > 1e-12f) Pos = ParentPos + Turned.getNormalized() * BoneLen;
							Lever[i] = FMath::Max(Lever[i], 1.f / FMath::Max(U, 0.2f));
						}
						if (Pass == 0) ++GHardMid;
						if (Pass == 0 && DebugHardTake()) UE_LOG(LogTemp, Display, TEXT("HardFix mid key=%llu i=%d/%u parent=%d kin=%d depth=%.4f R=%.4f colliderR=%.4f collider=%d boneLen=%.4f parentDepth=%.4f"),
							S.Key, i, S.ParticleCount, ParentIndex, !bParentFree, Depth, R, C.Radius, int32(&C - Shapes.GetData()), (Pos - ParentPos).magnitude(),
							C.Radius + R - (ParentPos - (A + AB * (L2 > 1e-12f ? FMath::Clamp((ParentPos - A).dot(AB) / L2, 0.f, 1.f) : 0.f))).magnitude());
					}
					Put(P.Pos, Pos);
					if (bParentFree) Put(S.Particles[ParentIndex].Pos, ParentPos);
				}
			}
			for (int32 i = 1; i < Count; ++i)
			{
				FOKPxParticle& P = S.Particles[i];
				// The corrections of all passes together move a particle at most MaxStep this frame.
				const PxVec3 Total = V(P.Pos) - Start[i];
				const float Limit = MaxStep * Lever[i];
				PxVec3 Shift = Total.magnitude() > Limit ? Total * (Limit / Total.magnitude()) : Total;
				{
					// The cap is for contacts (no visible jump); a bone is never left stretched beyond the soft slack.
					const PxVec3 ParentNow = V(S.Particles[P.Parent].Pos);
					const float Rest = FMath::Max(P.RestLen, 1e-5f);
					const float Slack = CVarLengthStiffness.GetValueOnAnyThread() > 0.f ? FMath::Max(CVarLengthSlack.GetValueOnAnyThread(), 1.f) : 1.f;
					const float MinLen = Rest / FMath::Max(1.f - P.MinFraction, 0.05f) / Slack, MaxLen = Rest / FMath::Max(1.f - P.MaxFraction, 0.05f) * Slack;
					const PxVec3 Arm = Start[i] + Shift - ParentNow;
					const float Len = Arm.magnitude();
					if (Len > 1e-6f && (Len < MinLen || Len > MaxLen)) Shift = ParentNow + Arm * (FMath::Clamp(Len, MinLen, MaxLen) / Len) - Start[i];
				}
				if (Shift.magnitudeSquared() <= 0.f) { Put(P.Pos, Start[i]); continue; }
				Put(P.Pos, Start[i] + Shift);
				// Inelastic: keep the tangential velocity, drop the part that drives back in (or the particle re-enters
				// next frame and is pushed out again).
				PxVec3 Vel = Velocity[i];
				const PxVec3 Normal = Shift.getNormalized();
				const float Into = Vel.dot(Normal);
				if (Into < 0.f) Vel -= Normal * Into;
				Put(P.Prev, V(P.Pos) - Vel);
				AtomicMaxFloat(GMaxProjectionBits, Shift.magnitude());
				if (Shift.magnitude() > 1e-4f) ++GHardFixes;
			}
			Chain->Push.SetNumUninitialized(Count);
			for (int32 i = 0; i < Count; ++i) Chain->Push[i] = V(S.Particles[i].Pos) - Simulated[i];
		}
		else Chain->Push.Reset();
		const bool bAudit = CVarAudit.GetValueOnAnyThread() != 0;
		if (bAudit)
		{
			// Jitter: a particle's frame-to-frame acceleration that the animation itself does not have.
			// x(t) - 2x(t-1) + x(t-2), simulated minus animated; hair that "jumps" shows up here.
			if (Chain->HistoryFrames > 0 && Chain->SimHistory[0].Num() != int32(S.ParticleCount)) Chain->HistoryFrames = 0;
			if (Chain->HistoryFrames == 0)
			{
				for (int32 k = 0; k < 2; ++k) { Chain->SimHistory[k].SetNum(S.ParticleCount); Chain->AnimHistory[k].SetNum(S.ParticleCount); }
			}
			float ChainJerk = 0.f;
			bool bChainJitter = false;
			for (uint32 i = 1; i < S.ParticleCount; ++i)
			{
				const PxVec3 Sim = V(S.Particles[i].Pos);
				GSwingMicrons += int64((Sim - AnimPos[i]).magnitude() * 1e6f); ++GSwingSamples;
				// How far below the animated pose (skeleton -Y): what the weight setting changes.
				GDropMicrons += int64((AnimPos[i].y - Sim.y) * 1e6f);
				{
					const float Off = (Sim - AnimPos[i]).magnitude();
					int32 Old = GMaxSwingBits.load(); float OldF; FMemory::Memcpy(&OldF, &Old, 4);
					if (Off > OldF) { AtomicMaxFloat(GMaxSwingBits, Off); GMaxSwingWhere = (uint64(S.Key) << 16) | uint64(i); }
				}
				if (Chain->HistoryFrames >= 2)
				{
					const PxVec3 SimAcc = Sim - Chain->SimHistory[0][i] * 2.f + Chain->SimHistory[1][i];
					const PxVec3 AnimAcc = AnimPos[i] - Chain->AnimHistory[0][i] * 2.f + Chain->AnimHistory[1][i];
					const PxVec3 Excess = SimAcc - AnimAcc;
					ChainJerk = FMath::Max(ChainJerk, Excess.magnitude());
					// Jitter = the unexplained acceleration flips direction from one frame to the next while both are
					// large (a smooth lag or swing keeps its direction for several frames).
					if (Chain->LastExcess.Num() == int32(S.ParticleCount) && Excess.magnitude() > 0.01f && Chain->LastExcess[i].magnitude() > 0.01f
						&& Excess.dot(Chain->LastExcess[i]) < -0.5f * Excess.magnitude() * Chain->LastExcess[i].magnitude())
					{
						bChainJitter = true;
					}
					if (Chain->LastExcess.Num() != int32(S.ParticleCount)) Chain->LastExcess.Init(PxVec3(0.f), S.ParticleCount);
					Chain->LastExcess[i] = Excess;
				}
				Chain->SimHistory[1][i] = Chain->SimHistory[0][i]; Chain->SimHistory[0][i] = Sim;
				Chain->AnimHistory[1][i] = Chain->AnimHistory[0][i]; Chain->AnimHistory[0][i] = AnimPos[i];
			}
			Chain->HistoryFrames = FMath::Min(Chain->HistoryFrames + 1, 2);
			if (ChainJerk > 0.03f) ++GPops; // 3 cm of unexplained acceleration in one frame (lag or pop)
			if (bChainJitter) ++GJitter;
			{
				const int32 By = (J.Solver & 7) + ((S.Particles[0].Flags & 2u) ? 8 : 0);
				++GStepsBy[By];
				if (ChainJerk > 0.03f) ++GPopsBy[By];
				if (bChainJitter) ++GJitterBy[By];
			}
			if (S.Particles[0].Flags & 32u)
			{
				++GTailSteps; if (ChainJerk > 0.03f) ++GTailPops; if (bChainJitter) ++GTailJitter;
				for (uint32 i = 1; i < S.ParticleCount; ++i)
				{
					const int64 Off = int64((V(S.Particles[i].Pos) - AnimPos[i]).magnitude() * 1e6f);
					GTailSwingMicrons += Off; ++GTailSwingSamples;
					int64 Old = GTailMaxSwingMicrons.load(); while (Off > Old && !GTailMaxSwingMicrons.compare_exchange_weak(Old, Off)) {}
				}
			}
			AtomicMaxFloat(GMaxJerkBits, ChainJerk);
		}
		if (bAudit)
		{
			float Worst = 0.f;
			uint64 WorstKey = 0;
			for (uint32 i = 1; i < S.ParticleCount; ++i)
			{
				const PxVec3 P = V(S.Particles[i].Pos);
				const PxVec3 Mid = (P + V(S.Particles[S.Particles[i].Parent].Pos)) * 0.5f;
				if (S.Particles[i].RestLen > 1e-4f) AtomicMaxFloat(GMaxStretchBits, (P - V(S.Particles[S.Particles[i].Parent].Pos)).magnitude() / S.Particles[i].RestLen);
				for (int32 k = 0; k < AuditShapes.Num(); ++k)
				{
					const FOKPxCapsule& C = AuditShapes[k];
					const int32 Sk = k + (J.Shapes.Num() - AuditShapes.Num()); // authored chain colliders precede the bodies in Shapes only
					if (J.FullRadius.IsValidIndex(Sk) && !J.Attached[Sk] && !(S.Particles[0].Flags & 2u))
					{
						// True penetration: the full proxy, except the body the chain hangs from (hair roots sit in the head).
						const float Full = J.FullRadius[Sk] + FMath::Max(S.Particles[i].Radius, 0.f);
						const float TrueDepth = FMath::Max(Full - SegmentDistance(P, V(C.A), V(C.B)), Full - BoneDistance(V(S.Particles[S.Particles[i].Parent].Pos), P, V(C.A), V(C.B)));
						if (TrueDepth > 0.f) AtomicMaxFloat(GTrueDepthBits, TrueDepth);
						if (TrueDepth > 0.03f) ++GTrueViolations; // beyond proxy roughness
						if (S.Particles[0].Flags & 32u)
						{
							// Tails at their mesh thickness: what is seen going through a body.
							// Beyond the allowed squash: the solid part of the thickness (TailFluff).
							// A tail tapers: along a bone the thickness goes from the parent particle's to this one's (the root end
							// of the first bones is the narrow base the partner's hands hold, not the fluff of the next bone).
							const float Fluff = FMath::Clamp(CVarTailFluff.GetValueOnAnyThread(), 0.f, 1.f);
							const FOKPxParticle& Up = S.Particles[S.Particles[i].Parent];
							const float ThickHere = FMath::Max(S.Particles[i].BodyRadius * Fluff, FMath::Max(S.Particles[i].Radius, 0.f));
							const float ThickUp = (Up.Flags & 1u) ? 0.f : FMath::Max(Up.BodyRadius * Fluff, FMath::Max(Up.Radius, 0.f));
							const float Thick = J.FullRadius[Sk] + ThickHere;
							float BoneU; PxVec3 BoneAt, AxisAt;
							const float BoneGap = BoneClosest(V(Up.Pos), P, V(C.A), V(C.B), BoneU, BoneAt, AxisAt);
							const float Seen = FMath::Max(Thick - SegmentDistance(P, V(C.A), V(C.B)), J.FullRadius[Sk] + FMath::Lerp(ThickUp, ThickHere, BoneU) - BoneGap);
							if (Seen > 0.f) AtomicMaxFloat(GTailDepthBits, Seen);
							if (Seen > 0.02f)
							{
								++GTailDeep; GTailDeepByBody[FMath::Min<uint64>(AuditKeys[k], 127)]++;
								static std::atomic<int32> TailSamples{0};
								if ((TailSamples++ % 500) == 0)
								{
									float U; PxVec3 OnBone, OnAxis;
									const PxVec3 ParentP = V(S.Particles[S.Particles[i].Parent].Pos);
									const float BoneDist = BoneClosest(ParentP, P, V(C.A), V(C.B), U, OnBone, OnAxis);
									UE_LOG(LogOKPhysX, Display, TEXT("tail inside body=%llu seen=%.3f chain=%llu i=%u thick=%.3f body=%.3f solverR=%.3f dist=%.3f bone=%.3f u=%.2f parentDist=%.3f boneLen=%.3f anim=%.3f"), AuditKeys[k], Seen, S.Key, i, S.Particles[i].BodyRadius, J.FullRadius[Sk], J.Rad(int32(i), Sk), SegmentDistance(P, V(C.A), V(C.B)), BoneDist, U, SegmentDistance(ParentP, V(C.A), V(C.B)), (P - ParentP).magnitude(), SegmentDistance(AnimPos[i], V(C.A), V(C.B)));
								}
							}
						}
					}
					const float R = J.Rad(int32(i), Sk) + FMath::Max(S.Particles[i].Radius, 0.f);
					// Particle and bone middle use the same contact distance (collider + particle radius) as the clearance,
					// the soft layer and the exact pass.
					const float Depth = FMath::Max(R - SegmentDistance(P, V(C.A), V(C.B)), R - BoneDistance(V(S.Particles[S.Particles[i].Parent].Pos), P, V(C.A), V(C.B)));
					if (Depth > Worst) { Worst = Depth; WorstKey = AuditKeys[k]; }
					if (Depth > 0.01f && !(S.Particles[0].Flags & 2u))
					{
						GViolationsByBody[FMath::Min<uint64>(AuditKeys[k], 127)]++;
						static std::atomic<int32> Samples{0};
						if ((Samples++ % 400) == 0)
						{
							const PxVec3 Root = V(S.Particles[0].Pos);
							UE_LOG(LogOKPhysX, Display, TEXT("violation body=%llu depth=%.3f chain=%llu particles=%u i=%u parent=%d radius=%.3f pr=%.3f rest=%.3f dist=%.3f mid=%.3f root=%.3f anim=%.3f animMid=%.3f"),
								AuditKeys[k], Depth, S.Key, S.ParticleCount, i, S.Particles[i].Parent, C.Radius, S.Particles[i].Radius, S.Particles[i].RestLen,
								SegmentDistance(P, V(C.A), V(C.B)), SegmentDistance(Mid, V(C.A), V(C.B)), SegmentDistance(Root, V(C.A), V(C.B)),
								SegmentDistance(AnimPos[i], V(C.A), V(C.B)), SegmentDistance((AnimPos[i] + AnimPos[S.Particles[i].Parent]) * 0.5f, V(C.A), V(C.B)));
						}
					}
				}
			}
			++GSteps;
			if (S.Particles[0].Flags & 2u)
			{
				// Breasts/hips pressed between limbs and torso: soft tissue, reported on its own.
				if (Worst > 0.01f) ++GBodyViolations;
				AtomicMaxFloat(GBodyPenetrationBits, Worst);
			}
			else
			{
				if (Worst > 0.01f) ++GViolations;
				AtomicMaxFloat(GMaxPenetrationBits, Worst);
			}
		}
		if (S.Group)
		{
			TSharedPtr<FGroup> Group;
			{
				UE::TUniqueLock GroupGuard(GGroupLock);
				TSharedPtr<FGroup>& Slot = GGroups.FindOrAdd(S.Group);
				if (!Slot) Slot = MakeShared<FGroup>();
				Group = Slot;
			}
			UE::TUniqueLock GroupGuard(Group->Lock);
			FSnapshot& Snap = Group->Chains.FindOrAdd(S.Key);
			Snap.Spheres.Reset();
			for (uint32 i = 1; i < S.ParticleCount; ++i)
			{
				const FOKPxParticle& P = S.Particles[i];
				// Half radius, floored: strands rooted together overlap at rest and must not fight.
				Snap.Spheres.Add(FVector4f(P.Pos[0], P.Pos[1], P.Pos[2], FMath::Max(P.Radius * 0.5f, 0.004f)));
			}
		}
		Chain->Lock.Unlock();
	}

	// Backend 3 was Bullet, which this project does not ship. Its number stays reserved so old settings and console
	// lines do not pick a different solver; they get PhysX (0), with one warning.
	int32 OKSupportedBackend(int32 Backend)
	{
#if !OK_SECONDARY_COMPARISON_BACKENDS
		// Jolt (4) and MuJoCo (5) are comparison backends, not in Shipping builds.
		if (Backend == 4 || Backend == 5)
		{
			static bool bWarnedComparison = false;
			if (!bWarnedComparison) { bWarnedComparison = true; UE_LOG(LogTemp, Warning, TEXT("OKPhysXSecondary: backend %d is a comparison backend (not in this build); using 0 (PhysX)."), Backend); }
			return 0;
		}
#endif
		if (Backend != 3) return Backend;
		static bool bWarned = false;
		if (!bWarned) { bWarned = true; UE_LOG(LogTemp, Warning, TEXT("OKPhysXSecondary: backend 3 (Bullet) is not available; using 0 (PhysX).")); }
		return 0;
	}

	int32 SolveJobsWith(TArrayView<FSolveJob> Jobs, const TSharedPtr<FBatch>& Batch, uint64 BatchKey, int32 Backend);

	int32 SolveJobs(TArrayView<FSolveJob> Jobs, const TSharedPtr<FBatch>& Batch, uint64 BatchKey)
	{
		const int32 Forced = CVarBackend.GetValueOnAnyThread();
		{
			// A different solver (menu, console, MCP) starts every chain from its animated pose.
			static std::atomic<int32> GLastSolver{INT32_MIN};
			const int32 Chosen = CVarEnabled.GetValueOnAnyThread() != 0 ? Forced : -2;
			if (GLastSolver.exchange(Chosen) != Chosen) ++GResetEpoch;
		}
		// Backend -1: every chain names its own solver in root flag bits 8..11 (solver + 1; 0 = PhysX), chosen per component
		// by ok-extract (card setting or the component default). 0..6 force one solver on every chain (comparisons).
		// Tails (root flag 32): a stronger pull back to the pose and less damping, before any solver reads the particles.
		// Authored tails are very loose (elasticity ~0.09, stiffness ~0.1): they drag behind the hips; with a stiffer spring
		// and less damping they overshoot and spring back. The step is rebuilt by ok-extract every frame (no accumulation).
		// TailCompress: bones may shorten to this fraction (length limit Rest / (1 - MinFraction), every solver).
		{
			const float Bounce = FMath::Clamp(CVarTailBounce.GetValueOnAnyThread(), 0.f, 2.f);
			const float Pull = 1.f + 1.5f * Bounce, Damp = FMath::Max(1.f - 0.5f * Bounce, 0.05f);
			const float Compress = FMath::Clamp(CVarTailCompress.GetValueOnAnyThread(), 0.3f, 1.f), MinFraction = 1.f - 1.f / Compress;
			for (FSolveJob& J : Jobs)
			{
				if (!J.S->ParticleCount || !(J.S->Particles[0].Flags & 32u)) continue;
				for (uint32 i = 1; i < J.S->ParticleCount; ++i)
				{
					FOKPxParticle& P = J.S->Particles[i];
					P.Damping *= Damp; P.Elasticity = FMath::Min(P.Elasticity * Pull, 1.f);
					P.MinFraction = FMath::Min(P.MinFraction, MinFraction);
				}
			}
		}
		for (FSolveJob& J : Jobs)
		{
			const uint32 Code = J.S->ParticleCount ? (J.S->Particles[0].Flags >> 8) & 15u : 0u;
			const int32 Wanted = Forced >= 0 ? Forced : (Code ? int32(Code) - 1 : 0);
			// DynamicBone (6) brings its own exact collision pass: no settling solver after a pose switch.
			J.Solver = OKSupportedBackend(Settling() && Wanted != 6 ? CVarSettleBackend.GetValueOnAnyThread() : Wanted);
		}
		bool bMixed = false;
		for (const FSolveJob& J : Jobs) bMixed |= J.Solver != Jobs[0].Solver;
		if (!bMixed) return SolveJobsWith(Jobs, Batch, BatchKey, Jobs.Num() ? Jobs[0].Solver : 0);
		// One call per solver; the jobs keep their step pointers, so their order does not matter.
		Algo::StableSortBy(Jobs, &FSolveJob::Solver);
		int32 Done = 0;
		for (int32 Begin = 0; Begin < Jobs.Num();)
		{
			int32 End = Begin + 1;
			while (End < Jobs.Num() && Jobs[End].Solver == Jobs[Begin].Solver) ++End;
			Done += SolveJobsWith(Jobs.Slice(Begin, End - Begin), Batch, BatchKey, Jobs[Begin].Solver);
			Begin = End;
		}
		return Done;
	}

	int32 SolveJobsWith(TArrayView<FSolveJob> Jobs, const TSharedPtr<FBatch>& Batch, uint64 BatchKey, int32 Backend)
	{
		if (Backend >= 1 && Backend <= 6)
		{
			// KawaiiPhysics / Chaos / Jolt / MuJoCo / DynamicBone: same prepared inputs (colliders, goals, inertia), their own
			// solver, same audit.
			TArray<FOKBackendChain> Chains;
			for (FSolveJob& J : Jobs)
			{
				J.bProjectOnly = Backend == 6;
				J.bReady = PrepareJob(J, Batch, false);
				if (!J.bReady) continue;
				FOKBackendChain& C = Chains.AddDefaulted_GetRef();
				C.Step = J.S;
				C.Colliders = J.Shapes;
				C.FromColliders = J.FromShapes;
				C.Anim.Reserve(J.AnimPos.Num());
				for (const PxVec3& A : J.AnimPos) C.Anim.Add(FVector3f(A.x, A.y, A.z));
				C.Dt = J.Dt;
				C.RefStep = J.RefStep;
				C.Gravity = FVector3f(J.Accel.x, J.Accel.y, J.Accel.z);
				C.Substeps = FMath::Clamp(FMath::CeilToInt(J.Substeps * FMath::Max(CVarAltSubstepScale.GetValueOnAnyThread(), 0.1f)), 1, 32);
				// DynamicBone (6) collides in every step through this, as the game's DynamicBone does.
				if (Backend == 6 || CVarAltSubstepCollision.GetValueOnAnyThread() != 0)
				{
					FSolveJob* Job = &J;
					C.SubstepContact = [Job](float Alpha, int32 Substeps, FVector3f* X, FVector3f* Vel)
					{
						const int32 Count = int32(Job->S->ParticleCount);
						TArray<PxVec3, TInlineAllocator<64>> Pos, Vl;
						Pos.SetNumUninitialized(Count); Vl.SetNumUninitialized(Count);
						for (int32 i = 0; i < Count; ++i) { Pos[i] = PxVec3(X[i].X, X[i].Y, X[i].Z); Vl[i] = PxVec3(Vel[i].X, Vel[i].Y, Vel[i].Z); }
						SubstepContact(*Job, Alpha, Substeps, Pos, Vl);
						for (int32 i = 1; i < Count; ++i) { X[i] = FVector3f(Pos[i].x, Pos[i].y, Pos[i].z); Vel[i] = FVector3f(Vl[i].x, Vl[i].y, Vl[i].z); }
					};
				}
			}
			bool bAnyReset = false;
			for (FSolveJob& J : Jobs) if (J.bReady && J.bReset) { bAnyReset = true; OKBackendsRelease(J.S->Key); }
			if (bAnyReset) OKBackendsRelease(BatchKey); // their worlds are rebuilt from the animated pose
			if (Chains.Num())
			{
				if (Backend == 1) OKKawaiiStep(Chains);
				else if (Backend == 2) OKChaosStep(BatchKey, Chains);
				else if (Backend == 4) OKJoltStep(BatchKey, Chains);
				else if (Backend == 6) OKDynamicBoneStep(Chains);
				else if (!OKMuJoCoStep(BatchKey, Chains)) OKChaosStep(BatchKey, Chains); // mujoco.dll missing
			}
			int32 Done = 0;
			// DynamicBone has no collision of its own: it always gets the exact pass (body proxies, partner, push memory).
			const bool bSafety = Backend == 6 || CVarAltSafetyNet.GetValueOnAnyThread() != 0;
			for (FSolveJob& J : Jobs) if (J.bReady) { FinishJob(J, false, bSafety); ++Done; }
			return Done;
		}
		int32 Substeps = 1;
		float Dt = 0.f;
		uint64 Mark = FPlatformTime::Cycles64();
		auto Lap = [&Mark](int32 Phase) { const uint64 Now = FPlatformTime::Cycles64(); GPhaseCycles[Phase] += int64(Now - Mark); Mark = Now; };
		for (FSolveJob& J : Jobs)
		{
			J.bReady = PrepareJob(J, Batch, true);
			if (J.bReady) { Substeps = FMath::Max(Substeps, J.Substeps); Dt = FMath::Max(Dt, J.Dt); }
		}
		if (Dt > 0.f)
		{
			const float Sub = Dt / Substeps;
			// Chains with their own scene (no batch) are stepped one by one; a batch shares one simulate().
			TArray<PxScene*, TInlineAllocator<8>> Scenes;
			for (FSolveJob& J : Jobs) if (J.bReady) Scenes.AddUnique(J.Chain->Scene);
			for (int32 Step = 0; Step < Substeps; ++Step)
			{
				const float Alpha = float(Step + 1) / float(Substeps);
				Lap(0);
				for (FSolveJob& J : Jobs) if (J.bReady) BeforeSubstep(J, Alpha);
				for (PxScene* Scene : Scenes) { Scene->simulate(Sub); Scene->fetchResults(true); }
				Lap(1);
				for (FSolveJob& J : Jobs) if (J.bReady) AfterSubstep(J, Alpha);
				Lap(2);
			}
		}
		Lap(0);
		int32 Solved = 0;
		for (FSolveJob& J : Jobs) if (J.bReady) { FinishJob(J, true); ++Solved; }
		Lap(3);
		return Solved;
	}

	int32 Solve(const void* StepPtr)
	{
		const FSolveTimer Timer;
		if (!StepPtr) return IsEnabled() ? 1 : 0; // null: "is PhysX on?" (Rust then never uses its own solver)
		if (!IsEnabled()) return 0;
		FSolveJob J;
		J.S = static_cast<FOKPxChainStep*>(const_cast<void*>(StepPtr));
		return SolveJobs(MakeArrayView(&J, 1), nullptr, J.S->Key);
	}

	int32 SolveBatch(uint64 BatchKey, void* Steps, uint32 Count)
	{
		const FSolveTimer Timer;
		if (!Steps || Count == 0) return 0;
		FOKPxChainStep* First = static_cast<FOKPxChainStep*>(Steps);
		if (First->ParticleCount > 0 && (First->Particles[0].Flags & 4u))
		{
			// The built-in solver already moved these chains: measure them with the same audit, change nothing.
			if (!CVarAudit.GetValueOnAnyThread()) return 0;
			for (uint32 i = 0; i < Count; ++i)
			{
				FSolveJob J;
				J.S = First + i;
				J.bAuditOnly = true;
				if (PrepareJob(J, nullptr, false)) FinishJob(J, false);
			}
			return 1;
		}
		if (!IsEnabled()) return 0;
		TSharedPtr<FBatch> Batch;
		{
			UE::TUniqueLock Guard(GBatchLock);
			TSharedPtr<FBatch>& Slot = GBatches.FindOrAdd(BatchKey);
			if (!Slot) { Slot = MakeShared<FBatch>(); Slot->Scene = CreateChainScene(); }
			Batch = Slot;
		}
		UE::TUniqueLock Guard(Batch->Lock);
		TArray<FSolveJob> Jobs;
		Jobs.SetNum(Count);
		for (uint32 i = 0; i < Count; ++i) Jobs[i].S = static_cast<FOKPxChainStep*>(Steps) + i;
		// No fallback: a chain whose input cannot be prepared is left untouched (it holds still), the others are solved.
		const int32 Solved = SolveJobs(Jobs, Batch, BatchKey);
		if (Solved != int32(Count))
		{
			static std::atomic<int32> Holds{0};
			if ((Holds++ % 600) == 0) UE_LOG(LogOKPhysX, Warning, TEXT("batch %llu: %u of %u chains could not be prepared and hold still"), BatchKey, Count - uint32(Solved), Count);
		}
		return 1;
	}

}

class FOKPhysXSecondaryModule : public IModuleInterface
{
public:
	virtual void ShutdownModule() override
	{
		{
			UE::TUniqueLock Guard(GMapLock);
			GChains.Empty();
		}
		{
			UE::TUniqueLock Guard(GRagdollLock);
			GRagdolls.Empty();
		}
		if (GDispatcher) { GDispatcher->release(); GDispatcher = nullptr; }
		if (GPhysics) { PxCloseExtensions(); GPhysics->release(); GPhysics = nullptr; }
		if (GFoundation) { GFoundation->release(); GFoundation = nullptr; }
		for (void*& Handle : GDllHandles) { if (Handle) { FPlatformProcess::FreeDllHandle(Handle); Handle = nullptr; } }
	}
};

IMPLEMENT_MODULE(FOKPhysXSecondaryModule, OKPhysXSecondary)
