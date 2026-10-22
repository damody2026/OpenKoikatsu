// A heroine sings for him (Docs/HEROINE_SINGING_IMPLEMENTATION_PLAN.md). Who sings and when is the rules' call
// (ok-rules song.rs: song_decide, song_lyrics); the song is made by ok-song (Tools/ok-song: generate, separate, her voice,
// mix, mouth envelope); this file asks, waits, offers, plays and cleans up.
//
//   Idle -> Preparing (job queued at ok-song) -> Ready (song on disk) -> Offered (he is talking with her, nothing else
//   going on: accept / later / decline) -> Intro (she says a line, TTS in her voice) -> Singing (the mix plays, her mouth
//   follows her vocal's envelope) -> Outro -> Idle.
// Leaving her, a phone call or her actor going away stops it and keeps the song for a later offer. Every HTTP answer is
// checked against Epoch (bumped by a reset, a slot load or a new event), so an old answer never changes a newer event.
// Automatic songs are off until the player turns them on; the control channel (/song) can prepare and play to test.

static constexpr double OKSongCheckSeconds=10,OKSongPollSeconds=3,OKSongOfferSeconds=60,OKSongLaterSeconds=300;

static FString OKSongFile(const FOKStreetSceneState& S){return S.Manifest.SavePath.IsEmpty()?FString():FPaths::GetPath(S.Manifest.SavePath)/TEXT("songs.json");}

static TSharedRef<IHttpRequest,ESPMode::ThreadSafe> OKSongGet(const FString& Url){
 auto Request=FHttpModule::Get().CreateRequest();Request->SetURL(Url);Request->SetVerb(TEXT("GET"));Request->SetTimeout(10);return Request;
}

// The bottom dialogue box: what she says or sings now (the free-talk caption line, so it shows during a talk with her).
static void OKSongCaption(FOKStreetSceneState& S,const FString& Line){
 const FString Name=S.Manifest.Characters.IsValidIndex(S.Song.Girl)?S.Manifest.Characters[S.Song.Girl].Name:FString();
 S.FreeTalkCaptionHer=Name.IsEmpty()?Line:Name+TEXT("：")+Line;
}

// Lyrics for the caption: the sung lines without the section marks, on one line.
static FString OKSongCaptionLyrics(const FString& Lyrics){
 TArray<FString> Lines;Lyrics.ParseIntoArrayLines(Lines);FString Out;
 for(const FString& L:Lines){const FString T=L.TrimStartAndEnd();if(T.IsEmpty()||T.StartsWith(TEXT("[")))continue;Out+=(Out.IsEmpty()?TEXT(""):TEXT(" / "))+T;}
 return Out;
}

static int32 OKSongGirlOf(const FOKStreetSceneState& S,const FString& Id){
 for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(OKStreetStableId(S.Manifest.Characters[I])==Id)return I;
 return INDEX_NONE;
}

static const FOKStreetRelation* OKSongRelation(const FOKStreetSceneState& S,int32 Girl){
 if(!S.Manifest.Characters.IsValidIndex(Girl))return nullptr;const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);
 return S.Store.Relations.FindByPredicate([&](const FOKStreetRelation& R){return R.Id==Id;});
}

void AOKStreetSocialScene::SongSave(){
 auto& S=*State;auto& G=S.Song;const FString Path=OKSongFile(S);if(Path.IsEmpty()||S.SelfTest)return;
 auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("schema"),1);Root->SetStringField(TEXT("save_id"),G.SaveId);Root->SetBoolField(TEXT("enabled"),G.Enabled);
 Root->SetNumberField(TEXT("sung_day"),G.SungDay);Root->SetNumberField(TEXT("sung_today"),G.SungToday);
 auto Last=MakeShared<FJsonObject>();Last->SetStringField(TEXT("event_id"),G.EventId);Last->SetStringField(TEXT("heroine_id"),G.HeroineId);Last->SetStringField(TEXT("voice_tag"),G.VoiceTag);
 Last->SetStringField(TEXT("artifact_key"),G.ArtifactKey);Last->SetStringField(TEXT("phase"),FString::FromInt(int32(G.Phase)));Root->SetObjectField(TEXT("last_event"),Last);
 FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));
 const FString Tmp=Path+TEXT(".tmp");if(FFileHelper::SaveStringToFile(Text,*Tmp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))IFileManager::Get().Move(*Path,*Tmp,true,true);
}

