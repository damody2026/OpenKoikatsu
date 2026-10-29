// Included by OKStreetSocialScene.cpp: being with several heroines at once ("follow me": any number of them may walk with
// him; talking to one of them makes the others join that conversation). The conversation partner stays the one the
// talk began with (FreeTalkPartner); the others are the party (State->Party). They stand around him facing him, follow him
// when he walks or goes somewhere with the partner, and a line he says gets one DeepSeek reply in which each of them answers
// in her own voice. Activities they join are played with talking poses for now (the partner keeps the paired clips).

// Who may join now: within calling distance on his floor (12 m), not following already, not in class, at work or asleep.
// The girl he is talking to may follow too: she keeps following after the talk ends (until she says goodbye or he lets her go).
bool AOKStreetSocialScene::PartyCanJoin(int32 Girl,FString* Why) const{
 const auto& S=*State;auto No=[&](const TCHAR* W){if(Why)*Why=W;return false;};
 if(!S.Agents.IsValidIndex(Girl-1)||!S.Manifest.Characters.IsValidIndex(Girl))return No(OKT(TEXT("ui.party.no_such_person")));
 // Already with him: the one he is talking to (she was listed among "the girls nearby" and invited to join her own talk).
 if(S.Party.Contains(Girl)||FreeTalkPartner()==Girl)return No(OKT(TEXT("ui.party.girl_already_following")));
 if(S.Party.Num()>=4)return No(OKT(TEXT("ui.party.too_many_people")));
 const auto& A=S.Agents[Girl-1];
 if(FMath::Abs(A.Position.Z-S.Male.Z)>150.f||FVector::Dist2D(A.Position,S.Male)>1200.f)return No(OKT(TEXT("ui.party.girl_not_nearby"))); // within calling distance: she walks over
 if(S.PrivateGirl==Girl||S.SleepGirl==Girl||S.SleepGirl2==Girl||S.RestroomGirl==Girl)return No(OKT(TEXT("ui.party.girl_busy_now")));
 if(const FOKStreetPlanBlock* B=LifeBlock(Girl,int32(OKStreetMindNow(S)%1440));B&&(B->Kind==TEXT("class")||B->Kind==TEXT("work")||B->Kind==TEXT("sleep")))
  return No(B->Kind==TEXT("class")?OKT(TEXT("ui.party.girl_in_class")):B->Kind==TEXT("work")?OKT(TEXT("ui.party.girl_working")):OKT(TEXT("ui.party.girl_sleeping")));
 // Someone in the group she dislikes: she will not come (his girlfriend comes anyway, reluctantly: PartyJoin). Asked of
 // the rules last, and only when someone is with him: the panel asks this for every heroine on each rebuild.
 {TArray<int32> Group=S.Party;if(FreeTalkPartner()!=INDEX_NONE)Group.AddUnique(FreeTalkPartner());int32 Who=INDEX_NONE;
  if(!Group.IsEmpty()&&BondMood(Girl,Group,&Who)==2){if(Why)*Why=OKMsg(TEXT("ui.party.girl_dislikes_no_join"), {{TEXT("who_name"), *S.Manifest.Characters[Who].Name}});return false;}}
 return true;
}

bool AOKStreetSocialScene::PartyJoin(int32 Girl){
 auto& S=*State;FString Why;
 if(!PartyCanJoin(Girl,&Why)){S.FreeTalkNotice=OKMsg(TEXT("ui.bank.heroine_reply_line"), {{TEXT("heroine_name"), *S.Manifest.Characters[Girl].Name}, {TEXT("reply"), *Why}});RefreshUI();return false;}
 if(S.SoloGirl==Girl)EndSolo();
 S.Party.Add(Girl);S.Agents[Girl-1].Held=true;S.ScheduleHeld.Add(Girl);S.Commutes.Remove(Girl);
 {TArray<int32> Group=S.Party;if(FreeTalkPartner()!=INDEX_NONE)Group.AddUnique(FreeTalkPartner());int32 Who=INDEX_NONE;
  if(BondMood(Girl,Group,&Who)==1){S.PartyEvents.Add(OKMsg(TEXT("ui.party.reluctant_join_note"), {{TEXT("girl_name"), *S.Manifest.Characters[Girl].Name}, {TEXT("who_name"), *S.Manifest.Characters[Who].Name}}));
   MindObserve(Girl,TEXT("party"),OKMsg(TEXT("ui.party.reluctant_follow"), {{TEXT("who_name"), *S.Manifest.Characters[Who].Name}}),PlaceHere(S.Male),0.4f);}
  else S.PartyEvents.Add(OKMsg(TEXT("ui.party.girl_joined_you"), {{TEXT("girl_name"), *SpokenName(Girl)}}));
  // The ones already with him who dislike her: they will not stay together (his girlfriend stays, reluctantly).
  for(const int32 M:Group){if(M==Girl)continue;int32 Disliked=INDEX_NONE;const int32 Mood=BondMood(M,{Girl},&Disliked);
   if(Mood==2){UE_LOG(LogTemp,Display,TEXT("Street party: actor %d dislikes %d and wants to leave"),M,Girl);PartyStartLeaving(M,false);S.PartyEvents.Add(OKMsg(TEXT("ui.party.clash_wants_to_leave"), {{TEXT("name"), *S.Manifest.Characters[M].Name}, {TEXT("other"), *S.Manifest.Characters[Girl].Name}}));}
   else if(Mood==1)S.PartyEvents.Add(OKMsg(TEXT("ui.party.reluctant_stay_note"), {{TEXT("grudge_name"), *S.Manifest.Characters[M].Name}, {TEXT("target_name"), *S.Manifest.Characters[Girl].Name}}));}}
 // Joining him where he sits with someone (Yuuka at the cafe table for their study evening) is not following him.
 const bool JoinsTalk=FreeTalkPartner()!=INDEX_NONE&&FreeTalkPartner()!=Girl;
 S.FreeTalkNotice=OKMsg(JoinsTalk?TEXT("ui.party.joins_you_line"):TEXT("ui.party.follows_you_line"), {{TEXT("girl_name"), *S.Manifest.Characters[Girl].Name}});S.Message=S.FreeTalkNotice;
 MindObserve(Girl,TEXT("party"),FreeTalkPartner()!=INDEX_NONE&&FreeTalkPartner()!=Girl?OKMsg(TEXT("ui.party.joined_talk_with"), {{TEXT("partner_name"), *S.Manifest.Characters[FreeTalkPartner()].Name}}):FString(OKT(TEXT("ui.party.he_asked_me_follow"))),PlaceHere(S.Male),0.3f);
 UE_LOG(LogTemp,Display,TEXT("Street party: actor %d joins (party %d)"),Girl,S.Party.Num());
 RefreshUI();return true;
}

