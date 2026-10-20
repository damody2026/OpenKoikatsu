#include "KKSkeletalBuilder.h"
#include "KKRuntimeSkinBounds.h"
#include "KKRustExtract.h"
#include "ok_extract.h"

#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/MorphTarget.h"
#include "GPUSkinPublicDefs.h"
#include "Misc/CoreMisc.h"
#include "Misc/SecureHash.h"
#include "RenderingThread.h"
#include "StaticMeshResources.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/AnimDataModel.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "Animation/Skeleton.h"
#include "BoneIndices.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ok_raw_texture.h"
#include "Modules/ModuleManager.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
#include <initializer_list>

void OKEnsureSkeletalMaterialUsage(UMaterialInterface* Material)
{
	UMaterial* Mat = Material ? Material->GetMaterial() : nullptr;
	if (!Mat)
	{
		return;
	}
	Mat->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
	Mat->SetUsageByFlag(MATUSAGE_StaticMesh, true);
	Mat->SetUsageByFlag(MATUSAGE_MorphTargets, true);
}

static UMaterial* OKSkelUsageMaterial()
{
#if WITH_EDITOR

	UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!Mat)
	{
		return nullptr;
	}
	Mat->MaterialDomain = MD_Surface;
	Mat->BlendMode = BLEND_Opaque;
	Mat->SetShadingModel(MSM_Unlit);
	Mat->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
	Mat->SetUsageByFlag(MATUSAGE_StaticMesh, true);
	Mat->SetMaterialUsage(MATUSAGE_SkeletalMesh);
	Mat->SetMaterialUsage(MATUSAGE_StaticMesh);
	UMaterialExpressionConstant3Vector* C = NewObject<UMaterialExpressionConstant3Vector>(Mat);
	C->Material = Mat;
	C->Constant = FLinearColor(0.5f, 0.5f, 0.5f);
	Mat->GetExpressionCollection().AddExpression(C);
	if (UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData())
	{
		Ed->EmissiveColor.Expression = C;
	}
	Mat->UpdateCachedExpressionData();
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	Mat->ForceRecompileForRendering();
	OKEnsureSkeletalMaterialUsage(Mat);
	Mat->AddToRoot();
	return Mat;

#else
return UMaterial::GetDefaultMaterial(MD_Surface);
#endif
}

static void OKAssignSkelUsageMaterial(USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		return;
	}
	static UMaterial* Shared = nullptr;
	if (!Shared)
	{
		Shared = OKSkelUsageMaterial();
	}
	if (!Shared)
	{
		return;
	}
	OKEnsureSkeletalMaterialUsage(Shared);
	TArray<FSkeletalMaterial> Mats;
	Mats.Add(FSkeletalMaterial(Shared));
	Mesh->SetMaterials(Mats);
}

static bool GSkelEnvRead = false;
static bool GSkelEnabled = false;
static bool GLiveGpu = false;
uint8 OKCaptureSkinEnvironmentState(){return (GSkelEnvRead?1:0)|(GSkelEnabled?2:0)|(GLiveGpu?4:0);}
void OKRestoreSkinEnvironmentState(uint8 State){GSkelEnvRead=(State&1)!=0;GSkelEnabled=(State&2)!=0;GLiveGpu=(State&4)!=0;}

bool OKSkelMeshEnabled()
{
	if (!GSkelEnvRead)
	{
		GSkelEnvRead = true;
		GSkelEnabled = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SKELMESH")) == TEXT("1");
	}
	return GSkelEnabled || GLiveGpu;
}

bool OKLiveGpuSkinRequested()
{
	return FPlatformMisc::GetEnvironmentVariable(TEXT("OK_CPU_SKIN")) != TEXT("1");
}

void OKPrepareLiveGpuEnv()
{
	// Explicit CPU renderer mode must survive the live-session environment setup.
	if (!OKLiveGpuSkinRequested())
	{
		GLiveGpu = false; GSkelEnabled = false; GSkelEnvRead = true;
#if PLATFORM_WINDOWS
		::SetEnvironmentVariableW(TEXT("OK_GPU_SKIN"), TEXT("0"));
		::SetEnvironmentVariableW(TEXT("OK_SKELMESH"), TEXT("0"));
#endif
		return;
	}
	GLiveGpu = true;
	GSkelEnabled = true;
	GSkelEnvRead = true;
#if PLATFORM_WINDOWS
	::SetEnvironmentVariableW(TEXT("OK_GPU_SKIN"), TEXT("1"));
	::SetEnvironmentVariableW(TEXT("OK_SKELMESH"), TEXT("1"));
	::SetEnvironmentVariableW(TEXT("OK_NOPOSE"), TEXT("1"));
	::SetEnvironmentVariableW(TEXT("OK_UE_ANIM"), TEXT("1"));
	// Keep the head skinned (kind 2). A CPU-baked head only follows cf_s_head rigidly, so ABMX head bones
	// (cf_J_FaceBase position/length/scale) and face sliders never reach the face after load.
	::SetEnvironmentVariableW(TEXT("OK_HEAD_GPUSKIN"), TEXT("1"));
#endif
}

static FVector OkToUePos(float X, float Y, float Z)
{
	return FVector(-X * 100.0f, Z * 100.0f, Y * 100.0f);
}

static FVector OkSkelToUeDir(float X, float Y, float Z)
{
	return FVector(-X, Z, Y);
}

// Unity column-vector TRS (same as pose::trs) then T_ue = B W B, B(x,y,z)=(-x,z,y).
static FTransform OkLocalToUe(const float Pos[3], const float Rot[4], const float Scl[3])
{
	const float X = Rot[0];
	const float Y = Rot[1];
	const float Z = Rot[2];
	const float W = Rot[3];
	const FVector T = OkToUePos(Pos[0], Pos[1], Pos[2]);
	const FVector S(Scl[0], Scl[2], Scl[1]);
	const FQuat Qb(0.f, 0.70710678118f, 0.70710678118f, 0.f);
	const FQuat Qu(X, Y, Z, W);
	FQuat Que = Qb * Qu * Qb;
	Que.Normalize();
	return FTransform(Que, T, S);
}

FTransform OKUnityLocalToUnreal(const float Pos[3],const float Rot[4],const float Scale[3])
{
	return OkLocalToUe(Pos,Rot,Scale);
}
void OKUnrealLocalToUnity(const FTransform& Local,float Pos[3],float Rot[4],float Scale[3])
{
	const FVector P=Local.GetTranslation(),S=Local.GetScale3D();
	Pos[0]=-P.X/100.;Pos[1]=P.Z/100.;Pos[2]=P.Y/100.;
	Scale[0]=S.X;Scale[1]=S.Z;Scale[2]=S.Y;
	const FQuat Basis(0.,0.7071067811865475,0.7071067811865475,0.);
	const FQuat Q=(Basis*Local.GetRotation()*Basis).GetNormalized();
	Rot[0]=Q.X;Rot[1]=Q.Y;Rot[2]=Q.Z;Rot[3]=Q.W;
}

struct FOkMat4
{
	float M[4][4];
};

static FOkMat4 OkMatIdent()
{
	FOkMat4 R;
	FMemory::Memzero(R);
	R.M[0][0] = R.M[1][1] = R.M[2][2] = R.M[3][3] = 1.f;
	return R;
}

static FOkMat4 OkMatMul(const FOkMat4& A, const FOkMat4& B)
{
	FOkMat4 O = OkMatIdent();
	for (int32 i = 0; i < 4; ++i)
	{
		for (int32 j = 0; j < 4; ++j)
		{
			O.M[i][j] = A.M[i][0] * B.M[0][j] + A.M[i][1] * B.M[1][j] + A.M[i][2] * B.M[2][j] + A.M[i][3] * B.M[3][j];
		}
	}
	return O;
}

static FOkMat4 OkMatTrs(const float Pos[3], const float Rot[4], const float Scl[3])
{
	const float X = Rot[0];
	const float Y = Rot[1];
	const float Z = Rot[2];
	const float W = Rot[3];
	const float X2 = X + X;
	const float Y2 = Y + Y;
	const float Z2 = Z + Z;
	const float XX = X * X2;
	const float YY = Y * Y2;
	const float ZZ = Z * Z2;
	const float XY = X * Y2;
	const float XZ = X * Z2;
	const float YZ = Y * Z2;
	const float WX = W * X2;
	const float WY = W * Y2;
	const float WZ = W * Z2;
	FOkMat4 R;
	R.M[0][0] = (1.f - (YY + ZZ)) * Scl[0];
	R.M[0][1] = (XY - WZ) * Scl[1];
	R.M[0][2] = (XZ + WY) * Scl[2];
	R.M[0][3] = Pos[0];
	R.M[1][0] = (XY + WZ) * Scl[0];
	R.M[1][1] = (1.f - (XX + ZZ)) * Scl[1];
	R.M[1][2] = (YZ - WX) * Scl[2];
	R.M[1][3] = Pos[1];
	R.M[2][0] = (XZ - WY) * Scl[0];
	R.M[2][1] = (YZ + WX) * Scl[1];
	R.M[2][2] = (1.f - (XX + YY)) * Scl[2];
	R.M[2][3] = Pos[2];
	R.M[3][0] = 0.f;
	R.M[3][1] = 0.f;
	R.M[3][2] = 0.f;
	R.M[3][3] = 1.f;
	return R;
}

