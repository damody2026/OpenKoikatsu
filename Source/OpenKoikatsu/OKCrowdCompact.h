#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "OKCrowdCompact.generated.h"
class USkeletalMesh;
#if WITH_EDITOR
USkeletalMesh* OKCompactCrowdMesh(USkeletalMesh* Source,const FString& Path,FString& Error,const TSet<FName>& AdditionalBones);
#endif

UCLASS()
class OPENKOIKATSU_API UOKCompactCrowdCommandlet : public UCommandlet {
 GENERATED_BODY()
public:
 UOKCompactCrowdCommandlet();
 virtual int32 Main(const FString& Params) override;
};
