// Included by OKStreetSocialScene.cpp: what the heroines feel for each other. Each one has a favor toward each other heroine
// (directional) and a stage between them (stranger / friend / lover). The rules live in Tools/ok-rules (src/bonds.rs, numbers
// in StreetSocialData/rules/bonds.toml): this side gathers the facts, writes the answers into the store and tells the minds.
// Below zero she will not do things together with that one (follow him, join a meal) unless she is his girlfriend, then she
// comes along reluctantly.

namespace {
const TCHAR* OKBondStageName(const FString& Stage){return Stage==TEXT("lover")?OKT(TEXT("ui.bonds.lover_stage")):Stage==TEXT("friend")?OKT(TEXT("ui.bonds.friend_stage")):OKT(TEXT("ui.bonds.stranger_label"));}
}

FString AOKStreetSocialScene::OrientationOf(int32 G) const{
 const auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(G))return TEXT("straight");
 const FString Id=OKStreetStableId(S.Manifest.Characters[G]);
 const FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
 if(!R.Orientation.IsEmpty())return R.Orientation;
 const FString Profile=OKStreetProfileString(S.MindProfiles.FindRef(Id),nullptr,TEXT("orientation"));
 return Profile==TEXT("bisexual")||Profile.Contains(OKT(TEXT("ui.bonds.bisexual_label")))||Profile.Contains(OKT(TEXT("ui.bonds.bisexual_simplified_label")))?TEXT("bisexual"):TEXT("straight");
}

void AOKStreetSocialScene::SetOrientation(int32 G,const FString& Orientation){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(G)||G<=0)return;
 auto Candidate=S.Store;const int32 I=OKStreetRelationIndex(Candidate,OKStreetStableId(S.Manifest.Characters[G]),S.Manifest.TalkBudget,true);
 if(!Candidate.Relations.IsValidIndex(I))return;
 Candidate.Relations[I].Orientation=Orientation;CommitStore(MoveTemp(Candidate));
 UE_LOG(LogTemp,Display,TEXT("Street bonds: actor %d orientation=%s"),G,*Orientation);RefreshUI();
}

float AOKStreetSocialScene::BondOf(int32 A,int32 B) const{
 auto& S=*State;OKRulesSyncDefaults(S);if(!S.Manifest.Characters.IsValidIndex(A)||!S.Manifest.Characters.IsValidIndex(B))return S.BondStartFavor;
 const FOKStreetRelation R=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[A]),S.Manifest.TalkBudget);
 const float* F=R.Toward.Find(OKStreetStableId(S.Manifest.Characters[B]));return F?*F:S.BondStartFavor;
}

FString AOKStreetSocialScene::BondStageOf(int32 A,int32 B) const{
 const auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(A)||!S.Manifest.Characters.IsValidIndex(B))return TEXT("stranger");
 const FOKStreetRelation R=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[A]),S.Manifest.TalkBudget);
 const FString* Stage=R.TowardStage.Find(OKStreetStableId(S.Manifest.Characters[B]));return Stage?*Stage:FString(TEXT("stranger"));
}

// One heroine as the rules see her (bonds.rs Heroine): orientation, her stranger cap, whether she is his girlfriend, her
// jealousy over him toward the others and her feelings for them, all read from Store.
TSharedRef<FJsonObject> AOKStreetSocialScene::BondFacts(const FOKStreetStore& Store,int32 G) const{
 const auto& S=*State;auto O=MakeShared<FJsonObject>();
 const FString Id=OKStreetStableId(S.Manifest.Characters[G]);
 const FOKStreetRelation R=OKStreetViewRelation(Store,Id,S.Manifest.TalkBudget);
 O->SetNumberField(TEXT("g"),G);O->SetStringField(TEXT("id"),Id);
 O->SetBoolField(TEXT("bisexual"),OrientationOf(G)==TEXT("bisexual"));
 O->SetNumberField(TEXT("stranger_cap"),FMath::Max(0,OKRulesFavorLimit(S,R,TEXT("stranger"))));
 O->SetBoolField(TEXT("girlfriend"),R.Stage==TEXT("lover"));
 auto Jealousy=MakeShared<FJsonObject>();for(const auto& P:R.JealousyToward)Jealousy->SetNumberField(P.Key,P.Value);
 O->SetObjectField(TEXT("jealousy"),Jealousy);
 auto Toward=MakeShared<FJsonObject>();
 for(const auto& P:R.Toward){auto T=MakeShared<FJsonObject>();T->SetNumberField(TEXT("favor"),P.Value);const FString* Stage=R.TowardStage.Find(P.Key);T->SetStringField(TEXT("stage"),Stage?*Stage:FString(TEXT("stranger")));Toward->SetObjectField(P.Key,T);}
 O->SetObjectField(TEXT("toward"),Toward);
 return O;
}

