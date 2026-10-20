#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
#include "ok_extract.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMisc.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ProceduralMeshComponent.h"
#include "RenderingThread.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
namespace {
struct FOwnedTestWorld
{
    UWorld* World = nullptr;
    FOwnedTestWorld() {
        const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
        if (World && GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    }
    ~FOwnedTestWorld() {
        if (World) {
            World->DestroyWorld(false);
            if (GEngine) GEngine->DestroyWorldContext(World);
        }
    }
};

TArray<uint8> SessionBytes(AKKPreviewActor* Actor)
{
    TArray<uint8> Bytes;
    auto Append=[&](const void* Data,int32 Count){if(Data&&Count>0)Bytes.Append(static_cast<const uint8*>(Data),Count);};
    const auto Body=Actor->Session.ShapeBody(), Face=Actor->Session.ShapeFace();
    Append(Body.GetData(),Body.Num()*sizeof(float));Append(Face.GetData(),Face.Num()*sizeof(float));
    for(int32 I=0;I<Actor->Session.SkelBoneCount();++I){OkBoneView B{};if(Actor->Session.SkelBoneAt(I,B)){
        Append(B.pose_pos,sizeof(B.pose_pos));Append(B.pose_rot,sizeof(B.pose_rot));Append(B.pose_scl,sizeof(B.pose_scl));}}
    for(int32 I=0;I<Actor->Session.PartCount();++I){OkMeshView P{};if(!Actor->Session.PartAt(I,P))continue;
        Append(P.positions,P.n_verts*3*sizeof(float));Append(P.normals,P.n_verts*3*sizeof(float));
        Append(P.indices,P.n_idx*sizeof(uint32));Append(P.uvs,P.n_verts*2*sizeof(float));
        Append(P.png,P.png_len);Append(P.alpha_mask,P.alpha_mask_len);
        Append(P.overtex_png,P.overtex_png_len);Append(P.overtex2_png,P.overtex2_png_len);
        Append(P.overcolor,sizeof(P.overcolor));Append(P.overcolor2,sizeof(P.overcolor2));}
    if(Actor->BodySkel){for(int32 I=0;I<Actor->BodySkel->GetNumBones();++I){
        const FTransform T=Actor->BodySkel->GetBoneTransform(I);const FVector P=T.GetTranslation(),S=T.GetScale3D();const FQuat R=T.GetRotation();
        Append(&P,sizeof(P));Append(&S,sizeof(S));Append(&R,sizeof(R));}}
    return Bytes;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKPreviewTextureLifetimeTest,"OpenKoikatsu.Import.ActorResourceLifetime",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKPreviewTextureLifetimeTest::RunTest(const FString&)
{
    FOwnedTestWorld Scope;if(!TestNotNull(TEXT("world"),Scope.World))return false;
    auto A=Scope.World->SpawnActor<AKKPreviewActor>();auto B=Scope.World->SpawnActor<AKKPreviewActor>();
    if(!A||!B)return false;
    UTexture2D* First=NewObject<UTexture2D>(A),*Second=NewObject<UTexture2D>(B);
    First->AddToRoot();Second->AddToRoot();A->LoadedTextures.Add(First);B->LoadedTextures.Add(Second);
    // This editor-created actor has never begun play: Destroyed must clean up.
    TestTrue(TEXT("destroy first"),A->Destroy());
    TestFalse(TEXT("destroyed actor texture unrooted"),First->IsRooted());
    TestTrue(TEXT("other actor texture stays rooted"),Second->IsRooted());
    B->EndPlay(EEndPlayReason::RemovedFromWorld);
    TestFalse(TEXT("EndPlay unroots remaining texture"),Second->IsRooted());
    TestTrue(TEXT("Destroy after EndPlay is safe"),B->Destroy());
    // Keep a failing pre-fix test from leaking its synthetic controls.
    if(First->IsRooted())First->RemoveFromRoot();if(Second->IsRooted())Second->RemoveFromRoot();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKMultiCharacterTest,"OpenKoikatsu.Import.MultiCharacterIsolation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKMultiCharacterTest::RunTest(const FString&)
{
    const FString Game=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MULTI_GAME"));
    const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MULTI_CARD"));
    if(Game.IsEmpty()||Card.IsEmpty()){AddError(TEXT("Set explicit OK_MULTI_GAME/OK_MULTI_CARD"));return false;}
    struct FRequest{FString Card;int32 Outfit;};TArray<FRequest> Requests;
    for(int32 Outfit:{0,1,2,3,5,6})Requests.Add({Card,Outfit});
    Requests.Add({Game/TEXT("UserData/chara/male/KK_224305.png"),0});
    Requests.Add({Game/TEXT("DefaultData/chara/male/ill_male_01.png"),0});
    Requests.Add({Game/TEXT("DefaultData/chara/male/ill_male_02.png"),0});Requests.Add({Card,0});
    FOwnedTestWorld Scope;if(!TestNotNull(TEXT("world"),Scope.World))return false;
    TArray<AKKPreviewActor*> Actors;TSet<UTexture2D*> Textures;
    for(int32 I=0;I<Requests.Num();++I){
        TArray<TArray<uint8>> Before;for(auto Other:Actors)Before.Add(SessionBytes(Other));
        auto Actor=Scope.World->SpawnActor<AKKPreviewActor>();if(!Actor)return false;
        Actor->SetActorLocation(FVector(I*300.f,0,0));FString Error;
        if(!TestTrue(TEXT("open independent session"),Actor->Session.Open(Game,Requests[I].Card,Requests[I].Outfit,Error))){AddError(Error);return false;}
        if(!TestTrue(TEXT("load independent renderer"),Actor->LoadSession(Error))){AddError(Error);return false;}
        Actor->SetActorTickEnabled(false);Actors.Add(Actor);
        for(int32 J=0;J<Before.Num();++J)TestTrue(TEXT("loading another actor preserves live pose/materials"),Before[J]==SessionBytes(Actors[J]));
        for(auto Texture:Actor->LoadedTextures){TestTrue(TEXT("mutable texture belongs to only one actor"),!Textures.Contains(Texture));Textures.Add(Texture);}
        for(const auto& Part:Actor->Parts)if(Part.Comp)TestTrue(TEXT("component owner is the intended actor"),Part.Comp->GetOwner()==Actor);
    }
    FlushRenderingCommands();
    for(int32 Target=0;Target<Actors.Num();++Target){
        TArray<TArray<uint8>> Before;for(auto Other:Actors)Before.Add(SessionBytes(Other));
        auto Actor=Actors[Target];auto Body=Actor->Session.ShapeBody(),Face=Actor->Session.ShapeFace();
        Body[0]=Body[0]<.5f?.9f:.1f;Face[0]=Face[0]<.5f?.8f:.2f;Actor->ApplyShapes(Face,Body);
        TestTrue(TEXT("target shape actually changed"),Before[Target]!=SessionBytes(Actor));
        for(int32 J=0;J<Actors.Num();++J)if(J!=Target)TestTrue(TEXT("shape changes only intended actor"),Before[J]==SessionBytes(Actors[J]));
    }
    TArray<TArray<uint8>> Survivors;for(int32 I=1;I<Actors.Num();++I)Survivors.Add(SessionBytes(Actors[I]));
    const auto Released=Actors[0]->LoadedTextures;TestTrue(TEXT("destroy loaded actor"),Actors[0]->Destroy());
    for(auto Texture:Released)TestFalse(TEXT("loaded actor texture released"),Texture->IsRooted());
    for(int32 I=1;I<Actors.Num();++I){TestTrue(TEXT("surviving actor state unchanged"),Survivors[I-1]==SessionBytes(Actors[I]));
        for(auto Texture:Actors[I]->LoadedTextures)TestTrue(TEXT("survivor textures remain live"),Texture->IsRooted());}
    for(int32 I=1;I<Actors.Num();++I)Actors[I]->Destroy();
    AddInfo(TEXT("10 actors; 6 Phoebe outfits, 3 male cards, duplicate card; load/shape/destroy isolation checked"));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKFaceDrawStreamsTest,"OpenKoikatsu.Import.NativeFaceDrawStreams",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKFaceDrawStreamsTest::RunTest(const FString&)
{
    FOwnedTestWorld Scope;auto Actor=Scope.World->SpawnActor<AKKPreviewActor>();FString Error;
    const FString Game=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MULTI_GAME"));
    const FString Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MULTI_CARD"));
    if(!Actor || !TestTrue(TEXT("explicit source session"),Actor->Session.Open(Game,Card,0,Error)))return false;
    if(!TestTrue(TEXT("native renderer"),Actor->LoadSession(Error)))return false;
    Actor->SetActorTickEnabled(false);int32 Faces=0;
    for(const auto& Part:Actor->Parts)if(Part.bNativeFaceDraw) {
        ++Faces;OkMeshView View{};Actor->Session.PartAt(Part.SessionIndex,View);
        TestEqual(TEXT("UV1 count"),Part.UV4.Num(),View.n_verts);
        TestEqual(TEXT("UV2 count"),Part.UV2.Num(),View.n_verts);
        TestEqual(TEXT("UV3 count"),Part.UV3.Num(),View.n_verts);
        if(Part.Comp)if(auto Section=Part.Comp->GetProcMeshSection(0))for(int32 V=0;V<View.n_verts;++V) {
            const auto& Vertex=Section->ProcVertexBuffer[V];
            TestTrue(TEXT("native UV1 retained"),Vertex.UV1.Equals(FVector2D(View.uv2[V*2],1.f-View.uv2[V*2+1]),1e-6));
            TestTrue(TEXT("native UV2 retained"),Vertex.UV2.Equals(FVector2D(View.uv3[V*2],1.f-View.uv3[V*2+1]),1e-6));
            TestTrue(TEXT("native UV3 retained"),Vertex.UV3.Equals(FVector2D(View.uv4[V*2],1.f-View.uv4[V*2+1]),1e-6));
        }
    }
    TestEqual(TEXT("native face draw present"),Faces,1);Actor->Destroy();return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKBodyMorphTest,"OpenKoikatsu.Import.BodyMorphControls",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKBodyMorphTest::RunTest(const FString&)
{
    const FString Game=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MULTI_GAME"));
    if(Game.IsEmpty()){AddError(TEXT("Set explicit OK_MULTI_GAME"));return false;}
    FOwnedTestWorld Scope;
    for(const FString Card: {TEXT("KK_226176.png"),TEXT("Alice_keiyuu.png")}) {
        auto Actor=Scope.World->SpawnActor<AKKPreviewActor>();
        auto Other=Scope.World->SpawnActor<AKKPreviewActor>();FString Error;
        if(!Actor||!Other)return false;
        for(auto A:{Actor,Other}) {
            if(!TestTrue(TEXT("open body morph source"),A->Session.Open(Game,Game/TEXT("UserData/chara/female")/Card,0,Error)) ||
               !TestTrue(TEXT("load morph renderer"),A->LoadSession(Error))) {AddError(Error);return false;}
            A->SetActorTickEnabled(false);
        }
        int32 Part=INDEX_NONE;
        for(int32 I=0;I<Actor->Session.PartCount();++I) {
            OkMeshView View{};
            if(Actor->Session.PartAt(I,View) && View.role && FString(UTF8_TO_TCHAR(View.role))==TEXT("body") && Actor->GetPartMorphNames(I).Num()>0) {Part=I;break;}
        }
        if(!TestTrue(TEXT("body source exposes authored morph"),Part!=INDEX_NONE))return false;
        const FName Name=Actor->GetPartMorphNames(Part)[0];
        const auto* Slot=Actor->MouthSlots.FindByPredicate([Part](const AKKPreviewActor::FOkMouthSlot& S){return S.SessionPartIndex==Part;});
        const auto* OtherSlot=Other->MouthSlots.FindByPredicate([Part](const AKKPreviewActor::FOkMouthSlot& S){return S.SessionPartIndex==Part;});
        if(!TestTrue(TEXT("body has active morph renderer"),Slot&&OtherSlot&&(Slot->Comp||Slot->CpuComp)))return false;
        const bool WantCpu=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_CPU_SKIN"))==TEXT("1");
        if(!TestTrue(TEXT("requested renderer mode actually selected"),WantCpu ? Slot->CpuComp!=nullptr && Slot->Comp==nullptr : Slot->Comp!=nullptr))return false;
        if(Slot->Comp) {
            auto* Target=Slot->Comp->GetSkeletalMeshAsset()->FindMorphTarget(Name);
            if(!TestNotNull(TEXT("source morph has a registered target"),Target))return false;
            bool Nonzero=false;
            for(const auto& Vertex:Target->GetMorphLODModels()[0].Vertices)Nonzero|=!Vertex.PositionDelta.IsNearlyZero();
            TestTrue(TEXT("source target deforms actual vertices"),Nonzero);
            FlushRenderingCommands();
            const auto* RenderData=Slot->Comp->GetSkeletalMeshAsset()->GetResourceForRendering();
            TestTrue(TEXT("GPU morph batches initialized"),RenderData && RenderData->LODRenderData[0].MorphTargetVertexInfoBuffers.GetNumBatches()>0);
            TestTrue(TEXT("GPU morph RHI initialized"),RenderData && RenderData->LODRenderData[0].MorphTargetVertexInfoBuffers.IsRHIInitialized());
        }
        TArray<FVector> Closed;
        if(Slot->CpuComp) for(const auto& V:Slot->CpuComp->GetProcMeshSection(0)->ProcVertexBuffer)Closed.Add(V.Position);
        double CpuMaximum=0;
        for(float Weight:{0.f,25.f,50.f,75.f,100.f,0.f}) {
            TestTrue(TEXT("set source morph percent"),Actor->SetPartMorphWeight(Part,Name,Weight));
            Actor->RefreshSkinnedDisplay(); // Pose refresh must retain the requested body weight and geometry.
            if(Slot->Comp) {
                TestEqual(TEXT("GPU percent conversion"),Slot->Comp->GetMorphTarget(Name),Weight/100.f);
                TestEqual(TEXT("other actor unchanged"),OtherSlot->Comp->GetMorphTarget(Name),0.f);
            } else {
                const auto* Section=Slot->CpuComp->GetProcMeshSection(0);
                double Maximum=0;
                for(int32 V=0;V<Closed.Num();++V)Maximum=FMath::Max(Maximum,(Section->ProcVertexBuffer[V].Position-Closed[V]).Size());
                if(Weight==0)TestTrue(TEXT("CPU closes to original geometry"),Maximum<1e-6);
                else TestTrue(TEXT("CPU morph produces actual vertex movement"),Maximum>1e-5);
                CpuMaximum=FMath::Max(CpuMaximum,Maximum);
                TestEqual(TEXT("CPU other actor override empty"),Other->PartMorphWeights.Num(),0);
            }
        }
        TestFalse(TEXT("unknown morph rejected"),Actor->SetPartMorphWeight(Part,TEXT("missing.source.morph"),50.f));
        TestFalse(TEXT("invalid part rejected"),Actor->SetPartMorphWeight(-1,Name,50.f));
        Actor->SetPartMorphWeight(Part,Name,100.f);
        Actor->DestroyAllParts();TestEqual(TEXT("reload clears user morph state"),Actor->PartMorphWeights.Num(),0);
        Actor->Destroy();Other->Destroy();
        AddInfo(Card+FString::Printf(TEXT(": six weights, vertex payload, closing, isolation and cleanup checked; CPU maximum=%.9f cm"),CpuMaximum));
    }
    FlushRenderingCommands();return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKClothedActorScaleTest,"OpenKoikatsu.Import.ClothedActorScaleIsolation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKClothedActorScaleTest::RunTest(const FString&)
{
    FOwnedTestWorld Scope;if(!TestNotNull(TEXT("world"),Scope.World))return false;
    TArray<AKKPreviewActor*> Actors;
    for(const TCHAR* Relative:{TEXT("female/KK_226176.png"),TEXT("male/KK_224305.png")}){
        auto Actor=Scope.World->SpawnActor<AKKPreviewActor>();if(!Actor)return false;
        FString Error;const FString Game=TEXT("C:/portable/KoikatuSunshine");
        if(!Actor->Session.Open(Game,Game/TEXT("UserData/chara")/Relative,0,Error)||!Actor->LoadSession(Error)){AddError(Error);return false;}
        const bool WantCpu=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_CPU_SKIN"))==TEXT("1");
        if(!TestTrue(TEXT("requested body renderer selected"),WantCpu?Actor->BodySkel==nullptr:Actor->BodySkel!=nullptr))return false;
        Actor->SetActorTickEnabled(false);Actor->SetActorLocation(FVector(Actors.Num()*400.f,0,0));Actors.Add(Actor);
    }
    int32 Observations=0;double MaximumBoundsErrorCm=0.;
    for(int32 Target=0;Target<Actors.Num();++Target){
        auto Actor=Actors[Target];auto Other=Actors[1-Target];
        const auto Untouched=SessionBytes(Other);
        const FString Key=Target==0?TEXT("states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01"):
            TEXT("states:studio/anime/00.unity3d|male / Base Layer.mc_m_walk_00_00_01");
        if(!Actor->Session.SetAnimClip(Key)||!Actor->Session.SetAnimSceneMode(4)){AddError(TEXT("neutral native walk selection failed"));return false;}
        for(float Time:{.4f,.8f,1.2f}){
            Actor->SetActorScale3D(FVector(1.f));
            if(!Actor->Session.ApplyAnimFrame(Time,0.f))return false;
            Actor->RefreshSkinnedDisplay();
            TMap<UProceduralMeshComponent*,FBoxSphereBounds> Baseline;
            for(const auto& Part:Actor->Parts)if(Part.Comp&&Part.Role.StartsWith(TEXT("clothes_"))){
                Part.Comp->UpdateBounds();Baseline.Add(Part.Comp,Part.Comp->Bounds);
            }
            if(!TestTrue(TEXT("actual garment components loaded"),Baseline.Num()>0))return false;
            const FVector Origin=Actor->GetActorLocation();
            for(float Scale:{.6f,1.8f,1.f}){
                Actor->SetActorScale3D(FVector(Scale));Actor->RefreshSkinnedDisplay();
                for(const auto& Pair:Baseline){
                    Pair.Key->UpdateBounds();const auto Actual=Pair.Key->Bounds;
                    const FVector ExpectedOrigin=Origin+Scale*(Pair.Value.Origin-Origin);
                    const FVector ExpectedExtent=Scale*Pair.Value.BoxExtent;
                    MaximumBoundsErrorCm=FMath::Max(MaximumBoundsErrorCm,FMath::Max((Actual.Origin-ExpectedOrigin).GetAbsMax(),(Actual.BoxExtent-ExpectedExtent).GetAbsMax()));
                    TestFalse(TEXT("finite clothed component bounds"),Actual.Origin.ContainsNaN()||Actual.BoxExtent.ContainsNaN()||!FMath::IsFinite(Actual.SphereRadius));
                    TestTrue(TEXT("garment bounds follow actor scale and origin"),Actual.Origin.Equals(ExpectedOrigin,.02)&&Actual.BoxExtent.Equals(ExpectedExtent,.02));
                    ++Observations;
                }
                TestTrue(TEXT("scaling one clothed actor preserves the other pose/materials/world bones"),Untouched==SessionBytes(Other));
            }
        }
    }
    FlushRenderingCommands();
    AddInfo(FString::Printf(TEXT("Clothed actor scale observations=%d max_bounds_error_cm=%.12f two_actors=1 isolation=1 cpu=%d"),Observations,MaximumBoundsErrorCm,FPlatformMisc::GetEnvironmentVariable(TEXT("OK_CPU_SKIN"))==TEXT("1")));
    for(auto Actor:Actors)Actor->Destroy();return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKPairScaleGuardTest,"OpenKoikatsu.Pairing.DifferentActorScaleGuard",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKPairScaleGuardTest::RunTest(const FString&)
{
    FOwnedTestWorld Scope;
    if(!TestNotNull(TEXT("world"),Scope.World))return false;
    auto First=Scope.World->SpawnActor<AKKPreviewActor>();
    auto Second=Scope.World->SpawnActor<AKKPreviewActor>();
    auto Pair=Scope.World->SpawnActor<AKKPairedPlayback>();
    if(!First||!Second||!Pair)return false;
    First->SetActorScale3D(FVector(1.f));Second->SetActorScale3D(FVector(2.f));
    FString Error;
    TestFalse(TEXT("different actor scale is currently unsupported"),
        Pair->Start(First,Second,TEXT(""),TEXT(""),TEXT(""),TEXT(""),TEXT(""),Error));
    TestEqual(TEXT("rejects before selecting any animation"),Error,FString(TEXT("配對角色需要共同場景與原點")));
    AddInfo(TEXT("Different world actor scales are rejected; skeletal height parameters are a separate path."));
    Pair->Destroy();First->Destroy();Second->Destroy();return !HasAnyErrors();
}
#endif
