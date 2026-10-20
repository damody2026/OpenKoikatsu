#pragma once
#include "OKPaintFilm.h"

// Texture detail is independent of the mesh vertex count. Charts have a one
// texel gutter; outside texels extend the triangle boundary for bilinear reads.
struct FOKPaintMask
{
 int32 Size=16;
 static int32 PhysicalChartSize(double Area,int32 Resolution){return FMath::Clamp(int32(FMath::RoundUpToPowerOfTwo(FMath::CeilToInt(FMath::Sqrt(FMath::Max(0.,Area)*2.)*FMath::Clamp(Resolution,512,4096)/256.+2.))),4,64);}
 int32 Resolution(const FOKPaintFace& Face) const {return Face.ChartSize>0?Face.ChartSize:Size;}
 TMap<int32,TArray<float>> Height;
 TSet<int32> DirtyFaces;
 TMap<int32,FVector> EdgeCoverage;
 // Persistent immutable read buffer; swap instead of cloning every chart.
 TMap<int32,TArray<float>> PreviousHeight;
 // Resolution-only runtime scratch; no character, pose or volume dependency.
 TArray<FVector> ChartBary;
 TArray<uint8> ChartInterior;
 int32 ChartResolution=0;
 void PrepareChartCoordinates();
 static FVector Bary(int32 X,int32 Y,int32 Size,bool& Inside);
 static double Mean(const TArray<float>& Values,int32 Size);
 TArray<float> Brush(const FOKPaintFace& Face,FVector Point,double Radius) const;
 double BrushArea(const FOKPaintFace& Face,const TArray<float>& Brush) const;
 void Deposit(int32 Face,const FOKPaintFace& Geometry,const TArray<float>& Brush,double Volume);
 double TakeDrip(int32 Face,const FOKPaintFace& Geometry,double Threshold,double Residual,double Maximum,FVector& Bary,double SurfaceSide=1);
 void Advance(const FOKPaintFilm& Film,double Dt,double Speed,double Viscosity,double Residual,bool ParallelCharts=true);
 // Conservative sparse texel transport owns both footprint and face mass.
 // No independent face flux or synthetic triangle-sized fallback brush.
 void Transport(FOKPaintFilm& Film,double Dt,double Speed,double Viscosity,double Residual,uint64 GeometryRevision=0,bool EagerGeometry=false,bool DirectChart=true);
 struct FRoute {int32 Face;FVector Bary;};
 struct FRoutePrediction {FRoute Route;uint8 Valid=0;};
 TArray<FRoutePrediction> RouteScratch;
 struct FGeometry {double Area;FVector Normal,DeltaBary;};
 TArray<FGeometry> TransportGeometry;
 TArray<uint8> TransportGeometryReady;
 TMap<uint64,FRoute> TransportRoutes;
 uint64 TransportRevision=0;double TransportTravel=-1;int32 TransportSize=0;
};
