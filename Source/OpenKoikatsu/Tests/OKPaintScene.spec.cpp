#include "../OKPaintSprayer.h"
#include "../OKPaintDemo.h"
#include "../OKPaintSkinning.h"
#include "KKPreviewActor.h"
#include "KKAffineProceduralMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "RenderingThread.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"
#include "TextureResource.h"
#include "../OpenKoikatsuPlayerController.h"
#include "Misc/AutomationTest.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "HAL/IConsoleManager.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKPaintSceneTest,"OpenKoikatsu.Paint.Scene",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKPaintSceneTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* A=World->SpawnActor<AOKPaintDemoCharacter>();auto* B=World->SpawnActor<AOKPaintDemoCharacter>(FVector(0,180,0),FRotator::ZeroRotator);auto* Spray=World->SpawnActor<AOKPaintSprayer>();World->BeginPlay();if(!A->HasActorBegunPlay())A->DispatchBeginPlay();if(!B->HasActorBegunPlay())B->DispatchBeginPlay();if(!Spray->HasActorBegunPlay())Spray->DispatchBeginPlay();
 auto NewSprayer=[&](){auto* S=World->SpawnActor<AOKPaintSprayer>();if(!S->HasActorBegunPlay())S->DispatchBeginPlay();return S;};
 TestNotNull(TEXT("Generated skeletal asset loaded"),A->Body->GetSkinnedAsset());
 // Visual transparency must reach the Niagara binding without reducing the
 // physical flow or changing deposited pigment color.
 Spray->Settings.JetOpacity=.25f;Spray->SetSpray(true,FVector(-150,0,110),FVector(1,0,0));
 TInlineComponentArray<UNiagaraComponent*> JetComponents;Spray->GetComponents(JetComponents);bool CheckedOpacity=false;
 for(auto* FX:JetComponents)if(FX->GetAsset()&&FX->GetAsset()->GetName()==TEXT("NS_PaintJet")){
  const auto Color=FX->GetOverrideParameters().GetParameterValue<FLinearColor>(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.PaintColor")));
  TestTrue(TEXT("Jet opacity reaches real Niagara user binding"),FMath::IsNearlyEqual(Color.A,.25f)&&Color.R==1&&Color.G==1&&Color.B==1);CheckedOpacity=true;
 }
 TestTrue(TEXT("Real Niagara jet checked"),CheckedOpacity);TestEqual(TEXT("Jet transparency leaves pigment alpha intact"),Spray->Settings.Color.A,1.f);

 Spray->Settings.ScatterDegrees=0;Spray->Settings.Flow=12;Spray->Settings.SurfaceHz=20;Spray->SetSpray(true,FVector(-150,0,110),FVector(1,0,0));
 double SumMs=0,PeakMs=0;for(int32 I=0;I<120;++I){A->Tick(1.f/60);B->Tick(1.f/60);A->Body->Bounds=FBoxSphereBounds(FBox(FVector(-1),FVector(1)));Spray->Tick(1.f/60);SumMs+=Spray->LastUpdateMs;PeakMs=FMath::Max(PeakMs,Spray->LastUpdateMs);}
 TestTrue(TEXT("Repeated spray deposits despite stale skeletal renderer bounds"),Spray->Deposited>10);TestTrue(TEXT("Triangle sweep gets hits"),Spray->HitCount>20);TestEqual(TEXT("Only aimed character acquires a surface"),Spray->SurfaceCount(),1);TestTrue(TEXT("Source/film/drop mass conserved"),FMath::Abs(Spray->ConservationError())<1.e-7);
 // Independent sprayers solve the same moving character and source packets.
 // Only the coating render backend differs; physical charts remain identical.
 auto* CoatingActor=World->SpawnActor<AOKPaintDemoCharacter>(FVector(0,360,0),FRotator::ZeroRotator);if(!CoatingActor->HasActorBegunPlay())CoatingActor->DispatchBeginPlay();
 auto* CoatingActorSecond=World->SpawnActor<AOKPaintDemoCharacter>(FVector(0,710,0),FRotator::ZeroRotator);if(!CoatingActorSecond->HasActorBegunPlay())CoatingActorSecond->DispatchBeginPlay();
 auto* CoatingBackend=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.AnimatedCoating"));const int32 PreviousBackend=CoatingBackend->GetInt();
 auto* PacketSweeps=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.PacketSweeps"));const int32 PreviousSweeps=PacketSweeps->GetInt();
 auto* SparseUploads=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.SparseUploads"));const int32 PreviousUploads=SparseUploads->GetInt();
 auto* SparseNormals=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.SparseNormals"));const int32 PreviousNormals=SparseNormals->GetInt();
 auto* VertexTransforms=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.CoatingVertexTransforms"));const int32 PreviousVertexTransforms=VertexTransforms->GetInt();
 auto* SparseGather=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.GpuCoatingSparseGather"));const int32 PreviousSparseGather=SparseGather->GetInt();
 auto* BatchAtlas=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.BatchAtlasUploads"));const int32 PreviousBatchAtlas=BatchAtlas->GetInt();
 auto* GeometryJobs=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.GeometryJobs"));const int32 PreviousGeometryJobs=GeometryJobs->GetInt();
 auto* AnimatedDrops=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.AnimatedDrops"));const int32 PreviousAnimatedDrops=AnimatedDrops->GetInt();int32 ComparedDrops=0;
 CoatingBackend->Set(0,ECVF_SetByCode);auto* StockCoating=NewSprayer();StockCoating->Settings=Spray->Settings;StockCoating->SetSpray(true,FVector(-150,360,110),FVector(1,0,0));
 CoatingBackend->Set(1,ECVF_SetByCode);auto* StreamCoating=NewSprayer();StreamCoating->Settings=Spray->Settings;StreamCoating->SetSpray(true,FVector(-150,360,110),FVector(1,0,0));
 StockCoating->Settings.DripThickness=StreamCoating->Settings.DripThickness=.001f;
 StockCoating->Settings.ResidualThickness=StreamCoating->Settings.ResidualThickness=.0001f;
 StockCoating->Settings.MaxSimulationParticles=StreamCoating->Settings.MaxSimulationParticles=256;
 // Aim between the legs at the torso underside: a front-facing closed face
 // cannot physically release a droplet, regardless of its thickness.
 StockCoating->SetSpray(true,FVector(0,360,-80),FVector::UpVector);StreamCoating->SetSpray(true,FVector(0,360,-80),FVector::UpVector);
 for(int32 I=0;I<72;++I){if(I==18){StockCoating->SetSpray(true,FVector(0,710,-80),FVector::UpVector);StreamCoating->SetSpray(true,FVector(0,710,-80),FVector::UpVector);}if(I==36){StockCoating->StopSpray();StreamCoating->StopSpray();}CoatingActor->Tick(1.f/60);CoatingActorSecond->Tick(1.f/60);PacketSweeps->Set(0,ECVF_SetByCode);SparseUploads->Set(0,ECVF_SetByCode);SparseNormals->Set(0,ECVF_SetByCode);BatchAtlas->Set(0,ECVF_SetByCode);AnimatedDrops->Set(0,ECVF_SetByCode);GeometryJobs->Set(0,ECVF_SetByCode);VertexTransforms->Set(0,ECVF_SetByCode);SparseGather->Set(0,ECVF_SetByCode);StockCoating->Tick(1.f/60);SparseGather->Set(1,ECVF_SetByCode);PacketSweeps->Set(2,ECVF_SetByCode);SparseUploads->Set(1,ECVF_SetByCode);SparseNormals->Set(1,ECVF_SetByCode);BatchAtlas->Set(1,ECVF_SetByCode);AnimatedDrops->Set(1,ECVF_SetByCode);GeometryJobs->Set(2,ECVF_SetByCode);VertexTransforms->Set(1,ECVF_SetByCode);StreamCoating->Tick(1.f/60);
  if(I==35||I==71){World->SendAllEndOfFrameUpdates();FlushRenderingCommands();FVector Anchor;int32 Face;UTextureRenderTarget2D* OriginalTexture=nullptr;UTextureRenderTarget2D* SparseTexture=nullptr;
   int32 ComparedVertices=0,ComparedFaces=0;TestTrue(TEXT("Geometry jobs preserve every current position, triangle, face bound and BVH node exactly"),StreamCoating->VerifyGeometryJobsParity(ComparedVertices,ComparedFaces));AddInfo(FString::Printf(TEXT("Geometry job oracle vertices=%d faces=%d exact=true"),ComparedVertices,ComparedFaces));
   int32 Compared=0;double Error=0.;TestTrue(TEXT("Attached mutable streams preserve stock physical state, every world vertex, normal, UV, color, tangent, conservative bounds and actual GPU buffers"),StreamCoating->VerifyAttachedVisuals(*StockCoating,Compared,Error));ComparedDrops+=Compared;AddInfo(FString::Printf(TEXT("Attached drop oracle compared=%d world_error_cm=%.12f"),Compared,Error));
   TestTrue(TEXT("Original upload oracle has an atlas"),StockCoating->GetPaintAnchor(0,-1,Anchor,OriginalTexture,Face));TestTrue(TEXT("Sparse upload has an atlas"),StreamCoating->GetPaintAnchor(0,-1,Anchor,SparseTexture,Face));
   if(OriginalTexture&&SparseTexture){TArray<FFloat16Color> OriginalPixels,SparsePixels;TestTrue(TEXT("Read original GPU atlas"),OriginalTexture->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(OriginalPixels));TestTrue(TEXT("Read sparse GPU atlas"),SparseTexture->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(SparsePixels));
    const bool Exact=OriginalPixels.Num()==SparsePixels.Num()&&(OriginalPixels.IsEmpty()||FMemory::Memcmp(OriginalPixels.GetData(),SparsePixels.GetData(),OriginalPixels.Num()*sizeof(FFloat16Color))==0);TestTrue(TEXT("Sparse and original uploads preserve every GPU half-float texel during spray and after release"),Exact);
   }
  }
 }
 SparseGather->Set(PreviousSparseGather,ECVF_SetByCode);
 VertexTransforms->Set(PreviousVertexTransforms,ECVF_SetByCode);
 SparseUploads->Set(PreviousUploads,ECVF_SetByCode);
 BatchAtlas->Set(PreviousBatchAtlas,ECVF_SetByCode);
 GeometryJobs->Set(PreviousGeometryJobs,ECVF_SetByCode);AnimatedDrops->Set(PreviousAnimatedDrops,ECVF_SetByCode);TestTrue(TEXT("Attached-drop oracle exercised real live visual droplets"),ComparedDrops>0);
 SparseNormals->Set(PreviousNormals,ECVF_SetByCode);
 PacketSweeps->Set(PreviousSweeps,ECVF_SetByCode);
 CoatingBackend->Set(PreviousBackend,ECVF_SetByCode);
 TestEqual(TEXT("Coating backend preserves deposits"),StockCoating->Deposited,StreamCoating->Deposited);TestEqual(TEXT("Coating backend preserves remaining volume"),StockCoating->RemainingVolume(),StreamCoating->RemainingVolume());
 TestTrue(TEXT("Atlas upload oracle exercises multiple independently moving painted surfaces"),StockCoating->SurfaceCount()>=2&&StreamCoating->SurfaceCount()>=2);
 auto FindCoating=[](AOKPaintSprayer* Source){TInlineComponentArray<UProceduralMeshComponent*> Components;Source->GetComponents(Components);for(auto* Component:Components)if(Component->ComponentHasTag(TEXT("OKPaintOverlay"))&&Component->GetProcMeshSection(0)&&Component->GetProcMeshSection(0)->ProcVertexBuffer.Num()>6)return Component;return static_cast<UProceduralMeshComponent*>(nullptr);};
 auto* StockLayer=FindCoating(StockCoating);auto* StreamLayer=FindCoating(StreamCoating);
 TestNotNull(TEXT("Stock coating exists"),StockLayer);TestNotNull(TEXT("Animated coating exists"),StreamLayer);
 if(StockLayer&&StreamLayer){auto* Owned=Cast<UKKAffineProceduralMeshComponent>(StreamLayer);TestTrue(TEXT("Owned coating uses compact render streams"),Owned&&Owned->UsesAnimatedStreams());
  const auto& Expected=*StockLayer->GetProcMeshSection(0);const auto& Actual=*StreamLayer->GetProcMeshSection(0);bool Exact=Expected.ProcIndexBuffer==Actual.ProcIndexBuffer&&Expected.ProcVertexBuffer.Num()==Actual.ProcVertexBuffer.Num()&&Expected.SectionLocalBox.Min==Actual.SectionLocalBox.Min&&Expected.SectionLocalBox.Max==Actual.SectionLocalBox.Max;
  if(Exact)for(int32 I=0;I<Expected.ProcVertexBuffer.Num();++I){const auto& X=Expected.ProcVertexBuffer[I];const auto& Y=Actual.ProcVertexBuffer[I];Exact&=X.Position==Y.Position&&X.Normal==Y.Normal&&X.UV0==Y.UV0&&X.Color==Y.Color&&X.Tangent.TangentX==Y.Tangent.TangentX&&X.Tangent.bFlipTangentY==Y.Tangent.bFlipTangentY;}
  TestTrue(TEXT("Coating backends preserve every vertex attribute and bounds"),Exact);
 }
 StockCoating->Destroy();StreamCoating->Destroy();CoatingActor->Destroy();CoatingActorSecond->Destroy();
 const auto SweepPair=Spray->MeasureSweepPair(2);TestEqual(TEXT("Captured parallel BVH queries match sequential and independent triangles"),SweepPair.Z,0.);
 TestEqual(TEXT("Ordered incident-face normals match original full-surface accumulation exactly"),Spray->MeasureNormalParity(),0.);
 FVector Before;UTextureRenderTarget2D* Texture=nullptr;int32 Face=-1;TestTrue(TEXT("Paint anchor has a dedicated atlas"),Spray->GetPaintAnchor(0,-1,Before,Texture,Face));FlushRenderingCommands();TArray<FFloat16Color> Pixels;if(Texture){TestTrue(TEXT("GPU thickness atlas can be read back"),Texture->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(Pixels));bool Nonzero=false;for(const auto& Pixel:Pixels)Nonzero|=Pixel.R.GetFloat()>0&&Pixel.A.GetFloat()>0;TestTrue(TEXT("GPU RT stores deposited thickness"),Nonzero);}
 Spray->SetSpray(true,FVector(-150,180,110),FVector(1,0,0));for(int32 I=0;I<60;++I){B->Tick(1.f/60);Spray->Tick(1.f/60);}TestEqual(TEXT("Second character gets independent paint surface"),Spray->SurfaceCount(),2);
 Spray->SetSpray(false,FVector::ZeroVector,FVector(1,0,0));A->Time=9;for(int32 I=0;I<30;++I){A->Tick(1.f/60);Spray->Tick(1.f/60);}TestTrue(TEXT("Bending and raised arms preserve volume"),FMath::Abs(Spray->ConservationError())<1.e-7);
 FVector After;int32 SameFace=-1;Spray->GetPaintAnchor(0,Face,After,Texture,SameFace);TestEqual(TEXT("Attached face identity survives animation"),SameFace,Face);TestTrue(TEXT("Paint anchor follows bent skeletal surface"),FVector::Dist(Before,After)>.1);
 TInlineComponentArray<UProceduralMeshComponent*> Overlays;Spray->GetComponents(Overlays);int32 Painted=0;for(auto* C:Overlays)if(C->ComponentHasTag(TEXT("OKPaintOverlay"))&&C->GetNumSections()>0)++Painted;TestTrue(TEXT("Actual paint overlay geometry generated"),Painted>=2);
 // A pose changes between simulation ticks: the rendered layer must already
 // match it, while the ping-pong mask remains at its independent cadence.
 Spray->Settings.SurfaceHz=5;Spray->Tick(.2f);UTextureRenderTarget2D* HeldTexture=nullptr;FVector HeldAnchor;int32 HeldFace=-1;Spray->GetPaintAnchor(0,Face,HeldAnchor,HeldTexture,HeldFace);
 UProceduralMeshComponent* SkinLayer=nullptr;for(auto* C:Overlays)if(C->GetAttachParent()==A->Body&&C->ComponentHasTag(TEXT("OKPaintOverlay")))SkinLayer=C;
 FVector OldLayer=SkinLayer&&SkinLayer->GetProcMeshSection(0)?SkinLayer->GetComponentTransform().TransformPosition(SkinLayer->GetProcMeshSection(0)->ProcVertexBuffer[0].Position):FVector::ZeroVector;
 A->Time=12;A->Tick(0);Spray->Tick(1.f/240);UTextureRenderTarget2D* SameTexture=nullptr;Spray->GetPaintAnchor(0,Face,After,SameTexture,SameFace);TestEqual(TEXT("Sub-interval pose update does not upload a new mask"),SameTexture,HeldTexture);
 auto* Mesh=Cast<USkeletalMesh>(A->Body->GetSkinnedAsset());if(Mesh&&SkinLayer&&SkinLayer->GetProcMeshSection(0)){
  const auto& LOD=Mesh->GetResourceForRendering()->LODRenderData[0];TArray<FMatrix44f> Matrices;TArray<FVector3f> Current;A->Body->GetCurrentRefToLocalMatrices(Matrices,0);USkinnedMeshComponent::ComputeSkinnedPositions(A->Body,Current,Matrices,LOD,*A->Body->GetSkinWeightBuffer(0));
  double MaxDistance=0;for(const auto& Vertex:SkinLayer->GetProcMeshSection(0)->ProcVertexBuffer){const FVector P=SkinLayer->GetComponentTransform().TransformPosition(Vertex.Position);double Nearest=1.e30;for(const auto& Source:Current)Nearest=FMath::Min(Nearest,FVector::DistSquared(P,A->Body->GetComponentTransform().TransformPosition(FVector(Source))));MaxDistance=FMath::Max(MaxDistance,FMath::Sqrt(Nearest));}
  TestTrue(TEXT("Paint vertices follow current pose between simulation updates"),MaxDistance<.027);TestTrue(TEXT("Sub-interval pose actually moved paint geometry"),FVector::Dist(OldLayer,SkinLayer->GetComponentTransform().TransformPosition(SkinLayer->GetProcMeshSection(0)->ProcVertexBuffer[0].Position))>.1);
 }else AddError(TEXT("Missing skeletal paint layer for frame alignment regression"));
 // Compare the actual generated layer under both scheduling modes at a held
 // pose. Tick(0) preserves ballistic/film clocks, so only coating is rebuilt.
 if(SkinLayer&&SkinLayer->GetProcMeshSection(0)){
  const FVector GeometryPair=Spray->MeasureGeometryPair();TestEqual(TEXT("Parallel geometry matches serial world vertices and collision triangles"),GeometryPair.Z,0.);
  AddInfo(FString::Printf(TEXT("Paint geometry serial=%.3fms parallel=%.3fms max_error=%.12f"),GeometryPair.X,GeometryPair.Y,GeometryPair.Z));
  auto* Shared=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.SharedPoseSnapshot"));const int32 SavedShared=Shared->GetInt();const float SavedHz=Spray->Settings.SurfaceHz;
  Shared->Set(1,ECVF_SetByCode);Spray->Settings.SurfaceHz=60;Spray->Tick(1.f/60);const auto SharedVertices=SkinLayer->GetProcMeshSection(0)->ProcVertexBuffer;
  Shared->Set(0,ECVF_SetByCode);Spray->Tick(0);const auto* OracleSection=SkinLayer->GetProcMeshSection(0);bool SharedSame=OracleSection&&OracleSection->ProcVertexBuffer.Num()==SharedVertices.Num();
  if(SharedSame)for(int32 I=0;I<SharedVertices.Num();++I)SharedSame&=SharedVertices[I].Position.Equals(OracleSection->ProcVertexBuffer[I].Position,1.e-12)&&SharedVertices[I].Normal.Equals(OracleSection->ProcVertexBuffer[I].Normal,1.e-12);
  TestTrue(TEXT("Within-tick shared geometry equals independent deformation oracle"),SharedSame);Shared->Set(SavedShared,ECVF_SetByCode);Spray->Settings.SurfaceHz=SavedHz;
  auto* Mode=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.CoatingParallel"));const int32 SavedMode=Mode->GetInt();
  auto* BufferMode=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.CoatingBufferReuse"));const int32 SavedBufferMode=BufferMode->GetInt();BufferMode->Set(0,ECVF_SetByCode);
  Mode->Set(0,ECVF_SetByCode);Spray->Tick(0);const auto SerialVertices=SkinLayer->GetProcMeshSection(0)->ProcVertexBuffer;const auto SerialIndices=SkinLayer->GetProcMeshSection(0)->ProcIndexBuffer;
  BufferMode->Set(1,ECVF_SetByCode);Mode->Set(2,ECVF_SetByCode);Spray->Tick(0);const auto* ParallelSection=SkinLayer->GetProcMeshSection(0);bool Same=ParallelSection&&ParallelSection->ProcVertexBuffer.Num()==SerialVertices.Num()&&ParallelSection->ProcIndexBuffer==SerialIndices;
  if(Same)for(int32 I=0;I<SerialVertices.Num();++I){const auto& X=SerialVertices[I];const auto& Y=ParallelSection->ProcVertexBuffer[I];Same&=X.Position.Equals(Y.Position,1.e-12)&&X.Normal.Equals(Y.Normal,1.e-12)&&X.UV0.Equals(Y.UV0,1.e-12)&&X.Tangent.TangentX.Equals(Y.Tangent.TangentX,1.e-12)&&X.Tangent.bFlipTangentY==Y.Tangent.bFlipTangentY&&X.Color==Y.Color;}
  TestTrue(TEXT("Forced parallel coating matches serial vertices, normals, UV, tangent and topology"),Same);Mode->Set(SavedMode,ECVF_SetByCode);BufferMode->Set(SavedBufferMode,ECVF_SetByCode);
 }Spray->Settings.SurfaceHz=20;
 AddInfo(FString::Printf(TEXT("Paint fixture 60Hz 120 frames mean CPU %.3fms peak %.3fms %s"),SumMs/120,PeakMs,*Spray->Statistics()));
 Spray->SetSpray(false,FVector::ZeroVector,FVector(1,0,0));const double OtherVolume=Spray->SurfaceVolume(1);UTextureRenderTarget2D* ClearedTexture=nullptr;FVector ClearAnchor;int32 ClearFace=-1;Spray->GetPaintAnchor(0,-1,ClearAnchor,ClearedTexture,ClearFace);TArray<FIntRect> ClearedRects;Spray->GetPaintAtlasRects(0,ClearedTexture,ClearedRects);TArray<FFloat16Color> BeforeClear;FlushRenderingCommands();if(ClearedTexture)ClearedTexture->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(BeforeClear);Spray->ClearPaintForActor(A);Spray->GetPaintAnchor(0,ClearFace,ClearAnchor,ClearedTexture,ClearFace);TestEqual(TEXT("Selective clear empties the chosen character"),Spray->SurfaceVolume(0),0.);TestEqual(TEXT("Selective clear preserves other character"),Spray->SurfaceVolume(1),OtherVolume);TestTrue(TEXT("Selective clear accounts removed mass"),FMath::Abs(Spray->ConservationError())<1.e-7);FlushRenderingCommands();if(ClearedTexture){TArray<FFloat16Color> EmptyPixels;TestTrue(TEXT("Cleared character RT readback"),ClearedTexture->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(EmptyPixels));bool Empty=true,OtherPreserved=BeforeClear.Num()==EmptyPixels.Num();for(int32 I=0;I<EmptyPixels.Num();++I){const FIntPoint At(I%ClearedTexture->SizeX,I/ClearedTexture->SizeX);const bool Owned=ClearedRects.ContainsByPredicate([&](const auto& R){return R.Contains(At);});const auto& P=EmptyPixels[I];if(Owned)Empty&=P.R.GetFloat()==0&&P.A.GetFloat()==0;else if(BeforeClear.IsValidIndex(I))OtherPreserved&=P.R==BeforeClear[I].R&&P.A==BeforeClear[I].A;}TestTrue(TEXT("Shared atlas selective clear preserves every other chart on GPU"),OtherPreserved);TestTrue(TEXT("No stale coverage or thickness remains on GPU"),Empty);}Spray->SetSpray(true,FVector(-150,0,110),FVector(1,0,0));for(int32 I=0;I<30;++I)Spray->Tick(1.f/60);TestTrue(TEXT("Painting again reuses the cleared surface chart"),Spray->SurfaceVolume(0)>0&&Spray->SurfaceCount()==2);
 Spray->ClearPaint();TestEqual(TEXT("Clear removes all surface state"),Spray->SurfaceCount(),0);TestEqual(TEXT("Clear removes volume"),Spray->RemainingVolume(),0.);
 auto* PC=World->SpawnActor<AOpenKoikatsuPlayerController>();PC->PaintAimHeld=true;TestTrue(TEXT("A then viewport left starts spray"),PC->UpdatePaintGesture(true,true,true,false));PC->PaintAimHeld=false;TestFalse(TEXT("Releasing A stops spray"),PC->UpdatePaintGesture(true,true,true,false));PC->UpdatePaintGesture(false,true,true,false);PC->PaintAimHeld=true;TestFalse(TEXT("Press on UI rejects spray"),PC->UpdatePaintGesture(true,true,true,true));TestFalse(TEXT("Dragging a UI press into viewport stays rejected"),PC->UpdatePaintGesture(true,true,true,false));PC->UpdatePaintGesture(false,true,true,false);TestTrue(TEXT("Fresh viewport press starts again"),PC->UpdatePaintGesture(true,true,true,false));TestFalse(TEXT("Focus loss stops spray"),PC->UpdatePaintGesture(true,true,false,false));TestFalse(TEXT("Focus loss clears A"),PC->PaintAimHeld);
 TestFalse(TEXT("Watergun starts inactive outside 3D preview"),PC->IsPaintPreviewActive());PC->SetPaintPreviewActive(true);TestTrue(TEXT("Entering 3D preview creates the shared sprayer"),PC->IsPaintPreviewActive()&&IsValid(PC->PaintSprayer));auto* SharedSprayer=PC->PaintSprayer.Get();PC->PaintAimHeld=true;PC->UpdatePaintGesture(false,true,true,false);PC->UpdatePaintGesture(true,true,true,false);PC->SetPaintPreviewActive(false);TestFalse(TEXT("Returning to cards disables watergun and clears held A"),PC->IsPaintPreviewActive()||PC->PaintAimHeld);TestEqual(TEXT("Leaving preview clears all paint"),SharedSprayer->RemainingVolume(),0.);TestFalse(TEXT("Inactive preview stops paint tick"),SharedSprayer->IsActorTickEnabled());PC->SetPaintPreviewActive(true);TestEqual(TEXT("Reopening 3D reuses one sprayer"),PC->PaintSprayer.Get(),SharedSprayer);TestFalse(TEXT("Held mouse cannot resume spray across preview transition"),PC->UpdatePaintGesture(true,true,true,false));PC->SetPaintPreviewActive(false);
 A->Animate=false;B->Animate=false;double Emission[3],Remain[3];for(int32 Rate=0;Rate<3;++Rate){Spray->ClearPaint();const int32 FPS=Rate==0?30:Rate==1?60:120;Spray->SetSpray(true,FVector(-150,0,110),FVector(1,0,0));for(int32 Frame=0;Frame<FPS*2;++Frame)Spray->Tick(1.f/FPS);Emission[Rate]=Spray->Emitted;Remain[Rate]=Spray->RemainingVolume()+Spray->FloorVolume+Spray->Escaped;TestTrue(TEXT("Actor mass conserved at each frame rate"),FMath::Abs(Spray->ConservationError())<1.e-7);}TestTrue(TEXT("Actor emitted volume matches at 30/60/120fps"),FMath::Abs(Emission[0]-Emission[1])<1.e-7&&FMath::Abs(Emission[1]-Emission[2])<1.e-7);TestTrue(TEXT("Actor total paint matches at 30/60/120fps"),FMath::Abs(Remain[0]-Remain[1])<1.e-7&&FMath::Abs(Remain[1]-Remain[2])<1.e-7);
 Spray->ClearPaint();auto* Plane=World->SpawnActor<AActor>();Plane->Tags.Add(TEXT("OKPaintable"));auto* PMC=NewObject<UKKAffineProceduralMeshComponent>(Plane);Plane->SetRootComponent(PMC);PMC->RegisterComponent();PMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);PMC->CreateMeshSection(0,{FVector(0,-30,70),FVector(0,30,70),FVector(0,30,140),FVector(0,-30,140)},{0,2,1,0,3,2},{},{},{},{},false);Plane->SetActorLocation(FVector(0,360,0));Plane->SetActorScale3D(FVector(2,.5,1.5));Spray->SetSpray(true,FVector(-150,360,140),FVector(1,0,0));for(int32 Frame=0;Frame<60;++Frame)Spray->Tick(1.f/60);TestTrue(TEXT("Native procedural surface receives paint"),Spray->Deposited>5);TestEqual(TEXT("Procedural candidate is independent"),Spray->SurfaceCount(),1);FVector Anchor;int32 PlaneFace=-1;UTextureRenderTarget2D* PlaneTexture=nullptr;TestTrue(TEXT("Procedural atlas anchor"),Spray->GetPaintAnchor(0,-1,Anchor,PlaneTexture,PlaneFace));TestTrue(TEXT("Nonuniform scale uses current world surface"),FMath::Abs(Anchor.X)<.001&&Anchor.Y>340&&Anchor.Y<380&&Anchor.Z>105&&Anchor.Z<210);TestTrue(TEXT("Procedural mass conserved"),FMath::Abs(Spray->ConservationError())<1.e-7);FlushRenderingCommands();if(PlaneTexture){TArray<FFloat16Color> TinyPixels;TestTrue(TEXT("Tiny procedural RT upload completed"),PlaneTexture->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(TinyPixels));bool HasThickness=false;for(const auto& Pixel:TinyPixels)HasThickness|=Pixel.A.GetFloat()>0;TestTrue(TEXT("Tiny RT contains actual thickness rather than skipping upload"),HasThickness);}Spray->SetSpray(false,FVector::ZeroVector,FVector(1,0,0));Spray->Settings.SurfaceHz=5;Spray->Tick(.2f);
 TArray<FVector> MovedVertices;for(const auto& Vertex:PMC->GetProcMeshSection(0)->ProcVertexBuffer)MovedVertices.Add(Vertex.Position+FVector(5,0,0));PMC->UpdateMeshSection(0,MovedVertices,{},{},{},{});Spray->Tick(1.f/240);
 bool CurrentPMC=false;TInlineComponentArray<UProceduralMeshComponent*> PMCLayers;Spray->GetComponents(PMCLayers);for(auto* C:PMCLayers)if(C->GetAttachParent()==PMC&&C->GetProcMeshSection(0)){CurrentPMC=true;for(const auto& Vertex:C->GetProcMeshSection(0)->ProcVertexBuffer){const FVector P=C->GetComponentTransform().TransformPosition(Vertex.Position);CurrentPMC&=FMath::Abs(P.X-9.975)<.002;}}TestTrue(TEXT("Native PMC coating follows a sub-interval mesh deformation with nonuniform scale"),CurrentPMC);Spray->Settings.SurfaceHz=20;
 FMatrix Shear=FMatrix::Identity;Shear.M[1][0]=.35;Shear.M[2][1]=-.2;PMC->SetRenderResidual(Shear);Spray->Tick(.2f);
 const auto* AffineSource=PMC->GetProcMeshSection(0);const FMatrix AffineWorld=PMC->GetRenderMatrix();
 const FVector PlaneOrigin=AffineWorld.TransformPosition(AffineSource->ProcVertexBuffer[0].Position);
 const FVector PlaneNormal=FVector::CrossProduct(FVector(AffineWorld.TransformPosition(AffineSource->ProcVertexBuffer[1].Position))-PlaneOrigin,FVector(AffineWorld.TransformPosition(AffineSource->ProcVertexBuffer[2].Position))-PlaneOrigin).GetSafeNormal();
 bool AffineCoating=false;double AffineDistance=0.;Spray->GetComponents(PMCLayers);
 for(auto* C:PMCLayers)if(C->GetAttachParent()==PMC)if(const auto* Section=C->GetProcMeshSection(0)){AffineCoating=true;for(const auto& Vertex:Section->ProcVertexBuffer)AffineDistance=FMath::Max(AffineDistance,FMath::Abs(FVector::DotProduct(C->GetRenderMatrix().TransformPosition(Vertex.Position)-PlaneOrigin,PlaneNormal)));}
 TestTrue(TEXT("Coating exists after affine bone shear"),AffineCoating);TestTrue(TEXT("Paint follows the exact sheared render surface with only its thickness offset"),AffineDistance<.027);
 TestTrue(TEXT("Affine surface transport conserves mass"),FMath::Abs(Spray->ConservationError())<1.e-7);PMC->SetRenderResidual(FMatrix::Identity);

 PMC->ClearAllMeshSections();Spray->Tick(.1f);TestEqual(TEXT("Replacing procedural topology invalidates old paint"),Spray->SurfaceCount(),0);TestTrue(TEXT("Invalidation accounts removed mass"),FMath::Abs(Spray->ConservationError())<1.e-7);
 Spray->ClearPaint();auto* Offset=World->SpawnActor<AActor>();Offset->Tags.Add(TEXT("OKPaintable"));auto* OffsetPMC=NewObject<UProceduralMeshComponent>(Offset);Offset->SetRootComponent(OffsetPMC);OffsetPMC->RegisterComponent();OffsetPMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);OffsetPMC->CreateMeshSection(0,{FVector(0,-30,70),FVector(0,30,70),FVector(0,30,140),FVector(0,-30,140)},{0,2,1,0,3,2},{},{},{},{},false);Offset->SetActorLocation(FVector(0,420,0));Offset->SetActorScale3D(FVector(2,.5,1.5));Spray->Settings.ScatterDegrees=30;Spray->SetSpray(true,FVector(-150,360,140),FVector(1,0,0));for(int32 Frame=0;Frame<240;++Frame)Spray->Tick(1.f/60);bool OffAxisPaint=false;for(int32 Index=0;Index<Spray->SurfaceCount();++Index){FVector P;int32 F;UTextureRenderTarget2D* T=nullptr;if(Spray->GetPaintAnchor(Index,0,P,T,F)&&P.Y>390&&Spray->SurfaceVolume(Index)>0)OffAxisPaint=true;}TestTrue(TEXT("Scatter hits off-axis surfaces outside the centre ray"),OffAxisPaint);TestTrue(TEXT("Scatter and in-flight discovery conserve mass"),FMath::Abs(Spray->ConservationError())<1.e-7);Spray->ClearPaint();auto* DripActor=World->SpawnActor<AActor>();DripActor->Tags.Add(TEXT("OKPaintable"));auto* DripPMC=NewObject<UProceduralMeshComponent>(DripActor);DripActor->SetRootComponent(DripPMC);DripPMC->RegisterComponent();DripPMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);DripPMC->CreateMeshSection(0,{FVector(-10,-10,0),FVector(-10,10,0),FVector(10,10,0),FVector(10,-10,0)},{0,1,2,0,2,3},{},{},{},{},false);DripActor->SetActorLocation(FVector(300,600,80));Spray->Settings.ScatterDegrees=0;Spray->Settings.SurfaceSpeed=0;Spray->Settings.DripThickness=.01f;Spray->Settings.AdhesionSeconds=.05f;Spray->SetSpray(true,FVector(300,600,20),FVector(0,0,1));bool WetAnchor=false;for(int32 I=0;I<90;++I){Spray->Tick(1.f/60);TInlineComponentArray<UProceduralMeshComponent*> Drops;Spray->GetComponents(Drops);for(auto* C:Drops)if(C->ComponentHasTag(TEXT("OKPaintOverlay"))&&!C->GetAttachParent()){WetAnchor|=FVector::Dist2D(C->GetComponentLocation(),FVector(300,600,80))<4&&C->GetComponentLocation().Z>79;}}TestTrue(TEXT("Runtime drop attaches at actual painted region of underside"),WetAnchor);TestTrue(TEXT("Runtime drop detaches after adhesion interval"),Spray->Dripped>0);TestTrue(TEXT("Runtime local dripping conserves emitted paint"),FMath::Abs(Spray->ConservationError())<1.e-7);Spray->ClearPaint();auto* Native=World->SpawnActor<AKKPreviewActor>();Native->SetActorTickEnabled(false);Native->SetActorLocation(FVector(0,800,0));auto* MorphDisplay=NewObject<USkeletalMeshComponent>(Native);MorphDisplay->SetupAttachment(Native->GetRootComponent());MorphDisplay->RegisterComponent();MorphDisplay->SetSkinnedAssetAndUpdate(A->Body->GetSkinnedAsset());MorphDisplay->SetCollisionEnabled(ECollisionEnabled::NoCollision);FKKLoadedPart NativePart;NativePart.Role=TEXT("body");NativePart.SessionIndex=3;Native->Parts.Add(NativePart);AKKPreviewActor::FOkMouthSlot MorphSlot;MorphSlot.Comp=MorphDisplay;MorphSlot.SessionPartIndex=3;Native->MouthSlots.Add(MorphSlot);Spray->Settings.ScatterDegrees=0;Spray->SetSpray(true,FVector(-150,800,110),FVector(1,0,0));for(int32 Frame=0;Frame<60;++Frame)Spray->Tick(1.f/60);TestTrue(TEXT("KKS morph replacement is paintable even when original part material differs"),Spray->Deposited>5&&Spray->SurfaceCount()==1);TestTrue(TEXT("KKS morph surface conserves paint mass"),FMath::Abs(Spray->ConservationError())<1.e-7);Spray->ClearPaint();Native->SetActorHiddenInGame(true);
 auto* Multi=World->SpawnActor<AActor>();Multi->Tags.Add(TEXT("OKPaintable"));auto* MultiPMC=NewObject<UProceduralMeshComponent>(Multi);Multi->SetRootComponent(MultiPMC);MultiPMC->RegisterComponent();MultiPMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);Multi->SetActorLocation(FVector(0,1100,0));
 MultiPMC->CreateMeshSection(0,{FVector(0,-90,70),FVector(0,-30,70),FVector(0,-30,140),FVector(0,-90,140)},{0,2,1,0,3,2},{},{},{},{},false);
 MultiPMC->CreateMeshSection(1,{FVector(0,-20,70),FVector(0,40,70),FVector(0,40,140),FVector(0,-20,140)},{0,2,1,0,3,2},{},{},{},{},false);
 Spray->Settings.SurfaceSpeed=0;Spray->Settings.DripThickness=10;Spray->SetSpray(true,FVector(-150,1100,110),FVector(1,0,0));for(int32 I=0;I<60;++I)Spray->Tick(1.f/60);
 TestTrue(TEXT("Independent second PMC material region receives paint"),Spray->Deposited>5);TestEqual(TEXT("All visible procedural material regions tracked"),Spray->SurfaceCount(),2);
 Multi->AddActorWorldOffset(FVector(2,0,0));const FVector SurfaceGeometryPair=Spray->MeasureSurfaceGeometryPair();
 TestEqual(TEXT("Independent surface workers preserve vertices, normals, triangles and BVH bounds"),SurfaceGeometryPair.Z,0.);
 AddInfo(FString::Printf(TEXT("Paint surface geometry serial=%.3fms parallel=%.3fms max_error=%.12f"),SurfaceGeometryPair.X,SurfaceGeometryPair.Y,SurfaceGeometryPair.Z));
 const double BeforeHidden=Spray->Deposited;MultiPMC->SetMeshSectionVisible(1,false);for(int32 I=0;I<60;++I)Spray->Tick(1.f/60);TestEqual(TEXT("Hidden PMC region receives no additional paint"),Spray->Deposited,BeforeHidden);
 TInlineComponentArray<UProceduralMeshComponent*> MultiLayers;Spray->GetComponents(MultiLayers);bool HiddenCoating=false;for(auto* Layer:MultiLayers)if(Layer->GetAttachParent()==MultiPMC&&Layer->GetNumSections()>0)HiddenCoating|=!Layer->IsVisible();TestTrue(TEXT("Hidden region coating is hidden"),HiddenCoating);
 MultiPMC->SetMeshSectionVisible(1,true);for(int32 I=0;I<60;++I)Spray->Tick(1.f/60);TestTrue(TEXT("Showing region restores paintability without losing film"),Spray->Deposited>BeforeHidden+5);
 const double BeforeActorHidden=Spray->Deposited;Multi->SetActorHiddenInGame(true);for(int32 I=0;I<60;++I)Spray->Tick(1.f/60);TestEqual(TEXT("Hidden owner receives no additional paint"),Spray->Deposited,BeforeActorHidden);for(auto* Layer:MultiLayers)if(Layer->GetAttachParent()==MultiPMC)TestFalse(TEXT("Hidden owner coating is hidden"),Layer->IsVisible());
 Multi->SetActorHiddenInGame(false);for(int32 I=0;I<60;++I)Spray->Tick(1.f/60);TestTrue(TEXT("Showing owner reuses preserved paint surfaces"),Spray->Deposited>BeforeActorHidden+5&&Spray->SurfaceCount()==2);TestTrue(TEXT("Multi-region hide/show conserves emitted mass"),FMath::Abs(Spray->ConservationError())<1.e-7);
 Spray->ClearPaint();
 auto* ValidationMesh=Cast<USkeletalMesh>(MorphDisplay->GetSkinnedAsset());auto* ValidationMorph=NewObject<UMorphTarget>(ValidationMesh,TEXT("PaintValidation"));auto& MorphLOD=ValidationMorph->GetMorphLODModels().AddDefaulted_GetRef();MorphLOD.NumBaseMeshVerts=ValidationMesh->GetResourceForRendering()->LODRenderData[0].GetNumVertices();MorphLOD.SectionIndices.Add(0);FMorphTargetDelta Delta;Delta.SourceIdx=10;Delta.PositionDelta=FVector3f(1.25f,-.75f,2.f);MorphLOD.Vertices.Add(Delta);MorphLOD.NumVertices=1;ValidationMesh->RegisterMorphTarget(ValidationMorph,false);
 Native->SetActorHiddenInGame(false);MorphDisplay->SetVisibility(true);MorphDisplay->SetWorldScale3D(FVector(1.4,.7,1.2));MorphDisplay->RecreateRenderState_Concurrent();FlushRenderingCommands();
 FOKPaintPose CachedMorphPose;
 for(float Weight:{.65f,-.35f,0.f}){MorphDisplay->SetMorphTarget(TEXT("PaintValidation"),Weight);MorphDisplay->RefreshBoneTransforms();auto& Fast=CachedMorphPose;if(!TestTrue(TEXT("Read-only skin pose prepared"),Fast.Prepare(MorphDisplay)))continue;TArray<FVector> FastPositions;for(uint32 I=0;I<Fast.LOD->GetNumVertices();++I)FastPositions.Add(Fast.Position(I));const bool CpuBefore=MorphDisplay->GetCPUSkinningEnabled();const int32 LodBefore=MorphDisplay->GetForcedLOD();TArray<FFinalSkinVertex> Oracle;MorphDisplay->GetCPUSkinnedVertices(Oracle,0);double MaximumError=0;for(int32 I=0;I<FMath::Min(Oracle.Num(),FastPositions.Num());++I)MaximumError=FMath::Max(MaximumError,FVector::Distance(FastPositions[I],MorphDisplay->GetComponentTransform().TransformPosition(FVector(Oracle[I].Position))));TestEqual(TEXT("Fast morph skin vertex count equals engine CPU oracle"),FastPositions.Num(),Oracle.Num());TestTrue(TEXT("Positive/negative morph plus nonuniform scale matches engine CPU oracle"),MaximumError<.003);FOKPaintPose Again;Again.Prepare(MorphDisplay);TestEqual(TEXT("Fast skinning preserves renderer CPU mode"),MorphDisplay->GetCPUSkinningEnabled(),CpuBefore);TestEqual(TEXT("Fast skinning preserves forced LOD"),MorphDisplay->GetForcedLOD(),LodBefore);}
 ValidationMesh->UnregisterMorphTarget(ValidationMorph);
 // A static two-triangle seam isolates the two fixed clocks from animation.
 // Use actual sprayer ticks at three render rates, not a copy of its scheduler.
 A->SetActorHiddenInGame(true);B->SetActorHiddenInGame(true);Native->SetActorHiddenInGame(true);Multi->SetActorHiddenInGame(true);
 auto* RateActor=World->SpawnActor<AActor>();RateActor->Tags.Add(TEXT("OKPaintable"));auto* RateMesh=NewObject<UProceduralMeshComponent>(RateActor);RateActor->SetRootComponent(RateMesh);RateMesh->RegisterComponent();RateMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 RateMesh->CreateMeshSection(0,{FVector(2000,-20,80),FVector(2000,20,80),FVector(2000,-20,140),FVector(2000,20,140)},{0,1,2,2,1,3},{},{FVector2D(0,0),FVector2D(1,0),FVector2D(0,1),FVector2D(1,1)},{},{},false);
 // Imported triangle winding may face into the model. The coating must
 // offset toward the struck side for either index order, without disabling depth.
 auto* WindingActor=World->SpawnActor<AActor>();WindingActor->Tags.Add(TEXT("OKPaintable"));
 auto* WindingMesh=NewObject<UProceduralMeshComponent>(WindingActor);WindingActor->SetRootComponent(WindingMesh);WindingMesh->RegisterComponent();WindingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 for(int32 Reverse:{0,1}){
  WindingMesh->CreateMeshSection(0,{FVector(2500,-20,80),FVector(2500,20,80),FVector(2500,-20,140),FVector(2500,20,140)},Reverse?TArray<int32>{2,1,0,3,1,2}:TArray<int32>{0,1,2,2,1,3},{},{FVector2D(0,0),FVector2D(1,0),FVector2D(0,1),FVector2D(1,1)},{},{},false);
  auto* WindingSpray=NewSprayer();WindingSpray->Settings.ScatterDegrees=0;WindingSpray->Settings.SurfaceSpeed=0;WindingSpray->SetSpray(true,FVector(2350,0,110),FVector(1,0,0));for(int32 I=0;I<60;++I)WindingSpray->Tick(1.f/60);
  bool FoundLayer=false;TInlineComponentArray<UProceduralMeshComponent*> WindingLayers;WindingSpray->GetComponents(WindingLayers);
  for(auto* Layer:WindingLayers)if(Layer->GetAttachParent()==WindingMesh)if(const auto* Section=Layer->GetProcMeshSection(0)){FoundLayer=true;for(const auto& Vertex:Section->ProcVertexBuffer)TestTrue(TEXT("Coating stays on struck side for either triangle winding"),Layer->GetComponentTransform().TransformPosition(Vertex.Position).X<2499.999);}
  TestTrue(TEXT("Winding fixture received visible coating geometry"),FoundLayer);WindingSpray->ClearPaint();WindingSpray->Destroy();
 }
 WindingActor->SetActorHiddenInGame(true);
 int32 ReferenceHits=-1;double ReferenceVolume=0;
 for(int32 FPS:{30,60,120}){auto* RateSpray=NewSprayer();RateSpray->Settings.ScatterDegrees=0;RateSpray->Settings.SurfaceHz=20;RateSpray->SetSpray(true,FVector(1850,0,110),FVector(1,0,0));
  for(int32 I=0;I<FPS*2;++I)RateSpray->Tick(1.f/FPS);RateSpray->SetSpray(false,FVector::ZeroVector,FVector::ForwardVector);for(int32 I=0;I<FPS;++I)RateSpray->Tick(1.f/FPS);
  TestTrue(TEXT("Multi-rate spray/release conserves mass"),FMath::Abs(RateSpray->ConservationError())<1.e-7);TestTrue(TEXT("Multi-rate fixture receives paint"),RateSpray->HitCount>40);
  const double Volume=RateSpray->SurfaceVolume(0);if(ReferenceHits<0){ReferenceHits=RateSpray->HitCount;ReferenceVolume=Volume;}else{TestEqual(TEXT("30/60/120 render rates have identical swept hits"),RateSpray->HitCount,ReferenceHits);TestTrue(TEXT("30/60/120 render rates retain identical surface volume"),FMath::Abs(Volume-ReferenceVolume)<1.e-6);}
  if(FPS==30){FRandomStream Rays(19);FOKPaintFace Left,Right;Left.P[0]=FVector(2000,-20,80);Left.P[1]=FVector(2000,20,80);Left.P[2]=FVector(2000,-20,140);Right.P[0]=Left.P[2];Right.P[1]=Left.P[1];Right.P[2]=FVector(2000,20,140);
   const auto BaseLeft=Left,BaseRight=Right;
   for(const auto& Transform:{FTransform::Identity,FTransform(FRotator(0,17,0),FVector(40,30,20),FVector(.7,1.3,.8))}){
    RateMesh->SetWorldTransform(Transform);RateSpray->Tick(.05f);Left=BaseLeft;Right=BaseRight;
    for(auto* TestFace:{&Left,&Right})for(auto& P:TestFace->P)P=Transform.TransformPosition(P);
   for(int32 Ray=0;Ray<128;++Ray){const FVector From=Transform.TransformPosition(FVector(1800,Rays.FRandRange(-30,30),Rays.FRandRange(70,150))),To=Transform.TransformPosition(FVector(2200,Rays.FRandRange(-30,30),Rays.FRandRange(70,150)));const double Radius=Rays.FRandRange(.05,2);double Expected=1,T;FVector Bary;bool Hit=false;
    for(const auto& OracleFace:{Left,Right})if(FOKPaintFilm::SweepTriangle(From,To,Radius,OracleFace,T,Bary)&&T<Expected){Expected=T;Hit=true;}
    int32 Surface,Triangle;double Actual=1;FVector ActualBary;const bool Found=RateSpray->FindPaintHit(From,To,Radius,Surface,Triangle,ActualBary,Actual);TestEqual(TEXT("BVH leaf rejection matches exhaustive swept-triangle oracle"),Found,Hit);if(Hit)TestTrue(TEXT("Leaf rejection preserves earliest swept hit"),FMath::Abs(Expected-Actual)<1.e-8);
   }
   }
   RateMesh->SetWorldTransform(FTransform::Identity);
  }
  RateSpray->ClearPaint();RateSpray->Destroy();
 }
 // Fill a second chart while staying within the already-created capacity.
 // This checks visible UV data and zero-area padding, not just counters.
 auto* PoolSpray=NewSprayer();PoolSpray->Settings.ScatterDegrees=0;PoolSpray->Settings.SurfaceSpeed=0;PoolSpray->Settings.Flow=.3f;PoolSpray->Settings.DripThickness=1;
 PoolSpray->SetSpray(true,FVector(1850,-15,94.3),FVector(1,0,0));for(int32 I=0;I<60;++I)PoolSpray->Tick(1.f/60);
 TInlineComponentArray<UProceduralMeshComponent*> PoolLayers;PoolSpray->GetComponents(PoolLayers);UProceduralMeshComponent* PoolLayer=nullptr;for(auto* Layer:PoolLayers)if(Layer->GetAttachParent()==RateMesh&&Layer->GetProcMeshSection(0))PoolLayer=Layer;
 const int32 PoolVertices=PoolLayer?PoolLayer->GetProcMeshSection(0)->ProcVertexBuffer.Num():0;const uint64 PoolRebuilds=PoolSpray->CoatingRebuilds;
 PoolSpray->SetSpray(true,FVector(1850,15,134.3),FVector(1,0,0));for(int32 I=0;I<60;++I)PoolSpray->Tick(1.f/60);
 TestNotNull(TEXT("Pooled coating generated"),PoolLayer);if(PoolLayer&&PoolLayer->GetProcMeshSection(0)){const auto& Part=*PoolLayer->GetProcMeshSection(0);TestEqual(TEXT("New chart reuses render vertex count"),Part.ProcVertexBuffer.Num(),PoolVertices);TestEqual(TEXT("Spreading within capacity avoids section recreation"),PoolSpray->CoatingRebuilds,PoolRebuilds);
  bool SecondChart=Part.ProcVertexBuffer.Num()>=6,Degenerate=true;if(SecondChart){FBox2D First(ForceInit),Second(ForceInit);for(int32 I=0;I<3;++I){First+=Part.ProcVertexBuffer[I].UV0;Second+=Part.ProcVertexBuffer[I+3].UV0;}SecondChart=!First.Intersect(Second); }
  for(int32 I=6;I+2<Part.ProcVertexBuffer.Num();I+=3)Degenerate&=Part.ProcVertexBuffer[I].Position==Part.ProcVertexBuffer[I+1].Position&&Part.ProcVertexBuffer[I].Position==Part.ProcVertexBuffer[I+2].Position;
  TestTrue(TEXT("Second painted triangle has its own UV tile"),SecondChart);TestTrue(TEXT("Unused capacity is zero-area geometry"),Degenerate);
 }TestTrue(TEXT("Pooled growth conserves paint"),FMath::Abs(PoolSpray->ConservationError())<1.e-7);
 PoolSpray->SetSpray(false,FVector::ZeroVector,FVector::ForwardVector);for(int32 I=0;I<120;++I)PoolSpray->Tick(1.f/60);
 const uint64 StillUpdates=PoolSpray->CoatingUpdates;for(int32 I=0;I<60;++I)PoolSpray->Tick(1.f/60);
 TestEqual(TEXT("Stationary coating does not resend mesh buffers"),PoolSpray->CoatingUpdates,StillUpdates);
 PoolSpray->Settings.Color=FLinearColor::Red;PoolSpray->Tick(1.f/60);if(PoolLayer){FLinearColor Color;auto* MID=Cast<UMaterialInstanceDynamic>(PoolLayer->GetMaterial(0));TestTrue(TEXT("Stationary coating color changes without a mask upload"),MID&&MID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("PaintColor")),Color)&&Color==FLinearColor::Red);}
 PoolSpray->ClearPaint();PoolSpray->Destroy();
 Spray->ClearPaint();FlushRenderingCommands();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
