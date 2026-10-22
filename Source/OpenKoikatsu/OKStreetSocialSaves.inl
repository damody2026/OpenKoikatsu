// ---- Save slots and the start menu. The live game is the folder of the relationship save: every change is written
// there at once and the clock hourly, so the game always saves itself. A slot is a copy of that game's files in
// Saves/<slot>/ with slot.json describing it ("auto" is also written every 10 minutes and on quitting). Loading a slot
// or starting a new game happens on the next start of the map (Saves/pending.json), when nothing has a file open.
// Settings (player.json: language; voice, ASR and DeepSeek settings) belong to the player, not to a game.

static const TCHAR* OKStreetSaveFiles[]={TEXT("relationships.json"),TEXT("relationships.json.bak"),TEXT("phone.json"),TEXT("free-talk-log.json"),TEXT("free-talk-memory.json"),TEXT("songs.json")};
static const TCHAR* OKStreetSaveDirs[]={TEXT("accounts"),TEXT("history"),TEXT("mind"),TEXT("Photos")};
static constexpr int32 OKStreetSaveSlotCount=8;
static constexpr double OKStreetAutoSaveSeconds=600;

// Debug logs of the mind (trace*.jsonl, ~50 MB each) and hand-made backups are not part of a game.
static bool OKStreetSaveKeeps(const FString& File){
 const FString Name=FPaths::GetCleanFilename(File);
 return !(Name.StartsWith(TEXT("trace"))&&Name.EndsWith(TEXT(".jsonl")))&&!Name.Contains(TEXT(".before-"));
}

static bool OKStreetCopyTree(const FString& From,const FString& To){
 auto& F=IFileManager::Get();TArray<FString> Files;F.FindFilesRecursive(Files,*From,TEXT("*"),true,false);bool Ok=true;
 for(const FString& File:Files){if(!OKStreetSaveKeeps(File))continue;FString Rel=File;FPaths::MakePathRelativeTo(Rel,*(From/TEXT("")));
  Ok&=F.Copy(*(To/Rel),*File,true,true)==COPY_OK;}
 return Ok;
}

// Copies a game's files from one folder to another (the live folder or a slot); the target's old game files go first.
static bool OKStreetCopyGame(const FString& From,const FString& To){
 auto& F=IFileManager::Get();F.MakeDirectory(*To,true);bool Ok=true;
 for(const TCHAR* Name:OKStreetSaveFiles){F.Delete(*(To/Name),false,true,true);if(F.FileExists(*(From/Name)))Ok&=F.Copy(*(To/Name),*(From/Name),true,true)==COPY_OK;}
 for(const TCHAR* Name:OKStreetSaveDirs){
  TArray<FString> Old;F.FindFilesRecursive(Old,*(To/Name),TEXT("*"),true,false);for(const FString& File:Old)if(OKStreetSaveKeeps(File))F.Delete(*File,false,true,true);
  if(F.DirectoryExists(*(From/Name)))Ok&=OKStreetCopyTree(From/Name,To/Name);}
 return Ok;
}

static void OKStreetDeleteGame(const FString& Dir){
 auto& F=IFileManager::Get();
 for(const TCHAR* Name:OKStreetSaveFiles)F.Delete(*(Dir/Name),false,true,true);
 for(const TCHAR* Name:OKStreetSaveDirs){TArray<FString> Old;F.FindFilesRecursive(Old,*(Dir/Name),TEXT("*"),true,false);for(const FString& File:Old)if(OKStreetSaveKeeps(File))F.Delete(*File,false,true,true);}
}

FString AOKStreetSocialScene::SaveSlotDir(const FString& Slot) const{
 return FPaths::GetPath(State->Manifest.SavePath)/TEXT("Saves")/Slot;
}

