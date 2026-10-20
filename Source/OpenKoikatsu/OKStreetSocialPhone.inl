void AOKStreetSocialScene::RunPhoneUseProbe(double Now){
 auto& S=*State;auto Fail=[&](const FString& Why){FinishPhoto();S.Errors.Add(Why);WriteReport(false);};const int32 Character=S.PhoneUseActorsVerified+1;
 if(Character>5){S.PhoneUseChecked=true;return;}
 if(S.PhoneUseProbePhase==0){
  S.PhoneUseProbeSave=OKStreetWriteSave(S.Store);S.PhoneUseProbeVoices=S.PhoneUseVoices;S.Agents[Character-1].Position=S.Male+FVector(150,0,0);BeginPair(Character,8);
  if(S.Pair.Partner!=Character||!S.Pair.PhoneUse){Fail(TEXT("PhoneUse activity admission failed"));return;}
  if(!S.PhoneUseCancelChecked){FinishPhoto();if(S.Pair.Partner!=INDEX_NONE||S.Target!=INDEX_NONE||!S.PairProps.IsEmpty()){Fail(TEXT("Queued phone-use cancellation retained reservation/props"));return;}S.PhoneUseCancelChecked=true;BeginPair(Character,8);}
  S.PhoneUseProbeSession=S.Pair;S.PhoneUseProbePhase=1;return;
 }
 if(S.Pair.Partner!=Character||!S.Pair.PhoneUse){Fail(TEXT("PhoneUse activity ended before verification"));return;}
 if(Now-S.Pair.RequestedAt>55){Fail(TEXT("PhoneUse activity preparation timeout"));return;}
 if(S.Pair.Phase!=1||Now-S.Pair.PlayingAt<1)return;
 FOKRuntimeActionStreamStatus A,B;
 if(!OKGetCrowdRuntimeActionStatus(this,0,A)||!OKGetCrowdRuntimeActionStatus(this,Character,B)||!A.Playing||!B.Playing||A.ClipId!=TEXT("phone_standing.okanim")||B.ClipId!=TEXT("phone_standing.okanim")||S.PairProps.Num()!=6){Fail(TEXT("PhoneUse activity private pose/props mismatch"));return;}
 for(int32 Participant=0;Participant<2;++Participant){FTransform Hand;if(!OKGetCrowdRuntimeBoneTransform(this,Participant==0?0:Character,TEXT("k_f_handL_00"),Hand)||Hand.ContainsNaN()){Fail(TEXT("PhoneUse activity hand invalid"));return;}
  if(!S.PairProps[Participant*3].IsValid()||S.PairProps[Participant*3]->IsHidden()||FVector::Dist(S.PairProps[Participant*3]->GetActorLocation(),Hand.GetLocation())>.1){Fail(TEXT("PhoneUse prop detached from current private hand"));return;}
  if(S.PhoneUseProbePhase==1)S.PhoneUseFirstHand[Participant]=Hand.GetLocation();
  else if(S.PhoneUseProbePhase==2&&Now-S.PhoneUseProbeAt>1.25&&FVector::Dist(S.PhoneUseFirstHand[Participant],Hand.GetLocation())<.5){Fail(TEXT("PhoneUse private hand/prop did not animate"));return;}
 }
 auto QueueFixture=[&](){const int32 Node=OKRulesFreshGreeting(S,Character);return Node!=INDEX_NONE&&QueueNode(Node,TEXT("greeting"));};
 if(S.PhoneUseProbePhase==1){FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("street.phone-use.%d.first.png"),Character),false,false);S.PhoneUseProbeAt=Now;S.PhoneUseProbePhase=2;return;}
 if(S.PhoneUseProbePhase==2){if(Now-S.PhoneUseProbeAt<1.3)return;FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("street.phone-use.%d.second.png"),Character),false,false);S.PhoneUseProbeAt=Now;S.PhoneUseProbePhase=3;return;}
 if(S.PhoneUseProbePhase==3){if(Now-S.PhoneUseProbeAt<.75)return;if(!QueueFixture()){Fail(TEXT("PhoneUse source voice fixture unavailable"));return;}S.PhoneUseProbePhase=4;return;}
 if(S.PhoneUseProbePhase==4){if(S.Node!=INDEX_NONE||S.PhoneUseVoices<=S.PhoneUseProbeVoices)return;if(!QueueFixture()){Fail(TEXT("PhoneUse active cancellation fixture unavailable"));return;}S.PhoneUseProbePhase=5;return;}
 if(!OKCrowdRuntimeSpeechActive(this,Character))return;
 const auto Before=S.PhoneUseProbeSession;EndTalk();EOKRuntimeSpeechState Speech;FString Error;
 if(!OKGetCrowdRuntimeSpeechState(this,Character,Speech,Error)||Speech!=EOKRuntimeSpeechState::Cancelled){Fail(TEXT("PhoneUse active speech was not cancelled"));return;}
 if(S.Pair.Partner!=INDEX_NONE||S.Target!=INDEX_NONE||!S.PairProps.IsEmpty()||!S.MealHandProps.IsEmpty()||S.Agents[Character-1].Held!=Before.SavedAgent.Held||S.Yaw!=Before.SavedYaw||S.Pitch!=Before.SavedPitch||S.Distance!=Before.SavedDistance||S.MouseUI!=Before.SavedMouseUI||OKStreetWriteSave(S.Store)!=S.PhoneUseProbeSave){Fail(TEXT("PhoneUse activity failed to restore actors/camera/mouse/save"));return;}
 ++S.PhoneUseActorsVerified;S.PhoneUseProbePhase=0;
}
