#include "OKInteractionScene.h"
#include "Async/Async.h"
#include "OKCharacterController.h"
#include "OKInteractionMode.h"
#include "OKHScene.h"
#include "OKMotionComboBox.h"
#include "OKMotionCatalogue.h"
#include "OKMotionLabelZh.h"
#include "OKPaintSprayer.h"
#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
#include "KKGameRoot.h"
#include "OpenKoikatsuPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/InvalidationBox.h"
#include "Components/PanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "ProceduralMeshComponent.h"
#include "UnrealClient.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "UObject/ConstructorHelpers.h"

static FCriticalSection GInteractionMotionPreparationMutex;
static FCriticalSection GInteractionSpeechCatalogMutex;
static TAutoConsoleVariable<int32> CVarInteractionCacheControls(TEXT("ok.Interaction.CacheControls"),1,TEXT("Cache unchanged interaction controls with Slate invalidation; evaluated when constructing the scene UI."));
static TAutoConsoleVariable<int32> CVarHSceneTestPanel(TEXT("ok.HScene.TestPanel"),0,TEXT("Show the card reader's test panel beside the street one-on-one's H panel (evaluated when the scene opens)."));
static TAutoConsoleVariable<int32> CVarInteractionCopyDebugPose(TEXT("ok.Interaction.CopyDebugPose"),0,TEXT("Retain optional skeletal debug pose copies during interaction playback. The actual editor always retains its debug data."));

AOKInteractionScene::AOKInteractionScene() {
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("ShowcaseRoot"));
    Floor=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShowcaseFloor"));Floor->SetupAttachment(RootComponent);
    Backdrop=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShowcaseBackdrop"));Backdrop->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    for(auto* Mesh:{Floor.Get(),Backdrop.Get()}) {Mesh->SetStaticMesh(Cube.Object);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCastShadow(false);}
    Floor->SetRelativeScale3D(FVector(12.,12.,.02));
    // Both surfaces use a neutral scene root, so floor scale cannot move the wall.
    Backdrop->SetRelativeLocation(FVector(0.,-450.,250.));Backdrop->SetWorldScale3D(FVector(12.,.02,6.));
    FrameHistogram.SetNumZeroed(3002);
}
void AOKInteractionScene::BeginPlay() {
    Super::BeginPlay();
    // Mode-owned stage lighting. Source scene lights remain untouched.
    if(auto* Light=GetWorld()->SpawnActor<ADirectionalLight>(GetActorLocation(),FRotator(-45.f,-70.f,0.f))){
        Light->SetOwner(this);Light->AttachToActor(this,FAttachmentTransformRules::KeepWorldTransform);
        Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);Light->GetLightComponent()->SetIntensity(3.f);Light->GetLightComponent()->SetCastShadows(false);
    }
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    // Debug visualization data does not feed the rendered pose. Scope the
    // existing engine switch to this scene; preserve explicit console overrides
    // and restore the previous value before returning to the character editor.
    if(!GIsEditor)if(auto* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.CopyDebugPose")))
        DebugPoseGuard=MakeUnique<TGuardConsoleVariable<int32>>(Variable,CVarInteractionCopyDebugPose.GetValueOnGameThread(),ECVF_SetByCode);
#endif
}
void AOKInteractionScene::EndPlay(const EEndPlayReason::Type Reason) {
    DebugPoseGuard.Reset();
    TArray<AActor*> StageActors;GetAttachedActors(StageActors);for(auto* Actor:StageActors)if(Actor&&Actor->GetOwner()==this)Actor->Destroy();
    Super::EndPlay(Reason);
}
void AOKInteractionScene::Tick(float Delta) {
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(ControlProfileTick)ControlProfileTick();
#endif
    const double Now=FPlatformTime::Seconds();
    if(PreviousFrame>0.){const double Ms=(Now-PreviousFrame)*1000.;++FrameSamples;FrameTotal+=Ms;FrameMaximum=FMath::Max(FrameMaximum,Ms);SlowFrames+=Ms>8.333333;++FrameHistogram[FMath::Clamp(FMath::CeilToInt(Ms*10.),0,3001)];
        GameTotal+=FPlatformTime::ToMilliseconds(GGameThreadTime);RenderTotal+=FPlatformTime::ToMilliseconds(GRenderThreadTime);RHITotal+=FPlatformTime::ToMilliseconds(GRHIThreadTime);GPUTotal+=FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());WaitTotal+=FPlatformTime::ToMilliseconds(GGameThreadWaitTime);SwapTotal+=FPlatformTime::ToMilliseconds(GSwapBufferTime);}
    PreviousFrame=Now;
}
void AOKInteractionScene::ResetPerformance(){FrameSamples=SlowFrames=0;FrameTotal=FrameMaximum=PreviousFrame=0.;GameTotal=RenderTotal=RHITotal=GPUTotal=WaitTotal=SwapTotal=0.;for(auto& Count:FrameHistogram)Count=0;}
FString AOKInteractionScene::PerformanceSummary() const {
    if(!FrameSamples)return TEXT("正在收集完整幀時間");
    auto Percentile=[this](double Rate){uint64 Count=0;const uint64 Target=FMath::Max<uint64>(1,uint64(FMath::CeilToDouble(FrameSamples*Rate)));for(int32 I=0;I<FrameHistogram.Num();++I){Count+=FrameHistogram[I];if(Count>=Target)return I==3001?FrameMaximum:I*.1;}return FrameMaximum;};
    return FString::Printf(TEXT("全幀平均 %.3f ms · P95約 %.1f · P99約 %.1f · 最大 %.3f ms\n超過8.33ms %.1f%% · 樣本 %llu\nGT %.3f · Render %.3f · RHI %.3f · GPU %.3f · GT等待 %.3f · Swap %.3f ms\n百分位精度0.1ms；GPU值0可能不支援，Present節奏需另測"),FrameTotal/FrameSamples,Percentile(.95),Percentile(.99),FrameMaximum,100.*SlowFrames/FrameSamples,FrameSamples,GameTotal/FrameSamples,RenderTotal/FrameSamples,RHITotal/FrameSamples,GPUTotal/FrameSamples,WaitTotal/FrameSamples,SwapTotal/FrameSamples);
}
void AOKInteractionScene::SaveReport() const {
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Tests/InteractionShowcase");IFileManager::Get().MakeDirectory(*Directory,true);
    const FString Path=Directory/(FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".txt"));
    FFileHelper::SaveStringToFile(PerformanceSummary(),*Path);
}

