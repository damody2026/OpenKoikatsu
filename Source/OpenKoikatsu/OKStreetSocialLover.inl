// Lover moments (included by OKStreetSocialScene.cpp after the elevators): what DoAct's acts look like.
//
// DoAct (OKStreetSocialMind.inl) decides how she takes it in this place (§9 etiquette); here she shows it. A kiss or
// an embrace is the original two-shot pose (lover.manifest.json, both actors at one anchor like the photo two-shots),
// held a few seconds and followed by her shy reaction; taking her hand or patting her head is her reaction alone.

namespace {
constexpr float OKLoverPoseSeconds=6.f,OKLoverReactSeconds=5.f,OKLoverTouchSeconds=3.9f;
// His hand contact, in his own frame (Unity metres: +y up, +z forward, +x his right), one 4 s loop of his stand:
// the hand rises to the target by the peak and back down by the end. (A head-pat contact was tried and removed: the
// runtime could not evaluate his arm up to her head, "authored frame evaluation failed"; the pat is her reaction.)
const TCHAR* OKLoverHandRecipe=TEXT(R"({"start":0.08,"peak":0.45,"end":0.92,"constraints":[{"effector":"k_f_handR_00","tip":[0,0,0],"target":[0.16,0.86,0.25],"chain":[{"name":"cf_j_hand_R","max_degrees":90},{"name":"cf_j_forearm01_R","max_degrees":150},{"name":"cf_j_arm00_R","max_degrees":150},{"name":"cf_j_shoulder_R","max_degrees":90}]}]})");
const TCHAR* OKLoverShy(int32 Reaction){
 // Thrilled: the happy one; shy: covering up; fine: in between.
 return Reaction==4?TEXT("lover_shy_2.okanim"):Reaction==1?TEXT("lover_shy_1.okanim"):TEXT("lover_shy_3.okanim");
}
}

void AOKStreetSocialScene::PlayLoverAct(int32 Girl,const FString& Act,int32 Reaction){
 if(!State||!S_ValidGirl(Girl))return;auto& S=*State;
 const bool Agreed=Reaction==0||Reaction==1||Reaction==4; // Fine, Shy, Thrilled (Protest 2, Refuse 3)
 // Her history (which act, how she took it) is written by Interact (OKStreetSocialHeroine.inl).
 if(!Agreed){
  // A protest still shows: she pulls back, embarrassed. A refusal is her words alone.
  if(Reaction==2)StartLoverReaction(Girl,TEXT("lover_shy_3.okanim"),2.5f);
  return;
 }
 if(Act==TEXT("kiss")||Act==TEXT("hug")){
  if(S.Pair.Partner!=INDEX_NONE||S.Companion==Girl&&S.OutingGirl!=INDEX_NONE)return; // mid-activity: words only
  if(S.Companion==Girl)FinishCompanion();
  // Face each other where he stands: the two-shot curves carry her offset from the shared anchor.
  S.NextPairYaw=S.Rotations[0].Yaw;
  S.NextPairFemaleClip=Act==TEXT("kiss")?TEXT("lover_kiss_female.okanim"):TEXT("lover_hold_female.okanim");
  S.NextPairMaleClip=Act==TEXT("kiss")?TEXT("lover_kiss_male.okanim"):TEXT("lover_hold_male.okanim");
  BeginPair(Girl,0);
  if(S.Pair.Partner!=Girl){S.NextPairFemaleClip.Reset();S.NextPairMaleClip.Reset();return;}
  S.LoverPose={Girl,FString(OKLoverShy(Reaction)),0.0,Act};
  S.Message=Act==TEXT("kiss")?OKT(TEXT("ui.lover.kissing_scene_hint")):OKT(TEXT("ui.lover.hugging_scene_hint"));RefreshUI();
  UE_LOG(LogTemp,Display,TEXT("Street lover: actor=%d %s (reaction %d)"),Girl,*Act,Reaction);
  return;
 }
 if(Act==TEXT("hold_hands")||Act==TEXT("head_pat")){
  if(S.Pair.Partner!=INDEX_NONE)return;
  // She stands just in front of him, facing him: where his hand goes.
  const FVector Front=FRotator(0,S.Rotations[0].Yaw+90.f,0).Vector();
  S.Agents[Girl-1].Position=FVector(S.Male.X,S.Male.Y,S.Agents[Girl-1].Position.Z)+Front*(Act==TEXT("head_pat")?45.f:50.f);
  // A head pat: her reaction. Holding hands: his hand reaches for hers while she gives her shy reaction (a contact
  // on her side raised the wrong arm, one on his side up to her head could not be evaluated).
  if(Act==TEXT("head_pat"))StartLoverReaction(Girl,OKLoverShy(Reaction),OKLoverReactSeconds);
  else StartLoverReaction(Girl,FString(TEXT("lover_hand_shy.okanim")),OKLoverTouchSeconds,TEXT("lover_hand_male.okanim"));
  return;
 }
 // The rest of DoAct's acts, shown with poses there are clips for (standing; mid-activity they stay words).
 if(S.Pair.Partner!=INDEX_NONE||S.SoloGirl==Girl)return;
 if(Act==TEXT("feed")||Act==TEXT("share_food")){
  // She holds it out to him, he takes it: her hand forward, his hand to hers.
  const FVector Front=FRotator(0,S.Rotations[0].Yaw+90.f,0).Vector();
  S.Agents[Girl-1].Position=FVector(S.Male.X,S.Male.Y,S.Agents[Girl-1].Position.Z)+Front*55.f;
  StartLoverReaction(Girl,FString(TEXT("lover_hand_shy.okanim")),OKLoverTouchSeconds,TEXT("lover_hand_male.okanim"));
 }
 else if(Act==TEXT("eat")||Act==TEXT("alcohol"))StartLoverReaction(Girl,S.DrinkFemaleClip,5.f);
 else if(Act==TEXT("phone_call"))StartLoverReaction(Girl,FString(TEXT("phone_standing.okanim")),6.f);
 else if(Act==TEXT("loud"))StartLoverReaction(Girl,FString(TEXT("lover_shy_2.okanim")),3.f);
 else if(Act==TEXT("lie_down")||Act==TEXT("lap_pillow")){
  // A bed close by: lie down together on it (her head on the pillow beside his for a lap pillow too).
  const FOKStreetPlace* Where=nullptr;
  if(const auto* Bed=OKStreetNearestBed(S.Male,500,nullptr,&Where))StartSleep(*Bed,Where,Girl,true); // a nap, not the night
  else StartLoverReaction(Girl,OKLoverShy(Reaction),OKLoverReactSeconds);
 }
}