// songs.json beside the save: the save's song id, whether automatic songs are on, what was sung today. A new save (no
// file) starts with none sung; a slot load brings its own file (OKStreetSaveFiles).
void AOKStreetSocialScene::SongLoad(){
 auto& S=*State;auto& G=S.Song;G.Loaded=true;
 FString Endpoint=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SONG_ENDPOINT"));
 G.Endpoint=(Endpoint.StartsWith(TEXT("http://127.0.0.1:"))||Endpoint.StartsWith(TEXT("http://localhost:")))?Endpoint:FString();
 FString Text;TSharedPtr<FJsonObject> J;const FString Path=OKSongFile(S);
 if(!Path.IsEmpty()&&FFileHelper::LoadFileToString(Text,*Path)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J)&&J.IsValid()){
  J->TryGetStringField(TEXT("save_id"),G.SaveId);J->TryGetBoolField(TEXT("enabled"),G.Enabled);J->TryGetNumberField(TEXT("sung_day"),G.SungDay);J->TryGetNumberField(TEXT("sung_today"),G.SungToday);
 }
 if(G.SaveId.IsEmpty()){G.SaveId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);SongSave();}
}

// Back to Idle; the job (if any) is cancelled at ok-song unless the song is kept for a later offer.
void AOKStreetSocialScene::SongReset(const FString& Why,bool CancelJob){
 auto& S=*State;auto& G=S.Song;
 if(SongOwnsVoice(G.Girl)){FString Ignore;OKStopCrowdRuntimeSpeech(this,G.Girl,Ignore);}
 if(CancelJob&&!G.JobId.IsEmpty()&&!G.Endpoint.IsEmpty()){auto Request=FHttpModule::Get().CreateRequest();Request->SetURL(G.Endpoint/TEXT("jobs")/G.JobId/TEXT("cancel"));Request->SetVerb(TEXT("POST"));Request->SetTimeout(5);Request->ProcessRequest();}
 const bool Keep=!CancelJob;++G.Epoch;G.Phase=EOKSongPhase::Idle;G.Override=false;G.RequestPending=G.PollPending=G.SayPending=G.SpeechStarted=false;G.Girl=INDEX_NONE;
 G.JobId.Reset();G.RequestId.Reset();G.JobStage.Reset();G.Mouth.Reset();G.SongVoiceId.Reset();if(!Keep){G.SongPath.Reset();G.ArtifactKey.Reset();}
 G.NextCheck=SceneTime()+OKSongCheckSeconds;if(!Why.IsEmpty()){G.Notice=Why;UE_LOG(LogTemp,Display,TEXT("Street song reset: %s"),*Why);}
 RefreshUI();
}

bool AOKStreetSocialScene::SongOwnsVoice(int32 Character) const{
 if(!State)return false;const auto& G=State->Song;
 return Character!=INDEX_NONE&&Character==G.Girl&&(G.Phase==EOKSongPhase::Intro||G.Phase==EOKSongPhase::Singing||G.Phase==EOKSongPhase::Outro);
}

// The rules' answer to "may a song be prepared now, by whom" (song_decide) over the heroines he has met.
TSharedPtr<FJsonObject> AOKStreetSocialScene::SongDecide(bool Override) const{
 const auto& S=*State;const auto& G=S.Song;auto Request=OKRulesRequest(TEXT("song_decide"));TArray<TSharedPtr<FJsonValue>> Heroines;
 for(const auto& R:S.Store.Relations)if(OKSongGirlOf(S,R.Id)!=INDEX_NONE)Heroines.Add(MakeShared<FJsonValueObject>(OKRulesRelationJson(R)));
 Request->SetArrayField(TEXT("heroines"),Heroines);
 auto Ask=MakeShared<FJsonObject>();const double Minutes=SceneTime()/60.0;Ask->SetNumberField(TEXT("played_minutes"),Minutes);Ask->SetNumberField(TEXT("day"),S.Store.CalendarDay);
 Ask->SetNumberField(TEXT("sung_today"),G.SungDay==S.Store.CalendarDay?G.SungToday:0);if(G.LastSongAtMinutes>=0)Ask->SetNumberField(TEXT("last_song_at"),G.LastSongAtMinutes);
 if(G.DeclinedAtMinutes>=0)Ask->SetNumberField(TEXT("declined_at"),G.DeclinedAtMinutes);Ask->SetBoolField(TEXT("event_active"),G.Phase!=EOKSongPhase::Idle);Ask->SetBoolField(TEXT("override_thresholds"),Override);
 Request->SetObjectField(TEXT("ask"),Ask);
 return OKRulesEval(S,Request);
}

