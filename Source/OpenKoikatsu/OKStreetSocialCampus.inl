// Included by OKStreetSocialScene.cpp: the campus (Docs/STREET_SOCIAL_CAMPUS_PLAN.md) — enrolment, attendance,
// exams graded from intelligence, studying (alone or with her), stairs, the department office and the timetable app.

FOKStreetAcademic* AOKStreetSocialScene::AcademicOf(FOKStreetStore& Store,int32 Character){
 if(Character==0)return &Store.PlayerAcademic;
 if(!State->Manifest.Characters.IsValidIndex(Character))return nullptr;
 return Store.Academics.Find(OKStreetStableId(State->Manifest.Characters[Character]));
}

// The campus minute is a rule (Tools/ok-rules src/academics.rs campus_tick): records, new terms, attendance, exams and
// the scholarship, studying, expulsion and the entrance exam again. Here the facts go in (where he is, whom she studies
// beside, where her plan put each heroine at each class) and the records, messages and memories come back.
void AOKStreetSocialScene::TickAcademics(){
 auto& S=*State;if(S.SelfTest)return;
 const FDateTime Now=StreetNow();const int32 Minute=Now.GetHour()*60+Now.GetMinute();
 const int64 Key=int64(S.Store.CalendarDay)*1440+Minute;if(Key==S.AcademicMinute)return;
 // Game minutes since the last tick (time skips count; capped so a long sleep is not a study marathon).
 const int32 Elapsed=S.AcademicMinute<0?1:int32(FMath::Clamp<int64>(Key-S.AcademicMinute,0,180));S.AcademicMinute=Key;
 auto Request=OKRulesRequest(TEXT("campus_tick"));
 Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);Request->SetNumberField(TEXT("minute"),Minute);Request->SetNumberField(TEXT("elapsed"),Elapsed);Request->SetStringField(TEXT("here"),PlaceHere(S.Male));
 Request->SetObjectField(TEXT("player"),OKStreetAcademicJson(S.Store.PlayerAcademic));
 const int32 Partner=S.Studying?StudyPartner():INDEX_NONE;if(Partner!=INDEX_NONE)S.StudiedWith.Add(Partner);
 TArray<TSharedPtr<FJsonValue>> Heroines;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);auto H=MakeShared<FJsonObject>();
  H->SetNumberField(TEXT("actor"),I);H->SetStringField(TEXT("id"),Id);H->SetStringField(TEXT("name"),S.Manifest.Characters[I].Name);
  if(const auto P=ProfileOf(I))H->SetObjectField(TEXT("profile"),P.ToSharedRef());
  if(const auto* A=S.Store.Academics.Find(Id))H->SetObjectField(TEXT("academic"),OKStreetAcademicJson(*A));
  // Where her plan put her in the middle of each class slot (09:00-12:00, 13:30-16:30).
  TArray<TSharedPtr<FJsonValue>> Slots;for(const int32 Mid:{630,900}){const auto* B=BlockNow(I,Mid);Slots.Add(MakeShared<FJsonValueString>(B&&B->Kind==TEXT("class")?B->Place:FString()));}
  H->SetArrayField(TEXT("slot_class"),Slots);
  {const auto* B=BlockNow(I,Minute);H->SetBoolField(TEXT("studying"),B&&B->Kind==TEXT("study")&&S.Agents.IsValidIndex(I-1)&&!S.Commutes.Contains(I)&&PlaceHere(S.Agents[I-1].Position)==B->Place);}
  Heroines.Add(MakeShared<FJsonValueObject>(H));
 }
 Request->SetArrayField(TEXT("heroines"),Heroines);
 auto State_=MakeShared<FJsonObject>();
 {TArray<TSharedPtr<FJsonValue>> Seen;for(const int32 C:S.SeenInClass)Seen.Add(MakeShared<FJsonValueNumber>(C));State_->SetArrayField(TEXT("seen_in_class"),Seen);}
 State_->SetStringField(TEXT("class_closed"),S.ClassClosed);
 {TArray<TSharedPtr<FJsonValue>> Done;for(const FString& D:S.ExamsDone)Done.Add(MakeShared<FJsonValueString>(D));State_->SetArrayField(TEXT("exams_done"),Done);}
 State_->SetBoolField(TEXT("studying"),S.Studying);State_->SetNumberField(TEXT("study_minutes"),S.StudyMinutes);State_->SetNumberField(TEXT("study_applied"),S.StudyApplied);State_->SetNumberField(TEXT("study_partner"),Partner);
 {auto Her=MakeShared<FJsonObject>();for(const auto& P:S.HerStudy)Her->SetNumberField(FString::FromInt(P.Key),P.Value);State_->SetObjectField(TEXT("her_study"),Her);}
 Request->SetObjectField(TEXT("state"),State_);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 // Scene state.
 const TSharedPtr<FJsonObject> St=Answer->GetObjectField(TEXT("state"));
 S.SeenInClass.Reset();for(const auto& V:St->GetArrayField(TEXT("seen_in_class")))S.SeenInClass.Add(int32(V->AsNumber()));
 S.ClassClosed=St->GetStringField(TEXT("class_closed"));
 S.ExamsDone.Reset();for(const auto& V:St->GetArrayField(TEXT("exams_done")))S.ExamsDone.Add(V->AsString());
 S.Studying=St->GetBoolField(TEXT("studying"));S.StudyMinutes=int32(St->GetNumberField(TEXT("study_minutes")));S.StudyApplied=int32(St->GetNumberField(TEXT("study_applied")));
 S.HerStudy.Reset();for(const auto& P:St->GetObjectField(TEXT("her_study"))->Values)S.HerStudy.Add(FCString::Atoi(*FString(*P.Key)),int32(P.Value->AsNumber()));
 {const FString Message=Answer->GetStringField(TEXT("message"));if(!Message.IsEmpty())S.Message=Message;}
 if(!Answer->GetBoolField(TEXT("changed")))return;
 auto Candidate=S.Store;
 OKStreetReadAcademic(Answer->GetObjectField(TEXT("player")),Candidate.PlayerAcademic);
 for(const auto& P:Answer->GetObjectField(TEXT("academics"))->Values){FOKStreetAcademic A;OKStreetReadAcademic(P.Value->AsObject(),A);Candidate.Academics.Add(FString(*P.Key),A);}
 ApplyRulesEffects(Candidate,*Answer);
 CommitStore(MoveTemp(Candidate));
}

