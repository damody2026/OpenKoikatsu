#include "Misc/AutomationTest.h"
#include "KKAnimationCatalog.h"
#include "KKRustExtract.h"
#include "HAL/PlatformMisc.h"
#include "ok_extract.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKStudioYureTest,"OpenKoikatsu.AnimationCatalog.StudioYureInitialization",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKStudioYureTest::RunTest(const FString&)
{
    auto* Catalog=UKKAnimationCatalog::LoadDefaultCatalog();if(!TestNotNull(TEXT("imported catalog"),Catalog)) return false;
    const FString OldSkip=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NODB")),OldExternal=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
    struct FRestore {FString Skip,External;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),Skip.IsEmpty()?nullptr:*Skip);FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),External.IsEmpty()?nullptr:*External);}} Restore{OldSkip,OldExternal};
    FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),nullptr);
    auto RowFor=[Catalog](const FString& Group,const FString& Category)->FName {TArray<FName> Found;for(FName R:Catalog->FindRows(TEXT("studio_catalog"),TEXT("category"),Category))
        if(Catalog->GetCell(TEXT("studio_catalog"),R,TEXT("origin"))==TEXT("base")&&Catalog->GetCell(TEXT("studio_catalog"),R,TEXT("group"))==Group&&Catalog->GetCell(TEXT("studio_catalog"),R,TEXT("id"))==TEXT("0")) Found.Add(R);return Found.Num()==1?Found[0]:NAME_None;};
    const FName HRow=RowFor(TEXT("2"),TEXT("128")),NormalRow=RowFor(TEXT("0"),TEXT("3")),FallbackRow=RowFor(TEXT("9"),TEXT("183"));
    int32 Frames=0,Positive=0,Atomic=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FOkSession Session,Control;FString Error;const FString Path=FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png");
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),Path,0,Error)||!Control.Open(TEXT("C:/portable/KoikatuSunshine"),Path,0,Error)) {AddError(Error);return false;}
        // Match the real viewer: GPU export first, H is the first animation.
        if(!TestTrue(TEXT("cold GPU export"),Session.SkelBoneCount()>0&&Control.SkelBoneCount()>0)) return false;
        auto Select=[&](FOkSession& S,FName Row,bool Profile){bool IsH,IsMotion;int32 Layer;FString Key,Yure,Owner;
            if(!Catalog->StudioSettings(Row,IsH,Layer,IsMotion)||!Catalog->ResolveStudioAnimation(Row,S.ClipNamesForSource(Catalog->StudioSource(Row)),Key,Error)) return false;
            if(Profile) Catalog->StudioYureSource(Row,Yure,Owner);
            return S.SetStudioAnimation(Key,IsH,.37f,.61f,IsMotion,.43f,Yure,Owner,Catalog->StudioPVMask(Row));};
        if(!Select(Session,HRow,true)||!Select(Control,HRow,false)) {AddError(Error);return false;}
        float ColdRates[3]={};
        if(!TestTrue(TEXT("first H selection preserves source head and EyeTargets"),Session.StepEyeLook(FVector(.45,1.55,2.),1,1.f/60.f,true,ColdRates))) return false;
        for(int32 I=0;I<22;++I) {const int32 Expected=I<2?0:(I<4?1:((I-4)%9<6?0:1));if(!TestEqual(TEXT("source Yure policy reaches session"),Session.StudioYureFlag(I),Expected)) return false;}
        TMap<FString,int32> ControlIndex;
        for(int32 I=0;I<Control.SkelBoneCount();++I) {OkBoneView B{};if(!Control.SkelBoneAt(I,B)) return false;const FString Name=UTF8_TO_TCHAR(B.name);
            if(!TestFalse(TEXT("unique control palette identity"),ControlIndex.Contains(Name))) return false;ControlIndex.Add(Name,I);}
        int32 Changed=0;double MaxDelta=0;
        for(int32 Frame=0;Frame<90;++Frame) {
            const float Time=Frame/60.f;if(!Session.ApplyAnimFrame(Time,1.f/60.f)||!Control.ApplyAnimFrame(Time,1.f/60.f)) return false;
            if(!TestEqual(TEXT("same exported palette"),Session.SkelBoneCount(),Control.SkelBoneCount())) return false;
            for(int32 I=0;I<Session.SkelBoneCount();++I) {
                OkBoneView A{},B{};if(!Session.SkelBoneAt(I,A)) return false;const FString Name=UTF8_TO_TCHAR(A.name);
                const int32* Other=ControlIndex.Find(Name);if(!TestNotNull(TEXT("same bone in independent session"),Other)||!Control.SkelBoneAt(*Other,B)) return false;
                double Delta=0;for(int32 J=0;J<3;++J){if(!TestTrue(TEXT("finite source physics and shape"),FMath::IsFinite(A.pose_pos[J])&&FMath::IsFinite(A.pose_scl[J]))) return false;
                    Delta=FMath::Max(Delta,double(FMath::Abs(A.pose_pos[J]-B.pose_pos[J])));Delta=FMath::Max(Delta,double(FMath::Abs(A.pose_scl[J]-B.pose_scl[J])));}
                for(int32 J=0;J<4;++J) if(!TestTrue(TEXT("finite quaternion"),FMath::IsFinite(A.pose_rot[J]))) return false;
                Changed+=Delta>1.e-5;MaxDelta=FMath::Max(MaxDelta,Delta);
            }
            ++Frames;
        }
        if(!TestTrue(TEXT("real Yure changes final palette versus all enabled"),Changed>0&&MaxDelta>1.e-4)) return false;++Positive;
        if(!Select(Session,NormalRow,true)||!TestEqual(TEXT("ordinary Studio retains previous breast policy"),Session.StudioYureFlag(0),0)) return false;
        if(!Select(Session,FallbackRow,true)) return false;for(int32 I=0;I<22;++I) if(!TestEqual(TEXT("missing Yure resets defaults"),Session.StudioYureFlag(I),1)) return false;
        FString Key;if(!Catalog->ResolveStudioAnimation(HRow,Session.ClipNamesForSource(Catalog->StudioSource(HRow)),Key,Error)) return false;
        if(!TestFalse(TEXT("invalid resource is atomic"),Session.SetStudioAnimation(Key,true,.37f,.61f,false,.43f,TEXT("../bad.unity3d"),TEXT("asset")))) return false;
        if(!TestEqual(TEXT("failed selection retains policy"),Session.StudioYureFlag(0),1)) return false;++Atomic;
        AddInfo(FString::Printf(TEXT("StudioYure card=%s frames=90 changed=%d max_delta=%f retained_normal=1 fallback=1 invalid_atomic=1 cold_eye_look=1"),Card,Changed,MaxDelta));
    }
    return TestEqual(TEXT("five-card Yure frames"),Frames,450)&&TestEqual(TEXT("positive controls"),Positive,5)&&TestEqual(TEXT("atomic controls"),Atomic,5);
}
#endif
