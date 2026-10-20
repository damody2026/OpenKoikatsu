#pragma once
#include "CoreMinimal.h"
#include "Components/Button.h"
#include "OKAppearanceButton.generated.h"
UCLASS()
class UOKAppearanceButton:public UButton {
 GENERATED_BODY()
public:
 UPROPERTY() TObjectPtr<class UOKCharacterController> Owner;
 UPROPERTY() TObjectPtr<class UImage> Thumbnail;
 UPROPERTY() TObjectPtr<class UTextBlock> ThumbnailStatus;
 FString ItemKey;
 UFUNCTION() void SelectItem();
};
