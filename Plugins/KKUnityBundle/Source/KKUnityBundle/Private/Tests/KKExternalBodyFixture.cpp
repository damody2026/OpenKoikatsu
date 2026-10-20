#include "KKExternalBodyFixture.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "ReferenceSkeleton.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
UAnimSequence* OKMakeExternalBodyFixture(UObject* Outer)
{
    USkeleton* Skeleton=NewObject<USkeleton>(Outer,NAME_None,RF_Transient);
    {
    FReferenceSkeletonModifier Modifier(Skeleton);
    auto Add=[&](const TCHAR* Name,int32 Parent,FVector Position) {
        Modifier.Add(FMeshBoneInfo(FName(Name),Name,Parent),FTransform(FQuat::Identity,Position),true);
        return Modifier.FindBoneIndex(FName(Name));
    };
    const int32 Root=Add(TEXT("root"),INDEX_NONE,FVector::ZeroVector);
    const int32 Hips=Add(TEXT("pelvis"),Root,FVector(0,0,90));
    const int32 Spine1=Add(TEXT("spine_01"),Hips,FVector(0,0,15));
    const int32 Spine2=Add(TEXT("spine_02"),Spine1,FVector(0,0,15));
    const int32 Spine3=Add(TEXT("spine_03"),Spine2,FVector(0,0,15));
    const int32 Neck=Add(TEXT("neck"),Spine3,FVector(0,0,10));
    Add(TEXT("head"),Neck,FVector(0,0,15));
    for(const TCHAR* Side:{TEXT("l"),TEXT("r")}) {
        const float Sign=FCString::Strcmp(Side,TEXT("l"))==0?-1.f:1.f;
        const int32 Clav=Add(*FString::Printf(TEXT("clavicle_%s"),Side),Spine3,FVector(0,Sign*12,0));
        const int32 Arm=Add(*FString::Printf(TEXT("upperarm_%s"),Side),Clav,FVector(0,Sign*16,0));
        const int32 Fore=Add(*FString::Printf(TEXT("lowerarm_%s"),Side),Arm,FVector(0,Sign*25,0));
        Add(*FString::Printf(TEXT("hand_%s"),Side),Fore,FVector(0,Sign*22,0));
        const int32 Thigh=Add(*FString::Printf(TEXT("thigh_%s"),Side),Hips,FVector(0,Sign*9,0));
        const int32 Calf=Add(*FString::Printf(TEXT("calf_%s"),Side),Thigh,FVector(0,0,-40));
        Add(*FString::Printf(TEXT("foot_%s"),Side),Calf,FVector(0,0,-40));
    }
    }
    UAnimSequence* Sequence=NewObject<UAnimSequence>(Outer,MakeUniqueObjectName(Outer,UAnimSequence::StaticClass(),TEXT("ExternalBody288Fixture")),RF_Transient);
    Sequence->SetSkeleton(Skeleton);
    IAnimationDataController& Controller=Sequence->GetController();
    Controller.OpenBracket(NSLOCTEXT("OK","ExternalFixture","External animation verification"),false);
    Controller.InitializeModel();Controller.SetFrameRate(FFrameRate(30,1),false);
    Controller.SetNumberOfFrames(FFrameNumber(60),false);
    const FReferenceSkeleton& Ref=Skeleton->GetReferenceSkeleton();
    for(int32 Bone=0;Bone<Ref.GetNum();++Bone) {
        const FString Name=Ref.GetBoneName(Bone).ToString();
        TArray<FVector> Positions,Scales;TArray<FQuat> Rotations;
        for(int32 Frame=0;Frame<=60;++Frame) {
            const float T=Frame/60.f,S=FMath::Sin(T*2.f*PI),C=FMath::Cos(T*2.f*PI);
            FVector P=Ref.GetRefBonePose()[Bone].GetTranslation();FRotator Rotation=FRotator::ZeroRotator;
            if(Name==TEXT("pelvis")) {P+=FVector(2*S,1.5f*S,4*S);Rotation=FRotator(3*S,5*C,2*S);}
            if(Name==TEXT("spine_01")) Rotation=FRotator(8*S,6*C,4*S);
            if(Name==TEXT("neck")) Rotation=FRotator(10*S,35*C,7*S);
            if(Name==TEXT("head")) Rotation=FRotator(6*C,8*S,5*S);
            if(Name.StartsWith(TEXT("upperarm"))) Rotation=FRotator(15*S,10*S,18*C);
            if(Name.StartsWith(TEXT("thigh"))) Rotation=FRotator(20*S*(Name.EndsWith(TEXT("l"))?1.f:-1.f),0,0);
            FQuat Q=Rotation.Quaternion();
            // Rest geometry remains a true T-pose. This diagnostic motion
            // lowers both arms so side cameras can inspect head attachments.
            if(Name.StartsWith(TEXT("upperarm")))
                Q=FQuat(FVector::ForwardVector,FMath::DegreesToRadians(Name.EndsWith(TEXT("l"))?75.f:-75.f))*Q;
            Positions.Add(P);Rotations.Add(Q);Scales.Add(FVector::OneVector);
        }
        Controller.AddBoneCurve(Ref.GetBoneName(Bone),false);
        Controller.SetBoneTrackKeys(Ref.GetBoneName(Bone),Positions,Rotations,Scales,false);
    }
    Controller.NotifyPopulated();Controller.CloseBracket(false);
    const auto Model=Sequence->GetDataModelInterface();
    const FTransform Quarter=Model->EvaluateBoneTrackTransform(TEXT("pelvis"),Model->GetFrameRate().AsFrameTime(.5),EAnimInterpolationType::Linear);
    UE_LOG(LogTemp,Display,TEXT("OK external fixture tracks=%d bones=%d quarter_hip_z=%.9f"),Model->GetNumBoneTracks(),Ref.GetNum(),Quarter.GetTranslation().Z);
    if(Model->GetNumBoneTracks()!=Ref.GetNum()||FMath::Abs(Quarter.GetTranslation().Z-94.)>1.e-4) return nullptr;
    return Sequence;
}
#endif
