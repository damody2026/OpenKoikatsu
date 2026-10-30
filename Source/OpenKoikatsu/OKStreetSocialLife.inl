// What the heroines' own days look like and do to them (included by OKStreetSocialScene.cpp after the routine).
//
// Docs/STREET_BEHAVIOR_FIX_PLAN.md: rest and sleep give energy back, a bath when she feels grimy, the catalogue wage for
// her shifts, a sitting / eating / studying pose while her schedule holds her where he can see, best friends walking
// side by side and facing each other when they meet, rain on screen, her own wish for intimacy, what she came over
// for once she reaches him, the mask slipping when they are alone, and acts nobody handles said out loud.

bool OKStreetFixedBlock(const FOKStreetPlanBlock* B); // round 3, below

namespace {
// The clips she sits for and the ones that bring a table (motions.toml, read once by ReadMotionTable).
TSet<FString>& OKLifeSeatedSet(){static TSet<FString> Clips;return Clips;}
TSet<FString>& OKLifeTableSet(){static TSet<FString> Clips;return Clips;}
bool OKLifeSeatedClip(const FString& Clip){return Clip.Contains(TEXT("seated"))||Clip==TEXT("facility_eat.okanim")||OKLifeSeatedSet().Contains(Clip);}
bool OKLifeTableClip(const FString& Clip){return Clip==TEXT("facility_eat.okanim")||OKLifeTableSet().Contains(Clip);}
constexpr float OKLifeRange=2200.f,OKRainRadius=1600.f,OKRainHeight=900.f;constexpr int32 OKRainDrops=700;
}

// What she is doing now: the plan's block, or the urgent cut-in TickSchedules put first (meal -2, rest -3, bath -4).
// The motions table (rules motions.toml): which clips she sits for and which bring a table.
void AOKStreetSocialScene::ReadMotionTable(){
 auto& S=*State;if(S.MotionTableRead)return;S.MotionTableRead=true;
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,OKRulesRequest(TEXT("motion_table")));if(!Out)return;
 const TArray<TSharedPtr<FJsonValue>>* List=nullptr;
 if(Out->TryGetArrayField(TEXT("seated"),List))for(const auto& V:*List)OKLifeSeatedSet().Add(V->AsString());
 if(Out->TryGetArrayField(TEXT("table"),List))for(const auto& V:*List)OKLifeTableSet().Add(V->AsString());
 UE_LOG(LogTemp,Display,TEXT("Street motions: %d seated loops, %d with a table"),OKLifeSeatedSet().Num(),OKLifeTableSet().Num());
}
// The loop she shows while her schedule holds her in place (motions.toml life rows): one of the clips for the block's
// kind and place, the same one all through the block (asked once per block; a cut-in meal or rest is one block for the
// day). Empty: her plain idle / book.
FString AOKStreetSocialScene::LifeClip(int32 Actor,const FOKStreetPlanBlock& B){
 auto& S=*State;ReadMotionTable();
 const bool CutIn=B.End-B.Start<=1;
 const FString Key=FString::Printf(TEXT("%d|%s|%s|%d|%d"),Actor,*B.Kind,*B.Place,CutIn?-1:B.Start,S.Store.CalendarDay);
 if(const FString* Have=S.LifeClipChoice.Find(Key))return *Have;
 auto Ask=OKRulesRequest(TEXT("motion_life"));Ask->SetStringField(TEXT("kind"),B.Kind);Ask->SetStringField(TEXT("place"),B.Place);
 Ask->SetNumberField(TEXT("seed"),double(GetTypeHash(Key)&0x3fffffff));
 FString Clip;if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Ask))Clip=Out->GetStringField(TEXT("clip"));
 if(S.LifeClipChoice.Num()>512)S.LifeClipChoice.Reset(); // yesterday's blocks
 S.LifeClipChoice.Add(Key,Clip);return Clip;
}

const FOKStreetPlanBlock* AOKStreetSocialScene::LifeBlock(int32 I,int32 Minute) const{
 auto& S=*State;const int32 Index=S.PlanBlock.FindRef(I,INDEX_NONE);
 if(Index>=-4&&Index<=-2){
  TSharedPtr<FOKStreetPlanBlock>& Slot=S.LifeNow.FindOrAdd(I);if(!Slot)Slot=MakeShared<FOKStreetPlanBlock>(); // stable when the map grows
  FOKStreetPlanBlock& U=*Slot;U.Kind=Index==-2?TEXT("meal"):Index==-3?TEXT("rest"):TEXT("bath");
  U.Place=S.Agents.IsValidIndex(I-1)?PlaceHere(S.Agents[I-1].Position):FString();U.Start=Minute;U.End=Minute+1;return &U;
 }
 return BlockNow(I,Minute);
}

// Her pay for a shift: the catalogue job's hourly wage (a job id, else the job at that place), 95 when unknown.
int32 AOKStreetSocialScene::HerWage(const FOKStreetPlanBlock& W) const{
 const auto& C=State->Catalog;
 if(const auto* J=W.JobId.IsEmpty()?nullptr:C.FindJob(W.JobId))return J->Wage;
 if(const auto* J=C.Jobs.FindByPredicate([&](const FOKStreetCatalogJob& X){return X.Place==W.Place;}))return J->Wage;
 return int32(Local(TEXT("wage"),95));
}

// The two heroines' masks (bias): alone with him, a lover who trusts him lets the act drop (social.rs mask_broken).
bool AOKStreetSocialScene::MaskBroken(int32 Character,bool bPublic) const{
 if(bPublic||!State||!State->Manifest.Characters.IsValidIndex(Character))return false;
 auto Request=OKRulesRequest(TEXT("mask_broken"));Request->SetObjectField(TEXT("relation"),OKRulesRelationJson(OKStreetViewRelation(State->Store,OKStreetStableId(State->Manifest.Characters[Character]),State->Manifest.TalkBudget)));Request->SetBoolField(TEXT("public"),bPublic);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(*State,Request);return Answer&&Answer->GetBoolField(TEXT("broken"));
}

// An act the model named that nothing here knows: logged and told, instead of silently dropped.
void AOKStreetSocialScene::UnknownAct(int32 Character,const FString& Kind){
 auto& S=*State;UE_LOG(LogTemp,Warning,TEXT("Street act not handled: actor=%d kind=%s"),Character,*Kind);
 if(!S.Manifest.Characters.IsValidIndex(Character))return;
 S.FreeTalkNotice=OKMsg(TEXT("ui.life.action_not_ready_notice"), {{TEXT("name"), *S.Manifest.Characters[Character].Name}, {TEXT("kind"), *Kind}});
 S.MindRecent.Add(OKStreetStableId(S.Manifest.Characters[Character]),OKMsg(TEXT("ui.life.action_failed_rephrase"), {{TEXT("kind"), *Kind}}));
 RefreshUI();
}

// What she came over for (Director initiative): her first words, and the follow-up the rules can do.
FString AOKStreetSocialScene::InitiativeNudge(int32 I,bool bInvite){
 auto& S=*State;const FString Why=S.InitiativeReason.FindRef(I);S.InitiativeReason.Remove(I);
 FString Line=bInvite?OKT(TEXT("ui.life.approach_invite_place")):OKT(TEXT("ui.life.approach_open_talk"));
 if(Why.IsEmpty())return Line;
 Line=OKMsg(TEXT("ui.life.approach_player_reason"), {{TEXT("why"), *Why}});
 if(Why.Contains(OKT(TEXT("ui.life.borrow_money_label"))))Line+=OKT(TEXT("ui.life.lend_json_instruction"));
 else if(Why.Contains(OKT(TEXT("ui.life.eat_meal_label")))||Why.Contains(OKT(TEXT("ui.life.treat_back_label")))){Line+=OKT(TEXT("ui.life.invite_meal_instruction"));S.HerTreatPromise.Add(I,OKStreetMindNow(S)+6*60);}
 else if(Why.Contains(OKT(TEXT("ui.life.ask_clearly_label"))))Line+=OKT(TEXT("ui.life.tone_ask_explanation"));
 else if(Why.Contains(OKT(TEXT("ui.life.share_burden_label"))))Line+=OKT(TEXT("ui.life.he_agrees_walk_together"));
 else if(Why.Contains(OKT(TEXT("ui.life.introduce_label")))){
  // Her best friend comes along to be introduced.
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);FString Friend;float Heat=-1;
  for(const auto& B:S.Store.BondHeat)if(B.Key.Contains(Id)&&B.Value>Heat){Heat=B.Value;FString L,Rt;B.Key.Split(TEXT("|"),&L,&Rt);Friend=L==Id?Rt:L;}
  const int32 F=CharacterOf(Friend);
  if(F!=INDEX_NONE&&S.Agents.IsValidIndex(F-1)&&!BusyWithPlayer(F)){
   const FVector Side=FRotator(0,S.Rotations[0].Yaw+60.f,0).Vector()*120.f;
   auto Path=ContactRoute(S.Agents[F-1].Position,S.Male+Side);
   if(!Path.IsEmpty()){S.Agents[F-1].Held=true;S.ScheduleHeld.Add(F);S.Commutes.Add(F,MoveTemp(Path));S.CommuteFinal.Remove(F);S.IntroFriend=F;S.IntroAt=SceneTime();}
   Line+=OKMsg(TEXT("ui.life.brought_friend_intro"), {{TEXT("friend_name"), *S.Manifest.Characters[F].Name}});
   UE_LOG(LogTemp,Display,TEXT("Street initiative: actor %d brings friend %d to meet him"),I,F);
  }
 }
 return Line+OKT(TEXT("ui.finance_apps.close_paren"));
}

