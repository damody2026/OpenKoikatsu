void AOKStreetSocialScene::RequestInvitation(int32 Character,int32 Command){
 if(!State||!(Command==3||Command==4))return;auto& S=*State;
 if(S.InvitationActive||S.PrivateGirl!=INDEX_NONE||S.Pair.Partner!=INDEX_NONE||S.Companion!=INDEX_NONE||S.Facility!=INDEX_NONE||!ContactReady(Character))return;
 if(S.Target==INDEX_NONE){if(BeginTalk(Character))S.InvitationQueued=Command;return;}
 if(S.Target!=Character||OKStreetConversationBusy(S.Node!=INDEX_NONE,S.CollectedActive,S.RelationshipPending,S.TouchActive))return;
 const auto R=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Character]),S.Manifest.TalkBudget);
 FString Reason;if(!OKRulesCanInvite(S,R,Command,Reason)){S.Message=Reason==TEXT("talk_budget")?OKT(TEXT("ui.invitations.invite_no_budget")):OKT(TEXT("ui.invitations.invite_already_today"));RefreshUI();return;}
 const int32 Stage=OKStreetStageIndex(R.Stage),Personality=S.Manifest.Characters[Character].PersonalityId;
 const auto* Item=S.Invitations.FindByPredicate([&](const FOKStreetInvitation& I){return I.Personality==Personality&&I.Stage==Stage&&I.Command==Command;});
 if(!Item){S.Message=OKT(TEXT("ui.invitations.invite_data_missing"));RefreshUI();return;}
 const bool Accept=PanelConsent(Character,Command==3?TEXT("meal"):TEXT("walk"),false); // the voiced scene follows the shared rule (Life.inl)
 const auto& Sequence=Accept?Item->Accept:Item->Refuse;
 if(Sequence.IsEmpty())return;
 S.InvitationActive=true;S.InvitationAccepted=Accept;S.InvitationCommand=Command;S.InvitationGirl=Character;S.InvitationSequence=Sequence;
 if(!QueueNode(S.InvitationSequence[0],TEXT("invitation"))){S.InvitationActive=false;return;}S.InvitationSequence.RemoveAt(0);
 SetSourcePlayerLine(Item->PlayerZh);RefreshUI();
}
void AOKStreetSocialScene::CompleteInvitation(){
 auto& S=*State;if(S.Target!=S.InvitationGirl||!S.Catalogue.Nodes.IsValidIndex(S.Node)||S.Catalogue.Nodes[S.Node].Category!=TEXT("invitation")){FailTalk(TEXT("Invitation lost its response transaction"));return;}++S.InvitationVoices;S.Node=INDEX_NONE;S.VoiceIds.Reset();S.ClipStarted=false;
 if(!S.InvitationSequence.IsEmpty()){const int32 Next=S.InvitationSequence[0];S.InvitationSequence.RemoveAt(0);const FString Prompt=S.PlayerLine;QueueNode(Next,TEXT("invitation"));SetSourcePlayerLine(Prompt);RefreshUI();return;}
 const int32 Character=S.InvitationGirl,Command=S.InvitationCommand;const bool Accept=S.InvitationAccepted;
 S.InvitationActive=false;S.InvitationGirl=INDEX_NONE;S.InvitationCommand=INDEX_NONE;S.ActiveCategory.Reset();
 const FString Id=OKStreetStableId(S.Manifest.Characters[Character]),Transaction=FGuid::NewGuid().ToString(EGuidFormats::Digits);
 auto Candidate=S.Store;const auto Result=OKRulesCommitInvitation(S,Candidate,Id,Command,Transaction);FString SaveError;
 if(!Result.Applied||!OKStreetAtomicSave(S.Manifest.SavePath,OKStreetWriteSave(Candidate),SaveError)){FailTalk(TEXT("Invitation atomic commit failed: ")+Result.Reason+SaveError);return;}
 S.Store=MoveTemp(Candidate);S.Transactions.Add(Transaction);
 FString Error;ResumeConversationPose(Character,Error);ResumeConversationPose(0,Error);
 if(Accept){++S.InvitationAccepts;if(Command==3)StartActivityFromPanel(Character,TEXT("meal"),true);else if(Command==4)BeginCompanion(Character);}
 else{++S.InvitationRefusals;S.Message=OKT(TEXT("ui.invitations.invite_declined"));RefreshUI();}
}
void AOKStreetSocialScene::RunInvitationProbe(double Now){
 auto& S=*State;
 auto Fail=[&](const FString& Why){S.Errors.Add(Why);EndTalk();S.Store=S.InvitationProbeStore;WriteReport(false);};
 if(S.InvitationProbeCase==20){S.Store=S.InvitationProbeStore;WriteReport(S.Errors.IsEmpty()&&S.InvitationVoices==20&&S.InvitationAccepts==10&&S.InvitationRefusals==10&&S.InvitationCancels==20&&S.InvitationLedgerChecks==20);return;}
 const int32 Character=S.InvitationProbeCase/4+1,Command=S.InvitationProbeCase%2?4:3;
 const bool Accept=(S.InvitationProbeCase%4)<2;
 if(S.InvitationProbePhase==0){
  if(S.InvitationProbeCase==0)S.InvitationProbeStore=S.Store;
  EndTalk();S.Store=S.InvitationProbeStore;const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
  auto* Relation=S.Store.Relations.FindByPredicate([&](const FOKStreetRelation& R){return R.Id==Id;});if(!Relation){FOKStreetRelation R;R.Id=Id;R.TalksRemaining=S.Manifest.TalkBudget;S.Store.Relations.Add(R);Relation=&S.Store.Relations.Last();}Relation->Stage=TEXT("stranger");Relation->Favor=50;Relation->TalksRemaining=S.Manifest.TalkBudget;
  S.InvitationProbeSave=OKStreetWriteSave(S.Store);FString FixtureError;if(!OKStreetAtomicSave(S.Manifest.SavePath,S.InvitationProbeSave,FixtureError)){Fail(TEXT("Invitation fixture save failed: ")+FixtureError);return;}S.Agents[Character-1].Position=S.Male+FVector(150,0,0);S.Target=Character;S.Agents[Character-1].Held=true;
  auto* Item=S.Invitations.FindByPredicate([&](const FOKStreetInvitation& I){return I.Personality==S.Manifest.Characters[Character].PersonalityId&&I.Stage==0&&I.Command==Command;});if(!Item){Fail(TEXT("Invitation test missing profile"));return;}
  const float Min=Item->Min,Max=Item->Max;Item->Min=Item->Max=Accept?100:0;
  RequestInvitation(Character,Command);if(!S.InvitationActive){Item->Min=Min;Item->Max=Max;Fail(TEXT("Invitation production request rejected"));return;}EndTalk();
  FOKStreetStore Cancelled;FString CancelError;
  if(OKStreetLoadSaveFile(S.Manifest.SavePath,Cancelled,CancelError)!=EOKStreetLoad::Ok||OKStreetWriteSave(Cancelled)!=S.InvitationProbeSave||S.InvitationActive||S.Node!=INDEX_NONE||S.Pair.Partner!=INDEX_NONE||S.Companion!=INDEX_NONE||S.InvitationProbeSave!=OKStreetWriteSave(S.Store)){Item->Min=Min;Item->Max=Max;Fail(TEXT("Invitation cancellation committed activity/save"));return;}++S.InvitationCancels;
  S.Target=Character;S.Agents[Character-1].Held=true;RequestInvitation(Character,Command);Item->Min=Min;Item->Max=Max;
  if(!S.InvitationActive||S.InvitationAccepted!=Accept){Fail(TEXT("Invitation retry outcome invalid"));return;}S.InvitationProbeAt=Now;S.InvitationProbePhase=1;return;
 }
 if(Now-S.InvitationProbeAt>40){Fail(TEXT("Invitation voice completion timeout"));return;}
 if(S.InvitationActive)return;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);FOKStreetStore Before,Reloaded;FString Error;
 if(OKStreetParseSave(S.InvitationProbeSave,Before,Error)!=EOKStreetLoad::Ok||OKStreetLoadSaveFile(S.Manifest.SavePath,Reloaded,Error)!=EOKStreetLoad::Ok||OKStreetWriteSave(Reloaded)!=OKStreetWriteSave(S.Store)){Fail(TEXT("Invitation persisted ledger reload failed: ")+Error);return;}
 const auto Prior=OKStreetViewRelation(Before,Id,S.Manifest.TalkBudget),After=OKStreetViewRelation(Reloaded,Id,S.Manifest.TalkBudget);
 if(After.TalksRemaining!=Prior.TalksRemaining-1||After.Favor!=Prior.Favor||After.Lewdness!=Prior.Lewdness||After.Stage!=Prior.Stage||After.Committed.Num()!=Prior.Committed.Num()+1){Fail(TEXT("Invitation must charge exactly once without relationship reward"));return;}
 for(const auto& Other:Before.Relations)if(Other.Id!=Id){auto A=Before,B=Reloaded;A.Relations={Other};B.Relations={OKStreetViewRelation(Reloaded,Other.Id,S.Manifest.TalkBudget)};if(OKStreetWriteSave(A)!=OKStreetWriteSave(B)){Fail(TEXT("Invitation modified another APNG relationship"));return;}}
 FString Reason;auto NextDay=After;OKStreetAdvanceDay(NextDay);
 if(OKRulesCanInvite(S,After,Command,Reason)!=(Command==4)||!OKRulesCanInvite(S,NextDay,Command,Reason)){Fail(TEXT("Invitation cooldown/repeat/day policy failed"));return;}
 auto Exhausted=After;Exhausted.TalksRemaining=Command==4?1:0;if(OKRulesCanInvite(S,Exhausted,Command,Reason)){Fail(TEXT("Invitation budget gate failed"));return;}
 auto OtherGirl=OKStreetViewRelation(Reloaded,TEXT("unrelated-apng-test-id"),S.Manifest.TalkBudget);if(!OKRulesCanInvite(S,OtherGirl,Command,Reason)){Fail(TEXT("Invitation ledger leaked across identities"));return;}
 auto Duplicate=Reloaded;if(OKRulesCommitInvitation(S,Duplicate,Id,Command,After.Committed.Last()).Applied||OKStreetWriteSave(Duplicate)!=OKStreetWriteSave(Reloaded)){Fail(TEXT("Invitation duplicate transaction charged twice"));return;}
 const FString EvidenceBase=FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("invitation-ledger-%02d"),S.InvitationProbeCase);
 if(!FFileHelper::SaveStringToFile(S.InvitationProbeSave,*(EvidenceBase+TEXT(".before.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)||!FFileHelper::SaveStringToFile(OKStreetWriteSave(Reloaded),*(EvidenceBase+TEXT(".after.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)){Fail(TEXT("Invitation ledger evidence write failed"));return;}
 ++S.InvitationLedgerChecks;
 if(Accept&&((Command==3&&(S.Pair.Partner!=Character||!S.Pair.Meal)&&S.OutingGirl!=Character)||(Command==4&&S.Companion!=Character))){Fail(TEXT("Accepted invitation did not dispatch activity"));return;}
 if(!Accept&&(S.Pair.Partner!=INDEX_NONE||S.Companion!=INDEX_NONE||S.OutingGirl!=INDEX_NONE)){Fail(TEXT("Refusal started activity"));return;}
 if(S.OutingGirl!=INDEX_NONE)CancelOuting(FString());if(S.Pair.Partner!=INDEX_NONE)FinishPhoto();if(S.Companion!=INDEX_NONE)FinishCompanion();EndTalk();if(Command==3){S.Target=Character;S.Agents[Character-1].Held=true;RequestInvitation(Character,Command);if(S.InvitationActive){Fail(TEXT("Reopened conversation bypassed meal ledger"));return;}EndTalk();}++S.InvitationProbeCase;S.InvitationProbePhase=0;
}
