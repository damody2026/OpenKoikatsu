// Included by OKStreetSocialScene.cpp after the rules glue: the language and the region on screen
// (Docs/STREET_I18N_PLAN_20261014.md). The words come from the rules' text tables (ok-rules locale.rs,
// StreetSocialData/Localization/<lang>/ui.toml), cached here and re-read after a rules reload or a language change.
// The language is the player's (Saved/StreetSocial/player.json "locale"); the region belongs to the save (Store.Region).

// A word of the scene by key ("ui.bluff.pile"), an ICU message with these arguments (formatted by the rules, as OKMsg).
FString AOKStreetSocialScene::T(const TCHAR* Key,const FStringFormatNamedArguments& Args) const{
 auto Map=MakeShared<FJsonObject>();for(const auto& A:Args)switch(A.Value.Type){
  case FStringFormatArg::Int:Map->SetNumberField(A.Key,double(A.Value.IntValue));break;case FStringFormatArg::UInt:Map->SetNumberField(A.Key,double(A.Value.UIntValue));break;
  case FStringFormatArg::Double:Map->SetNumberField(A.Key,A.Value.DoubleValue);break;
  case FStringFormatArg::StringLiteralWIDE:Map->SetStringField(A.Key,FString(A.Value.StringLiteralWIDEValue));break;
  case FStringFormatArg::StringLiteralANSI:Map->SetStringField(A.Key,FString(A.Value.StringLiteralANSIValue));break;
  default:Map->SetStringField(A.Key,A.Value.StringValue);}
 FString Out;return OKStreetRulesText(Key,Map,Out)?Out:FString(Key);
}

// The region a language plays in unless the player picks another (locale.rs home_region).
FString AOKStreetSocialScene::HomeRegion(const FString& Lang) const{
 if(!State)return TEXT("TW");auto Request=OKRulesRequest(TEXT("locale_info"));Request->SetStringField(TEXT("lang"),Lang);
 FString Out=TEXT("TW");if(const TSharedPtr<FJsonObject> A=OKRulesEval(*State,Request))A->TryGetStringField(TEXT("home_region"),Out);return Out;
}

// An amount of the region's money in its smallest unit: "NT$30", "¥5", "$1.99" (the rules' currency format).
FString AOKStreetSocialScene::Money(int64 Amount) const{return OKStreetMoneyText(Amount);}

