#include "KKAffineProceduralMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "RenderingThread.h"
#include "HAL/IConsoleManager.h"
#include "Async/ParallelFor.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKAffineProceduralTest,"OpenKoikatsu.AnimationCatalog.AffineRigidDraw",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKAffineProceduralTest::RunTest(const FString&)
{
    auto* Mesh=NewObject<UKKAffineProceduralMeshComponent>();
    const TArray<FVector> Vertices={FVector(-3,-2,-1),FVector(3,-2,1),FVector(0,4,2)};
    Mesh->CreateMeshSection(0,Vertices,{0,1,2},{},{},{},{},false);
    const FTransform Placement(FRotator(20,40,15),FVector(40,-10,30),FVector(1.3,.6,1.8));
    Mesh->SetWorldTransform(Placement);
    FMatrix Residual=FMatrix::Identity;Residual.M[0][1]=.35;Residual.M[2][0]=-.2;
    for(const FMatrix& Matrix:{Residual,FMatrix::Identity,Residual})
    {
        Mesh->SetRenderResidual(Matrix);
        const FMatrix Expected=Matrix*Placement.ToMatrixWithScale();
        TestTrue(TEXT("Render matrix retains shear and placement"),Mesh->GetRenderMatrix().Equals(Expected,1.e-6));
        const FBox Box=Mesh->CalcBounds(Placement).GetBox();
        for(int32 I=0;I<Vertices.Num();++I)
        {
            TestEqual(TEXT("Changing the affine pose never rewrites mesh vertices"),Mesh->GetProcMeshSection(0)->ProcVertexBuffer[I].Position,Vertices[I]);
            const FVector World=Expected.TransformPosition(Vertices[I]);
            TestTrue(TEXT("Affine render bounds contain every deformed vertex"),Box.ExpandBy(.0001).IsInside(World));
            TestTrue(TEXT("Surface mapping roundtrips the exact draw matrix"),FVector(Mesh->GetRenderMatrix().Inverse().TransformPosition(World)).Equals(Vertices[I],1.e-6));
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKAnimatedStreamUploadTest,"OpenKoikatsu.AnimationCatalog.StreamUpload",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKAnimatedStreamUploadTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Actor=World->SpawnActor<AActor>();
    auto* Mesh=NewObject<UKKAffineProceduralMeshComponent>(Actor);Mesh->bUseAnimatedStreams=true;
    Actor->SetRootComponent(Mesh);Mesh->RegisterComponent();
    constexpr int32 Count=13000; // Position/tangent/UV buffers cross64KiB boundaries.
    TArray<FVector> P={FVector(-1,-2,0),FVector(3,-2,0),FVector(0,4,0)},N;
    for(int32 I=3;I<Count;++I) P.Add(FVector(I*.01,1,2));
    N.Init(FVector::UpVector,Count);
    TArray<FVector2D> U0,U1,U2,U3;
    for (int32 I=0;I<Count;++I){U0.Add(FVector2D(.1,I));U1.Add(FVector2D(.2,I));U2.Add(FVector2D(.3,I));U3.Add(FVector2D(.4,I));}
    TArray<FColor> Colors;Colors.Init(FColor(10,20,30,40),Count);
    TArray<FProcMeshTangent> Tangents;Tangents.Init(FProcMeshTangent(FVector::ForwardVector,true),Count);
    Mesh->CreateMeshSection(0,P,{0,1,2},N,U0,U1,U2,U3,Colors,Tangents,false);
    World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
    TestTrue(TEXT("Initial proxy preserves four UVs, color and packed TBN"),Mesh->VerifyAnimatedRenderStreams(0,0,0));
    auto* Packing=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Stream.ParallelPacking"));const int32 OriginalPacking=Packing->GetInt();double PackingMs[2]={0.,0.};
    for(int32 Frame=0;Frame<60;++Frame){
        Packing->Set(Frame%2,ECVF_SetByCode);
        P[0].Z=Frame*.125;P.Last().Z=Frame*.375;N[0]=FVector(.2,.3,1).GetSafeNormal();
        // Double -> float before normal packing can round either side of a
        // half-integer to the same value. Preserve the stock quantization.
        const double Edge=3.5/127.+(Frame%2 ? 1.e-10 : -1.e-10);
        N.Last()=FVector(Edge,-Edge,.9);
        Tangents.Last()=FProcMeshTangent(FVector(-Edge,.8,Edge),Frame%2!=0);
        const double PackStarted=FPlatformTime::Seconds();UKKAffineProceduralMeshComponent::UpdateStreams(Mesh,0,P,N,{}, {},Tangents);PackingMs[Frame%2]+=(FPlatformTime::Seconds()-PackStarted)*1000.;
        const auto* Section=Mesh->GetProcMeshSection(0);const FBox Expected(P);
        TestTrue(TEXT("Serial and parallel packing retain exact CPU bounds"),Section->SectionLocalBox.Min==Expected.Min&&Section->SectionLocalBox.Max==Expected.Max);
        for(int32 I=0;I<Count;++I)if(Section->ProcVertexBuffer[I].Position!=P[I]||Section->ProcVertexBuffer[I].Normal!=N[I]||Section->ProcVertexBuffer[I].Tangent.TangentX!=Tangents[I].TangentX||Section->ProcVertexBuffer[I].Tangent.bFlipTangentY!=Tangents[I].bFlipTangentY){AddError(TEXT("Packed CPU vertex differs from authored input"));break;}
    }
    Packing->Set(OriginalPacking,ECVF_SetByCode);
    AddInfo(FString::Printf(TEXT("Stream packing serial_mean_ms=%.6f parallel_mean_ms=%.6f vertices=%d"),PackingMs[0]/30.,PackingMs[1]/30.,Count));
    TestTrue(TEXT("Sixty animated updates leave UV/color upload counts zero"),Mesh->VerifyAnimatedRenderStreams(0,0,0));
    TArray<UKKAffineProceduralMeshComponent*> BatchMeshes={Mesh};
    for(int32 I=0;I<3;++I){auto* Extra=NewObject<UKKAffineProceduralMeshComponent>(Actor);Extra->bUseAnimatedStreams=true;Extra->RegisterComponent();Extra->CreateMeshSection(0,P,{0,1,2},N,U0,U1,U2,U3,Colors,Tangents,false);BatchMeshes.Add(Extra);}
    World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
    auto* BatchMode=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Stream.BatchPacking"));const int32 SavedBatch=BatchMode->GetInt();double BatchMs[2]={0.,0.};
    for(int32 Frame=0;Frame<40;++Frame){const int32 Mode=Frame%2;BatchMode->Set(Mode,ECVF_SetByCode);P[0].X=Frame*.173;N.Last()=FVector(.1,-.2,.91);Tangents.Last().bFlipTangentY=Mode!=0;
        TArray<FKKAnimatedStreamRequest> Requests;for(auto* Target:BatchMeshes)Requests.Add({Target,0,&P,&N,&Tangents});
        const double Start=FPlatformTime::Seconds();UKKAffineProceduralMeshComponent::UpdateAnimatedStreamsBatch(Requests);BatchMs[Mode]+=(FPlatformTime::Seconds()-Start)*1000.;
        const FBox Expected(P);for(auto* Target:BatchMeshes){const auto& Section=*Target->GetProcMeshSection(0);TestTrue(TEXT("Batched bounds match exact authored scan"),Section.SectionLocalBox.Min==Expected.Min&&Section.SectionLocalBox.Max==Expected.Max);
            bool Exact=true;for(int32 I=0;I<Count;++I){const auto& V=Section.ProcVertexBuffer[I];Exact&=V.Position==P[I]&&V.Normal==N[I]&&V.Tangent.TangentX==Tangents[I].TangentX&&V.Tangent.bFlipTangentY==Tangents[I].bFlipTangentY;}TestTrue(TEXT("Batch retains every CPU vertex and tangent"),Exact);}
    }
    BatchMode->Set(SavedBatch,ECVF_SetByCode);
    for(auto* Target:BatchMeshes)TestTrue(TEXT("Multiple render proxies receive exact batch packets without UV/color uploads"),Target->VerifyAnimatedRenderStreams(0,0,0));
    TArray<FKKGeneratedStreamRequest> Generated;for(auto* Target:BatchMeshes)Generated.Add({Target,0});
    const double Edge=3.5/127.-1.e-10;N.Last()=FVector(Edge,-Edge,.9);Tangents.Last()=FProcMeshTangent(FVector(-Edge,.8,Edge),true);
    P.Last()=FVector(-23,47,19);
    TestTrue(TEXT("Generated batch fuses authored geometry and packing"),UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsBatch(Generated,[&](int32,int32 I,FProcMeshVertex& V){V.Position=P[I];V.Normal=N[I];V.Tangent=Tangents[I];}));
    for(auto* Target:BatchMeshes){TestTrue(TEXT("Generated GPU streams retain exact quantization, all UVs and colors"),Target->VerifyAnimatedRenderStreams(0,0,0));
        const auto& Section=*Target->GetProcMeshSection(0);const FBox Expected(P);TestTrue(TEXT("Generated bounds match authored scan"),Section.SectionLocalBox.Min==Expected.Min&&Section.SectionLocalBox.Max==Expected.Max);}
    TArray<int32> Canonical;Canonical.Init(-1,257);
    TestTrue(TEXT("Source jobs complete before stream publication"),UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsBatchWithSourceJobs(Generated,
        [&](int32,int32 I,FProcMeshVertex& V){V.Position=P[I];V.Normal=N[I];V.Tangent=Tangents[I];},Canonical.Num(),[&](int32 I){Canonical[I]=I*17;}));
    bool SourcesComplete=true;for(int32 I=0;I<Canonical.Num();++I)SourcesComplete&=Canonical[I]==I*17;
    TestTrue(TEXT("Every independent canonical source job completed"),SourcesComplete);
    for(auto* Target:BatchMeshes)TestTrue(TEXT("Combined source jobs preserve exact GPU streams"),Target->VerifyAnimatedRenderStreams(0,0,0));
    for(int32 Mode=0;Mode<2;++Mode){
        BatchMode->Set(Mode,ECVF_SetByCode);
        P[0].Z+=3.25;P.Last().X-=1.75;N[1]=FVector(.23,-.74,.31);
        TArray<int32> SourceDone;SourceDone.Init(0,13);
        TestTrue(TEXT("Source ranges generate all sections after their own source preparation"),UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsBySourceRanges(Generated,13,
            [&](int32 Job,FKKGeneratedStreamRange Emit){SourceDone[Job]=1;const int32 Begin=Count*Job/13,End=Count*(Job+1)/13;for(int32 W=0;W<Generated.Num();++W)Emit(W,Begin,End);},
            [&](int32,int32 I,FProcMeshVertex& V){V.Position=P[I];V.Normal=N[I];V.Tangent=Tangents[I];}));
        bool Complete=true;for(int32 Value:SourceDone)Complete&=Value==1;TestTrue(TEXT("Blocking source ranges complete every preparation job"),Complete);
        for(auto* Target:BatchMeshes){TestTrue(TEXT("Source range packets retain exact GPU quantization and static attributes"),Target->VerifyAnimatedRenderStreams(0,0,0));
            const FBox Expected(P);const auto& Section=*Target->GetProcMeshSection(0);TestTrue(TEXT("Source range bounds match full authored scan"),Section.SectionLocalBox.Min==Expected.Min&&Section.SectionLocalBox.Max==Expected.Max);}
    }
    BatchMode->Set(SavedBatch,ECVF_SetByCode);
    for(int32 Mode=0;Mode<2;++Mode){
        P[2].X+=1.25;P.Last().Z-=2.5;
        TestTrue(TEXT("External scheduler completes owned sections before publication"),UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsExternally(Generated,
            [&](FKKGeneratedSectionEmitter Emit){ParallelFor(Generated.Num(),[&](int32 W){Emit(W,[&](int32 I,FProcMeshVertex& V){V.Position=P[I];V.Normal=N[I];V.Tangent=Tangents[I];});},Mode==0?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);}));
        for(auto* Target:BatchMeshes){TestTrue(TEXT("External packets retain exact GPU quantization UV and colors"),Target->VerifyAnimatedRenderStreams(0,0,0));
            const FBox Expected(P);TestTrue(TEXT("External bounds match canonical scan"),Target->GetProcMeshSection(0)->SectionLocalBox.Min==Expected.Min&&Target->GetProcMeshSection(0)->SectionLocalBox.Max==Expected.Max);}
    }
    TestTrue(TEXT("Missing external section keeps previous streams"),UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsExternally(Generated,[&](FKKGeneratedSectionEmitter){}));
    for(auto* Target:BatchMeshes)TestTrue(TEXT("Missing external section retains valid GPU packet"),Target->VerifyAnimatedRenderStreams(0,0,0));
    const FKKGeneratedStreamRequest Duplicate=Generated[0];Generated.Add(Duplicate);bool Called=false;
    TestFalse(TEXT("Duplicate generated targets reject before mutation"),UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsBatch(Generated,[&](int32,int32,FProcMeshVertex&){Called=true;}));
    TestFalse(TEXT("Rejected batch never invokes generator"),Called);
    bool SourceCalled=false;
    TestFalse(TEXT("Rejected combined batch has no partial source updates"),UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsBatchWithSourceJobs(Generated,
        [&](int32,int32,FProcMeshVertex&){Called=true;},1,[&](int32){SourceCalled=true;}));
    TestFalse(TEXT("Rejected combined batch never invokes either callback"),Called||SourceCalled);
    bool RangeCalled=false;
    TestFalse(TEXT("Duplicate source range requests reject before source preparation"),UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsBySourceRanges(Generated,1,
        [&](int32,FKKGeneratedStreamRange){RangeCalled=true;},[&](int32,int32,FProcMeshVertex&){RangeCalled=true;}));
    TestFalse(TEXT("Rejected range batch never invokes either callback"),RangeCalled);
    bool ExternalCalled=false;
    TestFalse(TEXT("Duplicate external requests reject before scheduling"),UKKAffineProceduralMeshComponent::UpdateGeneratedStreamsExternally(Generated,[&](FKKGeneratedSectionEmitter){ExternalCalled=true;}));
    TestFalse(TEXT("Rejected external batch never runs native scheduler"),ExternalCalled);
    AddInfo(FString::Printf(TEXT("Stream batch serial_mean_ms=%.6f parallel_mean_ms=%.6f vertices=%d sections=%d"),BatchMs[0]/20.,BatchMs[1]/20.,Count*BatchMeshes.Num(),BatchMeshes.Num()));
    U3.Last()=FVector2D(.875,.625);Colors.Last()=FColor(200,100,50,128);
    UKKAffineProceduralMeshComponent::UpdateStreams(Mesh,0,P,N,U0,U1,U2,U3,Colors,Tangents);
    // Queue a second unchanged packet before the renderer consumes the first.
    P[2].Y+=2;
    UKKAffineProceduralMeshComponent::UpdateStreams(Mesh,0,P,N,U0,U1,U2,U3,Colors,Tangents);
    TestTrue(TEXT("Authored UV3/color changes upload once and survive next queued packet"),Mesh->VerifyAnimatedRenderStreams(0,1,1));
    const FBox ReferenceBox(P);
    const FBox ActualBox=Mesh->GetProcMeshSection(0)->SectionLocalBox;
    TestTrue(TEXT("Fused section bounds exactly match the separate stock position scan"),
        ActualBox.IsValid==ReferenceBox.IsValid&&ActualBox.Min==ReferenceBox.Min&&ActualBox.Max==ReferenceBox.Max);
    UKKAffineProceduralMeshComponent::SetStreamSectionVisible(Mesh,0,false);
    TestFalse(TEXT("Owned visibility API retains game-thread state"),Mesh->IsMeshSectionVisible(0));
    UKKAffineProceduralMeshComponent::SetStreamSectionVisible(Mesh,0,true);
    const FBox Box=Mesh->CalcBounds(FTransform::Identity).GetBox();
    for(const auto& V:P)TestTrue(TEXT("Bounds follow animated vertices"),Box.ExpandBy(.001).IsInside(V));
    // Coating-style envelopes must change only culling/publication, never the
    // current CPU geometry or GPU vertex/tangent payload.
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->bUseConservativeAnimatedBounds=true;
    UKKAffineProceduralMeshComponent::UpdateStreams(Mesh,0,P,N,{}, {},Tangents);
    World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
    const FVector Center=FBox(P).GetCenter();for(auto& V:P)V=(V+Center)*.5;
    UKKAffineProceduralMeshComponent::UpdateStreams(Mesh,0,P,N,{}, {},Tangents);
    TestFalse(TEXT("Contained CPU animation retains render primitive commands"),Mesh->IsRenderTransformDirty());
    const FBox Tight(P);const auto& CPUBox=Mesh->GetProcMeshSection(0)->SectionLocalBox;
    TestTrue(TEXT("Conservative culling retains exact current CPU section bounds"),Tight.Min==CPUBox.Min&&Tight.Max==CPUBox.Max);
    TestTrue(TEXT("Contained CPU pose still publishes every actual render vertex"),Mesh->VerifyAnimatedRenderStreams(0,1,1));
    BatchMode->Set(2,ECVF_SetByCode);P[1]=(P[1]+Center)*.5;
    TArray<FKKAnimatedStreamRequest> EnvelopeBatch={{Mesh,0,&P,&N,&Tangents}};
    UKKAffineProceduralMeshComponent::UpdateAnimatedStreamsBatch(EnvelopeBatch);
    TestFalse(TEXT("Contained parallel CPU batch retains render primitive commands"),Mesh->IsRenderTransformDirty());
    TestTrue(TEXT("Contained CPU batch preserves actual GPU data"),Mesh->VerifyAnimatedRenderStreams(0,1,1));
    P.Last().X+=1000;UKKAffineProceduralMeshComponent::UpdateAnimatedStreamsBatch(EnvelopeBatch);
    TestTrue(TEXT("Envelope expansion updates primitive bounds"),Mesh->IsRenderTransformDirty());
    TestTrue(TEXT("Expanding CPU batch publishes new GPU geometry"),Mesh->VerifyAnimatedRenderStreams(0,1,1));
    BatchMode->Set(SavedBatch,ECVF_SetByCode);
    const FTransform Scaled(FRotator(20,40,15),FVector(40,-10,30),FVector(-1.3,.6,1.8));
    FMatrix Shear=FMatrix::Identity;Shear.M[0][1]=.35;Mesh->SetRenderResidual(Shear);
    const FBox Envelope=Mesh->CalcBounds(Scaled).GetBox();
    for(const auto& V:P)TestTrue(TEXT("Expanded culling bounds contain negative scale and affine residual"),Envelope.ExpandBy(.001).IsInside(FVector((Shear*Scaled.ToMatrixWithScale()).TransformPosition(V))));
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