void AOKStreetSocialScene::PartyLeave(int32 Girl,const FString& Why){
 auto& S=*State;if(!S.Party.Contains(Girl))return;
 S.Party.Remove(Girl);S.PartyLeaving.Remove(Girl);for(auto It=S.PartyMoodChecked.CreateIterator();It;++It)if(It->StartsWith(FString::Printf(TEXT("%d|"),Girl)))It.RemoveCurrent();S.PartedAt.Add(Girl,FPlatformTime::Seconds());
 if(S.Agents.IsValidIndex(Girl-1)){S.Agents[Girl-1].Held=false;S.Agents[Girl-1].Mode=EOKStreetMove::Walking;}
 S.ScheduleHeld.Remove(Girl);
 S.PartyEvents.Add(OKMsg(TEXT("ui.party.girl_left_first"), {{TEXT("girl_name"), *S.Manifest.Characters[Girl].Name}}));
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_PartyLeaveMind);MindRemember(Girl,TEXT("party"),OKMsg(TEXT("ui.memory.left_group"),{{TEXT("place"),([&](const FString& Id){const FOKStreetPlace* P=OKStreetFindPlace(Id);return P?FString(P->Name):Id.IsEmpty()?FString(OKT(TEXT("ui.etiquette.street_location"))):Id;})(PlaceHere(S.Male))}}),PlaceHere(S.Male),0.25f);}
 UE_LOG(LogTemp,Display,TEXT("Street party: actor %d leaves (%s)"),Girl,*Why);{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_PartyLeaveUi);RefreshUI();}
}

// He says goodbye to one of them: she is leaving, and for 10 s anyone may keep her (PartyStartLeaving). The one he is
// talking to goes too, and if anyone else is still with him the talk carries on with them; alone with her it is the usual goodbye.
void AOKStreetSocialScene::PartyFarewell(int32 Girl){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl))return;
 const FString Name=S.Manifest.Characters[Girl].Name;
 const bool Others=S.Party.ContainsByPredicate([Girl](int32 X){return X!=Girl;});
 MindObserve(Girl,TEXT("farewell"),Others?FString(OKT(TEXT("ui.party.he_farewell_public"))):FString(OKT(TEXT("ui.party.he_said_goodbye"))),PlaceHere(S.Male),0.2f);
 UE_LOG(LogTemp,Display,TEXT("Street party: he says goodbye to actor %d (others stay=%d)"),Girl,Others?1:0);
 if(Girl==FreeTalkPartner()&&!Others){PartyLeave(Girl,TEXT("he said goodbye"));FreeTalkLeave(OKMsg(TEXT("ui.party.you_said_goodbye"), {{TEXT("name"), *Name}}));return;}
 PartyStartLeaving(Girl,true);
}

// She is leaving: 10 s in which he (a button or his words) or the others (in their lines) may keep her; her answer comes in the
// same reply ("stay"): she stays, or refuses and goes at once. AskOthers: he said goodbye, so the group answers now (she says
// goodbye, the others may ask her to stay) in one call; when she herself decided to go, that reply already happened.
void AOKStreetSocialScene::PartyStartLeaving(int32 Girl,bool AskOthers){
 auto& S=*State;if(S.PartyLeaving.Contains(Girl)||!S.Manifest.Characters.IsValidIndex(Girl))return;
 const FString Name=S.Manifest.Characters[Girl].Name;
 S.PartyLeaving.Add(Girl,FPlatformTime::Seconds()+10.0);
 S.Message=OKMsg(TEXT("ui.party.leaving_retain_window"), {{TEXT("name"), *Name}});
 UE_LOG(LogTemp,Display,TEXT("Street party: actor %d is leaving (10 s to keep her, ask=%d)"),Girl,AskOthers?1:0);
 if(AskOthers&&FreeTalkPartner()!=INDEX_NONE)S.TellHer.Add(OKMsg(TEXT("ui.party.farewell_hint_prompt"), {{TEXT("farewell_name"), *Name}, {TEXT("leaving_name"), *Name}, {TEXT("bye_name"), *Name}, {TEXT("decider_name"), *Name}}));
 RefreshUI();
}

// He asks her to stay. Talking with someone: his words go to the group and her answer comes in "stay". Nobody to talk to:
// he speaks to her alone and her own reply decides (a goodbye in it and she goes; otherwise she stays with him).
void AOKStreetSocialScene::PartyRetain(int32 Girl){
 auto& S=*State;if(!S.PartyLeaving.Contains(Girl)||!S.Manifest.Characters.IsValidIndex(Girl))return;
 const FString Name=S.Manifest.Characters[Girl].Name;
 UE_LOG(LogTemp,Display,TEXT("Street party: he asks actor %d to stay"),Girl);
 if(FreeTalkPartner()!=INDEX_NONE){
  if(S.FreeTalkBusy){S.FreeTalkNotice=OKT(TEXT("ui.party.girls_talking_wait"));RefreshUI();return;}
  S.FreeTalkDraft=OKMsg(TEXT("ui.party.please_stay_plea"), {{TEXT("name"), *Name}});SendFreeTalk();return;
 }
 S.PartyLeaving.Remove(Girl);S.QuietTalk=true;
 if(!BeginTalk(Girl)){S.QuietTalk=false;PartyLeave(Girl,TEXT("could not keep her"));return;}
 S.FreeTalkOpen=true;FreeTalkLiveSet(true);
 S.SystemNote=true;S.FreeTalkDraft=OKT(TEXT("ui.party.retained_decide_prompt"));SendFreeTalk();
}

// The girl he was talking to leaves the group: her talk (and walk or activity with him) ends, she walks off, and the next
// one with him becomes the one he talks to; the conversation stays open.
void AOKStreetSocialScene::PartyHandOver(int32 From){
 auto& S=*State;
 int32 Next=INDEX_NONE;for(const int32 G:S.Party)if(G!=From&&S.Agents.IsValidIndex(G-1)){Next=G;break;}
 const FString Name=S.Manifest.Characters.IsValidIndex(From)?S.Manifest.Characters[From].Name:FString();
 PartyLeave(From,TEXT("goodbye, the others stay"));
 if(Next==INDEX_NONE){FreeTalkLeave(OKMsg(TEXT("ui.party.you_said_goodbye"), {{TEXT("name"), *Name}}));return;}
 const bool Live=S.FreeTalkOpen;
 if(S.Pair.Partner==From)FinishPhoto();
 if(S.Facility!=INDEX_NONE&&S.FacilityCharacter==From)FinishFacility();
 if(S.OutingGirl==From)CancelOuting(FString());
 if(S.Companion==From)FinishCompanion();
 if(S.Target==From)EndTalk();
 if(S.Agents.IsValidIndex(From-1)){auto& A=S.Agents[From-1];A.Held=false;A.Mode=EOKStreetMove::Walking;A.Stuck=0;}
 S.QuietTalk=true;
 if(!BeginTalk(Next)){S.QuietTalk=false;UE_LOG(LogTemp,Display,TEXT("Street party: hand-over to %d failed"),Next);S.Message=OKMsg(TEXT("ui.party.left_first"), {{TEXT("name"), *Name}});RefreshUI();return;}
 if(Live){S.FreeTalkOpen=true;FreeTalkLiveSet(true);}
 S.PartyEvents.Add(OKMsg(TEXT("ui.party.left_only_you"), {{TEXT("name"), *Name}}));
 S.Message=OKMsg(TEXT("ui.party.leaves_continue_with_next"), {{TEXT("name"), *Name}, {TEXT("next_name"), *S.Manifest.Characters[Next].Name}});
 UE_LOG(LogTemp,Display,TEXT("Street party: actor %d left, the talk goes on with %d"),From,Next);RefreshUI();
}