// Queue her song at ok-song: lyrics for her stage and the hour (song_lyrics), her voice from her card / design.
bool AOKStreetSocialScene::SongPrepare(int32 Girl,bool Override,FString& Error){
 auto& S=*State;auto& G=S.Song;
 if(G.Endpoint.IsEmpty()){Error=TEXT("no ok-song service (OK_SONG_ENDPOINT)");return false;}
 const FOKStreetRelation* R=OKSongRelation(S,Girl);if(!R){Error=TEXT("he has not met her");return false;}
 if(G.Phase!=EOKSongPhase::Idle)SongReset(FString(),true);
 auto LyricsRequest=OKRulesRequest(TEXT("song_lyrics"));LyricsRequest->SetStringField(TEXT("stage"),R->Stage);LyricsRequest->SetNumberField(TEXT("hour"),int32((OKStreetMindNow(S)%1440)/60));LyricsRequest->SetNumberField(TEXT("day"),S.Store.CalendarDay);
 const auto Lyrics=OKRulesEval(S,LyricsRequest);if(!Lyrics.IsValid()){Error=TEXT("song_lyrics failed");return false;}
 ++G.Epoch;const uint32 Epoch=G.Epoch;
 G.Girl=Girl;G.Override=Override;G.HeroineId=R->Id;G.EventId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);G.RequestId=G.EventId;
 G.Template=Lyrics->GetStringField(TEXT("template"));G.Lyrics=Lyrics->GetStringField(TEXT("lyrics_display"));G.Phase=EOKSongPhase::Preparing;G.PhaseAt=SceneTime();G.JobStage=TEXT("submitting");
 auto Body=MakeShared<FJsonObject>();Body->SetNumberField(TEXT("schema"),1);Body->SetStringField(TEXT("request_id"),G.RequestId);Body->SetStringField(TEXT("save_id"),G.SaveId);
 Body->SetStringField(TEXT("event_id"),G.EventId);Body->SetNumberField(TEXT("generation"),Epoch);Body->SetStringField(TEXT("heroine_id"),R->Id);
 Body->SetStringField(TEXT("personality"),FString::FromInt(S.Manifest.Characters[Girl].PersonalityId));Body->SetNumberField(TEXT("age"),S.TtsAge);
 if(const FString Card=OKStreetCardFor(S,Girl);!Card.IsEmpty())Body->SetStringField(TEXT("card"),Card);
 Body->SetStringField(TEXT("lyrics_display"),G.Lyrics);Body->SetStringField(TEXT("lyrics_model"),Lyrics->GetStringField(TEXT("lyrics_model")));
 Body->SetArrayField(TEXT("tags"),Lyrics->GetArrayField(TEXT("tags")));Body->SetNumberField(TEXT("max_duration_ms"),Lyrics->GetNumberField(TEXT("max_duration_ms")));Body->SetNumberField(TEXT("recipe_version"),1);
 G.RequestPending=true;
 auto Request=OKVoicePickPost(G.Endpoint/TEXT("jobs"),Body,30.f);
 Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Epoch](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  if(!State||State->Song.Epoch!=Epoch)return;auto& L=State->Song;L.RequestPending=false;FString Err;
  const auto J=OKVoicePickAnswer(Response,Connected,Err);const TSharedPtr<FJsonObject>* Job=nullptr;
  if(!J||!J->TryGetObjectField(TEXT("job"),Job)){L.Phase=EOKSongPhase::Failed;L.PhaseAt=SceneTime();L.Notice=OKT(TEXT("ui.song.failed"))+Err;UE_LOG(LogTemp,Warning,TEXT("Street song: submit failed: %s"),*Err);RefreshUI();return;}
  (*Job)->TryGetStringField(TEXT("job_id"),L.JobId);(*Job)->TryGetStringField(TEXT("voice_tag"),L.VoiceTag);(*Job)->TryGetStringField(TEXT("artifact_key"),L.ArtifactKey);(*Job)->TryGetStringField(TEXT("status"),L.JobStage);
  UE_LOG(LogTemp,Display,TEXT("Street song: job %s for %s (voice %s, %s)"),*L.JobId,*L.HeroineId,*L.VoiceTag,*L.JobStage);
  L.NextCheck=0;SongSave();RefreshUI();
 });
 Request->ProcessRequest();
 UE_LOG(LogTemp,Display,TEXT("Street song: preparing %s for actor %d (%s, override=%d)"),*G.Template,Girl,*R->Id,Override?1:0);
 RefreshUI();return true;
}

