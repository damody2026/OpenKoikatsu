// Included by OKStreetSocialScene.cpp before the systems that ask the game rules (ok_rules.dll, Tools/ok-rules): a request
// builder, one place that logs failed calls, and the values the scene caches from the rules (re-read after a reload).

namespace {
// `rule` names the rule that answers; the request's own data may use `kind` (a rumour's, an activity's) freely.
// Every request says the language and the region (ok-rules locale.rs): the rules write their words in it.
FString GOKStreetLang=TEXT("zh-TW"),GOKStreetRegion=TEXT("CN");
TSharedRef<FJsonObject> OKRulesRequest(const TCHAR* Rule){auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("rule"),Rule);R->SetStringField(TEXT("lang"),GOKStreetLang);R->SetStringField(TEXT("region"),GOKStreetRegion);return R;}

// The rules' answer, or null (logged) when the rules are missing or refuse the request.
TSharedPtr<FJsonObject> OKRulesEval(const FOKStreetSceneState& S,const TSharedRef<FJsonObject>& Request){
 if(!S.Rules)return nullptr;FString Error;
 TSharedPtr<FJsonObject> Out=S.Rules->Eval(Request,Error);
 if(!Out.IsValid())UE_LOG(LogTemp,Warning,TEXT("Street rules: %s failed: %s"),*Request->GetStringField(TEXT("rule")),*Error);
 return Out;
}

// A relation as the rules see it (Tools/ok-rules src/relation.rs Relation): the scalar fields under the same names on both
// sides. Rules answer with only the fields that changed; OKRulesPatchRelation writes those back.
TSharedRef<FJsonObject> OKRulesRelationJson(const FOKStreetRelation& R,bool bTalk=false){
 auto O=MakeShared<FJsonObject>();
 O->SetStringField(TEXT("id"),R.Id);O->SetNumberField(TEXT("favor"),R.Favor);O->SetNumberField(TEXT("lewdness"),R.Lewdness);O->SetStringField(TEXT("stage"),R.Stage);
 O->SetNumberField(TEXT("talks_remaining"),R.TalksRemaining);O->SetNumberField(TEXT("day"),R.Day);O->SetNumberField(TEXT("sex_desire"),R.SexDesire);O->SetNumberField(TEXT("topic_points"),R.TopicPoints);
 for(const auto& A:OKStreetAxisFields(const_cast<FOKStreetRelation&>(R)))O->SetNumberField(A.Key,*A.Value);
 auto Toward=MakeShared<FJsonObject>();for(const auto& P:R.JealousyToward)Toward->SetNumberField(P.Key,P.Value);O->SetObjectField(TEXT("jealousy_toward"),Toward);
 O->SetStringField(TEXT("emotion"),R.Emotion);O->SetNumberField(TEXT("emotion_intensity"),R.EmotionIntensity);O->SetNumberField(TEXT("emotion_minute"),double(R.EmotionMinute));
 O->SetNumberField(TEXT("cash"),double(R.Cash));O->SetNumberField(TEXT("bank"),double(R.Bank));O->SetNumberField(TEXT("owes_player"),double(R.OwesPlayer));O->SetNumberField(TEXT("debt"),double(R.Debt));O->SetNumberField(TEXT("treat_balance"),R.TreatBalance);
 O->SetNumberField(TEXT("satiety"),R.Satiety);O->SetNumberField(TEXT("energy"),R.Energy);O->SetNumberField(TEXT("fun"),R.Fun);O->SetNumberField(TEXT("social"),R.Social);O->SetNumberField(TEXT("hygiene"),R.Hygiene);
 O->SetNumberField(TEXT("needs_minute"),double(R.NeedsMinute));O->SetNumberField(TEXT("perceived_wealth"),R.PerceivedWealth);O->SetNumberField(TEXT("chastity"),R.Chastity);O->SetNumberField(TEXT("stranger_cap"),R.StrangerCap);
 TArray<TSharedPtr<FJsonValue>> Flags;for(const FString& F:R.Flags)Flags.Add(MakeShared<FJsonValueString>(F));O->SetArrayField(TEXT("flags"),Flags);
 // The talk core's record goes only to the talk rules (talk.rs): lines talked, transactions, topics, cooldowns.
 if(bTalk){
  auto List=[&](const TCHAR* Name,const TArray<FString>& Values){TArray<TSharedPtr<FJsonValue>> Rows;for(const FString& V:Values)Rows.Add(MakeShared<FJsonValueString>(V));O->SetArrayField(Name,Rows);};
  List(TEXT("talked"),R.Talked);List(TEXT("committed"),R.Committed);List(TEXT("topics"),R.Topics);
  auto Cool=MakeShared<FJsonObject>();for(int32 K=0;K<R.CooldownIds.Num()&&K<R.CooldownDays.Num();++K)Cool->SetNumberField(R.CooldownIds[K],R.CooldownDays[K]);O->SetObjectField(TEXT("cooldowns"),Cool);
 }
 return O;
}