// Where each member stands: around him, on the side away from the partner's spot, 1.4-1.7 m away, facing him.
// While he walks she keeps up (running when far behind) and is put beside him after 6 s without getting closer.
void AOKStreetSocialScene::TickParty(float Dt){
 auto& S=*State;
 {TArray<int32> Gone;for(const auto& KV:S.PartyLeaving)if(FPlatformTime::Seconds()>=KV.Value)Gone.Add(KV.Key);
  for(const int32 G:Gone){S.PartyLeaving.Remove(G);const FString Name=S.Manifest.Characters.IsValidIndex(G)?S.Manifest.Characters[G].Name:FString();
   if(G==FreeTalkPartner())PartyHandOver(G);
   else{PartyLeave(G,TEXT("nobody kept her"));S.PartyEvents.Add(OKMsg(TEXT("ui.party.has_left"), {{TEXT("name"), *Name}}));S.Message=OKMsg(TEXT("ui.party.name_left_notice"), {{TEXT("name"), *Name}});}}}
 TickPartySeats();
 // His pace this frame, for the walking line to keep up with (cm/s).
 {const float Moved=FVector::Dist2D(S.Male,S.PartyLastMale);S.PartyLastMale=S.Male;S.PartyHisSpeed=Dt>0.f&&Moved<500.f?Moved/FMath::Max(Dt,0.001f):0.f;}
 if(S.Party.IsEmpty())return;
 const int32 Partner=FreeTalkPartner();if(Partner==INDEX_NONE)S.PartyLog.Reset();
 // He talks to someone a member dislikes: she will not stay (his girlfriend stays, reluctantly). Once for each pair.
 if(Partner!=INDEX_NONE)for(const int32 M:TArray<int32>(S.Party)){if(M==Partner||S.PartyLeaving.Contains(M))continue;const FString Key=FString::Printf(TEXT("%d|%d"),M,Partner);if(S.PartyMoodChecked.Contains(Key))continue;S.PartyMoodChecked.Add(Key);
  int32 Who=INDEX_NONE;const int32 Mood=BondMood(M,{Partner},&Who);const FString NM=S.Manifest.Characters[M].Name,NP=S.Manifest.Characters[Partner].Name;
  if(Mood==2){UE_LOG(LogTemp,Display,TEXT("Street party: actor %d dislikes the partner %d and wants to leave"),M,Partner);S.PartyEvents.Add(OKMsg(TEXT("ui.party.clash_wants_to_leave"), {{TEXT("name"), *NM}, {TEXT("other"), *NP}}));PartyStartLeaving(M,false);}
  else if(Mood==1){UE_LOG(LogTemp,Display,TEXT("Street party: actor %d dislikes the partner %d, stays for him"),M,Partner);S.PartyEvents.Add(OKMsg(TEXT("ui.party.dislikes_but_stays"), {{TEXT("name"), *NM}, {TEXT("other"), *NP}}));S.Message=OKMsg(TEXT("ui.party.unhappy_but_follows"), {{TEXT("name"), *NM}});}}
 // Talking: around him, away from the partner's side. Walking (nobody to talk to, or walking with the one he talks to):
 // in the group's formation, by his facing (places.rs party_formation: girlfriends at his sides, friends ahead and
 // behind). The one he walks with joins it while he leads; when she leads or they walk side by side the outing walks her.
 const bool Walking=Partner==INDEX_NONE||((S.Companion==Partner||S.OutingGirl==Partner)&&S.Pair.Partner==INDEX_NONE);
 const bool PartnerInLine=Walking&&Partner!=INDEX_NONE&&S.Companion==Partner&&S.FollowMode==0&&S.Agents.IsValidIndex(Partner-1);
 S.GroupWalkPartner=PartnerInLine?Partner:INDEX_NONE;
 auto StageOf=[&](int32 G){return OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[G]),S.Manifest.TalkBudget).Stage;};
 TArray<int32> Members;if(PartnerInLine)Members.Add(Partner);for(const int32 G:S.Party)if(G!=Partner)Members.Add(G);
 if(Walking){FString Key;for(const int32 G:Members)Key+=FString::Printf(TEXT("%d%s,"),G,*StageOf(G));
  if(Key!=S.PartySlotsKey){S.PartySlotsKey=Key;S.PartySlots.Reset();auto Ask=OKRulesRequest(TEXT("party_formation"));TArray<TSharedPtr<FJsonValue>> Rows;
   for(const int32 G:Members){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("actor"),G);O->SetBoolField(TEXT("lover"),StageOf(G)==TEXT("lover"));Rows.Add(MakeShared<FJsonValueObject>(O));}
   Ask->SetArrayField(TEXT("members"),Rows);
   if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Ask))for(const auto& V:Out->GetArrayField(TEXT("slots"))){const auto O=V->AsObject();S.PartySlots.Add(int32(O->GetNumberField(TEXT("actor"))),FVector2D(O->GetNumberField(TEXT("angle")),O->GetNumberField(TEXT("dist"))));}
   UE_LOG(LogTemp,Display,TEXT("Street party: walking formation %s"),*Key);}}
 // His heading: a character's yaw is its heading less 90 (OKStreetFacingMovement); taken as is, his sides were ahead of
 // and behind him and the two girlfriends walked one in front, one behind (R3-252).
 FVector Toward=!Walking&&Partner!=INDEX_NONE&&S.Agents.IsValidIndex(Partner-1)?(S.Agents[Partner-1].Position-S.Male):FRotator(0,S.Rotations[0].Yaw+90.f,0).Vector();Toward.Z=0;if(!Toward.Normalize())Toward=FVector::ForwardVector;
 static const float TalkAngles[]={-70.f,70.f,180.f,-125.f},WalkAngles[]={150.f,-150.f,180.f,125.f};
 int32 Slot=0;
 for(int32 K=0;K<Members.Num();++K){
  const int32 G=Members[K];if(!S.Agents.IsValidIndex(G-1))continue;auto& A=S.Agents[G-1];const bool IsPartner=G==Partner;
  const FVector2D* Formation=Walking?S.PartySlots.Find(G):nullptr;
  const float Angle=Formation?float(Formation->X):(Partner!=INDEX_NONE?TalkAngles:WalkAngles)[(Slot++)%4];const float Reach=Formation?float(Formation->Y):155.f;
  if(FMath::Abs(A.Position.Z-S.Male.Z)>150.f&&S.NpcLifts.Contains(G))continue; // riding a lift
  if(S.PartySeats.ContainsByPredicate([G](const FOKLifePose& P){return P.Actor==G&&P.Phase==1;})){A.Held=true;continue;} // sitting on her chair
  if(!IsPartner&&FVector::Dist2D(A.Position,S.Male)>2500.f){PartyLeave(G,TEXT("too far behind"));Members.RemoveAt(K--);continue;}
  FVector Spot=S.Male+Toward.RotateAngleAxis(Angle,FVector::UpVector)*Reach;Spot.Z=S.Male.Z;
  // A spot inside something (a counter, a wall): closer to him.
  if(OKStreetObstacleGap(Spot,25.f)<25.f)Spot=S.Male+Toward.RotateAngleAxis(Angle,FVector::UpVector)*FMath::Min(Reach,90.f);
  FVector Delta=Spot-A.Position;Delta.Z=0;const float Dist=Delta.Size();
  double& Ref=S.PartyGapRef.FindOrAdd(G);double& RefAt=S.PartyGapAt.FindOrAdd(G);
  // In the walking line she keeps her place closely and at his pace (the friend ahead of him lagged into his way).
  if(Dist>(Formation?25.f:60.f)){
   // In the line: his own pace plus a pull toward her place (at a fixed walking pace she trailed his run on a navigation
   // by a metre, the girlfriends behind his shoulders instead of beside him).
   const float Step=FMath::Clamp(Dt,0.f,.05f);
   const float Speed=Formation?FMath::Min(S.PartyHisSpeed*1.05f+Dist*3.f,OKStreetMaleRunSpeed*1.5f)*Step
    :(Dist>400.f?OKStreetMaleRunSpeed:OKStreetWalkSpeed)*Step;
   const FVector Next=StreetStep(A.Position,Delta/Dist,FMath::Min(Dist,Speed));const float Moved=FVector::Dist2D(Next,A.Position);
   if(Next!=A.Position){A.Position=FVector(Next.X,Next.Y,S.Male.Z);S.Rotations[G]=OKStreetFacingMovement(Delta,S.Rotations[G]);}
   // The one he walks with: her pace for her walk clip (StepCompanion hands her to the formation).
   if(IsPartner)S.GroupWalkPartnerSpeed=Moved/(FMath::Clamp(Dt,0.001f,.05f)*OKStreetWalkSpeed);
   const double Now=SceneTime();
   // Keeping pace beside him or in the line is not being stuck: she is put at her spot only when she falls behind it.
   if(Dist<Ref-30.0||RefAt==0||(Formation&&Dist<150.f)){Ref=Dist;RefAt=Now;}
   // Stuck behind a corner (she walks straight to her place): put at it, sooner when far behind in the line (the friend
   // fell 25 m behind a running navigation and left the group); a place inside a wall gives a free one around him.
   else if(Now-RefAt>(Formation?(Dist>400.f?1.5:4.0):6.0)){
    FVector To=Spot;for(int32 Turn=0;Turn<12&&OKStreetBlockedPoint(To,28.f);++Turn)To=S.Male+Toward.RotateAngleAxis(Angle+Turn*30.f,FVector::UpVector)*FMath::Min(Reach,130.f);
    To.Z=S.Male.Z;A.Position=To;Ref=0;RefAt=Now;UE_LOG(LogTemp,Display,TEXT("Street party: actor %d stood beside him (no progress, %.0f behind her place)"),G,Dist);}
  }else{
   Ref=0;RefAt=0;if(IsPartner)S.GroupWalkPartnerSpeed=0.f;
   if(Formation)S.Rotations[G]=FRotator(0,S.Rotations[0].Yaw,0); // in the walking line: the way he faces
   else if(Partner!=INDEX_NONE)S.Rotations[G]=OKStreetFacingMovement(S.Male-A.Position,S.Rotations[G]); // talking: she looks at him
  }
  A.Held=true;
 }
}