static bool OkMatInv(const FOkMat4& In, FOkMat4& Out)
{
	FOkMat4 A = In;
	Out = OkMatIdent();
	for (int32 i = 0; i < 4; ++i)
	{
		int32 Piv = i;
		float Best = FMath::Abs(A.M[i][i]);
		for (int32 r = i + 1; r < 4; ++r)
		{
			const float V = FMath::Abs(A.M[r][i]);
			if (V > Best)
			{
				Best = V;
				Piv = r;
			}
		}
		if (Best < 1e-8f)
		{
			return false;
		}
		if (Piv != i)
		{
			for (int32 c = 0; c < 4; ++c)
			{
				Swap(A.M[i][c], A.M[Piv][c]);
				Swap(Out.M[i][c], Out.M[Piv][c]);
			}
		}
		const float D = A.M[i][i];
		for (int32 c = 0; c < 4; ++c)
		{
			A.M[i][c] /= D;
			Out.M[i][c] /= D;
		}
		for (int32 r = 0; r < 4; ++r)
		{
			if (r == i)
			{
				continue;
			}
			const float F = A.M[r][i];
			for (int32 c = 0; c < 4; ++c)
			{
				A.M[r][c] -= F * A.M[i][c];
				Out.M[r][c] -= F * Out.M[i][c];
			}
		}
	}
	return true;
}

static void OkMatPoint(const FOkMat4& M, const float P[3], float Out[3])
{
	Out[0] = M.M[0][0] * P[0] + M.M[0][1] * P[1] + M.M[0][2] * P[2] + M.M[0][3];
	Out[1] = M.M[1][0] * P[0] + M.M[1][1] * P[1] + M.M[1][2] * P[2] + M.M[1][3];
	Out[2] = M.M[2][0] * P[0] + M.M[2][1] * P[1] + M.M[2][2] * P[2] + M.M[2][3];
}

static void OKFillBoneWorlds(FOkSession& Session, bool bPose, TArray<int32>& Parent, TArray<FOkMat4>& World)
{
	const int32 NB = Session.SkelBoneCount();
	Parent.SetNum(NB);
	World.SetNum(NB);
	TArray<FOkMat4> Local;
	Local.SetNum(NB);
	for (int32 i = 0; i < NB; ++i)
	{
		OkBoneView Bone;
		FMemory::Memzero(Bone);
		if (!Session.SkelBoneAt(i, Bone))
		{
			Parent[i] = -1;
			Local[i] = OkMatIdent();
			continue;
		}
		Parent[i] = Bone.parent;
		Local[i] = bPose
			? OkMatTrs(Bone.pose_pos, Bone.pose_rot, Bone.pose_scl)
			: OkMatTrs(Bone.rest_pos, Bone.rest_rot, Bone.rest_scl);
	}
	TArray<uint8> Done;
	Done.Init(0, NB);
	int32 Left = NB;
	for (int32 Pass = 0; Pass < NB && Left > 0; ++Pass)
	{
		for (int32 i = 0; i < NB; ++i)
		{
			if (Done[i])
			{
				continue;
			}
			const int32 P = Parent[i];
			if (P < 0 || P >= NB)
			{
				World[i] = Local[i];
				Done[i] = 1;
				--Left;
			}
			else if (Done[P])
			{
				World[i] = OkMatMul(World[P], Local[i]);
				Done[i] = 1;
				--Left;
			}
		}
	}
}

static FTransform OkUnityWorldToUeXf(const FOkMat4& W)
{
	FOkMat4 B = OkMatIdent();
	B.M[0][0] = -1.f;
	B.M[1][1] = 0.f;
	B.M[1][2] = 1.f;
	B.M[2][1] = 1.f;
	B.M[2][2] = 0.f;
	const FOkMat4 R = OkMatMul(OkMatMul(B, W), B);
	FVector AxisX(R.M[0][0], R.M[1][0], R.M[2][0]);
	FVector AxisY(R.M[0][1], R.M[1][1], R.M[2][1]);
	FVector AxisZ(R.M[0][2], R.M[1][2], R.M[2][2]);
	const float Sx = AxisX.Size();
	const float Sy = AxisY.Size();
	const float SzRaw = AxisZ.Size();
	if (Sx > 1e-8f)
	{
		AxisX /= Sx;
	}
	if (Sy > 1e-8f)
	{
		AxisY /= Sy;
	}
	float Sz = SzRaw;
	if (SzRaw > 1e-8f)
	{
		AxisZ /= SzRaw;
	}
	if (FVector::DotProduct(FVector::CrossProduct(AxisX, AxisY), AxisZ) < 0.f)
	{
		Sz = -Sz;
		AxisZ = -AxisZ;
	}
	FMatrix Rot(AxisX, AxisY, AxisZ, FVector::ZeroVector);
	const FQuat Q = Rot.ToQuat();
	const FVector T(R.M[0][3] * 100.f, R.M[1][3] * 100.f, R.M[2][3] * 100.f);
	return FTransform(Q, T, FVector(Sx, Sy, Sz));
}

// Unity local TRS -> UE local TRS. B(x,y,z)=(-x,z,y) and B*B=I, so
// local_ue = B * local_unity * B. One decompose of a small local matrix.
// Rebuilding the world matrix and calling GetRelativeTransform decomposes
// non-uniform scale (body shape) twice; the error accumulates down the arm
// and shows up as scattered vertices, worst on the hands.
static FTransform OkLocalTrsToUe(const float Pos[3], const float Rot[4], const float Scl[3])
{
	return OkUnityWorldToUeXf(OkMatTrs(Pos, Rot, Scl));
}

USkeleton* OKBuildSharedSkeleton(FOkSession& Session, UObject* Outer)
{
	const int32 NumBones = Session.SkelBoneCount();
	if (NumBones <= 0)
	{
		return nullptr;
	}
	// Immutable meshes can outlive their original actor. Never retain that
	// actor and its native session through an asset's Outer chain.
	USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Transient);
	FReferenceSkeleton RefSkel;
	{
		FReferenceSkeletonModifier Mod(RefSkel, Skeleton);
		for (int32 i = 0; i < NumBones; ++i)
		{
			OkBoneView Bone;
			FMemory::Memzero(Bone);
			if (!Session.SkelBoneAt(i, Bone) || !Bone.name)
			{
				return nullptr;
			}
			const FString Name = UTF8_TO_TCHAR(Bone.name);
			const int32 P = Bone.parent < 0 ? INDEX_NONE : Bone.parent;
			const FTransform Rest = OkLocalTrsToUe(Bone.rest_pos, Bone.rest_rot, Bone.rest_scl);
			Mod.Add(FMeshBoneInfo(*Name, Name, P), Rest, true);
		}
	}
	USkeletalMesh* Dummy = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient);
	Dummy->NeverStream = true;
	Dummy->SetRefSkeleton(RefSkel);
	Dummy->SetSkeleton(Skeleton);
	Skeleton->MergeAllBonesToBoneTree(Dummy);
	return Skeleton;
}

USkeletalMesh* OKBuildDummyLeaderMesh(USkeleton* Skeleton, UObject* Outer)
{
	if (!Skeleton)
	{
		return nullptr;
	}
	USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Outer, NAME_None, RF_Transient);
	Mesh->NeverStream = true;
	Mesh->SetRefSkeleton(Skeleton->GetReferenceSkeleton());
	Mesh->SetSkeleton(Skeleton);
	Mesh->CalculateInvRefMatrices();
	Mesh->AddLODInfo();
	TArray<FVector3f> Positions;
	Positions.Add(FVector3f(0, 0, 0));
	Positions.Add(FVector3f(1, 0, 0));
	Positions.Add(FVector3f(0, 1, 0));
	TArray<FSkinWeightInfo> Skin;
	Skin.SetNumZeroed(3);
	for (int32 v = 0; v < 3; ++v)
	{
		Skin[v].InfluenceWeights[0] = 65535;
	}
	TArray<uint32> Indices = {0, 1, 2};
	Mesh->AllocateResourceForRendering();
	FSkeletalMeshRenderData* RD = Mesh->GetResourceForRendering();
	if (!RD)
	{
		return nullptr;
	}
	FSkeletalMeshLODRenderData* LOD = new FSkeletalMeshLODRenderData();
	RD->LODRenderData.Add(LOD);
	RD->NumInlinedLODs = 1;
	RD->NumNonOptionalLODs = 1;
	RD->CurrentFirstLODIdx = 0;
	RD->PendingFirstLODIdx = 0;
	LOD->StaticVertexBuffers.PositionVertexBuffer.Init(Positions, true);
	LOD->StaticVertexBuffers.StaticMeshVertexBuffer.Init(3, 1, true);
	for (int32 v = 0; v < 3; ++v)
	{
		LOD->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(
			v, FVector3f(1, 0, 0), FVector3f(0, 1, 0), FVector3f(0, 0, 1));
		LOD->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(v, 0, FVector2f(0, 0));
	}
	LOD->SkinWeightVertexBuffer.SetMaxBoneInfluences(4);
	LOD->SkinWeightVertexBuffer = Skin;
	LOD->MultiSizeIndexContainer.CreateIndexBuffer(sizeof(uint32));
	LOD->MultiSizeIndexContainer.CopyIndexBuffer(Indices);
	FSkelMeshRenderSection Section;
	Section.NumTriangles = 1;
	Section.NumVertices = 3;
	Section.MaxBoneInfluences = 4;
	Section.BoneMap.Add(0);
	LOD->RenderSections.Add(Section);
	LOD->ActiveBoneIndices.Add(0);
	const int32 NB = Skeleton->GetReferenceSkeleton().GetNum();
	for (int32 i = 0; i < NB; ++i)
	{
		LOD->RequiredBones.Add(static_cast<FBoneIndexType>(i));
	}
	Mesh->SetImportedBounds(FBoxSphereBounds(FVector::ZeroVector, FVector(1, 1, 1), 1));
	OKAssignSkelUsageMaterial(Mesh);
	Mesh->InitResources();
	return Mesh;
}