TSharedRef<SWidget> UOKInteractionWidget::RebuildWidget() {
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    UHorizontalBox* Root=WidgetTree->ConstructWidget<UHorizontalBox>();WidgetTree->RootWidget=Root;
    USizeBox* Width=WidgetTree->ConstructWidget<USizeBox>();Width->SetWidthOverride(360.f);
    // The street's one-on-one has its own H panel (OKHScene.h); the test controls stay built (the pair code reads them) but hidden.
    if(Gallery&&Gallery->bHScene&&!CVarHSceneTestPanel.GetValueOnGameThread())Width->SetVisibility(ESlateVisibility::Collapsed);
    UInvalidationBox* Cache=WidgetTree->ConstructWidget<UInvalidationBox>();Cache->SetCanCache(CVarInteractionCacheControls.GetValueOnGameThread()!=0);Width->AddChild(Cache);
    UBorder* Background=WidgetTree->ConstructWidget<UBorder>();Background->SetBrushColor(FLinearColor(.025f,.045f,.07f,.96f));Cache->AddChild(Background);
    UScrollBox* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Background->AddChild(Scroll);
    Controls=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Controls);Root->AddChildToHorizontalBox(Width);
    USizeBox* Space=WidgetTree->ConstructWidget<USizeBox>();Space->SetVisibility(ESlateVisibility::HitTestInvisible);Root->AddChildToHorizontalBox(Space)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto Label=[this](const FString& Text){auto* L=WidgetTree->ConstructWidget<UTextBlock>();L->SetText(FText::FromString(Text));L->SetColorAndOpacity(FSlateColor(FLinearColor::White));L->SetAutoWrapText(true);Controls->AddChild(L);return L;};
    auto Button=[this](const FString& Text){auto* B=WidgetTree->ConstructWidget<UButton>();auto* L=WidgetTree->ConstructWidget<UTextBlock>();L->SetText(FText::FromString(Text));B->SetContent(L);Controls->AddChild(B);return B;};
    auto Check=[this](const FString& Text){auto* C=WidgetTree->ConstructWidget<UCheckBox>();auto* L=WidgetTree->ConstructWidget<UTextBlock>();L->SetText(FText::FromString(Text));L->SetColorAndOpacity(FSlateColor(FLinearColor::White));C->SetContent(L);Controls->AddChild(C);return C;};
    Label(TEXT("1對1即時互動場景"));Button(Gallery?Gallery->InteractionReturnLabel:TEXT("返回"))->OnClicked.AddDynamic(this,&UOKInteractionWidget::OnReturn);
    BodyTouch=Check(TEXT("身體點觸反應"));BodyTouch->OnCheckStateChanged.AddDynamic(this,&UOKInteractionWidget::OnBodyTouch);
    BodyIdle=Button(TEXT("單人點觸待機"));BodyIdle->SetIsEnabled(false);BodyIdle->OnClicked.AddDynamic(this,&UOKInteractionWidget::OnBodyIdle);
    BodyTouchStatus=Label(BodyPreparationError.IsEmpty()?TEXT("選擇具有點觸反應的姿勢與階段後即可使用；A＋左鍵使用水槍"):BodyPreparationError);
    if(Gallery&&Gallery->PreviewActor) {
        TArray<UWidget*> Borrowed={Gallery->EyeLookCameraCheck.Get(),Gallery->MouthPatternCombo.Get(),Gallery->MouthOpenSlider.Get(),Gallery->GameSourceCombo.Get(),Gallery->GameClipCombo.Get(),Gallery->StudioCategoryCombo.Get(),Gallery->StudioMotionCombo.Get(),Gallery->AnimationStatus.Get(),Gallery->PartnerMaleCombo.Get(),Gallery->PairCategoryCombo.Get(),Gallery->PairCombo.Get(),Gallery->PairStateCombo.Get(),Gallery->PairSpeedSlider.Get(),Gallery->PairStatus.Get(),Gallery->PhysicsBackendCombo.Get(),Gallery->PhysicsSoftnessSlider.Get(),Gallery->PhysicsWeightSlider.Get(),Gallery->PhysicsTailSlider.Get(),Gallery->PairPreparationStatus.Get(),Gallery->SpeechRootInput.Get(),Gallery->SpeechVoiceCombo.Get(),Gallery->SpeechCombo.Get(),Gallery->SpeechStatus.Get()};
        // Snapshot each complete parent before any removal. Restore order on return.
        for(UWidget* W:Borrowed)if(W&&W->GetParent()) {
            UPanelWidget* Parent=W->GetParent();
            if(!Panels.ContainsByPredicate([Parent](const FOKInteractionPanel& P){return P.Parent==Parent;})) {FOKInteractionPanel& P=Panels.AddDefaulted_GetRef();P.Parent=Parent;for(int32 I=0;I<Parent->GetChildrenCount();++I){auto* Child=Parent->GetChildAt(I);P.Children.Add(Child);P.SlotTemplates.Add(Child->Slot?DuplicateObject<UPanelSlot>(Child->Slot,this):nullptr);}}
        }
        auto Borrow=[this](UWidget* W){if(W){W->RemoveFromParent();Controls->AddChild(W);}};
        Borrow(Gallery->EyeLookCameraCheck);
        HeadCamera=Check(TEXT("面向鏡頭（頭部，雙人）"));HeadCamera->SetIsChecked(Gallery->PreviewActor->bHeadLookFollowCamera);HeadCamera->OnCheckStateChanged.AddDynamic(this,&UOKInteractionWidget::OnHeadCamera);
        Label(TEXT("嘴型與開合"));Borrow(Gallery->MouthPatternCombo);Borrow(Gallery->MouthOpenSlider);
        AutomaticMouth=Check(TEXT("語音自動開合"));AutomaticMouth->SetIsChecked(Gallery->PreviewActor->bSpeechDrivesMouth);AutomaticMouth->OnCheckStateChanged.AddDynamic(this,&UOKInteractionWidget::OnAutomaticMouth);
        Label(TEXT("肚子大小"));Belly=WidgetTree->ConstructWidget<USlider>();Belly->SetMinValue(-1.f);Belly->SetMaxValue(2.f);Belly->SetValue(Gallery->Selected.Body.ShapeValueBody.IsValidIndex(21)?Gallery->Selected.Body.ShapeValueBody[21]:.5f);Belly->OnValueChanged.AddDynamic(this,&UOKInteractionWidget::OnBelly);Controls->AddChild(Belly);
        Label(TEXT("單人動作（全部，可搜尋中文）"));
        ReadyMotionCombo=WidgetTree->ConstructWidget<UOKMotionComboBox>(UOKMotionComboBox::StaticClass(),TEXT("AllMotionCombo"));Controls->AddChild(ReadyMotionCombo);ReadyMotionCombo->OnSelectionChanged.AddDynamic(this,&UOKInteractionWidget::OnReadyMotion);
        MotionPreparation=Label(TEXT("動作準備中；目前動作繼續播放"));

        Borrow(Gallery->AnimationStatus);
        Button(TEXT("暫停／繼續姿勢"))->OnClicked.AddDynamic(this,&UOKInteractionWidget::OnPause);Button(TEXT("重播姿勢"))->OnClicked.AddDynamic(this,&UOKInteractionWidget::OnReplay);
        Label(TEXT("單人姿勢速度（0.25～4倍）"));SoloSpeed=WidgetTree->ConstructWidget<USlider>();SoloSpeed->SetMinValue(.25f);SoloSpeed->SetMaxValue(4.f);SoloSpeed->SetValue(Gallery->PreviewActor->AnimationPlaybackSpeed);SoloSpeed->OnValueChanged.AddDynamic(this,&UOKInteractionWidget::OnSoloSpeed);Controls->AddChild(SoloSpeed);
        // The male partner is fixed by the entry point; no selector, path box or replace button.
        Borrow(Gallery->PairPreparationStatus);Label(TEXT("雙人動作（全部，可搜尋中文）"));Borrow(Gallery->PairCombo);Borrow(Gallery->PairStateCombo);Borrow(Gallery->PairSpeedSlider);Borrow(Gallery->PairStatus);Button(TEXT("上一個雙人姿勢"))->OnClicked.AddDynamic(Gallery,&UOKCharacterController::OnPairPosePrev);Button(TEXT("下一個雙人姿勢"))->OnClicked.AddDynamic(Gallery,&UOKCharacterController::OnPairPoseNext);Button(TEXT("停止雙人互動"))->OnClicked.AddDynamic(Gallery,&UOKCharacterController::OnStopPair);
        if(Gallery->PhysicsBackendCombo){Label(TEXT("物理系統（即時切換）"));Gallery->FillPhysicsBackendCombo();Borrow(Gallery->PhysicsBackendCombo);}
        if(Gallery->PhysicsSoftnessSlider){Label(TEXT("頭髮／衣物硬度（1 最軟 → 10 最硬＝原設定）"));Borrow(Gallery->PhysicsSoftnessSlider);}
        if(Gallery->PhysicsWeightSlider){Label(TEXT("頭髮／衣物重量（1 原設定 → 10 加上完整重力）"));Borrow(Gallery->PhysicsWeightSlider);}
        if(Gallery->PhysicsTailSlider){Label(TEXT("尾巴彈性（0 原設定 → 1 預設 → 2 最彈）"));Borrow(Gallery->PhysicsTailSlider);}
        Label(TEXT("語音"));Borrow(Gallery->SpeechRootInput);Borrow(Gallery->SpeechVoiceCombo);Borrow(Gallery->SpeechCombo);
        SpeakButton=Button(TEXT("播放語音"));SpeakButton->SetIsEnabled(false);SpeakButton->OnClicked.AddDynamic(this,&UOKInteractionWidget::OnSpeak);Button(TEXT("停止語音"))->OnClicked.AddDynamic(this,&UOKInteractionWidget::OnStopSpeech);Borrow(Gallery->SpeechStatus);
    }
    Label(TEXT("水槍：A＋左鍵噴射，R清除。相機沿用滑鼠操作。"));Performance=Label(TEXT("效能尚未驗收"));
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    return Super::RebuildWidget();
}
void UOKInteractionWidget::RestoreControls() {
    if(MotionCancelled)*MotionCancelled=true;++MotionGeneration;
    if(SpeechCancelled)*SpeechCancelled=true;++SpeechPreparationGeneration;++SpeechGeneration;++SpeechCatalogGeneration;
    if(SpeechCatalogCancelled)*SpeechCatalogCancelled=true;
    if(Gallery&&Gallery->SpeechCombo)Gallery->SpeechCombo->OnSelectionChanged.RemoveDynamic(this,&UOKInteractionWidget::OnSpeechSelection);
    for(auto& Panel:Panels)if(Panel.Parent) {Panel.Parent->ClearChildren();for(int32 I=0;I<Panel.Children.Num();++I)if(UWidget* Child=Panel.Children[I]){Child->RemoveFromParent();Panel.Parent->AddChild(Child,Panel.SlotTemplates.IsValidIndex(I)?Panel.SlotTemplates[I].Get():nullptr);}}
    if(Gallery&&Gallery->GameSourceCombo){Gallery->GameSourceCombo->OnSelectionChanged.RemoveDynamic(this,&UOKInteractionWidget::OnMotionSource);Gallery->GameSourceCombo->OnSelectionChanged.AddUniqueDynamic(Gallery,&UOKCharacterController::OnGameSourceSelected);}
    Panels.Reset();
}
void UOKInteractionWidget::NativeTick(const FGeometry& Geometry,float Delta) {
    Super::NativeTick(Geometry,Delta);
    if(!Gallery||!IsValid(Gallery->PreviewActor)){OnReturn();return;}
    if(BodyIdleOwner.Get()!=Gallery->PreviewActor.Get())PrepareBodyIdle();
    const double Now=FPlatformTime::Seconds();
    if(SpeakButton){const int32 Selected=Gallery->SpeechCombo?Gallery->SpeechCombo->GetSelectedIndex():INDEX_NONE;const bool Ready=Gallery->PreviewActor->SpeechLines.IsValidIndex(Selected)&&SpeechCache.Contains(Gallery->PreviewActor->SpeechLines[Selected].AudioPath);if(SpeakButton->GetIsEnabled()!=Ready)SpeakButton->SetIsEnabled(Ready);}
    if(ReadyMotionCombo&&MotionSource!=TEXT("all:")){MotionSource=TEXT("all:");MotionPage=0;PrepareMotions();}
    if(MotionSearchAt>0.&&Now>=MotionSearchAt){MotionSearchAt=0.;PrepareMotions();}
    if(MotionPreparation){const FString Label=Gallery->VideoClipTimeLabel();if(!Label.IsEmpty())MotionPreparation->SetText(FText::FromString(Label));}
    if(Gallery->PreviewActor->Session.IsPortable()){
        const bool PartnerReady=Gallery->PartnerActor&&Gallery->PartnerActor->PreparePlaybackMaterials(false);
        const bool DisplayReady=Gallery->PreviewActor->PreparePlaybackMaterials(false);
        const bool PairReady=DisplayReady&&PartnerReady&&!Gallery->NativePairs.IsEmpty();
        if(Gallery->PairCombo)Gallery->PairCombo->SetIsEnabled(!Gallery->NativePairs.IsEmpty());
        if(Gallery->PairStateCombo)Gallery->PairStateCombo->SetIsEnabled(PairReady);
        if(PairReady)Gallery->TryStartPreparedPair();
        const bool WasEnabled=ReadyMotionCombo&&ReadyMotionCombo->GetIsEnabled();
        if(ReadyMotionCombo)ReadyMotionCombo->SetIsEnabled(!ReadyMotionKeys.IsEmpty());
        if(!DisplayReady&&MotionPreparation)MotionPreparation->SetText(FText::FromString(TEXT("正在背景準備顯示材質；其他操作可用")));

    }
    if(Scene&&Performance&&Now>=NextPerformanceUpdate){Performance->SetText(FText::FromString(Scene->PerformanceSummary()));NextPerformanceUpdate=Now+1.;}
    if(auto* PC=Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer())){
        FString Pair,State;
        if(Gallery->PairedPlayback){
            Pair=Gallery->ActiveBodyPairKey;State=Gallery->PairedPlayback->GetState();
        }else if(!BodyIdleKey.IsEmpty()&&Gallery->SelectedGameClip==BodyIdleKey&&Gallery->PreviewActor->PoseMode==2){
            Pair=BodyIdlePairKey;State=TEXT("Idle");
        }
        if(PC->BodyInteraction.Owner.Get()!=Gallery->PreviewActor.Get()&&BodyPrepareAttempt.Get()!=Gallery->PreviewActor.Get()){
            BodyPrepareAttempt=Gallery->PreviewActor;
            PC->SetBodyTouchEnabled(false);FString Error;
            if(!PC->BodyInteraction.Prepare(Gallery->PreviewActor,Error)&&BodyTouchStatus)BodyTouchStatus->SetText(FText::FromString(Error));
        }
        PC->BodyInteraction.SetContext(Pair,State);
        const bool Available=PC->BodyInteraction.IsAvailable();
        if(BodyTouch){BodyTouch->SetIsEnabled(Available);if(!Available&&BodyTouch->IsChecked()){BodyTouch->SetIsChecked(false);PC->SetBodyTouchEnabled(false);}}
    }
    if(!Gallery->PreviewActor->IsActorTickEnabled()&&!Gallery->PairedPlayback){Gallery->PreviewActor->UpdateHeadLookTarget();if(Gallery->PreviewActor->bShapeDirty||Gallery->PreviewActor->bHeadLookFollowCamera||Gallery->PreviewActor->bHeadLookPoseDirty||Gallery->PreviewActor->bBodyReactionPoseDirty||Gallery->PreviewActor->Session.IsBodyReactionPlaying())Gallery->PreviewActor->ApplyPoseFrame(Delta,false);Gallery->PreviewActor->AdvanceEyeLook(Delta);Gallery->PreviewActor->AdvanceFaceAnimation(Delta);Gallery->PreviewActor->AdvanceSpeech();}
}
void UOKInteractionWidget::OnBodyTouch(bool Enabled){if(auto* PC=Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer()))PC->SetBodyTouchEnabled(Enabled);}
void UOKInteractionWidget::PrepareBodyIdle(){
    if(!Gallery||!Gallery->PreviewActor)return;
    BodyIdleOwner=Gallery->PreviewActor;BodyIdleKey.Reset();BodyIdlePairKey.Reset();if(BodyIdle)BodyIdle->SetIsEnabled(false);
    const int32 Generation=++BodyIdleGeneration;const FString Root=FOkSession::CommonPairRoot();
    TWeakObjectPtr<UOKInteractionWidget> Weak(this);const auto Actor=BodyIdleOwner;
    Async(EAsyncExecution::ThreadPool,[Weak,Actor,Generation,Root](){
        FString Key,Pair;TArray<TSharedPtr<FJsonValue>> Rows;
        if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FOkSession::CommonInteractionPoses()),Rows)){
            // Preserve authored order. Select a solo pose with an actual Idle
            // state; neither a Studio clip nor a paired pose substitutes for it.
            for(const auto& V:Rows){const auto Row=V->AsObject();if(Row->GetIntegerField(TEXT("participants"))!=1)continue;
                const FString Source=Row->GetStringField(TEXT("female_source"));
                for(const FString& Label:FOkSession::AnimationSourceNames(Root,Source))if(Label.EndsWith(TEXT(" / [Controller layer 0] Base Layer.Idle"))){
                    Key=FOkSession::PairClipForRoot(Root,Source+TEXT("|")+Label,Row->GetIntegerField(TEXT("female_layer")));
                    if(!Key.IsEmpty()&&FOkSession::PrepareAnimationClip(Root,Key)){Pair=FString::Printf(TEXT("%s:%d"),*Row->GetStringField(TEXT("category")),Row->GetIntegerField(TEXT("id")));break;}Key.Reset();
                }
                if(!Key.IsEmpty())break;
            }
        }
        AsyncTask(ENamedThreads::GameThread,[Weak,Actor,Generation,Key,Pair](){
            auto* UI=Weak.Get();if(!UI||!UI->Gallery||UI->BodyIdleGeneration!=Generation||UI->Gallery->PreviewActor.Get()!=Actor.Get()||!Actor.IsValid())return;
            UI->BodyIdleKey=Key;UI->BodyIdlePairKey=Pair;if(UI->BodyIdle)UI->BodyIdle->SetIsEnabled(!Key.IsEmpty());
            UE_LOG(LogTemp,Display,TEXT("OK body idle prepared ready=%d pose=%s"),!Key.IsEmpty(),*Pair);
        });
    });
}
void UOKInteractionWidget::OnBodyIdle(){
    if(!Gallery||!Gallery->PreviewActor||BodyIdleKey.IsEmpty()||!BodyIdle||!BodyIdle->GetIsEnabled())return;
    auto* Actor=Gallery->PreviewActor.Get();if(!Actor->PreparePlaybackMaterials(false))return;
    Gallery->OnStopPair();if(!Actor->SetGameSceneMode(3,false)||!Actor->SetGameAnimation(BodyIdleKey)){Gallery->ShowAnimationStatus(TEXT("單人點觸待機播放失敗"),true);return;}
    Gallery->SelectedGameSceneMode=3;Gallery->SyncGameAnimationSelection(BodyIdleKey);
    if(auto* PC=Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer())){
        PC->BodyInteraction.SetContext(BodyIdlePairKey,TEXT("Idle"));const bool Available=PC->BodyInteraction.IsAvailable();
        PC->SetBodyTouchEnabled(Available);if(BodyTouch)BodyTouch->SetIsChecked(Available);
    }
}
void UOKInteractionWidget::OnReturn(){if(Gallery)Gallery->OnExitInteraction();}
void UOKInteractionWidget::OnHeadCamera(bool Enabled){if(Gallery&&Gallery->PreviewActor){const bool Applied=Gallery->PreviewActor->FollowHeadLookCamera(Enabled);
    // One switch for the pair: the partner follows (or stops following) the camera with the heroine.
    if(Gallery->PartnerActor){if(!Gallery->PartnerActor->StudioCam)Gallery->PartnerActor->StudioCam=Gallery->PreviewActor->StudioCam;Gallery->PartnerActor->FollowHeadLookCamera(Enabled);}
    if(HeadCamera)HeadCamera->SetIsChecked(Applied&&Enabled);}}
