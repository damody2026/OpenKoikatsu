// Free-talk partner and hands-free ("即時對話") mode.
//
// The partner is whoever free talk is with: the conversation target, else the girl walking with the player,
// else the girl in the current activity or facility. Free talk stays open across those states (walking to an
// activity, the activity itself) and closes only when there is no partner left.
//
// Hands-free mode keeps the ok-asr microphone open (/listen/*): the server cuts utterances by voice activity
// and transcribes them; this polls the results and sends each one to the model, queueing while a reply is
// pending. Whenever she is speaking (free-talk lines, a scripted line, a replay) the server is muted so the
// speakers are never heard as the player.

int32 AOKStreetSocialScene::FreeTalkPartner() const{
 if(!State)return INDEX_NONE;const auto& S=*State;
 // On a call, or typing to her on the phone: she is the one he talks to (OKStreetSocialPhoneInbox.inl).
 if(S.RemoteMode!=0&&S.Manifest.Characters.IsValidIndex(S.RemotePeer))return S.RemotePeer;
 for(const int32 Girl:{S.Target,S.OutingGirl,S.Companion,S.Pair.Partner,S.Facility!=INDEX_NONE?S.FacilityCharacter:INDEX_NONE,S.RestroomGirl,S.SleepGirl,S.SoloGirl})
  if(Girl>0&&S.Manifest.Characters.IsValidIndex(Girl))return Girl;
 return INDEX_NONE;
}

void AOKStreetSocialScene::FreeTalkClose(){
 auto& S=*State;
 if(S.FreeTalkLive)FreeTalkLiveSet(false);
 CancelFreeTalkVoice();
 // Her lines not spoken yet are not wanted any more: the voice server stops working on them.
 if(S.FreeTalkSpeechPlayed<S.FreeTalkSpeechLines.Num())OKStreetCancelQueuedSpeech(S);
 S.FreeTalkOpen=false;S.FreeTalkNotice.Reset();S.FreeTalkReplay.Reset();S.FreeTalkReplayParts.Reset();S.FreeTalkReplayHold=0;
 S.FreeTalkSpeechPlayed=S.FreeTalkSpeechLines.Num();S.FreeTalkCaptionPlayer.Reset();S.FreeTalkCaptionHer.Reset();
 S.FreeTalkHistoryOpen=false;S.FreeTalkGirl=INDEX_NONE;
 if(S.FreeTalkMicOpen)FreeTalkMicToggle();
 RefreshUI();
}

static void OKStreetFreeTalkAsrPost(const FString& Path,const FString& Body,TFunction<void(bool,const FString&)> Done){
 auto Request=FHttpModule::Get().CreateRequest();
 Request->SetURL(OKStreetFreeTalkAsrEndpoint()/Path);Request->SetVerb(TEXT("POST"));
 Request->SetHeader(TEXT("Content-Type"),TEXT("application/json"));Request->SetContentAsString(Body);Request->SetTimeout(5.f);
 Request->OnProcessRequestComplete().BindLambda([Done](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  FString Text,Error;bool Speech=false;
  const bool Ok=Connected&&Response.IsValid()&&OKStreetFreeTalkParseAsr(Response->GetContentAsString(),Text,Speech,Error);
  if(!(Connected&&Response.IsValid()))OKStreetFreeTalkEnsureAsrServer();
  if(Done)Done(Ok,Connected&&Response.IsValid()?Error:OKT(TEXT("ui.live_talk.asr_no_response_open"))+OKStreetFreeTalkAsrEndpoint()+OKT(TEXT("ui.live_talk.close_paren_period")));
 });
 if(!Request->ProcessRequest()&&Done)Done(false,OKT(TEXT("ui.live_talk.cannot_create_asr_request")));
}

// Opens the hands-free microphone. A failure does not switch hands-free off: it retries every 2 s (starting
// the server if it is not answering) until it works or the player turns the mode off.
void AOKStreetSocialScene::FreeTalkLiveConnect(){
 TWeakPtr<FOKStreetSceneState> WeakState=State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 const auto S=WeakState.Pin();if(!S||!S->FreeTalkLive||S->FreeTalkLiveStarting)return;
 S->FreeTalkLiveStarting=true;
 OKStreetFreeTalkAsrPost(TEXT("listen/start"),*FString::Printf(TEXT("{\"script\":\"%s\"}"),*OKStreetCppText().Lang),[WeakState,Weak](bool Ok,const FString& Error){
  const auto L=WeakState.Pin();if(!L)return;
  L->FreeTalkLiveStarting=false;if(!L->FreeTalkLive)return;
  if(Ok){
   UE_LOG(LogTemp,Display,TEXT("Free talk live: microphone open"));
   L->FreeTalkLiveReady=true;L->FreeTalkLivePollFails=0;L->FreeTalkLiveNextPoll=0;L->FreeTalkLiveMuted=false;
   L->FreeTalkNotice=OKT(TEXT("ui.live_talk.live_talk_enabled"));
   OKStreetFreeTalkAsrPost(TEXT("listen/mute"),TEXT("{\"mute\":false}"),nullptr);
  }else{
   UE_LOG(LogTemp,Warning,TEXT("Free talk live: microphone not open (%s); retrying"),*Error);
   L->FreeTalkLiveReady=false;L->FreeTalkLiveRetryAt=FPlatformTime::Seconds()+2;
   L->FreeTalkNotice=OKT(TEXT("ui.live_talk.asr_retrying_open_paren"))+Error+OKT(TEXT("ui.finance_apps.close_paren"));
  }
  if(Weak.IsValid()&&Weak->State==L)Weak->RefreshUI();
 });
}

void AOKStreetSocialScene::FreeTalkLiveSet(bool On){
 auto& S=*State;
 if(On==S.FreeTalkLive)return;
 S.FreeTalkLive=On;S.FreeTalkLiveReady=false;S.FreeTalkHeard.Reset();S.FreeTalkLivePending.Reset();S.FreeTalkLiveSpeaking=false;S.FreeTalkLiveMuted=false;S.FreeTalkLiveHisMute=false;
 if(!On){if(!S.FreeTalkMicOpen)OKStreetFreeTalkAsrPost(TEXT("listen/stop"),TEXT("{}"),nullptr);RefreshUI();return;}
 if(S.FreeTalkListening)CancelFreeTalkVoice();
 // Results from before this switch-on are skipped: the first poll only records the newest id.
 S.FreeTalkLiveBaseline=true;S.FreeTalkLiveNextPoll=0;
 S.FreeTalkNotice=OKT(TEXT("ui.live_talk.opening_microphone"));
 // Polling starts once the microphone is open: a poll before that would read "not listening".
 S.FreeTalkLiveStarting=false;S.FreeTalkLivePollFails=0;
 S.FreeTalkSilenceSince=FPlatformTime::Seconds();S.FreeTalkLiveTickAt=S.FreeTalkSilenceSince;S.FreeTalkNudgeAt=S.FreeTalkSilenceSince+FMath::FRandRange(5.f,10.f)*S.FreeTalkSilenceScale;S.FreeTalkSilencePenalties=0;S.FreeTalkLeavePending=false;
 FreeTalkLiveConnect();
 RefreshUI();
}

