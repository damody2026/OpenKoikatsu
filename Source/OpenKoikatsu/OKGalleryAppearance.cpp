#include "OKGalleryWidget.h"
#include "OKComboBox.h"
#include "OKAppearanceButton.h"
#include "OKAppearanceCardJob.h"
#include "KKGameRoot.h"
#include "KKCardReader.h"
#include "KKCardWorkspace.h"
#include "KKPreviewActor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Async/Async.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

namespace {
void CopyPendingHeadEdits(const FKKCharacterFile& Source,FKKCharacterFile& Destination){
 Destination.LastName=Source.LastName;Destination.FirstName=Source.FirstName;Destination.Nickname=Source.Nickname;
 Destination.Sex=Source.Sex;Destination.Personality=Source.Personality;Destination.VoiceRate=Source.VoiceRate;
 Destination.Face.ShapeValueFace=Source.Face.ShapeValueFace;Destination.Body=Source.Body;
}
UButton* Button(UWidgetTree* Tree,UHorizontalBox* Row,const TCHAR* Label){auto* B=Tree->ConstructWidget<UButton>();auto* T=Tree->ConstructWidget<UTextBlock>();T->SetText(FText::FromString(Label));T->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",16));B->SetContent(T);Row->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));return B;}
UTexture2D* Thumbnail(const FString& Path){TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Path))return nullptr;auto& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));auto Wrapper=Module.CreateImageWrapper(EImageFormat::PNG);if(!Wrapper||!Wrapper->SetCompressed(Bytes.GetData(),Bytes.Num())||Wrapper->GetWidth()>2048||Wrapper->GetHeight()>2048)return nullptr;TArray64<uint8> Raw;if(!Wrapper->GetRaw(ERGBFormat::BGRA,8,Raw))return nullptr;auto* Texture=UTexture2D::CreateTransient(Wrapper->GetWidth(),Wrapper->GetHeight(),PF_B8G8R8A8);if(!Texture)return nullptr;void* Data=Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);FMemory::Memcpy(Data,Raw.GetData(),Raw.Num());Texture->GetPlatformData()->Mips[0].BulkData.Unlock();Texture->UpdateResource();return Texture;}
}
void UOKAppearanceButton::SelectItem(){if(Owner)Owner->ApplyAppearanceItem(ItemKey);}
void UOKCharacterController::BuildAppearanceControls(UVerticalBox* Parent){auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Button(WidgetTree,Row,TEXT("圖片式外觀選擇"))->OnClicked.AddDynamic(this,&UOKCharacterController::OnOpenAppearance);Button(WidgetTree,Row,TEXT("新增飾品欄位"))->OnClicked.AddDynamic(this,&UOKCharacterController::OnAddAccessorySlot);Parent->AddChild(Row);BuildPropertyControls(Parent);}
void UOKCharacterController::BuildAppearancePanel(){
 AppearancePanel=WidgetTree->ConstructWidget<USizeBox>();AppearancePanel->SetWidthOverride(560);AppearancePanel->SetVisibility(ESlateVisibility::Collapsed);
 auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrushColor(FLinearColor(.035,.10,.14,1));AppearancePanel->SetContent(Border);auto* Col=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Col);
 auto* Nav=WidgetTree->ConstructWidget<UHorizontalBox>();Button(WidgetTree,Nav,TEXT("隨機"))->OnClicked.AddDynamic(this,&UOKCharacterController::OnAppearanceRandom);Button(WidgetTree,Nav,TEXT("重置類型"))->OnClicked.AddDynamic(this,&UOKCharacterController::OnAppearanceReset);Button(WidgetTree,Nav,TEXT("關閉"))->OnClicked.AddDynamic(this,&UOKCharacterController::OnCloseAppearance);Col->AddChild(Nav);
 AppearanceTarget=WidgetTree->ConstructWidget<UOKComboBoxString>();AppearanceTarget->OnSelectionChanged.AddDynamic(this,&UOKCharacterController::OnAppearanceTarget);Col->AddChild(AppearanceTarget);
 AppearanceSearch=WidgetTree->ConstructWidget<UEditableTextBox>();AppearanceSearch->SetHintText(FText::FromString(TEXT("搜尋名稱、編號或模組來源")));AppearanceSearch->OnTextChanged.AddDynamic(this,&UOKCharacterController::OnAppearanceSearch);Col->AddChild(AppearanceSearch);
 AppearanceStatus=WidgetTree->ConstructWidget<UTextBlock>();AppearanceStatus->SetAutoWrapText(true);AppearanceStatus->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",16));Col->AddChild(AppearanceStatus);
 auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();AppearanceGrid=WidgetTree->ConstructWidget<UWrapBox>();AppearanceGrid->SetInnerSlotPadding(FVector2D(4,4));Scroll->AddChild(AppearanceGrid);Col->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 auto* PageNav=WidgetTree->ConstructWidget<UHorizontalBox>();Button(WidgetTree,PageNav,TEXT("上一頁"))->OnClicked.AddDynamic(this,&UOKCharacterController::OnAppearancePrev);AppearancePageText=WidgetTree->ConstructWidget<UTextBlock>();PageNav->AddChild(AppearancePageText);Button(WidgetTree,PageNav,TEXT("下一頁"))->OnClicked.AddDynamic(this,&UOKCharacterController::OnAppearanceNext);Col->AddChild(PageNav);
}
void UOKCharacterController::RunAppearanceRequest(TSharedRef<FJsonObject> Request,TFunction<void(UOKCharacterController*,TSharedPtr<FJsonObject>,const FString&)> Complete,TSharedPtr<FOKAppearanceCardJob,ESPMode::ThreadSafe> CardJob){
 const FString Folder=FPaths::ProjectSavedDir()/TEXT("AppearanceEditor");IFileManager::Get().MakeDirectory(*Folder,true);const FString Id=FGuid::NewGuid().ToString(EGuidFormats::Digits),Input=Folder/(Id+TEXT(".request.json")),Output=Folder/(Id+TEXT(".report.json"));
 Request->SetStringField(TEXT("cache"),Folder/TEXT("cache"));FString Text;FJsonSerializer::Serialize(Request,TJsonWriterFactory<>::Create(&Text));if(!FFileHelper::SaveStringToFile(Text,*Input,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)){Complete(this,nullptr,TEXT("無法寫入圖片選擇請求"));return;}
 TWeakObjectPtr<UOKCharacterController> Weak(this);const FString Root=AppearanceRoot;
 FString CardInput,CardOutput,RequestTarget;Request->TryGetStringField(TEXT("card"),CardInput);Request->TryGetStringField(TEXT("output"),CardOutput);Request->TryGetStringField(TEXT("target"),RequestTarget);
 const bool bCatalog=Request->GetStringField(TEXT("action"))==TEXT("catalog");
 const int32 ContextSelection=SelectedIndex,ContextGeneration=LoadGeneration;
 double RequestOutfit=0;Request->TryGetNumberField(TEXT("outfit"),RequestOutfit);TArray<uint8> RequestClothes;RequestClothes.Append(ClothesStates,9);
 const double Queued=FPlatformTime::Seconds();
 Async(EAsyncExecution::ThreadPool,[Weak,Input,Output,Root,CardInput,CardOutput,RequestTarget,RequestOutfit,RequestClothes,CardJob,Queued,bCatalog,ContextSelection,ContextGeneration,Complete=MoveTemp(Complete)]() mutable {
  if(CardJob)CardJob->QueueMs=(FPlatformTime::Seconds()-Queued)*1000.;
  FString Error;TSharedPtr<FJsonObject> Result;
  auto Execute=[&](FString& Failure){FString Text;if(!FOkSession::AppearanceRequest(Root,Input,Output,Failure))return false;
   if(!FFileHelper::LoadFileToString(Text,*Output)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Result)||!Result){Failure=TEXT("無法讀取圖片選擇結果");return false;}bool Valid=true;if(Result->TryGetBoolField(TEXT("valid"),Valid)&&!Valid){Failure=TEXT("圖片選擇結果未通過驗證");return false;}return true;};
  const bool Succeeded=CardJob?CardJob->Run(CardInput,CardOutput,Execute,Error):Execute(Error);
  if(!Succeeded)Result.Reset();
  TSharedPtr<FOkAppearanceContext,ESPMode::ThreadSafe> PreparedContext;
  const bool bRebase=Succeeded&&(bCatalog||(CardJob&&!CardOutput.IsEmpty()));
  const FString ContextPath=bCatalog?CardInput:CardOutput;
  if(bRebase){
   PreparedContext=MakeShared<FOkAppearanceContext,ESPMode::ThreadSafe>();FString PrepareError;
   if(!PreparedContext->Open(Root,ContextPath,FPaths::ProjectContentDir()/TEXT("IndependentResources/v1"),FPaths::ProjectSavedDir()/TEXT("AppearanceEditor/cache"),int32(RequestOutfit),PrepareError)){
    UE_LOG(LogTemp,Display,TEXT("Appearance context unavailable, retaining full preparation: %s"),*PrepareError);PreparedContext.Reset();
   }
  }
  if(Succeeded&&CardJob&&!CardOutput.IsEmpty()&&(RequestTarget==TEXT("eyebrow")||RequestTarget.StartsWith(TEXT("eyeline_"))||
      RequestTarget.StartsWith(TEXT("eye_hi_"))||RequestTarget==TEXT("eye_white")||RequestTarget==TEXT("nose")||
      RequestTarget.StartsWith(TEXT("pupil"))||RequestTarget.StartsWith(TEXT("gradation")))){
   const double PrepareStart=FPlatformTime::Seconds();auto Prepared=MakeShared<FOkSession,ESPMode::ThreadSafe>();FString PrepareError;
   // CardOutput is an already-built portable APNG. A game root selects the
   // original source extractor, rebuilding the character instead of loading
   // the exact snapshot we have just written.
   if(Prepared->Open(FString(),CardOutput,int32(RequestOutfit),PrepareError,RequestClothes.GetData()))CardJob->AppearanceSession=MoveTemp(Prepared);
   else UE_LOG(LogTemp,Display,TEXT("Appearance incremental preparation fallback: %s"),*PrepareError);
   CardJob->PreviewPrepareMs=(FPlatformTime::Seconds()-PrepareStart)*1000.;
  }
  if(CardJob){auto Timing=MakeShared<FJsonObject>();Timing->SetBoolField(TEXT("valid"),Succeeded);Timing->SetBoolField(TEXT("worker_thread"),!IsInGameThread());Timing->SetStringField(TEXT("card"),CardInput);Timing->SetStringField(TEXT("output"),CardOutput);Timing->SetStringField(TEXT("error"),Error);Timing->SetNumberField(TEXT("snapshot_ms"),CardJob->SnapshotMs);Timing->SetNumberField(TEXT("queue_ms"),CardJob->QueueMs);Timing->SetNumberField(TEXT("input_save_ms"),CardJob->SaveMs);Timing->SetNumberField(TEXT("native_request_ms"),CardJob->RequestMs);Timing->SetNumberField(TEXT("output_read_ms"),CardJob->ReadMs);FString Text;FJsonSerializer::Serialize(Timing,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Input+TEXT(".timing.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
  AsyncTask(ENamedThreads::GameThread,[Weak,Result,Error,CardJob,PreparedContext,bRebase,bCatalog,ContextPath,RequestOutfit,ContextSelection,ContextGeneration,Complete=MoveTemp(Complete)]() mutable {if(auto* G=Weak.Get()){
   const bool Current=ContextSelection==G->SelectedIndex&&ContextGeneration==G->LoadGeneration&&int32(RequestOutfit)==G->OutfitIndex;
   Complete(G,Result,Error);
   // A complete geometry transaction may increment LoadGeneration while
   // publishing its new working card. Rebase after that publication so later
   // texture selections never use the previous geometry/source revision.
   if(Current&&bRebase&&(bCatalog||G->Entries[G->SelectedIndex].Path==ContextPath)){
    G->AppearanceContext=PreparedContext;G->AppearanceContextPath=ContextPath;G->AppearanceContextGeneration=G->LoadGeneration;G->AppearanceContextOutfit=int32(RequestOutfit);
   }
  }});});
}
void UOKCharacterController::OnOpenAppearance(){
 if(!bModelLayout||bLoadInFlight||bAppearanceBusy||bPropertyBusy||!Entries.IsValidIndex(SelectedIndex))return;AppearancePanel->SetVisibility(ESlateVisibility::Visible);if(MotionPanel)MotionPanel->SetVisibility(ESlateVisibility::Collapsed);
 RefreshAppearanceCatalog();
}
void UOKCharacterController::RefreshAppearanceCatalog(bool bSelectNewestSlot){
 if(MaterializeAppearance([bSelectNewestSlot](UOKCharacterController* G){G->RefreshAppearanceCatalog(bSelectNewestSlot);}))return;
 auto* Root=UKKGameRoot::Get();AppearanceRoot=Root?Root->Root:FString();
 if(AppearanceRoot.IsEmpty()||!IFileManager::Get().DirectoryExists(*(AppearanceRoot/TEXT("abdata")))){AppearanceStatus->SetText(FText::FromString(TEXT("請先設定遊戲目錄，才能讀取新增外觀的原始素材。")));return;}
 bAppearanceBusy=true;AppearanceStatus->SetText(FText::FromString(TEXT("正在建立遊戲與模組圖片目錄，首次讀取需要較長時間…")));auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("action"),TEXT("catalog"));R->SetStringField(TEXT("card"),Entries[SelectedIndex].Path);R->SetNumberField(TEXT("outfit"),OutfitIndex);
 const int32 Selection=SelectedIndex,Outfit=OutfitIndex,Generation=LoadGeneration;
 RunAppearanceRequest(R,[Selection,Outfit,Generation,bSelectNewestSlot](UOKCharacterController* G,TSharedPtr<FJsonObject> Data,const FString& Error){G->bAppearanceBusy=false;if(Selection!=G->SelectedIndex||Outfit!=G->OutfitIndex||Generation!=G->LoadGeneration)return;if(!Data){G->AppearanceStatus->SetText(FText::FromString(TEXT("外觀目錄讀取失敗：")+Error));return;}
  G->AppearanceTargets.Reset();G->AppearanceItems.Reset();G->AppearanceTarget->ClearOptions();
  for(auto V:Data->GetArrayField(TEXT("targets")))G->AppearanceTargets.Add(V->AsObject());for(auto V:Data->GetArrayField(TEXT("items")))G->AppearanceItems.Add(V->AsObject());
  FString Chosen=TEXT("眉毛種類");for(auto T:G->AppearanceTargets){G->AppearanceTarget->AddOption(T->GetStringField(TEXT("label")));if(bSelectNewestSlot&&T->GetStringField(TEXT("key")).StartsWith(TEXT("accessory"))&&T->GetIntegerField(TEXT("category"))==121)Chosen=T->GetStringField(TEXT("label"));}G->AppearanceTarget->SetSelectedOption(Chosen);
 });
}
void UOKCharacterController::OnCloseAppearance(){if(MaterializeAppearance([](UOKCharacterController* G){G->OnCloseAppearance();}))return;if(AppearancePanel)AppearancePanel->SetVisibility(ESlateVisibility::Collapsed);if(MotionPanel)MotionPanel->SetVisibility(bModelLayout?ESlateVisibility::Visible:ESlateVisibility::Collapsed);WarmClothingPreviews();}
void UOKCharacterController::OnAppearanceTarget(FString Value,ESelectInfo::Type){for(auto T:AppearanceTargets)if(T->GetStringField(TEXT("label"))==Value){AppearanceTargetKey=T->GetStringField(TEXT("key"));break;}AppearancePage=0;RebuildAppearancePage();}
void UOKCharacterController::OnAppearanceSearch(const FText&){AppearancePage=0;RebuildAppearancePage();}
void UOKCharacterController::OnAppearancePrev(){AppearancePage=FMath::Max(0,AppearancePage-1);RebuildAppearancePage();}
void UOKCharacterController::OnAppearanceNext(){AppearancePage++;RebuildAppearancePage();}
void UOKCharacterController::OnAppearanceRandom(){if(!bAppearanceBusy&&!AppearanceFiltered.IsEmpty())ApplyAppearanceItem(AppearanceItems[AppearanceFiltered[FMath::RandRange(0,AppearanceFiltered.Num()-1)]]->GetStringField(TEXT("key")));}
void UOKCharacterController::OnAppearanceReset(){ApplyAppearanceItem(FString());}
void UOKCharacterController::OnAddAccessorySlot(){ApplyAppearanceItem(TEXT("__add_accessory_slot"));}
void UOKCharacterController::RebuildAppearancePage(){
 if(!AppearanceGrid||bAppearanceBusy)return;int32 Category=-1;for(auto T:AppearanceTargets)if(T->GetStringField(TEXT("key"))==AppearanceTargetKey)Category=T->GetIntegerField(TEXT("category"));
 const FString Search=AppearanceSearch->GetText().ToString();AppearanceFiltered.Reset();for(int32 I=0;I<AppearanceItems.Num();++I){auto Item=AppearanceItems[I];auto E=Item->GetObjectField(TEXT("entry"));if(E->GetIntegerField(TEXT("category_no"))!=Category)continue;
  const FString Text=(Item->GetStringField(TEXT("source")).IsEmpty()?TEXT("原始遊戲 類型 "):TEXT("模組 類型 "))+E->GetStringField(TEXT("name"))+TEXT(" ")+FString::FromInt(E->GetIntegerField(TEXT("id")))+TEXT(" ")+Item->GetStringField(TEXT("source"));if(!Search.IsEmpty()&&!Text.Contains(Search))continue;AppearanceFiltered.Add(I);}
 AppearancePage=FMath::Clamp(AppearancePage,0,FMath::Max(0,(AppearanceFiltered.Num()-1)/32));++AppearancePageGeneration;AppearanceGrid->ClearChildren();AppearanceButtons.Reset();AppearanceTextures.Reset();
 for(int32 At=AppearancePage*32;At<FMath::Min((AppearancePage+1)*32,AppearanceFiltered.Num());++At){auto Item=AppearanceItems[AppearanceFiltered[At]];auto E=Item->GetObjectField(TEXT("entry"));auto* B=WidgetTree->ConstructWidget<UOKAppearanceButton>();B->Owner=this;B->ItemKey=Item->GetStringField(TEXT("key"));B->OnClicked.AddDynamic(B,&UOKAppearanceButton::SelectItem);B->SetToolTipText(FText::FromString(E->GetStringField(TEXT("name"))+TEXT("\n")+Item->GetStringField(TEXT("source"))));
  if(AppearanceSelectedKeys.FindRef(AppearanceTargetKey)==B->ItemKey)B->SetBackgroundColor(FLinearColor(.95,.45,.25));auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(124);auto* Col=WidgetTree->ConstructWidget<UVerticalBox>();Box->SetContent(Col);B->SetContent(Box);
  B->Thumbnail=WidgetTree->ConstructWidget<UImage>();B->Thumbnail->SetDesiredSizeOverride(FVector2D(104,104));auto* ThumbBox=WidgetTree->ConstructWidget<USizeBox>();ThumbBox->SetWidthOverride(104);ThumbBox->SetHeightOverride(104);ThumbBox->SetContent(B->Thumbnail);Col->AddChild(ThumbBox);B->ThumbnailStatus=WidgetTree->ConstructWidget<UTextBlock>();B->ThumbnailStatus->SetText(FText::FromString(TEXT("讀取縮圖…")));B->ThumbnailStatus->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",12));Col->AddChild(B->ThumbnailStatus);auto* Text=WidgetTree->ConstructWidget<UTextBlock>();Text->SetText(FText::FromString(FString::Printf(TEXT("類型 %d · %s"),E->GetIntegerField(TEXT("id")),Item->GetStringField(TEXT("source")).IsEmpty()?TEXT("原始遊戲"):TEXT("模組"))));Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));Text->SetAutoWrapText(true);Col->AddChild(Text);AppearanceGrid->AddChild(B);AppearanceButtons.Add(B);
 }
 AppearancePageText->SetText(FText::FromString(FString::Printf(TEXT("%d／%d 頁"),AppearancePage+1,FMath::Max(1,(AppearanceFiltered.Num()+31)/32))));AppearanceStatus->SetText(FText::FromString(FString::Printf(TEXT("共 %d 個項目；點選圖片套用，原始卡片不會被覆寫。"),AppearanceFiltered.Num())));LoadAppearanceThumbnails();
}
void UOKCharacterController::LoadAppearanceThumbnails(){
 if(bAppearanceThumbBusy||AppearanceButtons.IsEmpty())return;const int32 Generation=AppearancePageGeneration;bAppearanceThumbBusy=true;auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("action"),TEXT("thumbnails"));TArray<TSharedPtr<FJsonValue>> Keys;for(UOKAppearanceButton* B:AppearanceButtons)Keys.Add(MakeShared<FJsonValueString>(B->ItemKey));R->SetArrayField(TEXT("keys"),Keys);
 RunAppearanceRequest(R,[Generation](UOKCharacterController* G,TSharedPtr<FJsonObject> Data,const FString& Error){G->bAppearanceThumbBusy=false;if(Generation!=G->AppearancePageGeneration){G->LoadAppearanceThumbnails();return;}if(!Data){G->AppearanceStatus->SetText(FText::FromString(TEXT("縮圖讀取失敗：")+Error));return;}
  for(auto V:Data->GetArrayField(TEXT("thumbnails"))){auto Row=V->AsObject();FString Path;Row->TryGetStringField(TEXT("path"),Path);for(UOKAppearanceButton* B:G->AppearanceButtons)if(B->ItemKey==Row->GetStringField(TEXT("key"))){if(auto* T=Thumbnail(Path)){G->AppearanceTextures.Add(T);B->Thumbnail->SetBrushFromTexture(T);B->ThumbnailStatus->SetVisibility(ESlateVisibility::Collapsed);}else B->ThumbnailStatus->SetText(FText::FromString(TEXT("素材未提供縮圖")));}}
 });
}
bool UOKCharacterController::MaterializeAppearance(TFunction<void(UOKCharacterController*)> Complete){
 if(!Selected.DeferredPreview)return false;
 if(bAppearanceBusy||bPropertyBusy||bLoadInFlight||!Entries.IsValidIndex(SelectedIndex))return true;
 auto Job=MakeShared<FOKAppearanceCardJob,ESPMode::ThreadSafe>(Selected);
 const FString Output=FPaths::ProjectSavedDir()/TEXT("AppearanceEditor")/(FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png"));
 const FString Before=Entries[SelectedIndex].Path;const int32 Selection=SelectedIndex,Generation=LoadGeneration,Outfit=OutfitIndex;
 bAppearanceBusy=true;SetIsEnabled(false);TWeakObjectPtr<UOKCharacterController> Weak(this);
 Async(EAsyncExecution::ThreadPool,[Weak,Job,Output,Before,Selection,Generation,Outfit,Complete=MoveTemp(Complete)]() mutable {
  FString Error;const bool Saved=UKKCardWriter::SaveWithEdits(Job->Card,Output,Error);
  AsyncTask(ENamedThreads::GameThread,[Weak,Job,Output,Before,Selection,Generation,Outfit,Saved,Error,Complete=MoveTemp(Complete)]() mutable {
   auto* G=Weak.Get();if(!G)return;G->bAppearanceBusy=false;G->SetIsEnabled(true);
   if(Selection!=G->SelectedIndex||Generation!=G->LoadGeneration||Outfit!=G->OutfitIndex)return;
   if(!Saved){if(G->AppearanceStatus)G->AppearanceStatus->SetText(FText::FromString(TEXT("保存目前外觀失敗，保留編輯供重試：")+Error));return;}
   G->Selected=MoveTemp(Job->Card);G->SetWorkingCardPath(Output);
   if(G->PreviewCardPath==Before)G->PreviewCardPath=Output;
   if(G->AppearanceContextPath==Before)G->AppearanceContextPath=Output;
   G->ResetClothingPreviews(PortableGallerySignature(Output)+TEXT("|outfit=")+FString::FromInt(Outfit)+TEXT("|mode=portable"));
   Complete(G);
  });
 });
 return true;
}
bool UOKCharacterController::ApplyPreparedAppearance(const FString& Key){
 const FString Target=AppearanceTargetKey;
 if(ApplyPreparedGeometry(Key))return true;
 const bool Supported=Target==TEXT("eyebrow")||Target==TEXT("eyeline_up")||Target==TEXT("eyeline_down")||Target==TEXT("eye_white")||Target==TEXT("eye_hi_up")||Target==TEXT("eye_hi_down")||Target==TEXT("nose")||
  Target==TEXT("pupil0")||Target==TEXT("pupil1")||Target==TEXT("gradation0")||Target==TEXT("gradation1");
 if(!Supported||!AppearanceContext||AppearanceContextPath!=Entries[SelectedIndex].Path||AppearanceContextGeneration!=LoadGeneration||AppearanceContextOutfit!=OutfitIndex||!IsValid(PreviewActor.Get())||!PreviewActor->Session.IsPortable())return false;
 const FString Folder=FPaths::ProjectSavedDir()/TEXT("AppearanceEditor");IFileManager::Get().MakeDirectory(*Folder,true);
 const double SnapshotAt=FPlatformTime::Seconds();
 auto Job=MakeShared<FOKAppearanceCardJob,ESPMode::ThreadSafe>();auto Context=AppearanceContext;
 // Candidate metadata/checkpoint owns the complete next card. Only these
 // pending typed edits are merged back; copying the old 100MB APNG on the
 // game thread serves no purpose on this path.
 CopyPendingHeadEdits(Selected,Job->Card);Job->SnapshotMs=(FPlatformTime::Seconds()-SnapshotAt)*1000.;
 const int32 Selection=SelectedIndex,Generation=LoadGeneration,Outfit=OutfitIndex;
 bAppearanceBusy=true;if(ShapePanel)ShapePanel->SetIsEnabled(false);AppearanceTarget->SetIsEnabled(false);AppearanceSearch->SetIsEnabled(false);
 AppearanceStatus->SetText(FText::FromString(TEXT("正在套用外觀…")));
 TWeakObjectPtr<UOKCharacterController> Weak(this);
 const double Queued=FPlatformTime::Seconds();
 Async(EAsyncExecution::ThreadPool,[Weak,Context,Job,Selection,Generation,Outfit,Target,Key,Queued]{
  Job->QueueMs=(FPlatformTime::Seconds()-Queued)*1000.;
  FString Error;double Start=FPlatformTime::Seconds();auto Candidate=Context->Prepare(Target,Key,Error);
  Job->RequestMs=(FPlatformTime::Seconds()-Start)*1000.;
  TSharedPtr<FJsonObject> Preparation;FString PreparationText;
  if(Candidate&&Candidate->Info(PreparationText))FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PreparationText),Preparation);
  TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> Checkpoint;
  if(Candidate)Checkpoint=Context->Capture(*Candidate,Error);
  Start=FPlatformTime::Seconds();
  // Interactive edits own their exact metadata and immutable all-outfit
  // resources. Complete APNG I/O belongs to save/reload/navigation barriers,
  // rather than the click-to-render critical path.
  TArray<uint8> Raw;
  bool Ready=Checkpoint&&Candidate&&Candidate->CardMetadata(Raw);
  const double MetadataCopyMs=(FPlatformTime::Seconds()-Start)*1000.;
  if(Ready){
   FKKCharacterFile Loaded;Start=FPlatformTime::Seconds();Ready=UKKCardReader::LoadFromBytes(Raw,Loaded,Error);Job->ReadMs=(FPlatformTime::Seconds()-Start)*1000.;
   if(Ready){
    CopyPendingHeadEdits(Job->Card,Loaded);
    Loaded.DeferredPreview=MakeShared<FOKAppearanceDeferredPreview,ESPMode::ThreadSafe>(Checkpoint);
    Job->Card=MoveTemp(Loaded);
   }
  }
  AsyncTask(ENamedThreads::GameThread,[Weak,Context,Job,Candidate,Ready,Error,MetadataCopyMs,Preparation,Selection,Generation,Outfit,Target,Key]{
   auto* G=Weak.Get();if(!G)return;
   G->bAppearanceBusy=false;if(G->ShapePanel)G->ShapePanel->SetIsEnabled(true);G->AppearanceTarget->SetIsEnabled(true);G->AppearanceSearch->SetIsEnabled(true);
   if(Selection!=G->SelectedIndex||Generation!=G->LoadGeneration||Outfit!=G->OutfitIndex)return;
   FString Failure=Error;const double Start=FPlatformTime::Seconds();
   const bool Applied=Ready&&Candidate&&IsValid(G->PreviewActor.Get())&&G->PreviewActor->ApplyHeadTextureCandidate(*Context,*Candidate,Failure);
   if(!Applied){
    // No partial candidate is published. A changed source program, layout or
    // pending shape edit uses the established complete transaction instead.
    UE_LOG(LogTemp,Display,TEXT("Appearance prepared transaction fallback target=%s error=%s"),*Target,*Failure);
    G->AppearanceContext.Reset();G->ApplyAppearanceItem(Key);return;
   }
   G->Selected=MoveTemp(Job->Card);G->bIndependentCards=true;
   G->AppearanceSelectedKeys.Add(Target,Key);G->PreviewResourceSignature.Empty();G->ResetClothingPreviews();
   G->AppearanceLastTiming=MakeShared<FJsonObject>();G->AppearanceLastTiming->SetBoolField(TEXT("owned_candidate"),true);G->AppearanceLastTiming->SetBoolField(TEXT("durable_before_commit"),false);G->AppearanceLastTiming->SetBoolField(TEXT("pinned_save_revision"),true);
   G->AppearanceLastTiming->SetBoolField(TEXT("incremental_preview"),true);G->AppearanceLastTiming->SetNumberField(TEXT("native_request_ms"),Job->RequestMs);
   G->AppearanceLastTiming->SetNumberField(TEXT("selected_snapshot_ms"),Job->SnapshotMs);G->AppearanceLastTiming->SetNumberField(TEXT("queue_ms"),Job->QueueMs);
   if(Preparation)G->AppearanceLastTiming->SetObjectField(TEXT("native_preparation"),Preparation);
   G->AppearanceLastTiming->SetNumberField(TEXT("metadata_copy_ms"),MetadataCopyMs);G->AppearanceLastTiming->SetNumberField(TEXT("output_read_ms"),Job->ReadMs);
   G->AppearanceLastTiming->SetNumberField(TEXT("output_save_ms"),Job->SaveMs);G->AppearanceLastTiming->SetNumberField(TEXT("preview_commit_ms"),(FPlatformTime::Seconds()-Start)*1000.);
   // Selection does not change the catalog or thumbnails. Keep existing
   // widgets/textures rather than decoding and uploading the entire page.
   for(UOKAppearanceButton* B:G->AppearanceButtons)B->SetBackgroundColor(B->ItemKey==Key?FLinearColor(.95f,.45f,.25f):FLinearColor::White);
   G->AppearanceStatus->SetText(FText::FromString(TEXT("外觀已套用；另存時會保存完整角色資源。")));
  });
 });
 return true;
}
bool UOKCharacterController::ApplyPreparedGeometry(const FString& Key){
 // Exact-slot accessory geometry. Enabled only after real-display acceptance.
 if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APPEARANCE_GEOMETRY"))!=TEXT("1"))return false;
 const FString Target=AppearanceTargetKey;int32 Category=-1;
 for(auto T:AppearanceTargets)if(T->GetStringField(TEXT("key"))==Target)Category=T->GetIntegerField(TEXT("category"));
 if(!Target.StartsWith(TEXT("accessory"))||Category<121||Category>130)return false;
 if(!AppearanceContext||AppearanceContextPath!=Entries[SelectedIndex].Path||AppearanceContextGeneration!=LoadGeneration||AppearanceContextOutfit!=OutfitIndex||!IsValid(PreviewActor.Get())||!PreviewActor->Session.IsPortable())return false;
 const double SnapshotAt=FPlatformTime::Seconds();
 auto Job=MakeShared<FOKAppearanceCardJob,ESPMode::ThreadSafe>();auto Context=AppearanceContext;
 CopyPendingHeadEdits(Selected,Job->Card);Job->SnapshotMs=(FPlatformTime::Seconds()-SnapshotAt)*1000.;
 const int32 Selection=SelectedIndex,Generation=LoadGeneration,Outfit=OutfitIndex;
 bAppearanceBusy=true;if(ShapePanel)ShapePanel->SetIsEnabled(false);AppearanceTarget->SetIsEnabled(false);AppearanceSearch->SetIsEnabled(false);
 AppearanceStatus->SetText(FText::FromString(TEXT("正在套用外觀…")));
 TWeakObjectPtr<UOKCharacterController> Weak(this);const double Queued=FPlatformTime::Seconds();
 Async(EAsyncExecution::ThreadPool,[Weak,Context,Job,Selection,Generation,Outfit,Target,Key,Queued]{
  Job->QueueMs=(FPlatformTime::Seconds()-Queued)*1000.;
  FString Error;double Start=FPlatformTime::Seconds();auto Candidate=Context->PrepareGeometry(Target,Key,Error);
  Job->RequestMs=(FPlatformTime::Seconds()-Start)*1000.;
  TSharedPtr<FJsonObject> Preparation;FString PreparationText;
  if(Candidate&&Candidate->Info(PreparationText))FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PreparationText),Preparation);
  TSharedPtr<FOkAppearanceCheckpoint,ESPMode::ThreadSafe> Checkpoint;if(Candidate)Checkpoint=Context->Capture(*Candidate,Error);
  TArray<uint8> Raw;Start=FPlatformTime::Seconds();bool Ready=Checkpoint&&Candidate&&Candidate->CardMetadata(Raw);
  const double MetadataCopyMs=(FPlatformTime::Seconds()-Start)*1000.;
  if(Ready){
   FKKCharacterFile Loaded;Start=FPlatformTime::Seconds();Ready=UKKCardReader::LoadFromBytes(Raw,Loaded,Error);Job->ReadMs=(FPlatformTime::Seconds()-Start)*1000.;
   if(Ready){CopyPendingHeadEdits(Job->Card,Loaded);Loaded.DeferredPreview=MakeShared<FOKAppearanceDeferredPreview,ESPMode::ThreadSafe>(Checkpoint);Job->Card=MoveTemp(Loaded);}
  }
  AsyncTask(ENamedThreads::GameThread,[Weak,Context,Job,Candidate,Ready,Error,MetadataCopyMs,Preparation,Selection,Generation,Outfit,Target,Key]{
   auto* G=Weak.Get();if(!G)return;
   G->bAppearanceBusy=false;if(G->ShapePanel)G->ShapePanel->SetIsEnabled(true);G->AppearanceTarget->SetIsEnabled(true);G->AppearanceSearch->SetIsEnabled(true);
   if(Selection!=G->SelectedIndex||Generation!=G->LoadGeneration||Outfit!=G->OutfitIndex)return;
   FString Failure=Error;const double Start=FPlatformTime::Seconds();auto Publication=MakeShared<FJsonObject>();
   const bool Applied=Ready&&Candidate&&IsValid(G->PreviewActor.Get())&&G->PreviewActor->ApplyAccessoryGeometryCandidate(*Context,*Candidate,Failure,Publication);
   if(!Applied){
    // Nothing was published. Legacy cards without exact accessory provenance
    // and any rejected candidate use the established complete transaction.
    UE_LOG(LogTemp,Display,TEXT("Appearance geometry transaction fallback target=%s error=%s"),*Target,*Failure);
    G->AppearanceContext.Reset();G->ApplyAppearanceItem(Key);return;
   }
   G->Selected=MoveTemp(Job->Card);G->bIndependentCards=true;
   G->AppearanceSelectedKeys.Add(Target,Key);G->PreviewResourceSignature.Empty();G->ResetClothingPreviews();
   G->AppearanceLastTiming=MakeShared<FJsonObject>();G->AppearanceLastTiming->SetBoolField(TEXT("owned_candidate"),true);G->AppearanceLastTiming->SetBoolField(TEXT("geometry_transaction"),true);
   G->AppearanceLastTiming->SetBoolField(TEXT("durable_before_commit"),false);G->AppearanceLastTiming->SetBoolField(TEXT("pinned_save_revision"),true);
   G->AppearanceLastTiming->SetBoolField(TEXT("incremental_preview"),true);G->AppearanceLastTiming->SetNumberField(TEXT("native_request_ms"),Job->RequestMs);
   G->AppearanceLastTiming->SetNumberField(TEXT("selected_snapshot_ms"),Job->SnapshotMs);G->AppearanceLastTiming->SetNumberField(TEXT("queue_ms"),Job->QueueMs);
   if(Preparation)G->AppearanceLastTiming->SetObjectField(TEXT("native_preparation"),Preparation);
   G->AppearanceLastTiming->SetObjectField(TEXT("geometry_publication"),Publication);
   G->AppearanceLastTiming->SetNumberField(TEXT("metadata_copy_ms"),MetadataCopyMs);G->AppearanceLastTiming->SetNumberField(TEXT("output_read_ms"),Job->ReadMs);
   G->AppearanceLastTiming->SetNumberField(TEXT("output_save_ms"),Job->SaveMs);G->AppearanceLastTiming->SetNumberField(TEXT("preview_commit_ms"),(FPlatformTime::Seconds()-Start)*1000.);
   for(UOKAppearanceButton* B:G->AppearanceButtons)B->SetBackgroundColor(B->ItemKey==Key?FLinearColor(.95f,.45f,.25f):FLinearColor::White);
   G->AppearanceStatus->SetText(FText::FromString(TEXT("外觀已套用；另存時會保存完整角色資源。")));
  });
 });
 return true;
}
void UOKCharacterController::ApplyAppearanceItem(const FString& Key){
 const bool bAddSlot=Key==TEXT("__add_accessory_slot");
 if(bAppearanceBusy||bPropertyBusy||bLoadInFlight||bAnimationPreparing||PendingAnimationKind>=0||!Entries.IsValidIndex(SelectedIndex)||(!bAddSlot&&AppearanceTargetKey.IsEmpty()))return;
 if(!bAddSlot&&ApplyPreparedAppearance(Key))return;
 if(bAddSlot){auto* Root=UKKGameRoot::Get();AppearanceRoot=Root?Root->Root:FString();}
 const FString Folder=FPaths::ProjectSavedDir()/TEXT("AppearanceEditor"),Id=FGuid::NewGuid().ToString(EGuidFormats::Digits),Input=Folder/(Id+TEXT(".input.png")),Output=Folder/(Id+TEXT(".png"));IFileManager::Get().MakeDirectory(*Folder,true);
 auto CardJob=MakeShared<FOKAppearanceCardJob,ESPMode::ThreadSafe>(Selected);
 bAppearanceBusy=true;if(ShapePanel)ShapePanel->SetIsEnabled(false);AppearancePanel->SetVisibility(ESlateVisibility::Visible);AppearanceTarget->SetIsEnabled(false);AppearanceSearch->SetIsEnabled(false);AppearanceStatus->SetText(FText::FromString(TEXT("正在套用素材與建立可保存資源，完成後更新角色…")));auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("action"),bAddSlot?TEXT("accessory_slot_add"):TEXT("apply"));R->SetStringField(TEXT("card"),Input);R->SetStringField(TEXT("output"),Output);R->SetStringField(TEXT("resources"),FPaths::ProjectContentDir()/TEXT("IndependentResources/v1"));R->SetStringField(TEXT("target"),AppearanceTargetKey);R->SetStringField(TEXT("item"),Key);R->SetNumberField(TEXT("outfit"),OutfitIndex);
 const int32 Selection=SelectedIndex,Generation=LoadGeneration,Outfit=OutfitIndex;const FString Target=AppearanceTargetKey;
 RunAppearanceRequest(R,[Selection,Generation,Outfit,Target,Key,Output,CardJob](UOKCharacterController* G,TSharedPtr<FJsonObject> Data,const FString& Failure){G->bAppearanceBusy=false;if(G->ShapePanel)G->ShapePanel->SetIsEnabled(true);G->AppearanceTarget->SetIsEnabled(true);G->AppearanceSearch->SetIsEnabled(true);if(Selection!=G->SelectedIndex||Generation!=G->LoadGeneration||Outfit!=G->OutfitIndex)return;if(!Data){G->AppearanceStatus->SetText(FText::FromString(TEXT("外觀素材套用失敗，保留目前角色：")+Failure));return;}
  G->AppearanceLastTiming=MakeShared<FJsonObject>();
  G->AppearanceLastTiming->SetNumberField(TEXT("input_save_ms"),CardJob->SaveMs);G->AppearanceLastTiming->SetNumberField(TEXT("native_request_ms"),CardJob->RequestMs);
  G->AppearanceLastTiming->SetNumberField(TEXT("output_read_ms"),CardJob->ReadMs);G->AppearanceLastTiming->SetNumberField(TEXT("preview_session_prepare_ms"),CardJob->PreviewPrepareMs);
  G->Selected=MoveTemp(CardJob->Card);G->SetWorkingCardPath(Output);G->bIndependentCards=true;if(Key!=TEXT("__add_accessory_slot"))G->AppearanceSelectedKeys.Add(Target,Key);if(Key==TEXT("__add_accessory_slot")){G->RefreshAppearanceCatalog(true);}else{
   const double CommitStart=FPlatformTime::Seconds();
   FString UpdateError;const bool Incremental=CardJob->AppearanceSession&&IsValid(G->PreviewActor.Get())&&G->PreviewActor->ApplyHeadTextureSession(*CardJob->AppearanceSession,UpdateError);
   G->PreviewResourceSignature.Empty();
   if(Incremental){G->PreviewCardPath=Output;UE_LOG(LogTemp,Display,TEXT("Appearance preview committed without model reload target=%s"),*Target);}
   else{if(CardJob->AppearanceSession)UE_LOG(LogTemp,Display,TEXT("Appearance incremental commit fallback: %s"),*UpdateError);G->StartLoad3D(Target.StartsWith(TEXT("accessory"))||Target.StartsWith(TEXT("clothes")));}
   G->AppearanceLastTiming->SetBoolField(TEXT("incremental_preview"),Incremental);G->AppearanceLastTiming->SetNumberField(TEXT("preview_commit_ms"),(FPlatformTime::Seconds()-CommitStart)*1000.);
   G->RebuildAppearancePage();
  }
 },CardJob);
}
