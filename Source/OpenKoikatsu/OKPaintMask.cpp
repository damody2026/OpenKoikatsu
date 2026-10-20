#include "OKPaintMask.h"
#include "Async/ParallelFor.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
static TAutoConsoleVariable<int32> CVarPaintTransportParallel(TEXT("ok.Paint.TransportParallel"),1,TEXT("Parallel read-only route queries, preserving serial accumulation; 0 original, 1 automatic, 2 force for parity"));

static TAutoConsoleVariable<int32> CVarPaintChartCoordinates(TEXT("ok.Paint.ChartCoordinates"),1,TEXT("Reuse exact resolution-only chart coordinates; 0 uses the original calculations"));
void FOKPaintMask::PrepareChartCoordinates(){
 if(Size<=2||Size>128||(ChartResolution==Size&&ChartBary.Num()==Size*Size&&ChartInterior.Num()==Size*Size))return;
 ChartBary.SetNumUninitialized(Size*Size,EAllowShrinking::No);ChartInterior.SetNumUninitialized(Size*Size,EAllowShrinking::No);
 for(int32 Y=0;Y<Size;++Y)for(int32 X=0;X<Size;++X){bool Inside;ChartBary[Y*Size+X]=Bary(X,Y,Size,Inside);ChartInterior[Y*Size+X]=Inside;}
 ChartResolution=Size;
}

