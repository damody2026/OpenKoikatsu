#pragma once

#include "CoreMinimal.h"
#include "Components/Slider.h"
#include "Components/ComboBoxString.h"
#include "OKShapeSlider.generated.h"

class UOKCharacterController;
class UTextBlock;
class UComboBoxString;
class UButton;

UCLASS()
class OPENKOIKATSU_API UOKShapeSlider : public USlider
{
	GENERATED_BODY()

public:
	int32 Index = INDEX_NONE;
	bool bBody = false;
	FString AnimationParameter;
 FString ModBone, ModRight, ModCategory, PregnancyParameter, BodyParameter;
 int32 ModAxis=-1;
 bool ModCoordinateSpecific=false;
 UPROPERTY() TObjectPtr<UComboBoxString> ModSide;
 UPROPERTY() TObjectPtr<UWidget> ModRow;
 UPROPERTY() TObjectPtr<UButton> ModReset;
 UFUNCTION() void OnModSideChanged(FString SelectedSide, ESelectInfo::Type Type);

	UPROPERTY()
	TObjectPtr<UOKCharacterController> Owner;

	UPROPERTY()
	TObjectPtr<UTextBlock> ValueLabel;

	void RefreshValueLabel();

	UFUNCTION()
	void OnSliderMoved(float NewValue);

	UFUNCTION()
	void OnResetClicked();
};