// He ends the walk together (the button or Esc): she turns to him and waves goodbye, then goes her own way.
// Other ends of a walk (an activity starting, a kiss, sleep) have no goodbye.
void AOKStreetSocialScene::FarewellCompanion(){
 if(!State||State->Companion==INDEX_NONE)return;auto& S=*State;const int32 Girl=S.Companion;
 FinishCompanion();
 if(S_ValidGirl(Girl)&&FVector::Dist2D(S.Agents[Girl-1].Position,S.Male)<400.f){
  StartLoverReaction(Girl,TEXT("lover_wave.okanim"),3.f);
  UE_LOG(LogTemp,Display,TEXT("Street farewell: actor=%d waves"),Girl);
 }
}

// A gesture with her words (the model's "gesture", motions.toml): one of its clips for how she stands, shown a few
// seconds as her reaction. Mid-activity, on a chair or already reacting, the words go alone; during a photo
// "another_pose" (or a pose's own key) changes her pose.
void AOKStreetSocialScene::PlayGesture(int32 Girl,const FString& Gesture,const FString& Emotion,float SpeechSeconds){
 if(!State||!S_ValidGirl(Girl))return;auto& S=*State;
 if(S.Pair.Partner==Girl){if(Gesture==TEXT("another_pose")||Gesture.StartsWith(TEXT("twoshot_")))SwitchPhotoPose(Gesture==TEXT("another_pose")?FString():Gesture);return;}
 if(S.Pair.Partner!=INDEX_NONE||S.SoloGirl==Girl||S.LoverReact.Girl!=INDEX_NONE||S.LoverPose.Girl!=INDEX_NONE||S.TouchActive||S.Node!=INDEX_NONE)return;
 if(FVector::Dist2D(S.Agents[Girl-1].Position,S.Male)>400.f)return;
 auto Ask=OKRulesRequest(TEXT("motion_gesture"));Ask->SetStringField(TEXT("gesture"),Gesture);Ask->SetStringField(TEXT("posture"),TEXT("stand"));Ask->SetNumberField(TEXT("seed"),S.Random.RandRange(0,1<<30));
 Ask->SetStringField(TEXT("emotion"),Emotion);Ask->SetNumberField(TEXT("speech_seconds"),SpeechSeconds);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Ask);if(!Out)return;
 const FString Clip=Out->GetStringField(TEXT("clip"));
 if(Clip.IsEmpty()){UE_LOG(LogTemp,Display,TEXT("Street gesture: actor=%d %s not shown (%s)"),Girl,*Gesture,*Out->GetStringField(TEXT("why")));return;}
 UE_LOG(LogTemp,Display,TEXT("Street gesture: actor=%d %s -> %s"),Girl,*Gesture,*Clip);
 StartLoverReaction(Girl,Clip,float(Out->GetNumberField(TEXT("seconds"))));
}