void UOKInteractionWidget::OnBelly(float Value){if(Gallery)Gallery->OnInteractionBellyChanged(Value);}
void UOKInteractionWidget::OnAutomaticMouth(bool Enabled){if(Gallery&&Gallery->PreviewActor){Gallery->PreviewActor->bSpeechDrivesMouth=Enabled;Gallery->PreviewActor->ApplyMouth();}}
void UOKInteractionWidget::OnSoloSpeed(float Value){if(Gallery&&Gallery->PreviewActor&&FMath::IsFinite(Value))Gallery->PreviewActor->AnimationPlaybackSpeed=FMath::Clamp(Value,.25f,4.f);}
void UOKInteractionWidget::OnLoadPartner(){if(!Gallery)return;FString Path=PartnerPath?PartnerPath->GetText().ToString().TrimStartAndEnd():FString();Path.RemoveFromStart(TEXT("\""));Path.RemoveFromEnd(TEXT("\""));if(Path.IsEmpty())Gallery->OnLoadPartner();else Gallery->LoadPartnerFromPath(Path);}
void UOKInteractionWidget::OnPause(){if(Gallery)Gallery->OnPausePlayback();}
void UOKInteractionWidget::OnReplay(){if(Gallery)Gallery->OnReplayPlayback();}
void UOKInteractionWidget::OnMotionSource(FString Item,ESelectInfo::Type){if(Gallery)if(const auto* Source=Gallery->InteractionMotionSources.Find(Item)){Gallery->SelectedGameSource=*Source;MotionSource=*Source;PrepareMotions();}}
void UOKInteractionWidget::OnMotionSearch(const FText&){MotionPage=0;MotionSearchAt=FPlatformTime::Seconds()+.2;ReadyMotionCombo->SetIsEnabled(false);if(MotionCancelled)*MotionCancelled=true;++MotionGeneration;}
void UOKInteractionWidget::OnMotionPrevious(){MotionPage=FMath::Max(0,MotionPage-1);PrepareMotions();}
void UOKInteractionWidget::OnMotionNext(){++MotionPage;PrepareMotions();}
void UOKInteractionWidget::PrepareMotions(){
    if(!Gallery||!Gallery->PreviewActor||!ReadyMotionCombo)return;
    if(MotionSource==TEXT("all:")){
        // One merged list (Content/IndependentResources/MotionCatalogue/MotionCatalogue.json) with Chinese labels; the combo's own search box filters it.
        const auto& Entries=FOKMotionCatalogue::Entries();
        ReadyMotionCombo->ClearOptions();ReadyMotionKeys.Reset();
        ReadyMotionCombo->AddOption(FString::Printf(TEXT("選擇動作（共 %d 個，可搜尋）"),Entries.Num()));ReadyMotionCombo->SetSelectedIndex(0);
        for(const auto& Entry:Entries){ReadyMotionKeys.Add(Entry.Key);ReadyMotionCombo->AddOption(Entry.Name);}
        for(const FString& Key:FOkSession::VideoClipNames()){ReadyMotionKeys.Add(Key);ReadyMotionCombo->AddOption(Key.Mid(7));}
        ReadyMotionCombo->SetIsEnabled(!ReadyMotionKeys.IsEmpty());
        MotionPreparation->SetText(FText::FromString(FString::Printf(TEXT("共 %d 個動作；點開後可搜尋中文，選取後載入"),ReadyMotionKeys.Num())));
        return;
    }
    if(MotionCancelled)*MotionCancelled=true;MotionCancelled=MakeShared<FThreadSafeBool,ESPMode::ThreadSafe>(false);
    const auto Cancelled=MotionCancelled;const int32 Generation=++MotionGeneration;
    const bool Portable=Gallery->PreviewActor->Session.IsPortable();
    const FString Source=MotionSource,Search=MotionSearch?MotionSearch->GetText().ToString():FString();
    const bool Video=Source==TEXT("video:");
    const bool Imported=Video||(Portable&&!Source.StartsWith(TEXT("states:")));
    const FString Root=Portable?FOkSession::CommonPairRoot():(UKKGameRoot::Get()?UKKGameRoot::Get()->Root:FString());
    TArray<FString> PortableNames;
    if(Video)for(const FString& Key:FOkSession::VideoClipNames())PortableNames.Add(Key.Mid(7));
    else if(Imported)PortableNames=Gallery->PreviewActor->Session.ClipNamesForSource(Source);
    ReadyMotionCombo->SetIsEnabled(false);ReadyMotionCombo->ClearOptions();ReadyMotionKeys.Reset();
    MotionPreparation->SetText(FText::FromString(TEXT("讀取動作目錄；目前播放不中斷")));
    TWeakObjectPtr<UOKInteractionWidget> Weak(this);TWeakObjectPtr<AKKPreviewActor> Actor=Gallery->PreviewActor;
    Async(EAsyncExecution::ThreadPool,[Weak,Actor,Cancelled,Generation,Root,Source,Search,Imported,PortableNames=MoveTemp(PortableNames)](){
        if(*Cancelled)return;
        TArray<FString> Names=Imported?PortableNames:FOkSession::AnimationSourceNames(Root,Source);
        Names.RemoveAll([&](const FString& Name){return !Search.IsEmpty()&&!Name.Contains(Search);});
        if(*Cancelled)return;
        AsyncTask(ENamedThreads::GameThread,[Weak,Actor,Generation,Source,Names=MoveTemp(Names)](){
            auto* UI=Weak.Get();if(!UI||!UI->Gallery||UI->MotionGeneration!=Generation||UI->Gallery->PreviewActor!=Actor.Get()||!Actor.IsValid())return;
            UI->ReadyMotionCombo->AddOption(TEXT("選擇動作（選取後載入）"));UI->ReadyMotionCombo->SetSelectedIndex(0);
            for(const auto& Name:Names){UI->ReadyMotionKeys.Add(Source+TEXT("|")+Name);UI->ReadyMotionCombo->AddOption(Name);}
            UI->ReadyMotionCombo->SetIsEnabled(!Names.IsEmpty());
            UI->MotionPreparation->SetText(FText::FromString(FString::Printf(TEXT("共 %d 個動作；可直接選取後載入"),Names.Num())));
        });
    });
}
void UOKInteractionWidget::OnReadyMotion(FString Item,ESelectInfo::Type){
    if(!Gallery||!Gallery->PreviewActor)return;
    Item=OKMotionLabelDisplay(Item);
    const int32 Index=ReadyMotionCombo->GetSelectedIndex()-1;if(!ReadyMotionKeys.IsValidIndex(Index))return;
    Gallery->CancelPairCatalogue();Gallery->CancelPairPreparation();
    const int32 Request=++MotionSelectionGeneration,Generation=MotionGeneration,AnimationRequest=Gallery->AnimationPrepareGeneration;
    const FString Key=ReadyMotionKeys[Index];const bool Portable=Gallery->PreviewActor->Session.IsPortable();
    const bool Imported=Portable&&!Key.StartsWith(TEXT("states:"))&&!FOkSession::IsVideoClipKey(Key);
    const FString Root=Portable?FOkSession::CommonPairRoot():(UKKGameRoot::Get()?UKKGameRoot::Get()->Root:FString());
    TArray<uint8> Bytes;if(Imported)Bytes=FOkSession::ImportedSharedClipBytes(Key);
    auto Cancelled=MotionCancelled;TWeakObjectPtr<UOKInteractionWidget> Weak(this);TWeakObjectPtr<AKKPreviewActor> Actor=Gallery->PreviewActor;
    MotionPreparation->SetText(FText::FromString(TEXT("正在載入：")+Item+TEXT("；目前動作繼續播放")));
    Async(EAsyncExecution::ThreadPool,[Weak,Actor,Cancelled,Request,Generation,AnimationRequest,Root,Key,Item,Imported,Bytes=MoveTemp(Bytes)](){
        FScopeLock Guard(&GInteractionMotionPreparationMutex);if(Cancelled&&*Cancelled)return;
        FOkSession::BeginAnimationPreparation();ON_SCOPE_EXIT{FOkSession::EndAnimationPreparation();};
        const bool Ready=Imported?FOkSession::PrepareSharedClipBytes(Bytes):FOkSession::PrepareAnimationClip(Root,Key);
        AsyncTask(ENamedThreads::GameThread,[Weak,Actor,Request,Generation,AnimationRequest,Key,Item,Ready](){
            auto* UI=Weak.Get();if(!UI||!UI->Gallery||!Actor.IsValid()||UI->Gallery->PreviewActor!=Actor.Get()||UI->MotionGeneration!=Generation||UI->MotionSelectionGeneration!=Request||UI->Gallery->AnimationPrepareGeneration!=AnimationRequest)return;
            if(!Ready){UI->MotionPreparation->SetText(FText::FromString(TEXT("載入失敗：")+Item+TEXT("；請選其他動作")));return;}
            UI->Gallery->OnStopPair();
            if(Actor->SetGameAnimation(Key)){UI->Gallery->SyncGameAnimationSelection(Key);UI->MotionPreparation->SetText(FText::FromString(TEXT("正在播放：")+Item));}
            else UI->MotionPreparation->SetText(FText::FromString(TEXT("此動作無法套用至目前角色：")+Item));
        });
    });
}
void UOKInteractionWidget::OnSpeak(){
    if(!Gallery||!Gallery->PreviewActor||!Gallery->SpeechCombo)return;
    if(Gallery->SpeechIds.IsEmpty())Gallery->OnLoadSpeech();
    const int32 Index=Gallery->SpeechCombo->GetSelectedIndex();
    if(!Gallery->PreviewActor->SpeechLines.IsValidIndex(Index))return;
    const FString Path=Gallery->PreviewActor->SpeechLines[Index].AudioPath;
    const float Pitch=Gallery->PreviewActor->SpeechPitch;++SpeechGeneration;
    if(const auto* Pcm=SpeechCache.Find(Path)){SpeechCacheOrder.Remove(Path);SpeechCacheOrder.Add(Path);FString Error;Gallery->PreviewActor->StopHBreath();Gallery->bSpeechWasPlaying=Gallery->PreviewActor->PlaySpeechPcm(*Pcm,Pitch,Error);if(Gallery->SpeechStatus)Gallery->SpeechStatus->SetText(FText::FromString(Error.IsEmpty()?TEXT("說話中"):Error));return;}
    PreloadSpeech();
    if(Gallery->SpeechStatus)Gallery->SpeechStatus->SetText(FText::FromString(TEXT("語音準備中，完成後可立即播放")));
}
void UOKInteractionWidget::CacheSpeech(const FString& Path,TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm){
    if(!Pcm||SpeechCache.Contains(Path))return;
    const int64 Bytes=int64(Pcm->Samples.Num())*sizeof(int16),Limit=128*1024*1024;if(Bytes>Limit)return;
    const int32 Selected=Gallery&&Gallery->SpeechCombo?Gallery->SpeechCombo->GetSelectedIndex():INDEX_NONE;
    const FString Pinned=Gallery&&Gallery->PreviewActor&&Gallery->PreviewActor->SpeechLines.IsValidIndex(Selected)?Gallery->PreviewActor->SpeechLines[Selected].AudioPath:FString();
    while(SpeechCacheBytes+Bytes>Limit&&!SpeechCacheOrder.IsEmpty()) {const int32 Evict=SpeechCacheOrder.IndexOfByPredicate([&](const FString& Key){return Key!=Pinned;});if(Evict==INDEX_NONE)return;const FString Old=SpeechCacheOrder[Evict];SpeechCacheOrder.RemoveAt(Evict);if(const auto* Entry=SpeechCache.Find(Old))SpeechCacheBytes-=int64((*Entry)->Samples.Num())*sizeof(int16);SpeechCache.Remove(Old);}
    SpeechCache.Add(Path,Pcm);SpeechCacheOrder.Add(Path);SpeechCacheBytes+=Bytes;
}
void UOKInteractionWidget::OnSpeechSelection(FString,ESelectInfo::Type){++SpeechGeneration;if(SpeakButton)SpeakButton->SetIsEnabled(false);PreloadSpeech();}
void UOKInteractionWidget::PrepareSpeechCatalog(const FString& Root,int32 Personality,float Pitch){
    if(!Gallery||!IsValid(Gallery->PreviewActor))return;
    OnStopSpeech();if(SpeechCancelled)*SpeechCancelled=true;++SpeechPreparationGeneration;
    if(SpeechCatalogCancelled)*SpeechCatalogCancelled=true;SpeechCatalogCancelled=MakeShared<FThreadSafeBool,ESPMode::ThreadSafe>(false);const auto Cancelled=SpeechCatalogCancelled;
    const int32 Generation=++SpeechCatalogGeneration;TWeakObjectPtr<UOKInteractionWidget> Weak(this);TWeakObjectPtr<AKKPreviewActor> Actor=Gallery->PreviewActor;
    if(SpeakButton)SpeakButton->SetIsEnabled(false);
    if(!FMath::IsFinite(Pitch)||Pitch<=0.f){*Cancelled=true;if(Gallery->SpeechStatus)Gallery->SpeechStatus->SetText(FText::FromString(TEXT("語音音高無效")));return;}
    if(Gallery->SpeechStatus)Gallery->SpeechStatus->SetText(FText::FromString(TEXT("正在準備語音目錄")));
    Async(EAsyncExecution::ThreadPool,[Weak,Actor,Generation,Root,Personality,Pitch,Cancelled](){
        FScopeLock Lock(&GInteractionSpeechCatalogMutex);if(*Cancelled)return;
        TArray<FKKSpeechLine> Lines;FString Error;const bool Loaded=KKSpeech::LoadLines(Root,Personality,Lines,Error);
        if(*Cancelled)return;
        AsyncTask(ENamedThreads::GameThread,[Weak,Actor,Generation,Personality,Pitch,Loaded,Lines=MoveTemp(Lines),Error]() mutable {
            auto* UI=Weak.Get();if(!UI||!UI->Gallery||UI->SpeechCatalogGeneration!=Generation||!Actor.IsValid()||UI->Gallery->PreviewActor!=Actor.Get())return;
            auto* G=UI->Gallery.Get();Actor->SpeechLines=MoveTemp(Lines);Actor->SpeechPrevious=0.f;Actor->SpeechPitch=Pitch;
            G->SpeechIds.Reset();if(G->SpeechCombo)G->SpeechCombo->ClearOptions();
            if(!Loaded){if(G->SpeechStatus)G->SpeechStatus->SetText(FText::FromString(Error));return;}
            for(const auto& Line:Actor->SpeechLines){G->SpeechIds.Add(Line.Id);if(G->SpeechCombo)G->SpeechCombo->AddOption(Line.Text.IsEmpty()?Line.Id:Line.Text);}
            if(G->SpeechStatus)G->SpeechStatus->SetText(FText::FromString(FString::Printf(TEXT("%s · %d 句一般對話"),*G->HeroineVoiceLabel(Personality),G->SpeechIds.Num())));
            if(G->SpeechCombo&&!G->SpeechIds.IsEmpty())G->SpeechCombo->SetSelectedIndex(0);else UI->PreloadSpeech();
        });
    });
}
void UOKInteractionWidget::PreloadSpeech(){
    if(!Gallery||!Gallery->PreviewActor)return;
    if(SpeechCancelled)*SpeechCancelled=true;SpeechCancelled=MakeShared<FThreadSafeBool,ESPMode::ThreadSafe>(false);
    const auto Cancelled=SpeechCancelled;const int32 Generation=++SpeechPreparationGeneration;
    TArray<FKKSpeechLine> Lines=Gallery->PreviewActor->SpeechLines;const int32 Selected=Gallery->SpeechCombo?Gallery->SpeechCombo->GetSelectedIndex():INDEX_NONE;
    const FString SelectedPath=Lines.IsValidIndex(Selected)?Lines[Selected].AudioPath:FString();
    if(Gallery->SpeechStatus&&!SelectedPath.IsEmpty()&&!SpeechCache.Contains(SelectedPath))Gallery->SpeechStatus->SetText(FText::FromString(TEXT("正在準備所選語音")));
    if(Lines.IsValidIndex(Selected))Lines.Swap(0,Selected);
    Lines.RemoveAll([this](const FKKSpeechLine& Line){return SpeechCache.Contains(Line.AudioPath);});
    TWeakObjectPtr<UOKInteractionWidget> Weak(this);TWeakObjectPtr<AKKPreviewActor> Actor=Gallery->PreviewActor;
    Async(EAsyncExecution::ThreadPool,[Weak,Actor,Lines,Cancelled,Generation,SelectedPath](){
        int64 Bytes=0;
        for(const auto& Line:Lines){if(*Cancelled)return;TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm;FString Error;if(!KKSpeech::DecodeOgg(Line.AudioPath,Pcm,Error)){if(Line.AudioPath==SelectedPath)AsyncTask(ENamedThreads::GameThread,[Weak,Generation,Error](){auto* UI=Weak.Get();if(UI&&UI->Gallery&&UI->SpeechPreparationGeneration==Generation&&UI->Gallery->SpeechStatus)UI->Gallery->SpeechStatus->SetText(FText::FromString(Error));});continue;}const int64 Size=int64(Pcm->Samples.Num())*sizeof(int16);if(Size>128*1024*1024){if(Line.AudioPath==SelectedPath)AsyncTask(ENamedThreads::GameThread,[Weak,Actor,Generation](){auto* UI=Weak.Get();if(UI&&UI->Gallery&&UI->SpeechPreparationGeneration==Generation&&UI->Gallery->PreviewActor==Actor.Get()&&Actor.IsValid()&&UI->Gallery->SpeechStatus)UI->Gallery->SpeechStatus->SetText(FText::FromString(TEXT("所選語音超過128MiB容量限制，請選其他句子")));});continue;}if(Bytes+Size>128*1024*1024)break;Bytes+=Size;
            AsyncTask(ENamedThreads::GameThread,[Weak,Actor,Generation,SelectedPath,Path=Line.AudioPath,Pcm](){auto* UI=Weak.Get();if(UI&&UI->Gallery&&UI->SpeechPreparationGeneration==Generation&&UI->Gallery->PreviewActor==Actor.Get()&&Actor.IsValid()){UI->CacheSpeech(Path,Pcm);if(Path==SelectedPath&&UI->SpeechCache.Contains(Path)){if(UI->SpeakButton)UI->SpeakButton->SetIsEnabled(true);if(UI->Gallery->SpeechStatus&&!Actor->IsSpeaking())UI->Gallery->SpeechStatus->SetText(FText::FromString(TEXT("語音已準備，可立即播放")));}}});
        }
    });
}
void UOKInteractionWidget::OnStopSpeech(){++SpeechGeneration;if(Gallery)Gallery->OnStopSpeech();}