static void OKAddMorphs(USkeletalMesh* Mesh, FOkSession& Session, int32 PartIndex, int32 NV)
{
	if (!Mesh || PartIndex < 0 || NV <= 0)
	{
		return;
	}
	const int32 MC = Session.MorphCount(PartIndex);
	for (int32 m = 0; m < MC; ++m)
	{
		OkMorphView Morph;
		FMemory::Memzero(Morph);
		if (!Session.MorphAt(PartIndex, m, Morph) || !Morph.name || Morph.n <= 0 || !Morph.indices || !Morph.deltas)
		{
			continue;
		}
		const FName MorphName = UTF8_TO_TCHAR(Morph.name);
		UMorphTarget* Target = NewObject<UMorphTarget>(Mesh, MorphName);
		Target->BaseSkelMesh = Mesh;
		FMorphTargetLODModel LOD;
		LOD.Vertices.Reserve(Morph.n);
		for (int32 d = 0; d < Morph.n; ++d)
		{
			const int32 Src = static_cast<int32>(Morph.indices[d]);
			if (Src < 0 || Src >= NV)
			{
				continue;
			}
			const float* P = Morph.deltas + d * 3;
			FMorphTargetDelta Delta;
			const FVector Ue = OkToUePos(P[0], P[1], P[2]);
			Delta.PositionDelta = FVector3f(Ue);
			Delta.SourceIdx = static_cast<uint32>(Src);
			LOD.Vertices.Add(Delta);
		}
		if (LOD.Vertices.Num() == 0)
		{
			continue;
		}
		LOD.NumVertices = LOD.Vertices.Num();
		LOD.NumBaseMeshVerts = NV;
		LOD.SectionIndices.Add(0);
		Target->GetMorphLODModels().Add(MoveTemp(LOD));
		Mesh->RegisterMorphTarget(Target, false);
	}
	// RegisterMorphTarget(false) only appends to the target array. Build the
	// name-to-index map once after batching, before components/RHI consume it.
	Mesh->InitMorphTargets();
}

namespace {
struct FOKMeshCacheEntry { FString Key; TWeakObjectPtr<USkeletalMesh> Mesh; };
TArray<FOKMeshCacheEntry> GOKMeshCache;
uint64 GOKMeshCacheHits=0, GOKMeshCacheMisses=0;

// Hash the inputs consumed by this builder, never pointer addresses, posed
// vertices, or a source mesh name alone. Materials are component overrides.
struct FOKMeshFingerprint {
 FSHA1 Hash;
 template<class T> void Value(const T& V) { Hash.Update(reinterpret_cast<const uint8*>(&V),sizeof(T)); }
 void Text(const char* S) { const int32 N=S?FCStringAnsi::Strlen(S):-1;Value(N);if(N>0)Hash.Update(reinterpret_cast<const uint8*>(S),N); }
 template<class T> void Array(const T* P,int32 Count) { const int32 N=P?Count:-1;Value(N);if(N>0)Hash.Update(reinterpret_cast<const uint8*>(P),uint32(sizeof(T))*N); }
 void Transform(const FTransform& T) {
  const FVector P=T.GetTranslation(),S=T.GetScale3D();const FQuat Q=T.GetRotation();
  Value(P.X);Value(P.Y);Value(P.Z);Value(Q.X);Value(Q.Y);Value(Q.Z);Value(Q.W);Value(S.X);Value(S.Y);Value(S.Z);
 }
 FString Finish(){Hash.Final();FSHAHash Result;Hash.GetHash(Result.Hash);return Result.ToString();}
};
FString OKMeshKey(FOkSession& Session,const OkMeshView& V,USkeleton* Shared,int32 Part,float Width,bool Outline,bool Skip) {
 FOKMeshFingerprint H;H.Value(V.n_verts);H.Value(V.n_idx);H.Value(Width);H.Value(Outline);H.Value(Skip);
 H.Text(V.role);H.Text(V.name);H.Value(V.shader_kind);H.Value(V.eye_draw);
 H.Array(V.rest_positions,V.n_verts*3);H.Array(V.reference_normals?V.reference_normals:V.normals,V.n_verts*3);
 H.Array(V.rest_tangents,V.n_verts*4);H.Array(V.weights,V.n_verts*4);H.Array(V.bone_idx,V.n_verts*4);
 H.Array(V.indices,V.n_idx);H.Array(V.uvs,V.n_verts*2);H.Array(V.uv2,V.n_verts*2);H.Array(V.uv3,V.n_verts*2);H.Array(V.uv4,V.n_verts*2);
 H.Array(V.eye_sample_uvs,V.n_verts*2);H.Array(V.vertex_colors,V.n_verts*4);
 H.Array(V.detail_mask,V.detail_mask_len);H.Array(V.detail_mask_st,4);
 const auto& Ref=Shared->GetReferenceSkeleton();const int32 NB=Ref.GetNum();H.Value(NB);
 for(int32 B=0;B<NB;++B){const FTCHARToUTF8 Name(*Ref.GetBoneName(B).ToString());H.Text(Name.Get());H.Value(Ref.GetParentIndex(B));H.Transform(Ref.GetRefBonePose()[B]);}
 // The source palette's ordering is independent of the target reference rig.
 const int32 ExportCount=Session.SkelBoneCount();H.Value(ExportCount);
 for(int32 B=0;B<ExportCount;++B){OkBoneView Bone{};const bool Valid=Session.SkelBoneAt(B,Bone);H.Value(Valid);if(Valid)H.Text(Bone.name);}
 const int32 MC=Part>=0?Session.MorphCount(Part):0;H.Value(MC);
 for(int32 M=0;M<MC;++M){OkMorphView Morph{};const bool Valid=Session.MorphAt(Part,M,Morph);H.Value(Valid);if(Valid){H.Text(Morph.name);H.Value(Morph.n);H.Array(Morph.indices,Morph.n);H.Array(Morph.deltas,Morph.n*3);}}
 return H.Finish();
}
}
FOKSkeletalMeshCacheStats OKSkeletalMeshCacheStats(){check(IsInGameThread());return {GOKMeshCacheHits,GOKMeshCacheMisses,GOKMeshCache.Num()};}
void OKClearSkeletalMeshCache(){check(IsInGameThread());GOKMeshCache.Reset();GOKMeshCacheHits=GOKMeshCacheMisses=0;}

