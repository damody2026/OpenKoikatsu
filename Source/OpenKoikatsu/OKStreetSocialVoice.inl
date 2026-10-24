// Voice source setting: built-in recorded clips, or the local TTS server (Tools/ok-tts, Rust).
// QueueNode always admits the built-in clips first. In TTS mode it also asks the server for the
// line's Chinese text; the server writes an OGG below generated_voice_root and the reply replaces
// the clip sequence before playback starts. Playback, lip sync and completion use the normal
// runtime speech path, so dialogue rules and saves do not change. Any TTS failure or timeout
// keeps the built-in clips. Self-tests never read the user setting.
// In TTS mode the player (male) also speaks the line he says (PlayerLine) before her reply, in one of
// 22 tones (ok-tts voices.json "m0".."m21", each a VoiceDesign voice fixed as an x-vector profile), chosen from the
// dropdown; "off" keeps him silent.

static const TCHAR* const OKStreetMaleToneIds[]={TEXT("off"),TEXT("m0"),TEXT("m1"),TEXT("m2"),TEXT("m3"),TEXT("m4"),TEXT("m5"),TEXT("m6"),TEXT("m7"),TEXT("m8"),TEXT("m9"),TEXT("m10"),TEXT("m11"),TEXT("m12"),TEXT("m13"),TEXT("m14"),TEXT("m15"),TEXT("m16"),TEXT("m17"),TEXT("m18"),TEXT("m19"),TEXT("m20"),TEXT("m21")};
static const TCHAR* const OKStreetMaleToneNames[]={TEXT("不說話"),TEXT("溫柔體貼"),TEXT("爽朗陽光"),TEXT("冷靜沉穩"),TEXT("害羞靦腆"),TEXT("自信霸氣"),TEXT("慵懶隨性"),TEXT("斯文書生"),TEXT("調皮搞笑"),TEXT("清爽少年"),TEXT("成熟大叔"),TEXT("溫柔學長"),TEXT("熱血少年"),TEXT("優雅貴公子"),TEXT("痞帥浪子"),TEXT("木訥老實"),TEXT("溫潤醫生"),TEXT("開朗暖男"),TEXT("冷酷寡言"),TEXT("偶像少年"),TEXT("可靠大哥"),TEXT("文藝青年"),TEXT("腹黑笑面")};
static int32 OKStreetMaleToneIndex(const FString& Id){for(int32 I=0;I<UE_ARRAY_COUNT(OKStreetMaleToneIds);++I)if(Id==OKStreetMaleToneIds[I])return I;return INDEX_NONE;}

// Player (male) name: the card name unless the player typed one in the right panel; kept in
// voice-settings.json as player_name. Self-tests keep the card name so their evidence stays exact.
void AOKStreetSocialScene::LoadPlayerName(){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(0))return;S.PlayerCardName=S.Manifest.Characters[0].Name;
 if(S.SelfTest||S.RetargetSelfTest||S.Manifest.SavePath.IsEmpty())return;
 FString Body,Name;TSharedPtr<FJsonObject> J;
 if(FFileHelper::LoadFileToString(Body,*(FPaths::GetPath(S.Manifest.SavePath)/TEXT("voice-settings.json")))&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),J)&&J.IsValid()&&J->TryGetStringField(TEXT("player_name"),Name)){
  Name.TrimStartAndEndInline();if(!Name.IsEmpty())S.Manifest.Characters[0].Name=Name.Left(24);
 }
}

void AOKStreetSocialScene::SetPlayerName(const FString& Typed){
 if(!State||!State->Manifest.Characters.IsValidIndex(0))return;auto& S=*State;
 FString Name=Typed.TrimStartAndEnd().Left(24);
 if(Name==S.Manifest.Characters[0].Name)return;
 S.Manifest.Characters[0].Name=Name.IsEmpty()?S.PlayerCardName:Name;
 if(!S.SelfTest&&!S.RetargetSelfTest&&!S.Manifest.SavePath.IsEmpty()){
  const FString Path=FPaths::GetPath(S.Manifest.SavePath)/TEXT("voice-settings.json");FString Body,Out,Error;TSharedPtr<FJsonObject> J;
  if(!FFileHelper::LoadFileToString(Body,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),J)||!J.IsValid())J=MakeShared<FJsonObject>();
  if(Name.IsEmpty())J->RemoveField(TEXT("player_name"));else J->SetStringField(TEXT("player_name"),Name);
  FJsonSerializer::Serialize(J.ToSharedRef(),TJsonWriterFactory<>::Create(&Out));
  if(!OKStreetAtomicSave(Path,Out,Error)){S.TtsNotice=OKT(TEXT("ui.voice.hero_name_save_failed"))+Error;RefreshUI();return;}
 }
 S.TtsNotice=OKT(TEXT("ui.voice.hero_name_label"))+S.Manifest.Characters[0].Name+OKT(TEXT("ui.voice.next_line_applies"));RefreshUI();
}