void UOKCharacterController::ActivateInteraction() {
    if(bAppearanceBusy||bPropertyBusy)return;
    if(MaterializeAppearance([](UOKCharacterController* G){G->ActivateInteraction();}))return;
    if(InteractionWidget||bLoadInFlight)return;
    if(Selected.Sex!=1||!Entries.IsValidIndex(SelectedIndex)){ShowAnimationStatus(TEXT("請先選擇女角"),true);return;}
    if(!IsValid(PreviewActor)||!FPaths::IsSamePath(PreviewCardPath,Entries[SelectedIndex].Path)||PreviewOutfitIndex!=OutfitIndex||!bModelLayout) {
        bEnterInteractionAfterLoad=true;OnLoad3D();return;
    }
    if(!PreviewActor->Session.IsOpen()){ShowAnimationStatus(TEXT("角色沒有可互動的原生骨架"),true);return;}
    ApplyPendingEditorMods();
    if(bEditorModsDirty){ShowAnimationStatus(TEXT("角色參數套用失敗，請檢查模組資料。"),true);return;}
    PreviewActor->Session.ApplyBodyPhysics(Selected.Body.BustSoftness,Selected.Body.BustWeight);
    if(PreviewActor->bShapeDirty||PreviewActor->Session.ShapeBody()!=Selected.Body.ShapeValueBody||PreviewActor->Session.ShapeFace()!=Selected.Face.ShapeValueFace)
        PreviewActor->ApplyShapes(Selected.Face.ShapeValueFace,Selected.Body.ShapeValueBody);
    CancelPairPreparation();
    if(PreviewActor->Session.IsPortable()&&!bPairCataloguePreparing)ResumePairCatalogue();
    PreviewActor->StopHBreath();
    // Prepare the default native controller/materials before enabling interaction.
    if(PreviewActor->PoseMode==0&&!PreviewActor->Session.IsPortable())PreviewActor->SetPoseMode(1);
    if(PreviewActor->PoseMode==0&&PreviewActor->Session.IsPortable()){
        const double PrepareStart=FPlatformTime::Seconds();
        if(!PreviewActor->Session.StaticInteractionFrame(0.f)){ShowAnimationStatus(TEXT("角色互動物理準備失敗"),true);return;}
        PreviewActor->RefreshSkinnedDisplay();
        UE_LOG(LogTemp,Display,TEXT("OK interaction runtime physics preparation_ms=%.3f"),(FPlatformTime::Seconds()-PrepareStart)*1000.);
    }
    // Shader/render readiness is a scene-entry cost, never the first live click.
    // Portable characters may enter without selecting an initial animation.
    PreviewActor->PreparePlaybackMaterials(!PreviewActor->Session.IsPortable());
    // Finish per-character solver preparation in the entry transaction. No
    // file parsing or factory initialization is deferred to the first click.
    FString BodyError;
    if(auto* PC=Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer())){
        PC->SetBodyTouchEnabled(false);
        if(!PC->BodyInteraction.Prepare(PreviewActor,BodyError))UE_LOG(LogTemp,Warning,TEXT("OK body interaction unavailable: %s"),*BodyError);
    }
    InteractionScene=GetWorld()->SpawnActor<AOKInteractionScene>();
    if(!InteractionScene)return;
    InteractionScene->SetActorLocation(PreviewActor->GetActorLocation()+FVector(0.,0.,-1.));
    InteractionWidget=CreateWidget<UOKInteractionWidget>(GetOwningPlayer());
    if(!InteractionWidget){InteractionScene->Destroy();InteractionScene=nullptr;return;}
    InteractionWidget->BodyPrepareAttempt=PreviewActor;InteractionWidget->BodyPreparationError=BodyError;
    bInteractionPreviousPoseSharing=PreviewActor->bShareIdenticalPose;
    PreviewActor->bShareIdenticalPose=true;PreviewActor->bUsePrivateGpuPlayback=true;PreviewActor->RefreshSkinnedDisplay();
    if(PartnerActor){PartnerActor->bUsePrivateGpuPlayback=true;PartnerActor->RefreshSkinnedDisplay();}
    PreviewActor->bReuseSpeechAudio=true;
    RefreshInteractionMotionSources();
    InteractionWidget->Gallery=this;InteractionWidget->Scene=InteractionScene;
    SetModelLayout(true);InteractionWidget->AddToViewport(110);if(bHScene&&(HPanel=CreateWidget<UOKHScenePanel>(GetOwningPlayer()))!=nullptr){HPanel->Gallery=this;HPanel->AddToViewport(111);}if(SpeechCombo)SpeechCombo->OnSelectionChanged.AddDynamic(InteractionWidget,&UOKInteractionWidget::OnSpeechSelection);InteractionWidget->PreloadSpeech();SetVisibility(ESlateVisibility::Collapsed);
    if(auto* PC=Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer()))PC->SetPaintPreviewActive(true);
    OnLoadSpeech();
    UE_LOG(LogTemp,Display,TEXT("OK interaction entered actor=%s card=%s"),*PreviewActor->GetName(),*PreviewCardPath);
}
void UOKCharacterController::OnExitInteraction() {
    if(auto* PC=Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer()))PC->SetBodyTouchEnabled(false);
    if(PairCombo)PairCombo->SetIsEnabled(true);if(PairStateCombo)PairStateCombo->SetIsEnabled(true);
    bEnterInteractionAfterLoad=false;
    if(!InteractionWidget&&!InteractionScene)return;
    CancelPairCatalogue();++PartnerLoadGeneration;bPartnerLoadInFlight=false;PendingPartnerPath.Empty();if(PartnerActor){PartnerActor->bShareIdenticalPose=false;PartnerActor->bUsePrivateGpuPlayback=false;PartnerActor->RefreshSkinnedDisplay();}CancelPairPreparation();if(PreviewActor)PreviewActor->bReuseSpeechAudio=false;OnStopSpeech();
    if(PreviewActor){PreviewActor->FollowHeadLookCamera(false);PreviewActor->bManualSpeechPattern=false;PreviewActor->bSpeechDrivesMouth=true;PreviewActor->bShareIdenticalPose=bInteractionPreviousPoseSharing;PreviewActor->bUsePrivateGpuPlayback=false;PreviewActor->RefreshSkinnedDisplay();}
    if(auto* PC=Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer()))if(PC->PaintSprayer){PC->PaintSprayer->StopSpray();PC->PaintSprayer->ClearPaint();}
    if(HPanel){HResult=HPanel->Result();HPanel->Gallery=nullptr;HPanel->RemoveFromParent();HPanel=nullptr;}
    if(InteractionWidget){InteractionWidget->RestoreControls();InteractionWidget->Gallery=nullptr;InteractionWidget->RemoveFromParent();InteractionWidget=nullptr;}
    if(InteractionScene){InteractionScene->SaveReport();InteractionScene->Destroy();InteractionScene=nullptr;}
    SyncShapeSliders();SetModelLayout(true);ApplyClickInputMode();
    UE_LOG(LogTemp,Display,TEXT("OK interaction returned to gallery"));
    if(InteractionReturned)InteractionReturned();
}


void UOKCharacterController::OnEnterInteraction(){ AOKInteractionMode::EnterReader(this); }
