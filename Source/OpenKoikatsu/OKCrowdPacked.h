#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "OKCrowdPacked.generated.h"
UCLASS()
class OPENKOIKATSU_API UOKPackCrowdCommandlet : public UCommandlet {
 GENERATED_BODY()
public:
 UOKPackCrowdCommandlet();
 virtual int32 Main(const FString& Params) override;
};