void AOKStreetSocialScene::LoadUiTexts(){
 auto& S=*State;GOKStreetLang=S.Lang;GOKStreetRegion=S.Store.Region;
 // The code's own messages (OKT / OKMsg, OKStreetSocialText.h) are asked from the rules in this language and region.
 OKStreetUseCppLanguage(S.Lang,S.Store.Region);
 // The catalog's prices and wages in this region (the rules convert them: shelf prices snapped to its usual prices).
 S.LocalAmounts.Reset();
 if(const TSharedPtr<FJsonObject> Info=OKRulesEval(S,OKRulesRequest(TEXT("region_info")))){
  const TSharedPtr<FJsonObject> Items=Info->GetObjectField(TEXT("items")),Jobs=Info->GetObjectField(TEXT("jobs"));
  for(auto& I:S.Catalog.Items)if(const TSharedPtr<FJsonObject>* P=nullptr;Items->TryGetObjectField(I.Id,P))for(auto& KV:I.Price){double V=0;if((*P)->TryGetNumberField(KV.Key,V))KV.Value=int32(V);}
  for(auto& J:S.Catalog.Jobs){double V=0;if(Jobs->TryGetNumberField(J.Id,V))J.Wage=int32(V);}}
 if(const TSharedPtr<FJsonObject> Info=OKRulesEval(S,OKRulesRequest(TEXT("locale_info")))){
  const auto C=Info->GetObjectField(TEXT("currency"));S.MoneyDigits=int32(C->GetNumberField(TEXT("digits")));
  UE_LOG(LogTemp,Display,TEXT("Street locale: %s, region %s (%s), %d not translated"),*S.Lang,*S.Store.Region,*C->GetStringField(TEXT("code")),int32(Info->GetNumberField(TEXT("missing"))));}
 // The amount boxes start at an amount usual in this region (typed in its major unit: dollars, not cents).
 S.BankAmount=MoneyBox(Local(TEXT("cash"),1000));S.BetStake=MoneyBox(Local(TEXT("cash"),20));S.FinTransfer=MoneyBox(Local(TEXT("cash"),2000));S.VentureAmount=MoneyBox(Local(TEXT("cash"),5000));
 {// The original lines' subtitles in this language (absent: the lines' own Chinese).
  auto& Subtitles=OKStreetSubtitles();Subtitles.Reset();FString Body;TSharedPtr<FJsonObject> Table;
  const FString File=FPaths::GetPath(FOKStreetRules::TuningDir())/TEXT("Localization")/S.Lang/TEXT("dialogue.json");
  if(FFileHelper::LoadFileToString(Body,*File)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),Table)&&Table.IsValid())
   for(const auto& P:Table->Values)Subtitles.Add(FString(*P.Key),P.Value->AsString());
  const int32 Filled=FillSubtitleScripts();
  UE_LOG(LogTemp,Display,TEXT("Street subtitles: %d lines for %s (%d in its script from the lines' own Chinese)"),Subtitles.Num(),*S.Lang,Filled);}
 // The data's names in this language (names.toml: ui.name.<kind>.<id>), the data's own where there is none: every
 // display shows them. The property market comes from the rules, in this region's money and this language.
 {auto Named=[](const FString& Key,const TCHAR* Own)->const TCHAR*{const TCHAR* N=OKT(*Key);return FCString::Strcmp(N,*Key)==0?Own:N;};
  for(const FOKStreetPlace& Const:OKStreetPlaces()){auto& P=const_cast<FOKStreetPlace&>(Const);if(!P.ZhName){P.ZhName=P.Name;P.ZhAbout=P.About;}
   P.Name=Named(FString(TEXT("ui.name.place."))+P.Id,Named(FString(TEXT("ui.name.unit."))+P.Id,P.ZhName)); /* a lot: named like its property unit */P.About=Named(FString(TEXT("ui.name.place_about."))+P.Id,FString(P.Id).StartsWith(TEXT("lot_"))?Named(TEXT("ui.name.place_about.lot"),P.ZhAbout):P.ZhAbout);}
  // The activities' names come with their table in this language (OKRulesReadTables, below).
  for(auto& I:S.Catalog.Items){if(I.ZhName.IsEmpty())I.ZhName=I.Name;I.Name=Named(TEXT("ui.name.item.")+I.Id,*I.ZhName);}
  for(auto& J:S.Catalog.Jobs){if(J.ZhTitle.IsEmpty())J.ZhTitle=J.Title;J.Title=Named(TEXT("ui.name.job.")+J.Id,*J.ZhTitle);}
  NameCollectedTopics();
  {// Elevator stops (the models' "liftK:2F 客房"): their own Chinese in this language's script (locale.rs script_texts).
   auto& Labels=OKStreetLiftLabelTexts();Labels.Reset();TArray<FString> Own;TArray<TSharedPtr<FJsonValue>> Texts;
   for(const FOKStreetPlace& P:OKStreetPlaces())for(const FOKStreetModelAnchor* Lift:OKStreetPlaceLifts(P)){const FString L=OKStreetLiftLabel(*Lift);if(!Own.Contains(L)){Own.Add(L);Texts.Add(MakeShared<FJsonValueString>(L));}}
   auto Request=OKRulesRequest(TEXT("script_texts"));Request->SetArrayField(TEXT("texts"),Texts);
   if(S.Lang!=TEXT("zh-TW")&&!Texts.IsEmpty())if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request)){const auto& Out=Answer->GetArrayField(TEXT("texts"));for(int32 I=0;I<Out.Num()&&I<Own.Num();++I)Labels.Add(Own[I],Out[I]->AsString());}}
  if(const TSharedPtr<FJsonObject> Property=OKRulesEval(S,OKRulesRequest(TEXT("property_data"))))OKStreetSetProperty(Property);OKRulesReadTables(S);}
}

