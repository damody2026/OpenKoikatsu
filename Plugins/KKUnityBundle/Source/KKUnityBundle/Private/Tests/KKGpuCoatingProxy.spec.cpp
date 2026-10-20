#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "KKAffineProceduralMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "RenderingThread.h"
#include "Materials/Material.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKGpuCoatingProxyTest,"OpenKoikatsu.AnimationCatalog.GpuCoatingProxy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKGpuCoatingProxyTest::RunTest(const FString&){
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);auto* Actor=World->SpawnActor<AActor>();
 TArray<FVector> P={FVector(0,0,0),FVector(2,0,0),FVector(0,2,0)},N;N.Init(FVector::UpVector,3);TArray<FVector2D> UV;UV.Init(FVector2D(.2,.3),3);TArray<FColor> Colors;Colors.Init(FColor(10,20,30,40),3);TArray<FProcMeshTangent> Tangents;Tangents.Init(FProcMeshTangent(FVector::ForwardVector,false),3);
 auto Create=[&](){auto* C=NewObject<UKKAffineProceduralMeshComponent>(Actor);C->bUseAnimatedStreams=true;C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->RegisterComponent();C->CreateMeshSection(0,P,{0,1,2},N,UV,Colors,Tangents,false);C->SetMaterial(0,UMaterial::GetDefaultMaterial(MD_Surface));return C;};
 auto* Source=Create();auto* Overlay=Create();World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
 auto SourceRequest=[&](bool Install,float X){FKKGpuStreamRequest R;R.ProxyEpoch=Source->GetGpuStreamProxyEpoch();R.ExpectedGeneration=Install?0:1;R.ConservativeLocalBounds=FBox(P).ShiftBy(FVector(X,0,0));FMatrix44f M=FMatrix44f::Identity;M.M[3][0]=X;R.Palette.Add(M);if(Install)for(int32 I=0;I<3;++I){FOKGpuDeformSource S;S.Position=FVector4f(P[I].X,P[I].Y,P[I].Z,1);S.Weights.X=1;R.Source.Add(S);FOKGpuDeformDraw D;D.Binding.X=I;R.Draw.Add(D);}return R;};
 auto Check=[&](TSharedRef<FKKGpuStreamReceipt,ESPMode::ThreadSafe> R,bool Success){FlushRenderingCommands();if(Success&&!R->Error.IsEmpty())AddInfo(R->Error);TestEqual(TEXT("Coating transaction result"),R->Status.Load(),int32(Success?FKKGpuStreamReceipt::Published:FKKGpuStreamReceipt::Rejected));return R->Generation;};
 Check(Source->SubmitGpuStreams(0,SourceRequest(true,0)),true);
 auto Request=[&](bool Install,uint64 Generation){FKKGpuCoatingRequest R;R.SourceEpoch=Source->GetGpuStreamProxyEpoch();R.TargetEpoch=Overlay->GetGpuStreamProxyEpoch();R.Revision=1;R.ExpectedGeneration=Generation;R.ConservativeLocalBounds=FBox(FVector(-20,-20,-20),FVector(20,20,20));if(Install)for(uint32 I=0;I<3;++I){FOKGpuCoatingBinding B;B.Vertices=FUintVector4(I,0,1,0);B.Offset=FVector4f(0,0,.25f,0);R.Bindings.Add(B);}return R;};
 auto Expected=Overlay->GetProcMeshSection(0)->ProcVertexBuffer;uint64 Generation=0;
 for(int32 Frame=0;Frame<3;++Frame){
  // A pending batch must flush before coating consumes the current pose.
  {FKKGpuStreamBatchScope Batch;TArray<FKKGpuStreamSectionRequest> Requests;FKKGpuStreamSectionRequest Entry;Entry.Component=Source;Entry.Request=SourceRequest(false,Frame*2);Requests.Add(MoveTemp(Entry));UKKAffineProceduralMeshComponent::SubmitGpuStreamsBatch(MoveTemp(Requests));Generation=Check(Overlay->SubmitGpuCoating(0,Source,0,Request(Frame==0,Generation)),true);}
  for(int32 I=0;I<3;++I)Expected[I].Position=P[I]+FVector(Frame*2,0,.25);TestTrue(TEXT("Current source pose reaches actual overlay GPU buffers with static streams preserved"),Overlay->VerifyGpuRenderStreams(0,Expected,Generation));
 }
 auto Bad=Request(false,Generation);Bad.SourceEpoch++;Check(Overlay->SubmitGpuCoating(0,Source,0,MoveTemp(Bad)),false);
 Bad=Request(false,Generation);Bad.Revision++;Check(Overlay->SubmitGpuCoating(0,Source,0,MoveTemp(Bad)),false);
 Bad=Request(true,Generation);Bad.Bindings[0].Vertices.X=3;Check(Overlay->SubmitGpuCoating(0,Source,0,MoveTemp(Bad)),false);
 Bad=Request(false,0);Check(Overlay->SubmitGpuCoating(0,Source,0,MoveTemp(Bad)),false);
 TestTrue(TEXT("Rejected work preserves the last overlay frame"),Overlay->VerifyGpuRenderStreams(0,Expected,Generation));
 Source->MarkRenderStateDirty();Check(Overlay->SubmitGpuCoating(0,Source,0,Request(false,Generation)),false);World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
 Check(Overlay->SubmitGpuCoating(0,Source,0,Request(true,Generation)),false);
 TestTrue(TEXT("Source replacement without a published pose cannot overwrite the overlay"),Overlay->VerifyGpuRenderStreams(0,Expected,Generation));
 Check(Source->SubmitGpuStreams(0,SourceRequest(true,6)),true);
 Generation=Check(Overlay->SubmitGpuCoating(0,Source,0,Request(true,Generation)),true);
 for(int32 I=0;I<3;++I)Expected[I].Position=P[I]+FVector(6,0,.25);
 TestTrue(TEXT("Runtime bindings recover after source proxy replacement and publication"),Overlay->VerifyGpuRenderStreams(0,Expected,Generation));
 auto* Other=Create();World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
 TSharedPtr<FKKGpuStreamReceipt,ESPMode::ThreadSafe> First,Second;
 {FKKGpuCoatingBatchScope Batch;
  First=Overlay->SubmitGpuCoating(0,Source,0,Request(false,Generation));
  auto R=Request(true,0);R.TargetEpoch=Other->GetGpuStreamProxyEpoch();Second=Other->SubmitGpuCoating(0,Source,0,MoveTemp(R));
  TestEqual(TEXT("Coating receipts remain pending before shared graph flush"),First->Status.Load(),int32(FKKGpuStreamReceipt::Pending));
 }
 Generation=Check(First.ToSharedRef(),true);const uint64 OtherGeneration=Check(Second.ToSharedRef(),true);
 TestTrue(TEXT("Shared coating graph publishes both destinations exactly"),Overlay->VerifyGpuRenderStreams(0,Expected,Generation)&&Other->VerifyGpuRenderStreams(0,Expected,OtherGeneration));
 {FKKGpuCoatingBatchScope Batch;auto R=Request(false,OtherGeneration);R.TargetEpoch=Other->GetGpuStreamProxyEpoch();Second=Other->SubmitGpuCoating(0,Source,0,MoveTemp(R));Other->DestroyComponent();}
 Check(Second.ToSharedRef(),false);
 TestTrue(TEXT("Destroyed destination is rejected before deferred proxy dereference"),Overlay->VerifyGpuRenderStreams(0,Expected,Generation));
 GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}
#endif
