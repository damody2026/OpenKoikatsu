// Included by OKStreetSocialScene.cpp: plan §13.3 appointments, the task list under the minimap, arriving, being
// late, standing someone up, and the shop blacklist.

int32 AOKStreetSocialScene::AddAppointment(const FString& Kind,int32 Heroine,const FString& Place,int64 Start,int32 Grace,const FString& Label){
 auto& S=*State;auto Candidate=S.Store;
 // The same appointment again (a plan rebuilt after a reload, a repeated request): the one already there.
 if(const FOKStreetAppointment* Same=Candidate.Appointments.FindByPredicate([&](const FOKStreetAppointment& X){return X.Status==TEXT("pending")&&X.Kind==Kind&&X.Start==Start&&X.Place==Place&&X.Heroine==(S.Manifest.Characters.IsValidIndex(Heroine)&&Heroine>0?OKStreetStableId(S.Manifest.Characters[Heroine]):FString());}))return Same->Id;
 FOKStreetAppointment A;A.Id=Candidate.NextAppointment++;A.Kind=Kind;A.Place=Place;A.Start=Start;A.Grace=Grace;A.Label=Label;
 if(S.Manifest.Characters.IsValidIndex(Heroine)&&Heroine>0)A.Heroine=OKStreetStableId(S.Manifest.Characters[Heroine]);
 Candidate.Appointments.Add(A);
 if(!CommitStore(MoveTemp(Candidate)))return 0;
 UE_LOG(LogTemp,Display,TEXT("Street appointment %d: %s %s at %s minute %lld"),A.Id,*Kind,*Label,*Place,A.Start);
 if(Heroine>0)PlanPromisesToday(Heroine);
 RefreshUI();return A.Id;
}

// Today's plan for her holds today's meetings with him as they are now (made, moved or called off after the day was
// planned): plan.rs plan_set_promises.
void AOKStreetSocialScene::PlanPromisesToday(int32 Heroine){
 auto& S=*State;TArray<FOKStreetPlanBlock>* Day=S.Plans.Find(Heroine);if(!Day||!S.Manifest.Characters.IsValidIndex(Heroine))return;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Heroine]);
 auto Request=OKRulesRequest(TEXT("plan_set_promises"));TArray<TSharedPtr<FJsonValue>> Blocks,Promises;
 for(const auto& B:*Day)Blocks.Add(MakeShared<FJsonValueObject>(OKRulesBlockJson(B)));
 for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Heroine==Id&&A.Kind==TEXT("date")&&A.Start/1440==S.PlanDay){auto P=MakeShared<FJsonObject>();P->SetNumberField(TEXT("start"),double(A.Start%1440));P->SetStringField(TEXT("place"),A.Place);Promises.Add(MakeShared<FJsonValueObject>(P));}
 Request->SetArrayField(TEXT("blocks"),Blocks);Request->SetArrayField(TEXT("promises"),Promises);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 TArray<FOKStreetPlanBlock> New;for(const auto& V:Answer->GetArrayField(TEXT("blocks")))if(V->AsObject())New.Add(OKRulesReadBlock(*V->AsObject()));
 // Nothing changed (a meeting on another day): her day goes on as it is.
 auto Same=[](const FOKStreetPlanBlock& X,const FOKStreetPlanBlock& Y){return X.Start==Y.Start&&X.End==Y.End&&X.Kind==Y.Kind&&X.Place==Y.Place&&X.Activity==Y.Activity;};
 bool Changed=New.Num()!=Day->Num();for(int32 K=0;!Changed&&K<New.Num();++K)Changed=!Same(New[K],(*Day)[K]);
 if(!Changed)return;
 // Her current block is looked up again, as a change (-9, like ReleaseFromSchedule): she walks to the next place.
 // Not INDEX_NONE, which means the game just started and puts her there directly (she vanished next to him).
 *Day=MoveTemp(New);if(S.PlanBlock.Contains(Heroine))S.PlanBlock.Add(Heroine,-9);
 UE_LOG(LogTemp,Display,TEXT("Street plan: actor %d today's meetings with him: %d"),Heroine,Promises.Num());
}

int32 AOKStreetSocialScene::CharacterOf(const FString& Id) const{
 for(int32 I=1;I<State->Manifest.Characters.Num();++I)if(OKStreetStableId(State->Manifest.Characters[I])==Id)return I;
 return INDEX_NONE;
}

