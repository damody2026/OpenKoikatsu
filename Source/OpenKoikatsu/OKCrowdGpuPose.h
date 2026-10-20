#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/AssetUserData.h"
#include "OKCrowdGpuPose.generated.h"
class USkeletalMesh;
class UTransformProviderData;
UCLASS()
class UOKCrowdMatrixData : public UAssetUserData {
 GENERATED_BODY()
public:
 UPROPERTY() TArray<float> CrowdMatrixRows;
 UPROPERTY() int32 CrowdMatrixFrames=0;
 UPROPERTY() float CrowdMatrixDuration=0;
};
USTRUCT()
struct FOKCrowdGpuPart {
 GENERATED_BODY()
 UPROPERTY() TObjectPtr<USkeletalMesh> Mesh;
 UPROPERTY() TObjectPtr<UTransformProviderData> Provider;
 UPROPERTY() FTransform Transform;
};
// A selected card's private walk resources. No material or section merging.
UCLASS()
class UOKCrowdGpuPose : public UDataAsset {
 GENERATED_BODY()
public:
 UPROPERTY() FString SourceHash;
 UPROPERTY() TArray<FOKCrowdGpuPart> Parts;
};
int32 OKPrepareCrowdGpuPose(int32 Start,int32 End,const FString& Params);