// Writes the rules' answer into Store (both directions' favors and the stage between them) and tells both minds about
// a new stage. Returns whether anything changed.
bool AOKStreetSocialScene::BondApply(FOKStreetStore& Store,const FJsonObject& Outcome){
 auto& S=*State;bool Changed=false;
 auto IdOf=[&](int32 G){return S.Manifest.Characters.IsValidIndex(G)&&G>0?OKStreetStableId(S.Manifest.Characters[G]):FString();};
 const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
 if(Outcome.TryGetArrayField(TEXT("changes"),Rows))for(const auto& V:*Rows){
  const TSharedPtr<FJsonObject> C=V.IsValid()?V->AsObject():nullptr;if(!C)continue;
  const FString IdA=IdOf(int32(C->GetNumberField(TEXT("a")))),IdB=IdOf(int32(C->GetNumberField(TEXT("b"))));if(IdA.IsEmpty()||IdB.IsEmpty()||IdA==IdB)continue;
  const int32 IA=OKStreetRelationIndex(Store,IdA,S.Manifest.TalkBudget,true),IB=OKStreetRelationIndex(Store,IdB,S.Manifest.TalkBudget,true);
  if(!Store.Relations.IsValidIndex(IA)||!Store.Relations.IsValidIndex(IB))continue;
  auto& RA=Store.Relations[IA];auto& RB=Store.Relations[IB];
  RA.Toward.Add(IdB,float(C->GetNumberField(TEXT("favor_ab"))));RB.Toward.Add(IdA,float(C->GetNumberField(TEXT("favor_ba"))));
  // Strangers keep no stage entry, as a save read back has none.
  const FString Stage=C->GetStringField(TEXT("stage"));
  if(Stage==TEXT("stranger")){RA.TowardStage.Remove(IdB);RB.TowardStage.Remove(IdA);}else{RA.TowardStage.Add(IdB,Stage);RB.TowardStage.Add(IdA,Stage);}
  Changed=true;
 }
 if(Outcome.TryGetArrayField(TEXT("transitions"),Rows))for(const auto& V:*Rows){
  const TSharedPtr<FJsonObject> T=V.IsValid()?V->AsObject():nullptr;if(!T)continue;
  const int32 A=int32(T->GetNumberField(TEXT("a"))),B=int32(T->GetNumberField(TEXT("b")));if(IdOf(A).IsEmpty()||IdOf(B).IsEmpty())continue;
  const FString From=T->GetStringField(TEXT("from")),Next=T->GetStringField(TEXT("to")),NA=S.Manifest.Characters[A].Name,NB=S.Manifest.Characters[B].Name;
  const TCHAR* What=Next==TEXT("lover")?OKT(TEXT("ui.bonds.dating_status_label")):Next==TEXT("friend")?OKT(TEXT("ui.bonds.became_friends")):OKT(TEXT("ui.bonds.fell_out_stranger"));
  UE_LOG(LogTemp,Display,TEXT("Street bonds: %s and %s %s -> %s (%s; %.0f / %.0f)"),*NA,*NB,*From,*Next,*T->GetStringField(TEXT("why")),T->GetNumberField(TEXT("favor_ab")),T->GetNumberField(TEXT("favor_ba")));
  MindObserve(A,TEXT("bond"),OKMsg(TEXT("ui.bonds.me_with_name_what"), {{TEXT("name"), *NB}, {TEXT("what"), What}}),FString(),0.6f);
  MindObserve(B,TEXT("bond"),OKMsg(TEXT("ui.bonds.me_with_name_what"), {{TEXT("name"), *NA}, {TEXT("what"), What}}),FString(),0.6f);
 }
 return Changed;
}

// A single event between two of them, by name (bonds.toml [events]: a meeting, being stood up while with her, a game
// together), written into Store.
void AOKStreetSocialScene::BondEventIn(FOKStreetStore& Store,int32 A,int32 B,const FString& Event){
 auto& S=*State;if(A==B||!S.Manifest.Characters.IsValidIndex(A)||!S.Manifest.Characters.IsValidIndex(B)||A<=0||B<=0)return;
 auto Request=OKRulesRequest(TEXT("bond_event"));Request->SetStringField(TEXT("event"),Event);
 Request->SetObjectField(TEXT("a"),BondFacts(Store,A));Request->SetObjectField(TEXT("b"),BondFacts(Store,B));
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return;
 BondApply(Store,*Out);
 UE_LOG(LogTemp,Display,TEXT("Street bonds: event %s %d<->%d"),*Event,A,B);
}

// The same as one save.
void AOKStreetSocialScene::BondEvent(int32 A,int32 B,const FString& Event){
 auto Candidate=State->Store;BondEventIn(Candidate,A,B,Event);CommitStore(MoveTemp(Candidate));
}