// Her next two-shot pose while they pose for a photo: she lets the pose go, the new one is prepared, and both start it
// again from the shared anchor (TickPhoto's preparation phase).
bool AOKStreetSocialScene::SwitchPhotoPose(const FString& Pose){
 if(!State)return false;auto& S=*State;auto& P=S.Pair;
 if(P.Partner==INDEX_NONE||P.Phase!=1||P.CapturePending||S.PhotoPoseKey.IsEmpty())return false;
 auto Ask=OKRulesRequest(TEXT("motion_photo"));Ask->SetBoolField(TEXT("lover"),ContactLover(P.Partner));Ask->SetStringField(TEXT("pose"),Pose);Ask->SetStringField(TEXT("after"),S.PhotoPoseKey);Ask->SetNumberField(TEXT("seed"),S.Random.RandRange(0,1<<30));
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Ask);if(!Out)return false;
 const FString Female=Out->GetStringField(TEXT("female")),Male=Out->GetStringField(TEXT("male"));
 if(Female.IsEmpty()||Female==P.FemaleClip)return false;
 FString Error;ResumeBehavior(P.Partner,Error);
 if(!OKPrewarmCrowdRuntimeAction(this,P.Partner,Female,Error)){UE_LOG(LogTemp,Warning,TEXT("Street photo: pose %s: %s"),*Female,*Error);FinishPhoto(true);return false;}
 if(!Male.IsEmpty()&&Male!=P.MaleClip){ResumeBehavior(0,Error);if(OKPrewarmCrowdRuntimeAction(this,0,Male,Error))P.MaleClip=Male;}
 P.FemaleClip=Female;P.Phase=0;P.RequestedAt=SceneTime();S.PhotoPoseKey=Out->GetStringField(TEXT("key"));
 S.Message=OKT(TEXT("ui.scene.photo_changing_pose"));RefreshUI();
 UE_LOG(LogTemp,Display,TEXT("Street photo: actor=%d changes pose to %s (%s)"),P.Partner,*S.PhotoPoseKey,*Female);
 return true;
}

bool AOKStreetSocialScene::S_ValidGirl(int32 Girl) const{return State&&Girl>0&&State->Agents.IsValidIndex(Girl-1)&&State->Manifest.Characters.IsValidIndex(Girl);}

void AOKStreetSocialScene::StartLoverReaction(int32 Girl,const FString& Clip,float Seconds,const FString& MaleClip,const TCHAR* FemaleRecipe){
 auto& S=*State;if(S.LoverReact.Girl!=INDEX_NONE||S.Pair.Partner==Girl||S.SoloGirl==Girl)return;
 // Her own hand contact (holding hands), set once per heroine before her clip is first admitted.
 if(FemaleRecipe){const FString Key=FString::Printf(TEXT("%d:%s"),Girl,*Clip);if(!S.LoverContacts.Contains(Key)){FString Ignore;OKConfigureCrowdRuntimeActionContact(this,Girl,Clip,FemaleRecipe,Ignore);S.LoverContacts.Add(Key);}}
 FString Error;if(!OKPrewarmCrowdRuntimeAction(this,Girl,Clip,Error)){UE_LOG(LogTemp,Warning,TEXT("Street lover: reaction %s: %s"),*Clip,*Error);return;}
 if(!MaleClip.IsEmpty()){
  // His hand contact is part of the clip's private pose, set once before it is first admitted.
  if(!S.LoverContacts.Contains(MaleClip)){FString Ignore;OKConfigureCrowdRuntimeActionContact(this,0,MaleClip,OKLoverHandRecipe,Ignore);S.LoverContacts.Add(MaleClip);}
  if(!OKPrewarmCrowdRuntimeAction(this,0,MaleClip,Error)){UE_LOG(LogTemp,Warning,TEXT("Street lover: %s: %s"),*MaleClip,*Error);return;}
  const FVector ToHer=S.Agents[Girl-1].Position-S.Male;S.Rotations[0]=OKStreetFacingMovement(ToHer,S.Rotations[0]);
 }
 // Whatever she was walking to waits until this is over.
 S.Commutes.Remove(Girl);S.CommuteFinal.Remove(Girl);
 S.LoverReact=FOKStreetLoverReact();S.LoverReact.Girl=Girl;S.LoverReact.Clip=Clip;S.LoverReact.MaleClip=MaleClip;S.LoverReact.Phase=1;S.LoverReact.At=SceneTime();S.LoverReact.Seconds=Seconds;
 S.LoverReact.WasHeld=S.Agents[Girl-1].Held;S.Agents[Girl-1].Held=true;
 // She turns to him for it.
 const FVector ToHim=S.Male-S.Agents[Girl-1].Position;S.Rotations[Girl]=OKStreetFacingMovement(ToHim,S.Rotations[Girl]);
}

