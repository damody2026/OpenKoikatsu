#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "OKCardRowButton.generated.h"

class UOKCharacterController;

UCLASS()
class OPENKOIKATSU_API UOKCardRowButton : public UButton
{
	GENERATED_BODY()

public:
	int32 Index = INDEX_NONE;

	UPROPERTY()
	TObjectPtr<UOKCharacterController> Owner;

	UFUNCTION()
	void HandleClick();
};
