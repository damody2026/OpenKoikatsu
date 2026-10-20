#pragma once
#include "CoreMinimal.h"
#include "Components/SpinBox.h"
#include "Components/EditableTextBox.h"
#include "OKComboBox.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "OKPropertyInput.generated.h"
class UOKCharacterController;
UCLASS()
class OPENKOIKATSU_API UOKPropertyInput : public USpinBox {
 GENERATED_BODY()
public:
 UPROPERTY() TObjectPtr<UOKCharacterController> Owner;
 FString Key;
 int32 Generation=0;
 bool bBoolean=false;
 UFUNCTION() void Changed(float NewValue);
};
UCLASS()
class OPENKOIKATSU_API UOKPropertyParent : public UEditableTextBox {
 GENERATED_BODY()
public:
 UPROPERTY() TObjectPtr<UOKCharacterController> Owner;
 FString Key;
 int32 Generation=0;
 UFUNCTION() void Committed(const FText& Value,ETextCommit::Type Type);
};
UCLASS()
class OPENKOIKATSU_API UOKPropertyReset : public UButton {
 GENERATED_BODY()
public:
 UPROPERTY() TObjectPtr<UOKCharacterController> Owner;
 FString Key;
 UFUNCTION() void Reset();
};
UCLASS()
class OPENKOIKATSU_API UOKPropertyParentChoice : public UOKComboBoxString {
 GENERATED_BODY()
public:
 UPROPERTY() TObjectPtr<UOKPropertyParent> Input;
 TArray<FString> Keys;
 UFUNCTION() void Selected(FString Option,ESelectInfo::Type SelectionType);
};
/** A fixed list of string values (kind "choice", e.g. the per-component physics solver); Values[i] belongs to option i. */
UCLASS()
class OPENKOIKATSU_API UOKPropertyChoice : public UOKComboBoxString {
 GENERATED_BODY()
public:
 UPROPERTY() TObjectPtr<UOKCharacterController> Owner;
 FString Key;
 int32 Generation=0;
 TArray<FString> Values;
 UFUNCTION() void Selected(FString Option,ESelectInfo::Type SelectionType);
};
UCLASS()
class OPENKOIKATSU_API UOKPropertyToggle : public UCheckBox {
 GENERATED_BODY()
public:
 UPROPERTY() TObjectPtr<UOKCharacterController> Owner;
 FString Key;
 int32 Generation=0;
 UFUNCTION() void Changed(bool bNewChecked);
};