// The two of them sit down (a meal, drinks at a table): the others with them sit too, each at her spot beside him on a
// chair of her own (SpawnLifeProps), in the action slots the pair leaves free; up again when the pair gets up or she
// leaves the group. (She stood beside the table the whole time eating her tiramisu while the other two sat.)
void AOKStreetSocialScene::TickPartySeats(){
 auto& S=*State;const int32 Partner=FreeTalkPartner();const double Now=SceneTime();
 const bool Sitting=Partner!=INDEX_NONE&&S.Pair.Partner==Partner&&S.Pair.Phase==1&&(S.Pair.Meal||S.Pair.Seated);
 // Her seat is the partner's (the meal's eating clip brings the table); at a meal she eats while something of hers is
 // being eaten (TickMeal's MealAteAt), else sits idle, as the two of them do.
 const FString Clip=Sitting?S.Pair.FemaleClip:FString();
 for(int32 K=S.PartySeats.Num()-1;K>=0;--K){auto& P=S.PartySeats[K];
  if(!Sitting||P.Clip!=Clip||!S.Party.Contains(P.Actor)||P.Actor==Partner||S.PartyLeaving.Contains(P.Actor)){StopPartySeat(K);continue;}
  if(P.Phase!=0){
   if(!S.Pair.Meal||S.SeatedFemaleClip.IsEmpty())continue;
   const double* Ate=S.MealAteAt.Find(P.Actor);const FString Want=Ate&&FPlatformTime::Seconds()-*Ate<3.0?P.Clip:S.SeatedFemaleClip;
   FString& Playing=S.PartySeatNow.FindOrAdd(P.Actor,P.Clip);
   if(Want!=Playing){FString Error;if(OKPlayCrowdRuntimeAction(this,P.Actor,Want,Error))Playing=Want;}
   continue;}
  if(Now-P.Requested>60){S.PartySeatRetry.Add(P.Actor,Now+30);StopPartySeat(K);continue;}
  FOKRuntimeActionStreamStatus A;
  if(OKGetCrowdRuntimeActionStatus(this,P.Actor,A)&&A.Ready&&!A.Pending&&!A.Preparing&&A.ClipId==P.Clip){FString Error;
   if(OKPlayCrowdRuntimeAction(this,P.Actor,P.Clip,Error)){P.Phase=1;SpawnLifeProps(P,true);UE_LOG(LogTemp,Display,TEXT("Street party: actor %d sits with them (%s)"),P.Actor,*P.Clip);}
   else{S.PartySeatRetry.Add(P.Actor,Now+30);StopPartySeat(K);}}
 }
 if(!Sitting||!OKLifeSeatedClip(Clip))return;
 const int32 Free=FMath::Max(0,OKCrowdRuntimeActionSlots-2); // the two of them hold two
 for(const int32 G:S.Party){
  if(S.PartySeats.Num()>=Free)break;
  if(G==Partner||S.PartyLeaving.Contains(G)||!S.Agents.IsValidIndex(G-1)||S.PartySeatRetry.FindRef(G)>Now||S.PartySeats.ContainsByPredicate([G](const FOKLifePose& P){return P.Actor==G;}))continue;
  if(FVector::Dist2D(S.Agents[G-1].Position,S.Male)>260.f)continue; // at her spot beside him first (TickParty walks her there)
  // A heroine's own pose nearby gives its slot up (TickLife counts the seats too).
  StopLifePose(G);while(S.LifePoses.Num()>FMath::Max(0,Free-S.PartySeats.Num()-1))StopLifePose(S.LifePoses.Last().Actor);
  FString Error;
  if(OKPrewarmCrowdRuntimeAction(this,G,Clip,Error)){FOKLifePose P;P.Actor=G;P.Clip=Clip;P.Requested=Now;S.PartySeats.Add(P);}
  else{S.PartySeatRetry.Add(G,Now+30);UE_LOG(LogTemp,Warning,TEXT("Street party: actor %d cannot sit (%s): %s"),G,*Clip,*Error);}
 }
}
void AOKStreetSocialScene::StopPartySeat(int32 Index){
 auto& S=*State;if(!S.PartySeats.IsValidIndex(Index))return;auto& P=S.PartySeats[Index];
 FString Error;OKResumeCrowdRuntimeBehavior(this,P.Actor,Error);
 for(auto& Prop:P.Props)DestroyLater(Prop.Get());
 S.PartySeatNow.Remove(P.Actor);UE_LOG(LogTemp,Display,TEXT("Street party: actor %d gets up"),P.Actor);S.PartySeats.RemoveAt(Index);
}

