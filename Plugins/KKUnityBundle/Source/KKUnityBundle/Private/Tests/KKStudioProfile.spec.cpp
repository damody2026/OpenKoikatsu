#include "KKAnimationCatalog.h"
#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKStudioProfileTest,"OpenKoikatsu.AnimationCatalog.StudioSourceInitialization",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKStudioProfileTest::RunTest(const FString& Parameters)
{
    UKKAnimationCatalog* Catalog=UKKAnimationCatalog::LoadDefaultCatalog();if(!TestNotNull(TEXT("persistent source catalog"),Catalog)) return false;
    const FString OldSkip=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NODB")),OldExternal=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
    struct FRestore{FString Skip,External;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),Skip.IsEmpty()?nullptr:*Skip);FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),External.IsEmpty()?nullptr:*External);}} Restore{OldSkip,OldExternal};
    int32 Frames=0,Positive=0,Atomic=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),nullptr);FOkSession Session;FString Error;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)) {AddError(Error);return false;}
        FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),TEXT("1"));
        auto Snapshot=[&Session](){TArray<FTransform> Out;for(int32 I=0;I<Session.SkelBoneCount();++I){OkBoneView V{};if(!Session.SkelBoneAt(I,V)) return TArray<FTransform>();
            Out.Add(FTransform(FQuat(V.pose_rot[0],V.pose_rot[1],V.pose_rot[2],V.pose_rot[3]),FVector(V.pose_pos[0],V.pose_pos[1],V.pose_pos[2]),FVector(V.pose_scl[0],V.pose_scl[1],V.pose_scl[2])));}return Out;};
        for(const FString& Category:{FString(TEXT("3")),FString(TEXT("128")),FString(TEXT("183"))}) {
            FName Row=NAME_None;
            for(FName Candidate:Catalog->FindRows(TEXT("studio_catalog"),TEXT("category"),Category)) {
                if(Catalog->GetCell(TEXT("studio_catalog"),Candidate,TEXT("origin"))==TEXT("base")&&Catalog->GetCell(TEXT("studio_catalog"),Candidate,TEXT("id"))==TEXT("0")) {Row=Candidate;break;}
            }
            if(!TestFalse(TEXT("source row exists"),Row.IsNone())) return false;
            bool IsH,IsMotion;int32 Layer;if(!Catalog->StudioSettings(Row,IsH,Layer,IsMotion)) return false;
            FString Key;if(!Catalog->ResolveStudioAnimation(Row,Session.ClipNamesForSource(Catalog->StudioSource(Row)),Key,Error)) {AddError(Error);return false;}
            if(!TestTrue(TEXT("source-selected layer"),Key.Contains(FString::Printf(TEXT("[Studio layer %d]"),Layer)))) return false;
            if(!Session.SetStudioAnimation(Key,IsH,0.f,0.f,IsMotion,0.f)||!Session.ApplyAnimFrame(.23f,0.f)) return false;
            const auto Baseline=Snapshot();int32 Changed=0;
            for(int32 Option=0;Option<3;++Option) {
                const float Height=Option==0?0.f:(Option==1?.37f:.81f),Breast=Option==0?0.f:(Option==1?.61f:.19f),Motion=Option==0?0.f:(Option==1?.43f:.77f);
                if(!Session.SetStudioAnimation(Key,IsH,Height,Breast,IsMotion,Motion)) return false;
                TMap<FString,float> Values;
                for(int32 I=0;I<Session.AnimationFloatCount();++I){FString Name;float Value,Min,Max;if(Session.AnimationFloatAt(I,Name,Value,Min,Max)) Values.Add(Name,Value);}
                const FString HeightName=Values.Contains(TEXT("height1"))?TEXT("height1"):TEXT("height");
                if(Values.Contains(HeightName)&&!TestTrue(TEXT("H options overwrite card seed; normal retains card height"),
                    FMath::IsNearlyEqual(Values[HeightName],IsH?Height:Session.ShapeBody()[0],1.e-6f))) return false;
                for(float Time:{0.f,.23f,.71f}) {
                    if(!Session.ApplyAnimFrame(Time,0.f)) return false;const auto Pose=Snapshot();
                    if(!TestEqual(TEXT("complete GPU palette"),Pose.Num(),Baseline.Num())) return false;
                    for(int32 I=0;I<Pose.Num();++I) {if(!TestFalse(TEXT("finite source pose"),Pose[I].ContainsNaN())) return false;
                        if(Option==1&&Time==.23f) Changed+=!Pose[I].Equals(Baseline[I],1.e-5);}
                    ++Frames;
                }
            }
            if(IsH){if(!TestTrue(TEXT("saved Studio options reach actual palette"),Changed>0)) return false;++Positive;}
            const float Duration=Session.AnimDuration();const auto Pose=Snapshot();
            for(const FString& Bad:{Key+TEXT("_missing"),FString(TEXT("states:studio@BAD|missing"))}) {
                if(!TestFalse(TEXT("missing/ambiguous graph rejects"),Session.SetStudioAnimation(Bad,IsH,.37f,.61f,IsMotion,.43f))) return false;++Atomic;
            }
            if(!TestFalse(TEXT("invalid option rejects"),Session.SetStudioAnimation(Key,IsH,std::numeric_limits<float>::quiet_NaN(),.61f,IsMotion,.43f))) return false;++Atomic;
            if(!TestEqual(TEXT("failed initialization retains clock"),Session.AnimDuration(),Duration)) return false;
            const auto After=Snapshot();for(int32 I=0;I<Pose.Num();++I) if(!TestTrue(TEXT("failed initialization retains palette"),Pose[I].Equals(After[I],1.e-6))) return false;
            AddInfo(FString::Printf(TEXT("StudioSource card=%s category=%s layer=%d frames=9 changed_bones=%d invalid_atomic=3"),Card,*Category,Layer,Changed));
        }
    }
    return TestEqual(TEXT("five cards source frames"),Frames,135)&&TestEqual(TEXT("H source palette controls"),Positive,10)&&TestEqual(TEXT("invalid initialization controls"),Atomic,45);
}
#endif