void FOKPaintMask::Transport(FOKPaintFilm& Film,double Dt,double Speed,double Viscosity,double Residual,uint64 GeometryRevision,bool EagerGeometry,bool DirectChart)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintMaskTransport);
 if(Dt<=0||Height.IsEmpty())return;
 const bool CacheChart=CVarPaintChartCoordinates.GetValueOnAnyThread()!=0;if(CacheChart)PrepareChartCoordinates();
 const bool Uniform=!Film.Faces.ContainsByPredicate([this](const auto& F){return Resolution(F)!=Size;});
 const FVector* Chart=Uniform&&CacheChart&&ChartResolution==Size&&ChartBary.Num()==Size*Size?ChartBary.GetData():nullptr;
 const double InitialVolume=Film.Volume();
 Swap(PreviousHeight,Height);
 // Reuse chart storage; the previous read buffer stays immutable.
 for(auto& E:Height)FMemory::Memzero(E.Value.GetData(),E.Value.Num()*sizeof(float));

 const double Travel=Dt*FMath::Max(0.,Speed)/FMath::Max(.001,Viscosity);
 if(!GeometryRevision||TransportRevision!=GeometryRevision||TransportTravel!=Travel||TransportSize!=Size||TransportGeometry.Num()!=Film.Faces.Num()||TransportGeometryReady.Num()!=Film.Faces.Num()){
  TransportRoutes.Reset();TransportGeometry.SetNumUninitialized(Film.Faces.Num());
  TransportGeometryReady.SetNumZeroed(Film.Faces.Num());for(auto& Ready:TransportGeometryReady)Ready=0;
  TransportRevision=GeometryRevision;TransportTravel=Travel;TransportSize=Size;
 }
 // Only wet charts and neighbors reached by the exact surface walk need
 // transport geometry. The full mesh remains available to collision/BVH.
 auto GeometryAt=[&](int32 Id)->const FGeometry&{auto& G=TransportGeometry[Id];if(!TransportGeometryReady[Id]){const auto& F=Film.Faces[Id];G.Area=F.Area();G.Normal=F.Normal();const FVector Gravity=FVector(0,0,-1)-G.Normal*FVector::DotProduct(FVector(0,0,-1),G.Normal);G.DeltaBary=F.Barycentric(F.P[0]+Gravity*Travel)-FVector(1,0,0);TransportGeometryReady[Id]=1;}return G;};
 if(EagerGeometry)for(int32 Id=0;Id<Film.Faces.Num();++Id)GeometryAt(Id);
 auto QueryRoute=[&](int32 Id,const FVector& B,const FVector& Normal,const FVector& Gravity)->FRoute{
  const auto& F=Film.Faces[Id];const FVector Destination=F.P[0]*B.X+F.P[1]*B.Y+F.P[2]*B.Z+Gravity*Travel;
  int32 Target=Id;FVector Closest=FMath::ClosestPointOnTriangleToPoint(Destination,F.P[0],F.P[1],F.P[2]);double Best=FVector::DistSquared(Destination,Closest);
  for(int32 Hop=0;Hop<8;++Hop){int32 Next=Target;FVector Q=Closest;
   for(int32 N:Film.Faces[Target].Neighbor)if(Film.Faces.IsValidIndex(N)){const auto& Other=Film.Faces[N];if(FVector::DotProduct(Other.Normal(),Normal)<.25)continue;const FVector P=FMath::ClosestPointOnTriangleToPoint(Destination,Other.P[0],Other.P[1],Other.P[2]);const double D=FVector::DistSquared(Destination,P);if(D<Best-1.e-10){Best=D;Next=N;Q=P;}}
   Closest=Q;if(Next==Target)break;Target=Next;
  }
  FVector At=Film.Faces[Target].Barycentric(Closest);if(At.ContainsNaN()){Target=Id;At=B;}return {Target,At};
 };
 // Workers read immutable source texels, geometry and the previous route cache.
 // Only the original serial loop below writes texels, routes or the mass ledger.
 int64 PredictionCount=0;for(const auto& Entry:PreviousHeight)if(Film.Faces.IsValidIndex(Entry.Key)){const int32 N=Resolution(Film.Faces[Entry.Key]);PredictionCount+=N*N;}
 const int32 ParallelMode=CVarPaintTransportParallel.GetValueOnAnyThread();
 const bool Predict=ParallelMode!=0&&PredictionCount<=262144&&(ParallelMode==2||PredictionCount>=8192);
 if(Predict){
  RouteScratch.SetNum(PredictionCount,EAllowShrinking::No);for(auto& R:RouteScratch)R.Valid=0;
  struct FInput {int32 Id,Offset,Resolution;const float* Values;double Scale;FGeometry Geometry;FVector Gravity;};
  TArray<FInput,TInlineAllocator<32>> Inputs;int32 EntryOffset=0;
  for(const auto& Entry:PreviousHeight){const int32 Id=Entry.Key;if(!Film.Faces.IsValidIndex(Id))continue;const int32 ChartEdge=Resolution(Film.Faces[Id]),Offset=EntryOffset;EntryOffset+=ChartEdge*ChartEdge;
   const double MeanH=Mean(Entry.Value,ChartEdge);if(MeanH<=0||Film.Faces[Id].Volume<=0)continue;
   const auto& G=GeometryAt(Id);Inputs.Add({Id,Offset,ChartEdge,Entry.Value.GetData(),Film.Faces[Id].Volume/(MeanH*G.Area),G,FVector(0,0,-1)-G.Normal*FVector::DotProduct(FVector(0,0,-1),G.Normal)});
  }
  const auto& CachedRoutes=TransportRoutes;
  ParallelFor(TEXT("PaintTransportRoutes"),Inputs.Num(),1,[&](int32 Index){const auto& Input=Inputs[Index];const int32 ChartEdge=Input.Resolution,M=ChartEdge-2;
   for(int32 Y=0;Y<=M;++Y)for(int32 X=0;X<=M-Y;++X){const int32 Cell=Y*ChartEdge+X;const double H=FMath::Max(0.,double(Input.Values[Cell])*Input.Scale);
    if(H<=0||H-FMath::Min(H,FMath::Max(0.,Residual))<=0)continue;
    bool Inside;const FVector B=Chart?Chart[Cell]:Bary(X,Y,ChartEdge,Inside),Direct=B+Input.Geometry.DeltaBary;
    if(!Direct.ContainsNaN()&&Direct.X>=0&&Direct.Y>=0&&Direct.Z>=0)continue;
    if(CachedRoutes.Contains((uint64(uint32(Input.Id))<<32)|uint32(Cell)))continue;
    auto& Prediction=RouteScratch[Input.Offset+Cell];Prediction.Route=QueryRoute(Input.Id,B,Input.Geometry.Normal,Input.Gravity);Prediction.Valid=1;
   }
  });
 }
 auto Splat=[&](int32 Id,FVector B,double Mass,float* Chart=nullptr,double Area=0.){if(Mass<=0)return;const int32 ChartEdge=Resolution(Film.Faces[Id]),M=ChartEdge-2,Count=(ChartEdge-1)*ChartEdge/2;
  B.X=FMath::Max(0.,B.X);B.Y=FMath::Max(0.,B.Y);B.Z=FMath::Max(0.,B.Z);B/=FMath::Max(1.e-12,B.X+B.Y+B.Z);
  double X=B.Y*M,Y=B.Z*M;int32 IX=FMath::Min(FMath::FloorToInt(X),M),IY=FMath::Min(FMath::FloorToInt(Y),M-IX);
  const double FX=X-IX,FY=Y-IY;
  if(!Chart){auto& Values=Height.FindOrAdd(Id);if(Values.IsEmpty())Values.SetNumZeroed(ChartEdge*ChartEdge);Chart=Values.GetData();Area=GeometryAt(Id).Area;}
  const double H=Mass*Count/Area;
  auto Add=[&](int32 PX,int32 PY,double W){if(W>0&&PX>=0&&PY>=0&&PX+PY<=M)Chart[PY*ChartEdge+PX]+=H*W;};
  if(IX+IY==M){Add(IX,IY,1);return;}
  if(FX+FY<=1||IX+IY+2>M){const double Total=FMath::Max(1.,FX+FY);Add(IX,IY,FMath::Max(0.,1-FX-FY));Add(IX+1,IY,FX/Total);Add(IX,IY+1,FY/Total);}
  else{Add(IX+1,IY,1-FY);Add(IX,IY+1,1-FX);Add(IX+1,IY+1,FX+FY-1);}
 };
 int32 TransportEntryOffset=0;
 for(const auto& Entry:PreviousHeight){const int32 Id=Entry.Key;if(!Film.Faces.IsValidIndex(Id))continue;const auto& F=Film.Faces[Id];const int32 ChartEdge=Resolution(F),M=ChartEdge-2,Count=(ChartEdge-1)*ChartEdge/2,PredictionOffset=TransportEntryOffset;TransportEntryOffset+=ChartEdge*ChartEdge;
  const double MeanH=Mean(Entry.Value,ChartEdge);if(MeanH<=0||F.Volume<=0)continue;
  const auto& Geometry=GeometryAt(Id);
  const double Scale=F.Volume/(MeanH*Geometry.Area),CellArea=Geometry.Area/Count;
  const FVector Normal=Geometry.Normal,Gravity=FVector(0,0,-1)-Normal*FVector::DotProduct(FVector(0,0,-1),Normal);
  auto& HeldChart=Height.FindOrAdd(Id);if(HeldChart.IsEmpty())HeldChart.SetNumZeroed(ChartEdge*ChartEdge);float* HeldValues=HeldChart.GetData();
  for(int32 Y=0;Y<=M;++Y)for(int32 X=0;X<=M-Y;++X){const double H=FMath::Max(0.,double(Entry.Value[Y*ChartEdge+X])*Scale);if(H<=0)continue;
   bool Inside;const FVector B=Chart?Chart[Y*ChartEdge+X]:Bary(X,Y,ChartEdge,Inside);const double Held=FMath::Min(H,FMath::Max(0.,Residual));
   // Stationary residual belongs to this exact grid cell. Re-interpolating
   // its integer coordinate diffused roundoff into neighboring dry cells.
   if(Held>0)HeldValues[Y*ChartEdge+X]+=Held;
   const double Mobile=(H-Held)*CellArea;if(Mobile<=0)continue;
   const FVector Direct=B+Geometry.DeltaBary;
   // The source chart and its area are already resolved for this face. Map
   // insertion can move TArray objects but never their owned texel storage.
   if(!Direct.ContainsNaN()&&Direct.X>=0&&Direct.Y>=0&&Direct.Z>=0){Splat(Id,Direct,Mobile,DirectChart?HeldValues:nullptr,Geometry.Area);continue;}
   const uint64 RouteKey=(uint64(uint32(Id))<<32)|uint32(Y*ChartEdge+X);
   if(const auto* Route=TransportRoutes.Find(RouteKey)){Splat(Route->Face,Route->Bary,Mobile);continue;}
   const auto* Prediction=Predict?&RouteScratch[PredictionOffset+Y*ChartEdge+X]:nullptr;
   const FRoute Route=Prediction&&Prediction->Valid?Prediction->Route:QueryRoute(Id,B,Normal,Gravity);
   const int32 Target=Route.Face;const FVector At=Route.Bary;
   // Bound memory independently of total mesh size; uncached queries remain
   // exact. A pose/settings revision invalidates every route together.
   if(GeometryRevision&&TransportRoutes.Num()<65536)TransportRoutes.Add(RouteKey,{Target,At});Splat(Target,At,Mobile);
  }
 }
 for(int32 Id:Film.Active.Array())Film.AddVolume(Id,-Film.Faces[Id].Volume);
 EdgeCoverage.Reset();
 for(auto& Entry:Height){const int32 ChartEdge=Resolution(Film.Faces[Entry.Key]),M=ChartEdge-2;const double Volume=Mean(Entry.Value,ChartEdge)*Film.Faces[Entry.Key].Area();Film.AddVolume(Entry.Key,Volume);
  // Padding is only for sampling and carries no physical mass.
  for(int32 Y=0;Y<ChartEdge;++Y)for(int32 X=FMath::Max(0,M-Y+1);X<ChartEdge;++X){const int32 IX=FMath::Clamp((X-Y+M+1)/2,0,M);Entry.Value[Y*ChartEdge+X]=Entry.Value[(M-IX)*ChartEdge+IX];}
 }
 for(auto It=Height.CreateIterator();It;++It)if(!Film.Active.Contains(It.Key()))It.RemoveCurrent();
 for(const auto& E:Height){const auto* Before=PreviousHeight.Find(E.Key);if(!Before||Before->Num()!=E.Value.Num()||FMemory::Memcmp(Before->GetData(),E.Value.GetData(),E.Value.Num()*sizeof(float)))DirtyFaces.Add(E.Key);}
 for(const auto& E:PreviousHeight)if(!Height.Contains(E.Key))DirtyFaces.Add(E.Key);
 // Float atlas texels round independently. Preserve the double-precision
 // mass ledger; feed only the roundoff remainder into the largest wet chart.
 // This is not an independently computed face flux or a new footprint.
 int32 Largest=INDEX_NONE;double LargestVolume=0;
 for(int32 Id:Film.Active)if(Film.Faces[Id].Volume>LargestVolume){LargestVolume=Film.Faces[Id].Volume;Largest=Id;}
 if(Largest!=INDEX_NONE)Film.AddVolume(Largest,InitialVolume-Film.Volume());
}