// One heroine's pose ends (INDEX_NONE: all of them): her normal behaviour back, her spot and props restored.
void AOKStreetSocialScene::StopLifePose(int32 Only){
 if(!State)return;auto& S=*State;
 for(int32 K=S.LifePoses.Num()-1;K>=0;--K){
  auto& P=S.LifePoses[K];if(Only!=INDEX_NONE&&P.Actor!=Only)continue;const int32 I=P.Actor;
  FString Error;OKResumeCrowdRuntimeBehavior(this,I,Error);
  if(S.LifeRestore.Contains(I)&&S.Agents.IsValidIndex(I-1))S.Agents[I-1].Position=S.LifeRestore[I];S.LifeRestore.Remove(I);
  for(auto& Prop:P.Props)DestroyLater(Prop.Get());
  S.LifePoses.RemoveAt(K);
 }
}
// Safety net: a seated clip that keeps playing for a heroine who is in no seated activity (a pose left over when the
// player started talking, an outing, a walk) would carry her along sitting on thin air. Whatever the cause, the
// clip is ended and the cause logged. Two checks half a second apart, so the hand-over between activities passes.
void AOKStreetSocialScene::StopStrayPoses(){
 if(!State)return;auto& S=*State;const double Wall=FPlatformTime::Seconds();
 if(Wall<S.StrayPoseAt)return;S.StrayPoseAt=Wall+0.5;
 for(int32 I=1;I<=S.Agents.Num();++I){
  FOKRuntimeActionStreamStatus A;
  const bool Legit=InLifePose(I)||S.PartySeats.ContainsByPredicate([I](const FOKLifePose& P){return P.Actor==I;})||S.Pair.Partner==I||(S.Facility!=INDEX_NONE&&S.FacilityCharacter==I)||S.PrivateGirl==I||S.SleepGirl==I||S.SleepGirl2==I||S.SoloGirl==I
   ||S.LoverPose.Girl==I||S.LoverReact.Girl==I||S.AmbientActor==I||S.RestroomGirl==I||S.NpcLifts.Contains(I);
  if(Legit||!OKGetCrowdRuntimeActionStatus(this,I,A)||!A.Playing||!OKLifeSeatedClip(A.ClipId)){S.StrayPose.Remove(I);continue;}
  int32& Seen=S.StrayPose.FindOrAdd(I,0);
  if(++Seen<2)continue;
  FString Error;OKResumeCrowdRuntimeBehavior(this,I,Error);S.StrayPose.Remove(I);
  UE_LOG(LogTemp,Warning,TEXT("Street stray pose: actor %d was still playing %s outside any seated activity (target=%d companion=%d outing=%d pair=%d); stopped"),I,*A.ClipId,S.Target,S.Companion,S.OutingGirl,S.Pair.Partner);
 }
}
bool AOKStreetSocialScene::InLifePose(int32 I) const{return State&&State->LifePoses.ContainsByPredicate([I](const FOKLifePose& P){return P.Actor==I;});}

// A chair under her (the facility meal's sizes, in her frame: +Y ahead), and for a meal a table with a plate.
// SeatOnly: the seat and its legs, no back and no table of her own (one of a group at the pair's table: the back stood
// right in front of the talk camera, which looks across the table from the side).
void AOKStreetSocialScene::SpawnLifeProps(FOKLifePose& Pose,bool SeatOnly){
 auto& S=*State;const int32 I=Pose.Actor;const FString Clip=Pose.Clip;if(!S.Agents.IsValidIndex(I-1)||!OKLifeSeatedClip(Clip))return;
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Scenes/StreetSocial/Meshes/StreetCube.StreetCube"));if(!Cube)return;
 const FRotator Facing(0,S.Rotations[I].Yaw,0);const FVector Base=S.Agents[I-1].Position;
 auto Part=[&](FVector Local,FVector Size,int32 Material){
  auto* A=GetWorld()->SpawnActor<AStaticMeshActor>(Base+Facing.RotateVector(Local),Facing);if(!A)return;A->SetOwner(this);A->SetActorScale3D(Size/100.f);
  auto* C=A->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Cube);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetLightingChannels(true,true,false);
  const FString Path=FString::Printf(TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial%02d.StreetMaterial%02d"),Material,Material);
  if(auto* M=LoadObject<UMaterialInterface>(nullptr,*Path))C->SetMaterial(0,M);Pose.Props.Add(A);
 };
 Part(FVector(0,-15,41),FVector(47,45,6),8);if(!SeatOnly)Part(FVector(0,-40,71),FVector(47,6,60),8);
 for(float X:{-19.f,19.f})for(float Y:{-33.f,3.f})Part(FVector(X,Y,19),FVector(5,5,38),8);
 if(!SeatOnly&&OKLifeTableClip(Clip)){
  Part(FVector(0,62,69),FVector(90,70,6),8);for(float X:{-38.f,38.f})for(float Y:{32.f,92.f})Part(FVector(X,Y,33),FVector(6,6,66),8);
  Part(FVector(0,60,73),FVector(30,24,3),7);
 }
}

