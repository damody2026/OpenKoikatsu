// Included by OKStreetSocialScene.cpp: the development control channel. With -StreetSocialControl (and optionally
// -StreetSocialControlPort=47870) the game listens on 127.0.0.1 only and lets a developer (or a test driver) play it
// from outside: read the full state, type to the heroine, press the buttons on screen by their label, press keys
// (walking is real walking: the keys go through OKKeyDown/OKKeyPressed like a real keyboard), look around, start the
// built-in navigation and take screenshots. Requests are answered on the game thread between ticks.
// Never started without the command-line switch; the normal launcher does not pass it.

static TSharedPtr<IHttpRouter> GControlRouter;
static TArray<FHttpRouteHandle> GControlRoutes;
static bool GControlStarted=false,GControlRestoreMouse=false;
static double GControlRestoreMouseAt=0;
static int32 GControlShots=0;

// /camera: a fixed camera on one actor's bone (for like-for-like looks against the card reader's capture camera). Yaw and
// pitch are relative to the actor's facing (yaw 0 = in front of her, 90 = her left side); ortho_width > 0 is orthographic.
struct FOKControlCamera {bool Active=false;int32 Actor=1;FName Bone=TEXT("cf_j_head");double Yaw=90,Pitch=0,Distance=80,Fov=30,Ortho=0,SavedFov=90;};
static FOKControlCamera GControlCamera;
static bool OKApplyControlCamera(AActor* Owner,ACameraActor* Camera,const TSharedPtr<FJsonObject>& Report=nullptr){
 const auto& C=GControlCamera;FOKRuntimeCharacterStatus Status;FTransform Bone;
 if(!Camera||!OKGetCrowdRuntimeCharacterStatus(Owner,C.Actor,Status)||!OKGetCrowdRuntimeBoneTransform(Owner,C.Actor,C.Bone,Bone)||Bone.ContainsNaN())return false;
 const FVector Direction=Status.Transform.GetRotation().RotateVector(FRotator(C.Pitch,C.Yaw,0).Vector());
 const FVector Location=Bone.GetLocation()+Direction*C.Distance;Camera->SetActorLocationAndRotation(Location,(-Direction).Rotation());
 if(auto* Lens=Camera->GetCameraComponent()){Lens->SetConstraintAspectRatio(false);Lens->SetFieldOfView(C.Fov);Lens->SetProjectionMode(C.Ortho>0?ECameraProjectionMode::Orthographic:ECameraProjectionMode::Perspective);if(C.Ortho>0)Lens->SetOrthoWidth(C.Ortho);}
 if(Report.IsValid()){
  Report->SetStringField(TEXT("actor_rotation"),Status.Transform.Rotator().ToString());Report->SetStringField(TEXT("camera_location"),Location.ToString());
  // Head-related bone worlds (location, rotation, scale) to compare with the card reader / native session.
  auto Bones=MakeShared<FJsonObject>();
  for(const TCHAR* Name:{TEXT("cf_j_neck"),TEXT("cf_j_head"),TEXT("cf_s_head"),TEXT("p_cf_head_bone"),TEXT("cf_J_N_FaceRoot"),TEXT("cf_J_FaceRoot"),TEXT("cf_J_FaceBase"),TEXT("cf_J_FaceUp_ty"),TEXT("cf_J_FaceUp_tz"),TEXT("cf_J_FaceLow_s")}){
   FTransform T;if(!OKGetCrowdRuntimeBoneTransform(Owner,C.Actor,Name,T))continue;auto B=MakeShared<FJsonObject>();
   B->SetStringField(TEXT("location"),T.GetLocation().ToString());B->SetStringField(TEXT("rotation"),T.Rotator().ToString());B->SetStringField(TEXT("scale"),T.GetScale3D().ToString());
   B->SetStringField(TEXT("local_to_head"),Bone.InverseTransformPosition(T.GetLocation()).ToString());Bones->SetObjectField(Name,B);}
  Report->SetObjectField(TEXT("bones"),Bones);}
 return true;
}

// The map ending (quit, or started again for a load or a new game): the routes point at this scene, so they go with it
// and the next scene binds its own.
static void OKControlStop(){
 if(GControlRouter.IsValid())for(const auto& Route:GControlRoutes)GControlRouter->UnbindRoute(Route);
 GControlRoutes.Reset();GControlStarted=false;
}

static void OKControlReply(const FHttpResultCallback& Done,const TSharedRef<FJsonObject>& Body,int32 Code=200){
 FString Text;auto Writer=TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);FJsonSerializer::Serialize(Body,Writer);
 auto Response=FHttpServerResponse::Create(Text,TEXT("application/json; charset=utf-8"));Response->Code=static_cast<EHttpServerResponseCodes>(Code);Done(MoveTemp(Response));
}
static TSharedRef<FJsonObject> OKControlResult(bool Ok,const FString& Error=FString()){auto O=MakeShared<FJsonObject>();O->SetBoolField(TEXT("ok"),Ok);if(!Error.IsEmpty())O->SetStringField(TEXT("error"),Error);return O;}
static TSharedRef<FJsonObject> OKControlBody(const FHttpServerRequest& Req){
 const FUTF8ToTCHAR Utf8(reinterpret_cast<const ANSICHAR*>(Req.Body.GetData()),Req.Body.Num());
 const FString Text(Utf8.Length(),Utf8.Get());
 TSharedPtr<FJsonObject> Root;
 if(Text.IsEmpty()||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root.IsValid())Root=MakeShared<FJsonObject>();
 return Root.ToSharedRef();
}

