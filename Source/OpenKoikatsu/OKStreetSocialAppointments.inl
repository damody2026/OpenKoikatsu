// Included by OKStreetSocialScene.cpp: plan §13.3 appointments, the task list under the minimap, arriving, being
// late, standing someone up, and the shop blacklist.

int32 AOKStreetSocialScene::AddAppointment(const FString& Kind,int32 Heroine,const FString& Place,int64 Start,int32 Grace,const FString& Label,int32 Minutes,const FString& Status){
 auto& S=*State;auto Candidate=S.Store;
 const FString HerId=S.Manifest.Characters.IsValidIndex(Heroine)&&Heroine>0?OKStreetStableId(S.Manifest.Characters[Heroine]):FString();
 // He agrees to what she asked for ("OK, I'll come for you at ten" to her pickup text): that request becomes the
 // meeting, not a second one beside it.
 if(Status==TEXT("pending")&&!HerId.IsEmpty())if(FOKStreetAppointment* Asked=Candidate.Appointments.FindByPredicate([&](const FOKStreetAppointment& X){return X.Status==TEXT("asked")&&X.Heroine==HerId&&FMath::Abs(X.Start-Start)<=90;})){
  Asked->Status=TEXT("pending");const int32 Id=Asked->Id;const FString AskedLabel=Asked->Label;
  if(!CommitStore(MoveTemp(Candidate)))return 0;
  UE_LOG(LogTemp,Display,TEXT("Street appointment %d: he agreed to what she asked (%s)"),Id,*AskedLabel);
  PlanPromisesToday(Heroine);RefreshUI();return Id;}
 // The same appointment again (a plan rebuilt after a reload, a repeated request): the one already there.
 if(const FOKStreetAppointment* Same=Candidate.Appointments.FindByPredicate([&](const FOKStreetAppointment& X){return (X.Status==TEXT("pending")||X.Status==TEXT("asked"))&&X.Kind==Kind&&X.Start==Start&&X.Place==Place&&X.Heroine==(S.Manifest.Characters.IsValidIndex(Heroine)&&Heroine>0?OKStreetStableId(S.Manifest.Characters[Heroine]):FString());}))return Same->Id;
 FOKStreetAppointment A;A.Id=Candidate.NextAppointment++;A.Kind=Kind;A.Place=Place;A.Start=Start;A.Grace=Grace;A.Label=Label;A.Minutes=FMath::Clamp(Minutes,0,1440);A.Status=Status;
 if(S.Manifest.Characters.IsValidIndex(Heroine)&&Heroine>0)A.Heroine=OKStreetStableId(S.Manifest.Characters[Heroine]);
 Candidate.Appointments.Add(A);
 if(!CommitStore(MoveTemp(Candidate)))return 0;
 UE_LOG(LogTemp,Display,TEXT("Street appointment %d: %s %s at %s minute %lld (%s)"),A.Id,*Kind,*Label,*Place,A.Start,*Status);
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
 for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Heroine==Id&&A.Kind==TEXT("date")&&A.Start/1440==S.PlanDay){auto P=MakeShared<FJsonObject>();P->SetNumberField(TEXT("start"),double(A.Start%1440));P->SetStringField(TEXT("place"),A.Place);if(A.Minutes>0)P->SetNumberField(TEXT("end"),double(A.Start%1440+A.Minutes));Promises.Add(MakeShared<FJsonValueObject>(P));}
 Request->SetArrayField(TEXT("blocks"),Blocks);Request->SetArrayField(TEXT("promises"),Promises);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 TArray<FOKStreetPlanBlock> New;for(const auto& V:Answer->GetArrayField(TEXT("blocks")))if(V->AsObject())New.Add(OKRulesReadBlock(*V->AsObject()));
 // Nothing changed (a meeting on another day): her day goes on as it is.
 auto Same=[](const FOKStreetPlanBlock& X,const FOKStreetPlanBlock& Y){return X.Start==Y.Start&&X.End==Y.End&&X.Kind==Y.Kind&&X.Place==Y.Place&&X.Activity==Y.Activity;};
 bool Changed=New.Num()!=Day->Num();for(int32 K=0;!Changed&&K<New.Num();++K)Changed=!Same(New[K],(*Day)[K]);
 if(!Changed)return;
 // Her own meal still to come that the date took the place of: she is told she skips it (asked to dinner, she had said
 // "dinner at the mall at 18:15 first", and at the date claimed she had eaten there).
 {const int32 Now=int32(OKStreetMindNow(S)%1440);
  for(const auto& B:*Day)if(B.Kind==TEXT("meal")&&B.Start>Now&&!New.ContainsByPredicate([&](const FOKStreetPlanBlock& X){return X.Kind==B.Kind&&X.Start==B.Start&&X.Place==B.Place;})){
   const auto* P=OKStreetFindPlace(B.Place);
   MindObserve(Heroine,TEXT("meal"),OKMsg(TEXT("ui.schedule.meal_skipped_for_date"),{{TEXT("time"),FString::Printf(TEXT("%02d:%02d"),B.Start/60,B.Start%60)},{TEXT("place"),P?P->Name:B.Place}}),B.Place,0.3f);
   UE_LOG(LogTemp,Display,TEXT("Street plan: actor %d skips her %02d:%02d meal at %s for the date"),Heroine,B.Start/60,B.Start%60,*B.Place);}}
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
void AOKStreetSocialScene::AddPromiseFromChat(int32 Character,const FString& Day,const FString& Time,const FString& PlaceSaid,const FString& WhatSaid,const FString& Words,const FString& His,bool bReplaces,float Hours){
 // An activity given as the place ("dept_view", the view restaurant): they meet at its place to do it. Booked as no such
 // place, she was told the restaurant does not exist and then insisted it does.
 FString Place=PlaceSaid,WhatId=WhatSaid;
 if(!OKStreetFindPlace(Place))if(const FOKStreetActivity* Act=OKStreetFindActivity(Place);Act&&Act->Place&&*Act->Place){if(WhatId.IsEmpty())WhatId=Place;Place=Act->Place;}
 // An activity id in "what" ("meal") is said by its name in the label and her memory ("約meal" on the list).
 const FString What=OKStreetFindActivity(WhatId)?OKStreetActivityName(WhatId):WhatId;
 auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);const FOKStreetPlace* P=OKStreetFindPlace(Place);
 // Whether it is a meeting at all, a new one or the same one said again, and where and when, is promise.rs
 // (promise_from_chat); the judge's reading of both lines gives day, time and place.
 auto Request=OKRulesRequest(TEXT("promise_from_chat"));Request->SetStringField(TEXT("heroine"),Id);Request->SetStringField(TEXT("day"),Day);
 Request->SetStringField(TEXT("time"),Time);Request->SetStringField(TEXT("place"),Place);Request->SetBoolField(TEXT("place_known"),P!=nullptr);Request->SetBoolField(TEXT("replaces"),bReplaces);
 Request->SetNumberField(TEXT("now"),double(OKStreetMindNow(S)));Request->SetNumberField(TEXT("today"),S.Store.CalendarDay);
 // "After class" (time after_class / after_work): when what each has on today after now ends, his timetable and her
 // day; promise.rs takes the later. -1: nothing more today.
 {const int32 Now=int32(OKStreetMindNow(S)%1440),Weekday=int32(StreetNow().GetDayOfWeek());int32 HisClass=-1,HerClass=-1,HisWork=-1,HerWork=-1;
  for(const auto& C:S.Store.PlayerAcademic.Courses)if(C.Weekday==Weekday){const int32 End=C.Slot==0?720:990;if(End>Now)HisClass=FMath::Max(HisClass,End);}
  for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Kind==TEXT("shift")&&A.Start/1440==S.Store.CalendarDay){const int32 End=int32(A.Start%1440)+FMath::Max(A.Minutes,60);if(End>Now)HisWork=FMath::Max(HisWork,End);}
  if(const TArray<FOKStreetPlanBlock>* Plan=S.Plans.Find(Character))for(const auto& B:*Plan)if(B.End>Now){if(B.Kind==TEXT("class"))HerClass=FMath::Max(HerClass,B.End);else if(B.Kind==TEXT("work"))HerWork=FMath::Max(HerWork,B.End);}
  Request->SetNumberField(TEXT("his_class_end"),HisClass);Request->SetNumberField(TEXT("her_class_end"),HerClass);Request->SetNumberField(TEXT("his_work_end"),HisWork);Request->SetNumberField(TEXT("her_work_end"),HerWork);
  // Another day: their timetables and his shifts (promise.rs reads them for that day).
  auto Courses=[](const FOKStreetAcademic* A){TArray<TSharedPtr<FJsonValue>> Out;if(A&&A->Status==TEXT("enrolled"))for(const auto& C:A->Courses){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("weekday"),C.Weekday);O->SetNumberField(TEXT("slot"),C.Slot);Out.Add(MakeShared<FJsonValueObject>(O));}return Out;};
  Request->SetArrayField(TEXT("his_courses"),Courses(&S.Store.PlayerAcademic));Request->SetArrayField(TEXT("her_courses"),Courses(S.Store.Academics.Find(Id)));
  TArray<TSharedPtr<FJsonValue>> Shifts;for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Kind==TEXT("shift")){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("start"),double(A.Start));O->SetNumberField(TEXT("minutes"),A.Minutes);Shifts.Add(MakeShared<FJsonValueObject>(O));}
  Request->SetArrayField(TEXT("his_shifts"),Shifts);
  // Where either of them finishes today and when (promise.rs: a meeting at a place's door as it closes is picking one up
  // there, R6-44): her work and classes, his shifts.
  TArray<TSharedPtr<FJsonValue>> Ends;auto End=[&Ends](const FString& Where,int64 At){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("place"),Where);O->SetNumberField(TEXT("end"),double(At));Ends.Add(MakeShared<FJsonValueObject>(O));};
  if(const TArray<FOKStreetPlanBlock>* Plan=S.Plans.Find(Character))for(const auto& B:*Plan)if(B.Kind==TEXT("class")||B.Kind==TEXT("work"))End(B.Place,int64(S.Store.CalendarDay)*1440+B.End);
  for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Kind==TEXT("shift"))End(A.Place,A.Start+FMath::Max(A.Minutes,60));
  Request->SetArrayField(TEXT("ends_there"),Ends);}
 {const double Wall=FPlatformTime::Seconds();TArray<TSharedPtr<FJsonValue>> Pending;
  for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("id"),A.Id);O->SetStringField(TEXT("heroine"),A.Heroine);
   O->SetStringField(TEXT("kind"),A.Kind);O->SetStringField(TEXT("place"),A.Place);O->SetNumberField(TEXT("start"),double(A.Start));O->SetStringField(TEXT("label"),A.Label);
   const double* Booked=S.PromiseBookedAt.Find(A.Id);O->SetNumberField(TEXT("booked_ago"),Booked?Wall-*Booked:-1.0);Pending.Add(MakeShared<FJsonValueObject>(O));}
  Request->SetArrayField(TEXT("pending"),Pending);}
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return;
 const FString Action=Out->GetStringField(TEXT("action")),Reason=Out->GetStringField(TEXT("reason"));
 // An ignored promise has no start or minute.
 double StartAt=0,MinuteAt=0;Out->TryGetNumberField(TEXT("start"),StartAt);Out->TryGetNumberField(TEXT("minute"),MinuteAt);
 const int64 Start=int64(StartAt);const int32 Minute=int32(MinuteAt);
 if(Action==TEXT("ignore")){UE_LOG(LogTemp,Display,TEXT("Street promise ignored (%s): day \"%s\" time \"%s\" at %s"),*Reason,*Day,*Time,*Place);
  // The judge's "+365" (days from today) said as a date: it showed as it was, "+365 at the park: no time yet" (R6-24).
  const FString DaySaid=Day.StartsWith(TEXT("+"))&&Day.Mid(1).IsNumeric()?[&]{const FDateTime T=OKStreetCalendarDateTime(S.Store.CalendarDay+FCString::Atoi(*Day.Mid(1)),12.0);return OKMsg(TEXT("ui.calendar.date_full"),{{TEXT("year"),T.GetYear()},{TEXT("month"),T.GetMonth()},{TEXT("day"),T.GetDay()},{TEXT("weekday"),OKMsg(TEXT("ui.calendar.weekday_name"),{{TEXT("d"),int32(T.GetDayOfWeek())}})}});}():Day;
  // A place the town does not have ("the cinema"): both agreed and nothing was booked. She is told, so she says so and
  // they pick a place that exists; he sees it too.
  if(Reason==TEXT("not_a_place")&&!Place.IsEmpty()){S.TellHer.Add(OKMsg(TEXT("ui.appointments.no_such_place_note"),{{TEXT("place_name"),*Place}}));
   S.FreeTalkNotice=OKMsg(TEXT("ui.appointments.no_such_place_notice"),{{TEXT("place_name"),*Place}});RefreshUI();}
  // A day and a place but no hour ("Saturday at the park"): agreed, yet nothing to book. She asks him what time,
  // instead of the meeting being dropped without either of them knowing.
  // A day and nothing else yet ("all of Sunday with you"): she asks when and where.
  // With the hour said, only where: told "no time or place", Keqing asked "what time?" of his "tomorrow at six" (R5-11).
  if(Reason==TEXT("not_a_place")&&Place.IsEmpty()&&!Day.IsEmpty()){S.TellHer.Add(Time.IsEmpty()?OKMsg(TEXT("ui.appointments.no_time_place_note"),{{TEXT("day"),*DaySaid}}):OKMsg(TEXT("ui.appointments.no_place_note"),{{TEXT("day"),*DaySaid},{TEXT("time"),*Time}}));S.PromiseAskNotes.Add(S.TellHer.Last());
   UE_LOG(LogTemp,Display,TEXT("Street promise: %s agreed, no time or place yet; she asks"),*DaySaid);}
  if(Reason==TEXT("no_time")&&P&&!Day.IsEmpty()){S.TellHer.Add(OKMsg(TEXT("ui.appointments.no_time_note"),{{TEXT("day"),*DaySaid},{TEXT("place_name"),P->Name}}));S.PromiseAskNotes.Add(S.TellHer.Last());
   S.FreeTalkNotice=OKMsg(TEXT("ui.appointments.no_time_notice"),{{TEXT("day"),*DaySaid},{TEXT("place_name"),P->Name}});RefreshUI();}
  return;}
 // Booked, or said again with its time: her question still waiting ("what time? where?") is answered already; she
 // asked again right after "17:30 at the snack shop, I'll be there" (R3-242).
 if(!S.PromiseAskNotes.IsEmpty()){const int32 Dropped=S.TellHer.RemoveAll([&](const FString& N){return S.PromiseAskNotes.Contains(N);});S.PromiseAskNotes.Reset();
  if(Dropped)UE_LOG(LogTemp,Display,TEXT("Street promise: settled, %d question(s) to him about it dropped"),Dropped);}
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
 // As long as they said they would be together (the judge's promise.hours: "the whole Saturday"), else the usual.
 AddAppointment(TEXT("date"),Character,Place,Start,PatienceOf(Character),Label,Hours>0.f?FMath::Clamp(FMath::RoundToInt(Hours*60.f),10,840):0); // "just five minutes" too: her shift after it stays
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