// How long she waits for him (social.rs patience).
int32 AOKStreetSocialScene::PatienceOf(int32 Character) const{
 auto Request=OKRulesRequest(TEXT("appointment_patience"));if(const auto P=ProfileOf(Character))Request->SetObjectField(TEXT("profile"),P.ToSharedRef());
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(*State,Request);return Answer?int32(Answer->GetNumberField(TEXT("minutes"))):20;
}

// From chat (plan §13.3 "new_promise"): {"day": "+1" | "2010-09-05", "time": "19:00", "place": id, "what": text}.
void AOKStreetSocialScene::AddPromiseFromChat(int32 Character,const FString& Day,const FString& Time,const FString& Place,const FString& WhatSaid,const FString& Words,const FString& His,bool bReplaces){
 // An activity id in "what" ("meal") is said by its name in the label and her memory ("約meal" on the list).
 const FString What=OKStreetFindActivity(WhatSaid)?OKStreetActivityName(WhatSaid):WhatSaid;
 auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);const FOKStreetPlace* P=OKStreetFindPlace(Place);
 // Whether it is a meeting at all, a new one or the same one said again, and where and when, is promise.rs
 // (promise_from_chat); the judge's reading of both lines gives day, time and place.
 auto Request=OKRulesRequest(TEXT("promise_from_chat"));Request->SetStringField(TEXT("heroine"),Id);Request->SetStringField(TEXT("day"),Day);
 Request->SetStringField(TEXT("time"),Time);Request->SetStringField(TEXT("place"),Place);Request->SetBoolField(TEXT("place_known"),P!=nullptr);Request->SetBoolField(TEXT("replaces"),bReplaces);
 Request->SetNumberField(TEXT("now"),double(OKStreetMindNow(S)));Request->SetNumberField(TEXT("today"),S.Store.CalendarDay);
 {const double Wall=FPlatformTime::Seconds();TArray<TSharedPtr<FJsonValue>> Pending;
  for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("id"),A.Id);O->SetStringField(TEXT("heroine"),A.Heroine);
   O->SetStringField(TEXT("kind"),A.Kind);O->SetStringField(TEXT("place"),A.Place);O->SetNumberField(TEXT("start"),double(A.Start));O->SetStringField(TEXT("label"),A.Label);
   const double* Booked=S.PromiseBookedAt.Find(A.Id);O->SetNumberField(TEXT("booked_ago"),Booked?Wall-*Booked:-1.0);Pending.Add(MakeShared<FJsonValueObject>(O));}
  Request->SetArrayField(TEXT("pending"),Pending);}
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return;
 const FString Action=Out->GetStringField(TEXT("action")),Reason=Out->GetStringField(TEXT("reason"));
 const int64 Start=int64(Out->GetNumberField(TEXT("start")));const int32 Minute=int32(Out->GetNumberField(TEXT("minute")));
 if(Action==TEXT("ignore")){UE_LOG(LogTemp,Display,TEXT("Street promise ignored (%s): day \"%s\" time \"%s\" at %s"),*Reason,*Day,*Time,*Place);return;}
 if(Action==TEXT("keep")||Action==TEXT("move")){const int32 AppId=int32(Out->GetNumberField(TEXT("id")));
  const FOKStreetAppointment* A=S.Store.Appointments.FindByPredicate([&](const FOKStreetAppointment& X){return X.Id==AppId;});if(!A)return;
  if(Action==TEXT("keep")){UE_LOG(LogTemp,Display,TEXT("Street appointment: %s at minute %lld kept, said again (%s)"),*A->Label,A->Start,*Reason);return;}
  const FString Label=A->Label;const int64 From=A->Start;const FString FromPlace=A->Place;auto Candidate=S.Store;
  for(auto& C:Candidate.Appointments)if(C.Id==AppId){
   // A label that carries the time ("…接她（17:00）") shows the new one; another place, the label says it.
   C.Label.ReplaceInline(*FString::Printf(TEXT("%02d:%02d"),int32(C.Start%1440)/60,int32(C.Start%1440)%60),*FString::Printf(TEXT("%02d:%02d"),int32(Start%1440)/60,int32(Start%1440)%60));
   C.Start=Start;
   if(Out->GetBoolField(TEXT("place_changed"))&&P){C.Place=Place;C.Label=OKMsg(TEXT("ui.appointments.meet_with_summary"), {{TEXT("character_name"), *S.Manifest.Characters[Character].Name}, {TEXT("what"), What.IsEmpty()?OKT(TEXT("ui.appointments.meeting_label")):*What}, {TEXT("person_name"), P->Name}});}}
  if(CommitStore(MoveTemp(Candidate))){PlanPromisesToday(Character);UE_LOG(LogTemp,Display,TEXT("Street appointment: %s moved from minute %lld at %s to %lld at %s (%s)"),*Label,From,*FromPlace,Start,*Place,*Reason);RefreshUI();}
  return;}
 if(!P)return;
 // Closed then (breakfast at 8:00 at a shop opening at 10:00): not booked; she hears why, so the two pick another time.
 if(Action==TEXT("closed")){const FString Why=Out->GetStringField(TEXT("why"));
  S.TellHer.Add(OKMsg(TEXT("ui.appointments.closed_then_note"), {{TEXT("place_name"), P->Name}, {TEXT("why"), *Why}}));S.FreeTalkNotice=Why;
  UE_LOG(LogTemp,Display,TEXT("Street appointment: %s at minute %lld not booked, closed then (%s)"),*Place,Start,*Why);RefreshUI();return;}
 const FString Label=OKMsg(TEXT("ui.appointments.meet_with_summary"), {{TEXT("character_name"), *S.Manifest.Characters[Character].Name}, {TEXT("what"), What.IsEmpty()?OKT(TEXT("ui.appointments.meeting_label")):*What}, {TEXT("person_name"), P->Name}});
 // The meeting this one takes the place of goes quietly (promise.rs replaces_id).
 {const int32 Old=int32(Out->GetNumberField(TEXT("replaces_id")));if(Old>0)MovePromiseWith(Old);}
 AddAppointment(TEXT("date"),Character,Place,Start,PatienceOf(Character),Label);
 // Another girl is already booked near that time: he is told (both stay booked; it is his to sort out).
 {const FString Clash=Out->GetStringField(TEXT("clash"));if(!Clash.IsEmpty()){S.FreeTalkNotice=OKMsg(TEXT("ui.appointments.clash_notice"), {{TEXT("label"), *Clash}});UE_LOG(LogTemp,Display,TEXT("Street appointment: clashes with %s"),*Clash);RefreshUI();}}
 if(S.Store.Appointments.Num())S.PromiseBookedAt.Add(S.Store.Appointments.Last().Id,FPlatformTime::Seconds());
 HeroineNote(Character,TEXT("promise"),OKMsg(TEXT("ui.appointments.promised_meet_with_him"), {{TEXT("label"), *OKStreetCalendarLabel(int32(Start/1440)-(Minute<240?1:0),Minute/60.0)}, {TEXT("place_name"), P->Name}, {TEXT("what"), What.IsEmpty()?OKT(TEXT("ui.appointments.meeting_label")):*What}}),Place,0.7f);
}

