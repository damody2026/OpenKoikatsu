#include "../OKPaintAtlas.h"
#include "../OKPaintMask.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKPaintPhysicalAtlasTest,"OpenKoikatsu.Paint.PhysicalAtlas",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKPaintPhysicalAtlasTest::RunTest(const FString&)
{
 FOKPaintAtlas Pool;Pool.Reset(1024);TArray<FOKPaintAtlas::FAllocation> Handles;
 for(int32 Resolution:{512,1024,2048,4096}){
  FOKPaintAtlas Upload;Upload.Reset(Resolution);TArray<FIntRect> Dirty;
  for(int32 Y=0;Y<32;++Y)for(int32 X=0;X<32;++X)if((X+Y)%2==0){const FIntPoint At(X*Resolution/32,Y*Resolution/32);Dirty.Add(FIntRect(At,At+FIntPoint(4,4)));}
  const auto Rects=Upload.UploadRects(Dirty);TestTrue(TEXT("Worst fragmentation retains <=256 upload regions"),Rects.Num()<=256);
  for(const auto& D:Dirty)for(int32 Y=D.Min.Y;Y<D.Max.Y;++Y)for(int32 X=D.Min.X;X<D.Max.X;++X)TestTrue(TEXT("Every dirty texel covered"),Rects.ContainsByPredicate([&](const auto& R){return R.Contains(FIntPoint(X,Y));}));
  for(int32 I=0;I<Rects.Num();++I){const auto& R=Rects[I];TestTrue(TEXT("Uploads remain inside the page"),R.Min.X>=0&&R.Min.Y>=0&&R.Max.X<=Resolution&&R.Max.Y<=Resolution);for(int32 J=0;J<I;++J)TestFalse(TEXT("Upload regions do not overlap"),R.Intersect(Rects[J]));}
  const TArray<FIntRect> One={FIntRect(12,12,16,16)};const auto Sparse=Upload.UploadRects(One);int64 Area=0;for(const auto& R:Sparse)Area+=int64(R.Width())*R.Height();TestTrue(TEXT("A small edit avoids copying a quarter or whole atlas"),Area<int64(Resolution)*Resolution/16);
 }
 for(int32 I=0;I<700;++I){const auto A=Pool.Allocate(1<<(2+I%5));TestTrue(TEXT("Chart remains in its shared page"),A.Origin.X>=0&&A.Origin.Y>=0&&A.Origin.X+A.Size<=1024&&A.Origin.Y+A.Size<=1024);
  const FIntRect R(A.Origin,A.Origin+FIntPoint(A.Size,A.Size));for(const auto& B:Handles)if(A.Page==B.Page)TestFalse(TEXT("Different sizes/components cannot overlap"),R.Intersect(FIntRect(B.Origin,B.Origin+FIntPoint(B.Size,B.Size))));Handles.Add(A);}
 const int32 Pages=Pool.Pages.Num();for(int32 I=Handles.Num()-1;I>=0;--I)Pool.Release(Handles[I]);for(int32 I=0;I<Pages;++I){const auto A=Pool.Allocate(1024);TestEqual(TEXT("Released charts coalesce into a whole reusable page"),A.Page,I);}TestEqual(TEXT("No leaked pages after recycling"),Pool.Pages.Num(),Pages);
 TestTrue(TEXT("Smaller physical area no longer gets a 64-square chart"),FOKPaintMask::PhysicalChartSize(.01,1024)<FOKPaintMask::PhysicalChartSize(20.,1024));
 for(bool Reverse:{false,true}){
  FOKPaintFilm Film;Film.Faces.SetNum(2);auto& A=Film.Faces[0];auto& B=Film.Faces[1];A.P[0]=FVector(0,-4,4);A.P[1]=FVector(0,4,4);A.P[2]=FVector(0,-4,-4);B.P[0]=A.P[1];B.P[1]=FVector(0,4,-4);B.P[2]=A.P[2];A.ChartSize=Reverse?32:4;B.ChartSize=Reverse?4:32;Film.BuildAdjacency();FOKPaintMask Mask;
  const auto Brush=Mask.Brush(A,FVector(0,0,1),1.5);TestEqual(TEXT("Brush uses the source physical chart resolution"),Brush.Num(),A.ChartSize*A.ChartSize);Mask.Deposit(0,A,Brush,.5);Film.AddVolume(0,.5);
  for(int32 Step=0;Step<150;++Step){Mask.Transport(Film,.05,4,2,.004,Step+1);TestTrue(TEXT("Mixed-resolution seam transport conserves mass"),FMath::Abs(Film.Volume()-.5)<1.e-8);for(const auto& E:Mask.Height){TestEqual(TEXT("Destination chart has its own resolution"),E.Value.Num(),Film.Faces[E.Key].ChartSize*Film.Faces[E.Key].ChartSize);for(float H:E.Value)TestTrue(TEXT("Mixed chart thickness finite and nonnegative"),FMath::IsFinite(H)&&H>=0);}}
  TestTrue(TEXT("Paint crosses from one chart resolution to the other"),Film.Faces[1].Volume>0);
  FVector Anchor;const double Taken=Mask.TakeDrip(1,B,.001,0.,.008,Anchor);TestTrue(TEXT("Mixed-resolution dripping bounded by face mass"),Taken>=0&&Taken<=B.Volume+1.e-8);
 }
 return true;
}
#endif