void OKRulesPatchRelation(FOKStreetRelation& R,const FJsonObject& Patch){
 for(const auto& P:Patch.Values){
  const FString K(*P.Key);const TSharedPtr<FJsonValue>& V=P.Value;if(!V.IsValid())continue;
  auto Num=[&](){return V->AsNumber();};
  if(float* Axis=[&]()->float*{for(const auto& A:OKStreetAxisFields(R))if(A.Key==K)return A.Value;return nullptr;}()){*Axis=float(Num());continue;}
  if(K==TEXT("favor"))R.Favor=float(Num());else if(K==TEXT("lewdness"))R.Lewdness=float(Num());else if(K==TEXT("stage"))R.Stage=V->AsString();
  else if(K==TEXT("talks_remaining"))R.TalksRemaining=int32(Num());else if(K==TEXT("day"))R.Day=int32(Num());else if(K==TEXT("sex_desire"))R.SexDesire=float(Num());else if(K==TEXT("topic_points"))R.TopicPoints=float(Num());
  else if(K==TEXT("jealousy_toward")){R.JealousyToward.Reset();if(const TSharedPtr<FJsonObject> O=V->AsObject())for(const auto& J:O->Values)R.JealousyToward.Add(FString(*J.Key),float(J.Value->AsNumber()));}
  else if(K==TEXT("emotion"))R.Emotion=V->AsString();else if(K==TEXT("emotion_intensity"))R.EmotionIntensity=float(Num());else if(K==TEXT("emotion_minute"))R.EmotionMinute=int64(Num());
  else if(K==TEXT("cash"))R.Cash=int64(Num());else if(K==TEXT("bank"))R.Bank=int64(Num());else if(K==TEXT("owes_player"))R.OwesPlayer=int64(Num());else if(K==TEXT("debt"))R.Debt=int64(Num());else if(K==TEXT("treat_balance"))R.TreatBalance=int32(Num());
  else if(K==TEXT("satiety"))R.Satiety=float(Num());else if(K==TEXT("energy"))R.Energy=float(Num());else if(K==TEXT("fun"))R.Fun=float(Num());else if(K==TEXT("social"))R.Social=float(Num());else if(K==TEXT("hygiene"))R.Hygiene=float(Num());
  else if(K==TEXT("needs_minute"))R.NeedsMinute=int64(Num());else if(K==TEXT("perceived_wealth"))R.PerceivedWealth=float(Num());else if(K==TEXT("chastity"))R.Chastity=float(Num());else if(K==TEXT("stranger_cap"))R.StrangerCap=int32(Num());
  else if(K==TEXT("flags")){R.Flags.Reset();for(const auto& F:V->AsArray())R.Flags.Add(F->AsString());}
  else if(K==TEXT("talked")){R.Talked.Reset();for(const auto& F:V->AsArray())R.Talked.Add(F->AsString());}
  else if(K==TEXT("committed")){R.Committed.Reset();for(const auto& F:V->AsArray())R.Committed.Add(F->AsString());}
  else if(K==TEXT("topics")){R.Topics.Reset();for(const auto& F:V->AsArray())R.Topics.Add(F->AsString());}
  else if(K==TEXT("cooldowns")){R.CooldownIds.Reset();R.CooldownDays.Reset();if(const TSharedPtr<FJsonObject> C=V->AsObject()){TArray<FString> Keys;for(const auto& E:C->Values)Keys.Add(FString(*E.Key));Keys.Sort();for(const FString& Key:Keys){R.CooldownIds.Add(Key);R.CooldownDays.Add(int32(C->GetNumberField(Key)));}}}
  else if(K!=TEXT("id"))UE_LOG(LogTemp,Warning,TEXT("Street rules: unknown relation field %s"),*K);
 }
}

// A request about one relation: her record and her profile (the rules read her traits from it).
TSharedRef<FJsonObject> OKRulesRelationRequest(const TCHAR* Kind,const FOKStreetRelation& R,const TSharedPtr<FJsonObject>& Profile){
 auto Request=OKRulesRequest(Kind);Request->SetObjectField(TEXT("relation"),OKRulesRelationJson(R));
 Request->SetObjectField(TEXT("profile"),Profile.IsValid()?Profile.ToSharedRef():MakeShared<FJsonObject>());
 return Request;
}
// The same for a heroine by stable id, as the store has her now.
TSharedRef<FJsonObject> OKRulesHeroineRequest(const FOKStreetSceneState& S,const TCHAR* Kind,const FString& Id){
 return OKRulesRelationRequest(Kind,OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget),S.MindProfiles.FindRef(Id));
}