void AOKStreetSocialScene::TickLife(float Dt){
 if(!State)return;auto& S=*State;const double Now=SceneTime();
 if(FParse::Param(FCommandLine::Get(),TEXT("StreetSocialFindLots")))RunFindLots();
 StopStrayPoses();TickBooks();TickPhone();TickPhotos();TickEffect();TickUmbrella();TickStageTest();TickCompany();TickCompanyWorld();TickCompanyTest();TickHeroCompanyTest();TickAccountsTest();TickActivityWatch();TickLifeBlocks();TickNpcLifts();TickBiasActions();TickIntroFriend();
 const int32 Minute=int32(OKStreetMindNow(S)%1440);
 // How far she is from where her schedule block puts her. After an outing or a walk she is wherever it ended, and
 // a seated loop there would put a chair in the middle of the street.
 auto SpotDistance=[&](int32 I,const FOKStreetPlanBlock& B){float Yaw=0;const FVector Spot=SpotFor(I,B.Place,B.Kind==TEXT("class")&&!B.Activity.IsEmpty()?B.Activity:B.Kind,Yaw);return float(FVector::Dist2D(S.Agents[I-1].Position,Spot));};
 // 1. Poses while the schedule holds them near him: the nearest ones, sharing the action slots with the ambient owner
 // (which waits while any pose plays). What he does in the foreground keeps two slots for the two of them; the rest go on
 // with their day (at lunch with Alice, five others stood frozen round the cafeteria instead of eating).
 const bool Foreground=S.Target!=INDEX_NONE||S.Pair.Partner!=INDEX_NONE||S.Facility!=INDEX_NONE||S.PrivateGirl!=INDEX_NONE||S.SoloGirl!=INDEX_NONE||S.SleepPhase!=0||S.LoverReact.Girl!=INDEX_NONE||S.LoverPose.Girl!=INDEX_NONE;
 const int32 LifeLimit=FMath::Max(0,FMath::Min(2,FMath::Max(0,OKCrowdRuntimeActionSlots-(Foreground?2:0)))-S.PartySeats.Num()); // the party sitting with them first (TickPartySeats)
 while(S.LifePoses.Num()>LifeLimit)StopLifePose(S.LifePoses.Last().Actor);
 {
  // Keep a pose while her block wants that clip and he is around; a closer heroine with something to do takes over.
  for(int32 K=S.LifePoses.Num()-1;K>=0;--K){
   auto& P=S.LifePoses[K];const int32 I=P.Actor;const FOKStreetPlanBlock* B=LifeBlock(I,Minute);
   const FString Want=B?LifeClip(I,*B):FString();
   const float Dist=S.Agents.IsValidIndex(I-1)?float(FVector::Dist2D(S.Agents[I-1].Position,S.Male)):MAX_flt;
   bool Nearer=false;
   if(Dist>800.f)for(const int32 J:S.ScheduleHeld)if(J!=I&&!InLifePose(J)&&S.Agents.IsValidIndex(J-1)&&!S.Commutes.Contains(J)&&FVector::Dist2D(S.Agents[J-1].Position,S.Male)<500.f){const auto* BJ=LifeBlock(J,Minute);if(BJ&&!LifeClip(J,*BJ).IsEmpty())Nearer=true;}
   if(Nearer){S.LifeCooldown.Add(I,Now+30.0);StopLifePose(I);continue;}
   if(!S.ScheduleHeld.Contains(I)||S.Commutes.Contains(I)||S.NpcLifts.Contains(I)||BusyWithPlayer(I)||Want!=P.Clip||Dist>OKLifeRange*1.3f||(B&&SpotDistance(I,*B)>700.f)){StopLifePose(I);continue;}
   if(P.Phase==0){
    FOKRuntimeActionStreamStatus A;
    if(Now-P.Requested>60){StopLifePose(I);continue;}
    if(OKGetCrowdRuntimeActionStatus(this,I,A)&&A.Ready&&!A.Pending&&!A.Preparing&&A.ClipId==P.Clip){FString Error;
     bool Played;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_LifePlay);Played=OKPlayCrowdRuntimeAction(this,I,P.Clip,Error);}
     if(Played){P.Phase=1;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_LifeProps);SpawnLifeProps(P);}UE_LOG(LogTemp,Display,TEXT("Street life: actor %d %s (%s)"),I,*P.Clip,B?*B->Kind:TEXT(""));}else StopLifePose(I);}
   }
  }
  if(S.LifePoses.Num()<LifeLimit&&S.AmbientActor==INDEX_NONE&&S.AllReady&&!S.SelfTest&&Now>=S.LifeCheckAt){
   S.LifeCheckAt=Now+2.0;int32 Best=INDEX_NONE;float BestD=OKLifeRange;FString Clip;
   for(const int32 I:S.ScheduleHeld){
    if(InLifePose(I)||!S.Agents.IsValidIndex(I-1)||S.Commutes.Contains(I)||S.NpcLifts.Contains(I)||BusyWithPlayer(I)||S.LifeCooldown.FindRef(I)>Now)continue;
    const FOKStreetPlanBlock* B=LifeBlock(I,Minute);if(!B)continue;const FString C=LifeClip(I,*B);if(C.IsEmpty())continue;
    if(SpotDistance(I,*B)>450.f){S.LifeCooldown.Add(I,Now+10.0);continue;} // not at her post: no chair here
    const float D=FVector::Dist2D(S.Agents[I-1].Position,S.Male);if(D<BestD){BestD=D;Best=I;Clip=C;}
   }
   if(Best!=INDEX_NONE){
    FString Error;S.LifeCooldown.Add(Best,Now+20.0);
    // Asleep: on her bed if the room has one (no bed: no sleeping pose). Seated loops bring their own chair.
    bool Placed=true;
    if(Clip==TEXT("facility_sleep.okanim")){const FOKStreetPlace* Where=nullptr;Placed=false;if(const auto* Bed=OKStreetNearestBed(S.Agents[Best-1].Position,900,nullptr,&Where)){S.LifeRestore.Add(Best,S.Agents[Best-1].Position);S.Agents[Best-1].Position=Bed->Position;S.Rotations[Best]=FRotator(0,Bed->Yaw,0);Placed=true;}}
    if(!Placed){S.LifeCooldown.Add(Best,Now+60.0);UE_LOG(LogTemp,Display,TEXT("Street life: actor %d has no seat/bed for %s"),Best,*Clip);}
    else if([&]{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_LifePrewarm);return OKPrewarmCrowdRuntimeAction(this,Best,Clip,Error);}()){FOKLifePose P;P.Actor=Best;P.Clip=Clip;P.Requested=Now;S.LifePoses.Add(P);}
    else{S.LifeRestore.Remove(Best);UE_LOG(LogTemp,Warning,TEXT("Street life: %s for actor %d: %s"),*Clip,Best,*Error);}
   }
  }
 }
 // 2. The best friends who share the morning walk side by side: her route follows her friend's, a step to the side.
 if(S.MorningPair.Key!=INDEX_NONE){
  const int32 A=S.MorningPair.Key,B=S.MorningPair.Value;
  if(Minute>=12*60)S.MorningPair={INDEX_NONE,INDEX_NONE};
  else if(S.Commutes.Contains(A)&&S.Commutes.Contains(B)&&S.Agents.IsValidIndex(A-1)&&S.Agents.IsValidIndex(B-1)&&S.LifePairRoute.FindRef(B)!=S.Commutes[A].Num()+A*1000&&LifeBlock(A,Minute)&&LifeBlock(B,Minute)&&LifeBlock(A,Minute)->Place==LifeBlock(B,Minute)->Place){ // only when they go to the same place
   const auto& Lead=S.Commutes[A];TArray<FVector> Mine;
   for(int32 K=0;K<Lead.Num();++K){const FVector Dir=(Lead[FMath::Min(K+1,Lead.Num()-1)]-Lead[FMath::Max(K-1,0)]).GetSafeNormal2D();Mine.Add(Lead[K]+FVector(-Dir.Y,Dir.X,0)*70.f);}
   if(!Mine.IsEmpty()){Mine[0]=S.Agents[B-1].Position;S.Commutes.Add(B,MoveTemp(Mine));S.LifePairRoute.Add(B,S.Commutes[A].Num()+A*1000);}
  }
 }
 // 3. Heroines her schedule puts at the same spot (a meeting, the morning pair, a jealous "chance" visit) face each
 // other instead of standing side by side looking the same way.
 if(Now>=S.LifeFaceAt){
  S.LifeFaceAt=Now+1.0;
  for(const int32 I:S.ScheduleHeld)for(const int32 J:S.ScheduleHeld){
   if(J<=I||InLifePose(I)||InLifePose(J)||S.Commutes.Contains(I)||S.Commutes.Contains(J)||!S.Agents.IsValidIndex(I-1)||!S.Agents.IsValidIndex(J-1)||BusyWithPlayer(I)||BusyWithPlayer(J))continue;
   const FOKStreetPlanBlock* BI=LifeBlock(I,Minute);const FOKStreetPlanBlock* BJ=LifeBlock(J,Minute);
   if(!BI||!BJ||BI->Place!=BJ->Place||!LifeClip(I,*BI).IsEmpty()||!LifeClip(J,*BJ).IsEmpty())continue;
   const FVector D=S.Agents[J-1].Position-S.Agents[I-1].Position;if(D.Size2D()>250.f||FMath::Abs(D.Z)>80.f)continue;
   // A conversation distance: a step apart, turned to each other.
   if(D.Size2D()<90.f){const FVector Push=(D.Size2D()<1.f?FVector(1,0,0):D.GetSafeNormal2D())*(90.f-float(D.Size2D()))*0.5f;FVector AtI=S.Agents[I-1].Position-Push,AtJ=S.Agents[J-1].Position+Push;if(!OKStreetBlockedPoint(AtI,25)&&!OKStreetBlockedPoint(AtJ,25)){S.Agents[I-1].Position=AtI;S.Agents[J-1].Position=AtJ;}}
   const FVector E=S.Agents[J-1].Position-S.Agents[I-1].Position;
   S.Rotations[I]=OKStreetFacingMovement(E,S.Rotations[I]);S.Rotations[J]=OKStreetFacingMovement(-E,S.Rotations[J]);
  }
 }
 // Development: -StreetSocialLifeTest=<dir> takes him to each heroine her schedule holds, in turn, and photographs
 // her pose there.
 FString LifeShots;
 if(S.AllReady&&FParse::Value(FCommandLine::Get(),TEXT("StreetSocialLifeTest="),LifeShots)){
  static double Next=0,ShotAt=0;static int32 Cursor=0,Shot=0,ShotActor=INDEX_NONE;
  if(Next==0)Next=Now+8.0;
  if(Now>=Next){
   Next=Now+16.0;TArray<int32> Held=S.ScheduleHeld.Array();Held.Sort();
   for(int32 K=0;K<Held.Num();++K){const int32 I=Held[(Cursor+K)%Held.Num()];if(!S.Agents.IsValidIndex(I-1)||S.Commutes.Contains(I))continue;
    Cursor=(Cursor+K+1)%FMath::Max(1,Held.Num());const FVector At=S.Agents[I-1].Position;
    const FVector Ahead=FRotator(0,S.Rotations[I].Yaw+90.f,0).Vector();FVector Stand=At+Ahead*230.f;if(OKStreetBlockedPoint(Stand,25))Stand=At-Ahead*230.f;
    S.Male=FVector(Stand.X,Stand.Y,At.Z);S.Yaw=(At-S.Male).Rotation().Yaw+50.f;S.Pitch=-14;S.Distance=240;ShotAt=Now+11.0;ShotActor=I; // over his shoulder, not through him
    const FOKStreetPlanBlock* B=LifeBlock(I,Minute);UE_LOG(LogTemp,Display,TEXT("Street life test: visiting actor %d (%s at %s) she is at %s [%s] held=%d planblock=%d urgent=%s closed=%d"),I,B?*B->Kind:TEXT("-"),B?*B->Place:TEXT("-"),*PlaceHere(At),*At.ToCompactString(),int32(S.Agents[I-1].Held),S.PlanBlock.FindRef(I),*S.LifeUrgent.FindRef(I),[&]{FString W;return B&&PlaceClosedNow(B->Place,W)?1:0;}());break;}
  }
  if(ShotAt>0&&Now>=ShotAt){ShotAt=0;const FOKStreetPlanBlock* B=LifeBlock(ShotActor,Minute);
   FScreenshotRequest::RequestScreenshot(LifeShots/FString::Printf(TEXT("life.%02d.%d.%s.png"),Shot++,ShotActor,B?*B->Kind:TEXT("none")),false,false);
   UE_LOG(LogTemp,Display,TEXT("Street life test: shot actor %d pose=%s"),ShotActor,InLifePose(ShotActor)?*S.LifePoses.FindByPredicate([](const FOKLifePose& P){return P.Actor==ShotActor;})->Clip:TEXT("idle"));}
 }
 // 4. Rain: streaks round him and a dimmer sun (half) while it rains outdoors.
 const bool Raining=!S.SelfTest&&(RainingAt(S.Store.CalendarDay,Minute)||FParse::Param(FCommandLine::Get(),TEXT("StreetSocialRain")));
 if(Raining&&!S.Rain.IsValid()){
  QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_LifeRainStart);
  auto* Actor=GetWorld()->SpawnActor<AActor>();
  if(Actor){
   auto* Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->RegisterComponent();
   auto* Ism=NewObject<UInstancedStaticMeshComponent>(Actor);Ism->SetupAttachment(Root);Ism->RegisterComponent();
   Ism->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Scenes/StreetSocial/Meshes/StreetCube.StreetCube")));Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);Ism->SetCastShadow(false);Ism->SetMobility(EComponentMobility::Movable);
   if(auto* Tint=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"))){auto* M=UMaterialInstanceDynamic::Create(Tint,Actor);M->SetVectorParameterValue(TEXT("Color"),FLinearColor(.85f,.88f,.95f));Ism->SetMaterial(0,M);}
   S.RainDrops.Reset();FRandomStream Rng(S.Store.CalendarDay);
   for(int32 K=0;K<OKRainDrops;++K){const FVector2D P=FVector2D(Rng.FRandRange(-1,1),Rng.FRandRange(-1,1))*OKRainRadius;S.RainDrops.Add(FVector(P.X,P.Y,Rng.FRandRange(0,OKRainHeight)));Ism->AddInstance(FTransform(FRotator::ZeroRotator,FVector::ZeroVector,FVector(.008f,.008f,.45f)));}
   S.Rain=Actor;S.RainMesh=Ism;UE_LOG(LogTemp,Display,TEXT("Street rain: starts (%02d:%02d)"),Minute/60,Minute%60);
  }
 }
 else if(!Raining&&S.Rain.IsValid()){S.Rain->Destroy();S.Rain.Reset();S.RainMesh.Reset();UE_LOG(LogTemp,Display,TEXT("Street rain: stops"));}
 if(S.Rain.IsValid()&&S.RainMesh.IsValid()){
  // Indoors (a place's room) the streaks hide; outdoors they fall round the camera's focus.
  const bool Inside=!PlaceHere(S.Male).IsEmpty();S.Rain->SetActorHiddenInGame(Inside);
  if(!Inside){
   auto* Ism=S.RainMesh.Get();const float Fall=1100.f*FMath::Min(Dt,.05f);
   for(int32 K=0;K<S.RainDrops.Num();++K){FVector& P=S.RainDrops[K];P.Z-=Fall;if(P.Z<0)P.Z+=OKRainHeight;
    Ism->UpdateInstanceTransform(K,FTransform(FRotator::ZeroRotator,FVector(S.Male.X+P.X,S.Male.Y+P.Y,S.Male.Z+P.Z-100.f),FVector(.008f,.008f,.45f)),true,false,true);}
   Ism->MarkRenderStateDirty();
  }
  // Every frame: the clock's own sun update (once a second) would otherwise flash full sunlight between.
  if(S.ClockDrivesSun&&S.Sun.IsValid())S.Sun->GetLightComponent()->SetIntensity(OKStreetSunAt(OKStreetClockHours(S.ClockStartHours,FPlatformTime::Seconds()-S.ClockStartWall)).Intensity*.5f);
 }
 // 5. Her own wish (plan §12.4, her idea): a lover alone with him in a lockable room, in a good mood, once a day.
 // Not in the middle of a song (三月七 stopped her own song for him to ask, R4-6).
 if(S.Target!=INDEX_NONE&&S.RemoteMode==0&&!S.FreeTalkBusy&&!SongIsSinging()&&S.PendingIntimacy==INDEX_NONE&&S.PrivateGirl==INDEX_NONE&&S.LifeWishDay!=S.Store.CalendarDay&&Now>=S.LifeWishAt){
  S.LifeWishAt=Now+30.0;const int32 I=S.Target;
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);const FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
  // Not while she is cross with him: jealous a moment ago ("who were you so close with today?") she whispered she wanted
  // to be closer (R3-116).
  const bool Low=R.Emotion==OKT(TEXT("ui.life.mood_low"))||R.Emotion==OKT(TEXT("ui.life.tired_mood"))||R.Emotion==OKT(TEXT("ui.life.angry_mood"))
   ||R.Emotion==OKT(TEXT("ui.life.jealous_mood"))||R.Emotion==OKT(TEXT("ui.life.mocking_mood"))||R.Jealousy>=40.f||R.Resentment>=30.f;
  // Not with her class, shift or a promise within the hour (Raiden asked 51 minutes before her shift and missed it).
  bool Due=false;{const int32 Clock=int32(OKStreetMindNow(S)%1440);if(const TArray<FOKStreetPlanBlock>* Plan=S.Plans.Find(I))for(const auto& B:*Plan)
   if((B.Kind==TEXT("class")||B.Kind==TEXT("work")||B.Kind==TEXT("promise"))&&B.End>Clock&&B.Start<=Clock+60)Due=true;}
  if(R.Stage==TEXT("lover")&&OKStreetNormsOf(PlaceHere(S.Male)).Privacy>=2&&R.Attraction>=55&&R.Energy>=30&&!Low&&!Due&&FMath::FRand()<0.15f)AskIntimacyWish(I);
 }
}