// The collected topics' names (牽牛花): a name of this language by number (ui.name.topic.<no>), else their own Chinese in
// its script (locale.rs script_texts). Run when the language changes and once the topics are loaded.
void AOKStreetSocialScene::NameCollectedTopics(){
 auto& S=*State;if(!S.Rules)return;
 TArray<FOKStreetCollectedTopic*> Plain;TArray<TSharedPtr<FJsonValue>> Texts;
 for(auto& T:S.CollectedTopics.Topics){if(T.ZhName.IsEmpty())T.ZhName=T.Name;const FString Key=FString::Printf(TEXT("ui.name.topic.%d"),T.No);const TCHAR* N=OKT(*Key);
  if(FCString::Strcmp(N,*Key)!=0)T.Name=N;else{T.Name=T.ZhName;Plain.Add(&T);Texts.Add(MakeShared<FJsonValueString>(T.ZhName));}}
 if(!Texts.IsEmpty()){auto Request=OKRulesRequest(TEXT("script_texts"));Request->SetArrayField(TEXT("texts"),Texts);
    if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request)){const auto& Out=Answer->GetArrayField(TEXT("texts"));for(int32 I=0;I<Out.Num()&&I<Plain.Num();++I)Plain[I]->Name=Out[I]->AsString();}}
}

// An amount typed in a box, in the region's major unit ("1.99" dollars), as kept (199 cents); and back for a box.
int64 AOKStreetSocialScene::MoneyIn(const FString& Text) const{
 const int32 Digits=State?State->MoneyDigits:0;return int64(FMath::RoundToDouble(FCString::Atod(*Text.Replace(TEXT(","),TEXT("")))*FMath::Pow(10.0,Digits)));
}
FString AOKStreetSocialScene::MoneyBox(int64 Amount) const{
 const int32 Digits=State?State->MoneyDigits:0;if(Digits<=0)return FString::Printf(TEXT("%lld"),Amount);
 const int64 Unit=Digits==2?100:10;return Amount%Unit==0?FString::Printf(TEXT("%lld"),Amount/Unit):FString::Printf(TEXT("%.*f"),Digits,double(Amount)/double(Unit));
}

// An amount of the scene's own (start cash, a default amount box, a ticket price...) in the save's region.
int64 AOKStreetSocialScene::Local(const TCHAR* Kind,int64 Base) const{
 if(!State||State->Store.Region==TEXT("CN")||Base==0)return Base; // the base region: the game's own numbers
 auto& S=*State;const FString Key=FString::Printf(TEXT("%s|%s|%lld"),*S.Store.Region,Kind,Base);
 if(const int64* Hit=S.LocalAmounts.Find(Key))return *Hit;
 auto Request=OKRulesRequest(TEXT("region_amount"));auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("kind"),Kind);Item->SetNumberField(TEXT("base"),double(Base));
 Request->SetArrayField(TEXT("items"),{MakeShared<FJsonValueObject>(Item)});
 int64 Out=Base;if(const TSharedPtr<FJsonObject> A=OKRulesEval(S,Request)){const auto& L=A->GetArrayField(TEXT("amounts"));if(L.Num())Out=int64(L[0]->AsNumber());}
 S.LocalAmounts.Add(Key,Out);return Out;
}

// Finance (ok-mind) keeps a yuan wallet: its wallet amounts cross to the region's money and back (rules: fin_wallet).
TSharedPtr<FJsonObject> AOKStreetSocialScene::FinWallet(const TCHAR* Dir,const FString& Op,const TSharedPtr<FJsonObject>& Value) const{
 if(!State||!Value.IsValid()||State->Store.Region==TEXT("CN"))return Value;
 auto Request=OKRulesRequest(TEXT("fin_wallet"));Request->SetStringField(TEXT("dir"),Dir);Request->SetStringField(TEXT("op"),Op);Request->SetObjectField(TEXT("value"),Value);
 if(const TSharedPtr<FJsonObject> A=OKRulesEval(*State,Request)){const TSharedPtr<FJsonObject>* V=nullptr;if(A->TryGetObjectField(TEXT("value"),V))return *V;}
 return Value;
}