// Studying happens where the rules allow it (academics.rs study_place).
bool AOKStreetSocialScene::StudyPlaceHere(FString* Why) const{
 auto Request=OKRulesRequest(TEXT("study_place"));Request->SetStringField(TEXT("place"),PlaceHere(State->Male));
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(*State,Request);if(!Answer)return false;
 if(Answer->GetBoolField(TEXT("ok")))return true;
 if(Why)*Why=Answer->GetStringField(TEXT("why"));return false;
}

int32 AOKStreetSocialScene::StudyPartner() const{
 const auto& S=*State;
 for(const int32 I:{FreeTalkPartner(),S.Companion,S.Target})if(S.Manifest.Characters.IsValidIndex(I)&&I>0&&S.Agents.IsValidIndex(I-1)&&FVector::Dist(S.Agents[I-1].Position,S.Male)<400)return I;
 return INDEX_NONE;
}

void AOKStreetSocialScene::ToggleStudy(){
 auto& S=*State;FString Why;
 if(!S.Studying){if(!StudyPlaceHere(&Why)){S.Message=Why;RefreshUI();return;}S.Studying=true;S.StudyMinutes=S.StudyApplied=0;S.StudiedWith.Reset();
  const int32 Her=StudyPartner();S.Message=Her!=INDEX_NONE?OKMsg(TEXT("ui.campus.studying_with_name"), {{TEXT("name"), *S.Manifest.Characters[Her].Name}}):OKT(TEXT("ui.campus.studying_label"));RefreshUI();return;}
 S.Studying=false;
 if(S.StudyMinutes>S.StudyApplied){
  auto Candidate=S.Store;const int32 Her=StudyPartner();FOKStreetAcademic* HerRecord=Her!=INDEX_NONE?AcademicOf(Candidate,Her):nullptr;
  auto Request=OKRulesRequest(TEXT("campus_study"));Request->SetObjectField(TEXT("player"),OKStreetAcademicJson(Candidate.PlayerAcademic));Request->SetField(TEXT("her"),OKRulesAcademicValue(HerRecord));Request->SetNumberField(TEXT("minutes"),S.StudyMinutes-S.StudyApplied);
  if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request)){
   OKStreetReadAcademic(Answer->GetObjectField(TEXT("player")),Candidate.PlayerAcademic);
   const TSharedPtr<FJsonObject>* HerOut=nullptr;if(HerRecord&&Answer->TryGetObjectField(TEXT("her"),HerOut))OKStreetReadAcademic(*HerOut,*HerRecord);
   S.StudyApplied=S.StudyMinutes;CommitStore(MoveTemp(Candidate));
  }
 }
 const float Int=S.Store.PlayerAcademic.Intelligence;
 for(const int32 Her:S.StudiedWith){
  const FOKStreetAcademic* A=S.Store.Academics.Find(OKStreetStableId(S.Manifest.Characters[Her]));
  MindObserve(Her,TEXT("study"),OKMsg(TEXT("ui.campus.studied_with_him"), {{TEXT("minutes"), S.StudyMinutes}, {TEXT("teaching_suffix"), Int>=75?OKT(TEXT("ui.campus.tutor_praise_note")):TEXT("")}}),PlaceHere(S.Male),0.5f);
  S.MindRecent.Add(OKStreetStableId(S.Manifest.Characters[Her]),OKMsg(TEXT("ui.campus.studied_together_recap"), {{TEXT("minutes"), S.StudyMinutes}, {TEXT("intelligence"), A?A->Intelligence:0.f}}));
 }
 S.Message=OKMsg(TEXT("ui.campus.studied_minutes_summary"), {{TEXT("minutes"), S.StudyMinutes}, {TEXT("intelligence"), Int}});RefreshUI();
}

