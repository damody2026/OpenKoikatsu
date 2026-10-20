#pragma once
#include "CoreMinimal.h"
// Visibility graph over shared scene waypoints. Every edge must pass the real capsule sweep.
inline TArray<FVector> OKStreetContactRoute(const FVector& From,const FVector& To,const TArray<FVector>& Waypoints,TFunctionRef<bool(const FVector&,const FVector&)> Clear){
 TArray<FVector> Nodes{From,To};Nodes.Append(Waypoints);const int32 N=Nodes.Num();
 TArray<double> Dist;Dist.Init(DBL_MAX,N);Dist[0]=0;TArray<int32> Prev;Prev.Init(INDEX_NONE,N);TArray<bool> Done;Done.Init(false,N);
 for(int32 Iter=0;Iter<N;++Iter){int32 U=INDEX_NONE;for(int32 I=0;I<N;++I)if(!Done[I]&&(U==INDEX_NONE||Dist[I]<Dist[U]))U=I;if(U==INDEX_NONE||Dist[U]==DBL_MAX)break;if(U==1)break;Done[U]=true;
  for(int32 V=0;V<N;++V)if(!Done[V]&&V!=U){const double Cost=Dist[U]+FVector::Dist2D(Nodes[U],Nodes[V]);if(Cost<Dist[V]&&Clear(Nodes[U],Nodes[V])){Dist[V]=Cost;Prev[V]=U;}}
 }
 TArray<FVector> Result;if(Prev[1]==INDEX_NONE)return Result;for(int32 I=1;I!=INDEX_NONE;I=Prev[I])Result.Insert(Nodes[I],0);return Result;
}