void AOKStreetSocialScene::LoadVoiceSettings(const TSharedPtr<FJsonObject>& Manifest){
 auto& S=*State;
 // The launcher that started ok-tts names its port (local-instance.json, Tools/ok_instance.lua: each checkout its own
 // ports); it wins over an endpoint saved in voice-settings.json, which may come from another checkout.
 const FString LaunchedTts=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TTS_ENDPOINT"));
 const bool LaunchedTtsLocal=LaunchedTts.StartsWith(TEXT("http://127.0.0.1:"))||LaunchedTts.StartsWith(TEXT("http://localhost:"));
 if(LaunchedTtsLocal)S.TtsEndpoint=LaunchedTts;
 if(Manifest.IsValid())Manifest->TryGetStringField(TEXT("generated_voice_root"),S.TtsCacheDir);
 if(S.SelfTest||S.RetargetSelfTest||S.TtsCacheDir.IsEmpty()||S.Manifest.SavePath.IsEmpty()){S.TtsCacheDir.Reset();return;}
 S.TtsSettingsPath=FPaths::GetPath(S.Manifest.SavePath)/TEXT("voice-settings.json");
 S.TtsSmoke=FParse::Param(FCommandLine::Get(),TEXT("StreetSocialTtsSmoke"));if(S.TtsSmoke){S.TtsEnabled=true;return;}
 FString Body;TSharedPtr<FJsonObject> J;
 if(!FFileHelper::LoadFileToString(Body,*S.TtsSettingsPath)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),J)||!J.IsValid())return;
 FString Mode,Endpoint;double Age=0,Timeout=0;
 if(J->TryGetStringField(TEXT("voice_mode"),Mode))S.TtsEnabled=Mode==TEXT("tts");
 // Local server only; dialogue text is never sent off this machine.
 if(!LaunchedTtsLocal&&J->TryGetStringField(TEXT("tts_endpoint"),Endpoint)&&(Endpoint.StartsWith(TEXT("http://127.0.0.1:"))||Endpoint.StartsWith(TEXT("http://localhost:"))))S.TtsEndpoint=Endpoint;
 if(J->TryGetNumberField(TEXT("tts_age"),Age))S.TtsAge=FMath::Clamp(int32(Age),12,45);
 if(J->TryGetNumberField(TEXT("tts_timeout_seconds"),Timeout)&&Timeout>=2&&Timeout<=60)S.TtsTimeout=Timeout;
 FString Tone;if(J->TryGetStringField(TEXT("male_tone"),Tone)&&OKStreetMaleToneIndex(Tone)!=INDEX_NONE)S.MaleTone=Tone;
 if(S.TtsEnabled)ProbeTtsServer();
}

void AOKStreetSocialScene::SaveVoiceSettings(){
 auto& S=*State;if(S.TtsSettingsPath.IsEmpty())return;
 // Keep keys this scene does not own (the launcher reads tts_server_command).
 FString Body;TSharedPtr<FJsonObject> J;
 if(!FFileHelper::LoadFileToString(Body,*S.TtsSettingsPath)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),J)||!J.IsValid())J=MakeShared<FJsonObject>();
 J->SetStringField(TEXT("voice_mode"),S.TtsEnabled?TEXT("tts"):TEXT("builtin"));J->SetStringField(TEXT("tts_endpoint"),S.TtsEndpoint);
 J->SetNumberField(TEXT("tts_age"),S.TtsAge);J->SetNumberField(TEXT("tts_timeout_seconds"),S.TtsTimeout);J->SetStringField(TEXT("male_tone"),S.MaleTone);
 FString Out,Error;FJsonSerializer::Serialize(J.ToSharedRef(),TJsonWriterFactory<>::Create(&Out));
 if(!OKStreetAtomicSave(S.TtsSettingsPath,Out,Error))S.TtsNotice=OKT(TEXT("ui.voice.voice_settings_save_failed"))+Error;
}

