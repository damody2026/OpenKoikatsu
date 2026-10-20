#pragma once
#include "KKRuntimePoseBankTypes.h"
struct FKKRuntimeMeshInfluence {uint16 Bone=0,Weight=0;};
// One nonzero morph sample. Control is 0 mouth, 1 eyes, 2 eyebrow.
// Delta is bind space after capture; the native session is not required later.
struct FKKRuntimeFaceSparse {uint8 Control=0;uint16 Local=0;int32 Native=INDEX_NONE;FVector3f Delta=FVector3f::ZeroVector;};
struct FKKRuntimeFaceControl {TArray<int16> Close,Open;TArray<float> Baseline;};
struct FKKRuntimeFaceCatalog {FKKRuntimeFaceControl Control[3];};
struct FKKRuntimeMeshVertex {
 FVector3f Position,Normal,Tangent,InitialPosition;
 // Card-owned native mouth endpoints in this vertex's pre-skin reference space.
 FVector3f MouthClosed=FVector3f::ZeroVector,MouthOpen=FVector3f::ZeroVector;
 // Offset from the card-authored eye baseline to native pattern0 closed eyes.
 FVector3f BlinkClosed=FVector3f::ZeroVector;
 // Sparse generic face morphs. Catalog is shared by the part and survives reduction copies.
 TArray<FKKRuntimeFaceSparse> Face;
 TSharedPtr<const FKKRuntimeFaceCatalog,ESPMode::ThreadSafe> FaceCatalog;
 FVector2f UV[4];float TangentSign=1;
 FColor Color=FColor::White;
 FKKRuntimeMeshInfluence Skin[12];uint8 Influences=0;
};
// Plain owned data, valid after the native session closes. No UE asset build.
struct FKKRuntimeMeshPart {
 int32 NativePart=INDEX_NONE,ShaderKind=0;
 FString Name,Role,Blend;
 TArray<FKKRuntimeMeshVertex> Vertices;TArray<uint32> Indices;
};
// Validate every index first; rejection leaves every vertex binding unchanged.
KKRUNTIMEPOSE_API bool OKRemapRuntimeMeshBones(TArray<FKKRuntimeMeshPart>& Meshes,
 TConstArrayView<int32> OldToNew,int32 CompactBoneCount,FString& Error);
// Diagnostic: world-space GPU pose rows at the same initialization clip/time0.
KKRUNTIMEPOSE_API bool OKAuditRuntimeMeshInitialPose(TConstArrayView<FKKRuntimeMeshPart> Meshes,
 TConstArrayView<FVector4f> PoseRows,double& MaximumCm,FString& Error);

// Unity eye rotation is clockwise in UV space; inverse leaves output unchanged on error.
KKRUNTIMEPOSE_API bool OKRuntimeEyeExpressionUV(FVector2f Sample,float Turns,FVector2f& Out);