// Cancelling early costs little; within two hours it counts as half a no-show (social.rs appointment_cancel).
// A meeting just booked becomes a weekly one (series.rs series_add): its label says so ("…（每週三）").
void AOKStreetSocialScene::StartSeries(int32 Character,const FString& Place,const TSharedPtr<FJsonValue>& Repeat){
 auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 const FOKStreetAppointment* First=nullptr;for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Heroine==Id&&A.Place==Place&&A.Kind==TEXT("date")&&(!First||A.Id>First->Id))First=&A;
 if(!First)return;
 auto Request=OKRulesRequest(TEXT("series_add"));OKRulesPutExt(S,Request);Request->SetStringField(TEXT("heroine"),Id);Request->SetNumberField(TEXT("start"),double(First->Start));
 Request->SetStringField(TEXT("place"),Place);Request->SetStringField(TEXT("label"),First->Label);Request->SetField(TEXT("repeat"),Repeat);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid()||!Out->GetBoolField(TEXT("added")))return;
 auto Candidate=S.Store;OKRulesTakeExt(Candidate,*Out);const int32 FirstId=First->Id;const FString Label=Out->GetStringField(TEXT("label"));
 for(auto& A:Candidate.Appointments)if(A.Id==FirstId)A.Label=Label;
 if(CommitStore(MoveTemp(Candidate))){UE_LOG(LogTemp,Display,TEXT("Street series: %s"),*Label);RefreshUI();}
}

