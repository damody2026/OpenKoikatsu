// Included by OKStreetSocialScene.cpp: the 2010 calendar (plan §5) and the in-process heroine mind (plan §3).

// Called once the relationship save is loaded. A saved game continues at its saved minute; a new game starts at
// 08:00 on 2010-09-01. -StreetSocialClock= still overrides the time of day (self-tests and captures).
void AOKStreetSocialScene::StartCalendarAndMind(){
 auto& S=*State;
 // The game rules (ok_rules.dll) are required: without them the error is shown and the game quits.
 {S.Rules=MakeUnique<FOKStreetRules>();FString Error;
  if(S.Rules->Open(Error)){OKStreetSetActiveRules(S.Rules.Get());OKRulesSyncDefaults(S);
   UE_LOG(LogTemp,Display,TEXT("StreetSocial rules: %s (tuning %s)"),*S.Rules->DllPath(),*FOKStreetRules::TuningDir());}
  else{S.Rules.Reset();OKStreetRulesFatal(Error);return;}}
 LoadCatalog();
 // A saved game continues where he was (the start place below, a development switch, still wins).
 if(S.Store.HasPlayerSpot&&!S.SelfTest){S.Male=S.Store.PlayerSpot;if(S.Rotations.IsValidIndex(0))S.Rotations[0]=FRotator(0,S.Store.PlayerYaw,0);S.Yaw=S.Store.ViewYaw;S.SpotSavedMale=S.Male;
  UE_LOG(LogTemp,Display,TEXT("Street start: where he was saved, %s yaw %.0f"),*S.Male.ToCompactString(),S.Store.PlayerYaw);
  for(const auto& W:S.Store.WithHim)S.ResumeWithHim.Add(W.Id,W); // put back by TickSchedules once her plan is there
  // Their table as it was (a dish still being made is on it now): eaten on when he talks with her there (TickMeal).
  if(!S.Store.Table.IsEmpty()&&!S.Store.TablePlace.IsEmpty()){S.MealPlace=S.Store.TablePlace;
   for(const auto& T:S.Store.Table){FOKStreetDish D;D.ItemId=T.ItemId;D.Name=T.Name;D.ForHer=T.ForHer;D.Percent=T.Percent;D.Served=true;D.Her=T.Her.IsEmpty()?INDEX_NONE:CharacterOf(T.Her);S.Table.Add(D);}
   // Those dishes were ordered and paid there: the meal's end does not bill a set from the menu again (R4-8: ¥21 more
   // after a load, the rice and the pork chop already paid at the counter).
   if(S.Store.CalendarMinute>=0)S.OrderedAtPlace.Add(S.Store.TablePlace,int64(S.Store.CalendarDay)*1440+S.Store.CalendarMinute);
   UE_LOG(LogTemp,Display,TEXT("Street load: %d dishes back on the table at %s"),S.Table.Num(),*S.MealPlace);}
  // The basket as it was: its bill comes again once he talks with her there (TickSchedules).
  if(!S.Store.Cart.IsEmpty()){for(const auto& C:S.Store.Cart){FOKStreetCartLine L;L.ItemId=C.ItemId;L.Size=C.Size;L.Sweetness=C.Sweetness;L.Ice=C.Ice;L.ForHer=C.ForHer;L.HerOwn=C.HerOwn;L.Girl=C.Girl.IsEmpty()?INDEX_NONE:CharacterOf(C.Girl);S.Cart.Add(L);}
   S.ResumeCartPlace=S.Store.CartPlace;S.ResumeCartHer=S.Store.CartHer.IsEmpty()?INDEX_NONE:CharacterOf(S.Store.CartHer);
   UE_LOG(LogTemp,Display,TEXT("Street load: %d basket lines back at %s, the bill comes with her"),S.Cart.Num(),*S.ResumeCartPlace);}}
 // Development: OK_STREET_START_PLACE=<place id> starts the player inside that place (first anchor, else room, else door).
 {FString Start=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STREET_START_PLACE")),Anchor;Start.Split(TEXT("@"),&Start,&Anchor);if(Start.IsEmpty())Start=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STREET_START_PLACE"));
  if(!Start.IsEmpty()&&!S.SelfTest)if(const auto* P=OKStreetFindPlace(Start)){const FOKStreetModelAnchor* A=Anchor.IsEmpty()?(P->Anchors.Num()?&P->Anchors[0]:nullptr):P->Anchors.FindByPredicate([&](const FOKStreetModelAnchor& X){return X.Name==Anchor;});
   S.Male=A?A->Position:P->Rooms.Num()?P->Rooms[0].GetCenter():P->Door;if(!A)S.Male.Z=0;UE_LOG(LogTemp,Display,TEXT("Street start place: %s %s"),*Start,A?*A->Name:TEXT("(room)"));}}
 {FString Body;TSharedPtr<FJsonObject> Player;double Base=50;const FString PlayerFile=FPaths::GetPath(S.Manifest.SavePath)/TEXT("player.json");
  if(FFileHelper::LoadFileToString(Body,*PlayerFile)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),Player)&&Player.IsValid()){
   if(Player->TryGetNumberField(TEXT("appearance_base"),Base))S.PlayerLookBase=FMath::Clamp(float(Base),0.f,100.f);
   FString Mode;if(Player->TryGetStringField(TEXT("drama_mode"),Mode)&&(Mode==TEXT("A")||Mode==TEXT("B")||Mode==TEXT("C")))S.DramaMode=Mode[0];
   FString Locale;if(Player->TryGetStringField(TEXT("locale"),Locale)&&(Locale==TEXT("zh-TW")||Locale==TEXT("zh-CN")||Locale==TEXT("en")))S.Lang=Locale;
   // Before any character loads: their materials follow it (SetRenderStyle).
   FString Style;if(Player->TryGetStringField(TEXT("render_style"),Style))ApplyRenderStyleSetting(Style);}}
 LoadUiTexts();
 // Development: OK_STREET_START_DAY=YYYY-MM-DD and OK_STREET_START_CLOCK=HH:MM start the save on that day and time
 // (e.g. a class day on campus). They move the save's calendar, so use them on a test save.
 if(!S.SelfTest){
  const FString Day=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STREET_START_DAY")),At=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STREET_START_CLOCK"));
  TArray<FString> P;Day.ParseIntoArray(P,TEXT("-"));
  if(P.Num()==3){const FDateTime D(FCString::Atoi(*P[0]),FCString::Atoi(*P[1]),FCString::Atoi(*P[2]));S.Store.CalendarDay=FMath::Max(0,int32((D-OKStreetCalendarEpoch()).GetDays()));
   // A jump is not days without food or sleep: start everyone rested, not decayed over the skipped days.
   for(auto& R:S.Store.Relations){R.NeedsMinute=-1;R.Energy=FMath::Max(R.Energy,95.f);R.Satiety=FMath::Max(R.Satiety,70.f);R.Hygiene=FMath::Max(R.Hygiene,90.f);}UE_LOG(LogTemp,Display,TEXT("Street start day: %s (day %d)"),*Day,S.Store.CalendarDay);}
  // OK_STREET_TEST_DEBT=<yuan>: start owing that much on the card (the broke-rescue check).
  {const FString Debt=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STREET_TEST_DEBT"));if(!Debt.IsEmpty()){auto& Cd=S.Store.Banking.Card;Cd.Issued=true;Cd.Limit=FMath::Max<int64>(Cd.Limit,FCString::Atoi64(*Debt));Cd.Balance=FCString::Atoi64(*Debt);}}
  FString H,M;if(At.Split(TEXT(":"),&H,&M))S.Store.CalendarMinute=FMath::Clamp(FCString::Atoi(*H),0,23)*60+FMath::Clamp(FCString::Atoi(*M),0,59);
 }
 FString Clock;
 if(!FParse::Value(FCommandLine::Get(),TEXT("StreetSocialClock="),Clock)){
  S.ClockStartHours=S.Store.CalendarMinute>=0?S.Store.CalendarMinute/60.0:OKStreetNewGameHour;
  S.ClockStartWall=FPlatformTime::Seconds();S.NextSunUpdate=0;
 }
 S.CalendarLastHours=OKStreetClockHours(S.ClockStartHours,FPlatformTime::Seconds()-S.ClockStartWall);
 S.CalendarSavedHour=FMath::FloorToInt32(S.CalendarLastHours);
 UE_LOG(LogTemp,Display,TEXT("StreetSocial calendar: %s (game day %d)"),*OKStreetCalendarLabel(S.Store.CalendarDay,S.CalendarLastHours),S.Store.CalendarDay);
 // The mind is optional: without ok_mind.dll everything works as before.
 if(FParse::Param(FCommandLine::Get(),TEXT("StreetSocialNoMind")))return;
 const FString Settings=OKStreetFreeTalkSettingsPath(); // the project's, or the user's copy when Saved/ was wiped
 const FString SaveDir=FPaths::GetPath(S.Manifest.SavePath.IsEmpty()?FPaths::ProjectSavedDir()/TEXT("StreetSocial/x"):S.Manifest.SavePath)/(S.SelfTest?TEXT("mind-selftest"):TEXT("mind"));
 S.Mind=MakeUnique<FOKStreetMind>();FString Error;
 if(S.Mind->Open(SaveDir,Settings,S.SelfTest,Error,FPaths::GetPath(FPaths::GetPath(OKStreetPlaceModelDir())))){
  OKStreetSetActiveMind(S.Mind.Get());
  for(int32 I=1;I<S.Manifest.Characters.Num();++I)UE_LOG(LogTemp,Display,TEXT("StreetSocial mind heroine %d: %s %s"),I,*S.Manifest.Characters[I].Name,*OKStreetStableId(S.Manifest.Characters[I]));
  MindRequestProfiles();
  FString Status;S.Mind->Query(TEXT("{\"kind\":\"status\"}"),Status,Error);
  UE_LOG(LogTemp,Display,TEXT("StreetSocial mind: %s %s"),*S.Mind->DllPath(),*Status);
 }else{
  UE_LOG(LogTemp,Display,TEXT("StreetSocial mind unavailable, keeping the old behaviour: %s"),*Error);
  S.Mind.Reset();
 }
}

// N key: skip to 07:00 of the next game day (plan §5 replaces "next day" with a jump to the morning).
bool AOKStreetSocialScene::SkipToNextMorning(){
 auto& S=*State;
 const double Was=S.ClockStartHours,WasWall=S.ClockStartWall,WasLast=S.CalendarLastHours;
 const double Wake=double(SkipWakeMinute()%1440)/60.0; // 07:00, earlier for a morning meeting
 S.ClockStartHours=Wake;S.ClockStartWall=FPlatformTime::Seconds();S.NextSunUpdate=0;S.CalendarLastHours=Wake;
 if(NextDay()){
  // Last night's order still being made is not waiting for him in the morning: 胡桃's machine drink, paid at 14:28,
  // was "left behind at the girls' dorm" the next morning as he walked 优香 to school (R4-24).
  if(S.Serves.Num()||S.Table.Num())UE_LOG(LogTemp,Display,TEXT("Street skip: %d orders being made and %d dishes cleared overnight"),S.Serves.Num(),S.Table.Num());
  S.Serves.Reset();S.Table.Reset();S.ChatOrderAwaitsJudge.Reset();
  // He slept somewhere: where there is no bed for him (the street outside her dorm, a shop) he wakes in his own dorm,
  // not standing the whole night at the girls' dorm door he walked her to.
  const FString Here=PlaceHere(S.Male);auto Ask=OKRulesRequest(TEXT("wake_place"));Ask->SetStringField(TEXT("place"),Here);
  if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Ask)){const FString WakeAt=Out->GetStringField(TEXT("place"));
   if(WakeAt!=Here)if(const FOKStreetPlace* Bed=OKStreetFindPlace(WakeAt)){S.Male=PlaceInsideSpot(*Bed);UE_LOG(LogTemp,Display,TEXT("Street skip: he wakes at %s (%s)"),*WakeAt,*S.Male.ToCompactString());}}
  return true;}
 S.ClockStartHours=Was;S.ClockStartWall=WasWall;S.CalendarLastHours=WasLast;
 return false;
}