void AOKStreetSocialScene::TickLover(){
 if(!State)return;auto& S=*State;const double Now=SceneTime();
 // The pose: once both play it, hold it a few seconds, then her reaction.
 if(S.LoverPose.Girl!=INDEX_NONE){
  const int32 Girl=S.LoverPose.Girl;
  // The two-shot puts both on one spot: afterwards she steps back in front of him.
  auto StepBack=[&](){if(S.Agents.IsValidIndex(Girl-1)&&FVector::Dist2D(S.Agents[Girl-1].Position,S.Male)<60.f){const FVector Front=FRotator(0,S.Rotations[0].Yaw+90.f,0).Vector();S.Agents[Girl-1].Position=S.Male+Front*80.f;}};
  if(S.Pair.Partner!=Girl){StepBack();S.LoverPose=FOKStreetLoverPose();} // ended early (Esc)
  else if(S.Pair.Phase==1){
   // A selfie two-shot: both face the phone, so from in front, a little to the side.
   if(S.LoverPose.Since==0){S.LoverPose.Since=Now;S.Yaw=S.Pair.Yaw+250.f;S.Pitch=-6.f;S.Distance=240.f;}
   else if(Now-S.LoverPose.Since>OKLoverPoseSeconds){const FString After=S.LoverPose.After;S.LoverPose=FOKStreetLoverPose();FinishPhoto();StepBack();StartLoverReaction(Girl,After,OKLoverReactSeconds);}
  }
 }
 auto& R=S.LoverReact;if(R.Girl==INDEX_NONE)return;
 if(!S.Agents.IsValidIndex(R.Girl-1)){R=FOKStreetLoverReact();return;}
 FOKRuntimeActionStreamStatus A;
 if(R.Phase==1){
  FOKRuntimeActionStreamStatus B;
  const bool HisReady=R.MaleClip.IsEmpty()||(OKGetCrowdRuntimeActionStatus(this,0,B)&&B.Ready&&!B.Pending&&!B.Preparing&&B.ClipId==R.MaleClip);
  if(HisReady&&OKGetCrowdRuntimeActionStatus(this,R.Girl,A)&&A.Ready&&!A.Pending&&!A.Preparing&&A.ClipId==R.Clip){FString Error;OKPlayCrowdRuntimeAction(this,R.Girl,R.Clip,Error);if(!R.MaleClip.IsEmpty())OKPlayCrowdRuntimeAction(this,0,R.MaleClip,Error);R.Phase=2;R.At=Now;}
  else if(Now-R.At>15){if(S.SoloGirl!=R.Girl&&S.Pair.Partner!=R.Girl)S.Agents[R.Girl-1].Held=R.WasHeld||BusyWithPlayer(R.Girl);R=FOKStreetLoverReact();}
  return;
 }
 if(Now-R.At>R.Seconds){
  // Taken over meanwhile by an activity of hers (Raiden's shrine prayer was being made ready when her reaction ran out):
  // her clips and his are the activity's now; resuming her behaviour cancelled the prayer it was preparing.
  if(S.SoloGirl==R.Girl||S.Pair.Partner==R.Girl){UE_LOG(LogTemp,Display,TEXT("Street lover reaction: actor=%d ends inside an activity; its clips are left to it"),R.Girl);R=FOKStreetLoverReact();return;}
  // Held as it was before the reaction, unless he is with her now: a talk opened during her wave holds her too (R6-35: it
  // let her go, she walked off to her bench alone and the bench she then agreed to was refused, "not beside you").
  FString Error;ResumeBehavior(R.Girl,Error);if(!R.MaleClip.IsEmpty())ResumeBehavior(0,Error);S.Agents[R.Girl-1].Held=R.WasHeld||BusyWithPlayer(R.Girl);R=FOKStreetLoverReact();}
}