// Her wish said, and the choice it leaves him. She asks; he answers (the buttons, or in words). Her wish took him straight
// into the one-on-one scene, which only its own return button leaves (R3-228). Not answered in 40 s is not a yes.
void AOKStreetSocialScene::AskIntimacyWish(int32 I){
 auto& S=*State;S.LifeWishDay=S.Store.CalendarDay;
 if(S.Choices.ContainsByPredicate([I](const FOKStreetPendingChoice& C){return C.Kind==TEXT("intimacy")&&C.Heroine==I;}))return; // asked already, not answered yet
 S.SystemNote=true;S.FreeTalkDraft=OKT(TEXT("ui.life.whisper_closer_scene"));SendFreeTalk();
 MindObserve(I,TEXT("intimacy"),OKT(TEXT("ui.life.i_propose_closer")),PlaceHere(S.Male),0.6f);
 TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 // (No lean: his answer is not argued down.)
 OpenChoice(I,TEXT("intimacy"),OKT(TEXT("ui.life.intimacy_wish_choice")),{TEXT("accept"),TEXT("decline")},FString(),0.f,40,[Weak,I](const FString& Option){
  if(!Weak.IsValid()||!Weak->State)return;
  if(Option==TEXT("accept")){UE_LOG(LogTemp,Display,TEXT("Street life: actor %d's wish for intimacy, he accepted"),I);Weak->RequestIntimacy(I,true);return;}
  auto& L=*Weak->State;L.SystemNote=true;L.FreeTalkDraft=OKT(TEXT("ui.life.intimacy_wish_declined"));Weak->SendFreeTalk();
  UE_LOG(LogTemp,Display,TEXT("Street life: actor %d's wish for intimacy, he declined"),I);});
 UE_LOG(LogTemp,Display,TEXT("Street life: actor %d asks for intimacy herself (asks him)"),I);
}

// ---- 2026-10-12 plan, stage 1 (Docs/STREET_BEHAVIOR_PLAN_20261012.md) ----

// 1-1 A money reminder (entrust / help) is not a meeting: it is done once that money is settled. Being overdue is
// TickVentures' business (once); the reminder stays on the list until then instead of turning into a no-show.
bool AOKStreetSocialScene::EntrustReminderDone(const FOKStreetAppointment& A) const{
 return !State->Store.Entrusts.ContainsByPredicate([&](const FOKStreetEntrust& E){return E.Heroine==A.Heroine&&E.Status==TEXT("open");});
}

// 1-2 She has a reason to come to him (Director) and nothing fixed on now: her schedule lets her go.
bool AOKStreetSocialScene::InitiativeFree(int32 I) const{
 const auto& S=*State;if(!S.InitiativeReason.Contains(I))return false;
 const FOKStreetPlanBlock* B=LifeBlock(I,int32(OKStreetMindNow(S)%1440));
 return !B||!(B->Kind==TEXT("class")||B->Kind==TEXT("work")||B->Kind==TEXT("promise")||B->Kind==TEXT("sleep"));
}
// Off her schedule while she goes to him; after the talk TickSchedules sends her on (the block index no longer matches).
void AOKStreetSocialScene::ReleaseFromSchedule(int32 I){
 auto& S=*State;StopLifePose(I);
 S.ScheduleHeld.Remove(I);S.Commutes.Remove(I);S.CommuteFinal.Remove(I);S.PlanBlock.Add(I,-9);
}

// 1-3 A bill she cannot cover leaves her at 0 with a debt, income pays it first: game rules (day.rs charge_bill / settle_debt).

// The morning N wakes him at: 07:00, or half an hour before a meeting that morning (not before 04:00), so an early
// date (training at seven) is not slept through.
int64 AOKStreetSocialScene::SkipWakeMinute() const{
 const auto& S=*State;const int64 Now=OKStreetMindNow(S);const int64 Day=Now/1440+((Now%1440)>=240?1:0);
 int64 Wake=Day*1440+7*60;
 for(const auto& A:S.Store.Appointments)if(A.Status==TEXT("pending")&&A.Kind!=TEXT("entrust")&&A.Start>=Day*1440+240&&A.Start-30<Wake)Wake=FMath::Max<int64>(Day*1440+240,A.Start-30);
 return Wake;
}

// 1-5 N skips to the morning: an appointment in between asks first; a second N within 6 s skips it (a no-show).
bool AOKStreetSocialScene::SkipConfirmed(){
 auto& S=*State;
 const FOKStreetAppointment* Next=AppointmentBefore(SkipWakeMinute()-1);if(!Next)return true;
 if(SceneTime()-S.SkipAskedAt<6.0){S.SkipAskedAt=-100;return true;}
 S.SkipAskedAt=SceneTime();S.Message=OKMsg(TEXT("ui.life.pending_promise_skip"), {{TEXT("next_label"), *Next->Label}});RefreshUI();return false;
}

// ---- stage 3: one set of rules ----

// 3-1 Every panel entry asks her the same way as free talk: this turn's invitation stance, then the rule for that
// kind of invitation. bNotify: say her refusal (off when a voiced refusal scene plays instead).
bool AOKStreetSocialScene::PanelConsent(int32 Girl,const FString& Action,bool bNotify){
 auto& S=*State;if(Girl<=0||!S.Manifest.Characters.IsValidIndex(Girl))return false;
 if(S.OutingGirl==Girl&&OKRulesInviteKind(S,Action)!=int32(EOKStreetInvite::Private))return true; // already out together: the activity there was the invitation
 RollInviteStance(Girl);FString Why;
 if(InviteAllowedBiased(Girl,Action,Why))return true;
 S.MindRecent.Add(OKStreetStableId(S.Manifest.Characters[Girl]),OKT(TEXT("ui.life.he_invites_you"))+Action+OKT(TEXT("ui.life.you_refused_open_paren"))+Why+OKT(TEXT("ui.finance_apps.close_paren")));
 if(bNotify){S.Message=OKMsg(TEXT("ui.heroine.refused_reason"), {{TEXT("name"), *S.Manifest.Characters[Girl].Name}, {TEXT("reason"), *Why}});RefreshUI();}
 UE_LOG(LogTemp,Display,TEXT("Street consent refused: actor=%d action=%s (%s)"),Girl,*Action,*Why);
 return false;
}
// The one-on-one scene: friends at least, then the private-room invitation.
bool AOKStreetSocialScene::PrivateConsent(int32 Girl){
 auto& S=*State;if(Girl<=0||!S.Manifest.Characters.IsValidIndex(Girl))return false;
 if(AloneConsent(Girl,TEXT("rest")))return true; // lovers: the invitation; others: chastity against his charm (Heroine.inl)
 // Asked to her face: she answers it now in her own words (why is in her recent context), not only in a line on the screen.
 if(FreeTalkPartner()==Girl&&!S.FreeTalkBusy&&S.FreeTalkDraft.IsEmpty()){S.SystemNote=true;S.FreeTalkDraft=OKT(TEXT("ui.heroine.private_refused_prompt"));SendFreeTalk();}
 return false;
}

// 3-2 Touching her needs the relationship for it: the gates are the heroine module's table (OKStreetHeroine.h),
// the refusal is HeroineRefuseGate (OKStreetSocialHeroine.inl).

// 3-3 One relationship: favor and feelings made one again each day, friends after days of talking, a faded lover breaking
// up are the rules' (Tools/ok-rules day.rs day_people, from NextDay).

// ---- stage 4: what activities and her day leave behind ----