// Writes the clock into the save without touching anything else, so a quit keeps the time of day.
void AOKStreetSocialScene::SaveCalendar(){
 if(!State||State->SelfTest||State->Manifest.SavePath.IsEmpty())return;auto& S=*State;
 auto Candidate=S.Store;
 // Where he stands: outside the room of a facility he is in (it puts him in a set), not while riding an elevator.
 if(ElevatorWithMale()==INDEX_NONE&&S.LiftRide.Elevator==INDEX_NONE){Candidate.HasPlayerSpot=true;Candidate.PlayerSpot=S.FacilityEntered?S.FacilitySavedMale:S.Male;Candidate.PlayerYaw=S.FacilityEntered?S.FacilitySavedMaleRotation.Yaw:S.Rotations[0].Yaw;Candidate.ViewYaw=S.FacilityEntered?S.FacilitySavedYaw:S.Yaw;S.SpotSavedMale=Candidate.PlayerSpot;S.SpotSavedAt=FPlatformTime::Seconds();}
 Candidate.CalendarMinute=FMath::Clamp(FMath::FloorToInt32(OKStreetClockHours(S.ClockStartHours,FPlatformTime::Seconds()-S.ClockStartWall)*60.0),0,24*60-1);
 // Who is with him (R3-238): walking or out with him follows him again after a load; sitting, talking or in a set with
 // him waits beside him. Not put back yet from the last load: kept as it was saved.
 if(S.ResumeWithHim.IsEmpty()&&Candidate.HasPlayerSpot){Candidate.WithHim.Reset();const FVector His=Candidate.PlayerSpot;
  for(int32 I=1;I<S.Manifest.Characters.Num();++I){if(!S.Agents.IsValidIndex(I-1))continue;
   const bool Follow=S.Companion==I||S.OutingGirl==I||S.Party.Contains(I);
   // The one he is in free talk with counts too (shopping with her at the bookstore, the talk open, no activity on).
   // Not the one on the phone (R3-258): a message thread open to her at 111 m put her beside him at the library after a load.
   const bool Remote=S.RemoteMode!=0&&S.RemotePeer==I;
   const bool Stay=S.Pair.Partner==I||S.Target==I||(FreeTalkPartner()==I&&!Remote)||S.PrivateGirl==I||(S.Facility!=INDEX_NONE&&S.FacilityCharacter==I);
   if(!Follow&&!Stay)continue;
   // One of the group (not the one who walks with him): "party", she joins the group again after a load. As "follow" the
   // first of them by slot walked with him and took the talk, while the one he was talking to (惠惠 with 贞德·Alter at
   // the restaurant) only waited beside him.
   const bool Member=S.Party.Contains(I)&&S.Companion!=I&&S.OutingGirl!=I;
   FOKStreetStore::FWithHim W;W.Id=OKStreetStableId(S.Manifest.Characters[I]);W.Mode=Member?TEXT("party"):Follow?TEXT("follow"):TEXT("stay");
   const FVector Her=S.Agents[I-1].Position;
   // Where she stood, when that is by him (the trail behind him, or a set far from where he is put back: beside him).
   if(!S.FacilityEntered&&FVector::Dist2D(Her,His)<(Follow?230.f:600.f)&&FMath::Abs(Her.Z-His.Z)<150.f){W.Spot=Her;W.Yaw=S.Rotations.IsValidIndex(I)?S.Rotations[I].Yaw:0.f;}
   else{W.Spot=His+FRotator(0,Candidate.PlayerYaw+150.f,0).RotateVector(FVector(130,0,0));W.Spot.Z=Her.Z;if(FMath::Abs(Her.Z-His.Z)>=150.f)W.Spot.Z=His.Z;W.Yaw=Candidate.PlayerYaw;}
   Candidate.WithHim.Add(MoveTemp(W));}
  // Their table (paid; R3-254): a dish ordered at 11:46 never came after a load, ¥49 paid for nothing.
  Candidate.Table.Reset();Candidate.TablePlace.Reset();
  if(!S.Table.IsEmpty()){Candidate.TablePlace=S.MealPlace;for(const auto& D:S.Table){if(D.Percent<=0.f)continue;FOKStreetStore::FTableDish T;T.ItemId=D.ItemId;T.Name=D.Name;T.ForHer=D.ForHer;T.Percent=D.Percent;
    if(S.Manifest.Characters.IsValidIndex(D.Her)&&D.Her>0)T.Her=OKStreetStableId(S.Manifest.Characters[D.Her]);Candidate.Table.Add(MoveTemp(T));}}
  // The basket not paid yet (R3-257): "got my sketchbook" after a load, nothing bought, nothing paid.
  Candidate.Cart.Reset();Candidate.CartPlace.Reset();Candidate.CartHer.Reset();
  if(!S.Cart.IsEmpty()){const int32 With=S.RemoteMode!=0?INDEX_NONE:FreeTalkPartner();Candidate.CartPlace=PlaceHere(S.Male);if(S.Manifest.Characters.IsValidIndex(With)&&With>0)Candidate.CartHer=OKStreetStableId(S.Manifest.Characters[With]);
   for(const auto& L:S.Cart){FOKStreetStore::FCartItem C;C.ItemId=L.ItemId;C.Size=L.Size;C.Sweetness=L.Sweetness;C.Ice=L.Ice;C.ForHer=L.ForHer;C.HerOwn=L.HerOwn;
    if(S.Manifest.Characters.IsValidIndex(L.Girl)&&L.Girl>0)C.Girl=OKStreetStableId(S.Manifest.Characters[L.Girl]);Candidate.Cart.Add(MoveTemp(C));}}}
 S.Store=MoveTemp(Candidate);OKStreetAtomicSaveStoreAsync(S.Manifest.SavePath,S.Store);
}

void AOKStreetSocialScene::TickCalendarAndMind(){
 auto& S=*State;
 const double Hours=OKStreetClockHours(S.ClockStartHours,FPlatformTime::Seconds()-S.ClockStartWall);
 if(S.CalendarLastHours>=0&&!S.ClockPaused&&OKStreetDayStartsPassed(S.CalendarLastHours,Hours)>0){
  // 04:00: a new game day, the same as sleeping through it (talks reset, topic stock refreshes).
  if(NextDay()){S.Message=OKMsg(TEXT("ui.mind.new_day_label"), {{TEXT("calendar_label"), *OKStreetCalendarLabel(S.Store.CalendarDay,Hours)}});RefreshUI();}
 }
 S.CalendarLastHours=Hours;
 // Once per game hour (2.5 real minutes) the clock is saved, so quitting loses at most an hour.
 const int32 Hour=FMath::FloorToInt32(Hours);
 if(Hour!=S.CalendarSavedHour){S.CalendarSavedHour=Hour;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_SaveCalendar);SaveCalendar();}}
 // He walked somewhere else: saved too (at most every 20 s), so a game closed any way continues near where he was.
 else if(!S.SelfTest&&FVector::Dist2D(S.Male,S.SpotSavedMale)>500.f&&FPlatformTime::Seconds()-S.SpotSavedAt>20.0)SaveCalendar();
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickChoices);TickChoices();}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickNeeds);TickNeeds();}{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickBonds);TickBonds();}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickApproach);TickApproach();}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickIntimacy);TickIntimacy();}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickAppointments);TickAppointments();}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickAutoFriendship);TickAutoFriendship();}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickSchedules);TickSchedules();}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickJobs);TickJobs();}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickEncounters);TickEncounters();}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickInitiative);TickInitiative();}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickDirector);TickDirector();}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TickFinance);TickFinance();}
 if(S.Mind){
  // Talk moved on from her (another partner, or none for a while): summarise the meeting into her memories.
  const int32 Partner=FreeTalkPartner();
  if(S.MindTalkPartner!=INDEX_NONE&&Partner!=S.MindTalkPartner&&!S.FreeTalkBusy){MindSummarize(S.MindTalkPartner);S.MindTalkPartner=INDEX_NONE;}
  TArray<TSharedPtr<FJsonObject>> Results;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_MindPoll);S.Mind->Poll(Results);}
  for(const auto& R:Results){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_MindResult);
   FString Kind;R->TryGetStringField(TEXT("kind"),Kind);
   // A chat job someone is waiting for (SubmitMindChat).
   {double JobId=0;if(R->TryGetNumberField(TEXT("id"),JobId))if(auto* Done=S.MindChats.Find(uint64(JobId))){
    auto Call=MoveTemp(*Done);S.MindChats.Remove(uint64(JobId));bool Ok=false;FString Content,Error;R->TryGetBoolField(TEXT("ok"),Ok);R->TryGetStringField(TEXT("content"),Content);R->TryGetStringField(TEXT("error"),Error);
    QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_MindChatCallback);Call(Ok&&!Content.TrimStartAndEnd().IsEmpty(),Content,Ok&&Content.TrimStartAndEnd().IsEmpty()?FString(TEXT("empty content")):Error);continue;}}
   if(Kind==TEXT("talk")){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_MindTalkResult);HandleMindTalk(R);continue;}
   if(Kind==TEXT("reflect")){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_MindReflectResult);HandleReflect(R);continue;}
   if(Kind==TEXT("note"))continue;
   {FString Tag;if(Kind==TEXT("chat")&&R->TryGetStringField(TEXT("tag"),Tag)&&Tag.StartsWith(TEXT("overheard:"))){TArray<FString> P;Tag.ParseIntoArray(P,TEXT(":"));if(P.Num()==3){HandleOverheard(R,FCString::Atoi(*P[1]),FCString::Atoi(*P[2]));continue;}}}
   if(Kind==TEXT("profile")){const TSharedPtr<FJsonObject>* Profile=nullptr;FString Heroine;if(R->TryGetStringField(TEXT("heroine"),Heroine)&&R->TryGetObjectField(TEXT("profile"),Profile)){S.MindProfiles.Add(Heroine,*Profile);OKStreetApplyHerExtras(S,Heroine);InitWalletFromProfile(Heroine,*Profile);InitStrangerCap(Heroine,*Profile);if(S.PlannedWithoutProfiles&&MindProfilesReady()){S.PlannedWithoutProfiles=false;S.PlanDay=-1;UE_LOG(LogTemp,Display,TEXT("Street plan: the profiles came; today planned again"));}}}
   if(Kind==TEXT("profile")){bool Generated=false;R->TryGetBoolField(TEXT("generated"),Generated);FString Who,Why;R->TryGetStringField(TEXT("heroine"),Who);R->TryGetStringField(TEXT("error"),Why);UE_LOG(LogTemp,Display,TEXT("StreetSocial mind profile %s: %s%s"),*Who,Why.IsEmpty()?(Generated?TEXT("generated"):TEXT("loaded")):TEXT("failed "),*Why);continue;}
   FString Text;FJsonSerializer::Serialize(R.ToSharedRef(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
   UE_LOG(LogTemp,Display,TEXT("StreetSocial mind result: %s"),*Text.Left(600));
  }
 }
}

// FreeTalk through the mind: her persistent context lives in Rust (Saved/StreetSocial/mind/context), this turn's
// message carries the full state. False when the mind is not open (nothing is posted around it).
bool AOKStreetSocialScene::SubmitFreeTalkToMind(int32 Character,const FString& Id,const FString& System,const FString& UserMessage,const FString& Text){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_SubmitFreeTalkToMind);
 auto& S=*State;if(!S.Mind)return false;
 TSharedRef<FJsonObject> Job=MakeShared<FJsonObject>();
 Job->SetStringField(TEXT("kind"),TEXT("talk"));Job->SetStringField(TEXT("tag"),TEXT("freetalk"));Job->SetStringField(TEXT("priority"),TEXT("dialogue"));
 Job->SetStringField(TEXT("heroine"),Id);Job->SetStringField(TEXT("system"),System);Job->SetStringField(TEXT("user"),UserMessage);
 Job->SetNumberField(TEXT("max_tokens"),400);Job->SetNumberField(TEXT("temperature"),1.0); /* 1.2 drifted into word salad over long talks (DeepSeek: 1.0 for conversation) */ Job->SetBoolField(TEXT("json"),true);
 // Only used the first time she talks through the mind: her conversation so far comes along.
 TArray<TSharedPtr<FJsonValue>> Legacy;
 for(const auto& Turn:S.FreeTalkMemory.FindOrAdd(Id)){auto M=MakeShared<FJsonObject>();M->SetStringField(TEXT("role"),Turn.Role);M->SetStringField(TEXT("content"),Turn.Content);Legacy.Add(MakeShared<FJsonValueObject>(M));}
 Job->SetArrayField(TEXT("legacy_history"),Legacy);
 FString Json,Error;FJsonSerializer::Serialize(Job,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
 const uint64 JobId=S.Mind->Submit(Json,Error);
 if(!JobId){UE_LOG(LogTemp,Warning,TEXT("StreetSocial mind refused the talk: %s"),*Error);return false;}
 FOKStreetMindTalk Pending;Pending.Character=Character;Pending.Id=Id;Pending.Text=Text;S.MindTalks.Add(JobId,Pending);
 return true;
}

void AOKStreetSocialScene::HandleMindTalk(const TSharedPtr<FJsonObject>& R){
 auto& S=*State;
 FOKStreetMindTalk Pending;
 if(!S.MindTalks.RemoveAndCopyValue(uint64(R->GetNumberField(TEXT("id"))),Pending))return;
 if(!S.Manifest.Characters.IsValidIndex(Pending.Character)){S.FreeTalkBusy=false;return;}
 FString Content,Failure;bool Ok=false,Retry=false;R->TryGetBoolField(TEXT("ok"),Ok);
 FOKFreeTalkReply Reply;
 if(!Ok){FString Why;R->TryGetStringField(TEXT("error"),Why);Failure=OKT(TEXT("ui.mind.deepseek_error_prefix"))+Why;}
 else{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_MindTalkParse);if(!R->TryGetStringField(TEXT("content"),Content)||!OKStreetFreeTalkParseContent(Content,Reply,Failure,Retry)){if(Failure.IsEmpty())Failure=OKT(TEXT("ui.mind.deepseek_empty_reply"));}}
 const TSharedPtr<FJsonObject>* Usage=nullptr;double Hit=0,Miss=0,Chars=0;
 if(R->TryGetObjectField(TEXT("usage"),Usage)){(*Usage)->TryGetNumberField(TEXT("cache_hit_tokens"),Hit);(*Usage)->TryGetNumberField(TEXT("cache_miss_tokens"),Miss);}
 R->TryGetNumberField(TEXT("context_chars"),Chars);
 UE_LOG(LogTemp,Display,TEXT("Street free talk (mind): actor=%d ok=%d cache_hit=%.0f cache_miss=%.0f context_chars=%.0f"),Pending.Character,Failure.IsEmpty()?1:0,Hit,Miss,Chars);
 if(Failure.IsEmpty()){S.InteractedToday.Add(Pending.Character);S.MindTalkPartner=Pending.Character;
  // The day before her birthday she has it on her mind to hint at it (director note): from now on he could know (social.rs birthday_end).
  if(BirthdayOf(Pending.Character,S.Store.CalendarDay+1)&&S.Manifest.Characters.IsValidIndex(Pending.Character)){const FString Id=OKStreetStableId(S.Manifest.Characters[Pending.Character]);
   for(auto& R:S.Store.Relations)if(R.Id==Id)R.Flags.AddUnique(TEXT("birthday_told"));}TMap<FString,float> Talked;Talked.Add(TEXT("social"),6.f);QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_MindTalkFeed);FeedHer(Pending.Character,Talked);}
 FinishFreeTalk(Pending.Character,Pending.Id,Pending.Text,Failure.IsEmpty()?&Reply:nullptr,Failure);
}

