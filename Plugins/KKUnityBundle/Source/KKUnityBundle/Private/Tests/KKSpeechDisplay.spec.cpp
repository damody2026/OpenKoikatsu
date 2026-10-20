#include "KKPreviewActor.h"
#include "KKSpeech.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "RenderingThread.h"
#include "HAL/PlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
namespace {
struct FSpeechDisplayWorld {
    UWorld* World=nullptr;
    FSpeechDisplayWorld(){const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
        if(World&&GEngine)GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);}
    ~FSpeechDisplayWorld(){if(World){World->DestroyWorld(false);if(GEngine)GEngine->DestroyWorldContext(World);}}
};
struct FSpeechDisplaySnapshot {TArray<FVector> Positions;TArray<float> Weights;TArray<bool> Visible;};
FSpeechDisplaySnapshot Capture(AKKPreviewActor* Actor){
    FSpeechDisplaySnapshot State;
    for(const auto& Slot:Actor->MouthSlots){
        if(Slot.Patterns.IsEmpty())continue;
        if(Slot.CpuComp){State.Visible.Add(Slot.CpuComp->IsVisible());
            for(int32 S=0;S<Slot.CpuComp->GetNumSections();++S)if(const auto* Section=Slot.CpuComp->GetProcMeshSection(S))
                for(const auto& Vertex:Section->ProcVertexBuffer)State.Positions.Add(Vertex.Position);}
        if(Slot.Comp){State.Visible.Add(Slot.Comp->IsVisible());
            for(FName Name:Actor->GetPartMorphNames(Slot.SessionPartIndex))State.Weights.Add(Slot.Comp->GetMorphTarget(Name));}
    }
    return State;
}
bool Compare(FAutomationTestBase* Test,const FSpeechDisplaySnapshot& Actual,const FSpeechDisplaySnapshot& Expected,double& MaxPosition,float& MaxWeight){
    if(Actual.Positions.Num()!=Expected.Positions.Num()||Actual.Weights.Num()!=Expected.Weights.Num()||Actual.Visible!=Expected.Visible){Test->AddError(TEXT("mouth renderer snapshot topology/visibility mismatch"));return false;}
    for(int32 I=0;I<Actual.Positions.Num();++I){const double Error=(Actual.Positions[I]-Expected.Positions[I]).Size();
        if(!FMath::IsFinite(Error)){Test->AddError(TEXT("nonfinite displayed mouth vertex"));return false;}MaxPosition=FMath::Max(MaxPosition,Error);}
    for(int32 I=0;I<Actual.Weights.Num();++I){const float Error=FMath::Abs(Actual.Weights[I]-Expected.Weights[I]);
        if(!FMath::IsFinite(Error)){Test->AddError(TEXT("nonfinite displayed morph weight"));return false;}MaxWeight=FMath::Max(MaxWeight,Error);}
    return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSpeechRenderedMouth,"OpenKoikatsu.Speech.RenderedMouth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSpeechRenderedMouth::RunTest(const FString&){
    const bool Cpu=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_CPU_SKIN"))==TEXT("1");
    const FString Game=TEXT("C:/portable/KoikatuSunshine"),Voice=Game/TEXT("voice_extract");FString Error;
    TSharedPtr<const FKKSpeechPcm,ESPMode::ThreadSafe> Pcm;
    if(!KKSpeech::DecodeOgg(Voice/TEXT("audio/c32/adv/sun_tk_32_000.ogg"),Pcm,Error)){AddError(Error);return false;}
    FSpeechDisplayWorld Scope;if(!TestNotNull(TEXT("world"),Scope.World))return false;
    auto* A=Scope.World->SpawnActor<AKKPreviewActor>();auto* B=Scope.World->SpawnActor<AKKPreviewActor>();
    if(!A||!B)return false;
    for(auto* Actor:{A,B}){
        if(!Actor->Session.Open(Game,Game/TEXT("UserData/chara/female/KK_226176.png"),0,Error)||!Actor->LoadSession(Error)){AddError(Error);return false;}
        Actor->SetActorTickEnabled(false);Actor->SetAutomaticBlink(false);
        Actor->SetFaceControl(1,0,1.f,false);Actor->SetFaceControl(2,0,1.f,false);Actor->SetMouthOpen(.2f);
    }
    int32 Slots=0,GpuSlots=0,CpuSlots=0,GpuBodies=0;bool NonzeroGpuDelta=false;
    for(const auto& Slot:A->MouthSlots){OkMeshView View{};
        if(Slot.Comp&&A->Session.PartAt(Slot.SessionPartIndex,View)&&View.role&&FString(UTF8_TO_TCHAR(View.role))==TEXT("body")&&Slot.Comp->IsVisible())++GpuBodies;
    }
    TestTrue(TEXT("actual body renderer mode"),Cpu?GpuBodies==0:GpuBodies>0);
    for(const auto& Slot:A->MouthSlots)if(!Slot.Patterns.IsEmpty()){
        ++Slots;OkMeshView View{};
        if(!A->Session.PartAt(Slot.SessionPartIndex,View)){AddError(TEXT("missing mouth source part"));return false;}
        const bool Eligible=View.rest_positions&&View.weights&&View.bone_idx&&(View.skel_kind==1||View.skel_kind==2||View.skel_kind==3);
        const bool WantGpu=!Cpu&&Eligible;
        if(!TestTrue(TEXT("mouth renderer agrees with source skin eligibility"),WantGpu?Slot.Comp!=nullptr:Slot.CpuComp&& !Slot.Comp))return false;
        CpuSlots+=Slot.CpuComp?1:0;GpuSlots+=Slot.Comp?1:0;
        AddInfo(FString::Printf(TEXT("mouth source part=%d kind=%d eligible=%d gpu=%d cpu=%d"),Slot.SessionPartIndex,View.skel_kind,Eligible,Slot.Comp!=nullptr,Slot.CpuComp!=nullptr));
        if(Slot.Comp){
            for(const auto& Pair:Slot.Patterns)for(FName Name:{Pair.Key,Pair.Value})
                if(auto* Morph=Slot.Comp->GetSkeletalMeshAsset()->FindMorphTarget(Name))
                    for(const auto& Model:Morph->GetMorphLODModels())for(const auto& Vertex:Model.Vertices)NonzeroGpuDelta|=!Vertex.PositionDelta.IsNearlyZero();
            FlushRenderingCommands();const auto* Render=Slot.Comp->GetSkeletalMeshAsset()->GetResourceForRendering();
            TestTrue(TEXT("GPU mouth morph RHI present"),Render&&Render->LODRenderData[0].MorphTargetVertexInfoBuffers.IsRHIInitialized());
        }
    }
    if(!TestTrue(TEXT("mouth slots nonempty"),Slots>0))return false;
    if(GpuSlots>0)TestTrue(TEXT("GPU mouth source deltas nonzero"),NonzeroGpuDelta);
    const auto Other=Capture(B);double MaxPosition=0,IsolatedPosition=0;float MaxWeight=0,IsolatedWeight=0;
    double Movement=0;float WeightMovement=0;int32 Steps=0;int64 Observations=0;
    auto* FrameBatch=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Face.FrameBatch"));const int32 SavedBatch=FrameBatch->GetInt();ON_SCOPE_EXIT{FrameBatch->Set(SavedBatch,ECVF_SetByCode);};
    auto* PaletteMode=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Face.PreparedPalette"));const int32 SavedPalette=PaletteMode->GetInt();ON_SCOPE_EXIT{PaletteMode->Set(SavedPalette,ECVF_SetByCode);};
    double BatchPosition=0;float BatchWeight=0;
    TArray<int32> Patterns;for(int32 Pattern=0;Pattern<A->GetMouthPatternCount();++Pattern)Patterns.Add(Pattern);
    if(!TestTrue(TEXT("Every available mouth pattern participates in rendered verification"),Patterns.Num()>0))return false;
    for(int32 Pattern:Patterns){
        A->StopSpeech();A->SetMouthPattern(Pattern,false);A->SetMouthOpen(.2f);const auto Rest=Capture(A);
        auto Wave=NewObject<UKKSpeechWave>(A);Wave->Init(Pcm,48000);auto Generator=KKSpeech::MakeGenerator(Pcm,Wave->Meter,0.f,48000);
        A->SpeechWave=Wave;float Previous=A->SpeechPrevious;
        TArray<float> Buffer;Buffer.SetNumZeroed(8192*Pcm->Channels);
        while(!Generator->IsFinished()){
            Generator->OnGenerateAudio(Buffer.GetData(),Buffer.Num());
            const float ExpectedRate=KKSpeech::MouthRate(Wave->Meter->Rms.Load(),Previous);
            // Existing manual renderer is the wiring reference; NativeWeights separately
            // verifies its authored FBS arithmetic against the game dump.
            A->bSpeechActive=false;A->SetMouthOpen(ExpectedRate);const auto Reference=Capture(A);
            A->SetMouthOpen(.2f);A->bSpeechActive=true;
            const float HeldPrevious=A->SpeechPrevious;FrameBatch->Set(0,ECVF_SetByCode);PaletteMode->Set(0,ECVF_SetByCode);A->Tick(0.f);const auto Unbatched=Capture(A);
            A->SpeechPrevious=HeldPrevious;FrameBatch->Set(1,ECVF_SetByCode);PaletteMode->Set(1,ECVF_SetByCode);A->Tick(0.f);
            if(!Compare(this,Capture(A),Unbatched,BatchPosition,BatchWeight))return false;
            A->RefreshSkinnedDisplay();const auto Actual=Capture(A);
            if(!Compare(this,Actual,Reference,MaxPosition,MaxWeight)||!Compare(this,Capture(B),Other,IsolatedPosition,IsolatedWeight))return false;
            for(int32 I=0;I<Actual.Positions.Num();++I)Movement=FMath::Max(Movement,(Actual.Positions[I]-Rest.Positions[I]).Size());
            for(int32 I=0;I<Actual.Weights.Num();++I)WeightMovement=FMath::Max(WeightMovement,FMath::Abs(Actual.Weights[I]-Rest.Weights[I]));
            Observations+=Actual.Positions.Num()+Actual.Weights.Num();++Steps;
        }
        A->StopSpeech();if(!Compare(this,Capture(A),Rest,MaxPosition,MaxWeight))return false;
    }
    TestTrue(TEXT("manual/voice rendered equivalence"),MaxPosition<1e-6&&MaxWeight<1e-6);
    TestTrue(TEXT("Batched tick preserves displayed vertices and weights for every voice mouth"),BatchPosition==0.&&BatchWeight==0.f);
    TestTrue(TEXT("other rendered actor unchanged"),IsolatedPosition==0.&&IsolatedWeight==0.f);
    TestTrue(TEXT("actual mouth movement positive control"),CpuSlots>0?Movement>1e-5:WeightMovement>.01f);
    AddInfo(FString::Printf(TEXT("Rendered speech cpu=%d patterns=%d steps=%d slots=%d cpu_slots=%d gpu_slots=%d gpu_bodies=%d observations=%lld max_position_cm=%.12f max_weight=%.9f movement_cm=%.9f weight_movement=%.9f isolation=1"),Cpu,Patterns.Num(),Steps,Slots,CpuSlots,GpuSlots,GpuBodies,Observations,MaxPosition,MaxWeight,Movement,WeightMovement));
    A->Destroy();B->Destroy();FlushRenderingCommands();return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKNativeStreamSources,"OpenKoikatsu.Animation.StreamSources",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKNativeStreamSources::RunTest(const FString&){
    FSpeechDisplayWorld Scope;if(!Scope.World)return false;auto* Actor=Scope.World->SpawnActor<AKKPreviewActor>();if(!Actor)return false;
    const FString Game=TEXT("C:/portable/KoikatuSunshine");FString Error;
    if(!Actor->Session.Open(Game,Game/TEXT("UserData/chara/female/[Card] March_7th_keiyuu.png"),0,Error)||!Actor->LoadSession(Error)){AddError(Error);return false;}
    Actor->SetActorTickEnabled(false);
    struct FSnapshot {UProceduralMeshComponent* Mesh;TArray<FProcMeshSection> Sections;};TArray<FSnapshot> Saved;
    auto* Mode=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Animation.SourceConversion"));const int32 Previous=Mode->GetInt();ON_SCOPE_EXIT{Mode->Set(Previous,ECVF_SetByCode);};
    int64 Observations=0;double Ms[2]={0.,0.};
    for(int32 Pass=0;Pass<2;++Pass){
        for(auto& Part:Actor->Parts){Part.RuntimeSourceGeometry.Reset();if(!Part.BaseVertices.IsEmpty())Part.BaseVertices[0].X+=1.;}
        Mode->Set(Pass?2:0,ECVF_SetByCode);const double Start=FPlatformTime::Seconds();Actor->RefreshRuntimeAccessories();Ms[Pass]=(FPlatformTime::Seconds()-Start)*1000.;
        if(!Pass){for(const auto& Part:Actor->Parts)if(Part.Comp){FSnapshot Snapshot;Snapshot.Mesh=Part.Comp;for(int32 S=0;S<Part.Comp->GetNumSections();++S)if(const auto* Section=Part.Comp->GetProcMeshSection(S))Snapshot.Sections.Add(*Section);Saved.Add(MoveTemp(Snapshot));}}
        else for(const auto& Snapshot:Saved){
            TestEqual(TEXT("Source conversion retains section count"),Snapshot.Mesh->GetNumSections(),Snapshot.Sections.Num());
            for(int32 S=0;S<Snapshot.Sections.Num();++S){const auto& Expected=Snapshot.Sections[S];const auto* Actual=Snapshot.Mesh->GetProcMeshSection(S);if(!Actual||Actual->ProcVertexBuffer.Num()!=Expected.ProcVertexBuffer.Num()){AddError(TEXT("Source conversion changes topology"));return false;}
                TestTrue(TEXT("Source conversion preserves exact bounds"),Actual->SectionLocalBox.Min==Expected.SectionLocalBox.Min&&Actual->SectionLocalBox.Max==Expected.SectionLocalBox.Max);
                for(int32 I=0;I<Expected.ProcVertexBuffer.Num();++I){const auto& A=Actual->ProcVertexBuffer[I];const auto& B=Expected.ProcVertexBuffer[I];if(A.Position!=B.Position||A.Normal!=B.Normal||A.Tangent.TangentX!=B.Tangent.TangentX||A.Tangent.bFlipTangentY!=B.Tangent.bFlipTangentY||A.UV0!=B.UV0||A.UV1!=B.UV1||A.UV2!=B.UV2||A.UV3!=B.UV3||A.Color!=B.Color){AddError(TEXT("Source conversion changes displayed vertex attributes"));return false;}++Observations;}
            }
        }
    }
    TestTrue(TEXT("Real native procedural vertices compared"),Observations>0);AddInfo(FString::Printf(TEXT("Source conversion vertices=%lld exact=1 serial_ms=%.6f parallel_ms=%.6f"),Observations,Ms[0],Ms[1]));Actor->Destroy();FlushRenderingCommands();return !HasAnyErrors();
}
#endif