void AOKStreetSocialScene::TickFreeTalkLive(double Now){
 auto& S=*State;
 const int32 Partner=FreeTalkPartner();
 if(Partner==INDEX_NONE){if(S.FreeTalkOpen||S.FreeTalkLive||S.FreeTalkGirl!=INDEX_NONE)FreeTalkClose();return;}
 if(S.FreeTalkGirl!=Partner){
  // Another girl: her captions and the previous girl's unspoken lines do not carry over.
  if(S.FreeTalkGirl!=INDEX_NONE){S.FreeTalkCaptionPlayer.Reset();S.FreeTalkCaptionHer.Reset();S.FreeTalkSpeechPlayed=S.FreeTalkSpeechLines.Num();}
  S.FreeTalkGirl=Partner;
 }
 // A topic he chose while her answer was on its way (Topic): asked once she has said it.
 if(!S.PendingTopic.IsEmpty()&&!S.FreeTalkBusy&&!FreeTalkHerVoiceBusy()){const FString Category=MoveTemp(S.PendingTopic);S.PendingTopic.Reset();Topic(Category);}
 if(!S.FreeTalkLive&&!S.FreeTalkMicOpen)return;
 if(S.FreeTalkLive){
 // Her actual audio, not the dialogue box: a scripted line whose completion is lost must not keep the mic muted.
 const bool Speaking=FreeTalkHerVoiceBusy(); // in a group, whichever girl is speaking
 // He talks over her (the server hears speech above the raised barge-in threshold): she stops, the rest of
 // her reply is dropped, and his words go to the model as usual.
 // Her song is a performance, not a reply: talking does not stop it (and the microphone is off while she sings).
 const bool Singing=SongOwnsVoice(Partner);
 if(Speaking&&!Singing&&S.FreeTalkLiveSpeaking&&S.FreeTalkLiveReady&&!S.ControlMicIgnored){
  FString Ignore;OKStopCrowdRuntimeSpeech(this,Partner,Ignore);for(const int32 Who:S.FreeTalkSpeechSpeakers)if(Who>0&&Who!=Partner)OKStopCrowdRuntimeSpeech(this,Who,Ignore);
  GalgameDropGroupLines(); // the group's lines not said yet are dropped too
  S.FreeTalkSpeechPlayed=S.FreeTalkSpeechLines.Num();S.FreeTalkReplay.Reset();S.FreeTalkReplayParts.Reset();S.FreeTalkReplayHold=0;
  for(const auto& Stream:S.FreeTalkSpeechStreams)if(Stream)Stream->Finish();
  if(S.Node!=INDEX_NONE)S.LineInterrupted=true; // a scripted line: it ends here and counts as said (FinishLine)
  UE_LOG(LogTemp,Display,TEXT("Free talk live: the player interrupted her"));S.LiveTestInterrupted=true;
  S.FreeTalkNotice=OKT(TEXT("ui.live_talk.you_interrupted_continue"));
 }
 const bool Talking=FreeTalkHerVoiceBusy();
 // His typed line spoken aloud (TickFreeTalkHisVoice): the microphone is off meanwhile, or it would take his own
 // voice from the speakers for something he said.
 // Her song likewise: through the speakers it is a voice too, and the ASR would take the lyrics for his words.
 const bool HisVoice=FreeTalkHisVoiceBusy()||Singing;
 if(Talking!=S.FreeTalkLiveMuted||HisVoice!=S.FreeTalkLiveHisMute){
  S.FreeTalkLiveMuted=Talking;S.FreeTalkLiveHisMute=HisVoice;
  UE_LOG(LogTemp,Display,TEXT("Free talk live: mic %s (audio=%d pending=%d/%d replay=%d)"),HisVoice?(Singing?TEXT("off while she sings"):TEXT("off while he speaks")):Talking?TEXT("ducked"):TEXT("open"),OKCrowdRuntimeSpeechActive(this,Partner)?1:0,S.FreeTalkSpeechPlayed,S.FreeTalkSpeechLines.Num(),S.FreeTalkReplay.IsEmpty()&&S.FreeTalkReplayParts.IsEmpty()?0:1);
  OKStreetFreeTalkAsrPost(TEXT("listen/mute"),HisVoice?TEXT("{\"mute\":true,\"duck\":false}"):Talking?TEXT("{\"mute\":false,\"duck\":true}"):TEXT("{\"mute\":false,\"duck\":false}"),nullptr);
 }
 // Silence: 5-10 s after she finishes she speaks up herself; every 30 s he stays silent costs 1 favor, and
 // after the third she says goodbye and leaves. Anything he says resets it.
 const double Wall=FPlatformTime::Seconds();
 if(S.FreeTalkLiveReady){
  if(S.FreeTalkLiveSpeaking||!S.FreeTalkLivePending.IsEmpty()){S.FreeTalkSilenceSince=Wall;S.FreeTalkSilencePenalties=0;}
  // Paused (street clock paused, or the microphone panel open) is waiting too: no silence penalty meanwhile.
  // Walking somewhere together (an outing on its way) is not an awkward silence: side by side, quiet is fine.
  // Texting her on the phone is not standing in front of her: a reply can take minutes, and she writes again on her own
  // (SelfText), not after seconds of "silence".
  // Her song (its line before it, the song, its line after) is not his silence: a nudge in its opening line had her say
  // "say something!" and the song was cut off after nine seconds (R3-233).
  const bool Waiting=Talking||Singing||S.FreeTalkBusy||S.RemoteMode==1||S.OutingGirl!=INDEX_NONE||!S.FreeTalkDraft.IsEmpty()||!S.FreeTalkLivePending.IsEmpty()||S.FreeTalkLiveSpeaking||S.ClockPaused||S.FreeTalkMicOpen||S.Bluff.Active||S.SoloGirl!=INDEX_NONE||(S.Pair.Partner==Partner&&(S.Pair.Read||[&S]{const FOKStreetActivity* A=S.ActId.IsEmpty()?nullptr:OKStreetFindActivity(S.ActId);
   // reading or studying side by side: the reading or study pose, or anything done in the library
   return A&&((A->Place&&FCString::Strcmp(A->Place,TEXT("library"))==0)||(A->FemaleClip&&(FCString::Strstr(A->FemaleClip,TEXT("read"))||FCString::Strstr(A->FemaleClip,TEXT("study")))));}()))||S.Table.ContainsByPredicate([](const FOKStreetDish& D){return D.Served&&D.Percent>0.f;})||Wall<S.FreeTalkQuietUntil; // playing cards (praying side by side, reading together, eating) is not silence, nor keeping quiet when she asked for it
  // Only time he could have answered counts: the clock stands still while she thinks or speaks.
  if(Waiting)S.FreeTalkSilenceSince+=FMath::Clamp(Wall-S.FreeTalkLiveTickAt,0.0,1.0);
  S.FreeTalkLiveTickAt=Wall;
  if(S.FreeTalkLeavePending){if(!Waiting)FreeTalkSilentLeave();}
  else if(Waiting)S.FreeTalkNudgeAt=FMath::Max(S.FreeTalkNudgeAt,Wall+FMath::FRandRange(5.f,10.f)*S.FreeTalkSilenceScale);
  else{
   if(Wall-S.FreeTalkSilenceSince>=30.0*S.FreeTalkSilenceScale*(S.FreeTalkSilencePenalties+1)){
    ++S.FreeTalkSilencePenalties;S.LiveTestPenalties=FMath::Max(S.LiveTestPenalties,S.FreeTalkSilencePenalties);
    ApplyFreeTalkFavor(Partner,-1,OKT(TEXT("ui.live_talk.player_silent")));
    UE_LOG(LogTemp,Display,TEXT("Free talk live: silent %.0f s, favor -1 (%d/3)"),Wall-S.FreeTalkSilenceSince,S.FreeTalkSilencePenalties);
    if(S.FreeTalkSilencePenalties>=3){S.FreeTalkLeavePending=true;FreeTalkSendNudge(true);return;}
   }
   if(Wall>=S.FreeTalkNudgeAt){S.FreeTalkNudgeAt=Wall+FMath::FRandRange(5.f,10.f)*S.FreeTalkSilenceScale;FreeTalkSendNudge(false);}
  }
 }
 }
 if(!S.FreeTalkBusy&&!S.FreeTalkLivePending.IsEmpty()){
  S.SystemNote=false;S.FreeTalkDraft=S.FreeTalkLivePending;S.FreeTalkLivePending.Reset();
  SendFreeTalk();
 }
 // Not open (failed start, server restarted, device lost): keep trying.
 if(S.FreeTalkLive&&!S.FreeTalkLiveReady&&!S.FreeTalkLiveStarting&&FPlatformTime::Seconds()>=S.FreeTalkLiveRetryAt)FreeTalkLiveConnect();
 if(S.FreeTalkLivePolling||Now<S.FreeTalkLiveNextPoll||(S.FreeTalkLive&&!S.FreeTalkLiveReady&&!S.FreeTalkMicOpen))return;
 S.FreeTalkLivePolling=true;S.FreeTalkLiveNextPoll=Now+.25;
 auto Request=FHttpModule::Get().CreateRequest();
 Request->SetURL(OKStreetFreeTalkAsrEndpoint()/FString::Printf(TEXT("listen/poll?after=%lld"),S.FreeTalkLiveAfter));Request->SetVerb(TEXT("GET"));Request->SetTimeout(3.f);
 TWeakPtr<FOKStreetSceneState> WeakState=State;
 Request->OnProcessRequestComplete().BindLambda([WeakState](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  const auto L=WeakState.Pin();if(!L)return;
  L->FreeTalkLivePolling=false;
  TSharedPtr<FJsonObject> Root;
  if(!L->FreeTalkLive&&!L->FreeTalkMicOpen)return;
  // The server stopped answering: after ~1 s of failed polls, reconnect (which starts a server if needed).
  if(!Connected||!Response.IsValid()){
   if(++L->FreeTalkLivePollFails>=4&&L->FreeTalkLive&&L->FreeTalkLiveReady){
    UE_LOG(LogTemp,Warning,TEXT("Free talk live: ok-asr stopped answering; reconnecting"));
    L->FreeTalkLiveReady=false;L->FreeTalkLiveRetryAt=0;L->FreeTalkNotice=OKT(TEXT("ui.live_talk.asr_server_restarting"));
    OKStreetFreeTalkEnsureAsrServer();
   }
   return;
  }
  L->FreeTalkLivePollFails=0;
  if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Root)||!Root.IsValid())return;
  bool Speaking=false,Listening=false;Root->TryGetBoolField(TEXT("speaking"),Speaking);Root->TryGetBoolField(TEXT("listening"),Listening);
  L->FreeTalkLiveSpeaking=Speaking;
  // The server's mute must follow ours (it outlives the game; a lost request would leave it stuck).
  bool ServerMuted=false,ServerDucked=false;Root->TryGetBoolField(TEXT("muted"),ServerMuted);Root->TryGetBoolField(TEXT("ducked"),ServerDucked);
  if(L->FreeTalkLive&&L->FreeTalkLiveReady&&(ServerMuted!=L->FreeTalkLiveHisMute||(!ServerMuted&&ServerDucked!=L->FreeTalkLiveMuted))){
   UE_LOG(LogTemp,Display,TEXT("Free talk live: server mute %d duck %d differs from game %d, resyncing"),ServerMuted?1:0,ServerDucked?1:0,L->FreeTalkLiveMuted?1:0);
   OKStreetFreeTalkAsrPost(TEXT("listen/mute"),L->FreeTalkLiveHisMute?TEXT("{\"mute\":true,\"duck\":false}"):L->FreeTalkLiveMuted?TEXT("{\"mute\":false,\"duck\":true}"):TEXT("{\"mute\":false,\"duck\":false}"),nullptr);
  }
  double Level=0;Root->TryGetNumberField(TEXT("level"),Level);L->FreeTalkMicLevel=(float)Level;
  // The microphone closed under us (server restarted, device switched): reopen it instead of giving up.
  if(!Listening){
   // Only the level meter wants the microphone: reopen it too, the panel is useless on a dead stream.
   if(L->FreeTalkMicOpen&&!L->FreeTalkLive)OKStreetFreeTalkAsrPost(TEXT("listen/start"),TEXT("{}"),nullptr);
   if(L->FreeTalkLive&&L->FreeTalkLiveReady){
    UE_LOG(LogTemp,Warning,TEXT("Free talk live: ok-asr is not listening; reopening the microphone"));
    L->FreeTalkLiveReady=false;L->FreeTalkLiveRetryAt=0;L->FreeTalkNotice=OKT(TEXT("ui.live_talk.microphone_reopening"));
   }
   return;
  }
  const TArray<TSharedPtr<FJsonValue>>* Results=nullptr;
  if(!Root->TryGetArrayField(TEXT("results"),Results))return;
  for(const auto& Value:*Results){
   const TSharedPtr<FJsonObject> R=Value.IsValid()?Value->AsObject():nullptr;double Id=0;FString Text;
   if(!R.IsValid()||!R->TryGetNumberField(TEXT("id"),Id)||(int64)Id<=L->FreeTalkLiveAfter)continue;
   L->FreeTalkLiveAfter=(int64)Id;
   if(L->FreeTalkLiveBaseline||!L->FreeTalkLive||!R->TryGetStringField(TEXT("text"),Text))continue;
   Text.TrimStartAndEndInline();
   if(L->ControlMicIgnored){if(!Text.IsEmpty())UE_LOG(LogTemp,Display,TEXT("Free talk live: ignored (control mic off): %s"),*Text);continue;}
   if(!Text.IsEmpty())L->FreeTalkHeard=Text;
   if(!Text.IsEmpty())L->FreeTalkLivePending=L->FreeTalkLivePending.IsEmpty()?Text:L->FreeTalkLivePending+TEXT(" ")+Text;
  }
  L->FreeTalkLiveBaseline=false;
 });
 if(!Request->ProcessRequest())S.FreeTalkLivePolling=false;
}

