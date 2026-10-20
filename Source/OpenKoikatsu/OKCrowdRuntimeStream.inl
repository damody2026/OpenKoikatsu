// FScene methods. Plain-data workers own their inputs, never the scene lifetime.
// Two slots may prepare at once. Each request carries an epoch. Cancel, a
// failed admission, the 60-second deadline, and scene exit all make that epoch
// unable to publish or to start paired speech.
void ServiceStream(){
 if(!Installed||!Render->Ready.Load()||CaptureStage==1||CaptureStage==2)return;
 for(auto& Entry:Entries)if(Entry->PendingAction!=INDEX_NONE&&FPlatformTime::Seconds()-Entry->PendingActionAt>OKCrowdRuntimeActionPrepareSeconds){
  if(!Entry->PendingSpeechAction.IsEmpty()&&ClipIds.Find(Entry->PendingSpeechAction)==Entry->PendingAction)Entry->CancelSpeech();
  Entry->PendingAction=INDEX_NONE;Entry->PendingEpoch=0;++Entry->StreamErrors;
  UE_LOG(LogTemp,Warning,TEXT("Runtime action request exceeded 60-second preparation deadline; retained current animation"));
 }
 for(int32 Index=0;Index<OKCrowdRuntimeActionSlots;++Index){
  auto& Slot=ActionSlots[Index];if(Slot.Actor==INDEX_NONE||!Entries.IsValidIndex(Slot.Actor)){if(Slot.Actor!=INDEX_NONE)ClearActionSlot(Slot);continue;}
  auto& E=*Entries[Slot.Actor];
  const bool Want=Slot.Epoch!=0&&Slot.Epoch==E.PendingEpoch&&E.PendingAction==Slot.Source;
  const bool Live=E.StreamInstalled&&Slot.Epoch!=0&&Slot.Epoch==E.InstalledEpoch;
  if(!Want&&!Live){
   // An in-flight upload has not recorded its lease yet. Wait until it does,
   // then drop it. Do not publish the catalog for this epoch.
   if(Slot.Install&&Slot.Install->Status.Load()==0)continue;
   if(Slot.Install&&Slot.Install->Status.Load()==1){const int32 SlotIndex=Index;ENQUEUE_RENDER_COMMAND(DiscardUnactivatedRuntimeAction)([R=Render,SlotIndex](FRHICommandListImmediate& Cmd){R->ReleaseStreamIndex(SlotIndex,Cmd);});}
   ++StaleActionDrops;ClearActionSlot(Slot);continue;
  }
  if(Live){
   // A speech waiting for the replacement must not pin the old overlay.
   const bool SpeechBlocks=!E.PendingSpeechAction.IsEmpty()&&ClipIds.Find(E.PendingSpeechAction)==Slot.Source;
   const bool ClipBusy=E.ActionOverride!=INDEX_NONE||E.Clock.UsesClip(2);
   const bool Supersede=E.StreamHold&&E.PendingEpoch!=0&&E.ClipSources.IsValidIndex(2)&&E.PendingAction!=E.ClipSources[2];
   if(!SpeechBlocks&&!ClipBusy&&(!E.StreamHold||Supersede)){
    if(!RestoreStreamCatalog(E))continue;
    const int32 SlotIndex=Index;ENQUEUE_RENDER_COMMAND(RetireRuntimeAction)([R=Render,SlotIndex](FRHICommandListImmediate& Cmd){R->ReleaseStreamIndex(SlotIndex,Cmd);});
    ++E.StreamReleases;ClearActionSlot(Slot);
   }
   continue;
  }
  if(Slot.Job.IsValid()&&Slot.Job.IsReady()){
   Slot.Prepared=MakeUnique<FStreamPrepared>(Slot.Job.Consume());E.StreamPrepareMs=Slot.Prepared->Ms;
   if(!Slot.Prepared->Error.IsEmpty()){
    ++E.StreamErrors;UE_LOG(LogTemp,Warning,TEXT("Runtime action preparation: %s"),*Slot.Prepared->Error);
    if(!E.PendingSpeechAction.IsEmpty()&&ClipIds.Find(E.PendingSpeechAction)==Slot.Source)E.CancelSpeech();
    E.PendingAction=INDEX_NONE;E.PendingEpoch=0;ClearActionSlot(Slot);continue;
   }
  }
  if(Slot.Prepared&&!Slot.Install&&!E.Clock.UsesClip(2)){
   Slot.Install=MakeShared<FStreamInstall,ESPMode::ThreadSafe>();
   Slot.Install->Bank=TSharedPtr<FOKGpuAnimationBundle,ESPMode::ThreadSafe>(Slot.Prepared->Packed.Bank.Release());
   const int32 SlotIndex=Index;
   ENQUEUE_RENDER_COMMAND(AdmitRuntimeAction)([R=Render,S=Slot.Install,SlotIndex](FRHICommandListImmediate& Cmd){
    auto& Gpu=R->Streams[SlotIndex];
    FRDGBuilder G(Cmd);FString Error;R->Budget->Collect();uint64 Scratch=0;
    // Same per-slot scratch charge as the previous single overlay. A second slot
    // pays it again. Rejection keeps the pose already on screen.
    if(!R->Budget->Reserve(uint64(R->Bank->Data.Pose.Tasks.Num())*96,Scratch,Error)){S->Error=Error;G.Execute();S->Status.Store(-1);return;}
    if(!Gpu.Pose->InitializeManaged(G,S->Bank->Data.Pose,S->Bank->Data.OwnedClips,R->Budget,Error)){
     R->Budget->CancelUnused(Scratch);S->Error=Error;G.Execute();S->Status.Store(-1);return;
    }
    // Upload readiness is not activation. The game thread must publish the
    // matching clip catalog and CPU keys before render playback can use this bank.
    G.Execute();Gpu.ScratchLease=Scratch;S->Status.Store(1);
   });
  }
  if(Slot.Install&&Slot.Install->Status.Load()==-1){
   ++E.StreamErrors;UE_LOG(LogTemp,Warning,TEXT("Runtime action GPU admission retained current animation: %s"),*Slot.Install->Error);
   if(!E.PendingSpeechAction.IsEmpty()&&ClipIds.Find(E.PendingSpeechAction)==Slot.Source)E.CancelSpeech();
   E.PendingAction=INDEX_NONE;E.PendingEpoch=0;ClearActionSlot(Slot);continue;
  }
  if(Slot.Install&&Slot.Install->Status.Load()==1&&Slot.Prepared&&!E.Clock.UsesClip(2)){
   FString Error;const auto& Clip=Slot.Prepared->Clips[2];
   if(!E.Clock.ReplaceInactiveClip(2,{ClipIds[Slot.Source],Clip.Duration,ClipLoops[Slot.Source]},Error)){
    // A duplicate or invalid slot cannot succeed on retry. Drop it so the other slot stays usable.
    UE_LOG(LogTemp,Error,TEXT("Runtime inactive action slot: %s"),*Error);++E.StreamErrors;
    if(!E.PendingSpeechAction.IsEmpty()&&ClipIds.Find(E.PendingSpeechAction)==Slot.Source)E.CancelSpeech();
    E.PendingAction=INDEX_NONE;E.PendingEpoch=0;
    const int32 FailedSlot=Index;ENQUEUE_RENDER_COMMAND(DiscardRejectedRuntimeAction)([R=Render,FailedSlot](FRHICommandListImmediate& Cmd){R->ReleaseStreamIndex(FailedSlot,Cmd);});
    ClearActionSlot(Slot);continue;
   }
   E.BaseClips=MoveTemp(E.Clips);E.BaseKeys=E.CpuKeys;E.BaseSource=E.ClipSources[2];
   E.Clips=MoveTemp(Slot.Prepared->Clips);E.CpuKeys=MoveTemp(Slot.Prepared->Packed.CpuKeys);E.ClipSources[2]=Slot.Source;
   const int32 SlotIndex=Index;const int32 Actor=Slot.Actor;const uint64 Epoch=Slot.Epoch;auto Install=Slot.Install;
   ENQUEUE_RENDER_COMMAND(ActivateAcknowledgedRuntimeAction)([R=Render,S=Install,SlotIndex,Actor,Epoch](FRHICommandListImmediate&){
    auto& Gpu=R->Streams[SlotIndex];Gpu.Stream=S;Gpu.Actor=Actor;Gpu.Session=Epoch;Gpu.Reset=true;
   });
   const FVector PreviousShadowExtent=E.AnimationBounds.IsValid?E.AnimationBounds.GetExtent():FVector::ZeroVector;
   E.AnimationBounds+=Slot.Prepared->Bounds;
   UE_LOG(LogTemp,Display,TEXT("APNG shadow stream bounds: character=%s sections=%d before_cm=%s stream_cm=%s union_cm=%s"),*E.Id,E.MergedSections,*PreviousShadowExtent.ToString(),*Slot.Prepared->Bounds.GetExtent().ToString(),*E.AnimationBounds.GetExtent().ToString());
   for(auto* Component:{E.Gpu.Get(),E.DepthGpu.Get(),E.ShadowGpu.Get(),E.FrontGpu.Get(),E.OccluderGpu.Get()})if(Component){
    if(auto* Provider=Cast<UOKIndependentTransformProvider>(Component->GetTransformProvider()))Provider->SetDiagnosticBounds(E.AnimationBounds);
    Component->MarkRenderStateDirty();
   }
   const bool PlayNow=E.PendingPlay&&E.PendingSpeechAction.IsEmpty();const double PrepareMs=Slot.Prepared->Ms;
   E.InstalledEpoch=Epoch;E.StreamInstalled=true;E.PendingAction=INDEX_NONE;E.PendingEpoch=0;E.StreamPrepareMs=PrepareMs;
   Slot.Prepared.Reset();++E.StreamInstalls;
   if(PlayNow){E.Clock.RequestClip(2,.3,0,Error);E.ActionOverride=2;E.LastClip=2;++E.ActionRequests;}
   else if(E.PendingSpeechAction.IsEmpty())E.StreamHold=true;
   UE_LOG(LogTemp,Display,TEXT("Runtime streamed action slot=%d actor=%d clip=%s prepare_ms=%.3f"),SlotIndex,Actor,*ClipIds[Slot.Source],PrepareMs);
  }
 }
 for(int32 Actor=0;Actor<Entries.Num();++Actor){
  const int32 SlotIndex=FreeActionSlot();if(SlotIndex==INDEX_NONE)break;
  auto& E=*Entries[Actor];
  if(E.PendingAction==INDEX_NONE||E.PendingEpoch==0||E.Clips.Num()<3||E.Clock.UsesClip(2)||E.StreamInstalled||ActionSlotBusy(Actor))continue;
  auto& Slot=ActionSlots[SlotIndex];Slot.Actor=Actor;Slot.Source=E.PendingAction;Slot.Epoch=E.PendingEpoch;
  const double StreamPrepareStart=FPlatformTime::Seconds();const int32 Source=Slot.Source;
  auto Request=E.PoseRecipe;Request.AuthoredCurves=Curves[Source];if(const auto* Contact=E.ContactRecipes.Find(Source))Request.ContactRecipe=*Contact;
  // Long common activities remain at 60 Hz; only two on-demand preparation slots exist (key ceiling: FKKRuntimePoseRequest).
  Request.Duration=OKRuntimeSharedClipDuration(Request.AuthoredCurves);Request.AuthoredPrefix=ClipWindows.IsValidIndex(Source)&&ClipWindows[Source]>0;if(Request.AuthoredPrefix)Request.Duration=FMath::Min(Request.Duration,ClipWindows[Source]);Request.ClipIdentity=ClipIds[Source];
  auto Job=OKPrepareRuntimePoseBankAsync(MoveTemp(Request));auto Clips=E.Clips;auto Keys=E.CpuKeys;
  auto Recipe=E.ActorRecipe;auto Palette=E.PaletteRecipe;auto Envelope=E.SkinEnvelope;
  TArray<FString> Ids;for(int32 Clip=0;Clip<E.ClipSources.Num();++Clip)Ids.Add(SourceId(Clip==2?Source:E.ClipSources[Clip]));
  Slot.Job=Async(EAsyncExecution::ThreadPool,[Job=MoveTemp(Job),Clips=MoveTemp(Clips),Keys,Recipe=MoveTemp(Recipe),Palette=MoveTemp(Palette),Envelope,Ids=MoveTemp(Ids),Begin=StreamPrepareStart]()mutable{
   FStreamPrepared Result;auto New=Job.Consume();
   if(!New.Passed()){Result.Error=New.Error;return Result;}
   // Same weighted private-rig envelope as the bank, for this action only.
   // Missing geometry fails closed. Do not fall back to a per-bone sphere.
   // Bound all local channels together: parent scale from one clip can multiply
   // child translation from another during a crossfade.
   TArray<FOKRuntimeSkinEnvelopeClip> Views;Views.Reserve(Clips.Num());
   for(int32 Clip=0;Clip<Clips.Num();++Clip)if(Clip==2)Views.Add({New.Keys,New.Tracks});else Views.Add({*Keys,Clips[Clip].Tracks});
   double Radius=0;
   if(!Envelope.IsValid()||!OKRuntimeSkinEnvelopeRadius(*Envelope,Views,Radius,Result.Error)||!FMath::IsFinite(Radius)||Radius<=0){
    if(Result.Error.IsEmpty()||Result.Error==TEXT("Runtime animation envelope invalid"))Result.Error=TEXT("Streamed private animation bounds invalid");
    return Result;
   }
   Result.Bounds=OKRuntimeSkinEnvelopeBounds(Radius);
   Clips[2]=MoveTemp(New);TArray<FOKRuntimeGpuClip> Inputs;
   for(int32 Clip=0;Clip<Clips.Num();++Clip){
    FOKRuntimeGpuClip Item;Item.Id=Ids[Clip];Item.Duration=Clips[Clip].Duration;
    for(auto Track:Clips[Clip].Tracks){
     TConstArrayView<FVector4f> KeySource=Clip==2?TConstArrayView<FVector4f>(Clips[Clip].Keys):TConstArrayView<FVector4f>(*Keys);
     OKCopyGpuTrackKeys(Item.Keys,KeySource,Track);Item.Tracks.Add(Track);
    }
    Inputs.Add(MoveTemp(Item));
   }
   auto Bank=MakeUnique<FOKGpuAnimationBundle>();
   if(!OKBuildRuntimeGpuActorBank(MoveTemp(Recipe),MoveTemp(Inputs),MoveTemp(Palette),*Bank,Result.Error,768ull*1024*1024)
     ||!OKPackConstantGpuPoseChannels(Bank->Data.Pose,Result.Error))return Result;
   Result.Packed.CpuKeys=MakeShared<TArray<FVector4f>,ESPMode::ThreadSafe>(Bank->Data.Pose.Keys);
   const auto& Metadata=Bank->Manifest.Actors[0];
   for(int32 Clip=0;Clip<Clips.Num();++Clip){
    Clips[Clip].Keys.Empty();Clips[Clip].Tracks.Reset();
    Clips[Clip].Tracks.Append(Bank->Data.Pose.Tracks.GetData()+Metadata.Clips[Clip].TrackOffset,Metadata.PoseCount);
   }
   Result.Clips=MoveTemp(Clips);Result.Packed.Bank=MoveTemp(Bank);Result.Ms=(FPlatformTime::Seconds()-Begin)*1000;return Result;
  });
 }
}