// A talk effect both ways (talk.rs Effect).
TSharedRef<FJsonObject> OKRulesEffectJson(const FOKStreetEffect& E){
 auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("favor"),E.Favor);O->SetNumberField(TEXT("lewdness"),E.Lewdness);O->SetStringField(TEXT("relation_event"),E.RelationEvent);O->SetBoolField(TEXT("reset_favor"),E.ResetFavor);
 TArray<TSharedPtr<FJsonValue>> Flags;for(const FString& F:E.SetFlags)Flags.Add(MakeShared<FJsonValueString>(F));O->SetArrayField(TEXT("set_flags"),Flags);
 O->SetNumberField(TEXT("talk_cost"),E.TalkCost);O->SetStringField(TEXT("memory"),E.Memory);O->SetNumberField(TEXT("topic_id"),E.TopicId);O->SetNumberField(TEXT("cooldown_days"),E.CooldownDays);return O;
}
FOKStreetEffect OKRulesReadEffect(const FJsonObject& O){
 FOKStreetEffect E;double V=0;
 if(O.TryGetNumberField(TEXT("favor"),V))E.Favor=float(V);if(O.TryGetNumberField(TEXT("lewdness"),V))E.Lewdness=float(V);O.TryGetStringField(TEXT("relation_event"),E.RelationEvent);O.TryGetBoolField(TEXT("reset_favor"),E.ResetFavor);
 const TArray<TSharedPtr<FJsonValue>>* Flags=nullptr;if(O.TryGetArrayField(TEXT("set_flags"),Flags))for(const auto& F:*Flags)E.SetFlags.Add(F->AsString());
 if(O.TryGetNumberField(TEXT("talk_cost"),V))E.TalkCost=int32(V);O.TryGetStringField(TEXT("memory"),E.Memory);if(O.TryGetNumberField(TEXT("topic_id"),V))E.TopicId=int32(V);if(O.TryGetNumberField(TEXT("cooldown_days"),V))E.CooldownDays=int32(V);
 return E;
}
// A request to the talk rules about her: her talk record, personality, the budget and its switch.
TSharedRef<FJsonObject> OKRulesTalkRequest(const FOKStreetSceneState& S,const TCHAR* Kind,const FOKStreetRelation& R,const FOKStreetCharacterMeta* Meta){
 auto Request=OKRulesRequest(Kind);Request->SetObjectField(TEXT("relation"),OKRulesRelationJson(R,true));
 if(Meta){Request->SetStringField(TEXT("personality"),Meta->Personality);Request->SetNumberField(TEXT("personality_id"),Meta->PersonalityId);}
 Request->SetNumberField(TEXT("budget"),S.Manifest.TalkBudget);Request->SetBoolField(TEXT("talk_limit"),GOKStreetTalkLimit);return Request;
}
// Her favor cap at a stage (talk.rs favor_limit); INDEX_NONE without step-up rules. The displays ask every frame, so the
// answers are kept by what they depend on (stage, her own stranger cap) until the rules are loaded again.
int32 OKRulesFavorLimit(const FOKStreetSceneState& S,const FOKStreetRelation& R,const FString& Stage){
 const FString Key=FString::Printf(TEXT("%s|%d"),*Stage,R.StrangerCap);if(const int32* Known=S.FavorLimitCache.Find(Key))return *Known;
 auto Request=OKRulesRequest(TEXT("favor_limit"));Request->SetObjectField(TEXT("relation"),OKRulesRelationJson(R));Request->SetStringField(TEXT("stage"),Stage);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return INDEX_NONE;
 const int32 Limit=int32(Out->GetNumberField(TEXT("limit")));S.FavorLimitCache.Add(Key,Limit<0?INDEX_NONE:Limit);return Limit<0?INDEX_NONE:Limit;
}
// A finished line or event written into her record on Store (talk.rs talk_commit).
FOKStreetCommitResult OKRulesTalkCommit(const FOKStreetSceneState& S,FOKStreetStore& Store,const FString& Id,const FString& Transaction,const FOKStreetEffect& Effect){
 FOKStreetCommitResult Result;if(Id.IsEmpty()){Result.Reason=TEXT("no_stable_id");return Result;}
 const int32 Index=OKStreetRelationIndex(Store,Id,S.Manifest.TalkBudget,true);
 auto Request=OKRulesTalkRequest(S,TEXT("talk_commit"),Store.Relations[Index],nullptr);Request->SetStringField(TEXT("transaction"),Transaction);Request->SetObjectField(TEXT("effect"),OKRulesEffectJson(Effect));
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out){Result.Reason=TEXT("rules_unavailable");return Result;}
 Result.Applied=Out->GetBoolField(TEXT("applied"));Out->TryGetBoolField(TEXT("duplicate"),Result.Duplicate);Out->TryGetStringField(TEXT("reason"),Result.Reason);
 const TSharedPtr<FJsonObject>* Patch=nullptr;if(Result.Applied&&Out->TryGetObjectField(TEXT("relation"),Patch))OKRulesPatchRelation(Store.Relations[Index],**Patch);
 return Result;
}
// An invitation (3 meal, 4 walk) may be asked now; Effect is what asking costs (talk.rs invite_check).
bool OKRulesCanInvite(const FOKStreetSceneState& S,const FOKStreetRelation& R,int32 Command,FString& Reason,FOKStreetEffect* Effect=nullptr){
 auto Request=OKRulesTalkRequest(S,TEXT("invite_check"),R,nullptr);Request->SetNumberField(TEXT("command"),Command);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out){Reason=TEXT("rules_unavailable");return false;}
 if(!Out->GetBoolField(TEXT("ok"))){Reason=Out->GetStringField(TEXT("reason"));return false;}
 if(Effect)*Effect=OKRulesReadEffect(*Out->GetObjectField(TEXT("effect")));Reason.Reset();return true;
}
FOKStreetCommitResult OKRulesCommitInvitation(const FOKStreetSceneState& S,FOKStreetStore& Store,const FString& Id,int32 Command,const FString& Transaction){
 FOKStreetCommitResult Result;FOKStreetEffect Effect;
 if(!OKRulesCanInvite(S,OKStreetViewRelation(Store,Id,S.Manifest.TalkBudget),Command,Result.Reason,&Effect))return Result;
 return OKRulesTalkCommit(S,Store,Id,Transaction,Effect);
}

