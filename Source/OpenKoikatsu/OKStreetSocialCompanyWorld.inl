// His company in the world (included by OKStreetSocialScene.cpp): finding vacant lots on the map (a development
// tool), and what stands on a lot — the marker of a lot for rent or sale, the hoarding while it is built, the
// building with his company's sign once it is done (Docs/STREET_COMPANY_PLAN_20261012.md).

// Development: -StreetSocialFindLots scans the map for open ground beside the streets (no collision in a 14 x 11 m
// box, outside every place, 5-30 m from a place door or street point) and logs candidates as property.json "lots"
// rows, then quits. Re-run it whenever the map changes and copy the rows into the data file.
void AOKStreetSocialScene::RunFindLots(){
 auto& S=*State;static bool Done=false;if(Done||!S.AllReady)return;Done=true;
 TArray<FVector2D> Anchors;for(const auto& P:OKStreetPlaces())Anchors.Add(FVector2D(P.Door.X,P.Door.Y));
 TArray<FOKStreetPointData> Points;OKStreetWaypoints(Points);for(const auto& P:Points)Anchors.Add(FVector2D(P.Position.X,P.Position.Y));
 FBox2D Bounds(ForceInit);for(const FVector2D& A:Anchors)Bounds+=A;Bounds=Bounds.ExpandBy(2500.f);
 const FVector2D Size(1400.f,1100.f);
 struct FLot{FVector2D Center;float Yaw;float Near;};TArray<FLot> Found;
 FCollisionQueryParams Q(SCENE_QUERY_STAT(StreetFindLots),false,this);
 for(float X=Bounds.Min.X;X<=Bounds.Max.X;X+=400.f)for(float Y=Bounds.Min.Y;Y<=Bounds.Max.Y;Y+=400.f){
  const FVector2D C(X,Y);float Near=MAX_flt;FVector2D NearPoint=C;
  for(const FVector2D& A:Anchors){const float D=FVector2D::Distance(A,C);if(D<Near){Near=D;NearPoint=A;}}
  if(Near<900.f||Near>3000.f)continue;
  // Facing the nearest street point, snapped to a quarter turn (the building's front).
  const FVector2D To=NearPoint-C;const float Yaw=FMath::Abs(To.X)>=FMath::Abs(To.Y)?(To.X>0?0.f:180.f):(To.Y>0?90.f:270.f);
  const bool AlongX=Yaw==0.f||Yaw==180.f;const FVector Half(AlongX?Size.Y*.5f:Size.X*.5f,AlongX?Size.X*.5f:Size.Y*.5f,150.f);
  bool Inside=false;for(const auto& P:OKStreetPlaces())for(const FBox& R:P.Rooms)if(R.ExpandBy(FVector(Half.X+200,Half.Y+200,0)).IsInsideXY(FVector(C,0)))Inside=true;
  if(Inside)continue;
  if(GetWorld()->OverlapAnyTestByChannel(FVector(C,160.f),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeBox(Half+FVector(150,150,0)),Q))continue;
  if(OKStreetObstacleGap(FVector(C,0))<FMath::Max(Half.X,Half.Y)+100.f)continue;
  if(Found.ContainsByPredicate([&](const FLot& L){return FVector2D::Distance(L.Center,C)<2200.f;}))continue;
  Found.Add({C,Yaw,Near});
 }
 Found.Sort([](const FLot& A,const FLot& B){return A.Near<B.Near;});
 UE_LOG(LogTemp,Display,TEXT("Street lots: %d candidates (14 x 11 m, nearest first)"),Found.Num());
 for(int32 K=0;K<Found.Num()&&K<24;++K)UE_LOG(LogTemp,Display,TEXT("Street lot: {\"id\": \"lot_%02d\", \"center\": [%.0f, %.0f], \"size\": [1400, 1100], \"yaw\": %.0f}  // %.0f m from the street"),K+1,Found[K].Center.X,Found[K].Center.Y,Found[K].Yaw,Found[K].Near/100.f);
 FPlatformMisc::RequestExitWithStatus(false,0);
}

