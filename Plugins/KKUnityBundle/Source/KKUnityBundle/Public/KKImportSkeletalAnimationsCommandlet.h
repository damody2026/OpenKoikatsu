#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "KKImportSkeletalAnimationsCommandlet.generated.h"
UCLASS()
class UKKImportSkeletalAnimationsCommandlet:public UCommandlet {
 GENERATED_BODY()
public:
 UKKImportSkeletalAnimationsCommandlet();
 virtual int32 Main(const FString& Params) override;
};