// The save moved to another region: the rules convert every amount in it (save_convert), it is read back and kept.
// Lines the subtitle table has none for (confession lines, topics, lines added later): their own Chinese in this
// language's script (locale.rs script_texts), so a Simplified game shows no Traditional line. Run again whenever lines
// join the catalogue (the confession lines load after the texts). Answers how many it added.
int32 AOKStreetSocialScene::FillSubtitleScripts(){
 auto& S=*State;auto& Subtitles=OKStreetSubtitles();int32 Filled=0;
 if(S.Lang!=TEXT("zh-CN")||S.Catalogue.Nodes.IsEmpty()||!S.Rules)return 0;
 TArray<FString> Keys;TArray<TSharedPtr<FJsonValue>> Texts;
 for(const auto& N:S.Catalogue.Nodes)if(!N.TextZh.IsEmpty()){const FString Key=OKStreetSubtitleKey(N.TextJp,N.TextZh);if(!Subtitles.Contains(Key)&&!Keys.Contains(Key)){Keys.Add(Key);Texts.Add(MakeShared<FJsonValueString>(N.TextZh));}}
 if(Texts.IsEmpty())return 0;
 auto Request=OKRulesRequest(TEXT("script_texts"));Request->SetArrayField(TEXT("texts"),Texts);
 if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request)){const auto& Out=Answer->GetArrayField(TEXT("texts"));for(int32 I=0;I<Out.Num()&&I<Keys.Num();++I){Subtitles.Add(Keys[I],Out[I]->AsString());++Filled;}}
 return Filled;
}

void AOKStreetSocialScene::ChangeRegion(const FString& To){
 auto& S=*State;if(To==S.Store.Region||!(To==TEXT("TW")||To==TEXT("CN")||To==TEXT("US")))return;
 TSharedPtr<FJsonObject> Save;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OKStreetWriteSave(S.Store)),Save)||!Save.IsValid())return;
 auto Request=OKRulesRequest(TEXT("save_convert"));Request->SetStringField(TEXT("from"),S.Store.Region);Request->SetStringField(TEXT("to"),To);Request->SetObjectField(TEXT("save"),Save);
 const TSharedPtr<FJsonObject> A=OKRulesEval(S,Request);const TSharedPtr<FJsonObject>* Converted=nullptr;
 if(!A||!A->TryGetObjectField(TEXT("save"),Converted)){UE_LOG(LogTemp,Warning,TEXT("Street region: %s -> %s refused"),*S.Store.Region,*To);return;}
 FString Json;FJsonSerializer::Serialize(Converted->ToSharedRef(),TJsonWriterFactory<>::Create(&Json));
 FOKStreetStore Candidate;FString Error;if(OKStreetParseSave(Json,Candidate,Error)!=EOKStreetLoad::Ok){UE_LOG(LogTemp,Warning,TEXT("Street region: converted save unreadable: %s"),*Error);return;}
 Candidate.Region=To;const FString From=S.Store.Region;
 if(CommitStore(MoveTemp(Candidate))){UE_LOG(LogTemp,Display,TEXT("Street region: %s -> %s, cash %lld"),*From,*To,S.Store.PlayerCash);LoadUiTexts();RefreshUI();}
}