// An activity together ended (pair, photo, solo, facility): its time, price, who paid, what it did to her and the memory
// are rules (shop.rs activity_end); here the clock moves on and the money goes through the books.
void AOKStreetSocialScene::ActivityAftermath(const FString& ActivityId,int32 Girl,int64 StartMinute){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl)||Girl<=0||S.SelfTest)return;
 const FOKStreetActivity* A=nullptr;for(const auto& X:OKStreetActivities())if(ActivityId==X.Id)A=&X;
 if(A&&A->Kind==EOKStreetActivityKind::Restroom)return;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);
 const FString PlaceId=A&&A->Place&&*A->Place?FString(A->Place):PlaceHere(S.Male);const auto* P=PlaceId.IsEmpty()?nullptr:OKStreetFindPlace(PlaceId);
 const FString Name=A?FString(A->Name):ActivityId==TEXT("photo")?FString(OKT(TEXT("ui.life.take_photo_together"))):ActivityId==TEXT("sit_talk")?FString(OKT(TEXT("ui.life.sit_and_chat"))):ActivityId==TEXT("phone_together")?FString(OKT(TEXT("ui.life.scroll_phone_together"))):ActivityId;
 // Same thing again today: counted from her history.
 int32 Again=0;const int64 Today=OKStreetMindNow(S)/1440;
 for(const auto& E:HistoryOf(Id))if(E.Kind==TEXT("activity")&&E.Minute/1440==Today&&E.Text.Contains(Name))++Again;
 const TCHAR* Kind=!A?TEXT(""):A->Kind==EOKStreetActivityKind::Photo?TEXT("photo"):A->Kind==EOKStreetActivityKind::Solo?TEXT("solo"):A->Kind==EOKStreetActivityKind::Facility?TEXT("facility"):TEXT("pair");
 bool Promise=false;if(const int64* Until=S.HerTreatPromise.Find(Girl))Promise=*Until>=OKStreetMindNow(S);
 auto Request=OKRulesHeroineRequest(S,TEXT("activity_end"),Id);
 Request->SetStringField(TEXT("id"),ActivityId);Request->SetStringField(TEXT("kind"),Kind);Request->SetNumberField(TEXT("code"),A?A->Code:0);Request->SetStringField(TEXT("name"),Name);Request->SetStringField(TEXT("name_her"),S.Manifest.Characters[Girl].Name);
 Request->SetStringField(TEXT("place"),PlaceId);Request->SetNumberField(TEXT("month"),StreetNow().GetMonth());Request->SetNumberField(TEXT("again"),Again);Request->SetBoolField(TEXT("promise"),Promise);
 // A meal or a drink not paid at the counter comes from the menu. Paid at the counter means during it, or ordered just
 // before sitting down (she ordered on the way in: "my treat, a donut"); that bill is this meal's, not another one.
 // The order could not be paid, or he took it back: they ate nothing, not "a meal from the menu" billed at the end.
 Request->SetBoolField(TEXT("menu"),!S.ActCutShort&&A&&(A->Code==5||A->Code==2)&&A->Kind==EOKStreetActivityKind::Pair&&P&&S.LastCheckoutMinute<StartMinute-30&&S.OrderDroppedMinute<StartMinute-30
  // Dishes ordered there for this visit (within three hours: a private-room hour skipped in between made the half-hour
  // window miss them and the soup and tea were billed again as a set dinner).
  &&!(S.OrderedAtPlace.Contains(PlaceId)&&S.OrderedAtPlace[PlaceId]>=StartMinute-180));
 // What it is about (the machine's drink) bought from the menu and settled during it or just before: no fee on top (R3-240).
 Request->SetBoolField(TEXT("bought_at_counter"),S.LastCheckoutMinute>=StartMinute-30||(S.OrderedAtPlace.Contains(PlaceId)&&S.OrderedAtPlace[PlaceId]>=StartMinute-30));
 // At the machine, what they ordered here just before is the drink they had there: not still being made afterwards (it
 // is only served inside, and they drank at the door). 刻晴's tea, paid by chat, stayed "being made" after their machine
 // drink, to be "left behind" later (R5-2, R4-24).
 if(A&&A->FemaleClip&&FCString::Strstr(A->FemaleClip,TEXT("vending"))&&S.OrderedAtPlace.Contains(PlaceId)&&S.OrderedAtPlace[PlaceId]>=StartMinute-30){
  TSet<int32> Batches;for(int32 K=S.Serves.Num()-1;K>=0;--K)if(S.Serves[K].PlaceId==PlaceId){Batches.Add(S.Serves[K].Batch);S.Serves.RemoveAt(K);}
  const int32 Gone=S.Table.RemoveAll([&Batches](const FOKStreetDish& D){return !D.Served&&Batches.Contains(D.Batch);});
  if(Batches.Num())UE_LOG(LogTemp,Display,TEXT("Street activity: %d orders at %s were the machine's drink (%d dishes)"),Batches.Num(),*PlaceId,Gone);}
 // Cut short (the place closed on them): the minutes that really went by, not the activity's whole length.
 if(S.ActCutShort)Request->SetNumberField(TEXT("spent"),double(FMath::Max<int64>(0,OKStreetMindNow(S)-StartMinute)));Request->SetNumberField(TEXT("menu_seed"),int32(StartMinute)+Girl*31);Request->SetStringField(TEXT("agreed"),PayAgreed(Girl)); // who pays, as they settled it (the AI's reading)
 Request->SetNumberField(TEXT("elapsed"),double(FMath::Max<int64>(0,OKStreetMindNow(S)-StartMinute)));
 Request->SetNumberField(TEXT("player_cash"),double(S.Store.PlayerCash));Request->SetNumberField(TEXT("seed"),FMath::Rand());
 // The ticket of a place with an admission (aquarium, pool, onsen) is paid once a day there (shop.rs activity_end).
 Request->SetBoolField(TEXT("admitted"),S.AdmittedDay==S.Store.CalendarDay&&S.AdmittedPlace==PlaceId);
 OKRulesPutExt(S,Request); // and the ticket bought at the door today (entry_fee's ext.entries)
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 {bool Admission=false;if(Answer->TryGetBoolField(TEXT("admission"),Admission)&&Admission){S.AdmittedPlace=PlaceId;S.AdmittedDay=S.Store.CalendarDay;}}
 // Time: what the activity takes beyond the minutes that already went by while it played, never past an appointment.
 const int32 Want=int32(Answer->GetNumberField(TEXT("minutes")));const int64 Spent=OKStreetMindNow(S)-StartMinute;
 int64 Room=MAX_int64;for(const auto& X:S.Store.Appointments)if(X.Status==TEXT("pending")&&X.Kind!=TEXT("entrust")&&X.Start>OKStreetMindNow(S))Room=FMath::Min(Room,X.Start-5-OKStreetMindNow(S));
 // Nor past her own class, shift, promise or bedtime: the dessert ran on to 12:54 over her 12:41 class, and only then
 // she said she would be late (R3-226).
 {const int32 Minute=int32(OKStreetMindNow(S)%1440);if(const TArray<FOKStreetPlanBlock>* Plan=S.Plans.Find(Girl))for(const auto& B:*Plan)
  if(OKStreetFixedBlock(&B)&&B.Start>Minute)Room=FMath::Min<int64>(Room,B.Start-5-Minute);}
 if(!S.ClockPaused&&Want>Spent&&Room>0){
  const double Hours=OKStreetClockHours(S.ClockStartHours,FPlatformTime::Seconds()-S.ClockStartWall);
  const double Add=FMath::Min3(double(Want-Spent)/60.0,FMath::Max(0.0,23.75-Hours),double(FMath::Min<int64>(Room,24*60))/60.0); // never past the day's end or an appointment
  // What was ordered is on the table by then: the kitchen counts seconds of play, not the skipped half hour (her
  // cocoa came 40 minutes after she ordered it).
  if(Add>0){S.ClockStartHours+=Add;S.NextSunUpdate=0;for(auto& Due:S.Serves)Due.At=FMath::Min(Due.At,SceneTime());}
 }
 const int32 Price=int32(Answer->GetNumberField(TEXT("price")));
 // Used here only when something was paid (the vending machine): not for her drink at the next shop. A free activity
 // (reading at the library) leaves "my treat at the drink shop afterwards" for the drink shop.
 if(Price>0&&!PayAgreed(Girl).IsEmpty()&&S.PayAgreedShop.IsEmpty())S.PayAgreedShop=PlaceId;
 auto Candidate=S.Store;OKRulesTakeExt(Candidate,*Answer);ApplyRulesEffects(Candidate,*Answer);
 if(CommitStore(MoveTemp(Candidate)))FlushRulesEffects();
 if(Answer->GetBoolField(TEXT("treat_kept"))){S.HerTreatPromise.Remove(Girl);S.HerTreatKept.Add(Girl,OKStreetMindNow(S));}
 // The activity was just billed: what is still in the basket for this meal (her chat orders) would be paid a second time.
 if(Price>0&&!S.Cart.IsEmpty()){UE_LOG(LogTemp,Display,TEXT("Street activity aftermath: %d unpaid basket lines dropped, the activity bill covers them"),S.Cart.Num());S.Cart.Reset();}
 // What the activity leaves (activities.toml yields): reading together moves both books on.
 {const TArray<TSharedPtr<FJsonValue>>* Yields=nullptr;if(Answer->TryGetArrayField(TEXT("yields"),Yields))for(const auto& Y:*Yields)if(Y->AsString()==TEXT("book_progress"))ReadBooks(Girl,FMath::Max(Want,int32(OKStreetMindNow(S)-StartMinute)),FString());
   else if(Y->AsString()==TEXT("drawing"))HerPhoto(Girl,FString(),TEXT("scene"),false,true); // what she drew: the view in front of her
   else if(Y->AsString()==TEXT("work_progress"))HelpedHerWork(Girl,PlaceId,FMath::Max(Want,int32(OKStreetMindNow(S)-StartMinute)));
   else if(Y->AsString()==TEXT("study"))StudiedTogether(Girl,int32(OKStreetMindNow(S)-StartMinute));} // the minutes they actually sat over it
 TMap<FString,float> Feed;for(const auto& E:Answer->GetObjectField(TEXT("feed"))->Values)Feed.Add(FString(*E.Key),float(E.Value->AsNumber()));
 FeedHer(Girl,Feed);
 S.FreeTalkNotice=Answer->GetStringField(TEXT("notice"));RefreshUI();
 S.ActCutShort=false;
 // What it used up (the fish food), now that it is over (activities.rs at_end).
 {FString Ignore;ActivityCheck(Girl,ActivityId,Ignore,true,true);}
 // Done with her today (activities.rs once_per_day).
 {const int64 Day=OKStreetMindNow(S)/1440;if(S.ActivitiesDoneDay!=Day){S.ActivitiesDone.Reset();S.ActivitiesDoneDay=Day;}S.ActivitiesDone.FindOrAdd(Girl).AddUnique(ActivityId);}
 UE_LOG(LogTemp,Display,TEXT("Street activity aftermath: actor=%d %s minutes=%d price=%d payer=%s again=%d"),Girl,*ActivityId,Want,Price,*Answer->GetStringField(TEXT("payer")),Again);
 // A date with her there later today is kept by this (social.rs appointment_done_early): dinner promised for 19:00 and
 // eaten at 16:30 stayed pending, and skipping to the morning counted him a no-show.
 {const FString HerId=OKStreetStableId(S.Manifest.Characters[Girl]);auto Ask=OKRulesRequest(TEXT("appointment_done_early"));
  Ask->SetStringField(TEXT("heroine"),HerId);Ask->SetStringField(TEXT("place"),PlaceId);Ask->SetNumberField(TEXT("now"),double(OKStreetMindNow(S)));
  // Only what was made before it began: "after work at five, here" agreed over breakfast at the cafe is for later, and
  // it was crossed off as kept when breakfast ended at ten.
  TArray<TSharedPtr<FJsonValue>> Rows;for(const auto& Ap:S.Store.Appointments)if(Ap.Id<S.ActFirstAppointment){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("id"),Ap.Id);O->SetStringField(TEXT("kind"),Ap.Kind);
   O->SetStringField(TEXT("heroine"),Ap.Heroine);O->SetStringField(TEXT("place"),Ap.Place);O->SetStringField(TEXT("label"),Ap.Label);O->SetNumberField(TEXT("start"),double(Ap.Start));
   O->SetNumberField(TEXT("grace"),Ap.Grace);O->SetStringField(TEXT("status"),Ap.Status);Rows.Add(MakeShared<FJsonValueObject>(O));}
  Ask->SetArrayField(TEXT("appointments"),Rows);
  const TArray<TSharedPtr<FJsonValue>>* Done=nullptr;
  if(const TSharedPtr<FJsonObject> Early=OKRulesEval(S,Ask);Early.IsValid()&&Early->TryGetArrayField(TEXT("done"),Done)&&!Done->IsEmpty()){
   TSet<int32> Ids;for(const auto& V:*Done)Ids.Add(int32(V->AsNumber()));auto Kept=S.Store;
   for(const auto& Ap:Kept.Appointments)if(Ids.Contains(Ap.Id))UE_LOG(LogTemp,Display,TEXT("Street appointment %d: %s kept early (%s with her there now)"),Ap.Id,*Ap.Label,*ActivityId);
   Kept.Appointments.RemoveAll([&](const FOKStreetAppointment& Ap){return Ids.Contains(Ap.Id);});CommitStore(MoveTemp(Kept));}}
}

