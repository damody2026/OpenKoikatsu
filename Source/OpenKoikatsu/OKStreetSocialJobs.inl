// Included by OKStreetSocialScene.cpp: plan §8.6 part-time jobs for the player — applying, booking shifts (they
// become appointments, so missing one blacklists him), being held at the workplace, leaving early, fast-forward,
// wages, the manager's rating, and a heroine who manages the shop.

// A heroine whose profile says she manages this place (occupation.role = manager | owner), or INDEX_NONE.
int32 AOKStreetSocialScene::ManagerAt(const FString& Place) const{
 const auto& S=*State;
 if(const int32 OwnerId=HeroOwnerAt(Place);OwnerId!=INDEX_NONE)return OwnerId; // her own company (OKStreetSocialHeroCompany.inl)
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){const auto P=S.MindProfiles.FindRef(OKStreetStableId(S.Manifest.Characters[I]));
  const FString JobRole=OKStreetProfileString(P,TEXT("occupation"),TEXT("role")),Work=OKStreetProfileString(P,TEXT("occupation"),TEXT("workplace_place_id"));
  if(Work==Place&&(JobRole==TEXT("manager")||JobRole==TEXT("owner")))return I;}
 return INDEX_NONE;
}

void AOKStreetSocialScene::ApplyJob(const FString& JobId){
 auto& S=*State;const auto* Job=S.Catalog.FindJob(JobId);if(!Job||S.Store.PlayerJobs.Contains(JobId))return;
 if(S.JobRejected.Contains(JobId)&&S.JobRejected[JobId]==S.Store.CalendarDay){S.FreeTalkNotice=OKT(TEXT("ui.jobs.rejected_today_retry"));RefreshUI();return;}
 if(Blacklisted(Job->Place)){S.FreeTalkNotice=OKT(TEXT("ui.jobs.blacklisted_no_apply"));RefreshUI();return;}
 const int32 Boss=ManagerAt(Job->Place);
 if(Boss==INDEX_NONE){
  // The manager is only a dialog box (plan §8.6.7): a rule decision, no model call.
  // His look and how he did here before decide; a manager with nobody to spare says no now and then.
  auto Request=OKRulesRequest(TEXT("job_apply"));Request->SetNumberField(TEXT("look"),PlayerLook());if(S.Store.JobRating.Contains(JobId))Request->SetNumberField(TEXT("rating"),S.Store.JobRating[JobId]);Request->SetNumberField(TEXT("seed"),FMath::Rand());
  const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);const bool Yes=Answer&&Answer->GetBoolField(TEXT("yes"));
  S.FreeTalkNotice=Yes?OKMsg(TEXT("ui.jobs.manager_hire_offer"), {{TEXT("job_title"), *Job->Title}, {TEXT("wage"), Job->Wage}}):OKT(TEXT("ui.jobs.manager_no_openings"));
  Hire(JobId,Yes);return;
 }
 // She is the manager: the interview is a choice she takes part in (chat or buttons).
 const float Appeal=AppealOf(Boss);
 TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 OpenChoice(Boss,TEXT("hire"),OKMsg(TEXT("ui.jobs.wants_to_apply"), {{TEXT("job_title"), *Job->Title}}),{TEXT("accept"),TEXT("decline")},Appeal>15?TEXT("accept"):TEXT("decline"),0.6f,30,
  [Weak,JobId](const FString& Option){if(Weak.IsValid())Weak->Hire(JobId,Option==TEXT("accept"));});
 S.FreeTalkNotice=OKMsg(TEXT("ui.jobs.manager_interviewing"), {{TEXT("manager_name"), *S.Manifest.Characters[Boss].Name}});RefreshUI();
}