// The job's status; when ready, her mouth envelope and the mix copied under the generated voice root (the runtime only
// plays admitted voices from there).
void AOKStreetSocialScene::SongPoll(){
 auto& S=*State;auto& G=S.Song;if(G.JobId.IsEmpty()||G.PollPending)return;
 const uint32 Epoch=G.Epoch;G.PollPending=true;
 auto Request=OKSongGet(G.Endpoint/TEXT("jobs")/G.JobId);
 Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Epoch](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  if(!State||State->Song.Epoch!=Epoch)return;auto& L=State->Song;L.PollPending=false;FString Err;
  const auto J=OKVoicePickAnswer(Response,Connected,Err);const TSharedPtr<FJsonObject>* Job=nullptr;
  if(!J||!J->TryGetObjectField(TEXT("job"),Job))return; // the service may be restarting: ask again later
  FString Status;(*Job)->TryGetStringField(TEXT("status"),Status);L.JobStage=Status;
  if(Status==TEXT("failed")||Status==TEXT("cancelled")){
   const TSharedPtr<FJsonObject>* E=nullptr;FString Code,Message;if((*Job)->TryGetObjectField(TEXT("error"),E)){(*E)->TryGetStringField(TEXT("code"),Code);(*E)->TryGetStringField(TEXT("message"),Message);}
   L.Phase=EOKSongPhase::Failed;L.PhaseAt=SceneTime();L.Notice=OKT(TEXT("ui.song.failed"))+Code;UE_LOG(LogTemp,Warning,TEXT("Street song: job %s %s: %s %s"),*L.JobId,*Status,*Code,*Message);RefreshUI();return;
  }
  if(Status!=TEXT("ready")){RefreshUI();return;}
  const TSharedPtr<FJsonObject>* Files=nullptr;FString Song,Mouth,Text;TSharedPtr<FJsonObject> Env;
  if(!(*Job)->TryGetObjectField(TEXT("files"),Files)||!(*Files)->TryGetStringField(TEXT("song"),Song)||!(*Files)->TryGetStringField(TEXT("mouth"),Mouth)||!FFileHelper::LoadFileToString(Text,*Mouth)
     ||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Env)||!Env.IsValid()){L.Phase=EOKSongPhase::Failed;L.Notice=FString(OKT(TEXT("ui.song.failed")))+TEXT("files");RefreshUI();return;}
  L.Mouth.Reset();for(const auto& V:Env->GetArrayField(TEXT("values")))L.Mouth.Add(float(V->AsNumber()));L.Hop=float(Env->GetNumberField(TEXT("hop_ms")))/1000.f;
  const FString Copy=State->TtsCacheDir/TEXT("songs")/(L.ArtifactKey.IsEmpty()?L.JobId:L.ArtifactKey)+TEXT(".ogg");
  if(!IFileManager::Get().FileExists(*Copy)&&IFileManager::Get().Copy(*Copy,*Song)!=COPY_OK){L.Phase=EOKSongPhase::Failed;L.Notice=FString(OKT(TEXT("ui.song.failed")))+TEXT("copy");RefreshUI();return;}
  L.SongPath=Copy;L.Phase=EOKSongPhase::Ready;L.PhaseAt=SceneTime();L.Notice.Reset();
  UE_LOG(LogTemp,Display,TEXT("Street song: ready %s (%d mouth values, %s)"),*L.SongPath,L.Mouth.Num(),*L.ArtifactKey);SongSave();RefreshUI();
 });
 Request->ProcessRequest();
}

