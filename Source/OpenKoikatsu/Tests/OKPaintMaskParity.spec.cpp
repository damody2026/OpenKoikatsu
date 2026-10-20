#include "../OKPaintMask.h"
#include "Misc/AutomationTest.h"
#include "Async/ParallelFor.h"
#if WITH_DEV_AUTOMATION_TESTS



// Texture detail is independent of the mesh vertex count. Charts have a one
// texel gutter; outside texels extend the triangle boundary for bilinear reads.
struct FOKPaintMaskReference
{
 int32 Size=16;
 TMap<int32,TArray<float>> Height;
 TMap<int32,FVector> EdgeCoverage;
 static FVector Bary(int32 X,int32 Y,int32 Size,bool& Inside);
 static double Mean(const TArray<float>& Values,int32 Size);
 TArray<float> Brush(const FOKPaintFace& Face,FVector Point,double Radius) const;
 double BrushArea(const FOKPaintFace& Face,const TArray<float>& Brush) const;
 void Deposit(int32 Face,const FOKPaintFace& Geometry,const TArray<float>& Brush,double Volume);
 double TakeDrip(int32 Face,const FOKPaintFace& Geometry,double Threshold,double Residual,double Maximum,FVector& Bary);
 void Advance(const FOKPaintFilm& Film,double Dt,double Speed,double Viscosity,double Residual);
};