// Behaviour trace of the free-talk partner, for finding out why she does something odd (a pose left over, drifting,
// walking off). One "Street talk trace" line whenever any of her state changes, and a heartbeat every second while
// she moves (every 5 s while she stands): position, speed, what she is bound to, the action clip, her voice.
void AOKStreetSocialScene::TickFreeTalkTrace(double Now){
 auto& S=*State;
 // The screen a moment after each decision of hers (queued by ApplyChoiceReply), saved beside the game log.
 if(S.DecisionShotAt>0&&FPlatformTime::Seconds()>=S.DecisionShotAt){
  S.DecisionShotAt=0;FString Tag=S.DecisionShotTag;for(TCHAR& C:Tag)if(!FChar::IsAlnum(C)&&C!='_')C='_';
  const FString Dir=FPaths::GetPath(S.Manifest.SocialReport)/TEXT("decisions");IFileManager::Get().MakeDirectory(*Dir,true);
  const FString File=Dir/FString::Printf(TEXT("decision.%03d.%s.png"),++S.DecisionShots,*Tag);
  {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TraceShot);FScreenshotRequest::RequestScreenshot(File,true,false);}
  UE_LOG(LogTemp,Display,TEXT("Street decision shot: %s"),*File);
 }
 // Anyone else standing inside him or inside the girl with him (her plan's spot at the restaurant was his chair; at the
 // café another girl stood in his date's seat): stepped out too. Twice a second is enough.
 if(Now>=S.OverlapCheckAt){S.OverlapCheckAt=Now+0.5;QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TraceOverlap);
  TArray<FVector> Taken={S.Male};
  for(const int32 G2:{FreeTalkPartner(),S.Pair.Partner})if(S.Agents.IsValidIndex(G2-1))Taken.AddUnique(S.Agents[G2-1].Position);
  for(int32 I=1;I<=S.Agents.Num();++I){if(I==FreeTalkPartner())continue;auto& A=S.Agents[I-1];
   const bool With=S.Pair.Partner==I||(S.Facility!=INDEX_NONE&&S.FacilityCharacter==I)||S.PrivateGirl==I||S.SleepGirl==I||S.SleepGirl2==I||S.SoloGirl==I||S.LoverPose.Girl==I||S.LoverReact.Girl==I||S.RestroomGirl==I||S.NpcLifts.Contains(I)||S.Party.Contains(I);
   if(With)continue;
   const FVector* In=Taken.FindByPredicate([&](const FVector& T){return FVector::Dist2D(A.Position,T)<45.f&&FMath::Abs(A.Position.Z-T.Z)<100.f;});if(!In)continue;
   const FVector From=*In;FVector Away=A.Position-From;Away.Z=0;if(Away.Size2D()<1.f)Away=FRotator(0,S.Rotations[0].Yaw+90.f,0).Vector();Away.Normalize();
   QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_TraceOverlapMove);
   for(const float D:{110.f,160.f,220.f}){FVector To=From+Away*D;To.Z=A.Position.Z;
    if(!OKStreetBlockedPoint(To,28)&&!Taken.ContainsByPredicate([&](const FVector& T){return FVector::Dist2D(To,T)<45.f;})){A.Position=To;UE_LOG(LogTemp,Display,TEXT("Street overlap fixed: actor %d (not with him) moved out of %s"),I,From==S.Male?TEXT("the player"):TEXT("his partner"));break;}}}}
 const int32 G=FreeTalkPartner();
 if(G==INDEX_NONE||!S.Agents.IsValidIndex(G-1)||!(S.FreeTalkOpen||S.FreeTalkLive||S.Target==G)){S.TraceGirl=INDEX_NONE;S.TraceSig.Reset();return;}
 // Standing inside him (an activity that ended or changed under them): step her out to a talking distance.
 {
  auto& Mine=S.Agents[G-1];
  const bool Together=S.Pair.Partner==G||(S.Facility!=INDEX_NONE&&S.FacilityCharacter==G)||S.PrivateGirl==G||S.SleepGirl==G||S.SleepGirl2==G||S.SoloGirl==G||S.LoverPose.Girl==G||S.LoverReact.Girl==G||S.RestroomGirl==G||S.NpcLifts.Contains(G);
  if(!Together&&FVector::Dist2D(Mine.Position,S.Male)<45.f&&FMath::Abs(Mine.Position.Z-S.Male.Z)<100.f){
   FVector Away=Mine.Position-S.Male;Away.Z=0;if(Away.Size2D()<1.f)Away=FRotator(0,S.Rotations[0].Yaw,0).RotateVector(FVector(0,1,0));Away.Normalize();
   FVector To=S.Male+Away*110.f;To.Z=Mine.Position.Z;
   if(!OKStreetBlockedPoint(To,28)){Mine.Position=To;UE_LOG(LogTemp,Display,TEXT("Street overlap fixed: actor %d moved out of the player"),G);}
  }
 }
 const auto& A=S.Agents[G-1];const double Wall=FPlatformTime::Seconds();
 FOKRuntimeActionStreamStatus Act;const bool HasAct=OKGetCrowdRuntimeActionStatus(this,G,Act);
 TArray<FString> Flags;
 if(S.Target==G)Flags.Add(TEXT("target"));if(S.Companion==G)Flags.Add(TEXT("companion"));if(S.OutingGirl==G)Flags.Add(TEXT("outing"));if(S.Pair.Partner==G)Flags.Add(TEXT("pair"));
 if(S.Facility!=INDEX_NONE&&S.FacilityCharacter==G)Flags.Add(TEXT("facility"));if(S.PrivateGirl==G)Flags.Add(TEXT("private"));if(S.SoloGirl==G)Flags.Add(TEXT("solo"));
 if(S.AmbientActor==G)Flags.Add(TEXT("ambient"));if(InLifePose(G))Flags.Add(TEXT("lifepose"));if(S.Commutes.Contains(G))Flags.Add(TEXT("commute"));if(S.ScheduleHeld.Contains(G))Flags.Add(TEXT("scheduled"));
 if(S.LoverPose.Girl==G)Flags.Add(TEXT("loverpose"));if(S.LoverReact.Girl==G)Flags.Add(TEXT("loverreact"));if(S.NavGirl==G)Flags.Add(TEXT("nav"));if(A.Held)Flags.Add(TEXT("held"));
 const bool Speaking=OKCrowdRuntimeSpeechActive(this,G);
 const FString Sig=FString::Printf(TEXT("%s|%s|%d|%d|%d"),*FString::Join(Flags,TEXT(",")),HasAct?*Act.ClipId:TEXT("-"),HasAct&&Act.Playing?1:0,int32(A.Mode),Speaking?1:0);
 const double Moved=S.TraceGirl==G?FVector::Dist(S.TracePos,A.Position):0.0;
 const bool Changed=S.TraceGirl!=G||Sig!=S.TraceSig;
 const double Interval=Moved>3.0?1.0:5.0;
 if(!Changed&&Wall-S.TraceAt<Interval)return;
 UE_LOG(LogTemp,Display,TEXT("Street talk trace: actor=%d %s flags=[%s] clip=%s playing=%d mode=%d dwell=%.1f pos=(%.0f,%.0f,%.0f) moved=%.0f yaw=%.0f dist_to_player=%.0f speaking=%d live=%d busy=%d t=%.1f"),
  G,Changed?TEXT("CHANGE"):TEXT("tick"),*FString::Join(Flags,TEXT(",")),HasAct?*Act.ClipId:TEXT("-"),HasAct&&Act.Playing?1:0,int32(A.Mode),A.Dwell,A.Position.X,A.Position.Y,A.Position.Z,Moved,
  S.Rotations.IsValidIndex(G)?S.Rotations[G].Yaw:0.f,FVector::Dist2D(A.Position,S.Male),Speaking?1:0,S.FreeTalkLive?1:0,S.FreeTalkBusy?1:0,Now);
 S.TraceGirl=G;S.TraceSig=Sig;S.TraceAt=Wall;S.TracePos=A.Position;
}

