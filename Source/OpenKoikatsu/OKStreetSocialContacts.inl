// Included after scene state/UI definitions. All reservations belong to this scene.
bool AOKStreetSocialScene::ContactReady(int32 Character) const {
 if(!State||!State->Agents.IsValidIndex(Character-1)||!State->Manifest.Characters.IsValidIndex(Character))return false;
 FOKRuntimeCharacterStatus M,N;return OKGetCrowdRuntimeCharacterStatus(const_cast<AOKStreetSocialScene*>(this),0,M)&&OKGetCrowdRuntimeCharacterStatus(const_cast<AOKStreetSocialScene*>(this),Character,N)&&M.Visible&&N.Visible&&N.ActionsReady&&M.Error.IsEmpty()&&N.Error.IsEmpty();
}
bool AOKStreetSocialScene::ContactLover(int32 Character) const {return State&&State->Manifest.Characters.IsValidIndex(Character)&&OKStreetViewRelation(State->Store,OKStreetStableId(State->Manifest.Characters[Character]),State->Manifest.TalkBudget).Stage==TEXT("lover");}
bool AOKStreetSocialScene::ContactIdle() const {if(!State)return false;const auto& S=*State;return S.PrivateGirl==INDEX_NONE&&S.Pair.Partner==INDEX_NONE&&S.Facility==INDEX_NONE&&S.Companion==INDEX_NONE&&!OKStreetConversationBusy(S.Node!=INDEX_NONE,S.CollectedActive,S.RelationshipPending,S.TouchActive);}
TArray<FVector> AOKStreetSocialScene::ContactRoute(const FVector& From,const FVector& To) const {
 if(FMath::Abs(From.Z-To.Z)<150.f){auto Grid=GridRoute(From,To);if(Grid.Num()>=2)return Grid;}
 TArray<FOKStreetPointData> Shared;OKStreetWaypoints(Shared);TArray<FVector> Points;for(const auto& P:Shared)Points.Add(P.Position);
 return OKStreetContactRoute(From,To,Points,[&](const FVector& A,const FVector& B){
  const int32 Steps=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(A,B)/25));for(int32 I=0;I<=Steps;++I)if(OKStreetBlockedPoint(FMath::Lerp(A,B,float(I)/Steps),30))return false;
  FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(StreetContactRoute),false,this);const bool Blocked=GetWorld()->SweepSingleByChannel(Hit,A+FVector(0,0,85),B+FVector(0,0,85),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(30,78),Query);if(Blocked&&State->SelfTest)UE_LOG(LogTemp,Verbose,TEXT("Contact route obstructed actor=%s from=%s to=%s"),*GetNameSafe(Hit.GetActor()),*A.ToString(),*B.ToString());return !Blocked;
 });
}
void AOKStreetSocialScene::SelectContact(int32 Character){if(!State||!State->Agents.IsValidIndex(Character-1))return;State->ContactSelection=Character;RefreshUI(OKStreetUi::Right|OKStreetUi::Debug);}
void AOKStreetSocialScene::NavigateGirl(int32 Character){if(!ContactReady(Character)||State->PrivateGirl!=INDEX_NONE)return;auto& S=*State;
 // Talking or in an activity he cannot walk off: say so (as the map does) instead of accepting a route he never walks.
 if(S.Target!=INDEX_NONE||S.Pair.Partner!=INDEX_NONE||S.OutingGirl!=INDEX_NONE||S.Facility!=INDEX_NONE){S.Message=OKT(TEXT("ui.contacts.end_activity_before_nav"));S.ContactsNotice=S.Message;return;}
 // She is on another floor: the navigation takes the elevator (PlanNavLift, in StepNavigation).
 S.NavRide=FOKStreetLiftRide();S.NavGirl=Character;S.NavPointActive=false;S.NavAt=0;S.NavStep=1;S.NavReplans=0;S.NavProgressAt=SceneTime();S.NavPath=ContactRoute(S.Male,S.Agents[Character-1].Position);S.ContactsNotice=OKT(TEXT("ui.contacts.auto_travel_to_her_hint"));}