// The first greeting she may say to someone new (talk.rs talk_lines), as a catalogue index.
int32 OKRulesFreshGreeting(const FOKStreetSceneState& S,int32 Character){
 const auto& Meta=S.Manifest.Characters[Character];FOKStreetRelation Fresh;Fresh.Id=OKStreetStableId(Meta);Fresh.TalksRemaining=S.Manifest.TalkBudget;
 auto Request=OKRulesTalkRequest(S,TEXT("talk_lines"),Fresh,&Meta);Request->SetStringField(TEXT("category"),TEXT("greeting"));
 if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request))for(const auto& V:Out->GetArrayField(TEXT("items"))){const auto O=V->AsObject();if(!O->GetBoolField(TEXT("eligible")))continue;if(const int32* Node=S.Catalogue.ById.Find(O->GetStringField(TEXT("node"))))return *Node;}
 return INDEX_NONE;
}

// A plan block as the rules see it (plan.rs Block).
TSharedRef<FJsonObject> OKRulesBlockJson(const FOKStreetPlanBlock& B){
 auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("start"),B.Start);O->SetNumberField(TEXT("end"),B.End);O->SetStringField(TEXT("place"),B.Place);O->SetStringField(TEXT("activity"),B.Activity);
 O->SetStringField(TEXT("kind"),B.Kind);O->SetStringField(TEXT("source"),B.Source);O->SetStringField(TEXT("job_id"),B.JobId);O->SetNumberField(TEXT("budget"),B.Budget);return O;
}
FOKStreetPlanBlock OKRulesReadBlock(const FJsonObject& O){
 FOKStreetPlanBlock B;B.Start=int32(O.GetNumberField(TEXT("start")));B.End=int32(O.GetNumberField(TEXT("end")));O.TryGetStringField(TEXT("place"),B.Place);O.TryGetStringField(TEXT("activity"),B.Activity);
 O.TryGetStringField(TEXT("kind"),B.Kind);O.TryGetStringField(TEXT("source"),B.Source);O.TryGetStringField(TEXT("job_id"),B.JobId);double Budget=0;O.TryGetNumberField(TEXT("budget"),Budget);B.Budget=int32(Budget);return B;
}
// Doors of every place (x, y), for rules that measure walks (plan.rs).
TSharedRef<FJsonObject> OKRulesDoors(){
 auto O=MakeShared<FJsonObject>();
 for(const auto& P:OKStreetPlaces())O->SetArrayField(P.Id,{MakeShared<FJsonValueNumber>(P.Door.X),MakeShared<FJsonValueNumber>(P.Door.Y)});
 return O;
}

// Expected crowd 0..1 at a place and hour (places.rs crowd_at).
float OKRulesCrowdAt(const FOKStreetSceneState& S,const FString& Place,double Hours){
 auto Request=OKRulesRequest(TEXT("place_crowd"));Request->SetStringField(TEXT("place"),Place);Request->SetNumberField(TEXT("hours"),Hours);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);return Out.IsValid()?float(Out->GetNumberField(TEXT("crowd"))):0.4f;
}

// 0 ordinary .. 5 deeply dependent (bias.rs admiration_stage).
int32 OKRulesAdmirationStage(const FOKStreetSceneState& S,const FOKStreetRelation& R){
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,OKRulesRelationRequest(TEXT("admiration_stage"),R,S.MindProfiles.FindRef(R.Id)));
 return Out.IsValid()?int32(Out->GetNumberField(TEXT("admiration_stage"))):0;
}

// Money stress 0..100 and whether its worst need is a secret (mind.rs money_stress).
float OKRulesMoneyStress(const FOKStreetSceneState& S,const FOKStreetRelation& R,const TSharedPtr<FJsonObject>& Profile,bool& Secret){
 Secret=false;const TSharedPtr<FJsonObject> Out=OKRulesEval(S,OKRulesRelationRequest(TEXT("money_stress"),R,Profile));if(!Out.IsValid())return 0.f;
 Out->TryGetBoolField(TEXT("secret"),Secret);return float(Out->GetNumberField(TEXT("money_stress")));
}

// What kind of invitation an activity is (invite.rs kind_of), INDEX_NONE without the rules.
int32 OKRulesInviteKind(const FOKStreetSceneState& S,const FString& Action){
 auto Request=OKRulesRequest(TEXT("invite_kind"));Request->SetStringField(TEXT("action"),Action);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);return Out.IsValid()?int32(Out->GetNumberField(TEXT("kind"))):INDEX_NONE;
}

