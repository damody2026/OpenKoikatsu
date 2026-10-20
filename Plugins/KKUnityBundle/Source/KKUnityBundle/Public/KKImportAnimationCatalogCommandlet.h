#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "KKImportAnimationCatalogCommandlet.generated.h"

UCLASS()
class UKKImportAnimationCatalogCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UKKImportAnimationCatalogCommandlet();
	virtual int32 Main(const FString& Params) override;
};