void AOKStreetSocialScene::NavigatePoint(const FVector& Goal,const FString& Name){
 if(!State)return;auto& S=*State;
 if(S.Target!=INDEX_NONE||S.PrivateGirl!=INDEX_NONE||S.Pair.Partner!=INDEX_NONE||S.OutingGirl!=INDEX_NONE||S.Facility!=INDEX_NONE){S.Message=OKT(TEXT("ui.contacts.end_activity_before_map_nav"));return;}
 // On his floor the street grid is searched a slice a frame (TickNavSearch, 3 ms each): a long route held the frame
 // 6-8 ms. Another floor: the place graph (doors and corridors), at once.
 S.NavSearch.Reset();
 if(FMath::Abs(S.Male.Z-Goal.Z)<150.f){S.NavSearch=MakeShared<FOKStreetGridSearch>();S.NavSearch->From=S.Male;S.NavSearch->To=Goal;S.NavSearchGoal=Goal;S.NavSearchName=Name;return;}
 NavigatePointAlong(Goal,Name,PlaceRouteWaypoints(S.Male,Goal));
}
void AOKStreetSocialScene::TickNavSearch(){
 if(!State||!State->NavSearch)return;auto& S=*State;
 if(!GridSearchStep(*S.NavSearch,FPlatformTime::Seconds()+0.003))return;
 TArray<FVector> Path=MoveTemp(S.NavSearch->Route);const FVector From=S.NavSearch->From;S.NavSearch.Reset();
 if(Path.Num()<2)Path=PlaceRouteWaypoints(From,S.NavSearchGoal); // as PlaceRoute: no grid way, the place graph
 NavigatePointAlong(S.NavSearchGoal,S.NavSearchName,MoveTemp(Path));
}
// The rest of NavigatePoint, once the route is known (computed once, then walked in order).
void AOKStreetSocialScene::NavigatePointAlong(const FVector& Goal,const FString& Name,TArray<FVector>&& Path){
 auto& S=*State;
 // A place's door: on through it into its ground floor. Stopped at the door he was outside it, and its buttons (the
 // menu, applying for a job) were not there.
 FVector Target=Goal;for(const auto& P:OKStreetPlaces())if(!P.Rooms.IsEmpty()&&FVector::Dist2D(P.Door,Goal)<5.f&&FMath::Abs(P.Door.Z-Goal.Z)<100.f){Target=PlaceInsideSpot(P);if(!Target.Equals(Goal,1.f)&&!Path.IsEmpty())Path.Add(Target);break;}
 UE_LOG(LogTemp,Display,TEXT("Street navigation: goal=%s name=%s points=%d"),*Goal.ToCompactString(),*Name,Path.Num());
 // On another floor (he upstairs, or the spot upstairs): the navigation takes an elevator (PlanNavLift); no street
 // route is needed from upstairs.
 const bool Floors=NavNeedsLift(Goal);
 if(Path.IsEmpty()&&!Floors){S.Message=OKT(TEXT("ui.contacts.unreachable_try_elsewhere"));return;}
 if(Path.IsEmpty())Path={S.Male};
 S.NavRide=FOKStreetLiftRide();S.NavGirl=INDEX_NONE;S.NavPointActive=true;S.NavPoint=Floors?Target:FVector(Target.X,Target.Y,S.Male.Z);S.NavPointName=Name;S.NavPath=MoveTemp(Path);S.NavStep=1;S.NavReplans=0;S.NavAt=SceneTime();S.NavProgressAt=S.NavAt;
 S.Message=OKT(TEXT("ui.contacts.auto_travel_button"))+(Name.IsEmpty()?FString(OKT(TEXT("ui.contacts.map_selected_spot"))):Name)+OKT(TEXT("ui.contacts.wasd_cancel_nav_suffix"));
 if(S.BigMapOpen)ToggleBigMap(); // navigation, its notice and the message are bound to state: no rebuild
}
void AOKStreetSocialScene::StopNavigation(const FString& Notice){
 if(!State)return;auto& S=*State;S.NavSearch.Reset();S.NavGirl=INDEX_NONE;S.NavPointActive=false;S.NavPath.Reset();S.NavRide=FOKStreetLiftRide();S.ContactsNotice.Reset();
 if(!Notice.IsEmpty())S.Message=Notice;
}
void AOKStreetSocialScene::ToggleBigMap(){
 if(!State)return;auto& S=*State;
 if(!S.BigMapOpen){S.BigMapOpen=true;S.BigMapSavedMouse=S.MouseUI;S.MouseUI=true;}
 else{S.BigMapOpen=false;S.MouseUI=S.BigMapSavedMouse;}
 ApplyMouseMode(true);
}
// Walkable-street grid: a cell is free when a body standing there keeps the same clearance walking needs (30 cm from
// street obstacles and place blocks, no overlap with world collision). Cells are tested when A* first reaches them.
static constexpr float OKStreetNavCell=40.f;
bool AOKStreetSocialScene::NavCellFree(const FIntPoint& Cell,float Z) const{
 auto& Grid=State->NavGrid.FindOrAdd(FMath::RoundToInt(Z/100.f));
 if(const bool* Known=Grid.Find(Cell))return *Known;
 QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_NavCellTest);
 const FVector P(Cell.X*OKStreetNavCell,Cell.Y*OKStreetNavCell,Z);
 bool Free=OKStreetObstacleGap(P,30.f)>30.f;
 if(Free){FCollisionQueryParams Query(SCENE_QUERY_STAT(StreetNavCell),false,this);Free=!GetWorld()->OverlapAnyTestByChannel(P+FVector(0,0,85),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(40,78),Query);} // wider than his 28: a cell beside an awning or a sign is not a way
 Grid.Add(Cell,Free);return Free;
}
TArray<FVector> AOKStreetSocialScene::GridRoute(const FVector& From,const FVector& To) const{
 FOKStreetGridSearch G;G.From=From;G.To=To;GridSearchStep(G,TNumericLimits<double>::Max());return MoveTemp(G.Route);
}
bool AOKStreetSocialScene::GridSearchStep(FOKStreetGridSearch& G,double Deadline) const{
 if(G.Phase==2)return true;
 const float Z=G.From.Z;const FVector From=G.From,To=G.To;
 auto ToCell=[](const FVector& P){return FIntPoint(FMath::RoundToInt(P.X/OKStreetNavCell),FMath::RoundToInt(P.Y/OKStreetNavCell));};
 auto Centre=[Z](const FIntPoint& C){return FVector(C.X*OKStreetNavCell,C.Y*OKStreetNavCell,Z);};
 if(G.Phase==0){
  G.Z=Z;G.Start=ToCell(From);G.Goal=ToCell(To);G.Phase=2;
  // A goal on a counter, a door frame or a bench: the nearest free cell around it.
  if(!NavCellFree(G.Goal,Z)){
   bool Found=false;
   for(int32 R=1;R<=6&&!Found;++R)for(int32 DX=-R;DX<=R&&!Found;++DX)for(int32 DY=-R;DY<=R;++DY){
    if(FMath::Max(FMath::Abs(DX),FMath::Abs(DY))!=R)continue;const FIntPoint C=G.Goal+FIntPoint(DX,DY);if(NavCellFree(C,Z)){G.Goal=C;Found=true;break;}}
   if(!Found)return true;
  }
  if(G.Start==G.Goal){G.Route={From,To};return true;}
  G.Lo=FIntPoint(FMath::Min(G.Start.X,G.Goal.X)-60,FMath::Min(G.Start.Y,G.Goal.Y)-60);G.Hi=FIntPoint(FMath::Max(G.Start.X,G.Goal.X)+60,FMath::Max(G.Start.Y,G.Goal.Y)+60);
  G.W=G.Hi.X-G.Lo.X+1;const int32 Cells=G.W*(G.Hi.Y-G.Lo.Y+1);
  {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_GridAlloc);G.Cost.Init(TNumericLimits<float>::Max(),Cells);G.Came.SetNumUninitialized(Cells);G.Cell.SetNumZeroed(Cells);}
  G.Phase=1;
 }
 const FIntPoint Start=G.Start,Goal=G.Goal,Lo=G.Lo,Hi=G.Hi;
 // Near the start, cells too close to a wall are allowed (dearly), so standing by one never traps him.
 // A cell he was stopped walking into (a thin wall between two free cell centres) is not passable, even next to the start.
 const TSet<FIntPoint>* Learned=State->NavLearned.Find(FMath::RoundToInt(Z/100.f));
 const int32 W=G.W;
 auto Index=[&](const FIntPoint& C){return (C.X-Lo.X)+(C.Y-Lo.Y)*W;};
 auto Known=[&](const FIntPoint& C)->uint8{
  if(C.X<Lo.X||C.Y<Lo.Y||C.X>Hi.X||C.Y>Hi.Y)return 1|(NavCellFree(C,Z)?2:0)|(Learned&&Learned->Contains(C)?4:0);
  uint8& K=G.Cell[Index(C)];if(!K)K=1|(NavCellFree(C,Z)?2:0)|(Learned&&Learned->Contains(C)?4:0);return K;};
 auto Passable=[&](const FIntPoint& C){const uint8 K=Known(C);if(K&4)return false;return (K&2)||FMath::Max(FMath::Abs(C.X-Start.X),FMath::Abs(C.Y-Start.Y))<=1;};
 auto Heuristic=[&](const FIntPoint& C){const float DX=FMath::Abs(C.X-Goal.X),DY=FMath::Abs(C.Y-Goal.Y);return DX+DY+(UE_SQRT_2-2.f)*FMath::Min(DX,DY);};
 auto& Came=G.Came;auto& Cost=G.Cost;auto& Open=G.Open;
 auto Less=[](const TPair<float,FIntPoint>& A,const TPair<float,FIntPoint>& B){return A.Key<B.Key;};
 static const FIntPoint Steps[]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
 auto Release=[&G](){G.Cost.Empty();G.Came.Empty();G.Cell.Empty();G.Open.Empty();G.Path.Empty();};
 if(G.Phase==1){
 if(G.Expanded==0&&Open.IsEmpty()&&Cost[Index(Start)]!=0.f){Cost[Index(Start)]=0.f;Open.HeapPush({Heuristic(Start),Start},Less);}
 bool Reached=false;
 while(!Open.IsEmpty()&&G.Expanded<60000){
  // Out of time: the search goes on from here next slice (the open list, costs and links are kept).
  if((G.Expanded&31)==0&&FPlatformTime::Seconds()>Deadline)return false;
  TPair<float,FIntPoint> Top;Open.HeapPop(Top,Less);const FIntPoint C=Top.Value;const float Here=Cost[Index(C)];
  if(Top.Key>Here+Heuristic(C)+.01f)continue; // stale entry
  if(C==Goal){Reached=true;break;}
  ++G.Expanded;
  for(const FIntPoint& D:Steps){
   const FIntPoint N=C+D;if(N.X<Lo.X||N.Y<Lo.Y||N.X>Hi.X||N.Y>Hi.Y||!Passable(N))continue;
   if(D.X!=0&&D.Y!=0&&(!Passable(FIntPoint(C.X+D.X,C.Y))||!Passable(FIntPoint(C.X,C.Y+D.Y))))continue; // no cutting corners
   const float Step=(D.X!=0&&D.Y!=0?UE_SQRT_2:1.f)*(Known(N)&2?1.f:8.f);
   const int32 NI=Index(N);const float New=Here+Step;
   if(New<Cost[NI]){Cost[NI]=New;Came[NI]=Index(C);Open.HeapPush({New+Heuristic(N),N},Less);}
  }
 }
 if(!Reached){G.Phase=2;UE_LOG(LogTemp,Display,TEXT("Street grid route: none from %s to %s (%d cells)"),*From.ToCompactString(),*To.ToCompactString(),G.Expanded);Release();return true;}
 TArray<FIntPoint>& Cells=G.Path;Cells={Goal};while(Cells.Last()!=Start){const int32 P=Came[Index(Cells.Last())];Cells.Add(FIntPoint(Lo.X+P%W,Lo.Y+P/W));}
 Algo::Reverse(Cells);
 // The pull still asks what is known of the cells (Cell), not the costs or links.
 G.Cost.Empty();G.Came.Empty();G.Open.Empty();G.Route={From};G.PullI=0;G.PullJ=-1;G.Phase=3;
 }
 // Pull the string: keep only the corners a straight walk cannot cut.
 // A straight line between two cell centres can still clip the corner of something between the cell centres: the body-sized
 // sweep along it must also be clear, or the walk stops at that corner ("the way is blocked") in the open street.
 FCollisionQueryParams SweepQuery(SCENE_QUERY_STAT(StreetGridClear),false,this);
 auto Clear=[&](const FVector& A,const FVector& B){
  const int32 N=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(A,B)/(OKStreetNavCell*.5f)));
  for(int32 I=1;I<N;++I)if(!Passable(ToCell(FMath::Lerp(A,B,float(I)/N))))return false;
  FHitResult Hit;
  // Starting against a wall counts as clear only when the line leads away from it (as StreetStep backs out of one).
  if(GetWorld()->SweepSingleByChannel(Hit,FVector(A.X,A.Y,Z)+FVector(0,0,85),FVector(B.X,B.Y,Z)+FVector(0,0,85),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(30,78),SweepQuery)&&(!Hit.bStartPenetrating||FVector::DotProduct((B-A).GetSafeNormal2D(),Hit.Normal)<=0.f))return false;
  return true;};
 QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_GridPull);
 // A slice at a time too: out of time between two tries, it goes on from the same corner and cell next slice.
 const TArray<FIntPoint>& Cells=G.Path;TArray<FVector>& Route=G.Route;
 while(G.PullI<Cells.Num()-1){const int32 I=G.PullI;if(G.PullJ<0)G.PullJ=Cells.Num()-1;
  while(G.PullJ>I+1&&!Clear(I==0?From:Centre(Cells[I]),Centre(Cells[G.PullJ]))){--G.PullJ;if(FPlatformTime::Seconds()>Deadline)return false;}
  Route.Add(Centre(Cells[G.PullJ]));G.PullI=G.PullJ;G.PullJ=-1;}
 if(Goal==ToCell(To)&&Clear(Route.Last(),To))Route.Last()=To;
 G.Phase=2;Release();return true;
}
FVector AOKStreetSocialScene::StreetStep(const FVector& From,const FVector& Direction,float Distance) const{
 constexpr float Clearance=30.f;
 FVector D=Direction;D.Z=0;if(Distance<=0||!D.Normalize())return From;
 const float Here=OKStreetObstacleGap(From,Clearance+10.f); // compared only below the clearance (and 3 cm more)
 FCollisionQueryParams Query(SCENE_QUERY_STAT(StreetStep),false,this);
 auto Free=[&](const FVector& To){
  // Into an obstacle's clearance is blocked, but backing out of one he is already too close to never is. Along a wall at
  // the clearance's edge (30 here, 29 a step on) he may slide: a few centimetres closer, never under 25 (the capsule
  // sweep below still stops him at the wall itself).
  const float There=OKStreetObstacleGap(To,Clearance+10.f);if(There<=Clearance&&(There<Clearance-5.f||There<Here-3.f))return false;
  FHitResult Hit;
  if(GetWorld()->SweepSingleByChannel(Hit,From+FVector(0,0,85),To+FVector(0,0,85),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(28,78),Query))
   if(!Hit.bStartPenetrating||FVector::DotProduct((To-From).GetSafeNormal2D(),Hit.Normal)<=0.f)return false;
  return true;};
 // Straight on, else slide along the obstacle: either axis, then 45 degrees either way (corners, posts, trees).
 const FVector Tries[]={D,FVector(FMath::Sign(D.X),0,0),FVector(0,FMath::Sign(D.Y),0),D.RotateAngleAxis(45,FVector::UpVector),D.RotateAngleAxis(-45,FVector::UpVector)};
 for(const FVector& T:Tries){if(T.IsNearlyZero()||FVector::DotProduct(T,D)<.3f)continue;const FVector To=From+T*Distance;if(Free(To))return To;}
 return From;
}
// Where to walk to speak to her: where she is, or, when she sits among furniture (a sofa, a bench by a table), the
// free spot within talking range nearest him.
FVector AOKStreetSocialScene::NavApproachSpot(const FVector& Her) const{
 if(!OKStreetBlockedPoint(Her,30))return Her;
 const FVector Male=State->Male;FVector Best=Her;float BestDistance=TNumericLimits<float>::Max();
 // On her side of the walls: reading by a shelf in the bookstore, the free spot nearest him was the pavement outside its
 // wall, and the navigation said "beside her" there.
 const FString HerPlace=PlaceHere(Her);
 for(const float R:{120.f,160.f,200.f})for(int32 A=0;A<18;++A){
  const float Angle=A*20.f*PI/180.f;const FVector P=Her+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*R;
  if(OKStreetBlockedPoint(P,30)||PlaceHere(P)!=HerPlace)continue;const float D=FVector::Dist2D(P,Male);if(D<BestDistance){BestDistance=D;Best=P;}}
 return Best;
}

