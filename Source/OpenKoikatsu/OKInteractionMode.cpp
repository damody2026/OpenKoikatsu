#include "OKInteractionMode.h"
#include "OKCharacterController.h"
#include "OKInteractionScene.h"
#include "OKHScene.h"
#include "KKPreviewActor.h"
#include "ok_extract.h"
#include "Components/PoseableMeshComponent.h"
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
struct FInteractionRoleLoad {FOkSession Session;FKKCharacterFile Card;FString Error,Pairs;bool Ok=false;};
}
AOKInteractionMode::AOKInteractionMode(){PrimaryActorTick.bCanEverTick=true;}
AOKInteractionMode* AOKInteractionMode::Enter(UOKCharacterController* Input,const FTransform* InPlace){
    if(!Input||!Input->GetWorld()||!Input->GetOwningPlayer()||Input->Selected.Sex!=1)return nullptr;
    for(TActorIterator<AOKInteractionMode> It(Input->GetWorld());It;++It)if(!It->Finished)return It->Controller==Input?*It:nullptr;
    auto* Mode=Input->GetWorld()->SpawnActor<AOKInteractionMode>();if(!Mode)return nullptr;
    Mode->Controller=Input;Mode->Started=FPlatformTime::Seconds();if(InPlace){Mode->bInPlace=true;Mode->InPlaceAt=*InPlace;Input->bInteractionInPlace=true;}Mode->PreviousView=Input->GetOwningPlayer()->GetViewTarget();Mode->PreviousMouse=Input->GetOwningPlayer()->bShowMouseCursor;
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
        if(Cast<AKKPreviewActor>(A)||Cast<AKKPairedPlayback>(A))A->SetActorTickEnabled(false);
        if(InPlace)continue; // the world goes on around them: its sounds, its sights
        TInlineComponentArray<UAudioComponent*> Audio;A->GetComponents(Audio);for(auto* Component:Audio)if(Component->IsPlaying()){Mode->SourceAudio.Add({Component,Component->bIsPaused!=0});Component->SetPaused(true);}
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
AOKInteractionMode* AOKInteractionMode::EnterCards(APlayerController* Player,const FString& Female,const FString& Male,int32 FemaleOutfit,const FTransform* InPlace){
    if(!Player||!FPaths::FileExists(Female)||!FPaths::FileExists(Male))return nullptr;
    auto* Shared=CreateWidget<UOKCharacterController>(Player);Shared->bInteractionOnly=true;Shared->bIndependentCards=!UKKCardWorkspace::PortableSourcePath(Female).IsEmpty();Shared->Selected.Sex=1;Shared->InitializeInteractionControls();
    auto* Mode=Enter(Shared,InPlace);if(!Mode)return nullptr;
    Mode->OwnController=true;Mode->MalePath=Male;Mode->ReturnLabel=TEXT("返回多人街區");Mode->LoadFemale(Female,FemaleOutfit);return Mode;
}
void AOKInteractionMode::LoadFemale(const FString& Path,int32 Outfit){
    Controller->bLoadInFlight=true;
    const FString Portable=UKKCardWorkspace::PortableSourcePath(Path);
    const FString Root=Portable.IsEmpty()&&UKKGameRoot::Get()?UKKGameRoot::Get()->Root:FString();
    const FString Source=Portable.IsEmpty()?Path:Portable;
    auto Job=MakeShared<FInteractionRoleLoad,ESPMode::ThreadSafe>();const auto Token=Cancelled;TWeakObjectPtr<AOKInteractionMode> Weak(this);
    FOkSession::InitBindings();
    Async(EAsyncExecution::Thread,[Weak,Job,Token,Path,Root,Source,Outfit](){
        if(*Token)return;
        // Each step timed: March 7th's high model took 14 s here (Encore 1.6 s) with nothing in the log.
        const double T0=FPlatformTime::Seconds();
        Job->Ok=UKKCardReader::LoadFromFile(Path,Job->Card,Job->Error);
        if(Job->Ok&&Job->Card.Sex!=1){Job->Ok=false;Job->Error=TEXT("這張卡是男性角色，不能當女角：")+FPaths::GetCleanFilename(Path);}
        const double T1=FPlatformTime::Seconds();
        Job->Ok=Job->Ok&&Job->Session.Open(Root,Source,Outfit,Job->Error);
        const double T2=FPlatformTime::Seconds();double T3=T2,T35=T2,T4=T2;
        // Her materials first, every texture in parallel (the precache below then finds them prepared): one at a time,
        // March 7th's took 10 s here (Encore 1 s), the player looking at the loading screen.
        if(Job->Ok&&!*Token){Job->Session.ApplyBodyPhysics(Job->Card.Body.BustSoftness,Job->Card.Body.BustWeight);T3=FPlatformTime::Seconds();
         FString MaterialError;if(!Job->Session.PrepareRuntimeMaterials(MaterialError,true))UE_LOG(LogTemp,Warning,TEXT("Interaction female materials: %s"),*MaterialError);T35=FPlatformTime::Seconds();
         AKKPreviewActor::PrecacheSessionTextures(Job->Session,true);T4=FPlatformTime::Seconds();
         // The touch reactions' shared data, parsed here rather than in the frame her scene comes up.
         FString BodyError;if(!FOKBodyInteraction::WarmShared(BodyError))UE_LOG(LogTemp,Warning,TEXT("Interaction body data: %s"),*BodyError);
         // Her pair list too, as the card reader's own load does (RefreshPairs read it from the session on the game thread).
         Job->Pairs=Job->Session.NativeAnimationPairs();}
        UE_LOG(LogTemp,Display,TEXT("Interaction female load %s: card %.0f ms, session %.0f ms, body physics %.0f ms, materials %.0f ms, textures %.0f ms"),*FPaths::GetCleanFilename(Path),(T1-T0)*1000.,(T2-T1)*1000.,(T3-T2)*1000.,(T35-T3)*1000.,(T4-T35)*1000.);
        AsyncTask(ENamedThreads::GameThread,[Weak,Job,Token,Path,Outfit](){
            if(*Token||!Weak.IsValid())return;auto* Mode=Weak.Get();auto* C=Mode->Controller.Get();
            if(!Job->Ok){Mode->Fail(Job->Error.IsEmpty()?TEXT("女角高模載入失敗"):Job->Error);return;}
            C->Selected=MoveTemp(Job->Card);C->Entries.Reset();auto& E=C->Entries.AddDefaulted_GetRef();E.Path=Path;E.FileName=FPaths::GetCleanFilename(Path);C->SelectedIndex=0;C->OutfitIndex=Outfit;C->PreviewOutfitIndex=Outfit;C->PreviewCardPath=Path;C->bModelLayout=true;
            if(!Job->Pairs.IsEmpty()){C->PendingLoadPairJson=MoveTemp(Job->Pairs);C->PendingLoadPairGeneration=C->LoadGeneration;}
            Mode->LoadingFemale=Mode->GetWorld()->SpawnActor<AKKPreviewActor>();if(!Mode->LoadingFemale){Mode->Fail(TEXT("無法建立高模角色"));return;}Mode->LoadingFemale->bPreserveSourceWorld=true;if(Mode->bInPlace)Mode->LoadingFemale->SetActorTransform(FTransform(Mode->InPlaceAt.GetRotation(),Mode->InPlaceAt.GetLocation()));else Mode->LoadingFemale->SetActorLocation(FVector::ZeroVector);Mode->LoadingFemale->SetActorHiddenInGame(true);Mode->LoadingFemale->Session=MoveTemp(Job->Session);
            FString Error;if(!Mode->LoadingFemale->BeginSessionLoad(Error)){Mode->Fail(Error);return;}
        });
    });
}
void AOKInteractionMode::BeginHScene(const FString& Name,const FString& Stage,const TSharedPtr<FJsonObject>& Experience,const FString& Player){
    if(!Controller)return;Controller->bHScene=true;Controller->HPlayerName=Player;Controller->HHeroineName=Name;Controller->HHeroineStage=Stage;Controller->HExperience=Experience;Controller->HResult.Reset();
}
TSharedPtr<FJsonObject> AOKInteractionMode::HSceneControl(const TSharedPtr<FJsonObject>& Body,FString& Why){
    UOKHScenePanel* Panel=Controller?Controller->HPanel.Get():nullptr;if(!Panel){Why=TEXT("no H panel (scene not ready)");return nullptr;}
    double Kind=0,Speed=-1;FString Pose;
    if(Body.IsValid()){
        if(Body->TryGetNumberField(TEXT("speed"),Speed)&&Speed>=0)Panel->SetSpeed(float(Speed));
        if(Body->TryGetStringField(TEXT("pose"),Pose)&&!Pose.IsEmpty()&&!Panel->SelectPoseNamed(Pose)){Why=TEXT("no pose named ")+Pose;return nullptr;}
        if(Body->TryGetNumberField(TEXT("kind"),Kind)&&Kind>0)Panel->Click(int32(Kind));
        FString Say;if(Body->TryGetStringField(TEXT("say"),Say)&&!Say.IsEmpty())Panel->Say(Say);
        // {"height":{"her":0..1,"him":0..1}}: their height slider (ChaFile shapeBody[0]) for testing the pair at other sizes.
        double BlushValue;if(Body->TryGetNumberField(TEXT("blush"),BlushValue))Panel->BlushTest=float(BlushValue);
        // {"siru":[face,ftop,fbot,btop,bbot]}: set the liquid levels (0..2) directly, to look at how they draw.
        const TArray<TSharedPtr<FJsonValue>>* SiruLevels=nullptr;if(Body->TryGetArrayField(TEXT("siru"),SiruLevels)&&SiruLevels->Num()==5){int32 L[5];for(int32 A=0;A<5;++A)L[A]=int32((*SiruLevels)[A]->AsNumber());Panel->SetSiruTest(L);}
        double FaceCam=0;if(Body->TryGetNumberField(TEXT("face_cam"),FaceCam))Panel->LookAtFace(float(FaceCam));
        double CrotchCam=0;const TSharedPtr<FJsonObject>* CrotchView=nullptr;if(Body->TryGetNumberField(TEXT("crotch_cam"),CrotchCam))Panel->LookAtCrotch(float(CrotchCam));
        else if(Body->TryGetObjectField(TEXT("crotch_cam"),CrotchView)){double Dist=60,Pitch=15,Turn=0;(*CrotchView)->TryGetNumberField(TEXT("distance"),Dist);(*CrotchView)->TryGetNumberField(TEXT("pitch"),Pitch);(*CrotchView)->TryGetNumberField(TEXT("turn"),Turn);bool Front=false;(*CrotchView)->TryGetBoolField(TEXT("front"),Front);Panel->LookAtCrotch(float(Dist),float(Pitch),float(Turn),Front);}
        const TSharedPtr<FJsonObject>* Heights=nullptr;if(Body->TryGetObjectField(TEXT("height"),Heights)){
            // The pair player holds views into both sessions: reshaping under it crashed the game. Stop it first; the next pose
            // chosen makes the pair again at the new sizes.
            Controller->CancelPairPreparation();
            if(Controller->PairedPlayback){Controller->PairedPlayback->Stop();Controller->PairedPlayback->Destroy();Controller->PairedPlayback=nullptr;}
            for(AKKPreviewActor* A:{Controller->PreviewActor.Get(),Controller->PartnerActor.Get()})if(IsValid(A))A->SetActorTickEnabled(true);
            auto Set=[](AKKPreviewActor* A,double V){if(!IsValid(A)||A->ShapeBody.IsEmpty())return;A->ShapeBody[0]=float(V);A->ApplyShapes(A->ShapeFace,A->ShapeBody);};
            double V;if((*Heights)->TryGetNumberField(TEXT("her"),V))Set(Controller->PreviewActor,V);if((*Heights)->TryGetNumberField(TEXT("him"),V))Set(Controller->PartnerActor,V);}
    }
    TSharedPtr<FJsonObject> View=Panel->Describe();
    // {"faces":true}: her eye (1) and brow (2) pattern labels, for choosing expressions.
    bool Faces=false;if(View.IsValid()&&Body.IsValid()&&Body->TryGetBoolField(TEXT("faces"),Faces)&&Faces&&IsValid(Controller->PreviewActor))
        for(int32 Control=1;Control<=2;++Control){TArray<TSharedPtr<FJsonValue>> L;for(int32 P=0;P<Controller->PreviewActor->GetFacePatternCount(Control);++P)L.Add(MakeShared<FJsonValueString>(Controller->PreviewActor->GetFacePatternLabel(Control,P)));View->SetArrayField(Control==1?TEXT("eyes"):TEXT("brows"),L);}
    // {"parts":true}: every mesh component on her (role, class, shown), to see what draws.
    bool PartsDump=false;if(View.IsValid()&&Body.IsValid()&&Body->TryGetBoolField(TEXT("parts"),PartsDump)&&PartsDump&&IsValid(Controller->PreviewActor)){
        TArray<TSharedPtr<FJsonValue>> L;TInlineComponentArray<UMeshComponent*> Components;Controller->PreviewActor->GetComponents(Components);
        for(UMeshComponent* C:Components){int32 Index=INDEX_NONE;for(const FName& Tag:C->ComponentTags)if(Tag.ToString().StartsWith(TEXT("KKSessionPart_")))Index=FCString::Atoi(*Tag.ToString().Mid(14));
            FString PartRole;OkMeshView V{};if(Index!=INDEX_NONE&&Controller->PreviewActor->Session.PartAt(Index,V)&&V.role)PartRole=UTF8_TO_TCHAR(V.role);
            L.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%d %s %s vis=%d hig=%d main=%d parent=%s"),Index,*PartRole,*C->GetClass()->GetName(),C->IsVisible(),C->bHiddenInGame,C->bRenderInMainPass,C->GetAttachParent()?*C->GetAttachParent()->GetName():TEXT("-"))));}
        View->SetArrayField(TEXT("parts"),L);}
    // {"his_parts":true}: the same for him, with each part's renderer name (the penis is o_dankon / cm_o_dan00).
    bool HisDump=false;if(View.IsValid()&&Body.IsValid()&&Body->TryGetBoolField(TEXT("his_parts"),HisDump)&&HisDump&&IsValid(Controller->PartnerActor)){
        TArray<TSharedPtr<FJsonValue>> L;TInlineComponentArray<UMeshComponent*> Components;Controller->PartnerActor->GetComponents(Components);
        for(UMeshComponent* C:Components){int32 Index=INDEX_NONE;for(const FName& Tag:C->ComponentTags)if(Tag.ToString().StartsWith(TEXT("KKSessionPart_")))Index=FCString::Atoi(*Tag.ToString().Mid(14));
            FString PartRole,PartName;OkMeshView V{};if(Index!=INDEX_NONE&&Controller->PartnerActor->Session.PartAt(Index,V)){if(V.role)PartRole=UTF8_TO_TCHAR(V.role);if(V.name)PartName=UTF8_TO_TCHAR(V.name);}
            const UMaterialInterface* M=C->GetNumMaterials()>0?C->GetMaterial(0):nullptr;
            L.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%d %s %s vis=%d mat=%s"),Index,*PartRole,*PartName,C->IsVisible(),M?*M->GetName():TEXT("-"))));}
        View->SetArrayField(TEXT("his_parts"),L);}
    // {"bones":true}: where their hips, hands, feet and heads are (world cm), for placing props under a pose.
    bool BonesDump=false;if(View.IsValid()&&Body.IsValid()&&Body->TryGetBoolField(TEXT("bones"),BonesDump)&&BonesDump){
        auto Dump=[&](AKKPreviewActor* A,const TCHAR* Field,const TCHAR* P){if(!IsValid(A)||!A->BodySkel)return;auto O=MakeShared<FJsonObject>();
            for(const TCHAR* B:{TEXT("j_hips"),TEXT("j_hand_L"),TEXT("j_hand_R"),TEXT("j_foot_L"),TEXT("j_foot_R"),TEXT("j_head"),TEXT("j_spine02")}){const FName Name(FString(P)+B);
                if(A->BodySkel->GetBoneIndex(Name)==INDEX_NONE)continue;const FVector L=A->BodySkel->GetBoneLocation(Name);O->SetStringField(B,FString::Printf(TEXT("%.0f %.0f %.0f"),L.X,L.Y,L.Z));}
            O->SetStringField(TEXT("actor"),A->GetActorLocation().ToCompactString());View->SetObjectField(Field,O);};
        Dump(Controller->PreviewActor,TEXT("her_bones"),TEXT("cf_"));Dump(Controller->PartnerActor,TEXT("his_bones"),TEXT("cm_"));}
    return View;
}
bool AOKInteractionMode::HSceneTakeLine(FString& Text,FString& Context,bool& System){return Controller&&Controller->HPanel&&Controller->HPanel->TakeLine(Text,Context,System);}
bool AOKInteractionMode::HSceneHear(const FString& Text,FString& Context){return Controller&&Controller->HPanel&&Controller->HPanel->Hear(Text,Context);}
void AOKInteractionMode::HSceneShowLine(const FString& Text){if(Controller&&Controller->HPanel)Controller->HPanel->ShowLine(Text);}
FString AOKInteractionMode::HSceneJudgeFacts() const{return Controller&&Controller->HPanel?Controller->HPanel->JudgeFacts():FString();}
bool AOKInteractionMode::HSceneApplyTalk(const TSharedPtr<FJsonObject>& Verdict){return Controller&&Controller->HPanel&&Controller->HPanel->ApplyTalk(Verdict);}
bool AOKInteractionMode::HSceneSpeak(const FString& Path){
    if(!Controller||!IsValid(Controller->PreviewActor))return false;auto* Her=Controller->PreviewActor.Get();
    if(Controller->HPanel&&Controller->HPanel->LineIsStale())return false; // said to what happened before (UOKHScenePanel::ShowLine)
    Her->StopHBreath();FString Why;const bool Ok=Her->PlaySpeechFile(Path,1.f,Why);
    if(!Ok)UE_LOG(LogTemp,Warning,TEXT("H scene: her line did not play: %s"),*Why);
    if(Ok)UE_LOG(LogTemp,Display,TEXT("H scene: her line spoken on her (%s)"),*FPaths::GetCleanFilename(Path));
    if(Ok&&Controller->HPanel)Controller->HPanel->SpokeLine();
    return Ok;
}
bool AOKInteractionMode::HSceneHasReaction() const{return Controller&&Controller->HPanel&&Controller->HPanel->HasReaction();}
bool AOKInteractionMode::HSceneSpeaking() const{return Controller&&IsValid(Controller->PreviewActor)&&Controller->PreviewActor->IsSpeaking()&&!Controller->PreviewActor->bHBreath;}
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
    if(Controller->PairedPlayback)Evidence->SetNumberField(TEXT("playback_end_seconds"),Controller->PairedPlayback->GetElapsed());Controller->InteractionReturned=nullptr;Controller->OnExitInteraction();if(Controller->HResult.IsValid())Evidence->SetObjectField(TEXT("h"),Controller->HResult);++Controller->LoadGeneration;++Controller->PartnerLoadGeneration;Controller->CancelPairCatalogue();Controller->CancelPairPreparation();
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