void AOKStreetSocialScene::Hire(const FString& JobId,bool Accepted){
 auto& S=*State;if(!Accepted){S.JobRejected.Add(JobId,S.Store.CalendarDay);if(S.FreeTalkNotice.IsEmpty())S.FreeTalkNotice=OKT(TEXT("ui.jobs.not_hired"));RefreshUI();return;}
 auto Candidate=S.Store;Candidate.PlayerJobs.AddUnique(JobId);if(!Candidate.JobRating.Contains(JobId))Candidate.JobRating.Add(JobId,60);
 CommitStore(MoveTemp(Candidate));RefreshUI();
}

// Booking tomorrow's shift makes it an appointment (plan §13.3): no-show = blacklist, early-leave = docked pay.
void AOKStreetSocialScene::BookShift(const FString& JobId,int32 Shift){
 auto& S=*State;const auto* Job=S.Catalog.FindJob(JobId);if(!Job||!Job->Shifts.IsValidIndex(Shift))return;
 const FVector2D H=Job->Shifts[Shift];const int64 Start=int64(S.Store.CalendarDay+1)*1440+int64(H.X*60);
 if(S.Store.Appointments.ContainsByPredicate([&](const FOKStreetAppointment& A){return A.Kind==TEXT("shift")&&A.Start==Start&&A.Status==TEXT("pending");}))return;
 const auto* P=OKStreetFindPlace(Job->Place);
 AddAppointment(TEXT("shift"),INDEX_NONE,Job->Place,Start,10,OKMsg(TEXT("ui.jobs.job_pay_range"), {{TEXT("place"), P?P->Name:*Job->Place}, {TEXT("min"), H.X}, {TEXT("max"), H.Y}}));
}

// Once he arrived for a shift (the appointment is done or late), he works until its end.
void AOKStreetSocialScene::TickJobs(){
 auto& S=*State;const int64 Now=OKStreetMindNow(S);
 // Fast-forward while working (plan §8.6.3 專心上班): time runs 8x until something needs him.
 const double Wall=FPlatformTime::Seconds();const double DeltaWall=S.ShiftTickWall>0?FMath::Min(Wall-S.ShiftTickWall,0.5):0;S.ShiftTickWall=Wall;
 if(S.ShiftAppointment&&S.ShiftFast){
  if(S.ApproachGirl!=INDEX_NONE||S.Target!=INDEX_NONE||!S.Choices.IsEmpty()){S.ShiftFast=false;S.FreeTalkNotice=OKT(TEXT("ui.jobs.someone_calling_resume_speed"));RefreshUI();}
  else S.ClockStartHours=FMath::Fmod(S.ClockStartHours+DeltaWall*7.0*OKStreetClockSpeed/3600.0,24.0);
 }
 if(!S.ShiftAppointment){
  for(const auto& A:S.Store.Appointments)if(A.Kind==TEXT("shift")&&(A.Status==TEXT("done")||A.Status==TEXT("late"))&&Now>=A.Start-30&&Now<A.Start+12*60){
   // A shift whose end has passed is over (paid at 12:00, it started again and ended at once: "worked 0 minutes, ¥0"
   // replaced the real payslip).
   for(const FString& J:S.Store.PlayerJobs)if(const auto* Job=S.Catalog.FindJob(J))if(Job->Place==A.Place)for(const auto& H:Job->Shifts)if(int64(H.X*60)==A.Start%1440&&Now<A.Start-A.Start%1440+int64(H.Y*60)){
    S.ShiftAppointment=A.Id;S.ShiftJob=J;S.ShiftStart=FMath::Max<int64>(A.Start,Now); /* paid from when he came */S.ShiftEnd=A.Start-A.Start%1440+int64(H.Y*60);S.ShiftAwaySince=-1;S.ShiftChats=0;
    S.FreeTalkNotice=OKMsg(TEXT("ui.jobs.shift_started_notice"), {{TEXT("job_title"), *Job->Title}, {TEXT("end_hour"), (S.ShiftEnd%1440)/60}, {TEXT("end_minute"), S.ShiftEnd%60}});RefreshUI();
   }
   if(S.ShiftAppointment)break;
  }
  return;
 }
 const auto* Job=S.Catalog.FindJob(S.ShiftJob);if(!Job){S.ShiftAppointment=0;return;}
 if(Now>=S.ShiftEnd){EndShift(false);return;}
 // Held at the workplace (plan §8.6.2): out of the shop for more than five game minutes is leaving early.
 if(PlaceHere(S.Male)!=Job->Place){if(S.ShiftAwaySince<0)S.ShiftAwaySince=Now;else if(Now-S.ShiftAwaySince>5)EndShift(true);}
 else S.ShiftAwaySince=-1;
}