// A bias answer (bias.rs Bias) as the scene's struct.
FOKStreetBias OKRulesBiasFrom(const TSharedPtr<FJsonObject>& Answer){
 FOKStreetBias B;const TSharedPtr<FJsonObject>* J=nullptr;if(!Answer.IsValid()||!Answer->TryGetObjectField(TEXT("bias"),J))return B;
 const FJsonObject& O=**J;auto F=[&](const TCHAR* Name){double V=0;O.TryGetNumberField(Name,V);return float(V);};
 B.Suspicion=F(TEXT("suspicion"));B.IdealizePlayer=F(TEXT("idealize_player"));B.RivalNegativity=F(TEXT("rival_negativity"));B.DeferToPlayer=F(TEXT("defer_to_player"));
 B.Compete=F(TEXT("compete"));B.Surveil=F(TEXT("surveil"));B.PossessiveDisplay=F(TEXT("possessive_display"));B.Withdraw=F(TEXT("withdraw"));
 B.TrueLiking=F(TEXT("liking"));B.ShownAffection=F(TEXT("shown_affection"));B.ShownAdmiration=F(TEXT("shown_admiration"));B.ShownJealousy=F(TEXT("shown_jealousy"));
 B.Mood=F(TEXT("mood"));B.PeakJealousy=F(TEXT("peak_jealousy"));O.TryGetStringField(TEXT("rival"),B.Rival);O.TryGetBoolField(TEXT("masked"),B.Masked);O.TryGetStringField(TEXT("inner_line"),B.InnerLine);
 const TArray<TSharedPtr<FJsonValue>>* Combos=nullptr;if(O.TryGetArrayField(TEXT("combos"),Combos))for(const auto& C:*Combos)B.Combos.Add(C->AsString());
 return B;
}

// The university term a date belongs to and its phase (campus.rs term_at); Fees: the term whose fee window contains the
// date, when one does. Dates come back as FDateTime at midnight.
FOKStreetTerm OKRulesTermAt(const FOKStreetSceneState& S,const FDateTime& Date,EOKStreetTermPhase& Phase,FOKStreetTerm* Fees=nullptr,bool* bFees=nullptr,FOKStreetTerm* Next=nullptr){
 FOKStreetTerm T;Phase=EOKStreetTermPhase::Break;if(bFees)*bFees=false;
 auto Request=OKRulesRequest(TEXT("campus_term"));Request->SetArrayField(TEXT("date"),{MakeShared<FJsonValueNumber>(Date.GetYear()),MakeShared<FJsonValueNumber>(Date.GetMonth()),MakeShared<FJsonValueNumber>(Date.GetDay())});
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return T;
 auto Read=[](const TSharedPtr<FJsonObject>& O){FOKStreetTerm X;if(!O.IsValid())return X;X.Key=O->GetStringField(TEXT("key"));
  auto D=[&](const TCHAR* Name){const auto& A=O->GetArrayField(Name);return A.Num()==3?FDateTime(int32(A[0]->AsNumber()),int32(A[1]->AsNumber()),int32(A[2]->AsNumber())):FDateTime();};
  X.Start=D(TEXT("start"));X.Midterm=D(TEXT("midterm"));X.Final=D(TEXT("final"));X.End=D(TEXT("end"));return X;};
 T=Read(Out->GetObjectField(TEXT("term")));const FString P=Out->GetStringField(TEXT("phase"));
 Phase=P==TEXT("classes")?EOKStreetTermPhase::Classes:P==TEXT("midterm")?EOKStreetTermPhase::Midterm:P==TEXT("final")?EOKStreetTermPhase::Final:EOKStreetTermPhase::Break;
 const TSharedPtr<FJsonObject>* F=nullptr;if(Out->TryGetObjectField(TEXT("fees_term"),F)){if(Fees)*Fees=Read(*F);if(bFees)*bFees=true;}
 const TSharedPtr<FJsonObject>* N=nullptr;if(Next&&Out->TryGetObjectField(TEXT("next_term"),N))*Next=Read(*N);
 return T;
}

// His bank's numbers for the bank app (bank.rs bank_rates): today's rates, deposit terms, fees, limits.
TSharedPtr<FJsonObject> OKRulesBankInfo(const FOKStreetSceneState& S,int32 Day){
 auto Request=OKRulesRequest(TEXT("bank_rates"));Request->SetNumberField(TEXT("day"),Day);return OKRulesEval(S,Request);
}

// The banking part of a rules' answer written back (its codec is the save's), with his academic record's fee fields.
void OKRulesReadBank(FOKStreetStore& C,const FJsonObject& Answer){
 const TSharedPtr<FJsonObject>* B=nullptr;if(Answer.TryGetObjectField(TEXT("banking"),B)){C.Banking=FOKStreetBanking();OKStreetReadBanking(*B,C.Banking);}
 const TSharedPtr<FJsonObject>* A=nullptr;
 if(Answer.TryGetObjectField(TEXT("player_academic"),A)){(*A)->TryGetStringField(TEXT("status"),C.PlayerAcademic.Status);(*A)->TryGetStringField(TEXT("leave_until"),C.PlayerAcademic.LeaveUntil);(*A)->TryGetStringField(TEXT("fees_paid"),C.PlayerAcademic.FeesPaidTerm);}
 const TSharedPtr<FJsonObject>* H=nullptr;
 if(Answer.TryGetObjectField(TEXT("academics"),H))for(const auto& P:(*H)->Values)if(auto* X=C.Academics.Find(FString(*P.Key)))P.Value->AsObject()->TryGetStringField(TEXT("fees_paid"),X->FeesPaidTerm);
}