static USkeletalMesh* OKBuildSkeletalMeshUncached(FOkSession& Session, const OkMeshView& View, USkeleton* Shared, int32 PartIndex, float OutlineWidthCm, bool bOutlinePass, bool bSkipFaceCavity, bool bSourceWinding)
{
	if (!View.rest_positions || !View.bone_idx || !View.weights || View.n_verts <= 0 || !Shared)
	{
		return nullptr;
	}
	const int32 NumBones = Session.SkelBoneCount();
	if (NumBones <= 0)
	{
		return nullptr;
	}

	USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient);
	Mesh->NeverStream = true;
	Mesh->SetRefSkeleton(Shared->GetReferenceSkeleton());
	Mesh->SetSkeleton(Shared);
	Mesh->CalculateInvRefMatrices();
	Mesh->AddLODInfo();

 // Export palettes and saved Skeletons can have different sibling ordering.
 // The slot index belongs to this Session, never to a separately saved rig.
 TArray<int32> ExportToMesh;ExportToMesh.SetNum(NumBones);
 for(int32 B=0;B<NumBones;++B){OkBoneView Bone{};
  if(!Session.SkelBoneAt(B,Bone)||!Bone.name)return nullptr;
  ExportToMesh[B]=Mesh->GetRefSkeleton().FindBoneIndex(FName(UTF8_TO_TCHAR(Bone.name)));
  if(ExportToMesh[B]==INDEX_NONE){UE_LOG(LogTemp,Error,TEXT("Skin source bone missing in target: %s"),UTF8_TO_TCHAR(Bone.name));return nullptr;}
 }

	const int32 NV = View.n_verts;
	// The Outline vertex program samples _DetailMask.b and multiplies its
	// extrusion by 1 - blue (main_skin/0000.asm.txt:40-56). Keep that mask on
	// the skinned shell too; without it the hand's layered geometry becomes a
	// solid black silhouette instead of a narrow contour.
	TArray<uint8> DetailRaw;
	int32 DetailW = 0;
	int32 DetailH = 0;
	if (OutlineWidthCm > 0.f && View.detail_mask && View.detail_mask_len > 16 && View.uvs)
	{
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		if (!OKDecodeSessionTexture(View.detail_mask, View.detail_mask_len, DetailRaw, DetailW, DetailH))
		{
			DetailRaw.Reset();
			DetailW = DetailH = 0;
		}
	}
	TArray<FVector3f> Positions;
	Positions.SetNumUninitialized(NV);
	TArray<int32> Used;
	Used.Init(-1, NumBones);
	TArray<FBoneIndexType> BoneMap;
	TArray<FSkinWeightInfo> Skin;
	Skin.SetNumZeroed(NV);
	for (int32 v = 0; v < NV; ++v)
	{
		const float* P = View.rest_positions + v * 3;
		const FVector Ue = OkToUePos(P[0], P[1], P[2]);
		FVector Position = Ue;
		const float* ReferenceNormals = View.reference_normals ? View.reference_normals : View.normals;
		if (OutlineWidthCm > 0.f && ReferenceNormals)
		{
			const float* Normal = ReferenceNormals + v * 3;
			float Coverage = 1.f;
			if (DetailW > 0 && DetailH > 0 && DetailRaw.Num() >= DetailW * DetailH * 4)
			{
				const float U = View.uvs[v * 2] * View.detail_mask_st[0] + View.detail_mask_st[2];
				const float V = View.uvs[v * 2 + 1] * View.detail_mask_st[1] + View.detail_mask_st[3];
				const int32 X = FMath::Clamp(FMath::FloorToInt((U - FMath::FloorToFloat(U)) * DetailW), 0, DetailW - 1);
				const int32 Y = FMath::Clamp(FMath::FloorToInt((V - FMath::FloorToFloat(V)) * DetailH), 0, DetailH - 1);
				Coverage = 1.f - DetailRaw[(Y * DetailW + X) * 4] / 255.f;
			}
			Position += OkSkelToUeDir(Normal[0], Normal[1], Normal[2]).GetSafeNormal() * (OutlineWidthCm * Coverage);
		}
		Positions[v] = FVector3f(Position);
		const float* Wf = View.weights + v * 4;
		const uint16* Bi = View.bone_idx + v * 4;
		float SumW = 0.f;
		int32 Infl[4] = {0, 0, 0, 0};
		float Ww[4] = {0, 0, 0, 0};
		for (int32 k = 0; k < 4; ++k)
		{
			const int32 B = static_cast<int32>(Bi[k]);
			if (B < 0 || B >= NumBones || Wf[k] <= 1e-6f)
			{
				continue;
			}
			if (Used[B] < 0)
			{
				Used[B] = BoneMap.Num();
				BoneMap.Add(static_cast<FBoneIndexType>(ExportToMesh[B]));
			}
			Infl[k] = Used[B];
			Ww[k] = Wf[k];
			SumW += Wf[k];
		}
		if (SumW < 1e-6f)
		{
			if (BoneMap.Num() == 0)
			{
				Used[0] = 0;
				BoneMap.Add(0);
			}
			Skin[v].InfluenceBones[0] = 0;
			Skin[v].InfluenceWeights[0] = 65535;
			continue;
		}
		uint32 Acc = 0;
		int32 MaxK = 0;
		for (int32 k = 0; k < 4; ++k)
		{
			uint16 Wi = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(Ww[k] / SumW * 65535.f), 0, 65535));
			Skin[v].InfluenceBones[k] = static_cast<FBoneIndexType>(Infl[k]);
			Skin[v].InfluenceWeights[k] = Wi;
			Acc += Wi;
			if (Wi >= Skin[v].InfluenceWeights[MaxK])
			{
				MaxK = k;
			}
		}
		if (Acc != 65535 && Acc > 0)
		{
			const int32 D = 65535 - static_cast<int32>(Acc);
			Skin[v].InfluenceWeights[MaxK] = static_cast<uint16>(
				FMath::Clamp(static_cast<int32>(Skin[v].InfluenceWeights[MaxK]) + D, 0, 65535));
		}
	}

	TArray<uint32> Indices;
	if (View.indices && View.n_idx >= 3)
	{
		Indices.Reserve(View.n_idx);
		for (int32 t = 0; t + 2 < View.n_idx; t += 3)
		{
			// Match the procedural face outline: exclude only triangles wholly
			// inside the authored mouth cavity (Unity V < .09, UE V > .91).
			if (bOutlinePass && bSkipFaceCavity && View.uvs &&
				View.uvs[View.indices[t] * 2 + 1] < .09f &&
				View.uvs[View.indices[t + 1] * 2 + 1] < .09f &&
				View.uvs[View.indices[t + 2] * 2 + 1] < .09f)
				continue;
#if WITH_EDITOR
            const bool ReverseOutline=!bSourceWinding&&(bOutlinePass || OutlineWidthCm>0.f);
#else
            // Cooked source outline programs cull the front face themselves.
            // They require the surface winding, unlike the editor shell graph.
            const bool ReverseOutline=false;
#endif
            Indices.Add(ReverseOutline ? View.indices[t] : View.indices[t + 2]);
            Indices.Add(View.indices[t + 1]);
            Indices.Add(ReverseOutline ? View.indices[t + 2] : View.indices[t]);
		}
	}

	Mesh->AllocateResourceForRendering();
	FSkeletalMeshRenderData* RD = Mesh->GetResourceForRendering();
	if (!RD)
	{
		return nullptr;
	}
	FSkeletalMeshLODRenderData* LOD = new FSkeletalMeshLODRenderData();
	RD->LODRenderData.Add(LOD);
	RD->NumInlinedLODs = 1;
	RD->NumNonOptionalLODs = 1;
	RD->CurrentFirstLODIdx = 0;
	RD->PendingFirstLODIdx = 0;

	// Skin uses independent UV1/UV2/UV3; face retains all four streams. Hair gloss
	// keeps Unity UV1 independently at UE TEXCOORD2.
	const bool bBodyOverlay = View.role && FCStringAnsi::Stricmp(View.role, "body") == 0;
	const bool bFaceOverlay=View.name && FCStringAnsi::Stricmp(View.name,"cf_O_face")==0 && (View.shader_kind==5 || View.shader_kind==7);
	// main_eye samples the two authored highlight coordinates at TEXCOORD1/2.
	// They are independent of UV0 and the generic overlay (Unity UV4).
	const bool bEyeOverlay = View.eye_draw != 0;
	const int32 NumUV = bFaceOverlay ? 4 : bEyeOverlay ? 3 : bBodyOverlay
		? (View.uv3 ? 3 : (View.uv2 ? 2 : 1))
		: ((View.shader_kind == 1 || View.shader_kind == 10) ? 3 : (View.uv4 ? 2 : 1));
	LOD->StaticVertexBuffers.PositionVertexBuffer.Init(Positions, true);
	LOD->StaticVertexBuffers.StaticMeshVertexBuffer.Init(NV, NumUV, true);
	// Shader kind 20 draws its baked albedo from COLOR0.
	if ((bBodyOverlay || bFaceOverlay || View.shader_kind == 20) && View.vertex_colors)
	{
		TArray<FColor> Colors;
		Colors.Reserve(NV);
		for (int32 v = 0; v < NV; ++v)
		{
			const uint8* C = View.vertex_colors + v * 4;
			Colors.Add(FColor(C[0], C[1], C[2], C[3]));
		}
		LOD->StaticVertexBuffers.ColorVertexBuffer.InitFromColorArray(Colors);
		Mesh->SetHasVertexColors(true);
	}
	// UE requires a tangent basis even when Unity's mesh has no tangent channel.
	// Build that fallback from UVs for geometry/materials that need one; the
	// extractor withholds tangent-space normal maps for missing source tangents.
	TArray<FVector> TangentSum;
	TArray<FVector> BitangentSum;
	TangentSum.Init(FVector::ZeroVector, NV);
	BitangentSum.Init(FVector::ZeroVector, NV);
	if (View.uvs)
	{
		for (int32 t = 0; t + 2 < Indices.Num(); t += 3)
		{
			const uint32 A = Indices[t], B = Indices[t + 1], C = Indices[t + 2];
			if (A >= static_cast<uint32>(NV) || B >= static_cast<uint32>(NV) || C >= static_cast<uint32>(NV)) continue;
			const FVector E1 = FVector(Positions[B] - Positions[A]);
			const FVector E2 = FVector(Positions[C] - Positions[A]);
			const FVector2D U0(View.uvs[A * 2], 1.f - View.uvs[A * 2 + 1]);
			const FVector2D U1(View.uvs[B * 2], 1.f - View.uvs[B * 2 + 1]);
			const FVector2D U2(View.uvs[C * 2], 1.f - View.uvs[C * 2 + 1]);
			const FVector2D D1 = U1 - U0, D2 = U2 - U0;
			const double Det = D1.X * D2.Y - D1.Y * D2.X;
			if (FMath::Abs(Det) < 1e-12) continue;
			const FVector T = (E1 * D2.Y - E2 * D1.Y) / Det;
			const FVector Bt = (E2 * D1.X - E1 * D2.X) / Det;
			for (uint32 I : {A, B, C}) { TangentSum[I] += T; BitangentSum[I] += Bt; }
		}
	}
	for (int32 v = 0; v < NV; ++v)
	{
		FVector N(0, 0, 1);
		const float* ReferenceNormals = View.reference_normals ? View.reference_normals : View.normals;
		if (ReferenceNormals)
		{
			const float* Nv = ReferenceNormals + v * 3;
			N = OkSkelToUeDir(Nv[0], Nv[1], Nv[2]);
			if (N.IsNearlyZero())
			{
				N = FVector(0, 0, 1);
			}
			else
			{
				N.Normalize();
			}
		}
		// Shader Forge/main_hair vertex shader passes the authored normal
		// through the object-to-world normal matrix. The source mesh name does
		// not change its sign; keep the normal and tangent basis from the dump.
	FVector T = TangentSum[v];
	if (View.rest_tangents)
	{
		const float* SourceTangent = View.rest_tangents + v * 4;
		T = OkSkelToUeDir(SourceTangent[0], SourceTangent[1], SourceTangent[2]);
	}
	T -= N * FVector::DotProduct(N, T);
		if (T.SizeSquared() < 1e-8f) T = FVector::CrossProduct(N, FVector(0, 0, 1));
		if (T.SizeSquared() < 1e-6f)
		{
			T = FVector::CrossProduct(N, FVector(1, 0, 0));
		}
	T.Normalize();
	FVector Bv = FVector::CrossProduct(N, T);
	if (View.rest_tangents)
	{
		// The normal PNG and its sampling UV are both flipped vertically, so
		// its green component still belongs to Unity's +V bitangent. The
		// coordinate change to UE is a rotation (positive determinant).
		if (View.rest_tangents[v * 4 + 3] < 0.f) Bv = -Bv;
	}
	else if (FVector::DotProduct(Bv, BitangentSum[v]) > 0.f) Bv = -Bv;
		LOD->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(
			v, FVector3f(T), FVector3f(Bv), FVector3f(N));
		FVector2f UV(0, 0);
		if (View.uvs)
		{
			const float* U = (bEyeOverlay && View.eye_sample_uvs ? View.eye_sample_uvs : View.uvs) + v * 2;
			UV = FVector2f(U[0], 1.0f - U[1]);
		}
		LOD->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(v, 0, UV);
		if (NumUV > 1)
		{
			const float* Source = (bBodyOverlay || bFaceOverlay || bEyeOverlay) ? View.uv2 : View.uv4;
			const float* U4 = Source ? Source + v * 2 : nullptr;
			LOD->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(
				v, 1, U4 ? FVector2f(U4[0], 1.0f - U4[1]) : FVector2f(0, 0));
		}
		if (NumUV > 2)
		{
			const float* Source = (bBodyOverlay || bFaceOverlay || bEyeOverlay) ? View.uv3 : View.uv2;
			// Match Unity's missing hair UV1 vertex binding (native A/B oracle).
			if (!Source && (View.shader_kind == 1 || View.shader_kind == 10)) Source = View.uvs;
			const float* U2 = Source ? Source + v * 2 : nullptr;
			LOD->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(
				v, 2, U2 ? FVector2f(U2[0], 1.0f - U2[1]) : FVector2f(0,1));
		}
		if (NumUV > 3) {
			const float* U3=View.uv4?View.uv4+v*2:nullptr;
			LOD->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(v,3,U3?FVector2f(U3[0],1.f-U3[1]):FVector2f(0,0));
		}
	}

	LOD->SkinWeightVertexBuffer.SetMaxBoneInfluences(4);
	LOD->SkinWeightVertexBuffer.SetUse16BitBoneIndex(true);
	// 8-bit weights (1/256) shear blended finger verts. Keep the 16-bit weights
	// we already packed into FSkinWeightInfo.
	LOD->SkinWeightVertexBuffer.SetUse16BitBoneWeight(true);
	LOD->SkinWeightVertexBuffer.SetNeedsCPUAccess(true);
	LOD->SkinWeightVertexBuffer = Skin;
	LOD->MultiSizeIndexContainer.CreateIndexBuffer(sizeof(uint32));
	LOD->MultiSizeIndexContainer.CopyIndexBuffer(Indices);

	FSkelMeshRenderSection Section;
	Section.MaterialIndex = 0;
	Section.BaseIndex = 0;
	Section.NumTriangles = Indices.Num() / 3;
	Section.BaseVertexIndex = 0;
	Section.NumVertices = NV;
	Section.MaxBoneInfluences = 4;
	Section.bCastShadow = true;
	Section.BoneMap = BoneMap;
	LOD->RenderSections.Add(Section);
	LOD->ActiveBoneIndices = BoneMap;
	LOD->RequiredBones.Reset();
	for (int32 i = 0; i < NumBones; ++i)
	{
		LOD->RequiredBones.Add(static_cast<FBoneIndexType>(i));
	}

	FBox Bound(ForceInit);
	for (const FVector3f& P : Positions)
	{
		Bound += FVector(P);
	}
	Mesh->SetImportedBounds(FBoxSphereBounds(Bound));

	OKAssignSkelUsageMaterial(Mesh);
	OKAddMorphs(Mesh, Session, PartIndex, NV);
	Mesh->InitResources();
	UE_LOG(LogTemp, Display, TEXT("OK skel build verts=%d bones=%d boneMap=%d invRef=%d"),
		NV, NumBones, BoneMap.Num(), Mesh->GetRefBasesInvMatrix().Num());
	return Mesh;
}

