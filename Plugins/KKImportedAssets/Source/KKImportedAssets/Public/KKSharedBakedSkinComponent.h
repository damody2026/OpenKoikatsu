#pragma once
#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "KKSharedBakedSkinComponent.generated.h"

// Every imported part keeps its own skin weights and inverse bind matrices.
// Followers contribute their private palette bones to the one pose evaluator.
UCLASS()
class KKIMPORTEDASSETS_API UKKSharedBakedSkinComponent : public USkeletalMeshComponent {
 GENERATED_BODY()
public:
 virtual void GetAdditionalRequiredBonesForLeader(int32 LODIndex,TArray<FBoneIndexType>& Bones) const override;
};
