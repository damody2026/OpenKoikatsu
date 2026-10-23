// Shared ambient activity descriptors; all private poses and attachments use the selected APNG.
struct FOKStreetAmbientPart {FVector Size,Offset;FRotator Rotation;int32 Material;};
struct FOKStreetAmbientDefinition {
 FString Clip;const TCHAR* Hand;const TCHAR* Mesh;TArray<FOKStreetAmbientPart> Parts;
 double PlaybackSeconds=8,PrepareSeconds=60,CooldownSeconds=45;bool OneShot=false;
 FTransform Transform(const FTransform& Bone,int32 Part) const {const auto& P=Parts[Part];return FTransform(P.Rotation,P.Offset,P.Size/100.f)*Bone;}
};
static FOKStreetAmbientDefinition OKStreetAmbientDefinition(int32 Kind,const FString& DrinkClip){
 if(Kind==3){FOKStreetAmbientDefinition D{TEXT("groom_standing.okanim"),TEXT("k_f_handR_00"),nullptr,{}};D.OneShot=true;return D;}
 if(Kind==1)return {DrinkClip,TEXT("k_f_handR_00"),TEXT("/Game/Scenes/StreetSocial/Meshes/StreetCylinder.StreetCylinder"),{
  {FVector(5.6f,5.6f,19),FVector(0,0,10),FRotator::ZeroRotator,9},
  {FVector(3.6f,3.6f,2.4f),FVector(0,0,20.7f),FRotator::ZeroRotator,11}}};
 return {Kind==2?TEXT("selfie_standing.okanim"):TEXT("phone_standing.okanim"),TEXT("k_f_handL_00"),TEXT("/Game/Scenes/StreetSocial/Meshes/StreetCube.StreetCube"),{
  {FVector(6.358756f,13.567981f,.703464f),FVector::ZeroVector,FRotator(0,0,180),11},
  {FVector(5.8f,11.4f,.06f),FVector(0,0,-.39f),FRotator(0,0,180),5},
  {FVector(.7f,.7f,.08f),FVector(2,5,.4f),FRotator(0,0,180),7}}};
}
// One autonomous owner leaves the second runtime action slot available to gameplay.
void AOKStreetSocialScene::StopAmbientActivity(){
 if(!State||State->AmbientActor==INDEX_NONE)return;auto& S=*State;const int32 Character=S.AmbientActor;
 S.AmbientActor=INDEX_NONE;S.AmbientPhase=0;
 for(auto& Prop:S.AmbientProps)if(Prop.IsValid())Prop->Destroy();S.AmbientProps.Reset();
 FString Error;OKResumeCrowdRuntimeBehavior(this,Character,Error);
 if(S.Agents.IsValidIndex(Character-1)){
  const FVector Position=S.Agents[Character-1].Position;S.Agents[Character-1]=S.AmbientSaved;
  auto& Agent=S.Agents[Character-1];Agent.Position=Position;Agent.Held=false;Agent.Dwell=0;Agent.DwellPoint=INDEX_NONE;Agent.Stuck=0;Agent.Mode=EOKStreetMove::Walking;
 }
 S.AmbientCooldown.Add(Character,SceneTime()+OKStreetAmbientDefinition(S.AmbientAction,S.DrinkFemaleClip).CooldownSeconds);
}
void AOKStreetSocialScene::TickAmbientActivity(double Now){
 auto& S=*State;
 const bool Foreground=S.Target!=INDEX_NONE||S.Pair.Partner!=INDEX_NONE||S.Companion!=INDEX_NONE||S.Facility!=INDEX_NONE||S.PrivateGirl!=INDEX_NONE||!S.LifePoses.IsEmpty();
 if(Foreground||!S.AllReady||(S.SelfTest&&!S.AmbientOnlyTest&&!S.AmbientDrinkOnlyTest&&!S.AmbientSelfieOnlyTest&&!S.AmbientGroomOnlyTest)){StopAmbientActivity();return;}

 if(S.AmbientActor==INDEX_NONE){
  for(int32 Offset=0;Offset<S.Agents.Num();++Offset){
   const int32 Index=(S.AmbientCursor+Offset)%S.Agents.Num();auto& Agent=S.Agents[Index];
   if(Agent.Held||Agent.Dwell<=1||(Agent.Mode!=EOKStreetMove::Resting&&Agent.Mode!=EOKStreetMove::Looking)||S.AmbientCooldown.FindRef(Agent.Slot)>Now)continue;
   FOKRuntimeCharacterStatus Status;if(!OKGetCrowdRuntimeCharacterStatus(this,Agent.Slot,Status)||!Status.Visible||!Status.ActionsReady||!Status.Error.IsEmpty())continue;
   S.AmbientAction=S.AmbientGroomOnlyTest?3:S.AmbientSelfieOnlyTest?2:S.AmbientDrinkOnlyTest?1:S.AmbientOnlyTest?0:S.AmbientNextAction;
   const auto Definition=OKStreetAmbientDefinition(S.AmbientAction,S.DrinkFemaleClip);if(Definition.Clip.IsEmpty())continue;
   S.AmbientNextAction=(S.AmbientAction+1)%4;
   S.AmbientActor=Agent.Slot;S.AmbientSaved=Agent;S.AmbientRequested=Now;S.AmbientPhase=0;S.AmbientCursor=(Index+1)%S.Agents.Num();Agent.Held=true;
   FString Error;if(!OKPrewarmCrowdRuntimeAction(this,Agent.Slot,Definition.Clip,Error))StopAmbientActivity();return;
  }
  return;
 }
 const auto Definition=OKStreetAmbientDefinition(S.AmbientAction,S.DrinkFemaleClip);const auto& Clip=Definition.Clip;
 const int32 Character=S.AmbientActor;FOKRuntimeCharacterStatus Status;FOKRuntimeActionStreamStatus Action;
 if(!OKGetCrowdRuntimeCharacterStatus(this,Character,Status)||!Status.Visible||!Status.Error.IsEmpty()||!OKGetCrowdRuntimeActionStatus(this,Character,Action)){StopAmbientActivity();return;}
 if(S.AmbientPhase==0){
  if(Now-S.AmbientRequested>=Definition.PrepareSeconds){StopAmbientActivity();return;}
  if(!Action.Ready||Action.Pending||Action.Preparing||Action.ClipId!=Clip)return;
  auto* Mesh=Definition.Parts.IsEmpty()?nullptr:LoadObject<UStaticMesh>(nullptr,Definition.Mesh);if(!Definition.Parts.IsEmpty()&&!Mesh){StopAmbientActivity();return;}
  for(int32 Part=0;Part<Definition.Parts.Num();++Part){
   auto* Prop=GetWorld()->SpawnActor<AStaticMeshActor>();if(!Prop){StopAmbientActivity();return;}Prop->SetOwner(this);Prop->SetActorHiddenInGame(true);
   auto* C=Prop->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Mesh);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
   const int32 Material=Definition.Parts[Part].Material;const FString Path=FString::Printf(TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial%02d.StreetMaterial%02d"),Material,Material);
   auto* M=LoadObject<UMaterialInterface>(nullptr,*Path);if(!M){Prop->Destroy();StopAmbientActivity();return;}C->SetMaterial(0,M);S.AmbientProps.Add(Prop);
  }
  FString Error;if(!OKPlayCrowdRuntimeAction(this,Character,Clip,Error)){StopAmbientActivity();return;}
  FOKRuntimeActionStreamStatus Started;if(!OKGetCrowdRuntimeActionStatus(this,Character,Started)||!FMath::IsFinite(Started.DurationSeconds)||Started.DurationSeconds<=0){StopAmbientActivity();return;}
  S.AmbientDuration=Started.DurationSeconds;S.AmbientPhase=1;S.AmbientPlaying=Now;return;
 }
 if(Definition.OneShot&&!Action.Playing){if(Now-S.AmbientPlaying+.35>=S.AmbientDuration)++S.AmbientNaturalCompletions;StopAmbientActivity();return;}
 if(Now-S.AmbientPlaying>=(Definition.OneShot?S.AmbientDuration+2:Definition.PlaybackSeconds)||!Action.Playing||Action.ClipId!=Clip||S.AmbientProps.Num()!=Definition.Parts.Num()){StopAmbientActivity();return;}
 FTransform Hand;if(!OKGetCrowdRuntimeBoneTransform(this,Character,Definition.Hand,Hand)||Hand.ContainsNaN()){StopAmbientActivity();return;}
 for(int32 Part=0;Part<Definition.Parts.Num();++Part){if(!S.AmbientProps[Part].IsValid()){StopAmbientActivity();return;}auto* Prop=S.AmbientProps[Part].Get();Prop->SetActorTransform(Definition.Transform(Hand,Part));Prop->SetActorHiddenInGame(false);}
 ++S.AmbientSamples;
}
void AOKStreetSocialScene::RunAmbientActivityProbe(double Now){
 auto& S=*State;const int32 Character=S.AmbientVerified+1;const auto Definition=OKStreetAmbientDefinition(S.AmbientGroomOnlyTest?3:S.AmbientSelfieOnlyTest?2:S.AmbientDrinkOnlyTest?1:0,S.DrinkFemaleClip);
 auto Fail=[&](const FString& Why){StopAmbientActivity();S.Errors.Add(Why);WriteReport(false);};
 if(Character>5){S.AmbientChecked=true;return;}
 auto Fixture=[&](){
  for(auto& Agent:S.Agents){Agent.Held=true;Agent.Dwell=0;}
  auto& Agent=S.Agents[Character-1];TArray<FOKStreetPointData> Points;OKStreetWaypoints(Points);
  for(int32 I=0;I<Points.Num();++I)if(Points[I].Kind==EOKStreetPoint::Rest){Agent.DwellPoint=I;Agent.Position=Points[I].Position;break;}
  Agent.Held=false;Agent.Mode=EOKStreetMove::Resting;Agent.Dwell=20;S.AmbientCooldown.Remove(Character);
  S.Male=Agent.Position-FVector(150,0,0);S.AmbientProbeAt=Now;
 };
 if(S.AmbientProbePhase==0){StopAmbientActivity();S.AmbientProbeSave=OKStreetWriteSave(S.Store);Fixture();S.AmbientProbePhase=1;return;}
 if(Now-S.AmbientProbeAt>65){Fail(TEXT("Autonomous activity phase timed out"));return;}
 if(S.AmbientProbePhase==1){
  if(S.AmbientActor!=Character)return;BeginPair(Character,8);
  if(S.Pair.Partner!=Character||S.AmbientActor!=INDEX_NONE){Fail(TEXT("Foreground pair did not preempt autonomous preparation"));return;}FinishPhoto();
  if(!S.AmbientProps.IsEmpty()||S.Agents[Character-1].Held){Fail(TEXT("Autonomous pending cancellation retained ownership"));return;}
  ++S.AmbientPrepareCancels;S.AmbientProbeAt=Now;S.AmbientProbePhase=2;return;
 }
 if(S.AmbientProbePhase==2){
  if(Now-S.AmbientProbeAt<1)return;FOKRuntimeActionStreamStatus A;
  if(S.AmbientActor!=INDEX_NONE||!S.AmbientProps.IsEmpty()||!OKGetCrowdRuntimeActionStatus(this,Character,A)||A.Playing){Fail(TEXT("Cancelled autonomous preparation published late"));return;}
  Fixture();S.AmbientProbePhase=3;return;
 }
 if(S.AmbientProbePhase==3||S.AmbientProbePhase==5){
  if(S.AmbientActor!=Character||S.AmbientPhase!=1||Now-S.AmbientPlaying<(Definition.OneShot?FMath::Min(1.0,S.AmbientDuration*.15):1.0))return;
  FOKRuntimeActionStreamStatus A;FTransform Hand;
  if(!OKGetCrowdRuntimeActionStatus(this,Character,A)||!A.Playing||A.ClipId!=Definition.Clip||!OKGetCrowdRuntimeBoneTransform(this,Character,Definition.Hand,Hand)||Hand.ContainsNaN()||S.AmbientProps.Num()!=Definition.Parts.Num()||(!Definition.Parts.IsEmpty()&&(!S.AmbientProps[0].IsValid()||S.AmbientProps[0]->IsHidden()||FVector::Dist(S.AmbientProps[0]->GetActorLocation(),Definition.Transform(Hand,0).GetLocation())>.1))){Fail(TEXT("Autonomous activity private pose/hand prop mismatch"));return;}
  S.AmbientProbeHand=Hand.GetLocation();S.AmbientProbeAt=Now;
  if(S.AmbientProbePhase==3){S.Yaw=(S.Agents[Character-1].Position-S.Male).Rotation().Yaw;S.Pitch=-8;S.Distance=300;FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("street.auto-%s.%d.png"),S.AmbientGroomOnlyTest?TEXT("groom"):S.AmbientSelfieOnlyTest?TEXT("selfie"):S.AmbientDrinkOnlyTest?TEXT("drink"):TEXT("phone"),Character),false,false);S.AmbientProbePhase=4;}
  else S.AmbientProbePhase=6;return;
 }
 if(S.AmbientProbePhase==4){
  if(Now-S.AmbientProbeAt<(Definition.OneShot?FMath::Min(1.5,S.AmbientDuration*.2):1.5))return;FTransform Hand;
  if(!OKGetCrowdRuntimeBoneTransform(this,Character,Definition.Hand,Hand)||FVector::Dist(S.AmbientProbeHand,Hand.GetLocation())<.5){Fail(TEXT("Autonomous private activity hand did not move"));return;}
  if(!BeginTalk(Character)||S.Target!=Character||S.AmbientActor!=INDEX_NONE||!S.AmbientProps.IsEmpty()){Fail(TEXT("Conversation did not preempt autonomous activity"));return;}
  EndTalk();++S.AmbientInterrupts;Fixture();S.AmbientProbePhase=5;return;
 }
 if(S.AmbientProbePhase==6){
  if(S.AmbientActor!=INDEX_NONE)return;
  if(Now-S.AmbientPlaying+(Definition.OneShot?.35:0)<(Definition.OneShot?S.AmbientDuration:Definition.PlaybackSeconds)){Fail(TEXT("Autonomous activity ended before its natural duration"));return;}
  if(!S.AmbientProps.IsEmpty()||S.Agents[Character-1].Held||S.Agents[Character-1].Mode!=EOKStreetMove::Walking){Fail(TEXT("Autonomous completion did not release movement"));return;}
  S.AmbientProbePosition=S.Agents[Character-1].Position;S.AmbientProbeAt=Now;S.AmbientProbePhase=7;return;
 }
 if(Now-S.AmbientProbeAt<1.25)return;
 if(FVector::Dist2D(S.AmbientProbePosition,S.Agents[Character-1].Position)<5||OKStreetWriteSave(S.Store)!=S.AmbientProbeSave){Fail(TEXT("Autonomous activity failed walk recovery or changed social save"));return;}
 ++S.AmbientVerified;S.AmbientProbePhase=0;
}

