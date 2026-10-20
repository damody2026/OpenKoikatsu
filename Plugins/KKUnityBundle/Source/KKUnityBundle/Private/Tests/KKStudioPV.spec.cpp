#include "Misc/AutomationTest.h"
#include "KKAnimationCatalog.h"
#include "KKRustExtract.h"
#include "ok_extract.h"
#include "HAL/PlatformMisc.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKStudioPVTest,"OpenKoikatsu.AnimationCatalog.StudioPVPlayback",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKStudioPVTest::RunTest(const FString&)
{
    auto* Catalog=UKKAnimationCatalog::LoadDefaultCatalog();if(!TestNotNull(TEXT("source catalog"),Catalog)) return false;
    FName H=NAME_None,Normal=NAME_None;int32 HCount=0,NormalCount=0;
    for(FName R:Catalog->FindRows(TEXT("studio_catalog"),TEXT("id"),TEXT("0")))
        if(Catalog->GetCell(TEXT("studio_catalog"),R,TEXT("origin"))==TEXT("base")) {
            if(Catalog->GetCell(TEXT("studio_catalog"),R,TEXT("category"))==TEXT("128")&&Catalog->GetCell(TEXT("studio_catalog"),R,TEXT("group"))==TEXT("2")&&Catalog->GetCell(TEXT("studio_catalog"),R,TEXT("kind"))==TEXT("hanime")) {H=R;++HCount;}
            if(Catalog->GetCell(TEXT("studio_catalog"),R,TEXT("category"))==TEXT("3")&&Catalog->GetCell(TEXT("studio_catalog"),R,TEXT("group"))==TEXT("0")&&Catalog->GetCell(TEXT("studio_catalog"),R,TEXT("kind"))==TEXT("anime")) {Normal=R;++NormalCount;}
        }
    if(!TestEqual(TEXT("unique source H identity"),HCount,1)||!TestEqual(TEXT("unique source normal identity"),NormalCount,1)) return false;
    if(!TestEqual(TEXT("actual H disables hands only"),Catalog->StudioPVMask(H),12)||!TestEqual(TEXT("normal restores all"),Catalog->StudioPVMask(Normal),15)) return false;
    const FString OldExternal=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM"));
    struct FRestore {FString Old;~FRestore(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_UE_ANIM"),Old.IsEmpty()?nullptr:*Old);}} Restore{OldExternal};
    int32 Frames=0,Positive=0,Atomic=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")}) {
        FOkSession Session,Control;FString Error;const FString Path=FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png");
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),Path,0,Error)||!Control.Open(TEXT("C:/portable/KoikatuSunshine"),Path,0,Error)) {AddError(Error);return false;}
        Session.SkelBoneCount();Control.SkelBoneCount();
        auto Select=[&](FOkSession& S,FName Row) {bool IsH,IsMotion;int32 Layer;FString Key,Yure,Owner;
            if(!Catalog->StudioSettings(Row,IsH,Layer,IsMotion)||!Catalog->ResolveStudioAnimation(Row,S.ClipNamesForSource(Catalog->StudioSource(Row)),Key,Error)) return false;
            Catalog->StudioYureSource(Row,Yure,Owner);
            return S.SetStudioAnimation(Key,IsH,.37f,.61f,IsMotion,.43f,Yure,Owner,Catalog->StudioPVMask(Row));};
        if(!Select(Session,H)||!Select(Control,H)||!Session.ApplyAnimFrame(0.f,0.f)||!Control.ApplyAnimFrame(0.f,0.f)) return false;
        TArray<int32> Sources,Targets,ControlSources,ControlTargets;
        // Palette handles belong to a session. Join serialized slots, never
        // reuse an integer index in a separately loaded skeleton.
        for(int32 Slot=0;Slot<4;++Slot) {
            Sources.Add(Session.StudioPVBone(Slot,true));Targets.Add(Session.StudioPVBone(Slot,false));
            ControlSources.Add(Control.StudioPVBone(Slot,true));ControlTargets.Add(Control.StudioPVBone(Slot,false));
            if(!TestTrue(TEXT("both actual prefab references exported"),Sources.Last()>=0&&Targets.Last()>=0&&ControlSources.Last()>=0&&ControlTargets.Last()>=0)) return false;
            OkBoneView A{},B{};if(!Session.SkelBoneAt(Targets.Last(),A)||!Control.SkelBoneAt(ControlTargets.Last(),B)) return false;
            if(!TestEqual(TEXT("same source destination identity"),FString(UTF8_TO_TCHAR(A.name)),FString(UTF8_TO_TCHAR(B.name)))) return false;
        }
        int32 Changed=0;double Max=0;
        for(int32 Stage=0;Stage<5;++Stage) {
            const int32 Mask=(Stage==0||Stage==4)?15:12;const bool FK=Stage==2;
            if(!Session.SetStudioPV(Mask,FK)||!Control.SetStudioPV(Mask,true)) return false;
            for(int32 K=0;K<4;++K) if(!TestEqual(TEXT("per-slot gate"),Session.StudioPVFlag(K),!FK&&(Mask&(1<<K))?1:0)) return false;
            for(int32 Frame=0;Frame<12;++Frame) {
                TArray<OkBodyPose> Input,ControlInput;
                for(int32 K=0;K<4;++K){OkBodyPose P{};if(!Session.ExternalBodyPoseAt(Sources[K],P)) return false;
                    P.position[0]+=.01f*(K+1)+Frame*.001f;P.position[1]-=.023f;P.scale[0]=.91f;P.scale[1]=1.17f;P.scale[2]=.83f;Input.Add(P);P.bone=ControlSources[K];ControlInput.Add(P);}
                if(!Session.ApplyExternalBodyFrame(Frame/60.f,0.f,Input)||!Control.ApplyExternalBodyFrame(Frame/60.f,0.f,ControlInput)) return false;
                for(int32 K=0;K<4;++K){OkBoneView A{},B{};if(!Session.SkelBoneAt(Targets[K],A)||!Control.SkelBoneAt(ControlTargets[K],B)) return false;
                    double D=0;for(int32 J=0;J<3;++J){if(!TestTrue(TEXT("finite shared palette"),FMath::IsFinite(A.pose_pos[J])&&FMath::IsFinite(A.pose_scl[J]))) return false;
                        D=FMath::Max(D,double(FMath::Abs(A.pose_pos[J]-B.pose_pos[J])));D=FMath::Max(D,double(FMath::Abs(A.pose_scl[J]-B.pose_scl[J])));}
                    if(FK&&D>1.e-6) AddError(FString::Printf(TEXT("PV FK diagnostic card=%s frame=%d slot=%d index=%d A=%s B=%s posA=(%.6f,%.6f,%.6f) posB=(%.6f,%.6f,%.6f) sclA=(%.6f,%.6f,%.6f) sclB=(%.6f,%.6f,%.6f) flagsA=%d flagsB=%d"),Card,Frame,K,Targets[K],UTF8_TO_TCHAR(A.name),UTF8_TO_TCHAR(B.name),A.pose_pos[0],A.pose_pos[1],A.pose_pos[2],B.pose_pos[0],B.pose_pos[1],B.pose_pos[2],A.pose_scl[0],A.pose_scl[1],A.pose_scl[2],B.pose_scl[0],B.pose_scl[1],B.pose_scl[2],Session.StudioPVFlag(K),Control.StudioPVFlag(K)));
                    if(FK&&!TestEqual(TEXT("FK skips PV exactly"),D,0.)) return false;Changed+=D>1.e-4;Max=FMath::Max(Max,D);}
                ++Frames;
            }
        }
        if(!TestTrue(TEXT("PV affects the displayed bone palette"),Changed>0&&Max>1.e-3)) return false;++Positive;
        for(int32 Bad:{-1,16}) {if(!TestFalse(TEXT("invalid PV policy rejects"),Session.SetStudioPV(Bad,false))) return false;++Atomic;}
        if(!Select(Session,Normal)) return false;for(int32 K=0;K<4;++K) if(!TestEqual(TEXT("normal load restores PV"),Session.StudioPVFlag(K),1)) return false;
        AddInfo(FString::Printf(TEXT("StudioPV card=%s frames=60 changed=%d max_delta=%f mask=0011 FK_hold=1 normal_reset=1 invalid_atomic=2"),Card,Changed,Max));
    }
    return TestEqual(TEXT("five-card PV shared frames"),Frames,300)&&TestEqual(TEXT("positive controls"),Positive,5)&&TestEqual(TEXT("atomic cases"),Atomic,10);
}
#endif