USkeletalMesh* OKBuildSkeletalMesh(FOkSession& Session,const OkMeshView& View,UObject* Outer,USkeleton* Shared,int32 PartIndex,float OutlineWidthCm,bool bOutlinePass,bool bSkipFaceCavity,bool bSourceWinding) {
 check(IsInGameThread());
 if(!Shared||!View.rest_positions||!View.bone_idx||!View.weights||View.n_verts<=0||Session.SkelBoneCount()<=0)return nullptr;
 const FString Key=OKMeshKey(Session,View,Shared,PartIndex,OutlineWidthCm,bOutlinePass,bSkipFaceCavity)+(bSourceWinding?TEXT("|source-winding"):TEXT(""));
 for(int32 I=GOKMeshCache.Num()-1;I>=0;--I){
  if(!GOKMeshCache[I].Mesh.IsValid()){GOKMeshCache.RemoveAt(I);continue;}
  if(GOKMeshCache[I].Key==Key){auto Entry=GOKMeshCache[I];GOKMeshCache.RemoveAt(I);GOKMeshCache.Add(Entry);++GOKMeshCacheHits;return Entry.Mesh.Get();}
 }
 ++GOKMeshCacheMisses;
 auto* Mesh=OKBuildSkeletalMeshUncached(Session,View,Shared,PartIndex,OutlineWidthCm,bOutlinePass,bSkipFaceCavity,bSourceWinding);
 if(Mesh){if(GOKMeshCache.Num()>=256)GOKMeshCache.RemoveAt(0);GOKMeshCache.Add({Key,Mesh});}
 return Mesh;
}

