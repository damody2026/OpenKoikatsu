#pragma once
#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "KKImportedPoseComponent.generated.h"

// Preserve SkeletalMeshComponent's morph processing while supplying an exact
// component-space pose, including rank-one affine palette bones.
UCLASS()
class KKIMPORTEDASSETS_API UKKImportedPoseComponent : public USkeletalMeshComponent {
 GENERATED_BODY()
public:
 bool SetImportedPose(const TArray<FTransform>& Pose);
 virtual void RefreshBoneTransforms(FActorComponentTickFunction* TickFunction=nullptr) override;
private:
 TArray<FTransform> ImportedPose;
};
