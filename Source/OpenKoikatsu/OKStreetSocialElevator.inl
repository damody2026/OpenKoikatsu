// Walk-in elevators (included by OKStreetSocialScene.cpp after the outing code).
//
// Every multi-storey place has one shaft (Tools/street_models/lift.py): its "liftK:label" anchors are the landing spots
// in front of it, facing away from it. The car (floor, ceiling, light, button panel) and each floor's landing doors are
// spawned here because they move. Closed landing doors are runtime blocks, so nobody walks into an empty shaft; the
// shaft walls themselves come with the model. Who stands in the car when it leaves rides with it.
//
//   phases: 0 closed at Floor, 1 opening, 2 open, 3 closing, 4 moving to Target

namespace {
constexpr float OKLiftDoorSeconds=1.0f,OKLiftSpeed=300.f,OKLiftHalfOut=70.f,OKLiftHalfSide=100.f,OKLiftDoorHeight=230.f;
}

bool AOKStreetSocialScene::InElevatorCar(int32 Index,const FVector& P) const{
 if(!State||!State->Elevators.IsValidIndex(Index))return false;const auto& E=State->Elevators[Index];
 const FVector D=P-E.Centre;
 return FMath::Abs(FVector::DotProduct(D,E.Axis))<=OKLiftHalfOut&&FMath::Abs(FVector::DotProduct(D,E.Side))<=OKLiftHalfSide&&FMath::Abs(P.Z-E.CarZ)<120.f;
}
int32 AOKStreetSocialScene::ElevatorWithMale() const{
 if(!State)return INDEX_NONE;for(int32 I=0;I<State->Elevators.Num();++I)if(InElevatorCar(I,State->Male))return I;return INDEX_NONE;
}
// The landing he stands at (same floor, close to the doors), or none.
int32 AOKStreetSocialScene::ElevatorLanding(int32 Index,const FVector& P) const{
 const auto& E=State->Elevators[Index];
 for(int32 F=0;F<E.Landing.Num();++F)if(FMath::Abs(P.Z-E.StopZ[F])<100.f&&FVector::Dist2D(P,E.Landing[F])<140.f&&!InElevatorCar(Index,P))return F;
 return INDEX_NONE;
}

