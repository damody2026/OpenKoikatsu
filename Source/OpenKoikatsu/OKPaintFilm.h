#pragma once
#include "CoreMinimal.h"

// Volume in cm^3, area in cm^2, thickness in cm. Geometry is the displayed pose.
struct FOKPaintFace
{
 FVector P[3];
 FVector2D UV[3];
 int32 Neighbor[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
 double Volume=0.;
 // Stable runtime sampling density, independent of mesh splitting and pose.
 int32 ChartSize=0;
 FVector Center() const { return (P[0]+P[1]+P[2])/3.; }
 FVector Normal() const { return FVector::CrossProduct(P[1]-P[0],P[2]-P[0]).GetSafeNormal(); }
 double Area() const { return FMath::Max(.000001,FVector::CrossProduct(P[1]-P[0],P[2]-P[0]).Length()*.5); }
 // Authored meshes can contain collapsed faces. Preserve the engine's zero
 // result for these without treating expected geometry as a per-frame warning.
 FVector Barycentric(const FVector& Point) const { FVector Result=FVector::ZeroVector;if(!FMath::ComputeBarycentricTri(Point,P[0],P[1],P[2],Result,UE_DOUBLE_SMALL_NUMBER))return FVector::ZeroVector;return Result; }
 FVector2D GravityUV() const;
};
struct FOKPaintFilm
{
 TArray<FOKPaintFace> Faces;
 TSet<int32> Active;
 void AddVolume(int32 Face,double Amount);
 void BuildAdjacency(double WeldTolerance=.001);
 void Advance(double Dt,double Viscosity,double Speed,double RetainedThickness,const TMap<int32,FVector>* EdgeCoverage=nullptr);
 double Volume() const;
 // Swept sphere against the actual deformed triangle; physics-asset UVs are unused.
 static bool SweepTriangle(const FVector& A,const FVector& B,double Radius,const FOKPaintFace& Face,double& T,FVector& Bary);
};