// A study activity together (activities.toml yields "study": the dorm lounge, a classroom): its minutes count as studying
// with her (campus_study), as the study button does. Two hours over their notes at the lounge used to count for nothing.
void AOKStreetSocialScene::StudiedTogether(int32 Girl,int32 Minutes){
 auto& S=*State;if(Minutes<=0||S.Studying||!S.Manifest.Characters.IsValidIndex(Girl))return; // the button's own session counts them already
 auto Candidate=S.Store;FOKStreetAcademic* HerRecord=AcademicOf(Candidate,Girl);
 auto Request=OKRulesRequest(TEXT("campus_study"));Request->SetObjectField(TEXT("player"),OKStreetAcademicJson(Candidate.PlayerAcademic));Request->SetField(TEXT("her"),OKRulesAcademicValue(HerRecord));Request->SetNumberField(TEXT("minutes"),Minutes);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 OKStreetReadAcademic(Answer->GetObjectField(TEXT("player")),Candidate.PlayerAcademic);
 const TSharedPtr<FJsonObject>* HerOut=nullptr;if(HerRecord&&Answer->TryGetObjectField(TEXT("her"),HerOut))OKStreetReadAcademic(*HerOut,*HerRecord);
 if(!CommitStore(MoveTemp(Candidate)))return;
 const float Int=S.Store.PlayerAcademic.Intelligence;
 MindObserve(Girl,TEXT("study"),OKMsg(TEXT("ui.campus.studied_with_him"),{{TEXT("minutes"),Minutes},{TEXT("teaching_suffix"),Int>=75?OKT(TEXT("ui.campus.tutor_praise_note")):TEXT("")}}),PlaceHere(S.Male),0.5f);
 S.Message=OKMsg(TEXT("ui.campus.studied_minutes_summary"),{{TEXT("minutes"),Minutes},{TEXT("intelligence"),Int}});
 UE_LOG(LogTemp,Display,TEXT("Street study: %d minutes with actor %d (an activity together), intelligence %.1f"),Minutes,Girl,Int);
}