void AOKStreetSocialScene::SpawnElevators(){
 auto& S=*State;S.Elevators.Reset();S.LiftRide=FOKStreetLiftRide();
 UMaterialInterface* Tint=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
 UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Scenes/StreetSocial/Meshes/StreetCube.StreetCube"));
 if(!Cube)return;
 for(int32 PlaceIndex=0;PlaceIndex<OKStreetPlaces().Num();++PlaceIndex){
  const auto& Place=OKStreetPlaces()[PlaceIndex];const auto Lifts=OKStreetPlaceLifts(Place);
  if(Lifts.Num()<2)continue;
  FOKStreetElevator E;E.Place=PlaceIndex;
  for(const auto* Stop:Lifts){
   const FVector Out=FRotator(0,Stop->Yaw,0).Vector();
   E.Landing.Add(Stop->Position);E.StopZ.Add(Stop->Position.Z);E.Out.Add(Out);E.Labels.Add(OKStreetLiftLabel(*Stop));
  }
  // The shaft centre is 1.6 m behind every landing spot.
  E.Centre=Lifts[0]->Position-E.Out[0]*160.f;E.Centre.Z=0;E.Axis=E.Out[0];E.Side=FVector(-E.Axis.Y,E.Axis.X,0);
  for(int32 F=1;F<Lifts.Num();++F){FVector C=Lifts[F]->Position-E.Out[F]*160.f;C.Z=0;if(FVector::Dist2D(C,E.Centre)>10.f)UE_LOG(LogTemp,Warning,TEXT("Street elevator: %s floor %d shaft at %s, not %s"),Place.Id,F+1,*C.ToCompactString(),*E.Centre.ToCompactString());}
  E.CarZ=E.StopZ[0];
  auto Part=[&](FVector Size,FLinearColor Colour,TArray<FOKStreetLiftPart>& Into,FVector Local){
   auto* A=GetWorld()->SpawnActor<AStaticMeshActor>(E.Centre,FRotator(0,E.Axis.Rotation().Yaw,0));if(!A)return;A->SetOwner(this);A->SetActorScale3D(Size/100.f);
   auto* C=A->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Cube);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetLightingChannels(true,true,false);
   if(Tint){auto* M=UMaterialInstanceDynamic::Create(Tint,A);M->SetVectorParameterValue(TEXT("Color"),Colour);C->SetMaterial(0,M);}
   Into.Add({A,Local});};
  const FLinearColor Steel(FColor(196,200,206)),Dark(FColor(40,42,48)),Light(FColor(255,246,214)),Floor(FColor(120,104,92)),Button(FColor(255,190,90));
  // The car, in its own frame: X along the shaft axis (toward the floor-1 landing), Y across, Z up from its floor.
  Part(FVector(140,200,6),Floor,E.Car,FVector(0,0,-3));
  Part(FVector(140,200,4),Steel,E.Car,FVector(0,0,252));
  Part(FVector(60,40,2),Light,E.Car,FVector(0,0,249));
  Part(FVector(2,180,6),Steel,E.Car,FVector(-68,0,95));                       // handrail on the back
  Part(FVector(26,3,46),Dark,E.Car,FVector(40,96,118));                       // button panel by the doors
  for(int32 B=0;B<FMath::Min(E.Landing.Num(),10);++B)Part(FVector(4,2,3),Button,E.Car,FVector(35+(B%2)*10,94,100+(B/2)*8));
  // Landing doors: two steel leaves in each floor's opening (on whichever side that floor opens).
  for(int32 F=0;F<E.Landing.Num();++F){
   TArray<FOKStreetLiftPart> Leaves;
   for(int32 L=0;L<2;++L)Part(FVector(4,56,OKLiftDoorHeight),Steel,Leaves,FVector::ZeroVector);
   E.Doors.Add(Leaves);
  }
  E.DoorBlocked.Init(false,E.Landing.Num());
  // The car and the way out of it on every floor stay clear: a solid part of the model standing there (a counter put
  // where an upper floor's shaft is) would shut him in the car. Its block and its collision go.
  {int32 Cleared=0;
   // Two boxes a floor, inside the shaft walls and the door frame: the car's floor, and the doorway out to the landing.
   auto Area=[](const FVector& From,const FVector& To,const FVector& Along,float HalfAcross,float Z){
    const FVector Mid=(From+To)*.5f;const FVector Across(-Along.Y,Along.X,0);
    const FVector Ext=(Along*(FVector::Dist2D(From,To)*.5f)).GetAbs()+(Across*HalfAcross).GetAbs();
    return FBox(FVector(Mid.X-Ext.X,Mid.Y-Ext.Y,Z+5.f),FVector(Mid.X+Ext.X,Mid.Y+Ext.Y,Z+200.f));};
   for(int32 F=0;F<E.Landing.Num();++F){
    const FVector C(E.Centre.X,E.Centre.Y,0),L(E.Landing[F].X,E.Landing[F].Y,0),Out=E.Out[F];
    const FBox Areas[]={Area(C-Out*(OKLiftHalfOut-15.f),C+Out*(OKLiftHalfOut-15.f),Out,OKLiftHalfSide-15.f,E.StopZ[F]),Area(C+Out*(OKLiftHalfOut-15.f),L,Out,50.f,E.StopZ[F])};
    for(const FBox& Column:Areas){
     // Furniture only (no side over 3 m): a wall the doorway meets stays.
     auto Furniture=[](const FBox& B){const FVector Size=B.GetSize();return FMath::Max(Size.X,Size.Y)<=300.f;};
     Cleared+=OKStreetRuntimeBlocks().RemoveAll([&](const FBox& B){const bool Hit=B.Intersect(Column)&&Furniture(B);if(Hit)UE_LOG(LogTemp,Display,TEXT("Street elevator: %s floor %d clears %s .. %s"),Place.Id,F+1,*B.Min.ToCompactString(),*B.Max.ToCompactString());return Hit;});
     for(auto& Weak:S.PlaceActors)if(AActor* Actor=Weak.Get()){FVector Origin,Extent;Actor->GetActorBounds(true,Origin,Extent);
      const FBox Bounds(Origin-Extent,Origin+Extent);if(Bounds.Intersect(Column)&&Furniture(Bounds))Actor->SetActorEnableCollision(false);}}
   }
   if(Cleared)UE_LOG(LogTemp,Warning,TEXT("Street elevator: %s had %d solid parts in its car or doorways; cleared"),Place.Id,Cleared);}
  S.Elevators.Add(MoveTemp(E));
  const int32 Index=S.Elevators.Num()-1;
  for(int32 F=0;F<S.Elevators[Index].Landing.Num();++F)SetLiftDoorBlock(Index,F,true);
  PlaceElevatorParts(Index);
  UE_LOG(LogTemp,Display,TEXT("Street elevator: %s, %d floors, shaft at %s"),Place.Id,S.Elevators[Index].Landing.Num(),*S.Elevators[Index].Centre.ToCompactString());
 }
}