// One line of his, the whole party answers: one DeepSeek call writes the lines (who says what), favor for each, who
// leaves, and an activity they all start now.
// The name a girl goes by in talk: her persona's display name (日代千鶴) when there is one, else the card's name.
// The group prompt uses only this one; a card name in romaji next to "千鹤" from him made her ask who he meant.
// The model's JSON with stray full-width punctuation outside the strings ("why": "……吐槽"。}) parsed as nothing and the
// group "did not answer": outside a string such a character is never JSON, so it is dropped.
static FString OKStreetJsonDropStrayPunct(const FString& In){
 FString Out;Out.Reserve(In.Len());bool InString=false,Escaped=false;
 for(const TCHAR C:In){
  if(InString){Out.AppendChar(C);if(Escaped)Escaped=false;else if(C==TEXT('\\'))Escaped=true;else if(C==TEXT('"'))InString=false;continue;}
  if(C==TEXT('"')){InString=true;Out.AppendChar(C);continue;}
  if(C>0x2000&&C!=0xFEFF&&!FChar::IsWhitespace(C))continue; // 。，、… and the like between tokens
  Out.AppendChar(C);
 }
 return Out;
}

// He is at this place: inside it, or by its door (a shrine or a park is mostly its grounds by the door).
bool AOKStreetSocialScene::AtPlace(const FString& Id) const{
 const auto& S=*State;const FOKStreetPlace* P=OKStreetFindPlace(Id);if(!P)return false;
 return OKStreetInsidePlace(*P,S.Male)||(!P->Door.IsNearlyZero()&&FVector::Dist2D(P->Door,S.Male)<600.f);
}

FString AOKStreetSocialScene::SpokenName(int32 G) const{
 const auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(G))return FString();
 // In the game's script: a persona written in Simplified ("贞德") was 【贞德】 in a Traditional game's group prompt, and
 // the model answered as 「貞德」 (two names for one girl in what she and the others remember).
 FOKFreeTalkPersona P;FString E;if(OKStreetFreeTalkLoadPersona(S.Manifest.Characters[G].Name,P,E)&&!P.DisplayName.IsEmpty())return OKStreetScriptedName(P.DisplayName);
 return OKStreetScriptedName(S.Manifest.Characters[G].Name);
}

void AOKStreetSocialScene::SendPartyTalk(const FString& Text){
 auto& S=*State;const int32 Partner=FreeTalkPartner();if(Partner==INDEX_NONE||S.FreeTalkBusy)return;
 FOKFreeTalkSettings Settings;FString Error;if(!OKStreetFreeTalkLoadSettings(Settings,Error)){S.FreeTalkNotice=Error;RefreshUI();return;}
 TArray<int32> Members{Partner};for(const int32 G:S.Party)if(G!=Partner)Members.Add(G);
 const FString Him=S.Manifest.Characters.IsValidIndex(0)?S.Manifest.Characters[0].Name:FString(OKT(TEXT("ui.party.he_pronoun")));
 FString System=OKT(TEXT("ui.party.traditional_chinese_only_rule"));
 System+=OKMsg(TEXT("ui.party.group_dialogue_prompt"), {{TEXT("him"), *Him}});
 FString States;
 for(const int32 G:Members){
  FOKFreeTalkPersona P;FString E;OKStreetFreeTalkLoadPersona(S.Manifest.Characters[G].Name,P,E);
  const FString Id=OKStreetStableId(S.Manifest.Characters[G]);const FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
  const TCHAR* Stage=R.Stage==TEXT("lover")?OKT(TEXT("ui.party.his_girlfriend_label")):R.Stage==TEXT("friend")?OKT(TEXT("ui.bonds.friend_stage")):OKT(TEXT("ui.party.just_met"));
  const TCHAR* Hunger=R.Satiety<35.f?OKT(TEXT("ui.party.very_hungry")):R.Satiety<65.f?OKT(TEXT("ui.party.somewhat_hungry")):R.Satiety<80.f?OKT(TEXT("ui.party.not_very_hungry")):R.Satiety<OKStreetFullSatiety?OKT(TEXT("ui.party.rather_full")):OKT(TEXT("ui.party.very_full"));
  // Named by her spoken name; the card's name (romaji) is mentioned once so a line that uses it still reaches her.
  const FString Also=!P.DisplayName.IsEmpty()&&P.DisplayName!=S.Manifest.Characters[G].Name?OKMsg(TEXT("ui.party.card_name_hint"), {{TEXT("card_name"), *S.Manifest.Characters[G].Name}}):FString();
  // Who she is stays in the system text (the same every turn: DeepSeek's cache keeps it); how she is now goes to the
  // turn's own message below.
  System+=OKMsg(TEXT("ui.party.girl_profile_static"), {{TEXT("name"), *SpokenName(G)}, {TEXT("also"), *Also}, {TEXT("profile"), *P.Profile.Left(500)}});
  States+=OKMsg(TEXT("ui.party.girl_state_line"), {{TEXT("name"), *SpokenName(G)}, {TEXT("stage"), Stage}, {TEXT("favor"), R.Favor}, {TEXT("hunger"), Hunger}});
  // The meetings she already has with him ("lunch at the cafeteria at half past twelve"), as the one-on-one judge is
  // told: "we agreed to eat together" otherwise went to the restaurant.
  {FString Booked;for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Heroine==Id)Booked+=OKMsg(TEXT("ui.scene.schedule_entry"), {{TEXT("when"), *OKStreetCalendarLabel(int32(A.Start/1440),double(A.Start%1440)/60.0)}, {TEXT("label"), *A.Label}});
   if(!Booked.IsEmpty())States+=OKMsg(TEXT("ui.party.booked_with_him"), {{TEXT("name"), *SpokenName(G)}, {TEXT("meetings"), *Booked}});}
 }
 {const FString Bonds=BondText(Members);if(!Bonds.IsEmpty())States+=OKT(TEXT("ui.party.girls_relations_label"))+Bonds+TEXT("\n");}
 // What is on the table now (whose, how much is left): a dish that was never ordered is not "eaten up" in their lines.
 if(!S.Table.IsEmpty())States+=OKT(TEXT("ui.party.table_now_label"))+DishTableText()+TEXT("\n");
 // Who pays, once settled ("my treat" a turn ago): so nobody asks again whether it is his treat.
 {const FString How=PayAgreed(FreeTalkPartner());if(How==TEXT("him")||How==TEXT("split")||How==TEXT("her"))States+=OKMsg(TEXT("ui.party.pay_agreed_line"),{{TEXT("how"),How==TEXT("him")?OKT(TEXT("ui.party.pay_agreed_him")):How==TEXT("split")?OKT(TEXT("ui.party.pay_agreed_split")):OKT(TEXT("ui.party.pay_agreed_her"))}});}
 System+=OKT(TEXT("ui.party.ai_party_prompt"));
 // Who pays, every turn (not only at the menu): "my treat" said on the way there is the deal at the place (RecordPayAgreed).
 System+=OKT(TEXT("ui.party.payer_format_instruction"));
 for(const auto& A:OKStreetActivities())System+=OKMsg(TEXT("ui.party.menu_id_name_entry"), {{TEXT("id"), A.Id}, {TEXT("name"), A.Name}});
 System+=OKT(TEXT("ui.party.place_id_label"));for(const auto& P:OKStreetPlaces())System+=OKMsg(TEXT("ui.party.menu_id_name_entry"), {{TEXT("id"), P.Id}, {TEXT("name"), P.Name}});
 // In a shop: the menu, and what each of them orders now (he pays; no extra call).
 FString Menu;{const FString Shop=ShopPlaceHere();FString Closed;if(!Shop.IsEmpty()&&!PlaceClosedNow(Shop,Closed))for(const auto* I:ItemsHere(Shop))if(I->Consumable)Menu+=OKMsg(TEXT("ui.party.menu_item_price_entry"), {{TEXT("item_id"), *I->Id}, {TEXT("item_name"), *I->Name}, {TEXT("price"), I->PriceOf(I->DefaultSize())}});}
 if(!Menu.IsEmpty())System+=OKT(TEXT("ui.party.menu_listing_header"))+Menu+OKT(TEXT("ui.party.order_format_instruction"));
 const FDateTime Now=StreetNow();
 FString User=States+OKMsg(TEXT("ui.party.now_status_line"), {{TEXT("hour"), Now.GetHour()}, {TEXT("minute"), Now.GetMinute()}, {TEXT("place_name"), [&]{const FOKStreetPlace* P=OKStreetFindPlace(PlaceHere(S.Male));return P?FString(P->Name):FString(OKT(TEXT("ui.etiquette.street_location")));}()}, {TEXT("doing"), *FreeTalkDoing()}});
 if(!S.PartyLog.IsEmpty())User+=OKT(TEXT("ui.party.previous_dialogue_header"))+FString::Join(S.PartyLog,TEXT("\n"))+TEXT("\n");
 for(const FString& Ev:S.PartyEvents)User+=Ev+TEXT("\n");
 for(const auto& KV:S.PartyLeaving)if(S.Manifest.Characters.IsValidIndex(KV.Key))User+=OKMsg(TEXT("ui.party.about_to_leave_seconds"), {{TEXT("name"), *SpokenName(KV.Key)}, {TEXT("seconds_left"), FMath::Max(0,int32(KV.Value-FPlatformTime::Seconds()))}});
 S.PartyEvents.Reset();
 // A nudge or a notice (his silence, an order served) is the situation, not his words.
 const bool Nudge=OKStreetFreeTalkIsNudge(Text);
 if(Nudge)User+=Text;
 else{
  User+=OKMsg(TEXT("ui.party.speaker_says_line"), {{TEXT("him"), *Him}, {TEXT("text"), *Text}});
 }
 S.FreeTalkCaptionHer=TEXT("……");
 S.FreeTalkBusy=true;++S.FreeTalkSent;S.FreeTalkDraft.Reset();S.FreeTalkNotice=OKT(TEXT("ui.party.girls_thinking"));RefreshUI();
 TArray<TSharedPtr<FJsonValue>> Messages;
 auto AddMessage=[&Messages](const TCHAR* Who,const FString& Content){TSharedRef<FJsonObject> O=MakeShared<FJsonObject>();O->SetStringField(TEXT("role"),Who);O->SetStringField(TEXT("content"),Content);Messages.Add(MakeShared<FJsonValueObject>(O));};
 AddMessage(TEXT("system"),System);AddMessage(TEXT("user"),User);
 TWeakPtr<FOKStreetSceneState> WeakState=State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 // Through the heroine mind (ok-mind): it calls the model.
 SubmitMindChat(TEXT("party"),TEXT("dialogue"),Messages,600,1.1f,[WeakState,Weak,Text,Members](bool Ok,const FString& Content,const FString& Failure){
  const auto L=WeakState.Pin();if(!L||!Weak.IsValid()||Weak->State!=L)return;
  L->FreeTalkBusy=false;
  TSharedPtr<FJsonObject> Reply;if(Ok&&!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content),Reply)){Reply.Reset();FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OKStreetJsonDropStrayPunct(Content)),Reply);if(Reply.IsValid())UE_LOG(LogTemp,Display,TEXT("Street party talk: the reply parsed after dropping stray punctuation"));}
  if(!Reply.IsValid()){UE_LOG(LogTemp,Warning,TEXT("Street party talk failed: %s"),*Failure);L->FreeTalkNotice=OKT(TEXT("ui.party.group_no_reply_retry"));Weak->RefreshUI();return;}
  L->FreeTalkAnswered=L->FreeTalkSent; // the open choices were in that request (TickChoices)
  Weak->FinishPartyTalk(Text,Members,Reply);
 });
}

