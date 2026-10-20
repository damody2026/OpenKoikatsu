#pragma once
#include "CoreMinimal.h"
#include "Components/ListView.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "OKCardListView.generated.h"

class UOKCharacterController;
class UOKCardRowButton;
class UTextBlock;

UCLASS()
class OPENKOIKATSU_API UOKCardListItem : public UObject
{
 GENERATED_BODY()
public:
 TWeakObjectPtr<UOKCharacterController> Owner;
 int32 Index=INDEX_NONE;
 FString Path;
};

UCLASS()
class OPENKOIKATSU_API UOKCardListEntry : public UUserWidget, public IUserObjectListEntry
{
 GENERATED_BODY()
public:
 UPROPERTY() TObjectPtr<UOKCardListItem> Item;
 UPROPERTY() TObjectPtr<UOKCardRowButton> Button;
 UPROPERTY() TObjectPtr<UTextBlock> Label;
 void RefreshLabel();
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeOnListItemObjectSet(UObject* Object) override;
};

UCLASS()
class OPENKOIKATSU_API UOKCardListView : public UListView
{
 GENERATED_BODY()
public:
 UOKCardListView(const FObjectInitializer& Initializer);
};