void AOKStreetSocialScene::DestroyElevators(){
 if(!State)return;
 for(auto& E:State->Elevators){for(auto& P:E.Car)if(P.Actor.IsValid())P.Actor->Destroy();for(auto& D:E.Doors)for(auto& P:D)if(P.Actor.IsValid())P.Actor->Destroy();}
 State->Elevators.Reset();State->LiftRide=FOKStreetLiftRide();
}

// A closed landing door is a thin runtime block across its opening (only at that floor's height).
void AOKStreetSocialScene::SetLiftDoorBlock(int32 Index,int32 Floor,bool Closed){
 auto& E=State->Elevators[Index];if(E.DoorBlocked[Floor]==Closed)return;E.DoorBlocked[Floor]=Closed;
 const FVector Mid=E.Centre+E.Out[Floor]*75.f;const FVector Half=E.Out[Floor].GetAbs()*8.f+E.Side.GetAbs()*58.f;
 const FBox Box(FVector(Mid.X-Half.X,Mid.Y-Half.Y,E.StopZ[Floor]),FVector(Mid.X+Half.X,Mid.Y+Half.Y,E.StopZ[Floor]+OKLiftDoorHeight+40.f));
 auto& Blocks=OKStreetRuntimeBlocks();
 if(Closed)Blocks.Add(Box);else Blocks.RemoveAll([&Box](const FBox& B){return B.Min.Equals(Box.Min,.5)&&B.Max.Equals(Box.Max,.5);});
 State->NavGrid.Reset();
}

void AOKStreetSocialScene::PlaceElevatorParts(int32 Index){
 auto& E=State->Elevators[Index];const FRotator Frame(0,E.Axis.Rotation().Yaw,0);
 const FVector Base(E.Centre.X,E.Centre.Y,E.CarZ);
 for(auto& P:E.Car)if(P.Actor.IsValid())P.Actor->SetActorLocation(Base+Frame.RotateVector(P.Local));
 for(int32 F=0;F<E.Doors.Num();++F){
  const float Open=F==E.Floor?E.Door:0.f;
  for(int32 L=0;L<E.Doors[F].Num();++L){
   const float Sign=L==0?-1.f:1.f;const float Across=Sign*(28.f+Open*52.f);
   const FVector At=FVector(E.Centre.X,E.Centre.Y,E.StopZ[F]+OKLiftDoorHeight*.5f)+E.Out[F]*73.f+E.Side*Across;
   if(auto* A=E.Doors[F][L].Actor.Get())A->SetActorLocationAndRotation(At,FRotator(0,E.Out[F].Rotation().Yaw,0));
  }
 }
}

// Someone standing in the doorway keeps the doors open (they reopen if closing).
bool AOKStreetSocialScene::LiftDoorwayBusy(int32 Index) const{
 const auto& S=*State;const auto& E=S.Elevators[Index];const FVector Mid=E.Centre+E.Out[E.Floor]*75.f;
 auto In=[&](const FVector& P){const FVector D=P-Mid;return FMath::Abs(D.Z-E.StopZ[E.Floor])<120.f&&FMath::Abs(FVector::DotProduct(D,E.Out[E.Floor]))<45.f&&FMath::Abs(FVector::DotProduct(D,E.Side))<70.f;};
 if(In(S.Male))return true;for(const auto& A:S.Agents)if(In(A.Position))return true;return false;
}