// ------------------------------------------------------------------------------------------------ stairs

// The stair landing he stands on: (place index, floor) or false.
bool AOKStreetSocialScene::StairHere(int32& PlaceIndex,int32& Floor) const{
 const auto& Places=OKStreetPlaces();const FVector M=State->Male;
 for(int32 P=0;P<Places.Num();++P){
  if(!Places[P].Id||!(FString(Places[P].Id).StartsWith(TEXT("campus_"))||FString(Places[P].Id)==TEXT("girls_dorm")||FString(Places[P].Id)==TEXT("boys_dorm")))continue; // the dorms have a stair beside the lift too
  for(const auto& A:Places[P].Anchors)if(A.Name.StartsWith(TEXT("stair"))&&FVector::Dist2D(A.Position,M)<170.f&&FMath::Abs(A.Position.Z-M.Z)<100.f){PlaceIndex=P;Floor=FCString::Atoi(*A.Name.Mid(5));return true;}
 }
 return false;
}

void AOKStreetSocialScene::TakeStairs(int32 Dir){
 auto& S=*State;int32 PlaceIndex=INDEX_NONE,Floor=0;if(!StairHere(PlaceIndex,Floor))return;
 const auto& P=OKStreetPlaces()[PlaceIndex];const FString Want=FString::Printf(TEXT("stair%d"),Floor+Dir);
 const auto* To=P.Anchors.FindByPredicate([&](const FOKStreetModelAnchor& A){return A.Name==Want;});if(!To)return;
 const FVector Delta=To->Position-S.Male;S.Male=To->Position;
 if(APlayerController* Pc=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr)if(Pc->PlayerCameraManager)Pc->PlayerCameraManager->StartCameraFade(1.f,0.f,0.7f,FLinearColor::Black,false,false);
 // Whoever walks with him climbs too.
 for(const int32 I:{S.Companion,S.OutingGirl})if(S.Agents.IsValidIndex(I-1)&&FVector::Dist(S.Agents[I-1].Position,S.Male-Delta)<500)S.Agents[I-1].Position+=Delta;
 S.Message=OKMsg(TEXT("ui.campus.walk_stairs_to"), {{TEXT("floor"), Floor+Dir}});S.StairKey=-1;RefreshUI();
}

void AOKStreetSocialScene::TickCampusProximity(){
 auto& S=*State;int32 P=INDEX_NONE,F=0;const int32 Stair=StairHere(P,F)?P*10+F:-1;
 const FString Office=OfficeHere();
 if(Stair!=S.StairKey||Office!=S.OfficeKey){S.StairKey=Stair;S.OfficeKey=Office;RefreshUI();}
}

// The department office counter he stands at ("" if none).
FString AOKStreetSocialScene::OfficeHere() const{
 const FString Place=PlaceHere(State->Male);if(!Place.StartsWith(TEXT("campus_")))return FString();
 const auto* P=OKStreetFindPlace(Place);if(!P)return FString();
 for(const auto& A:P->Anchors)if(A.Name.StartsWith(TEXT("office_"))&&FVector::Dist2D(A.Position,State->Male)<260.f&&FMath::Abs(A.Position.Z-State->Male.Z)<100.f)return A.Name.Mid(7);
 return FString();
}

// ------------------------------------------------------------------------------------------------ heroine timetable

FString AOKStreetSocialScene::AcademicText(int32 Character) const{
 const auto& S=*State;const FOKStreetAcademic* A=Character==0?&S.Store.PlayerAcademic:S.Store.Academics.Find(OKStreetStableId(S.Manifest.Characters[Character]));
 if(!A||A->Dept.IsEmpty())return FString();
 const auto* D=OKStreetFindDept(A->Dept);const auto* C=D?OKStreetFindCollege(D->College):nullptr;
 const TCHAR* Status=A->Status==TEXT("enrolled")?OKT(TEXT("ui.campus.enrolled_label")):A->Status==TEXT("leave")?OKT(TEXT("ui.campus.on_leave_label")):A->Status==TEXT("expelled")?OKT(TEXT("ui.campus.expelled_label")):OKT(TEXT("ui.campus.not_student_label"));
 FString Out=OKMsg(TEXT("ui.campus.course_dept_status"), {{TEXT("course_name"), C?*C->Name:TEXT("")}, {TEXT("dept_name"), D?*D->Name:*A->Dept}, {TEXT("status"), Status}});
 if(Character>0)Out+=OKMsg(TEXT("ui.campus.intelligence_gain"), {{TEXT("intelligence"), A->Intelligence}});
 return Out;
}

