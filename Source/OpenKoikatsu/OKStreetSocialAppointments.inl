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
void AOKStreetSocialScene::AddPromiseFromChat(int32 Character,const FString& Day,const FString& Time,const FString& Place,const FString& WhatSaid,const FString& Words,const FString& His){
 // An activity id in "what" ("meal") is said by its name in the label and her memory ("約meal" on the list).
 const FString What=OKStreetFindActivity(WhatSaid)?OKStreetActivityName(WhatSaid):WhatSaid;
 auto& S=*State;if(!OKStreetFindPlace(Place)){UE_LOG(LogTemp,Display,TEXT("Street promise ignored: %s is not a place"),*Place);return;}
 // No clear time, no appointment (it used to become 19:00 by default, a time nobody said).
 {FString H,M;if(!Time.Split(TEXT(":"),&H,&M)||!H.IsNumeric()||!M.IsNumeric()){UE_LOG(LogTemp,Display,TEXT("Street promise ignored: no clear time (\"%s\") for %s"),*Time,*Place);return;}}
 // Whether both agreed, which day and which time are the judge's reading of both lines (the model sees what was said);
 // keyword guards on top of it (time_said, day_said, "no day said, so today") kept overruling it and could never cover
 // every way of saying it ("今天中午有约，明天中午十二点？" was booked for today).
 int32 GameDay=S.Store.CalendarDay;
 if(Day.StartsWith(TEXT("+")))GameDay+=FMath::Clamp(FCString::Atoi(*Day.Mid(1)),0,60);
 else{FDateTime Date;if(FDateTime::ParseIso8601(*Day,Date))GameDay=int32((Date-OKStreetCalendarEpoch()).GetTotalDays());}
 FString H,M;int32 Minute=19*60;if(Time.Split(TEXT(":"),&H,&M))Minute=FMath::Clamp(FCString::Atoi(*H),0,23)*60+FMath::Clamp(FCString::Atoi(*M),0,59);
 // Before 04:00 belongs to the previous game day's night.
 const int64 Start=int64(GameDay)*1440+Minute+(Minute<240?1440:0);
 if(Start<=OKStreetMindNow(S))return;
 // One meeting with her at a time: the same hour already has one (the model's promise and the judge's, or a repeat).
 // The same place on the same day is the same meeting said again at another time ("three, not two"): it moves to the
 // time said last instead of becoming a second date.
 // The same meeting booked twice from one exchange (her reply's promise and the judge's), one of them a day off ("seven
 // tonight" read as tomorrow): within a minute and a half, same place and hour a day apart is that meeting, on the
 // nearer day.
 {const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);const double Wall=FPlatformTime::Seconds();
  for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Heroine==Id&&A.Kind==TEXT("date")&&A.Place==Place&&FMath::Abs(A.Start-Start)==1440
   &&Wall-S.PromiseBookedAt.FindRef(A.Id)<90.0){
   if(Start<A.Start){auto Candidate=S.Store;for(auto& C:Candidate.Appointments)if(C.Id==A.Id)C.Start=Start;if(CommitStore(MoveTemp(Candidate))){PlanPromisesToday(Character);RefreshUI();}}
   UE_LOG(LogTemp,Display,TEXT("Street appointment: %s booked again a day apart from the same exchange; one meeting, minute %lld"),*A.Label,FMath::Min(A.Start,Start));return;}}
 {const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);auto DayOf=[](int64 M){return (M-240)/1440;};
  // Her own "pick me up at five" is a meeting like a date: a new time for it moves it (it stayed at 17:00 next to a 17:30).
  for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Heroine==Id&&(A.Kind==TEXT("date")||A.Kind==TEXT("pickup"))&&(FMath::Abs(A.Start-Start)<60||(A.Place==Place&&DayOf(A.Start)==DayOf(Start)))){
   // "Five o'clock, don't forget" said again without am/pm: twelve hours off is the same time; the daytime one of the two
   // (07:00-22:00) is meant, the one already booked when both are.
   auto Daytime=[](int64 M){const int64 Min=M%1440;return Min>=7*60&&Min<22*60;};
   const int64 Said=FMath::Abs(A.Start-Start)!=720?Start:(Daytime(Start)&&!Daytime(A.Start)?Start:A.Start);
   if(Said!=Start)UE_LOG(LogTemp,Display,TEXT("Street appointment: %s said again 12 h off (am/pm unsaid); stays at minute %lld"),*A.Label,Said);
   // Said again with another time or place ("the playground, not this park"): the meeting moves there.
   if(A.Start!=Said||A.Place!=Place){auto Candidate=S.Store;const FOKStreetPlace* To=OKStreetFindPlace(Place);
    for(auto& C:Candidate.Appointments)if(C.Id==A.Id){
     // A label that carries the time ("…接她（17:00）") shows the new one.
     C.Label.ReplaceInline(*FString::Printf(TEXT("%02d:%02d"),int32(C.Start%1440)/60,int32(C.Start%1440)%60),*FString::Printf(TEXT("%02d:%02d"),int32(Said%1440)/60,int32(Said%1440)%60));
     C.Start=Said;
     if(C.Place!=Place&&To){C.Place=Place;C.Label=OKMsg(TEXT("ui.appointments.meet_with_summary"), {{TEXT("character_name"), *S.Manifest.Characters[Character].Name}, {TEXT("what"), What.IsEmpty()?OKT(TEXT("ui.appointments.meeting_label")):*What}, {TEXT("person_name"), To->Name}});}}
    const FString Label=A.Label;const int64 From=A.Start;const FString FromPlace=A.Place;
    if(CommitStore(MoveTemp(Candidate))){PlanPromisesToday(Character);UE_LOG(LogTemp,Display,TEXT("Street appointment: %s moved from minute %lld at %s to %lld at %s"),*Label,From,*FromPlace,Said,*Place);RefreshUI();}}
   else UE_LOG(LogTemp,Display,TEXT("Street appointment: %s at minute %lld kept, the new one at %s is the same meeting"),*A.Label,A.Start,*Place);
   return;}}
 const auto* P=OKStreetFindPlace(Place);
 // Booked for when the place is closed (breakfast at 8:00 at a shop opening at 10:00): not booked; she hears why, so the
 // two of them pick another time or place.
 {FString Why;const int64 Minute0=Start%1440;if(P&&PlaceClosedAt(Place,OKStreetCalendarDateTime(int32(Start/1440),Minute0/60.0),Why)){
  S.TellHer.Add(OKMsg(TEXT("ui.appointments.closed_then_note"), {{TEXT("place_name"), P->Name}, {TEXT("why"), *Why}}));S.FreeTalkNotice=Why;
  UE_LOG(LogTemp,Display,TEXT("Street appointment: %s at minute %lld not booked, closed then (%s)"),*Place,Start,*Why);RefreshUI();return;}}
 const FString Label=OKMsg(TEXT("ui.appointments.meet_with_summary"), {{TEXT("character_name"), *S.Manifest.Characters[Character].Name}, {TEXT("what"), What.IsEmpty()?OKT(TEXT("ui.appointments.meeting_label")):*What}, {TEXT("person_name"), P->Name}});
 AddAppointment(TEXT("date"),Character,Place,Start,PatienceOf(Character),Label);
 // Another girl is already booked near that time: he is told (both stay booked; it is his to sort out).
 {const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
  for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Heroine!=Id&&A.Kind!=TEXT("entrust")&&FMath::Abs(A.Start-Start)<90){
   S.FreeTalkNotice=OKMsg(TEXT("ui.appointments.clash_notice"), {{TEXT("label"), *A.Label}});UE_LOG(LogTemp,Display,TEXT("Street appointment: clashes with %s"),*A.Label);RefreshUI();break;}}
 if(S.Store.Appointments.Num())S.PromiseBookedAt.Add(S.Store.Appointments.Last().Id,FPlatformTime::Seconds());
 HeroineNote(Character,TEXT("promise"),OKMsg(TEXT("ui.appointments.promised_meet_with_him"), {{TEXT("label"), *OKStreetCalendarLabel(int32(Start/1440)-(Minute<240?1:0),Minute/60.0)}, {TEXT("place_name"), P->Name}, {TEXT("what"), What.IsEmpty()?OKT(TEXT("ui.appointments.meeting_label")):*What}}),Place,0.7f);
}