// Each weekly meeting's next one, once the last has gone by (series.rs series_due), checked every game hour.
void AOKStreetSocialScene::TickSeries(){
 auto& S=*State;const int64 Now=OKStreetMindNow(S);if(Now/60==S.SeriesHour)return;S.SeriesHour=Now/60;
 if(S.Store.Ext.IsEmpty()||!S.Store.Ext.Contains(TEXT("\"series\"")))return;
 auto Request=OKRulesRequest(TEXT("series_due"));OKRulesPutExt(S,Request);Request->SetNumberField(TEXT("now"),double(Now));
 TArray<TSharedPtr<FJsonValue>> Pending;for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("heroine"),A.Heroine);O->SetNumberField(TEXT("start"),double(A.Start));Pending.Add(MakeShared<FJsonValueObject>(O));}
 Request->SetArrayField(TEXT("pending"),Pending);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid()||Out->GetArrayField(TEXT("book")).IsEmpty())return;
 auto Candidate=S.Store;OKRulesTakeExt(Candidate,*Out);if(!CommitStore(MoveTemp(Candidate)))return;
 for(const auto& V:Out->GetArrayField(TEXT("book"))){const auto O=V->AsObject();const int32 Girl=CharacterOf(O->GetStringField(TEXT("heroine")));if(Girl==INDEX_NONE)continue;
  AddAppointment(TEXT("date"),Girl,O->GetStringField(TEXT("place")),int64(O->GetNumberField(TEXT("start"))),PatienceOf(Girl),O->GetStringField(TEXT("label")));PlanPromisesToday(Girl);
  UE_LOG(LogTemp,Display,TEXT("Street series: next %s at minute %lld"),*O->GetStringField(TEXT("label")),int64(O->GetNumberField(TEXT("start"))));}
 RefreshUI();
}

void AOKStreetSocialScene::CancelPromiseWith(int32 Character,const FString& Scope){
 auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 // Her word "cancel it": the soonest meeting with her, a pickup included ("I can't come for you at five today"
 // called off Wednesday's stake-out and left today's pickup standing).
 const FOKStreetAppointment* Soonest=nullptr;
 for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Heroine==Id&&(A.Kind==TEXT("date")||A.Kind==TEXT("pickup"))&&(!Soonest||A.Start<Soonest->Start))Soonest=&A;
 if(!Soonest)return;
 // Part of a weekly meeting (series.rs series_cancel): all of them when that was said, this one when that was said,
 // and asked when nobody said which ("以後每週的也取消嗎？" — yes: all; no answer: this one).
 const int32 AppointmentId=Soonest->Id;const int64 Start=Soonest->Start;
 auto Ask=[this,Id,Start](const FString& Said){auto& L=*State;auto Request=OKRulesRequest(TEXT("series_cancel"));OKRulesPutExt(L,Request);Request->SetStringField(TEXT("heroine"),Id);Request->SetNumberField(TEXT("start"),double(Start));Request->SetStringField(TEXT("scope"),Said);
  const TSharedPtr<FJsonObject> Out=OKRulesEval(L,Request);if(Out.IsValid()&&!Out->GetBoolField(TEXT("ask"))){auto Candidate=L.Store;OKRulesTakeExt(Candidate,*Out);CommitStore(MoveTemp(Candidate));}return Out;};
 const TSharedPtr<FJsonObject> First=Ask(Scope);
 // Her own act field on a weekly meeting: the judge, a moment later, says this one or all (and asks only if it cannot tell).
 if(First.IsValid()&&First->GetBoolField(TEXT("ask"))&&Scope==TEXT("her")){UE_LOG(LogTemp,Display,TEXT("Street series: her cancel waits for the judge (this one or all)"));return;}
 if(First.IsValid()&&First->GetBoolField(TEXT("ask"))){TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
  OpenChoice(Character,TEXT("series_cancel"),OKT(TEXT("ui.appointments.cancel_all_weeks")),{TEXT("accept"),TEXT("decline")},TEXT("decline"),.3f,20,[Weak,Ask,AppointmentId](const FString& Option){if(!Weak.IsValid())return;Ask(Option==TEXT("accept")?TEXT("all"):TEXT("this"));Weak->CancelAppointment(AppointmentId);});
  RefreshUI();return;}
 CancelAppointment(AppointmentId);
}

// The meeting with her moves to the time just agreed (the judge's "replaces"): the old one goes quietly, it was not
// called off ("又取消了？" for a day moved, and the cost of a cancellation). What she feels about it is in her reply.
void AOKStreetSocialScene::MovePromiseWith(int32 AppointmentId){
 auto& S=*State;auto Candidate=S.Store;FOKStreetAppointment* Old=Candidate.Appointments.FindByPredicate([&](const FOKStreetAppointment& A){return A.Id==AppointmentId&&A.Status==TEXT("pending");});
 if(!Old)return;Old->Status=TEXT("moved");const FString Label=Old->Label;const int32 Moved=Old->Id;const int32 Character=CharacterOf(Old->Heroine);
 if(CommitStore(MoveTemp(Candidate))){if(Character!=INDEX_NONE)PlanPromisesToday(Character);UE_LOG(LogTemp,Display,TEXT("Street appointment %d moved (a new time follows): %s"),Moved,*Label);}
}

