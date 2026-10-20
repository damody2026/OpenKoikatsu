#include "OKMotionLabelZh.h"
#include "OKGalleryWidget.h"
#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
#include "Async/Async.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/IConsoleManager.h"
#include "HAL/RunnableThread.h"
#include <atomic>

static TAutoConsoleVariable<int32> CVarPairPrepareThreads(TEXT("ok.Pair.PrepareThreads"),4,TEXT("Threads that prepare the remaining states of a paired pose after its first state starts playing (each keeps its own decoded-bundle cache)."));


static FString PairCategoryLabel(const FString& Key){
    if(Key==TEXT("AnimationInfo_00"))return TEXT("愛撫");
    if(Key==TEXT("AnimationInfo_01"))return TEXT("奉仕");
    if(Key==TEXT("AnimationInfo_02"))return TEXT("插入");
    if(Key==TEXT("AnimationInfo_03"))return TEXT("特殊");
    return Key; // Preserve new source categories instead of dropping unknown groups.
}
int32 UOKCharacterController::SelectedPairCatalogueIndex() const {
    const int32 Option=PairCombo?PairCombo->GetSelectedIndex()-1:INDEX_NONE;
    return PairVisibleIndices.IsValidIndex(Option)?PairVisibleIndices[Option]:INDEX_NONE;
}
void UOKCharacterController::CancelPairCatalogue(){
    if(PairCatalogueCancelled)*PairCatalogueCancelled=true;
    ++PairCatalogueGeneration;bPairCataloguePreparing=false;
}
// The first selection of a pose parses both animation controllers (seconds per bundle, about 9 s per pose); the result
// is kept in the animation disk cache (OK_ANIM_CACHE_DIR). Parse the whole catalogue once in the background, so a pose
// picked later only reads the cache. Process-wide and once per run: later catalogue rebuilds reuse the in-memory result.
static void WarmPairCatalogue(const TArray<TSharedPtr<FJsonObject>>& Rows){
    static std::atomic<bool> GStarted{false};
    if(GStarted.exchange(true))return;
    const FString Root=FOkSession::CommonPairRoot();if(Root.IsEmpty())return;
    struct FPose{FString FemaleSource,MaleSource;int32 FemaleLayer=0,MaleLayer=0;};
    TArray<FPose> Poses;TArray<FString> Sources;
    for(const auto& Row:Rows)if(Row){
        FPose& Pose=Poses.AddDefaulted_GetRef();
        Pose.FemaleSource=Row->GetStringField(TEXT("female_source"));Pose.MaleSource=Row->GetStringField(TEXT("male_source"));
        Pose.FemaleLayer=int32(Row->GetNumberField(TEXT("female_layer")));Pose.MaleLayer=int32(Row->GetNumberField(TEXT("male_layer")));
        Sources.AddUnique(Pose.FemaleSource);Sources.AddUnique(Pose.MaleSource);
    }
    Sources.RemoveAll([](const FString& Source){return Source.IsEmpty();});
    Async(EAsyncExecution::Thread,[Root,Sources,Poses](){
        if(FRunnableThread* Thread=FRunnableThread::GetRunnableThread())Thread->SetThreadPriority(TPri_BelowNormal);
        const double Start=FPlatformTime::Seconds();
        // 1. The controllers: what every selection needs before anything else.
        TMap<FString,TArray<FString>> Labels;
        for(const FString& Source:Sources){
            if(IsEngineExitRequested())return;
            FOkSession::BeginAnimationPreparation();
            Labels.Add(Source,FOkSession::AnimationSourceNames(Root,Source));
            FOkSession::EndAnimationPreparation();
        }
        const double Controllers=FPlatformTime::Seconds()-Start;
        // 2. The clips of the state each pose plays first (same choice as PreparePairSelection: WLoop, else Idle, else
        // the first common state), so the first state of a pose never picked before is ready at once too.
        const FString Prefix=TEXT(" / [Controller layer 0] Base Layer.");
        auto States=[&](const FString& Source){
            TMap<FString,FString> Out;
            for(const FString& Label:Labels.FindRef(Source)){const int32 At=Label.Find(Prefix);if(At==INDEX_NONE)continue;
                const FString State=Label.Mid(At+Prefix.Len());if(!State.Contains(TEXT(".")))Out.Add(State,Source+TEXT("|")+Label);}
            return Out;
        };
        int32 Clips=0;
        for(const FPose& Pose:Poses){
            if(IsEngineExitRequested())return;
            const TMap<FString,FString> Female=States(Pose.FemaleSource),Male=States(Pose.MaleSource);
            TArray<FString> Common;for(const auto& Entry:Female)if(Male.Contains(Entry.Key))Common.Add(Entry.Key);
            if(Common.IsEmpty())continue;
            Common.Sort();
            const FString First=Common.Contains(TEXT("WLoop"))?FString(TEXT("WLoop")):Common.Contains(TEXT("Idle"))?FString(TEXT("Idle")):Common[0];
            FOkSession::BeginAnimationPreparation();
            const FString FK=FOkSession::PairClipForRoot(Root,Female[First],Pose.FemaleLayer),MK=FOkSession::PairClipForRoot(Root,Male[First],Pose.MaleLayer);
            if(!FK.IsEmpty())Clips+=FOkSession::PrepareAnimationClip(Root,FK)?1:0;
            if(!MK.IsEmpty())Clips+=FOkSession::PrepareAnimationClip(Root,MK)?1:0;
            FOkSession::EndAnimationPreparation();
        }
        UE_LOG(LogTemp,Display,TEXT("OK pair catalogue warmed controllers=%d (%.1f s) first-state clips=%d seconds=%.1f"),Sources.Num(),Controllers,Clips,FPlatformTime::Seconds()-Start);
    });
}
void UOKCharacterController::PreparePairCatalogue(const TArray<TSharedPtr<FJsonObject>>& Rows,const TArray<FString>& Labels){
    WarmPairCatalogue(Rows);
    PairCatalogueJsonRows.Reset();PairCatalogueLabels.Reset();for(const FString& Label:Labels)PairCatalogueLabels.Add(OKMotionLabelDisplay(Label));PairCatalogueProcessed=0;ReadyPairs.SetNum(Rows.Num());
    TArray<FString> Categories;
    for(const auto& Row:Rows){FString Json;FJsonSerializer::Serialize(Row.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));PairCatalogueJsonRows.Add(MoveTemp(Json));Categories.AddUnique(PairCategoryLabel(Row->GetStringField(TEXT("category"))));}
    if(PairCategoryCombo){PairCategoryCombo->ClearOptions();PairCategoryCombo->AddOption(TEXT("全部"));for(const auto& Category:Categories)PairCategoryCombo->AddOption(Category);PairCategoryCombo->SetSelectedIndex(0);}
}
void UOKCharacterController::OnPairCategorySelected(FString Item,ESelectInfo::Type){
    if(bSuppressPairControls||!PairCombo)return;
    CancelPairCatalogue();CancelPairPreparation();
    const int32 Previous=SelectedPairCatalogueIndex();
    bSuppressPairControls=true;PairCombo->ClearOptions();PairCombo->AddOption(TEXT("選擇動作"));PairVisibleIndices.Reset();
    for(int32 I=0;I<NativePairs.Num();++I)if(Item==TEXT("全部")||PairCategoryLabel(NativePairs[I]->GetStringField(TEXT("category")))==Item){PairVisibleIndices.Add(I);PairCombo->AddOption(PairCatalogueLabels[I]);}
    const int32 PriorOption=PairVisibleIndices.Find(Previous);PairCombo->SetSelectedIndex(PriorOption==INDEX_NONE?0:PriorOption+1);
    if(PriorOption==INDEX_NONE&&PairStateCombo){PairStateCombo->ClearOptions();PairStateCombo->AddOption(TEXT("選擇階段"));PairStateCombo->SetSelectedIndex(0);}
    bSuppressPairControls=false;
    if(PairPreparationStatus)PairPreparationStatus->SetText(FText::FromString(FString::Printf(TEXT("%s · %d 個動作；選取後載入"),*Item,PairVisibleIndices.Num())));
    if(!PairedPlayback&&PartnerActor&&!PairVisibleIndices.IsEmpty()){bPairAutoStartPending=true;TryStartPreparedPair();}
}
void UOKCharacterController::ResumePairCatalogue(){
    if(!IsValid(PartnerActor)||bPairCataloguePreparing||PairedPlayback)return;
    bPairAutoStartPending=true;TryStartPreparedPair();
}
void UOKCharacterController::PreparePairSelection(int32 I){
    if(!NativePairs.IsValidIndex(I)||!PreviewActor||!PartnerActor)return;
    CancelPairCatalogue();const int32 Gen=PairCatalogueGeneration,Request=++PairPrepareGeneration;
    const FString Root=FOkSession::CommonPairRoot();if(Root.IsEmpty()){ShowPairStatus(TEXT("缺少共用雙人動畫資料庫"),true);return;}
    if(ActivePairProgress)ActivePairProgress->bCancelled=true; // the previous pose stops completing its other states
    PairCatalogueCancelled=MakeShared<FThreadSafeBool,ESPMode::ThreadSafe>(false);auto Cancelled=PairCatalogueCancelled;
    auto Progress=MakeShared<FOKPairProgress,ESPMode::ThreadSafe>();ActivePairProgress=Progress;
    PairCatalogueFinished=MakeShared<FThreadSafeBool,ESPMode::ThreadSafe>(false);auto Finished=PairCatalogueFinished;
    bPairCataloguePreparing=true;bPairPreparing=true;
    auto Prepared=MakeShared<FOKReadyPair,ESPMode::ThreadSafe>();Prepared->Json=PairCatalogueJsonRows[I];Prepared->Label=PairCatalogueLabels[I];
    TWeakObjectPtr<UOKCharacterController> Weak(this);TWeakObjectPtr<AKKPreviewActor> Actor=PreviewActor,Partner=PartnerActor;
    ShowPairStatus(TEXT("正在載入：")+Prepared->Label+TEXT("；目前動作繼續播放"),false);
    if(PairPreparationStatus)PairPreparationStatus->SetText(FText::FromString(TEXT("載入中：")+Prepared->Label));
    const FString SpeechRoot=SpeechRootInput?SpeechRootInput->GetText().ToString():FString();
    Async(EAsyncExecution::Thread,[Weak,Actor,Partner,Cancelled,Finished,Prepared,Progress,Root,SpeechRoot,Gen,Request,I](){
        ON_SCOPE_EXIT{*Finished=true;};
        const double T0=FPlatformTime::Seconds();
        static FCriticalSection PreparationLock;FScopeLock Guard(&PreparationLock);if(*Cancelled)return;
        const double T1=FPlatformTime::Seconds();
        if(!SpeechRoot.IsEmpty()){FString Error;KKSpeech::PrepareHBreathIndex(SpeechRoot,Error);}
        const double T2=FPlatformTime::Seconds();
        FOkSession::BeginAnimationPreparation();ON_SCOPE_EXIT{FOkSession::EndAnimationPreparation();};
        TSharedPtr<FJsonObject> Row;
        if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Prepared->Json),Row)&&Row){
            const FString FS=Row->GetStringField(TEXT("female_source")),MS=Row->GetStringField(TEXT("male_source"));
            auto Collect=[&](const FString& Source,TMap<FString,FString>& Out){
                TSet<FString> Duplicates;
                for(const auto& Label:FOkSession::AnimationSourceNames(Root,Source)){
                    const FString Prefix=TEXT(" / [Controller layer 0] Base Layer.");const int32 At=Label.Find(Prefix);if(At==INDEX_NONE)continue;
                    FString State=Label.Mid(At+Prefix.Len());if(State.Contains(TEXT(".")))continue;
                    if(Out.Contains(State))Duplicates.Add(State);else Out.Add(State,Source+TEXT("|")+Label);
                }
                for(const auto& State:Duplicates)Out.Remove(State);
            };
            // The two controllers are separate bundles: parse them at the same time.
            TFuture<void> MaleCollect=Async(EAsyncExecution::Thread,[&Collect,&MS,Prepared](){
                FOkSession::BeginAnimationPreparation();ON_SCOPE_EXIT{FOkSession::EndAnimationPreparation();};
                Collect(MS,Prepared->MaleStates);
            });
            Collect(FS,Prepared->FemaleStates);
            MaleCollect.Wait();
            const double T3=FPlatformTime::Seconds();
            // Resolving the clip keys is cheap; decoding the clips and building the paired IK setup is what takes
            // seconds per state. Resolve every key, prepare the state that plays first, hand the pose to the game
            // thread, then prepare the others in parallel while it plays.
            TArray<FString> States;Prepared->FemaleStates.GetKeys(States);States.Sort();
            for(const FString& State:States){
                if(*Cancelled)break;if(!Prepared->MaleStates.Contains(State))continue;
                const FString FK=FOkSession::PairClipForRoot(Root,Prepared->FemaleStates[State],int32(Row->GetNumberField(TEXT("female_layer"))));
                const FString MK=FOkSession::PairClipForRoot(Root,Prepared->MaleStates[State],int32(Row->GetNumberField(TEXT("male_layer"))));
                if(FK.IsEmpty()||MK.IsEmpty())continue;
                Prepared->FemaleKeys.Add(State,FK);Prepared->MaleKeys.Add(State,MK);
            }
            for(const auto& State:States)if(!Prepared->FemaleKeys.Contains(State)){Prepared->FemaleStates.Remove(State);Prepared->MaleStates.Remove(State);}
            const FString FemaleMotion=Row->GetStringField(TEXT("female_motion")),MaleMotion=Row->GetStringField(TEXT("male_motion"));
            UE_LOG(LogTemp,Display,TEXT("OK pair preparation index=%d lock_wait=%.2f speech=%.2f states=%.2f keys=%.2f"),I,T1-T0,T2-T1,T3-T2,FPlatformTime::Seconds()-T3);
            // Reads only immutable data (keys are fixed from here on); safe on any thread.
            auto PrepareOne=[Root,Prepared,FemaleMotion,MaleMotion](const FString& State,const FThreadSafeBool& Stop){
                const FString& FK=Prepared->FemaleKeys[State];const FString& MK=Prepared->MaleKeys[State];
                return FOkSession::PrepareAnimationClip(Root,FK)&&!Stop&&FOkSession::PrepareAnimationClip(Root,MK)&&!Stop
                    &&FOkSession::PrepareAnimationPair(Root,State,FemaleMotion,MaleMotion);
            };
            TArray<FString> Order;Prepared->FemaleKeys.GetKeys(Order);Order.Sort();
            // Same default as OnPairSelected: WLoop, else Idle, else the first state.
            const FString First=Order.Contains(TEXT("WLoop"))?FString(TEXT("WLoop")):Order.Contains(TEXT("Idle"))?FString(TEXT("Idle")):Order.IsEmpty()?FString():Order[0];
            Order.Remove(First);Progress->Pending=Order;Prepared->Progress=Progress;
            const double Start=FPlatformTime::Seconds();
            bool bAnyReady=false;
            for(FString Candidate=First;!Candidate.IsEmpty()&&!*Cancelled;){
                const bool bOk=PrepareOne(Candidate,*Cancelled);Progress->Finish(Candidate,bOk);
                if(bOk){bAnyReady=true;break;}
                if(!Progress->Pop(Candidate))break; // the default state failed: play the next one that works
            }
            const double FirstSeconds=FPlatformTime::Seconds()-Start;
            if(*Cancelled)return;
            if(!bAnyReady){Prepared->FemaleKeys.Reset();}
            // Hand the pose over now; the remaining states complete below.
            AsyncTask(ENamedThreads::GameThread,[Weak,Actor,Partner,Cancelled,Prepared,Gen,Request,I,FirstSeconds](){
                auto* G=Weak.Get();if(!G||*Cancelled||Gen!=G->PairCatalogueGeneration||Request!=G->PairPrepareGeneration||!Actor.IsValid()||!Partner.IsValid()||G->PreviewActor!=Actor.Get()||G->PartnerActor!=Partner.Get())return;
                G->bPairCataloguePreparing=false;G->bPairPreparing=false;
                if(Prepared->FemaleKeys.IsEmpty()){G->ShowPairStatus(TEXT("此動作載入失敗或無雙方共用階段；請選其他動作"),true);if(G->PairPreparationStatus)G->PairPreparationStatus->SetText(FText::FromString(TEXT("載入失敗：")+Prepared->Label));return;}
                G->ReadyPairs[I]=Prepared;++G->PairCatalogueProcessed;
                if(G->PairPreparationStatus)G->PairPreparationStatus->SetText(FText::FromString(FString::Printf(TEXT("已載入：%s（其餘 %d 個階段背景準備中）"),*Prepared->Label,Prepared->FemaleKeys.Num()-1)));
                if(G->SelectedPairCatalogueIndex()==I)G->OnPairSelected(G->PairCombo->GetSelectedOption(),ESelectInfo::Direct);
                UE_LOG(LogTemp,Display,TEXT("OK pair first state ready index=%d states=%d first_seconds=%.2f"),I,Prepared->FemaleKeys.Num(),FirstSeconds);
            });
            if(!bAnyReady)return;
            // The pose is playing: the next selection need not wait for its other states (they stop once cancelled).
            Guard.Unlock();
            // The other states, in parallel. Each thread keeps its own decoded-bundle cache for its run.
            const int32 Threads=FMath::Clamp(CVarPairPrepareThreads.GetValueOnAnyThread(),1,16);
            auto Work=[Progress,PrepareOne](){
                FOkSession::BeginAnimationPreparation();ON_SCOPE_EXIT{FOkSession::EndAnimationPreparation();};
                FString State;
                while(!Progress->bCancelled&&Progress->Pop(State))Progress->Finish(State,PrepareOne(State,Progress->bCancelled));
            };
            TArray<TFuture<void>> Helpers;
            for(int32 T=1;T<Threads;++T)Helpers.Add(Async(EAsyncExecution::Thread,Work));
            Work();
            for(TFuture<void>& Helper:Helpers)Helper.Wait();
            const double AllSeconds=FPlatformTime::Seconds()-Start;
            if(Progress->bCancelled)return;
            Progress->bFinished=true;
            AsyncTask(ENamedThreads::GameThread,[Weak,Prepared,Progress,I,AllSeconds,Threads](){
                auto* G=Weak.Get();if(!G)return;
                TArray<FString> Failed;{FScopeLock Guard(&Progress->Lock);Failed=Progress->Failed.Array();}
                for(const FString& State:Failed){Prepared->FemaleKeys.Remove(State);Prepared->MaleKeys.Remove(State);Prepared->FemaleStates.Remove(State);Prepared->MaleStates.Remove(State);}
                if(!Failed.IsEmpty()&&G->SelectedPairCatalogueIndex()==I&&G->PairStateCombo){
                    // Drop the states that could not be prepared from the menu, keeping the current selection.
                    G->bSuppressPairControls=true;for(const FString& State:Failed)G->PairStateCombo->RemoveOption(State);G->bSuppressPairControls=false;
                }
                if(G->PairPreparationStatus&&G->SelectedPairCatalogueIndex()==I)G->PairPreparationStatus->SetText(FText::FromString(TEXT("已載入：")+Prepared->Label));
                UE_LOG(LogTemp,Display,TEXT("OK pair demand prepared index=%d states=%d failed=%d all_seconds=%.2f threads=%d"),I,Prepared->FemaleKeys.Num(),Failed.Num(),AllSeconds,Threads);
            });
            return;
        }
        if(*Cancelled)return;
        AsyncTask(ENamedThreads::GameThread,[Weak,Actor,Partner,Cancelled,Prepared,Gen,Request,I](){
            auto* G=Weak.Get();if(!G||*Cancelled||Gen!=G->PairCatalogueGeneration||Request!=G->PairPrepareGeneration||!Actor.IsValid()||!Partner.IsValid()||G->PreviewActor!=Actor.Get()||G->PartnerActor!=Partner.Get())return;
            G->bPairCataloguePreparing=false;G->bPairPreparing=false;
            if(Prepared->FemaleKeys.IsEmpty()){G->ShowPairStatus(TEXT("此動作載入失敗或無雙方共用階段；請選其他動作"),true);if(G->PairPreparationStatus)G->PairPreparationStatus->SetText(FText::FromString(TEXT("載入失敗：")+Prepared->Label));return;}
            G->ReadyPairs[I]=Prepared;++G->PairCatalogueProcessed;
            if(G->PairPreparationStatus)G->PairPreparationStatus->SetText(FText::FromString(TEXT("已載入：")+Prepared->Label));
            if(G->SelectedPairCatalogueIndex()==I)G->OnPairSelected(G->PairCombo->GetSelectedOption(),ESelectInfo::Direct);
            UE_LOG(LogTemp,Display,TEXT("OK pair demand prepared index=%d states=%d"),I,Prepared->FemaleKeys.Num());
        });
    });
}
