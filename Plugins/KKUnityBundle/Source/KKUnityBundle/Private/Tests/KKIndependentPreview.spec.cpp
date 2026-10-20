#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKIndependentPreviewTest,"OpenKoikatsu.IndependentCard.PortableSession",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKIndependentPreviewTest::RunTest(const FString& Parameters)
{
    const FString Pack=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_PREVIEW_PACK"));
    if(Pack.IsEmpty()) {AddWarning(TEXT("Set OK_TEST_PREVIEW_PACK to test a converted local snapshot"));return true;}
    FOkSession Session; FString Error;
    if(!TestTrue(TEXT("open without game root"),Session.Open(FString(),Pack,0,Error))) {AddError(Error);return false;}
    TestTrue(TEXT("portable flag"),Session.IsPortable());
    TestTrue(TEXT("has parts"),Session.PartCount()>0);
    const FString ExpectedCount=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_OUTFIT_COUNT"));
    TestEqual(TEXT("converted outfit count"),Session.CoordinateCount(),ExpectedCount.IsEmpty()?1:FCString::Atoi(*ExpectedCount));
    TestEqual(TEXT("body channels"),Session.ShapeBody().Num(),44);
    TestEqual(TEXT("face channels"),Session.ShapeFace().Num(),52);
    const FString ExpectedHeight=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_PREVIEW_HEIGHT"));
    const FString ExpectedBust=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_PREVIEW_BUST"));
    if(!ExpectedHeight.IsEmpty())
    {
        TestTrue(TEXT("session uses saved PNG height"),FMath::IsNearlyEqual(Session.ShapeBody()[0],FCString::Atof(*ExpectedHeight),1e-6f));
    }
    if(!ExpectedBust.IsEmpty()) TestTrue(TEXT("session uses saved PNG bust"),FMath::IsNearlyEqual(Session.ShapeBody()[4],FCString::Atof(*ExpectedBust),1e-6f));
    TestEqual(TEXT("no source animation scan"),Session.ClipSources().Num(),0);
    TestEqual(TEXT("no source clips"),Session.ClipNames().Num(),0);
    TestFalse(TEXT("reject source motion"),Session.SetAnimClip(TEXT("Stand_D_00")));
    TArray<float> Body=Session.ShapeBody(); const TArray<float> Face=Session.ShapeFace();
    Body[0]=0.f; TestTrue(TEXT("short shape"),Session.ApplyShape(Body,Face));
    TArray<FVector> Before,BeforeScale;
    for(int32 I=0;I<Session.SkelBoneCount();++I) {OkBoneView B{};Session.SkelBoneAt(I,B);Before.Add(FVector(B.pose_pos[0],B.pose_pos[1],B.pose_pos[2]));BeforeScale.Add(FVector(B.pose_scl[0],B.pose_scl[1],B.pose_scl[2]));}
    Body[0]=1.f; TestTrue(TEXT("tall shape"),Session.ApplyShape(Body,Face));
    bool Changed=false;
    for(int32 I=0;I<Session.SkelBoneCount();++I) {OkBoneView B{};Session.SkelBoneAt(I,B);FVector Position(B.pose_pos[0],B.pose_pos[1],B.pose_pos[2]);FVector Scale(B.pose_scl[0],B.pose_scl[1],B.pose_scl[2]);TestFalse(TEXT("finite bone"),Position.ContainsNaN()||Scale.ContainsNaN());Changed|=!Position.Equals(Before[I],1e-6)||!Scale.Equals(BeforeScale[I],1e-6);}
    TestTrue(TEXT("shape changes bones"),Changed);
    FOkSession Moved=MoveTemp(Session);TestTrue(TEXT("move preserves portable"),Moved.IsPortable());
    TestFalse(TEXT("old handle released"),Session.IsOpen());
    return true;
}
#endif