void AOKStreetSocialScene::PressElevator(int32 Index,int32 Floor){
 if(!State||!State->Elevators.IsValidIndex(Index))return;auto& S=*State;auto& E=S.Elevators[Index];
 if(!E.Landing.IsValidIndex(Floor))return;
 const auto& Place=OKStreetPlaces()[E.Place];
 if(Floor>0&&FCString::Strcmp(Place.Id,TEXT("girls_dorm"))==0&&InElevatorCar(Index,S.Male)){S.Message=OKT(TEXT("ui.elevator.dorm_upstairs_restricted"));RefreshUI();return;}
 E.Target=Floor;
 if(E.Phase==2&&Floor!=E.Floor)E.OpenUntil=FMath::Min(E.OpenUntil,SceneTime()+.8);
 S.Message=OKMsg(TEXT("ui.elevator.press_floor_button"), {{TEXT("floor_label"), *E.Labels[Floor]}});RefreshUI();
 UE_LOG(LogTemp,Display,TEXT("Street elevator: %s press %s (car at %s, phase %d)"),Place.Id,*E.Labels[Floor],*E.Labels[E.Floor],E.Phase);
}
void AOKStreetSocialScene::ElevatorDoorButton(int32 Index,bool Open){
 if(!State||!State->Elevators.IsValidIndex(Index))return;auto& E=State->Elevators[Index];
 if(Open){if(E.Phase==0||E.Phase==3){E.Phase=1;}else if(E.Phase==2)E.OpenUntil=FMath::Max(E.OpenUntil,SceneTime()+4.0);}
 else if(E.Phase==2)E.OpenUntil=SceneTime();
}

// The girl he is with walks into the car beside him (or out after him): a short scripted walk.
void AOKStreetSocialScene::LiftWalk(int32 Girl,const TArray<FVector>& Path){
 if(!State->Agents.IsValidIndex(Girl-1)||Path.IsEmpty())return;auto& S=*State;
 const bool Running=S.LiftWalks.Contains(Girl);auto& W=S.LiftWalks.FindOrAdd(Girl);if(!Running)W.WasHeld=S.Agents[Girl-1].Held;W.Path=Path;W.Started=true;W.At=S.Agents[Girl-1].Position;
 S.Agents[Girl-1].Held=true;
}