// What free talk can do that otherwise only a panel button does: give or lend, take the photo, sleep here, ride.
bool AOKStreetSocialScene::HandleWorldAct(int32 Girl,const FString& Kind,const FString& Arg){
 if(!State)return false;auto& S=*State;
 if(Kind==TEXT("gift")){
  // By id or by (part of) the name, among what he carries that can be given.
  // Her "gift" without naming it ("收下了"): which thing is the judge's to read (its gift field); only when he carries
  // a single thing is it that one.
  if(Arg.IsEmpty()){FString Pick;
   if(Pick.IsEmpty()&&S.Store.Inventory.Num()==1)Pick=S.Store.Inventory[0];
   if(!Pick.IsEmpty()){UE_LOG(LogTemp,Display,TEXT("Street gift: her act named nothing; %s is all he carries"),*Pick);StartGift(Pick);return true;}}
  for(const FString& Id:S.Store.Inventory){const auto* Item=S.Catalog.Find(Id);if(!Item)continue;
   if(Id==Arg||(!Arg.IsEmpty()&&(Item->Name.Contains(Arg)||Arg.Contains(Item->Name)))){StartGift(Id);return true;}}
  S.FreeTalkNotice=OKT(TEXT("ui.lover.not_in_backpack"))+Arg+OKT(TEXT("ui.contacts.full_stop"));RefreshUI();return true;
 }
 // Lending is his to offer: her act asks for it; he hands it over when the judge reads that he does (he_lends).
 if(Kind==TEXT("lend")){S.LendAskGirl=Girl;S.LendAskAmount=int32(FMath::Clamp<int64>(Arg.IsEmpty()?Local(TEXT("cash"),200):MoneyIn(Arg),1,Local(TEXT("cash"),100000)));S.LendAskMinute=OKStreetMindNow(S);UE_LOG(LogTemp,Display,TEXT("Street act: she asks to borrow %d; his to hand over"),S.LendAskAmount);return true;}
 // He hands her money to keep (pays her back what she paid for him): her "I'll take it" used to move nothing.
 if(Kind==TEXT("give_money")){const int64 Amount=Arg.IsEmpty()?0:MoneyIn(Arg);
  if(Amount<=0){UE_LOG(LogTemp,Display,TEXT("Street act: give_money without an amount (%s)"),*Arg);return true;}
  // The bill she paid, already handed back by his "my treat" (PayHerBack): her give_money for the same money is that
  // refund, not more. 惠惠's ¥60 set meal went back to her twice and he paid ¥189 for a ¥69 order (R6-58).
  if(Girl==S.PaidBackGirl&&Amount==S.PaidBackAmount&&OKStreetMindNow(S)-S.PaidBackAt<=15){UE_LOG(LogTemp,Display,TEXT("Street act: give_money %lld to actor=%d is the bill already paid back"),Amount,Girl);return true;}
  // What his money does is the judge's reading of both lines: "I'll lend you fifty" was her give_money and the judge's
  // he_lends at once, and he paid a hundred. It waits for the verdict (GiveMoneyNow, or dropped for a loan).
  S.PendingGiveMoney.Add(Girl,TPair<int64,double>(Amount,FPlatformTime::Seconds()));
  UE_LOG(LogTemp,Display,TEXT("Street act: give_money %lld to actor=%d waits for the judge"),Amount,Girl);return true;}
 // She will not take the bill his "my treat" just paid back and hands it back: 胡桃 said "you paid your own, take the
 // money back" and her give_money was read as that refund again, so nothing moved. Only that money, and only soon after.
 if(Kind==TEXT("return_money")){const int64 Amount=FMath::Min<int64>(Arg.IsEmpty()?S.PaidBackAmount:MoneyIn(Arg),S.PaidBackAmount);
  if(Girl!=S.PaidBackGirl||Amount<=0||OKStreetMindNow(S)-S.PaidBackAt>15){UE_LOG(LogTemp,Display,TEXT("Street act: return_money %s from actor=%d is not money he just paid back"),*Arg,Girl);return true;}
  auto Settle=OKRulesHeroineRequest(S,TEXT("return_money_settle"),OKStreetStableId(S.Manifest.Characters[Girl]));Settle->SetNumberField(TEXT("amount"),double(Amount));Settle->SetStringField(TEXT("name"),S.Manifest.Characters[Girl].Name);
  if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Settle)){auto Candidate=S.Store;ApplyRulesEffects(Candidate,*Answer);if(CommitStore(MoveTemp(Candidate)))FlushRulesEffects();}
  S.PaidBackGirl=INDEX_NONE;S.PaidBackAmount=0;
  UE_LOG(LogTemp,Display,TEXT("Street act: actor=%d returns %lld he paid back"),Girl,Amount);RefreshUI();return true;}
 // Only a posing session takes a photo: not in the middle of a meal, a drink, a book or the phone.
 if(Kind==TEXT("take_photo")){
  if(S.Pair.Partner!=INDEX_NONE&&S.Pair.Phase==1&&!S.Pair.Seated&&!S.Pair.Drink&&!S.Pair.Meal&&!S.Pair.PhoneUse&&!S.Pair.Watermelon&&!S.Pair.Splash&&!S.Pair.Read&&!S.Pair.Bath&&!S.Pair.NoProps){if(!S.Pair.CapturePending)CapturePhoto();}
  else if(S.Pair.Partner!=INDEX_NONE){S.FreeTalkNotice=OKT(TEXT("ui.lover.not_time_for_photo"));UE_LOG(LogTemp,Display,TEXT("Street act refused: take_photo during another activity (meal=%d seated=%d drink=%d)"),S.Pair.Meal?1:0,S.Pair.Seated?1:0,S.Pair.Drink?1:0);RefreshUI();}
  return true;
 }
 // She cannot eat any more: she stops eating what is left on the table.
 // Not while she is still very hungry: 惠惠, starving after her explosion, said "just right, I'm full" after one bite of
 // her rice ball and the bento was packed untouched (R6-53). She keeps eating and is told how hungry she still is.
 if(Kind==TEXT("stop_eating")&&OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Girl]),S.Manifest.TalkBudget).Satiety<OKStreetHungrySatiety){
  S.TellHer.Add(OKMsg(TEXT("ui.scene.game_fact_note"),{{TEXT("fact"),OKT(TEXT("ui.lover.still_hungry_not_full"))}}));UE_LOG(LogTemp,Display,TEXT("Street meal: actor=%d stop_eating refused, still very hungry"),Girl);return true;}
 if(Kind==TEXT("stop_eating")){S.StopEating=true;S.FreeTalkNotice=OKT(TEXT("ui.lover.she_is_full"));UE_LOG(LogTemp,Display,TEXT("Street meal: she stops eating by her own word"));RefreshUI();return true;}
 // She does not want a dish he ordered for her: it comes off the order (by name or id).
 if(Kind==TEXT("cancel_order")){
  const int32 Before=S.Cart.Num();
  S.Cart.RemoveAll([&](const FOKStreetCartLine& L){const auto* Item=S.Catalog.Find(L.ItemId);return L.ItemId==Arg||(Item&&!Arg.IsEmpty()&&(Item->Name.Contains(Arg)||Arg.Contains(Item->Name)));});
  if(S.Cart.Num()<Before){S.FreeTalkNotice=OKMsg(TEXT("ui.lover.she_refused_dish_removed"), {{TEXT("dish"), *Arg}});UE_LOG(LogTemp,Display,TEXT("Street order cancelled by her: %s (%d removed)"),*Arg,Before-S.Cart.Num());RefreshUI();}
  return true;
 }
 if(Kind==TEXT("sleep_here")){
  const FOKStreetPlace* Where=nullptr;
  if(const auto* Bed=OKStreetNearestBed(S.Male,400,nullptr,&Where)){StartSleep(*Bed,Where,Girl);}
  else{S.FreeTalkNotice=OKT(TEXT("ui.lover.no_bed_nearby"));RefreshUI();}
  return true;
 }
 if(Kind==TEXT("elevator")){
  // Not in the car yet but in a building with one ("let's take the lift to 9F" in the lobby): the outing she agreed to
  // walks him to it and rides (StepLiftRide); "step into the elevator first" there read as if they could not go.
  const int32 Lift=ElevatorWithMale();
  if(Lift==INDEX_NONE){const bool InLiftBuilding=OKStreetPlaces().ContainsByPredicate([&](const FOKStreetPlace& P){return OKStreetInsidePlace(P,S.Male)&&OKStreetPlaceLifts(P).Num()>=2;});
   if(!InLiftBuilding){S.FreeTalkNotice=OKT(TEXT("ui.lover.enter_elevator_first"));RefreshUI();}else UE_LOG(LogTemp,Display,TEXT("Street act elevator: he is not in the car; the outing takes the lift"));return true;}
  const auto& E=S.Elevators[Lift];
  // "3", "3F", "三樓", "third floor" or part of a stop's label: the rules read it (locale.rs floor_named).
  int32 Floor=INDEX_NONE;
  {auto Request=OKRulesRequest(TEXT("floor_named"));Request->SetStringField(TEXT("text"),Arg);TArray<TSharedPtr<FJsonValue>> Labels;for(const FString& L:E.Labels)Labels.Add(MakeShared<FJsonValueString>(L));Request->SetArrayField(TEXT("labels"),Labels);
   if(const TSharedPtr<FJsonObject> A=OKRulesEval(S,Request))Floor=int32(A->GetNumberField(TEXT("floor")));}
  if(E.Landing.IsValidIndex(Floor))PressElevator(Lift,Floor);
  return true;
 }
 return false;
}

