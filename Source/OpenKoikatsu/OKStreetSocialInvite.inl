// Included by OKStreetSocialScene.cpp: plan §12 charm and invitations, §13.1 approach, §13.2 follow modes,
// §12.5 sleeping with one or two heroines.

// His look (plan §4.2b): the base from Saved/StreetSocial/player.json ("appearance_base", default 50) plus what he wears.
float AOKStreetSocialScene::PlayerLook() const{
 const auto& S=*State;float Look=S.PlayerLookBase;
 for(const FString& Item:S.Store.Inventory)Look+=Item==TEXT("dept_men_coat")?8.f:Item==TEXT("dept_men_shirt")?5.f:Item==TEXT("dept_watch")?4.f:0.f;
 return FMath::Clamp(Look,0.f,100.f);
}

// His appeal to her (plan §12.1, invite.rs appeal): his look and her idea of his wealth against her resistances.
float AOKStreetSocialScene::AppealOf(int32 Character) const{
 const auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Character))return 0.f;
 auto Request=OKRulesHeroineRequest(S,TEXT("appeal"),OKStreetStableId(S.Manifest.Characters[Character]));Request->SetNumberField(TEXT("look"),PlayerLook());
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);return Out.IsValid()?float(Out->GetNumberField(TEXT("appeal"))):0.f;
}

// This turn's answers to every kind of invitation (plan §12.2), rolled once so her words and the rules agree.
// The rules roll every kind (invite.rs stance); a kind she refused less than ten minutes ago stays a no.
FString AOKStreetSocialScene::RollInviteStance(int32 Character){
 auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 const FString Place=PlaceHere(S.Male);const FDateTime Now=StreetNow();const double T=SceneTime();
 TArray<TSharedPtr<FJsonValue>> Cooling;
 for(int32 C=0;C<int32(EOKStreetInvite::Count);++C){const double* At=S.InviteRefusedAt.Find(Id+FString::Printf(TEXT("|%d"),C));Cooling.Add(MakeShared<FJsonValueBoolean>(At&&T-*At<600));}
 auto Request=OKRulesHeroineRequest(S,TEXT("invite_stance"),Id);
 Request->SetNumberField(TEXT("look"),PlayerLook());Request->SetStringField(TEXT("place"),Place);Request->SetNumberField(TEXT("hours"),Now.GetHour()+Now.GetMinute()/60.0);
 Request->SetArrayField(TEXT("cooling"),Cooling);Request->SetNumberField(TEXT("seed"),FMath::Rand());
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return FString();
 TArray<bool> Accept;for(const auto& V:Out->GetArrayField(TEXT("accept")))Accept.Add(V->AsBool());
 S.InviteStance.Add(Id,Accept);
 return Out->GetStringField(TEXT("text"));
}

// The model said yes to an outing: the rolled stance must agree (plan §12.2). A refusal is remembered, and
// asking again and again after a no costs respect.
bool AOKStreetSocialScene::InviteAllowed(int32 Character,const FString& Action,FString& Why){
 auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 const int32 C=OKRulesInviteKind(S,Action);
 const TArray<bool>* Stance=S.InviteStance.Find(Id);
 if(!Stance||!Stance->IsValidIndex(C)||(*Stance)[C]){S.InviteRefusals.Remove(Id);return true;}
 const FString Key=Id+FString::Printf(TEXT("|%d"),C);const bool Again=S.InviteRefusedAt.Contains(Key)&&SceneTime()-S.InviteRefusedAt[Key]<600;
 S.InviteRefusedAt.Add(Key,SceneTime());
 // Her reason, and what asking again and again after a no costs (invite.rs invite_refused).
 auto Request=OKRulesHeroineRequest(S,TEXT("invite_refused"),Id);Request->SetStringField(TEXT("action"),Action);Request->SetBoolField(TEXT("again"),Again);Request->SetNumberField(TEXT("refusals"),S.InviteRefusals.FindRef(Id));
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid()){Why=OKT(TEXT("ui.invite.not_in_mood"));return false;}
 Why=Out->GetStringField(TEXT("why"));S.InviteRefusals.Add(Id,int32(Out->GetNumberField(TEXT("refusals"))));
 ApplyRulesRelation(Id,*Out);
 for(const auto& V:Out->GetArrayField(TEXT("notes"))){const TSharedPtr<FJsonObject> N=V->AsObject();HeroineNote(Character,*N->GetStringField(TEXT("act")),N->GetStringField(TEXT("text")),PlaceHere(S.Male),float(N->GetNumberField(TEXT("importance"))));}
 return false;
}