// ------------------------------------------------------------------------------------------------ apps

// Counter names: a department's office, or one of the administration building's (註冊組 / 教務處 / 學務處).
static FString OKCampusOfficeName(const FString& Id){
 if(const auto* D=OKStreetFindDept(Id))return FString(D->Name)+OKT(TEXT("ui.campus.office_suffix"));
 return Id==TEXT("registrar")?OKT(TEXT("ui.campus.registrar_office")):Id==TEXT("aa")?OKT(TEXT("ui.campus.academic_affairs_office")):Id==TEXT("student")?OKT(TEXT("ui.campus.student_affairs_office")):OKT(TEXT("ui.campus.general_education_office"));
}

void AOKStreetSocialScene::BuildCampusRows(const TSharedRef<SVerticalBox>& Panel){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 auto Row=SNew(SWrapBox).UseAllottedSize(true);
 int32 P=INDEX_NONE,F=0;
 if(StairHere(P,F)){
  const auto& Place=OKStreetPlaces()[P];auto Has=[&](int32 K){const FString N=FString::Printf(TEXT("stair%d"),K);return Place.Anchors.ContainsByPredicate([&](const FOKStreetModelAnchor& A){return A.Name==N;});};
  if(Has(F+1))Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.campus.stairs_up_to"), {{TEXT("floor"), F+1}}),[Weak](){Weak->TakeStairs(1);})];
  if(Has(F-1))Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.campus.stairs_down_to"), {{TEXT("floor"), F-1}}),[Weak](){Weak->TakeStairs(-1);})];
 }
 const FString Office=OfficeHere();
 if(!Office.IsEmpty()){const auto* D=OKStreetFindDept(Office);
  Row->AddSlot().Padding(2)[OKFinButton(Weak,OKCampusOfficeName(Office),[Weak,Office](){Weak->State->DeptOffice=Office;Weak->OpenApp(TEXT("dept"),false);},true,FLinearColor(1,.85f,.3f))];}
 if(StudyPlaceHere())Row->AddSlot().Padding(2)[OKFinButton(Weak,S.Studying?OKT(TEXT("ui.campus.stop_study_button")):(StudyPartner()!=INDEX_NONE?OKT(TEXT("ui.campus.study_together_label")):OKT(TEXT("ui.campus.study_label"))),[Weak](){Weak->ToggleStudy();},true,FLinearColor(.6f,.9f,1))];
 Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.campus.phone_schedule_title")),[Weak](){Weak->OpenApp(TEXT("timetable"),true);})];
 Panel->AddSlot().AutoHeight().Padding(2)[SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[OKFinText(OKMsg(TEXT("ui.campus.academic_status_line"), {{TEXT("academic_text"), *AcademicText(0)}, {TEXT("intelligence"), S.Store.PlayerAcademic.Intelligence}}),FLinearColor(.6f,.9f,1),11,true)]+SVerticalBox::Slot().AutoHeight()[Row]];
}