void AOKStreetSocialScene::TickElevators(float Dt){
 if(!State||State->Elevators.IsEmpty())return;auto& S=*State;const double Now=SceneTime();
 // Girls standing in the car with him stay put (the follow would walk her back out); he steps off, she is free.
 for(auto It=S.LiftPins.CreateIterator();It;++It){
  const int32 Girl=It.Key();if(!S.Agents.IsValidIndex(Girl-1)||ElevatorWithMale()==INDEX_NONE||S.LiftWalks.Contains(Girl)){It.RemoveCurrent();continue;}
  auto& A=S.Agents[Girl-1];A.Position=FVector(It.Value().X,It.Value().Y,A.Position.Z);A.Held=true; // the street walk would take her (and drop her to the ground)
 }
 // Scripted walks into / out of a car.
 for(auto It=S.LiftWalks.CreateIterator();It;++It){
  const int32 Girl=It.Key();auto& W=It.Value();if(!S.Agents.IsValidIndex(Girl-1)||W.Path.IsEmpty()){It.RemoveCurrent();continue;}
  auto& A=S.Agents[Girl-1];A.Position=FVector(W.At.X,W.At.Y,A.Position.Z);FVector D=W.Path[0]-A.Position;D.Z=0;const float Len=D.Size();
  if(Len<6.f){A.Position=FVector(W.Path[0].X,W.Path[0].Y,A.Position.Z);W.At=A.Position;W.Path.RemoveAt(0);if(W.Path.IsEmpty()){if(!W.KeepHeld)A.Held=W.WasHeld||BusyWithPlayer(Girl);else S.LiftPins.Add(Girl,A.Position);if(S.Companion==Girl){S.CompanionTrail.Reset();S.CompanionTrail.Add(S.Male);S.CompanionLastMale=S.Male;}It.RemoveCurrent();}continue;}
  const float Step=FMath::Min(Len,OKStreetWalkSpeed*FMath::Min(Dt,.05f));A.Position+=D/Len*Step;W.At=A.Position;S.Rotations[Girl]=OKStreetFacingMovement(D,S.Rotations[Girl]);
 }
 for(int32 Index=0;Index<S.Elevators.Num();++Index){
  auto& E=S.Elevators[Index];
  // Standing at a landing calls the car (the call button by the doors).
  const int32 Called=ElevatorLanding(Index,S.Male);
  if(Called!=INDEX_NONE&&E.Target==INDEX_NONE&&!(E.Floor==Called&&(E.Phase==1||E.Phase==2))){
   if(E.Floor==Called&&(E.Phase==0||E.Phase==3))E.Phase=1;else if(E.Floor!=Called){E.Target=Called;if(S.LiftCalledAt.FindRef(Index)<Now-5){S.Message=OKMsg(TEXT("ui.elevator.waiting_for_floor"), {{TEXT("called_floor_label"), *E.Labels[Called]}});RefreshUI();}S.LiftCalledAt.Add(Index,Now);}
  }
  switch(E.Phase){
  case 0:
   if(E.Target!=INDEX_NONE){if(E.Target==E.Floor){E.Target=INDEX_NONE;E.Phase=1;}else{
    // Leaving: whoever is in the car rides along.
    E.Riders.Reset();E.MaleRiding=InElevatorCar(Index,S.Male);
    for(int32 I=0;I<S.Agents.Num();++I)if(InElevatorCar(Index,S.Agents[I].Position))E.Riders.Add(I+1);
    E.Phase=4;}}
   break;
  case 1:
   E.Door=FMath::Min(1.f,E.Door+Dt/OKLiftDoorSeconds);
   if(E.Door>.6f)SetLiftDoorBlock(Index,E.Floor,false);
   if(E.Door>=1.f){E.Phase=2;E.OpenUntil=Now+4.0;}
   break;
  case 2:
   if(LiftDoorwayBusy(Index))E.OpenUntil=FMath::Max(E.OpenUntil,Now+1.2);
   if(ElevatorLanding(Index,S.Male)==E.Floor)E.OpenUntil=FMath::Max(E.OpenUntil,Now+1.0);
   if(LiftWalkNear(Index))E.OpenUntil=FMath::Max(E.OpenUntil,Now+.6);
   if(Now>E.OpenUntil){E.Phase=3;SetLiftDoorBlock(Index,E.Floor,true);}
   break;
  case 3:
   if(LiftDoorwayBusy(Index)){E.Phase=1;break;}
   E.Door=FMath::Max(0.f,E.Door-Dt/OKLiftDoorSeconds);
   if(E.Door<=0.f){E.Phase=0;if(E.Target==E.Floor)E.Target=INDEX_NONE;}
   break;
  case 4:{
   const float Goal=E.StopZ[E.Target];const float Delta=Goal-E.CarZ;
   // Ease in and out: full speed in the middle, slower over the last metre.
   const float Speed=OKLiftSpeed*FMath::Clamp(FMath::Abs(Delta)/100.f,.25f,1.f);
   E.CarZ=FMath::Abs(Delta)<=Speed*Dt?Goal:E.CarZ+FMath::Sign(Delta)*Speed*Dt;
   if(E.MaleRiding)S.Male.Z=E.CarZ;
   for(const int32 R:E.Riders)if(S.Agents.IsValidIndex(R-1)){S.Agents[R-1].Position.Z=E.CarZ;S.Agents[R-1].Held=true;}
   if(E.CarZ==Goal){
    E.Floor=E.Target;E.Target=INDEX_NONE;E.Phase=1;
    if(E.MaleRiding){S.Message=OKMsg(TEXT("ui.elevator.floor_arrived"), {{TEXT("floor_label"), *E.Labels[E.Floor]}});RefreshUI();}
    UE_LOG(LogTemp,Display,TEXT("Street elevator: %s arrived at %s with him=%d riders=%d"),OKStreetPlaces()[E.Place].Id,*E.Labels[E.Floor],E.MaleRiding,E.Riders.Num());
    E.MaleRiding=false;E.Riders.Reset();
    if(S.Companion!=INDEX_NONE){S.CompanionTrail.Reset();S.CompanionTrail.Add(S.Male);S.CompanionLastMale=S.Male;}
   }
   break;}
  }
  // She comes along: with him in the car and a floor pressed, the girl he is with walks in beside him.
  const int32 Girl=FreeTalkPartner()>0?FreeTalkPartner():S.Companion;
  if(Girl>0&&S.Agents.IsValidIndex(Girl-1)&&InElevatorCar(Index,S.Male)&&E.Target!=INDEX_NONE&&E.Target!=E.Floor&&(E.Phase==2||E.Phase==1)&&!S.LiftWalks.Contains(Girl)){
   const FVector Her=S.Agents[Girl-1].Position;
   // Beside him at the back of the car: walked to from outside, or from the doorway the follow left her in.
   const FVector D=Her-E.Centre;const bool Inside=InElevatorCar(Index,Her)&&FVector::DotProduct(D,E.Out[E.Floor])<35.f;
   if(!S.LiftPins.Contains(Girl)&&FMath::Abs(Her.Z-E.StopZ[E.Floor])<100.f&&FVector::Dist2D(Her,E.Centre)<900.f){
    const float SideOfHim=FVector::DotProduct(S.Male-E.Centre,E.Side)>0?-1.f:1.f;
    const FVector Spot=FVector(E.Centre.X,E.Centre.Y,E.StopZ[E.Floor])+E.Side*(SideOfHim*45.f)-E.Out[E.Floor]*15.f;
    if(Inside&&FVector::Dist2D(Her,Spot)<40.f)S.LiftPins.Add(Girl,Her);
    else{TArray<FVector> Path;if(!InElevatorCar(Index,Her))Path.Add(E.Landing[E.Floor]);Path.Add(Spot);LiftWalk(Girl,Path);S.LiftWalks[Girl].KeepHeld=true;
     UE_LOG(LogTemp,Display,TEXT("Street elevator: actor=%d walks into the car"),Girl);}
   }
  }
  // ...and out after him when he steps off.
  if(Girl>0&&S.Agents.IsValidIndex(Girl-1)&&(E.Phase==1||E.Phase==2)&&!S.LiftWalks.Contains(Girl)&&InElevatorCar(Index,S.Agents[Girl-1].Position)&&!InElevatorCar(Index,S.Male)&&FMath::Abs(S.Male.Z-E.StopZ[E.Floor])<100.f){
   const FVector Beside=E.Landing[E.Floor]+E.Side*60.f;
   LiftWalk(Girl,{E.Landing[E.Floor],Beside});
   if(auto* W=S.LiftWalks.Find(Girl))W->WasHeld=S.Companion==Girl||S.OutingGirl==Girl;
  }
  PlaceElevatorParts(Index);
 }
}