void AOKStreetSocialScene::CancelAppointment(int32 AppointmentId){
 auto& S=*State;const int64 Now=OKStreetMindNow(S);
 auto Candidate=S.Store;FOKStreetAppointment* Found=Candidate.Appointments.FindByPredicate([&](const FOKStreetAppointment& A){return A.Id==AppointmentId&&A.Status==TEXT("pending");});
 if(!Found)return;
 Found->Status=TEXT("cancelled");const FString Label=Found->Label;const FString Id=Found->Heroine;const int32 Girl=Id.IsEmpty()?INDEX_NONE:CharacterOf(Id);
 // With a heroine it costs her feelings by how late it is called off; a shift or a table is just given up.
 if(Girl!=INDEX_NONE){
  auto Request=OKRulesRelationRequest(TEXT("appointment_cancel"),OKStreetViewRelation(Candidate,Id,S.Manifest.TalkBudget),ProfileOf(Girl));Request->SetNumberField(TEXT("minutes_left"),double(Found->Start-Now));
  if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request))ApplyRulesEffects(Candidate,*Answer);
 }
 if(CommitStore(MoveTemp(Candidate)))FlushRulesEffects();
 // Called off to her face: she answers it in the talk (the cost is the rule's, not hers to add).
 if(Girl!=INDEX_NONE&&FreeTalkPartner()==Girl)S.TellHer.Add(OKMsg(TEXT("ui.appointments.cancel_appointment_prompt"), {{TEXT("label"), *Label}}));
 if(Girl!=INDEX_NONE)PlanPromisesToday(Girl);
 UE_LOG(LogTemp,Display,TEXT("Street appointment %d cancelled: %s"),AppointmentId,*Label);
 S.FreeTalkNotice=OKT(TEXT("ui.appointments.cancelled_prefix"))+Label;RefreshUI();
}

bool AOKStreetSocialScene::Blacklisted(const FString& Place,int32* DaysLeft) const{
 const int32* Until=State->Store.Blacklist.Find(Place);
 if(!Until||*Until<=State->Store.CalendarDay)return false;
 if(DaysLeft)*DaysLeft=*Until-State->Store.CalendarDay;
 return true;
}

// The next pending appointment that a fast-forward (sleep, wait) would skip; plan §13.3.2 stops 30 min before.
const FOKStreetAppointment* AOKStreetSocialScene::AppointmentBefore(int64 UntilMinute) const{
 for(const auto& A:State->Store.Appointments)if(A.Status==TEXT("pending")&&A.Kind!=TEXT("entrust")&&A.Start-30<=UntilMinute)return &A; // money owed is not a meeting
 return nullptr;
}

// Arriving, being late, being stood up and the blacklist are rules (social.rs appointments_tick); here whether he is
// there, who he is busy with, and her walking over to him when he arrives.
void AOKStreetSocialScene::SetAsideAbsentMeetings(){
 auto& S=*State;
 // A meeting with a heroine who is not in this game's cast (an old save, another selection) is "away": not listed, not
 // late, not a no-show ("迟到 0:20" for a girl who was not there). Back in the cast, it is pending again if still ahead.
 {auto Request=OKRulesRequest(TEXT("appointments_cast"));TArray<TSharedPtr<FJsonValue>> Cast,Rows;
  for(int32 I=1;I<S.Manifest.Characters.Num();++I)Cast.Add(MakeShared<FJsonValueString>(OKStreetStableId(S.Manifest.Characters[I])));
  for(const auto& A:S.Store.Appointments){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("id"),A.Id);O->SetStringField(TEXT("heroine"),A.Heroine);O->SetStringField(TEXT("status"),A.Status);O->SetNumberField(TEXT("start"),double(A.Start));Rows.Add(MakeShared<FJsonValueObject>(O));}
  const int32 Minute=FMath::Max(0,S.Store.CalendarMinute);Request->SetArrayField(TEXT("cast"),Cast);Request->SetArrayField(TEXT("appointments"),Rows);Request->SetNumberField(TEXT("now"),double(int64(S.Store.CalendarDay)*1440+Minute+(Minute<240?1440:0)));
  if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request)){
   for(const auto& V:Out->GetArrayField(TEXT("changes"))){const auto C=V->AsObject();const int32 AppId=int32(C->GetNumberField(TEXT("id")));for(auto& A:S.Store.Appointments)if(A.Id==AppId)A.Status=C->GetStringField(TEXT("status"));}
   const int32 Away=int32(Out->GetNumberField(TEXT("away"))),Back=int32(Out->GetNumberField(TEXT("back")));
   if(Away+Back)UE_LOG(LogTemp,Display,TEXT("Street appointments: %d with heroines not in this cast set aside, %d back"),Away,Back);}}
}

