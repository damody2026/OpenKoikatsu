#include "../OKInteractionScene.h"
#include "../OKGalleryWidget.h"
#include "../OpenKoikatsuPlayerController.h"
#include "../OKPaintSprayer.h"
#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
#include "Components/EditableTextBox.h"
#include "Dom/JsonObject.h"
#include "KKGameRoot.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/PoseableMeshComponent.h"
#include "SkeletalRenderPublic.h"
#include "ok_extract.h"
#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HighResScreenshot.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "DynamicRHI.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <dxgi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#if !UE_BUILD_SHIPPING
namespace {
struct FPresentSample {
    const void* SwapChain=nullptr;
    uint32 Count=0;
    double At=0.;
    bool Valid=false;
};
FPresentSample ReadPresentCount(){
    FPresentSample Result;
#if PLATFORM_WINDOWS
    // Query the existing viewport at phase boundaries only. No ETW service,
    // flush, per-frame query or simulated tick count substitutes for Present.
    if(!GDynamicRHI||(FCString::Strcmp(GDynamicRHI->GetName(),TEXT("D3D12"))!=0&&FCString::Strcmp(GDynamicRHI->GetName(),TEXT("D3D11"))!=0))return Result;
    if(!GEngine||!GEngine->GameViewport||!FSlateApplication::IsInitialized())return Result;
    const auto Window=GEngine->GameViewport->GetWindow();if(!Window.IsValid())return Result;
    auto* Viewport=static_cast<FRHIViewport*>(FSlateApplication::Get().GetRenderer()->GetViewportResource(*Window));
    if(!Viewport)return Result;
    auto* Swap=static_cast<IDXGISwapChain*>(Viewport->GetNativeSwapChain());if(!Swap)return Result;
    const double Before=FPlatformTime::Seconds();UINT Count=0;
    Result.Valid=SUCCEEDED(Swap->GetLastPresentCount(&Count));
    Result.At=(Before+FPlatformTime::Seconds())*.5;Result.Count=Count;Result.SwapChain=Swap;
#endif
    return Result;
}
struct FInteractionAudit : TSharedFromThis<FInteractionAudit> {
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UOKGalleryWidget> Gallery;
    FTimerHandle Timer;
    FString Card,Output;
    TArray<FTransform> FemalePairBefore,MalePairBefore;
    float PairHeld=0.f;int32 PairPoseBefore=0,PairSwitchChecks=0;double PairSwitchAt=0.,PairCallbackMs=0.;TWeakObjectPtr<AKKPreviewActor> PreviousMale;
    bool SpeedAudit=false,SpeedMeasuring=false;int32 SpeedPhase=0,SpeedRequests=0;
    double SpeedWallAt=0.,SpeedCallbackMaxMs=0.,SpeedElapsedAt=0.,SpeedWorldAt=0.;
    TArray<double> SpeedMeans,SpeedFrames;
    bool PairEyes=false;FTransform EyeCameraBefore;int32 EyeProbe=0;double EyeProbeAt=0.;
    bool PairProfile=false;bool AssignedWorldAB=false;double PairProfileAt=0.;int32 PairProfilePhase=0;
    bool PairWatergunProfile=false;FName SprayFollowBone;FVector SprayFollowLocal;double SprayTotals[8]={};uint64 SpraySamples=0;
    bool PairInteractionProfile=false;double ControlStarted=0.,ControlMouthAt=-1.;int32 ControlRequests=0;bool ControlAudioObserved=false;FRotator ControlCameraRotation;
    void UpdateControlInputs(){
        TRACE_CPUPROFILER_EVENT_SCOPE(OK_InteractionControlInput);
        auto* G=Gallery.Get();if(!G||!G->InteractionWidget||!G->PreviewActor||Stage<81||Stage>82)return;
        auto* UI=G->InteractionWidget.Get();auto* Actor=G->PreviewActor.Get();const double T=FPlatformTime::Seconds()-ControlStarted;
        const bool Combined=PairProfilePhase>=6&&PairProfilePhase<=8;
        if(PairProfilePhase==2||Combined)Actor->StudioCam->SetActorRotation(ControlCameraRotation+FRotator(FMath::Sin(T)*2.,FMath::Cos(T)*2.,0));
        if(PairProfilePhase==3||(Combined&&PairProfilePhase!=7)){
            if(T-ControlMouthAt>=.25){const int32 Count=Actor->GetMouthPatternCount();if(Count>0)G->MouthPatternCombo->SetSelectedIndex(int32(T*4)%Count);ControlMouthAt=T;}
            G->OnMouthChanged(.5f+.4f*FMath::Sin(float(T)*3.f));++ControlRequests;
        }
        if(PairProfilePhase==4||PairProfilePhase==7){if(!Actor->IsSpeaking())UI->OnSpeak();ControlAudioObserved|=Actor->IsSpeaking();}
        if(PairProfilePhase==5||Combined){UI->OnBelly(.5f+.2f*FMath::Sin(float(T)*2.f));++ControlRequests;}
        if(PairProfilePhase==6||PairProfilePhase==7){auto* Role=PairProfilePhase==6?Actor:G->PartnerActor.Get();if(auto* PC=Cast<AOpenKoikatsuPlayerController>(G->GetOwningPlayer()))if(Role&&Role->BodySkel)PC->ScriptedPaintAim=Role->BodySkel->GetSocketTransform(SprayFollowBone).TransformPosition(SprayFollowLocal);}
    }
    bool PlaybackBudgetPassed=true,PlaybackPresentPassed=true;
    static float AuditSpeed(int32 Index){const float Rates[]={1.f,.25f,1.f,.5f,1.f,2.f,1.f,4.f,1.f,-1.f,1.f,0.f,1.f};return Rates[Index];}
    void ChangeSpeed(UOKGalleryWidget* G,float Rate){const double Begin=FPlatformTime::Seconds();G->PairSpeedSlider->SetValue(Rate/4.f);G->OnPairSpeed(Rate/4.f);SpeedCallbackMaxMs=FMath::Max(SpeedCallbackMaxMs,(FPlatformTime::Seconds()-Begin)*1000.);++SpeedRequests;}
    static bool PoseChanged(const TArray<FTransform>& Before,AKKPreviewActor* Role){if(!Role||!Role->BodySkel)return false;const auto& Now=Role->BodySkel->BoneSpaceTransforms;for(int32 I=0;I<FMath::Min(Before.Num(),Now.Num());++I)if(!Before[I].Equals(Now[I],1.e-5f))return true;return false;}
    static bool SelectedPairCommitted(UOKGalleryWidget* G){
        if(!G->PairedPlayback||!G->PartnerActor)return false;
        const int32 Index=G->PairCombo->GetSelectedIndex()-1;const FString State=G->PairStateCombo->GetSelectedOption();
        if(!G->NativePairs.IsValidIndex(Index)||!G->FemalePairStates.Contains(State)||!G->MalePairStates.Contains(State))return false;
        auto Row=G->NativePairs[Index];
        return G->PairedPlayback->GetState()==State
            &&G->PairedPlayback->GetFemaleClip()==G->PreviewActor->Session.NativePairClip(G->FemalePairStates[State],int32(Row->GetNumberField(TEXT("female_layer"))))
            &&G->PairedPlayback->GetMaleClip()==G->PartnerActor->Session.NativePairClip(G->MalePairStates[State],int32(Row->GetNumberField(TEXT("male_layer"))));
    }
    int32 Stage=0,Waits=0,Pattern=0,MouthCount=0;
    double At=0.,MaximumControlMs=0.,EntryRequestedAt=0.;
    bool Portable=false;
    bool SkipPaint=false;
    bool PairPaint=false;int32 PaintRole=0,PairPaintHitsBefore=0,PaintAnchorSurface=INDEX_NONE,PaintAnchorFace=INDEX_NONE;
    double PairPaintDepositedBefore=0.;FVector PaintAnchorBefore;FTransform PaintCameraBefore;
    TWeakObjectPtr<UProceduralMeshComponent> PaintTargetComponent;int32 PaintTargetTriangle=INDEX_NONE;
    bool PairPaintTarget(AKKPreviewActor* Role,FVector& Target,bool Select){
        if(Select){Role->EnsurePrivateGpuQueryGeometry();PaintTargetComponent.Reset();PaintTargetTriangle=INDEX_NONE;double Highest=-DBL_MAX;
            for(const auto& Part:Role->Parts)if(Part.PrivateGpuDisplay.IsValid()&&Part.Comp&&Part.Comp->IsVisible()){
                auto* C=Part.Comp.Get();const auto* Section=C->GetProcMeshSection(0);if(!Section||!Section->bSectionVisible)continue;const FMatrix Matrix=C->GetRenderMatrix();
                for(int32 I=0;I+2<Section->ProcIndexBuffer.Num();I+=3){FVector P[3];for(int32 E=0;E<3;++E)P[E]=Matrix.TransformPosition(Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+E]].Position);
                    const FVector Center=(P[0]+P[1]+P[2])/3.;if(Center.ContainsNaN()||FVector::CrossProduct(P[1]-P[0],P[2]-P[0]).Length()<.04)continue;
                    if(Center.Z>Highest){Highest=Center.Z;PaintTargetComponent=C;PaintTargetTriangle=I;}}
            }
        }
        auto* C=PaintTargetComponent.Get();if(!C)return false;Role->EnsurePrivateGpuQueryGeometry(C);
        const auto* Section=C->GetProcMeshSection(0);if(!Section||PaintTargetTriangle<0||PaintTargetTriangle+2>=Section->ProcIndexBuffer.Num())return false;
        Target=FVector::ZeroVector;for(int32 E=0;E<3;++E)Target+=FVector(C->GetRenderMatrix().TransformPosition(Section->ProcVertexBuffer[Section->ProcIndexBuffer[PaintTargetTriangle+E]].Position))/3.;
        return !Target.ContainsNaN();
    }
    float SpeedStartPhase=0.f;
    float LoopProbePhase=0.f;int32 LoopProbeBone=INDEX_NONE;FTransform LoopProbePose;
    bool HeardSpeech=false,PaintScreenshot=false;
    int32 PaintHits=0;double PaintDeposited=0.;
    FString FrameReport;
    TArray<float> FixedInteractionBody;
    bool Measure=false,BudgetPassed=true;int32 Phase=0;
    FPresentSample PresentStart;
    int32 PairProfilePaintStart=0;
    bool PresentPassed=true;
    double LastLiveSwitch=-1.;int32 LiveSwitches=0;
    void Finish(const FString& Error){
        const auto Keep=AsShared();
        if(auto* G=Gallery.Get())if(G->InteractionScene)G->InteractionScene->ControlProfileTick={};
        if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);
        const bool Visible=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_VISIBLE"))==TEXT("1");
        const FString Report=FString::Printf(TEXT("audit_mode=%s\naudit_completed=%d\nfunctional_pass=%d\nerror=%s\nmouth_patterns=%d\nspeech_active_observed=%d\npaint_hits=%d\npaint_deposited=%.9f\nmaximum_synchronous_control_ms=%.3f\n%s\nvisible_window_requested=%d\n120fps_present_verified=%d\n"),PairProfile?TEXT("paired_profile"):Measure?TEXT("performance"):TEXT("functional"),Error.IsEmpty(),!PairProfile&&!Measure&&Error.IsEmpty(),*Error,MouthCount,HeardSpeech,PaintHits,PaintDeposited,MaximumControlMs,*FrameReport,Visible,PairProfile?Error.IsEmpty()&&PairProfilePhase>=3&&Visible&&BudgetPassed&&PresentPassed:Measure&&Phase>=6&&Error.IsEmpty()&&Visible&&BudgetPassed&&PresentPassed);
        const FString ScopedReport=Report+FString::Printf(TEXT("120fps_playback_present_verified=%d\nplayback_scope_excludes_entry_budget=1\n"),PairProfile&&Error.IsEmpty()&&PairProfilePhase>=3&&Visible&&PlaybackBudgetPassed&&PlaybackPresentPassed);
        FFileHelper::SaveStringToFile(ScopedReport,*(Output/TEXT("report.txt")));
        UE_LOG(LogTemp,Display,TEXT("OK interaction audit %s"),*ScopedReport);FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);
    }
    void Tick(){
        const auto Keep=AsShared();
        auto* W=World.Get();if(!W)return;
        if(++Waits>6000){Finish(TEXT("Audit timed out"));return;}
        if(Stage==0){
            for(TObjectIterator<UOKGalleryWidget> It;It;++It)if(It->GetWorld()==W){Gallery=*It;break;}
            auto* G=Gallery.Get();if(!G)return;
            // Visible audit windows must not inherit a held physical mouse
            // button. The functional watergun stages explicitly enable fire.
            if(auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController())){
                PC->ScriptedPaintAim=FVector::ZeroVector;PC->bScriptedPaintFire=false;
            }
            G->bIndependentCards=Portable;G->Entries.Reset();FKKCardListEntry Entry;Entry.Path=Card;Entry.FileName=FPaths::GetCleanFilename(Card);G->Entries.Add(Entry);G->OutfitIndex=0;G->SelectIndex(0);EntryRequestedAt=FPlatformTime::Seconds();G->OnEnterInteraction();Stage=1;return;
        }
        auto* G=Gallery.Get();if(!G){Finish(TEXT("Gallery destroyed"));return;}
        if(Stage==1){if(G->bLoadInFlight)return;if(!G->InteractionWidget||!G->InteractionScene){Finish(TEXT("Showcase entry failed"));return;}
            auto Quality=[](const TCHAR* Name){const auto* V=IConsoleManager::Get().FindConsoleVariable(Name);return V?V->GetFloat():-1.f;};
            const bool NativeTAA=Quality(TEXT("r.AntiAliasingMethod"))==2.f&&Quality(TEXT("r.ScreenPercentage"))==100.f&&Quality(TEXT("r.SecondaryScreenPercentage.GameViewport"))==100.f&&Quality(TEXT("r.DynamicRes.OperationMode"))==0.f;
            FrameReport+=FString::Printf(TEXT("native_resolution_taa=%d\n"),NativeTAA);
            if(!NativeTAA){Finish(TEXT("Character viewport lost native resolution / TAA settings"));return;}
            FixedInteractionBody=G->Selected.Body.ShapeValueBody;const double EntryMs=(FPlatformTime::Seconds()-EntryRequestedAt)*1000.;
            FrameReport+=FString::Printf(TEXT("apng_entry=%d\nentry_to_controls_ms=%.3f\nentry_within_10_seconds=%d\n"),Portable,EntryMs,EntryMs<=10000.);
            FrameReport+=FString::Printf(TEXT("background_display_pending_at_entry=%d\n"),!G->PreviewActor->bPlaybackMaterialsReady);
            if(Portable&&!G->PreviewActor->Session.IsPortable()){Finish(TEXT("APNG entry was not independent"));return;}
            const bool IgnoreEntryBudget=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_IGNORE_ENTRY_BUDGET"))==TEXT("1");
            FrameReport+=FString::Printf(TEXT("entry_budget_enforced=%d\n"),!IgnoreEntryBudget);
            if(Portable&&EntryMs>10000.){BudgetPassed=false;if(!PairProfile&&!IgnoreEntryBudget){Finish(TEXT("APNG entry exceeded 10 seconds"));return;}}
            if(PairProfile){G->InteractionWidget->PartnerPath->SetText(FText::FromString(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_PARTNER"))));G->InteractionWidget->OnLoadPartner();At=W->GetTimeSeconds();Stage=20;return;}
            At=W->GetTimeSeconds();Stage=Measure?100:9;return;}
        if(Stage==27){if(G->PartnerLoadsPending>0)return;if(G->PartnerActor){Finish(TEXT("Stale male load published after return"));return;}G->OnEnterInteraction();if(!G->InteractionWidget){Finish(TEXT("Pair reentry failed"));return;}G->OnExitInteraction();Finish(FString());return;}
        auto* A=G->PreviewActor.Get();auto* UI=G->InteractionWidget.Get();if(!A||!UI){Finish(TEXT("Missing live character or controls"));return;}
        const double Age=W->GetTimeSeconds()-At;
        if(Stage==50){
            if(G->PairCatalogueFinished&&!*G->PairCatalogueFinished)return;
            if(G->bPairPreparing||!SelectedPairCommitted(G))return;
            G->PairedPlayback->bMeasureAdvance=true;UI->OnHeadCamera(true);A->FollowEyeLookCamera(A->EyeLookPattern);
            if(AssignedWorldAB)FPlatformMisc::SetEnvironmentVar(TEXT("OK_ASSIGNED_WORLD_FRAME"),TEXT("0"));
            if(PairWatergunProfile){Stage=70;return;}
            if(PairInteractionProfile){ControlCameraRotation=A->StudioCam->GetActorRotation();G->InteractionScene->AddTickPrerequisiteActor(G->PairedPlayback);G->InteractionScene->ControlProfileTick=[Run=AsShared()](){Run->UpdateControlInputs();};Stage=80;return;}
            PairProfileAt=FPlatformTime::Seconds();Stage=51;return;
        }
        if(Stage==80||Stage==81||Stage==82){
            auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());auto* S=PC?PC->PaintSprayer.Get():nullptr;if(!S){Finish(TEXT("Controls profile missing watergun"));return;}
            if(Stage==80){
                if(PairProfilePhase==4||PairProfilePhase==7){const int32 Voice=G->SpeechCombo->GetSelectedIndex();if(!A->SpeechLines.IsValidIndex(Voice)||!UI->SpeechCache.Contains(A->SpeechLines[Voice].AudioPath))return;}
                PC->bScriptedPaintFire=false;PC->ScriptedPaintAim=FVector::ZeroVector;S->StopSpray();if(PairProfilePhase!=8)S->ClearPaint();UI->OnStopSpeech();
                UI->OnHeadCamera(PairProfilePhase==2||(PairProfilePhase>=6&&PairProfilePhase<=8));G->OnEyeLookCamera(PairProfilePhase==2||(PairProfilePhase>=6&&PairProfilePhase<=8));UI->OnAutomaticMouth(PairProfilePhase==4||PairProfilePhase==7);ChangeSpeed(G,PairProfilePhase==1?4.f:1.f);
                A->StudioCam->SetActorRotation(ControlCameraRotation);
                if(PairProfilePhase==6||PairProfilePhase==7){auto* Role=PairProfilePhase==6?A:G->PartnerActor.Get();FVector Target;if(!PairPaintTarget(Role,Target,true)){Finish(TEXT("Controls spray target missing"));return;}
                    double Best=DBL_MAX;SprayFollowBone=NAME_None;for(int32 B=0;B<Role->BodySkel->GetNumBones();++B){const FName Name=Role->BodySkel->GetBoneName(B);const double D=FVector::DistSquared(Role->BodySkel->GetBoneLocation(Name),Target);if(D<Best){Best=D;SprayFollowBone=Name;}}
                    if(SprayFollowBone.IsNone()){Finish(TEXT("Controls follow bone missing"));return;}SprayFollowLocal=Role->BodySkel->GetSocketTransform(SprayFollowBone).InverseTransformPosition(Target);A->StudioCam->SetActorLocation(Target+FVector(0,0,120));ControlCameraRotation=FVector(0,0,-1).Rotation();A->StudioCam->SetActorRotation(ControlCameraRotation);PC->bScriptedPaintFire=true;
                }
                ControlStarted=PairProfileAt=FPlatformTime::Seconds();ControlMouthAt=-1.;ControlRequests=0;ControlAudioObserved=false;Stage=81;return;
            }
            if(Stage==81){if(FPlatformTime::Seconds()-PairProfileAt<3.)return;G->InteractionScene->ResetPerformance();G->PairedPlayback->ResetAdvanceMetrics();PresentStart=ReadPresentCount();PairProfilePaintStart=S->HitCount;PairPaintDepositedBefore=S->Deposited;
                const double Values[]={S->TotalUpdateMs,S->TotalGeometryMs,S->TotalSimulationMs,S->TotalCoatingMs,S->TotalBallisticMs,S->TotalMaskTransportMs,S->TotalFilmTransportMs,S->TotalDripUpdateMs};for(int32 I=0;I<8;++I)SprayTotals[I]=Values[I];
                UE_LOG(LogTemp,Display,TEXT("OK controls profile begin phase=%d wall=%.6f"),PairProfilePhase,FPlatformTime::Seconds());{TRACE_CPUPROFILER_EVENT_SCOPE(OK_AllControlsProfileBegin);}PairProfileAt=FPlatformTime::Seconds();Stage=82;return;}
            if(FPlatformTime::Seconds()-PairProfileAt<10.)return;{TRACE_CPUPROFILER_EVENT_SCOPE(OK_AllControlsProfileEnd);}
            const auto* Scene=G->InteractionScene.Get();const double N=double(Scene->FrameSamples);const auto End=ReadPresentCount();if(N<=0){Finish(TEXT("No controls frame samples"));return;}
            const double FPS=PresentStart.Valid&&End.Valid&&PresentStart.SwapChain==End.SwapChain&&End.At>PresentStart.At?double(uint32(End.Count-PresentStart.Count))/(End.At-PresentStart.At):0.;const int32 Hits=S->HitCount-PairProfilePaintStart;const double Deposit=S->Deposited-PairPaintDepositedBefore;
            FrameReport+=FString::Printf(TEXT("controls_phase_%d_mean_ms=%.6f\ncontrols_phase_%d_max_ms=%.6f\ncontrols_phase_%d_present_fps=%.6f\ncontrols_phase_%d_distribution=%s\ncontrols_phase_%d_paint=%s\ncontrols_phase_%d_hits=%d\ncontrols_phase_%d_deposited=%.9f\ncontrols_phase_%d_requests=%d\ncontrols_phase_%d_audio_observed=%d\n"),PairProfilePhase,Scene->FrameTotal/N,PairProfilePhase,Scene->FrameMaximum,PairProfilePhase,FPS,PairProfilePhase,*Scene->PerformanceSummary(),PairProfilePhase,*S->Statistics(),PairProfilePhase,Hits,PairProfilePhase,Deposit,PairProfilePhase,ControlRequests,PairProfilePhase,ControlAudioObserved);
            const double Values[]={S->TotalUpdateMs,S->TotalGeometryMs,S->TotalSimulationMs,S->TotalCoatingMs,S->TotalBallisticMs,S->TotalMaskTransportMs,S->TotalFilmTransportMs,S->TotalDripUpdateMs};const TCHAR* Names[]={TEXT("actor"),TEXT("geometry"),TEXT("simulation"),TEXT("coating"),TEXT("ballistic"),TEXT("mask"),TEXT("film"),TEXT("drip")};for(int32 I=0;I<8;++I)FrameReport+=FString::Printf(TEXT("controls_phase_%d_%s_ms=%.6f\n"),PairProfilePhase,Names[I],(Values[I]-SprayTotals[I])/N);
            UE_LOG(LogTemp,Display,TEXT("OK controls profile end phase=%d wall=%.6f"),PairProfilePhase,FPlatformTime::Seconds());
            if((PairProfilePhase==6||PairProfilePhase==7)&&(Hits<=0||Deposit<=0)){Finish(TEXT("Controls combined spray produced no fresh paint"));return;}
            if((PairProfilePhase==4||PairProfilePhase==7)&&!ControlAudioObserved){Finish(TEXT("Controls voice not active"));return;}
            if((PairProfilePhase<=5||PairProfilePhase==9)&&(Hits!=0||S->SurfaceCount()!=0)){Finish(TEXT("Controls clean phase contaminated"));return;}
            BudgetPassed&=Scene->FrameTotal/N<=8.&&Scene->FrameMaximum<=300.;PresentPassed&=FPS>120.;PlaybackBudgetPassed=BudgetPassed;PlaybackPresentPassed=PresentPassed;
            if(++PairProfilePhase==10){PC->bScriptedPaintFire=false;S->StopSpray();FrameReport+=TEXT("pair_profile_completed=1\nall_controls_profile_completed=1\n");Finish(FString());return;}Stage=80;return;
        }
        if(Stage==70||Stage==71||Stage==72){
            auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());auto* S=PC?PC->PaintSprayer.Get():nullptr;
            auto* Role=PairProfilePhase<3?A:G->PartnerActor.Get();
            if(!S||!Role||!Role->BodySkel){Finish(TEXT("Watergun profile actors/sprayer unavailable"));return;}
            const bool Shooting=PairProfilePhase==1||PairProfilePhase==4;
            if(Stage==70){
                PC->bScriptedPaintFire=false;S->StopSpray();
                if(PairProfilePhase==0||PairProfilePhase==3){
                    S->ClearPaint();FVector Target;if(!PairPaintTarget(Role,Target,true)){Finish(TEXT("Watergun profile target unavailable"));return;}
                    double Best=DBL_MAX;SprayFollowBone=NAME_None;
                    for(int32 B=0;B<Role->BodySkel->GetNumBones();++B){const FName Name=Role->BodySkel->GetBoneName(B);const double D=FVector::DistSquared(Role->BodySkel->GetBoneLocation(Name),Target);if(D<Best){Best=D;SprayFollowBone=Name;}}
                    if(SprayFollowBone.IsNone()){Finish(TEXT("Watergun profile follow bone unavailable"));return;}
                    SprayFollowLocal=Role->BodySkel->GetSocketTransform(SprayFollowBone).InverseTransformPosition(Target);
                    A->StudioCam->SetActorLocation(Target+FVector(0,0,120));A->StudioCam->SetActorRotation(FVector(0,0,-1).Rotation());
                }
                if(PairProfilePhase==6){S->ClearPaint();PC->ScriptedPaintAim=FVector::ZeroVector;}
                PC->bScriptedPaintFire=Shooting;PairProfileAt=FPlatformTime::Seconds();Stage=71;return;
            }
            if(PairProfilePhase<6)PC->ScriptedPaintAim=Role->BodySkel->GetSocketTransform(SprayFollowBone).TransformPosition(SprayFollowLocal);
            if(Stage==71){
                if(FPlatformTime::Seconds()-PairProfileAt<3.)return;
                G->InteractionScene->ResetPerformance();G->PairedPlayback->ResetAdvanceMetrics();PresentStart=ReadPresentCount();
                SpraySamples=S->UpdateSamples;const double Values[]={S->TotalUpdateMs,S->TotalGeometryMs,S->TotalSimulationMs,S->TotalCoatingMs,S->TotalBallisticMs,S->TotalMaskTransportMs,S->TotalFilmTransportMs,S->TotalDripUpdateMs};for(int32 I=0;I<8;++I)SprayTotals[I]=Values[I];
                PairProfilePaintStart=S->HitCount;PairPaintDepositedBefore=S->Deposited;
                TRACE_CPUPROFILER_EVENT_SCOPE(OK_PairWatergunProfileBegin);
                UE_LOG(LogTemp,Display,TEXT("OK watergun profile begin phase=%d wall=%.6f"),PairProfilePhase,FPlatformTime::Seconds());PairProfileAt=FPlatformTime::Seconds();Stage=72;return;
            }
            if(FPlatformTime::Seconds()-PairProfileAt<15.)return;
            TRACE_CPUPROFILER_EVENT_SCOPE(OK_PairWatergunProfileEnd);
            auto* Scene=G->InteractionScene.Get();const FPresentSample End=ReadPresentCount();const double N=double(Scene->FrameSamples);
            if(N<=0){Finish(TEXT("Watergun profile missing frame samples"));return;}
            const double FPS=PresentStart.Valid&&End.Valid&&PresentStart.SwapChain==End.SwapChain&&End.At>PresentStart.At?double(uint32(End.Count-PresentStart.Count))/(End.At-PresentStart.At):0.;
            const int32 Hits=S->HitCount-PairProfilePaintStart;const double Deposit=S->Deposited-PairPaintDepositedBefore;
            FrameReport+=FString::Printf(TEXT("watergun_phase_%d_frame_mean_ms=%.6f\nwatergun_phase_%d_frame_max_ms=%.6f\nwatergun_phase_%d_present_fps=%.6f\nwatergun_phase_%d_game_ms=%.6f\nwatergun_phase_%d_render_ms=%.6f\nwatergun_phase_%d_rhi_ms=%.6f\nwatergun_phase_%d_gpu_ms=%.6f\nwatergun_phase_%d_hits=%d\nwatergun_phase_%d_deposited=%.9f\nwatergun_phase_%d_distribution=%s\nwatergun_phase_%d_statistics=%s\n"),PairProfilePhase,Scene->FrameTotal/N,PairProfilePhase,Scene->FrameMaximum,PairProfilePhase,FPS,PairProfilePhase,Scene->GameTotal/N,PairProfilePhase,Scene->RenderTotal/N,PairProfilePhase,Scene->RHITotal/N,PairProfilePhase,Scene->GPUTotal/N,PairProfilePhase,Hits,PairProfilePhase,Deposit,PairProfilePhase,*Scene->PerformanceSummary(),PairProfilePhase,*S->Statistics());
            const double Values[]={S->TotalUpdateMs,S->TotalGeometryMs,S->TotalSimulationMs,S->TotalCoatingMs,S->TotalBallisticMs,S->TotalMaskTransportMs,S->TotalFilmTransportMs,S->TotalDripUpdateMs};const TCHAR* Names[]={TEXT("actor"),TEXT("geometry"),TEXT("simulation"),TEXT("coating"),TEXT("ballistic"),TEXT("mask"),TEXT("film"),TEXT("drip")};
            for(int32 I=0;I<8;++I)FrameReport+=FString::Printf(TEXT("watergun_phase_%d_%s_mean_per_frame_ms=%.6f\n"),PairProfilePhase,Names[I],(Values[I]-SprayTotals[I])/N);
            FrameReport+=FString::Printf(TEXT("watergun_phase_%d_samples=%llu\nwatergun_phase_%d_actor_samples=%llu\n"),PairProfilePhase,Scene->FrameSamples,PairProfilePhase,S->UpdateSamples-SpraySamples);
            UE_LOG(LogTemp,Display,TEXT("OK watergun profile end phase=%d wall=%.6f"),PairProfilePhase,FPlatformTime::Seconds());
            if(Shooting&&(Hits<=0||Deposit<=0)){Finish(TEXT("Watergun profile spray did not deposit"));return;}
            if((PairProfilePhase==0||PairProfilePhase==3||PairProfilePhase==6)&&(Hits!=0||S->SurfaceCount()!=0)){Finish(TEXT("Watergun clean baseline contaminated by paint"));return;}
            BudgetPassed&=Scene->FrameTotal/N<=8.&&Scene->FrameMaximum<=300.;PresentPassed&=FPS>120.;PlaybackBudgetPassed=BudgetPassed;PlaybackPresentPassed=PresentPassed;
            PaintHits+=Hits;PaintDeposited+=Deposit;
            if(++PairProfilePhase==7){PC->bScriptedPaintFire=false;S->StopSpray();PC->ScriptedPaintAim.Reset();FrameReport+=TEXT("pair_profile_completed=1\nwatergun_profile_completed=1\n");Finish(FString());return;}
            Stage=70;return;
        }
        if(Stage==51){
            if(FPlatformTime::Seconds()-PairProfileAt<3.)return;
            A->PreparePlaybackMaterials(false);G->PartnerActor->PreparePlaybackMaterials(false);
            G->InteractionScene->ResetPerformance();G->PairedPlayback->ResetAdvanceMetrics();PresentStart=ReadPresentCount();
            if(auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController())){
                PairProfilePaintStart=PC->PaintSprayer?PC->PaintSprayer->HitCount:0;
            }
            UE_LOG(LogTemp,Display,TEXT("OK pair profile begin phase=%d elapsed=%.6f"),PairProfilePhase,G->PairedPlayback->GetElapsed());
            FrameReport+=FString::Printf(TEXT("pair_profile_%d_speed=%.3f\npair_profile_%d_state=%s\npair_profile_%d_selected_pair=%s\n"),PairProfilePhase,G->PairedPlayback->GetSpeed(),PairProfilePhase,*G->PairedPlayback->GetState(),PairProfilePhase,*G->PairCombo->GetSelectedOption());
            if(AssignedWorldAB)FrameReport+=FString::Printf(TEXT("pair_profile_%d_assigned_world_frame=%d\n"),PairProfilePhase,PairProfilePhase==1);
            if(PairProfilePhase==0){TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairProfileQuiet0);}
            else if(PairProfilePhase==1){TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairProfileQuiet1);}
            else if(AssignedWorldAB){TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairProfileQuiet2);}
            else{TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairProfileVerbose);}
            PairProfileAt=FPlatformTime::Seconds();Stage=52;return;
        }
        if(Stage==52){
            if(FPlatformTime::Seconds()-PairProfileAt<(!AssignedWorldAB&&PairProfilePhase==2?5.:15.))return;
            auto* Scene=G->InteractionScene.Get();auto* P=G->PairedPlayback.Get();const FPresentSample End=ReadPresentCount();
            UE_LOG(LogTemp,Display,TEXT("OK pair profile end phase=%d elapsed=%.6f"),PairProfilePhase,P->GetElapsed());
            if(!Scene->FrameSamples||!P->AdvanceSamples){Finish(TEXT("Pair profile has no samples"));return;}
            const double FPS=PresentStart.Valid&&End.Valid&&PresentStart.SwapChain==End.SwapChain&&End.At>PresentStart.At?double(uint32(End.Count-PresentStart.Count))/(End.At-PresentStart.At):0.;
            FrameReport+=FString::Printf(TEXT("pair_profile_%d_samples=%llu\npair_profile_%d_advance_mean_ms=%.6f\npair_profile_%d_frame_mean_ms=%.6f\npair_profile_%d_frame_max_ms=%.6f\npair_profile_%d_present_fps=%.6f\npair_profile_%d_game_ms=%.6f\npair_profile_%d_render_ms=%.6f\npair_profile_%d_rhi_ms=%.6f\npair_profile_%d_gpu_ms=%.6f\npair_profile_%d_wait_ms=%.6f\n"),PairProfilePhase,Scene->FrameSamples,PairProfilePhase,P->AdvanceTotalMs/P->AdvanceSamples,PairProfilePhase,Scene->FrameTotal/Scene->FrameSamples,PairProfilePhase,Scene->FrameMaximum,PairProfilePhase,FPS,PairProfilePhase,Scene->GameTotal/Scene->FrameSamples,PairProfilePhase,Scene->RenderTotal/Scene->FrameSamples,PairProfilePhase,Scene->RHITotal/Scene->FrameSamples,PairProfilePhase,Scene->GPUTotal/Scene->FrameSamples,PairProfilePhase,Scene->WaitTotal/Scene->FrameSamples);
            FrameReport+=FString::Printf(TEXT("pair_profile_%d_distribution=%s\n"),PairProfilePhase,*Scene->PerformanceSummary());
            if(auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController())){
                const int32 Hits=(PC->PaintSprayer?PC->PaintSprayer->HitCount:0)-PairProfilePaintStart;
                FrameReport+=FString::Printf(TEXT("pair_profile_%d_unrequested_paint_hits=%d\n"),PairProfilePhase,Hits);
                BudgetPassed&=Hits==0;
                PlaybackBudgetPassed&=Hits==0;
            }
            if(PairProfilePhase<2||AssignedWorldAB){BudgetPassed&=Scene->FrameTotal/Scene->FrameSamples<=8.&&Scene->FrameMaximum<=300.;PresentPassed&=FPS>=119.5;}
            if(PairProfilePhase<2||AssignedWorldAB){PlaybackBudgetPassed&=Scene->FrameTotal/Scene->FrameSamples<=8.&&Scene->FrameMaximum<=300.;PlaybackPresentPassed&=FPS>120.;}
            if(++PairProfilePhase==3){FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_PROFILE"),TEXT("0"));FrameReport+=TEXT("pair_profile_completed=1\n");Finish(FString());return;}
            if(AssignedWorldAB)FPlatformMisc::SetEnvironmentVar(TEXT("OK_ASSIGNED_WORLD_FRAME"),PairProfilePhase==1?TEXT("1"):TEXT("0"));
            else {
                if(PairProfilePhase==1)G->PairCombo->SetSelectedIndex(FMath::Min(2,G->PairCombo->GetOptionCount()-1));
                if(PairProfilePhase==2)FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_PROFILE"),TEXT("1"));
            }
            PairProfileAt=FPlatformTime::Seconds();Stage=51;return;
        }
        if(Stage==9){
            if(Age>60.){Finish(FString::Printf(TEXT("Solo motion preparation timed out: source=%s keys=%d materials=%d sources=%d"),*UI->MotionSource,UI->ReadyMotionKeys.Num(),A->bPlaybackMaterialsReady,A->Session.ClipSources().Num()));return;}
            if(!UI->ReadyMotionCombo||!UI->ReadyMotionCombo->GetIsEnabled()||UI->ReadyMotionKeys.IsEmpty())return;
            FrameReport+=FString::Printf(TEXT("request_to_first_ready_motion_ms=%.3f\n"),(FPlatformTime::Seconds()-EntryRequestedAt)*1000.);
            UI->ReadyMotionCombo->SetSelectedIndex(1);
            if(G->SelectedGameClip!=UI->ReadyMotionKeys[0]||G->LastAnimationCommitMs>300.){Finish(TEXT("Prepared motion selection failed or exceeded 300ms"));return;}
            FString SelectedSource,SelectedClip;UI->ReadyMotionKeys[0].Split(TEXT("|"),&SelectedSource,&SelectedClip);
            if(G->SelectedGameSource!=SelectedSource||!G->GameClipCombo||G->GameRawClipKeys.FindRef(G->GameClipCombo->GetSelectedOption())!=UI->ReadyMotionKeys[0]){Finish(TEXT("Gallery selection did not follow showcase playback"));return;}
            MaximumControlMs=FMath::Max(MaximumControlMs,G->LastAnimationCommitMs);
            Stage=12;return;
        }
        if(Stage==12){
            // Selection publishes pose zero and rebases the first world delta,
            // which can include loading before the new clock existed.
            if(A->AnimationReadyClock.Pending)return;
            UI->OnSoloSpeed(2.f);if(A->AnimationPlaybackSpeed!=2.f){Finish(TEXT("Solo speed control failed"));return;}
            SpeedStartPhase=A->AnimPhase;At=W->GetTimeSeconds();Stage=10;return;
        }
        if(Stage==10&&Age>.25){
            if(!FMath::IsNearlyEqual(double(A->AnimPhase-SpeedStartPhase),Age*2.,.05)){Finish(TEXT("Solo speed did not advance the animation clock at 2x"));return;}
            UI->OnSoloSpeed(1.f);UI->OnReplay();Stage=13;return;
        }
        if(Stage==13){
            if(A->AnimationReadyClock.Pending||A->AnimPhase<2.f*A->AnimDuration+.125f*A->AnimDuration)return;
            if(!A->BodySkel||A->AnimDuration<=0.f){Finish(TEXT("Walking loop fixture missing skeletal display"));return;}
            LoopProbeBone=A->BodySkel->GetBoneIndex(TEXT("cf_j_leg01_L"));
            if(!A->BodySkel->BoneSpaceTransforms.IsValidIndex(LoopProbeBone)){Finish(TEXT("Walking loop fixture missing leg bone"));return;}
            LoopProbePose=A->BodySkel->BoneSpaceTransforms[LoopProbeBone];LoopProbePhase=A->AnimPhase;Stage=14;return;
        }
        if(Stage==14){
            if(A->AnimPhase<LoopProbePhase+.2f*A->AnimDuration)return;
            if(A->BodySkel->BoneSpaceTransforms[LoopProbeBone].Equals(LoopProbePose,1.e-5f)){Finish(TEXT("Walking stopped after two raw clip cycles"));return;}
            FrameReport+=TEXT("walking_moves_after_two_cycles=1\n");
            UI->OnPause();if(A->IsActorTickEnabled()){Finish(TEXT("Solo pause did not pause pose owner"));return;}
            UI->OnPause();if(!A->IsActorTickEnabled()){Finish(TEXT("Solo resume did not resume pose owner"));return;}
            UI->OnReplay();At=W->GetTimeSeconds();Stage=2;return;
        }
        if(Stage>=100){
            auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());auto* S=PC?PC->PaintSprayer.Get():nullptr;
            if(!S){Finish(TEXT("Performance sprayer unavailable"));return;}
            if(Stage==100&&A->PoseMode!=2){
                if(!UI->ReadyMotionCombo||!UI->ReadyMotionCombo->GetIsEnabled()||UI->ReadyMotionKeys.IsEmpty())return;
                UI->ReadyMotionCombo->SetSelectedIndex(1);
                MaximumControlMs=FMath::Max(MaximumControlMs,G->LastAnimationCommitMs);
                if(A->PoseMode!=2||G->SelectedGameClip!=UI->ReadyMotionKeys[0]||G->LastAnimationCommitMs>300.){Finish(TEXT("Initial prepared performance motion failed"));return;}
                MaximumControlMs=FMath::Max(MaximumControlMs,G->LastAnimationCommitMs);At=W->GetTimeSeconds();return;
            }
            if(Stage==100&&Age>2.){PC->ScriptedPaintAim=A->GetActorLocation();PC->bScriptedPaintFire=false;G->InteractionScene->ResetPerformance();PresentStart=ReadPresentCount();At=W->GetTimeSeconds();Stage=101;return;}
            if(Stage==101){
                if(Phase==2)UI->OnBelly(.5f+.3f*FMath::Sin(float(Age)*2.f));
                if(Phase==1&&Age-LastLiveSwitch>=.25){LastLiveSwitch=Age;const int32 Count=A->GetMouthPatternCount();if(Count>0){G->MouthPatternCombo->SetSelectedIndex(Pattern++%Count);G->OnMouthChanged(.8f);}}
                if(Phase==5&&Age-LastLiveSwitch>=.25){
                    LastLiveSwitch=Age;if(UI->ReadyMotionKeys.IsEmpty()){Finish(TEXT("No prepared motion for live switching phase"));return;}
                    const int32 Choice=LiveSwitches++%UI->ReadyMotionKeys.Num();UI->ReadyMotionCombo->SetSelectedIndex(1+Choice);MaximumControlMs=FMath::Max(MaximumControlMs,G->LastAnimationCommitMs);
                    if(G->SelectedGameClip!=UI->ReadyMotionKeys[Choice]){Finish(TEXT("Prepared live motion failed to commit"));return;}
                    if(G->LastAnimationCommitMs>300.){Finish(TEXT("Live prepared motion switch exceeded 300ms"));return;}
                }
                if(Phase==3){PC->ScriptedPaintAim=A->BodySkel?A->BodySkel->GetBoneLocation(TEXT("cf_j_hips")):A->GetActorLocation()+FVector(0.,0.,80.);PC->bScriptedPaintFire=true;}
                if(Age<10.)return;
                const auto* Scene=G->InteractionScene.Get();const bool Pass=Scene->FrameSamples&&Scene->FrameTotal/Scene->FrameSamples<=8.&&Scene->FrameMaximum<=300.;BudgetPassed&=Pass;
                FrameReport+=FString::Printf(TEXT("phase=%d budget_pass=%d\n%s\nwatergun=%s\n"),Phase,Pass,*Scene->PerformanceSummary(),*S->Statistics());
                const auto PresentEnd=ReadPresentCount();const double PresentSeconds=PresentEnd.At-PresentStart.At;
                const bool PresentSupported=PresentStart.Valid&&PresentEnd.Valid&&PresentStart.SwapChain==PresentEnd.SwapChain&&PresentSeconds>0.;
                const double PresentHz=PresentSupported?double(uint32(PresentEnd.Count-PresentStart.Count))/PresentSeconds:0.;PresentPassed&=PresentSupported&&PresentHz>=120.;
                FrameReport+=FString::Printf(TEXT("native_dxgi_present_supported=%d present_submission_hz=%.3f present_interval_seconds=%.6f\n"),PresentSupported,PresentHz,PresentSeconds);
                if(++Phase>=6){PaintHits=S->HitCount;PaintDeposited=S->Deposited;PC->ScriptedPaintAim.Reset();PC->bScriptedPaintFire=false;G->OnExitInteraction();FrameReport+=FString::Printf(TEXT("all_phase_budget_pass=%d\nplayer_controller_tick_preserved=1\nlive_prepared_motion_switches=%d\n"),BudgetPassed,LiveSwitches);Finish(FString());return;}
                if(Phase==1){UI->OnHeadCamera(true);G->OnEyeLookCamera(true);UI->OnSpeak();}
                if(Phase==4){PC->bScriptedPaintFire=false;S->StopSpray();}
                LastLiveSwitch=-1.;G->InteractionScene->ResetPerformance();PresentStart=ReadPresentCount();At=W->GetTimeSeconds();return;
            }
            return;
        }
        if(Stage==2&&Age>2.){
            const double Start=FPlatformTime::Seconds();UI->HeadCamera->SetIsChecked(true);UI->OnHeadCamera(true);G->EyeLookCameraCheck->SetIsChecked(true);G->OnEyeLookCamera(true);MaximumControlMs=FMath::Max(MaximumControlMs,(FPlatformTime::Seconds()-Start)*1000.);
            if(!A->bHeadLookFollowCamera||!A->bEyeLookFollowCamera){Finish(TEXT("Independent head/eye tracking failed"));return;}
            MouthCount=A->GetMouthPatternCount();if(MouthCount<=0){Finish(TEXT("No mouth patterns"));return;}
            FScreenshotRequest::RequestScreenshot(Output/TEXT("Tracking.png"),true,false);Stage=3;return;
        }
        if(Stage==3){
            if(Pattern<MouthCount){const double Start=FPlatformTime::Seconds();G->MouthPatternCombo->SetSelectedIndex(Pattern++);G->OnMouthChanged(.8f);MaximumControlMs=FMath::Max(MaximumControlMs,(FPlatformTime::Seconds()-Start)*1000.);return;}
            const int32 Selected=G->SpeechCombo?G->SpeechCombo->GetSelectedIndex():INDEX_NONE;
            if(!A->SpeechLines.IsValidIndex(Selected)){Finish(TEXT("No selected voice line"));return;}
            if(!UI->SpeechCache.Contains(A->SpeechLines[Selected].AudioPath))return;
            UI->OnBelly(.75f);UI->OnSpeak();At=W->GetTimeSeconds();Stage=4;return;
        }
        if(Stage==4){
            HeardSpeech|=A->IsSpeaking();
            if(Age>2.){if(!HeardSpeech){Finish(TEXT("Actual audio playback did not start"));return;}FScreenshotRequest::RequestScreenshot(Output/TEXT("SpeechMouth.png"),true,false);UI->OnAutomaticMouth(false);G->OnMouthChanged(.25f);if(!FMath::IsNearlyEqual(A->EffectiveFaceRate(0),.25f)){Finish(TEXT("Manual mouth did not override audio"));return;}UI->OnAutomaticMouth(true);
                const double Start=FPlatformTime::Seconds();G->SpeechVoiceCombo->SetSelectedIndex(2);G->SpeechVoiceCombo->SetSelectedIndex(1);MaximumControlMs=FMath::Max(MaximumControlMs,(FPlatformTime::Seconds()-Start)*1000.);At=W->GetTimeSeconds();Stage=11;return;}
        }
        if(Stage==11){
            if(Age>15.){Finish(TEXT("Background voice catalog preparation timed out"));return;}
            const int32 Selected=G->SpeechCombo->GetSelectedIndex();if(!A->SpeechLines.IsValidIndex(Selected)||A->SpeechLines[Selected].Personality!=0||!UI->SpeechCache.Contains(A->SpeechLines[Selected].AudioPath)||!UI->SpeakButton->GetIsEnabled())return;
            UI->OnSpeak();if(!A->IsSpeaking()){Finish(TEXT("Prepared replacement voice did not play"));return;}FrameReport+=TEXT("voice_catalog_cancel_and_replacement_pass=1\n");At=W->GetTimeSeconds();Stage=5;return;
        }
        if(Stage==5&&Age>3.){
            UI->OnStopSpeech();UI->OnHeadCamera(false);G->OnEyeLookCamera(false);
            if(A->bHeadLookFollowCamera||A->bEyeLookTracking||A->IsSpeaking()){Finish(TEXT("Stop controls failed"));return;}
            FrameReport+=G->InteractionScene->PerformanceSummary();
            FScreenshotRequest::RequestScreenshot(Output/TEXT("Showcase.png"),true,false);Stage=6;At=W->GetTimeSeconds();return;
        }
        if(Stage==6&&Age>1.){
            auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());
            if(!PC||!PC->IsPaintPreviewActive()||!PC->PaintSprayer){Finish(TEXT("Showcase watergun not enabled"));return;}
            if(SkipPaint){FrameReport+=TEXT("watergun_validation_skipped=1\n");PaintScreenshot=true;At=W->GetTimeSeconds();Stage=8;return;}
            PC->ScriptedPaintAim=A->GetActorLocation();PC->bScriptedPaintFire=false;At=W->GetTimeSeconds();Stage=7;return;
        }
        if(Stage==7){
            auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());auto* S=PC?PC->PaintSprayer.Get():nullptr;
            if(!S||!A->BodySkel||!A->StudioCam){Finish(TEXT("Missing spray character geometry"));return;}
            const FVector Target=A->BodySkel->GetBoneLocation(TEXT("cf_j_hips"))-FVector(0.,0.,12.);
            PC->ScriptedPaintAim=Target;PC->bScriptedPaintFire=true;
            if(Age>3.){PC->bScriptedPaintFire=false;S->StopSpray();PaintHits=S->HitCount;PaintDeposited=S->Deposited;
                if(PaintHits<=0||PaintDeposited<=0.){Finish(TEXT("Showcase spray did not deposit on character"));return;}
                if(FMath::Abs(S->ConservationError())>1.e-7){Finish(TEXT("Showcase paint conservation failed"));return;}
                At=W->GetTimeSeconds();Stage=8;}
            return;
        }
        if(Stage==20){
            if(G->bPartnerLoadInFlight||G->bPairPreparing)return;
            if(G->bPairCataloguePreparing&&G->ReadyPairs.Num()<(PairProfile?2:4))return;
            if(!G->PartnerActor||!G->PairedPlayback||!G->PartnerActor->Session.IsPortable()){Finish(TEXT("APNG male pair failed to start"));return;}
            if(!UKKGameRoot::Get()->Root.IsEmpty()){Finish(TEXT("APNG pair depended on game root"));return;}
            if(G->NativePairs.Num()<2){Finish(TEXT("Missing pair catalogue"));return;}
            FrameReport+=FString::Printf(TEXT("pair_first_ready_commit_ms=%.3f\n"),G->LastPairSwitchMs);
            if(G->LastPairSwitchMs>300.){if(PairProfile)BudgetPassed=false;else{Finish(TEXT("First ready pair commit exceeded 300ms"));return;}}
            if(PairProfile){G->CancelPairCatalogue();FPlatformMisc::SetEnvironmentVar(TEXT("OK_ANIM_PROFILE"),TEXT("0"));Stage=50;return;}
            FemalePairBefore=A->BodySkel->BoneSpaceTransforms;MalePairBefore=G->PartnerActor->BodySkel->BoneSpaceTransforms;
            At=W->GetTimeSeconds();Stage=21;return;
        }
        if(Stage==21&&Age>0.5){
            if(!PoseChanged(FemalePairBefore,A)||!PoseChanged(MalePairBefore,G->PartnerActor)||!FMath::IsNearlyEqual(A->AnimPhase,G->PartnerActor->AnimPhase,1.e-6f)){Finish(TEXT("Pair motion or shared clock failed"));return;}
            UI->OnHeadCamera(true);UI->AutomaticMouth->SetIsChecked(false);UI->OnAutomaticMouth(false);G->OnMouthChanged(.4f);UI->OnBelly(.7f);FixedInteractionBody=G->Selected.Body.ShapeValueBody;UI->OnSpeak();UI->OnPause();PairHeld=G->PairedPlayback->GetElapsed();At=W->GetTimeSeconds();Stage=22;return;
        }
        if(Stage==22&&Age>0.2){
            if(G->PairedPlayback->GetElapsed()!=PairHeld){Finish(TEXT("Pair pause failed"));return;}
#if WITH_DEV_AUTOMATION_TESTS
            if(PairEyes){FString ShadowError;
                if(!A->VerifyDeferredShadowTarget(ShadowError)||!G->PartnerActor->VerifyDeferredShadowTarget(ShadowError)){Finish(ShadowError);return;}
                FrameReport+=TEXT("paired_stable_shadow_depth_oracle_pass=1\n");
            }
#endif
            if(!A->bHeadLookFollowCamera||!FMath::IsNearlyEqual(A->EffectiveFaceRate(0),.4f)||A->Session.ShapeBody()!=FixedInteractionBody||G->Selected.Body.ShapeValueBody!=FixedInteractionBody){Finish(TEXT("Pair head/mouth controls or fixed body lock failed"));return;}
            UI->OnReplay();if(G->PairedPlayback->IsPaused()||G->PairedPlayback->GetElapsed()!=0.f){Finish(TEXT("Pair replay failed"));return;}
            PairPoseBefore=G->PairCombo->GetSelectedIndex();G->OnPairPoseNext();At=W->GetTimeSeconds();Stage=23;return;
        }
        if(Stage==23){
            if(G->bPairPreparing)return;
            if(!SelectedPairCommitted(G)||G->PairCombo->GetSelectedIndex()==PairPoseBefore){Finish(TEXT("Pair pose switch failed"));return;}
            FemalePairBefore=A->BodySkel->BoneSpaceTransforms;MalePairBefore=G->PartnerActor->BodySkel->BoneSpaceTransforms;
            At=W->GetTimeSeconds();Stage=24;return;
        }
        if(Stage==24&&Age>0.5){
            if(!PoseChanged(FemalePairBefore,A)||!PoseChanged(MalePairBefore,G->PartnerActor)){Finish(TEXT("Switched pair did not move both roles"));return;}
            if(G->PairStateCombo->GetOptionCount()>2){int32 Next=G->PairStateCombo->GetSelectedIndex()+1;if(Next>=G->PairStateCombo->GetOptionCount())Next=1;G->PairStateCombo->SetSelectedIndex(Next);}
            At=W->GetTimeSeconds();Stage=25;return;
        }
        if(Stage==25){
            if(G->bPairPreparing)return;if(!SelectedPairCommitted(G)){Finish(TEXT("Pair stage switch failed"));return;}
            const double PreviousFrame=G->InteractionScene->PreviousFrame;G->InteractionScene->ResetPerformance();PairSwitchAt=FPlatformTime::Seconds();G->InteractionScene->PreviousFrame=PreviousFrame>0.?PreviousFrame:PairSwitchAt;G->OnPairPoseNext();PairCallbackMs=(FPlatformTime::Seconds()-PairSwitchAt)*1000.;At=W->GetTimeSeconds();Stage=30;return;
        }
        if(Stage==30){
            if(G->bPairPreparing)return;
            if(!SelectedPairCommitted(G)){Finish(TEXT("Ready pair switch did not commit"));return;}
            if(Age<.35)return;
            const double Ms=FMath::Max(G->LastPairSwitchMs,PairCallbackMs),Frame=G->InteractionScene->FrameMaximum;
            FrameReport+=FString::Printf(TEXT("pair_switch_%d_click_to_play_ms=%.3f\npair_switch_%d_max_frame_ms=%.3f\n"),PairSwitchChecks,Ms,PairSwitchChecks,Frame);
            FrameReport+=FString::Printf(TEXT("pair_switch_%d_pose=%d\npair_switch_%d_state=%s\n"),PairSwitchChecks,G->PairCombo->GetSelectedIndex(),PairSwitchChecks,*G->PairStateCombo->GetSelectedOption());
            if(Ms>300.||Frame>300.){Finish(TEXT("Ready pair switch exceeded 300ms"));return;}
            if(++PairSwitchChecks<12){
                const double PreviousFrame=G->InteractionScene->PreviousFrame;G->InteractionScene->ResetPerformance();PairSwitchAt=FPlatformTime::Seconds();G->InteractionScene->PreviousFrame=PreviousFrame>0.?PreviousFrame:PairSwitchAt;
                if(PairSwitchChecks%3==0){int32 Next=G->PairStateCombo->GetSelectedIndex()+1;if(Next>=G->PairStateCombo->GetOptionCount())Next=1;G->PairStateCombo->SetSelectedIndex(Next);}
                else if(PairSwitchChecks==8)G->OnPairPosePrev();else G->OnPairPoseNext();
                PairCallbackMs=(FPlatformTime::Seconds()-PairSwitchAt)*1000.;At=W->GetTimeSeconds();return;
            }
            FrameReport+=TEXT("pair_ready_switch_budget_pass=1\npair_switch_checks=12\n");
            FrameReport+=FString::Printf(TEXT("portable_male_pair_pass=1\npair_catalogue_count=%d\npair_shared_clock=1\npair_pose_switch=1\npair_pause_replay=1\n"),G->NativePairs.Num());
            if(PairEyes){EyeCameraBefore=A->StudioCam->GetActorTransform();G->PairedPlayback->SetPaused(true);G->OnStopSpeech();A->SetAutomaticBlink(false);A->SetFaceControl(1,0,1.f,false);A->StopEyeLookTracking();A->SetEyeGaze(0.f,0.f,0.f);const FVector Head=A->BodySkel->GetBoneLocation(TEXT("cf_j_head"));A->StudioCam->SetActorLocation(Head-A->StudioCam->GetActorForwardVector()*65.f);A->StudioCam->SetActorRotation((Head-A->StudioCam->GetActorLocation()).Rotation());EyeProbeAt=FPlatformTime::Seconds();Stage=40;return;}
            if(SpeedAudit){G->CancelPairCatalogue();G->PairStateCombo->SetSelectedOption(TEXT("WLoop"));Stage=32;return;}
            if(PairPaint){G->CancelPairCatalogue();G->OnStopSpeech();PaintCameraBefore=A->StudioCam->GetActorTransform();At=W->GetTimeSeconds();Stage=60;return;}
            FScreenshotRequest::RequestScreenshot(Output/TEXT("Paired.png"),true,false);At=W->GetTimeSeconds();Stage=26;return;
        }
        if(Stage==40){
            if(FPlatformTime::Seconds()-EyeProbeAt<1.)return;
            if(EyeProbe==0&&FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_EYE_READBACK"))==TEXT("1")){TInlineComponentArray<USkinnedMeshComponent*> Components;A->GetComponents(Components);FString Stats;
                for(const auto& Part:A->Parts)if(Part.EyeSort==1||Part.EyeSort==2){OkMeshView View{};A->Session.PartAt(Part.SessionIndex,View);Stats+=FString::Printf(TEXT("part=%d name=%s eye=%d sort=%d uv_count=%d\n"),Part.SessionIndex,View.name?UTF8_TO_TCHAR(View.name):TEXT("?"),View.eye_draw,Part.EyeSort,Part.UVs.Num());
                    for(USkinnedMeshComponent* C:Components)if(C->GetName()==FString::Printf(TEXT("Skel_%d"),Part.SessionIndex)||C->GetName()==FString::Printf(TEXT("Mouth_%d"),Part.SessionIndex)){TArray<FFinalSkinVertex> Vertices;if(C->IsVisible()&&C->GetMeshObject())C->GetCPUSkinnedVertices(Vertices,0);FBox Box(ForceInit);for(const auto& V:Vertices)Box+=C->GetComponentTransform().TransformPosition(FVector(V.Position));Stats+=FString::Printf(TEXT("component=%s visible=%d vertices=%d box=%s bounds=%s\n"),*C->GetName(),C->IsVisible(),Vertices.Num(),*Box.ToString(),*C->Bounds.GetBox().ToString());}}
                FFileHelper::SaveStringToFile(Stats,*(Output/TEXT("pair-eye-geometry.txt")));}
            const TCHAR* Names[]={TEXT("PairEyesNeutral.png"),TEXT("PairEyesTracking.png"),TEXT("PairEyesClosed.png"),TEXT("PairEyesReopened.png"),TEXT("PairEyesPlaying.png")};
            FScreenshotRequest::RequestScreenshot(Output/Names[EyeProbe],true,false);
            FrameReport+=FString::Printf(TEXT("pair_eye_%d_open=%.6f\npair_eye_%d_gaze=%.6f,%.6f,%.6f\n"),EyeProbe,A->EffectiveFaceRate(1),EyeProbe,A->EyeGaze[0],A->EyeGaze[1],A->EyeGaze[2]);
            EyeProbeAt=FPlatformTime::Seconds();Stage=41;return;
        }
        if(Stage==41){
            if(FPlatformTime::Seconds()-EyeProbeAt<.5)return;
            if(++EyeProbe<5){if(EyeProbe==4)G->PairedPlayback->SetPaused(false);if(EyeProbe==1)A->FollowEyeLookCamera(A->EyeLookPattern);if(EyeProbe==2)A->SetFaceControl(1,0,0.f,false);if(EyeProbe==3)A->SetFaceControl(1,0,1.f,false);EyeProbeAt=FPlatformTime::Seconds();Stage=40;return;}
            A->StudioCam->SetActorTransform(EyeCameraBefore);A->SetAutomaticBlink(true);G->PairedPlayback->SetPaused(false);G->StartPairBreath();
            if(SpeedAudit){G->CancelPairCatalogue();G->PairStateCombo->SetSelectedOption(TEXT("WLoop"));Stage=32;return;}
            At=W->GetTimeSeconds();Stage=26;return;
        }
        if(Stage==32){
            if(G->PairCatalogueFinished&&!*G->PairCatalogueFinished)return;
            if(G->bPairPreparing||!SelectedPairCommitted(G))return;
            G->PairedPlayback->bMeasureAdvance=true;SpeedWallAt=FPlatformTime::Seconds();ChangeSpeed(G,1.f);Stage=31;return;
        }
        if(Stage==31){
            auto* P=G->PairedPlayback.Get();auto* Scene=G->InteractionScene.Get();
            const double Now=FPlatformTime::Seconds(),PhaseAge=Now-SpeedWallAt;const float Rate=AuditSpeed(SpeedPhase);
            if(Rate<0.)ChangeSpeed(G,.25f+3.75f*float(.5+.5*FMath::Sin(PhaseAge*6.)));
            if(!SpeedMeasuring){if(PhaseAge<1.5)return;Scene->ResetPerformance();P->ResetAdvanceMetrics();SpeedElapsedAt=P->GetElapsed();SpeedWorldAt=W->GetTimeSeconds();SpeedMeasuring=true;return;}
            if(PhaseAge<5.5)return;
            if(!Scene->FrameSamples||!P->AdvanceSamples||!FMath::IsNearlyEqual(A->AnimPhase,G->PartnerActor->AnimPhase,1.e-6f)){Finish(TEXT("Speed audit lost frames/shared clock"));return;}
            const double Mean=P->AdvanceTotalMs/P->AdvanceSamples,Frame=Scene->FrameTotal/Scene->FrameSamples;
            const double ActualRate=(P->GetElapsed()-SpeedElapsedAt)/(W->GetTimeSeconds()-SpeedWorldAt);
            SpeedMeans.Add(Mean);SpeedFrames.Add(Frame);
            FrameReport+=FString::Printf(TEXT("pair_speed_%d_rate=%.3f\npair_speed_%d_actual_rate=%.6f\npair_speed_%d_advance_mean_ms=%.6f\npair_speed_%d_advance_max_ms=%.6f\npair_speed_%d_frame_mean_ms=%.6f\npair_speed_%d_frame_max_ms=%.6f\npair_speed_%d_samples=%llu\n"),SpeedPhase,Rate,SpeedPhase,ActualRate,SpeedPhase,Mean,SpeedPhase,P->AdvanceMaximumMs,SpeedPhase,Frame,SpeedPhase,Scene->FrameMaximum,SpeedPhase,P->AdvanceSamples);
            if(Rate>=0.f&&FMath::Abs(ActualRate-Rate)>.05){Finish(TEXT("Speed did not advance the actual shared clock at the requested rate"));return;}
            if(Scene->FrameMaximum>300.){Finish(TEXT("Pair speed audit frame exceeded 300ms"));return;}
            if(++SpeedPhase<13){SpeedWallAt=Now;SpeedMeasuring=false;ChangeSpeed(G,AuditSpeed(SpeedPhase)<0.f?1.f:AuditSpeed(SpeedPhase));return;}
            bool Stable=true;for(int32 I=1;I<12;I+=2){const double Baseline=(SpeedMeans[I-1]+SpeedMeans[I+1])*.5,FrameBaseline=(SpeedFrames[I-1]+SpeedFrames[I+1])*.5;const bool Pass=SpeedMeans[I]<=Baseline*1.15+.5,FramePass=SpeedFrames[I]<=FrameBaseline*1.15+.5;Stable&=Pass&&FramePass;FrameReport+=FString::Printf(TEXT("pair_speed_%d_adjacent_baseline_ms=%.6f\npair_speed_%d_cpu_budget_pass=%d\npair_speed_%d_adjacent_frame_baseline_ms=%.6f\npair_speed_%d_frame_budget_pass=%d\n"),I,Baseline,I,Pass?1:0,I,FrameBaseline,I,FramePass?1:0);}
            FrameReport+=FString::Printf(TEXT("pair_speed_cpu_stable=%d\npair_speed_callback_max_ms=%.6f\npair_speed_change_requests=%d\n"),Stable?1:0,SpeedCallbackMaxMs,SpeedRequests);
            if(!Stable||SpeedCallbackMaxMs>8.){Finish(TEXT("Pair speed added CPU work or expensive UI callbacks"));return;}
            P->bMeasureAdvance=false;ChangeSpeed(G,1.f);G->ResumePairCatalogue();FScreenshotRequest::RequestScreenshot(Output/TEXT("Paired.png"),true,false);At=W->GetTimeSeconds();Stage=26;return;
        }
        if(Stage==60){
            auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());auto* S=PC?PC->PaintSprayer.Get():nullptr;
            auto* Role=PaintRole==0?A:G->PartnerActor.Get();
            if(!Role||!Role->BodySkel||!S){Finish(TEXT("Missing paired watergun actor"));return;}
            FVector Target;if(!PairPaintTarget(Role,Target,true)){Finish(TEXT("No exposed private GPU paint target"));return;}
            // Aim from above at an actual exposed triangle. A partner may
            // correctly occlude hips in a paired pose; that is not a gun bug.
            A->StudioCam->SetActorLocation(Target+FVector(0,0,120));A->StudioCam->SetActorRotation(FVector(0,0,-1).Rotation());
            // The first role discards earlier solo strokes. The second role
            // starts clean without erasing the first role's verified wet surfaces:
            // both must survive the final paired pose-switch query below.
            if(PaintRole==0)S->ClearPaint();else S->ClearPaintForActor(Role);
            PairPaintHitsBefore=S->HitCount;PairPaintDepositedBefore=S->Deposited;
            FemalePairBefore=A->BodySkel->BoneSpaceTransforms;MalePairBefore=G->PartnerActor->BodySkel->BoneSpaceTransforms;
            PC->ScriptedPaintAim=Target;PC->bScriptedPaintFire=true;At=W->GetTimeSeconds();Stage=61;return;
        }
        if(Stage==61){
            auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());auto* S=PC->PaintSprayer.Get();auto* Role=PaintRole==0?A:G->PartnerActor.Get();
            FVector Target;if(!PairPaintTarget(Role,Target,false)){Finish(TEXT("Private GPU spray target changed layout"));return;}PC->ScriptedPaintAim=Target;
            if(Age<3.)return;PC->bScriptedPaintFire=false;S->StopSpray();At=W->GetTimeSeconds();Stage=62;return;
        }
        if(Stage==62&&Age>1.){
            auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());auto* S=PC->PaintSprayer.Get();auto* Role=PaintRole==0?A:G->PartnerActor.Get();
            int32 Checked=0,Painted=0;double ErrorCm=0.;FString Error;
            if(!S->VerifyPrivateGpuPaintGeometry(Role,Checked,Painted,PaintAnchorSurface,ErrorCm,Error)){Finish(Error);return;}
            const int32 Hits=S->HitCount-PairPaintHitsBefore;const double Deposited=S->Deposited-PairPaintDepositedBefore;
            FrameReport+=FString::Printf(TEXT("pair_paint_%d_hits=%d\npair_paint_%d_deposited=%.9f\npair_paint_%d_private_surfaces=%d\npair_paint_%d_private_painted=%d\npair_paint_%d_query_error_cm=%.9f\n"),PaintRole,Hits,PaintRole,Deposited,PaintRole,Checked,PaintRole,Painted,PaintRole,ErrorCm);
            if(Hits<=0||Deposited<=0||Checked<=0||Painted<=0){Finish(TEXT("Paired spray did not paint private GPU surfaces"));return;}
            if(!PoseChanged(FemalePairBefore,A)||!PoseChanged(MalePairBefore,G->PartnerActor)){Finish(TEXT("Paired animation stopped during spray"));return;}
            if(FMath::Abs(S->ConservationError())>1.e-7){Finish(TEXT("Paired spray conservation failed"));return;}
            UTextureRenderTarget2D* Texture=nullptr;
            if(!S->GetPaintAnchor(PaintAnchorSurface,INDEX_NONE,PaintAnchorBefore,Texture,PaintAnchorFace)||!Texture){Finish(TEXT("Missing private GPU paint anchor/texture"));return;}
            FScreenshotRequest::RequestScreenshot(Output/FString::Printf(TEXT("PairPaint%d.png"),PaintRole),true,false);
            At=W->GetTimeSeconds();Stage=63;return;
        }
        if(Stage==63&&Age>.7){
            auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());auto* S=PC->PaintSprayer.Get();
            FVector Now;UTextureRenderTarget2D* Texture=nullptr;int32 Face=INDEX_NONE;
            if(!S->GetPaintAnchor(PaintAnchorSurface,PaintAnchorFace,Now,Texture,Face)||Now.ContainsNaN()||FVector::Distance(Now,PaintAnchorBefore)<.001){Finish(TEXT("Paint anchor did not follow paired animation"));return;}
            FrameReport+=FString::Printf(TEXT("pair_paint_%d_anchor_motion_cm=%.6f\n"),PaintRole,FVector::Distance(Now,PaintAnchorBefore));
            if(++PaintRole<2){At=W->GetTimeSeconds();Stage=60;return;}
            G->OnPairPoseNext();At=W->GetTimeSeconds();Stage=64;return;
        }
        if(Stage==64&&Age>1.){
            if(G->bPairPreparing)return;auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());auto* S=PC->PaintSprayer.Get();
            for(auto* Role:{A,G->PartnerActor.Get()}){int32 Checked=0,Painted=0,Anchor=INDEX_NONE;double ErrorCm=0.;FString Error;if(!S->VerifyPrivateGpuPaintGeometry(Role,Checked,Painted,Anchor,ErrorCm,Error)||Checked<=0){Finish(Error.IsEmpty()?TEXT("Missing GPU surfaces after pose switch"):Error);return;}}
            PaintHits=S->HitCount;PaintDeposited=S->Deposited;
            if(FMath::Abs(S->ConservationError())>1.e-7){Finish(TEXT("Paint conservation failed after pose switch"));return;}
            S->ClearPaint();if(S->RemainingVolume()!=0.||S->IsSpraying()){Finish(TEXT("Paired watergun clear failed"));return;}
            FrameReport+=TEXT("paired_watergun_pass=1\npaired_watergun_clear_pass=1\npaired_watergun_pose_query_pass=1\n");
            A->StudioCam->SetActorTransform(PaintCameraBefore);PC->ScriptedPaintAim.Reset();PC->bScriptedPaintFire=false;
            At=W->GetTimeSeconds();Stage=26;return;
        }
        if(Stage==26&&Age>1.){
            PreviousMale=G->PartnerActor;UI->OnLoadPartner();Stage=28;return;
        }
        if(Stage==28){
            if(G->bPartnerLoadInFlight||G->bPairPreparing)return;
            if(!SelectedPairCommitted(G)||G->PartnerActor==PreviousMale.Get()){Finish(TEXT("Male replacement failed"));return;}
            PreviousMale=G->PartnerActor;G->LoadPartnerFromPath(Card);Stage=29;return;
        }
        if(Stage==29){
            if(G->PartnerLoadsPending>0)return;
            if(G->PartnerActor!=PreviousMale.Get()||!SelectedPairCommitted(G)){Finish(TEXT("Invalid replacement discarded current pair"));return;}
            FrameReport+=TEXT("male_replacement_and_failure_preservation=1\npair_live_head_mouth_belly_controls=1\n");
            G->OnStopPair();if(G->PartnerActor||G->PairedPlayback){Finish(TEXT("Pair stop leaked actor"));return;}
            UI->PartnerPath->SetText(FText::FromString(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_PARTNER"))));UI->OnLoadPartner();
            G->OnExitInteraction();if(G->bPartnerLoadInFlight||G->InteractionWidget){Finish(TEXT("Return failed to cancel pending male load"));return;}
            At=W->GetTimeSeconds();Stage=27;return;
        }
        if(Stage==8&&Age>2.&&!PaintScreenshot){FScreenshotRequest::RequestScreenshot(Output/TEXT("Painted.png"),true,false);PaintScreenshot=true;return;}
        if(Stage==8&&Age>3.){
            auto* PC=Cast<AOpenKoikatsuPlayerController>(W->GetFirstPlayerController());PC->ScriptedPaintAim=A->GetActorLocation();PC->bScriptedPaintFire=false;
            const FString Partner=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_PARTNER"));
            if(!Partner.IsEmpty()){G->LoadPartnerFromPath(Card);UI->PartnerPath->SetText(FText::FromString(Partner));UI->OnLoadPartner();UI->OnLoadPartner();if(G->PartnerLoadsPending!=1){Finish(TEXT("Male loader spawned unbounded workers"));return;}FrameReport+=TEXT("male_latest_request_queue=1\n");Stage=20;At=W->GetTimeSeconds();return;}
            G->OnExitInteraction();if(G->InteractionWidget||G->InteractionScene||!G->MouthPatternCombo->GetParent()){Finish(TEXT("Return did not restore gallery"));return;}
            if(A->bShareIdenticalPose!=G->bInteractionPreviousPoseSharing){Finish(TEXT("Return did not restore pose ownership"));return;}
            if(!A->bShareIdenticalPose)for(UPoseableMeshComponent* F:A->SkelFollowers)if(F&&F->LeaderPoseComponent.IsValid()){Finish(TEXT("Return left a gallery bone editor attached to a shared leader"));return;}
            if(PC->PaintSprayer->RemainingVolume()!=0.){Finish(TEXT("Return did not clear showcase paint"));return;}
            G->OnEnterInteraction();if(!G->InteractionWidget){Finish(TEXT("Reentry failed"));return;}G->OnExitInteraction();Finish(FString());
        }
    }
};
}
static FAutoConsoleCommandWithWorld GInteractionAudit(TEXT("ok.InteractionAudit"),TEXT("Real heroine gallery entry/control/audio/return audit; OK_INTERACTION_CARD, ROOT, OUTPUT"),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){
    const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_CARD")),Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_ROOT")),Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_OUTPUT"));
    const bool Portable=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_APNG"))==TEXT("1");
    if(!W||Card.IsEmpty()||(!Portable&&Root.IsEmpty())||Output.IsEmpty()){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    UKKGameRoot::Get()->Root=Root;IFileManager::Get().MakeDirectory(*Output,true);
    auto Run=MakeShared<FInteractionAudit>();Run->World=W;Run->Card=Card;Run->Output=Output;Run->Portable=Portable;Run->SpeedAudit=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_PAIR_SPEED"))==TEXT("1");Run->Measure=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_PERFORMANCE"))==TEXT("1");
    Run->PairEyes=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_PAIR_EYES"))==TEXT("1");
    Run->PairProfile=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_PAIR_PROFILE"))==TEXT("1");
    Run->PairWatergunProfile=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_PAIR_WATERGUN_PROFILE"))==TEXT("1");
    Run->PairInteractionProfile=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_PAIR_CONTROLS_PROFILE"))==TEXT("1");
    Run->AssignedWorldAB=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_ASSIGNED_WORLD_AB"))==TEXT("1");
    Run->SkipPaint=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_SKIP_PAINT"))==TEXT("1");
    Run->PairPaint=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_INTERACTION_PAIR_PAINT"))==TEXT("1");
    W->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateLambda([Run](){Run->Tick();}),.1f,true);
}));
#endif