TSharedRef<SWidget> AOKStreetSocialScene::BuildCampusApp(bool bOffice){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);const auto& A=S.Store.PlayerAcademic;
 auto Box=SNew(SVerticalBox);auto Line=[&](TSharedRef<SWidget> W){Box->AddSlot().AutoHeight().Padding(2)[W];};
 Line(OKFinText(OKMsg(TEXT("ui.campus.academic_summary_line"), {{TEXT("academic_text"), *AcademicText(0)}, {TEXT("term"), A.Term.IsEmpty()?TEXT("-"):*A.Term}, {TEXT("intelligence"), A.Intelligence}}),FLinearColor(1,.85f,.3f),14,true));
 // On a break the dates shown are the coming term's (the one just over is no use to him).
 EOKStreetTermPhase Phase;FOKStreetTerm Next;FOKStreetTerm Term=OKRulesTermAt(S,StreetNow(),Phase,nullptr,nullptr,&Next);if(Phase==EOKStreetTermPhase::Break&&!Next.Key.IsEmpty())Term=Next;
 Line(OKFinText(OKMsg(TEXT("ui.campus.term_schedule_line"), {{TEXT("phase"), Phase==EOKStreetTermPhase::Break?OKT(TEXT("ui.campus.break_next_term")):OKT(TEXT("ui.campus.this_term_label"))}, {TEXT("start"), *Term.Start.ToString(TEXT("%m/%d"))}, {TEXT("midterm"), *Term.Midterm.ToString(TEXT("%m/%d"))}, {TEXT("final"), *Term.Final.ToString(TEXT("%m/%d"))}}),FLinearColor(.8f,.8f,.8f),11));
 Line(OKFinText(OKT(TEXT("ui.campus.schedule_attendance_note")),FLinearColor(.6f,.85f,1),12,true));
 for(const auto& C:A.Courses)Line(OKFinText(OKMsg(TEXT("ui.campus.course_grade_report"), {{TEXT("name"), *C.Name}, {TEXT("slot"), *OKStreetSlotLabel(C)}, {TEXT("room"), *OKStreetRoomLabel(C)}, {TEXT("attended"), C.Attended}, {TEXT("held"), C.Held}, {TEXT("midterm"), C.Midterm<0?TEXT("-"):*FString::Printf(TEXT("%.0f"),C.Midterm)}, {TEXT("final"), C.Final<0?TEXT("-"):*FString::Printf(TEXT("%.0f"),C.Final)}, {TEXT("grade"), C.Grade<0?TEXT("-"):*FString::Printf(TEXT("%.0f%s"),C.Grade,C.Grade<60?OKT(TEXT("ui.campus.fail_grade_label")):TEXT(""))}}),C.Grade>=0&&C.Grade<60?FLinearColor(1,.4f,.4f):FLinearColor::White,11));
 if(bOffice){
  const auto* Here=OKStreetFindDept(S.DeptOffice);
  const bool Registrar=S.DeptOffice==TEXT("registrar"),Affairs=S.DeptOffice==TEXT("aa");
  Line(OKFinText(OKCampusOfficeName(S.DeptOffice)+(Registrar?OKT(TEXT("ui.campus.entrance_exam_signup_hint")):Affairs?OKT(TEXT("ui.campus.transfer_apply_hint")):OKT(TEXT("ui.campus.registrar_services_hint"))),FLinearColor(1,.85f,.3f),13,true));
  auto Row=SNew(SWrapBox).UseAllottedSize(true);
  Row->AddSlot().Padding(2)[OKFinButton(Weak,S.ShowTranscript?OKT(TEXT("ui.campus.collapse_transcript")):OKT(TEXT("ui.campus.request_transcript")),[Weak](){Weak->State->ShowTranscript=!Weak->State->ShowTranscript;Weak->RefreshUI();})];
  if(A.Status==TEXT("enrolled"))Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.campus.take_leave_option")),[Weak](){Weak->CampusAction(TEXT("leave"),FString());})];
  if(A.Status==TEXT("leave"))Row->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.campus.return_from_leave")),[Weak](){Weak->CampusAction(TEXT("return"),FString());})];
  if(Here&&(A.Status==TEXT("expelled")||A.Status==TEXT("none")))Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.campus.entrance_exam_option"), {{TEXT("school_name"), *Here->Name}, {TEXT("bar"), Here->Bar}}),[Weak,Id=FString(Here->Id)](){Weak->CampusAction(TEXT("admit"),Id);},true,FLinearColor(1,.85f,.3f))];
  if(Here&&A.Status==TEXT("enrolled")&&A.Dept!=Here->Id)Row->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.campus.apply_transfer_option"), {{TEXT("dept_name"), *Here->Name}, {TEXT("bar"), Here->Bar}}),[Weak,Id=FString(Here->Id)](){Weak->CampusAction(TEXT("transfer"),Id);},true,FLinearColor(1,.85f,.3f))];
  Line(Row);
  if(Registrar){auto Fees=SNew(SWrapBox).UseAllottedSize(true);
   Fees->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.campus.pay_tuition_dorm_total"), {{TEXT("total"), [&](){const auto Info=OKRulesBankInfo(S,S.Store.CalendarDay);return Info.IsValid()?int64(Info->GetNumberField(TEXT("tuition"))+Info->GetNumberField(TEXT("dorm"))):int64(0);}()}}),[Weak](){Weak->BankAction(TEXT("fees_pay"),0,0);},true,FLinearColor(1,.85f,.3f))];
   Fees->AddSlot().Padding(2)[OKFinButton(Weak,OKT(TEXT("ui.campus.apply_student_loan_tuition")),[Weak](){Weak->BankAction(TEXT("loan_apply"),0,0);})];
   Line(Fees);}
  // The administration counters list every department.
  if((Registrar&&(A.Status==TEXT("expelled")||A.Status==TEXT("none")))||(Affairs&&A.Status==TEXT("enrolled"))){
   auto All=SNew(SWrapBox).UseAllottedSize(true);
   for(const auto& D:OKStreetDepts()){if(Affairs&&A.Dept==D.Id)continue;const FString Id=D.Id;const FString Action=Registrar?TEXT("admit"):TEXT("transfer");
    All->AddSlot().Padding(2)[OKFinButton(Weak,OKMsg(TEXT("ui.campus.transfer_or_register_option"), {{TEXT("action"), Registrar?OKT(TEXT("ui.campus.register_exam_label")):OKT(TEXT("ui.campus.transfer_in_label"))}, {TEXT("dept_name"), *D.Name}, {TEXT("bar"), D.Bar}}),[Weak,Id,Action](){Weak->CampusAction(Action,Id);})];}
   Line(All);
  }
  if(S.ShowTranscript){Line(OKFinText(OKT(TEXT("ui.campus.past_grades_label")),FLinearColor(.6f,.85f,1),12,true));
   if(A.Transcript.IsEmpty())Line(OKFinText(OKT(TEXT("ui.campus.no_finished_semester")),FLinearColor(.7f,.7f,.7f),11));
   for(const FString& T:A.Transcript){TArray<FString> P;T.ParseIntoArray(P,TEXT("|"));if(P.Num()==4)Line(OKFinText(OKMsg(TEXT("ui.campus.course_score_row"), {{TEXT("name"), *P[0]}, {TEXT("slot"), *P[1]}, {TEXT("score"), *P[2]}, {TEXT("attendance"), *P[3]}}),FCString::Atof(*P[2])<60?FLinearColor(1,.4f,.4f):FLinearColor::White,11));}}
 }
 return SNew(SScrollBox)+SScrollBox::Slot()[Box];
}

// The office's leave / return / transfer / admission (academics.rs campus_office).
void AOKStreetSocialScene::CampusAction(const FString& Action,const FString& Dept){
 auto& S=*State;
 auto Request=OKRulesRequest(TEXT("campus_office"));Request->SetStringField(TEXT("action"),Action);Request->SetStringField(TEXT("dept"),Dept);Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);
 Request->SetObjectField(TEXT("player"),OKStreetAcademicJson(S.Store.PlayerAcademic));
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 if(Answer->GetBoolField(TEXT("changed"))){auto Candidate=S.Store;OKStreetReadAcademic(Answer->GetObjectField(TEXT("player")),Candidate.PlayerAcademic);CommitStore(MoveTemp(Candidate));}
 const FString Notice=Answer->GetStringField(TEXT("notice"));if(Notice.IsEmpty())return;
 S.FinNotice=Notice;S.Message=S.FinNotice;RefreshUI();
}