// She says a line in her own voice (ok-tts with her card), then the phase moves on when it has been heard. A line TTS
// cannot make is skipped: the song never waits on it.
void AOKStreetSocialScene::SongSay(const FString& Text){
 auto& S=*State;auto& G=S.Song;G.SayPending=true;G.SpeechStarted=false;const uint32 Epoch=G.Epoch;const int32 Girl=G.Girl;
 auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("text"),Text);Body->SetStringField(TEXT("personality"),FString::FromInt(S.Manifest.Characters[Girl].PersonalityId));
 Body->SetNumberField(TEXT("age"),S.TtsAge);Body->SetStringField(TEXT("out_dir"),S.TtsCacheDir);if(const FString Card=OKStreetCardFor(S,Girl);!Card.IsEmpty())Body->SetStringField(TEXT("card"),Card);
 auto Request=OKVoicePickPost(S.TtsEndpoint/TEXT("synthesize"),Body,S.TtsTimeout);
 Request->OnProcessRequestComplete().BindWeakLambda(this,[this,Epoch,Girl](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  if(!State||State->Song.Epoch!=Epoch)return;auto& L=State->Song;L.SayPending=false;FString Err,Path,Id;
  const auto J=OKVoicePickAnswer(Response,Connected,Err);
  if(J&&J->TryGetStringField(TEXT("path"),Path)&&OKAdmitCrowdRuntimeVoice(this,Path,Id,Err)&&OKSpeakCrowdRuntimeLine(this,Girl,Id,Err))L.SpeechStarted=true;
  else UE_LOG(LogTemp,Warning,TEXT("Street song: her line was skipped: %s"),*Err);
 });
 Request->ProcessRequest();
}

// Nothing else wants her or him now: he is talking with her and no line, touch, call or event is running.
bool AOKStreetSocialScene::SongMayOffer(int32 Girl) const{
 const auto& S=*State;
 return S.Target==Girl&&S.Node==INDEX_NONE&&!S.CollectedActive&&!S.RelationshipPending&&!S.TouchActive&&!S.FreeTalkBusy&&S.RingGirl==INDEX_NONE&&!S.RemoteMode
  &&ContactReady(Girl)&&!OKCrowdRuntimeSpeechActive(const_cast<AOKStreetSocialScene*>(this),Girl)&&S.FreeTalkSpeechPlayed>=S.FreeTalkSpeechLines.Num();
}

void AOKStreetSocialScene::SongAnswer(const FString& Answer){
 auto& S=*State;auto& G=S.Song;if(G.Phase!=EOKSongPhase::Offered&&!(G.Phase==EOKSongPhase::Ready&&Answer==TEXT("accept")))return;
 if(Answer==TEXT("accept")){G.Phase=EOKSongPhase::Intro;G.PhaseAt=SceneTime();G.Notice.Reset();SongSay(OKT(TEXT("ui.song.intro_line")));OKSongCaption(S,OKT(TEXT("ui.song.intro_line")));UE_LOG(LogTemp,Display,TEXT("Street song: accepted (actor %d)"),G.Girl);}
 else if(Answer==TEXT("later")){G.Phase=EOKSongPhase::Ready;G.LaterUntil=SceneTime()+OKSongLaterSeconds;G.Notice=OKT(TEXT("ui.song.later_notice"));}
 else{G.Phase=EOKSongPhase::Ready;G.DeclinedAtMinutes=SceneTime()/60.0;G.LaterUntil=SceneTime()+20*60;G.Notice=OKT(TEXT("ui.song.declined_notice"));SongSave();}
 RefreshUI();
}

void AOKStreetSocialScene::SongSkip(){
 auto& S=*State;auto& G=S.Song;if(G.Phase!=EOKSongPhase::Singing&&G.Phase!=EOKSongPhase::Intro)return;
 FString Ignore;OKStopCrowdRuntimeSpeech(this,G.Girl,Ignore);G.Phase=EOKSongPhase::Outro;G.PhaseAt=SceneTime();SongSay(OKT(TEXT("ui.song.outro_line")));OKSongCaption(S,OKT(TEXT("ui.song.outro_line")));RefreshUI();
}

