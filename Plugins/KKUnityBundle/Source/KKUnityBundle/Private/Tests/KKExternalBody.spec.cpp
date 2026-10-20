#include "KKExternalBodyFixture.h"
#include "KKRustExtract.h"
#include "KKSkeletalBuilder.h"
#include "ok_extract.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKExternalSparseTest,"OpenKoikatsu.AnimationCatalog.ExternalSparseTracks",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKExternalSparseTest::RunTest(const FString& Parameters)
{
    UAnimSequence* Sequence=OKMakeExternalBodyFixture(GetTransientPackage());
    if(!TestNotNull(TEXT("animated sparse fixture"),Sequence)) return false;
    const FReferenceSkeleton& Ref=Sequence->GetSkeleton()->GetReferenceSkeleton();
    const int32 Arm=Ref.FindBoneIndex(TEXT("upperarm_l"));
    if(!TestTrue(TEXT("remove unanimated source track"),Sequence->GetController().RemoveBoneTrack(TEXT("upperarm_l"),false))) return false;
    TArray<FTransform> Locals=Ref.GetRefBonePose();
    Locals[Arm].SetScale3D(FVector(1.2,.8,1.1));const FTransform Before=Locals[Arm];
    if(!TestTrue(TEXT("other tracks still animate"),OKRetargetUnrealPose(Locals,Ref,Sequence,.5f)>0)||
        !TestTrue(TEXT("missing track preserves shaped local pose"),Locals[Arm].Equals(Before,1.e-7))) return false;
    for(float Sign:{1.f,-1.f}) {
        const FTransform Original(FRotator(17,31,-23).Quaternion(),FVector(13.125,117.25,-9.5),FVector(Sign*1.2,.8,1.1));
        float P[3],Q[4],S[3];OKUnrealLocalToUnity(Original,P,Q,S);
        if(!TestTrue(TEXT("Unity meters, all axes, signed nonuniform scale roundtrip"),OKUnityLocalToUnreal(P,Q,S).Equals(Original,1.e-5))) return false;
    }
    TArray<FVector> Positions,Scales;TArray<FQuat> Rotations;
    const int32 Hips=Ref.FindBoneIndex(TEXT("pelvis"));
    for(int32 Frame=0;Frame<=60;++Frame) {
        Positions.Add(Ref.GetRefBonePose()[Hips].GetTranslation());
        Rotations.Add(FQuat::Identity);Scales.Add(FVector(1,0,1));
    }
    if(!TestTrue(TEXT("author invalid zero-scale track"),Sequence->GetController().SetBoneTrackKeys(TEXT("pelvis"),Positions,Rotations,Scales,false))) return false;
    Locals=Ref.GetRefBonePose();
    if(!TestEqual(TEXT("invalid track rejected before actor/history reset"),OKRetargetUnrealPose(Locals,Ref,Sequence,.5f),0)) return false;
    AddInfo(TEXT("ExternalSparse tracks=20 preserved_missing=1 coordinate_roundtrips=2 invalid_clip_rejected=1"));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKExternalBodyTest,"OpenKoikatsu.AnimationCatalog.ExternalBodyDynamics",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKExternalBodyTest::RunTest(const FString& Parameters)
{
    const FString OldSkip=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NODB"));
    struct FRestore {FString Value;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),Value.IsEmpty()?nullptr:*Value);}} Restore{OldSkip};
    FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),nullptr);
    UAnimSequence* Sequence=OKMakeExternalBodyFixture(GetTransientPackage());
    if(!TestNotNull(TEXT("authored animated fixture"),Sequence)) return false;
    int32 Frames=0,Cards=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FOkSession Session;FString Error;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)) {AddError(Error);return false;}
        USkeleton* Target=OKBuildSharedSkeleton(Session,GetTransientPackage());
        if(!TestNotNull(TEXT("target skeleton"),Target)) return false;
        TArray<int32> Joints;int32 Hips=INDEX_NONE;
        for(int32 Bone=0;Bone<Session.SkelBoneCount();++Bone) {
            OkBoneView View{};Session.SkelBoneAt(Bone,View);const FString Name=UTF8_TO_TCHAR(View.name);
            if(Name==TEXT("cf_j_hips")) Hips=Bone;
            for(const TCHAR* Side:{TEXT("L"),TEXT("R")}) for(int32 J=1;J<=3;++J)
                if(Name==FString::Printf(TEXT("cf_j_bust%02d_%s"),J,Side)) Joints.Add(Bone);
        }
        if(!TestEqual(TEXT("six particles, not reference bones"),Joints.Num(),6)||!TestTrue(TEXT("hips"),Hips!=INDEX_NONE)) return false;
        OkBodyPose HipBase{};Session.ExternalBodyPoseAt(Hips,HipBase);
        double MaxDelta=0.,MaxRotation=0.,MaxHipMotion=0.,HipVariation=0.;FVector FirstHip=FVector::ZeroVector;int32 Changed=0;
        for(int32 Frame=0;Frame<180;++Frame) {
            const float Phase=FMath::Fmod(Frame/60.f,2.f);
            TArray<OkBodyPose> Input;
            if(!TestTrue(TEXT("real UE sequence retarget"),Session.BuildUnrealBodyFrame(Target->GetReferenceSkeleton(),Sequence,Phase,Input))) return false;
            FVector Hip(HipBase.position[0],HipBase.position[1],HipBase.position[2]);
            for(const auto& P:Input) if(P.bone==Hips) Hip=FVector(P.position[0],P.position[1],P.position[2]);
            if(Frame==0) FirstHip=Hip;
            MaxHipMotion=FMath::Max(MaxHipMotion,FVector::Distance(Hip,FVector(HipBase.position[0],HipBase.position[1],HipBase.position[2])));
            HipVariation=FMath::Max(HipVariation,FVector::Distance(Hip,FirstHip));
            if(!Session.ApplyExternalBodyFrame(Phase,1.f/60.f,Input)) return false;
            TArray<OkBoneView> Dynamic;
            for(int32 Joint:Joints) {OkBoneView View{};Session.SkelBoneAt(Joint,View);Dynamic.Add(View);}
            FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),TEXT("1"));
            if(!Session.ApplyExternalBodyFrame(Phase,0.f,Input)) return false;
            FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),nullptr);
            for(int32 J=0;J<Joints.Num();++J) {
                OkBoneView Base{};Session.SkelBoneAt(Joints[J],Base);const auto& D=Dynamic[J];
                const FVector Position(D.pose_pos[0],D.pose_pos[1],D.pose_pos[2]);
                const FQuat Rotation(D.pose_rot[0],D.pose_rot[1],D.pose_rot[2],D.pose_rot[3]);
                if(!TestTrue(TEXT("finite external dynamics"),!Position.ContainsNaN()&&!Rotation.ContainsNaN())) return false;
                const double Delta=FVector::Distance(Position,FVector(Base.pose_pos[0],Base.pose_pos[1],Base.pose_pos[2]));
                const double Angle=Rotation.AngularDistance(FQuat(Base.pose_rot[0],Base.pose_rot[1],Base.pose_rot[2],Base.pose_rot[3]));
                MaxDelta=FMath::Max(MaxDelta,Delta);MaxRotation=FMath::Max(MaxRotation,Angle);Changed+=Delta>1.e-5||Angle>1.e-4;
            }
            ++Frames;
        }
        if(!TestTrue(TEXT("external physics and pelvis translation"),Changed>0&&MaxDelta>1.e-5&&MaxRotation>1.e-4&&MaxHipMotion>.001&&HipVariation>.001)) return false;
        AddInfo(FString::Printf(TEXT("ExternalBody card=%s frames=180 changed=%d max_delta_cm=%.9f max_rotation_rad=%.9f hip_motion_cm=%.9f"),Card,Changed,MaxDelta*100.,MaxRotation,MaxHipMotion*100.));++Cards;
    }
    return TestEqual(TEXT("five cards"),Cards,5)&&TestEqual(TEXT("900 external frames"),Frames,900);
}
#endif