// An entrusted sum as the rules see it (venture.rs Entrust), and back.
TSharedRef<FJsonObject> OKRulesEntrustJson(const FOKStreetEntrust& E){
 auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("id"),E.Id);O->SetStringField(TEXT("heroine"),E.Heroine);O->SetStringField(TEXT("kind"),E.Kind);
 O->SetNumberField(TEXT("principal"),double(E.Principal));O->SetNumberField(TEXT("returned"),double(E.Returned));O->SetNumberField(TEXT("day"),E.Day);O->SetNumberField(TEXT("due"),E.Due);
 O->SetStringField(TEXT("status"),E.Status);O->SetStringField(TEXT("split"),E.Split);O->SetBoolField(TEXT("overdue"),E.Overdue);O->SetBoolField(TEXT("jealous"),E.Jealous);
 return O;
}
FOKStreetEntrust OKRulesReadEntrust(const FJsonObject& O){
 FOKStreetEntrust E;E.Id=int32(O.GetNumberField(TEXT("id")));E.Heroine=O.GetStringField(TEXT("heroine"));E.Kind=O.GetStringField(TEXT("kind"));
 E.Principal=int64(O.GetNumberField(TEXT("principal")));E.Returned=int64(O.GetNumberField(TEXT("returned")));E.Day=int32(O.GetNumberField(TEXT("day")));E.Due=int32(O.GetNumberField(TEXT("due")));
 E.Status=O.GetStringField(TEXT("status"));E.Split=O.GetStringField(TEXT("split"));E.Overdue=O.GetBoolField(TEXT("overdue"));E.Jealous=O.GetBoolField(TEXT("jealous"));
 return E;
}

// What a payment of pay leaves in hand after 五險一金 and tax (property.rs net_pay), for a heroine's shift.
int64 OKRulesNetPay(const FOKStreetSceneState& S,int64 Gross,float Months){
 auto Request=OKRulesRequest(TEXT("net_pay"));Request->SetNumberField(TEXT("gross"),double(Gross));Request->SetNumberField(TEXT("months"),Months);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);return Out.IsValid()?int64(Out->GetNumberField(TEXT("net"))):Gross;
}

// Academic records both ways (academics.rs Academic = OKStreetAcademicJson).
TSharedRef<FJsonValue> OKRulesAcademicValue(const FOKStreetAcademic* A){return A?TSharedRef<FJsonValue>(MakeShared<FJsonValueObject>(OKStreetAcademicJson(*A))):TSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());}

// A rumour both ways (social.rs Rumor = the save's rumour row).
TSharedRef<FJsonObject> OKRulesRumorJson(const FOKStreetRumor& R){
 auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("id"),R.Id);O->SetStringField(TEXT("text"),R.Text);O->SetStringField(TEXT("about"),R.About);O->SetStringField(TEXT("kind"),R.Kind);
 O->SetNumberField(TEXT("heat"),R.Heat);O->SetNumberField(TEXT("confidence"),R.Confidence);O->SetNumberField(TEXT("day"),R.Day);
 TArray<TSharedPtr<FJsonValue>> Known;for(const FString& K:R.KnownBy)Known.Add(MakeShared<FJsonValueString>(K));O->SetArrayField(TEXT("known_by"),Known);
 auto Heard=MakeShared<FJsonObject>();for(const auto& P:R.Heard)Heard->SetNumberField(P.Key,P.Value);O->SetObjectField(TEXT("heard"),Heard);return O;
}
void OKRulesReadRumor(const FJsonObject& O,FOKStreetRumor& R){
 double V=0;if(O.TryGetNumberField(TEXT("id"),V))R.Id=int32(V);O.TryGetStringField(TEXT("text"),R.Text);O.TryGetStringField(TEXT("about"),R.About);O.TryGetStringField(TEXT("kind"),R.Kind);
 if(O.TryGetNumberField(TEXT("heat"),V))R.Heat=float(V);if(O.TryGetNumberField(TEXT("confidence"),V))R.Confidence=float(V);if(O.TryGetNumberField(TEXT("day"),V))R.Day=int32(V);
 const TArray<TSharedPtr<FJsonValue>>* Known=nullptr;if(O.TryGetArrayField(TEXT("known_by"),Known)){R.KnownBy.Reset();for(const auto& K:*Known)R.KnownBy.Add(K->AsString());}
 const TSharedPtr<FJsonObject>* Heard=nullptr;if(O.TryGetObjectField(TEXT("heard"),Heard)){R.Heard.Reset();for(const auto& P:(*Heard)->Values)R.Heard.Add(FString(*P.Key),float(P.Value->AsNumber()));}
}