// Extra lines for 【正在做什麼】 so the model knows what the new acts can use.
FString AOKStreetSocialScene::FreeTalkExtras() const{
 if(!State)return FString();const auto& S=*State;FString Out;
 TArray<FString> Bag;for(const FString& Id:S.Store.Inventory)if(const auto* Item=S.Catalog.Find(Id))Bag.AddUnique(Item->Name);
 // An empty bag is said too: 胡桃 twice ate the candy he only said he gave her (R6-56).
 Out+=OKT(TEXT("ui.lover.his_backpack_label"))+(Bag.IsEmpty()?FString(OKT(TEXT("ui.lover.his_backpack_empty"))):FString::Join(Bag,OKT(TEXT("ui.bank.list_separator"))));
 const int32 Lift=ElevatorWithMale();
 if(Lift!=INDEX_NONE)Out+=OKT(TEXT("ui.lover.elevator_reaches_label"))+FString::Join(S.Elevators[Lift].Labels,OKT(TEXT("ui.bank.list_separator")));
 if(S.SleepPhase==0&&OKStreetNearestBed(S.Male,400,nullptr))Out+=OKT(TEXT("ui.lover.bed_nearby_note"));
 if(S.Pair.Partner!=INDEX_NONE&&S.Pair.Phase==1&&!S.Pair.Seated&&!S.Pair.Drink&&!S.Pair.Meal&&!S.Pair.PhoneUse&&!S.Pair.Watermelon&&!S.Pair.Splash&&!S.Pair.Read&&!S.Pair.Bath&&!S.Pair.NoProps)Out+=OKT(TEXT("ui.lover.posing_photo_note"));
 if(const int32 Her=FreeTalkPartner();Her!=INDEX_NONE&&S.HerTreatPromise.Contains(Her))Out+=OKT(TEXT("ui.lover.you_promised_treat_note"));
 if(!S.Table.IsEmpty())Out+=OKT(TEXT("ui.lover.on_table_label"))+DishTableText().Replace(TEXT("\n"),OKT(TEXT("ui.bank.list_separator")));
 Out+=CompanyPromptLine(FreeTalkPartner()); // what she knows of his company
 return Out;
}

