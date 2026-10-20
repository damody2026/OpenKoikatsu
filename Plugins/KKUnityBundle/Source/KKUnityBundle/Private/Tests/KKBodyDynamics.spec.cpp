#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
namespace
{
struct FBodyEnvironment
{
    TMap<FString,FString> Saved;
    void Set(const TCHAR* Key,const TCHAR* Value)
    {
        if(!Saved.Contains(Key)) Saved.Add(Key,FPlatformMisc::GetEnvironmentVariable(Key));
        FPlatformMisc::SetEnvironmentVar(Key,Value);
    }
    ~FBodyEnvironment()
    {
        for(const auto& Entry:Saved) FPlatformMisc::SetEnvironmentVar(*Entry.Key,Entry.Value.IsEmpty()?nullptr:*Entry.Value);
    }
};
struct FBodyPose { FVector Position; FQuat Rotation; FVector Scale; };
TMap<FString,FBodyPose> BustPose(FOkSession& Session)
{
    TMap<FString,FBodyPose> Result;
    for(int32 Index=0;Index<Session.SkelBoneCount();++Index)
    {
        OkBoneView Bone{};
        if(!Session.SkelBoneAt(Index,Bone)||Bone.layer!=1) continue;
        FString Name=UTF8_TO_TCHAR(Bone.name);
        bool IsParticle=false;
        for(const TCHAR* Side:{TEXT("L"),TEXT("R")})
            for(int32 Joint=1;Joint<=3;++Joint)
                IsParticle|=Name==FString::Printf(TEXT("cf_j_bust%02d_%s"),Joint,Side);
        if(!IsParticle) continue;
        Result.Add(Name,{FVector(Bone.pose_pos[0],Bone.pose_pos[1],Bone.pose_pos[2]),
            FQuat(Bone.pose_rot[0],Bone.pose_rot[1],Bone.pose_rot[2],Bone.pose_rot[3]),
            FVector(Bone.pose_scl[0],Bone.pose_scl[1],Bone.pose_scl[2])});
    }
    return Result;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKBodyDynamicsPlaybackTest,
    "OpenKoikatsu.AnimationCatalog.BodyDynamics",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKBodyDynamicsPlaybackTest::RunTest(const FString& Parameters)
{
    FBodyEnvironment Environment;
    Environment.Set(TEXT("OK_GPU_SKIN"),TEXT("1"));
    Environment.Set(TEXT("OK_SKELMESH"),TEXT("1"));
    Environment.Set(TEXT("OK_UE_ANIM"),nullptr);
    Environment.Set(TEXT("OK_NODB"),nullptr);
    int32 Cards=0,Frames=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")})
    {
        FOkSession Session;FString Error;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)) {AddError(Error);return false;}
        if(!TestTrue(TEXT("source Studio walk"),Session.SetAnimClip(TEXT("states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01"))) ||
            !TestTrue(TEXT("Studio body helpers"),Session.SetAnimSceneMode(4))) return false;
        const float Duration=Session.AnimDuration();
        if(!TestTrue(TEXT("actual walk duration"),Duration>2.f&&Duration<2.1f)) return false;
        double MaxTranslation=0.,MaxRotation=0.;int32 Changed=0;
        for(int32 Frame=0;Frame<180;++Frame)
        {
            const float Phase=FMath::Fmod(Frame/60.f,Duration);
            if(!Session.ApplyAnimFrame(Phase,1.f/60.f)) return false;
            const auto Dynamic=BustPose(Session);
            if(!TestEqual(TEXT("both three-joint source breast chains"),Dynamic.Num(),6)) return false;
            // Numerical negative control: resample the identical Animator phase
            // with dynamics skipped. No time is consumed and particle history
            // is untouched; the next real frame receives the same history.
            Environment.Set(TEXT("OK_NODB"),TEXT("1"));
            Session.ApplyAnimFrame(Phase,0.f);
            const auto Authored=BustPose(Session);
            Environment.Set(TEXT("OK_NODB"),nullptr);
            for(const auto& Entry:Dynamic)
            {
                const FBodyPose* Base=Authored.Find(Entry.Key);
                if(!TestNotNull(TEXT("same authored bone"),Base)) return false;
                const FBodyPose& Pose=Entry.Value;
                if(!TestTrue(TEXT("finite body simulation"),!Pose.Position.ContainsNaN()&&!Pose.Rotation.ContainsNaN()&&!Pose.Scale.ContainsNaN())) return false;
                const double Translation=FVector::Distance(Pose.Position,Base->Position);
                const double Rotation=Pose.Rotation.AngularDistance(Base->Rotation);
                MaxTranslation=FMath::Max(MaxTranslation,Translation);MaxRotation=FMath::Max(MaxRotation,Rotation);
                Changed+=Translation>1.e-5||Rotation>1.e-4;
            }
            ++Frames;
        }
        if(!TestTrue(TEXT("physics changes the source breast pose"),Changed>0&&MaxTranslation>1.e-5&&MaxRotation>1.e-4)) return false;
        AddInfo(FString::Printf(TEXT("BodyDynamics card=%s frames=180 joints=6 changed=%d max_delta_cm=%.9f max_rotation_rad=%.9f"),Card,Changed,MaxTranslation*100.,MaxRotation));
        ++Cards;
    }
    return TestEqual(TEXT("all five source cards"),Cards,5)&&TestEqual(TEXT("looping physical frames"),Frames,900);
}
#endif
