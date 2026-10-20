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
  else if(Now-R.At>15){S.Agents[R.Girl-1].Held=R.WasHeld;R=FOKStreetLoverReact();}
  return;
 }
 if(Now-R.At>R.Seconds){FString Error;OKResumeCrowdRuntimeBehavior(this,R.Girl,Error);if(!R.MaleClip.IsEmpty())OKResumeCrowdRuntimeBehavior(this,0,Error);S.Agents[R.Girl-1].Held=R.WasHeld;R=FOKStreetLoverReact();}
}

// What free talk can do that otherwise only a panel button does: give or lend, take the photo, sleep here, ride.
bool AOKStreetSocialScene::HandleWorldAct(int32 Girl,const FString& Kind,const FString& Arg){
 if(!State)return false;auto& S=*State;
 if(Kind==TEXT("gift")){
  // By id or by (part of) the name, among what he carries that can be given.
  // Her "gift" without naming it ("收下了" to his "这套文具给你"): what his words name, else the one thing he carries.
  // His line this turn: LastPlayerSaid is already cleared when her act runs (the orders took it), the talk's own record is not.
  if(Arg.IsEmpty()){FString Pick;const FString Said=S.SaidToHer.Num()?S.SaidToHer.Last():S.LastPlayerSaid;
   for(const FString& Id:S.Store.Inventory)if(const auto* Item=S.Catalog.Find(Id);Item&&Said.Contains(Item->Name)){Pick=Id;break;}
   if(Pick.IsEmpty())for(const FString& Id:S.Store.Inventory)if(const auto* Item=S.Catalog.Find(Id);Item&&Item->Name.Len()>=2&&Said.Contains(Item->Name.Left(2))){Pick=Id;break;}
   if(Pick.IsEmpty()&&S.Store.Inventory.Num()==1)Pick=S.Store.Inventory[0];
   if(!Pick.IsEmpty()){UE_LOG(LogTemp,Display,TEXT("Street gift: her act named nothing; %s from his words"),*Pick);StartGift(Pick);return true;}}
  for(const FString& Id:S.Store.Inventory){const auto* Item=S.Catalog.Find(Id);if(!Item)continue;
   if(Id==Arg||(!Arg.IsEmpty()&&(Item->Name.Contains(Arg)||Arg.Contains(Item->Name)))){StartGift(Id);return true;}}
  S.FreeTalkNotice=OKT(TEXT("ui.lover.not_in_backpack"))+Arg+OKT(TEXT("ui.contacts.full_stop"));RefreshUI();return true;
 }
 // Lending is his to offer: her act alone (he said he only has about a thousand) opened a loan he never offered.
 if(Kind==TEXT("lend")){auto Ask=OKRulesRequest(TEXT("lend_offered"));const FString His=S.SaidToHer.Num()?S.SaidToHer.Last():S.LastPlayerSaid;Ask->SetStringField(TEXT("his"),His);
  if(const TSharedPtr<FJsonObject> Offered=OKRulesEval(S,Ask);Offered.IsValid()&&!Offered->GetBoolField(TEXT("offered"))){UE_LOG(LogTemp,Display,TEXT("Street act: lend skipped, he did not offer (%s)"),*His.Left(60));
   S.LendAskGirl=Girl;S.LendAskAmount=int32(FMath::Clamp<int64>(Arg.IsEmpty()?Local(TEXT("cash"),200):MoneyIn(Arg),1,Local(TEXT("cash"),100000)));S.LendAskMinute=OKStreetMindNow(S);return true;}}
 if(Kind==TEXT("lend"))S.LendAskGirl=INDEX_NONE;
 if(Kind==TEXT("lend")){const int32 Amount=int32(FMath::Clamp<int64>(Arg.IsEmpty()?Local(TEXT("cash"),200):MoneyIn(Arg),1,Local(TEXT("cash"),100000)));StartLoan(Amount);return true;}
 // He hands her money to keep (pays her back what she paid for him): her "I'll take it" used to move nothing.
 if(Kind==TEXT("give_money")){const int64 Amount=Arg.IsEmpty()?0:MoneyIn(Arg);
  if(Amount<=0){UE_LOG(LogTemp,Display,TEXT("Street act: give_money without an amount (%s)"),*Arg);return true;}
  if(S.Store.PlayerCash<Amount){S.FreeTalkNotice=OKT(TEXT("ui.economy.not_enough_cash_you"));RefreshUI();return true;}
  auto Settle=OKRulesHeroineRequest(S,TEXT("give_money_settle"),OKStreetStableId(S.Manifest.Characters[Girl]));Settle->SetNumberField(TEXT("amount"),double(Amount));Settle->SetStringField(TEXT("name"),S.Manifest.Characters[Girl].Name);
  if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Settle)){auto Candidate=S.Store;ApplyRulesEffects(Candidate,*Answer);if(CommitStore(MoveTemp(Candidate)))FlushRulesEffects();}
  UE_LOG(LogTemp,Display,TEXT("Street act: give_money %lld to actor=%d"),Amount,Girl);RefreshUI();return true;}
 // Only a posing session takes a photo: not in the middle of a meal, a drink, a book or the phone.
 if(Kind==TEXT("take_photo")){
  if(S.Pair.Partner!=INDEX_NONE&&S.Pair.Phase==1&&!S.Pair.Seated&&!S.Pair.Drink&&!S.Pair.Meal&&!S.Pair.PhoneUse&&!S.Pair.Watermelon&&!S.Pair.Splash&&!S.Pair.Read&&!S.Pair.Bath&&!S.Pair.NoProps){if(!S.Pair.CapturePending)CapturePhoto();}
  else if(S.Pair.Partner!=INDEX_NONE){S.FreeTalkNotice=OKT(TEXT("ui.lover.not_time_for_photo"));UE_LOG(LogTemp,Display,TEXT("Street act refused: take_photo during another activity (meal=%d seated=%d drink=%d)"),S.Pair.Meal?1:0,S.Pair.Seated?1:0,S.Pair.Drink?1:0);RefreshUI();}
  return true;
 }
 // She cannot eat any more: she stops eating what is left on the table.
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
 if(!Bag.IsEmpty())Out+=OKT(TEXT("ui.lover.his_backpack_label"))+FString::Join(Bag,OKT(TEXT("ui.bank.list_separator")));
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