// NPC idles: a heroine who is walking keeps the walk clip through one-frame stalls (waypoints, blocked steps), so the
// reading pose never flashes mid-walk; a stop shows the plain standing idle, and only after 10 s standing still does
// she take out her book. Most stop points are walked past; the stops that remain are long (12–20 s).
float AOKStreetSocialScene::NpcIdleTick(FOKStreetAgentSim& Agent,float Walk,double Now){
 auto& S=*State;const int32 Slot=Agent.Slot;
 if(!S.SelfTest){
  // Arriving at a look/rest/seat point: mostly walk on; now and then stop for a while (12–20 s) instead of
  // pausing a second or two at every point, which made her walk-stop-walk.
  const float Prev=S.PrevDwell.FindRef(Slot);
  if(Prev<=0&&Agent.Dwell>0&&!Agent.Held){
   if(S.Random.FRand()<0.7f){Agent.Dwell=0;Agent.DwellPoint=INDEX_NONE;Agent.Mode=EOKStreetMove::Walking;}
   else Agent.Dwell=S.Random.FRandRange(12.f,20.f);
  }
  S.PrevDwell.Add(Slot,Agent.Dwell);
 }
 // On her way somewhere (a commute, coming over to him) too: a step that did not move every other frame made her
 // switch walk/stand each frame for minutes.
 if(Walk>0){S.LastMovedAt.Add(Slot,Now);S.LastWalk.Add(Slot,Walk);S.StillSince.Add(Slot,Now);}
 else if((S.Commutes.Contains(Slot)||(!Agent.Held&&Agent.Mode==EOKStreetMove::Walking))&&Now-S.LastMovedAt.FindRef(Slot)<0.4)Walk=S.LastWalk.FindRef(Slot);
 else if(S.Commutes.Contains(Slot))S.StillSince.Add(Slot,Now);
 else if(!S.StillSince.Contains(Slot))S.StillSince.Add(Slot,Now);
 // Held by her schedule she may read during leisure or rest (not at work, a meal, class or in a conversation).
 bool FreeTime=!Agent.Held;
 if(Agent.Held&&S.ScheduleHeld.Contains(Slot)&&S.Target!=Slot&&!S.Commutes.Contains(Slot))if(const auto* B=BlockNow(Slot,StreetNow().GetHour()*60+StreetNow().GetMinute()))FreeTime=B->Kind==TEXT("leisure")||B->Kind==TEXT("rest")||B->Kind==TEXT("study");
 // Not everyone reads: each stop rolls once (about 4 in 10 stops end up with a book).
 const double Since=S.StillSince.FindRef(Slot);const bool Reader=FRandomStream(int32(FMath::Fmod(Since*10.0,2000000000.0))+Slot*7919).FRand()<0.4f;
 // Self-tests keep the old reading idle whenever she stands still (a probe holds her in place; she is not talking).
 const bool Reading=Walk<=0&&(S.SelfTest?S.Target!=Slot:FreeTime&&Reader&&Now-Since>=10.0);
 OKSetCrowdRuntimeIdleReading(this,Slot,Reading);
 {const int32 Pose=Walk>0?0:Reading?2:1;int32& Last=S.IdlePose.FindOrAdd(Slot,-1);if(Pose!=Last){Last=Pose;UE_LOG(LogTemp,Display,TEXT("Street idle: slot=%d pose=%s still=%.1f t=%.2f"),Slot,Pose==0?TEXT("walk"):Pose==1?TEXT("stand"):TEXT("read"),Now-S.StillSince.FindRef(Slot),Now);}}
 return Walk;
}