void AOKStreetSocialScene::TickAppointments(){
 auto& S=*State;const int64 Now=OKStreetMindNow(S);
 if(Now==S.AppointmentTickMinute)return;S.AppointmentTickMinute=Now;
 TickSeries();
 if(!S.bAbsentCastChecked){S.bAbsentCastChecked=true;SetAsideAbsentMeetings();} // the rules are open by now (not at load)
 // She walks over to him at the meeting place and opens the talk.
 auto ArriveNow=[this,&S](int32 Girl){
   if(S.ApproachGirl==INDEX_NONE&&S.Agents.IsValidIndex(Girl-1)&&S.Target!=Girl){
    // On another floor (upstairs in the department store, he is down on the street): she comes down first and sets out
    // from the building's door. A straight line from the upper floor kept her walking into the wall.
    // Or inside another building (at work in the restaurant): from its door too; the street routes do not reach inside.
    {auto& A=S.Agents[Girl-1];const FString HerPlace=PlaceHere(A.Position);
     if(FMath::Abs(A.Position.Z-S.Male.Z)>100.f||(!HerPlace.IsEmpty()&&HerPlace!=PlaceHere(S.Male)))if(const auto* P=OKStreetFindPlace(HerPlace);P&&!P->Door.IsNearlyZero()){
     UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d is on another floor of %s (z %.0f, he %.0f); she sets out from its door"),Girl,P->Id,A.Position.Z,S.Male.Z);
     A.Position=FVector(P->Door.X,P->Door.Y,S.Male.Z);S.Commutes.Remove(Girl);S.CommuteFinal.Remove(Girl);}}
    auto Path=ContactRoute(S.Agents[Girl-1].Position,S.Male);
    if(Path.IsEmpty())Path={S.Agents[Girl-1].Position,S.Male}; // no planned route (indoors): walk straight to him
    // Out of his sight (as the schedule does): she is already on her way, the last stretch along the route away; only that
    // part is walked where he can see it.
    if(FVector::Dist2D(S.Agents[Girl-1].Position,S.Male)>4000.f&&Path.Num()>=2){
     int32 From=0;for(int32 K=0;K<Path.Num();++K)if(FVector::Dist2D(Path[K],S.Male)<=2500.f){From=FMath::Max(0,K-1);break;}
     if(From>0){S.Agents[Girl-1].Position=Path[From];Path.RemoveAt(0,From);UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d was out of sight; already on her way, at %s"),Girl,*Path[0].ToCompactString());}}
    // Time to walk the route, with room to spare (a fixed 90 s gave up halfway across the map).
    float Length=0;for(int32 K=1;K<Path.Num();++K)Length+=FVector::Dist2D(Path[K-1],Path[K]);
    S.ApproachLimit=FMath::Max(90.0,30.0+2.0*Length/OKStreetWalkSpeed);
    S.ApproachGirl=Girl;S.ApproachBlockedNear=false;S.ApproachBlockedSince=0;S.ApproachPath=MoveTemp(Path);S.ApproachStarted=FPlatformTime::Seconds();S.ApproachInvite=false;S.ApproachAppointment=true;S.ApproachRetries=0;S.Agents[Girl-1].Held=true;
    UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d comes to him (route points=%d, she is at %s, he at %s)"),Girl,S.ApproachPath.Num(),*S.Agents[Girl-1].Position.ToCompactString(),*S.Male.ToCompactString());
   }else if(S.Target==Girl){UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d is already with him, no walk over"),Girl);}
   else{UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d waits, another girl is walking over (approach=%d)"),Girl,S.ApproachGirl);}
 };
 for(auto It=S.ArriveLater.CreateIterator();It;++It)if(Now>=It.Value()){const int32 Girl=It.Key();It.RemoveCurrent();ArriveNow(Girl);}
 if(!S.Store.Appointments.ContainsByPredicate([](const FOKStreetAppointment& A){return A.Status==TEXT("pending");})&&!S.Store.Appointments.ContainsByPredicate([&](const FOKStreetAppointment& A){return Now-A.Start>2*1440;}))return;
 auto IdOf=[&](int32 I){return I>0&&S.Manifest.Characters.IsValidIndex(I)?OKStreetStableId(S.Manifest.Characters[I]):FString();};
 auto Request=OKRulesRequest(TEXT("appointments_tick"));Request->SetNumberField(TEXT("now"),double(Now));Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);
 Request->SetStringField(TEXT("talking_to"),IdOf(S.Target));Request->SetStringField(TEXT("approaching"),IdOf(S.ApproachGirl));Request->SetStringField(TEXT("with"),IdOf(S.Target!=INDEX_NONE?S.Target:S.Companion));
 Request->SetStringField(TEXT("came_with"),IdOf(S.OutingGirl!=INDEX_NONE?S.OutingGirl:S.Companion!=INDEX_NONE?S.Companion:S.Pair.Partner));
 TArray<TSharedPtr<FJsonValue>> Rows;auto Hers=MakeShared<FJsonObject>();
 for(const auto& A:S.Store.Appointments){
  auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("id"),A.Id);O->SetStringField(TEXT("kind"),A.Kind);O->SetStringField(TEXT("heroine"),A.Heroine);O->SetStringField(TEXT("place"),A.Place);
  O->SetStringField(TEXT("label"),A.Label);O->SetNumberField(TEXT("start"),double(A.Start));O->SetNumberField(TEXT("grace"),A.Grace);O->SetStringField(TEXT("status"),A.Status);
  if(A.Status==TEXT("pending")){
   const auto* P=OKStreetFindPlace(A.Place);O->SetBoolField(TEXT("there"),PlaceHere(S.Male)==A.Place||(P&&FVector::Dist2D(P->Door,S.Male)<1000));
   if(A.Kind==TEXT("entrust"))O->SetBoolField(TEXT("entrust_done"),EntrustReminderDone(A));
   const int32 Girl=CharacterOf(A.Heroine);
   if(Girl!=INDEX_NONE&&!Hers->HasField(A.Heroine)){auto H=MakeShared<FJsonObject>();H->SetObjectField(TEXT("relation"),OKRulesRelationJson(OKStreetViewRelation(S.Store,A.Heroine,S.Manifest.TalkBudget)));if(const auto Pr=ProfileOf(Girl))H->SetObjectField(TEXT("profile"),Pr.ToSharedRef());Hers->SetObjectField(A.Heroine,H);}
  }
  Rows.Add(MakeShared<FJsonValueObject>(O));
 }
 Request->SetArrayField(TEXT("appointments"),Rows);Request->SetObjectField(TEXT("heroines"),Hers);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 const auto& Updates=Answer->GetArrayField(TEXT("updates"));const auto& Remove=Answer->GetArrayField(TEXT("remove"));const auto& Black=Answer->GetArrayField(TEXT("blacklist"));
 const TArray<TSharedPtr<FJsonValue>> NoFired;const TArray<TSharedPtr<FJsonValue>>* Fired=&NoFired;Answer->TryGetArrayField(TEXT("fired"),Fired);
 if(Updates.IsEmpty()&&Remove.IsEmpty()&&Black.IsEmpty()&&Fired->IsEmpty())return;
 auto Candidate=S.Store;
 for(const auto& V:Updates){const auto O=V->AsObject();const int32 Id=int32(O->GetNumberField(TEXT("id")));if(auto* A=Candidate.Appointments.FindByPredicate([&](const FOKStreetAppointment& X){return X.Id==Id;}))A->Status=O->GetStringField(TEXT("status"));}
 TSet<int32> Gone;for(const auto& V:Remove)Gone.Add(int32(V->AsNumber()));Candidate.Appointments.RemoveAll([&](const FOKStreetAppointment& A){return Gone.Contains(A.Id);});
 for(const auto& V:Black){const auto O=V->AsObject();Candidate.Blacklist.Add(O->GetStringField(TEXT("place")),int32(O->GetNumberField(TEXT("until"))));}
 // A shift he never came to: the job there is gone (the place itself stays open to him).
 for(const auto& V:*Fired){const FString Place=V->AsObject()->GetStringField(TEXT("place"));Candidate.PlayerJobs.RemoveAll([&](const FString& J){const auto* Job=S.Catalog.FindJob(J);return Job&&Job->Place==Place;});}
 TSet<FString> Settled;for(const auto& V:Updates){const auto O=V->AsObject();const int32 Id=int32(O->GetNumberField(TEXT("id")));if(const auto* A=Candidate.Appointments.FindByPredicate([&](const FOKStreetAppointment& X){return X.Id==Id;});A&&!A->Heroine.IsEmpty()&&A->Status!=TEXT("pending"))Settled.Add(A->Heroine);}
 ApplyRulesEffects(Candidate,*Answer);
 if(CommitStore(MoveTemp(Candidate)))FlushRulesEffects();
 // A meeting kept (or missed) leaves her day: lunch at the cafeteria met at 11:30, she still walked back there at 12:45
 // for "the promise" when he turned to someone else.
 for(const FString& Her:Settled)if(const int32 Girl=CharacterOf(Her);Girl!=INDEX_NONE)PlanPromisesToday(Girl);
 {const FString Notice=Answer->GetStringField(TEXT("notice"));if(!Notice.IsEmpty())S.FreeTalkNotice=Notice;}
 // She comes to him at the meeting place and opens the talk.
 for(const auto& V:Answer->GetArrayField(TEXT("arrive"))){
  const int32 Girl=CharacterOf(V->AsObject()->GetStringField(TEXT("heroine")));if(Girl==INDEX_NONE)continue;
  // Still at work (or in class) until the meeting: she comes at its time, not half an hour early off her shift ("I am still
  // working, 25 more minutes" when she had already walked over).
  {const int32 Id=int32(V->AsObject()->GetNumberField(TEXT("id")));const FOKStreetAppointment* A=S.Store.Appointments.FindByPredicate([&](const FOKStreetAppointment& X){return X.Id==Id;});
   const FOKStreetPlanBlock* B=BlockNow(Girl,int32(Now%1440));
   if(A&&Now<A->Start&&B&&(B->Kind==TEXT("work")||B->Kind==TEXT("class"))){S.ArriveLater.Add(Girl,A->Start);UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d is at %s until %02d:%02d; she comes then"),Girl,*B->Kind,int32(A->Start%1440)/60,int32(A->Start%60));continue;}}
  ArriveNow(Girl);
 }
 RefreshUI();
}

// Plan §13.3.2: the list under the minimap, soonest first, with game-time countdowns and the blacklist.
FString AOKStreetSocialScene::TaskListText() const{
 const auto& S=*State;const int64 Now=OKStreetMindNow(S);
 TArray<const FOKStreetAppointment*> Pending;for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending"))Pending.Add(&A);
 Pending.Sort([](const FOKStreetAppointment& A,const FOKStreetAppointment& B){return A.Start<B.Start;});
 TArray<FString> Lines;
 for(int32 I=0;I<Pending.Num()&&I<5;++I){const auto& A=*Pending[I];const int64 Left=A.Start-Now;
  const FString When=Left>=0?OKMsg(TEXT("ui.appointments.time_until_later"), {{TEXT("hours"), Left/60}, {TEXT("minutes"), Left%60}}):OKMsg(TEXT("ui.appointments.late_by"), {{TEXT("hours"), (-Left)/60}, {TEXT("minutes"), (-Left)%60}});
  const TCHAR* Mark=Left<0?TEXT("‼"):Left<15?TEXT("●"):Left<60?TEXT("○"):TEXT("·");
  Lines.Add(OKMsg(TEXT("ui.appointments.appointment_row"), {{TEXT("mark"), Mark}, {TEXT("label"), *A.Label}, {TEXT("when"), *When}}));}
 if(!S.FeesDueText.IsEmpty())Lines.Add(FString(TEXT("‼ "))+S.FeesDueText);
 for(const auto& B:S.Store.Blacklist)if(B.Value>S.Store.CalendarDay){const auto* P=OKStreetFindPlace(B.Key);Lines.Add(OKMsg(TEXT("ui.appointments.blacklist_days_left"), {{TEXT("name"), P?P->Name:*B.Key}, {TEXT("days"), B.Value-S.Store.CalendarDay}}));}
 return FString::Join(Lines,TEXT("\n"));
}

FSlateColor AOKStreetSocialScene::TaskListColor() const{
 const int64 Now=OKStreetMindNow(*State);int64 Soonest=MAX_int64;
 for(const auto& A:State->Store.Appointments)if(A.Status==TEXT("pending"))Soonest=FMath::Min(Soonest,A.Start-Now);
 return FSlateColor(Soonest<0?FLinearColor(1,.3f,.3f):Soonest<15?FLinearColor(1,.6f,.2f):Soonest<60?FLinearColor(1,.9f,.3f):FLinearColor::White);
}

// Clicking the list walks him to the soonest appointment.
void AOKStreetSocialScene::NavigateToNextAppointment(){
 const auto& S=*State;const FOKStreetAppointment* Next=nullptr;
 for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&(!Next||A.Start<Next->Start))Next=&A;
 if(!Next)return;const auto* P=OKStreetFindPlace(Next->Place);if(!P)return;
 NavigatePoint(P->Door,P->Name);
}