void AOKStreetSocialScene::ToggleVoiceSource(){
 if(!State)return;auto& S=*State;
 if(S.TtsCacheDir.IsEmpty()){S.TtsNotice=OKT(TEXT("ui.voice.no_tts_cache_dir"));RefreshUI();return;}
 S.TtsEnabled=!S.TtsEnabled;S.TtsNotice=S.TtsEnabled?OKT(TEXT("ui.voice.voice_tts_next_line")):OKT(TEXT("ui.voice.voice_builtin_recording"));
 SaveVoiceSettings();if(S.TtsEnabled)ProbeTtsServer();ProbeVoiceInfo();RefreshUI();
}

void AOKStreetSocialScene::SetMaleTone(int32 Index){
 if(!State||!UE_ARRAY_COUNT(OKStreetMaleToneIds)||Index<0||Index>=UE_ARRAY_COUNT(OKStreetMaleToneIds))return;auto& S=*State;
 S.MaleTone=OKStreetMaleToneIds[Index];SaveVoiceSettings();
 S.TtsNotice=FString(OKT(TEXT("ui.scene.his_tone_label")))+OKT(OKStreetMaleToneNames[Index])+OKT(TEXT("ui.voice.next_line_applies"));RefreshUI();
}

FString AOKStreetSocialScene::MaleToneLabel() const{
 const int32 I=State?OKStreetMaleToneIndex(State->MaleTone):INDEX_NONE;return I==INDEX_NONE?FString(TEXT("?")):FString(OKT(OKStreetMaleToneNames[I]));
}

// Her personality and the voice-design prompt the server will use, shown under the male tone menu.
void AOKStreetSocialScene::ProbeVoiceInfo(){
 if(!State)return;auto& S=*State;S.TtsVoiceInfo.Reset();
 if(!S.TtsEnabled||S.TtsCacheDir.IsEmpty()||!S.Manifest.Characters.IsValidIndex(S.Target))return;
 const auto& Her=S.Manifest.Characters[S.Target];const int32 Target=S.Target;
 auto Request=FHttpModule::Get().CreateRequest();
 Request->SetURL(S.TtsEndpoint/FString::Printf(TEXT("voice?personality=%d&age=%d"),Her.PersonalityId,S.TtsAge));Request->SetVerb(TEXT("GET"));Request->SetTimeout(3);
 Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Target](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  if(!State||State->Target!=Target)return;auto& S=*State;TSharedPtr<FJsonObject> J;FString Key,Name,Label,Instruct;
  if(!Connected||!Response.IsValid()||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),J)||!J.IsValid()||!J->TryGetStringField(TEXT("instruct"),Instruct))return;
  J->TryGetStringField(TEXT("personality"),Key);J->TryGetStringField(TEXT("name"),Name);J->TryGetStringField(TEXT("label"),Label);S.TtsVoiceInstruct=Instruct;
  S.TtsVoiceInfo=OKMsg(TEXT("ui.voice.heroine_voice_profile"), {{TEXT("heroine_name"), *S.Manifest.Characters[Target].Name}, {TEXT("trait_key"), *Key}, {TEXT("trait_name"), *Name}, {TEXT("trait_label"), *Label}, {TEXT("tts_hint"), *Instruct}});
  RefreshUI();
 });
 Request->ProcessRequest();
 ProbeCardVoice();
}

// The server starts with the game and needs ~10 s to load its model, so "not ready" at launch is normal: ask again
// every 2 s (up to ~80 s) and only then say it is not answering.
void AOKStreetSocialScene::ProbeTtsServer(int32 Attempt){
 auto& S=*State;auto Request=FHttpModule::Get().CreateRequest();
 Request->SetURL(S.TtsEndpoint/TEXT("health"));Request->SetVerb(TEXT("GET"));Request->SetTimeout(3);
 Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Attempt](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  if(!State||!State->TtsEnabled)return;TSharedPtr<FJsonObject> J;bool Ready=false;
  if(Connected&&Response.IsValid()&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),J)&&J.IsValid())J->TryGetBoolField(TEXT("ready"),Ready);
  const bool Waiting=!Ready&&Attempt<40;const FString Before=State->TtsNotice;
  State->TtsNotice=Ready?OKT(TEXT("ui.voice.voice_tts_server_ready")):Waiting?OKT(TEXT("ui.voice.voice_tts_server_starting")):OKT(TEXT("ui.voice.voice_tts_no_response"))+State->TtsEndpoint+OKT(TEXT("ui.voice.tts_server_restart_hint"));
  // Still loading: ask again in 2 s (the timer dies with the scene).
  if(Waiting)GetWorldTimerManager().SetTimer(TtsProbeTimer,FTimerDelegate::CreateWeakLambda(this,[this,Attempt](){if(State&&State->TtsEnabled)ProbeTtsServer(Attempt+1);}),2.f,false);
  // Only a changed notice rebuilds the panel (the probe repeats every 2 s while the server loads).
  if(State->TtsNotice!=Before)RefreshUI();
 });
 Request->ProcessRequest();
}