// Defaults the scene keeps at hand (start favor, tick periods), fetched again whenever the rules were opened or reloaded.
void OKRulesReadTables(FOKStreetSceneState& S);
// The save's ext (the rules' own state: her things, loans, photos) into a request, with today; and back from an answer,
// with what goes into or out of his bag (his_add, his_remove).
void OKRulesPutExt(const FOKStreetSceneState& S,const TSharedRef<FJsonObject>& Request){
 TSharedPtr<FJsonObject> E;if(S.Store.Ext.IsEmpty()||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S.Store.Ext),E)||!E.IsValid())E=MakeShared<FJsonObject>();
 Request->SetObjectField(TEXT("ext"),E);Request->SetNumberField(TEXT("day"),S.Store.CalendarDay);
}
void OKRulesTakeExt(FOKStreetStore& Store,const FJsonObject& Answer){
 const TSharedPtr<FJsonObject>* E=nullptr;if(Answer.TryGetObjectField(TEXT("ext"),E)&&E->IsValid()){FString Out;auto W=TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);FJsonSerializer::Serialize(E->ToSharedRef(),W);Store.Ext=Out;}
 const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
 if(Answer.TryGetArrayField(TEXT("his_add"),Items))for(const auto& V:*Items)Store.Inventory.Add(V->AsString());
 if(Answer.TryGetArrayField(TEXT("his_remove"),Items))for(const auto& V:*Items)Store.Inventory.RemoveSingle(V->AsString());
}
void OKRulesSyncDefaults(FOKStreetSceneState& S){
 if(!S.Rules||S.RulesDefaultsGeneration==S.Rules->Generation())return;
 S.RulesDefaultsGeneration=S.Rules->Generation();
 if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,OKRulesRequest(TEXT("bonds_defaults")))){
  S.BondStartFavor=float(Out->GetNumberField(TEXT("start_favor")));S.BondTickMinutes=FMath::Max<int64>(1,int64(Out->GetNumberField(TEXT("tick_minutes"))));
 }
 if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,OKRulesRequest(TEXT("needs_defaults"))))S.NeedsTickMinutes=FMath::Max<int64>(1,int64(Out->GetNumberField(TEXT("tick_minutes"))));
 // The dialogue catalogues (talk.rs): rules.json beside the dialogue catalogue, the collected topics. The save records the
 // step-up it was played with; favors loaded from an older save are kept inside the caps.
 S.FavorLimitCache.Reset();
 if(!S.Manifest.DialogueCatalogue.IsEmpty()){
  auto Request=OKRulesRequest(TEXT("talk_load"));Request->SetStringField(TEXT("catalogue"),FPaths::ConvertRelativePathToFull(S.Manifest.DialogueCatalogue));Request->SetStringField(TEXT("topics"),S.TopicCataloguePath.IsEmpty()?FString():FPaths::ConvertRelativePathToFull(S.TopicCataloguePath));
  if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request)){
   const TArray<TSharedPtr<FJsonValue>>* Step=nullptr;
   if(Out->TryGetArrayField(TEXT("step_up"),Step)&&Step->Num()==3){S.Store.FavorCapIsKksStepUp=true;S.Store.StepUpStranger=int32((*Step)[0]->AsNumber());S.Store.StepUpFriend=int32((*Step)[1]->AsNumber());S.Store.StepUpLover=int32((*Step)[2]->AsNumber());double Beyond=-1;S.Store.StepUpBeyond=Out->TryGetNumberField(TEXT("beyond"),Beyond)?int32(Beyond):INDEX_NONE;}
   auto Clamp=OKRulesRequest(TEXT("favor_clamp"));TArray<TSharedPtr<FJsonValue>> Rows;for(const auto& R:S.Store.Relations)Rows.Add(MakeShared<FJsonValueObject>(OKRulesRelationJson(R)));Clamp->SetArrayField(TEXT("relations"),Rows);
   if(const TSharedPtr<FJsonObject> C=OKRulesEval(S,Clamp))for(const auto& V:C->GetArrayField(TEXT("changes"))){const auto O=V->AsObject();for(auto& R:S.Store.Relations)if(R.Id==O->GetStringField(TEXT("id")))R.Favor=float(O->GetNumberField(TEXT("favor")));}
   UE_LOG(LogTemp,Display,TEXT("Street rules: dialogue catalogue %d lines, %d branches, %d topics"),int32(Out->GetNumberField(TEXT("nodes"))),int32(Out->GetNumberField(TEXT("branches"))),int32(Out->GetNumberField(TEXT("topics"))));
  }else{S.Errors.Add(TEXT("Dialogue rules rejected (ok_rules talk_load)"));}
 }
 OKRulesReadTables(S);
}