// Follow modes (plan §13.2): 0 he leads (free or autopilot), 1 she leads, 2 side by side. Only for outings.
void AOKStreetSocialScene::SetFollowMode(int32 Mode){
 auto& S=*State;S.FollowMode=FMath::Clamp(Mode,0,2);S.FollowLeadInit=false;
 static const TCHAR* Names[]={TEXT("你帶路"),TEXT("她帶路（只能自動導航）"),TEXT("並肩走（只能自動導航）")};
 S.FreeTalkNotice=OKMsg(TEXT("ui.invite.companion_label"), {{TEXT("mode"), OKT(Names[S.FollowMode])}});RefreshUI();
}

// She leads or they walk abreast along the outing route; the player only watches (input stays locked).
float AOKStreetSocialScene::StepOutingLed(float Dt){
 auto& S=*State;const int32 Girl=S.OutingGirl;if(Girl==INDEX_NONE||!S.Agents.IsValidIndex(Girl-1))return 0;
 auto& A=S.Agents[Girl-1];const float Step=OKStreetWalkSpeed*FMath::Clamp(Dt,0.f,.05f);
 if(!S.FollowLeadInit){S.FollowLead=S.FollowMode==1?A.Position:(A.Position+S.Male)*0.5f;S.FollowLeadInit=true;}
 S.FollowGirlWalk=0;
 if(S.OutingStep>=S.OutingRoute.Num()){if(FVector::Dist2D(S.Male,A.Position)<=220)ArriveOuting();return 0;}
 // She has not moved for 2 s (a wall between her and the route, leaving a shop): the way again from where she stands,
 // around what is in the way; after three tries he leads instead (his walk plans around obstacles itself).
 {const double Now=SceneTime();
  if(FVector::Dist2D(A.Position,S.LedLastPos)>5.f||S.LedBlockedSince<=0){S.LedLastPos=A.Position;S.LedBlockedSince=Now;}
  else if(Now-S.LedBlockedSince>2.0){S.LedBlockedSince=Now;++S.LedReplans;
   if(S.LedReplans>3){S.LedReplans=0;UE_LOG(LogTemp,Display,TEXT("Street outing: actor=%d cannot lead past %s; he leads"),Girl,*A.Position.ToCompactString());SetFollowMode(0);return 0;}
   TArray<FVector> Way=GridRoute(A.Position,S.OutingRoute.Last());if(Way.Num()<2)Way=PlaceRoute(A.Position,S.OutingRoute.Last());
   if(Way.Num()>=2){S.OutingRoute=MoveTemp(Way);S.OutingStep=1;}
   S.FollowLead=A.Position;
   UE_LOG(LogTemp,Display,TEXT("Street outing: actor=%d leads blocked at %s, way again (%d points, try %d)"),Girl,*A.Position.ToCompactString(),S.OutingRoute.Num(),S.LedReplans);}}
 FVector Delta=S.OutingRoute[S.OutingStep]-S.FollowLead;Delta.Z=0;const float Distance=Delta.Size();
 if(Distance<20){++S.OutingStep;S.LedReplans=0;return 0;}
 const FVector Dir=Delta/Distance;
 // The follower must keep up before the leader goes on.
 if(FVector::Dist2D(S.Male,S.FollowLead)>350)return 0;
 S.FollowLead+=Dir*FMath::Min(Distance,Step);
 const FVector Side=FVector(-Dir.Y,Dir.X,0)*35.f;
 FCollisionQueryParams Q(SCENE_QUERY_STAT(StreetLedWalk),false,this);FHitResult Hit;
 auto Clear=[&](const FVector& From,const FVector& To){return !OKStreetBlockedPoint(To,28)&&!GetWorld()->SweepSingleByChannel(Hit,From+FVector(0,0,85),To+FVector(0,0,85),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(28,78),Q);};
 FVector GirlTo,MaleTo;
 if(S.FollowMode==2){GirlTo=S.FollowLead+Side;MaleTo=S.FollowLead-Side;
  // Doors, lifts and narrow paths: single file, she first, then abreast again.
  if(!Clear(A.Position,GirlTo)||!Clear(S.Male,MaleTo)){GirlTo=S.FollowLead;MaleTo=S.FollowLead-Dir*120.f;}}
 else{GirlTo=S.FollowLead;MaleTo=S.FollowLead-Dir*140.f;}
 if(Clear(A.Position,GirlTo)){const FVector Moved=GirlTo-A.Position;A.Position=GirlTo;if(!Moved.IsNearlyZero())S.Rotations[Girl]=OKStreetFacingMovement(Moved,S.Rotations[Girl]);S.FollowGirlWalk=Moved.Size2D()/FMath::Max(.001f,FMath::Min(Dt,.05f)*OKStreetWalkSpeed);}
 const FVector MaleDelta=MaleTo-S.Male;
 if(MaleDelta.Size2D()>1&&Clear(S.Male,S.Male+MaleDelta.GetClampedToMaxSize2D(OKStreetMaleWalkSpeed*FMath::Min(Dt,.05f)))){const FVector Moved=MaleDelta.GetClampedToMaxSize2D(OKStreetMaleWalkSpeed*FMath::Min(Dt,.05f));S.Male+=Moved;S.Rotations[0]=OKStreetFacingMovement(Moved,S.Rotations[0]);S.OutingProgressAt=SceneTime();return Moved.Size2D()/FMath::Max(.001f,FMath::Min(Dt,.05f)*OKStreetWalkSpeed);}
 return 0;
}

