// Included by OKStreetSocialScene.cpp: plan §16 P12 full Inspector. `ok.Street.Inspect` writes the whole simulated
// state (calendar, wallet, finance, company, every heroine's axes, needs, plan and money) to
// Saved/StreetSocial/inspect.json and logs a short summary; `ok.Street.Inspect <name>` logs one heroine in detail.

FString AOKStreetSocialScene::InspectJson(const FString& Only){
 auto& S=*State;auto Root=MakeShared<FJsonObject>();FString Error;
 const int64 Now=OKStreetMindNow(S);
 Root->SetStringField(TEXT("calendar"),OKStreetCalendarLabel(S.Store.CalendarDay,double(Now%1440)/60.0));
 Root->SetNumberField(TEXT("day"),S.Store.CalendarDay);
 Root->SetNumberField(TEXT("cash"),double(S.Store.PlayerCash));Root->SetNumberField(TEXT("bank"),double(S.Store.PlayerBank));
 Root->SetStringField(TEXT("drama_mode"),FString::Chr(S.DramaMode));Root->SetNumberField(TEXT("look"),PlayerLook());
 if(S.Mind){FString Status;if(S.Mind->Query(TEXT("{\"kind\":\"status\"}"),Status,Error)){TSharedPtr<FJsonObject> O;if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Status),O)&&O)Root->SetObjectField(TEXT("mind"),O);}
  if(const auto F=Fin(TEXT("summary"),nullptr,Error))Root->SetObjectField(TEXT("finance"),F);}
 {auto C=MakeShared<FJsonObject>();const auto& Co=S.Store.Company;C->SetStringField(TEXT("name"),Co.Name);C->SetNumberField(TEXT("founded"),Co.Founded);C->SetNumberField(TEXT("staff"),Co.Staff.Num());C->SetNumberField(TEXT("last_profit"),double(Co.LastProfit));Root->SetObjectField(TEXT("company"),C);}
 {auto Ac=MakeShared<FJsonObject>();Ac->SetObjectField(TEXT("player"),OKStreetAcademicJson(S.Store.PlayerAcademic));auto Hs=MakeShared<FJsonObject>();for(const auto& P:S.Store.Academics)Hs->SetObjectField(P.Key,OKStreetAcademicJson(P.Value));Ac->SetObjectField(TEXT("heroines"),Hs);
  EOKStreetTermPhase Phase;const FOKStreetTerm Term=OKRulesTermAt(S,StreetNow(),Phase);Ac->SetStringField(TEXT("term"),Term.Key);Ac->SetNumberField(TEXT("phase"),int32(Phase));Root->SetObjectField(TEXT("academics"),Ac);}
 TArray<TSharedPtr<FJsonValue>> Entrusts;for(const auto& E:S.Store.Entrusts){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("heroine"),E.Heroine);O->SetStringField(TEXT("kind"),E.Kind);O->SetNumberField(TEXT("principal"),double(E.Principal));O->SetStringField(TEXT("status"),E.Status);O->SetNumberField(TEXT("due"),E.Due);O->SetBoolField(TEXT("overdue"),E.Overdue);Entrusts.Add(MakeShared<FJsonValueObject>(O));}
 Root->SetArrayField(TEXT("entrusts"),Entrusts);
 TArray<TSharedPtr<FJsonValue>> Heroines;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I){
  if(!Only.IsEmpty()&&!S.Manifest.Characters[I].Name.Contains(Only))continue;
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
  auto H=MakeShared<FJsonObject>();H->SetStringField(TEXT("name"),S.Manifest.Characters[I].Name);H->SetStringField(TEXT("id"),Id);H->SetStringField(TEXT("stage"),R.Stage);
  auto Axes=MakeShared<FJsonObject>();for(const auto& A:OKStreetAxisFields(R))Axes->SetNumberField(A.Key,*A.Value);H->SetObjectField(TEXT("axes"),Axes);
  H->SetNumberField(TEXT("admiration_stage"),OKRulesAdmirationStage(S,R));H->SetNumberField(TEXT("cash"),double(R.Cash));H->SetNumberField(TEXT("treat_balance"),R.TreatBalance);
  bool Secret=false;H->SetNumberField(TEXT("money_stress"),MoneyStressOf(R,ProfileOf(I),Secret));
  auto Needs=MakeShared<FJsonObject>();Needs->SetNumberField(TEXT("satiety"),R.Satiety);Needs->SetNumberField(TEXT("energy"),R.Energy);Needs->SetNumberField(TEXT("fun"),R.Fun);Needs->SetNumberField(TEXT("social"),R.Social);Needs->SetNumberField(TEXT("hygiene"),R.Hygiene);H->SetObjectField(TEXT("needs"),Needs);
  H->SetStringField(TEXT("emotion"),R.Emotion);H->SetNumberField(TEXT("mood"),BiasOf(I).Mood);
  TArray<TSharedPtr<FJsonValue>> Flags;for(const FString& F:R.Flags)Flags.Add(MakeShared<FJsonValueString>(F));H->SetArrayField(TEXT("flags"),Flags);
  if(const auto* B=BlockNow(I,int32(Now%1440)))H->SetStringField(TEXT("now"),OKMsg(TEXT("ui.inspector.inspector_label"), {{TEXT("kind"), *B->Kind}, {TEXT("place"), *B->Place}, {TEXT("source"), *B->Source}}));
  H->SetStringField(TEXT("plan"),PlanText(I));H->SetStringField(TEXT("heard"),HeardText(I));H->SetStringField(TEXT("today"),TodayText(I));
  if(S.Agents.IsValidIndex(I-1))H->SetNumberField(TEXT("distance_m"),FVector::Dist2D(S.Agents[I-1].Position,S.Male)/100.0);
  Heroines.Add(MakeShared<FJsonValueObject>(H));
 }
 Root->SetArrayField(TEXT("heroines"),Heroines);
 FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));return Text;
}

static void OKStreetInspectCommand(const TArray<FString>& Args,UWorld* World){
 if(!World)return;
 for(TActorIterator<AOKStreetSocialScene> It(World);It;++It){
  const FString Text=It->InspectJson(Args.Num()?Args[0]:FString());
  const FString Path=FPaths::ProjectSavedDir()/TEXT("StreetSocial/inspect.json");
  FFileHelper::SaveStringToFile(Text,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  UE_LOG(LogTemp,Display,TEXT("Street inspect written to %s (%d chars)"),*Path,Text.Len());
  if(Args.Num())UE_LOG(LogTemp,Display,TEXT("%s"),*Text.Left(4000));
  return;
 }
 UE_LOG(LogTemp,Warning,TEXT("ok.Street.Inspect: no StreetSocial scene in this world"));
}
static FAutoConsoleCommandWithWorldAndArgs GOKStreetInspect(TEXT("ok.Street.Inspect"),TEXT("Write the StreetSocial simulation state to Saved/StreetSocial/inspect.json; optional heroine name logs her"),FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OKStreetInspectCommand));
