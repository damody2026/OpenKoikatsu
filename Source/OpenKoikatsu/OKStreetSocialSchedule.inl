// Included by OKStreetSocialScene.cpp: plan §6.1–6.3 the heroines' own days — rule-written plans and a small
// behaviour tree per heroine. No LLM call happens here (plan §6.0).

bool AOKStreetSocialScene::BusyWithPlayer(int32 I) const{
 const auto& S=*State;
 return S.Target==I||S.Companion==I||S.OutingGirl==I||S.Pair.Partner==I||(S.Facility!=INDEX_NONE&&S.FacilityCharacter==I)||S.PrivateGirl==I
  ||S.ApproachGirl==I||S.ArriveAfterLift.Contains(I)||S.SleepGirl==I||S.SleepGirl2==I||S.RestroomGirl==I||S.SoloGirl==I||S.PendingIntimacy==I||S.Party.Contains(I)
  ||S.InspectSlot==I; // in the inspection room (OKStreetSocialInspect.inl)
}

// Where she stands in a place: an anchor that suits what she is doing, spread out so heroines do not overlap.
FVector AOKStreetSocialScene::SpotFor(int32 I,const FString& PlaceId,const FString& Kind,float& Yaw) const{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_SpotFor);
 const auto* P=OKStreetFindPlace(PlaceId);Yaw=0;if(!P)return State->Agents[I-1].Position;
 static const TMap<FString,TArray<FString>> Wanted={
  {TEXT("work"),{TEXT("counter"),TEXT("stand"),TEXT("shelf"),TEXT("bar"),TEXT("lobby"),TEXT("food"),TEXT("pets"),TEXT("seat")}},
  {TEXT("meal"),{TEXT("table"),TEXT("food"),TEXT("seat"),TEXT("counter")}},
  {TEXT("class"),{TEXT("seat"),TEXT("class2")}},
  {TEXT("study"),{TEXT("seat"),TEXT("class2"),TEXT("shelf"),TEXT("lounge")}},
  {TEXT("leisure"),{TEXT("seat"),TEXT("bench"),TEXT("shelf"),TEXT("tank"),TEXT("pets"),TEXT("fountain"),TEXT("counter")}},
  {TEXT("encounter"),{TEXT("seat"),TEXT("counter"),TEXT("table")}},
  {TEXT("rest"),{TEXT("lounge"),TEXT("seat")}},{TEXT("sleep"),{TEXT("lounge"),TEXT("seat")}},
  {TEXT("errand"),{TEXT("counter"),TEXT("shelf"),TEXT("food"),TEXT("stand")}},{TEXT("social"),{TEXT("seat"),TEXT("table"),TEXT("bench"),TEXT("counter")}},
  {TEXT("bath"),{TEXT("bath"),TEXT("lounge"),TEXT("seat")}},{TEXT("promise"),{TEXT("seat"),TEXT("table"),TEXT("bench"),TEXT("fountain"),TEXT("counter")}}};
 const FOKStreetModelAnchor* Found=nullptr;
 // Asleep: in a bed of the place (the dorms have rooms upstairs), each heroine in her own, the lower floors first. No bed: the lounge.
 if(Kind==TEXT("sleep")){
  TArray<const FOKStreetModelAnchor*> Beds;for(const auto& A:P->Anchors)if(A.Name.StartsWith(TEXT("bed")))Beds.Add(&A);
  if(!Beds.IsEmpty()){Beds.Sort([](const FOKStreetModelAnchor& X,const FOKStreetModelAnchor& Y){return X.Name<Y.Name;});const auto* Bed=Beds[(I-1)%Beds.Num()];Yaw=Bed->Yaw;return Bed->Position;}
 }
 // Campus classes name their room exactly ("class3_2": floor 3, room 2).
 if(Kind.StartsWith(TEXT("class"))&&Kind.Contains(TEXT("_")))Found=P->Anchors.FindByPredicate([&](const FOKStreetModelAnchor& A){return A.Name==Kind;});
 // Every anchor of the first kind the place has (all its tables, not only the first), less those by him (the table he
 // eats at: three heroines stood pressed behind his chair), spread over the heroines.
 if(!Found)if(const auto* Names=Wanted.Find(Kind.StartsWith(TEXT("class"))?FString(TEXT("class")):Kind))for(const FString& N:*Names){
  // Only anchors inside the place (the cafe's street-side counter left a heroine "on shift" out on the street).
  TArray<const FOKStreetModelAnchor*> All,Free;for(const auto& A:P->Anchors)if(A.Name.StartsWith(N)&&OKStreetInsidePlace(*P,A.Position)){All.Add(&A);if(FVector::Dist2D(A.Position,State->Male)>250.f)Free.Add(&A);}
  const auto& Use=Free.IsEmpty()?All:Free;if(!Use.IsEmpty()){Found=Use[(I-1)%Use.Num()];break;}}
 FVector Spot=Found?Found->Position:(P->Rooms.IsEmpty()?P->Door:P->Rooms[0].GetCenter());
 if(!Found&&!P->Rooms.IsEmpty())Spot.Z=P->Rooms[0].Min.Z;
 Yaw=Found?Found->Yaw:0;
 // The only such spot is where he is (the restaurant has one table anchor, and he eats at it): three metres further
 // from him, inside the room, instead of pressed behind his chair.
 if(Found&&FVector::Dist2D(Spot,State->Male)<250.f&&!P->Rooms.IsEmpty()){
  FVector Away=Spot-State->Male;Away.Z=0;if(!Away.Normalize())Away=FRotator(0,Yaw,0).Vector();
  const FBox& Room=P->Rooms[0];Spot+=Away*300.f;
  Spot.X=FMath::Clamp(Spot.X,Room.Min.X+60.f,FMath::Max(Room.Min.X+60.f,Room.Max.X-60.f));Spot.Y=FMath::Clamp(Spot.Y,Room.Min.Y+60.f,FMath::Max(Room.Min.Y+60.f,Room.Max.Y-60.f));}
 // Side by side in rows of five (65 cm apart), a second row behind for heroines 6..10.
 return Spot+FRotator(0,Yaw,0).RotateVector(FVector(-((I-1)/5)*70.f,(((I-1)%5)-2)*65.f,0));
}