void AOKStreetSocialScene::TickSong(double Now){
 if(!State||State->SelfTest)return;auto& S=*State;auto& G=S.Song;
 if(!G.Loaded)SongLoad();
 if(G.Endpoint.IsEmpty())return;
 // The singer's actor went away, he left her, or a call came: stop what is playing; a made song stays for later.
 if((G.Phase==EOKSongPhase::Intro||G.Phase==EOKSongPhase::Singing||G.Phase==EOKSongPhase::Outro)&&(S.Target!=G.Girl||S.RingGirl!=INDEX_NONE||S.RemoteMode||!ContactReady(G.Girl))){
  FString Ignore;OKStopCrowdRuntimeSpeech(this,G.Girl,Ignore);G.SayPending=G.SpeechStarted=false;++G.Epoch;G.Phase=EOKSongPhase::Ready;G.LaterUntil=Now+60;G.Notice=OKT(TEXT("ui.song.interrupted"));
  UE_LOG(LogTemp,Display,TEXT("Street song: interrupted (target %d, ring %d, remote %d)"),S.Target,S.RingGirl,S.RemoteMode?1:0);RefreshUI();return;
 }
 switch(G.Phase){
 case EOKSongPhase::Idle:{
  if(!G.Enabled||Now<G.NextCheck)return;G.NextCheck=Now+OKSongCheckSeconds;
  const auto D=SongDecide(false);if(!D.IsValid())return;bool Ok=false;D->TryGetBoolField(TEXT("ok"),Ok);D->TryGetStringField(TEXT("reason"),G.LastReason);
  const TSharedPtr<FJsonObject>* Singer=nullptr;if(!Ok||!D->TryGetObjectField(TEXT("singer"),Singer))return;
  FString Id;(*Singer)->TryGetStringField(TEXT("id"),Id);const int32 Girl=OKSongGirlOf(S,Id);FString Error;
  if(Girl!=INDEX_NONE&&!SongPrepare(Girl,false,Error))UE_LOG(LogTemp,Warning,TEXT("Street song: %s"),*Error);
  return;}
 case EOKSongPhase::Preparing:
  if(!G.RequestPending&&Now>=G.NextCheck){G.NextCheck=Now+OKSongPollSeconds;SongPoll();}
  return;
 case EOKSongPhase::Ready:
  // Still the one he likes most? (Not asked for a test song.) If not, this song waits in the cache for her.
  if(!G.Override&&Now>=G.NextCheck){G.NextCheck=Now+OKSongCheckSeconds;const auto D=SongDecide(true);const TSharedPtr<FJsonObject>* Singer=nullptr;FString Id;
   if(D.IsValid()&&D->TryGetObjectField(TEXT("singer"),Singer)&&(*Singer)->TryGetStringField(TEXT("id"),Id)&&Id!=G.HeroineId){SongReset(OKT(TEXT("ui.song.singer_changed")),false);return;}}
  if(Now>=G.LaterUntil&&SongMayOffer(G.Girl)){G.Phase=EOKSongPhase::Offered;G.PhaseAt=Now;G.Notice.Reset();S.FreeTalkCaptionHer=OKMsg(TEXT("ui.song.offer"),{{TEXT("name"),*S.Manifest.Characters[G.Girl].Name}});UE_LOG(LogTemp,Display,TEXT("Street song: offered by actor %d"),G.Girl);RefreshUI();}
  return;
 case EOKSongPhase::Offered:
  if(S.Target!=G.Girl){G.Phase=EOKSongPhase::Ready;RefreshUI();}
  else if(Now-G.PhaseAt>OKSongOfferSeconds)SongAnswer(TEXT("later"));
  return;
 case EOKSongPhase::Intro:
 case EOKSongPhase::Outro:{
  // Her line is done (or was skipped, or never started within 20 s): on to the song / the end.
  const bool Done=!G.SayPending&&(!G.SpeechStarted||!OKCrowdRuntimeSpeechActive(this,G.Girl));
  if(!Done&&Now-G.PhaseAt<20)return;
  if(G.Phase==EOKSongPhase::Intro){
   FString Error,Id;G.SpeechStarted=false;
   if(!OKAdmitCrowdRuntimeVoice(this,G.SongPath,Id,Error)||!OKSpeakCrowdRuntimeSong(this,G.Girl,Id,G.Mouth,G.Hop,Error)){SongReset(OKT(TEXT("ui.song.failed"))+Error,false);return;}
   G.SongVoiceId=Id;G.Phase=EOKSongPhase::Singing;G.PhaseAt=Now;OKSongCaption(S,OKSongCaptionLyrics(G.Lyrics));UE_LOG(LogTemp,Display,TEXT("Street song: singing (actor %d, %s)"),G.Girl,*G.SongPath);RefreshUI();
  }else{
   G.SungToday=G.SungDay==S.Store.CalendarDay?G.SungToday+1:1;G.SungDay=S.Store.CalendarDay;G.LastSongAtMinutes=Now/60.0;SongSave();
   UE_LOG(LogTemp,Display,TEXT("Street song: completed (actor %d)"),G.Girl);SongReset(OKT(TEXT("ui.song.completed")),false);G.SongPath.Reset();G.ArtifactKey.Reset();
  }
  return;}
 case EOKSongPhase::Singing:
  // The runtime starts the decoded song a tick or two after the request; it is over once it has played and stopped.
  if(OKCrowdRuntimeSpeechActive(this,G.Girl)){G.SpeechStarted=true;return;}
  if(G.SpeechStarted||Now-G.PhaseAt>10){G.Phase=EOKSongPhase::Outro;G.PhaseAt=Now;SongSay(OKT(TEXT("ui.song.outro_line")));OKSongCaption(S,OKT(TEXT("ui.song.outro_line")));RefreshUI();}
  return;
 case EOKSongPhase::Failed:
  if(Now-G.PhaseAt>600)SongReset(FString(),false);
  return;
 }
}