// Before the map loads the save: a load or a new game asked for by the last run of the map.
void AOKStreetSocialScene::ApplyPendingSave(){
 auto& S=*State;if(S.SelfTest||S.LiveTestPhase>=0||S.Manifest.SavePath.IsEmpty())return;
 const FString Live=FPaths::GetPath(S.Manifest.SavePath),Pending=Live/TEXT("Saves/pending.json");
 FString Body;TSharedPtr<FJsonObject> P;if(!FFileHelper::LoadFileToString(Body,*Pending))return;IFileManager::Get().Delete(*Pending);
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),P)||!P.IsValid())return;
 const FString Action=P->GetStringField(TEXT("action")),Slot=P->GetStringField(TEXT("slot"));
 State->SaveApplied=true;P->TryGetBoolField(TEXT("control_mic_ignored"),S.ControlMicIgnored);
 if(Action==TEXT("new")){OKStreetDeleteGame(Live);P->TryGetStringField(TEXT("region"),S.NewGameRegion);S.SaveNotice=OKT(TEXT("ui.saves.new_started"));UE_LOG(LogTemp,Display,TEXT("Street saves: new game (%s)"),*S.NewGameRegion);}
 else if(Action==TEXT("load")&&IFileManager::Get().FileExists(*(SaveSlotDir(Slot)/TEXT("relationships.json")))){
  const bool Ok=OKStreetCopyGame(SaveSlotDir(Slot),Live);S.SaveNotice=OKMsg(Ok?TEXT("ui.saves.loaded"):TEXT("ui.saves.load_failed"),{{TEXT("slot"),SaveSlotName(Slot)}});
  UE_LOG(LogTemp,Display,TEXT("Street saves: loaded slot %s (%s)"),*Slot,Ok?TEXT("ok"):TEXT("incomplete"));}
}

FString AOKStreetSocialScene::SaveSlotName(const FString& Slot) const{
 return Slot==TEXT("auto")?OKT(TEXT("ui.saves.auto_slot")):OKMsg(TEXT("ui.saves.slot_n"),{{TEXT("n"),Slot}});
}