// Watches what is going on with him: an activity starting, and its end (whichever way it ends).
void AOKStreetSocialScene::TickActivityWatch(){
 auto& S=*State;
 const int32 Girl=S.Pair.Partner!=INDEX_NONE?S.Pair.Partner:S.SoloGirl!=INDEX_NONE?S.SoloGirl:S.Facility!=INDEX_NONE?S.FacilityCharacter:INDEX_NONE;
 // What kind of activity it is. One activity replaced by another with the same girl (watching the fish, then a photo
 // in front of the tank) ends there and settles as itself, for the minutes it lasted; the new one starts. Only the
 // girl was watched: the photo settled as forty minutes of watching the fish with its ticket, and the judge was told
 // they were still watching the fish.
 const FString Sig=Girl==INDEX_NONE?FString():S.Pair.Partner!=INDEX_NONE?FString::Printf(TEXT("pair:%d%d%d%d"),S.Pair.Seated?1:0,S.Pair.PhoneUse?1:0,S.Pair.Meal?1:0,S.Pair.Drink?1:0)
  :S.SoloGirl!=INDEX_NONE?TEXT("solo:")+S.SoloId:FString::Printf(TEXT("facility:%d"),S.Facility);
 if(Girl!=INDEX_NONE&&S.ActGirl==Girl&&!S.ActSig.IsEmpty()&&Sig!=S.ActSig){
  UE_LOG(LogTemp,Display,TEXT("Street activity: actor=%d %s (%s) gives way to %s"),Girl,*S.ActId,*S.ActSig,*Sig);
  S.ActGirl=INDEX_NONE;if(!S.ActLover){S.ActCutShort=true;ActivityAftermath(S.ActId,Girl,S.ActStart);}
 }
 if(Girl!=INDEX_NONE&&S.ActGirl==INDEX_NONE){
  S.ActGirl=Girl;S.ActSig=Sig;S.ActStart=OKStreetMindNow(S);S.ActFirstAppointment=S.Store.NextAppointment;S.ActCutShort=false;S.ActLover=S.LoverPose.Girl!=INDEX_NONE; // a kiss or an embrace is not an outing activity
  if(S.LastActivityGirl==Girl&&!S.LastActivity.IsEmpty()&&SceneTime()-S.LastActivityAt<20.0)S.ActId=S.LastActivity;
  else if(const auto* F=S.Facility!=INDEX_NONE?OKStreetFacilityActivity(S.Facility):nullptr)S.ActId=F->Id;
  else if(S.Pair.Partner!=INDEX_NONE)S.ActId=S.Pair.Seated?TEXT("sit_talk"):S.Pair.PhoneUse?TEXT("phone_together"):S.Pair.Meal?TEXT("meal"):S.Pair.Drink?TEXT("drink"):TEXT("photo");
  else S.ActId=S.SoloGirl!=INDEX_NONE&&!S.SoloId.IsEmpty()?S.SoloId:FString(TEXT("solo")); // the solo activity she is doing (watching the fish)
  S.LastActivity.Reset();S.LastActivityGirl=INDEX_NONE;
 }
 else if(Girl==INDEX_NONE&&S.ActGirl!=INDEX_NONE){
  const int32 Was=S.ActGirl;S.ActGirl=INDEX_NONE;
  if(!S.ActLover)ActivityAftermath(S.ActId,Was,S.ActStart);
 }
}

// 4-3 Her errand buys something there; a meeting of two friends brings them closer and each remembers the other.
void AOKStreetSocialScene::TickLifeBlocks(){
 auto& S=*State;const int64 Now=OKStreetMindNow(S);const int32 Minute=int32(Now%1440);
 if(Now==S.LifeBlocksMinute)return;S.LifeBlocksMinute=Now;
 for(const int32 I:S.ScheduleHeld){
  if(S.Commutes.Contains(I)||S.NpcLifts.Contains(I)||BusyWithPlayer(I))continue;
  int32 Index=INDEX_NONE;const FOKStreetPlanBlock* B=BlockNow(I,Minute,&Index);if(!B||B->Place.IsEmpty())continue;
  const FString Key=FString::Printf(TEXT("%d|%d|%d"),I,S.Store.CalendarDay,B->Start);if(S.LifeDone.Contains(Key))continue;
  const FString Id=OKStreetStableId(S.Manifest.Characters[I]);
  if(B->Kind==TEXT("errand")){
   S.LifeDone.Add(Key);
   const int32 RI=OKStreetRelationIndex(S.Store,Id,S.Manifest.TalkBudget,true);if(!S.Store.Relations.IsValidIndex(RI))continue;
   // What she buys is a rule (shop.rs errand).
   auto Request=OKRulesRequest(TEXT("errand"));Request->SetStringField(TEXT("place"),B->Place);Request->SetNumberField(TEXT("month"),StreetNow().GetMonth());Request->SetNumberField(TEXT("budget"),B->Budget);
   Request->SetNumberField(TEXT("cash"),double(S.Store.Relations[RI].Cash));Request->SetNumberField(TEXT("seed"),int32(Now)+I);
   const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer||!Answer->HasField(TEXT("item")))continue;
   const int32 Price=int32(Answer->GetNumberField(TEXT("price")));
   auto Candidate=S.Store;auto& R=Candidate.Relations[RI];if(R.Cash>=0)R.Cash-=Price;CommitStore(MoveTemp(Candidate));
   MindObserve(I,TEXT("errand"),OKMsg(TEXT("ui.life.bought_item_at_place"), {{TEXT("place_name"), OKStreetFindPlace(B->Place)?OKStreetFindPlace(B->Place)->Name:*B->Place}, {TEXT("item_name"), *Answer->GetStringField(TEXT("name"))}, {TEXT("price"), Price}}),B->Place,0.25f);
   UE_LOG(LogTemp,Display,TEXT("Street errand: actor %d bought %s at %s"),I,*Answer->GetStringField(TEXT("item")),*B->Place);
  }
  else if(B->Kind==TEXT("social")){
   // The friend she meets: the other heroine with the same meeting.
   for(const int32 J:S.ScheduleHeld){if(J==I||S.Commutes.Contains(J))continue;const FOKStreetPlanBlock* BJ=BlockNow(J,Minute);
    if(!BJ||BJ->Kind!=TEXT("social")||BJ->Place!=B->Place||BJ->Start!=B->Start)continue;
    const FString KeyJ=FString::Printf(TEXT("%d|%d|%d"),J,S.Store.CalendarDay,BJ->Start);S.LifeDone.Add(Key);S.LifeDone.Add(KeyJ);
    const FString IdJ=OKStreetStableId(S.Manifest.Characters[J]);
    auto Candidate=S.Store;FString BondKey=Id+TEXT("|")+IdJ;
    for(const auto& Bond:Candidate.BondHeat)if(Bond.Key.Contains(Id)&&Bond.Key.Contains(IdJ))BondKey=Bond.Key;
    // Their bond after meeting is a rule (shop.rs friends_met).
    auto Request=OKRulesRequest(TEXT("friends_met"));if(const float* Heat=Candidate.BondHeat.Find(BondKey))Request->SetNumberField(TEXT("heat"),*Heat);
    if(const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request)){Candidate.BondHeat.Add(BondKey,float(Answer->GetNumberField(TEXT("heat"))));CommitStore(MoveTemp(Candidate));}
    const FString Where=OKStreetFindPlace(B->Place)?OKStreetFindPlace(B->Place)->Name:B->Place;
    MindObserve(I,TEXT("friends"),OKMsg(TEXT("ui.life.party_chat_with_friend"), {{TEXT("friend_name"), *S.Manifest.Characters[J].Name}, {TEXT("place"), *Where}}),B->Place,0.3f);
    MindObserve(J,TEXT("friends"),OKMsg(TEXT("ui.life.party_chat_with_friend"), {{TEXT("friend_name"), *S.Manifest.Characters[I].Name}, {TEXT("place"), *Where}}),B->Place,0.3f);
    UE_LOG(LogTemp,Display,TEXT("Street friends: actors %d and %d meet at %s"),I,J,*B->Place);break;}
  }
 }
}

