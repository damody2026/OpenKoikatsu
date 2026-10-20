#include "OKMotionComboBox.h"
#include "OKMotionLabelZh.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/SListView.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"

UOKMotionComboBox::UOKMotionComboBox(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Colours, font and row style come from UOKComboBoxString.
	SetMaxListHeight(420.f);
}

TSharedRef<SWidget> UOKMotionComboBox::HandleGenerateWidget(TSharedPtr<FString> Item) const
{
	// UComboBoxString's default text can take a black color from the global style.
	// Explicit color covers both the selected label and every generated popup row.
	const FString Label = OKMotionLabelDisplay(Item.IsValid() ? *Item : FString());
	return SNew(STextBlock)
		.Text(FText::FromString(Label))
		.Font(GetFont())
		.ColorAndOpacity(FLinearColor::White);
}

TSharedRef<SWidget> UOKMotionComboBox::RebuildWidget()
{
	TSharedRef<SWidget> Control = Super::RebuildWidget();
	SetSearchText(FText::GetEmpty());
	MyComboBox->SetMenuContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
		[
			SAssignNew(SearchBox, SSearchBox)
			.HintText(FText::FromString(TEXT("搜尋名稱、分類或來源…")))
			.DelayChangeNotificationsWhileTyping(false)
			.OnTextChanged(BIND_UOBJECT_DELEGATE(FOnTextChanged, SetSearchText))
			.OnTextCommitted(BIND_UOBJECT_DELEGATE(FOnTextCommitted, HandleSearchCommitted))
			.OnKeyDownHandler(BIND_UOBJECT_DELEGATE(FOnKeyDown, HandleSearchKey))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MaxDesiredHeight(GetMaxListHeight())
			[
				SAssignNew(SearchList, SListView<TSharedPtr<FString>>)
				.ListItemsSource(&FilteredOptions)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow_Lambda([this](TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner)
				{
					return SNew(STableRow<TSharedPtr<FString>>, Owner).Style(&GetItemStyle()).Padding(FMargin(4.f))
						[HandleGenerateWidget(Item)];
				})
				.OnMouseButtonClick_Lambda([this](TSharedPtr<FString> Item)
				{
					CommitSearchSelection(Item, ESelectInfo::OnMouseClick);
				})
				.OnKeyDownHandler(BIND_UOBJECT_DELEGATE(FOnKeyDown, HandleListKey))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
		[
			SNew(STextBlock).ColorAndOpacity(FLinearColor::White)
			.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%d / %d"), FilteredOptions.Num(), Options.Num())); })
		]);
	MyComboBox->SetMenuContentWidgetToFocus(SearchBox);
	return Control;
}

void UOKMotionComboBox::HandleOpening()
{
	Super::HandleOpening();
	// Options can be replaced by asynchronous source preparation. Re-read the
	// authoritative array on every opening; never clear/reindex UMG's options.
	// Display translation depends only on the raw string. Keep cached labels for
	// unchanged options across openings, but prune removed sources.
	TSet<FString> Live;
	for (const auto& Item : Options) if (Item) Live.Add(*Item);
	for (auto It = SearchLabels.CreateIterator(); It; ++It)
		if (!Live.Contains(It.Key())) It.RemoveCurrent();
	SetSearchText(FText::GetEmpty());
	if (SearchBox) SearchBox->SetText(FText::GetEmpty());
	if (SearchList && FilteredOptions.Contains(CurrentOptionPtr))
	{
		SearchList->SetSelection(CurrentOptionPtr, ESelectInfo::Direct);
		SearchList->RequestScrollIntoView(CurrentOptionPtr);
	}
}

FReply UOKMotionComboBox::HandleSearchKey(const FGeometry&, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape)
	{
		CloseSearchMenu();
		return FReply::Handled();
	}
	if ((Event.GetKey() == EKeys::Down || Event.GetKey() == EKeys::Up) && SearchList && FilteredOptions.Num())
	{
		const auto Item = Event.GetKey() == EKeys::Down ? FilteredOptions[0] : FilteredOptions.Last();
		SearchList->SetSelection(Item, ESelectInfo::OnNavigation);
		SearchList->RequestScrollIntoView(Item);
		return FReply::Handled().SetUserFocus(SearchList.ToSharedRef(), EFocusCause::Navigation);
	}
	return FReply::Unhandled();
}

void UOKMotionComboBox::HandleSearchCommitted(const FText& Text, ETextCommit::Type Type)
{
	if (Type == ETextCommit::OnEnter)
	{
		SetSearchText(Text);
		if (FilteredOptions.Num()) CommitSearchSelection(FilteredOptions[0], ESelectInfo::OnKeyPress);
	}
}

FReply UOKMotionComboBox::HandleListKey(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape) return HandleSearchKey(Geometry, Event);
	if (Event.GetKey() == EKeys::Enter)
	{
		const auto Items = SearchList->GetSelectedItems();
		if (Items.Num()) CommitSearchSelection(Items[0], ESelectInfo::OnKeyPress);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void UOKMotionComboBox::SetSearchText(const FText& Text)
{
	SearchText = Text.ToString();
	TArray<FString> Terms;
	SearchText.ParseIntoArrayWS(Terms);
	FilteredOptions.Reset();
	for (const auto& Item : Options)
	{
		if (!Item) continue;
		bool Match = true;
		if (Terms.Num())
		{
			FString* Label = SearchLabels.Find(*Item);
			if (!Label) Label = &SearchLabels.Add(*Item, *Item + TEXT("\n") + OKMotionLabelDisplay(*Item));
			for (const FString& Term : Terms)
				if (!Label->Contains(Term, ESearchCase::IgnoreCase)) { Match = false; break; }
		}
		if (Match) FilteredOptions.Add(Item);
	}
	if (SearchList) SearchList->RequestListRefresh();
}

TArray<FString> UOKMotionComboBox::GetFilteredOptions() const
{
	TArray<FString> Result;
	for (const auto& Item : FilteredOptions) Result.Add(*Item);
	return Result;
}

void UOKMotionComboBox::CommitSearchSelection(TSharedPtr<FString> Item, ESelectInfo::Type Type)
{
	// Navigation and list pruning are not playback requests. Only explicit
	// mouse/Enter commits use the existing full-key UMG selection path.
	if (!GetIsEnabled() || !Item || (Type != ESelectInfo::OnMouseClick && Type != ESelectInfo::OnKeyPress)
		|| !FilteredOptions.Contains(Item) || !Options.Contains(Item)) return;
	SetSelectedOption(*Item);
	CloseSearchMenu();
}

void UOKMotionComboBox::CloseSearchMenu()
{
	if (!MyComboBox) return;
	// The base combo knows only its original list. Restore focus for users in
	// our replacement popup so Escape/Enter cannot leave focus in a closed menu.
	FSlateApplication::Get().ForEachUser([this](FSlateUser& User)
	{
		if (User.IsWidgetInFocusPath(SearchBox) || User.IsWidgetInFocusPath(SearchList))
			User.SetFocus(MyComboBox.ToSharedRef());
	});
	MyComboBox->SetIsOpen(false);
}

void UOKMotionComboBox::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	SearchBox.Reset();
	SearchList.Reset();
	FilteredOptions.Reset();
	SearchLabels.Reset();
}