// He sits down with her (a meal, a drink, a seat) where other heroines' plans already stood them: the cafeteria's table
// had five of them in a row around his chair. Each walks to her own spot away from him (SpotFor leaves out the anchors
// by him); one whose only spot is by him stays.
void AOKStreetSocialScene::MakeRoomAround(int32 Partner){
 auto& S=*State;const FString Here=PlaceHere(S.Male);if(Here.IsEmpty())return;
 const int32 Minute=int32(OKStreetMindNow(S)%1440);
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  if(I==Partner||I==S.Companion||!S.ScheduleHeld.Contains(I)||!S.Agents.IsValidIndex(I-1))continue;
  // On her way in, she counts by where she will stop (two came in after he sat down and stood by his chair).
  const auto& A=S.Agents[I-1];const auto* Way=S.Commutes.Find(I);
  const FVector Stop=S.CommuteFinal.Contains(I)?S.CommuteFinal[I]:Way&&!Way->IsEmpty()?Way->Last():A.Position;
  if(FVector::Dist2D(Stop,S.Male)>=250.f)continue;
  const auto* B=BlockNow(I,Minute);if(!B||B->Place!=Here)continue;
  float Yaw=0;const FVector Spot=SpotFor(I,B->Place,B->Kind==TEXT("class")&&!B->Activity.IsEmpty()?B->Activity:B->Kind,Yaw);
  if(FVector::Dist2D(Spot,S.Male)<250.f)continue;
  auto Path=PlaceRoute(A.Position,Spot);if(Path.IsEmpty())continue;
  S.Commutes.Add(I,MoveTemp(Path));S.CommuteFinal.Remove(I);
  UE_LOG(LogTemp,Display,TEXT("Street plan: actor %d makes room at %s (%.0f from him, walks %.0f)"),I,*Here,FVector::Dist2D(A.Position,S.Male),FVector::Dist2D(A.Position,Spot));
 }
}

