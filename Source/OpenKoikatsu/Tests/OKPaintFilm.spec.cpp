#include "../OKPaintFilm.h"
#include "../OKPaintMask.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKPaintFilmTest,"OpenKoikatsu.Paint.Film",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKPaintFilmTest::RunTest(const FString&)
{
 FOKPaintFilm Film;Film.Faces.SetNum(2);auto& A=Film.Faces[0];auto& B=Film.Faces[1];
 A.P[0]=FVector(0,0,2);A.P[1]=FVector(0,1,1);A.P[2]=FVector(0,-1,1);
 B.P[0]=FVector(0,-1,1);B.P[1]=FVector(0,1,1);B.P[2]=FVector(0,0,0);
 A.UV[0]=FVector2D(0,0);A.UV[1]=FVector2D(1,0);A.UV[2]=FVector2D(0,1);
 B.UV[0]=FVector2D(4,5);B.UV[1]=FVector2D(3,5);B.UV[2]=FVector2D(4,4);Film.BuildAdjacency();
 TestEqual(TEXT("Geometric seam crossed despite unrelated UV islands"),A.Neighbor[1],1);Film.AddVolume(0,1.);
 double T;FVector Bary;TestTrue(TEXT("High speed sweep hits thin deformed surface"),FOKPaintFilm::SweepTriangle(FVector(-10000,0,1.5),FVector(10000,0,1.5),.1,A,T,Bary));
 TestTrue(TEXT("Hit barycentric sum"),FMath::IsNearlyEqual(Bary.X+Bary.Y+Bary.Z,1.,.0001));TestFalse(TEXT("Off-target misses"),FOKPaintFilm::SweepTriangle(FVector(-10,20,20),FVector(10,20,20),.1,A,T,Bary));
 const auto BeforeUV=A.GravityUV();TestTrue(TEXT("Gravity has UV direction on vertical surface"),BeforeUV.Length()>0);
 for(int32 I=0;I<240;++I)Film.Advance(1./120.,3,10,.002);
 TestTrue(TEXT("Paint crosses the UV seam downhill"),B.Volume>0);TestTrue(TEXT("Volume conserved"),FMath::Abs(Film.Volume()-1.)<1.e-9);TestTrue(TEXT("Nonnegative volumes"),A.Volume>=0&&B.Volume>=0);
 FOKPaintFilm Slow=Film,Fast=Film;Slow.AddVolume(0,1);Fast.AddVolume(0,1);Slow.Advance(.01,20,10,.002);Fast.Advance(.01,.1,10,.002);TestTrue(TEXT("Higher viscosity transfers less"),Slow.Faces[0].Volume>Fast.Faces[0].Volume);
 // Rotating the physical surface reverses flow, without changing authored UVs.
 const FQuat Rotate(FVector::ForwardVector,PI);for(auto& F:Film.Faces)for(auto& P:F.P)P=Rotate.RotateVector(P);const auto RotatedUV=Film.Faces[0].GravityUV();TestTrue(TEXT("World gravity changes after pose rotation"),FVector2D::DotProduct(BeforeUV,RotatedUV)<0);
 auto Simulate=[&](double FrameDt){FOKPaintFilm F;F.Faces=Slow.Faces;for(auto& Face:F.Faces)Face.Volume=0;F.AddVolume(0,1);double Acc=0;for(int32 Frame=0;Frame<FMath::RoundToInt(2./FrameDt);++Frame){Acc+=FrameDt;while(Acc+1.e-10>=1./120.){F.Advance(1./120.,3,10,.002);Acc-=1./120.;}}return F.Faces[0].Volume;};
 TestTrue(TEXT("30/60 fps fixed-step agreement"),FMath::Abs(Simulate(1./30.)-Simulate(1./60.))<1.e-9);TestTrue(TEXT("60/120 fps fixed-step agreement"),FMath::Abs(Simulate(1./60.)-Simulate(1./120.))<1.e-9);
 FOKPaintFilm NonManifold;NonManifold.Faces={A,B,B};NonManifold.BuildAdjacency();TestEqual(TEXT("Non-manifold shared edge is closed"),NonManifold.Faces[0].Neighbor[1],INDEX_NONE);
 Film.AddVolume(0,-1000);TestTrue(TEXT("Removal cannot create negative thickness"),Film.Faces[0].Volume>=0);
 FOKPaintFace Flat;Flat.P[0]=FVector(0,0,0);Flat.P[1]=FVector(20,0,0);Flat.P[2]=FVector(0,20,0);FOKPaintMask Mask;Mask.Size=64;const auto Brush=Mask.Brush(Flat,FVector(5,5,0),2);Mask.Deposit(0,Flat,Brush,1.);const auto& Profile=Mask.Height[0];TestTrue(TEXT("Texture brush has partial coverage inside a single coarse triangle"),Profile[16*64+16]>0&&Profile[0]==0&&Profile[50]==0);TestTrue(TEXT("Texel brush integrates to deposited volume"),FMath::Abs(FOKPaintMask::Mean(Profile,64)*Flat.Area()-1.)<1.e-6);
 FOKPaintFilm MaskFilm;MaskFilm.Faces={Flat};MaskFilm.AddVolume(0,1);MaskFilm.Faces[0].P[2].Z=10;Mask.Advance(MaskFilm,.01,8,8,.004);TestTrue(TEXT("Fine mask stays finite and normalized after animated tangent advection"),FMath::Abs(FOKPaintMask::Mean(Mask.Height[0],64)*MaskFilm.Faces[0].Area()-1)<1.e-6);for(float H:Mask.Height[0])if(!FMath::IsFinite(H)||H<0)AddError(TEXT("Invalid fine-mask thickness"));
 FOKPaintFace Opposite;Opposite.P[0]=FVector(20,20,0);Opposite.P[1]=Flat.P[2];Opposite.P[2]=Flat.P[1];FOKPaintMask Seam;Seam.Size=64;const auto Left=Seam.Brush(Flat,FVector(10,10,0),3),Right=Seam.Brush(Opposite,FVector(10,10,0),3);const double BrushArea=Seam.BrushArea(Flat,Left)+Seam.BrushArea(Opposite,Right);Seam.Deposit(0,Flat,Left,Seam.BrushArea(Flat,Left)/BrushArea);Seam.Deposit(1,Opposite,Right,Seam.BrushArea(Opposite,Right)/BrushArea);TestTrue(TEXT("Circular texture stroke crosses a triangle/UV seam without a thickness step"),FMath::Abs(Seam.Height[0][31*64+31]-Seam.Height[1][31*64+31])<1.e-6);
 TestTrue(TEXT("Interior brush does not invent wet triangle edges"),Mask.EdgeCoverage[0].IsNearlyZero(1.e-6));
 FOKPaintFilm EdgeFilm;EdgeFilm.Faces=Slow.Faces;for(auto& Face:EdgeFilm.Faces)Face.Volume=0;EdgeFilm.AddVolume(0,1);TMap<int32,FVector> Coverage;Coverage.Add(0,FVector::ZeroVector);EdgeFilm.Advance(.1,3,10,0,&Coverage);TestEqual(TEXT("Dry shared edge blocks coarse mean-volume transport"),EdgeFilm.Faces[0].Volume,1.);Coverage[0]=FVector::OneVector;EdgeFilm.Advance(.1,3,10,0,&Coverage);TestTrue(TEXT("Wetting the shared edge permits conserved transport"),EdgeFilm.Faces[1].Volume>0&&FMath::Abs(EdgeFilm.Volume()-1)<1.e-9);
 FOKPaintFace Vertical;Vertical.P[0]=FVector(0,0,0);Vertical.P[1]=FVector(0,20,0);Vertical.P[2]=FVector(0,0,20);Vertical.Volume=1;
 FOKPaintMask Drip;Drip.Size=64;Drip.Deposit(0,Vertical,Drip.Brush(Vertical,FVector(0,5,5),2),1);FVector DripAnchor;
 TestEqual(TEXT("Wet triangle interior does not drip from dry lowest vertices"),Drip.TakeDrip(0,Vertical,.02,0,.008,DripAnchor),0.);
 Drip.Height.Reset();Drip.Deposit(0,Vertical,Drip.Brush(Vertical,FVector(0,5,.5),1.5),1);const double Removed=Drip.TakeDrip(0,Vertical,.02,0,.008,DripAnchor);const FVector DripPoint=Vertical.P[0]*DripAnchor.X+Vertical.P[1]*DripAnchor.Y+Vertical.P[2]*DripAnchor.Z;
 TestTrue(TEXT("Local wet edge drips even when triangle mean thickness is below threshold"),Vertical.Volume/Vertical.Area()<.02&&Removed>0);
 TestTrue(TEXT("Drop follows actual wet barycentric position, not dry corner"),DripPoint.Y>3&&DripPoint.Y<7&&DripPoint.Z<1);
 TestTrue(TEXT("Local droplet removal conserves mask volume"),FMath::Abs(FOKPaintMask::Mean(Drip.Height[0],64)*Vertical.Area()+Removed-1)<1.e-6);
 // Compare quantized welding against the preserved string-key behavior on a
 // real-sized chart, including negative coordinates, seams and nonmanifolds.
 FOKPaintFilm Chart;for(int32 Y=0;Y<100;++Y)for(int32 X=0;X<100;++X){
  const FVector P(-100000.+X,-200000.+Y,-300000.);auto& L=Chart.Faces.AddDefaulted_GetRef();L.P[0]=P;L.P[1]=P+FVector(1,0,0);L.P[2]=P+FVector(0,1,0);
  auto& R=Chart.Faces.AddDefaulted_GetRef();R.P[0]=P+FVector(1,1,0);R.P[1]=P+FVector(0,1,0);R.P[2]=P+FVector(1,0,0);
 }
 const FOKPaintFace Duplicate=Chart.Faces[0];Chart.Faces.Add(Duplicate); // A third edge owner must not become a seam.
 auto Reference=[&](){TArray<FIntVector> Neighbors;Neighbors.Init(FIntVector(-1),Chart.Faces.Num());TMap<FString,TArray<TPair<int32,int32>>> Edges;
  auto Key=[](FVector P){return FString::Printf(TEXT("%lld,%lld,%lld"),FMath::RoundToInt64(P.X/.001),FMath::RoundToInt64(P.Y/.001),FMath::RoundToInt64(P.Z/.001));};
  for(int32 I=0;I<Chart.Faces.Num();++I)for(int32 E=0;E<3;++E){const FString A=Key(Chart.Faces[I].P[E]),B=Key(Chart.Faces[I].P[(E+1)%3]);Edges.FindOrAdd(A<B?A+TEXT(";")+B:B+TEXT(";")+A).Add({I,E});}
  for(const auto& Edge:Edges)if(Edge.Value.Num()==2){const auto L=Edge.Value[0],R=Edge.Value[1];Neighbors[L.Key][L.Value]=R.Key;Neighbors[R.Key][R.Value]=L.Key;}return Neighbors;
 };
 const double TextStart=FPlatformTime::Seconds();const auto Expected=Reference();const double TextMs=(FPlatformTime::Seconds()-TextStart)*1000;
 const double BinaryStart=FPlatformTime::Seconds();Chart.BuildAdjacency();const double BinaryMs=(FPlatformTime::Seconds()-BinaryStart)*1000;bool Same=true;
 for(int32 I=0;I<Chart.Faces.Num();++I)for(int32 E=0;E<3;++E)Same&=Chart.Faces[I].Neighbor[E]==Expected[I][E];
 TestTrue(TEXT("Binary welding preserves all seam and nonmanifold edges on 20001 faces"),Same);
 AddInfo(FString::Printf(TEXT("Adjacency fixture faces=%d text_reference=%.3fms binary=%.3fms parity=%d"),Chart.Faces.Num(),TextMs,BinaryMs,Same));
 return true;
}
#endif