void AOKStreetSocialScene::StartControlServer(){
 int32 Port=47870;FParse::Value(FCommandLine::Get(),TEXT("StreetSocialControlPort="),Port);
 OKVirtualInput().Active=true;
 GControlRouter=FHttpServerModule::Get().GetHttpRouter(Port,true);
 if(!GControlRouter.IsValid()){UE_LOG(LogTemp,Warning,TEXT("Street control: could not open port %d"),Port);return;}

 // The whole picture of the game for whoever is playing from outside.
 auto Build=[this]()->TSharedRef<FJsonObject>{
  auto& S=*State;auto O=MakeShared<FJsonObject>();const double Wall=FPlatformTime::Seconds();
  const double Hours=OKStreetClockHours(S.ClockStartHours,Wall-S.ClockStartWall);
  {auto T=MakeShared<FJsonObject>();T->SetNumberField(TEXT("hours"),Hours);T->SetStringField(TEXT("clock"),FString::Printf(TEXT("%02d:%02d"),int32(Hours),int32((Hours-int32(Hours))*60)));T->SetBoolField(TEXT("paused"),S.ClockPaused);O->SetObjectField(TEXT("time"),T);}
  {auto P=MakeShared<FJsonObject>();P->SetNumberField(TEXT("x"),S.Male.X);P->SetNumberField(TEXT("y"),S.Male.Y);P->SetNumberField(TEXT("z"),S.Male.Z);P->SetNumberField(TEXT("yaw"),S.Rotations.IsValidIndex(0)?S.Rotations[0].Yaw:0.0);
   P->SetStringField(TEXT("place"),PlaceHere(S.Male));P->SetNumberField(TEXT("gap"),OKStreetObstacleGap(S.Male));P->SetNumberField(TEXT("node"),S.Node);P->SetBoolField(TEXT("relationship_pending"),S.RelationshipPending);P->SetBoolField(TEXT("collected_active"),S.CollectedActive);P->SetBoolField(TEXT("cursor_mode"),S.MouseUI);P->SetNumberField(TEXT("camera_yaw"),S.Yaw);P->SetNumberField(TEXT("camera_pitch"),S.Pitch);
   P->SetNumberField(TEXT("cash"),double(S.Store.PlayerCash));P->SetNumberField(TEXT("bank"),double(S.Store.PlayerBank));O->SetObjectField(TEXT("player"),P);}
  O->SetStringField(TEXT("message"),S.Message);O->SetStringField(TEXT("notice"),S.FreeTalkNotice);
  O->SetNumberField(TEXT("target"),S.Target);O->SetNumberField(TEXT("partner"),FreeTalkPartner());O->SetNumberField(TEXT("ring"),S.RingGirl); // ring: who is calling him now (-1: nobody)
  {TArray<TSharedPtr<FJsonValue>> Party;for(const int32 G:S.Party)Party.Add(MakeShared<FJsonValueNumber>(G));O->SetArrayField(TEXT("party"),Party);O->SetBoolField(TEXT("bluff"),S.Bluff.Active);}
  {TArray<TSharedPtr<FJsonValue>> Appts;for(const auto& A:S.Store.Appointments){if(A.Status!=TEXT("pending"))continue;auto R=MakeShared<FJsonObject>();
    R->SetNumberField(TEXT("id"),A.Id);R->SetStringField(TEXT("kind"),A.Kind);R->SetStringField(TEXT("label"),A.Label);R->SetNumberField(TEXT("start"),double(A.Start));Appts.Add(MakeShared<FJsonValueObject>(R));}
   O->SetArrayField(TEXT("appointments"),Appts);}
  // The elevator near him (diagnostics): its floor, doors, the side it opens to and whether he is in the car; and his
  // gap to the nearest known obstacle.
  {O->SetNumberField(TEXT("obstacle_gap"),OKStreetObstacleGap(S.Male));
   {TArray<TSharedPtr<FJsonValue>> Near;for(const FBox& Bx:OKStreetRuntimeBlocks()){const FVector C=Bx.GetCenter();if(FVector::Dist2D(C,S.Male)<300.f)Near.Add(MakeShared<FJsonValueString>(Bx.Min.ToCompactString()+TEXT(" .. ")+Bx.Max.ToCompactString()));}
    O->SetArrayField(TEXT("blocks_near"),Near);}
   for(int32 I=0;I<S.Elevators.Num();++I){const auto& E=S.Elevators[I];if(FVector::Dist2D(E.Centre,S.Male)>1500.f)continue;
    auto L=MakeShared<FJsonObject>();L->SetStringField(TEXT("place"),OKStreetPlaces()[E.Place].Id);L->SetNumberField(TEXT("floor"),E.Floor);L->SetNumberField(TEXT("target"),E.Target);L->SetNumberField(TEXT("phase"),E.Phase);
    L->SetNumberField(TEXT("car_z"),E.CarZ);L->SetBoolField(TEXT("he_in_car"),InElevatorCar(I,S.Male));L->SetNumberField(TEXT("landing_here"),ElevatorLanding(I,S.Male));
    L->SetStringField(TEXT("centre"),E.Centre.ToCompactString());if(E.Out.IsValidIndex(E.Floor))L->SetStringField(TEXT("out"),E.Out[E.Floor].ToCompactString());if(E.Landing.IsValidIndex(E.Floor))L->SetStringField(TEXT("landing"),E.Landing[E.Floor].ToCompactString());
    O->SetObjectField(TEXT("elevator"),L);break;}}
  {auto A=MakeShared<FJsonObject>();A->SetNumberField(TEXT("partner"),S.Pair.Partner);A->SetNumberField(TEXT("phase"),S.Pair.Phase);A->SetBoolField(TEXT("meal"),S.Pair.Meal);A->SetBoolField(TEXT("seated"),S.Pair.Seated);A->SetBoolField(TEXT("drink"),S.Pair.Drink);O->SetObjectField(TEXT("pair"),A);}
  {auto A=MakeShared<FJsonObject>();A->SetNumberField(TEXT("companion"),S.Companion);A->SetNumberField(TEXT("outing_girl"),S.OutingGirl);A->SetStringField(TEXT("outing_activity"),S.OutingActivity);A->SetStringField(TEXT("outing_place"),S.OutingPlace);
   A->SetNumberField(TEXT("outing_step"),S.OutingStep);A->SetNumberField(TEXT("outing_points"),S.OutingRoute.Num());A->SetNumberField(TEXT("nav_girl"),S.NavGirl);A->SetBoolField(TEXT("nav_point"),S.NavPointActive);A->SetStringField(TEXT("nav_name"),S.NavPointName);
   A->SetNumberField(TEXT("facility"),S.Facility);A->SetNumberField(TEXT("private_girl"),S.PrivateGirl);O->SetObjectField(TEXT("activity"),A);}
  {auto F=MakeShared<FJsonObject>();F->SetBoolField(TEXT("open"),S.FreeTalkOpen);F->SetBoolField(TEXT("live"),S.FreeTalkLive);F->SetBoolField(TEXT("live_ready"),S.FreeTalkLiveReady);F->SetBoolField(TEXT("busy"),S.FreeTalkBusy);
   F->SetStringField(TEXT("heard"),S.FreeTalkHeard);F->SetNumberField(TEXT("mic_level"),S.FreeTalkMicLevel);F->SetBoolField(TEXT("mic_speaking"),S.FreeTalkLiveSpeaking);F->SetStringField(TEXT("draft"),S.FreeTalkDraft);
   F->SetStringField(TEXT("caption_player"),S.FreeTalkCaptionPlayer);F->SetStringField(TEXT("caption_her"),S.FreeTalkCaptionHer);F->SetBoolField(TEXT("farewell_pending"),S.FarewellPending);F->SetNumberField(TEXT("queued_events"),S.TellHer.Num());O->SetObjectField(TEXT("free_talk"),F);}
  {TArray<TSharedPtr<FJsonValue>> Rows;for(const auto& L:S.Cart){auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("item"),L.ItemId);if(const auto* I=S.Catalog.Find(L.ItemId))R->SetStringField(TEXT("name"),I->Name);R->SetBoolField(TEXT("for_her"),L.ForHer);Rows.Add(MakeShared<FJsonValueObject>(R));}O->SetArrayField(TEXT("cart"),Rows);}
  {TArray<TSharedPtr<FJsonValue>> Rows;for(const auto& D:S.Table){auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("name"),D.Name);R->SetBoolField(TEXT("for_her"),D.ForHer);R->SetBoolField(TEXT("served"),D.Served);R->SetNumberField(TEXT("percent"),D.Percent);Rows.Add(MakeShared<FJsonValueObject>(R));}O->SetArrayField(TEXT("table"),Rows);}
  O->SetNumberField(TEXT("pending_serves"),S.Serves.Num());O->SetStringField(TEXT("shop"),ShopPlaceHere());
  {FString Why;O->SetBoolField(TEXT("shop_closed"),!ShopPlaceHere().IsEmpty()&&PlaceClosedNow(ShopPlaceHere(),Why));}
  {TArray<TSharedPtr<FJsonValue>> Rows;
   for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(S.Agents.IsValidIndex(I-1)){
    const auto& A=S.Agents[I-1];auto G=MakeShared<FJsonObject>();
    G->SetNumberField(TEXT("slot"),I);G->SetStringField(TEXT("name"),S.Manifest.Characters[I].Name);
    G->SetNumberField(TEXT("x"),A.Position.X);G->SetNumberField(TEXT("y"),A.Position.Y);G->SetNumberField(TEXT("z"),A.Position.Z);G->SetNumberField(TEXT("dist"),FVector::Dist2D(A.Position,S.Male));
    G->SetNumberField(TEXT("yaw_target"),S.Rotations.IsValidIndex(I)?S.Rotations[I].Yaw:0.0);G->SetNumberField(TEXT("yaw_body"),S.RenderedRotations.IsValidIndex(I)?S.RenderedRotations[I].Yaw:0.0);
    G->SetBoolField(TEXT("held"),A.Held);G->SetNumberField(TEXT("mode"),int32(A.Mode));G->SetStringField(TEXT("place"),PlaceHere(A.Position));
    G->SetBoolField(TEXT("target"),S.Target==I);G->SetBoolField(TEXT("companion"),S.Companion==I);G->SetBoolField(TEXT("outing"),S.OutingGirl==I);G->SetBoolField(TEXT("pair"),S.Pair.Partner==I);
    G->SetBoolField(TEXT("life_pose"),InLifePose(I));G->SetBoolField(TEXT("commute"),S.Commutes.Contains(I));G->SetBoolField(TEXT("scheduled"),S.ScheduleHeld.Contains(I));G->SetBoolField(TEXT("promised_treat"),S.HerTreatPromise.Contains(I));G->SetBoolField(TEXT("lover_pose"),S.LoverPose.Girl==I);G->SetBoolField(TEXT("lover_react"),S.LoverReact.Girl==I);
    FOKRuntimeActionStreamStatus Act;const bool HasAct=OKGetCrowdRuntimeActionStatus(this,I,Act);G->SetStringField(TEXT("clip"),HasAct?Act.ClipId:FString());G->SetBoolField(TEXT("playing"),HasAct&&Act.Playing);G->SetBoolField(TEXT("speaking"),OKCrowdRuntimeSpeechActive(this,I));
    const FOKStreetRelation R=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[I]),S.Manifest.TalkBudget);
    G->SetNumberField(TEXT("gap"),OKStreetObstacleGap(A.Position));G->SetStringField(TEXT("stage"),R.Stage);G->SetNumberField(TEXT("favor"),R.Favor);G->SetNumberField(TEXT("cash"),double(R.Cash));G->SetNumberField(TEXT("owes_player"),double(R.OwesPlayer));G->SetNumberField(TEXT("satiety"),R.Satiety);G->SetNumberField(TEXT("energy"),R.Energy);
    G->SetStringField(TEXT("orientation"),OrientationOf(I));
    {auto Bonds=MakeShared<FJsonObject>();for(int32 J=1;J<S.Manifest.Characters.Num();++J)if(J!=I){auto B=MakeShared<FJsonObject>();B->SetNumberField(TEXT("favor"),BondOf(I,J));B->SetStringField(TEXT("stage"),BondStageOf(I,J));Bonds->SetObjectField(S.Manifest.Characters[J].Name,B);}G->SetObjectField(TEXT("toward"),Bonds);}
    Rows.Add(MakeShared<FJsonValueObject>(G));}
   O->SetArrayField(TEXT("girls"),Rows);}
  return O;
 };

 auto Bind=[&](const TCHAR* Path,EHttpServerRequestVerbs Verb,TFunction<void(const FHttpServerRequest&,const FHttpResultCallback&)> Fn){
  GControlRoutes.Add(GControlRouter->BindRoute(FHttpPath(Path),Verb,FHttpRequestHandler::CreateLambda([Fn](const FHttpServerRequest& Req,const FHttpResultCallback& Done){Fn(Req,Done);return true;})));
 };

 Bind(TEXT("/health"),EHttpServerRequestVerbs::VERB_GET,[](const FHttpServerRequest&,const FHttpResultCallback& Done){OKControlReply(Done,OKControlResult(true));});
 Bind(TEXT("/state"),EHttpServerRequestVerbs::VERB_GET,[Build](const FHttpServerRequest&,const FHttpResultCallback& Done){OKControlReply(Done,Build());});

 // He says something to her: the same path as typing it, or as the recogniser handing over what it heard.
 Bind(TEXT("/say"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;FString Said;OKControlBody(Req)->TryGetStringField(TEXT("text"),Said);const FString Text=Said.TrimStartAndEnd();
  if(Text.IsEmpty()){OKControlReply(Done,OKControlResult(false,TEXT("empty text")));return;}
  if(FreeTalkPartner()==INDEX_NONE){OKControlReply(Done,OKControlResult(false,TEXT("nobody to talk to")));return;}
  if(S.FreeTalkBusy){OKControlReply(Done,OKControlResult(false,TEXT("busy: she is still answering")));return;}
  S.SystemNote=false;S.FreeTalkHeard=Text;S.FreeTalkDraft=Text;SendFreeTalk();
  OKControlReply(Done,OKControlResult(true));
 });

 // Types into the phone's message box of the open thread and presses send (the thread is chosen with the phone's buttons).
 Bind(TEXT("/phone"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;FString Said;OKControlBody(Req)->TryGetStringField(TEXT("text"),Said);
  if(S.App!=TEXT("phone")){OKControlReply(Done,OKControlResult(false,TEXT("the phone is not open")));return;}
  if(Said.TrimStartAndEnd().IsEmpty()){OKControlReply(Done,OKControlResult(false,TEXT("empty text")));return;}
  if(S.FreeTalkBusy){OKControlReply(Done,OKControlResult(false,TEXT("busy: she is still answering")));return;}
  S.PhoneDraft=Said;PhoneSend();OKControlReply(Done,OKControlResult(true));
 });

 // Types into a text box of the panels, as keys would: {box: n} (the n-th box on screen) or {hint: "..."} (part of its
 // hint text), {text}, {enter: true} to press Enter after. Without text: the boxes on screen (index, hint, text).
 Bind(TEXT("/type"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;auto B=OKControlBody(Req);FString Text,Hint,Label;double Box=-1;bool Enter=false;
  const bool HasText=B->TryGetStringField(TEXT("text"),Text);B->TryGetStringField(TEXT("hint"),Hint);B->TryGetStringField(TEXT("label"),Label);B->TryGetNumberField(TEXT("box"),Box);B->TryGetBoolField(TEXT("enter"),Enter);
  // Each box with the text shown just before it (its label: 价格, 股数…), which stays put when boxes come and go.
  TArray<TSharedRef<SEditableTextBox>> Boxes;TArray<FString> Labels;FString LastText;
  TFunction<void(const TSharedRef<SWidget>&)> Walk=[&](const TSharedRef<SWidget>& W){
   if(W->GetType()==FName(TEXT("SEditableTextBox"))){if(!W->GetCachedGeometry().GetLocalSize().IsNearlyZero()){Boxes.Add(StaticCastSharedRef<SEditableTextBox>(W));Labels.Add(LastText);}return;}
   if(W->GetType()==FName(TEXT("STextBlock")))LastText=StaticCastSharedRef<STextBlock>(W)->GetText().ToString();
   if(FChildren* C=W->GetChildren())for(int32 I=0;I<C->Num();++I)Walk(C->GetChildAt(I));};
  if(S.Overlay.IsValid())Walk(S.Overlay.ToSharedRef());
  auto O=OKControlResult(true);
  if(!HasText){TArray<TSharedPtr<FJsonValue>> Rows;for(int32 I=0;I<Boxes.Num();++I){auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("box"),I);R->SetStringField(TEXT("hint"),Boxes[I]->GetHintText().ToString());R->SetStringField(TEXT("label"),Labels[I]);R->SetStringField(TEXT("text"),Boxes[I]->GetText().ToString());Rows.Add(MakeShared<FJsonValueObject>(R));}
   O->SetArrayField(TEXT("boxes"),Rows);OKControlReply(Done,O);return;}
  int32 Pick=INDEX_NONE;
  if(!Hint.IsEmpty()){for(int32 I=0;I<Boxes.Num();++I)if(Boxes[I]->GetHintText().ToString().Contains(Hint)){Pick=I;break;}}
  else if(!Label.IsEmpty()){for(int32 I=0;I<Boxes.Num();++I)if(Labels[I].Contains(Label)){Pick=I;break;}}
  else if(Boxes.IsValidIndex(int32(Box)))Pick=int32(Box);
  if(Pick==INDEX_NONE){OKControlReply(Done,OKControlResult(false,TEXT("no such text box")));return;}
  auto& App=FSlateApplication::Get();App.SetKeyboardFocus(Boxes[Pick],EFocusCause::SetDirectly);Boxes[Pick]->SelectAllText();
  for(const TCHAR C:Text)App.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
  if(Enter){const FKeyEvent K(EKeys::Enter,FModifierKeysState(),0,false,0,0);App.ProcessKeyDownEvent(K);App.ProcessKeyUpEvent(K);}
  O->SetStringField(TEXT("text"),Boxes[Pick]->GetText().ToString());OKControlReply(Done,O);
 });

 // Her face, set directly (tuning the look without waiting for the model): {girl, blush, sweat, gaze_x, gaze_y,
 // highlight, tears, expr, fade_scale (dev: stretch the look's easing), peek (only read)}. Answers what the runtime did,
 // the look drawn now and the shape cross-fade counters.
 Bind(TEXT("/play"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;auto B=OKControlBody(Req);double Girl=-1,Seconds=4;B->TryGetNumberField(TEXT("girl"),Girl);B->TryGetNumberField(TEXT("seconds"),Seconds);
  if(!S.Manifest.Characters.IsValidIndex(int32(Girl))||Girl<1){OKControlReply(Done,OKControlResult(false,FString::Printf(TEXT("girl: 1..%d"),S.Manifest.Characters.Num()-1)));return;}
  FString Clip,Gesture,Emotion;B->TryGetStringField(TEXT("clip"),Clip);B->TryGetStringField(TEXT("gesture"),Gesture);B->TryGetStringField(TEXT("emotion"),Emotion);
  if(!Clip.IsEmpty())StartLoverReaction(int32(Girl),Clip,float(Seconds));else PlayGesture(int32(Girl),Gesture,Emotion,float(Seconds));
  OKControlReply(Done,OKControlResult(true,FString()));});
 Bind(TEXT("/face"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;auto B=OKControlBody(Req);double Girl=-1,V=0;B->TryGetNumberField(TEXT("girl"),Girl);
  if(!S.Manifest.Characters.IsValidIndex(int32(Girl))||Girl<1){OKControlReply(Done,OKControlResult(false,FString::Printf(TEXT("girl: 1..%d"),S.Manifest.Characters.Num()-1)));return;}
  FString Expr,Error;bool Peek=false;B->TryGetBoolField(TEXT("peek"),Peek);
  if(!Peek&&B->TryGetStringField(TEXT("expr"),Expr))if(const auto* E=S.Expressions.Find(S.Manifest.Characters[int32(Girl)].PersonalityId,Expr))OKSetCrowdRuntimeFaceExpression(this,int32(Girl),E->Face,E->Blink,Error);
  FOKRuntimeFaceLook Look;if(B->TryGetNumberField(TEXT("blush"),V))Look.Blush=float(V);if(B->TryGetNumberField(TEXT("sweat"),V))Look.Sweat=float(V);
  if(B->TryGetNumberField(TEXT("gaze_x"),V))Look.Gaze.X=V;if(B->TryGetNumberField(TEXT("gaze_y"),V))Look.Gaze.Y=V;if(B->TryGetNumberField(TEXT("highlight"),V))Look.Highlight=int32(V);if(B->TryGetNumberField(TEXT("tears"),V))Look.Tears=int32(V);if(B->TryGetNumberField(TEXT("fade_scale"),V))Look.FadeScale=float(V);
  const bool Ok=Peek||OKSetCrowdRuntimeFaceLook(this,int32(Girl),Look,Error);auto O=OKControlResult(Ok,Error);
  // What her face shows this frame, easing toward the goal ({"peek": true} only reads it).
  {FOKRuntimeFaceLookNow N;if(OKGetCrowdRuntimeFaceLook(this,int32(Girl),N)){auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("blush"),N.Blush);J->SetNumberField(TEXT("sweat"),N.Sweat);J->SetNumberField(TEXT("gaze_x"),N.Gaze.X);J->SetNumberField(TEXT("gaze_y"),N.Gaze.Y);J->SetNumberField(TEXT("tears"),N.Tears);J->SetNumberField(TEXT("tear_shown"),N.TearShown);J->SetNumberField(TEXT("highlight"),N.Highlight);J->SetBoolField(TEXT("fading"),N.Fading);O->SetObjectField(TEXT("look_now"),J);}}
  // The shape cross-fades so far (mouth / eyes / brows, 0.15 s each as the source): started, frames drawn mid-way, done.
  {FOKRuntimeFaceStatus F;if(OKGetCrowdRuntimeFaceStatus(this,int32(Girl),F)){O->SetNumberField(TEXT("fades_started"),double(F.Started));O->SetNumberField(TEXT("fade_frames"),double(F.Intermediate));O->SetNumberField(TEXT("fades_completed"),double(F.Completed));}}
  // {"catch": candelas}: the catch light the lit tears shine with.
  if(B->TryGetNumberField(TEXT("catch"),V)&&S.CatchLight.IsValid())S.CatchLight->SetIntensity(float(V));
  // {"set": {"kind": 4, "name": "_Color", "value": [1,0,0,1]}}: a raw parameter on one kind of her materials.
  const TSharedPtr<FJsonObject>* Set=nullptr;if(B->TryGetObjectField(TEXT("set"),Set)){double Kind=0;FString Name;(*Set)->TryGetNumberField(TEXT("kind"),Kind);(*Set)->TryGetStringField(TEXT("name"),Name);
   const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;FLinearColor C(0,0,0,0);bool Vector=false;double X=0;
   if((*Set)->TryGetArrayField(TEXT("value"),Values)){Vector=true;for(int32 K=0;K<4&&K<Values->Num();++K)(&C.R)[K]=float((*Values)[K]->AsNumber());}else if((*Set)->TryGetNumberField(TEXT("value"),X))C.R=float(X);
   O->SetNumberField(TEXT("set_materials"),OKSetCrowdRuntimeLookParameter(this,int32(Girl),int32(Kind),Name,C,Vector));}
  OKControlReply(Done,O);
 });

 Bind(TEXT("/buttons"),EHttpServerRequestVerbs::VERB_GET,[this](const FHttpServerRequest&,const FHttpResultCallback& Done){
  auto& S=*State;auto O=OKControlResult(true);TArray<TSharedPtr<FJsonValue>> Rows,Clipped;
  if(S.Overlay.IsValid()){TArray<TPair<FString,TSharedPtr<SWidget>>> Buttons;OKProbeCollect(S.Overlay.ToSharedRef(),nullptr,Buttons);
   // A row scrolled out of its list is listed too, its index in "clipped" (the menu has more than nine dishes).
   for(const auto& B:Buttons){if(B.Key.IsEmpty()||!B.Value->IsEnabled())continue;const bool Out=OKProbeScrolledOut(B.Value);
    if(!Out&&B.Value->GetCachedGeometry().GetLocalSize().IsNearlyZero())continue;if(Out)Clipped.Add(MakeShared<FJsonValueNumber>(Rows.Num()));Rows.Add(MakeShared<FJsonValueString>(B.Key));}}
  O->SetArrayField(TEXT("buttons"),Rows);O->SetArrayField(TEXT("clipped"),Clipped);OKControlReply(Done,O);
 });

 // Press the button with this label (as the mouse would). The "@" development shortcuts (summon, teleport) are refused.
 Bind(TEXT("/click"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;FString Label;auto ClickBody=OKControlBody(Req);ClickBody->TryGetStringField(TEXT("label"),Label);double Nth=0;ClickBody->TryGetNumberField(TEXT("index"),Nth);
  if(Label.IsEmpty()||Label.StartsWith(TEXT("@"))){OKControlReply(Done,OKControlResult(false,TEXT("a plain button label is required")));return;}
  if(!S.MouseUI){GControlRestoreMouse=true;S.MouseUI=true;ApplyMouseMode(true);}
  GControlRestoreMouseAt=FPlatformTime::Seconds()+0.5;
  // Slate drops synthetic mouse input while the game window is not the foreground window.
  if(GEngine&&GEngine->GameViewport){if(TSharedPtr<SWindow> Win=GEngine->GameViewport->GetWindow())Win->BringToFront(true);FSlateApplication::Get().SetAllUserFocusToGameViewport();}
  FString Why;const bool Ok=ProbePress(Label,Why,int32(Nth));OKControlReply(Done,OKControlResult(Ok,Why));
 });

 // Cursor mode on/off (what Ctrl or the middle mouse button toggles): off lets WASD and E work again.
 // Development money for testing what needs it (a company, a bankruptcy): through the books like any cash, so the
 // journal stays reconciled; a negative amount takes money away.
 Bind(TEXT("/money"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;double Amount=0;OKControlBody(Req)->TryGetNumberField(TEXT("amount"),Amount);
  auto Candidate=S.Store;OKCash(Candidate,int64(Amount),TEXT("adjust"),OKT(TEXT("ui.control.test_add_money_control")));const bool Ok=CommitStore(MoveTemp(Candidate));
  auto O=OKControlResult(Ok);O->SetNumberField(TEXT("cash"),double(S.Store.PlayerCash));RefreshUI();OKControlReply(Done,O);
 });

 // The room microphone while a test drives the talk: off, what it hears (someone else in the room) is dropped.
 Bind(TEXT("/mic"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;bool Ignore=true;OKControlBody(Req)->TryGetBoolField(TEXT("ignore"),Ignore);S.ControlMicIgnored=Ignore;
  auto O=OKControlResult(true);O->SetBoolField(TEXT("ignore"),Ignore);OKControlReply(Done,O);
 });

 // The card table (吹牛): do = start | play (cards, claim: rank 0..12 when leading) | pass | pick (cards: his selection) |
 // call | stop | (nothing: just read).
 // Answers the table as he sees it: his cards, everyone's count, whose turn, the window, the log.
 Bind(TEXT("/bluff"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;auto& B=S.Bluff;const auto Body=OKControlBody(Req);FString Do;Body->TryGetStringField(TEXT("do"),Do);
  TArray<int32> Cards;const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(Body->TryGetArrayField(TEXT("cards"),A))for(const auto& V:*A)Cards.Add(int32(V->AsNumber()));
  double ClaimField=-1;Body->TryGetNumberField(TEXT("claim"),ClaimField);
  if(Do==TEXT("start"))StartBluff();
  else if(Do==TEXT("play"))BluffPlay(0,Cards,ClaimField>=0?int32(ClaimField):B.Rank>=0?B.Rank:B.ClaimRank);
  else if(Do==TEXT("pass"))BluffPass();
  else if(Do==TEXT("pick")){B.Selected.Reset();for(const int32 C:Cards)BluffToggle(C);}
  else if(Do==TEXT("call"))BluffChallenge(0);
  else if(Do==TEXT("stop"))EndBluff(OKT(TEXT("ui.control.game_stopped_by_you")),INDEX_NONE);
  auto O=OKControlResult(true);O->SetBoolField(TEXT("active"),B.Active);O->SetBoolField(TEXT("intro"),B.Intro);O->SetBoolField(TEXT("window"),B.Window);
  O->SetNumberField(TEXT("turn"),B.Turn);O->SetStringField(TEXT("rank"),B.Rank>=0?OKBluffRankName(B.Rank):FString(TEXT("lead")));O->SetBoolField(TEXT("open"),B.Open);O->SetNumberField(TEXT("pile"),B.PileCount);O->SetNumberField(TEXT("last_seat"),B.LastSeat);
  TArray<TSharedPtr<FJsonValue>> Mine,Names,Counts,Log;
  if(B.Hands.IsValidIndex(0))for(const int32 C:B.Hands[0])Mine.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%d:%s"),C,*OKBluffCardName(C))));
  for(int32 K=0;K<B.Seats.Num();++K){Names.Add(MakeShared<FJsonValueString>(BluffSeatName(K)));Counts.Add(MakeShared<FJsonValueNumber>(B.Hands.IsValidIndex(K)?B.Hands[K].Num():0));}
  for(int32 I=FMath::Max(0,B.Log.Num()-8);I<B.Log.Num();++I)Log.Add(MakeShared<FJsonValueString>(B.Log[I]));
  O->SetArrayField(TEXT("hand"),Mine);O->SetArrayField(TEXT("seats"),Names);O->SetArrayField(TEXT("counts"),Counts);O->SetArrayField(TEXT("log"),Log);
  O->SetStringField(TEXT("notice"),S.FreeTalkNotice);OKControlReply(Done,O);
 });

 // Development: put heroines (slots) around him, held where they are (her schedule and commute stopped), to test what
 // needs several of them together (a group talk, the card table) without walking them in from across town.
 Bind(TEXT("/summon"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;TArray<int32> Slots;const auto Body=OKControlBody(Req);const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(Body->TryGetArrayField(TEXT("slots"),A)) /* the body kept alive: A points into it */for(const auto& V:*A)Slots.Add(int32(V->AsNumber()));
  int32 K=0;for(const int32 G:Slots){if(!S.Agents.IsValidIndex(G-1))continue;
   const float Angle=FMath::DegreesToRadians(S.Rotations[0].Yaw+60.f+35.f*K++);
   S.Agents[G-1].Position=S.Male+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*170.f;S.Commutes.Remove(G);S.LiftWalks.Remove(G);S.Agents[G-1].Held=true;S.ScheduleHeld.Add(G);
   UE_LOG(LogTemp,Display,TEXT("Street control: summoned actor %d next to him"),G);}
  RefreshUI();OKControlReply(Done,OKControlResult(true));
 });

 // The language (lang: zh-TW | zh-CN | en; its region follows unless follow_region is false) and the save's region
 // (region: TW | CN | US; the save's money is converted by the rules). Answers what is in use.
 Bind(TEXT("/locale"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;const auto Body=OKControlBody(Req);FString Lang,Region;
  const bool HasRegion=Body->TryGetStringField(TEXT("region"),Region);bool Follow=true;Body->TryGetBoolField(TEXT("follow_region"),Follow);
  if(HasRegion)ChangeRegion(Region);
  if(Body->TryGetStringField(TEXT("lang"),Lang)&&(Lang==TEXT("zh-TW")||Lang==TEXT("zh-CN")||Lang==TEXT("en")))SetLanguage(Lang,Follow&&!HasRegion);
  auto O=OKControlResult(true);O->SetStringField(TEXT("lang"),S.Lang);O->SetStringField(TEXT("region"),S.Store.Region);O->SetStringField(TEXT("money_sample"),Money(Local(TEXT("price"),30)));O->SetNumberField(TEXT("cash"),double(S.Store.PlayerCash));
  OKControlReply(Done,O);
 });

 // The characters' look: style = koikatsu | ramp:<key> (the game's shadow ramps, live) | hg (saves "auto" and reloads
 // the map on it). Answers the one in use and every style there is.
 Bind(TEXT("/render"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  const auto Body=OKControlBody(Req);FString Style;if(Body->TryGetStringField(TEXT("style"),Style))SetRenderStyle(Style);
  auto O=OKControlResult(true);O->SetStringField(TEXT("style"),RenderStyle());O->SetStringField(TEXT("label"),RenderStyleLabel(RenderStyle()));O->SetBoolField(TEXT("restarting"),State->SaveRestarting);
  TArray<TSharedPtr<FJsonValue>> Styles;Styles.Add(MakeShared<FJsonValueString>(TEXT("koikatsu")));for(const auto& R:OKRenderRamps())Styles.Add(MakeShared<FJsonValueString>(TEXT("ramp:")+R.Key));Styles.Add(MakeShared<FJsonValueString>(TEXT("hg")));O->SetArrayField(TEXT("styles"),Styles);
  OKControlReply(Done,O);
 });

 // Save slots and the start menu: op = open | start | close | save | load | new (slot: auto, 1..8). Answers the slots.
 Bind(TEXT("/saves"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;const auto Body=OKControlBody(Req);FString Op,Slot;Body->TryGetStringField(TEXT("op"),Op);Body->TryGetStringField(TEXT("slot"),Slot);
  if(Op==TEXT("open"))OpenSaveMenu(TEXT("game"));else if(Op==TEXT("start"))OpenSaveMenu(TEXT("start"));else if(Op==TEXT("close"))CloseSaveMenu();
  else if(Op==TEXT("save")&&!Slot.IsEmpty())SaveToSlot(Slot);
  auto O=OKControlResult(true);O->SetStringField(TEXT("menu"),S.SaveMenu);O->SetStringField(TEXT("notice"),S.SaveNotice);
  auto Slots=MakeShared<FJsonObject>();Slots->SetStringField(TEXT("auto"),SaveSlotSummary(TEXT("auto")));for(int32 I=1;I<=OKStreetSaveSlotCount;++I)Slots->SetStringField(FString::FromInt(I),SaveSlotSummary(FString::FromInt(I)));
  O->SetObjectField(TEXT("slots"),Slots);OKControlReply(Done,O);
  // Last: the map starts again, and this scene (and these routes) end.
  if(Op==TEXT("load")&&!Slot.IsEmpty())RestartWithSave(TEXT("load"),Slot);else if(Op==TEXT("new"))RestartWithSave(TEXT("new"),FString());
 });

 // The street clock paused or running (the panel's pause button): {"paused": true|false}.
 Bind(TEXT("/clock"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;bool Paused=S.ClockPaused;OKControlBody(Req)->TryGetBoolField(TEXT("paused"),Paused);
  if(Paused!=S.ClockPaused){const double Wall=FPlatformTime::Seconds();if(Paused)S.ClockStartHours=OKStreetClockHours(S.ClockStartHours,Wall-S.ClockStartWall);S.ClockStartWall=Wall;S.ClockPaused=Paused;RefreshUI();}
  auto O=OKControlResult(true);O->SetBoolField(TEXT("paused"),S.ClockPaused);OKControlReply(Done,O);
 });

 Bind(TEXT("/cursor"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;bool On=false;OKControlBody(Req)->TryGetBoolField(TEXT("on"),On);
  S.MouseUI=On;ApplyMouseMode(true);RefreshUI();OKControlReply(Done,OKControlResult(true));
 });

 // What the shop he stands in sells now: id, name, prices, whether it is food or drink.
 Bind(TEXT("/menu"),EHttpServerRequestVerbs::VERB_GET,[this](const FHttpServerRequest&,const FHttpResultCallback& Done){
  auto& S=*State;auto O=OKControlResult(true);const FString Place=ShopPlaceHere();O->SetStringField(TEXT("place"),Place);FString Why;O->SetBoolField(TEXT("closed"),!Place.IsEmpty()&&PlaceClosedNow(Place,Why));
  TArray<TSharedPtr<FJsonValue>> Rows;
  if(!Place.IsEmpty())for(const auto* I:ItemsHere(Place)){
   auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("id"),I->Id);R->SetStringField(TEXT("name"),I->Name);R->SetBoolField(TEXT("consumable"),I->Consumable);
   TArray<FString> Prices;for(const auto& P:I->Price)Prices.Add(P.Key.IsEmpty()?FString::FromInt(P.Value):P.Key+FString::FromInt(P.Value));R->SetStringField(TEXT("price"),FString::Join(Prices,TEXT("/")));
   Rows.Add(MakeShared<FJsonValueObject>(R));}
  O->SetArrayField(TEXT("items"),Rows);OKControlReply(Done,O);
 });

 Bind(TEXT("/key"),EHttpServerRequestVerbs::VERB_POST,[](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto B=OKControlBody(Req);FString Name;B->TryGetStringField(TEXT("key"),Name);double Seconds=0;B->TryGetNumberField(TEXT("seconds"),Seconds);
  const FKey Key(*Name);if(!Key.IsValid()){OKControlReply(Done,OKControlResult(false,TEXT("unknown key name (E, W, Escape, LeftShift, One, ...)")));return;}
  auto& V=OKVirtualInput();V.Pending.Add(Key);if(Seconds>0)V.DownUntil.Add(Key,FPlatformTime::Seconds()+Seconds);
  OKControlReply(Done,OKControlResult(true));
 });

 Bind(TEXT("/look"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;auto B=OKControlBody(Req);double Yaw=0,Pitch=0;
  if(B->TryGetNumberField(TEXT("yaw"),Yaw))S.Yaw=Yaw;if(B->TryGetNumberField(TEXT("pitch"),Pitch))S.Pitch=FMath::Clamp(Pitch,-50.0,15.0);
  OKControlReply(Done,OKControlResult(true));
 });

 // The built-in autopilot: to a heroine ("girl": 1..10) or to a place's door ("place": id). The player walks at normal speed.
 Bind(TEXT("/navigate"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto B=OKControlBody(Req);bool Stop=false;B->TryGetBoolField(TEXT("stop"),Stop);double Girl=0;FString Place;
  if(Stop){StopNavigation(FString());OKControlReply(Done,OKControlResult(true));return;}
  if(B->TryGetNumberField(TEXT("girl"),Girl)){NavigateGirl(int32(Girl));OKControlReply(Done,OKControlResult(true));return;}
  // {"place": id, "floor": N}: that floor's elevator landing in the place (navigation across floors).
  double Floor=-1;if(B->TryGetStringField(TEXT("place"),Place)&&B->TryGetNumberField(TEXT("floor"),Floor)){const FOKStreetPlace* P=OKStreetFindPlace(Place);const auto Lifts=P?OKStreetPlaceLifts(*P):TArray<const FOKStreetModelAnchor*>();
   if(!Lifts.IsValidIndex(int32(Floor))){OKControlReply(Done,OKControlResult(false,TEXT("no such floor")));return;}
   NavigatePoint(Lifts[int32(Floor)]->Position+FVector(0,0,0),P->Name);OKControlReply(Done,OKControlResult(true));return;}
  if(B->TryGetStringField(TEXT("place"),Place)){const FOKStreetPlace* P=OKStreetFindPlace(Place);if(!P){OKControlReply(Done,OKControlResult(false,TEXT("unknown place id")));return;}NavigatePoint(P->Door,P->Name);OKControlReply(Done,OKControlResult(true));return;}
  OKControlReply(Done,OKControlResult(false,TEXT("girl, place or stop is required")));
 });

 // The game rules (ok_rules.dll): {"action":"status"} (default), {"action":"reload"} (swap in a newer cargo build, else
 // re-read the tuning files) or {"action":"eval","request":{"kind":...}} (ask the rules directly, nothing is applied).
 // The heroine mind (ok-mind): status, or reload (a new build, or the settings file read again) while it is idle.
 Bind(TEXT("/mind"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;const TSharedRef<FJsonObject> Body=OKControlBody(Req);FString Action=TEXT("status");Body->TryGetStringField(TEXT("action"),Action);
  if(!S.Mind){OKControlReply(Done,OKControlResult(false,TEXT("mind not open")));return;}
  FString Out,Error;TSharedRef<FJsonObject> O=OKControlResult(true);
  if(Action==TEXT("reload")){WaitFinQuery();const bool Ok=S.Mind->Reload(Out,Error);O=OKControlResult(Ok,Error);O->SetStringField(TEXT("result"),Out);
   UE_LOG(LogTemp,Display,TEXT("StreetSocial mind reload: %s%s"),Ok?*Out:TEXT("failed: "),Ok?TEXT(""):*Error);}
  if(S.Mind->Query(TEXT("{\"kind\":\"status\"}"),Out,Error)){TSharedPtr<FJsonObject> Status;if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Out),Status)&&Status.IsValid())O->SetObjectField(TEXT("status"),Status);}
  O->SetStringField(TEXT("dll"),S.Mind->DllPath());
  OKControlReply(Done,O);
 });
 // Her song (OKStreetSocialSong.inl): {"action": state|prepare|play|accept|later|decline|skip|cancel|enable|disable, "girl": N}.
 Bind(TEXT("/song"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  const TSharedRef<FJsonObject> Body=OKControlBody(Req);FString Action=TEXT("state");Body->TryGetStringField(TEXT("action"),Action);int32 Girl=0;Body->TryGetNumberField(TEXT("girl"),Girl);
  OKControlReply(Done,SongControl(Action,Girl));
 });
 Bind(TEXT("/rules"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;const TSharedRef<FJsonObject> Body=OKControlBody(Req);FString Action=TEXT("status");Body->TryGetStringField(TEXT("action"),Action);
  if(!S.Rules){OKControlReply(Done,OKControlResult(false,TEXT("rules not open")));return;}
  FString Error;TSharedRef<FJsonObject> O=OKControlResult(true);
  if(Action==TEXT("reload")){FString Out;const bool Ok=S.Rules->Reload(Out,Error);O=OKControlResult(Ok,Error);O->SetStringField(TEXT("result"),Out);if(Ok){LoadUiTexts();RefreshUI();}}
  else if(Action==TEXT("eval")){const TSharedPtr<FJsonObject>* Request=nullptr;
   if(!Body->TryGetObjectField(TEXT("request"),Request)||!Request->IsValid()){OKControlReply(Done,OKControlResult(false,TEXT("request object required")));return;}
   const TSharedPtr<FJsonObject> Answer=S.Rules->Eval(Request->ToSharedRef(),Error);O=OKControlResult(Answer.IsValid(),Error);if(Answer.IsValid())O->SetObjectField(TEXT("answer"),Answer);}
  else{auto Request=MakeShared<FJsonObject>();Request->SetStringField(TEXT("rule"),TEXT("status"));const TSharedPtr<FJsonObject> Answer=S.Rules->Eval(Request,Error);
   O=OKControlResult(Answer.IsValid(),Error);if(Answer.IsValid())O->SetObjectField(TEXT("status"),Answer);}
  O->SetStringField(TEXT("dll"),S.Rules->DllPath());O->SetNumberField(TEXT("generation"),S.Rules->Generation());
  OKControlReply(Done,O);
 });

 // {actor:1..10, bone, yaw, pitch, distance, fov, ortho_width} pins the camera (see GControlCamera); {off:true} releases it.
 Bind(TEXT("/camera"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;auto B=OKControlBody(Req);auto& C=GControlCamera;bool Off=false;B->TryGetBoolField(TEXT("off"),Off);
  ACameraActor* Camera=S.Camera.Get();UCameraComponent* Lens=Camera?Camera->GetCameraComponent():nullptr;
  if(Off){if(C.Active&&Lens){Lens->SetProjectionMode(ECameraProjectionMode::Perspective);Lens->SetFieldOfView(C.SavedFov);}C.Active=false;OKControlReply(Done,OKControlResult(true));return;}
  if(!Lens){OKControlReply(Done,OKControlResult(false,TEXT("no street camera")));return;}
  if(!C.Active)C.SavedFov=Lens->FieldOfView;
  double Actor=C.Actor;B->TryGetNumberField(TEXT("actor"),Actor);C.Actor=int32(Actor);FString Bone;if(B->TryGetStringField(TEXT("bone"),Bone))C.Bone=FName(*Bone);
  B->TryGetNumberField(TEXT("yaw"),C.Yaw);B->TryGetNumberField(TEXT("pitch"),C.Pitch);B->TryGetNumberField(TEXT("distance"),C.Distance);B->TryGetNumberField(TEXT("fov"),C.Fov);B->TryGetNumberField(TEXT("ortho_width"),C.Ortho);
  C.Active=true;auto O=OKControlResult(true);
  if(!OKApplyControlCamera(this,Camera,O)){C.Active=false;OKControlReply(Done,OKControlResult(false,TEXT("actor or bone unavailable")));return;}
  O->SetNumberField(TEXT("fov"),Lens->FieldOfView);O->SetNumberField(TEXT("ortho_width"),C.Ortho);OKControlReply(Done,O);
 });

 // {command: "stat unit"} runs an engine console command on the game thread and returns what it printed (profiling:
 // "stat dumphitches", "Trace.File <path> cpu,gpu,frame", "Trace.Stop"). Viewport commands (stat, HighResShot) go
 // through the game viewport, the rest through the engine. Development only: the channel itself needs -StreetSocialControl.
 Bind(TEXT("/exec"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  FString Command;OKControlBody(Req)->TryGetStringField(TEXT("command"),Command);Command.TrimStartAndEndInline();
  if(Command.IsEmpty()){OKControlReply(Done,OKControlResult(false,TEXT("command is required")));return;}
  FStringOutputDevice Out;Out.SetAutoEmitLineTerminator(true);bool Handled=false;
  if(GEngine&&GEngine->GameViewport&&GetWorld())Handled=GEngine->GameViewport->Exec(GetWorld(),*Command,Out);
  if(!Handled&&GEngine)Handled=GEngine->Exec(GetWorld(),*Command,Out);
  UE_LOG(LogTemp,Display,TEXT("Street control exec: %s (%s)"),*Command,Handled?TEXT("handled"):TEXT("not handled"));
  auto O=OKControlResult(Handled,Handled?FString():TEXT("not a known command"));O->SetStringField(TEXT("output"),Out);OKControlReply(Done,O);
 });

 // {scale: 3}: the engine's high-resolution shot at that multiple of the window (no UI), for close looks at a face.
 Bind(TEXT("/screenshot"),EHttpServerRequestVerbs::VERB_POST,[this](const FHttpServerRequest& Req,const FHttpResultCallback& Done){
  auto& S=*State;const FString Dir=FPaths::GetPath(S.Manifest.SocialReport)/TEXT("control_shots");IFileManager::Get().MakeDirectory(*Dir,true);
  double Scale=1;OKControlBody(Req)->TryGetNumberField(TEXT("scale"),Scale);Scale=FMath::Clamp(Scale,1.0,4.0);
  const FString File=Dir/FString::Printf(TEXT("shot.%04d.png"),++GControlShots);
  // HighResShot is the game viewport's command (GEngine->Exec does not route it there).
  if(Scale>1.01&&GEngine&&GEngine->GameViewport&&GetWorld()){GEngine->GameViewport->Exec(GetWorld(),*FString::Printf(TEXT("HighResShot %.2f filename=\"%s\""),Scale,*File),*GLog);}
  else FScreenshotRequest::RequestScreenshot(File,true,false);
  auto O=OKControlResult(true);O->SetStringField(TEXT("path"),File);OKControlReply(Done,O);
 });

 FHttpServerModule::Get().StartAllListeners();
 UE_LOG(LogTemp,Display,TEXT("Street control: listening on http://127.0.0.1:%d (state, say, buttons, click, key, look, navigate, rules, screenshot)"),Port);
}

// Every tick: start the channel once (when asked for on the command line), turn the keys asked for since the last tick into
// "just pressed" for this one, and give the cursor back to the game a moment after a button was pressed through it.
void AOKStreetSocialScene::TickControl(){
 if(!GControlStarted&&FParse::Param(FCommandLine::Get(),TEXT("StreetSocialControl"))){GControlStarted=true;StartControlServer();}
 auto& V=OKVirtualInput();if(!V.Active)return;
 V.Pressed=MoveTemp(V.Pending);V.Pending.Reset();
 if(GControlRestoreMouse&&FPlatformTime::Seconds()>=GControlRestoreMouseAt){GControlRestoreMouse=false;State->MouseUI=false;ApplyMouseMode(true);RefreshUI();}
}