static TAutoConsoleVariable<int32> CVarPaintSeamQueryReuse(TEXT("ok.Paint.SeamQueryReuse"),1,TEXT("Reuse immutable seam readers and winning closest point; 0 retains the previous query path for comparison"));

FVector FOKPaintMask::Bary(int32 X,int32 Y,int32 N,bool& Inside)
{
 const double U=double(X)/(N-2),V=double(Y)/(N-2);
 Inside=U>=0&&V>=0&&U+V<=1;
 if(Inside)return FVector(1-U-V,U,V);
 if(U>=0&&V>=0){const double T=FMath::Clamp((U-V+1)*.5,0.,1.);return FVector(0,T,1-T);}
 const FVector P(U,V,0),A(0,0,0),B(1,0,0),C(0,1,0);
 const FVector Q=FMath::ClosestPointOnTriangleToPoint(P,A,B,C);
 return FVector(1-Q.X-Q.Y,Q.X,Q.Y);
}
double FOKPaintMask::Mean(const TArray<float>& Values,int32 N)
{
 double Sum=0;int32 Count=0;
 for(int32 Y=0;Y<=N-2;++Y)for(int32 X=0;X<=N-2-Y;++X){Sum+=Values[Y*N+X];++Count;}
 return Count?Sum/Count:0;
}
TArray<float> FOKPaintMask::Brush(const FOKPaintFace& F,FVector Point,double Radius) const
{
 const int32 ChartEdge=Resolution(F);
 TArray<float> Result;Result.SetNumZeroed(ChartEdge*ChartEdge);
 const FVector* Chart=CVarPaintChartCoordinates.GetValueOnAnyThread()!=0&&ChartResolution==ChartEdge&&ChartBary.Num()==ChartEdge*ChartEdge?ChartBary.GetData():nullptr;
 // Keep a subtexel hit visible, without filling its entire mesh triangle.
 const double R=FMath::Max(Radius,FMath::Sqrt(F.Area())/(ChartEdge-2));
 for(int32 Y=0;Y<ChartEdge;++Y)for(int32 X=0;X<ChartEdge;++X){bool Inside;const FVector B=Chart?Chart[Y*ChartEdge+X]:Bary(X,Y,ChartEdge,Inside);const FVector P=F.P[0]*B.X+F.P[1]*B.Y+F.P[2]*B.Z;
  const double T=FMath::Max(0.,1.-FVector::DistSquared(P,Point)/(R*R));Result[Y*ChartEdge+X]=T*T;}
 return Result;
}
double FOKPaintMask::BrushArea(const FOKPaintFace& F,const TArray<float>& Brush) const{return F.Area()*Mean(Brush,Resolution(F));}
void FOKPaintMask::Deposit(int32 Id,const FOKPaintFace& F,const TArray<float>& Brush,double Volume)
{
 const int32 ChartEdge=Resolution(F);
 const double Area=BrushArea(F,Brush);if(Area<=1.e-12||Volume<=0)return;
 auto& Values=Height.FindOrAdd(Id);if(Values.IsEmpty())Values.SetNumZeroed(ChartEdge*ChartEdge);
 for(int32 I=0;I<Values.Num();++I)Values[I]+=Volume/Area*Brush[I];
 DirtyFaces.Add(Id);
}
double FOKPaintMask::TakeDrip(int32 Id,const FOKPaintFace& F,double Threshold,double Residual,double Maximum,FVector& Anchor,double SurfaceSide)
{
 const int32 ChartEdge=Resolution(F);
 const FVector* Chart=CVarPaintChartCoordinates.GetValueOnAnyThread()!=0&&ChartResolution==ChartEdge&&ChartBary.Num()==ChartEdge*ChartEdge?ChartBary.GetData():nullptr;
 auto* Values=Height.Find(Id);if(!Values||F.Volume<=0||Maximum<=0)return 0;
 const double Average=Mean(*Values,ChartEdge);if(Average<=0)return 0;
 // Film may have transferred volume since the last mask update. Synchronize
 // before testing local thickness; draining a texel must drain the same mass.
 const double Scale=F.Volume/(F.Area()*Average);double MaximumHeight=0;bool Changed=false;for(float& H:*Values){const float Before=H;H*=Scale;Changed|=H!=Before;MaximumHeight=FMath::Max(MaximumHeight,double(H));}if(Changed)DirtyFaces.Add(Id);
 if(MaximumHeight<Threshold||MaximumHeight<=Residual)return 0;
 const int32 Count=(ChartEdge-1)*ChartEdge/2;const double CellArea=F.Area()/Count;
 const bool Down=FVector::DotProduct(F.Normal()*SurfaceSide,FVector(0,0,-1))>.55;
 bool LowerBoundary[3];const double CenterZ=F.Center().Z;
 for(int32 E=0;E<3;++E)LowerBoundary[E]=F.Neighbor[E]==INDEX_NONE&&(F.P[E].Z+F.P[(E+1)%3].Z)*.5<CenterZ-.001;
 // An upward/vertical closed face cannot release any texel. Keep the mass
 // synchronization above, but avoid searching a chart with no exit boundary.
 if(!Down&&!LowerBoundary[0]&&!LowerBoundary[1]&&!LowerBoundary[2])return 0;
 int32 Selected=-1;double Lowest=DBL_MAX,Thickest=0;
 for(int32 Y=0;Y<=ChartEdge-2;++Y)for(int32 X=0;X<=ChartEdge-2-Y;++X){const int32 I=Y*ChartEdge+X;const double H=(*Values)[I];if(H<Threshold||H<=Residual)continue;bool Inside;const FVector B=Chart?Chart[I]:Bary(X,Y,ChartEdge,Inside),P=F.P[0]*B.X+F.P[1]*B.Y+F.P[2]*B.Z;
  bool Edge=false;for(int32 E=0;E<3;++E)Edge|=LowerBoundary[E]&&B[(E+2)%3]<=1./(ChartEdge-2)+1.e-9;
  if(!Down&&!Edge)continue;if(P.Z<Lowest-1.e-6||(FMath::Abs(P.Z-Lowest)<1.e-6&&H>Thickest)){Lowest=P.Z;Thickest=H;Selected=I;Anchor=B;}
 }
 if(Selected<0)return 0;
 const double Amount=FMath::Min(Maximum,FMath::Max(0.,double((*Values)[Selected])-Residual)*CellArea);
 if(Amount<=1.e-12)return 0;(*Values)[Selected]=FMath::Max(0.,double((*Values)[Selected])-Amount/CellArea);DirtyFaces.Add(Id);
 // Extend the updated boundary to padding pixels. Padding has no physical area.
 for(int32 Y=0;Y<ChartEdge;++Y)for(int32 X=0;X<ChartEdge;++X)if(X+Y>ChartEdge-2){bool Inside;const FVector B=Chart?Chart[Y*ChartEdge+X]:Bary(X,Y,ChartEdge,Inside);const int32 IX=FMath::Clamp(FMath::RoundToInt(B.Y*(ChartEdge-2)),0,ChartEdge-2);const int32 IY=ChartEdge-2-IX;(*Values)[Y*ChartEdge+X]=(*Values)[IY*ChartEdge+IX];}
 return Amount;
}
void FOKPaintMask::Advance(const FOKPaintFilm& Film,double Dt,double Speed,double Viscosity,double Residual,bool ParallelCharts)
{
 // Backtrace in the animated world tangent plane. Crossing a chart edge reads
 // the adjacent geometric triangle, including unrelated source UV islands.
 Swap(PreviousHeight,Height);
 const auto& Previous=PreviousHeight;
 auto SampleValues=[&](const TArray<float>* Values,FVector B,int32 Size)->double{if(!Values||B.ContainsNaN())return 0;
  const double X=FMath::Clamp(B.Y*(Size-2),0.,double(Size-1)),Y=FMath::Clamp(B.Z*(Size-2),0.,double(Size-1));const int32 IX=FMath::FloorToInt(X),IY=FMath::FloorToInt(Y),NX=FMath::Min(IX+1,Size-1),NY=FMath::Min(IY+1,Size-1);
  return FMath::Lerp(FMath::Lerp(double((*Values)[IY*Size+IX]),double((*Values)[IY*Size+NX]),X-IX),FMath::Lerp(double((*Values)[NY*Size+IX]),double((*Values)[NY*Size+NX]),X-IX),Y-IY);};
 auto Sample=[&](int32 Id,FVector B){return SampleValues(Previous.Find(Id),B,Resolution(Film.Faces[Id]));};
 auto Read=[&](int32 Id,FVector World)->double{const auto& F=Film.Faces[Id];const FVector Q=FMath::ClosestPointOnTriangleToPoint(World,F.P[0],F.P[1],F.P[2]);return Sample(Id,F.Barycentric(Q));};
 // Allocate map entries before workers start. Workers read the immutable
 // previous snapshot and write one distinct chart each; no UObject/RHI work.
 // Chart coordinates depend only on resolution, not on the animated face.
 const bool CacheChart=CVarPaintChartCoordinates.GetValueOnAnyThread()!=0;if(CacheChart)PrepareChartCoordinates();
 const bool ChartReady=CacheChart&&ChartResolution==Size&&ChartBary.Num()==Size*Size&&ChartInterior.Num()==Size*Size;
 TArray<FVector> ChartScratch;TArray<uint8> InteriorScratch;
 if(!ChartReady){ChartScratch.SetNumUninitialized(Size*Size);InteriorScratch.SetNumUninitialized(Size*Size);
  for(int32 Y=0;Y<Size;++Y)for(int32 X=0;X<Size;++X){bool Inside;ChartScratch[Y*Size+X]=Bary(X,Y,Size,Inside);InteriorScratch[Y*Size+X]=Inside;}
 }
 const auto& Chart=ChartReady?ChartBary:ChartScratch;const auto& Interior=ChartReady?ChartInterior:InteriorScratch;
 const bool ReuseSeamQuery=CVarPaintSeamQueryReuse.GetValueOnAnyThread()!=0;
 const double Travel=Dt*FMath::Max(0.,Speed)/FMath::Max(.001,Viscosity);
 const auto Active=Film.Active.Array();TArray<TArray<float>*> Outputs;Outputs.SetNumZeroed(Active.Num());
 for(int32 Id:Active){if(Film.Faces[Id].Volume<=1.e-12)Height.Remove(Id);else Height.FindOrAdd(Id);}
 for(int32 I=0;I<Active.Num();++I)if(Film.Faces[Active[I]].Volume>1.e-12)Outputs[I]=Height.Find(Active[I]);
 ParallelFor(TEXT("PaintChartTransport"),Active.Num(),64,[&](int32 Work){if(!Outputs[Work])return;const int32 Id=Active[Work];const auto& F=Film.Faces[Id];const int32 Size=Resolution(F);auto& Values=*Outputs[Work];Values.SetNumZeroed(Size*Size);const double Area=F.Area();
  const auto* OldValues=Previous.Find(Id);
  if(OldValues){const double Average=Mean(*OldValues,Size);if(Average>0){const double Scale=F.Volume/(Average*Area);double Peak=0;for(float H:*OldValues)Peak=FMath::Max(Peak,double(H));
   if(Peak*Scale<=FMath::Max(0.,Residual)){Values=*OldValues;for(float& H:Values)H*=Scale;return;}
  }}
  // Cache immutable chart readers once per face. Even a dry neighbor must
  // participate in nearest-face selection: skipping it would change seams.
  const FOKPaintFace* Neighbors[3]={nullptr,nullptr,nullptr};
  const TArray<float>* NeighborValues[3]={nullptr,nullptr,nullptr};
  for(int32 E=0;E<3;++E)if(Film.Faces.IsValidIndex(F.Neighbor[E])){Neighbors[E]=&Film.Faces[F.Neighbor[E]];NeighborValues[E]=Previous.Find(F.Neighbor[E]);}
  const FVector Normal=F.Normal(),G=FVector(0,0,-1)-Normal*FVector::DotProduct(FVector(0,0,-1),Normal);
  const FVector Displacement=G*Travel;
  const FVector Shift=F.Barycentric(F.P[0]-Displacement)-FVector(1,0,0);
  TArray<float,TInlineAllocator<128>> Padding;Padding.Init(-1.f,Size*2-1);
  const bool ValidChart=!Shift.ContainsNaN()&&FMath::IsNearlyEqual(Shift.X+Shift.Y+Shift.Z,0.,1.e-7)&&FVector::CrossProduct(F.P[1]-F.P[0],F.P[2]-F.P[0]).SquaredLength()>UE_DOUBLE_SMALL_NUMBER;
  for(int32 Y=0;Y<Size;++Y)for(int32 X=0;X<Size;++X){const int32 Pixel=Y*Size+X;bool Inside;const FVector B=Size==this->Size?Chart[Pixel]:Bary(X,Y,Size,Inside);if(Size==this->Size)Inside=Interior[Pixel]!=0;const int32 Pad=X-Y+Size-1;if(!Inside&&Padding[Pad]>=0){Values[Y*Size+X]=Padding[Pad];continue;}const double RawLocal=ValidChart?(Inside?(OldValues?double((*OldValues)[Pixel]):0.):SampleValues(OldValues,B,Size)):Read(Id,F.P[0]*B.X+F.P[1]*B.Y+F.P[2]*B.Z);const double Local=RawLocal<1.e-12?0:RawLocal,Mobile=FMath::Max(0.,Local-Residual);
   const FVector BackB=B+Shift;
   // An interior point has zero distance to this triangle. No neighbor can
   // improve the original nearest-face test; only seam pixels need geometry.
   if(ValidChart&&BackB.X>=0&&BackB.Y>=0&&BackB.Z>=0){Values[Y*Size+X]=FMath::Min(Local,Residual)+(Local>0?Mobile/Local:1.)*SampleValues(OldValues,BackB,Size);if(!Inside)Padding[Pad]=Values[Y*Size+X];continue;}
   const FVector P=F.P[0]*B.X+F.P[1]*B.Y+F.P[2]*B.Z,Back=P-Displacement;
   const FOKPaintFace* Source=&F;const TArray<float>* SourceValues=OldValues;
   FVector Closest=FMath::ClosestPointOnTriangleToPoint(Back,F.P[0],F.P[1],F.P[2]);double Best=FVector::DistSquared(Back,Closest);
   for(int32 E=0;E<3;++E)if(Neighbors[E]){const auto& Other=*Neighbors[E];const FVector Q=FMath::ClosestPointOnTriangleToPoint(Back,Other.P[0],Other.P[1],Other.P[2]);const double Dist=FVector::DistSquared(Back,Q);if(Dist<Best-1.e-10){Best=Dist;Source=&Other;SourceValues=NeighborValues[E];Closest=Q;}}
   // Reuse the winning closest point; the old Read path projected it again.
   const double Incoming=ReuseSeamQuery?(SourceValues?SampleValues(SourceValues,Source->Barycentric(Closest),Resolution(*Source)):0.):Read(int32(Source-Film.Faces.GetData()),Back);
   Values[Y*Size+X]=FMath::Min(Local,Residual)+(Local>0?Mobile/Local:1.)*Incoming;if(!Inside)Padding[Pad]=Values[Y*Size+X];
  }
  double Total=Mean(Values,Size)*Area;
  if(Total<1.e-12){ // Incoming finite-volume flux seeds the shared boundary.
   FVector Seed=F.Center();double Up=-DBL_MAX;for(int32 N:F.Neighbor)if(Film.Faces.IsValidIndex(N)&&Previous.Contains(N)){const auto& Other=Film.Faces[N];if(Other.Center().Z>Up){Up=Other.Center().Z;Seed=FMath::ClosestPointOnTriangleToPoint(Other.Center(),F.P[0],F.P[1],F.P[2]);}}
   Values=Brush(F,Seed,FMath::Sqrt(Area)*.5);Total=Mean(Values,Size)*Area;
  }
  // The finite-volume film owns mass. Interpolation cannot create paint or
  // remove it; the higher resolution chart only determines its spatial shape.
  if(Total>0)for(float& H:Values)H=FMath::Max(0.,double(H)*F.Volume/Total);
 },!ParallelCharts||Active.Num()<128?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);
 for(auto It=Height.CreateIterator();It;++It)if(!Film.Active.Contains(It.Key()))It.RemoveCurrent();
 EdgeCoverage.Reset();
 for(const auto& Entry:Height){const int32 ChartEdge=Resolution(Film.Faces[Entry.Key]);const double Average=Mean(Entry.Value,ChartEdge);FVector Edges=FVector::ZeroVector;if(Average>0){const int32 M=ChartEdge-2;for(int32 I=0;I<=M;++I){Edges.X+=Entry.Value[I];Edges.Y+=Entry.Value[I*ChartEdge+M-I];Edges.Z+=Entry.Value[I*ChartEdge];}Edges/=((M+1)*Average);}EdgeCoverage.Add(Entry.Key,Edges);}

}