// Walks the gold route at running pace. Arrival, a blocked path or her becoming unavailable ends it.
float AOKStreetSocialScene::StepNavigation(float Dt){
 auto& S=*State;
 if((S.NavGirl==INDEX_NONE&&!S.NavPointActive)||S.SelfTest||S.Target!=INDEX_NONE||S.Pair.Partner!=INDEX_NONE||S.OutingGirl!=INDEX_NONE||S.PrivateGirl!=INDEX_NONE||S.Facility!=INDEX_NONE)return 0;
 if(S.NavGirl!=INDEX_NONE&&!S.Agents.IsValidIndex(S.NavGirl-1)){StopNavigation(FString());return 0;}
 const FVector Her=S.NavPointActive?S.NavPoint:S.Agents[S.NavGirl-1].Position;
 const FVector Goal=S.NavPointActive?S.NavPoint:NavApproachSpot(Her);
 // He is walking himself: the autopilot waits, then goes on from where he is.
 if(SceneTime()-S.ManualMoveAt<3.0)return 0;
 // On another floor: to an elevator first, then the ride (the route there is the plan's).
 if(S.NavRide.Elevator==INDEX_NONE&&NavNeedsLift(Goal)&&!PlanNavLift(Goal)){StopNavigation(OKT(TEXT("ui.contacts.other_floor_no_path")));return 0;}
 if(S.NavRide.Elevator!=INDEX_NONE&&S.NavStep>=S.NavPath.Num()){
  float Rate=0;const int32 Done=AdvanceLiftRide(S.NavRide,Dt,Rate);
  if(Done==2){StopNavigation(OKT(TEXT("ui.contacts.path_blocked_stop")));return 0;}
  if(Done==1){const bool Down=S.NavRide.Descend;S.NavRide=FOKStreetLiftRide();
   // Off the car: on this floor the grid; down on the street the place graph. Still another floor away: plan again.
   const FVector Now=S.NavPointActive?S.NavPoint:S.Agents[S.NavGirl-1].Position;
   const bool More=NavNeedsLift(Now);
   TArray<FVector> Rest=More?TArray<FVector>{S.Male}:Down?(S.NavPointActive?PlaceRoute(S.Male,Now):ContactRoute(S.Male,Now)):GridRoute(S.Male,Now);
   if(Rest.Num()<2&&!More)Rest={S.Male,Now};
   if(S.NavPointActive&&!More)S.NavPoint.Z=S.Male.Z; // still another floor away: the goal keeps its height
   S.NavPath=MoveTemp(Rest);S.NavStep=1;S.NavReplans=0;S.NavProgressAt=SceneTime();}
  return Rate;
 }
 if(S.ManualReplan){S.ManualReplan=false;auto Path=S.NavPointActive?PlaceRoute(S.Male,Goal):ContactRoute(S.Male,Goal);if(Path.Num()>=2){S.NavPath=MoveTemp(Path);S.NavStep=1;S.NavReplans=0;S.NavProgressAt=SceneTime();}S.Message=OKT(TEXT("ui.contacts.resume_auto_travel"));RefreshUI();}
 // Close to her counts only on her side of the walls: in the same place (both in the bookstore, both outside), not
 // 1.2 m away through the shop's wall, where E cannot reach her.
 if((FVector::Dist2D(S.Male,Goal)<=(S.NavPointActive?80.f:40.f)||(!S.NavPointActive&&FVector::Dist2D(S.Male,Her)<=180.f&&PlaceHere(S.Male)==PlaceHere(Her)))&&FMath::Abs(S.Male.Z-Goal.Z)<150.f){
  // Walked up to her: if she was on her way somewhere she notices him, stops and turns to him for a moment, so he can
  // speak to her instead of chasing her down the street.
  if(!S.NavPointActive&&S.Agents.IsValidIndex(S.NavGirl-1)){const int32 G=S.NavGirl;S.CommutePausedUntil.Add(G,SceneTime()+10.0);
   if(S.Commutes.Contains(G))S.Rotations[G]=FRotator(0,(S.Male-S.Agents[G-1].Position).Rotation().Yaw,0);}
  StopNavigation(S.NavPointActive?OKT(TEXT("ui.contacts.arrived_label"))+(S.NavPointName.IsEmpty()?FString(OKT(TEXT("ui.contacts.destination_label"))):S.NavPointName)+OKT(TEXT("ui.contacts.full_stop")):OKT(TEXT("ui.contacts.arrived_short"))+S.Manifest.Characters[S.NavGirl].Name+OKT(TEXT("ui.contacts.beside_her_talk_hint")));return 0;}
 while(S.NavStep<S.NavPath.Num()&&FVector::Dist2D(S.Male,S.NavPath[S.NavStep])<=30)++S.NavStep;
 FVector Next=S.NavPath.IsValidIndex(S.NavStep)?S.NavPath[S.NavStep]:Goal;
 // Chasing her: close by and nothing in between, straight at where she is now (the route, drawn every 0.75 s, lags a
 // girl who is walking).
 if(!S.NavPointActive&&FMath::Abs(Goal.Z-S.Male.Z)<150.f&&FVector::Dist2D(S.Male,Goal)<900.f){
  bool Clear=true;const int32 Steps=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(S.Male,Goal)/40.f));
  for(int32 I=1;I<Steps&&Clear;++I)if(OKStreetBlockedPoint(FMath::Lerp(S.Male,Goal,float(I)/Steps),30))Clear=false;
  if(Clear)Next=Goal;
 }
 FVector Delta=Next-S.Male;Delta.Z=0;const float Distance=Delta.Size();if(Distance<1)return 0;
 const float Step=FMath::Min(Distance,OKStreetMaleRunSpeed*FMath::Min(Dt,.05f));
 const double Now=SceneTime();
 const FVector Moved=StreetStep(S.Male,Delta/Distance,Step);
 if(Moved==S.Male){
  // Blocked: find a new way from where he stands before giving up.
  if(Now-S.NavProgressAt>1.0){
   if(S.NavReplans>=6){StopNavigation(OKT(TEXT("ui.contacts.path_blocked_stop")));return 0;} // each try learns a blocked cell, so a few more find the way round
   ++S.NavReplans;S.NavProgressAt=Now;
   // Stopped by something solid the grid did not see: the cell ahead is known blocked from now on.
   {const FVector Ahead=S.Male+Delta/Distance*OKStreetNavCell;const FIntPoint Cell(FMath::RoundToInt(Ahead.X/OKStreetNavCell),FMath::RoundToInt(Ahead.Y/OKStreetNavCell));
    const FIntPoint Here(FMath::RoundToInt(S.Male.X/OKStreetNavCell),FMath::RoundToInt(S.Male.Y/OKStreetNavCell));
    if(Cell!=Here){S.NavLearned.FindOrAdd(FMath::RoundToInt(S.Male.Z/100.f)).Add(Cell);UE_LOG(LogTemp,Display,TEXT("Street navigation: cell (%d,%d) learned blocked"),Cell.X,Cell.Y);}}
   // Wedged (three stops in a row where he stands): out to the nearest free cell he can reach in a straight line, up to
   // four cells away, and the way again from there.
   if(S.NavReplans==3){
    const float Z=S.Male.Z;const FIntPoint Here(FMath::RoundToInt(S.Male.X/OKStreetNavCell),FMath::RoundToInt(S.Male.Y/OKStreetNavCell));
    const TSet<FIntPoint>* Known=S.NavLearned.Find(FMath::RoundToInt(Z/100.f));FCollisionQueryParams Q(SCENE_QUERY_STAT(StreetNavUnstick),false,this);bool Out=false;
    for(int32 R=1;R<=4&&!Out;++R)for(int32 DX=-R;DX<=R&&!Out;++DX)for(int32 DY=-R;DY<=R&&!Out;++DY){
     if(FMath::Max(FMath::Abs(DX),FMath::Abs(DY))!=R)continue;const FIntPoint C=Here+FIntPoint(DX,DY);if((Known&&Known->Contains(C))||!NavCellFree(C,Z))continue;
     const FVector P(C.X*OKStreetNavCell,C.Y*OKStreetNavCell,Z);FHitResult H;
     if(GetWorld()->SweepSingleByChannel(H,S.Male+FVector(0,0,85),P+FVector(0,0,85),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(28,78),Q)&&!H.bStartPenetrating)continue;
     UE_LOG(LogTemp,Display,TEXT("Street navigation: wedged at %s, out to %s"),*S.Male.ToCompactString(),*P.ToCompactString());S.Male=P;Out=true;}
    // Shut in (between a facade and its inner wall): out at the nearest door on the street (doors are where walking
    // starts), and the way again from there.
    if(!Out){const FOKStreetPlace* Near=nullptr;for(const auto& P:OKStreetPlaces())if(FMath::Abs(P.Door.Z-Z)<150.f&&(!Near||FVector::Dist2D(P.Door,S.Male)<FVector::Dist2D(Near->Door,S.Male)))Near=&P;
     if(Near&&FVector::Dist2D(Near->Door,S.Male)<2000.f){UE_LOG(LogTemp,Warning,TEXT("Street navigation: shut in at %s, out at the door of %s"),*S.Male.ToCompactString(),Near->Id);S.Male=FVector(Near->Door.X,Near->Door.Y,Z);Out=true;}}
    if(Out){auto Path=S.NavPointActive?PlaceRoute(S.Male,Goal):ContactRoute(S.Male,Goal);if(Path.Num()>=2){S.NavPath=MoveTemp(Path);S.NavStep=1;}return 0;}
   }
   // First around what is in the way, to the next point only (the grid keeps the body's clearance; the same route again
   // would run into the same wall); the whole way again only when that finds nothing.
   TArray<FVector> Around=S.NavReplans==1?GridRoute(S.Male,Next):TArray<FVector>();
   if(Around.Num()>=2){if(S.NavPath.IsValidIndex(S.NavStep)&&Next==S.NavPath[S.NavStep])S.NavPath.RemoveAt(0,S.NavStep+1);else S.NavPath.Reset();
    Around.Append(S.NavPath);S.NavPath=MoveTemp(Around);S.NavStep=1;}
   else{auto Path=S.NavPointActive?PlaceRoute(S.Male,Goal):ContactRoute(S.Male,Goal);
    if(Path.Num()>=2){S.NavPath=MoveTemp(Path);S.NavStep=1;}}
   {
    // What is in the way: the clearance gap on his side, and the first thing a body-sized sweep toward the next point hits.
    FString Hit=TEXT("none");FCollisionQueryParams Q(SCENE_QUERY_STAT(StreetNavWhy),false,this);FHitResult H;const FVector Dir=Delta/Distance;
    if(GetWorld()->SweepSingleByChannel(H,S.Male+FVector(0,0,85),S.Male+Dir*Step+FVector(0,0,85),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(28,78),Q))Hit=FString::Printf(TEXT("%s pen=%d"),*GetNameSafe(H.GetActor()),H.bStartPenetrating?1:0);
    UE_LOG(LogTemp,Display,TEXT("Street navigation: blocked why: next=%s gap_here=%.0f gap_ahead=%.0f sweep=%s"),*Next.ToCompactString(),OKStreetObstacleGap(S.Male),OKStreetObstacleGap(S.Male+Dir*Step),*Hit);
   }
   UE_LOG(LogTemp,Display,TEXT("Street navigation: blocked at %s, replanned %d (points=%d)"),*S.Male.ToCompactString(),S.NavReplans,S.NavPath.Num());
  }
  return 0;
 }
 S.Rotations[0]=OKStreetFacingMovement(Moved-S.Male,S.Rotations[0]);S.Male=Moved;S.NavProgressAt=Now;
 if(FVector::Dist2D(S.Male,S.NavPath.IsValidIndex(S.NavStep)?S.NavPath[S.NavStep]:Goal)<60)S.NavReplans=0;
 return OKStreetMaleRunSpeed/OKStreetWalkSpeed;
}
void AOKStreetSocialScene::BeginPrivate(int32 Character){
 if(!ContactReady(Character)||!ContactIdle()||!State->HighCards.IsValidIndex(Character)||State->HighCards.IsEmpty())return;
 auto& S=*State;StopAmbientActivity();EndTalk();
 auto* Mode=AOKInteractionMode::EnterCards(GetWorld()->GetFirstPlayerController(),S.HighCards[Character],S.HighCards[0]);
 if(!Mode){S.ContactsNotice=OKT(TEXT("ui.contacts.interaction_load_failed"));RefreshUI();return;}
 S.PrivateGirl=Character;S.HighPrivateAt=FPlatformTime::Seconds();S.HighPrivateSeenReady=false;S.HighPrivateResult.Reset();S.PrivateMouse=S.MouseUI;S.PrivateMode=Mode;
 const FString Directory=FPaths::GetPath(S.Manifest.SocialReport)/TEXT("HighInteraction")/FGuid::NewGuid().ToString(EGuidFormats::Digits);IFileManager::Get().MakeDirectory(*Directory,true);
 S.HighPrivateReport=Directory/TEXT("result.json");Mode->ReportPath=S.HighPrivateReport;
 Mode->AutoReturnSeconds=S.SelfTest?2:0;
 TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 Mode->Returned=[Weak](TSharedRef<FJsonObject> Result){if(Weak.IsValid()&&Weak->State){Weak->State->HighPrivateResult=Result;Weak->State->HighPrivateSeenReady=Result->HasField(TEXT("partner_ready"))&&Result->GetBoolField(TEXT("partner_ready"));Weak->FinishPrivate();if(Weak->State->SelfTest&&!Weak->State->Reported)Weak->RunContactsProbe(Weak->SceneTime());}};
 OKSuspendCrowdRuntimeScene(this,true);S.MouseToggleRequested=false;S.ContactsNotice=OKT(TEXT("ui.contacts.loading_one_on_one"));RefreshUI();
 if(S.Overlay)S.Overlay->SetVisibility(EVisibility::Collapsed);
 UE_LOG(LogTemp,Display,TEXT("Street shared private started slot=%d pid=%u"),Character,FPlatformProcess::GetCurrentProcessId());
}
void AOKStreetSocialScene::FinishPrivate(){
 if(!State||State->PrivateGirl==INDEX_NONE)return;auto& S=*State;
 auto Mode=S.PrivateMode;const int32 Girl=S.PrivateGirl;S.PrivateMode.Reset();S.PrivateGirl=INDEX_NONE;
 if(Mode.IsValid()&&!Mode->IsFinished()){Mode->Returned=nullptr;Mode->Exit(true);}
 S.PrivatePausedSeconds+=FMath::Max(0.,FPlatformTime::Seconds()-S.HighPrivateAt);S.LastWall=0;
 OKSuspendCrowdRuntimeScene(this,false);S.MouseUI=S.PrivateMouse;S.MouseToggleRequested=false;
 if(auto* PC=GetWorld()->GetFirstPlayerController())PC->SetViewTarget(S.Camera.Get());
 if(S.Overlay)S.Overlay->SetVisibility(EVisibility::Visible);
 // Her memory of the session: when it started (the memory's own time), how long, and every thing they did, in order.
 if(S.HighPrivateResult.IsValid()&&S.Manifest.Characters.IsValidIndex(Girl)){
  const TArray<TSharedPtr<FJsonValue>>* Acts=nullptr;TArray<FString> Done;
  if(S.HighPrivateResult->TryGetArrayField(TEXT("actions"),Acts))for(const auto& V:*Acts){const auto O=V->AsObject();if(!O)continue;
   const int32 Sec=int32(O->GetNumberField(TEXT("seconds")));Done.Add(OKMsg(TEXT("ui.memory.private_action"),{{TEXT("label"),O->GetStringField(TEXT("label"))},{TEXT("minute"),Sec/60},{TEXT("second"),Sec%60}}));}
  const int32 Seconds=int32(S.HighPrivateResult->HasField(TEXT("seconds"))?S.HighPrivateResult->GetNumberField(TEXT("seconds")):0.);
  const FString Text=OKMsg(TEXT("ui.memory.private_session"),{{TEXT("minutes"),FMath::Max(1,Seconds/60)},{TEXT("count"),Done.Num()},{TEXT("actions"),FString::Join(Done,OKT(TEXT("ui.memory.list_separator")))}});
  MindObserve(Girl,TEXT("private"),Text,PlaceHere(S.Male),Done.IsEmpty()?0.5f:0.75f);
 }
 S.ContactsNotice=OKT(TEXT("ui.contacts.exited_one_on_one"));ApplyMouseMode(S.ViewportFocused);RefreshUI();
}
bool AOKStreetSocialScene::TickPrivate(double Now){
 auto& S=*State;if(S.PrivateGirl==INDEX_NONE)return false;
 if(S.PrivateMode.IsValid())return true;
 FinishPrivate();if(S.SelfTest){S.Errors.Add(TEXT("Shared interaction mode disappeared without returning"));WriteReport(false);}return true;
}
void AOKStreetSocialScene::TickContacts(float Dt,double Now){
 auto& S=*State;
 if(S.PrivateGirl!=INDEX_NONE&&!ContactReady(S.PrivateGirl)){FinishPrivate();return;}
 // Her route is redrawn as she moves; not while the navigation is on its way to (or in) an elevator, or with her upstairs.
 if(S.NavGirl!=INDEX_NONE&&Now-S.NavAt>.75&&S.NavRide.Elevator==INDEX_NONE&&S.Agents.IsValidIndex(S.NavGirl-1)&&!NavNeedsLift(S.Agents[S.NavGirl-1].Position)){if(!ContactReady(S.NavGirl)){S.NavGirl=INDEX_NONE;S.NavPath.Reset();S.ContactsNotice=OKT(TEXT("ui.contacts.nav_target_unavailable"));RefreshUI();}else{S.NavAt=Now;S.NavPath=ContactRoute(S.Male,NavApproachSpot(S.Agents[S.NavGirl-1].Position));S.NavStep=1;}}
}
void AOKStreetSocialScene::RunContactsProbe(double Now){
 auto& S=*State;if(S.ContactsChecked){if(S.ContactsOnlyTest)WriteReport(S.Errors.IsEmpty());return;}
 auto Fail=[&](const TCHAR* Why){S.Errors.Add(Why);FinishPrivate();EndTalk();S.Store=S.ContactsProbeStore;S.Agents=S.ContactsProbeAgents;S.Rotations=S.ContactsProbeRotations;S.Male=S.ContactsProbeMale;WriteReport(false);};
 if(Now-S.ReadyAt>1800){Fail(TEXT("Contacts runtime probe timeout"));return;}
 if(S.ContactsProbePhase==0){
  // Visible geometry can precede asynchronous action admission.
  for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(!ContactReady(I))return;
  S.ContactsProbeStore=S.Store;S.ContactsProbeAgents=S.Agents;S.ContactsProbeRotations=S.Rotations;S.ContactsProbeMale=S.Male;S.ContactsProbeAt=Now;S.ContactsOpen=true;S.ContactsSnapshot.Yaw=S.Yaw;S.ContactsSnapshot.Pitch=S.Pitch;S.ContactsSnapshot.Distance=S.Distance;S.ContactsSnapshot.Greetings=S.Greetings;
  auto Clear=[](const FVector& A,const FVector& B){for(int32 I=0;I<=100;++I){const FVector P=FMath::Lerp(A,B,I/100.f);if(P.X>40&&P.X<60&&FMath::Abs(P.Y)<20)return false;}return true;};
  const auto Detour=OKStreetContactRoute(FVector(0,0,0),FVector(100,0,0),{FVector(0,50,0),FVector(100,50,0)},Clear);
  if(Detour.Num()<3||!OKStreetContactRoute(FVector(0,0,0),FVector(100,0,0),{},Clear).IsEmpty()){Fail(TEXT("Navigation graph failed obstacle/unreachable check"));return;}
  for(const bool Intro:{false,true}){auto C=MakeShared<FJsonObject>();C->SetNumberField(TEXT("id"),0);C->SetStringField(TEXT("street_support"),TEXT("supported"));if(Intro){C->SetStringField(TEXT("context"),TEXT("introduction"));C->SetStringField(TEXT("rule"),TEXT("fallback_shuffle_pool"));}TArray<FString> Terms;int32 Id;FString Filter,Support,Error;if(!OKStreetAppendConditionObject(C,Terms,Id,Filter,Support,Error)||!Terms.Contains(Intro?TEXT("speak=always"):TEXT("speak=never"))){Fail(TEXT("Lover introduction fallback confused with Speak condition zero"));return;}}
  for(auto& A:S.Agents)A.Held=true;
   for(int32 I=1;I<S.Manifest.Characters.Num();++I){NavigateGirl(I);if(S.NavGirl!=I||S.NavPath.Num()<2){Fail(TEXT("Roster girl navigation path missing"));return;}++S.ContactsRoutes;const int32 R=OKStreetRelationIndex(S.Store,OKStreetStableId(S.Manifest.Characters[I]),S.Manifest.TalkBudget,true);S.Store.Relations[R].Stage=TEXT("lover");}
  SelectContact(5);RebuildUI(); // RefreshUI only marks the panel dirty; count what is actually built
  if(S.ContactSelection!=5||S.ContactRows!=5||S.ContactActions!=2||S.Target!=INDEX_NONE){Fail(TEXT("Roster must use one shared two-action panel without changing conversation"));return;}S.ContactsUI=true;
  S.ContactsProbePhase=3;return;
 }
 if(S.PrivateAnyCase<15){
  const int32 Character=S.PrivateAnyCase/3+1,Stage=S.PrivateAnyCase%3;const TCHAR* Stages[]={TEXT("stranger"),TEXT("friend"),TEXT("lover")};
  if(S.PrivateAnyPhase==0){
   // Each heroine's profile arrives once, asynchronously, and initializes saved fields (wallet, stranger cap). Take
   // the "before" save after all of them, or a profile landing during the high scene reads as a changed street state.
   int32 Missing=0;for(int32 G=1;G<S.Manifest.Characters.Num();++G)if(!S.MindProfiles.Contains(OKStreetStableId(S.Manifest.Characters[G])))++Missing;
   if(Missing>0){
    const double Now=FPlatformTime::Seconds();if(S.PrivateAnyProfileWait<=0)S.PrivateAnyProfileWait=Now;
    if(Now-S.PrivateAnyProfileWait<120)return;
    UE_LOG(LogTemp,Warning,TEXT("Private-scene test: %d heroine profiles still missing after 120 s; testing without them"),Missing);
   }
   S.PrivateAnyProfileWait=0;
   EndTalk();const int32 R=OKStreetRelationIndex(S.Store,OKStreetStableId(S.Manifest.Characters[Character]),S.Manifest.TalkBudget,true);S.Store.Relations[R].Stage=Stages[Stage];S.Store.Relations[R].Favor=0;S.Store.Relations[R].TalksRemaining=5;S.PrivateAnySave=OKStreetWriteSave(S.Store);
   S.HighPrivateAgents=S.Agents;S.HighPrivateRotations=S.Rotations;S.HighPrivateMale=S.Male;
   BeginPrivate(Character);if(S.PrivateGirl!=Character){Fail(TEXT("Any-relationship high-model scene admission failed"));return;}S.PrivateAnyPhase=1;return;
  }
  if(S.PrivateGirl!=INDEX_NONE)return;
  if(!S.HighPrivateResult||!S.HighPrivateResult->GetBoolField(TEXT("same_process"))||int32(S.HighPrivateResult->GetNumberField(TEXT("process_id")))!=int32(FPlatformProcess::GetCurrentProcessId())||S.HighPrivateResult->GetStringField(TEXT("state"))!=TEXT("returned")||!S.HighPrivateResult->GetBoolField(TEXT("partner_ready"))||S.HighPrivateResult->GetStringField(TEXT("scene_class"))!=TEXT("/Script/OpenKoikatsu.OKInteractionScene")||OKStreetWriteSave(S.Store)!=S.PrivateAnySave||S.Male!=S.HighPrivateMale||S.Rotations!=S.HighPrivateRotations){UE_LOG(LogTemp,Error,TEXT("Shared return mismatch result=%d save=%d male=%d rotations=%d"),S.HighPrivateResult.IsValid(),OKStreetWriteSave(S.Store)==S.PrivateAnySave,S.Male==S.HighPrivateMale,S.Rotations==S.HighPrivateRotations);
   // Both saves beside the report, so a changed field can be found.
   if(OKStreetWriteSave(S.Store)!=S.PrivateAnySave){const FString Dir=FPaths::GetPath(S.Manifest.SocialReport);FFileHelper::SaveStringToFile(S.PrivateAnySave,*(Dir/TEXT("shared-return.before.json")));FFileHelper::SaveStringToFile(OKStreetWriteSave(S.Store),*(Dir/TEXT("shared-return.after.json")));}
   Fail(TEXT("High-model private return did not preserve original street state"));return;}
  for(int32 I=0;I<S.Agents.Num();++I)if(S.Agents[I].Position!=S.HighPrivateAgents[I].Position||S.Agents[I].Held!=S.HighPrivateAgents[I].Held){Fail(TEXT("A crowd agent moved while high scene was active"));return;}
  auto E=MakeShared<FJsonObject>();E->SetNumberField(TEXT("slot"),Character);E->SetNumberField(TEXT("stage"),Stage);E->SetStringField(TEXT("report"),S.HighPrivateReport);E->SetObjectField(TEXT("high_model"),S.HighPrivateResult);S.PrivateAnyEvidence.Add(MakeShared<FJsonValueObject>(E));
  if(Stage==2)++S.ContactsPrivate;
  ++S.PrivateAnyCase;S.PrivateAnyPhase=0;return;
 }
 S.Store=S.ContactsProbeStore;S.Agents=S.ContactsProbeAgents;S.Rotations=S.ContactsProbeRotations;S.Male=S.ContactsProbeMale;S.NavGirl=INDEX_NONE;S.NavPath.Reset();S.Yaw=S.ContactsSnapshot.Yaw;S.Pitch=S.ContactsSnapshot.Pitch;S.Distance=S.ContactsSnapshot.Distance;S.Greetings=S.ContactsSnapshot.Greetings;S.ContactsNotice.Reset();S.ContactsChecked=true;S.ContactsOpen=false;S.ContactSelection=INDEX_NONE;RefreshUI();
}