// The street's plans for the day come from the rules (Tools/ok-rules src/plan.rs plan_all): each heroine's day, her own
// morning and pace, errands, friends meeting, leaving in time, rain, pickups. Here only the facts go in and the plans,
// the best friends' shared morning, pickup appointments and texts come back.
void AOKStreetSocialScene::PlanAll(){
 auto& S=*State;S.PlanDay=S.Store.CalendarDay;S.PlanBlock.Reset();S.PlanEaten.Reset();S.MorningPair={INDEX_NONE,INDEX_NONE};S.LifePairRoute.Reset();
 // His most frequented place (for jealous heroines who "happen" to be there).
 FString Haunt;int32 Most=0;for(const auto& P:S.PlayerPlaceMinutes)if(P.Value>Most){Most=P.Value;Haunt=P.Key;}
 auto Request=OKRulesRequest(TEXT("plan_all"));
 Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);Request->SetNumberField(TEXT("now"),double(OKStreetMindNow(S)));Request->SetStringField(TEXT("haunt"),Haunt);Request->SetNumberField(TEXT("seed"),FMath::Rand());
 TArray<TSharedPtr<FJsonValue>> Heroines;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);
  auto H=MakeShared<FJsonObject>();H->SetNumberField(TEXT("actor"),I);H->SetStringField(TEXT("id"),Id);H->SetStringField(TEXT("name"),S.Manifest.Characters[I].Name);
  const TSharedPtr<FJsonObject> Profile=S.MindProfiles.FindRef(Id);H->SetObjectField(TEXT("profile"),Profile.IsValid()?Profile.ToSharedRef():MakeShared<FJsonObject>());
  H->SetObjectField(TEXT("relation"),OKRulesRelationJson(OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget)));
  H->SetNumberField(TEXT("invest_total"),double(S.HerInvestTotal.FindRef(Id)));H->SetStringField(TEXT("intent"),S.Intents.FindRef(Id));
  if(const auto* A=S.Store.Academics.Find(Id))H->SetObjectField(TEXT("academic"),OKStreetAcademicJson(*A));
  TArray<TSharedPtr<FJsonValue>> Promises;
  for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Heroine==Id&&A.Start/1440==S.Store.CalendarDay){auto B=MakeShared<FJsonObject>();B->SetNumberField(TEXT("start"),double(A.Start%1440));B->SetStringField(TEXT("place"),A.Place);if(A.Minutes>0)B->SetNumberField(TEXT("end"),double(A.Start%1440+A.Minutes));Promises.Add(MakeShared<FJsonValueObject>(B));}
  H->SetArrayField(TEXT("promises"),Promises);
  for(const auto& E:S.Store.Company.Staff)if(E.Heroine==Id){H->SetStringField(TEXT("company_place"),CompanyHqPlace());H->SetStringField(TEXT("company_role"),E.Role);}
  {const FString Own=HeroCompanyPlace(I);if(!Own.IsEmpty()){H->SetStringField(TEXT("company_place"),Own);H->SetStringField(TEXT("company_role"),TEXT("owner"));}} // her own company
  Heroines.Add(MakeShared<FJsonValueObject>(H));
 }
 Request->SetArrayField(TEXT("heroines"),Heroines);
 TArray<TSharedPtr<FJsonValue>> Bonds;for(const auto& Bond:S.Store.BondHeat)Bonds.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueString>(Bond.Key),MakeShared<FJsonValueNumber>(Bond.Value)}));
 Request->SetArrayField(TEXT("bond_heat"),Bonds);
 Request->SetObjectField(TEXT("doors"),OKRulesDoors());
 {auto Names=MakeShared<FJsonObject>();for(const auto& P:OKStreetPlaces())Names->SetStringField(P.Id,P.Name);Request->SetObjectField(TEXT("place_names"),Names);}
 // His meetings still ahead: a pickup is not asked for on top of one (plan.rs).
 {TArray<TSharedPtr<FJsonValue>> Meet;for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")||A.Status==TEXT("asked"))Meet.Add(MakeShared<FJsonValueNumber>(double(A.Start)));Request->SetArrayField(TEXT("his_meetings"),Meet);}
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 const TSharedPtr<FJsonObject>* Plans=nullptr;
 if(Answer->TryGetObjectField(TEXT("plans"),Plans))for(const auto& P:(*Plans)->Values){
  TArray<FOKStreetPlanBlock> Day;for(const auto& V:P.Value->AsArray())if(V->AsObject())Day.Add(OKRulesReadBlock(*V->AsObject()));
  S.Plans.Add(FCString::Atoi(*FString(*P.Key)),MoveTemp(Day));
 }
 const TArray<TSharedPtr<FJsonValue>>* Pair=nullptr;
 if(Answer->TryGetArrayField(TEXT("morning_pair"),Pair)&&Pair->Num()==2)S.MorningPair={int32((*Pair)[0]->AsNumber()),int32((*Pair)[1]->AsNumber())}; // walking side by side (OKStreetSocialLife.inl)
 const TArray<TSharedPtr<FJsonValue>>* Pickups=nullptr;
 if(Answer->TryGetArrayField(TEXT("pickups"),Pickups))for(const auto& V:*Pickups){const auto O=V->AsObject();if(!O)continue;
  AddAppointment(TEXT("pickup"),int32(O->GetNumberField(TEXT("actor"))),O->GetStringField(TEXT("place")),int64(O->GetNumberField(TEXT("start"))),int32(O->GetNumberField(TEXT("grace"))),O->GetStringField(TEXT("label")),0,TEXT("asked"));} // her request: a meeting once he says yes
 ApplyRulesEffects(S.Store,*Answer);
}

const FOKStreetPlanBlock* AOKStreetSocialScene::BlockNow(int32 I,int32 Minute,int32* Index) const{
 const auto* Plan=State->Plans.Find(I);if(!Plan)return nullptr;
 for(int32 B=0;B<Plan->Num();++B)if(Minute>=(*Plan)[B].Start&&Minute<(*Plan)[B].End){if(Index)*Index=B;return &(*Plan)[B];}
 return nullptr;
}

bool AOKStreetSocialScene::MindProfilesReady() const{
 const auto& S=*State;for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(!S.MindProfiles.Contains(OKStreetStableId(S.Manifest.Characters[I])))return false;return true;
}