// The player's language: kept in player.json with what else is there. Each language's own setting comes with it
// (zh-TW: Taiwan, zh-CN: mainland China, en: the US) unless asked not to; the region can also be picked on its own.
void AOKStreetSocialScene::SetLanguage(const FString& Lang,bool FollowRegion){
 auto& S=*State;
 if(FollowRegion){const FString Pair=HomeRegion(Lang);if(Pair!=S.Store.Region)ChangeRegion(Pair);}
 if(Lang==S.Lang)return;S.Lang=Lang;
 const FString PlayerFile=FPaths::GetPath(S.Manifest.SavePath)/TEXT("player.json");FString Body;TSharedPtr<FJsonObject> Player;
 if(!FFileHelper::LoadFileToString(Body,*PlayerFile)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),Player)||!Player.IsValid())Player=MakeShared<FJsonObject>();
 Player->SetStringField(TEXT("locale"),Lang);
 if(!S.SelfTest&&!S.Manifest.SavePath.IsEmpty()){FString Out;FJsonSerializer::Serialize(Player.ToSharedRef(),TJsonWriterFactory<>::Create(&Out));FFileHelper::SaveStringToFile(Out,*PlayerFile,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
 LoadUiTexts();RefreshUI(OKStreetUi::All|OKStreetUi::Shell); // the shell's labels too
}

// The characters' look (Plugins/OKHGShading): "koikatsu" (each card's own ramp), "ramp:<key>" (one of the game's
// shadow ramps, Content/KKPreview/Ramps, applied live) or "hg" (the HG materials: the map starts again on "auto").
FString AOKStreetSocialScene::RenderStyle() const{
 if(OKRenderStyle()==1)return TEXT("hg");const FString Ramp=OKRenderRampKey();return Ramp.IsEmpty()?TEXT("koikatsu"):TEXT("ramp:")+Ramp;
}
bool AOKStreetSocialScene::ApplyRenderStyleSetting(const FString& Style){
 // player.json is read before the scene names the ramp catalogue; an unknown key then falls back to the card ramps.
 FString Ramp;if(Style.StartsWith(TEXT("ramp:"))){Ramp=Style.Mid(5);if(Ramp.IsEmpty())return false;}
 else if(Style!=TEXT("koikatsu")&&Style!=TEXT("hg"))return false;
 OKSetRenderStyle(Style==TEXT("hg")?1:0);OKSetRenderRampKey(Ramp);return true;
}
FString AOKStreetSocialScene::RenderStyleLabel(const FString& Style) const{
 if(Style==TEXT("hg")||Style==TEXT("koikatsu"))return OKT(*(TEXT("ui.settings.render_style_")+Style));
 const FString Key=Style.Mid(5);FString Name=Key;
 // Every ramp has a translated name (ui.toml ramp_<key>); one exported later without it shows the game's own name.
 const FString Id=TEXT("ui.settings.ramp_")+Key;Name=OKT(*Id);
 if(Name==Id)if(const FOKRenderRamp* R=OKRenderRamps().FindByPredicate([&](const FOKRenderRamp& X){return X.Key==Key;}))Name=R->Name;
 return OKMsg(TEXT("ui.settings.render_style_ramp"),{{TEXT("name"),Name}});
}
void AOKStreetSocialScene::SetRenderStyle(const FString& Style){
 auto& S=*State;const FString Old=RenderStyle();
 if(Style==Old||S.SaveRestarting)return;
 if(Style.StartsWith(TEXT("ramp:"))&&!OKRenderRamps().ContainsByPredicate([&](const FOKRenderRamp& R){return R.Key==Style.Mid(5);}))return;
 if(!ApplyRenderStyleSetting(Style))return;
 const FString PlayerFile=FPaths::GetPath(S.Manifest.SavePath)/TEXT("player.json");FString Body;TSharedPtr<FJsonObject> Player;
 if(!FFileHelper::LoadFileToString(Body,*PlayerFile)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),Player)||!Player.IsValid())Player=MakeShared<FJsonObject>();
 Player->SetStringField(TEXT("render_style"),Style);
 UE_LOG(LogTemp,Display,TEXT("Street render style: %s -> %s"),*Old,*Style);
 if(!S.SelfTest&&!S.Manifest.SavePath.IsEmpty()){FString Out;FJsonSerializer::Serialize(Player.ToSharedRef(),TJsonWriterFactory<>::Create(&Out));FFileHelper::SaveStringToFile(Out,*PlayerFile,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);}
 // Ramps swap on the materials in place; HG has its own materials, made at load: the game goes to "auto" and the
 // map starts again on it.
 if((Old==TEXT("hg"))==(Style==TEXT("hg"))){OKSetCrowdRuntimeRamp(this,OKRenderRampKey());RefreshUI();return;}
 if(S.SelfTest||S.Manifest.SavePath.IsEmpty()||!S.Started){RefreshUI();return;}
 SaveToSlot(TEXT("auto"));RestartWithSave(TEXT("load"),TEXT("auto"));
 S.SaveNotice=OKT(TEXT("ui.settings.render_style_restarting"));RefreshUI();
}
