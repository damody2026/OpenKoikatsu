// Included by OKStreetSocialScene.cpp: plan §6.6 initiative (heroines acting on their own) and §14 the open-world
// Director (helping him discover hidden troubles, holidays, weather, exam weeks, birthdays). Rules only.

// Weather and holidays are rules (Tools/ok-rules src/plan.rs raining_at / holiday_on).
bool AOKStreetSocialScene::RainingAt(int32 Day,int32 Minute) const{
 auto Request=OKRulesRequest(TEXT("weather"));Request->SetNumberField(TEXT("day"),Day);Request->SetNumberField(TEXT("minute"),Minute);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(*State,Request);return Answer&&Answer->GetBoolField(TEXT("raining"));
}

FString AOKStreetSocialScene::HolidayOn(int32 Day) const{
 auto Request=OKRulesRequest(TEXT("holiday"));Request->SetNumberField(TEXT("day"),Day);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(*State,Request);return Answer?Answer->GetStringField(TEXT("holiday")):FString();
}

bool AOKStreetSocialScene::BirthdayOf(int32 I,int32 Day) const{
 const FString B=OKStreetProfileString(ProfileOf(I),nullptr,TEXT("birthday"));TArray<FString> P;B.ParseIntoArray(P,TEXT("-"));
 if(P.Num()!=3)return false;const FDateTime D=OKStreetCalendarDateTime(Day,12.0);
 return D.GetMonth()==FCString::Atoi(*P[1])&&D.GetDay()==FCString::Atoi(*P[2]);
}

