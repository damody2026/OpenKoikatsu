#include "OKGalleryWidget.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "KKPreviewActor.h"
#include "KKSkeletalBuilder.h"
#include "KKCardWorkspace.h"
#include "KKGameRoot.h"
#include "KKAnimationCatalog.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/EditableTextBox.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "Widgets/SWindow.h"
#include "KKRustExtract.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <commdlg.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
void OKBackgroundWorkerPriority(); // OKGalleryWidget.cpp
void OKRetireActor(AActor* Actor); // OKGalleryWidget.cpp: hidden now, destroyed over the next frames

struct FOKClothingWarmJob {
 FOkSession Session;FString Key,Error,Mods;FVector2D Physics;bool Ok=false;TWeakObjectPtr<AKKPreviewActor> Actor;
 // Garment states this session was opened with. A cached actor must report
 // them, or a later "wear all" compares against all-zero and skips its reload.
 uint8 States[9]{};
};
namespace {
FString StatesKey(const uint8* States){return FString::Printf(TEXT("%u,%u,%u,%u,%u,%u,%u,%u,%u"),States[0],States[1],States[2],States[3],States[4],States[5],States[6],States[7],States[8]);}
}
void UOKCharacterController::ResetClothingPreviews(const FString& Context){
 TRACE_CPUPROFILER_EVENT_SCOPE(KK_GalleryResetClothing);
 ++ClothingWarmGeneration;ClothingWarmQueue.Reset();bClothingRulesPending=false;
 if(ClothingWarmReady)if(auto* Actor=ClothingWarmReady->Actor.Get())OKRetireActor(Actor);
 ClothingWarmReady.Reset();
 // Obsolete workers retain the gate until completion, bounding background
 // loads even when cards change faster than resources can be prepared.
 for(auto& Entry:ClothingPreviews)if(IsValid(Entry.Value)&&Entry.Value!=PreviewActor&&Entry.Value!=StalePreview)OKRetireActor(Entry.Value);
 ClothingPreviews.Reset();ClothingPreviewOrder.Reset();ClothingPreviewMods.Reset();ClothingPreviewContext=Context;
}
void UOKCharacterController::CacheClothingPreview(AKKPreviewActor* Actor,const FString& Key){
 TRACE_CPUPROFILER_EVENT_SCOPE(KK_GalleryCacheClothing);
 TRACE_CPUPROFILER_EVENT_SCOPE(KK_GalleryCacheBody);if(IsValid(Actor))Actor->SetClothingHidden(false);
 if(!IsValid(Actor)||ClothingPreviewContext.IsEmpty()||!Key.StartsWith(ClothingPreviewContext+TEXT("|clothes="))){if(IsValid(Actor))OKRetireActor(Actor);return;}
 if(auto* Existing=ClothingPreviews.Find(Key))if(IsValid(*Existing)&&*Existing!=Actor)OKRetireActor(*Existing);
 {TRACE_CPUPROFILER_EVENT_SCOPE(KK_GalleryHideClothing);Actor->StopSpeech();Actor->SuspendDisplayTicks(true);Actor->SetActorTickEnabled(false);Actor->SetActorHiddenInGame(true);Actor->SetActorEnableCollision(false);}
 ClothingPreviews.Add(Key,Actor);ClothingPreviewMods.Add(Key,EditorModsJson());ClothingPreviewOrder.Remove(Key);ClothingPreviewOrder.Add(Key);
 // At most three inactive native sessions and GPU displays, regardless of
 // the number of garment combinations. Oldest inactive state is evicted.
 while(ClothingPreviewOrder.Num()>3){TRACE_CPUPROFILER_EVENT_SCOPE(KK_GalleryEvictClothing);const FString Old=ClothingPreviewOrder[0];ClothingPreviewOrder.RemoveAt(0);if(auto* Entry=ClothingPreviews.Find(Old))if(IsValid(*Entry))OKRetireActor(*Entry);ClothingPreviews.Remove(Old);ClothingPreviewMods.Remove(Old);}
}
bool UOKCharacterController::TryClothingPreview(const FString& Key){
 TRACE_CPUPROFILER_EVENT_SCOPE(KK_GalleryTryClothing);
 auto* Found=ClothingPreviews.Find(Key);if(!Found||!IsValid(*Found)||!(*Found)->bPlaybackMaterialsReady||!IsValid(PreviewActor))return false;
 const bool Profile=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE"))==TEXT("1");double ProfileAt=FPlatformTime::Seconds();
 auto ProfileStep=[&](const TCHAR* Step){if(Profile){const double Now=FPlatformTime::Seconds();UE_LOG(LogTemp,Display,TEXT("OK clothing prepared switch step=%s ms=%.3f"),Step,(Now-ProfileAt)*1000.);ProfileAt=Now;}};
 AKKPreviewActor* Next=*Found;AKKPreviewActor* Previous=PreviewActor;
 const bool ModsChanged=bEditorModsDirty||ClothingPreviewMods.FindRef(Key)!=EditorModsJson();ClothingPreviewMods.Remove(Key);
 ClothingPreviews.Remove(Key);ClothingPreviewOrder.Remove(Key);
 const bool Playing=Previous->IsActorTickEnabled();const float Time=Previous->AnimPhase;
 Next->SpeechLines=Previous->SpeechLines;Next->SpeechPitch=Previous->SpeechPitch;Next->bSpeechDrivesMouth=Previous->bSpeechDrivesMouth;
 const int32 Mode=Previous->PoseMode;
 Next->SetActorTransform(Previous->GetActorTransform());
 if(Next->StudioCam&&Next->StudioCam!=Previous->StudioCam)Next->StudioCam->Destroy();
 Next->StudioCam=Previous->StudioCam;Previous->StudioCam=nullptr;
 Next->OrbitFocus=Previous->OrbitFocus;Next->OrbitYaw=Previous->OrbitYaw;Next->OrbitPitch=Previous->OrbitPitch;Next->OrbitDistance=Previous->OrbitDistance;
 CacheClothingPreview(Previous,PreviewResourceSignature);
 PreviewActor=Next;PreviewResourceSignature=Key;FMemory::Memcpy(LoadedClothesStates,ClothesStates,sizeof(ClothesStates));
 // The cache key is these exact states; the display must report them too.
 FMemory::Memcpy(Next->DisplayClothesStates,ClothesStates,sizeof(ClothesStates));
 Next->SuspendDisplayTicks(false);Next->SetActorHiddenInGame(false);Next->SetActorEnableCollision(false);
 ProfileStep(TEXT("display_swap"));
 FString Error;
 if(Next->bShapeDirty)Next->ApplyPoseFrame(0.f);
 Next->SetGameSceneMode(SelectedGameSceneMode);
 if(Mode==2&&!SelectedGameClip.IsEmpty()){
  if(SelectedStudioRow.IsNone())Next->SetGameAnimation(SelectedGameClip,false);
  else Next->SetStudioAnimation(AnimationCatalog,SelectedStudioRow,Error,SavedStudioOptions.X,SavedStudioOptions.Y,SavedStudioOptions.Z);
 }else if(Next->PoseMode!=Mode)Next->SetPoseMode(Mode);
 ProfileStep(TEXT("animation"));
 for(int32 I=0;I<Previous->Session.AnimationFloatCount();++I){FString Name;float Value,Min,Max;if(Previous->Session.AnimationFloatAt(I,Name,Value,Min,Max))Next->Session.SetAnimationFloat(Name,Value);}
 ProfileStep(TEXT("animation_parameters"));
 SetModelLayout(true);
 if(ModsChanged){bEditorModsDirty=true;ApplyPendingEditorMods();}
 ProfileStep(TEXT("modifiers"));
 Next->Session.ApplyBodyPhysics(Selected.Body.BustSoftness,Selected.Body.BustWeight);
 if(Next->bShapeDirty||Next->Session.ShapeBody()!=Selected.Body.ShapeValueBody||Next->Session.ShapeFace()!=Selected.Face.ShapeValueFace)Next->MarkShapeDirty(Selected.Face.ShapeValueFace,Selected.Body.ShapeValueBody);
 Next->AnimationPlaybackSpeed=Previous->AnimationPlaybackSpeed;Next->AnimPhase=Time;Next->FaceClock=Previous->FaceClock;
 ProfileStep(TEXT("shape_physics"));
 Next->BeginFaceDisplayUpdate();Next->SetMouthPattern(SelectedMouthPattern,false);Next->SetMouthOpen(SelectedMouthOpen);RefreshFaceControls();Next->EndFaceDisplayUpdate();
 ProfileStep(TEXT("face"));
 RefreshHandControls();Next->FollowHeadLookCamera(Previous->bHeadLookFollowCamera);Next->ApplyPoseFrame(0.f);Next->SetActorTickEnabled(Playing);
 ProfileStep(TEXT("hands_pose"));
 ApplyPenisVisible();SetModelLayout(true);
 ProfileStep(TEXT("layout"));
 if(InspectorText)InspectorText->SetText(FText::FromString(Error.IsEmpty()?TEXT("已切換服裝狀態"):Error));
 bLastClothingSwitchCached=true;UE_LOG(LogTemp,Display,TEXT("OK clothing preview cache hit %s"),*Key);return true;
}
static bool ClothingWarmDisabled(){
  return FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NO_CLOTHING_WARM"))==TEXT("1");
 }
