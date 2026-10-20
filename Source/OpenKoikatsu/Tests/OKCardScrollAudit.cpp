#include "../OKGalleryWidget.h"
#include "../OKCardListView.h"
#include "../OKCardRowButton.h"
#include "KKPreviewActor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace {
struct FOKCardScrollAudit {
 TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UOKGalleryWidget> Gallery;
 FString Output,LastPath;int32 Stage=0,Count=0;double Start=FPlatformTime::Seconds();
 TArray<TSharedPtr<FJsonValue>> Cases;
 void Add(const TCHAR* Name){auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("case"),Name);C->SetBoolField(TEXT("valid"),true);Cases.Add(MakeShared<FJsonValueObject>(C));}
 bool Stop(const FString& Error){auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("valid"),Error.IsEmpty());R->SetStringField(TEXT("error"),Error);R->SetArrayField(TEXT("cases"),Cases);R->SetNumberField(TEXT("card_count"),Count);R->SetStringField(TEXT("selected_path"),LastPath);FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*Output);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);return false;}
 void Shot(const TCHAR* Name){FScreenshotRequest::RequestScreenshot(FPaths::GetPath(Output)/Name,true,false);}
 bool Tick(float){
  auto* W=World.Get();if(!W||FPlatformTime::Seconds()-Start>180)return Stop(TEXT("Card scroll audit timeout"));
  auto* G=Gallery.Get();if(!G){for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W&&It->CardListView){Gallery=*It;G=*It;break;}if(!G)return true;}
  if(Stage==0){
   if(G->CardListView->GetCachedGeometry().GetLocalSize().Y<100)return true;
   Count=G->Entries.Num();if(Count<81||G->CardListView->GetNumItems()!=Count)return Stop(TEXT("Whole multi-page directory missing from continuous list"));
   if(G->WidgetTree->FindWidget(TEXT("Prev"))||G->WidgetTree->FindWidget(TEXT("Next")))return Stop(TEXT("Pagination buttons still present"));
   const auto& Root=G->GetCachedGeometry();const auto& List=G->CardListView->GetCachedGeometry();const auto Bottom=Root.AbsoluteToLocal(List.LocalToAbsolute(List.GetLocalSize()));
   if(Bottom.Y>Root.GetLocalSize().Y+1)return Stop(TEXT("List extends below viewport instead of scrolling"));
   if(G->CardListView->GetDisplayedEntryWidgets().IsEmpty())return Stop(TEXT("List failed to create visible card rows"));
   if(G->CardListView->GetDisplayedEntryWidgets().Num()>=Count)return Stop(TEXT("List does not virtualize offscreen cards"));
   Add(TEXT("all_cards_in_bounded_virtualized_list_without_pagination"));Shot(TEXT("top.png"));Stage=1;return true;
  }
  if(Stage==1){G->CardListView->ScrollToBottom();Stage=2;return true;}
  if(Stage==2){
   UOKCardListEntry* Last=nullptr;for(auto* Entry:G->CardListView->GetDisplayedEntryWidgets())if(auto* Row=Cast<UOKCardListEntry>(Entry))if(Row->Item&&Row->Item->Index==Count-1)Last=Row;
   if(!Last||!Last->Button)return true;
   const auto& B=Last->Button->GetCachedGeometry();const auto& L=G->CardListView->GetCachedGeometry();const auto Center=L.AbsoluteToLocal(B.LocalToAbsolute(B.GetLocalSize()*.5));
   if(!Last->Button->GetIsEnabled()||B.GetLocalSize().Y<1||Center.Y<0||Center.Y>L.GetLocalSize().Y)return Stop(TEXT("Last card button is outside scroll viewport or disabled"));
   Add(TEXT("scroll_to_bottom_reaches_last_card_beyond_old_pages"));LastPath=G->Entries.Last().Path;Last->Button->HandleClick();
   if(G->SelectedIndex!=Count-1||G->Selected.OriginalBytes.IsEmpty())return Stop(TEXT("Recycled last row selected wrong card"));
   Add(TEXT("recycled_row_selects_exact_last_card"));Shot(TEXT("bottom.png"));Stage=3;return true;
  }
  if(Stage==3){G->OnLoad3D();Stage=4;return true;}
  if(Stage==4){if(G->bLoadInFlight)return true;if(!IsValid(G->PreviewActor)||!G->PreviewActor->Session.IsOpen()||G->SelectedIndex!=Count-1)return Stop(TEXT("Last card 3D load failed"));Add(TEXT("last_card_loads_in_character_editor"));G->SetModelLayout(false);G->CardListView->ScrollToTop();Stage=5;return true;}
  if(Stage==5){bool First=false;for(auto* Entry:G->CardListView->GetDisplayedEntryWidgets())if(auto* Row=Cast<UOKCardListEntry>(Entry))First|=Row->Item&&Row->Item->Index==0;if(!First)return true;Add(TEXT("scroll_back_to_first_card"));Shot(TEXT("report.png"));Stage=6;return true;}
  return Stop(FString());
 }
};
static FAutoConsoleCommandWithWorld CardScrollAudit(TEXT("ok.AuditCardScroll"),TEXT("Verify continuous virtualized card navigation"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){const FString Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GUI_RELOAD_OUTPUT"));if(!W||Output.IsEmpty())return;auto R=MakeShared<FOKCardScrollAudit>();R->World=W;R->Output=Output;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R](float D){return R->Tick(D);}));}));
}
#endif
