#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "KKAffineProceduralMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "RenderingThread.h"
#include "Materials/Material.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKGpuStreamProxyTest,"OpenKoikatsu.AnimationCatalog.GpuStreamProxy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKGpuStreamProxyTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Actor=World->SpawnActor<AActor>();auto* Mesh=NewObject<UKKAffineProceduralMeshComponent>(Actor);
 Mesh->bUseAnimatedStreams=true;Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Actor->SetRootComponent(Mesh);Mesh->RegisterComponent();
 constexpr int32 Count=13000;
 TArray<FVector> P={FVector(-2,-2,0),FVector(2,-2,0),FVector(0,2,0)},N;
 for(int32 I=3;I<Count;++I)P.Add(FVector(I%3*.125,0,0));N.Init(FVector::UpVector,Count);
 TArray<FVector2D> U0,U1,U2,U3;TArray<FColor> Colors;TArray<FProcMeshTangent> Tangents;
 for(int32 I=0;I<Count;++I){U0.Add(FVector2D(.1,.2));U1.Add(FVector2D(.2,.3));U2.Add(FVector2D(.3,.4));U3.Add(FVector2D(.4,.5));Colors.Add(FColor(10,20,30,40));Tangents.Add(FProcMeshTangent(FVector::ForwardVector,true));}
 Mesh->CreateMeshSection(0,P,{0,1,2,2,1,0},N,U0,U1,U2,U3,Colors,Tangents,false);
 Mesh->SetMaterial(0,UMaterial::GetDefaultMaterial(MD_Surface));World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
 const uint64 Epoch=Mesh->GetGpuStreamProxyEpoch();const auto Original=Mesh->GetProcMeshSection(0)->ProcVertexBuffer;
 auto Request=[&](bool Install,uint64 Generation,FVector Delta){FKKGpuStreamRequest R;R.ProxyEpoch=Epoch;R.ExpectedGeneration=Generation;
  FMatrix44f M=FMatrix44f::Identity;M.M[3][0]=Delta.X;M.M[3][1]=Delta.Y;R.Palette.Add(M);R.ConservativeLocalBounds=FBox(P).ShiftBy(Delta);
  if(Install)for(int32 I=0;I<Count;++I){FOKGpuDeformSource V;V.Position=FVector4f(P[I].X,P[I].Y,P[I].Z,1);V.Tangent.W=-1;V.Weights.X=1;R.Source.Add(V);FOKGpuDeformDraw D;D.Binding.X=I;R.Draw.Add(D);}return R;
 };
 auto VerifyReceipt=[&](TSharedRef<FKKGpuStreamReceipt,ESPMode::ThreadSafe> Receipt,bool Success,uint64 Generation){FlushRenderingCommands();
  if(Success&&!Receipt->Error.IsEmpty())AddInfo(FString::Printf(TEXT("GPU receipt rejection: %s"),*Receipt->Error));
  TestEqual(TEXT("Publication status"),Receipt->Status.Load(),int32(Success?FKKGpuStreamReceipt::Published:FKKGpuStreamReceipt::Rejected));
  TestEqual(TEXT("Publication generation"),Receipt->Generation,Generation);if(Success)TestTrue(TEXT("Successful receipt has no error"),Receipt->Error.IsEmpty());else TestFalse(TEXT("Rejected receipt explains failure"),Receipt->Error.IsEmpty());
 };
 TArray<FProcMeshVertex> Expected=Original;
 for(int32 Frame=0;Frame<3;++Frame){const FVector Delta(Frame*2,Frame*.125,0);
  VerifyReceipt(Mesh->SubmitGpuStreams(0,Request(Frame==0,Frame?1:0,Delta)),true,1);
  for(int32 I=0;I<Count;++I)Expected[I].Position=P[I]+Delta;
  TestTrue(TEXT("Actual LocalVertexFactory buffers preserve GPU output, material, indices, four UVs and colors"),Mesh->VerifyGpuRenderStreams(0,Expected,1));
  TestTrue(TEXT("GPU publication does not regenerate CPU query vertices"),Mesh->GetProcMeshSection(0)->ProcVertexBuffer[0].Position==P[0]);
  const auto Bounds=Mesh->CalcBounds(FTransform::Identity).GetBox();for(int32 I=0;I<3;++I)TestTrue(TEXT("Conservative bounds include published pose"),Bounds.ExpandBy(.001).IsInside(Expected[I].Position));
 }
 // A new GPU pose inside the existing conservative envelope still publishes
 // every vertex, but must not invalidate primitive transforms/draw commands.
 World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
 auto Contained=Mesh->SubmitGpuStreams(0,Request(false,1,FVector(2,.125,0)));
 TestFalse(TEXT("Contained GPU pose does not dirty render transform"),Mesh->IsRenderTransformDirty());
 VerifyReceipt(Contained,true,1);for(int32 I=0;I<Count;++I)Expected[I].Position=P[I]+FVector(2,.125,0);
 TestTrue(TEXT("Contained pose still updates actual GPU vertex streams"),Mesh->VerifyGpuRenderStreams(0,Expected,1));
 VerifyReceipt(Mesh->SubmitGpuStreams(0,Request(false,1,FVector(4,.25,0))),true,1);
 for(int32 I=0;I<Count;++I)Expected[I].Position=P[I]+FVector(4,.25,0);
 auto Invalid=Request(false,1,FVector(100,0,0));Invalid.Palette[0].M[0][0]=NAN;VerifyReceipt(Mesh->SubmitGpuStreams(0,MoveTemp(Invalid)),false,1);
 VerifyReceipt(Mesh->SubmitGpuStreams(0,Request(false,0,FVector(200,0,0))),false,1);
 TestTrue(TEXT("Invalid/stale work retains last GPU frame and static streams"),Mesh->VerifyGpuRenderStreams(0,Expected,1));
 auto StaleEpoch=Request(false,1,FVector::ZeroVector);StaleEpoch.ProxyEpoch=Epoch+1;VerifyReceipt(Mesh->SubmitGpuStreams(0,MoveTemp(StaleEpoch)),false,1);
 TestTrue(TEXT("Old GPU pose remains after epoch rejection"),Mesh->VerifyGpuRenderStreams(0,Expected,1));
 auto* Other=NewObject<UKKAffineProceduralMeshComponent>(Actor);Other->bUseAnimatedStreams=true;Other->SetCollisionEnabled(ECollisionEnabled::NoCollision);Other->RegisterComponent();
 Other->CreateMeshSection(0,P,{0,1,2},N,U0,U1,U2,U3,Colors,Tangents,false);Other->SetMaterial(0,UMaterial::GetDefaultMaterial(MD_Surface));World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
 TestTrue(TEXT("Proxy epochs are unique across different character components"),Other->GetGpuStreamProxyEpoch()!=Epoch);
 VerifyReceipt(Other->SubmitGpuStreams(0,Request(true,0,FVector::ZeroVector)),false,0);
 // One graph spans independent components; a duplicate must not update the
 // same resident object twice before graph execution.
 auto MainReceipt=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>(),OtherReceipt=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>(),DuplicateReceipt=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>();
 TArray<FKKGpuStreamSectionRequest> Batch;
 Batch.Add({Mesh,0,Request(false,1,FVector(4,.25,0)),MainReceipt});
 auto OtherRequest=Request(true,0,FVector(2,.125,0));OtherRequest.ProxyEpoch=Other->GetGpuStreamProxyEpoch();
 Batch.Add({Other,0,MoveTemp(OtherRequest),OtherReceipt});
 Batch.Add({Mesh,0,Request(false,1,FVector(300,0,0)),DuplicateReceipt});
 UKKAffineProceduralMeshComponent::SubmitGpuStreamsBatch(MoveTemp(Batch));
 TestFalse(TEXT("Contained batch does not dirty main render transform"),Mesh->IsRenderTransformDirty());
 TestTrue(TEXT("Expanding batch updates independent component bounds"),Other->IsRenderTransformDirty());
 VerifyReceipt(MainReceipt,true,1);VerifyReceipt(OtherReceipt,true,1);VerifyReceipt(DuplicateReceipt,false,1);
 TestTrue(TEXT("Batched main preserves exact streams"),Mesh->VerifyGpuRenderStreams(0,Expected,1));
 auto OtherExpected=Original;for(int32 I=0;I<Count;++I)OtherExpected[I].Position=P[I]+FVector(2,.125,0);
 TestTrue(TEXT("Batched independent component publishes its own streams"),Other->VerifyGpuRenderStreams(0,OtherExpected,1));

 auto SubmitOne=[&](UKKAffineProceduralMeshComponent* C,FKKGpuStreamRequest R,TSharedPtr<FKKGpuStreamReceipt,ESPMode::ThreadSafe> Receipt){
  R.ProxyEpoch=C->GetGpuStreamProxyEpoch();TArray<FKKGpuStreamSectionRequest> One;One.Add({C,0,MoveTemp(R),Receipt});UKKAffineProceduralMeshComponent::SubmitGpuStreamsBatch(MoveTemp(One));
 };
 auto ScopedMain=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>(),ScopedOther=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>();
 {
  FKKGpuStreamBatchScope Frame;
  SubmitOne(Mesh,Request(false,1,FVector(4,.25,0)),ScopedMain);
  SubmitOne(Other,Request(false,1,FVector(2,.125,0)),ScopedOther);
  TestEqual(TEXT("Disjoint publications wait for scope flush"),ScopedMain->Status.Load(),int32(FKKGpuStreamReceipt::Pending));
 }
 VerifyReceipt(ScopedMain,true,1);VerifyReceipt(ScopedOther,true,1);
 auto RepeatedFirst=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>(),RepeatedLast=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>();
 auto NestedFirst=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>(),NestedLast=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>();
 {
  FKKGpuStreamBatchScope Frame;
  SubmitOne(Mesh,Request(false,1,FVector(5,.5,0)),RepeatedFirst);
  SubmitOne(Mesh,Request(false,1,FVector(4,.25,0)),RepeatedLast);
  {FKKGpuStreamBatchScope Nested;SubmitOne(Other,Request(false,1,FVector(3,.3,0)),NestedFirst);}
  SubmitOne(Other,Request(false,1,FVector(2,.125,0)),NestedLast);
 }
 VerifyReceipt(RepeatedFirst,true,1);VerifyReceipt(RepeatedLast,true,1);VerifyReceipt(NestedFirst,true,1);VerifyReceipt(NestedLast,true,1);
 TestTrue(TEXT("Repeated and nested scopes preserve last main publication"),Mesh->VerifyGpuRenderStreams(0,Expected,1));
 TestTrue(TEXT("Repeated and nested scopes preserve last other publication"),Other->VerifyGpuRenderStreams(0,OtherExpected,1));
 auto BeforeDirect=MakeShared<FKKGpuStreamReceipt,ESPMode::ThreadSafe>();
 TSharedPtr<FKKGpuStreamReceipt,ESPMode::ThreadSafe> Direct;
 {FKKGpuStreamBatchScope Frame;SubmitOne(Mesh,Request(false,1,FVector(6,.6,0)),BeforeDirect);Direct=Mesh->SubmitGpuStreams(0,Request(false,1,FVector(4,.25,0)));}
 VerifyReceipt(BeforeDirect,true,1);VerifyReceipt(Direct.ToSharedRef(),true,1);
 TestTrue(TEXT("Direct submission preserves ordering with scoped batch"),Mesh->VerifyGpuRenderStreams(0,Expected,1));

 auto* Camera=World->SpawnActor<AActor>();auto* Capture=NewObject<USceneCaptureComponent2D>(Camera);Camera->SetRootComponent(Capture);Capture->RegisterComponent();
 auto* Target=NewObject<UTextureRenderTarget2D>(Capture);Target->ClearColor=FLinearColor::Black;Target->InitCustomFormat(256,256,PF_B8G8R8A8,false);Target->UpdateResourceImmediate(true);
 Capture->TextureTarget=Target;Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->ProjectionType=ECameraProjectionMode::Orthographic;Capture->OrthoWidth=30;
 Capture->SetWorldLocationAndRotation(FVector(0,0,20),FRotator(-90,0,0));Capture->CaptureSource=SCS_FinalColorLDR;
 Capture->ShowFlags.SetLighting(false);Capture->ShowFlags.SetPostProcessing(false);Capture->ShowFlags.SetAntiAliasing(false);
 auto Pixels=[&](){World->SendAllEndOfFrameUpdates();FlushRenderingCommands();Capture->CaptureScene();FlushRenderingCommands();TArray<FColor> Image;Target->GameThread_GetRenderTargetResource()->ReadPixels(Image);return Image;};
 const auto GPU=Pixels();
 TArray<FVector> CPU;for(const auto& V:Expected)CPU.Add(V.Position);
 UKKAffineProceduralMeshComponent::UpdateStreams(Mesh,0,CPU,N,{}, {},Tangents);
 const auto Reference=Pixels();int32 VisiblePixels=0,Mismatches=0;
 for(int32 I=0;I<GPU.Num();++I){if(GPU[I].R||GPU[I].G||GPU[I].B)++VisiblePixels;if(!Reference.IsValidIndex(I)||GPU[I]!=Reference[I])++Mismatches;}
 TestEqual(TEXT("Raster capture pixel count"),GPU.Num(),256*256);TestTrue(TEXT("Raster capture contains drawn geometry"),VisiblePixels>100);
 TestEqual(TEXT("GPU scene draw matches CPU scene draw at same pose"),Mismatches,0);
 const FString ImageDir=FPaths::ProjectSavedDir()/TEXT("Tests/GpuStreamProxy");IFileManager::Get().MakeDirectory(*ImageDir,true);
 TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(256,256,GPU,PNG);FFileHelper::SaveArrayToFile(PNG,*(ImageDir/TEXT("GPU.png")));
 PNG.Reset();FImageUtils::PNGCompressImageArray(256,256,Reference,PNG);FFileHelper::SaveArrayToFile(PNG,*(ImageDir/TEXT("CPU.png")));
 TestTrue(TEXT("CPU takeover restores ordinary stream verification"),Mesh->VerifyAnimatedRenderStreams(0,0,0));
 auto GenerationQuery=Request(false,0,FVector::ZeroVector);GenerationQuery.QueryGenerationOnly=true;
 VerifyReceipt(Mesh->SubmitGpuStreams(0,MoveTemp(GenerationQuery)),true,2);
 TestTrue(TEXT("Generation handshake retains CPU streams"),Mesh->VerifyAnimatedRenderStreams(0,0,0));
 VerifyReceipt(Mesh->SubmitGpuStreams(0,Request(false,1,FVector::ZeroVector)),false,2);
 VerifyReceipt(Mesh->SubmitGpuStreams(0,Request(true,2,FVector::ZeroVector)),true,3);
 Mesh->MarkRenderStateDirty();World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
 TestTrue(TEXT("Proxy rebuild changes epoch"),Mesh->GetGpuStreamProxyEpoch()>Epoch);
 VerifyReceipt(Mesh->SubmitGpuStreams(0,Request(false,3,FVector::ZeroVector)),false,3);
 TestTrue(TEXT("Rebuilt proxy retains original CPU authored streams"),Mesh->VerifyAnimatedRenderStreams(0,0,0));
 AddInfo(FString::Printf(TEXT("Actual GPU stream proxy frames=3 vertices=%d raster_pixels=%d visible_pixels=%d pixel_mismatches=%d"),Count,GPU.Num(),VisiblePixels,Mismatches));
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);FlushRenderingCommands();return true;
}
#endif