// 4-6 Seen by him, she takes the lift to her spot upstairs (to the lift, call it, in, her floor, out, to the spot)
// instead of appearing there. Out of his sight, or with no lift, she is simply placed (the caller does that).
bool AOKStreetSocialScene::StartNpcLift(int32 I,const FVector& Spot){
 auto& S=*State;if(!S.Agents.IsValidIndex(I-1)||FVector::Dist2D(S.Agents[I-1].Position,S.Male)>4000.f)return false;
 const FString Place=PlaceHere(Spot);int32 PlaceIndex=INDEX_NONE;const auto& Places=OKStreetPlaces();
 for(int32 K=0;K<Places.Num();++K)if(Place==Places[K].Id)PlaceIndex=K;
 const int32 Lift=PlaceIndex==INDEX_NONE?INDEX_NONE:ElevatorOfPlace(PlaceIndex);if(Lift==INDEX_NONE)return false;
 const auto& E=S.Elevators[Lift];auto Nearest=[&](float Z){int32 Best=0;for(int32 F=1;F<E.StopZ.Num();++F)if(FMath::Abs(E.StopZ[F]-Z)<FMath::Abs(E.StopZ[Best]-Z))Best=F;return Best;};
 FOKNpcLift L;L.Elevator=Lift;L.From=Nearest(S.Agents[I-1].Position.Z);L.To=Nearest(Spot.Z);L.Spot=Spot;L.At=SceneTime();
 if(L.From==L.To)return false;
 S.NpcLifts.Add(I,L);
 TArray<FVector> Path=PlaceRoute(S.Agents[I-1].Position,E.Landing[L.From]);if(Path.IsEmpty())Path.Add(E.Landing[L.From]);LiftWalk(I,Path);
 UE_LOG(LogTemp,Display,TEXT("Street lift: actor %d takes the lift from %s to %s"),I,*E.Labels[L.From],*E.Labels[L.To]);
 return true;
}

void AOKStreetSocialScene::TickNpcLifts(){
 auto& S=*State;const double Now=SceneTime();
 for(auto It=S.NpcLifts.CreateIterator();It;++It){
  const int32 I=It.Key();auto& L=It.Value();
  if(!S.Agents.IsValidIndex(I-1)||!S.Elevators.IsValidIndex(L.Elevator)){It.RemoveCurrent();continue;}
  auto& A=S.Agents[I-1];auto& E=S.Elevators[L.Elevator];A.Held=true;
  // Her plan moved on, or the lift is taking far too long (he holds it, it is busy): she is simply there.
  if(S.Commutes.Contains(I)||(BusyWithPlayer(I)&&!S.ArriveAfterLift.Contains(I))){It.RemoveCurrent();continue;}
  if(Now-L.At>90.0){A.Position=L.Spot;S.LiftWalks.Remove(I);UE_LOG(LogTemp,Display,TEXT("Street lift: actor %d gave up waiting, placed at her spot"),I);It.RemoveCurrent();continue;}
  if(S.LiftWalks.Contains(I))continue; // walking a leg
  const FVector Inside=FVector(E.Centre.X,E.Centre.Y,E.StopZ[L.From])-E.Out[L.From]*15.f+E.Side*(I%2?30.f:-30.f);
  switch(L.Stage){
  case 0: // at the landing: call the car here, step in once the doors are open
   if(E.Floor==L.From&&E.Phase==2){LiftWalk(I,{Inside});L.Stage=1;}
   else if(E.Target==INDEX_NONE){if(E.Floor==L.From&&(E.Phase==0||E.Phase==3))E.Phase=1;else if(E.Floor!=L.From)E.Target=L.From;}
   break;
  case 1: // inside: her floor (whoever is in the car rides along when it leaves)
   if(InElevatorCar(L.Elevator,A.Position)){if(E.Target==INDEX_NONE&&E.Phase==2){E.Target=L.To;E.OpenUntil=FMath::Min(E.OpenUntil,Now+.8);}if(E.Phase==4)L.Stage=2;}
   else L.Stage=0;
   break;
  case 2: // riding; out at her floor and on to her spot
   if(E.Floor==L.To&&(E.Phase==1||E.Phase==2)&&FMath::Abs(A.Position.Z-E.StopZ[L.To])<60.f){TArray<FVector> Path=PlaceRoute(E.Landing[L.To],L.Spot);if(Path.IsEmpty())Path.Add(L.Spot);Path.Insert(E.Landing[L.To],0);LiftWalk(I,Path);L.Stage=3;}
   break;
  default:
   UE_LOG(LogTemp,Display,TEXT("Street lift: actor %d reached her spot on %s"),I,*E.Labels[L.To]);S.NpcLiftArrivedZ.Add(I,A.Position.Z);It.RemoveCurrent();break;
  }
 }
}

// ---- stage 5 ----

// Her bias right now (the same inputs as the free-talk state block).
FOKStreetBias AOKStreetSocialScene::BiasOf(int32 Girl) const{
 const auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl)||Girl<=0)return FOKStreetBias();
 return OKRulesBiasFrom(MindBiasAnswer(Girl,false));
}

// 5-2 Deferring to him: a refusal she would give may still turn into a yes ("if you really want to...", social.rs
// bias_defer).
bool AOKStreetSocialScene::InviteAllowedBiased(int32 Character,const FString& Action,FString& Why){
 if(InviteAllowed(Character,Action,Why))return true;
 auto& S=*State;const float Defer=BiasOf(Character).DeferToPlayer;
 auto Request=OKRulesRequest(TEXT("bias_defer"));Request->SetNumberField(TEXT("defer"),Defer);Request->SetNumberField(TEXT("seed"),FMath::Rand());Request->SetStringField(TEXT("why"),Why);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer||!Answer->GetBoolField(TEXT("yes")))return false;
 S.MindRecent.Add(OKStreetStableId(S.Manifest.Characters[Character]),Answer->GetStringField(TEXT("recent")));
 UE_LOG(LogTemp,Display,TEXT("Street consent: actor=%d defers to him on %s (defer %.2f)"),Character,*Action,Defer);
 return true;
}

// Surveil / compete: he is with another heroine near her, she comes over; possessive display: walking with him and
// another heroine close, she takes his hand (social.rs bias_actions). Suspicion colours what she writes.
void AOKStreetSocialScene::TickBiasActions(){
 auto& S=*State;const double Now=SceneTime();if(Now<S.BiasCheckAt||S.SelfTest)return;S.BiasCheckAt=Now+30.0;
 // Why she would come to him goes stale: "you saw him with March 7th" at noon was her text to him at 18:43. A reason
 // is dated when first seen here and dropped after an hour of game time; a used one forgets its date.
 {const int64 Minute=OKStreetMindNow(S);for(auto It=S.InitiativeSince.CreateIterator();It;++It)if(!S.InitiativeReason.Contains(It.Key()))It.RemoveCurrent();
  for(auto It=S.InitiativeReason.CreateIterator();It;++It){const int64& Since=S.InitiativeSince.FindOrAdd(It.Key(),Minute);
   if(Minute-Since>60){UE_LOG(LogTemp,Display,TEXT("Street initiative: actor %d no longer (%s)"),It.Key(),*It.Value());S.InitiativeSince.Remove(It.Key());It.RemoveCurrent();}}}
 // Who he is with in person (not someone on the phone).
 const int32 With=S.Target!=INDEX_NONE?S.Target:S.Companion!=INDEX_NONE?S.Companion:S.OutingGirl;
 auto Request=OKRulesRequest(TEXT("bias_actions"));Request->SetNumberField(TEXT("seed"),FMath::Rand());
 TArray<TSharedPtr<FJsonValue>> Near;
 if(With>0&&S.Agents.IsValidIndex(With-1)){
  Request->SetStringField(TEXT("with_name"),S.Manifest.Characters[With].Name);
  for(int32 I=1;I<S.Manifest.Characters.Num();++I){
   if(I==With||!S.Agents.IsValidIndex(I-1)||BusyWithPlayer(I)||S.InitiativeReason.Contains(I)||!SeesHim(I))continue; // from the same side of the walls (walking past the restaurant she "saw him with Jeanne" inside)
   const FOKStreetBias B=BiasOf(I);auto H=MakeShared<FJsonObject>();H->SetNumberField(TEXT("actor"),I);H->SetNumberField(TEXT("surveil"),B.Surveil);H->SetNumberField(TEXT("compete"),B.Compete);
   H->SetNumberField(TEXT("dist"),FVector::Dist2D(S.Agents[I-1].Position,S.Male)/100.0);Near.Add(MakeShared<FJsonValueObject>(H)); // seen from within 10 m (social.rs bias_actions)
  }
 }
 Request->SetArrayField(TEXT("near"),Near);
 const int32 Walker=S.Companion;
 if(Walker>0&&S.Agents.IsValidIndex(Walker-1)&&S.Target==INDEX_NONE&&Now>=S.PossessiveAt.FindRef(Walker)){
  bool Rival=false;for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(I!=Walker&&S.Agents.IsValidIndex(I-1)&&FVector::Dist2D(S.Agents[I-1].Position,S.Male)<800.f)Rival=true;
  auto W=MakeShared<FJsonObject>();W->SetNumberField(TEXT("actor"),Walker);W->SetNumberField(TEXT("possessive"),BiasOf(Walker).PossessiveDisplay);W->SetBoolField(TEXT("rival_near"),Rival);Request->SetObjectField(TEXT("walker"),W);
 }
 if(Near.IsEmpty()&&!Request->HasField(TEXT("walker")))return;
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 for(const auto& V:Answer->GetArrayField(TEXT("come"))){
  const auto O=V->AsObject();const int32 I=int32(O->GetNumberField(TEXT("actor")));S.InitiativeReason.Add(I,O->GetStringField(TEXT("why")));
  if(S.ApproachNext.IsValidIndex(I))S.ApproachNext[I]=FPlatformTime::Seconds();
  UE_LOG(LogTemp,Display,TEXT("Street bias: actor %d comes over seeing him with %d"),I,With);
 }
 const int32 Hold=int32(Answer->GetNumberField(TEXT("hold_hands")));
 if(Hold>0){
  S.PossessiveAt.Add(Hold,Now+300.0);
  UE_LOG(LogTemp,Display,TEXT("Street bias: actor %d takes his hand in front of another heroine"),Hold);
  DoActHers(Hold,TEXT("hold_hands")); // her idea: no taking his hand before the relationship allows it
 }
}