// The minute on the calendar (its date as OKStreetCalendarDateTime shows it): the small hours before 04:00 still belong to
// the game day before, so they are a day on. Without it the clock went back a day at midnight and forward at 04:00, and
// sitting together from 03:42 to 04:02 was told as 1461 minutes (R3-224).
static int64 OKStreetMindNow(const FOKStreetSceneState& S){
 const int64 Minute=FMath::Clamp(FMath::FloorToInt64(OKStreetClockHours(S.ClockStartHours,FPlatformTime::Seconds()-S.ClockStartWall)*60.0),int64(0),int64(1439));
 return int64(S.Store.CalendarDay)*1440+Minute+(Minute<int64(OKStreetDayStartHour*60.0)?1440:0);
}

static FString OKStreetMindJson(const TSharedRef<FJsonObject>& Object){
 FString Json;FJsonSerializer::Serialize(Object,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));return Json;
}

// A rule-written memory for one heroine (plan §6.0: no LLM). Kind picks the default importance in Rust.
void AOKStreetSocialScene::MindObserve(int32 Character,const FString& Kind,const FString& Summary,const FString& Place,float Importance,const TCHAR* Type){
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_HistoryAdd);HistoryAdd(Character,Kind,TEXT("world"),Summary,Place);} // every event also goes into her own history (OKStreetSocialPhoneInbox.inl)
 MindRemember(Character,Kind,Summary,Place,Importance,Type);
}

// Her memory only (the history line written elsewhere): stamped with the game day and minute.
void AOKStreetSocialScene::MindRemember(int32 Character,const FString& Kind,const FString& Summary,const FString& Place,float Importance,const TCHAR* Type){
 if(!State||!State->Mind||!State->Manifest.Characters.IsValidIndex(Character)||Character==0)return;auto& S=*State;QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_MindRemember);
 const int64 Now=OKStreetMindNow(S);
 TSharedRef<FJsonObject> Job=MakeShared<FJsonObject>();
 Job->SetStringField(TEXT("kind"),TEXT("observe"));Job->SetStringField(TEXT("heroine"),OKStreetStableId(S.Manifest.Characters[Character]));
 Job->SetStringField(TEXT("memory_kind"),Kind);Job->SetStringField(TEXT("memory_type"),Type);Job->SetStringField(TEXT("summary"),Summary);Job->SetStringField(TEXT("place"),Place);
 Job->SetNumberField(TEXT("day"),double(Now/1440));Job->SetNumberField(TEXT("minute"),double(Now%1440));
 TArray<TSharedPtr<FJsonValue>> Entities;Entities.Add(MakeShared<FJsonValueString>(TEXT("player")));if(!Place.IsEmpty())Entities.Add(MakeShared<FJsonValueString>(Place));Job->SetArrayField(TEXT("entities"),Entities);
 if(Importance>=0)Job->SetNumberField(TEXT("importance"),Importance);
 FString Error;if(!S.Mind->Submit(OKStreetMindJson(Job),Error))UE_LOG(LogTemp,Warning,TEXT("StreetSocial mind observe failed: %s"),*Error);
}

// Top memories for this moment, as lines for the 【你記得的事】 block. Sync: the memory stream is in memory.
FString AOKStreetSocialScene::MindRecallText(const FString& Id,const FString& PlayerText){
 if(!State||!State->Mind)return FString();auto& S=*State;
 TSharedRef<FJsonObject> Q=MakeShared<FJsonObject>();
 Q->SetStringField(TEXT("kind"),TEXT("recall"));Q->SetStringField(TEXT("heroine"),Id);Q->SetNumberField(TEXT("now"),double(OKStreetMindNow(S)));Q->SetNumberField(TEXT("k"),5);
 TArray<TSharedPtr<FJsonValue>> Entities;Entities.Add(MakeShared<FJsonValueString>(TEXT("player")));
 for(const auto* Place:NearbyPlaces(1500))if(Place)Entities.Add(MakeShared<FJsonValueString>(Place->Id));
 Q->SetArrayField(TEXT("entities"),Entities);
 // Chinese has no spaces: the player's line cut at punctuation gives phrases a memory may contain.
 TArray<TSharedPtr<FJsonValue>> Words;FString Chunk;
 auto Flush=[&](){if(Chunk.Len()>=2)Words.Add(MakeShared<FJsonValueString>(Chunk.Left(8)));Chunk.Reset();};
 for(const TCHAR C:PlayerText){if(FChar::IsWhitespace(C)||FChar::IsPunct(C)||C==TEXT('，')||C==TEXT('。')||C==TEXT('？')||C==TEXT('！')||C==TEXT('、'))Flush();else Chunk.AppendChar(C);}
 Flush();
 Q->SetArrayField(TEXT("keywords"),Words);
 FString Out,Error;if(!S.Mind->Query(OKStreetMindJson(Q),Out,Error))return FString();
 TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Out),Root)||!Root.IsValid())return FString();
 const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(!Root->TryGetArrayField(TEXT("memories"),Rows))return FString();
 TArray<FString> Lines;
 for(const auto& Row:*Rows){const auto M=Row->AsObject();if(!M)continue;FString MemId,Summary;M->TryGetStringField(TEXT("id"),MemId);M->TryGetStringField(TEXT("summary"),Summary);
  const int64 T=int64(M->GetNumberField(TEXT("t")));const FDateTime When=OKStreetCalendarDateTime(int32(T/1440),(T%1440)/60.0);
  Lines.Add(FString::Printf(TEXT("- (%s) %d/%d %02d:%02d %s"),*MemId,When.GetMonth(),When.GetDay(),When.GetHour(),When.GetMinute(),*Summary));} // with the clock time: she knows when it happened
 return FString::Join(Lines,TEXT("\n"));
}

// First launch: each heroine gets a profile written from her card (plan §3.1); existing files are kept.
void AOKStreetSocialScene::MindRequestProfiles(){
 if(!State||!State->Mind)return;auto& S=*State;
 TArray<TSharedPtr<FJsonValue>> Places;for(const auto& Place:OKStreetPlaces())Places.Add(MakeShared<FJsonValueString>(Place.Id));
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  const auto& C=S.Manifest.Characters[I];
  TSharedRef<FJsonObject> Card=MakeShared<FJsonObject>();
  Card->SetStringField(TEXT("name"),C.Name);Card->SetNumberField(TEXT("personality_id"),C.PersonalityId);Card->SetStringField(TEXT("sex"),C.Sex);
  TArray<TSharedPtr<FJsonValue>> Interests;for(int32 N:C.InterestCategories)Interests.Add(MakeShared<FJsonValueNumber>(N));Card->SetArrayField(TEXT("interest_categories"),Interests);
  FOKFreeTalkPersona Persona;FString Error;if(OKStreetFreeTalkLoadPersona(C.Name,Persona,Error))Card->SetStringField(TEXT("persona"),Persona.Profile);
  TSharedRef<FJsonObject> Job=MakeShared<FJsonObject>();
  Job->SetStringField(TEXT("kind"),TEXT("profile"));Job->SetStringField(TEXT("heroine"),OKStreetStableId(C));Job->SetObjectField(TEXT("card"),Card);Job->SetArrayField(TEXT("places"),Places);
  if(!S.Mind->Submit(OKStreetMindJson(Job),Error))UE_LOG(LogTemp,Warning,TEXT("StreetSocial mind profile request failed: %s"),*Error);
 }
}

// End of a meeting: she writes it into her memory stream (plan §6.5), a branch on her cached context.
void AOKStreetSocialScene::MindSummarize(int32 Character){
 if(!State||!State->Mind||!State->Manifest.Characters.IsValidIndex(Character))return;auto& S=*State;
 const int64 Now=OKStreetMindNow(S);
 TSharedRef<FJsonObject> Job=MakeShared<FJsonObject>();
 Job->SetStringField(TEXT("kind"),TEXT("summarize"));Job->SetStringField(TEXT("heroine"),OKStreetStableId(S.Manifest.Characters[Character]));
 Job->SetNumberField(TEXT("day"),double(Now/1440));Job->SetNumberField(TEXT("minute"),double(Now%1440));
 FString Error;if(!S.Mind->Submit(OKStreetMindJson(Job),Error))UE_LOG(LogTemp,Warning,TEXT("StreetSocial mind summary request failed: %s"),*Error);
}

// Her bias right now from the rules (mind.rs mind_bias, or mind_status with her needs and money stress too). Alone with
// him: a private facility, or sleeping together; everything else counts as in public.
TSharedPtr<FJsonObject> AOKStreetSocialScene::MindBiasAnswer(int32 Character,bool bStatus) const{
 const auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Character))return nullptr;
 const bool Public=!(S.PrivateGirl==Character||S.Facility!=INDEX_NONE||S.SleepGirl==Character);
 auto Request=OKRulesHeroineRequest(S,bStatus?TEXT("mind_status"):TEXT("mind_bias"),OKStreetStableId(S.Manifest.Characters[Character]));
 Request->SetBoolField(TEXT("public"),Public);Request->SetBoolField(TEXT("mask_broken"),MaskBroken(Character,Public));Request->SetNumberField(TEXT("now"),double(OKStreetMindNow(S)));
 return OKRulesEval(S,Request);
}