// She ended it in her own words ("we're done"): no longer his girlfriend (social.rs breakup_said). The nightly breakup
// only knew favor 0 and resentment, so "let's break up" left her his lover the next morning.
void AOKStreetSocialScene::BreakUpSaid(int32 Character){
 if(!State||!State->Manifest.Characters.IsValidIndex(Character))return;auto& S=*State;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Character]);
 auto Candidate=S.Store;
 auto Request=OKRulesRelationRequest(TEXT("breakup_said"),OKStreetViewRelation(Candidate,Id,S.Manifest.TalkBudget),ProfileOf(Character));
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 ApplyRulesEffects(Candidate,*Answer);
 if(CommitStore(MoveTemp(Candidate)))FlushRulesEffects();
 UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d broke up with him in her own words"),Character);RefreshUI();
}
// He hands her money to keep (pays her back what she paid for him), once the judge has not read it as a loan.
void AOKStreetSocialScene::GiveMoneyNow(int32 Girl,int64 Amount){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl)||Amount<=0)return;
 if(S.Store.PlayerCash<Amount){S.FreeTalkNotice=OKT(TEXT("ui.economy.not_enough_cash_you"));RefreshUI();return;}
 auto Settle=OKRulesHeroineRequest(S,TEXT("give_money_settle"),OKStreetStableId(S.Manifest.Characters[Girl]));Settle->SetNumberField(TEXT("amount"),double(Amount));Settle->SetStringField(TEXT("name"),S.Manifest.Characters[Girl].Name);
 if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Settle)){auto Candidate=S.Store;ApplyRulesEffects(Candidate,*Answer);if(CommitStore(MoveTemp(Candidate)))FlushRulesEffects();}
 UE_LOG(LogTemp,Display,TEXT("Street act: give_money %lld to actor=%d"),Amount,Girl);RefreshUI();
}
// The judge's he_lends and her pending give_money, settled by the rules (shop.rs money_handover): a loan, the gift, or
// still waiting for the verdict.
void AOKStreetSocialScene::SettleMoneyHandover(int32 Girl,double Lend,bool bVerdict){
 auto& S=*State;const TPair<int64,double>* Give=S.PendingGiveMoney.Find(Girl);
 auto Request=OKRulesRequest(TEXT("money_handover"));Request->SetNumberField(TEXT("he_lends"),Lend);Request->SetBoolField(TEXT("verdict"),bVerdict);
 Request->SetNumberField(TEXT("give_pending"),Give?double(Give->Key):0.0);Request->SetNumberField(TEXT("waited"),Give?FPlatformTime::Seconds()-Give->Value:0.0);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return;const FString Action=Out->GetStringField(TEXT("action"));
 if(Action==TEXT("loan")){UE_LOG(LogTemp,Display,TEXT("Street judge: actor=%d he lends %.0f%s"),Girl,Lend,Give?TEXT(" (her give_money is this loan)"):TEXT(""));S.PendingGiveMoney.Remove(Girl);S.LendAskGirl=INDEX_NONE;StartLoan(int32(Lend));
  // Read from a turn she has already answered (R3-261): her「谢谢你」was the answer, yet the money waited 105 s for another
  // reply while she asked if he did not want to lend it after all.
  if(bVerdict)for(auto& C:S.Choices)if(C.Kind==TEXT("loan")&&C.Heroine==Girl){C.SentAtOpen=FMath::Min(C.SentAtOpen,S.FreeTalkAnswered-1);C.Deadline=FMath::Min(C.Deadline,SceneTime()+2.0);}}
 else if(Action==TEXT("give")&&Give){const int64 Amount=Give->Key;S.PendingGiveMoney.Remove(Girl);GiveMoneyNow(Girl,Amount);
  // The money of the bill she paid went back this way: PayHerBack must not hand it over again (R6-58).
  if(Girl==S.HerPaidGirl&&Amount==S.HerPaidAmount){S.HerPaidAmount=0;S.PaidBackGirl=Girl;S.PaidBackAmount=Amount;S.PaidBackAt=OKStreetMindNow(S);}}
}

// Whether a second dish like the one she has goes in (shop.rs order_second_dish): the facts are what is on her table.
bool AOKStreetSocialScene::SecondDishOk(int32 Her,bool bForHer,const FString& ItemId){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Her))return false;
 auto Request=OKRulesRequest(TEXT("order_second_dish"));Request->SetBoolField(TEXT("has_like"),true);Request->SetBoolField(TEXT("for_her"),bForHer);
 Request->SetNumberField(TEXT("satiety"),OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Her]),S.Manifest.TalkBudget).Satiety);
 Request->SetBoolField(TEXT("same_dish"),S.Table.ContainsByPredicate([&](const FOKStreetDish& D){return D.ForHer==bForHer&&(!bForHer||D.Her==Her)&&D.ItemId==ItemId&&D.Percent>0;}));
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);return Out.IsValid()&&Out->GetBoolField(TEXT("order"));
}
