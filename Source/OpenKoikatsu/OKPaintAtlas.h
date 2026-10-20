#pragma once
#include "CoreMinimal.h"

// Runtime buddy pages shared by every painted component in one scene.
// Handles remain stable through animation; released charts coalesce and reuse.
struct FOKPaintAtlas
{
 struct FAllocation {int32 Page=INDEX_NONE,Size=0;FIntPoint Origin=FIntPoint::ZeroValue;bool IsValid() const{return Page!=INDEX_NONE;}};
 struct FPage {TArray<FIntPoint> Free[13];};
 int32 Resolution=1024;TArray<FPage> Pages;
 TArray<FIntRect> UploadRects(TConstArrayView<FIntRect> Dirty) const {
  // Copy only touched tiles from the shared CPU mirror. Neighbor charts in a
  // tile are retained. Row runs cap RHI commands without a whole-page upload.
  auto Runs=[&](int32 Grid){TBitArray<> Tiles(false,Grid*Grid);const int32 Tile=Resolution/Grid;
   for(const auto& Rect:Dirty){FIntRect R=Rect;R.Clip(FIntRect(0,0,Resolution,Resolution));if(R.Width()<=0||R.Height()<=0)continue;
    for(int32 Y=R.Min.Y/Tile;Y<=(R.Max.Y-1)/Tile;++Y)for(int32 X=R.Min.X/Tile;X<=(R.Max.X-1)/Tile;++X)Tiles[Y*Grid+X]=true;
   }
   TArray<FIntRect> Out;for(int32 Y=0;Y<Grid;++Y)for(int32 X=0;X<Grid;){if(!Tiles[Y*Grid+X]){++X;continue;}const int32 Begin=X;while(X<Grid&&Tiles[Y*Grid+X])++X;
    const int32 Existing=Out.IndexOfByPredicate([&](const auto& R){return R.Min.X==Begin*Tile&&R.Max.X==X*Tile&&R.Max.Y==Y*Tile;});
    if(Existing!=INDEX_NONE)Out[Existing].Max.Y=(Y+1)*Tile;else Out.Add(FIntRect(Begin*Tile,Y*Tile,X*Tile,(Y+1)*Tile));
   }return Out;
  };
  auto Rects=Runs(32);return Rects.Num()<=256?MoveTemp(Rects):Runs(16);
 }
 void Reset(int32 Size){Pages.Reset();Resolution=FMath::RoundUpToPowerOfTwo(FMath::Clamp(Size,512,4096));}
 FAllocation Allocate(int32 Size){
  Size=FMath::RoundUpToPowerOfTwo(FMath::Clamp(Size,4,Resolution));const int32 Level=FMath::FloorLog2(Size),Top=FMath::FloorLog2(Resolution);
  for(int32 P=0;;++P){if(P==Pages.Num()){auto& Page=Pages.AddDefaulted_GetRef();Page.Free[Top].Add(FIntPoint::ZeroValue);}
   auto& Page=Pages[P];for(int32 L=Level;L<=Top;++L)if(!Page.Free[L].IsEmpty()){
    FIntPoint At=Page.Free[L].Pop(EAllowShrinking::No);for(int32 K=L-1;K>=Level;--K){const int32 Half=1<<K;Page.Free[K].Add(At+FIntPoint(Half,0));Page.Free[K].Add(At+FIntPoint(0,Half));Page.Free[K].Add(At+FIntPoint(Half,Half));}return {P,Size,At};
   }
  }
 }
 void Release(FAllocation A){
  if(!A.IsValid()||!Pages.IsValidIndex(A.Page))return;auto& Page=Pages[A.Page];int32 L=FMath::FloorLog2(A.Size);FIntPoint At=A.Origin;
  while((1<<L)<Resolution){const int32 Side=1<<L,Parent=Side*2;const FIntPoint Base(At.X/Parent*Parent,At.Y/Parent*Parent);FIntPoint Other[3];int32 Count=0;
   for(int32 Y=0;Y<2;++Y)for(int32 X=0;X<2;++X){const FIntPoint Q=Base+FIntPoint(X*Side,Y*Side);if(Q!=At)Other[Count++]=Q;}
   if(!Page.Free[L].Contains(Other[0])||!Page.Free[L].Contains(Other[1])||!Page.Free[L].Contains(Other[2]))break;
   for(const auto& Q:Other)Page.Free[L].RemoveSingleSwap(Q,EAllowShrinking::No);At=Base;++L;
  }Page.Free[L].Add(At);
 }
};
