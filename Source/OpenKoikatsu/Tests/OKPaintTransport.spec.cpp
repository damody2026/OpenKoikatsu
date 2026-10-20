#include "../OKPaintMask.h"
#include "../OKPaintSprayer.h"
#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKPaintTransportTest,"OpenKoikatsu.Paint.ConservativeTransport",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKPaintTransportTest::RunTest(const FString&)
{
 auto* ChartCoordinates=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.ChartCoordinates"));const int32 PreviousChartMode=ChartCoordinates->GetInt();ON_SCOPE_EXIT{ChartCoordinates->Set(PreviousChartMode,ECVF_SetByCode);};
 auto* ParallelRoutes=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.TransportParallel"));const int32 PreviousParallel=ParallelRoutes->GetInt();ON_SCOPE_EXIT{ParallelRoutes->Set(PreviousParallel,ECVF_SetByCode);};
 for(double Speed:{1600.,4000.})for(const FVector& Target:{FVector(30,0,10),FVector(400,25,60),FVector(800,-70,-60)}){
  const FVector V=AOKPaintSprayer::AimDirection(FVector::ZeroVector,Target,Speed)*Speed;
  const double Time=FVector2D(Target.X,Target.Y).Length()/FVector2D(V.X,V.Y).Length();
  TestTrue(TEXT("Ballistic aim reaches narrow target"),(V*Time-FVector(0,0,490*Time*Time)).Equals(Target,1.e-7));
 }
 for(int32 Size:{8,16,32}){
  FOKPaintFilm Film;Film.Faces.SetNum(2);
  auto& A=Film.Faces[0];auto& B=Film.Faces[1];
  A.P[0]=FVector(0,-4,4);A.P[1]=FVector(0,4,4);A.P[2]=FVector(0,-4,-4);
  B.P[0]=A.P[1];B.P[1]=FVector(0,4,-4);B.P[2]=A.P[2];Film.BuildAdjacency();
  FOKPaintMask Mask;Mask.Size=Size;const FVector Hit(0,0,1);
  for(int32 Id=0;Id<2;++Id){const auto Brush=Mask.Brush(Film.Faces[Id],Hit,1.5);const double Mass=Mask.BrushArea(Film.Faces[Id],Brush)*.05;Mask.Deposit(Id,Film.Faces[Id],Brush,Mass);Film.AddVolume(Id,Mass);}
  const double Initial=Film.Volume();
  // Dry authored faces, including collapsed triangles, must not alter wet
  // transport or trigger work proportional to the whole mesh every revision.
  Film.Faces.AddDefaulted(128);
  for(int32 Id=2;Id<Film.Faces.Num();++Id)for(auto& Point:Film.Faces[Id].P)Point=FVector::ZeroVector;
  auto CachedFilm=Film,EagerFilm=Film;auto CachedMask=Mask,EagerMask=Mask;uint64 Revision=1;
  for(int32 Step=0;Step<100;++Step){Mask.Transport(Film,.05,4,2,.004);TestTrue(TEXT("Mass conserved across unrelated charts"),FMath::Abs(Film.Volume()-Initial)<2.e-6);
   ChartCoordinates->Set(1,ECVF_SetByCode);ParallelRoutes->Set(2,ECVF_SetByCode);CachedMask.Transport(CachedFilm,.05,4,2,.004,Revision);
   ChartCoordinates->Set(0,ECVF_SetByCode);ParallelRoutes->Set(0,ECVF_SetByCode);EagerMask.Transport(EagerFilm,.05,4,2,.004,Revision,true,false);
   TestEqual(TEXT("Sparse and whole-mesh geometry preserve exact mass"),CachedFilm.Volume(),EagerFilm.Volume());
   TestEqual(TEXT("Sparse and whole-mesh geometry preserve chart count"),CachedMask.Height.Num(),EagerMask.Height.Num());
   for(const auto& E:CachedMask.Height){const auto* Values=EagerMask.Height.Find(E.Key);if(!Values||E.Value!=*Values){AddError(TEXT("Sparse transport differs from eager transport"));return false;}}
   TestTrue(TEXT("Cached routes preserve mass"),FMath::Abs(CachedFilm.Volume()-Film.Volume())<1.e-8);
   for(const auto& E:Mask.Height){const auto* Values=CachedMask.Height.Find(E.Key);if(!Values){AddError(TEXT("Cached footprint differs"));return false;}for(int32 I=0;I<E.Value.Num();++I)if(FMath::Abs(E.Value[I]-(*Values)[I])>1.e-7){AddError(TEXT("Cached thickness differs"));return false;}}
   if(Step==40){for(auto* Current:{&Film,&CachedFilm,&EagerFilm})for(auto& Face:Current->Faces)for(auto& P:Face.P)P=FQuat(FVector(1,0,0),.2).RotateVector(P);++Revision;}
   for(const auto& E:Mask.Height)for(float H:E.Value)if(!FMath::IsFinite(H)||H<0){AddError(TEXT("Invalid thickness"));return false;}
  }
  int32 SparseCount=0,EagerCount=0;for(uint8 Ready:CachedMask.TransportGeometryReady)SparseCount+=Ready;for(uint8 Ready:EagerMask.TransportGeometryReady)EagerCount+=Ready;
  TestTrue(TEXT("Only reachable wet geometry is evaluated"),SparseCount<=2&&EagerCount==EagerFilm.Faces.Num());
  const auto& Face=Film.Faces[0];for(const FVector& Point:{Face.P[0],Face.Center(),FVector(0,10,-10)})TestTrue(TEXT("Barycentric core preserves the engine result"),Face.Barycentric(Point)==FMath::ComputeBaryCentric2D(Point,Face.P[0],Face.P[1],Face.P[2]));
  TestTrue(TEXT("Collapsed authored triangle retains engine zero fallback"),Film.Faces.Last().Barycentric(FVector(1,2,3))==FVector::ZeroVector);
  TestTrue(TEXT("No face-wide fallback fills dry upper corner"),Mask.Height[0][0]<1.e-6);
  double CentroidZ=0;for(int32 Id:Film.Active)CentroidZ+=Film.Faces[Id].Center().Z*Film.Faces[Id].Volume;
  TestTrue(TEXT("World gravity moves mass downward"),CentroidZ/Film.Volume()<0);
  ChartCoordinates->Set(1,ECVF_SetByCode);FOKPaintFilm Still;Still.Faces.Add(Film.Faces[0]);Still.Faces[0].Volume=0;FOKPaintMask Residue;Residue.Size=Size;TArray<float> Cell;Cell.SetNumZeroed(Size*Size);Cell[Size+1]=.001;const double Mass=Residue.BrushArea(Still.Faces[0],Cell);Residue.Deposit(0,Still.Faces[0],Cell,Mass);Still.AddVolume(0,Mass);
  for(int32 Step=0;Step<100;++Step)Residue.Transport(Still,.05,4,2,.004,1);
  int32 WetCells=0;for(int32 Y=0;Y<Size-1;++Y)for(int32 X=0;X<Size-1-Y;++X)WetCells+=Residue.Height[0][Y*Size+X]>0;
  TestEqual(TEXT("Residual does not diffuse into dry cells"),WetCells,1);
  Residue.DirtyFaces.Reset();Residue.Transport(Still,.05,4,2,.004,1);TestTrue(TEXT("Stationary residual does not request an atlas upload"),Residue.DirtyFaces.IsEmpty());
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKPaintChartCoordinatesTest,"OpenKoikatsu.Paint.ChartCoordinates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKPaintChartCoordinatesTest::RunTest(const FString&){
 auto* Mode=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.ChartCoordinates"));const int32 Before=Mode->GetInt();ON_SCOPE_EXIT{Mode->Set(Before,ECVF_SetByCode);};
 FOKPaintMask Mask;FOKPaintFace Face;Face.P[0]=FVector(-2.,1.,3.);Face.P[1]=FVector(1.,4.,-2.);Face.P[2]=FVector(4.,-3.,1.);
 for(int32 Size:{16,32,64,128,256,16}){
  Mask.Size=Size;Mask.PrepareChartCoordinates();
  if(Size<=128)for(int32 Y=0;Y<Size;++Y)for(int32 X=0;X<Size;++X){bool Inside;const FVector Expected=FOKPaintMask::Bary(X,Y,Size,Inside);if(Expected!=Mask.ChartBary[Y*Size+X]||Inside!=bool(Mask.ChartInterior[Y*Size+X])){AddError(TEXT("Cached chart differs from original calculation"));return false;}}
  for(const FVector& Point:{Face.P[0],Face.Center(),FVector(0.,0.,0.)}){
   Mode->Set(0,ECVF_SetByCode);const auto Legacy=Mask.Brush(Face,Point,.75);
   Mode->Set(1,ECVF_SetByCode);TestTrue(TEXT("Brush bytes identical across cache, resolution changes and oversized fallback"),Legacy==Mask.Brush(Face,Point,.75));
  }
 }
 return true;
}
#endif