// Plan §13.1: every 2-10 real minutes each heroine weighs coming to him; one at a time, 60 s apart.
void AOKStreetSocialScene::TickApproach(){
 auto& S=*State;if(S.SelfTest)return;
 const double Now=FPlatformTime::Seconds();
 if(S.ApproachNext.Num()!=S.Manifest.Characters.Num()){S.ApproachNext.Init(0,S.Manifest.Characters.Num());for(int32 I=1;I<S.ApproachNext.Num();++I)S.ApproachNext[I]=Now+FMath::FRandRange(120.0,600.0);}
 if(S.ApproachGirl!=INDEX_NONE){StepApproach();return;}
 // Busy with someone or something: nobody comes over. Browsing the bookshop each on their own (a solo activity on a date
 // with her) is still that date: 狂死香 walked up to him while he was there with Arthuria.
 if(S.RemoteMode!=0||S.Target!=INDEX_NONE||S.FreeTalkBusy||S.OutingGirl!=INDEX_NONE||S.Pair.Partner!=INDEX_NONE||S.Facility!=INDEX_NONE||S.SoloGirl!=INDEX_NONE||S.SleepPhase!=0||S.PrivateGirl!=INDEX_NONE||S.RingGirl!=INDEX_NONE||Now-S.ApproachLast<60)return;
 // In game time, an hour between any two girls coming over: with the clock fast, 60 real seconds let one after another
 // come to him every half hour of his day (fin, 過子 and 狂死香 within ten minutes). A paused clock brings nobody.
 if(OKStreetMindNow(S)-S.ApproachLastMinute<60)return;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  if(Now<S.ApproachNext[I]||!S.Agents.IsValidIndex(I-1))continue;
  S.ApproachNext[I]=Now+FMath::FRandRange(120.0,600.0);
  if((S.Agents[I-1].Held&&!InitiativeFree(I))||S.Companion==I||!ContactReady(I))continue;
  if(const double* Parted=S.PartedAt.Find(I);Parted&&Now-*Parted<180.0)continue; // they just parted (a goodbye, or he sent her off): not straight back // her own reason lets her leave her plan (Life.inl)
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);const float Distance=FVector::Dist2D(S.Agents[I-1].Position,S.Male)/100.f;
  // Whether she comes over (invite.rs approach_score): her feelings, his appeal, distance, a recent no, today's intent, withdrawal.
  auto Request=OKRulesHeroineRequest(S,TEXT("approach_score"),Id);Request->SetNumberField(TEXT("look"),PlayerLook());Request->SetNumberField(TEXT("distance_m"),Distance);
  Request->SetBoolField(TEXT("rejected"),S.InviteRefusals.FindRef(Id)>0);Request->SetStringField(TEXT("intent"),S.Intents.FindRef(Id));Request->SetNumberField(TEXT("withdraw"),BiasOf(I).Withdraw);
  const TSharedPtr<FJsonObject> Approach=OKRulesEval(S,Request);if(!Approach.IsValid())continue;
  const float Score=float(Approach->GetNumberField(TEXT("score"))),Appeal=float(Approach->GetNumberField(TEXT("appeal")));
  UE_LOG(LogTemp,Display,TEXT("Street approach check: actor=%d score=%.0f appeal=%.0f distance=%.0fm"),I,Score,Appeal,Distance);
  if(!Approach->GetBoolField(TEXT("comes"))&&!S.InitiativeReason.Contains(I))continue;
  if(Distance>60){ReachOutByPhone(I);S.ApproachLast=Now;return;} // far: a message, or a lover calls (OKStreetSocialPhoneInbox.inl)
  auto Path=ContactRoute(S.Agents[I-1].Position,S.Male);if(Path.IsEmpty())continue;
  if(S.ScheduleHeld.Contains(I))ReleaseFromSchedule(I);
  S.ApproachLastMinute=OKStreetMindNow(S);
  S.ApproachGirl=I;S.ApproachBlockedNear=false;S.ApproachBlockedSince=0;S.ApproachPath=MoveTemp(Path);S.ApproachStarted=Now;S.ApproachLimit=90.0;S.ApproachLast=Now;S.Agents[I-1].Held=true;
  // Charmed enough: she comes to ask him out, and will lead the way (plan §12.3).
  S.ApproachInvite=Approach->GetBoolField(TEXT("invites"));
  S.FreeTalkNotice=OKMsg(TEXT("ui.invite.walks_toward_you"), {{TEXT("name"), *S.Manifest.Characters[I].Name}});RefreshUI();
  return;
 }
}