// Male == false: her reply, which replaces the line's recorded clips. Male == true: the player's line,
// spoken by actor 0 before her reply (see Tick); a failure only skips his voice.
void AOKStreetSocialScene::RequestTtsLine(const FString& Text,const FString& Voice,bool Male){
 auto& S=*State;const uint32 Serial=Male?++S.MaleSerial:++S.TtsSerial;
 if(Male){S.MaleTtsPending=true;S.MaleRequestedAt=SceneTime();}else{S.TtsPending=true;S.TtsRequestedAt=SceneTime();}
 auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("text"),Text);Body->SetStringField(TEXT("personality"),Voice);Body->SetNumberField(TEXT("age"),S.TtsAge);Body->SetStringField(TEXT("out_dir"),S.TtsCacheDir);
 // Her card: a voice of her own stored there is used (the player's tone comes from the menu, not his card).
 if(!Male)if(const FString Card=OKStreetCardFor(S,S.Target);!Card.IsEmpty())Body->SetStringField(TEXT("card"),Card);
 FString Payload;FJsonSerializer::Serialize(Body,TJsonWriterFactory<>::Create(&Payload));
 auto Request=FHttpModule::Get().CreateRequest();
 Request->SetURL(S.TtsEndpoint/TEXT("synthesize"));Request->SetVerb(TEXT("POST"));Request->SetHeader(TEXT("Content-Type"),TEXT("application/json; charset=utf-8"));Request->SetContentAsString(Payload);Request->SetTimeout(S.TtsTimeout);
 Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Serial,Male](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  // A newer line, an ended talk or the playback timeout already owns the voice.
  if(!State)return;auto& S=*State;
  if(Male?(S.MaleSerial!=Serial||!S.MaleTtsPending):(S.TtsSerial!=Serial||!S.TtsPending))return;
  (Male?S.MaleTtsPending:S.TtsPending)=false;
  FString Error=OKT(TEXT("ui.scene.tts_connect_failed")),Path,Id;TSharedPtr<FJsonObject> J;bool Ok=false;
  if(Connected&&Response.IsValid()&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),J)&&J.IsValid()){
   if(J->TryGetBoolField(TEXT("ok"),Ok)&&Ok&&J->TryGetStringField(TEXT("path"),Path))Ok=OKAdmitCrowdRuntimeVoice(this,Path,Id,Error);
   else{Ok=false;J->TryGetStringField(TEXT("error"),Error);}
  }
  if(Male){
   if(Ok&&S.MalePhase==1){S.MaleVoiceId=Id;UE_LOG(LogTemp,Display,TEXT("Street TTS male voice admitted: tone=%s path=%s"),*S.MaleTone,*Path);}
   else if(!Ok){S.MalePhase=3;S.TtsNotice=OKT(TEXT("ui.voice.male_tts_failed_skip"))+Error;UE_LOG(LogTemp,Warning,TEXT("Street TTS male skipped: %s"),*Error);RefreshUI();}
   return;
  }
  if(Ok&&S.Node!=INDEX_NONE&&!S.ClipStarted&&S.Voice==0){S.VoiceIds={Id};++S.TtsLines;UE_LOG(LogTemp,Display,TEXT("Street TTS voice admitted: actor=%d path=%s"),S.Target,*Path);if(!S.TtsNotice.IsEmpty()&&S.TtsNotice.StartsWith(OKT(TEXT("ui.voice.tts_failed")))){S.TtsNotice.Reset();RefreshUI();}}
  else if(!Ok){++S.TtsFallbacks;S.TtsNotice=OKT(TEXT("ui.voice.tts_failed_builtin"))+Error;UE_LOG(LogTemp,Warning,TEXT("Street TTS fallback: %s"),*Error);RefreshUI();}
 });
 Request->ProcessRequest();
}