FVector FOKPaintMaskReference::Bary(int32 X,int32 Y,int32 N,bool& Inside)
{
 const double U=double(X)/(N-2),V=double(Y)/(N-2);
 Inside=U>=0&&V>=0&&U+V<=1;
 const FVector P(U,V,0),A(0,0,0),B(1,0,0),C(0,1,0);
 const FVector Q=FMath::ClosestPointOnTriangleToPoint(P,A,B,C);
 return FVector(1-Q.X-Q.Y,Q.X,Q.Y);
}
double FOKPaintMaskReference::Mean(const TArray<float>& Values,int32 N)
{
 double Sum=0;int32 Count=0;
 for(int32 Y=0;Y<=N-2;++Y)for(int32 X=0;X<=N-2-Y;++X){Sum+=Values[Y*N+X];++Count;}
 return Count?Sum/Count:0;
}
TArray<float> FOKPaintMaskReference::Brush(const FOKPaintFace& F,FVector Point,double Radius) const
{
 TArray<float> Result;Result.SetNumZeroed(Size*Size);
 // Keep a subtexel hit visible, without filling its entire mesh triangle.
 const double R=FMath::Max(Radius,FMath::Sqrt(F.Area())/(Size-2));
 for(int32 Y=0;Y<Size;++Y)for(int32 X=0;X<Size;++X){bool Inside;const FVector B=Bary(X,Y,Size,Inside);const FVector P=F.P[0]*B.X+F.P[1]*B.Y+F.P[2]*B.Z;
  const double T=FMath::Max(0.,1.-FVector::DistSquared(P,Point)/(R*R));Result[Y*Size+X]=T*T;}
 return Result;
}
double FOKPaintMaskReference::BrushArea(const FOKPaintFace& F,const TArray<float>& Brush) const{return F.Area()*Mean(Brush,Size);}
void FOKPaintMaskReference::Deposit(int32 Id,const FOKPaintFace& F,const TArray<float>& Brush,double Volume)
{
 const double Area=BrushArea(F,Brush);if(Area<=1.e-12||Volume<=0)return;
 auto& Values=Height.FindOrAdd(Id);if(Values.IsEmpty())Values.SetNumZeroed(Size*Size);
 for(int32 I=0;I<Values.Num();++I)Values[I]+=Volume/Area*Brush[I];
}
double FOKPaintMaskReference::TakeDrip(int32 Id,const FOKPaintFace& F,double Threshold,double Residual,double Maximum,FVector& Anchor)
{
 auto* Values=Height.Find(Id);if(!Values||F.Volume<=0||Maximum<=0)return 0;
 const double Average=Mean(*Values,Size);if(Average<=0)return 0;
 // Film may have transferred volume since the last mask update. Synchronize
 // before testing local thickness; draining a texel must drain the same mass.
 const double Scale=F.Volume/(F.Area()*Average);for(float& H:*Values)H*=Scale;
 const int32 Count=(Size-1)*Size/2;const double CellArea=F.Area()/Count;
 const bool Down=FVector::DotProduct(F.Normal(),FVector(0,0,-1))>.55;
 int32 Selected=-1;double Lowest=DBL_MAX,Thickest=0;
 for(int32 Y=0;Y<=Size-2;++Y)for(int32 X=0;X<=Size-2-Y;++X){const int32 I=Y*Size+X;const double H=(*Values)[I];if(H<Threshold||H<=Residual)continue;bool Inside;const FVector B=Bary(X,Y,Size,Inside),P=F.P[0]*B.X+F.P[1]*B.Y+F.P[2]*B.Z;
  bool Edge=false;for(int32 E=0;E<3;++E)if(F.Neighbor[E]==INDEX_NONE&&B[(E+2)%3]<=1./(Size-2)+1.e-9){const FVector Mid=(F.P[E]+F.P[(E+1)%3])*.5;Edge|=Mid.Z<F.Center().Z-.001;}
  if(!Down&&!Edge)continue;if(P.Z<Lowest-1.e-6||(FMath::Abs(P.Z-Lowest)<1.e-6&&H>Thickest)){Lowest=P.Z;Thickest=H;Selected=I;Anchor=B;}
 }
 if(Selected<0)return 0;
 const double Amount=FMath::Min(Maximum,FMath::Max(0.,double((*Values)[Selected])-Residual)*CellArea);
 if(Amount<=1.e-12)return 0;(*Values)[Selected]=FMath::Max(0.,double((*Values)[Selected])-Amount/CellArea);
 // Extend the updated boundary to padding pixels. Padding has no physical area.
 for(int32 Y=0;Y<Size;++Y)for(int32 X=0;X<Size;++X)if(X+Y>Size-2){bool Inside;const FVector B=Bary(X,Y,Size,Inside);const int32 IX=FMath::Clamp(FMath::RoundToInt(B.Y*(Size-2)),0,Size-2);const int32 IY=Size-2-IX;(*Values)[Y*Size+X]=(*Values)[IY*Size+IX];}
 return Amount;
}
void FOKPaintMaskReference::Advance(const FOKPaintFilm& Film,double Dt,double Speed,double Viscosity,double Residual)
{
 // Backtrace in the animated world tangent plane. Crossing a chart edge reads
 // the adjacent geometric triangle, including unrelated source UV islands.
 const auto Previous=Height;
 auto Read=[&](int32 Id,FVector World)->double{const auto* Values=Previous.Find(Id);if(!Values)return 0;const auto& F=Film.Faces[Id];const FVector Q=FMath::ClosestPointOnTriangleToPoint(World,F.P[0],F.P[1],F.P[2]);const FVector B=FMath::ComputeBaryCentric2D(Q,F.P[0],F.P[1],F.P[2]);if(B.ContainsNaN())return 0;
  const double X=FMath::Clamp(B.Y*(Size-2),0.,double(Size-1)),Y=FMath::Clamp(B.Z*(Size-2),0.,double(Size-1));const int32 IX=FMath::FloorToInt(X),IY=FMath::FloorToInt(Y),NX=FMath::Min(IX+1,Size-1),NY=FMath::Min(IY+1,Size-1);
  return FMath::Lerp(FMath::Lerp(double((*Values)[IY*Size+IX]),double((*Values)[IY*Size+NX]),X-IX),FMath::Lerp(double((*Values)[NY*Size+IX]),double((*Values)[NY*Size+NX]),X-IX),Y-IY);};
 for(int32 Id:Film.Active){const auto& F=Film.Faces[Id];if(F.Volume<=1.e-12){Height.Remove(Id);continue;}auto& Values=Height.FindOrAdd(Id);Values.SetNumZeroed(Size*Size);const FVector G=FVector(0,0,-1)-F.Normal()*FVector::DotProduct(FVector(0,0,-1),F.Normal());
  for(int32 Y=0;Y<Size;++Y)for(int32 X=0;X<Size;++X){bool Inside;const FVector B=Bary(X,Y,Size,Inside),P=F.P[0]*B.X+F.P[1]*B.Y+F.P[2]*B.Z;const double RawLocal=Read(Id,P);const double Local=RawLocal<1.e-12?0:RawLocal,Mobile=FMath::Max(0.,Local-Residual);
   const FVector Back=P-G*(Dt*FMath::Max(0.,Speed)/FMath::Max(.001,Viscosity));int32 Source=Id;double Best=FVector::DistSquared(Back,FMath::ClosestPointOnTriangleToPoint(Back,F.P[0],F.P[1],F.P[2]));
   for(int32 N:F.Neighbor)if(Film.Faces.IsValidIndex(N)){const auto& Other=Film.Faces[N];const double Dist=FVector::DistSquared(Back,FMath::ClosestPointOnTriangleToPoint(Back,Other.P[0],Other.P[1],Other.P[2]));if(Dist<Best-1.e-10){Best=Dist;Source=N;}}
   Values[Y*Size+X]=FMath::Min(Local,Residual)+(Local>0?Mobile/Local:1.)*Read(Source,Back);
  }
  double Total=Mean(Values,Size)*F.Area();
  if(Total<1.e-12){ // Incoming finite-volume flux seeds the shared boundary.
   FVector Seed=F.Center();double Up=-DBL_MAX;for(int32 N:F.Neighbor)if(Film.Faces.IsValidIndex(N)&&Previous.Contains(N)){const auto& Other=Film.Faces[N];if(Other.Center().Z>Up){Up=Other.Center().Z;Seed=FMath::ClosestPointOnTriangleToPoint(Other.Center(),F.P[0],F.P[1],F.P[2]);}}
   Values=Brush(F,Seed,FMath::Sqrt(F.Area())*.5);Total=Mean(Values,Size)*F.Area();
  }
  // The finite-volume film owns mass. Interpolation cannot create paint or
  // remove it; the higher resolution chart only determines its spatial shape.
  if(Total>0)for(float& H:Values)H=FMath::Max(0.,double(H)*F.Volume/Total);
 }
 EdgeCoverage.Reset();
 for(const auto& Entry:Height){const double Average=Mean(Entry.Value,Size);FVector Edges=FVector::ZeroVector;if(Average>0){const int32 M=Size-2;for(int32 I=0;I<=M;++I){Edges.X+=Entry.Value[I];Edges.Y+=Entry.Value[I*Size+M-I];Edges.Z+=Entry.Value[I*Size];}Edges/=((M+1)*Average);}EdgeCoverage.Add(Entry.Key,Edges);}
 for(auto It=Height.CreateIterator();It;++It)if(!Film.Active.Contains(It.Key()))It.RemoveCurrent();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKPaintMaskParity,"OpenKoikatsu.Paint.MaskParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKPaintMaskParity::RunTest(const FString&)
{
 double Worst=0;FString Context;
 for(int32 N:{4,8,16,64}){
  for(int32 Y=-2;Y<N+2;++Y)for(int32 X=-2;X<N+2;++X){bool A,B;const auto P=FOKPaintMask::Bary(X,Y,N,A),Q=FOKPaintMaskReference::Bary(X,Y,N,B);if(A!=B||!P.Equals(Q,1.e-12)){AddError(TEXT("Chart projection differs from reference"));return false;}}
  for(int32 Pose=0;Pose<4;++Pose)for(double Dt:{0.,1./120.,.03}){
   FOKPaintFilm Film;Film.Faces.SetNum(2);auto& A=Film.Faces[0];auto& B=Film.Faces[1];
   A.P[0]=FVector(0,0,2);A.P[1]=FVector(0,1,1);A.P[2]=FVector(0,-1,1);
   B.P[0]=A.P[2];B.P[1]=A.P[1];B.P[2]=FVector(0,0,0);
   const FQuat R(FVector(1,2,3).GetSafeNormal(),Pose*.8);for(auto& F:Film.Faces)for(auto& P:F.P)P=R.RotateVector(P*FVector(.3,2,3))+FVector(100,-200,50);
   Film.BuildAdjacency();Film.AddVolume(0,1);Film.AddVolume(1,.4);
   FOKPaintMask Fast;FOKPaintMaskReference Ref;Fast.Size=Ref.Size=N;
   for(int32 Id=0;Id<2;++Id){const auto& F=Film.Faces[Id];const auto Brush=Ref.Brush(F,F.Center(),.6);Fast.Deposit(Id,F,Brush,F.Volume);Ref.Deposit(Id,F,Brush,F.Volume);}
   for(int32 Step=0;Step<4;++Step){Fast.Advance(Film,Dt,8,3,.004);Ref.Advance(Film,Dt,8,3,.004);for(const auto& Entry:Ref.Height){const auto& Values=Fast.Height[Entry.Key];for(int32 I=0;I<Values.Num();++I){const double D=FMath::Abs(double(Values[I])-Entry.Value[I]);if(D>Worst){Worst=D;Context=FString::Printf(TEXT("N=%d pose=%d dt=%g step=%d face=%d pixel=%d fast=%g old=%g"),N,Pose,Dt,Step,Entry.Key,I,Values[I],Entry.Value[I]);}}}}
  }
 }
 // Drip eligibility is evaluated once per animated face. Compare its full
 // result (including synchronization when no texel can exit) with the oracle.
 for(int32 N:{4,8,16,64})for(int32 Pose=0;Pose<6;++Pose)for(bool Closed:{false,true}){
  FOKPaintFace Face;Face.P[0]=FVector(0,0,2);Face.P[1]=FVector(0,2,0);Face.P[2]=FVector(0,-2,0);
  const FQuat R(FVector(1,2,3).GetSafeNormal(),Pose*.7);for(auto& P:Face.P)P=R.RotateVector(P*FVector(.3,2,3))+FVector(100,-200,50);
  for(int32 E=0;E<3;++E)Face.Neighbor[E]=Closed?0:INDEX_NONE;Face.Volume=2.;
  FOKPaintMask Fast;FOKPaintMaskReference Ref;Fast.Size=Ref.Size=N;
  const auto Brush=Ref.Brush(Face,Face.Center(),1.);Fast.Deposit(0,Face,Brush,Face.Volume);Ref.Deposit(0,Face,Brush,Face.Volume);
  Face.Volume*=.7;FVector A=FVector::ZeroVector,B=FVector::ZeroVector;
  const double Amount=Fast.TakeDrip(0,Face,.005,.004,.008,A),Expected=Ref.TakeDrip(0,Face,.005,.004,.008,B);
  TestTrue(TEXT("Drip amount matches original exit search"),FMath::IsNearlyEqual(Amount,Expected,1.e-12));
  if(Amount>0)TestTrue(TEXT("Drip anchor matches original exit search"),A.Equals(B,1.e-12));
  for(int32 I=0;I<N*N;++I)Worst=FMath::Max(Worst,FMath::Abs(double(Fast.Height[0][I])-Ref.Height[0][I]));
 }
 // Retained paint must keep its footprint while geometry area changes.
 FOKPaintFilm HeldFilm;HeldFilm.Faces.SetNum(1);auto& HeldFace=HeldFilm.Faces[0];HeldFace.P[0]=FVector(0,0,0);HeldFace.P[1]=FVector(0,2,0);HeldFace.P[2]=FVector(0,0,2);HeldFilm.AddVolume(0,.00001);
 FOKPaintMask Held;Held.Size=8;Held.Deposit(0,HeldFace,Held.Brush(HeldFace,HeldFace.Center(),.3),HeldFace.Volume);const auto HeldBefore=Held.Height[0];Held.Advance(HeldFilm,.5,8,8,.004);TestTrue(TEXT("Retained paint does not diffuse into dry texels"),Held.Height[0]==HeldBefore);
 // Exercise the worker path, including growth of the sparse output map.
 FOKPaintFilm Batch;Batch.Faces.SetNum(256);FOKPaintMask Parallel;FOKPaintMaskReference Serial;Parallel.Size=Serial.Size=16;
 for(int32 Id=0;Id<256;++Id){auto& F=Batch.Faces[Id];const FVector P(Id*3.,0,0);F.P[0]=P;F.P[1]=P+FVector(0,2,0);F.P[2]=P+FVector(0,0,2);Batch.AddVolume(Id,.3+Id*.001);const auto Brush=Serial.Brush(F,F.Center(),.4);if(Id%2==0){Parallel.Deposit(Id,F,Brush,F.Volume);Serial.Deposit(Id,F,Brush,F.Volume);}}
 for(int32 Step=0;Step<3;++Step){Parallel.Advance(Batch,.05,8,8,.004,Step%2==0);Serial.Advance(Batch,.05,8,8,.004);}
 for(const auto& E:Serial.Height)for(int32 I=0;I<E.Value.Num();++I)Worst=FMath::Max(Worst,FMath::Abs(double(Parallel.Height[E.Key][I])-E.Value[I]));
 // Multiple surfaces run concurrently, each owning its maps; the film is
 // immutable until every surface has completed transport.
 TArray<FOKPaintMask> Surfaces;TArray<FOKPaintMaskReference> SurfaceRefs;
 for(int32 I=0;I<4;++I){Surfaces.Add(Parallel);SurfaceRefs.Add(Serial);}
 ParallelFor(TEXT("PaintSurfaceMaskParity"),4,1,[&](int32 I){Surfaces[I].Advance(Batch,.05,8,8,.004,false);});
 for(int32 I=0;I<4;++I){SurfaceRefs[I].Advance(Batch,.05,8,8,.004);for(const auto& E:SurfaceRefs[I].Height)for(int32 P=0;P<E.Value.Num();++P)Worst=FMath::Max(Worst,FMath::Abs(double(Surfaces[I].Height[E.Key][P])-E.Value[P]));}
 TestTrue(TEXT("Optimized advection matches world projection oracle"),Worst<1.e-5);AddInfo(FString::Printf(TEXT("Mask oracle max thickness difference %.12g"),Worst));AddInfo(Context);return true;
}
#endif