// The song part of the right panel: the switch, what is being prepared, the offer and the lyrics while she sings.
void AOKStreetSocialScene::BuildSongRow(TSharedRef<SVerticalBox> Box){
 auto& S=*State;auto& G=S.Song;if(G.Endpoint.IsEmpty()||!S.TtsEnabled)return;const TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 const FString Name=S.Manifest.Characters.IsValidIndex(G.Girl)?S.Manifest.Characters[G.Girl].Name:FString();
 Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(G.Enabled?OKT(TEXT("ui.song.enabled")):OKT(TEXT("ui.song.disabled"))))
  .OnClicked_Lambda([Weak](){if(Weak.IsValid()){auto& L=Weak->State->Song;L.Enabled=!L.Enabled;L.NextCheck=0;Weak->SongSave();Weak->RefreshUI();}return FReply::Handled();})];
 FString Line;
 switch(G.Phase){
 case EOKSongPhase::Preparing:Line=OKMsg(TEXT("ui.song.preparing"),{{TEXT("name"),*Name},{TEXT("stage"),*G.JobStage}});break;
 case EOKSongPhase::Ready:Line=OKMsg(TEXT("ui.song.ready"),{{TEXT("name"),*Name}});break;
 case EOKSongPhase::Offered:Line=OKMsg(TEXT("ui.song.offer"),{{TEXT("name"),*Name}});break;
 case EOKSongPhase::Intro:case EOKSongPhase::Singing:case EOKSongPhase::Outro:Line=OKMsg(TEXT("ui.song.singing"),{{TEXT("name"),*Name}})+TEXT("\n")+G.Lyrics;break;
 default:break;
 }
 if(!Line.IsEmpty())Box->AddSlot().AutoHeight().Padding(8,2)[SNew(STextBlock).Text(FText::FromString(Line)).AutoWrapText(true).WrapTextAt(360)];
 if(G.Phase==EOKSongPhase::Offered){
  Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4,0)[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.song.accept")))).OnClicked_Lambda([Weak](){if(Weak.IsValid())Weak->SongAnswer(TEXT("accept"));return FReply::Handled();})]
   +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.song.later")))).OnClicked_Lambda([Weak](){if(Weak.IsValid())Weak->SongAnswer(TEXT("later"));return FReply::Handled();})]
   +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.song.decline")))).OnClicked_Lambda([Weak](){if(Weak.IsValid())Weak->SongAnswer(TEXT("decline"));return FReply::Handled();})]];
 }
 if(G.Phase==EOKSongPhase::Intro||G.Phase==EOKSongPhase::Singing)Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(4)[SNew(SButton).ButtonStyle(OKStreetGlassButton()).Text(FText::FromString(OKT(TEXT("ui.song.skip"))))
  .OnClicked_Lambda([Weak](){if(Weak.IsValid())Weak->SongSkip();return FReply::Handled();})];
 if(!G.Notice.IsEmpty())Box->AddSlot().AutoHeight().Padding(8,2)[SNew(STextBlock).Text(FText::FromString(G.Notice)).AutoWrapText(true).WrapTextAt(360)];
}