void AOKStreetSocialScene::StepApproach(){
 auto& S=*State;const int32 I=S.ApproachGirl;auto& A=S.Agents[I-1];
 auto Stop=[&](){A.Held=false;S.ApproachGirl=INDEX_NONE;S.ApproachPath.Reset();S.ApproachBlockedNear=false;};
 if(FPlatformTime::Seconds()-S.ApproachStarted>S.ApproachLimit||(S.Target!=INDEX_NONE&&S.Target!=I)){
  UE_LOG(LogTemp,Display,TEXT("Street approach: actor=%d gave up (%s) at %s, he is at %s, distance %.0f"),I,FPlatformTime::Seconds()-S.ApproachStarted>S.ApproachLimit?*FString::Printf(TEXT("%.0f s"),S.ApproachLimit):TEXT("he talks to someone else"),*A.Position.ToCompactString(),*S.Male.ToCompactString(),FVector::Dist2D(A.Position,S.Male));
  Stop();return;
 }
 // Arrived: beside him, or held up (a shelf, someone standing there) a couple of steps away; she stood at 176 cm for
 // 90 s and gave up.
 if((FVector::Dist2D(A.Position,S.Male)<170||(S.ApproachBlockedNear&&FVector::Dist2D(A.Position,S.Male)<260))&&S.RemoteMode==0){ // on the phone: she waits until he hangs up
  Stop();
  // Her own opening follows (InitiativeNudge): a scripted "oh, chatting time♡" line before it was a second opener, and
  // often not hers (fin is not bubbly).
  if(S.Target==INDEX_NONE){S.SkipGreeting=true;BeginTalk(I);S.SkipGreeting=false;}
  // She speaks first (a nudge: no player caption).
  S.SystemNote=true;S.FreeTalkDraft=InitiativeNudge(I,S.ApproachInvite); // what she came for (OKStreetSocialLife.inl)
  if(S.ApproachInvite)SetFollowMode(1);
  MindObserve(I,TEXT("approach"),S.ApproachInvite?OKT(TEXT("ui.invite.i_invite_him")):OKT(TEXT("ui.invite.i_approach_talk")),PlaceHere(S.Male),0.35f);
  SendFreeTalk();
  return;
 }
 if(SceneTime()-S.ApproachRouteAt>.75){
  auto Fresh=ContactRoute(A.Position,S.Male);
  if(Fresh.IsEmpty())Fresh={A.Position,S.Male}; // no planned route (indoors, other floor): straight to him, the body sweep below stops her at walls
  S.ApproachPath=MoveTemp(Fresh);S.ApproachRouteAt=SceneTime();
 }
 while(S.ApproachPath.Num()>1&&FVector::Dist2D(A.Position,S.ApproachPath[1])<12)S.ApproachPath.RemoveAt(0);
 if(S.ApproachPath.Num()<2)return;
 const FVector Delta=S.ApproachPath[1]-A.Position;const FVector Dir=Delta.GetSafeNormal2D();const float Length=FMath::Min(float(Delta.Size2D()),OKStreetWalkSpeed*FMath::Clamp(float(GetWorld()->GetDeltaSeconds()),0.f,.05f));
 // The same stepping as his own walking: slides along a wall and backs out of one she stands too close to.
 const FVector Next=StreetStep(A.Position,Dir,Length);
 if(Next==A.Position){
  if(FVector::Dist2D(A.Position,S.Male)<260)S.ApproachBlockedNear=true;
  // Held up for 8 s still well away from him (a fence or a bench the route runs into): on to the next point of her
  // route, so she arrives instead of standing there until she gives up.
  if(S.ApproachBlockedSince==0)S.ApproachBlockedSince=SceneTime();
  else if(SceneTime()-S.ApproachBlockedSince>8.0&&FVector::Dist2D(A.Position,S.Male)>600.f&&S.ApproachPath.Num()>=2){
   UE_LOG(LogTemp,Display,TEXT("Street approach: actor=%d held up at %s for 8 s; on to the next route point %s"),I,*A.Position.ToCompactString(),*S.ApproachPath[1].ToCompactString());
   A.Position=FVector(S.ApproachPath[1].X,S.ApproachPath[1].Y,A.Position.Z);S.ApproachPath.RemoveAt(0);S.ApproachBlockedSince=0;return;}
  // Held up inside a building he is not in (a hallway the route does not know): out of its door.
  {const FString In=PlaceHere(A.Position);if(!In.IsEmpty()&&In!=PlaceHere(S.Male))if(const auto* P=OKStreetFindPlace(In);P&&!P->Door.IsNearlyZero()&&FVector::Dist2D(A.Position,P->Door)>80.f){
   UE_LOG(LogTemp,Display,TEXT("Street approach: actor=%d held up inside %s; she comes out of its door"),I,P->Name);A.Position=FVector(P->Door.X,P->Door.Y,A.Position.Z);S.ApproachRouteAt=-1000;return;}}
  if(FPlatformTime::Seconds()>S.ApproachBlockLogAt){S.ApproachBlockLogAt=FPlatformTime::Seconds()+5.0;UE_LOG(LogTemp,Display,TEXT("Street approach: actor=%d blocked at %s going to %s (route points=%d)"),I,*A.Position.ToCompactString(),*S.ApproachPath[1].ToCompactString(),S.ApproachPath.Num());}
  return;
 }
 S.ApproachBlockedSince=0;
 A.Position=FVector(Next.X,Next.Y,A.Position.Z);S.Rotations[I]=OKStreetFacingMovement(Dir*Length,S.Rotations[I]);
}