void AOKStreetSocialScene::FreeTalkSendNudge(bool Leaving){
 auto& S=*State;if(S.FreeTalkBusy)return;
 S.FreeTalkDraft=OKStreetFreeTalkNudge(FMath::RoundToInt(FPlatformTime::Seconds()-S.FreeTalkSilenceSince),Leaving);
 UE_LOG(LogTemp,Display,TEXT("Free talk live: player silent, she speaks up%s"),Leaving?TEXT(" and leaves"):TEXT(""));++S.LiveTestNudges;
 SendFreeTalk();
}

// After her goodbye: hands-free off, the activity and the talk end, and she walks off.
void AOKStreetSocialScene::FreeTalkLeave(const FString& Message){
 auto& S=*State;const int32 Girl=FreeTalkPartner();S.FreeTalkLeavePending=false;S.FarewellPending=false;S.FarewellAsked=false;S.FarewellFromTalk=false;S.FreeTalkLeftAt=FPlatformTime::Seconds();
 FreeTalkLiveSet(false);
 if(Girl!=INDEX_NONE){
  S.PartedAt.Add(Girl,FPlatformTime::Seconds());
  {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_LeaveParty);PartyLeave(Girl,TEXT("she said goodbye"));} // a goodbye ends following him too
  if(S.Pair.Partner==Girl){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_LeavePhoto);FinishPhoto(false);}
  if(S.Facility!=INDEX_NONE&&S.FacilityCharacter==Girl){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_LeaveFacility);FinishFacility();}
  if(S.OutingGirl==Girl){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_LeaveOuting);CancelOuting(FString());}
  if(S.Companion==Girl){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_LeaveCompanion);FinishCompanion();}
  if(S.Target==Girl){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_LeaveEndTalk);EndTalk();}
  if(S.Agents.IsValidIndex(Girl-1)){auto& Agent=S.Agents[Girl-1];Agent.Held=false;Agent.Mode=EOKStreetMove::Walking;Agent.Stuck=0;}
 }
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_LeaveClose);FreeTalkClose();}
 S.Message=Message;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_LeaveUi);RefreshUI();}
 UE_LOG(LogTemp,Display,TEXT("Free talk: left (%s)"),*Message);
}