// Plan §4.2 / §10 in the FreeTalk state block: exact axis numbers and the fixed-phrase inner line.
void AOKStreetSocialScene::FillMindStatus(int32 Character,FOKFreeTalkStatus& Status){
 if(!State||!State->Manifest.Characters.IsValidIndex(Character))return;auto& S=*State;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 const FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
 TSharedPtr<FJsonObject> Mind;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_StatusBias);Mind=MindBiasAnswer(Character,true);}
 const FOKStreetBias Bias=OKRulesBiasFrom(Mind);
 TArray<FString> Axes;
 static const TMap<FString,FString> Names={{TEXT("trust"),TEXT("信任")},{TEXT("admiration"),TEXT("崇拜")},{TEXT("attraction"),TEXT("吸引")},{TEXT("dependency"),TEXT("依賴")},{TEXT("mystique"),TEXT("神秘感")},{TEXT("jealousy"),TEXT("忌妒")},{TEXT("greed"),TEXT("物慾")},{TEXT("lifestyle_desire"),TEXT("生活欲望")},{TEXT("respect"),TEXT("尊重")},{TEXT("resentment"),TEXT("怨懟")}};
 for(const auto& A:OKStreetAxisFields(const_cast<FOKStreetRelation&>(R)))Axes.Add(FString::Printf(TEXT("%s %.0f"),OKT(*Names.FindRef(A.Key)),*A.Value));
 Axes.Add(OKMsg(TEXT("ui.mind.admiration_stage_label"), {{TEXT("stage"), Mind.IsValid()?int32(Mind->GetNumberField(TEXT("admiration_stage"))):0}}));
 Status.Axes=FString::Join(Axes,OKT(TEXT("ui.bank.list_separator")));
 Status.Inner=Bias.InnerLine;
 // Apart (a message or a call) the place, menu and people around him are not around her: she knows where he is only if
 // he says so or somebody saw him (what she heard).
 const bool Apart=RemoteWith(Character);
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_StatusMenu);Status.Menu=Apart?FString():MenuText(R.Satiety,100.f/SatietyMax(Character),Character);}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_StatusScene);Status.Scene=Apart?FString():SceneLine(Character);}
 // Whose number each has: without his she cannot send him the photo they just took (she said her phone "can't do that").
 if(!Apart){const bool His=HasHerNumber(Character),Hers=HasHerNumber(Character,true);
  Status.Scene+=OKMsg(TEXT("ui.mind.phone_contact_status"),{{TEXT("state"),His&&Hers?TEXT("both"):His?TEXT("his_only"):Hers?TEXT("hers_only"):TEXT("none")}});
  // Whether she has a song she can really sing now (the song system plays it): 安可 said "here it comes!" at their
  // singing date with none ready, then stood silent and asked why he said nothing (R6-55).
  const EOKSongPhase Song=S.Song.Girl==Character?S.Song.Phase:EOKSongPhase::Idle;
  Status.Scene+=OKMsg(TEXT("ui.mind.song_status"),{{TEXT("state"),Song==EOKSongPhase::Ready||Song==EOKSongPhase::Offered?TEXT("ready"):Song>=EOKSongPhase::Intro&&Song<=EOKSongPhase::Outro?TEXT("singing"):Song==EOKSongPhase::Preparing?TEXT("preparing"):TEXT("none")}});}
 if(!Apart&&S.ShiftAppointment)if(const auto* Job=S.Catalog.FindJob(S.ShiftJob)){const FDateTime T=StreetNow();Status.Scene+=OKMsg(TEXT("ui.mind.he_working_here_status"), {{TEXT("job_title"), *Job->Title}, {TEXT("busyness_percent"), OKRulesCrowdAt(S,Job->Place,T.GetHour()+T.GetMinute()/60.0)*100.f}, {TEXT("minutes_to_shift_end"), FMath::Max<int64>(0,S.ShiftEnd-OKStreetMindNow(S))}});NoteShiftChat();}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_StatusInvite);Status.Invite=RollInviteStance(Character);}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_StatusHeard);Status.Heard=HeardText(Character);}
 // The meetings the game really has with him: she must not remember others (a time she said once is not a promise).
 // Who they said pays, with it: his apology dinner became "my apology dinner, I pay" at the table (R3-216).
 auto PayNote=[](const FOKStreetAppointment& A){return A.Payer==TEXT("him")?FString(OKT(TEXT("ui.mind.promise_he_pays"))):A.Payer==TEXT("her")?FString(OKT(TEXT("ui.mind.promise_you_pay"))):A.Payer==TEXT("split")?FString(OKT(TEXT("ui.mind.promise_split"))):FString();};
 {TArray<FString> Lines;for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Heroine==Id){const int32 Minute=int32(A.Start%1440);
   Lines.Add(OKMsg(TEXT("ui.bank.heroine_reply_line"), {{TEXT("heroine_name"), *OKStreetCalendarLabel(int32(A.Start/1440),Minute/60.0)}, {TEXT("reply"), A.Label+PayNote(A)}}));}
  // The one they are keeping right now (met in the last two hours): it is not gone, they are in it.
  for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("done")&&A.Heroine==Id&&A.Kind==TEXT("date")&&FMath::Abs(OKStreetMindNow(S)-A.Start)<120){const int32 Minute=int32(A.Start%1440);
   Lines.Add(OKMsg(TEXT("ui.bank.heroine_reply_line"), {{TEXT("heroine_name"), *OKStreetCalendarLabel(int32(A.Start/1440),Minute/60.0)}, {TEXT("reply"), A.Label+PayNote(A)+OKT(TEXT("ui.mind.promise_being_kept"))}}));}
  Status.Promises=Lines.IsEmpty()?FString(OKT(TEXT("ui.mind.no_pending_promises"))):FString::Join(Lines,OKT(TEXT("ui.economy.semicolon_mark")));}
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_StatusToday);Status.Today=TodayText(Character);}
 if(Mind.IsValid())Status.Needs=Mind->GetStringField(TEXT("needs_text"));
 // Her spirits (R3-269), in a word: in the turn's own status, not the cached prefix. Her usual self says nothing.
 {const FString Band=OKStreetMoodBand(MoodNow(Character));if(Band!=TEXT("usual"))Status.Needs+=OKMsg(TEXT("ui.mind.spirits_status"),{{TEXT("band"),Band}});}
 // What runs out of her and how wet she is (OKStreetSocialArousal.inl): she feels it, so he may say so and she knows.
 if(!Apart)Status.Needs+=OKStreetBodyWetStatus(S,Character,OKStreetMindNow(S));
 if(const FString* Recent=S.MindRecent.Find(Id)){Status.Recent=*Recent;S.MindRecent.Remove(Id);}
 TArray<FOKStreetPendingChoice> Mine;for(const auto& C:S.Choices)if(C.Heroine==Character)Mine.Add(C);
 Status.Choices=OKStreetChoiceBlock(Mine,SceneTime());
}

// The model's rel_delta and emotion for this turn, through the rules (plan §7 Validator) and saved.
void AOKStreetSocialScene::ApplyMindRelation(int32 Character,const TSharedPtr<FJsonObject>& RelDelta,const FString& Emotion){
 if(!State||!State->Manifest.Characters.IsValidIndex(Character))return;auto& S=*State;
 if(!RelDelta.IsValid()&&Emotion.IsEmpty())return;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 auto Candidate=S.Store;
 const int32 Index=OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true);
 if(!Candidate.Relations.IsValidIndex(Index))return;
 auto& R=Candidate.Relations[Index];
 // The validator (mind.rs rel_delta): known axes only, at most +-5 a turn scaled by her traits, emotions from the list.
 auto Request=OKRulesRelationRequest(TEXT("rel_delta"),R,S.MindProfiles.FindRef(Id));
 if(RelDelta.IsValid())Request->SetObjectField(TEXT("delta"),RelDelta.ToSharedRef());Request->SetStringField(TEXT("emotion"),Emotion);Request->SetNumberField(TEXT("now"),double(OKStreetMindNow(S)));
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return;
 TArray<FString> Applied;for(const auto& V:Out->GetArrayField(TEXT("applied")))Applied.Add(V->AsString());
 OKRulesPatchRelation(R,*Out->GetObjectField(TEXT("relation")));
 FString Error;
 S.Store=MoveTemp(Candidate);
 if(!S.SelfTest&&!S.Manifest.SavePath.IsEmpty())OKStreetAtomicSaveStoreAsync(S.Manifest.SavePath,S.Store);
 if(!Applied.IsEmpty()){S.FreeTalkNotice+=TEXT("　")+FString::Join(Applied,TEXT(" "));UE_LOG(LogTemp,Display,TEXT("Street relation: actor=%d %s emotion=%s"),Character,*FString::Join(Applied,TEXT(" ")),*Emotion);}
}

// Opens a choice for the menu and the chat (plan §7.1). OnResolve runs once with the final option.
FString AOKStreetSocialScene::OpenChoice(int32 Heroine,const FString& Kind,const FString& Label,const TArray<FString>& Options,const FString& HerLean,float HerInsist,double Seconds,TFunction<void(const FString&)> OnResolve){
 auto& S=*State;
 FOKStreetPendingChoice C;C.Id=FString::Printf(TEXT("c%d"),S.NextChoice++);C.Kind=Kind;C.Label=Label;C.Options=Options;C.Heroine=Heroine;
 C.HerLean=Options.Contains(HerLean)?HerLean:FString();C.HerInsist=FMath::Clamp(HerInsist,0.f,1.f);C.Deadline=Seconds>0?SceneTime()+Seconds:0;C.OnResolve=MoveTemp(OnResolve);C.HisLinesAtOpen=S.HisLines;
 C.SentAtOpen=S.FreeTalkSent;C.OpenedAt=SceneTime();
 S.Choices.Add(MoveTemp(C));
 UE_LOG(LogTemp,Display,TEXT("Street choice open: %s %s %s lean=%s"),*S.Choices.Last().Id,*Kind,*Label,*HerLean);
 return S.Choices.Last().Id;
}

bool AOKStreetSocialScene::ResolveChoice(const FString& Id,const FString& Option,float PlayerInsist){
 auto& S=*State;
 const int32 Index=S.Choices.IndexOfByPredicate([&](const FOKStreetPendingChoice& C){return C.Id==Id;});
 if(Index==INDEX_NONE)return false;
 // Her pushback and who wins are the rules' (mind.rs negotiate).
 auto& C=S.Choices[Index];auto Request=OKRulesRequest(TEXT("negotiate"));
 TArray<TSharedPtr<FJsonValue>> Options;for(const FString& O:C.Options)Options.Add(MakeShared<FJsonValueString>(O));Request->SetArrayField(TEXT("options"),Options);
 Request->SetStringField(TEXT("lean"),C.HerLean);Request->SetNumberField(TEXT("insist"),C.HerInsist);Request->SetNumberField(TEXT("round"),C.Round);
 Request->SetStringField(TEXT("option"),Option);Request->SetNumberField(TEXT("player_insist"),PlayerInsist);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return false;
 const FString Outcome=Out->GetStringField(TEXT("outcome")),Final=Out->GetStringField(TEXT("final"));
 C.Round=int32(Out->GetNumberField(TEXT("round")));C.HerInsist=float(Out->GetNumberField(TEXT("insist")));
 if(Outcome==TEXT("rejected"))return false;
 if(Outcome==TEXT("pushback")){
  S.FreeTalkNotice=OKMsg(TEXT("ui.mind.she_disagrees_persuade"), {{TEXT("label"), *C.Label}});RefreshUI();
  UE_LOG(LogTemp,Display,TEXT("Street choice pushback: %s round=%d"),*Id,C.Round);
  return true;
 }
 FOKStreetPendingChoice Done=MoveTemp(S.Choices[Index]);S.Choices.RemoveAt(Index);
 UE_LOG(LogTemp,Display,TEXT("Street choice resolved: %s %s -> %s"),*Done.Id,*Done.Kind,*Final);
 if(Done.OnResolve)Done.OnResolve(Final);
 RefreshUI();
 return true;
}

// Past the deadline her lean is taken (or the first option when she has none).
void AOKStreetSocialScene::TickChoices(){
 auto& S=*State;const double Now=SceneTime();
 // Her give_money still waiting after half a minute: the judge never answered; it goes through as she said.
 {TArray<int32> Waiting;S.PendingGiveMoney.GetKeys(Waiting);for(const int32 G:Waiting)SettleMoneyHandover(G,0,false);}
 for(int32 I=S.Choices.Num()-1;I>=0;--I){
  // A bill not settled when they walk out of the shop: nothing is bought there. It timed out a minute later in the park
  // and her latte was paid for and "served in 30 s" by the pond (R6-8).
  if(const auto& C=S.Choices[I];!C.Place.IsEmpty()&&ShopPlaceHere()!=C.Place){
   const FOKStreetPlace* P=OKStreetFindPlace(C.Place);TArray<FString> Names;for(const auto& L:S.Cart)if(const auto* It=S.Catalog.Find(L.ItemId))Names.Add(It->Name);
   if(C.Heroine==FreeTalkPartner()&&!Names.IsEmpty())S.TellHer.Add(OKMsg(TEXT("ui.economy.left_unpaid_prompt"),{{TEXT("place_name"),P?P->Name:C.Place},{TEXT("names"),FString::Join(Names,OKT(TEXT("ui.bank.list_separator")))}}));
   UE_LOG(LogTemp,Display,TEXT("Street choice dropped: %s %s, they left %s unsettled"),*C.Id,*C.Kind,*C.Place);
   S.Cart.Reset();S.Choices.RemoveAt(I);RefreshUI();continue;}
  if(S.Choices[I].Deadline<=0||Now<S.Choices[I].Deadline)continue;
  // Her answer cannot come before she has replied to a turn that knew of it (a reply asked before it opened does not),
  // nor while she is still saying it: a gift was taken on its 15 s deadline while she talked on about dango, and she
  // then asked whose cake that was. At most a minute and a half longer, if she never gets a turn.
  {const auto& C=S.Choices[I];
   if(C.Heroine==FreeTalkPartner()&&(S.FreeTalkBusy||FreeTalkHerVoiceBusy()||S.FreeTalkAnswered<=C.SentAtOpen)&&Now<C.OpenedAt+105.0){S.Choices[I].Deadline=Now+5.0;continue;}}
  FOKStreetPendingChoice Done=MoveTemp(S.Choices[I]);S.Choices.RemoveAt(I);
  FString Final=!Done.HerLean.IsEmpty()?Done.HerLean:(Done.Options.IsEmpty()?FString():Done.Options[0]);
  // Her wish to be close: his silence is not a yes (R3-228).
  if(Done.Kind==TEXT("intimacy"))Final=TEXT("decline");
  // The bill is his to decide: what they settled, as the AI read it (shop.rs checkout_timeout).
  if(Done.Kind==TEXT("checkout")){
   auto Request=OKRulesRequest(TEXT("checkout_timeout"));Request->SetStringField(TEXT("lean"),Done.HerLean);Request->SetStringField(TEXT("agreed"),PayAgreed(Done.Heroine));
   if(const TSharedPtr<FJsonObject> A=OKRulesEval(S,Request)){const FString O=A->GetStringField(TEXT("option"));if(Done.Options.Contains(O))Final=O;}}
  UE_LOG(LogTemp,Display,TEXT("Street choice timed out: %s -> %s"),*Done.Id,*Final);
  if(Done.OnResolve&&!Final.IsEmpty())Done.OnResolve(Final);
 }
}

