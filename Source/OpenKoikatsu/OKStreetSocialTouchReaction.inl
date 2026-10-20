// Runs real ray targeting, streamed private GPU poses and source audio on all APNGs.
void AOKStreetSocialScene::RunTouchReactionProbe(double Now){
 auto& S=*State;auto Fail=[&](const TCHAR* Why){S.Errors.Add(Why);EndTalk();WriteReport(false);};
 if(Now-S.StartedAt>600){Fail(TEXT("Touch reaction probe timeout"));return;}
 if(S.TouchReactionCase>=20){EndTalk();WriteReport(S.Errors.IsEmpty());return;}
 const int32 Character=S.TouchReactionCase/4+1,Kind=S.TouchReactionCase%4;
 const int32 Regions[]={0,1,2,4};const int32 Region=Regions[Kind];
 const FName HitBones[]={TEXT("cf_j_head"),TEXT("cf_j_head"),TEXT("cf_j_hand_L"),TEXT("cf_j_bust02_L")};
 const TCHAR* Clips[]={TEXT("touch_head.okanim"),TEXT("touch_cheek.okanim"),TEXT("touch_hand.okanim"),TEXT("touch_chest.okanim")};
 if(S.TouchReactionPhase==0){
  EndTalk();for(auto& A:S.Agents){A.Held=true;A.Position=FVector(-550,3000+A.Slot*300,0);}
  S.Agents[Character-1].Position=FVector(-550,-350,0);S.Male=FVector(-430,-350,0);
  auto& R=S.Store.Relations[OKStreetRelationIndex(S.Store,OKStreetStableId(S.Manifest.Characters[Character]),S.Manifest.TalkBudget,true)];R.Stage=TEXT("friend");R.TalksRemaining=20;
  S.TouchReactionAt=Now;S.TouchReactionPhase=1;return;
 }
 if(Now-S.TouchReactionAt>60){Fail(TEXT("Touch reaction case did not finish"));return;}
 if(S.TouchReactionPhase==1){
  if(!BeginTalk(Character)){Fail(TEXT("Touch reaction could not begin real conversation"));return;}
  S.TouchSee=false;S.TouchReactionRoot=S.Agents[Character-1].Position;S.TouchReactionSave=OKStreetWriteSave(S.Store);
  S.TouchReactionPhase=2;S.TouchReactionAt=Now;return;
 }
 if(S.TouchReactionPhase==2){
  // Let the submitted facing and conversation pose reach the render instance.
  if(Now-S.TouchReactionAt<.5)return;
  FVector Point;if(!OKGetCrowdRuntimeBonePosition(this,Character,HitBones[Kind],Point)){Fail(TEXT("Touch ray target bone missing"));return;}
  const FVector Forward=FRotationMatrix(S.RenderedRotations[Character]).GetUnitAxis(EAxis::Y);
  if(Region<2)Point+=FVector(0,0,Region==0?13:3)+Forward*(Region==0?0:7);
  if(TouchRay(Point+Forward*35,-Forward)!=Region||!S.TouchActive||S.ActiveGesture!=Clips[Kind]){Fail(TEXT("World click did not start the regional authored reaction"));return;}
  const int32 Node=S.Node;const auto Counts=S.TouchCounts;Touch(Region);
  if(S.Node!=Node||!S.TouchCounts.OrderIndependentCompareEqual(Counts)){Fail(TEXT("Repeated click restarted an active reaction"));return;}
  OKGetCrowdRuntimeBonePosition(this,Character,TEXT("cf_j_head"),S.TouchReactionStart);
  S.TouchReactionPlayed=false;S.TouchReactionPeak=0;S.TouchReactionPhase=3;return;
 }
 FOKRuntimeActionStreamStatus Action;OKGetCrowdRuntimeActionStatus(this,Character,Action);
 if(Action.Playing&&Action.ClipId==Clips[Kind]){
  if(!FMath::IsNearlyEqual(Action.DurationSeconds,.75f,.02f)){Fail(TEXT("Short reaction used the entire long source pose"));return;}
  S.TouchReactionPlayed=true;FVector Head;if(OKGetCrowdRuntimeBonePosition(this,Character,TEXT("cf_j_head"),Head))S.TouchReactionPeak=FMath::Max(S.TouchReactionPeak,float(FVector::Dist(Head,S.TouchReactionStart)));
  if(Action.PlaybackSeconds>.25f&&S.TouchReactionPeak>.1f&&S.TouchReactionPhase==3){FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SocialReport)/FString::Printf(TEXT("touch-reaction.%d.%d.png"),Character,Region),true,false);S.TouchReactionPhase=4;}
  return;
 }
 if(!S.TouchReactionPlayed||Action.Pending||Action.Preparing)return;
 if(S.TouchReactionPhase!=4||S.TouchReactionPeak<=.1f||S.Agents[Character-1].Position!=S.TouchReactionRoot){Fail(TEXT("Touch response lacked visible private bone movement or moved the actor root"));return;}
 auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("slot"),Character);Row->SetNumberField(TEXT("region"),Region);Row->SetStringField(TEXT("clip"),Clips[Kind]);Row->SetNumberField(TEXT("head_displacement_cm"),S.TouchReactionPeak);Row->SetBoolField(TEXT("root_preserved"),true);Row->SetBoolField(TEXT("repeat_blocked"),true);S.TouchReactionEvidence.Add(MakeShared<FJsonValueObject>(Row));
 EndTalk();if(S.TouchActive){Fail(TEXT("Touch cancellation did not restore conversation state"));return;}
 ++S.TouchReactionCase;S.TouchReactionPhase=0;
}