// Cancelling early costs little; within two hours it counts as half a no-show (social.rs appointment_cancel).
void AOKStreetSocialScene::CancelPromiseWith(int32 Character){
 auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 // Her word "cancel it": the soonest meeting with her, a pickup included ("I can't come for you at five today"
 // called off Wednesday's stake-out and left today's pickup standing).
 const FOKStreetAppointment* Soonest=nullptr;
 for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Heroine==Id&&(A.Kind==TEXT("date")||A.Kind==TEXT("pickup"))&&(!Soonest||A.Start<Soonest->Start))Soonest=&A;
 if(Soonest)CancelAppointment(Soonest->Id);
}

// The meeting with her moves to the time just agreed (the judge's "replaces"): the old one goes quietly, it was not
// called off ("又取消了？" for a day moved, and the cost of a cancellation). What she feels about it is in her reply.
void AOKStreetSocialScene::MovePromiseWith(int32 Character){
 auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 auto Candidate=S.Store;FOKStreetAppointment* Soonest=nullptr;
 for(auto& A:Candidate.Appointments)if(A.Status==TEXT("pending")&&A.Heroine==Id&&(A.Kind==TEXT("date")||A.Kind==TEXT("pickup"))&&(!Soonest||A.Start<Soonest->Start))Soonest=&A;
 if(!Soonest)return;Soonest->Status=TEXT("moved");const FString Label=Soonest->Label;const int32 Moved=Soonest->Id;
 if(CommitStore(MoveTemp(Candidate))){PlanPromisesToday(Character);UE_LOG(LogTemp,Display,TEXT("Street appointment %d moved (a new time follows): %s"),Moved,*Label);}
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
void AOKStreetSocialScene::TickAppointments(){
 auto& S=*State;const int64 Now=OKStreetMindNow(S);
 if(Now==S.AppointmentTickMinute)return;S.AppointmentTickMinute=Now;
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