void UOKCharacterController::SuspendClothingWarmForCapture(){
 if(!bClothingWarmHeld)UE_LOG(LogTemp,Display,TEXT("OK clothing warm held for product capture"));
 bClothingWarmHeld=true;
 ++ClothingWarmGeneration;ClothingWarmQueue.Reset();bClothingRulesPending=false;
 if(ClothingWarmReady)if(auto* Actor=ClothingWarmReady->Actor.Get())OKRetireActor(Actor);
 ClothingWarmReady.Reset();
}
void UOKCharacterController::WarmClothingPreviews(){
 if(bClothingWarmHeld||ClothingWarmDisabled())return;
 if(!IsValid(PreviewActor)||ClothingPreviewContext.IsEmpty()||!Entries.IsValidIndex(SelectedIndex))return;
 // Rule discovery reads and validates the source card/catalogue. Queue it
 // behind the same single-worker gate as native preview preparation.
 bClothingRulesPending=true;
}

void UOKCharacterController::TickClothingPreviews(){
 TRACE_CPUPROFILER_EVENT_SCOPE(KK_GalleryClothingTick);
 if(bClothingWarmHeld||ClothingWarmDisabled())return;
 if(IsValid(PreviewActor)&&(PreviewActor->bProductSweep||PreviewActor->bProductLightLatched))return;
 // Hidden outfit actors allocate textures and compile materials as well as
 // opening native sessions. Give the active editor exclusive preparation
 // priority while the user browses appearance choices; retain queued jobs
 // and resume them when that panel closes.
 if(bLoadInFlight||bAppearanceBusy||bAppearanceThumbBusy||bPropertyBusy||
    (AppearancePanel&&AppearancePanel->IsVisible())||bAnimationPreparing||PendingAnimationKind>=0||!bModelLayout||InteractionWidget||InteractionScene||PairedPlayback||!IsValid(PreviewActor)||!Entries.IsValidIndex(SelectedIndex))return;
 if(Selected.DeferredPreview)return; // OnCloseAppearance persists before resuming background source reads.
 if(PreviewActor->bClothingHidden)return;
 // With OK_PRELOAD_NEIGHBOR the next card in the list is prepared first (OKGalleryNeighbor.cpp).
 if(!ClothingWarmReady&&NeighborPreloadPending())return;
 for(auto& Entry:ClothingPreviews)if(IsValid(Entry.Value)&&!Entry.Value->bPlaybackMaterialsReady)Entry.Value->PreparePlaybackMaterials(false);
 if(ClothingWarmReady){
  auto Job=ClothingWarmReady;
  if(!Job->Ok||Job->Key==PreviewResourceSignature||ClothingPreviews.Contains(Job->Key)){
   if(auto* Actor=Job->Actor.Get())Actor->Destroy();
   if(!Job->Ok)UE_LOG(LogTemp,Warning,TEXT("OK clothing warm load: %s"),*Job->Error);
   ClothingWarmReady.Reset();return;
  }
  FString Error;bool Ok=false;
  auto* Actor=Job->Actor.Get();
  if(!Actor){
   Actor=GetWorld()->SpawnActor<AKKPreviewActor>(PreviewActor->GetActorLocation(),PreviewActor->GetActorRotation());
   if(Actor){Job->Actor=Actor;Actor->SetActorHiddenInGame(true);Actor->SetActorTickEnabled(false);
    FMemory::Memcpy(Actor->DisplayClothesStates,Job->States,sizeof(Job->States));
    Actor->Session=MoveTemp(Job->Session);Actor->UseGalleryPhysics();if(Job->Physics!=FVector2D(Selected.Body.BustSoftness,Selected.Body.BustWeight))Actor->Session.ApplyBodyPhysics(Selected.Body.BustSoftness,Selected.Body.BustWeight);const FString Mods=EditorModsJson();if(Mods!=Job->Mods)Actor->Session.ApplyEditorMods(Mods,OutfitIndex);
    Ok=Actor->BeginSessionLoad(Error);
   }
  }else Ok=Actor->ContinueSessionLoad(Error);
  if(Actor)Actor->SuspendDisplayTicks(true);
  if(!Ok){if(Actor)Actor->Destroy();ClothingWarmReady.Reset();UE_LOG(LogTemp,Warning,TEXT("OK clothing warm display: %s"),*Error);}
  else if(Actor->IsSessionLoadComplete()){
   CacheClothingPreview(Actor,Job->Key);ClothingWarmReady.Reset();UE_LOG(LogTemp,Display,TEXT("OK clothing preview prepared incrementally %s"),*Job->Key);
  }
  return;
 }
 if(bClothingRulesPending&&!bClothingWarmInFlight){
  bClothingRulesPending=false;bClothingWarmInFlight=true;
  const UKKGameRoot* Root=bIndependentCards?nullptr:UKKGameRoot::Get();
  const FString Game=Root?Root->Root:FString(),Card=Entries[SelectedIndex].Path,Context=ClothingPreviewContext;
  const int32 Generation=ClothingWarmGeneration,Outfit=OutfitIndex;
  TWeakObjectPtr<UOKCharacterController> Weak(this);
  Async(EAsyncExecution::Thread,[Weak,Game,Card,Context,Generation,Outfit](){
   OKBackgroundWorkerPriority();
   TArray<FString> Keys;FString Errors;
   for(int32 Action:{100,101,103}){
    uint8 Base[9]{},State[9]{};FString Error;
    if(Action==103)FMemory::Memset(Base,3,sizeof(Base));
    if(FOkSession::ClothingTransition(Game,Card,Outfit,Base,Action==103?101:Action,State,Error))Keys.AddUnique(Context+TEXT("|clothes=")+StatesKey(State));
    else Errors+=Error+TEXT("\n");
   }
   AsyncTask(ENamedThreads::GameThread,[Weak,Context,Generation,Keys=MoveTemp(Keys),Errors=MoveTemp(Errors)](){
    if(auto* G=Weak.Get()){
     G->bClothingWarmInFlight=false;
     if(Generation!=G->ClothingWarmGeneration||Context!=G->ClothingPreviewContext)return;
     for(const FString& Key:Keys)if(Key!=G->PreviewResourceSignature&&!G->ClothingPreviews.Contains(Key))G->ClothingWarmQueue.AddUnique(Key);
     if(!Errors.IsEmpty())UE_LOG(LogTemp,Warning,TEXT("OK clothing warm rules: %s"),*Errors);
    }
   });
  });
  return;
 }
 if(bClothingWarmInFlight||ClothingWarmQueue.IsEmpty())return;
 const FString Key=ClothingWarmQueue[0];ClothingWarmQueue.RemoveAt(0);
 if(Key==PreviewResourceSignature||ClothingPreviews.Contains(Key))return;
 TArray<FString> Values;Key.Mid(Key.Find(TEXT("|clothes="))+9).ParseIntoArray(Values,TEXT(","));if(Values.Num()!=9)return;
 TArray<uint8> States;for(const FString& Value:Values)States.Add(uint8(FCString::Atoi(*Value)));
 UKKGameRoot* Root=bIndependentCards?nullptr:UKKGameRoot::Get();const FString Game=Root?Root->Root:FString();
 const FString Card=bIndependentCards?UKKCardWorkspace::LoadableSourcePath(Entries[SelectedIndex].Path):Entries[SelectedIndex].Path;
 const int32 Generation=ClothingWarmGeneration,Outfit=OutfitIndex;bClothingWarmInFlight=true;
 auto Job=MakeShared<FOKClothingWarmJob,ESPMode::ThreadSafe>();Job->Key=Key;for(int32 I=0;I<9;++I)Job->States[I]=States[I];Job->Mods=EditorModsJson();Job->Physics=FVector2D(Selected.Body.BustSoftness,Selected.Body.BustWeight);TWeakObjectPtr<UOKCharacterController> Weak(this);
 const bool PrepareSkeleton=OKLiveGpuSkinRequested();
 Async(EAsyncExecution::Thread,[Weak,Generation,Game,Card,Outfit,States,Job,PrepareSkeleton](){
  OKBackgroundWorkerPriority();
  const double Begin=FPlatformTime::Seconds();UE_LOG(LogTemp,Display,TEXT("OK clothing warm begin %s"),*Job->Key);
  // With the launcher's warm processes (OK_WARM_PRIORITY), an OK card's variant that is not in the open cache is
  // requested from them and waited for here. Building it in this process would hold the process-wide card pack
  // mount for seconds, and the next card the user opens would wait behind it.
  const FString WarmFile=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_WARM_PRIORITY"));
  if(!WarmFile.IsEmpty()&&FOkSession::CacheReady(Card,Outfit,States.GetData())==0){
   FString Line=FPaths::ConvertRelativePathToFull(Card)+FString::Printf(TEXT("\t%d\t"),Outfit);
   for(int32 I=0;I<States.Num();++I){if(I)Line+=TEXT(",");Line.AppendInt(States[I]);}
   FFileHelper::SaveStringToFile(Line+TEXT("\n"),*WarmFile,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
   int32 Ready=0; // once a second: the check hashes the card file
   while(Ready==0&&FPlatformTime::Seconds()-Begin<45.){FPlatformProcess::Sleep(1.f);Ready=FOkSession::CacheReady(Card,Outfit,States.GetData());}
   if(Ready==0){
    UE_LOG(LogTemp,Display,TEXT("OK clothing warm left to the warm processes %s"),*Job->Key);
    AsyncTask(ENamedThreads::GameThread,[Weak](){if(auto* G=Weak.Get())G->bClothingWarmInFlight=false;});
    return;
   }
  }
  // Speculative: gives way to the card the user opens next (the open fails and the variant is simply not ready).
  FOkSession::SetThreadBackground(true);
  Job->Ok=Job->Session.Open(Game,Card,Outfit,Job->Error,States.GetData());const double Opened=FPlatformTime::Seconds();
  FOkSession::SetThreadBackground(false);
  if(Job->Ok){Job->Session.ApplyBodyPhysics(Job->Physics.X,Job->Physics.Y);Job->Session.ApplyEditorMods(Job->Mods,Outfit);AKKPreviewActor::PrecacheSessionTextures(Job->Session,PrepareSkeleton,true);}
  UE_LOG(LogTemp,Display,TEXT("OK clothing warm worker ok=%d open_ms=%.3f texture_ms=%.3f"),Job->Ok,(Opened-Begin)*1000.,(FPlatformTime::Seconds()-Opened)*1000.);
  AsyncTask(ENamedThreads::GameThread,[Weak,Generation,Job](){if(auto* G=Weak.Get()){G->bClothingWarmInFlight=false;if(Generation==G->ClothingWarmGeneration)G->ClothingWarmReady=Job;}});
 });
}

// ---------------------------------------------------------------------------------------------- OK clothes (.okclothes)
// One outfit with its plugin data (Material Editor, KCOX, accessory states ...); the shared models stay in the OK base
// library, so the file is about the size of a coordinate card (Docs/OK_BASE_LIBRARY_PLAN.md). Files go to the save folder.
namespace {
bool OKPickFile(const TCHAR* Filter,const TCHAR* Title,FString& Out){
#if PLATFORM_WINDOWS
 TCHAR File[4096]={0};OPENFILENAMEW Dialog{};Dialog.lStructSize=sizeof(Dialog);
 if(GEngine&&GEngine->GameViewport)if(TSharedPtr<SWindow> Window=GEngine->GameViewport->GetWindow())if(Window->GetNativeWindow().IsValid())Dialog.hwndOwner=(HWND)Window->GetNativeWindow()->GetOSWindowHandle();
 Dialog.lpstrFilter=Filter;Dialog.lpstrFile=File;Dialog.nMaxFile=UE_ARRAY_COUNT(File);Dialog.lpstrTitle=Title;
 Dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
 if(!GetOpenFileNameW(&Dialog))return false;Out=File;return true;
#else
 return false;
#endif
}
}
void UOKCharacterController::OnExportOkClothes(){
 auto Report=[this](const FString& Text){if(InspectorText)InspectorText->SetText(FText::FromString(Text));if(GEngine)GEngine->AddOnScreenDebugMessage(1,8.f,FColor::Cyan,Text);};
 if(!Entries.IsValidIndex(SelectedIndex)){Report(TEXT("請先選擇一張角色卡。"));return;}
 const FString Directory=OutputDirectoryInput?OutputDirectoryInput->GetText().ToString().TrimStartAndEnd():FString();
 if(Directory.IsEmpty()){Report(TEXT("請先在卡片列表填寫另存資料夾。"));return;}
 const FString Card=Entries[SelectedIndex].Path;
 const FString Output=FPaths::ConvertRelativePathToFull(Directory)/FString::Printf(TEXT("%s_outfit%d.okclothes"),*FPaths::GetBaseFilename(Card),OutfitIndex);
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);
 FString Summary;const bool Ok=FOkSession::ClothesExport(Card,OutfitIndex,Output,Summary);
 UE_LOG(LogTemp,Display,TEXT("OK clothes export %s -> %s: %s"),*Card,*Output,*Summary);
 Report(Ok?FString::Printf(TEXT("已匯出服裝 %d：%s"),OutfitIndex,*Output):TEXT("匯出服裝失敗：")+Summary);
}
void UOKCharacterController::OnWearOkClothes(){
 auto Report=[this](const FString& Text){if(InspectorText)InspectorText->SetText(FText::FromString(Text));if(GEngine)GEngine->AddOnScreenDebugMessage(1,8.f,FColor::Cyan,Text);};
 if(!Entries.IsValidIndex(SelectedIndex)){Report(TEXT("請先選擇一張角色卡。"));return;}
 const FString Directory=OutputDirectoryInput?OutputDirectoryInput->GetText().ToString().TrimStartAndEnd():FString();
 if(Directory.IsEmpty()){Report(TEXT("請先在卡片列表填寫另存資料夾。"));return;}
 FString Clothes;if(!OKPickFile(TEXT("OK 服裝\0*.okclothes\0所有檔案\0*.*\0"),TEXT("選擇要穿上的 OK 服裝"),Clothes))return;
 const FString Card=Entries[SelectedIndex].Path;
 // A new card: the original stays as it was.
 const FString Output=UKKCardWorkspace::NewCardPath(FPaths::ConvertRelativePathToFull(Directory),Card);
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);
 FString Summary;const bool Ok=FOkSession::ClothesWear(Card,Clothes,OutfitIndex,Output,Summary);
 UE_LOG(LogTemp,Display,TEXT("OK clothes wear %s on %s outfit %d -> %s: %s"),*Clothes,*Card,OutfitIndex,*Output,*Summary);
 if(!Ok){Report(TEXT("穿上服裝失敗：")+Summary);return;}
 Report(FString::Printf(TEXT("已穿上為服裝 %d，另存新卡：%s"),OutfitIndex,*Output));
 RefreshCardList();
}
void UOKCharacterController::OnImportClothesCard(){
 auto Report=[this](const FString& Text){if(InspectorText)InspectorText->SetText(FText::FromString(Text));if(GEngine)GEngine->AddOnScreenDebugMessage(1,8.f,FColor::Cyan,Text);};
 const FString Directory=OutputDirectoryInput?OutputDirectoryInput->GetText().ToString().TrimStartAndEnd():FString();
 if(Directory.IsEmpty()){Report(TEXT("請先在卡片列表填寫另存資料夾。"));return;}
 FString Coordinate;if(!OKPickFile(TEXT("服裝卡\0*.png\0所有檔案\0*.*\0"),TEXT("選擇遊戲的服裝卡"),Coordinate))return;
 const FString Output=FPaths::ConvertRelativePathToFull(Directory)/(FPaths::GetBaseFilename(Coordinate)+TEXT(".okclothes"));
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);
 FString Summary;const bool Ok=FOkSession::ClothesImport(Coordinate,Output,Summary);
 UE_LOG(LogTemp,Display,TEXT("OK clothes import %s -> %s: %s"),*Coordinate,*Output,*Summary);
 Report(Ok?TEXT("已轉成 OK 服裝：")+Output:TEXT("轉換服裝卡失敗：")+Summary);
}