// What the model read from the player (resolve) and what she proposes (propose); outcomes stay with the rules.
void AOKStreetSocialScene::ApplyChoiceReply(int32 Character,const FOKFreeTalkReply& In){
 auto& S=*State;
 // An act written in the activity field ("action":"promise_treat"): read as the act. It was run as an activity, refused,
 // and she was told "no such activity or place"; the treat she promised was not kept.
 FOKFreeTalkReply Reply=In;
 if(Reply.ActKind.IsEmpty()&&OKStreetReplyActOnly(Reply.Action)){Reply.ActKind=Reply.Action;Reply.Action.Reset();}
 // She turned his treat down ("不用请，我自己付"): the next bill with her is each paying their own, also when what she
 // orders is placed later (carried to the shop she is walking him to).
 if(!Reply.Payer.IsEmpty())RecordPayAgreed(Character,Reply.Payer);
 // Every decision of hers is logged, and the screen is captured a moment later (TickFreeTalkTrace) so what she
 // did can be seen: the activity she agreed to, her act, what she ordered, and what they were doing at the time.
 UE_LOG(LogTemp,Display,TEXT("Street decision: actor=%d action=%s act=%s arg=%s orders=%d resolve=%s/%s emotion=%s pair=%d meal=%d seated=%d phase=%d target=%d companion=%d outing=%d lines=%d"),
  Character,*Reply.Action,*Reply.ActKind,*Reply.ActArg,Reply.Orders.Num(),*Reply.ResolveId,*Reply.ResolveOption,*Reply.Emotion,S.Pair.Partner,S.Pair.Meal?1:0,S.Pair.Seated?1:0,S.Pair.Phase,S.Target,S.Companion,S.OutingGirl,Reply.Lines.Num());
 // Goodbye or a treat she promised, when she says so in her structured fields (act end_talk / promise_treat). Whether his
 // goodbye ended the talk, or her words promised to pay, is judged by DeepSeek after the reply (JudgeFreeTalk).
 // A goodbye while he is still walking her somewhere waits for the door (the walk's end resumes the talk); walking off
 // ("leave") does not.
 const bool OnTheWay=S.OutingGirl==Character&&Reply.ActKind!=TEXT("leave");
 if(OnTheWay&&(Reply.ActKind==TEXT("end_talk")||Reply.Action==TEXT("end_talk")||Reply.Action==TEXT("farewell")||Reply.Action==TEXT("goodbye"))){UE_LOG(LogTemp,Display,TEXT("Street farewell: actor=%d not on the way; the outing reaches %s first"),Character,*S.OutingPlace);}
 else if(Reply.ActKind==TEXT("end_talk")||Reply.ActKind==TEXT("leave")||Reply.Action==TEXT("end_talk")||Reply.Action==TEXT("farewell")||Reply.Action==TEXT("goodbye")){
  S.FarewellPending=true;S.FarewellFromTalk=true;S.FarewellAt=FPlatformTime::Seconds();UE_LOG(LogTemp,Display,TEXT("Street farewell: actor=%d the conversation ends after her last line (act=%s)"),Character,*Reply.ActKind);
 }
 if(Reply.ActKind==TEXT("promise_treat")&&OKStreetMindNow(S)-S.HerTreatKept.FindRef(Character,-100000)>=60){/* not again within the hour she paid one (R3-227) */S.HerTreatPromise.Add(Character,OKStreetMindNow(S)+6*60);UE_LOG(LogTemp,Display,TEXT("Street treat promise: actor=%d will pay for what he orders (act)"),Character);}
 // Only when asked for (-DecisionShots): the engine's window readback behind them crashed the game once (E: UE5.8,
 // ConvertDXGIToFColor writing to null), a debugging aid must not take the play down with it.
 static const bool Shots=FParse::Param(FCommandLine::Get(),TEXT("DecisionShots"));
 if(Shots&&S.DecisionShots<200){S.DecisionShotAt=FPlatformTime::Seconds()+1.5;S.DecisionShotTag=!Reply.ActKind.IsEmpty()?Reply.ActKind:!Reply.Action.IsEmpty()?Reply.Action:Reply.Orders.Num()>0?FString(TEXT("order")):FString(TEXT("talk"));}
 if(!Reply.ProposeId.IsEmpty())for(auto& C:S.Choices)if(C.Id==Reply.ProposeId&&C.Heroine==Character&&C.Options.Contains(Reply.ProposeOption)){
  // She may change her mind; her insistence never jumps past what her rules allow by more than a step.
  C.HerLean=Reply.ProposeOption;C.HerInsist=FMath::Clamp(FMath::Max(C.HerInsist,Reply.ProposeInsist*0.8f),0.f,1.f);
 }
 // What she read from him: only from a line he said after the choice opened (her own reply does not decide the bill for him).
 if(!Reply.ResolveId.IsEmpty()){const auto* C=S.Choices.FindByPredicate([&](const FOKStreetPendingChoice& X){return X.Id==Reply.ResolveId;});
  if(C&&C->Kind==TEXT("checkout")&&S.HisLines==C->HisLinesAtOpen){UE_LOG(LogTemp,Display,TEXT("Street choice: %s not resolved by her reply, he has not answered it yet"),*C->Id);}
  else{
   // Settled in her own words ("fine, then I'll take it"): that is her side of the haggling, not a pushback the rules
   // add on top (R6-43: 惠惠 accepted his treat aloud, the rules pushed back for her and the bill timed out to her paying).
   // Still wanting her way, she says so in this reply (propose).
   if(Reply.ProposeId!=Reply.ResolveId)if(auto* M=S.Choices.FindByPredicate([&](const FOKStreetPendingChoice& X){return X.Id==Reply.ResolveId;});M&&M->Heroine==Character&&M->Options.Contains(Reply.ResolveOption))M->HerLean=Reply.ResolveOption;
   ResolveChoice(Reply.ResolveId,Reply.ResolveOption,Reply.ResolveInsist);}}
 // Meetings are booked by the judge, which reads his line and hers (her own promise field cannot tell whether he said yes).
 HandleFinanceReply(Character,Reply.Predict,Reply.Trade,Reply.Bet);
 // Ordering by chat (plan §8.2): only items this place sells; then checkout when asked.
 const FString Place=OrderShopHere(Character);
 int32 Added=0;
 // Not in a shop yet: the one they are about to walk to (this reply's go:place or activity) reads her ids; she has no
 // menu out here and writes "cafe_latte" for the cafe's cf_latte (his americano, ordered on the doorstep, was lost).
 FString Dest=Place.IsEmpty()?OutingShop(Character):Place;
 if(Dest.IsEmpty()){if(Reply.Action.StartsWith(TEXT("go:")))Dest=Reply.Action.Mid(3);else if(const FOKStreetActivity* G=OKStreetFindActivity(Reply.Action);G&&G->Place&&*G->Place)Dest=G->Place;}
 // Two things in one order (oden and a rice ball, starving after her shift) are both ordered: "still has one" is about
 // what was there before this reply, not the line just added from it.
 const int32 CartBefore=S.Cart.Num();
 for(auto O:Reply.Orders){
  O.Id=ResolveItemId(O.Id,Dest);
  const auto* Item=S.Catalog.Find(O.Id);
  const bool ForHer=O.ForHer; // whose it is: the AI's "for"
  // Ordered on the way to that shop ("let's go to the store: a tea egg and water"): carried until they get there.
  const FOKStreetActivity* Going=OKStreetFindActivity(Reply.Action);
  // Or the shop the outing is already walking to (a later reply on the way has no action of its own).
  const bool ShopAhead=Item&&Item->Place!=Place&&(Reply.Action==TEXT("go:")+Item->Place||(Going&&Going->Place&&Item->Place==Going->Place)||(Place.IsEmpty()&&Item->Place==OutingShop(Character)));
  if(ShopAhead){FOKStreetCartLine Carry;Carry.ItemId=O.Id;Carry.ForHer=ForHer;Carry.HerOwn=ForHer;Carry.Size=Item->Price.Contains(O.Size)?O.Size:Item->DefaultSize();
   if(!Item->Options.IsEmpty()){Carry.Sweetness=O.Sweetness.IsEmpty()?OKT(TEXT("ui.mind.full_sugar_label")):O.Sweetness;Carry.Ice=O.Ice.IsEmpty()?OKT(TEXT("ui.mind.normal_state_label")):O.Ice;}
   S.CarriedOrders.FindOrAdd(Character).Add(Carry);S.CarriedOrderSaid.Add(Character,Reply.Reply);UE_LOG(LogTemp,Display,TEXT("Street order carried: %s until they reach %s"),*O.Id,*Item->Place);continue;}
  // Not sold here: she is told, so she says so (胡桃 ordered "tiramisu and red bean soup" at the snack shop's door, both
  // were dropped without a word, and she ate a strawberry cake while his soup never came, R4-28).
  if(!Item||!SoldHere(*Item,Place)){UE_LOG(LogTemp,Display,TEXT("Street order rejected: %s at %s"),*O.Id,*Place);
   S.TellHer.Add(OKMsg(TEXT("ui.economy.not_sold_here_order"),{{TEXT("item"),Item?Item->Name:O.Id}}));continue;}
  // Whoever it is for still has one of these on the table or on its way ("then a tea" again, the tea she ordered a
  // moment ago still coming): not a second one (as for the judge's orders).
  bool HadBefore=false;{const TArray<FOKStreetCartLine> Mine(S.Cart.GetData()+CartBefore,S.Cart.Num()-CartBefore);S.Cart.SetNum(CartBefore);HadBefore=HasDishLike(ForHer?Character:0,*Item);S.Cart.Append(Mine);}
  if(HadBefore&&SecondDishOk(Character,ForHer,O.Id))HadBefore=false; // starving, a different dish (shop.rs order_second_dish)
  if(HadBefore||S.Cart.ContainsByPredicate([&](const FOKStreetCartLine& C){return C.ItemId==O.Id&&C.ForHer==ForHer;})){
   // Another of its kind still on the table, not this one coming: she is told it was not ordered. 胡桃 said "the lemon
   // tart, fine, ordered" while his tiramisu was half eaten, and none came.
   if(HadBefore&&!DishComing(ForHer?Character:0,O.Id))S.TellHer.Add(OKMsg(TEXT("ui.economy.still_has_one_order"),{{TEXT("for"),ForHer?TEXT("her"):TEXT("him")},{TEXT("item"),Item->Name}}));
   UE_LOG(LogTemp,Display,TEXT("Street order skipped: %s, %s still has one"),*O.Id,ForHer?TEXT("she"):TEXT("he"));continue;}
  // A thing (not food) bought for the same one minutes ago (the judge's gift, then her own order for it): not twice.
  if(!Item->Consumable)if(const int64* At=S.RecentBought.Find(O.Id+(ForHer?TEXT("|her"):TEXT("|him")));At&&OKStreetMindNow(S)-*At<=15){
   S.TellHer.Add(OKMsg(TEXT("ui.economy.already_bought_order"),{{TEXT("for"),ForHer?TEXT("her"):TEXT("him")},{TEXT("item"),Item->Name}}));
   UE_LOG(LogTemp,Display,TEXT("Street order skipped: %s, bought for %s %lld min ago"),*O.Id,ForHer?TEXT("her"):TEXT("him"),OKStreetMindNow(S)-*At);continue;}
  // Food for her when she is already full is not ordered (she would leave it after a bite); a drink still is.
  if(ForHer&&TooFullFor(Character,*Item,true)){
   S.TellHer.Add(OKMsg(TEXT("ui.economy.too_full_to_order"),{{TEXT("item"),Item->Name}}));UE_LOG(LogTemp,Display,TEXT("Street order rejected: %s, she is full"),*O.Id);continue;}
  // Not one for herself while she is on her shift serving him: 爱丽丝, making his milk tea at the drink shop, ordered a
  // milk tea for herself and he paid for both (R4-21).
  // Told so: not told, she asked where her milk tea was, "still being made, then" (R5-10).
  if(ForHer&&ShiftUntil(Character)>0){S.TellHer.Add(OKMsg(TEXT("ui.economy.on_shift_no_order"),{{TEXT("item"),Item->Name}}));UE_LOG(LogTemp,Display,TEXT("Street order rejected: %s, she is on her shift"),*O.Id);continue;}
  FOKStreetCartLine Line;Line.ItemId=O.Id;Line.ForHer=ForHer;Line.HerOwn=ForHer;Line.Size=Item->Price.Contains(O.Size)?O.Size:Item->DefaultSize();
  if(!Item->Options.IsEmpty()){Line.Sweetness=O.Sweetness.IsEmpty()?OKT(TEXT("ui.mind.full_sugar_label")):O.Sweetness;Line.Ice=O.Ice.IsEmpty()?OKT(TEXT("ui.mind.normal_state_label")):O.Ice;}
  S.ShopPlace=Place;S.Cart.Add(Line);++Added;
 }
 S.LastPlayerSaid.Reset();
 // Ordered in conversation (his words, or hers): straight to the kitchen, he pays unless she insists.
 // Unless her reply says who pays, it waits for the judge reading his words ("我请你"): billed at once, 雷电将军's latte
 // went by her own lean (split) a second before the judge said he treats (R4-3). TickServes bills it if no verdict comes.
 if(Added>0){if(Reply.Payer.IsEmpty())S.ChatOrderAwaitsJudge.Add(Character,FPlatformTime::Seconds());else PlaceChatOrder(Character,Reply.Reply);}
 if(Reply.ActKind==TEXT("checkout"))StartCheckout();
 // Her own wish waiting for his answer (AskIntimacyWish): the line she asks it with is not his yes. It took 三月七
 // straight into the one-on-one scene in the middle of her song (R4-6).
 else if(Reply.ActKind==TEXT("intimacy")){if(!S.Choices.ContainsByPredicate([Character](const FOKStreetPendingChoice& C){return C.Kind==TEXT("intimacy")&&C.Heroine==Character;}))RequestIntimacy(Character,false);
  else UE_LOG(LogTemp,Display,TEXT("Street life: actor %d asks in her own words; her wish waits for his answer"),Character);}
 // A weekly one: the judge says which ("her" defers). Only on a call with her (R3-268); hers, so it costs him nothing.
 else if(Reply.ActKind==TEXT("cancel_promise")&&S.RemoteMode==2&&S.RemotePeer==Character){S.CancelCallGirl=Character;S.CancelCallGrace=-1;S.CancelCallSaid.Reset();CancelPromiseWith(Character,TEXT("her"));}
 else if(Reply.ActKind==TEXT("entrust")){FString A,D;if(!Reply.ActArg.Split(TEXT("|"),&A,&D))A=Reply.ActArg;StartEntrust(Character,TEXT("entrust"),FCString::Atoi64(*A),D.IsEmpty()?30:FCString::Atoi(*D));}
 else if(Reply.ActKind==TEXT("follow_mode"))SetFollowMode(Reply.ActArg==TEXT("her_lead")?1:Reply.ActArg==TEXT("side_by_side")?2:0);
 else if(Reply.ActKind==TEXT("end_talk")||Reply.ActKind==TEXT("leave")||Reply.ActKind==TEXT("promise_treat")){} // handled above (the goodbye, the promise)
 // "Let's go to the restaurant" with act eat: the eating is there, not here (she ate at the shrine and the keeper came
 // over, R3-126).
 // The meal they are already at ("cafeteria_meal" while eating at the cafeteria, she feeds him) is not somewhere else.
 else if((Reply.ActKind==TEXT("eat")||Reply.ActKind==TEXT("share_food")||Reply.ActKind==TEXT("feed"))&&[&]{
   const FString& A=Reply.Action;if(A.IsEmpty()||A==TEXT("none")||A==TEXT("leave")||A==TEXT("stroll"))return false;
   const FOKStreetActivity* Act=OKStreetFindActivity(A);
   const FString Dest=Act?FString(Act->Place?Act->Place:TEXT("")):A.StartsWith(TEXT("go:"))?A.Mid(3):OKStreetFindPlace(A)?A:FString();
   return !Dest.IsEmpty()&&Dest!=PlaceHere(S.Male);}()){
  UE_LOG(LogTemp,Display,TEXT("Street act: %s left for where they are going (%s)"),*Reply.ActKind,*Reply.Action);}
 else if(!Reply.ActKind.IsEmpty()&&EatActBlocked(Character,Reply.ActKind)){}
 else if(!Reply.ActKind.IsEmpty()&&HandleWorldAct(Character,Reply.ActKind,Reply.ActArg)){} // gift, lend, photo, sleep, elevator (OKStreetSocialLover.inl)
 else if(!Reply.ActKind.IsEmpty()&&DoActHers(Character,Reply.ActKind)){}
 else if(!Reply.ActKind.IsEmpty()&&Reply.ActKind!=TEXT("none"))UnknownAct(Character,Reply.ActKind);
 else if(!Reply.ActKind.IsEmpty())UE_LOG(LogTemp,Display,TEXT("Street act (not yet handled): %s %s"),*Reply.ActKind,*Reply.ActArg);
}


