#pragma once

#include "CoreMinimal.h"
#include "OKComboBox.h"
#include "OKMotionComboBox.generated.h"

class SSearchBox;
template<typename ItemType> class SListView;

// Searchable popup on top of the shared readable combo style.
UCLASS()
class OPENKOIKATSU_API UOKMotionComboBox : public UOKComboBoxString
{
	GENERATED_BODY()
	friend class FOKMotionSearchTest;

public:
	UOKMotionComboBox(const FObjectInitializer& ObjectInitializer);
	void SetSearchText(const FText& Text);
	TArray<FString> GetFilteredOptions() const;
	void CommitSearchSelection(TSharedPtr<FString> Item, ESelectInfo::Type Type);
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> HandleGenerateWidget(TSharedPtr<FString> Item) const override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void HandleOpening() override;
	FReply HandleSearchKey(const FGeometry& Geometry, const FKeyEvent& Event);
	void HandleSearchCommitted(const FText& Text, ETextCommit::Type Type);
	FReply HandleListKey(const FGeometry& Geometry, const FKeyEvent& Event);
	void CloseSearchMenu();
	TArray<TSharedPtr<FString>> FilteredOptions;
	TMap<FString, FString> SearchLabels;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SListView<TSharedPtr<FString>>> SearchList;
	FString SearchText;
};
