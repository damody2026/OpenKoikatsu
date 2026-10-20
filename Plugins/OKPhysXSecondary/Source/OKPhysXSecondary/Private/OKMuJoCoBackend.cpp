#include "OKSecondaryDrive.h"
#include "Async/Mutex.h"
#include "Async/UniqueLock.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

// Comparison backend: compiled only when OK_SECONDARY_COMPARISON_BACKENDS (not Shipping, OKPhysXSecondary.Build.cs);
// otherwise the stubs at the end keep the dispatch working (backends 4/5 then run PhysX, OKSupportedBackend).
#if OK_SECONDARY_COMPARISON_BACKENDS
// MuJoCo 3.15.0 (Apache-2.0, Google DeepMind), mujoco.dll staged by Tools/build_physics_backends.lua (delay-loaded).
THIRD_PARTY_INCLUDES_START
#pragma push_macro("check")
#undef check
#include <mujoco/mujoco.h>
#pragma pop_macro("check")
THIRD_PARTY_INCLUDES_END

static TAutoConsoleVariable<float> CVarMuJoCoTimeConst(
	TEXT("OKPhysXSecondary.MuJoCoTimeConst"), 0.01f, TEXT("solref time constant (s) of the MuJoCo contacts (MuJoCo clamps it to 2 timesteps)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarMuJoCoTendonTimeConst(
	TEXT("OKPhysXSecondary.MuJoCoTendonTimeConst"), 0.01f, TEXT("solref time constant (s) of the MuJoCo bone-length tendon limits."), ECVF_Default);
static TAutoConsoleVariable<float> CVarMuJoCoDampRatio(
	TEXT("OKPhysXSecondary.MuJoCoDampRatio"), 1.f, TEXT("solref damping ratio of the MuJoCo contacts and tendon limits."), ECVF_Default);
static TAutoConsoleVariable<float> CVarMuJoCoSlack(
	TEXT("OKPhysXSecondary.MuJoCoSlack"), 0.001f, TEXT("Bone length may vary by this fraction around the rest length before the tendon limit acts."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarMuJoCoIterations(
	TEXT("OKPhysXSecondary.MuJoCoIterations"), 100, TEXT("Constraint solver iterations (mjOption.iterations)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarMuJoCoImpRatio(
	TEXT("OKPhysXSecondary.MuJoCoImpRatio"), 1.f, TEXT("Frictional-to-normal constraint impedance ratio (mjOption.impratio)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarMuJoCoFriction(
	TEXT("OKPhysXSecondary.MuJoCoFriction"), 0.2f, TEXT("Sliding friction of the MuJoCo particles against the body capsules."), ECVF_Default);
static TAutoConsoleVariable<float> CVarMuJoCoMargin(
	TEXT("OKPhysXSecondary.MuJoCoMargin"), 0.f, TEXT("Contact margin (m) of the MuJoCo particles: contacts become active this far before touching."), ECVF_Default);

// Every chain is one mjModel built with mjSpec: each simulated particle a 1 kg body with three world-axis slide joints
// (qpos = world position) and a sphere geom; roots are mocap bodies; each bone a spatial tendon limited to its rest
// length; the colliders are mocap capsules resized in place (geom_size/rbound/aabb) every frame. Contacts only between
// particles (contype 1) and colliders (conaffinity 1). Goal spring, damping and gravity come from FOKParticleDrive as a
// velocity change before every mj_step (MuJoCo's gravity is off).
namespace
{
	FORCEINLINE FVector3f F3(const float* P) { return FVector3f(P[0], P[1], P[2]); }
	FORCEINLINE void Put3(float* Out, const FVector3f& V) { Out[0] = V.X; Out[1] = V.Y; Out[2] = V.Z; }

	UE::FMutex GMuJoCoInitLock;
	int32 GMuJoCoState = 0; // 0 untried, 1 ready, -1 failed
	bool EnsureMuJoCo()
	{
		UE::TUniqueLock Guard(GMuJoCoInitLock);
		if (GMuJoCoState) return GMuJoCoState > 0;
		GMuJoCoState = -1;
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OKPhysXSecondary"));
		if (!Plugin) return false;
		const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries/ThirdParty/Win64/mujoco.dll"));
		if (!FPlatformProcess::GetDllHandle(*Path))
		{
			UE_LOG(LogTemp, Error, TEXT("OKPhysXSecondary: failed to load %s"), *Path);
			return false;
		}
		UE_LOG(LogTemp, Display, TEXT("OKPhysXSecondary: MuJoCo %hs"), mj_versionString());
		GMuJoCoState = 1;
		return true;
	}

	struct FMjChain
	{
		mjModel* Model = nullptr;
		mjData* Data = nullptr;
		TArray<int32> QposAdr;   // per particle: first of its 3 slide joints (roots: -1)
		TArray<int32> DofAdr;
		TArray<int32> Mocap;     // per particle: mocap index of a root (others: -1)
		TArray<int32> ColliderGeom, ColliderMocap;
		TArray<int32> ParticleGeom; // per particle (roots: -1)
		TArray<float> ParticleRadius;
		TArray<int32> TendonId;     // per particle: its bone-length tendon (roots: -1)
		TArray<float> Rest;
		FVector3f LastRoot = FVector3f::ZeroVector;

		~FMjChain()
		{
			if (Data) mj_deleteData(Data);
			if (Model) mj_deleteModel(Model);
		}

		bool Build(const FOKBackendChain& C, int32 ColliderCapacity, float TimeConst)
		{
			const FOKPxChainStep& S = *C.Step;
			const int32 Count = int32(S.ParticleCount);
			mjSpec* Spec = mj_makeSpec();
			Spec->option.gravity[0] = Spec->option.gravity[1] = Spec->option.gravity[2] = 0;
			Spec->option.timestep = C.Dt / FMath::Max(C.Substeps, 1);
			mjsBody* World = mjs_findBody(Spec, "world");
			TArray<mjsBody*> Bodies; TArray<mjsJoint*> FirstJoint; TArray<mjsGeom*> Geoms; TArray<mjsTendon*> Tendons;
			Geoms.SetNumZeroed(Count); Tendons.SetNumZeroed(Count); Rest.Init(0.f, Count); ParticleRadius.Init(0.f, Count);
			Bodies.SetNumZeroed(Count); FirstJoint.SetNumZeroed(Count);
			const mjtNum Ref[2] = {TimeConst, 1.};
			for (int32 i = 0; i < Count; ++i)
			{
				const FOKPxParticle& P = S.Particles[i];
				mjsBody* Body = mjs_addBody(World, nullptr);
				Bodies[i] = Body;
				char Name[32];
				FCStringAnsi::Snprintf(Name, sizeof(Name), "p%d", i);
				mjsSite* Site = mjs_addSite(Body, nullptr);
				mjs_setName(Site->element, Name);
				if (P.Parent < 0)
				{
					Body->mocap = 1;
					Body->pos[0] = C.Anim[i].X; Body->pos[1] = C.Anim[i].Y; Body->pos[2] = C.Anim[i].Z;
					continue;
				}
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					mjsJoint* Joint = mjs_addJoint(Body, nullptr);
					Joint->type = mjJNT_SLIDE;
					Joint->axis[0] = Axis == 0; Joint->axis[1] = Axis == 1; Joint->axis[2] = Axis == 2;
					if (Axis == 0) FirstJoint[i] = Joint;
				}
				mjsGeom* Geom = mjs_addGeom(Body, nullptr);
				Geom->type = mjGEOM_SPHERE;
				Geom->size[0] = FMath::Max(P.Radius, 1e-4f);
				Geoms[i] = Geom;
				ParticleRadius[i] = FMath::Max(P.Radius, 1e-4f);
				Geom->mass = 1.;
				Geom->contype = 1; Geom->conaffinity = 0;
				Geom->friction[0] = 0.2;
				Geom->solref[0] = Ref[0]; Geom->solref[1] = Ref[1];
			}
			for (int32 i = 0; i < Count; ++i)
			{
				const FOKPxParticle& P = S.Particles[i];
				if (P.Parent < 0) continue;
				// MuJoCo rejects an empty or degenerate range: floor the length (coincident particles, zero rest length).
				const float Length = FMath::Max(P.RestLen > 1e-5f ? P.RestLen : FVector3f::Distance(C.Anim[P.Parent], C.Anim[i]), 1e-3f);
				Rest[i] = Length;
				mjsTendon* Tendon = mjs_addTendon(Spec, nullptr);
				Tendons[i] = Tendon;
				char A[32], B[32];
				FCStringAnsi::Snprintf(A, sizeof(A), "p%d", P.Parent);
				FCStringAnsi::Snprintf(B, sizeof(B), "p%d", i);
				mjs_wrapSite(Tendon, A);
				mjs_wrapSite(Tendon, B);
				Tendon->limited = mjLIMITED_TRUE;
				Tendon->range[0] = Length * 0.999; Tendon->range[1] = Length * 1.001; // set every frame (MuJoCoSlack)
				Tendon->solref_limit[0] = Ref[0]; Tendon->solref_limit[1] = Ref[1];
			}
			TArray<mjsGeom*> Colliders;
			for (int32 k = 0; k < ColliderCapacity; ++k)
			{
				mjsBody* Body = mjs_addBody(World, nullptr);
				Body->mocap = 1;
				Body->pos[0] = Body->pos[1] = Body->pos[2] = 1e5;
				mjsGeom* Geom = mjs_addGeom(Body, nullptr);
				Geom->type = mjGEOM_CAPSULE; // axis = local Z
				Geom->size[0] = 0.01; Geom->size[1] = 0.01;
				Geom->contype = 0; Geom->conaffinity = 1;
				Geom->friction[0] = 0.2;
				Geom->solref[0] = Ref[0]; Geom->solref[1] = Ref[1];
				Colliders.Add(Geom);
			}
			Model = mj_compile(Spec, nullptr);
			if (!Model)
			{
				UE_LOG(LogTemp, Error, TEXT("OKPhysXSecondary: MuJoCo compile failed: %hs"), mjs_getError(Spec));
				mj_deleteSpec(Spec);
				return false;
			}
			QposAdr.Init(-1, Count); DofAdr.Init(-1, Count); Mocap.Init(-1, Count); ParticleGeom.Init(-1, Count); TendonId.Init(-1, Count);
			for (int32 i = 0; i < Count; ++i)
			{
				if (Geoms[i]) ParticleGeom[i] = mjs_getId(Geoms[i]->element);
				if (Tendons[i]) TendonId[i] = mjs_getId(Tendons[i]->element);
			}
			for (int32 i = 0; i < Count; ++i)
			{
				if (FirstJoint[i])
				{
					const int32 Joint = mjs_getId(FirstJoint[i]->element);
					QposAdr[i] = Model->jnt_qposadr[Joint];
					DofAdr[i] = Model->jnt_dofadr[Joint];
				}
				else Mocap[i] = Model->body_mocapid[mjs_getId(Bodies[i]->element)];
			}
			for (mjsGeom* Geom : Colliders)
			{
				const int32 Id = mjs_getId(Geom->element);
				ColliderGeom.Add(Id);
				ColliderMocap.Add(Model->body_mocapid[Model->geom_bodyid[Id]]);
			}
			mj_deleteSpec(Spec);
			Data = mj_makeData(Model);
			LastRoot = C.Anim[0];
			return true;
		}

		void SetCollider(int32 k, const FOKPxCapsule& Cap, const FOKPxCapsule& From, float Alpha, bool bShape)
		{
			const FVector3f A = FMath::Lerp(F3(From.A), F3(Cap.A), Alpha), B = FMath::Lerp(F3(From.B), F3(Cap.B), Alpha);
			const float Length = (B - A).Size();
			const FQuat4f Q = Length > 1e-4f ? FQuat4f::FindBetweenNormals(FVector3f(0.f, 0.f, 1.f), (B - A) / Length) : FQuat4f::Identity;
			const FVector3f Center = (A + B) * 0.5f;
			mjtNum* Pos = Data->mocap_pos + 3 * ColliderMocap[k];
			mjtNum* Quat = Data->mocap_quat + 4 * ColliderMocap[k];
			Pos[0] = Center.X; Pos[1] = Center.Y; Pos[2] = Center.Z;
			Quat[0] = Q.W; Quat[1] = Q.X; Quat[2] = Q.Y; Quat[3] = Q.Z;
			if (!bShape) return;
			const int32 g = ColliderGeom[k];
			const mjtNum R = FMath::Max(Cap.Radius, 1e-4f), H = FMath::Max((F3(Cap.B) - F3(Cap.A)).Size() * 0.5f, 1e-5f);
			Model->geom_size[3 * g] = R;
			Model->geom_size[3 * g + 1] = H;
			Model->geom_rbound[g] = R + H;
			mjtNum* Box = Model->geom_aabb + 6 * g;
			Box[0] = Box[1] = Box[2] = 0;
			Box[3] = R; Box[4] = R; Box[5] = R + H;
		}

		/** The tunables that live in mjModel: applied every frame, no rebuild. */
		void ApplySettings()
		{
			const mjtNum Contact[2] = {FMath::Max(CVarMuJoCoTimeConst.GetValueOnAnyThread(), 1e-4f), FMath::Max(CVarMuJoCoDampRatio.GetValueOnAnyThread(), 0.01f)};
			const mjtNum Length[2] = {FMath::Max(CVarMuJoCoTendonTimeConst.GetValueOnAnyThread(), 1e-4f), Contact[1]};
			const float Slack = FMath::Clamp(CVarMuJoCoSlack.GetValueOnAnyThread(), 0.f, 0.5f);
			const float RadiusScale = OKAltRadiusScale();
			const mjtNum Friction = FMath::Max(CVarMuJoCoFriction.GetValueOnAnyThread(), 0.f), Margin = FMath::Max(CVarMuJoCoMargin.GetValueOnAnyThread(), 0.f);
			Model->opt.iterations = FMath::Clamp(CVarMuJoCoIterations.GetValueOnAnyThread(), 1, 1000);
			Model->opt.impratio = FMath::Max(CVarMuJoCoImpRatio.GetValueOnAnyThread(), 0.01f);
			for (int32 i = 0; i < ParticleGeom.Num(); ++i)
			{
				if (const int32 g = ParticleGeom[i]; g >= 0)
				{
					Model->geom_size[3 * g] = Model->geom_rbound[g] = ParticleRadius[i] * RadiusScale;
					mjtNum* Box = Model->geom_aabb + 6 * g;
					Box[3] = Box[4] = Box[5] = Model->geom_rbound[g];
					Model->geom_solref[2 * g] = Contact[0]; Model->geom_solref[2 * g + 1] = Contact[1];
					Model->geom_friction[3 * g] = Friction;
					Model->geom_margin[g] = Margin;
				}
				if (const int32 t = TendonId[i]; t >= 0)
				{
					Model->tendon_range[2 * t] = Rest[i] * (1.f - Slack);
					Model->tendon_range[2 * t + 1] = Rest[i] * (1.f + Slack) + 1e-6f;
					Model->tendon_solref_lim[2 * t] = Length[0]; Model->tendon_solref_lim[2 * t + 1] = Length[1];
				}
			}
			for (const int32 g : ColliderGeom) { Model->geom_solref[2 * g] = Contact[0]; Model->geom_solref[2 * g + 1] = Contact[1]; Model->geom_friction[3 * g] = Friction; }
		}

		void Park(int32 k)
		{
			mjtNum* Pos = Data->mocap_pos + 3 * ColliderMocap[k];
			Pos[0] = Pos[1] = Pos[2] = 1e5;
		}
	};

	struct FMjBatch
	{
		UE::FMutex Lock;
		TArray<TUniquePtr<FMjChain>> Chains;
		TArray<int32> Signature;
		bool bFresh = true;
		double LastStep = 0.;

		void Step(TArrayView<FOKBackendChain> In)
		{
			TArray<int32> Sig;
			for (const FOKBackendChain& C : In)
			{
				Sig.Add(int32(C.Step->ParticleCount));
				for (uint32 i = 0; i < C.Step->ParticleCount; ++i) Sig.Add(C.Step->Particles[i].Parent);
			}
			if (Sig != Signature)
			{
				Chains.Reset();
				Chains.SetNum(In.Num());
				Signature = MoveTemp(Sig);
				bFresh = true;
			}
			const float TimeConst = FMath::Max(CVarMuJoCoTimeConst.GetValueOnAnyThread(), 1e-4f);
			const double Now = FPlatformTime::Seconds();
			if (LastStep > 0. && Now - LastStep > 0.25) bFresh = true;
			LastStep = Now;

			float Dt = 0.f;
			int32 Substeps = 1;
			for (const FOKBackendChain& C : In) { Dt = FMath::Max(Dt, C.Dt); Substeps = FMath::Max(Substeps, C.Substeps); }
			if (Dt <= 0.f) return;
			const float Sub = Dt / Substeps;

			TArray<FVector3f> X, Vel;
			for (int32 c = 0; c < In.Num(); ++c)
			{
				FOKBackendChain& C = In[c];
				FOKPxChainStep& S = *C.Step;
				TUniquePtr<FMjChain>& Chain = Chains[c];
				// More colliders than the model has: rebuild with room to spare.
				if (Chain && Chain->ColliderGeom.Num() < C.Colliders.Num()) Chain.Reset();
				if (!Chain)
				{
					Chain = MakeUnique<FMjChain>();
					if (!Chain->Build(C, FMath::Max(8, C.Colliders.Num() + 4), TimeConst)) { Chain.Reset(); continue; }
				}
				mjModel* M = Chain->Model;
				mjData* D = Chain->Data;
				M->opt.timestep = Sub;
				Chain->ApplySettings();
				FOKParticleDrive Drive;
				Drive.Init(C);
				const int32 Count = int32(S.ParticleCount);
				const FVector3f RootTo = F3(S.Particles[0].Pos);
				const FVector3f RootFrom = OKRootFrom(Chain->LastRoot, RootTo, bFresh);
				for (int32 i = 0; i < Count; ++i)
				{
					const FOKPxParticle& P = S.Particles[i];
					if (Chain->QposAdr[i] < 0) continue;
					const FVector3f V = (F3(P.Pos) - F3(P.Prev)) * (S.VelocityScale / C.RefStep);
					for (int32 a = 0; a < 3; ++a) { D->qpos[Chain->QposAdr[i] + a] = P.Pos[a]; D->qvel[Chain->DofAdr[i] + a] = V[a]; }
				}
				for (int32 k = 0; k < Chain->ColliderGeom.Num(); ++k)
				{
					if (k < C.Colliders.Num()) Chain->SetCollider(k, C.Colliders[k], C.FromColliders[k], 0.f, true);
					else Chain->Park(k);
				}
				X.SetNumUninitialized(Count);
				Vel.SetNumUninitialized(Count);
				for (int32 Step = 0; Step < Substeps; ++Step)
				{
					const float A0 = float(Step) / Substeps, A1 = float(Step + 1) / Substeps;
					const FVector3f R0 = FMath::Lerp(RootFrom, RootTo, A0), R1 = FMath::Lerp(RootFrom, RootTo, A1);
					for (int32 i = 0; i < Count; ++i)
					{
						if (Chain->QposAdr[i] < 0)
						{
							X[i] = R0; Vel[i] = (R1 - R0) / Sub;
							mjtNum* Pos = D->mocap_pos + 3 * Chain->Mocap[i];
							Pos[0] = R1.X; Pos[1] = R1.Y; Pos[2] = R1.Z;
							continue;
						}
						for (int32 a = 0; a < 3; ++a) { X[i][a] = float(D->qpos[Chain->QposAdr[i] + a]); Vel[i][a] = float(D->qvel[Chain->DofAdr[i] + a]); }
					}
					for (int32 i = 0; i < Count; ++i)
					{
						if (Chain->QposAdr[i] < 0) continue;
						const FVector3f V = Drive.Velocity(i, X.GetData(), Vel.GetData(), Sub);
						for (int32 a = 0; a < 3; ++a) D->qvel[Chain->DofAdr[i] + a] = V[a];
					}
					for (int32 k = 0; k < C.Colliders.Num(); ++k) Chain->SetCollider(k, C.Colliders[k], C.FromColliders[k], A1, false);
					mj_step(M, D);
					if (C.SubstepContact)
					{
						for (int32 i = 0; i < Count; ++i)
						{
							if (Chain->QposAdr[i] < 0) { X[i] = R1; Vel[i] = (R1 - R0) / Sub; continue; }
							for (int32 a = 0; a < 3; ++a) { X[i][a] = float(D->qpos[Chain->QposAdr[i] + a]); Vel[i][a] = float(D->qvel[Chain->DofAdr[i] + a]); }
						}
						C.SubstepContact(A1, Substeps, X.GetData(), Vel.GetData());
						for (int32 i = 0; i < Count; ++i)
						{
							if (Chain->QposAdr[i] < 0) continue;
							for (int32 a = 0; a < 3; ++a) { D->qpos[Chain->QposAdr[i] + a] = X[i][a]; D->qvel[Chain->DofAdr[i] + a] = Vel[i][a]; }
						}
					}
				}
				const float Back = C.RefStep / FMath::Max(S.VelocityScale, 1e-3f);
				for (int32 i = 0; i < Count; ++i)
				{
					if (Chain->QposAdr[i] < 0) continue;
					FVector3f P, V;
					for (int32 a = 0; a < 3; ++a) { P[a] = float(D->qpos[Chain->QposAdr[i] + a]); V[a] = float(D->qvel[Chain->DofAdr[i] + a]); }
					Put3(S.Particles[i].Pos, P);
					Put3(S.Particles[i].Prev, P - V * Back);
				}
				Chain->LastRoot = RootTo;
			}
			bFresh = false;
		}
	};

	UE::FMutex GMuJoCoLock;
	TMap<uint64, TSharedPtr<FMjBatch>> GMuJoCoBatches;
}

bool OKMuJoCoStep(uint64 BatchKey, TArrayView<FOKBackendChain> Chains)
{
	if (!EnsureMuJoCo()) return false;
	TSharedPtr<FMjBatch> Batch;
	{
		UE::TUniqueLock Guard(GMuJoCoLock);
		TSharedPtr<FMjBatch>& Slot = GMuJoCoBatches.FindOrAdd(BatchKey);
		if (!Slot) Slot = MakeShared<FMjBatch>();
		Batch = Slot;
	}
	UE::TUniqueLock Guard(Batch->Lock);
	Batch->Step(Chains);
	return true;
}

void OKMuJoCoRelease(uint64 Key)
{
	UE::TUniqueLock Guard(GMuJoCoLock);
	GMuJoCoBatches.Remove(Key); // a step still running keeps its own reference
}
#else
bool OKMuJoCoStep(uint64, TArrayView<FOKBackendChain>) { return false; }
void OKMuJoCoRelease(uint64) {}
#endif