// She felt ignored: the same, with the silence counted as the cause.
void AOKStreetSocialScene::FreeTalkSilentLeave(){
 FreeTalkLeave(OKT(TEXT("ui.live_talk.she_left_neglected")));State->LiveTestLeft=true;
 UE_LOG(LogTemp,Display,TEXT("Free talk live: she left after the player stayed silent"));
}

// While they talk face to face she keeps facing him, and he her: the direction is set every frame (not once when the
// conversation opens, when she may still be mid-stride or turned away), and her body turns toward it at its own speed.
void AOKStreetSocialScene::TickFaceTalkPartner(bool Late){
 auto& S=*State;const int32 G=S.Target;
 if(G==INDEX_NONE||!S.Agents.IsValidIndex(G-1)||!S.Rotations.IsValidIndex(G)){if(!Late&&S.HeadLookGirl!=INDEX_NONE){OKSetCrowdRuntimeHeadLook(this,S.HeadLookGirl,false,FVector::ZeroVector);S.HeadLookGirl=INDEX_NONE;}return;}
 // Her own activity decides her direction in these states. When she is off by more than 30 degrees while one of them holds,
 // the reason is logged once in a while, so a wrong direction can be traced.
 const TCHAR* Skip=S.Pair.Partner!=INDEX_NONE?TEXT("pair"):S.Facility!=INDEX_NONE?TEXT("facility"):S.PrivateGirl!=INDEX_NONE?TEXT("private"):S.LoverPose.Girl==G?TEXT("lover pose"):S.LoverReact.Girl==G?TEXT("lover reaction"):
  (S.SleepGirl==G||S.SleepGirl2==G)?TEXT("sleep"):S.SoloGirl==G?TEXT("solo activity"):S.RestroomGirl==G?TEXT("restroom"):S.NpcLifts.Contains(G)?TEXT("lift"):nullptr;
 const FVector To=S.Male-S.Agents[G-1].Position;
 // Face to face she also looks him in the eye: her head turns up to his (a shorter girl's idle clip looks ahead or down
 // at his chest), over whatever clip she plays. Not while her own activity holds her (Skip), and she lets go when the
 // talk ends.
 if(!Late){
  const int32 Look=!Skip&&To.Size2D()>=20.f&&To.Size2D()<400.f?G:INDEX_NONE;
  if(S.HeadLookGirl!=INDEX_NONE&&S.HeadLookGirl!=Look)OKSetCrowdRuntimeHeadLook(this,S.HeadLookGirl,false,FVector::ZeroVector);
  S.HeadLookGirl=Look;
  if(Look!=INDEX_NONE){FVector Eyes;if(OKGetCrowdRuntimeBonePosition(this,0,FName(TEXT("cm_j_head")),Eyes))Eyes.Z+=6.f;else Eyes=S.Male+FVector(0,0,155);OKSetCrowdRuntimeHeadLook(this,Look,true,Eyes);}
 }
 if(Skip||To.Size2D()<20.f){
  if(Late&&FMath::Abs(FMath::FindDeltaAngleDegrees(S.RenderedRotations[G].Yaw,OKStreetFacingMovement(To,S.Rotations[G]).Yaw))>30.f&&FPlatformTime::Seconds()>S.FaceLogAt){
   S.FaceLogAt=FPlatformTime::Seconds()+3.0;
   UE_LOG(LogTemp,Display,TEXT("Street facing: actor %d is not turned to him (%s) body %.0f toward him %.0f dist=%.0f partner=%d pair=%d facility=%d"),G,Skip?Skip:TEXT("too close"),S.RenderedRotations[G].Yaw,OKStreetFacingMovement(To,S.Rotations[G]).Yaw,To.Size2D(),FreeTalkPartner(),S.Pair.Partner,S.Facility);
  }
  return;
 }
 // Late call: did another system change her direction since the early call of this frame?
 if(Late&&FMath::Abs(FMath::FindDeltaAngleDegrees(S.Rotations[G].Yaw,float(S.FaceSetYaw)))>1.f&&FPlatformTime::Seconds()>S.FaceLogAt){
  S.FaceLogAt=FPlatformTime::Seconds()+2.0;
  UE_LOG(LogTemp,Display,TEXT("Street facing: actor %d direction was changed by another system after the face-the-partner step (%.0f -> %.0f); set again (lover pose=%d react=%d life=%d commute=%d companion=%d)"),G,float(S.FaceSetYaw),S.Rotations[G].Yaw,S.LoverPose.Girl==G?1:0,S.LoverReact.Girl==G?1:0,InLifePose(G)?1:0,S.Commutes.Contains(G)?1:0,S.Companion==G?1:0);
 }
 S.Rotations[G]=OKStreetFacingMovement(To,S.Rotations[G]);
 S.Rotations[0]=OKStreetFacingMovement(-To,S.Rotations[0]);
 S.FaceSetYaw=S.Rotations[G].Yaw;
 // Her body follows that direction through the limited turn (Tick). If it is still off after 1.5 s, something is holding
 // it (a turn that never completes): put it there at once, and log how far off it was.
 const double Wall=FPlatformTime::Seconds();
 const float Off=FMath::Abs(FMath::FindDeltaAngleDegrees(S.RenderedRotations[G].Yaw,S.Rotations[G].Yaw));
 if(Off>20.f){
  if(S.FaceOffSince==0)S.FaceOffSince=Wall;
  else if(Wall-S.FaceOffSince>1.5){
   UE_LOG(LogTemp,Display,TEXT("Street facing: actor %d was %.0f degrees off for %.1f s (body %.0f, wanted %.0f, him %.0f); turned at once"),G,Off,Wall-S.FaceOffSince,S.RenderedRotations[G].Yaw,S.Rotations[G].Yaw,S.Rotations[0].Yaw);
   S.RenderedRotations[G]=S.Rotations[G];S.FaceOffSince=0;
  }
 }else S.FaceOffSince=0;
}

