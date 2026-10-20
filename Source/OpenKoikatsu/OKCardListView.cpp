#include "OKCardListView.h"
#include "OKGalleryWidget.h"
#include "OKCardRowButton.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "UObject/ConstructorHelpers.h"

UOKCardListView::UOKCardListView(const FObjectInitializer& Initializer):Super(Initializer)
{
#if WITH_EDITOR
 static ConstructorHelpers::FClassFinder<UUserWidget> Entry(TEXT("/Game/UI/WBP_CardListEntry"));
 EntryWidgetClass=Entry.Class;
 SetSelectionMode(ESelectionMode::None);
 SetScrollbarVisibility(ESlateVisibility::Visible);
#endif
}

TSharedRef<SWidget> UOKCardListEntry::RebuildWidget()
{
 if(WidgetTree&&!WidgetTree->RootWidget){
  auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetHeightOverride(32);
  Button=WidgetTree->ConstructWidget<UOKCardRowButton>();
  Label=WidgetTree->ConstructWidget<UTextBlock>();
  Label->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",16));
  Button->SetContent(Label);Size->SetContent(Button);WidgetTree->RootWidget=Size;
  Button->OnClicked.AddDynamic(Button,&UOKCardRowButton::HandleClick);
 }
 RefreshLabel();return Super::RebuildWidget();
}

void UOKCardListEntry::NativeOnListItemObjectSet(UObject* Object)
{
 IUserObjectListEntry::NativeOnListItemObjectSet(Object);
 Item=Cast<UOKCardListItem>(Object);RefreshLabel();
}

void UOKCardListEntry::RefreshLabel()
{
 if(!Button||!Label)return;
 auto* G=Item?Item->Owner.Get():nullptr;
 const bool Valid=G&&G->Entries.IsValidIndex(Item->Index)&&G->CardListView&&G->CardListView->GetItemAt(Item->Index)==Item;
 Button->Owner=Valid?G:nullptr;Button->Index=Valid?Item->Index:INDEX_NONE;
 if(!Valid){Label->SetText(FText::GetEmpty());return;}
 const auto& E=G->Entries[Item->Index];
 const TCHAR* Kind=E.Kind==EKKCardKind::KKS?TEXT("KKS"):E.Kind==EKKCardKind::KK?TEXT("KK"):E.Kind==EKKCardKind::KKSP?TEXT("KKSP"):TEXT("卡片");
 Label->SetText(FText::FromString(FString::Printf(TEXT("%s %s  [%s]  %s"),*E.LastName,*E.FirstName,Kind,*E.FileName)));
}