// One ride, for an outing or the navigation: at the landing he waits for the car (standing there calls it), steps in
// (the girl with him follows), presses the floor, rides, and steps out. Rate: his walk rate for the animation.
// Answers 0 on the way, 1 stepped out on R.Floor, 2 stuck for a minute (or the elevator is gone).
int32 AOKStreetSocialScene::AdvanceLiftRide(FOKStreetLiftRide& R,float Dt,float& Rate){
 auto& S=*State;const double Now=SceneTime();Rate=0;
 if(!S.Elevators.IsValidIndex(R.Elevator))return 2;
 auto& E=S.Elevators[R.Elevator];
 if(R.At==0)R.At=Now;
 auto WalkTo=[&](const FVector& Goal){FVector D=Goal-S.Male;D.Z=0;const float Len=D.Size();if(Len<8.f)return true;
  const FVector To=StreetStep(S.Male,D/Len,FMath::Min(Len,OKStreetMaleWalkSpeed*FMath::Min(Dt,.05f)));
  if(To!=S.Male){S.Rotations[0]=OKStreetFacingMovement(To-S.Male,S.Rotations[0]);S.Male=To;}return false;};
 if(Now-R.At>60){UE_LOG(LogTemp,Warning,TEXT("Street elevator: ride stuck at stage %d"),R.Stage);return 2;}
 const int32 Here=E.StopZ.IndexOfByPredicate([&](float Z){return FMath::Abs(Z-S.Male.Z)<100.f;});
 switch(R.Stage){
 case 0: // waiting at the landing (standing there calls the car)
  if(Here!=INDEX_NONE&&E.Floor==Here&&E.Phase==2){R.Stage=1;}
  else if(Here!=INDEX_NONE&&ElevatorLanding(R.Elevator,S.Male)==INDEX_NONE)WalkTo(E.Landing[Here]);
  return 0;
 case 1:{ // step in, to the back of the car
  if(E.Phase!=2&&E.Phase!=1){R.Stage=0;return 0;}
  const FVector Inside=FVector(E.Centre.X,E.Centre.Y,S.Male.Z)-E.Out[E.Floor]*25.f-E.Side*35.f;
  if(WalkTo(Inside)){PressElevator(R.Elevator,R.Floor);R.Stage=2;}
  Rate=OKStreetMaleWalkSpeed/OKStreetWalkSpeed;return 0;}
 case 2: // riding: off at the floor once the doors open
  if(E.Floor==R.Floor&&E.Phase==2&&FMath::Abs(S.Male.Z-E.StopZ[R.Floor])<5.f)R.Stage=3;
  return 0;
 case 3:{ // step out
  const FVector Out=E.Landing[R.Floor]+E.Out[R.Floor]*60.f;Rate=OKStreetMaleWalkSpeed/OKStreetWalkSpeed;
  // Out of the car and next to that spot is off the elevator too: a wall a few centimetres short of it (a narrow
  // landing) would otherwise hold him there for the minute and end it as "blocked".
  if(WalkTo(FVector(Out.X,Out.Y,S.Male.Z))||(!InElevatorCar(R.Elevator,S.Male)&&FVector::Dist2D(S.Male,Out)<80.f)){UE_LOG(LogTemp,Display,TEXT("Street elevator: ride reached %s"),*E.Labels[R.Floor]);return 1;}
  return 0;}
 }
 return 0;
}