void OKAddSlotSockets(USkeletalMesh* Mesh, FOkSession& Session)
{
#if WITH_EDITOR

	if (!Mesh)
	{
		return;
	}
	const int32 NumBones = Session.SkelBoneCount();
	for (int32 i = 0; i < NumBones; ++i)
	{
		OkBoneView Bone;
		FMemory::Memzero(Bone);
		if (!Session.SkelBoneAt(i, Bone) || !Bone.name)
		{
			continue;
		}
		const FString Name = UTF8_TO_TCHAR(Bone.name);
		if (!Name.StartsWith(TEXT("a_n_"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		USkeletalMeshSocket* Sock = NewObject<USkeletalMeshSocket>(Mesh);
		Sock->SocketName = *Name;
		Sock->BoneName = *Name;
		Mesh->AddSocket(Sock);
	}

#else
return;
#endif
}

void OKBuildSkelPose(FOkSession& Session,TArray<FTransform>& Out)
{
 const int32 Count=Session.SkelBoneCount();Out.SetNum(FMath::Max(0,Count));
 for(int32 I=0;I<Count;++I){OkBoneView Bone={};Out[I]=Session.SkelBoneAt(I,Bone)?OkLocalTrsToUe(Bone.pose_pos,Bone.pose_rot,Bone.pose_scl):FTransform::Identity;}
}
void OKApplySkelPose(UPoseableMeshComponent* Comp, FOkSession& Session)
{
 TArray<FTransform> Pose;OKBuildSkelPose(Session,Pose);OKApplySkelPose(Comp,Pose);
}
void OKApplySkelPose(UPoseableMeshComponent* Comp,const TArray<FTransform>& Pose)
{
 if(!Comp||!Comp->GetSkinnedAsset()||Pose.IsEmpty())return;
 const int32 Required=Comp->GetSkinnedAsset()->GetRefSkeleton().GetNum();
 if(Comp->BoneSpaceTransforms.Num()!=Required||Comp->GetNumComponentSpaceTransforms()!=Required)Comp->AllocateTransformData();
 if(Comp->BoneSpaceTransforms.Num()<Pose.Num())Comp->BoneSpaceTransforms.SetNum(Pose.Num());
 const int32 Count=FMath::Min(Pose.Num(),Comp->BoneSpaceTransforms.Num());
 for(int32 I=0;I<Count;++I)Comp->BoneSpaceTransforms[I]=Pose[I];
 FKKRuntimeAffinePoseScope PoseInput(Pose,Comp);
 // Engine RefreshBoneTransforms fills component space, updates bounds/children,
 // and marks dynamic data/transform dirty. A pose change does not recreate the
 // render proxy or mesh buffers; those are unchanged.
 Comp->MarkRefreshTransformDirty();Comp->InvalidateCachedBounds();
 Comp->RefreshBoneTransforms();
 // UPoseableMeshComponent does not publish leader changes to followers like
 // USkeletalMeshComponent does. Shared followers need fresh bounds and render
 // data after the leader pose, while retaining their own geometry/materials.
 for(const auto& Follower:Comp->GetFollowerPoseComponents())
  if(USkinnedMeshComponent* Display=Follower.Get())Display->UpdateBounds();
 Comp->RefreshFollowerComponents();Comp->ClearMotionVector();
}

static int32 FindKkBone(const FReferenceSkeleton& Ref, std::initializer_list<const TCHAR*> Names)
{
	for (const TCHAR* N : Names)
	{
		const int32 I = Ref.FindBoneIndex(FName(N));
		if (I != INDEX_NONE)
		{
			return I;
		}
	}
	return INDEX_NONE;
}

static FString NormBone(const FString& In)
{
	FString S = In.ToLower();
	S.ReplaceInline(TEXT(" "), TEXT(""));
	S.ReplaceInline(TEXT("-"), TEXT("_"));
	return S;
}

static FQuat AccumulateBoneRotation(const TArray<FTransform>& Locals, const FReferenceSkeleton& Ref, int32 Bone)
{
	FQuat Rot = FQuat::Identity;
	TArray<int32, TInlineAllocator<24>> Chain;
	for (int32 Index = Bone; Index != INDEX_NONE && Locals.IsValidIndex(Index); Index = Ref.GetParentIndex(Index))
	{
		Chain.Add(Index);
	}
	for (int32 i = Chain.Num() - 1; i >= 0; --i)
	{
		Rot = Rot * Locals[Chain[i]].GetRotation();
	}
	return Rot;
}

// Arms point along ±X. The same world-up yaw swings the left arm forward and the right arm back.
static void SwingAroundWorldUp(TArray<FTransform>& Locals, const FReferenceSkeleton& Ref, int32 Bone, float Degrees)
{
	if (!Locals.IsValidIndex(Bone) || Bone >= Ref.GetNum() || FMath::IsNearlyZero(Degrees))
	{
		return;
	}
	FQuat RestCS = FQuat::Identity;
	TArray<int32, TInlineAllocator<24>> Chain;
	for (int32 Index = Bone; Index != INDEX_NONE; Index = Ref.GetParentIndex(Index))
	{
		Chain.Add(Index);
	}
	for (int32 i = Chain.Num() - 1; i >= 0; --i)
	{
		RestCS = RestCS * Ref.GetRefBonePose()[Chain[i]].GetRotation();
	}
	const int32 Parent = Ref.GetParentIndex(Bone);
	const FQuat ParentCS = Parent >= 0 ? AccumulateBoneRotation(Locals, Ref, Parent) : FQuat::Identity;
	const FQuat WorldSwing(FVector::UpVector, FMath::DegreesToRadians(Degrees));
	Locals[Bone].SetRotation(ParentCS.Inverse() * (WorldSwing * RestCS));
}

UAnimSequence* OKBuildUeWalkSequence(UObject* Outer)
{
#if WITH_EDITOR

	USkeleton* Skel = NewObject<USkeleton>(Outer, NAME_None, RF_Transient);
	{
		FReferenceSkeletonModifier Mod(Skel);
		auto Add = [&](const TCHAR* Name, int32 Parent) -> int32
		{
			Mod.Add(FMeshBoneInfo(FName(Name), Name, Parent), FTransform::Identity, true);
			return Mod.FindBoneIndex(FName(Name));
		};
		const int32 Root = Add(TEXT("root"), INDEX_NONE);
		const int32 Pelvis = Add(TEXT("pelvis"), Root);
		const int32 Spine01 = Add(TEXT("spine_01"), Pelvis);
		const int32 Spine02 = Add(TEXT("spine_02"), Spine01);
		const int32 Spine03 = Add(TEXT("spine_03"), Spine02);
		const int32 Neck = Add(TEXT("neck"), Spine03);
		Add(TEXT("head"), Neck);
		const int32 ClavL = Add(TEXT("clavicle_l"), Spine03);
		const int32 ClavR = Add(TEXT("clavicle_r"), Spine03);
		Add(TEXT("upperarm_l"), ClavL);
		Add(TEXT("upperarm_r"), ClavR);
		Add(TEXT("lowerarm_l"), Mod.FindBoneIndex(TEXT("upperarm_l")));
		Add(TEXT("lowerarm_r"), Mod.FindBoneIndex(TEXT("upperarm_r")));
		Add(TEXT("hand_l"), Mod.FindBoneIndex(TEXT("lowerarm_l")));
		Add(TEXT("hand_r"), Mod.FindBoneIndex(TEXT("lowerarm_r")));
		Add(TEXT("thigh_l"), Pelvis);
		Add(TEXT("thigh_r"), Pelvis);
		Add(TEXT("calf_l"), Mod.FindBoneIndex(TEXT("thigh_l")));
		Add(TEXT("calf_r"), Mod.FindBoneIndex(TEXT("thigh_r")));
		Add(TEXT("foot_l"), Mod.FindBoneIndex(TEXT("calf_l")));
		Add(TEXT("foot_r"), Mod.FindBoneIndex(TEXT("calf_r")));
	}
	UAnimSequence* Seq = NewObject<UAnimSequence>(Outer, NAME_None, RF_Transient);
	Seq->SetSkeleton(Skel);
	IAnimationDataController& Ctrl = Seq->GetController();
	// Editor builds use UAnimationSequencerDataModel. Without InitializeModel the
	// movie scene / FK rig never exists, so AddBoneCurve reports success and writes nothing.
	Ctrl.OpenBracket(NSLOCTEXT("OK", "BuildWalk", "Build walk sequence"), false);
	Ctrl.InitializeModel();
	Ctrl.SetFrameRate(FFrameRate(30, 1), false);
	constexpr int32 Frames = 30;
	Ctrl.SetNumberOfFrames(FFrameNumber(Frames), false);
	const FReferenceSkeleton& Ref = Skel->GetReferenceSkeleton();
	TArray<FVector> Pos;
	TArray<FQuat> Rot;
	TArray<FVector> Scale;
	Pos.SetNum(Frames + 1);
	Rot.SetNum(Frames + 1);
	Scale.Init(FVector::OneVector, Frames + 1);
	for (int32 b = 0; b < Ref.GetNum(); ++b)
	{
		const FName Bone = Ref.GetBoneName(b);
		const FString N = Bone.ToString();
		for (int32 f = 0; f <= Frames; ++f)
		{
			const float T = static_cast<float>(f) / static_cast<float>(Frames);
			const float S = FMath::Sin(T * 2.f * PI);
			float Pitch = 0.f;
			if (N == TEXT("thigh_l"))
			{
				Pitch = S * 40.f;
			}
			else if (N == TEXT("thigh_r"))
			{
				Pitch = -S * 40.f;
			}
			else if (N == TEXT("calf_l"))
			{
				Pitch = FMath::Max(0.f, -S) * 50.f;
			}
			else if (N == TEXT("calf_r"))
			{
				Pitch = FMath::Max(0.f, S) * 50.f;
			}
			else if (N == TEXT("upperarm_l") || N == TEXT("upperarm_r"))
			{
				// Stored as roll, applied later as the same world-up yaw on both arms.
				Pitch = S * 50.f;
			}
			else if (N == TEXT("clavicle_l") || N == TEXT("clavicle_r"))
			{
				Pitch = S * 20.f;
			}
			else if (N == TEXT("pelvis"))
			{
				Pos[f] = FVector(0.f, 0.f, FMath::Abs(S) * 4.f);
			}
			Rot[f] = FRotator(0.f, 0.f, Pitch).Quaternion();
			if (N != TEXT("pelvis"))
			{
				Pos[f] = FVector::ZeroVector;
			}
		}
		const bool bAdded = Ctrl.AddBoneCurve(Bone, false);
		const bool bSet = Ctrl.SetBoneTrackKeys(Bone, Pos, Rot, Scale, false);
		if (N == TEXT("thigh_l"))
		{
			TArray<FName> TrackNames;
			if (TScriptInterface<IAnimationDataModel> Model = Seq->GetDataModelInterface())
			{
				Model->GetBoneTrackNames(TrackNames);
			}
			const UObject* ModelObject = Seq->GetDataModelInterface().GetObject();
			UE_LOG(LogTemp, Display, TEXT("OK ue walk thigh_l add=%d set=%d tracks=%d frames=%d play=%.3f model=%s"),
				bAdded ? 1 : 0,
				bSet ? 1 : 0,
				TrackNames.Num(),
				Seq->GetDataModelInterface() ? Seq->GetDataModelInterface()->GetNumberOfFrames() : -1,
				Seq->GetDataModelInterface() ? Seq->GetDataModelInterface()->GetPlayLength() : -1.0,
				ModelObject ? *ModelObject->GetClass()->GetName() : TEXT("none"));
		}
	}
	Ctrl.NotifyPopulated();
	Ctrl.CloseBracket(false);
	FTransform ThighSample = FTransform::Identity;
	FTransform ArmSample = FTransform::Identity;
	if (TScriptInterface<IAnimationDataModel> Model = Seq->GetDataModelInterface())
	{
		const FFrameTime Frame = Model->GetFrameRate().AsFrameTime(0.25);
		ThighSample = Model->EvaluateBoneTrackTransform(TEXT("thigh_l"), Frame, EAnimInterpolationType::Linear);
		ArmSample = Model->EvaluateBoneTrackTransform(TEXT("upperarm_l"), Frame, EAnimInterpolationType::Linear);
	}
	UE_LOG(LogTemp, Display, TEXT("OK ue walk sequence bones=%d length=%.3f thighRoll=%.1f armRoll=%.1f"),
		Ref.GetNum(), Seq->GetPlayLength(), ThighSample.Rotator().Roll, ArmSample.Rotator().Roll);
	return Seq;

#else
return nullptr;
#endif
}

bool OKUeWalkSequenceHasStride(UAnimSequence* Seq)
{
#if WITH_EDITOR

	if (!Seq)
	{
		return false;
	}
	const TScriptInterface<IAnimationDataModel> Model = Seq->GetDataModelInterface();
	if (!Model || Model->GetPlayLength() <= 0.05 || Model->GetNumBoneTracks() <= 0)
	{
		return false;
	}
	const FFrameTime Frame = Model->GetFrameRate().AsFrameTime(0.25);
	const FRotator Thigh = Model->EvaluateBoneTrackTransform(TEXT("thigh_l"), Frame, EAnimInterpolationType::Linear).Rotator();
	return FMath::Abs(Thigh.Roll) > 1.f || FMath::Abs(Thigh.Pitch) > 1.f || FMath::Abs(Thigh.Yaw) > 1.f;

#else
return false;
#endif
}

void OKApplyUeWalkDirect(UPoseableMeshComponent* Comp, float Time)
{
	if (!Comp || Comp->BoneSpaceTransforms.Num() == 0)
	{
		return;
	}
	USkinnedAsset* Asset = Comp->GetSkinnedAsset();
	if (!Asset)
	{
		return;
	}
	const FReferenceSkeleton& KkRef = Asset->GetRefSkeleton();
	const float S = FMath::Sin(Time * 2.f * PI);
	struct FSwing
	{
		const TCHAR* Bone;
		float Pitch;
	};
	const FSwing Swings[] = {
		{TEXT("cf_j_thigh00_L"), S * 40.f},
		{TEXT("cf_j_thigh00_R"), -S * 40.f},
		{TEXT("cf_j_leg01_L"), FMath::Max(0.f, -S) * 50.f},
		{TEXT("cf_j_leg01_R"), FMath::Max(0.f, S) * 50.f},
		{TEXT("cf_j_arm00_L"), S * 50.f},
		{TEXT("cf_j_arm00_R"), S * 50.f},
		{TEXT("cf_j_shoulder_L"), S * 20.f},
		{TEXT("cf_j_shoulder_R"), S * 20.f},
	};
	for (const FSwing& Swing : Swings)
	{
		const int32 Kk = Comp->GetBoneIndex(Swing.Bone);
		if (Kk == INDEX_NONE || !Comp->BoneSpaceTransforms.IsValidIndex(Kk) || Kk >= KkRef.GetNum())
		{
			if (FCString::Strstr(Swing.Bone, TEXT("arm00")) != nullptr)
			{
				UE_LOG(LogTemp, Warning, TEXT("OK walk missing bone %s"), Swing.Bone);
			}
			continue;
		}
		const bool bArm = FCString::Strstr(Swing.Bone, TEXT("arm")) != nullptr
			|| FCString::Strstr(Swing.Bone, TEXT("shoulder")) != nullptr;
		if (bArm)
		{
			SwingAroundWorldUp(Comp->BoneSpaceTransforms, KkRef, Kk, Swing.Pitch);
		}
		else
		{
			// Thigh local X is the left-right axis, so roll swings the leg forward.
			const FQuat Delta = FRotator(0.f, 0.f, Swing.Pitch).Quaternion();
			Comp->BoneSpaceTransforms[Kk].SetRotation(KkRef.GetRefBonePose()[Kk].GetRotation() * Delta);
		}
	}
	Comp->MarkRefreshTransformDirty();
	Comp->FillComponentSpaceTransforms();
	Comp->RefreshBoneTransforms();
	Comp->MarkRenderDynamicDataDirty();
	// PoseableMesh updates bones every frame but leaves last frame's matrices
	// behind. Motion blur then smears the whole arm/leg swing. Clear velocity.
	Comp->ClearMotionVector();
}

// Pure retargeting: input is the card-shaped Animator baseline, not last
// frame's simulated/displayed pose. Physics is applied by the session afterward.
int32 OKRetargetUnrealPose(TArray<FTransform>& Locals, const FReferenceSkeleton& KkRef, UAnimSequence* Seq, float Time)
{
#if WITH_EDITOR

    if (!Seq || !Seq->GetSkeleton() || Locals.Num()!=KkRef.GetNum() || !FMath::IsFinite(Time)) return 0;
    const FReferenceSkeleton& WalkRef=Seq->GetSkeleton()->GetReferenceSkeleton();
    const TArray<FTransform> Baseline=Locals;
    auto Anatomy=[](const TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,
        std::initializer_list<const TCHAR*> Hips,std::initializer_list<const TCHAR*> Head,
        std::initializer_list<const TCHAR*> Left,std::initializer_list<const TCHAR*> Right,
        FQuat& Basis,double& Height) {
        auto Point=[&](std::initializer_list<const TCHAR*> Names,FVector& Out) {
            const int32 Bone=FindKkBone(Ref,Names); if(Bone==INDEX_NONE) return false;
            FTransform World=Pose[Bone];
            for(int32 Parent=Ref.GetParentIndex(Bone);Parent!=INDEX_NONE;Parent=Ref.GetParentIndex(Parent)) World=World*Pose[Parent];
            Out=World.GetTranslation();return true;
        };
        FVector H,T,L,R; if(!Point(Hips,H)||!Point(Head,T)||!Point(Left,L)||!Point(Right,R)) return false;
        const FVector Up=T-H; const FVector Lateral=L-R; Height=Up.Length();
        if(Height<1.e-4||Lateral.Length()<1.e-4||FVector::CrossProduct(Up,Lateral).Length()<1.e-4) return false;
        Basis=FRotationMatrix::MakeFromZX(Up,Lateral).ToQuat();return true;
    };
    FQuat SourceBasis=FQuat::Identity,TargetBasis=FQuat::Identity;double SourceHeight=1.,TargetHeight=1.;
    const bool SourceAnatomy=Anatomy(WalkRef.GetRefBonePose(),WalkRef,{TEXT("pelvis"),TEXT("hips"),TEXT("cf_j_hips")},
        {TEXT("head"),TEXT("cf_j_head")},{TEXT("upperarm_l"),TEXT("cf_j_arm00_L")},{TEXT("upperarm_r"),TEXT("cf_j_arm00_R")},SourceBasis,SourceHeight);
    const bool TargetAnatomy=Anatomy(Baseline,KkRef,{TEXT("cf_j_hips")},{TEXT("cf_j_head")},
        {TEXT("cf_j_arm00_L")},{TEXT("cf_j_arm00_R")},TargetBasis,TargetHeight);
    const FQuat Alignment=SourceAnatomy&&TargetAnatomy?TargetBasis*SourceBasis.Inverse():FQuat::Identity;
    const double StatureRatio=SourceAnatomy&&TargetAnatomy?TargetHeight/SourceHeight:1.;
	const FAnimExtractContext Ctx(static_cast<double>(Time), false, {}, true);
	int32 Mapped = 0;
	for (int32 w = 0; w < WalkRef.GetNum(); ++w)
	{
		// A sparse source clip leaves unanimated bones at their reference pose.
		if (TScriptInterface<IAnimationDataModel> SourceModel=Seq->GetDataModelInterface())
			if(!SourceModel->IsValidBoneTrackName(WalkRef.GetBoneName(w))) continue;
		const FString Wn = NormBone(WalkRef.GetBoneName(w).ToString());
		int32 Kk = KkRef.FindBoneIndex(WalkRef.GetBoneName(w));
		if (Kk != INDEX_NONE) { /* Exact KKS bone identity takes priority. */ }
		else if (Wn.Contains(TEXT("pelvis")) || Wn == TEXT("hips"))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_hips")});
		}
		else if (Wn.Contains(TEXT("spine_01")) || Wn.Contains(TEXT("spine01")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_spine01")});
		}
		else if (Wn.Contains(TEXT("spine_02")) || Wn.Contains(TEXT("spine02")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_spine02")});
		}
		else if (Wn.Contains(TEXT("spine_03")) || Wn.Contains(TEXT("spine03")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_spine03")});
		}
		else if (Wn.Contains(TEXT("neck")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_neck")});
		}
		else if (Wn == TEXT("head") || Wn.EndsWith(TEXT("_head")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_head")});
		}
		else if (Wn.Contains(TEXT("clavicle_l")) || Wn.Contains(TEXT("shoulder_l")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_shoulder_L"), TEXT("cf_j_shoulder00_L")});
		}
		else if (Wn.Contains(TEXT("clavicle_r")) || Wn.Contains(TEXT("shoulder_r")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_shoulder_R"), TEXT("cf_j_shoulder00_R")});
		}
		else if (Wn.Contains(TEXT("upperarm_l")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_arm00_L")});
		}
		else if (Wn.Contains(TEXT("upperarm_r")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_arm00_R")});
		}
		else if (Wn.Contains(TEXT("lowerarm_l")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_forearm01_L")});
		}
		else if (Wn.Contains(TEXT("lowerarm_r")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_forearm01_R")});
		}
		else if (Wn.Contains(TEXT("hand_l")) && !Wn.Contains(TEXT("index")) && !Wn.Contains(TEXT("middle")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_hand_L")});
		}
		else if (Wn.Contains(TEXT("hand_r")) && !Wn.Contains(TEXT("index")) && !Wn.Contains(TEXT("middle")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_hand_R")});
		}
		else if (Wn.Contains(TEXT("thigh_l")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_thigh00_L")});
		}
		else if (Wn.Contains(TEXT("thigh_r")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_thigh00_R")});
		}
		else if (Wn.Contains(TEXT("calf_l")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_leg01_L")});
		}
		else if (Wn.Contains(TEXT("calf_r")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_leg01_R")});
		}
		else if (Wn.Contains(TEXT("foot_l")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_leg03_L"), TEXT("cf_j_foot_L")});
		}
		else if (Wn.Contains(TEXT("foot_r")))
		{
			Kk = FindKkBone(KkRef, {TEXT("cf_j_leg03_R"), TEXT("cf_j_foot_R")});
		}
		if (Kk == INDEX_NONE || !Locals.IsValidIndex(Kk) || Kk >= KkRef.GetNum())
		{
			continue;
		}
		FTransform WalkPose = FTransform::Identity;
		if (TScriptInterface<IAnimationDataModel> Model = Seq->GetDataModelInterface())
		{
			const FFrameTime Frame = Model->GetFrameRate().AsFrameTime(static_cast<double>(Time));
			WalkPose = Model->EvaluateBoneTrackTransform(WalkRef.GetBoneName(w), Frame, EAnimInterpolationType::Linear);
		}
		else
		{
			Seq->GetBoneTransform(WalkPose, FSkeletonPoseBoneIndex(w), Ctx, true);
		}
		const FTransform WalkRest = WalkRef.GetRefBonePose()[w];
        // Reject invalid tracks during preflight, before SetUnrealAnimation
        // resets the actor or the source particle history. The C ABI has the
        // same nonzero-scale/quaternion contract.
        const FVector WalkScale=WalkPose.GetScale3D(), RestScale=WalkRest.GetScale3D();
        if(WalkPose.ContainsNaN()||WalkRest.ContainsNaN()||
            WalkPose.GetRotation().SizeSquared()<1.e-12||WalkRest.GetRotation().SizeSquared()<1.e-12||
            FMath::Abs(WalkScale.X)<1.e-8||FMath::Abs(WalkScale.Y)<1.e-8||FMath::Abs(WalkScale.Z)<1.e-8||
            FMath::Abs(RestScale.X)<1.e-8||FMath::Abs(RestScale.Y)<1.e-8||FMath::Abs(RestScale.Z)<1.e-8) return 0;
        const FQuat SourceDelta = WalkRest.GetRotation().Inverse() * WalkPose.GetRotation();
        const FQuat SourceCS = AccumulateBoneRotation(WalkRef.GetRefBonePose(), WalkRef, w);
        const FQuat TargetCS = AccumulateBoneRotation(Baseline, KkRef, Kk);
        const FQuat WorldDelta = Alignment * SourceCS * SourceDelta * SourceCS.Inverse() * Alignment.Inverse();
        FQuat TargetRotation = Baseline[Kk].GetRotation() * TargetCS.Inverse() * WorldDelta * TargetCS;
        TargetRotation.Normalize();
        Locals[Kk].SetRotation(TargetRotation);
        const int32 SourceParent = WalkRef.GetParentIndex(w), TargetParent = KkRef.GetParentIndex(Kk);
        const FQuat SourceParentCS = AccumulateBoneRotation(WalkRef.GetRefBonePose(), WalkRef, SourceParent);
        const FQuat TargetParentCS = AccumulateBoneRotation(Baseline, KkRef, TargetParent);
        const FVector Translation = TargetParentCS.Inverse().RotateVector(Alignment.RotateVector(
            SourceParentCS.RotateVector(WalkPose.GetTranslation()-WalkRest.GetTranslation()))) * StatureRatio;
        Locals[Kk].SetTranslation(Baseline[Kk].GetTranslation()+Translation);
        Locals[Kk].SetScale3D(Baseline[Kk].GetScale3D() * WalkPose.GetScale3D() / WalkRest.GetScale3D());
		++Mapped;
	}
    return Mapped;

#else
return 0;
#endif
}

void OKOverlayUeWalk(UPoseableMeshComponent* Comp,UAnimSequence* Seq,float Time)
{
    if(!Comp||!Comp->GetSkinnedAsset()) return;
    if(!OKRetargetUnrealPose(Comp->BoneSpaceTransforms,Comp->GetSkinnedAsset()->GetRefSkeleton(),Seq,Time)) return;
    Comp->MarkRefreshTransformDirty();Comp->FillComponentSpaceTransforms();Comp->RefreshBoneTransforms();
    Comp->MarkRenderDynamicDataDirty();Comp->ClearMotionVector();
}

bool OKExtractSkinnedMesh(UPoseableMeshComponent* Comp, TArray<FVector>& OutVerts, TArray<int32>& OutTris, TArray<FVector2D>& OutUV, TArray<FVector>& OutNormals)
{
	OutVerts.Reset();
	OutTris.Reset();
	OutUV.Reset();
	OutNormals.Reset();
	if (!Comp)
	{
		return false;
	}
	USkinnedAsset* Asset = Comp->GetSkinnedAsset();
	if (!Asset)
	{
		return false;
	}
	FSkeletalMeshRenderData* RD = Asset->GetResourceForRendering();
	if (!RD || RD->LODRenderData.Num() == 0)
	{
		return false;
	}
	FSkeletalMeshLODRenderData& LOD = RD->LODRenderData[0];
	TArray<FMatrix44f> Rtl;
	Comp->GetCurrentRefToLocalMatrices(Rtl, 0);
	TArray<FVector3f> Skinned;
	USkinnedMeshComponent::ComputeSkinnedPositions(Comp, Skinned, Rtl, LOD, LOD.SkinWeightVertexBuffer);
	if (Skinned.Num() == 0)
	{
		return false;
	}
	OutVerts.Reserve(Skinned.Num());
	OutUV.Reserve(Skinned.Num());
	OutNormals.Reserve(Skinned.Num());
	for (int32 i = 0; i < Skinned.Num(); ++i)
	{
		OutVerts.Add(FVector(Skinned[i]));
		const FVector2f UV = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(i, 0);
		OutUV.Add(FVector2D(UV.X, UV.Y));
		const FVector3f N = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(i);
		OutNormals.Add(FVector(N));
	}
	TArray<uint32> Idx;
	LOD.MultiSizeIndexContainer.GetIndexBuffer(Idx);
	OutTris.Reserve(Idx.Num());
	for (uint32 I : Idx)
	{
		OutTris.Add(static_cast<int32>(I));
	}
	return OutTris.Num() >= 3;
}

bool OKDeformFromRef(FOkSession& Session, const OkMeshView& View, TArray<FVector>& OutVerts, bool bTransformVectors)
{
	OutVerts.Reset();
	if (!View.rest_positions || !View.weights || !View.bone_idx || View.n_verts <= 0)
	{
		return false;
	}
	const int32 NB = Session.SkelBoneCount();
	if (NB <= 0)
	{
		return false;
	}
	TArray<int32> Parent;
	TArray<FOkMat4> RestL;
	TArray<FOkMat4> PoseL;
	Parent.SetNum(NB);
	RestL.SetNum(NB);
	PoseL.SetNum(NB);
	for (int32 i = 0; i < NB; ++i)
	{
		OkBoneView Bone;
		FMemory::Memzero(Bone);
		if (!Session.SkelBoneAt(i, Bone))
		{
			Parent[i] = -1;
			RestL[i] = OkMatIdent();
			PoseL[i] = OkMatIdent();
			continue;
		}
		Parent[i] = Bone.parent;
		RestL[i] = OkMatTrs(Bone.rest_pos, Bone.rest_rot, Bone.rest_scl);
		PoseL[i] = OkMatTrs(Bone.pose_pos, Bone.pose_rot, Bone.pose_scl);
	}
	TArray<FOkMat4> RestW;
	TArray<FOkMat4> PoseW;
	RestW.SetNum(NB);
	PoseW.SetNum(NB);
	TArray<uint8> Done;
	Done.Init(0, NB);
	int32 Left = NB;
	for (int32 Pass = 0; Pass < NB && Left > 0; ++Pass)
	{
		for (int32 i = 0; i < NB; ++i)
		{
			if (Done[i])
			{
				continue;
			}
			const int32 P = Parent[i];
			if (P < 0 || P >= NB)
			{
				RestW[i] = RestL[i];
				PoseW[i] = PoseL[i];
				Done[i] = 1;
				--Left;
			}
			else if (Done[P])
			{
				RestW[i] = OkMatMul(RestW[P], RestL[i]);
				PoseW[i] = OkMatMul(PoseW[P], PoseL[i]);
				Done[i] = 1;
				--Left;
			}
		}
	}
	TArray<FOkMat4> Pal;
	Pal.SetNum(NB);
	for (int32 i = 0; i < NB; ++i)
	{
		FOkMat4 Inv;
		if (Done[i] && OkMatInv(RestW[i], Inv))
		{
			Pal[i] = OkMatMul(PoseW[i], Inv);
		}
		else
		{
			Pal[i] = OkMatIdent();
		}
	}
	OutVerts.SetNumUninitialized(View.n_verts);
	for (int32 v = 0; v < View.n_verts; ++v)
	{
		const float* P = View.rest_positions + v * 3;
		const float* Wf = View.weights + v * 4;
		const uint16* Bi = View.bone_idx + v * 4;
		float Acc[3] = {0.f, 0.f, 0.f};
		float Ws = 0.f;
		for (int32 k = 0; k < 4; ++k)
		{
			if (Wf[k] <= 1e-6f)
			{
				continue;
			}
			const int32 B = static_cast<int32>(Bi[k]);
			if (B < 0 || B >= NB)
			{
				continue;
			}
			float T[3];
			OkMatPoint(Pal[B], P, T);
			if (bTransformVectors)
			{
				const float Zero[3] = {0.f, 0.f, 0.f};
				float Translation[3]; OkMatPoint(Pal[B], Zero, Translation);
				for (int32 Axis = 0; Axis < 3; ++Axis) T[Axis] -= Translation[Axis];
			}
			Acc[0] += T[0] * Wf[k];
			Acc[1] += T[1] * Wf[k];
			Acc[2] += T[2] * Wf[k];
			Ws += Wf[k];
		}
		if (Ws > 1e-5f)
		{
			Acc[0] /= Ws;
			Acc[1] /= Ws;
			Acc[2] /= Ws;
		}
		else
		{
			Acc[0] = P[0];
			Acc[1] = P[1];
			Acc[2] = P[2];
		}
		OutVerts[v] = OkToUePos(Acc[0], Acc[1], Acc[2]);
	}
	return true;
}