void AOKStreetSocialScene::EndShift(bool Early){
 auto& S=*State;const auto* Job=S.Catalog.FindJob(S.ShiftJob);const int64 Now=OKStreetMindNow(S);
 const int64 Worked=FMath::Clamp<int64>(FMath::Min(Now,S.ShiftEnd)-FMath::Max(S.ShiftStart,Now-24*60),0,24*60);
 auto Candidate=S.Store;const int32 Wage=Job?Job->Wage:int32(Local(TEXT("wage"),95));
 // Pay, the manager's rating and being let go are rules (shop.rs shift_end).
 auto Request=OKRulesRequest(TEXT("shift_end"));Request->SetNumberField(TEXT("wage"),Wage);Request->SetNumberField(TEXT("worked"),double(Worked));Request->SetBoolField(TEXT("early"),Early);
 Request->SetNumberField(TEXT("chats"),S.ShiftChats);Request->SetNumberField(TEXT("rating"),Candidate.JobRating.FindRef(S.ShiftJob));
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 const int64 Pay=int64(Answer->GetNumberField(TEXT("pay")));
 OKCash(Candidate,Pay,TEXT("wage"),OKMsg(TEXT("ui.jobs.shift_minutes_label"), {{TEXT("job_title"), Job?*Job->Title:*S.ShiftJob}, {TEXT("minutes"), Worked}}));
 HeroJobPaid(Candidate,S.ShiftJob,Pay,Worked); // at her company: her company pays (OKStreetSocialHeroCompany.inl)
 S.PayslipNote=WithholdPlayerWage(Candidate,Pay,Worked); // 五險一金 and income tax (OKStreetSocialCompany.inl)
 Candidate.JobRating.Add(S.ShiftJob,int32(Answer->GetNumberField(TEXT("rating"))));
 if(Answer->GetBoolField(TEXT("fired"))){Candidate.PlayerJobs.Remove(S.ShiftJob);S.FreeTalkNotice=OKT(TEXT("ui.jobs.fired_notice"));}
 CommitStore(MoveTemp(Candidate));
 if(S.FreeTalkNotice.IsEmpty()||!S.FreeTalkNotice.Contains(OKT(TEXT("ui.jobs.fired_label"))))S.FreeTalkNotice=OKMsg(TEXT("ui.jobs.shift_summary"), {{TEXT("ending"), Early?OKT(TEXT("ui.jobs.leave_early_label")):OKT(TEXT("ui.jobs.end_shift"))}, {TEXT("minutes"), Worked}, {TEXT("pay"), Pay}, {TEXT("rating"), S.Store.JobRating.FindRef(S.ShiftJob)}});
 S.FreeTalkNotice+=S.PayslipNote;
 // Heroines who worked the same shift remember it (plan §8.6.3 同班互動).
 if(Job)for(int32 I=1;I<S.Manifest.Characters.Num();++I){int32 B=INDEX_NONE;const auto* Block=BlockNow(I,int32((Now-1)%1440),&B);
  if(Block&&Block->Kind==TEXT("work")&&Block->Place==Job->Place)MindObserve(I,TEXT("shared_shift"),Early?OKT(TEXT("ui.jobs.left_early_alone")):OKT(TEXT("ui.jobs.work_until_end_together")),Job->Place,Early?0.5f:0.45f);}
 S.ShiftAppointment=0;S.ShiftJob.Reset();S.ShiftFast=false;RefreshUI();
}

