#include "OKInteractionMode.h"
#include "OKCharacterController.h"
#include "OKInteractionScene.h"
#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
#include "KKCardReader.h"
#include "KKCardWorkspace.h"
#include "KKGameRoot.h"
#include "OpenKoikatsuPlayerController.h"
#include "Async/Async.h"
#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "Components/AudioComponent.h"
#include "EngineUtils.h"
#include "HAL/PlatformProcess.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Components/ComboBoxString.h"
#include "ProceduralMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"

namespace {
struct FInteractionRoleLoad {FOkSession Session;FKKCharacterFile Card;FString Error;bool Ok=false;};
}
AOKInteractionMode::AOKInteractionMode(){PrimaryActorTick.bCanEverTick=true;}
AOKInteractionMode* AOKInteractionMode::Enter(UOKCharacterController* Input){
    if(!Input||!Input->GetWorld()||!Input->GetOwningPlayer()||Input->Selected.Sex!=1)return nullptr;
    for(TActorIterator<AOKInteractionMode> It(Input->GetWorld());It;++It)if(!It->Finished)return It->Controller==Input?*It:nullptr;
    auto* Mode=Input->GetWorld()->SpawnActor<AOKInteractionMode>();if(!Mode)return nullptr;
    Mode->Controller=Input;Mode->Started=FPlatformTime::Seconds();Mode->PreviousView=Input->GetOwningPlayer()->GetViewTarget();Mode->PreviousMouse=Input->GetOwningPlayer()->bShowMouseCursor;
    if(auto* PC=Cast<AOpenKoikatsuPlayerController>(Input->GetOwningPlayer())){
        Mode->PreviousPaint=PC->IsPaintPreviewActive();Mode->PreviousTouch=PC->bBodyTouchEnabled;
        // Detach the source context before stage activation can cancel it.
        Mode->PreviousBodyInteraction=MoveTemp(PC->BodyInteraction);PC->BodyInteraction=FOKBodyInteraction();
        Mode->PreviousBodyGesture=MoveTemp(PC->BodyGesture);PC->BodyGesture=FOKBodyGesture();PC->bBodyTouchEnabled=false;
    }
    Mode->Cancelled=MakeShared<FThreadSafeBool,ESPMode::ThreadSafe>(false);
    for(TActorIterator<AActor> It(Input->GetWorld());It;++It){
        AActor* A=*It;if(A==Mode)continue;
        Mode->SourceActors.Add({A,A->IsHidden(),A->GetActorEnableCollision(),A->IsActorTickEnabled()});
        TInlineComponentArray<UAudioComponent*> Audio;A->GetComponents(Audio);for(auto* Component:Audio)if(Component->IsPlaying()){Mode->SourceAudio.Add({Component,Component->bIsPaused!=0});Component->SetPaused(true);}
        if(Cast<AKKPreviewActor>(A)||Cast<AKKPairedPlayback>(A))A->SetActorTickEnabled(false);
        if(A!=Input->PreviewActor&&A!=Input->PartnerActor){A->SetActorHiddenInGame(true);A->SetActorEnableCollision(false);}
    }
    if(GEngine&&GEngine->GameViewport){
        TWeakObjectPtr<AOKInteractionMode> Weak(Mode);
        Mode->PreparingOverlay=SNew(SBorder).HAlign(HAlign_Center).VAlign(VAlign_Center).BorderBackgroundColor(FLinearColor(.015f,.025f,.04f,.92f))[
            SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("正在準備男女高模與雙人動作…")))]
            +SVerticalBox::Slot().AutoHeight().Padding(0,20)[SNew(SButton).Text(FText::FromString(TEXT("取消並返回"))).OnClicked_Lambda([Weak](){if(Weak.IsValid())Weak->Exit(true);return FReply::Handled();})]];
        GEngine->GameViewport->AddViewportWidgetContent(Mode->PreparingOverlay.ToSharedRef(),120);
    }
    Input->InteractionReturned=[Weak=TWeakObjectPtr<AOKInteractionMode>(Mode)](){if(Weak.IsValid())Weak->Exit();};
    return Mode;
}
AOKInteractionMode* AOKInteractionMode::EnterReader(UOKCharacterController* Source){
    if(!Source||Source->InteractionWidget||Source->bLoadInFlight||Source->Selected.Sex!=1)return nullptr;
    if(!IsValid(Source->PreviewActor)||!Source->PreviewActor->Session.IsOpen()){
        Source->bEnterInteractionAfterLoad=true;Source->OnLoad3D();return nullptr;
    }
    Source->ApplyPendingEditorMods();if(Source->bEditorModsDirty)return nullptr;
    auto* Shared=CreateWidget<UOKCharacterController>(Source->GetOwningPlayer());if(!Shared)return nullptr;
    Shared->bInteractionOnly=true;Shared->bIndependentCards=Source->bIndependentCards;Shared->Selected=Source->Selected;
    Shared->Entries=Source->Entries;Shared->SelectedIndex=Source->SelectedIndex;Shared->OutfitIndex=Source->OutfitIndex;
    Shared->PreviewOutfitIndex=Source->PreviewOutfitIndex;Shared->PreviewCardPath=Source->PreviewCardPath;Shared->CurrentCardDirectory=Source->CurrentCardDirectory;Shared->bModelLayout=true;
    FMemory::Memcpy(Shared->ClothesStates,Source->ClothesStates,9);FMemory::Memcpy(Shared->LoadedClothesStates,Source->LoadedClothesStates,9);
    Shared->InitializeInteractionControls();
    auto* Mode=Enter(Shared);if(!Mode)return nullptr;Mode->OwnController=true;
    Mode->LoadingFemale=Mode->GetWorld()->SpawnActor<AKKPreviewActor>();
    if(!Mode->LoadingFemale||!Source->PreviewActor->Session.CloneForInteraction(Mode->LoadingFemale->Session)){Mode->Fail(TEXT("無法複製目前角色的完整高模資源"));return nullptr;}
    FMemory::Memcpy(Mode->LoadingFemale->DisplayClothesStates,Source->PreviewActor->DisplayClothesStates,9);
    Mode->LoadingFemale->bPreserveSourceWorld=true;Mode->LoadingFemale->SetActorLocation(FVector::ZeroVector);Mode->LoadingFemale->SetActorHiddenInGame(true);Shared->bLoadInFlight=true;
    FString Error;if(!Mode->LoadingFemale->BeginSessionLoad(Error)){Mode->Fail(Error);return nullptr;}
    if(Source->PartnerMalePaths.IsValidIndex(Source->SelectedPartnerMale))Mode->MalePath=Source->PartnerMalePaths[Source->SelectedPartnerMale];
    else if(!Shared->PartnerMalePaths.IsEmpty())Mode->MalePath=Shared->PartnerMalePaths[0];
    const auto SourceVisibility=Source->GetVisibility();Source->SetVisibility(ESlateVisibility::Collapsed);
    Mode->Returned=[Weak=TWeakObjectPtr<UOKCharacterController>(Source),SourceVisibility](TSharedRef<FJsonObject>){if(Weak.IsValid()){Weak->SetVisibility(SourceVisibility);Weak->ApplyClickInputMode();}};
    return Mode;
}
AOKInteractionMode* AOKInteractionMode::EnterCards(APlayerController* Player,const FString& Female,const FString& Male){
    if(!Player||!FPaths::FileExists(Female)||!FPaths::FileExists(Male))return nullptr;
    auto* Shared=CreateWidget<UOKCharacterController>(Player);Shared->bInteractionOnly=true;Shared->bIndependentCards=!UKKCardWorkspace::PortableSourcePath(Female).IsEmpty();Shared->Selected.Sex=1;Shared->InitializeInteractionControls();
    auto* Mode=Enter(Shared);if(!Mode)return nullptr;
    Mode->OwnController=true;Mode->MalePath=Male;Mode->ReturnLabel=TEXT("返回多人街區");Mode->LoadFemale(Female);return Mode;
}
void AOKInteractionMode::LoadFemale(const FString& Path){
    Controller->bLoadInFlight=true;
    const FString Portable=UKKCardWorkspace::PortableSourcePath(Path);
    const FString Root=Portable.IsEmpty()&&UKKGameRoot::Get()?UKKGameRoot::Get()->Root:FString();
    const FString Source=Portable.IsEmpty()?Path:Portable;
    auto Job=MakeShared<FInteractionRoleLoad,ESPMode::ThreadSafe>();const auto Token=Cancelled;TWeakObjectPtr<AOKInteractionMode> Weak(this);
    FOkSession::InitBindings();
    Async(EAsyncExecution::Thread,[Weak,Job,Token,Path,Root,Source](){
        if(*Token)return;
        // Each step timed: March 7th's high model took 14 s here (Encore 1.6 s) with nothing in the log.
        const double T0=FPlatformTime::Seconds();
        Job->Ok=UKKCardReader::LoadFromFile(Path,Job->Card,Job->Error);
        if(Job->Ok&&Job->Card.Sex!=1){Job->Ok=false;Job->Error=TEXT("這張卡是男性角色，不能當女角：")+FPaths::GetCleanFilename(Path);}
        const double T1=FPlatformTime::Seconds();
        Job->Ok=Job->Ok&&Job->Session.Open(Root,Source,0,Job->Error);
        const double T2=FPlatformTime::Seconds();double T3=T2,T4=T2;
        if(Job->Ok&&!*Token){Job->Session.ApplyBodyPhysics(Job->Card.Body.BustSoftness,Job->Card.Body.BustWeight);T3=FPlatformTime::Seconds();AKKPreviewActor::PrecacheSessionTextures(Job->Session,true);T4=FPlatformTime::Seconds();}
        UE_LOG(LogTemp,Display,TEXT("Interaction female load %s: card %.0f ms, session %.0f ms, body physics %.0f ms, textures %.0f ms"),*FPaths::GetCleanFilename(Path),(T1-T0)*1000.,(T2-T1)*1000.,(T3-T2)*1000.,(T4-T3)*1000.);
        AsyncTask(ENamedThreads::GameThread,[Weak,Job,Token,Path](){
            if(*Token||!Weak.IsValid())return;auto* Mode=Weak.Get();auto* C=Mode->Controller.Get();
            if(!Job->Ok){Mode->Fail(Job->Error.IsEmpty()?TEXT("女角高模載入失敗"):Job->Error);return;}
            C->Selected=MoveTemp(Job->Card);C->Entries.Reset();auto& E=C->Entries.AddDefaulted_GetRef();E.Path=Path;E.FileName=FPaths::GetCleanFilename(Path);C->SelectedIndex=0;C->OutfitIndex=0;C->PreviewOutfitIndex=0;C->PreviewCardPath=Path;C->bModelLayout=true;
            Mode->LoadingFemale=Mode->GetWorld()->SpawnActor<AKKPreviewActor>();if(!Mode->LoadingFemale){Mode->Fail(TEXT("無法建立高模角色"));return;}Mode->LoadingFemale->bPreserveSourceWorld=true;Mode->LoadingFemale->SetActorLocation(FVector::ZeroVector);Mode->LoadingFemale->SetActorHiddenInGame(true);Mode->LoadingFemale->Session=MoveTemp(Job->Session);
            FString Error;if(!Mode->LoadingFemale->BeginSessionLoad(Error)){Mode->Fail(Error);return;}
        });
    });
}
void AOKInteractionMode::Fail(const FString& Why){Error=Why;UE_LOG(LogTemp,Error,TEXT("Shared interaction mode: %s"),*Why);Exit(true);}
void AOKInteractionMode::Tick(float Delta){
    Super::Tick(Delta);if(Finished||!Controller)return;
    if(auto* PC=Controller->GetOwningPlayer())if(PC->WasInputKeyJustPressed(EKeys::Escape)){Exit(!Ready);return;}
    if(FPlatformTime::Seconds()-Started>180&&!Ready&&(!Activated||ReturnLabel==TEXT("返回多人街區"))){Fail(TEXT("一對一互動準備逾時"));return;}
    if(LoadingFemale){FString Why;
        if(!LoadingFemale->ContinueSessionLoad(Why)){Fail(Why);return;}
        if(!LoadingFemale->IsSessionLoadComplete())return;
        bool FullOff=true;for(uint8 State:Controller->ClothesStates)FullOff&=State==3;
        if(FullOff)LoadingFemale->SetClothingHidden(true);
        if(!LoadingFemale->BeginProgressiveMaterials())return;
        Controller->PreviewActor=LoadingFemale;LoadingFemale=nullptr;Controller->PreviewActor->SetActorTickEnabled(false);Controller->bLoadInFlight=false;
        Controller->PreviewActor->ApplyShapes(Controller->Selected.Face.ShapeValueFace,Controller->Selected.Body.ShapeValueBody);
        Controller->PreviewActor->SetupStudioView(true);Controller->PreviewActor->SetActorHiddenInGame(false);Controller->RefreshPairs();
    }
    if(!OwnController)Controller->TickPreviewPreparation();
    if(!Activated){
        if(Controller->bLoadInFlight)return;
        Controller->InteractionReturnLabel=ReturnLabel;Controller->ActivateInteraction();
        if(!Controller->InteractionWidget)return;
        Activated=true;OriginalPosition=Controller->PreviewActor->GetActorLocation();
        if(!MalePath.IsEmpty())Controller->LoadPartnerFromPath(MalePath);
    }
    UpdatePairPhysics();
    const bool Both=IsValid(Controller->PreviewActor)&&IsValid(Controller->PartnerActor)&&Controller->PartnerActor->Session.IsOpen();
    if(!Ready&&Both&&Controller->PairedPlayback&&Controller->PairedPlayback->GetElapsed()>0&&Controller->PreviewActor->AreInteractionStreamsReady()&&Controller->PartnerActor->AreInteractionStreamsReady()){
        if(PreparingOverlay&&GEngine&&GEngine->GameViewport)GEngine->GameViewport->RemoveViewportWidgetContent(PreparingOverlay.ToSharedRef());PreparingOverlay.Reset();
        Ready=true;ReadyAt=FPlatformTime::Seconds();Evidence=MakeShared<FJsonObject>();Evidence->SetStringField(TEXT("state"),TEXT("ready"));Evidence->SetBoolField(TEXT("partner_ready"),true);Evidence->SetBoolField(TEXT("same_process"),true);Evidence->SetNumberField(TEXT("process_id"),FPlatformProcess::GetCurrentProcessId());Evidence->SetStringField(TEXT("scene_class"),Controller->InteractionScene->GetClass()->GetPathName());Evidence->SetNumberField(TEXT("source_parts"),Controller->PreviewActor->Session.PartCount());Evidence->SetBoolField(TEXT("source_geometry"),true);
        UE_LOG(LogTemp,Display,TEXT("Shared interaction ready pid=%u female_parts=%d male_parts=%d"),FPlatformProcess::GetCurrentProcessId(),Controller->PreviewActor->Session.PartCount(),Controller->PartnerActor->Session.PartCount());
        Evidence->SetStringField(TEXT("card"),Controller->PreviewCardPath);Evidence->SetStringField(TEXT("partner"),MalePath);
        Evidence->SetStringField(TEXT("widget_class"),Controller->InteractionWidget->GetClass()->GetPathName());
        int64 Vertices=0;int32 Components=0;for(const auto& Part:Controller->PreviewActor->Parts){Vertices+=Part.BaseVertices.Num();if(IsValid(Part.Comp))++Components;}
        Evidence->SetNumberField(TEXT("render_vertices"),Vertices);Evidence->SetNumberField(TEXT("render_components"),Components);
        Evidence->SetNumberField(TEXT("playback_start_seconds"),Controller->PairedPlayback->GetElapsed());
        if(!ReportPath.IsEmpty())FScreenshotRequest::RequestScreenshot(FPaths::GetPath(ReportPath)/TEXT("high-interaction.png"),true,false);
    }
#if !UE_BUILD_SHIPPING
    if(Ready&&!TickFeatureAudit())return;
#endif
    if(Ready&&AutoReturnSeconds>0&&FPlatformTime::Seconds()-ReadyAt>=AutoReturnSeconds)Exit();
}
void AOKInteractionMode::UpdatePairPhysics(){
    for(AKKPreviewActor* Actor:{Controller->PreviewActor.Get(),Controller->PartnerActor.Get()})
        if(IsValid(Actor)&&!PairPhysicsActors.Contains(Actor)){Actor->AddPairPhysics();PairPhysicsActors.Add(Actor);}
}
void AOKInteractionMode::Exit(bool WasCancelled){
    if(Exiting||Finished)return;Exiting=true;if(Cancelled)*Cancelled=true;
    for(const TWeakObjectPtr<AKKPreviewActor>& Actor:PairPhysicsActors)if(Actor.IsValid())Actor->RemovePairPhysics();PairPhysicsActors.Reset();
    if(PreparingOverlay&&GEngine&&GEngine->GameViewport)GEngine->GameViewport->RemoveViewportWidgetContent(PreparingOverlay.ToSharedRef());PreparingOverlay.Reset();
    if(!Evidence)Evidence=MakeShared<FJsonObject>();Evidence->SetStringField(TEXT("state"),!WasCancelled&&Activated?TEXT("returned"):TEXT("cancelled"));Evidence->SetStringField(TEXT("error"),Error);Evidence->SetBoolField(TEXT("same_process"),true);Evidence->SetNumberField(TEXT("process_id"),FPlatformProcess::GetCurrentProcessId());
    if(Controller){Evidence->SetArrayField(TEXT("actions"),Controller->ActionLog);Evidence->SetNumberField(TEXT("seconds"),Ready?FMath::RoundToDouble(FPlatformTime::Seconds()-ReadyAt):0.);Controller->ActionLog.Reset();Controller->ActionLogStart=0;
    if(Controller->PairedPlayback)Evidence->SetNumberField(TEXT("playback_end_seconds"),Controller->PairedPlayback->GetElapsed());Controller->InteractionReturned=nullptr;Controller->OnExitInteraction();++Controller->LoadGeneration;++Controller->PartnerLoadGeneration;Controller->CancelPairCatalogue();Controller->CancelPairPreparation();
        if(OwnController){Controller->OnStopPair();if(Controller->PreviewActor)Controller->PreviewActor->Destroy();Controller->PreviewActor=nullptr;}
        if(auto* PC=Controller->GetOwningPlayer()){if(PreviousView)PC->SetViewTarget(PreviousView);PC->bShowMouseCursor=PreviousMouse;if(auto* O=Cast<AOpenKoikatsuPlayerController>(PC)){O->SetBodyTouchEnabled(false);O->BodyInteraction=MoveTemp(PreviousBodyInteraction);O->BodyGesture=MoveTemp(PreviousBodyGesture);O->bBodyTouchEnabled=PreviousTouch;O->SetPaintPreviewActive(PreviousPaint);}}
    }
    if(LoadingFemale){LoadingFemale->Destroy();LoadingFemale=nullptr;}
    for(const auto& State:SourceActors)if(auto* Actor=State.Actor.Get()){Actor->SetActorHiddenInGame(State.Hidden);Actor->SetActorEnableCollision(State.Collision);Actor->SetActorTickEnabled(State.Tick);}
    for(const auto& State:SourceAudio)if(auto* Audio=State.Audio.Get())Audio->SetPaused(State.Paused);SourceAudio.Reset();
    SourceActors.Reset();
    if(!ReportPath.IsEmpty()){FString Json;FJsonSerializer::Serialize(Evidence.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*ReportPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
    Finished=true;if(Returned)Returned(Evidence.ToSharedRef());Destroy();
}
void AOKInteractionMode::EndPlay(const EEndPlayReason::Type Reason){if(!Finished)Exit(true);Super::EndPlay(Reason);}