// 5-4 Reservation: a table two hours from now at this place, for him and whoever he is with. Coming is a date kept
// (with her) or simply a table used; not coming puts him on the shop's blacklist.
void AOKStreetSocialScene::BookTable(const FString& Place){
 auto& S=*State;const auto* P=OKStreetFindPlace(Place);if(!P)return;
 if(S.Store.Appointments.ContainsByPredicate([](const FOKStreetAppointment& X){return X.Kind==TEXT("reservation")&&X.Status==TEXT("pending");})){S.Message=OKT(TEXT("ui.life.already_have_reservation"));RefreshUI();return;}
 const int64 Now=OKStreetMindNow(S);const int64 Start=(Now/60+2)*60;
 const int32 With=FreeTalkPartner()>0?FreeTalkPartner():S.Companion;
 const FString Label=OKMsg(TEXT("ui.life.reservation_booked_line"), {{TEXT("name"), P->Name}, {TEXT("hour"), (Start%1440)/60}, {TEXT("minute"), Start%60}, {TEXT("with_suffix"), With>0?*OKMsg(TEXT("ui.life.with_name_suffix"), {{TEXT("with_name"), *S.Manifest.Characters[With].Name}}):TEXT("")}});
 AddAppointment(TEXT("reservation"),With>0?With:INDEX_NONE,Place,Start,15,Label);
 if(With>0)MindObserve(With,TEXT("promise"),OKMsg(TEXT("ui.life.he_reserved_invite"), {{TEXT("place_name"), P->Name}, {TEXT("hour"), (Start%1440)/60}, {TEXT("minute"), Start%60}}),Place,0.6f);
 S.Message=OKT(TEXT("ui.life.booked_label"))+Label;RefreshUI();
}

FString AOKStreetSocialScene::SuspicionNote(int32 Girl) const{
 return BiasOf(Girl).Suspicion>0.5f?FString(OKT(TEXT("ui.life.suspect_cheating_thought"))):FString();
}

// ---- round 3 (Docs/STREET_BEHAVIOR_PLAN_20261012b.md) ----

// B1 Her pay for the minutes she was actually at work in this shift block (an interruption and her return are two
// stretches of the same shift); whatever was paid for it already is not paid again.
int32 AOKStreetSocialScene::HerShiftPay(int32 I,const FOKStreetPlanBlock& W,int32 Wage,int32 Minute){
 auto& S=*State;const int32 Entered=S.PlanBlockSince.Contains(I)?S.PlanBlockSince[I]:W.Start;
 const FString Key=FString::Printf(TEXT("%d|%d|%d"),I,S.Store.CalendarDay,W.Start);
 // Paid through which minute of this shift: a stretch is paid once, whatever calls this again.
 int32& Through=S.PaidShiftMinutes.FindOrAdd(Key);Through=FMath::Max(Through,W.Start);
 const int32 From=FMath::Max(Through,Entered),To=FMath::Min(Minute,W.End);const int32 Minutes=FMath::Max(0,To-From);
 Through=FMath::Max(Through,To);return Wage*Minutes/60;
}

// B2 A fixed block (class, work, a promise, the night) is only cut into when the need is really low.
bool OKStreetFixedBlock(const FOKStreetPlanBlock* B){
 return B&&(B->Kind==TEXT("class")||B->Kind==TEXT("work")||B->Kind==TEXT("promise")||B->Kind==TEXT("sleep"));
}

// D7 Nothing going on with him: no talk, outing, walk, activity, call, night or private scene.
bool AOKStreetSocialScene::NothingGoingOn() const{
 const auto& S=*State;
 return S.Target==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&S.OutingGirl==INDEX_NONE&&S.Companion==INDEX_NONE&&S.SoloGirl==INDEX_NONE&&S.Facility==INDEX_NONE
  &&S.PrivateGirl==INDEX_NONE&&S.SleepPhase==0&&S.RemoteMode==0&&S.RingGirl==INDEX_NONE&&S.ApproachGirl==INDEX_NONE&&S.RestroomGirl==INDEX_NONE;
}

// D1 An act the model or her own impulse chose: below the relationship for it she simply does not do it; no
// penalty for him.
bool AOKStreetSocialScene::DoActHers(int32 Character,const FString& Act){
 auto& S=*State;S.ActIsHers=true;const bool Done=DoAct(Character,Act);S.ActIsHers=false;return Done;
}

// D4 The friend she brought to be introduced goes back to her own day once the introduction is over.
void AOKStreetSocialScene::TickIntroFriend(){
 auto& S=*State;if(S.IntroFriend==INDEX_NONE)return;
 if(S.Target!=INDEX_NONE||S.ApproachGirl!=INDEX_NONE||SceneTime()-S.IntroAt<20.0)return;
 const int32 F=S.IntroFriend;S.IntroFriend=INDEX_NONE;
 if(S.Agents.IsValidIndex(F-1)&&!BusyWithPlayer(F)){ReleaseFromSchedule(F);S.Agents[F-1].Held=false;S.Agents[F-1].Mode=EOKStreetMove::Walking;}
 UE_LOG(LogTemp,Display,TEXT("Street initiative: introduced friend %d goes back to her day"),F);
}

// C5 Her own messages: at most three a day, half an hour apart (social.rs self_text).
bool AOKStreetSocialScene::SelfTextAllowed(int32 Girl){
 auto& S=*State;auto& Log=S.SelfTexts.FindOrAdd(Girl);
 auto Request=OKRulesRequest(TEXT("self_text"));Request->SetNumberField(TEXT("now"),double(OKStreetMindNow(S)));
 // Her messages since his last one (calls and texts): she does not keep writing into silence (social.rs self_text).
 {int32 Unanswered=0;if(S.Manifest.Characters.IsValidIndex(Girl)){const auto& List=HistoryOf(OKStreetStableId(S.Manifest.Characters[Girl]));for(int32 K=List.Num()-1;K>=0;--K){const auto& E=List[K];
   if(!OKStreetHistoryIsPhone(E.Kind))continue;if(E.Who==TEXT("him"))break;if(E.Who==TEXT("her")&&E.Kind==TEXT("message"))++Unanswered;}}
  Request->SetNumberField(TEXT("unanswered"),Unanswered);}
 TArray<TSharedPtr<FJsonValue>> Rows;for(const int64 M:Log)Rows.Add(MakeShared<FJsonValueNumber>(double(M)));Request->SetArrayField(TEXT("log"),Rows);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return false;
 Log.Reset();for(const auto& V:Answer->GetArrayField(TEXT("log")))Log.Add(int64(V->AsNumber()));
 return Answer->GetBoolField(TEXT("allowed"));
}

// D9 A lift's doors stay open for a girl walking into or out of that car, not for any walk anywhere.
bool AOKStreetSocialScene::LiftWalkNear(int32 Index) const{
 const auto& S=*State;if(!S.Elevators.IsValidIndex(Index))return false;const auto& E=S.Elevators[Index];
 for(const auto& W:S.LiftWalks)if(S.Agents.IsValidIndex(W.Key-1)&&FVector::Dist2D(S.Agents[W.Key-1].Position,E.Centre)<400.f&&FMath::Abs(S.Agents[W.Key-1].Position.Z-E.StopZ[E.Floor])<150.f)return true;
 return false;
}

// A3 The night or a nap could not start (the poses never came ready): everyone back as they were.
void AOKStreetSocialScene::AbortSleep(const FString& Why){
 auto& S=*State;FString Error;OKResumeCrowdRuntimeBehavior(this,0,Error);
 for(const int32 Girl:{S.SleepGirl,S.SleepGirl2})if(Girl!=INDEX_NONE&&S.Agents.IsValidIndex(Girl-1)){OKResumeCrowdRuntimeBehavior(this,Girl,Error);auto& A=S.Agents[Girl-1];A.Held=false;A.Mode=EOKStreetMove::Walking;A.Position=S.SleepBed+FRotator(0,S.SleepYaw,0).RotateVector(FVector(40,120,0));}
 S.Male=S.SleepBed+FRotator(0,S.SleepYaw,0).RotateVector(FVector(110,0,0));S.Male.Z=OKStreetFloorZ(S.SleepBed);
 S.SleepGirl=INDEX_NONE;S.SleepGirl2=INDEX_NONE;S.SleepPhase=0;S.SleepNap=false;S.SleepWake=false;
 S.Message=Why;RefreshUI();UE_LOG(LogTemp,Warning,TEXT("Street sleep aborted: %s"),*Why);
}

// C4/D11 Unseen messages she still means to send survive a restart (with the read marks, in phone.json).
void AOKStreetSocialScene::PhoneSave(){
 auto& S=*State;if(S.SelfTest||S.Manifest.SavePath.IsEmpty())return;
 TSharedRef<FJsonObject> Root=MakeShared<FJsonObject>(),Seen=MakeShared<FJsonObject>();for(const auto& P:S.PhoneSeen)Seen->SetStringField(P.Key,FString::Printf(TEXT("%lld"),P.Value));Root->SetObjectField(TEXT("seen"),Seen);
 TArray<TSharedPtr<FJsonValue>> Pending;
 for(const auto& T:S.PendingTexts)if(S.Manifest.Characters.IsValidIndex(T.Girl)){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("heroine"),OKStreetStableId(S.Manifest.Characters[T.Girl]));O->SetStringField(TEXT("why"),T.Why);O->SetStringField(TEXT("kind"),T.Kind);Pending.Add(MakeShared<FJsonValueObject>(O));}
 Root->SetArrayField(TEXT("pending"),Pending);
 FString Out;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Out));FFileHelper::SaveStringToFile(Out,*(FPaths::GetPath(S.Manifest.SavePath)/TEXT("phone.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// C3 Nothing going on with him except, possibly, this girl.
bool AOKStreetSocialScene::NothingGoingOnExcept(int32 Girl) const{
 const auto& S=*State;
 for(const int32 X:{S.Target,S.Pair.Partner,S.OutingGirl,S.Companion,S.SoloGirl,S.Facility!=INDEX_NONE?S.FacilityCharacter:INDEX_NONE,S.PrivateGirl,S.SleepGirl,S.RestroomGirl})if(X!=INDEX_NONE&&X!=Girl)return false;
 return S.SleepPhase==0;
}