// The 【今天】 line: date events, weather now, her birthday (and tomorrow's, so she can hint).
namespace{const TCHAR* OKPhoneDoingWord(const FString& Kind);} // OKStreetSocialPhoneInbox.inl
FString AOKStreetSocialScene::TodayText(int32 Character) const{
 const auto& S=*State;const int64 Now=OKStreetMindNow(S);const int32 Day=S.Store.CalendarDay;TArray<FString> Parts;
 const FString Holiday=HolidayOn(Day);if(!Holiday.IsEmpty())Parts.Add(Holiday);
 // Places shut all day today (the library on Mondays): she said she was off to the library on a Monday (R3-206).
 {const int32 Weekday=int32(StreetNow().GetDayOfWeek());TArray<FString> Shut;
  for(const auto& Pair:S.Catalog.Places)if(Pair.Value.ClosedWeekdays.Contains(Weekday)){const FOKStreetPlace* P=OKStreetFindPlace(Pair.Key);Shut.Add(P?FString(P->Name):Pair.Key);}
  if(Shut.Num()){Shut.Sort();Parts.Add(OKMsg(TEXT("ui.director.closed_today_line"),{{TEXT("places"),FString::Join(Shut,OKT(TEXT("ui.bank.list_separator")))}}));}}
 Parts.Add(RainingAt(Day,int32(Now%1440))?OKT(TEXT("ui.director.raining_now")):OKT(TEXT("ui.director.not_raining")));
 if(S.Manifest.Characters.IsValidIndex(Character)&&Character>0)if(const FString* Inv=S.HerInvestText.Find(OKStreetStableId(S.Manifest.Characters[Character])))Parts.Add(*Inv);
 {const FString Mine=AcademicText(Character);if(!Mine.IsEmpty())Parts.Add(OKT(TEXT("ui.director.you_are_label"))+Mine);
  // A girl who has only just met him does not know what he studies (unless he says so in the talk, which she then hears).
  const bool Stranger=S.Manifest.Characters.IsValidIndex(Character)&&Character>0&&OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Character]),S.Manifest.TalkBudget).Stage==TEXT("stranger");
  if(Stranger)Parts.Add(OKT(TEXT("ui.director.his_major_unknown")));
  else Parts.Add(OKMsg(TEXT("ui.director.he_is_intelligence_line"), {{TEXT("academic_title"), *AcademicText(0)}, {TEXT("intelligence"), S.Store.PlayerAcademic.Intelligence}}));}
 // School is out (summer, until the term starts): no classes for anyone. She once answered "I have class at noon".
 {EOKStreetTermPhase Phase;FOKStreetTerm Next;FOKStreetTerm Term=OKRulesTermAt(S,StreetNow(),Phase,nullptr,nullptr,&Next);
  if(Phase==EOKStreetTermPhase::Break)Parts.Add(OKMsg(TEXT("ui.director.school_break_line"),{{TEXT("start"),(Next.Key.IsEmpty()?Term:Next).Start.ToString(TEXT("%m/%d"))}}));
  // An exam week has no classes and one sitting, its Friday 17:00 (academics.rs): she once had "management, one paper
  // this morning" on the Wednesday (R3-213).
  else if(Phase==EOKStreetTermPhase::Midterm||Phase==EOKStreetTermPhase::Final){const FDateTime Week=Phase==EOKStreetTermPhase::Midterm?Term.Midterm:Term.Final;
   Parts.Add(OKMsg(TEXT("ui.director.exam_week_line"),{{TEXT("exam"),Phase==EOKStreetTermPhase::Midterm?OKT(TEXT("ui.director.midterm_word")):OKT(TEXT("ui.director.final_word"))},{TEXT("date"),(Week+FTimespan::FromDays(4)).ToString(TEXT("%m/%d"))}}));}}
 // What her own day still holds (the plan she follows): what she says she has to do comes from here.
 if(const auto* Plan=S.Plans.Find(Character)){TArray<FString> Next;const int32 Minute=int32(Now%1440);
  for(const auto& B:*Plan)if(B.End>Minute&&Next.Num()<4&&B.Kind!=TEXT("sleep")){const FOKStreetPlace* P=OKStreetFindPlace(B.Place);
   // On the hour or the quarter as people say it ("快十点"), not the staggered 09:44 she once repeated word for word.
   const int32 Said=(B.Start+7)/15*15;
   const FString Block=OKMsg(TEXT("ui.director.plan_block"),{{TEXT("doing"),OKPhoneDoingWord(B.Kind)},{TEXT("place"),P?FString(P->Name):B.Place}});
   // The block she is in already is now, not at its start: at 10:30 刻晴 said she would go to the library "at 10:15" (R4-2).
   if(B.Start<=Minute){const int32 Until=(B.End+7)/15*15;Next.Add(OKMsg(TEXT("ui.director.plan_now"),{{TEXT("end"),FString::Printf(TEXT("%02d:%02d"),Until/60%24,Until%60)},{TEXT("block"),Block}}));}
   else Next.Add(FString::Printf(TEXT("%02d:%02d %s"),Said/60%24,Said%60,*Block));}
  if(Next.Num())Parts.Add(OKMsg(TEXT("ui.director.your_plan_line"),{{TEXT("plan"),FString::Join(Next,OKT(TEXT("ui.bank.list_separator")))}}));}
 if(BirthdayOf(Character,Day))Parts.Add(OKT(TEXT("ui.director.today_is_birthday")));else if(BirthdayOf(Character,Day+1))Parts.Add(OKT(TEXT("ui.director.tomorrow_birthday_hint")));
 return FString::Join(Parts,OKT(TEXT("ui.bank.list_separator")));
}

