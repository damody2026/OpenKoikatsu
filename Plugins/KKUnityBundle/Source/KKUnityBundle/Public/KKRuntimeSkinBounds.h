#pragma once

#include "CoreMinimal.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "KKRuntimeSkinBounds.generated.h"

class UMorphTarget;
class FOkSession;
struct FSkelMeshRefPoseOverride;

// One explicit pose publication, including manual updates within a frame.
// Component scopes certify the exact input copied by OKApplySkelPose only.
class KKUNITYBUNDLE_API FKKRuntimeAffinePoseScope {
public:
 explicit FKKRuntimeAffinePoseScope(TArrayView<const FTransform> Pose,const USkinnedMeshComponent* Component=nullptr);
 ~FKKRuntimeAffinePoseScope();
 FKKRuntimeAffinePoseScope(const FKKRuntimeAffinePoseScope&)=delete;
 FKKRuntimeAffinePoseScope& operator=(const FKKRuntimeAffinePoseScope&)=delete;
private:
 TArrayView<const FTransform> PreviousInput;
 const USkinnedMeshComponent* PreviousComponent=nullptr;
 uint64 PreviousEpoch=0;
};

// Per-component immutable renderer snapshots; old snapshots may still be read
// on the render thread. Source local/reference poses are never modified.
struct FKKRuntimeAffinePalette
{
 TWeakObjectPtr<USkinnedAsset> Asset;
 TArray<FMatrix44f> ReferenceInverse,World;
 TSet<int32> UsedBones;
 TSet<int32> ExtraUsedBones;
 bool RequiredBonesDirty=false;
 TArray<int32> NeededBones;
 TArray<FTransform> PreviousLocals;
 TArray<FTransform> PreviousApproximate;
 TSharedPtr<FSkelMeshRefPoseOverride> Snapshot;
 TWeakObjectPtr<USkinnedAsset> DirectAsset;
 bool HasDirectPalette=false;
 TArray<FMatrix44f> DirectMatrices;
 TArray<FTransform> DirectCarriers;
 bool Publish(USkinnedMeshComponent& Component,TConstArrayView<FMatrix44f> RefToLocal,FString& Error);
 void ClearDirect();
 void Update(USkinnedMeshComponent& Component,TArrayView<const FTransform> Locals);
};

// Immutable reference envelopes, local to the mesh's own bone palette.
// Applying these envelopes costs O(used bones), not O(vertices), per pose.
struct FKKRuntimeSkinBounds
{
 TWeakObjectPtr<USkinnedAsset> Asset;
 TArray<int32> Bones;
 TArray<FBox3f> Reference;
 TMap<const UMorphTarget*,TArray<FBox3f>> Morphs;
 int32 VertexCount=0, MorphCount=-1;
 const void* WeightBuffer=nullptr;
 bool Calculate(const USkinnedMeshComponent& Component,const FTransform& LocalToWorld,FBoxSphereBounds& Result);
};

UCLASS()
class KKUNITYBUNDLE_API UKKRuntimePoseableMeshComponent : public UPoseableMeshComponent
{
 GENERATED_BODY()
public:
 virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
 virtual void FinalizeBoneTransform() override;
 virtual const TSharedPtr<FSkelMeshRefPoseOverride>& GetRefPoseOverride() const override;
 bool TryUseExactLeader(UKKRuntimePoseableMeshComponent* Leader);
 void VerifyExactLeader(TArrayView<const FTransform> Pose) const;
 UKKRuntimePoseableMeshComponent* VerifiedExactLeader() const;
 const TArray<FMatrix44f>& BoundsSkinPalette() const;
 // Render-only private rigs: native solved reference-to-local matrices are
 // already skin palettes. No source TRS decomposition or inverse is needed.
 // Rejection retains the previous successful publication.
 bool PublishExactSkinPalette(TConstArrayView<FMatrix44f> RefToLocal,FString& Error);
 bool PublishNativeSkinPalette(const FOkSession& Session,int32 Part,TConstArrayView<int32> Slots,FString& Error);
 void ClearExactSkinPalette();
private:
 friend class UKKRuntimeSkeletalMeshComponent;
 TWeakObjectPtr<UKKRuntimePoseableMeshComponent> ExactLeader,CandidateLeader;
 TWeakObjectPtr<USkinnedAsset> CandidateAsset,CandidateLeaderAsset;
 bool CandidateCompatible=false;
 mutable FKKRuntimeSkinBounds RuntimeBounds;
 FKKRuntimeAffinePalette AffinePalette;
 uint64 PosePublication=0;
 mutable uint64 BoundsPalettePublication=MAX_uint64;
 mutable TWeakObjectPtr<USkinnedAsset> BoundsPaletteAsset;
 mutable const FSkelMeshRefPoseOverride* BoundsPaletteOverride=nullptr;
 mutable TArray<FBoneIndexType> BoundsPaletteBones;
 mutable TArray<FMatrix44f> BoundsPalette;
};

UCLASS()
class KKUNITYBUNDLE_API UKKRuntimeSkeletalMeshComponent : public USkeletalMeshComponent
{
 GENERATED_BODY()
public:
 virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
 virtual void FinalizeBoneTransform() override;
 virtual const TSharedPtr<FSkelMeshRefPoseOverride>& GetRefPoseOverride() const override;
 bool TryUseExactLeader(UKKRuntimePoseableMeshComponent* Leader);
 void PublishOwnedMorphTargets();
 UKKRuntimePoseableMeshComponent* VerifiedExactLeader() const;
private:
 TWeakObjectPtr<UKKRuntimePoseableMeshComponent> ExactLeader,CandidateLeader;
 TWeakObjectPtr<USkinnedAsset> CandidateAsset,CandidateLeaderAsset;
 bool CandidateCompatible=false;
 mutable FKKRuntimeSkinBounds RuntimeBounds;
 FKKRuntimeAffinePalette AffinePalette;
};