// A second heroine joins the night (plan §12.5): she must be close, accept sleeping over, and not be jealous of
// the first; she takes another bed in the room with the single sleeping clip.
void AOKStreetSocialScene::AddSecondSleeper(int32 First,const FOKStreetPlace* Place,const FString& FirstBed){
 auto& S=*State;S.SleepGirl2=INDEX_NONE;if(!Place)return;
 const FString FirstId=OKStreetStableId(S.Manifest.Characters[First]);
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  if(I==First||!S.Agents.IsValidIndex(I-1)||FVector::Dist2D(S.Agents[I-1].Position,S.Male)>600||!ContactReady(I))continue;
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);
  const TArray<bool>* Stance=S.InviteStance.Find(Id);if(!Stance||!Stance->IsValidIndex(int32(EOKStreetInvite::Sleep))||!(*Stance)[int32(EOKStreetInvite::Sleep)])continue;
  const FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget),R1=OKStreetViewRelation(S.Store,FirstId,S.Manifest.TalkBudget);
  if(R.JealousyToward.FindRef(FirstId)>40||R1.JealousyToward.FindRef(Id)>40){S.FreeTalkNotice=OKMsg(TEXT("ui.invite.refuses_sleep_together"), {{TEXT("name"), *S.Manifest.Characters[I].Name}, {TEXT("first_name"), *S.Manifest.Characters[First].Name}});continue;}
  const FOKStreetModelAnchor* Bed=Place->Anchors.FindByPredicate([&](const FOKStreetModelAnchor& B){return B.Name.StartsWith(TEXT("bed"))&&B.Name!=FirstBed;});
  if(!Bed)continue;
  FString Error;if(!OKPrewarmCrowdRuntimeAction(this,I,TEXT("facility_sleep.okanim"),Error)){UE_LOG(LogTemp,Warning,TEXT("Street second sleeper unavailable: %s"),*Error);continue;}
  S.SleepGirl2=I;S.SleepGirl2Bed=Bed->Position;S.SleepGirl2Yaw=Bed->Yaw;S.Agents[I-1].Held=true;S.Agents[I-1].Position=Bed->Position;S.Rotations[I]=FRotator(0,Bed->Yaw,0);
  MindObserve(I,TEXT("sleep_together"),OKMsg(TEXT("ui.invite.overnight_with_label"), {{TEXT("companion"), *S.Manifest.Characters[First].Name}, {TEXT("place"), Place->Name}}),Place->Id);
  MindObserve(First,TEXT("sleep_together"),OKMsg(TEXT("ui.invite.also_sleep_together"), {{TEXT("name"), *S.Manifest.Characters[I].Name}}),Place->Id,0.6f);
  return;
 }
}