// Whenever she is with him (a walk, an outing, a meal, or he spoke to her) the free talk and the microphone are open. A
// conversation that ended (goodbye, aborted greeting) and was followed by something she does with him (she agrees to a
// drink, a walk) otherwise left them talking with a dead microphone. Not right after a goodbye, and never against the
// player's own "close free talk" button.
void AOKStreetSocialScene::TickEnsureFreeTalk(){
 auto& S=*State;const int32 G=FreeTalkPartner();
 if(G==INDEX_NONE){S.FreeTalkUserClosed=false;return;}
 if(S.SelfTest||S.FreeTalkLive||S.FreeTalkUserClosed||S.FarewellPending||S.FreeTalkMicOpen)return;
 if(FPlatformTime::Seconds()<S.FreeTalkLeftAt+4.0)return;
 S.FreeTalkOpen=true;FreeTalkLiveSet(true);
 UE_LOG(LogTemp,Display,TEXT("Street free talk: opened again by itself, she is with him (partner=%d target=%d companion=%d outing=%d pair=%d)"),G,S.Target,S.Companion,S.OutingGirl,S.Pair.Partner);
}

// What they agreed to do once this is over (the judge's then): when her activity has ended (a meal: its table eaten
// clean, or everyone full), nothing they ordered is still coming and the talk is free, they go. Given up after two game
// hours, or when she is no longer the one he talks to.
void AOKStreetSocialScene::TickThenAction(){
 auto& S=*State;if(S.ThenAction.IsEmpty())return;const int32 Girl=S.ThenGirl;
 if(OKStreetMindNow(S)>S.ThenUntil||Girl==INDEX_NONE||FreeTalkPartner()!=Girl){UE_LOG(LogTemp,Display,TEXT("Street then: %s for actor %d dropped (%s)"),*S.ThenAction,Girl,FreeTalkPartner()!=Girl?TEXT("they parted"):TEXT("too long"));S.ThenAction.Reset();return;}
 // Anything they sat down to (a meal, a drink, coffee and cake at the cafe) is done once what they ordered in it is eaten.
 const bool MealDone=S.Pair.Partner==Girl&&S.TableClearedAt>=S.Pair.PlayingAt&&S.Pair.Phase==1;
 if((S.Pair.Partner==Girl&&!MealDone)||(S.Facility!=INDEX_NONE&&S.FacilityCharacter==Girl)||S.SoloGirl==Girl||S.OutingGirl==Girl||!S.RetryAction.IsEmpty()||S.FreeTalkBusy)return;
 if(OKStreetConversationBusy(S.Node!=INDEX_NONE,S.CollectedActive,S.RelationshipPending,S.TouchActive))return;
 if(S.Table.ContainsByPredicate([Girl](const FOKStreetDish& D){return !D.Served&&(!D.ForHer||D.Her==INDEX_NONE||D.Her==Girl);}))return;
 // Eating where they stand (the convenience store, no seat): what is on the table is eaten first ("once we have eaten
 // I'll walk you home" left with her rice ball half eaten), unless nobody can eat any more of it (TickMeal keeps
 // TableClearedAt current while the rest is left over).
 if(S.Table.ContainsByPredicate([Girl](const FOKStreetDish& D){return D.Served&&D.Percent>0.f&&(!D.ForHer||D.Her==INDEX_NONE||D.Her==Girl);})&&SceneTime()-S.TableClearedAt>1.0)return;
 // Arrived where they went first (a walk in the park, then lunch): a while there before going on.
 if(S.ThenStayMinutes>0){if(S.ThenReadyAt<=0){S.ThenReadyAt=OKStreetMindNow(S)+S.ThenStayMinutes;S.ThenUntil=FMath::Max(S.ThenUntil,S.ThenReadyAt+60);UE_LOG(LogTemp,Display,TEXT("Street then: %s for actor %d after %lld minutes here"),*S.ThenAction,Girl,S.ThenStayMinutes);}
  if(OKStreetMindNow(S)<S.ThenReadyAt)return;}
 FString Error;const FString Action=S.ThenAction;S.ThenAction.Reset();
 const bool Started=BeginOuting(Girl,Action,Error);
 UE_LOG(LogTemp,Display,TEXT("Street then: %s for actor %d %s%s"),*Action,Girl,Started?TEXT("started"):TEXT("refused: "),*Error);
 if(!Started){S.FreeTalkNotice=OKT(TEXT("ui.live_talk.she_agreed_but"))+Error;S.TellHer.Add(OKMsg(TEXT("ui.live_talk.promise_failed_explain"),{{TEXT("action"),*OKStreetActivityName(Action)},{TEXT("place"),TEXT("none")},{TEXT("error"),*Error}}));}
}