// An outing going upstairs (or down from upstairs): the ride, then the outing walks on to its spot.
float AOKStreetSocialScene::StepLiftRide(float Dt){
 auto& S=*State;auto& R=S.LiftRide;float Rate=0;
 const int32 Done=AdvanceLiftRide(R,Dt,Rate);
 // Something went wrong for too long: fall back to being there.
 if(Done==2){S.Male=R.Spot;S.OutingRoute={S.Male,R.Spot};S.OutingStep=1;R=FOKStreetLiftRide();return 0;}
 if(Done==1){
  // Up to a spot in this building: the grid inside; down on the way out: the street route from here.
  TArray<FVector> Rest=R.Descend?PlaceRoute(S.Male,R.Spot):GridRoute(S.Male,R.Spot);if(Rest.Num()<2)Rest={S.Male,R.Spot};
  S.OutingRoute=MoveTemp(Rest);S.OutingStep=1;S.OutingProgressAt=SceneTime();
  UE_LOG(LogTemp,Display,TEXT("Street elevator: outing continues, %d more points"),S.OutingRoute.Num());
  R=FOKStreetLiftRide();
 }
 return Rate;
}

// Navigation to a girl or a spot on another floor (plan): upstairs in a building, to its elevator and down to the
// street (or straight to her floor when she is in the same building); on the street with her upstairs somewhere, to
// that building's elevator on the ground floor and up. The walk to the landing is the navigation's route; the ride is
// S.NavRide. False: no elevator can take him there (he is told why when there is a reason).
// Is the goal on another floor than him: another height, or upstairs in a building he is not upstairs in (the same
// height in two buildings is two floors, joined only through the street).
bool AOKStreetSocialScene::NavNeedsLift(const FVector& Goal) const{
 const auto& S=*State;if(FMath::Abs(Goal.Z-S.Male.Z)>=150.f)return true;
 if(S.Male.Z<=100.f)return false;
 for(const auto& P:OKStreetPlaces())if(OKStreetInsidePlace(P,S.Male))return !OKStreetInsidePlace(P,Goal);
 return false;
}