// They settled that what they do now is today's meeting with her, kept early (the judge's kept_promise): "come by the
// cafe for a drink at 11:30", she came at 9:30 and he bought it — the meeting still stood two hours later and she said
// "don't forget 11:30" over the very drink (R6-5). Her soonest meeting today, not one on another day.
void AOKStreetSocialScene::KeepPromiseNow(int32 Character){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Character))return;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 const int64 Now=OKStreetMindNow(S);auto Candidate=S.Store;FOKStreetAppointment* Soonest=nullptr;
 for(auto& A:Candidate.Appointments)if(A.Status==TEXT("pending")&&A.Heroine==Id&&A.Kind==TEXT("date")&&A.Start/1440==Now/1440&&Now<=A.Start+A.Grace&&(!Soonest||A.Start<Soonest->Start))Soonest=&A;
 if(!Soonest)return;Soonest->Status=TEXT("done");const FString Label=Soonest->Label;
 if(CommitStore(MoveTemp(Candidate))){UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d kept early (%s)"),Character,*Label);PlanPromisesToday(Character);}
}

void AOKStreetSocialScene::CancelPromiseWith(int32 Character,const FString& Scope){
 auto& S=*State;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 // Her act and the judge read the same "cancel it": one date, not the next one too (R3-268).
 if(const double* At=S.CancelledAt.Find(Character);At&&SceneTime()-*At<20.0){UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d already cancelled one just now"),Character);return;}
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
 // With a heroine it is called off on the phone (R3-268), and costs her feelings by how he put it (social.rs
 // call_cancel); a yandere lover comes to him instead. A shift or a table is just given up.
 bool Comes=false;
 if(Girl!=INDEX_NONE){
  S.CancelledAt.Add(Girl,SceneTime());
  auto Request=OKRulesRelationRequest(TEXT("call_cancel"),OKStreetViewRelation(Candidate,Id,S.Manifest.TalkBudget),ProfileOf(Girl));Request->SetNumberField(TEXT("minutes_left"),double(Found->Start-Now));
  const bool Judged=S.CancelCallGirl==Girl;
  Request->SetStringField(TEXT("said"),Judged?S.CancelCallSaid:FString());Request->SetNumberField(TEXT("grace"),Judged?S.CancelCallGrace:-1.0);
  Request->SetStringField(TEXT("by"),Judged&&S.CancelCallGrace<0&&S.CancelCallSaid.IsEmpty()?TEXT("her"):TEXT(""));
  if(Judged){S.CancelCallGirl=INDEX_NONE;S.CancelCallGrace=-1;S.CancelCallSaid.Reset();}
  if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request)){ApplyRulesEffects(Candidate,*Answer);Comes=Answer->GetBoolField(TEXT("comes"));
   UE_LOG(LogTemp,Display,TEXT("Street appointment %d call cancel: cost %d%s"),AppointmentId,int32(Answer->GetNumberField(TEXT("cost"))),Comes?TEXT(", she comes to him"):TEXT(""));}
 }
 // A yandere lover loses no favor over it: what her replies on this call took (said before the cancel was settled) is
 // given back to where it was when the call began.
 if(Comes&&S.RemotePeer==Girl&&S.CallStartFavor>=0){const int32 Index=OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true);
  if(Candidate.Relations.IsValidIndex(Index)&&Candidate.Relations[Index].Favor<S.CallStartFavor){UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d favor back to %.0f (was %.0f)"),Girl,S.CallStartFavor,Candidate.Relations[Index].Favor);Candidate.Relations[Index].Favor=S.CallStartFavor;}}
 if(CommitStore(MoveTemp(Candidate)))FlushRulesEffects();
 if(Comes)YandereComes(Girl);
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
 // Join: he is talking with another girl; she comes over and joins them (StepApproach), not a talk of her own.
 auto ArriveNow=[this,&S](int32 Girl,bool Join=false){
   const bool WithHim=S.Target==Girl||S.Companion==Girl||S.OutingGirl==Girl||S.Pair.Partner==Girl; // walking over with him already (they set off for the cafeteria together)
   if(S.ApproachGirl==INDEX_NONE&&S.Agents.IsValidIndex(Girl-1)&&!WithHim){
    // He waits upstairs (Yuuka was to meet him after class; he was on the fourth floor of the EE building): she takes the
    // lift up and comes over from there. Walking at him from the ground floor she never reached him and went to work.
    // Far away in another building (at work in the drink shop): he is indoors upstairs and does not see the street, so she
    // is at his building's door already, and rides up from there.
    if(S.Male.Z-S.Agents[Girl-1].Position.Z>100.f&&!S.ArriveAfterLift.Contains(Girl))if(const auto* His=OKStreetFindPlace(PlaceHere(S.Male));His&&!His->Door.IsNearlyZero()&&PlaceHere(S.Agents[Girl-1].Position)!=His->Id&&FVector::Dist2D(S.Agents[Girl-1].Position,S.Male)>3500.f){
     S.Agents[Girl-1].Position=His->Door;UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d is far off; she is at the door of %s, he is upstairs"),Girl,His->Id);}
    if(S.Male.Z-S.Agents[Girl-1].Position.Z>100.f&&!S.ArriveAfterLift.Contains(Girl)&&StartNpcLift(Girl,S.Male)){
     S.ArriveAfterLift.Add(Girl);S.Agents[Girl-1].Held=true;S.Commutes.Remove(Girl);S.CommuteFinal.Remove(Girl);
     UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d takes the lift up to him (z %.0f, he %.0f)"),Girl,S.Agents[Girl-1].Position.Z,S.Male.Z);return;}
    // On another floor (upstairs in the department store, he is down on the street): she comes down first and sets out
    // from the building's door. A straight line from the upper floor kept her walking into the wall.
    // Or inside another building (at work in the restaurant): from its door too; the street routes do not reach inside.
    {auto& A=S.Agents[Girl-1];const FString HerPlace=PlaceHere(A.Position);
     if(FMath::Abs(A.Position.Z-S.Male.Z)>100.f||(!HerPlace.IsEmpty()&&HerPlace!=PlaceHere(S.Male)))if(const auto* P=OKStreetFindPlace(HerPlace);P&&!P->Door.IsNearlyZero()){
     UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d is on another floor of %s (z %.0f, he %.0f); she sets out from its door"),Girl,P->Id,A.Position.Z,S.Male.Z);
     A.Position=FVector(P->Door.X,P->Door.Y,FMath::Abs(S.Male.Z-P->Door.Z)>100.f?P->Door.Z:S.Male.Z);S.Commutes.Remove(Girl);S.CommuteFinal.Remove(Girl);}}
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
    S.ApproachGirl=Girl;S.ApproachBlockedNear=false;S.ApproachBlockedSince=0;S.ApproachPath=MoveTemp(Path);S.ApproachStarted=FPlatformTime::Seconds();S.ApproachInvite=false;S.ApproachAppointment=true;S.ApproachJoin=Join;S.ApproachRetries=0;S.Agents[Girl-1].Held=true;
    UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d comes to him (route points=%d, she is at %s, he at %s)"),Girl,S.ApproachPath.Num(),*S.Agents[Girl-1].Position.ToCompactString(),*S.Male.ToCompactString());
   }else if(WithHim){UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d is already with him, no walk over"),Girl);}
   // She is the one walking over already (to sing for him, and on the way he texted her to meet): that walk is the
   // meeting. She waited for herself ("another girl is walking over (approach=9)").
   else if(S.ApproachGirl==Girl){S.ApproachAppointment=true;UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d is walking over to him already"),Girl);}
   else{
    // Another one walking over first (the three of them meeting for dinner): she comes after her, not never. Only the
    // first came; the other two stood at home all evening, their meetings already counted as kept.
    const bool First=!S.ArriveWaiting.Contains(Girl);const int64 Since=S.ArriveWaiting.FindOrAdd(Girl,OKStreetMindNow(S));
    if(OKStreetMindNow(S)-Since<=60){S.ArriveLater.Add(Girl,OKStreetMindNow(S)+1);if(First)UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d waits, another girl is walking over (approach=%d); she comes after her"),Girl,S.ApproachGirl);}
    else{S.ArriveWaiting.Remove(Girl);UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d waited an hour for another girl walking over (approach=%d); gives up"),Girl,S.ApproachGirl);}
    return;}
   S.ArriveWaiting.Remove(Girl);
 };
 // A wait for him that ended long ago (she joined, or he was alone when she came) does not count against a later one.
 for(auto It=S.MeetJoinSince.CreateIterator();It;++It)if(Now-It.Value()>120)It.RemoveCurrent();
 // Up on his floor (the lift ride is over): she walks over to him from there. He left that floor meanwhile (she set off
 // while he rode up to the view restaurant, and got off at 7F): she goes after him again in a minute, up to an hour.
 for(auto It=S.ArriveAfterLift.CreateIterator();It;++It){const int32 G=*It;if(S.NpcLifts.Contains(G))continue;It.RemoveCurrent();
  if(S.Agents.IsValidIndex(G-1)&&FMath::Abs(S.Agents[G-1].Position.Z-S.Male.Z)<100.f)ArriveNow(G);
  else{const int64 Since=S.MeetJoinSince.FindOrAdd(G,Now);
   if(Now-Since<=60){S.ArriveLater.Add(G,Now+1);UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d came up, he is no longer on that floor; she goes after him"),G);}
   else{S.MeetJoinSince.Remove(G);UE_LOG(LogTemp,Display,TEXT("Street appointment: actor=%d came up, he is no longer on that floor; gives up after an hour"),G);}}}
 // Due now (after her shift, or after the one walking over before her); he talking with another girl by then, she joins them.
 {TArray<int32> Due;for(auto It=S.ArriveLater.CreateIterator();It;++It)if(Now>=It.Value()){Due.Add(It.Key());It.RemoveCurrent();}
  for(const int32 Girl:Due){const int32 With=FreeTalkPartner();ArriveNow(Girl,With!=INDEX_NONE&&With!=Girl);}}
 if(!S.Store.Appointments.ContainsByPredicate([](const FOKStreetAppointment& A){return A.Status==TEXT("pending");})&&!S.Store.Appointments.ContainsByPredicate([&](const FOKStreetAppointment& A){return Now-A.Start>2*1440;}))return;
 auto IdOf=[&](int32 I){return I>0&&S.Manifest.Characters.IsValidIndex(I)?OKStreetStableId(S.Manifest.Characters[I]):FString();};
 auto Request=OKRulesRequest(TEXT("appointments_tick"));Request->SetNumberField(TEXT("now"),double(Now));Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);
 Request->SetStringField(TEXT("talking_to"),IdOf(S.Target));Request->SetStringField(TEXT("talking_name"),S.Target>0?SpokenName(S.Target):FString());Request->SetStringField(TEXT("approaching"),IdOf(S.ApproachGirl));Request->SetStringField(TEXT("with"),IdOf(S.Target!=INDEX_NONE?S.Target:S.Companion));
 Request->SetStringField(TEXT("came_with"),IdOf(S.OutingGirl!=INDEX_NONE?S.OutingGirl:S.Companion!=INDEX_NONE?S.Companion:S.Pair.Partner));
 if(S.ArrivedWith!=INDEX_NONE){Request->SetStringField(TEXT("arrived_with"),IdOf(S.ArrivedWith));Request->SetStringField(TEXT("arrived_place"),S.ArrivedPlace);Request->SetNumberField(TEXT("arrived_minute"),double(S.ArrivedMinute));}
 TArray<TSharedPtr<FJsonValue>> Rows;auto Hers=MakeShared<FJsonObject>();
 for(const auto& A:S.Store.Appointments){
  auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("id"),A.Id);O->SetStringField(TEXT("kind"),A.Kind);O->SetStringField(TEXT("heroine"),A.Heroine);O->SetStringField(TEXT("place"),A.Place);
  O->SetStringField(TEXT("label"),A.Label);O->SetNumberField(TEXT("start"),double(A.Start));O->SetNumberField(TEXT("grace"),A.Grace);O->SetStringField(TEXT("status"),A.Status);
  if(A.Status==TEXT("pending")||A.Status==TEXT("asked")){ // asked: her, for the note when it lapses unanswered
   const auto* P=OKStreetFindPlace(A.Place);O->SetBoolField(TEXT("there"),PlaceHere(S.Male)==A.Place||(P&&FVector::Dist2D(P->Door,S.Male)<1000));
   if(A.Kind==TEXT("entrust"))O->SetBoolField(TEXT("entrust_done"),EntrustReminderDone(A));
   const int32 Girl=CharacterOf(A.Heroine);
   // Since when she is at the place (inside, or within 10 m of its door): how long she has waited is from then.
   if(Girl!=INDEX_NONE&&S.Agents.IsValidIndex(Girl-1)){const FVector& At=S.Agents[Girl-1].Position;
    const bool HerThere=PlaceHere(At)==A.Place||(P&&FVector::Dist2D(P->Door,At)<1000);
    if(!HerThere)S.SheThereSince.Remove(A.Id);else if(!S.SheThereSince.Contains(A.Id))S.SheThereSince.Add(A.Id,Now);
    O->SetNumberField(TEXT("she_since"),double(S.SheThereSince.Contains(A.Id)?S.SheThereSince[A.Id]:-1));
    // She is there for it while he talks with another girl: her wait is that, not him being late (social.rs busy_seen).
    if(S.SheThereSince.Contains(A.Id)&&S.Target!=INDEX_NONE&&S.Target!=Girl)S.AppointmentBusySeen.Add(A.Id);
    O->SetBoolField(TEXT("busy_seen"),S.AppointmentBusySeen.Contains(A.Id));}
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
  bool Join=false;V->AsObject()->TryGetBoolField(TEXT("join"),Join);
  // Who they said would pay when they made it: the bill there follows it (RecordPayAgreed), as said that day (R3-216).
  {const int32 Id=int32(V->AsObject()->GetNumberField(TEXT("id")));if(const FOKStreetAppointment* A=S.Store.Appointments.FindByPredicate([&](const FOKStreetAppointment& X){return X.Id==Id;});A&&!A->Payer.IsEmpty())
    RecordPayAgreed(Girl,A->Payer,TEXT("go:")+A->Place);}
  ArriveNow(Girl,Join);
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
 RefreshFeesDue();if(!S.FeesDueText.IsEmpty())Lines.Add(FString(TEXT("‼ "))+S.FeesDueText);
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