// The stats panel (plan §10.3): exact numbers, the band names, every jealousy target, mood and the tsundere mask.
FString AOKStreetSocialScene::MindPanelText(int32 Character) const{
 if(!State||!State->Manifest.Characters.IsValidIndex(Character))return FString();const auto& S=*State;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 const FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
 const TSharedPtr<FJsonObject> Mind=MindBiasAnswer(Character,false);if(!Mind.IsValid())return FString();
 const FOKStreetBias Bias=OKRulesBiasFrom(Mind);const float Mood=Bias.Mood;
 const TSharedPtr<FJsonObject> Bands=Mind->GetObjectField(TEXT("rival_bands"));
 FString Out=OKMsg(TEXT("ui.mind.admiration_jealousy_summary"), {{TEXT("admiration"), R.Admiration}, {TEXT("admiration_band"), *Mind->GetStringField(TEXT("admiration_band"))}, {TEXT("admiration_stage"), int32(Mind->GetNumberField(TEXT("admiration_stage")))}, {TEXT("jealousy"), R.Jealousy}, {TEXT("jealousy_band"), *Mind->GetStringField(TEXT("jealousy_band"))}});
 for(const auto& Pair:R.JealousyToward){
  FString Name=Pair.Key;for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(OKStreetStableId(S.Manifest.Characters[I])==Pair.Key)Name=S.Manifest.Characters[I].Name;
  Out+=OKMsg(TEXT("ui.mind.jealousy_target_line"), {{TEXT("name"), *Name}, {TEXT("value"), Pair.Value}, {TEXT("band"), *Bands->GetStringField(Pair.Key)}});
 }
 Out+=OKMsg(TEXT("ui.mind.stat_breakdown_line"), {{TEXT("trust"), R.Trust}, {TEXT("attraction"), R.Attraction}, {TEXT("dependency"), R.Dependency}, {TEXT("mystique"), R.Mystique}, {TEXT("greed"), R.Greed}, {TEXT("lifestyle_desire"), R.LifestyleDesire}, {TEXT("respect"), R.Respect}, {TEXT("resentment"), R.Resentment}});
 if(Bias.Masked)Out+=OKMsg(TEXT("ui.mind.tsundere_stat_line"), {{TEXT("true_liking"), Bias.TrueLiking}, {TEXT("shown_affection"), Bias.ShownAffection}, {TEXT("admiration"), R.Admiration}, {TEXT("shown_admiration"), Bias.ShownAdmiration}, {TEXT("peak_jealousy"), Bias.PeakJealousy}, {TEXT("shown_jealousy"), Bias.ShownJealousy}});
 if(Mood>0.05f)Out+=OKMsg(TEXT("ui.mind.mood_line"), {{TEXT("emotion"), *R.Emotion}, {TEXT("mood_percent"), Mood*100.f}});
 // Her spirits and her stomach as she is made (R3-269): satiety in her own units, now / at most.
 {const float Max=SatietyMax(Character),Spirits=MoodNow(Character);Out+=OKMsg(TEXT("ui.mind.spirits_panel"),{{TEXT("band"),OKStreetMoodBand(Spirits)},{TEXT("level"),FMath::RoundToInt(Spirits)},{TEXT("satiety_now"),FMath::RoundToInt(R.Satiety*Max/100.f)},{TEXT("satiety_max"),FMath::RoundToInt(Max)}});}
 if(!Bias.Combos.IsEmpty())Out+=OKT(TEXT("ui.mind.status_header"))+FString::Join(Bias.Combos,OKT(TEXT("ui.bank.list_separator")));
 Out+=OKMsg(TEXT("ui.mind.needs_wallet_line"), {{TEXT("satiety"), R.Satiety}, {TEXT("energy"), R.Energy}, {TEXT("fun"), R.Fun}, {TEXT("social"), R.Social}, {TEXT("hygiene"), R.Hygiene}, {TEXT("cash"), R.Cash}, {TEXT("debt_suffix"), R.Debt>0?*OKMsg(TEXT("ui.mind.debt_amount_note"), {{TEXT("debt"), R.Debt}}):TEXT("")}, {TEXT("appeal"), AppealOf(Character)}});
 // The number in words, as a group talk has it: she ate breakfast in the dorm (satiety 98) and still said "just hungry,
 // let's go" when he asked her to breakfast (R3-212). "Not very hungry" up to 92 let Jeanne Alter at 91, two hours
 // after her noodles, say "I'm a bit hungry too" (R4-12): from 80 she is still full.
 Out+=OKMsg(TEXT("ui.mind.hunger_line"),{{TEXT("hunger"),R.Satiety<35.f?OKT(TEXT("ui.party.very_hungry")):R.Satiety<65.f?OKT(TEXT("ui.party.somewhat_hungry")):R.Satiety<80.f?OKT(TEXT("ui.party.not_very_hungry")):R.Satiety<OKStreetFullSatiety?OKT(TEXT("ui.party.rather_full")):OKT(TEXT("ui.party.very_full"))}});
 {auto Request=OKRulesHeroineRequest(S,TEXT("heroine_private"),Id);Request->SetNumberField(TEXT("charm"),PlayerLook());
  if(const TSharedPtr<FJsonObject> P=OKRulesEval(S,Request)){const float Chance=float(P->GetNumberField(TEXT("chance"))),Resistance=float(P->GetNumberField(TEXT("resistance")));
   Out+=OKMsg(TEXT("ui.mind.chastity_charm_line"), {{TEXT("chastity"), P->GetNumberField(TEXT("chastity"))}, {TEXT("charm_resistance"), int32(P->GetNumberField(TEXT("charm_resistance")))}, {TEXT("player_appeal"), PlayerLook()}, {TEXT("alone_chance_text"), Chance<0.f?OKT(TEXT("ui.mind.lover_by_invite")):Chance<=0.f?*OKMsg(TEXT("ui.mind.no_chance_resistance"), {{TEXT("resistance"), Resistance}}):*OKMsg(TEXT("ui.mind.chance_resistance_label"), {{TEXT("chance_percent"), Chance*100.f}, {TEXT("resistance"), Resistance}})}});}}
 const FString Plan=PlanText(Character);if(!Plan.IsEmpty())Out+=TEXT("\n")+Plan;
 return Out;
}

