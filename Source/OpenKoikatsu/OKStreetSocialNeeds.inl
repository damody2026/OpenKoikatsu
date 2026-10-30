// Included by OKStreetSocialScene.cpp: the heroines' needs on game time. The rules (Tools/ok-rules src/needs.rs, numbers in
// StreetSocialData/rules/needs.toml) decay them and give some back by what her day has her doing; this side passes each
// heroine's needs, the minutes since they were last updated and her current block, and writes the answer back. The store
// is saved with the hourly calendar save.
void AOKStreetSocialScene::TickNeeds(){
 auto& S=*State;if(S.SelfTest||!S.Rules)return;OKRulesSyncDefaults(S);const int64 Now=OKStreetMindNow(S);
 // The clock set back ("-1 hour", a test rewinding the day): counted from now. Left in the future, every heroine's needs
 // stood still until the clock caught up (all ten at satiety 100 from lunch to 21:42).
 if(S.NeedsTickMinute>Now)S.NeedsTickMinute=-1;
 if(S.NeedsTickMinute>=0&&Now-S.NeedsTickMinute<S.NeedsTickMinutes)return;S.NeedsTickMinute=Now;
 const int32 Minute=int32(Now%1440);
 TArray<TSharedPtr<FJsonValue>> Rows;
 for(auto& R:S.Store.Relations){
  if(R.NeedsMinute<0||R.NeedsMinute>Now){R.NeedsMinute=Now;continue;}
  auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),R.Id);O->SetNumberField(TEXT("minutes"),double(Now-R.NeedsMinute));
  auto N=MakeShared<FJsonObject>();N->SetNumberField(TEXT("satiety"),R.Satiety);N->SetNumberField(TEXT("energy"),R.Energy);N->SetNumberField(TEXT("fun"),R.Fun);
  N->SetNumberField(TEXT("social"),R.Social);N->SetNumberField(TEXT("hygiene"),R.Hygiene);O->SetObjectField(TEXT("needs"),N);
  // What she is doing: her plan's block, or the urgent cut-in (hungry, worn out, a bath) that replaced it (LifeBlock).
  const int32 I=CharacterOf(R.Id);
  if(I!=INDEX_NONE)O->SetNumberField(TEXT("satiety_max"),SatietyMax(I)); // her appetite: a meal fills her by its share (R3-269)
  // With him (talking, walking along, an activity together): that is what she is doing, not her plan's block (a meal
  // block fed her while she only stood talking to him; what they eat together feeds her through the table).
  const bool WithHim=I!=INDEX_NONE&&(S.Target==I||FreeTalkPartner()==I||S.Companion==I||S.OutingGirl==I||S.Pair.Partner==I||S.Party.Contains(I));
  if(WithHim)O->SetStringField(TEXT("activity"),TEXT("social"));
  else if(const FOKStreetPlanBlock* B=I==INDEX_NONE?nullptr:LifeBlock(I,Minute)){
   const FString Home=OKStreetProfileString(S.MindProfiles.FindRef(R.Id),nullptr,TEXT("home_place_id"));const bool AtHome=B->Place==Home||B->Place==TEXT("girls_dorm");
   // Not there yet (on her way, or put back beside him in the park after a load while her block says lunch at the
   // department store): it does not feed or rest her. 惠惠 went from satiety 87 to 99 standing in the park.
   const bool Away=!AtHome&&!B->Place.IsEmpty()&&S.Agents.IsValidIndex(I-1)&&PlaceHere(S.Agents[I-1].Position)!=B->Place;
   if(!Away)O->SetStringField(TEXT("activity"),B->Kind);else UE_LOG(LogTemp,Display,TEXT("Street needs: actor %d is not at %s yet (%s there), no %s gain"),I,*B->Place,*PlaceHere(S.Agents[I-1].Position),*B->Kind);O->SetBoolField(TEXT("at_home"),AtHome);
  }
  Rows.Add(MakeShared<FJsonValueObject>(O));
 }
 if(Rows.IsEmpty())return;
 auto Request=OKRulesRequest(TEXT("needs_tick"));Request->SetNumberField(TEXT("hour"),Minute/60);Request->SetArrayField(TEXT("heroines"),Rows);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return;
 const TArray<TSharedPtr<FJsonValue>>* Updated=nullptr;if(!Out->TryGetArrayField(TEXT("heroines"),Updated))return;
 for(const auto& V:*Updated){
  const TSharedPtr<FJsonObject> U=V.IsValid()?V->AsObject():nullptr;const TSharedPtr<FJsonObject>* N=nullptr;if(!U||!U->TryGetObjectField(TEXT("needs"),N))continue;
  FOKStreetRelation* R=S.Store.Relations.FindByPredicate([&](const FOKStreetRelation& X){return X.Id==U->GetStringField(TEXT("id"));});if(!R)continue;
  R->Satiety=float((*N)->GetNumberField(TEXT("satiety")));R->Energy=float((*N)->GetNumberField(TEXT("energy")));R->Fun=float((*N)->GetNumberField(TEXT("fun")));
  R->Social=float((*N)->GetNumberField(TEXT("social")));R->Hygiene=float((*N)->GetNumberField(TEXT("hygiene")));R->NeedsMinute=Now;
 }
}