void AOKStreetSocialScene::FinishPartyTalk(const FString& Text,const TArray<int32>& Members,const TSharedPtr<FJsonObject>& Reply){
 auto& S=*State;
 // The exact name first ("贞德" is Jeanne, not "贞德·Alter" that also holds it), then the looser match.
 auto Find=[&](const FString& Name)->int32{for(const int32 G:Members)if(S.Manifest.Characters.IsValidIndex(G)&&(Name.Equals(SpokenName(G),ESearchCase::IgnoreCase)||Name.Equals(S.Manifest.Characters[G].Name,ESearchCase::IgnoreCase)))return G;
  for(const int32 G:Members)if(S.Manifest.Characters.IsValidIndex(G)&&OKStreetNameMatches(Name,SpokenName(G)))return G;for(const int32 G:Members)if(S.Manifest.Characters.IsValidIndex(G)&&OKStreetNameMatches(Name,S.Manifest.Characters[G].Name))return G;return INDEX_NONE;};
 TArray<int32> Speakers;TArray<FString> Lines,Caption;TMap<int32,TArray<FString>> Said;
 const TArray<TSharedPtr<FJsonValue>>* Arr=nullptr;
 // Their lines in the order the model wrote them: who answers, and who keeps quiet, is its call from the situation.
 TArray<TPair<int32,FString>> Ordered;
 if(Reply->TryGetArrayField(TEXT("lines"),Arr))for(const auto& V:*Arr){
  const TSharedPtr<FJsonObject> O=V.IsValid()?V->AsObject():nullptr;if(!O)continue;FString Who,Line;O->TryGetStringField(TEXT("who"),Who);O->TryGetStringField(TEXT("text"),Line);
  const int32 G=Find(Who);
  if(G==INDEX_NONE&&!Line.IsEmpty())UE_LOG(LogTemp,Warning,TEXT("Street party talk: line of \"%s\" dropped (no girl here by that name): %s"),*Who,*Line);
  if(G==INDEX_NONE||Line.IsEmpty())continue;
  Ordered.Add({G,Line});
 }
 for(const auto& [G,Line]:Ordered){
  Speakers.Add(G);Lines.Add(Line);Said.FindOrAdd(G).Add(Line);Caption.Add(OKStreetScriptedName(S.Manifest.Characters[G].Name)+OKT(TEXT("ui.economy.colon_mark"))+Line);
  FreeTalkAddLine(G,false,Line);S.PartyLog.Add(SpokenName(G)+OKT(TEXT("ui.economy.colon_mark"))+Line);
 }
 while(S.PartyLog.Num()>14)S.PartyLog.RemoveAt(0);
 S.FreeTalkCaptionHer=FString::Join(Caption,TEXT("\n"));
 // Each one keeps the exchange in her own memory, so a later one-on-one talk remembers it.
 for(const int32 G:Members){
  FOKFreeTalkReply Mine;if(const auto* Her=Said.Find(G)){Mine.Lines=*Her;Mine.Reply=FString::Join(*Her,TEXT(""));}else Mine.Reply=OKT(TEXT("ui.party.no_speech"));
  auto& Memory=S.FreeTalkMemory.FindOrAdd(OKStreetStableId(S.Manifest.Characters[G]));
  Memory.Add({TEXT("user"),OKT(TEXT("ui.party.in_group_note"))+Text});Memory.Add({TEXT("assistant"),OKStreetFreeTalkMemoryReply(Mine)});OKStreetFreeTalkTrimMemory(Memory);
 }
 FreeTalkSaveMemory();
 // Her one-on-one talks go through the mind's own context (SubmitFreeTalkToMind), not the memory above: the exchange goes
 // there too, or the apology he made in front of both of them in the morning was never heard ("I'm still angry about
 // yesterday" at lunch).
 {const FString Sep=OKT(TEXT("ui.party.group_note_separator"));TArray<FString> Heard;for(const auto& [G,Line]:Ordered)Heard.Add(SpokenName(G)+OKT(TEXT("ui.economy.colon_mark"))+Line);
  if(!Heard.IsEmpty()){const FString Exchange=FString::Join(Heard,*Sep);
   const FString Note=OKStreetFreeTalkIsNudge(Text)?OKMsg(TEXT("ui.party.group_talk_note"),{{TEXT("lines"),*Exchange}}):OKMsg(TEXT("ui.party.group_talk_note_his"),{{TEXT("his"),*Text},{TEXT("lines"),*Exchange}});
   for(const int32 G:Members)MindNote(G,Note);}}
 // Favor for each, then who leaves, then what they all do now.
 const TSharedPtr<FJsonObject>* Favor=nullptr;FString Notes;
 if(Reply->TryGetObjectField(TEXT("favor"),Favor))for(const auto& KV:(*Favor)->Values){const int32 G=Find(FString(KV.Key));double D=0;if(G!=INDEX_NONE&&KV.Value.IsValid()&&KV.Value->TryGetNumber(D)&&FMath::RoundToInt(D)!=0){ApplyFreeTalkFavor(G,FMath::Clamp(FMath::RoundToInt(D),-3,3),FString());Notes+=OKMsg(TEXT("ui.party.favor_delta_entry"), {{TEXT("name"), *S.Manifest.Characters[G].Name}, {TEXT("delta"), FMath::RoundToInt(D)}});}}
 S.FreeTalkNotice=Notes.IsEmpty()?FString(OKT(TEXT("ui.party.favor_unchanged"))):OKT(TEXT("ui.party.favor_label"))+Notes;
 // Each girl's face this turn ("faces": {name: {expr, blush, tears, sweat, gaze}}), as in a talk of two.
 const TSharedPtr<FJsonObject>* Faces=nullptr;
 if(Reply->TryGetObjectField(TEXT("faces"),Faces))for(const auto& KV:(*Faces)->Values){const int32 G=Find(FString(KV.Key));const TSharedPtr<FJsonObject>* Face=nullptr;
  if(G!=INDEX_NONE&&KV.Value.IsValid()&&KV.Value->TryGetObject(Face)&&Face->IsValid()){FOKFreeTalkReply Hers;OKStreetFreeTalkParseFace(**Face,Hers);FString Emotion;(*Face)->TryGetStringField(TEXT("emotion"),Emotion);ApplyMindExpression(G,Emotion,&Hers);}}
 if(S.TtsCacheDir.Len()&&!Lines.IsEmpty())SpeakPartyLines(Speakers,Lines);
 const int32 Partner=FreeTalkPartner();
 if(Reply->TryGetArrayField(TEXT("leave"),Arr))for(const auto& V:*Arr){FString Name;if(!V.IsValid()||!V->TryGetString(Name))continue;const int32 G=Find(Name);
  if(G!=INDEX_NONE&&S.PartyLeaving.Contains(G))continue; // already leaving
  if(G==Partner&&S.Party.ContainsByPredicate([G](int32 X){return X!=G;}))PartyStartLeaving(G,false);
  else if(G==Partner){S.FarewellPending=true;S.FarewellFromTalk=false;S.FarewellAt=FPlatformTime::Seconds();UE_LOG(LogTemp,Display,TEXT("Street party: the partner %d leaves, the talk ends"),G);}
  else if(G!=INDEX_NONE)PartyStartLeaving(G,false);}
 // Kept: her answer, at once. Staying ends the leaving; refusing, she goes right after her line.
 {const TSharedPtr<FJsonObject>* Stay=nullptr;
  if(Reply->TryGetObjectField(TEXT("stay"),Stay))for(const auto& KV:(*Stay)->Values){const int32 G=Find(FString(KV.Key));bool Keep=false;
   if(G==INDEX_NONE||!S.PartyLeaving.Contains(G)||!KV.Value.IsValid()||!KV.Value->TryGetBool(Keep))continue;
   if(Keep){S.PartyLeaving.Remove(G);S.Message=OKMsg(TEXT("ui.party.left_behind"), {{TEXT("girl_name"), *S.Manifest.Characters[G].Name}});MindObserve(G,TEXT("party"),OKT(TEXT("ui.party.stay_after_retain")),PlaceHere(S.Male),0.3f);}
   else{S.PartyLeaving[G]=FPlatformTime::Seconds()+3.0;S.Message=OKMsg(TEXT("ui.party.refused_to_stay"), {{TEXT("girl_name"), *S.Manifest.Characters[G].Name}});}
   UE_LOG(LogTemp,Display,TEXT("Street party: actor %d kept -> %s"),G,Keep?TEXT("stays"):TEXT("refuses, goes now"));}}
 // He told them himself about another girl (told_about, the AI's reading): every girl here heard it from him.
 {TArray<FString> Told;FString One;const TArray<TSharedPtr<FJsonValue>>* List=nullptr;if(Reply->TryGetArrayField(TEXT("told_about"),List))for(const auto& V:*List){FString N;if(V.IsValid()&&V->TryGetString(N)&&!N.IsEmpty())Told.Add(N);}else if(Reply->TryGetStringField(TEXT("told_about"),One)&&!One.IsEmpty())Told.Add(One);for(const FString& N:Told){if(Partner!=INDEX_NONE)ToldAboutRival(Partner,N);for(const int32 G:S.Party)if(G!=Partner)ToldAboutRival(G,N);}}
 // A meeting later agreed on in front of everyone ("dinner at half past five, all of us"): each who said yes has it with
 // him, booked as in a talk of two (AddPromiseFromChat, promise.rs). The group reply had no such field, and at half past
 // five nobody came.
 {const TSharedPtr<FJsonObject>* Promise=nullptr;
  if(!OKStreetFreeTalkIsNudge(Text)&&Reply->TryGetObjectField(TEXT("promise"),Promise)&&Promise&&Promise->IsValid()){
   FString Day,Time,Place,What;double Hours=0;(*Promise)->TryGetStringField(TEXT("day"),Day);(*Promise)->TryGetStringField(TEXT("time"),Time);
   (*Promise)->TryGetStringField(TEXT("place"),Place);(*Promise)->TryGetStringField(TEXT("what"),What);(*Promise)->TryGetNumberField(TEXT("hours"),Hours);
   TArray<int32> Agreed;const TArray<TSharedPtr<FJsonValue>>* Who=nullptr;
   if((*Promise)->TryGetArrayField(TEXT("who"),Who))for(const auto& V:*Who){FString Name;const int32 G=V.IsValid()&&V->TryGetString(Name)?Find(Name):INDEX_NONE;if(G!=INDEX_NONE)Agreed.AddUnique(G);}
   UE_LOG(LogTemp,Display,TEXT("Street party promise: day \"%s\" time \"%s\" at %s (%s), %d agreed"),*Day,*Time,*Place,*What,Agreed.Num());
   if(!Place.IsEmpty()||!Day.IsEmpty())for(const int32 G:Agreed){const TArray<FString>* Hers=Said.Find(G);
    AddPromiseFromChat(G,Day,Time,Place,What,Text+TEXT("\n")+(Hers?FString::Join(*Hers,TEXT("")):FString()),Text,false,float(Hours));}}}
 FString DoNow,Why;Reply->TryGetStringField(TEXT("do_now"),DoNow);Reply->TryGetStringField(TEXT("why"),Why);
 UE_LOG(LogTemp,Display,TEXT("Street party talk: %d lines, do_now=\"%s\"; %s"),Lines.Num(),*DoNow,*Why);
 // Something to do now starts from what he said: a turn that was only the situation (his silence, the end of a game)
 // cannot start one by itself ("let's play again!" with no word from him dealt a new game).
 if(!DoNow.IsEmpty()&&OKStreetFreeTalkIsNudge(Text)){UE_LOG(LogTemp,Display,TEXT("Street party: do_now %s ignored, he did not say anything this turn"),*DoNow);DoNow.Reset();}
 // Who pays, as they settled it ("today it's my treat" to the table): every girl's bill below follows it (PayAgreed).
 {FString Payer;if(Partner!=INDEX_NONE&&Reply->TryGetStringField(TEXT("payer"),Payer)&&!Payer.IsEmpty())RecordPayAgreed(Partner,Payer,DoNow);}
 // Orders: each girl's to her own plate and bill (whatever he was putting in the basket stays there).
 {const FString Shop=ShopPlaceHere();
  if(!Shop.IsEmpty()&&Partner!=INDEX_NONE&&Reply->TryGetArrayField(TEXT("orders"),Arr)&&!Arr->IsEmpty()){
   const TArray<FOKStreetCartLine> Kept=S.Cart;S.Cart.Reset();
   for(const auto& V:*Arr){const TSharedPtr<FJsonObject> O=V.IsValid()?V->AsObject():nullptr;if(!O)continue;FString Who,ItemId;O->TryGetStringField(TEXT("who"),Who);O->TryGetStringField(TEXT("id"),ItemId);
    const FOKStreetCatalogItem* Item=S.Catalog.Find(ItemId);
    if(!Item)for(const auto* I:ItemsHere(Shop))if(I->Name==ItemId||ItemId.Contains(I->Name)){Item=I;break;} // by name
    if(!Item||!SoldHere(*Item,Shop)||!Item->Consumable)continue;ItemId=Item->Id;
    {const int32 For=Find(Who);const int32 Eater=For==INDEX_NONE?0:For;
     if(HasDishLike(Eater,*Item)){UE_LOG(LogTemp,Display,TEXT("Street party order skipped: %s, %s still has one"),*Item->Name,Eater==0?TEXT("he"):*S.Manifest.Characters[Eater].Name);continue;}
     // Food for a girl already full is not ordered (as one-on-one, TooFullFor): she would leave it untouched.
     if(Eater!=0&&TooFullFor(Eater,*Item)){S.PartyEvents.Add(OKMsg(TEXT("ui.party.too_full_to_order"),{{TEXT("girl_name"),*S.Manifest.Characters[Eater].Name},{TEXT("item"),Item->Name}}));
      UE_LOG(LogTemp,Display,TEXT("Street party order skipped: %s, %s is full"),*Item->Name,*S.Manifest.Characters[Eater].Name);continue;}}
    FOKStreetCartLine Line;Line.ItemId=ItemId;Line.Size=Item->DefaultSize();if(!Item->Options.IsEmpty()){Line.Sweetness=OKT(TEXT("ui.mind.full_sugar_label"));Line.Ice=OKT(TEXT("ui.mind.normal_state_label"));}
    const int32 G=Find(Who);if(G==Partner)Line.ForHer=true;else if(G!=INDEX_NONE)Line.Girl=G;
    S.Cart.Add(Line);UE_LOG(LogTemp,Display,TEXT("Street party order: %s for %s"),*Item->Name,G==INDEX_NONE?TEXT("him"):*S.Manifest.Characters[G].Name);}
   if(!S.Cart.IsEmpty()){S.ShopPlace=Shop;const int32 Told=S.TellHer.Num();SettleGroupOrder(Partner);
    // The till line is for a one-on-one meal; the group sees the dishes come.
    while(S.TellHer.Num()>Told)S.TellHer.RemoveAt(S.TellHer.Num()-1);}
   S.Cart.Append(Kept);
  }}
 // At the card table the game ends only by the table, not by a do_now.
 if(S.Bluff.Active&&!DoNow.IsEmpty()){UE_LOG(LogTemp,Display,TEXT("Street party: do_now %s ignored, they are playing cards"),*DoNow);}
 else if(DoNow==TEXT("card_bluff")){
  // Agreed on outside ("let's go in and play cards"): to a seat inside a place first, the game deals there (ArriveOuting).
  FString CannotStart;const FString Seat=BluffCanStart(&CannotStart)?FString():CardTableOuting(Text);
  if(Seat.IsEmpty())StartBluff();
  else{S.BluffOnArrival=Seat;UE_LOG(LogTemp,Display,TEXT("Street bluff: agreed outdoors, to %s first"),*Seat);ExecuteFreeTalkAction(Partner,OKStreetStableId(S.Manifest.Characters[Partner]),Seat);}
 }
 else if(!DoNow.IsEmpty()&&Partner!=INDEX_NONE){
  // The one he talks to says goodbye in this same reply ("go ahead, I'll come after my shift") while the others agree to
  // go: they go without her. Run on her, the plan was refused by her shift and the one left heard only "she is working"
  // ("then I'll eat alone"). Hand over now and the one staying does it.
  int32 Doer=Partner;
  if(DoNow!=TEXT("leave")&&S.PartyLeaving.Contains(Partner)&&S.Party.ContainsByPredicate([&S,Partner](int32 X){return X!=Partner&&!S.PartyLeaving.Contains(X);})){
   S.PartyLeaving.Remove(Partner);PartyHandOver(Partner);Doer=FreeTalkPartner();
   UE_LOG(LogTemp,Display,TEXT("Street party: actor %d leaves in the reply that agreed on %s; %d does it"),Partner,*DoNow,Doer);}
  if(S.Manifest.Characters.IsValidIndex(Doer))ExecuteFreeTalkAction(Doer,OKStreetStableId(S.Manifest.Characters[Doer]),DoNow);
 }
 RefreshUI();
}

// Their lines in order, each in her own voice (the free-talk speech queue with a speaker per line).
void AOKStreetSocialScene::SpeakPartyLines(const TArray<int32>& Speakers,const TArray<FString>& Lines){
 auto& S=*State;if(Speakers.IsEmpty())return;
 SpeakFreeTalk(Speakers[0],Lines,++S.FreeTalkSerial);
 S.FreeTalkSpeechSpeakers=Speakers;
}