// Her emotion on her face (plan §4.3), using the source expression catalogue every personality shares. A masked
// tsundere who answers angrily or mockingly blushes instead (plan §10.4 "嘴硬，臉卻紅了").
// Her own "face" of the reply goes further (plan §4.3 表情): any expression of the catalogue, tears, cheek blush, sweat,
// where her eyes are and how open; what the reply leaves out comes from the expression's own sheet.
void AOKStreetSocialScene::ApplyMindExpression(int32 Character,const FString& Emotion,const FOKFreeTalkReply* Reply){
 if(!State||(Emotion.IsEmpty()&&!(Reply&&Reply->FaceGiven))||!State->Manifest.Characters.IsValidIndex(Character)||State->Expressions.Items.IsEmpty())return;auto& S=*State;
 static const TMap<FString,FString> Faces={{TEXT("驚訝"),TEXT("驚き")},{TEXT("崇拜"),TEXT("見つめながらドキドキ")},{TEXT("曖昧"),TEXT("ドキドキ笑顔")},{TEXT("害羞"),TEXT("照れ顔")},
  {TEXT("生氣"),TEXT("怒り１")},{TEXT("吃醋"),TEXT("ちょっと膨れる感じ")},{TEXT("感動"),TEXT("涙目笑顔")},{TEXT("興奮"),TEXT("興奮気味の笑顔")},
  {TEXT("低落"),TEXT("落胆")},{TEXT("疲倦"),TEXT("困り顔")},{TEXT("嘲諷"),TEXT("呆れ")},{TEXT("平常"),TEXT("微笑")},
  // A Simplified game's mood words (the prompt lists them in its own script).
  {TEXT("惊讶"),TEXT("驚き")},{TEXT("暧昧"),TEXT("ドキドキ笑顔")},{TEXT("生气"),TEXT("怒り１")},{TEXT("感动"),TEXT("涙目笑顔")},{TEXT("兴奋"),TEXT("興奮気味の笑顔")},{TEXT("嘲讽"),TEXT("呆れ")}};
 const int32 Personality=S.Manifest.Characters[Character].PersonalityId;
 const FString* Face=Faces.Find(Emotion);
 FString Name=Face?*Face:FString(TEXT("微笑"));
 if((Emotion==OKT(TEXT("ui.life.angry_mood"))||Emotion==OKT(TEXT("ui.mind.mockery_label")))&&BiasOf(Character).Masked)Name=OKT(TEXT("ui.mind.serious_face_shy"));
 // The prompt reaches her in the game's script, the catalogue's Japanese names with it (笑顔 -> 笑颜, 驚き -> 惊き): her
 // answer is read back through the same conversion (locale.rs script_texts), built once per language.
 FString Asked=Reply?Reply->FaceExpr:FString();
 if(!Asked.IsEmpty()&&!S.Expressions.Find(Personality,Asked)){
  static TMap<FString,FString> Alias;static FString AliasLang;
  if(AliasLang!=S.Lang){AliasLang=S.Lang;Alias.Reset();TArray<FString> Own;TArray<TSharedPtr<FJsonValue>> Texts;
   for(const auto& It:S.Expressions.Items){FString Pid,N;if(It.Key.Split(TEXT("\t"),&Pid,&N)&&!Own.Contains(N)){Own.Add(N);Texts.Add(MakeShared<FJsonValueString>(N));}}
   auto Request=OKRulesRequest(TEXT("script_texts"));Request->SetArrayField(TEXT("texts"),Texts);
   if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request)){const auto& Out=Answer->GetArrayField(TEXT("texts"));for(int32 I=0;I<Out.Num()&&I<Own.Num();++I)if(Out[I]->AsString()!=Own[I])Alias.Add(Out[I]->AsString(),Own[I]);}}
  if(const FString* Back=Alias.Find(Asked))Asked=*Back;}
 if(!Asked.IsEmpty()&&S.Expressions.Find(Personality,Asked))Name=Asked;
 else if(Reply&&!Reply->FaceExpr.IsEmpty())UE_LOG(LogTemp,Display,TEXT("Street mind face: actor=%d no expression named %s, %s instead"),Character,*Reply->FaceExpr,*Name);
 // Tears she sheds: a tearful expression when the one chosen has none (warm tears for a moved or happy heart).
 const int32 Tears=Reply?Reply->FaceTears:-1;
 if(Tears>0){const auto* Chosen=S.Expressions.Find(Personality,Name);
  if(!Chosen||Chosen->Tears==0){static const TSet<FString> Warm={TEXT("感動"),TEXT("興奮"),TEXT("曖昧"),TEXT("崇拜"),TEXT("害羞"),TEXT("感动"),TEXT("兴奋"),TEXT("暧昧")};Name=Tears>=2?TEXT("泣き顔"):Warm.Contains(Emotion)?TEXT("涙目笑顔"):TEXT("見つめ合い（ちょっと涙目）");}}
 const auto* Expression=S.Expressions.Find(Personality,Name);if(!Expression)return;
 FKKRuntimeFaceState State2=Expression->Face;bool Blink=Expression->Blink;
 // Her brows, eyes and mouth as she chose them for this line (motions.toml face), over the named expression's: the
 // named ones alone gave every "happy" line the same face.
 FString Parts;
 if(Reply&&(!Reply->FaceBrow.IsEmpty()||!Reply->FaceEyes.IsEmpty()||!Reply->FaceMouth.IsEmpty())){
  auto Ask=OKRulesRequest(TEXT("face_parts"));Ask->SetStringField(TEXT("brow"),Reply->FaceBrow);Ask->SetStringField(TEXT("eyes"),Reply->FaceEyes);Ask->SetStringField(TEXT("mouth"),Reply->FaceMouth);
  if(const TSharedPtr<FJsonObject> P=OKRulesEval(S,Ask)){
   const int32 B=int32(P->GetNumberField(TEXT("brow"))),E=int32(P->GetNumberField(TEXT("eyes"))),M=int32(P->GetNumberField(TEXT("mouth")));
   if(B>=0){State2.BrowPattern=B;State2.BrowBlend=true;}
   if(E>=0){State2.EyePattern=E;State2.EyeBlend=true;if(Reply->FaceEyes==TEXT("closed")||Reply->FaceEyes==TEXT("smile_closed"))Blink=false;else Blink=true;}
   // The mouth she chose also says how far it opens while she is silent (a gasp wide, a smile barely): the named
   // expression's mouth_open was 1 for nearly all of them.
   if(M>=0){State2.MouthPattern=M;State2.MouthBlend=true;double Open=-1;if(P->TryGetNumberField(TEXT("mouth_open"),Open)&&Open>=0)State2.MouthOpening=float(Open);}
   Parts=FString::Printf(TEXT(" brow=%s(%d) eyes=%s(%d) mouth=%s(%d)"),*Reply->FaceBrow,B,*Reply->FaceEyes,E,*Reply->FaceMouth,M);}}
 // Where she looks, in her words ("away", "look away", "down at her feet"): the first direction it names.
 FString Gaze=Reply?Reply->FaceGaze.ToLower():FString();
 for(const TCHAR* Word:{TEXT("closed"),TEXT("away"),TEXT("down"),TEXT("up"),TEXT("him")})if(Gaze.Contains(Word)){Gaze=Word;break;}
 if(Gaze==TEXT("closed")){State2.EyeOpening=0.f;Blink=false;}
 else if(Reply&&Reply->FaceEyesOpen>=0.f)State2.EyeOpening=State2.EyeOpening<0.f?Reply->FaceEyesOpen:FMath::Min(State2.EyeOpening,Reply->FaceEyesOpen);
 FString Error;
 if(OKSetCrowdRuntimeFaceExpression(this,Character,State2,Blink,Error))UE_LOG(LogTemp,Display,TEXT("Street mind expression: actor=%d emotion=%s face=%s%s"),Character,*Emotion,*Name,*Parts);
 FOKRuntimeFaceLook Look;Look.Blush=Reply&&Reply->FaceBlush>=0.f?Reply->FaceBlush:Expression->Blush;
 if(Character>=0&&Character<OKStreetCastCount)Look.Blush=FMath::Max(Look.Blush,S.ArousalBlush[Character]); // her desire past 100 keeps her flushed (TickArousal)
 // Her tears on her face: welling up shows the small drops, crying the large streams (the source's tearsLv 1 / 3);
 // without a word on it, what the expression itself carries.
 Look.Tears=Tears>=0?(Tears>=2?3:Tears):Expression->Tears;Look.Sweat=Reply&&Reply->FaceSweat>=0.f?Reply->FaceSweat:0.f;
 // Looking away goes to one side or the other by turn, not always the same.
 static int32 Side=1;
 if(Gaze==TEXT("away")||(Gaze.IsEmpty()&&Expression->EyesLook>=2)){Side=-Side;Look.Gaze=FVector2D(0.7f*Side,0.15f);}
 else if(Gaze==TEXT("down"))Look.Gaze=FVector2D(0.f,0.6f);else if(Gaze==TEXT("up"))Look.Gaze=FVector2D(0.f,-0.5f);
 if(!OKSetCrowdRuntimeFaceLook(this,Character,Look,Error)){UE_LOG(LogTemp,Display,TEXT("Street mind face look: actor=%d %s"),Character,*Error);}
 else if(Reply&&Reply->FaceGiven)UE_LOG(LogTemp,Display,TEXT("Street mind face: actor=%d expr=%s blush=%.2f tears=%d sweat=%.2f gaze=%s"),Character,*Name,Look.Blush,Tears,Look.Sweat,*Gaze);
}

// What this place sells, for the 【這裡的菜單】 line (empty outside a shop or when it is closed).
// Food for her that she could not finish: her satiety, what she still has coming (her dishes on the table not yet
// eaten, and in the cart) and this dish together over OKStreetOverfullSatiety. A drink always fits.
bool AOKStreetSocialScene::TooFullFor(int32 Girl,const FOKStreetCatalogItem& Item,bool HerChoice) const{
 // Whether she could still finish it is the rules' (shop.rs order_too_full): here only what she has coming.
 const auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl))return false;
 auto Dish=[](const FOKStreetCatalogItem& I,double Left){auto O=MakeShared<FJsonObject>();const float* V=I.Effects.Find(TEXT("satiety"));
  O->SetNumberField(TEXT("fill"),V?*V:0.f);O->SetStringField(TEXT("category"),I.Category);O->SetBoolField(TEXT("drink"),I.IsDrink());O->SetNumberField(TEXT("left"),Left);return O;};
 TArray<TSharedPtr<FJsonValue>> Coming;
 for(const auto& D:S.Table)if(D.ForHer&&D.Percent>0.f)if(const auto* I=S.Catalog.Find(D.ItemId))Coming.Add(MakeShared<FJsonValueObject>(Dish(*I,D.Percent/100.f)));
 for(const auto& C:S.Cart)if(C.ForHer)if(const auto* I=S.Catalog.Find(C.ItemId))Coming.Add(MakeShared<FJsonValueObject>(Dish(*I,1.0)));
 auto Request=OKRulesRequest(TEXT("order_too_full"));Request->SetArrayField(TEXT("coming"),Coming);Request->SetObjectField(TEXT("item"),Dish(Item,1.0));Request->SetBoolField(TEXT("her_choice"),HerChoice); // she picked it herself (R6-12)
 Request->SetNumberField(TEXT("satiety"),OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Girl]),S.Manifest.TalkBudget).Satiety);
 if(const TSharedPtr<FJsonObject> P=ProfileOf(Girl))Request->SetObjectField(TEXT("profile"),P.ToSharedRef()); // her appetite (R3-269)
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);return Out.IsValid()&&Out->GetBoolField(TEXT("too_full"));
}

FString AOKStreetSocialScene::MenuText(float Satiety,float Appetite,int32 Girl) const{
 // On the way to a shop with her, that shop's: without it she made up ids ("drink_bubble_tea"), was told the drink shop
 // has none, and said "no bubble tea here?" of the 珍珠奶茶 on its menu.
 FString Place=ShopPlaceHere();const bool Ahead=Place.IsEmpty();if(Ahead)Place=OutingShop(Girl);if(Place.IsEmpty())return FString();
 FString Why;if(PlaceClosedNow(Place,Why))return Why;
 TArray<FString> Lines;
 if(Ahead){const auto* P=OKStreetFindPlace(Place);Lines.Add(OKMsg(TEXT("ui.mind.menu_ahead"),{{TEXT("place"),P?P->Name:Place}}));}
 // How much she can still eat (she stops at OKStreetFullSatiety): a dish that fills more than that is marked, so she
 // does not order a whole meal and leave it after a bite.
 const float Room=Satiety>=0.f?OKStreetOverfullSatiety-Satiety:1000.f;
 // In words, not the number: a model given "satiety 98/100" says "satiety 98, I'm not hungry" aloud.
 if(Satiety>=0.f&&ItemsHere(Place).ContainsByPredicate([](const FOKStreetCatalogItem* I){return I&&I->Consumable;}))Lines.Add(OKMsg(TEXT("ui.mind.menu_room"),{{TEXT("band"),Satiety>=OKStreetFullSatiety-15.f?TEXT("full"):Satiety>=45.f?TEXT("some"):TEXT("hungry")}}));
 for(const auto* I:ItemsHere(Place)){
  TArray<FString> Prices;for(const auto& P:I->Price)Prices.Add(P.Key.IsEmpty()?FString::FromInt(P.Value):P.Key+FString::FromInt(P.Value));
  const float Fills=OKStreetItemFill(*I); // a drink fits even when full
  // Non-food on the list (the park's fish food) is marked: right under "you just ate", she turned it down as "not hungry".
  Lines.Add(FString::Printf(TEXT("%s %s %s"),*I->Id,*I->Name,*FString::Join(Prices,TEXT("/")))+(!I->Consumable?OKT(TEXT("ui.mind.menu_not_food")):Fills*Appetite>Room?OKT(TEXT("ui.mind.menu_too_much")):FString()));
 }
 if(!State->Cart.IsEmpty()){TArray<FString> Cart;for(const auto& L:State->Cart)if(const auto* I=State->Catalog.Find(L.ItemId))Cart.Add((L.ForHer?OKT(TEXT("ui.economy.she_speaker_label")):OKT(TEXT("ui.mind.he_label")))+I->Name);Lines.Add(OKT(TEXT("ui.mind.ordered_label"))+FString::Join(Cart,OKT(TEXT("ui.bank.list_separator"))));}
 return FString::Join(Lines,OKT(TEXT("ui.economy.semicolon_mark")));
}

// The place a point stands in (walk-in places only), or empty on the street.
FString AOKStreetSocialScene::PlaceHere(const FVector& Where) const{
 for(const auto& Place:OKStreetPlaces())if(OKStreetInsidePlace(Place,Where))return Place.Id;
 return FString();
}

// Plan §7 [場景]: where they are, the place's norms, how crowded, and who they know is near.
FString AOKStreetSocialScene::SceneLine(int32 Character) const{
 const auto& S=*State;
 const FString Place=PlaceHere(S.Male);const FOKStreetPlaceNorms N=OKStreetNormsOf(Place);
 const FDateTime Now=StreetNow();const float Crowd=OKRulesCrowdAt(S,Place,Now.GetHour()+Now.GetMinute()/60.0);
 const auto* P=Place.IsEmpty()?nullptr:OKStreetFindPlace(Place);
 int32 Floor=0;if(P)for(int32 I=0;I<P->Rooms.Num();++I)if(P->Rooms[I].IsInsideOrOn(S.Male)&&P->RoomLevels.IsValidIndex(I))Floor=P->RoomLevels[I];
 static const TCHAR* Privacy[]={TEXT("公共"),TEXT("半公共"),TEXT("可上鎖"),TEXT("私密")};
 FString Line=OKMsg(TEXT("ui.mind.location_info_line"), {{TEXT("place_name_or_street"), P?P->Name:OKT(TEXT("ui.etiquette.street_location"))}, {TEXT("floor_suffix"), Floor>1?*FString::Printf(TEXT(" %dF"),Floor):TEXT("")}, {TEXT("location_label"), *N.Label}, {TEXT("privacy_label"), OKT(Privacy[FMath::Clamp(N.Privacy,0,3)])}, {TEXT("privacy_level"), N.Privacy}, {TEXT("quiet_label"), N.Quiet>=0.7f?OKT(TEXT("ui.mind.quiet_required")):N.Quiet<=0.2f?OKT(TEXT("ui.mind.busy_label")):OKT(TEXT("ui.mind.normal_volume_label"))}, {TEXT("food_label"), N.FoodAllowed?OKT(TEXT("ui.mind.food_allowed_tag")):OKT(TEXT("ui.mind.food_banned_tag"))}, {TEXT("staff_label"), N.Staff?OKT(TEXT("ui.mind.has_staff_tag")):TEXT("")}, {TEXT("crowd_label"), Crowd>=0.7f?OKT(TEXT("ui.mind.crowd_many")):Crowd>=0.35f?OKT(TEXT("ui.mind.normal_label")):OKT(TEXT("ui.mind.crowd_few"))}});
 TArray<FString> Known;
 // A name inside her own (贞德 beside 贞德·Alter) is someone else: asked "is that Jeanne over there?", Jeanne Alter
 // answered "she is me, I am her" (R5-14).
 const FString Own=S.Manifest.Characters.IsValidIndex(Character)?S.Manifest.Characters[Character].Name:FString();
 for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(I!=Character&&S.Agents.IsValidIndex(I-1)){const float D=FVector::Dist2D(S.Agents[I-1].Position,S.Male)/100.f;const FString& Name=S.Manifest.Characters[I].Name;
  const bool Overlaps=!Own.IsEmpty()&&!Name.IsEmpty()&&(Own.Contains(Name)||Name.Contains(Own));
  if(D<25.f)Known.Add(OKMsg(TEXT("ui.mind.nearby_person_distance"), {{TEXT("name"), *Name}, {TEXT("distance"), D}})+(Overlaps?OKT(TEXT("ui.mind.nearby_not_you")):TEXT("")));}
 if(!Known.IsEmpty())Line+=OKT(TEXT("ui.mind.known_people_label"))+FString::Join(Known,OKT(TEXT("ui.bank.list_separator")));
 // On the way somewhere with him: said only "street", she took his "we're here, go ask" for arriving and spoke to the
 // dorm manager from the road (R6-66).
 if(S.OutingGirl==Character&&Character!=INDEX_NONE){FString Dest=S.OutingActivity.StartsWith(TEXT("go:"))?S.OutingActivity.Mid(3):FString();
  if(Dest.IsEmpty())if(const FOKStreetActivity* A=OKStreetFindActivity(S.OutingActivity))Dest=A->Place?A->Place:TEXT("");
  if(!Dest.IsEmpty()&&Dest!=Place)if(const auto* To=OKStreetFindPlace(Dest))Line+=OKMsg(TEXT("ui.mind.heading_to_line"),{{TEXT("place"),To->Name}});}
 const FString Things=ThingsFacts(Character);
 return Line+ActivityBlockedLine(Character)+(Things.IsEmpty()?FString():OKT(TEXT("ui.bank.list_separator"))+Things)+FinanceSceneText();
}