// An outing she agreed to but could not start because her scripted line was still playing starts when it is over.
void AOKStreetSocialScene::TickRetryAction(){
 auto& S=*State;if(S.RetryAction.IsEmpty())return;
 const double Now=SceneTime();
 if(Now>S.RetryUntil||S.RetryGirl==INDEX_NONE||FreeTalkPartner()!=S.RetryGirl){S.RetryAction.Reset();return;}
 if(OKStreetConversationBusy(S.Node!=INDEX_NONE,S.CollectedActive,S.RelationshipPending,S.TouchActive))return;
 // Waiting for what they ordered (BeginOuting's wait_for_order): not before it is on the table.
 const int32 Girl=S.RetryGirl;if(S.Table.ContainsByPredicate([Girl](const FOKStreetDish& D){return !D.Served&&(!D.ForHer||D.Her==INDEX_NONE||D.Her==Girl);}))return;
 FString Error;const FString Action=S.RetryAction;S.RetryAction.Reset();
 const bool Started=BeginOuting(S.RetryGirl,Action,Error);
 UE_LOG(LogTemp,Display,TEXT("Street free talk action: %s retried after her line: %s%s"),*Action,Started?TEXT("started"):TEXT("refused: "),*Error);
 if(!Started){
  S.FreeTalkNotice=OKT(TEXT("ui.live_talk.she_agreed_but"))+Error;
  S.TellHer.Add(OKMsg(TEXT("ui.live_talk.promise_failed_explain"), {{TEXT("action"), *Action}, {TEXT("error"), *Error}}));
 }
}

// They said goodbye: once she has finished speaking, the conversation ends and she goes her way.
void AOKStreetSocialScene::TickFarewell(){
 auto& S=*State;
 // Goodbye said with her drink still in her hand (Alice at her dorm's vending machine): once it is finished, or nobody can
 // eat more, the goodbye goes on. It waited for the meal and then never came: the two stood there talking.
 if(S.FarewellAfterMeal!=INDEX_NONE&&!S.FarewellPending){const int32 G=S.FarewellAfterMeal;
  if(FreeTalkPartner()!=G)S.FarewellAfterMeal=INDEX_NONE;
  else if(!S.Table.ContainsByPredicate([G](const FOKStreetDish& D){return D.Her==G&&D.ForHer&&D.Percent>0.f;})||S.TableClearedAt>=S.FarewellAfterMealAt){
   S.FarewellAfterMeal=INDEX_NONE;S.FarewellPending=true;S.FarewellFromTalk=true;S.FarewellAt=FPlatformTime::Seconds();
   UE_LOG(LogTemp,Display,TEXT("Street farewell: actor=%d finished eating; the goodbye goes on"),G);}}
 if(!S.FarewellPending)return;
 const int32 G=FreeTalkPartner();if(G==INDEX_NONE){S.FarewellPending=false;return;}
 const double Wall=FPlatformTime::Seconds();
 const bool Voicing=OKCrowdRuntimeSpeechActive(this,G)||!S.FreeTalkReplay.IsEmpty()||!S.FreeTalkReplayParts.IsEmpty();
 const bool Queued=S.FreeTalkSpeechPlayed<S.FreeTalkSpeechLines.Num();
 // Her goodbye is on screen already: lines the voice server has not delivered yet (a busy GPU, 15 s timeouts per line,
 // and Yuuka stood there 50 s after "take care") do not hold the parting past the time it takes to read them. A line
 // being spoken is let finish, and so are the parts that follow it while they keep coming (gaps under 3 s): the voice
 // that started 7.8 s late had her walk off after its first part, three parts of her goodbye unsaid.
 if(Voicing||S.FarewellVoiceAt<S.FarewellAt)S.FarewellVoiceAt=Voicing?Wall:S.FarewellAt-1000.0;
 int32 Chars=0;for(const FString& Line:S.FreeTalkSpeechLines)Chars+=Line.Len();
 const bool VoiceLate=Queued&&!Voicing&&Wall>S.FarewellAt+4.0+0.2*Chars&&Wall>S.FarewellVoiceAt+3.0;
 if(Wall<S.FarewellAt+1.5||Voicing||(Queued&&!VoiceLate)||S.FreeTalkBusy)return;
 if(VoiceLate)UE_LOG(LogTemp,Display,TEXT("Street farewell: actor=%d parts without the voice of %d line(s) not delivered after %.1f s"),G,S.FreeTalkSpeechLines.Num()-S.FreeTalkSpeechPlayed,Wall-S.FarewellAt);
 FreeTalkLeave(OKT(TEXT("ui.live_talk.parted_she_left")));
}

// Development: -StreetSocialLiveTalkTest=<wav> [-StreetSocialSilenceScale=0.2] runs hands-free end to end against
// the local ok-asr / ok-tts servers and DeepSeek. Part 1 (girl 1): stay silent until she speaks up, loses favor
// three times and leaves. Part 2 (girl 2): say the wav (ok-asr /listen/inject), wait for her spoken answer, say it
// again over her and expect the interruption. Results go to the log ("LIVE TALK TEST"), then the game quits.
void AOKStreetSocialScene::TickLiveTalkTest(double Now){
 auto& S=*State;if(S.LiveTestPhase<0||!S.AllReady)return;
 const double Wall=FPlatformTime::Seconds();
 auto Inject=[&](){
  auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("path"),S.LiveTestWav);FString Payload;FJsonSerializer::Serialize(Body,TJsonWriterFactory<>::Create(&Payload));
  OKStreetFreeTalkAsrPost(TEXT("listen/inject"),Payload,nullptr);UE_LOG(LogTemp,Display,TEXT("LIVE TALK TEST: player speaks (%s)"),*S.LiveTestWav);
 };
 auto Begin=[&](int32 Girl){
  S.Agents[Girl-1].Position=S.Male+FVector(150,0,0);S.Agents[Girl-1].Held=true;
  S.FreeTalkSpeechLines.Reset();S.FreeTalkSpeechPlayed=0;S.QuietTalk=true;BeginTalk(Girl);S.QuietTalk=false;S.FreeTalkOpen=true;FreeTalkLiveSet(true);
 };
 auto Finish=[&](){
  UE_LOG(LogTemp,Display,TEXT("LIVE TALK TEST: nudges=%d penalties=%d left=%d reply=%d interrupted=%d -> %s"),S.LiveTestNudges,S.LiveTestPenalties,S.LiveTestLeft?1:0,S.LiveTestHerReply?1:0,S.LiveTestInterrupted?1:0,
   (S.LiveTestNudges>0&&S.LiveTestPenalties>=3&&S.LiveTestLeft&&S.LiveTestInterrupted)?TEXT("PASS"):TEXT("FAIL"));
  SetCharacterStat(1,0,S.LiveTestFavor);S.LiveTestPhase=-1;FPlatformMisc::RequestExitWithStatus(false,0);
 };
 switch(S.LiveTestPhase){
 case 0:S.LiveTestFavor=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[1]),S.Manifest.TalkBudget).Favor;Begin(1);S.LiveTestAt=Wall;S.LiveTestPhase=1;break;
 case 1:if(S.LiveTestLeft){S.FreeTalkSilenceScale=1;S.LiveTestAt=Wall;S.LiveTestPhase=2;}else if(Wall-S.LiveTestAt>180){UE_LOG(LogTemp,Warning,TEXT("LIVE TALK TEST: she never left"));S.LiveTestPhase=2;S.LiveTestAt=Wall;}break;
 case 2:if(Wall-S.LiveTestAt>3){Begin(2);S.LiveTestAt=Wall;S.LiveTestPhase=3;}break;
 case 3:if(S.FreeTalkLiveReady&&Wall-S.LiveTestAt>3){Inject();S.LiveTestAt=Wall;S.LiveTestPhase=4;}else if(Wall-S.LiveTestAt>30)Finish();break;
 case 4: // her spoken answer to the injected line
  if(S.FreeTalkLiveMuted&&!S.FreeTalkSpeechLines.IsEmpty()&&S.FreeTalkSpeechPlayed>0){S.LiveTestHerReply=true;Inject();S.LiveTestAt=Wall;S.LiveTestPhase=5;}
  else if(Wall-S.LiveTestAt>45){UE_LOG(LogTemp,Warning,TEXT("LIVE TALK TEST: no spoken reply"));Finish();}
  break;
 case 5:if(S.LiveTestInterrupted||Wall-S.LiveTestAt>12)Finish();break;
 }
}

