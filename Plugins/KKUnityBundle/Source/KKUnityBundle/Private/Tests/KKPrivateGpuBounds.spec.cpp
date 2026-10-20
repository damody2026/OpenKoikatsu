#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "KKPrivateGpuBounds.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKPrivateGpuBoundsTest,"OpenKoikatsu.GPU.PrivateBounds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKPrivateGpuBoundsTest::RunTest(const FString&){
 FKKPrivateGpuSource Source;Source.Revision=71;Source.PaletteCount=3;Source.FallbackSlot=2;
 TArray<FOKGpuDeformDraw> Draw;
 for(int32 I=0;I<257;++I){auto& V=Source.Vertices.AddDefaulted_GetRef();V.Position=FVector4f((I%11)*.12f-.7f,(I%17)*.08f-.4f,(I%23)*.17f,1);
  V.Normal=FVector4f(0,1,0,0);V.Tangent=FVector4f(1,0,0,1);V.Bones=FUintVector4(0,1,0,1);
  switch(I%5){case 0:V.Weights=FVector4f(1.2f,-.2f,0,0);break;case 1:V.Weights=FVector4f(.5001f,-.5f,0,0);break;case 2:V.Weights=FVector4f(-1,0,0,0);break;case 3:V.Weights=FVector4f(1.e-7f,0,0,0);break;default:V.Weights=FVector4f(.1f,.2f,.3f,.4f);}
  auto& D=Draw.AddDefaulted_GetRef();D.Binding=FUintVector4(I,0,0,0);D.Offset=FVector4f(.1f,-.3f,.2f,0);
 }
 FKKPrivateGpuBounds Bounds;FString Error;if(!TestTrue(TEXT("Signed source bounds built"),OKBuildPrivateGpuBounds(Source,Draw,Bounds,Error)))return false;
 TestTrue(TEXT("Runtime bounds groups independent of source count"),Bounds.Groups.Num()<8);
 for(int32 Frame=0;Frame<16;++Frame){FKKPrivateGpuPose Pose;Pose.Revision=Source.Revision;Pose.Palette.Init(FMatrix44f::Identity,3);
  Pose.Palette[0].M[0][0]=-1.3f;Pose.Palette[0].M[1][0]=.7f;Pose.Palette[0].M[3][2]=Frame*.03f;
  Pose.Palette[1].M[2][1]=.33f;Pose.Palette[1].M[3][0]=Frame*.07f;
  Pose.PostCorrection.M[0][0]=Frame%3?-.8f:0;Pose.PostCorrection.M[2][1]=.3f;Pose.PostCorrection.M[3][1]=-.4f;
  FBox Box(ForceInit);if(!TestTrue(TEXT("Affine frame bounds evaluated"),OKEvaluatePrivateGpuBounds(Bounds,Pose,Box,Error)))return false;
  for(const auto& V:Source.Vertices){float Total=0;FVector4f P(0,0,0,0);
   for(int32 S=0;S<4;++S){const float W=V.Weights[S];if(FMath::Abs(W)<1.e-6f)continue;P+=Pose.Palette[V.Bones[S]].TransformFVector4(V.Position)*W;Total+=W;}
   P=Total>1.e-5f?P/Total:V.Position;P.W=1;P=Pose.PostCorrection.TransformFVector4(P);
   const FVector UE(-P.X*100.f,P.Z*100.f,P.Y*100.f);
   if(!Box.IsInsideOrOn(UE)||!Box.IsInsideOrOn(UE+FVector(.1,-.3,.2))){AddError(TEXT("Signed/fallback vertex escaped bounds"));return false;}
  }
  Pose.Revision++;const FBox Stable=Box;TestFalse(TEXT("Stale bounds rejected"),OKEvaluatePrivateGpuBounds(Bounds,Pose,Box,Error));TestTrue(TEXT("Rejected bounds retain previous box"),Box.Min==Stable.Min&&Box.Max==Stable.Max);
 }
 return !HasAnyErrors();
}
#endif