// Every few game minutes (bonds.toml together.tick_minutes): the rules see where each heroine is, what her plan has her
// doing and whether she is with him, and answer how their feelings for each other move.
void AOKStreetSocialScene::TickBonds(){
 auto& S=*State;if(S.SelfTest||!S.Rules)return;OKRulesSyncDefaults(S);const int64 Now=OKStreetMindNow(S);
 if(S.BondTickMinute<0){S.BondTickMinute=Now;return;}
 if(Now-S.BondTickMinute<S.BondTickMinutes)return;
 const int64 Minutes=Now-S.BondTickMinute;S.BondTickMinute=Now;
 const int32 Minute=int32(Now%1440);
 TArray<int32> WithHim=S.Party;if(FreeTalkPartner()!=INDEX_NONE)WithHim.AddUnique(FreeTalkPartner());
 TArray<TSharedPtr<FJsonValue>> Heroines;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  TSharedRef<FJsonObject> H=BondFacts(S.Store,I);
  if(S.Agents.IsValidIndex(I-1)){const FVector P=S.Agents[I-1].Position;H->SetArrayField(TEXT("pos"),{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)});}
  if(const FOKStreetPlanBlock* B=LifeBlock(I,Minute)){H->SetStringField(TEXT("activity"),B->Kind);H->SetStringField(TEXT("place"),B->Place);}
  H->SetBoolField(TEXT("with_him"),WithHim.Contains(I));
  Heroines.Add(MakeShared<FJsonValueObject>(H));
 }
 auto Request=OKRulesRequest(TEXT("bonds_tick"));Request->SetNumberField(TEXT("minutes"),double(Minutes));Request->SetArrayField(TEXT("heroines"),Heroines);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return;
 auto Candidate=S.Store;const bool Changed=BondApply(Candidate,*Out);
 if(Changed)CommitStore(MoveTemp(Candidate));
}

// Whether she will be in a group with these others: refuse (2: someone she dislikes), reluctant (1: she dislikes someone
// but is his girlfriend), or fine (0). Who: the one she dislikes most.
int32 AOKStreetSocialScene::BondMood(int32 Girl,const TArray<int32>& Others,int32* Who) const{
 const auto& S=*State;if(Who)*Who=INDEX_NONE;
 if(!S.Manifest.Characters.IsValidIndex(Girl)||Girl<=0)return 0;
 TArray<TSharedPtr<FJsonValue>> Rows;
 for(const int32 M:Others)if(S.Manifest.Characters.IsValidIndex(M)&&M>0){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("g"),M);O->SetStringField(TEXT("id"),OKStreetStableId(S.Manifest.Characters[M]));Rows.Add(MakeShared<FJsonValueObject>(O));}
 auto Request=OKRulesRequest(TEXT("bond_mood"));Request->SetObjectField(TEXT("girl"),BondFacts(S.Store,Girl));Request->SetArrayField(TEXT("others"),Rows);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return 0;
 const int32 Mood=int32(Out->GetNumberField(TEXT("mood"))),Worst=int32(Out->GetNumberField(TEXT("who")));
 if(Who)*Who=Worst>=0?Worst:INDEX_NONE;
 return Mood;
}

// For the group talk: what they are to each other (context only).
FString AOKStreetSocialScene::BondText(const TArray<int32>& Members) const{
 const auto& S=*State;TArray<FString> Lines;
 for(const int32 A:Members)for(const int32 B:Members)if(A!=B&&S.Manifest.Characters.IsValidIndex(A)&&S.Manifest.Characters.IsValidIndex(B)){
  const float F=BondOf(A,B);
  Lines.Add(OKMsg(TEXT("ui.bonds.pair_bond_summary"), {{TEXT("name_a"), *SpokenName(A)}, {TEXT("name_b"), *SpokenName(B)}, {TEXT("stage"), OKBondStageName(BondStageOf(A,B))}, {TEXT("affection"), F}, {TEXT("dislike_suffix"), F<0?OKT(TEXT("ui.bonds.dislike_her_note")):TEXT("")}}));
 }
 return FString::Join(Lines,TEXT("\n"));
}

// The panel line: her orientation and what she feels for each of the others.
FString AOKStreetSocialScene::BondPanelText(int32 G) const{
 const auto& S=*State;FString Out=OKMsg(TEXT("ui.bonds.orientation_line"), {{TEXT("orientation"), OrientationOf(G)==TEXT("bisexual")?OKT(TEXT("ui.bonds.bisexual_full_label")):OKT(TEXT("ui.bonds.straight_label"))}});
 for(int32 O=1;O<S.Manifest.Characters.Num();++O)if(O!=G)Out+=OKMsg(TEXT("ui.bonds.bond_line"), {{TEXT("name"), *S.Manifest.Characters[O].Name}, {TEXT("stage"), OKBondStageName(BondStageOf(G,O))}, {TEXT("bond"), BondOf(G,O)}});
 return Out;
}
