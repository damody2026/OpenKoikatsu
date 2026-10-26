// The inspection room: one heroine alone in an empty room high above the street, the camera turned round her by keys, so
// her clothes and body can be looked at from any side without hunting for her in town (StreetSocial.cmd -Inspect [N],
// the control channel's /inspect, ok-game-mcp game_inspect). Her day stops while she is in it (BusyWithPlayer) and goes
// on when she leaves; he is put out of view behind the camera.

static const FVector OKInspectOrigin(0.f,0.f,20000.f);
// Her facing: a heroine's front is her +Y (yaw 0 looks along +Y), so -90 turns her to the camera at yaw 0 (+X of her).
static const FRotator OKInspectFacing(0.f,-90.f,0.f);

static FVector OKInspectView(float Yaw,float Pitch){return FRotator(Pitch,Yaw,0.f).Vector();}

bool AOKStreetSocialScene::BeginInspect(int32 Slot,FString& Error){
 if(!State){Error=TEXT("no scene");return false;}auto& S=*State;
 if(!S.Agents.IsValidIndex(Slot-1)||!S.Manifest.Characters.IsValidIndex(Slot)){Error=TEXT("no such heroine");return false;}
 const bool Was=S.InspectSlot!=INDEX_NONE;
 if(Was&&S.InspectSlot!=Slot){const int32 Prev=S.InspectSlot;S.InspectSlot=INDEX_NONE;auto& P=S.Agents[Prev-1];P.Position=S.InspectHerBack;P.Held=S.ScheduleHeld.Contains(Prev);if(S.PlanBlock.Contains(Prev))S.PlanBlock.Add(Prev,INDEX_NONE);}
 if(!Was){
  // Whatever he was doing ends: the talk, a walk with someone, an activity, the navigation.
  if(S.Pair.Partner!=INDEX_NONE)FinishPhoto();
  if(S.OutingGirl!=INDEX_NONE)CancelOuting(FString());
  StopNavigation(FString());EndTalk();
  S.InspectMaleBack=S.Male;S.InspectYaw=0.f;S.InspectPitch=8.f;S.InspectDistance=420.f;}
 // The room: a grey floor, built once.
 if(S.InspectRoom.IsEmpty()){
  auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Scenes/StreetSocial/Meshes/StreetCube.StreetCube"));
  UMaterialInterface* Tint=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
  if(!Cube){Error=TEXT("room mesh missing");return false;}
  auto Part=[&](const FVector& Center,const FVector& Size,const FLinearColor& Colour){
   auto* A=GetWorld()->SpawnActor<AStaticMeshActor>(OKInspectOrigin+Center,FRotator::ZeroRotator);if(!A)return;
   A->SetOwner(this);A->SetActorScale3D(Size/100.f);auto* C=A->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Cube);
   C->SetCollisionEnabled(ECollisionEnabled::NoCollision);if(Tint){auto* D=UMaterialInstanceDynamic::Create(Tint,A);D->SetVectorParameterValue(TEXT("Color"),Colour);C->SetMaterial(0,D);}
   S.InspectRoom.Add(A);};
  // Only a floor: walls stood dark against the light whichever side the camera was on, and the sky behind her is plain.
  Part(FVector(0,0,-10),FVector(1600,1600,20),FLinearColor(0.42f,0.42f,0.44f));}
 for(const auto& A:S.InspectRoom)if(A.IsValid())A->SetActorHiddenInGame(false);
 // Her: out of her pose, walk or seat, standing in the middle facing the camera at yaw 0.
 StopLifePose(Slot);S.Commutes.Remove(Slot);S.PendingMoves.Remove(Slot);S.CommuteShown.Remove(Slot);
 auto& A=S.Agents[Slot-1];S.InspectHerBack=A.Position;A.Held=true;A.Position=OKInspectOrigin;S.Rotations[Slot]=OKInspectFacing;S.RenderedRotations[Slot]=OKInspectFacing;
 S.InspectSlot=Slot;
 S.Message=OKMsg(TEXT("ui.inspect.hint"),{{TEXT("name"),*S.Manifest.Characters[Slot].Name}});RefreshUI();
 UE_LOG(LogTemp,Display,TEXT("Street inspect: actor %d in the inspection room"),Slot);
 return true;
}