// What stands on each lot, kept in step with the company (every 2 s): a sign on vacant land (green: for rent or
// sale), his sign on his empty lot (gold), a hoarding with walls rising while it is built, and the building with a
// way in on the ground floor (counter and desks inside) once it is done. Its walls block walking (collision and
// the street's obstacle list); the upper floor is closed.
void AOKStreetSocialScene::TickCompanyWorld(){
 auto& S=*State;const double Now=SceneTime();if(Now<S.LotWorldAt||!S.AllReady)return;S.LotWorldAt=Now+2.0;
 const auto& D=OKStreetProperty();const int32 Day=S.Store.CalendarDay;
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Scenes/StreetSocial/Meshes/StreetCube.StreetCube"));if(!Cube)return;
 for(const auto& U:D.Units){
  if(!U.bLot)continue;
  FString User;const FOKStreetHolding* H=LotHolding(U.Id,&User); // his, or a heroine's company's
  const auto* B=H?D.Building(H->Building):nullptr;
  const bool Building=H&&B&&H->BuildStart>=0;const bool Done=Building&&Day>=H->BuildDone&&H->BuildPaid>=B->Cost;
  const float Progress=Building&&!Done?FMath::Clamp(float(Day-H->BuildStart+1)/float(FMath::Max(1,B->Days)),0.05f,0.95f):0.f;
  const FString Key=FString::Printf(TEXT("%d|%s|%s|%d|%.2f"),H?1:0,H?*H->Building:TEXT(""),Done?TEXT("done"):Building?TEXT("work"):TEXT("-"),B?B->Floors:0,Progress);
  if(S.LotWorldKeys.FindRef(U.Id)==Key)continue;
  S.LotWorldKeys.Add(U.Id,Key);
  // Clear what stood there.
  for(auto& A:S.LotActors.FindOrAdd(U.Id))if(A.IsValid())A->Destroy();S.LotActors[U.Id].Reset();
  for(const FBox& Box:S.LotBlocks.FindOrAdd(U.Id))OKStreetRuntimeBlocks().RemoveAll([&](const FBox& X){return X.Equals(Box);});S.LotBlocks[U.Id].Reset();
  const FRotator R(0,U.Yaw,0);const FVector C(U.Center.X,U.Center.Y,0.f);const float HX=U.Size.Y*0.5f,HY=U.Size.X*0.5f; // local X: toward the street
  auto Part=[&](FVector Local,FVector Size,int32 Material,bool Solid){
   auto* A=GetWorld()->SpawnActor<AStaticMeshActor>(C+R.RotateVector(Local),R);if(!A)return;A->SetOwner(this);A->SetActorScale3D(Size/100.f);
   auto* M=A->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(Cube);M->SetCollisionEnabled(Solid?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
   const FString Path=FString::Printf(TEXT("/Game/Scenes/StreetSocial/Materials/StreetMaterial%02d.StreetMaterial%02d"),Material,Material);if(auto* Mat=LoadObject<UMaterialInterface>(nullptr,*Path))M->SetMaterial(0,Mat);
   S.LotActors[U.Id].Add(A);
   if(Solid){const FVector W=C+R.RotateVector(Local);const FVector Ext=R.RotateVector(Size*0.5f).GetAbs();const FBox Box(W-Ext,W+Ext);OKStreetRuntimeBlocks().Add(Box);S.LotBlocks[U.Id].Add(Box);}
  };
  // The ground: a plate, and a sign at the front (green for sale/rent, gold when it is his).
  Part(FVector(0,0,1),FVector(HX*2,HY*2,2),0,false);
  const FVector SignAt(HX-60,-HY+80,0);
  if(!Done){Part(SignAt+FVector(0,0,90),FVector(8,8,180),8,true);Part(SignAt+FVector(0,0,190),FVector(10,160,90),H?5:4,false);}
  if(Building&&!Done){
   // Hoarding round the site with an opening at the front, poles, and the walls rising with the days.
   for(const float Y:{-HY+10,HY-10})Part(FVector(0,Y,100),FVector(HX*2,10,200),8,true);
   Part(FVector(-HX+10,0,100),FVector(10,HY*2,200),8,true);
   for(const float Y:{-HY+10,HY-10})Part(FVector(HX-10,Y*0.6f,100),FVector(10,HY*0.8f,200),8,true);
   const float Wall=350.f*B->Floors*Progress;
   for(const float Y:{-HY+110,HY-110})Part(FVector(0,Y,Wall*0.5f),FVector(HX*2-220,20,Wall),3,false);
   for(int32 K=-2;K<=2;++K)Part(FVector(K*(HX-110)/2.f,-HY+60,Wall*0.5f+60),FVector(8,8,Wall+120),9,false);
  }
  if(Done){
   // The building, inset 1 m from the lot's edge: walls (a door in the front), floors, roof, windows, a sign.
   const float BX=HX-100,BY=HY-100,Height=350.f*B->Floors;
   Part(FVector(-BX,0,Height*0.5f),FVector(20,BY*2,Height),3,true);
   for(const float Y:{-BY,BY})Part(FVector(0,Y,Height*0.5f),FVector(BX*2,20,Height),3,true);
   // Front: wall either side of a 2.4 m door, and over it.
   const float Door=120.f;
   for(const float Sign:{-1.f,1.f}){const float Width=BY-Door;Part(FVector(BX,Sign*(Door+Width*0.5f),Height*0.5f),FVector(20,Width,Height),3,true);}
   Part(FVector(BX,0,(Height+280.f)*0.5f),FVector(20,Door*2,Height-280.f),3,false);
   for(int32 F=1;F<B->Floors;++F)Part(FVector(0,0,350.f*F),FVector(BX*2,BY*2,20),0,false);
   Part(FVector(0,0,Height+10),FVector(BX*2+40,BY*2+40,20),8,false);
   for(int32 F=0;F<B->Floors;++F)for(const float Sign:{-1.f,1.f})Part(FVector(BX+12,Sign*(Door+(BY-Door)*0.5f),350.f*F+190),FVector(4,(BY-Door)*0.7f,120),11,false);
   Part(FVector(BX+16,0,Height-(B->Floors>1?120:60)),FVector(6,Door*2+120,60),B->Kind==TEXT("cafe")?6:B->Kind==TEXT("shop")?7:5,false);
   // Inside the ground floor: a counter by the front, desks or tables behind.
   Part(FVector(BX-130,0,50),FVector(60,220,100),8,true);
   for(const float Y:{-200.f,200.f}){Part(FVector(-150,Y,72),FVector(120,80,6),8,false);for(const float LX:{-200.f,-100.f})Part(FVector(LX,Y,36),FVector(6,6,72),8,false);}
  }
  UE_LOG(LogTemp,Display,TEXT("Street lot %s: %s"),*U.Id,*Key);
 }
}
