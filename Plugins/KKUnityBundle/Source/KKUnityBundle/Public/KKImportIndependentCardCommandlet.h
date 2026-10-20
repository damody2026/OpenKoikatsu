#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "KKImportIndependentCardCommandlet.generated.h"
UCLASS()
class KKUNITYBUNDLE_API UKKImportIndependentCardCommandlet:public UCommandlet {
 GENERATED_BODY()
public:
 UKKImportIndependentCardCommandlet();
 virtual int32 Main(const FString& Params) override;
};