void AOKStreetSocialScene::TickSchedules(){
 auto& S=*State;if(S.SelfTest||!S.Mind)return;
 const double Wall=FPlatformTime::Seconds();if(Wall<S.ScheduleTickAt)return;S.ScheduleTickAt=Wall+0.5;
 // The day is planned from the heroines' profiles (student: classes, the cafeteria at noon), which the mind sends a
 // moment after the map starts: planned before they came, every heroine had a day of "leisure street" and lunch in
 // the department store, all ten at once, the whole day after a load. Wait for them (30 s at most).
 if(S.PlanDay!=S.Store.CalendarDay){
  const bool Ready=MindProfilesReady();
  if(!Ready&&S.PlanWaitSince==0)S.PlanWaitSince=Wall;
  if(!Ready&&Wall-S.PlanWaitSince<30)return;
  if(!Ready)UE_LOG(LogTemp,Warning,TEXT("Street plan: profiles still missing after 30 s; planning without them"));
  S.PlanWaitSince=0;S.PlannedWithoutProfiles=!Ready;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_SchedulePlanAll);PlanAll();}
 }
 // Walking with him when the game was saved: once both are drawn, the walk goes on (R3-238).
 if(S.ResumeFollow!=INDEX_NONE){
  if(S.Companion==INDEX_NONE)BeginCompanion(S.ResumeFollow);
  if(S.Companion==S.ResumeFollow||Wall>S.ResumeFollowUntil){UE_LOG(LogTemp,Display,TEXT("Street load: actor %d %s"),S.ResumeFollow,S.Companion==S.ResumeFollow?TEXT("walks with him again"):TEXT("could not walk with him again; waits beside him"));
   if(S.Companion==S.ResumeFollow)S.WaitsForHim.Remove(S.ResumeFollow);S.ResumeFollow=INDEX_NONE;}}
 // Sitting with her: whoever comes to stand by his table moves on (R3-140).
 if(S.Pair.Partner!=INDEX_NONE&&S.Pair.Conversation())MakeRoomAround(S.Pair.Partner);
 const int64 Now=OKStreetMindNow(S);const int32 Minute=int32(Now%1440);
 if(Now!=S.PlaceCountMinute){S.PlaceCountMinute=Now;const FString Here=PlaceHere(S.Male);if(!Here.IsEmpty())++S.PlayerPlaceMinutes.FindOrAdd(Here);
  // Walked into another place: its admission, if it has one (shop.rs entry_fee).
  // Where a game or a save starts him is free; elsewhere the rules ask each minute until they settle it (passing the door is not going in).
  if(Here!=S.EntryPlace){S.EntryPlace=Here;S.EntryMinutes=0;S.bEntrySettled=Here.IsEmpty()||!S.bEntryPlaceKnown;}else ++S.EntryMinutes;
  // She joined him inside (he bought his ticket alone, she met him in the hall): asked again, and the rules charge
  // only the ticket not bought yet today (R3-110).
  const int32 WithHer=S.Companion!=INDEX_NONE?S.Companion:S.OutingGirl;
  if(S.bEntrySettled&&!Here.IsEmpty()&&S.bEntryPlaceKnown&&WithHer>0&&WithHer!=S.EntryHer)S.bEntrySettled=false;
  if(!S.bEntrySettled)S.bEntrySettled=PayEntry(Here,S.EntryMinutes);
  if(S.bEntrySettled)S.EntryHer=WithHer;S.bEntryPlaceKnown=true;}
 // Urgent needs (hungry, worn out, grimy) cut in before the plan: the rules decide (plan.rs schedule_tick).
 TMap<int32,TPair<int32,FOKStreetPlanBlock>> Urgents;
 {
  auto Request=OKRulesRequest(TEXT("schedule_tick"));Request->SetNumberField(TEXT("minute"),Minute);
  {FString Why;Request->SetBoolField(TEXT("cafeteria_open"),!PlaceClosedNow(TEXT("cafeteria"),Why));}
  TArray<TSharedPtr<FJsonValue>> Rows;
  for(int32 I=1;I<S.Manifest.Characters.Num();++I){
   if(!S.Agents.IsValidIndex(I-1)||BusyWithPlayer(I))continue;
   const FString Id=OKStreetStableId(S.Manifest.Characters[I]);const FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
   auto H=MakeShared<FJsonObject>();H->SetNumberField(TEXT("actor"),I);H->SetNumberField(TEXT("satiety"),R.Satiety);H->SetNumberField(TEXT("energy"),R.Energy);H->SetNumberField(TEXT("hygiene"),R.Hygiene);
   if(const FOKStreetPlanBlock* B=BlockNow(I,Minute))H->SetObjectField(TEXT("block"),OKRulesBlockJson(*B));
   H->SetNumberField(TEXT("previous"),S.PlanBlock.FindRef(I,INDEX_NONE));H->SetBoolField(TEXT("bathing"),S.LifeUrgent.FindRef(I)==TEXT("bath"));
   H->SetStringField(TEXT("home"),OKStreetProfileString(S.MindProfiles.FindRef(Id),nullptr,TEXT("home_place_id")));
   Rows.Add(MakeShared<FJsonValueObject>(H));
  }
  Request->SetArrayField(TEXT("heroines"),Rows);
  TSharedPtr<FJsonObject> Answer;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_ScheduleRules);Answer=OKRulesEval(S,Request);}
  if(Answer)for(const auto& V:Answer->GetArrayField(TEXT("heroines"))){
   const auto O=V->AsObject();if(!O)continue;const int32 I=int32(O->GetNumberField(TEXT("actor")));
   if(O->GetBoolField(TEXT("bathing")))S.LifeUrgent.Add(I,TEXT("bath"));else if(S.LifeUrgent.FindRef(I)==TEXT("bath"))S.LifeUrgent.Remove(I);
   const TSharedPtr<FJsonObject>* U=nullptr;if(!O->TryGetObjectField(TEXT("urgent"),U))continue;
   Urgents.Add(I,{int32((*U)->GetNumberField(TEXT("index"))),OKRulesReadBlock(*(*U)->GetObjectField(TEXT("block")))});
   bool Starts=false;if((*U)->TryGetBoolField(TEXT("starts"),Starts)&&Starts){UE_LOG(LogTemp,Display,TEXT("Street life: actor %d goes home for a bath"),I);}
  }
 }
 // Block changes are spread over frames: each costs a route (a grid or waypoint search with sweeps) and a memory, and
 // a clock jump or the hour changed all ten heroines in one frame (25-34 ms). Once this tick has spent its share, the
 // rest change on the next frame (the tick then runs again right away).
 const double ChangeBudget=FPlatformTime::Seconds()+.002;int32 Changes=0;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  if(!S.Agents.IsValidIndex(I-1)||BusyWithPlayer(I))continue;
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);
  const int32 Rel=OKStreetRelationIndex(S.Store,Id,S.Manifest.TalkBudget,true);if(!S.Store.Relations.IsValidIndex(Rel))continue;
  auto& R=S.Store.Relations[Rel];
  int32 Index=INDEX_NONE;const FOKStreetPlanBlock* Block=BlockNow(I,Minute,&Index);
  FOKStreetPlanBlock Urgent;
  if(const auto* U=Urgents.Find(I)){Urgent=U->Value;Block=&Urgent;Index=U->Key;}
  const int32 Previous=S.PlanBlock.FindRef(I,INDEX_NONE);
  // Nothing recorded yet: the day (or the game) just started, she is put where her block is. A gap in her plan
  // (INDEX_NONE recorded: no block then) is not a start; from it she walks on like from any block.
  const bool Fresh=!S.PlanBlock.Contains(I);
  // She was with him when the game was saved: back where she was, not where her plan puts her (R3-238). Walking or out
  // with him she follows him again; otherwise she waits there for him a while (the rules: places.rs resume_with_him).
  if(Fresh&&!S.ResumeWithHim.IsEmpty())if(const FOKStreetStore::FWithHim* W=S.ResumeWithHim.Find(Id)){
   auto Ask=OKRulesRequest(TEXT("resume_with_him"));Ask->SetStringField(TEXT("mode"),W->Mode);Ask->SetBoolField(TEXT("fixed"),OKStreetFixedBlock(Block));
   const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Ask);
   if(Out&&Out->GetBoolField(TEXT("keep"))){auto& A=S.Agents[I-1];
    A.Position=W->Spot;S.Rotations[I]=FRotator(0,W->Yaw,0);A.Held=true;S.ScheduleHeld.Add(I);S.PendingMoves.Remove(I);S.Commutes.Remove(I);
    S.PlanBlock.Add(I,Index);S.PlanBlockSince.Add(I,Minute);S.WaitsForHim.Add(I,Now+int64(Out->GetNumberField(TEXT("wait_minutes"))));
    if(Out->GetBoolField(TEXT("follow"))){S.ResumeFollow=I;S.ResumeFollowUntil=FPlatformTime::Seconds()+30;}
    UE_LOG(LogTemp,Display,TEXT("Street load: actor %d was with him (%s), put back at %s"),I,*W->Mode,*W->Spot.ToCompactString());
    S.ResumeWithHim.Remove(Id);continue;}
   UE_LOG(LogTemp,Display,TEXT("Street load: actor %d was with him (%s), her %s block takes her"),I,*W->Mode,Block?*Block->Kind:TEXT("-"));
   S.ResumeWithHim.Remove(Id);}
  if((Fresh||Index!=Previous)&&Changes>0&&FPlatformTime::Seconds()>ChangeBudget){S.ScheduleTickAt=0;continue;} // her change waits for the next frame
  // She wrote him where she is ("at the bookstore out of the rain, a bit bored"): she waits there a while for him, not
  // off home nine seconds later (R3-230); a class, shift, promise or the night still takes her.
  if(!Fresh&&Index!=Previous&&S.WaitsForHim.FindRef(I,0)>OKStreetMindNow(S)&&!OKStreetFixedBlock(Block)){
   static TMap<int32,int32> Told;if(Told.FindRef(I,-2)!=Index){Told.Add(I,Index);UE_LOG(LogTemp,Display,TEXT("Street plan: actor %d stays where she wrote him from (%s waits) until %02d:%02d"),I,Block?*Block->Kind:TEXT("-"),int32(S.WaitsForHim[I]%1440)/60,int32(S.WaitsForHim[I]%60));}
   continue;}
  if(Fresh||Index!=Previous){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_ScheduleBlockChange);++Changes;
   // Leaving a work block: her wage for the hours.
   if(const auto* Plan=S.Plans.Find(I))if(Plan->IsValidIndex(Previous)&&(*Plan)[Previous].Kind==TEXT("work")){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_BlockPay);
    const auto& W=(*Plan)[Previous];const int32 Wage=HerWage(W);const int32 Pay=HerShiftPay(I,W,Wage,Minute);
    {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_BlockPayNet);if(R.Cash>=0)R.Cash+=OKRulesNetPay(S,Pay,float(Pay)/float(FMath::Max(1,Wage))/176.f);}
    QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_BlockPayObserve);MindObserve(I,TEXT("shared_shift"),OKMsg(TEXT("ui.schedule.worked_part_time"), {{TEXT("place"), OKStreetFindPlace(W.Place)?OKStreetFindPlace(W.Place)->Name:*W.Place}, {TEXT("hours"), (W.End-W.Start)/60}, {TEXT("pay"), Pay}}),W.Place,0.2f);}
   S.PlanBlock.Add(I,Index);S.PlanBlockSince.Add(I,Minute);S.PlanEaten.Remove(I);
   auto& A=S.Agents[I-1];
   // Nowhere in particular (the street): let go to roam. Inside a place she first goes out of its door (she stayed in the
   // closed library and the restaurant for hours, roaming inside them).
   if(!Block||Block->Place.IsEmpty()){
    const FOKStreetPlace* In=OKStreetFindPlace(PlaceHere(A.Position));
    if(In&&!In->Rooms.IsEmpty()&&S.ScheduleHeld.Contains(I)){
     if(FVector::Dist2D(A.Position,S.Male)<4000){QueueHeroineWalk(I,A.Position,In->Door,true,false,FVector::ZeroVector,In->Door,0,In->Id);continue;} // routed a slice a frame
     A.Position=In->Door;S.PendingMoves.Remove(I);}
    if(S.ScheduleHeld.Contains(I)){A.Held=false;S.ScheduleHeld.Remove(I);S.Commutes.Remove(I);}S.PendingMoves.Remove(I);continue;}
   FString Why;FOKStreetPlanBlock Alt;
   QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_BlockPlace);
   if(PlaceClosedNow(Block->Place,Why)&&Block->Kind!=TEXT("sleep")&&Block->Kind!=TEXT("rest")){ // closed: the nearest open place like it, else home (plan.rs plan_alternative)
    auto Request=OKRulesRequest(TEXT("plan_alternative"));Request->SetObjectField(TEXT("block"),OKRulesBlockJson(*Block));Request->SetObjectField(TEXT("doors"),OKRulesDoors());
    Request->SetArrayField(TEXT("from"),{MakeShared<FJsonValueNumber>(A.Position.X),MakeShared<FJsonValueNumber>(A.Position.Y)});
    {const FDateTime T=StreetNow();Request->SetNumberField(TEXT("hours"),T.GetHour()+T.GetMinute()/60.0);Request->SetNumberField(TEXT("weekday"),int32(T.GetDayOfWeek()));}
    Request->SetStringField(TEXT("home"),OKStreetProfileString(S.MindProfiles.FindRef(Id),nullptr,TEXT("home_place_id")));
    TSharedPtr<FJsonObject> Answer;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_PlanAlternative);Answer=OKRulesEval(S,Request);}if(!Answer)continue;Alt=OKRulesReadBlock(*Answer->GetObjectField(TEXT("block")));
    UE_LOG(LogTemp,Display,TEXT("Street plan: actor %d finds %s closed, goes to %s (%s) instead"),I,*Block->Place,*Alt.Place,*Alt.Kind);
    if(auto* Plan=S.Plans.Find(I);Plan&&Plan->IsValidIndex(Index)){(*Plan)[Index]=Alt;Block=&(*Plan)[Index];}else Block=&Alt;}
   // Her own day, block by block, is hers to remember (with its time); meal, work, errand and social write their own.
   if(Block->Kind!=TEXT("meal")&&Block->Kind!=TEXT("work")&&Block->Kind!=TEXT("errand")&&Block->Kind!=TEXT("social")){
    const FOKStreetPlace* P=OKStreetFindPlace(Block->Place);
    MindRemember(I,TEXT("life"),OKMsg(TEXT("ui.memory.life_block"),{{TEXT("kind"),Block->Kind},{TEXT("place"),P?FString(P->Name):Block->Place},{TEXT("activity"),Block->Activity},
     {TEXT("has_activity"),Block->Activity.IsEmpty()||Block->Kind!=TEXT("class")?TEXT("no"):TEXT("yes")},{TEXT("start"),FString::Printf(TEXT("%02d:%02d"),Block->Start/60%24,Block->Start%60)},{TEXT("end"),FString::Printf(TEXT("%02d:%02d"),Block->End/60%24,Block->End%60)}}),Block->Place,0.1f);}
   float Yaw=0;const FVector Spot=SpotFor(I,Block->Place,Block->Kind==TEXT("class")&&!Block->Activity.IsEmpty()?Block->Activity:Block->Kind,Yaw);
   UE_LOG(LogTemp,Display,TEXT("Street plan: actor %d %02d:%02d %s at %s (%s, %.0f from him)"),I,Minute/60,Minute%60,*Block->Kind,*Block->Place,!Fresh&&FVector::Dist2D(A.Position,S.Male)<4000?TEXT("walks"):TEXT("placed"),FVector::Dist2D(A.Position,S.Male));
   A.Held=true;S.ScheduleHeld.Add(I);
   // Seen by him: she walks there. Out of sight: she is simply there (plan §6.3).
   // An upper floor: she walks to the building's door, then goes up (no walking through floors).
   const auto* Dest=OKStreetFindPlace(Block->Place);const bool Upstairs=Spot.Z>100&&Dest;
   // The day's first block (the game just started) puts her there directly too: no crowd leaving at once.
   if(!Fresh&&FVector::Dist2D(A.Position,S.Male)<4000){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_BlockQueueWalk);QueueHeroineWalk(I,A.Position,Upstairs?Dest->Door:Spot,false,Upstairs,Spot,Spot,Yaw,Block->Place);continue;} // routed a slice a frame
   S.PendingMoves.Remove(I);
   A.Position=Spot;S.Rotations[I]=FRotator(0,Yaw,0);S.Commutes.Remove(I);
   if(Block->Kind==TEXT("class")||Block->Kind==TEXT("study"))UE_LOG(LogTemp,Display,TEXT("Street %s: actor %d at %s %s z=%.0f"),*Block->Kind,I,*Block->Place,*Block->Activity,Spot.Z);
  }
  // Walking along her route: advanced every frame by TickCommutes (a 0.5 s step here made her walk-stop-walk).
  if(S.Commutes.Contains(I)||S.PendingMoves.Contains(I))continue; // still walking there, or her way still being routed
  // At a meal: she orders what she likes within her budget and pays for it (plan §6.2 自主消費).
  if(Block&&Block->Kind==TEXT("meal")&&!S.PlanEaten.Contains(I)&&!Block->Place.IsEmpty()){
   S.PlanEaten.Add(I);
   auto Request=OKRulesRelationRequest(TEXT("plan_meal"),R,S.MindProfiles.FindRef(Id));Request->SetStringField(TEXT("place"),Block->Place);Request->SetNumberField(TEXT("month"),StreetNow().GetMonth());
   Request->SetNumberField(TEXT("budget"),Block->Budget);Request->SetNumberField(TEXT("seed"),int32(Now)+I);
   if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);Answer&&Answer->HasField(TEXT("item"))){
    OKRulesPatchRelation(R,*Answer->GetObjectField(TEXT("relation")));
    MindObserve(I,TEXT("meal"),OKMsg(TEXT("ui.schedule.ate_at_place"), {{TEXT("place"), OKStreetFindPlace(Block->Place)?OKStreetFindPlace(Block->Place)->Name:*Block->Place}, {TEXT("food"), *Answer->GetStringField(TEXT("name"))}, {TEXT("price"), int32(Answer->GetNumberField(TEXT("price")))}}),Block->Place,0.15f);
   }
  }
 }
}