static const TCHAR* OKSongPhaseName(EOKSongPhase P){
 switch(P){case EOKSongPhase::Idle:return TEXT("idle");case EOKSongPhase::Preparing:return TEXT("preparing");case EOKSongPhase::Ready:return TEXT("ready");case EOKSongPhase::Offered:return TEXT("offered");
  case EOKSongPhase::Intro:return TEXT("intro");case EOKSongPhase::Singing:return TEXT("singing");case EOKSongPhase::Outro:return TEXT("outro");default:return TEXT("failed");}
}

// Control channel /song (development): state | prepare [girl] | play | accept | later | decline | skip | cancel | enable | disable.
// "prepare" with a girl is an override (any heroine, thresholds skipped) and says so; automatic songs never use it.
TSharedRef<FJsonObject> AOKStreetSocialScene::SongControl(const FString& Action,int32 Girl){
 auto& S=*State;auto& G=S.Song;if(!G.Loaded)SongLoad();auto O=MakeShared<FJsonObject>();FString Error;bool Ok=true;
 if(Action==TEXT("prepare")){
  int32 Who=Girl;if(Who<=0){const auto D=SongDecide(true);const TSharedPtr<FJsonObject>* Singer=nullptr;FString Id;if(D.IsValid()&&D->TryGetObjectField(TEXT("singer"),Singer)&&(*Singer)->TryGetStringField(TEXT("id"),Id))Who=OKSongGirlOf(S,Id);}
  Ok=Who>0&&SongPrepare(Who,Girl>0,Error);if(Who<=0)Error=TEXT("no singer");
 }
 else if(Action==TEXT("play")||Action==TEXT("accept")){Ok=G.Phase==EOKSongPhase::Ready||G.Phase==EOKSongPhase::Offered;if(Ok)SongAnswer(TEXT("accept"));else Error=TEXT("no ready song");}
 else if(Action==TEXT("later")||Action==TEXT("decline"))SongAnswer(Action);
 else if(Action==TEXT("skip"))SongSkip();
 else if(Action==TEXT("cancel"))SongReset(TEXT("cancelled"),true);
 else if(Action==TEXT("enable")||Action==TEXT("disable")){G.Enabled=Action==TEXT("enable");G.NextCheck=0;SongSave();}
 else if(Action!=TEXT("state")){Ok=false;Error=TEXT("unknown action");}
 const auto D=SongDecide(true);
 O->SetBoolField(TEXT("ok"),Ok);if(!Error.IsEmpty())O->SetStringField(TEXT("error"),Error);
 O->SetStringField(TEXT("phase"),OKSongPhaseName(G.Phase));O->SetBoolField(TEXT("enabled"),G.Enabled);O->SetBoolField(TEXT("override"),G.Override);O->SetStringField(TEXT("endpoint"),G.Endpoint);
 O->SetNumberField(TEXT("girl"),G.Girl);O->SetStringField(TEXT("heroine_id"),G.HeroineId);O->SetStringField(TEXT("voice_tag"),G.VoiceTag);O->SetStringField(TEXT("job_id"),G.JobId);O->SetStringField(TEXT("job_stage"),G.JobStage);
 O->SetStringField(TEXT("artifact_key"),G.ArtifactKey);O->SetStringField(TEXT("song"),G.SongPath);O->SetNumberField(TEXT("mouth_values"),G.Mouth.Num());O->SetStringField(TEXT("template"),G.Template);
 O->SetStringField(TEXT("notice"),G.Notice);O->SetStringField(TEXT("last_reason"),G.LastReason);O->SetStringField(TEXT("save_id"),G.SaveId);O->SetNumberField(TEXT("sung_today"),G.SungDay==S.Store.CalendarDay?G.SungToday:0);
 O->SetBoolField(TEXT("singing_audio"),G.Girl!=INDEX_NONE&&OKCrowdRuntimeSpeechActive(this,G.Girl));O->SetNumberField(TEXT("mouth"),G.Girl!=INDEX_NONE?OKCrowdRuntimeSpeechMouth(this,G.Girl):0);
 if(D.IsValid())O->SetObjectField(TEXT("decision"),D);
 return O;
}
