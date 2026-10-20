#include "OKPaintFilm.h"

namespace {
struct FOKPaintWeldPoint {
 int64 X,Y,Z;
 bool operator==(const FOKPaintWeldPoint& B) const {return X==B.X&&Y==B.Y&&Z==B.Z;}
 bool operator<(const FOKPaintWeldPoint& B) const {return X!=B.X?X<B.X:Y!=B.Y?Y<B.Y:Z<B.Z;}
 friend uint32 GetTypeHash(const FOKPaintWeldPoint& P){return HashCombine(HashCombine(::GetTypeHash(P.X),::GetTypeHash(P.Y)),::GetTypeHash(P.Z));}
};
struct FOKPaintWeldEdge {
 FOKPaintWeldPoint A,B;
 bool operator==(const FOKPaintWeldEdge& Other) const {return A==Other.A&&B==Other.B;}
 friend uint32 GetTypeHash(const FOKPaintWeldEdge& E){return HashCombine(GetTypeHash(E.A),GetTypeHash(E.B));}
};
struct FOKPaintEdgeOwners {int32 Count=0,Face[2]={-1,-1},Edge[2]={-1,-1};};
}

FVector2D FOKPaintFace::GravityUV() const
{
 const FVector Down(0,0,-1),N=Normal(),G=Down-N*FVector::DotProduct(Down,N);
 const FVector E1=P[1]-P[0],E2=P[2]-P[0];
 const double A=E1.SquaredLength(),B=FVector::DotProduct(E1,E2),C=E2.SquaredLength(),D=A*C-B*B;
 if(D<1.e-12)return FVector2D::ZeroVector;
 const double X=FVector::DotProduct(G,E1),Y=FVector::DotProduct(G,E2);
 return (UV[1]-UV[0])*((X*C-Y*B)/D)+(UV[2]-UV[0])*((Y*A-X*B)/D);
}
void FOKPaintFilm::BuildAdjacency(double Tolerance)
{
 // Geometric edge identity bridges duplicate UV/material vertices, without
 // connecting merely nearby centres or unrelated crossing surfaces.
 TMap<FOKPaintWeldEdge,FOKPaintEdgeOwners> Edges;Edges.Reserve(Faces.Num()*3);
 auto Point=[Tolerance](FVector P){return FOKPaintWeldPoint{FMath::RoundToInt64(P.X/Tolerance),FMath::RoundToInt64(P.Y/Tolerance),FMath::RoundToInt64(P.Z/Tolerance)};};
 for(int32 I=0;I<Faces.Num();++I)for(int32 E=0;E<3;++E){
  Faces[I].Neighbor[E]=INDEX_NONE;const auto A=Point(Faces[I].P[E]),B=Point(Faces[I].P[(E+1)%3]);
  auto& Owners=Edges.FindOrAdd(A<B?FOKPaintWeldEdge{A,B}:FOKPaintWeldEdge{B,A});
  if(Owners.Count<2){Owners.Face[Owners.Count]=I;Owners.Edge[Owners.Count]=E;}++Owners.Count;
 }
 // Three or more owners remain disconnected, just as in the original path.
 for(const auto& Edge:Edges)if(Edge.Value.Count==2){const auto& Owners=Edge.Value;Faces[Owners.Face[0]].Neighbor[Owners.Edge[0]]=Owners.Face[1];Faces[Owners.Face[1]].Neighbor[Owners.Edge[1]]=Owners.Face[0];}
}
void FOKPaintFilm::AddVolume(int32 Face,double Amount){if(!Faces.IsValidIndex(Face)||!FMath::IsFinite(Amount))return;auto& F=Faces[Face];F.Volume=FMath::Max(0.,F.Volume+Amount);if(F.Volume>0)Active.Add(Face);else Active.Remove(Face);}
double FOKPaintFilm::Volume() const {double V=0;for(int32 I:Active)V+=Faces[I].Volume;return V;}
void FOKPaintFilm::Advance(double Dt,double Viscosity,double Speed,double Retained,const TMap<int32,FVector>* EdgeCoverage)
{
 if(Dt<=0||!FMath::IsFinite(Dt))return;
 TMap<int32,double> Delta;
 for(int32 I:Active){const auto& F=Faces[I];
  const double Mobile=FMath::Max(0.,F.Volume-F.Area()*FMath::Max(0.,Retained));if(Mobile<=0)continue;
  const FVector N=F.Normal(),Down(0,0,-1),Gravity=Down-N*FVector::DotProduct(Down,N);
  double Weights[3]={0,0,0},Total=0;
  for(int32 E=0;E<3;++E)if(Faces.IsValidIndex(F.Neighbor[E])){
   const auto& Other=Faces[F.Neighbor[E]];const FVector D=Other.Center()-F.Center();
   // A finite-volume graph is the cross-chart flux. GravityUV is the same
   // tangent direction in the dedicated per-triangle chart, never fixed V.
   Weights[E]=FMath::Max(0.,FVector::DotProduct(Gravity,D)/FMath::Max(.001,D.SquaredLength()));
   if(EdgeCoverage){const auto* Coverage=EdgeCoverage->Find(I);Weights[E]*=Coverage?FMath::Clamp((*Coverage)[E],0.,10.):0.;}
   Total+=Weights[E];
  }
  if(Total<=0)continue;
  const double Flux=Mobile*(1.-FMath::Exp(-Dt*FMath::Max(0.,Speed)*Total/FMath::Max(.001,Viscosity)));
  Delta.FindOrAdd(I)-=Flux;for(int32 E=0;E<3;++E)if(Weights[E]>0)Delta.FindOrAdd(F.Neighbor[E])+=Flux*Weights[E]/Total;
 }
 for(const auto& Change:Delta)AddVolume(Change.Key,Change.Value);
}
bool FOKPaintFilm::SweepTriangle(const FVector& A,const FVector& B,double R,const FOKPaintFace& F,double& Time,FVector& Bary)
{
 // Distance(segment,triangle) is convex. Conservative advancement by distance
 // minus radius cannot jump over a thin surface, even for a high-speed packet.
 const FVector D=B-A;const double Length=D.Length();if(Length<1.e-8)return false;
 double T=0;
 for(int32 Iter=0;Iter<64 && T<=1.;++Iter){
  const FVector Q=A+D*T,Closest=FMath::ClosestPointOnTriangleToPoint(Q,F.P[0],F.P[1],F.P[2]);
  const double Distance=(Q-Closest).Length();
  if(Distance<=R+.001){Time=T;Bary=F.Barycentric(Closest);return !Bary.ContainsNaN();}
  T+=FMath::Max(.0000001,(Distance-R)/Length*.95);
 }
 return false;
}