// For the stats panel: today's plan with the current block marked.
FString AOKStreetSocialScene::PlanText(int32 I) const{
 const auto* Plan=State->Plans.Find(I);if(!Plan)return FString();
 const int32 Minute=int32(OKStreetMindNow(*State)%1440);TArray<FString> Parts;
 for(const auto& B:*Plan){const auto* P=OKStreetFindPlace(B.Place);Parts.Add(FString::Printf(TEXT("%s%02d:%02d %s@%s"),Minute>=B.Start&&Minute<B.End?TEXT("▶"):TEXT(""),B.Start/60,B.Start%60,*B.Kind,P?P->Name:OKT(TEXT("ui.etiquette.street_location"))));}
 return OKT(TEXT("ui.schedule.today_schedule_label"))+FString::Join(Parts,OKT(TEXT("ui.bank.list_separator")));
}

// Commutes move every frame at walking speed, so she walks continuously from place to place.
// A heroine's walk: on the same floor its grid route is searched a slice a frame (TickRouteJobs), so ten heroines changing
// blocks at once, or one route through streets not searched before (a physics test per new cell), no longer hold a frame
// 20-37 ms. Another floor takes the waypoint graph at once (PlaceRoute does the same). Asked again before it is done,
// the newer walk replaces the older.
// Destroying a prop actor costs ~1.6 ms (its components unregistered, render and physics state torn down): six props of
// a pose ending in one frame held it 10 ms. Hidden at once, they go a few a frame, within 1 ms.
void AOKStreetSocialScene::DestroyLater(AActor* Actor){
 if(!IsValid(Actor))return;
 Actor->SetActorHiddenInGame(true);Actor->SetActorEnableCollision(false);Actor->SetActorTickEnabled(false);
 if(State)State->DestroyQueue.Add(Actor);else Actor->Destroy();
}
void AOKStreetSocialScene::TickDestroyQueue(){
 if(!State)return;auto& Q=State->DestroyQueue;const double Deadline=FPlatformTime::Seconds()+0.001;
 while(!Q.IsEmpty()){AActor* Actor=Q[0].Get();Q.RemoveAt(0);
  // One that took long (6 props took 10 ms before this queue; one alone took 10 ms in Sched34E): which, and how big.
  if(IsValid(Actor)){const double T0=FPlatformTime::Seconds();const FString Name=Actor->GetName();const int32 Parts=Actor->GetComponents().Num();Actor->Destroy();
   const double Ms=(FPlatformTime::Seconds()-T0)*1000.0;if(Ms>3.0)UE_LOG(LogTemp,Display,TEXT("Street destroy slow: %s (%d components) %.1f ms"),*Name,Parts,Ms);}
  if(FPlatformTime::Seconds()>Deadline)break;}
}
void AOKStreetSocialScene::QueueHeroineWalk(int32 I,const FVector& From,const FVector& To,bool Leave,bool HasFinal,const FVector& Final,const FVector& Fallback,float Yaw,const FString& Place){
 auto& S=*State;FOKStreetPendingMove Move;Move.Leave=Leave;Move.HasFinal=HasFinal;Move.Final=Final;Move.Fallback=Fallback;Move.Yaw=Yaw;Move.Place=Place;
 S.PendingMoves.Remove(I);
 if(FMath::Abs(From.Z-To.Z)>=150.f){FinishHeroineWalk(I,Move,PlaceRouteWaypoints(From,To));return;}
 Move.Search=MakeShared<FOKStreetGridSearch>();Move.Search->From=From;Move.Search->To=To;
 S.PendingMoves.Add(I,MoveTemp(Move));
}
// What a found (or missing) route does, as the schedule did when it searched in place.
void AOKStreetSocialScene::FinishHeroineWalk(int32 I,const FOKStreetPendingMove& Move,TArray<FVector>&& Path){
 auto& S=*State;if(!S.Agents.IsValidIndex(I-1))return;auto& A=S.Agents[I-1];
 // Taken by him (a talk, a call, an elevator) while it was being routed: the walk is dropped like a commute.
 if(BusyWithPlayer(I)||S.LiftWalks.Contains(I)||S.LiftPins.Contains(I))return;
 if(Path.Num()>=2){
  S.Commutes.Add(I,MoveTemp(Path));
  if(Move.Leave){S.CommuteFinal.Remove(I);S.ReleaseAtDoor.Add(I);UE_LOG(LogTemp,Display,TEXT("Street plan: actor %d leaves %s for the street"),I,*Move.Place);}
  else if(Move.HasFinal)S.CommuteFinal.Add(I,Move.Final);else S.CommuteFinal.Remove(I);
  return;
 }
 if(Move.Leave){A.Position=Move.Fallback;if(S.ScheduleHeld.Contains(I)){A.Held=false;S.ScheduleHeld.Remove(I);S.Commutes.Remove(I);}
  else A.Held=false; // off her street walk inside a place (Tick): at the door, the street walk again
  return;}
 A.Position=Move.Fallback;S.Rotations[I]=FRotator(0,Move.Yaw,0);S.Commutes.Remove(I);
}
// The pending walks' route searches, 2 ms a frame between them; each that is done starts its walk.
void AOKStreetSocialScene::TickRouteJobs(){
 auto& S=*State;if(S.PendingMoves.IsEmpty())return;
 const double Deadline=FPlatformTime::Seconds()+.002;TArray<int32> Done;
 for(auto& Pair:S.PendingMoves){
  if(FPlatformTime::Seconds()>Deadline)break;
  auto& Search=*Pair.Value.Search;
  if(!GridSearchStep(Search,Deadline))continue;
  Done.Add(Pair.Key);
 }
 for(const int32 I:Done){
  FOKStreetPendingMove Move=S.PendingMoves.FindAndRemoveChecked(I);
  TArray<FVector> Path=MoveTemp(Move.Search->Route);
  // No way on the grid: the waypoint graph, as PlaceRoute falls back to.
  if(Path.Num()<2)Path=PlaceRouteWaypoints(Move.Search->From,Move.Search->To);
  FinishHeroineWalk(I,Move,MoveTemp(Path));
 }
}