// Plan §12.4 level 4 (adults, both consenting): the rules decide whether she agrees (like a private-room invite,
// harder), only in a lockable place; elsewhere she leads him to one first. The act itself runs in the existing
// in-process high-detail one-on-one mode (paired playback), where either side can stop at any time.
void AOKStreetSocialScene::RequestIntimacy(int32 Character,bool bHerIdea){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Character)||Character==0)return;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 const FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
 // His idea: the same yes or no as every invitation (PanelConsent remembers a refusal and the pestering).
 if(!bHerIdea&&!AloneConsent(Character,TEXT("intimacy")))return;
 const FString Place=PlaceHere(S.Male);
 if(OKStreetNormsOf(Place).Privacy>=2){S.PendingIntimacy=Character;return;}
 // Not here: she leads the way to the nearest lockable room (plan §12.4 "換個地方").
 static const TArray<TPair<FString,FString>> Rooms={{TEXT("boys_dorm"),TEXT("go:boys_dorm")},{TEXT("lodge"),TEXT("rest")},{TEXT("minshuku"),TEXT("inn_rest")},{TEXT("hotel"),TEXT("hotel_rest")}};
 const FOKStreetPlace* Best=nullptr;FString Activity;float BestD=MAX_flt;
 for(const auto& Room:Rooms)if(const auto* P=OKStreetFindPlace(Room.Key)){FString Why;if(PlaceClosedNow(Room.Key,Why))continue;const float D=FVector::Dist2D(P->Door,S.Male);if(D<BestD){BestD=D;Best=P;Activity=Room.Value;}}
 if(!Best){S.FreeTalkNotice=OKT(TEXT("ui.invite.no_lockable_place"));RefreshUI();return;}
 S.MindRecent.Add(Id,OKMsg(TEXT("ui.invite.move_elsewhere_prompt"), {{TEXT("place"), Best->Name}}));
 FString Error;SetFollowMode(1);
 if(BeginOuting(Character,Activity,Error)){S.PendingIntimacy=Character;S.FreeTalkNotice=OKMsg(TEXT("ui.invite.she_pulls_toward"), {{TEXT("place"), Best->Name}});}
 else S.FreeTalkNotice=OKT(TEXT("ui.invite.move_failed_prefix"))+Error;
 RefreshUI();
}

// Arrived in a lockable room: hand over to the high-detail one-on-one; when it returns, she remembers it.
void AOKStreetSocialScene::TickIntimacy(){
 auto& S=*State;
 if(S.IntimacyGirl!=INDEX_NONE&&S.PrivateGirl==INDEX_NONE){
  const int32 Girl=S.IntimacyGirl;S.IntimacyGirl=INDEX_NONE;
  // What it leaves is a rule (shop.rs intimacy_after).
  auto Request=OKRulesHeroineRequest(S,TEXT("intimacy_after"),OKStreetStableId(S.Manifest.Characters[Girl]));Request->SetStringField(TEXT("place"),PlaceHere(S.Male));
  if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request)){auto Candidate=S.Store;ApplyRulesEffects(Candidate,*Answer);if(CommitStore(MoveTemp(Candidate)))FlushRulesEffects();}
  return;
 }
 if(S.PendingIntimacy==INDEX_NONE||S.OutingGirl!=INDEX_NONE)return;
 if(OKStreetNormsOf(PlaceHere(S.Male)).Privacy<2)return;
 const int32 Girl=S.PendingIntimacy;S.PendingIntimacy=INDEX_NONE;
 if(S.Facility!=INDEX_NONE)FinishFacility();
 if(S.Pair.Partner!=INDEX_NONE)FinishPhoto();
 BeginPrivate(Girl);
 if(S.PrivateGirl==Girl)S.IntimacyGirl=Girl;else{S.FreeTalkNotice=OKT(TEXT("ui.invite.private_interaction_unavailable"));RefreshUI();}
}