// One status line for the panel while hands-free mode is on.
FString AOKStreetSocialScene::FreeTalkLiveStatus() const{
 const auto& S=*State;
 if(!S.FreeTalkLive)return FString();
 if(S.FreeTalkLiveMuted)return OKT(TEXT("ui.live_talk.she_speaking_status"));
 if(S.FreeTalkLiveSpeaking)return OKT(TEXT("ui.live_talk.you_speaking_status"));
 if(!S.FreeTalkLivePending.IsEmpty())return OKT(TEXT("ui.live_talk.wait_her_reply_send"))+S.FreeTalkLivePending;
 return OKT(TEXT("ui.live_talk.listening_speak_now"));
}

// ---- Microphone settings: list the inputs, switch the one ok-asr uses, and show a live level meter.
// Opening the meter keeps the ok-asr microphone open (unmuted, transcripts ignored) until it closes.
void AOKStreetSocialScene::FreeTalkMicToggle(){
 auto& S=*State;
 S.FreeTalkMicOpen=!S.FreeTalkMicOpen;S.FreeTalkMicLevel=0;
 // Adjusting the microphone takes as long as it takes: the street clock and the silence penalties stand still
 // while the panel is open. A clock the player had already paused stays paused afterwards.
 const double Wall=FPlatformTime::Seconds();
 if(S.FreeTalkMicOpen&&!S.ClockPaused){
  S.ClockStartHours=OKStreetClockHours(S.ClockStartHours,Wall-S.ClockStartWall);S.ClockStartWall=Wall;S.ClockPaused=true;S.FreeTalkMicPausedClock=true;
 }else if(!S.FreeTalkMicOpen&&S.FreeTalkMicPausedClock){
  S.ClockPaused=false;S.FreeTalkMicPausedClock=false;S.ClockStartWall=Wall;
 }
 if(S.FreeTalkMicOpen){
  FreeTalkMicRefresh();
  S.FreeTalkLiveBaseline=true;S.FreeTalkLiveNextPoll=0;
  if(!S.FreeTalkLive){OKStreetFreeTalkAsrPost(TEXT("listen/start"),TEXT("{}"),nullptr);OKStreetFreeTalkAsrPost(TEXT("listen/mute"),TEXT("{\"mute\":false}"),nullptr);}
 }else if(!S.FreeTalkLive)OKStreetFreeTalkAsrPost(TEXT("listen/stop"),TEXT("{}"),nullptr);
 RefreshUI();
}

void AOKStreetSocialScene::FreeTalkMicRefresh(){
 auto Request=FHttpModule::Get().CreateRequest();
 Request->SetURL(OKStreetFreeTalkAsrEndpoint()/TEXT("devices"));Request->SetVerb(TEXT("GET"));Request->SetTimeout(3.f);
 TWeakPtr<FOKStreetSceneState> WeakState=State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 Request->OnProcessRequestComplete().BindLambda([WeakState,Weak](FHttpRequestPtr,FHttpResponsePtr Response,bool Connected){
  const auto L=WeakState.Pin();if(!L)return;
  TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Devices=nullptr;
  if(!Connected||!Response.IsValid()||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Root)||!Root.IsValid()||!Root->TryGetArrayField(TEXT("devices"),Devices)){
   L->FreeTalkNotice=OKT(TEXT("ui.live_talk.asr_cannot_list_mics"));
  }else{
   L->FreeTalkMics.Reset();for(const auto& V:*Devices){FString Name;if(V.IsValid()&&V->TryGetString(Name))L->FreeTalkMics.Add(Name);}
   Root->TryGetStringField(TEXT("current"),L->FreeTalkMic);
  }
  if(Weak.IsValid()&&Weak->State==L)Weak->RefreshUI();
 });
 Request->ProcessRequest();
}

void AOKStreetSocialScene::FreeTalkMicSelect(const FString& Name){
 auto& S=*State;
 S.FreeTalkMic=Name;S.FreeTalkMicLevel=0;
 TSharedRef<FJsonObject> Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("device"),Name);
 FString Payload;FJsonSerializer::Serialize(Body,TJsonWriterFactory<>::Create(&Payload));
 TWeakPtr<FOKStreetSceneState> WeakState=State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 OKStreetFreeTalkAsrPost(TEXT("device"),Payload,[WeakState,Weak,Name](bool Ok,const FString& Error){
  const auto L=WeakState.Pin();if(!L)return;
  // Clicking through the list quickly: only the last choice reports and is remembered, answers may arrive late.
  if(L->FreeTalkMic!=Name)return;
  L->FreeTalkNotice=Ok?OKT(TEXT("ui.live_talk.switched_microphone_label"))+Name:Error;
  // Remember it for the next launch (the launcher passes the project's asr-settings.json).
  const FString Path=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ASR_SETTINGS"));
  if(Ok&&!Path.IsEmpty()){
   FString Text;TSharedPtr<FJsonObject> Root;
   if(!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root.IsValid())Root=MakeShared<FJsonObject>();
   Root->SetStringField(TEXT("device"),Name);
   FString Out;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Out));
   if(!FFileHelper::SaveStringToFile(Out,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))L->FreeTalkNotice+=OKT(TEXT("ui.live_talk.cannot_save_settings"));
  }
  if(Weak.IsValid()&&Weak->State==L)Weak->RefreshUI();
 });
 RefreshUI();
}
