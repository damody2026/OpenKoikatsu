#include "KKPairedPlayback.h"
const TArray<uint8>* AKKPairedPlayback::WalkCycle = nullptr;
#include "KKPreviewActor.h"
#include "KKGpuStream.h"
#include "Misc/ScopeExit.h"
#include "OKPhysXSecondary.h"
#include "KKOkAnimation.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

static TAutoConsoleVariable<int32> CVarPairAsync(TEXT("ok.Anim.AsyncPair"), 1,
	TEXT("1: OK pair playback solves the next frame on a worker thread while the game thread goes on (one frame of latency)."));
static TAutoConsoleVariable<int32> CVarPairUseOK(TEXT("ok.Anim.UseOK"), 1,
	TEXT("1: paired states with a converted OK group (Saved/OKAnim/Pairs, OKAnim.SavePair / ok.Demo.PairBatch) play through the OK player; 0: always the native KK pair."));

AKKPairedPlayback::AKKPairedPlayback() {PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bStartWithTickEnabled=false;}
bool AKKPairedPlayback::Start(AKKPreviewActor* Female,AKKPreviewActor* Male,const FString& FemaleKey,const FString& MaleKey,
	const FString& State,const FString& FemaleMotion,const FString& MaleMotion,FString& Error)
{
	if(!IsValid(Female)||!IsValid(Male)||Female==Male||Female->GetWorld()!=Male->GetWorld()
		||!Female->GetActorTransform().Equals(Male->GetActorTransform(),.01f))
	{Error=TEXT("配對角色需要共同場景與原點");return false;}
	Stop();FemaleActor=Female;MaleActor=Male;FemaleClip=FemaleKey;MaleClip=MaleKey;StateName=State;FemaleIK=FemaleMotion;MaleIK=MaleMotion;
	OldFemaleTick=Female->IsActorTickEnabled();OldMaleTick=Male->IsActorTickEnabled();
	if(!Restart(Error)) {Stop();return false;}
	Female->AddPairPhysics();Male->AddPairPhysics();bPairPhysics=true;
	return true;
}
bool AKKPairedPlayback::Restart(FString& Error)
{
	SyncAsync();
	if(!IsValid(FemaleActor)||!IsValid(MaleActor)) {Error=TEXT("配對角色已關閉");return false;}
	if(!FemaleActor->Session.HasAnimationClip(FemaleClip)||!MaleActor->Session.HasAnimationClip(MaleClip)) {Error=TEXT("配對控制器狀態不存在");return false;}
	if(!FemaleActor->SetGameSceneMode(3,false)||!MaleActor->SetGameSceneMode(3,false)) {Error=TEXT("配對場景模式設定失敗");return false;}
	// Choose both controllers without publishing independent, uncorrected
	// frames. Advance(0) below publishes the single coupled initial frame.
	if(!FemaleActor->SetGameAnimation(FemaleClip,false)||!MaleActor->SetGameAnimation(MaleClip,false)) {Error=TEXT("配對控制器狀態不存在");return false;}
	FemaleActor->SetCoordinatedPlayback(true);MaleActor->SetCoordinatedPlayback(true);
	FemaleActor->SetActorTickEnabled(false);MaleActor->SetActorTickEnabled(false);
	Elapsed=0.;bPaused=false;bActive=true;bVaginaRange=false;SetActorTickEnabled(true);
	// The converted OK group of this state, when there is one: same characters, placement, IK and physics through the OK player.
	OkPlayer.Reset();OkReport.Reset();
	if(CVarPairUseOK.GetValueOnGameThread()!=0){
		const FString Path=FOkAnimBridge::PairPackagePath(FemaleClip,MaleClip,StateName);TArray<uint8> Package;FString Spec;TArray<TArray<uint8>> Anims;
		if(IFileManager::Get().FileExists(*Path)&&FFileHelper::LoadFileToArray(Package,*Path)&&FOkAnimBridge::ParseGroupPackage(Package,Spec,Anims)){
			TSharedPtr<FOkAnimPlayer> Player=MakeShared<FOkAnimPlayer>();
			if(Player->Create(Spec,Anims)&&Player->Roles()==2){OkPlayer=Player;if(WalkCycle)OkPlayer->SetWalk(*WalkCycle);}else UE_LOG(LogTemp,Warning,TEXT("OK pair group unusable: %s %s"),*Path,*FOkAnimBridge::LastError());
		}
	}
	// A new pose can put hair/cloth inside the bodies: settle the chains first (generic, every pose and state).
	OKPhysXSecondary::Settle();
	if(!Advance(0.f,Error)){Stop();return false;}return true;
}
bool AKKPairedPlayback::SetSpeed(float Value) {if(!FMath::IsFinite(Value)||Value<0.f||Value>4.f) return false;Speed=Value;return true;}
bool AKKPairedPlayback::Advance(float Delta,FString& Error)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairAdvance);
	const double Begin=bMeasureAdvance?FPlatformTime::Seconds():0.;
	ON_SCOPE_EXIT{if(Begin>0.){const double Ms=(FPlatformTime::Seconds()-Begin)*1000.;++AdvanceSamples;AdvanceTotalMs+=Ms;AdvanceMaximumMs=FMath::Max(AdvanceMaximumMs,Ms);}};
	if(!bActive||!IsValid(FemaleActor)||!IsValid(MaleActor)||!FMath::IsFinite(Delta)||Delta<0.f) {Error=TEXT("無效配對播放狀態");return false;}
	// A frame solved in the background (ok.Anim.AsyncPair) is shown first: nothing below may read the sessions while
	// the worker writes them.
	if(bSolvePending){SyncAsync();if(!bSolveOk){Error=TEXT("OK pair frame: ")+OkReport;return false;}}
    const bool Reactions[2]={FemaleActor->Session.IsBodyReactionPlaying(),MaleActor->Session.IsBodyReactionPlaying()};
    const bool BodyUpdate=Reactions[0]||Reactions[1]||FemaleActor->bBodyReactionPoseDirty||MaleActor->bBodyReactionPoseDirty;
	if(bPaused&&!BodyUpdate&&!FemaleActor->bShapeDirty&&!MaleActor->bShapeDirty&&!FemaleActor->bHeadLookFollowCamera&&!FemaleActor->bHeadLookPoseDirty&&!MaleActor->bHeadLookFollowCamera&&!MaleActor->bHeadLookPoseDirty){for(auto* PreviewRole:{FemaleActor.Get(),MaleActor.Get()}){PreviewRole->BeginFaceDisplayUpdate();PreviewRole->AdvanceEyeLook(Delta);PreviewRole->AdvanceFaceAnimation(Delta);PreviewRole->AdvanceSpeech();PreviewRole->EndFaceDisplayUpdate();}return true;}
	const float PoseDelta=bPaused?0.f:Delta;
	const double Next=FMath::Min(Elapsed+double(PoseDelta)*double(Speed),double(MAX_flt));
	FemaleActor->UpdateHeadLookTarget();MaleActor->UpdateHeadLookTarget();
	for(auto* PreviewRole:{FemaleActor.Get(),MaleActor.Get()})if(PreviewRole->bShapeDirty){TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairLiveShape);if(!PreviewRole->Session.ApplyShapeLive(PreviewRole->PendingBody,PreviewRole->PendingFace,true)){Error=TEXT("配對角色形狀更新失敗");return false;}PreviewRole->bShapeDirty=false;}
 if(OkPlayer&&CVarPairAsync.GetValueOnGameThread()!=0&&Delta>0.f){TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairOKAsync);
  // The next frame is solved in the background and shown at the start of the next tick.
  FemaleActor->PublishPhysicsFrame();MaleActor->PublishPhysicsFrame();
  PendingNext=Next;PendingDelta=BodyUpdate?Delta:PoseDelta;PendingReactions[0]=Reactions[0];PendingReactions[1]=Reactions[1];
  TSharedPtr<FOkAnimPlayer> Player=OkPlayer;FOkSession* F=&FemaleActor->Session;FOkSession* M=&MaleActor->Session;const float T=float(Next),D=PendingDelta;
  bSolvePending=true;bPendingNative=false;
  PendingSolve=UE::Tasks::Launch(UE_SOURCE_LOCATION,[this,Player,F,M,T,D]{bSolveOk=Player->ApplySessions({F,M},T,D,&PendingReport);});
  return true;}
 if(OkPlayer){TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairOK);
  FemaleActor->PublishPhysicsFrame();MaleActor->PublishPhysicsFrame();
  if(!OkPlayer->ApplySessions({&FemaleActor->Session,&MaleActor->Session},float(Next),BodyUpdate?Delta:PoseDelta,&OkReport)){Error=TEXT("OK pair frame: ")+OkReport;return false;}
  OkPlayer->ApplyFaceCurves(0,float(Next),*FemaleActor);OkPlayer->ApplyFaceCurves(1,float(Next),*MaleActor);}
 else{
  // No OK group for this state (none are saved for the game's pairs, so this is every one-on-one pose): the native
  // GPU pair frame is a Rust call as well and goes to a worker the same way. On the game thread it took 6 ms of a
  // 13 ms frame (77 FPS in the one-on-one scene).
  TArray<int32> FemaleParts,MaleParts;TArray<uint64> FemaleRevisions,MaleRevisions;
  if(CVarPairAsync.GetValueOnGameThread()!=0&&Delta>0.f&&AKKPreviewActor::PairedFrameGpuParts(*FemaleActor,*MaleActor,FemaleParts,FemaleRevisions,MaleParts,MaleRevisions)){
   TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairNativeAsync);
   FemaleActor->PublishPhysicsFrame();MaleActor->PublishPhysicsFrame();
   PendingNext=Next;PendingDelta=BodyUpdate?Delta:PoseDelta;PendingReactions[0]=Reactions[0];PendingReactions[1]=Reactions[1];
   FOkSession* F=&FemaleActor->Session;FOkSession* M=&MaleActor->Session;const float T=float(Next),D=PendingDelta;
   bSolvePending=true;bPendingNative=true;
   PendingSolve=UE::Tasks::Launch(UE_SOURCE_LOCATION,[this,F,M,T,D,State=StateName,FemaleMotion=FemaleIK,MaleMotion=MaleIK,
    FemaleParts=MoveTemp(FemaleParts),FemaleRevisions=MoveTemp(FemaleRevisions),MaleParts=MoveTemp(MaleParts),MaleRevisions=MoveTemp(MaleRevisions)]{
    FString Report;bSolveOk=FOkSession::ApplyPairedFrameGpu(*F,*M,T,D,State,FemaleMotion,MaleMotion,FemaleParts,FemaleRevisions,MaleParts,MaleRevisions,Report);PendingReport=Report;});
   return true;}
  TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairNative);if(!AKKPreviewActor::ApplyPairedFrameWithDisplay(*FemaleActor,*MaleActor,float(Next),BodyUpdate?Delta:PoseDelta,StateName,FemaleIK,MaleIK,Error)) return false;}
	Elapsed=Next;
	return ShowFrame(Delta,Reactions,Error);
}
bool AKKPairedPlayback::ShowFrame(float Delta,const bool Reactions[2],FString& Error)
{

 FKKGpuStreamBatchScope GpuFrame;
	for(AKKPreviewActor* PreviewRole : {FemaleActor.Get(),MaleActor.Get()}) {
  TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairRoleDisplay);
		PreviewRole->BeginFaceDisplayUpdate();
		PreviewRole->bHeadLookPoseDirty=false;
        const int32 BodyRoleIndex=PreviewRole==FemaleActor?0:1;
        PreviewRole->bBodyReactionPoseDirty=Reactions[BodyRoleIndex]&&!PreviewRole->Session.IsBodyReactionPlaying();
		PreviewRole->AnimPhase=float(Elapsed);PreviewRole->AnimDuration=PreviewRole->Session.AnimDuration();PreviewRole->RefreshSkinnedDisplay();
  {TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairFace);
		PreviewRole->AdvanceEyeLook(Delta);PreviewRole->AdvanceFaceAnimation(Delta);
		if(PreviewRole->IsSpeaking()) PreviewRole->AdvanceSpeech();
		PreviewRole->EndFaceDisplayUpdate();}
  {TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairShadow);PreviewRole->RefreshShotShadow(true);}
	}
 {TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairContactMorph);FemaleActor->DriveVaginaOpening(*MaleActor,VaginaRangeMin,VaginaRangeMax,bVaginaRange);}
	return true;
}
void AKKPairedPlayback::SyncAsync()
{
	if(!bSolvePending)return;
	{TRACE_CPUPROFILER_EVENT_SCOPE(KK_PairOKWait);PendingSolve.Wait();}
	bSolvePending=false;OkReport=PendingReport;
	if(!bSolveOk||!IsValid(FemaleActor)||!IsValid(MaleActor))return;
	if(OkPlayer&&!bPendingNative){OkPlayer->ApplyFaceCurves(0,float(PendingNext),*FemaleActor);OkPlayer->ApplyFaceCurves(1,float(PendingNext),*MaleActor);}
	Elapsed=PendingNext;
	FString Error;
	ShowFrame(PendingDelta,PendingReactions,Error);
}
void AKKPairedPlayback::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);if(!bActive) return;FString Error;
	if(!Advance(DeltaSeconds,Error)) {UE_LOG(LogTemp,Error,TEXT("OK paired playback failed: %s"),*Error);Stop();}
}
void AKKPairedPlayback::Stop() {
	SyncAsync();
	if(bPairPhysics){if(IsValid(FemaleActor))FemaleActor->RemovePairPhysics();if(IsValid(MaleActor))MaleActor->RemovePairPhysics();bPairPhysics=false;}
	if(bActive) {if(IsValid(FemaleActor)){FemaleActor->SetCoordinatedPlayback(false);FemaleActor->CloseVaginaOpening();FemaleActor->SetActorTickEnabled(OldFemaleTick);}if(IsValid(MaleActor)){MaleActor->SetCoordinatedPlayback(false);MaleActor->SetActorTickEnabled(OldMaleTick);}}
	bVaginaRange=false;
	bActive=false;SetActorTickEnabled(false);
}
void AKKPairedPlayback::EndPlay(const EEndPlayReason::Type Reason) {Stop();Super::EndPlay(Reason);}
