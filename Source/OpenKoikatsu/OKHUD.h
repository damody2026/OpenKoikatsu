#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "OKHUD.generated.h"

UCLASS()
class OPENKOIKATSU_API AOKHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

	void SetGalleryVisible(bool bVisible);

	UPROPERTY()
	TObjectPtr<class UOKGalleryWidget> Gallery;
};