// Her things, and what is lent between them (items.rs items_facts): for her words and for the judge.
// He confessed to her where others could see (within 10 m, the same floor): each of them remembers it and her answer
// (yes, no, not yet). Anko sat two metres away while he asked Yuuka out and only had yesterday's dinner to be jealous of.
void AOKStreetSocialScene::ConfessionWitnessed(int32 Girl,const TCHAR* Outcome){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl))return;const FString Place=PlaceHere(S.Male);
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  if(I==Girl||!S.Agents.IsValidIndex(I-1))continue;const FVector& At=S.Agents[I-1].Position;
  if(FVector::Dist2D(At,S.Male)>1000.f||FMath::Abs(At.Z-S.Male.Z)>300.f)continue;
  MindObserve(I,TEXT("saw_confession"),OKMsg(TEXT("ui.memory.saw_confession"),{{TEXT("name"),S.Manifest.Characters[Girl].Name},{TEXT("outcome"),Outcome}}),Place,0.8f);
  UE_LOG(LogTemp,Display,TEXT("Street witness: actor %d saw him confess to actor %d (%s)"),I,Girl,Outcome);
 }
}

FString AOKStreetSocialScene::ThingsFacts(int32 Character) const{
 const auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Character)||Character<=0)return FString();
 const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);auto Request=OKRulesHeroineRequest(S,TEXT("items_facts"),Id);OKRulesPutExt(S,Request);Request->SetStringField(TEXT("girl"),Id);
 // Raining now: whether she has an umbrella with her goes in (said and walked the same way).
 Request->SetBoolField(TEXT("raining"),RainingAt(S.Store.CalendarDay,int32(OKStreetMindNow(S)%1440)));Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);FString Text=Out.IsValid()?Out->GetStringField(TEXT("text")):FString();
 FString Loan;{auto Ask=OKRulesHeroineRequest(S,TEXT("errand_facts"),Id);OKRulesPutExt(S,Ask);Ask->SetStringField(TEXT("girl"),Id);if(const TSharedPtr<FJsonObject> L=OKRulesEval(S,Ask))Loan=L->GetStringField(TEXT("text"));}
 // Festivals today and coming soon (events.rs): hers to bring up or ask him to, or not.
 FString Events;{auto Ask=OKRulesRequest(TEXT("events_facts"));Ask->SetNumberField(TEXT("day"),S.Store.CalendarDay);if(const TSharedPtr<FJsonObject> E=OKRulesEval(S,Ask))Events=E->GetStringField(TEXT("text"));}
 for(const FString& More:{PhotoFacts(Character),ReadingFacts(Character),Loan,Events})if(!More.IsEmpty())Text+=(Text.IsEmpty()?FString():FString(OKT(TEXT("ui.economy.semicolon_mark"))))+More; // one fact from the next: "；"
 return Text;
}

// The "give it back" button: the same rule the judge's he_returns_item goes through.
void AOKStreetSocialScene::ReturnBorrowed(int32 Her,const FString& ItemId){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Her))return;
 auto Request=OKRulesRequest(TEXT("items_judge"));OKRulesPutExt(S,Request);Request->SetStringField(TEXT("girl"),OKStreetStableId(S.Manifest.Characters[Her]));Request->SetStringField(TEXT("he_returns_item"),ItemId);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return;
 auto Candidate=S.Store;OKRulesTakeExt(Candidate,*Out);if(!CommitStore(MoveTemp(Candidate)))return;
 const FString Notice=Out->GetStringField(TEXT("notice"));
 if(!Notice.IsEmpty()){S.FreeTalkNotice=Notice;S.TellHer.Add(OKMsg(TEXT("ui.scene.game_fact_note"),{{TEXT("fact"),Notice}}));} // she hears it as a fact of the moment, and answers it
 UE_LOG(LogTemp,Display,TEXT("Street things: he returns %s to actor=%d (%s)"),*ItemId,Her,*Notice);RefreshUI();
}

// Catalogue effects on her needs (eating, drinking, a spa...), and mood.
void AOKStreetSocialScene::FeedHer(int32 Character,const TMap<FString,float>& Effects){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Character))return;
 const int32 I=OKStreetRelationIndex(S.Store,OKStreetStableId(S.Manifest.Characters[Character]),S.Manifest.TalkBudget,true);if(!S.Store.Relations.IsValidIndex(I))return;
 auto& R=S.Store.Relations[I];const float Appetite=100.f/SatietyMax(Character); // her stomach's share of the food (R3-269)
 for(const auto& E:Effects){if(E.Key==TEXT("satiety")){R.Satiety=FMath::Clamp(R.Satiety+E.Value*Appetite,0.f,100.f);continue;}float* V=E.Key==TEXT("satiety")?&R.Satiety:E.Key==TEXT("energy")?&R.Energy:E.Key==TEXT("fun")?&R.Fun:E.Key==TEXT("social")?&R.Social:E.Key==TEXT("hygiene")?&R.Hygiene:nullptr;if(V)*V=FMath::Clamp(*V+E.Value,0.f,100.f);}
}

// Money stress (plan §4.3): a week's spending by her style against her cash, and urgent needs she cannot cover.
float AOKStreetSocialScene::MoneyStressOf(const FOKStreetRelation& R,const TSharedPtr<FJsonObject>& Profile,bool& Secret) const{
 return OKRulesMoneyStress(*State,R,Profile,Secret); // mind.rs money_stress: style, urgent needs, debt
}

// An action asked for in chat or the menu (plan §9.3): the place decides how awkward it is, she reacts by her
// shyness, staff may speak up, and it becomes a memory. Returns false for unknown acts.
bool AOKStreetSocialScene::DoAct(int32 Character,const FString& Act){
 const FOKHeroineAct* A=OKHeroineFind(Act);
 if(!A||A->Cat!=TEXT("act")||!State->Manifest.Characters.IsValidIndex(Character))return false;
 FOKHeroineArgs Args;Args.Hers=State->ActIsHers;Interact(Character,Act,Args);
 return true;
}

// Food carried from a shop (plan §11 "在圖書館吃洋芋片"): eaten wherever they are, with the place's reaction.
void AOKStreetSocialScene::EatFromBag(const FString& ItemId,bool Feed){
 auto& S=*State;const auto* Item=S.Catalog.Find(ItemId);const int32 Her=FreeTalkPartner();
 if(!Item||!S.Store.Inventory.Contains(ItemId))return;
 auto Candidate=S.Store;Candidate.Inventory.RemoveSingle(ItemId);CommitStore(MoveTemp(Candidate));
 if(Her!=INDEX_NONE){FeedHer(Her,Item->Effects);DoAct(Her,Feed?TEXT("feed"):Item->Tags.Contains(TEXT("shareable"))?TEXT("share_food"):TEXT("eat"));}
 else{S.FreeTalkNotice=OKMsg(TEXT("ui.mind.you_ate_item"), {{TEXT("item_name"), *Item->Name}});RefreshUI();}
}

// The end of a game day (plan §6.5), before the calendar moves on: heroines who spent time with him reflect through
// DeepSeek on their own context; the others get a rule-written summary and a money check. Runs once per day.
void AOKStreetSocialScene::MindEndOfDay(){
 auto& S=*State;if(!S.Mind||S.ReflectedDay==S.Store.CalendarDay)return;S.ReflectedDay=S.Store.CalendarDay;
 CheckBirthdays();S.InitiativesToday=0;
 const int64 Now=OKStreetMindNow(S);
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);
  // Her day from her plan, in words (both paths use it).
  TArray<FString> Parts;if(const auto* Plan=S.Plans.Find(I))for(const auto& B:*Plan)if(B.Kind!=TEXT("sleep")&&B.Kind!=TEXT("leisure")){const auto* P=OKStreetFindPlace(B.Place);Parts.Add(FString::Printf(TEXT("%s%s"),B.Kind==TEXT("work")?OKT(TEXT("ui.mind.work_activity_label")):B.Kind==TEXT("class")?OKT(TEXT("ui.mind.class_activity_label")):B.Kind==TEXT("meal")?OKT(TEXT("ui.mind.meal_activity_label")):TEXT(""),P?P->Name:OKT(TEXT("ui.etiquette.street_location"))));}
  const FString Today=FString::Join(Parts,OKT(TEXT("ui.bank.list_separator")));
  if(S.InteractedToday.Contains(I)){
   TSharedRef<FJsonObject> Job=MakeShared<FJsonObject>();Job->SetStringField(TEXT("kind"),TEXT("reflect"));Job->SetStringField(TEXT("heroine"),Id);
   Job->SetNumberField(TEXT("day"),double(Now/1440));Job->SetNumberField(TEXT("minute"),double(Now%1440));Job->SetStringField(TEXT("today"),Today);
   FString Error;if(!S.Mind->Submit(OKStreetMindJson(Job),Error))UE_LOG(LogTemp,Warning,TEXT("StreetSocial reflect request failed: %s"),*Error);
   continue;
  }
  if(!Today.IsEmpty())MindObserve(I,TEXT("summary"),OKT(TEXT("ui.mind.today_label"))+Today,FString(),0.25f,TEXT("summary"));
  const FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);bool Secret=false;
  const float Stress=MoneyStressOf(R,S.MindProfiles.FindRef(Id),Secret);
  if(Stress>60){MindObserve(I,TEXT("reflection"),OKT(TEXT("ui.mind.month_tight_more_shifts")),FString(),0.6f,TEXT("reflection"));S.Intents.Add(Id,TEXT("more_shifts"));}
  else if(R.Social<30)S.Intents.Add(Id,TEXT("seek_player"));
  else S.Intents.Remove(Id);
 }
 S.InteractedToday.Reset();
}

// Her reflection came back: long-term changes (at most +-3 per axis, plan §6.5) and what she wants tomorrow.
void AOKStreetSocialScene::HandleReflect(const TSharedPtr<FJsonObject>& R){
 auto& S=*State;FString Id;bool Ok=false;R->TryGetStringField(TEXT("heroine"),Id);R->TryGetBoolField(TEXT("ok"),Ok);if(!Ok||Id.IsEmpty())return;
 FString Intent;if(R->TryGetStringField(TEXT("intent"),Intent)&&Intent!=TEXT("none"))S.Intents.Add(Id,Intent);else S.Intents.Remove(Id);
 const TSharedPtr<FJsonObject>* Adjust=nullptr;
 if(R->TryGetObjectField(TEXT("rel_adjust"),Adjust)){
  // Her reflection nudges her axes, at most 3 each way (talk.rs reflect_adjust).
  auto Request=OKRulesHeroineRequest(S,TEXT("reflect_adjust"),Id);Request->SetObjectField(TEXT("adjust"),*Adjust);
  if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request)){ApplyRulesRelation(Id,*Out);UE_LOG(LogTemp,Display,TEXT("Street reflection %s: %s intent=%s"),*Id,*Out->GetStringField(TEXT("applied")),*Intent);}
 }
}

// He told her himself about another girl (the judge's told_about): if that is the girl she is jealous of, she heard it
// from him and it no longer sends her to have it out with him as a rumour (social.rs rival_named compares the names).
void AOKStreetSocialScene::ToldAboutRival(int32 Character,const FString& Name){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Character))return;
 auto Ask=OKRulesHeroineRequest(S,TEXT("rival_named"),OKStreetStableId(S.Manifest.Characters[Character]));Ask->SetStringField(TEXT("told"),Name);
 auto Names=MakeShared<FJsonObject>();for(int32 I=1;I<S.Manifest.Characters.Num();++I)Names->SetStringField(OKStreetStableId(S.Manifest.Characters[I]),S.Manifest.Characters[I].Name);Ask->SetObjectField(TEXT("names"),Names);
 if(const TSharedPtr<FJsonObject> Named=OKRulesEval(S,Ask);Named.IsValid()&&Named->GetBoolField(TEXT("named"))){
  auto Candidate=S.Store;ApplyRulesEffects(Candidate,*Named);if(CommitStore(MoveTemp(Candidate)))FlushRulesEffects();
  UE_LOG(LogTemp,Display,TEXT("Street rival named: actor=%d heard about %s from him; no rumour confrontation for it"),Character,*Name);}
}