// The live game copied into a slot, with what the slot list shows (the game's day and time, money, lovers).
bool AOKStreetSocialScene::SaveToSlot(const FString& Slot){
 auto& S=*State;if(S.SelfTest||S.Manifest.SavePath.IsEmpty())return false;
 SaveCalendar();
 const FString Dir=SaveSlotDir(Slot),Temp=Dir+TEXT(".writing");auto& F=IFileManager::Get();F.DeleteDirectory(*Temp,false,true);
 if(!OKStreetCopyGame(FPaths::GetPath(S.Manifest.SavePath),Temp)){F.DeleteDirectory(*Temp,false,true);S.SaveNotice=OKMsg(TEXT("ui.saves.save_failed"),{{TEXT("slot"),SaveSlotName(Slot)}});RefreshUI();return false;}
 auto M=MakeShared<FJsonObject>();
 M->SetStringField(TEXT("saved_at"),FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M")));
 M->SetNumberField(TEXT("day"),S.Store.CalendarDay);M->SetNumberField(TEXT("minute"),S.Store.CalendarMinute);
 M->SetStringField(TEXT("region"),S.Store.Region);M->SetNumberField(TEXT("cash"),double(S.Store.PlayerCash));M->SetNumberField(TEXT("bank"),double(S.Store.PlayerBank));
 TArray<TSharedPtr<FJsonValue>> Lovers;
 for(const auto& R:S.Store.Relations)if(R.Stage==TEXT("lover"))for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(OKStreetStableId(S.Manifest.Characters[I])==R.Id)Lovers.Add(MakeShared<FJsonValueString>(S.Manifest.Characters[I].Name));
 M->SetArrayField(TEXT("lovers"),Lovers);
 FString Out;FJsonSerializer::Serialize(M,TJsonWriterFactory<>::Create(&Out));FFileHelper::SaveStringToFile(Out,*(Temp/TEXT("slot.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 F.DeleteDirectory(*Dir,false,true);
 if(!F.Move(*Dir,*Temp,true,true)){S.SaveNotice=OKMsg(TEXT("ui.saves.save_failed"),{{TEXT("slot"),SaveSlotName(Slot)}});RefreshUI();return false;}
 S.NextAutoSave=FPlatformTime::Seconds()+OKStreetAutoSaveSeconds;
 if(Slot!=TEXT("auto"))S.SaveNotice=OKMsg(TEXT("ui.saves.saved"),{{TEXT("slot"),SaveSlotName(Slot)}});
 UE_LOG(LogTemp,Display,TEXT("Street saves: saved slot %s"),*Slot);RefreshUI();return true;
}

// A slot's line in the list: its day and time in the game, money and lovers, and when it was saved; "" if empty.
FString AOKStreetSocialScene::SaveSlotSummary(const FString& Slot) const{
 FString Body;TSharedPtr<FJsonObject> M;
 if(!FFileHelper::LoadFileToString(Body,*(SaveSlotDir(Slot)/TEXT("slot.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),M)||!M.IsValid())return FString();
 TArray<FString> Lovers;const TArray<TSharedPtr<FJsonValue>>* L=nullptr;if(M->TryGetArrayField(TEXT("lovers"),L))for(const auto& V:*L)Lovers.Add(V->AsString());
 const int32 Minute=int32(M->GetNumberField(TEXT("minute")));
 // The slot's money is in its own region's units: shown as that region's money.
 const FString Region=M->GetStringField(TEXT("region"));const int64 Cash=int64(M->GetNumberField(TEXT("cash")))+int64(M->GetNumberField(TEXT("bank")));
 FString MoneyText=Money(Cash);
 if(Region!=State->Store.Region){auto Request=OKRulesRequest(TEXT("money_format"));Request->SetStringField(TEXT("region"),Region);Request->SetNumberField(TEXT("amount"),double(Cash));
  if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(*State,Request))Answer->TryGetStringField(TEXT("text"),MoneyText);} // locale.rs money_format
 return OKMsg(TEXT("ui.saves.summary"),{{TEXT("calendar"),OKStreetCalendarLabel(int32(M->GetNumberField(TEXT("day"))),FMath::Max(0,Minute)/60.0)},{TEXT("money"),MoneyText},
  {TEXT("lovers"),Lovers.Num()?FString::Join(Lovers,TEXT("、")):OKT(TEXT("ui.saves.no_lovers"))},{TEXT("saved_at"),M->GetStringField(TEXT("saved_at"))}});
}

// Load a slot or start anew: the game now is kept in "auto" first (unless "auto" is what is loaded), then the map
// starts again and takes the files (ApplyPendingSave).
void AOKStreetSocialScene::RestartWithSave(const FString& Action,const FString& Slot){
 auto& S=*State;if(S.SelfTest||S.Manifest.SavePath.IsEmpty())return;
 if(Slot!=TEXT("auto"))SaveToSlot(TEXT("auto"));
 auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("action"),Action);P->SetStringField(TEXT("slot"),Slot);
 P->SetStringField(TEXT("region"),HomeRegion(S.Lang));
 // The control channel's "the room microphone is not him" is the tester's setting, not the game's: it survives the map
 // starting again for a load or a new game.
 P->SetBoolField(TEXT("control_mic_ignored"),S.ControlMicIgnored);
 FString Out;FJsonSerializer::Serialize(P,TJsonWriterFactory<>::Create(&Out));
 const FString Pending=FPaths::GetPath(S.Manifest.SavePath)/TEXT("Saves/pending.json");IFileManager::Get().MakeDirectory(*FPaths::GetPath(Pending),true);
 if(!FFileHelper::SaveStringToFile(Out,*Pending,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))return;
 UE_LOG(LogTemp,Display,TEXT("Street saves: restarting the map for %s %s"),*Action,*Slot);
 S.SaveRestarting=true;S.SaveNotice=OKT(TEXT("ui.saves.restarting"));RefreshUI();
 // The map package this scene lives in, started again (the command line, the manifest and -Control stay).
 UGameplayStatics::OpenLevel(this,FName(*GetWorld()->GetOutermost()->GetName()));
}

void AOKStreetSocialScene::OpenSaveMenu(const FString& Mode){
 auto& S=*State;
 if(S.SaveMenu.IsEmpty()){S.SaveMenuPausedClock=!S.ClockPaused;if(!S.ClockPaused){S.ClockStartHours=OKStreetClockHours(S.ClockStartHours,FPlatformTime::Seconds()-S.ClockStartWall);S.ClockStartWall=FPlatformTime::Seconds();S.ClockPaused=true;}}
 S.SaveMenu=Mode;S.SaveConfirmNew=false;S.MouseUI=true;ApplyMouseMode(true);RefreshUI();
}

void AOKStreetSocialScene::CloseSaveMenu(){
 auto& S=*State;if(S.SaveMenu.IsEmpty())return;
 S.SaveMenu.Reset();if(S.SaveMenuPausedClock){S.ClockPaused=false;S.ClockStartWall=FPlatformTime::Seconds();}S.SaveMenuPausedClock=false;RefreshUI();
}

// Each frame: the region a new game was started in, and the 10-minute autosave.
void AOKStreetSocialScene::TickSaves(){
 auto& S=*State;if(S.SelfTest||S.Manifest.SavePath.IsEmpty()||S.SaveRestarting)return;
 // The start menu when the game starts, unless this start is the load or new game just chosen. Test drivers (-Control)
 // skip it unless OK_STREET_START_MENU=1; OK_STREET_START_MENU=0 skips it for anyone.
 if(!S.StartMenuDecided){S.StartMenuDecided=true;const FString Env=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STREET_START_MENU"));
  const bool Show=Env==TEXT("1")||(Env!=TEXT("0")&&!FParse::Param(FCommandLine::Get(),TEXT("StreetSocialControl")));
  if(S.SaveApplied){S.Message=S.SaveNotice;RefreshUI();}else if(Show&&S.LiveTestPhase<0)OpenSaveMenu(TEXT("start"));}
 if(!S.NewGameRegion.IsEmpty()&&S.Rules){const FString Region=S.NewGameRegion;S.NewGameRegion.Reset();ChangeRegion(Region);SaveCalendar();}
 const double Now=FPlatformTime::Seconds();
 if(S.NextAutoSave<=0)S.NextAutoSave=Now+OKStreetAutoSaveSeconds;
 else if(Now>=S.NextAutoSave&&S.SaveMenu.IsEmpty())SaveToSlot(TEXT("auto"));
}

// The menu: at the start (continue, new game, load, language) and from the panel (also save into a slot).
TSharedRef<SWidget> AOKStreetSocialScene::BuildSaveMenu(){
 auto& S=*State;const bool Start=S.SaveMenu==TEXT("start");
 auto Weak=TWeakObjectPtr<AOKStreetSocialScene>(this);
 auto Button=[](const FString& Text,TFunction<void()> Do,bool Enabled=true){return SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(Enabled).HAlign(HAlign_Center)
  .Text(FText::FromString(Text)).OnClicked_Lambda([Do](){Do();return FReply::Handled();});};
 auto List=SNew(SVerticalBox);
 List->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(OKT(Start?TEXT("ui.saves.title_start"):TEXT("ui.saves.title_game")))).Font(FCoreStyle::GetDefaultFontStyle("Bold",26)).ColorAndOpacity(FLinearColor::White)];
 if(!S.SaveNotice.IsEmpty())List->AddSlot().AutoHeight().Padding(8,2)[SNew(STextBlock).Text(FText::FromString(S.SaveNotice)).ColorAndOpacity(FLinearColor(.65f,.85f,1.f)).AutoWrapText(true)];
 if(S.SaveRestarting)return SNew(SBorder).BorderImage(OKStreetGlassPanel()).Padding(24)[List];
 auto Top=SNew(SHorizontalBox);
 Top->AddSlot().AutoWidth().Padding(4)[Button(OKT(Start?TEXT("ui.saves.continue"):TEXT("ui.saves.back")),[Weak](){if(Weak.IsValid())Weak->CloseSaveMenu();})];
 Top->AddSlot().AutoWidth().Padding(4)[Button(OKT(S.SaveConfirmNew?TEXT("ui.saves.new_confirm"):TEXT("ui.saves.new_game")),[Weak](){if(!Weak.IsValid())return;auto& L=*Weak->State;
  if(!L.SaveConfirmNew){L.SaveConfirmNew=true;Weak->RefreshUI();return;}Weak->RestartWithSave(TEXT("new"),FString());})];
 // The language here too, so a new game can start in it (its region comes with it).
 if(S.LangOptions.IsEmpty())for(const TCHAR* L:{TEXT("zh-TW"),TEXT("zh-CN"),TEXT("en")})S.LangOptions.Add(MakeShared<FString>(L));
 const int32 LangIndex=FMath::Max(0,S.LangOptions.IndexOfByPredicate([&](const TSharedPtr<FString>& L){return *L==S.Lang;}));
 Top->AddSlot().AutoWidth().Padding(4)[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&S.LangOptions).InitiallySelectedItem(S.LangOptions[LangIndex])
  .OnGenerateWidget_Lambda([Weak](TSharedPtr<FString> Item){return SNew(STextBlock).Text(FText::FromString(Item.IsValid()&&Weak.IsValid()?Weak->T(*(TEXT("ui.settings.lang_")+*Item)):FString()));})
  .OnSelectionChanged_Lambda([Weak](TSharedPtr<FString> Item,ESelectInfo::Type Info){if(Weak.IsValid()&&Item.IsValid()&&Info!=ESelectInfo::Direct)Weak->SetLanguage(*Item);})
  [SNew(STextBlock).Text(FText::FromString(OKMsg(TEXT("ui.settings.language"),{{TEXT("name"),T(*(TEXT("ui.settings.lang_")+S.Lang))}})))]];
 List->AddSlot().AutoHeight().Padding(4)[Top];
 List->AddSlot().AutoHeight().Padding(8,10,8,2)[SNew(STextBlock).Text(FText::FromString(OKT(TEXT("ui.saves.slots_label")))).ColorAndOpacity(FLinearColor::White)];
 TArray<FString> Slots={TEXT("auto")};for(int32 I=1;I<=OKStreetSaveSlotCount;++I)Slots.Add(FString::FromInt(I));
 for(const FString& Slot:Slots){
  const FString Summary=SaveSlotSummary(Slot);auto Row=SNew(SHorizontalBox);
  Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4)[SNew(SBox).WidthOverride(110)[SNew(STextBlock).Text(FText::FromString(SaveSlotName(Slot))).ColorAndOpacity(FLinearColor::White)]];
  Row->AddSlot().FillWidth(1).VAlign(VAlign_Center).Padding(4)[SNew(STextBlock).Text(FText::FromString(Summary.IsEmpty()?OKT(TEXT("ui.saves.empty")):Summary)).ColorAndOpacity(FLinearColor(.85f,.88f,.92f)).AutoWrapText(true)];
  if(!Start&&Slot!=TEXT("auto"))Row->AddSlot().AutoWidth().Padding(4)[Button(OKT(TEXT("ui.saves.save_here")),[Weak,Slot](){if(Weak.IsValid())Weak->SaveToSlot(Slot);})];
  Row->AddSlot().AutoWidth().Padding(4)[Button(OKT(TEXT("ui.saves.load")),[Weak,Slot](){if(Weak.IsValid())Weak->RestartWithSave(TEXT("load"),Slot);},!Summary.IsEmpty())];
  List->AddSlot().AutoHeight().Padding(2)[Row];
 }
 List->AddSlot().AutoHeight().Padding(8,10,8,2)[SNew(STextBlock).Text(FText::FromString(OKT(TEXT("ui.saves.autosave_note")))).ColorAndOpacity(FLinearColor(.7f,.74f,.8f)).AutoWrapText(true)];
 return SNew(SOverlay)
  +SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0,0,0,Start?.72f:.5f))]
  +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(SBox).WidthOverride(860)[SNew(SBorder).BorderImage(OKStreetGlassPanel()).Padding(20)[List]]];
}