void AOKStreetSocialScene::EndInspect(){
 if(!State||State->InspectSlot==INDEX_NONE)return;auto& S=*State;const int32 Slot=S.InspectSlot;S.InspectSlot=INDEX_NONE;
 if(S.Agents.IsValidIndex(Slot-1)){auto& A=S.Agents[Slot-1];A.Position=S.InspectHerBack;A.Held=S.ScheduleHeld.Contains(Slot);
  // Her day takes her again from where she was (a gap in her plan: she walks to where it has her now).
  if(S.PlanBlock.Contains(Slot))S.PlanBlock.Add(Slot,INDEX_NONE);S.ScheduleTickAt=0;}
 S.Male=S.InspectMaleBack;
 for(const auto& A:S.InspectRoom)if(A.IsValid())A->SetActorHiddenInGame(true);
 S.Message=OKT(TEXT("ui.inspect.left"));RefreshUI();
 UE_LOG(LogTemp,Display,TEXT("Street inspect: actor %d back to her day"),Slot);
}

void AOKStreetSocialScene::TickInspect(float Dt,APlayerController* PC,bool bFocused){
 auto& S=*State;const int32 Slot=S.InspectSlot;if(Slot==INDEX_NONE||!S.Agents.IsValidIndex(Slot-1))return;
 Dt=FMath::Min(Dt,.05f);
 if(bFocused&&PC){
  if(OKKeyDown(PC,EKeys::Left))S.InspectYaw-=90.f*Dt;if(OKKeyDown(PC,EKeys::Right))S.InspectYaw+=90.f*Dt;
  if(OKKeyDown(PC,EKeys::Up))S.InspectPitch+=45.f*Dt;if(OKKeyDown(PC,EKeys::Down))S.InspectPitch-=45.f*Dt;
  if(OKKeyPressed(PC,EKeys::MouseScrollUp))S.InspectDistance*=.9f;if(OKKeyPressed(PC,EKeys::MouseScrollDown))S.InspectDistance*=1.1f;
  if(OKKeyPressed(PC,EKeys::Home)){S.InspectYaw=0.f;S.InspectPitch=8.f;S.InspectDistance=420.f;}
  const bool Next=OKKeyPressed(PC,EKeys::PageDown),Prev=OKKeyPressed(PC,EKeys::PageUp);
  if(Next||Prev){const int32 N=S.Agents.Num();int32 To=Slot;for(int32 K=0;K<N;++K){To=(To-1+(Next?1:N-1))%N+1;if(S.Manifest.Characters.IsValidIndex(To))break;}FString Error;if(To!=Slot)BeginInspect(To,Error);}
  if(OKKeyPressed(PC,EKeys::End)){EndInspect();return;}}
 S.InspectYaw=FMath::UnwindDegrees(S.InspectYaw);S.InspectPitch=FMath::Clamp(S.InspectPitch,-30.f,70.f);S.InspectDistance=FMath::Clamp(S.InspectDistance,60.f,900.f);
 // She stays put, facing the camera at yaw 0; nothing of her day moves her (BusyWithPlayer), this keeps her there against anything else.
 auto& A=S.Agents[Slot-1];A.Position=OKInspectOrigin;A.Held=true;S.Rotations[Slot]=S.RenderedRotations[Slot]=OKInspectFacing;S.Commutes.Remove(Slot);S.PendingMoves.Remove(Slot);
 // The look: at her face up close, her middle from further off.
 const float Height=FMath::GetMappedRangeValueClamped(FVector2D(100.f,420.f),FVector2D(140.f,85.f),S.InspectDistance);
 const FVector Focus=OKInspectOrigin+FVector(0,0,Height);const FVector Dir=OKInspectView(S.InspectYaw,S.InspectPitch);
 const FVector At=Focus+Dir*S.InspectDistance;
 // He waits behind the camera, out of the picture.
 S.Male=OKInspectOrigin+FVector(Dir.X,Dir.Y,0).GetSafeNormal()*(S.InspectDistance+600.f);
 if(S.Camera.IsValid()){S.Camera->SetActorLocationAndRotation(At,(-Dir).Rotation());if(auto* Lens=S.Camera->GetCameraComponent()){Lens->SetProjectionMode(ECameraProjectionMode::Perspective);Lens->SetFieldOfView(45.f);}}
}