// Plan §6.6: three times a day each heroine weighs acting first; the strongest reason wins (social.rs initiative).
// Acting means walking over (or messaging when far) with what she wants on her mind; the words come later.
void AOKStreetSocialScene::TickInitiative(){
 auto& S=*State;if(S.SelfTest||!S.Mind)return;
 const int64 Now=OKStreetMindNow(S);const int32 Minute=int32(Now%1440);
 // Only near a slot change is there anything to weigh (07:00, 12:00, 18:00).
 const int32 Slot=Minute/60>=18?3:Minute/60>=12?2:Minute/60>=7?1:0;if(Slot==0||S.Store.CalendarDay*4+Slot==S.InitiativeSlot)return;
 auto Request=OKRulesRequest(TEXT("initiative"));Request->SetNumberField(TEXT("minute"),Minute);Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);Request->SetNumberField(TEXT("last_slot"),S.InitiativeSlot);
 Request->SetNumberField(TEXT("player_cash"),double(S.Store.PlayerCash)); // she does not come to borrow from a man with nothing
 Request->SetBoolField(TEXT("rain"),RainingAt(S.Store.CalendarDay,Minute)&&PlaceHere(S.Male).IsEmpty());Request->SetStringField(TEXT("holiday"),HolidayOn(S.Store.CalendarDay));
 auto Names=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Rows;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);Names->SetStringField(Id,S.Manifest.Characters[I].Name);
  auto H=MakeShared<FJsonObject>();H->SetNumberField(TEXT("actor"),I);H->SetStringField(TEXT("id"),Id);H->SetObjectField(TEXT("relation"),OKRulesRelationJson(OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget)));
  if(const auto P=ProfileOf(I))H->SetObjectField(TEXT("profile"),P.ToSharedRef());
  H->SetBoolField(TEXT("busy"),BusyWithPlayer(I));H->SetBoolField(TEXT("near"),S.Agents.IsValidIndex(I-1)&&FVector::Dist2D(S.Agents[I-1].Position,S.Male)<3000);H->SetBoolField(TEXT("birthday_tomorrow"),BirthdayOf(I,S.Store.CalendarDay+1));
  Rows.Add(MakeShared<FJsonValueObject>(H));
 }
 Request->SetObjectField(TEXT("names"),Names);Request->SetArrayField(TEXT("heroines"),Rows);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 S.InitiativeSlot=int32(Answer->GetNumberField(TEXT("slot")));
 double Actor=0;if(!Answer->TryGetNumberField(TEXT("actor"),Actor))return;
 const int32 BestI=int32(Actor);const FString Why=Answer->GetStringField(TEXT("why"));
 ApplyRulesEffects(S.Store,*Answer);S.InitiativeReason.Add(BestI,Why);
 if(S.ApproachNext.IsValidIndex(BestI))S.ApproachNext[BestI]=FPlatformTime::Seconds();
 ++S.InitiativesToday;
 UE_LOG(LogTemp,Display,TEXT("Street initiative: actor=%d score=%.0f %s"),BestI,Answer->GetNumberField(TEXT("score")),*Why);
}

// The open-world Director (plan §14): no crises are handed out; it only makes sure he can find out about one
// (social.rs director).
void AOKStreetSocialScene::TickDirector(){
 auto& S=*State;if(S.SelfTest||!S.Mind||S.DirectorDay==S.Store.CalendarDay)return;S.DirectorDay=S.Store.CalendarDay;
 auto Request=OKRulesRequest(TEXT("director"));Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);
 TArray<TSharedPtr<FJsonValue>> Rows;for(int32 I=1;I<S.Manifest.Characters.Num();++I){auto H=MakeShared<FJsonObject>();H->SetStringField(TEXT("id"),OKStreetStableId(S.Manifest.Characters[I]));H->SetStringField(TEXT("name"),S.Manifest.Characters[I].Name);if(const auto P=ProfileOf(I))H->SetObjectField(TEXT("profile"),P.ToSharedRef());Rows.Add(MakeShared<FJsonValueObject>(H));}
 Request->SetArrayField(TEXT("heroines"),Rows);
 TArray<TSharedPtr<FJsonValue>> Known;for(const auto& R:S.Store.Rumors){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("about"),R.About);O->SetStringField(TEXT("kind"),R.Kind);Known.Add(MakeShared<FJsonValueObject>(O));}
 Request->SetArrayField(TEXT("rumors"),Known);
 TArray<TSharedPtr<FJsonValue>> Bonds;for(const auto& Bond:S.Store.BondHeat)Bonds.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueString>(Bond.Key),MakeShared<FJsonValueNumber>(Bond.Value)}));
 Request->SetArrayField(TEXT("bond_heat"),Bonds);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer||Answer->GetArrayField(TEXT("rumors")).IsEmpty())return;
 auto Candidate=S.Store;
 for(const auto& V:Answer->GetArrayField(TEXT("rumors"))){FOKStreetRumor R;OKRulesReadRumor(*V->AsObject(),R);R.Id=Candidate.NextRumor++;Candidate.Rumors.Add(R);UE_LOG(LogTemp,Display,TEXT("Street director: %s's hidden need can now be heard about"),*R.About);}
 CommitStore(MoveTemp(Candidate));
}

// End of day birthday check (plan §11.3): forgetting costs "being seen" (social.rs birthday_end).
void AOKStreetSocialScene::CheckBirthdays(){
 auto& S=*State;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  if(!BirthdayOf(I,S.Store.CalendarDay))continue;
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);
  auto Request=OKRulesRelationRequest(TEXT("birthday_end"),OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget),ProfileOf(I));Request->SetBoolField(TEXT("together"),S.InteractedToday.Contains(I));
  const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)continue;
  auto Candidate=S.Store;ApplyRulesEffects(Candidate,*Answer);CommitStore(MoveTemp(Candidate));
 }
}