// Chatting during a busy shift (plan §8.6.3): the manager speaks up; a heroine manager does it herself.
void AOKStreetSocialScene::NoteShiftChat(){
 auto& S=*State;const auto* Job=S.Catalog.FindJob(S.ShiftJob);if(!Job)return;
 const FDateTime T=StreetNow();if(OKRulesCrowdAt(S,Job->Place,T.GetHour()+T.GetMinute()/60.0)<0.6f)return;
 ++S.ShiftChats;if(S.ShiftChats<=2)return;
 const int32 Boss=ManagerAt(Job->Place);
 if(Boss!=INDEX_NONE){S.MindRecent.Add(OKStreetStableId(S.Manifest.Characters[Boss]),OKT(TEXT("ui.jobs.manager_chat_peak_hours")));S.FreeTalkNotice=OKMsg(TEXT("ui.jobs.manager_glared"), {{TEXT("manager_name"), *S.Manifest.Characters[Boss].Name}});}
 else S.FreeTalkNotice=OKT(TEXT("ui.jobs.manager_greet_customers"));
}

// The shop panel's job rows: apply, book a shift for tomorrow, and the working-mode toggle.
void AOKStreetSocialScene::BuildJobRows(const TSharedRef<SVerticalBox>& Panel){
 auto& S=*State;const FString Place=PlaceHere(S.Male);if(Place.IsEmpty())return;
 TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 auto Button=[&](const FString& Text,TFunction<void()> Do){return SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(Text)).OnClicked_Lambda([Weak,Do](){if(Weak.IsValid())Do();return FReply::Handled();});};
 auto Row=SNew(SWrapBox).UseAllottedSize(true);bool Any=false;
 for(const auto& Job:S.Catalog.Jobs){if(Job.Place!=Place)continue;Any=true;const FString Id=Job.Id;
  if(!S.Store.PlayerJobs.Contains(Id)){Row->AddSlot().Padding(1)[Button(OKMsg(TEXT("ui.jobs.apply_job_label"), {{TEXT("job_title"), *Job.Title}, {TEXT("wage"), Job.Wage}}),[Weak,Id](){Weak->ApplyJob(Id);})];continue;}
  for(int32 Shift=0;Shift<Job.Shifts.Num();++Shift){
   // Over one of his classes: said on the button (academics.rs class_clash); booking it is still his choice.
   FString Clash;{auto Ask=OKRulesRequest(TEXT("class_clash"));Ask->SetObjectField(TEXT("player"),OKStreetAcademicJson(S.Store.PlayerAcademic));Ask->SetNumberField(TEXT("day"),S.Store.CalendarDay+1);Ask->SetNumberField(TEXT("from"),Job.Shifts[Shift].X);Ask->SetNumberField(TEXT("to"),Job.Shifts[Shift].Y);
    if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Ask)){const FString Course=Out->GetStringField(TEXT("course"));if(!Course.IsEmpty())Clash=OKMsg(TEXT("ui.jobs.class_clash_suffix"),{{TEXT("course"),Course}});}}
   Row->AddSlot().Padding(1)[Button(OKMsg(TEXT("ui.jobs.schedule_tomorrow_shift"), {{TEXT("job_title"), *Job.Title}, {TEXT("start_hour"), Job.Shifts[Shift].X}, {TEXT("end_hour"), Job.Shifts[Shift].Y}})+Clash,[Weak,Id,Shift](){Weak->BookShift(Id,Shift);})];}
 }
 if(S.ShiftAppointment)Row->AddSlot().Padding(1)[Button(S.ShiftFast?OKT(TEXT("ui.jobs.chat_while_working")):OKT(TEXT("ui.jobs.work_focus_fast_forward")),[Weak](){Weak->State->ShiftFast=!Weak->State->ShiftFast;Weak->RefreshUI();})];
 if(Any||S.ShiftAppointment)Panel->AddSlot().AutoHeight()[Row];
}