// The tables the scene shows on its own (campus, place norms, her panel's interactions): read with the rules and again
// when the language changes (their names are in it).
void OKRulesReadTables(FOKStreetSceneState& S){
 // The activities together (activities.rs): where and how each is shown; the rules decide whether one may start.
 if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,OKRulesRequest(TEXT("activities_table")))){
  auto& All=OKStreetActivitiesMutable();All.Reset();
  auto Kind=[](const FString& K){using E=EOKStreetActivityKind;return K==TEXT("photo")?E::Photo:K==TEXT("facility")?E::Facility:K==TEXT("walk")?E::Walk:K==TEXT("restroom")?E::Restroom:K==TEXT("sleep")?E::Sleep:K==TEXT("solo")?E::Solo:E::Pair;};
  for(const auto& V:Out->GetArrayField(TEXT("activities"))){const auto O=V->AsObject();
   auto Text=MakeUnique<FOKStreetActivityText>();Text->Id=O->GetStringField(TEXT("id"));Text->Name=O->GetStringField(TEXT("name"));Text->ZhName=O->GetStringField(TEXT("own_name"));
   Text->Place=O->GetStringField(TEXT("place"));Text->Anchor=O->GetStringField(TEXT("anchor"));Text->FemaleClip=O->GetStringField(TEXT("female_clip"));Text->MaleClip=O->GetStringField(TEXT("male_clip"));O->TryGetStringField(TEXT("effect"),Text->Effect);
   FOKStreetActivity A;A.Id=*Text->Id;A.Name=*Text->Name;A.ZhName=*Text->ZhName;A.Place=*Text->Place;A.Kind=Kind(O->GetStringField(TEXT("kind")));A.Code=int32(O->GetNumberField(TEXT("code")));
   // Absent is a null pointer, as the code reading them expects (no clip: the pose's own).
   A.Anchor=Text->Anchor.IsEmpty()?nullptr:*Text->Anchor;A.FemaleClip=Text->FemaleClip.IsEmpty()?nullptr:*Text->FemaleClip;A.MaleClip=Text->MaleClip.IsEmpty()?nullptr:*Text->MaleClip;A.Effect=Text->Effect.IsEmpty()?nullptr:*Text->Effect;{double Turn=0;if(O->TryGetNumberField(TEXT("turn"),Turn))A.Turn=float(Turn);}
   All.Add(A);OKStreetActivityTexts().Add(MoveTemp(Text));}
  UE_LOG(LogTemp,Display,TEXT("Street activities: %d from the rules"),All.Num());}
 // Campus tables for the displays (academics.rs).
 if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,OKRulesRequest(TEXT("campus_tables")))){
  auto& Colleges=OKStreetCollegesTable();Colleges.Reset();
  for(const auto& V:Out->GetArrayField(TEXT("colleges"))){const auto O=V->AsObject();FOKStreetCollege C;C.Id=O->GetStringField(TEXT("id"));C.Name=O->GetStringField(TEXT("name"));C.Building=O->GetStringField(TEXT("building"));C.Code=O->GetStringField(TEXT("code"));C.BuildingName=O->GetStringField(TEXT("building_name"));Colleges.Add(C);}
  auto& Depts=OKStreetDeptsTable();Depts.Reset();
  for(const auto& V:Out->GetArrayField(TEXT("depts"))){const auto O=V->AsObject();FOKStreetDept D;D.Id=O->GetStringField(TEXT("id"));D.Name=O->GetStringField(TEXT("name"));D.College=O->GetStringField(TEXT("college"));D.Bar=int32(O->GetNumberField(TEXT("bar")));Depts.Add(D);}
 }
 // Tables the scene reads on its own: the place norms (its displays, simple checks) and her panel's interactions.
 if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,OKRulesRequest(TEXT("places_table")))){
  auto Norms=[](const TSharedPtr<FJsonObject>& O){FOKStreetPlaceNorms N;if(!O.IsValid())return N;double V=0;
   if(O->TryGetNumberField(TEXT("privacy"),V))N.Privacy=int32(V);if(O->TryGetNumberField(TEXT("quiet"),V))N.Quiet=float(V);if(O->TryGetNumberField(TEXT("formality"),V))N.Formality=float(V);
   O->TryGetBoolField(TEXT("food"),N.FoodAllowed);O->TryGetBoolField(TEXT("staff"),N.Staff);O->TryGetStringField(TEXT("label"),N.Label);return N;};
  auto& T=OKStreetNormsTable();T.Places.Reset();T.Default=Norms(Out->GetObjectField(TEXT("default")));
  for(const auto& P:Out->GetObjectField(TEXT("norms"))->Values)T.Places.Add(FString(*P.Key),Norms(P.Value->AsObject()));
 }
 if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,OKRulesRequest(TEXT("heroine_acts")))){
  auto& Acts=OKHeroineActs();Acts.Reset();
  for(const auto& V:Out->GetArrayField(TEXT("acts"))){const TSharedPtr<FJsonObject> O=V->AsObject();if(!O)continue;FOKHeroineAct A;
   A.Id=O->GetStringField(TEXT("id"));A.Cat=O->GetStringField(TEXT("cat"));A.Name=O->GetStringField(TEXT("name"));A.Button=O->GetStringField(TEXT("button"));
   A.Stage=int32(O->GetNumberField(TEXT("stage")));A.FaceToFace=O->GetBoolField(TEXT("face_to_face"));Acts.Add(A);}
 }
}
}