void AOKStreetSocialScene::TickCommutes(float Dt){
 auto& S=*State;Dt=FMath::Min(Dt,.05f);
 TArray<int32> Done;
 for(auto& Pair:S.Commutes){
  const int32 I=Pair.Key;TArray<FVector>& Path=Pair.Value;if(!S.Agents.IsValidIndex(I-1)){Done.Add(I);continue;}
  auto& A=S.Agents[I-1];
  // She noticed him coming up to her: she waits a moment before walking on.
  if(const double* Until=S.CommutePausedUntil.Find(I)){if(SceneTime()<*Until)continue;S.CommutePausedUntil.Remove(I);}
  while(Path.Num()>1&&FVector::Dist2D(A.Position,Path[1])<15)Path.RemoveAt(0);
  if(Path.Num()<2){Done.Add(I);continue;}
  const FVector Delta=Path[1]-A.Position;const FVector Step=Delta.GetSafeNormal2D()*FMath::Min(float(Delta.Size2D()),OKStreetWalkSpeed*Dt);
  A.Position+=Step;A.Position.Z=Path[1].Z;S.Rotations[I]=OKStreetFacingMovement(Step,S.Rotations[I]);
 }
 for(const int32 I:Done){S.Commutes.Remove(I);if(S.ReleaseAtDoor.Remove(I)&&S.Agents.IsValidIndex(I-1)){S.Agents[I-1].Held=false;S.ScheduleHeld.Remove(I);}if(const FVector* F=S.CommuteFinal.Find(I)){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_CommuteLift);if(S.Agents.IsValidIndex(I-1)&&!StartNpcLift(I,*F))S.Agents[I-1].Position=*F;S.CommuteFinal.Remove(I);}} // seen: up by lift (Life.inl)
}