bool AOKStreetSocialScene::PlanNavLift(const FVector& Goal){
 auto& S=*State;const auto& Places=OKStreetPlaces();
 auto FloorOf=[](const TArray<const FOKStreetModelAnchor*>& Lifts,float Z){int32 Best=0;for(int32 F=0;F<Lifts.Num();++F)if(FMath::Abs(Lifts[F]->Position.Z-Z)<FMath::Abs(Lifts[Best]->Position.Z-Z))Best=F;return Best;};
 auto Ride=[&](int32 PlaceIx,int32 Floor,bool Descend,const TArray<FVector>& Walk){
  if(FCString::Strcmp(Places[PlaceIx].Id,TEXT("girls_dorm"))==0&&Floor>0){StopNavigation(OKT(TEXT("ui.elevator.dorm_upstairs_restricted")));return true;}
  S.NavRide=FOKStreetLiftRide();S.NavRide.Elevator=ElevatorOfPlace(PlaceIx);S.NavRide.Floor=Floor;S.NavRide.Descend=Descend;S.NavRide.Spot=Goal;
  S.NavPath=Walk.Num()>=2?Walk:TArray<FVector>{S.Male,Walk.Last()};S.NavStep=1;S.NavReplans=0;S.NavProgressAt=SceneTime();
  S.Message=OKMsg(TEXT("ui.contacts.nav_by_elevator"),{{TEXT("place_name"),Places[PlaceIx].Name}});RefreshUI();
  UE_LOG(LogTemp,Display,TEXT("Street navigation: by the elevator of %s to floor %d (%d points to the landing)"),Places[PlaceIx].Id,Floor,S.NavPath.Num());
  return true;};
 // Upstairs in the girls' dorm is their rooms: said before any ride.
 if(Goal.Z>100.f)if(const FOKStreetPlace* Dorm=OKStreetFindPlace(TEXT("girls_dorm")))if(OKStreetInsidePlace(*Dorm,Goal)){StopNavigation(OKT(TEXT("ui.elevator.dorm_upstairs_restricted")));return true;}
 // Upstairs: down (or across floors in the same building).
 if(S.Male.Z>100.f)for(int32 P=0;P<Places.Num();++P){
  if(!OKStreetInsidePlace(Places[P],S.Male))continue;const auto Lifts=OKStreetPlaceLifts(Places[P]);if(Lifts.IsEmpty()||ElevatorOfPlace(P)==INDEX_NONE)continue;
  const int32 From=FloorOf(Lifts,S.Male.Z);const bool Same=OKStreetInsidePlace(Places[P],Goal);const int32 To=Same?FloorOf(Lifts,Goal.Z):0;
  if(From==To)return false;
  return Ride(P,To,!Same,GridRoute(S.Male,Lifts[From]->Position));
 }
 // On the ground: up in the building she (or the spot) is in.
 if(Goal.Z>100.f)for(int32 P=0;P<Places.Num();++P){
  if(!OKStreetInsidePlace(Places[P],Goal))continue;const auto Lifts=OKStreetPlaceLifts(Places[P]);if(Lifts.IsEmpty()||ElevatorOfPlace(P)==INDEX_NONE)continue;
  TArray<FVector> Walk;
  if(OKStreetInsidePlace(Places[P],S.Male))Walk=GridRoute(S.Male,Lifts[0]->Position);
  else{Walk=FVector::Dist2D(S.Male,Places[P].Door)<200?TArray<FVector>{S.Male,Places[P].Door}:PlaceRoute(S.Male,Places[P].Door);if(Walk.Num()<2)return false;
   TArray<FVector> In=GridRoute(Walk.Last(),Lifts[0]->Position);if(In.Num()>=2){In.RemoveAt(0);Walk.Append(In);}else Walk.Add(Lifts[0]->Position);}
  return Ride(P,FloorOf(Lifts,Goal.Z),false,Walk);
 }
 return false;
}
