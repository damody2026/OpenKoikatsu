#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "KKPrivateGpuSource.h"
#include "KKPrivateGpuBounds.h"
#include "KKRustExtract.h"
#include "KKExactSkinning.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "HAL/PlatformProcess.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKPrivateGpuSourceTest,"OpenKoikatsu.AnimationCatalog.PrivateGpuSource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKPrivateGpuSourceTest::RunTest(const FString&){
 const FString Input=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LIVE_PALETTE_AUDIT_CARDS"));
 if(Input.IsEmpty()){AddInfo(TEXT("Actual APNG fixture omitted; supply explicit -PaletteCards"));return true;}
 if(!TestTrue(TEXT("Actual GPU deform available"),OKGpuDeformAvailable()))return false;
 FKKScopedNativeSkinSource Mode;TArray<FString> Cards;Input.ParseIntoArray(Cards,TEXT("|"),true);
 TArray<uint8> Curves;if(!FFileHelper::LoadFileToArray(Curves,*(FPaths::ProjectContentDir()/TEXT("IndependentResources/RuntimeAnimations/walk_01.okanim"))))return false;
 auto Direction=[](const float* V){return FVector(-V[0],V[2],V[1]).GetSafeNormal();};
 struct FPart {FKKPrivateGpuSource Source;TArray<FOKGpuDeformDraw> Draw;FKKPrivateGpuBounds Bounds;};
 struct FProbe {FString Name;int32 Part=0;TSharedPtr<FRHIGPUBufferReadback,ESPMode::ThreadSafe> Readback;TArray<FOKGpuDeformOutput> Expected,Actual;};
 double MaxPosition=0,MaxNormal=0,MaxTangent=0;int64 Checked=0;int32 Publications=0,Missing=0;
 for(const FString& Card:Cards){
  FOkSession Session;FString Error;if(!Session.Open(FString(),Card,0,Error)){AddError(Error);return false;}
  if(!Session.InstallSharedClip(Curves))return false;
  TMap<int32,FPart> Parts;
  for(int32 Frame=0;Frame<3;++Frame){
   if(!Session.ApplyAnimFrame(Frame*.17f,Frame?1.f/120.f:0.f))return false;
   // Shared-curve sampling solves rigs/particles but intentionally leaves CPU
   // geometry unpublished. Query the SAME already-solved pose, exactly like
   // the renderer's RefreshSkinnedDisplay, without another physics step.
   if(!Session.Skin()){AddError(TEXT("Native posed query geometry could not be published"));return false;}
   TArray<TSharedRef<FProbe,ESPMode::ThreadSafe>> Probes;bool Submitted=true;
   for(int32 Index=0;Index<Session.PartCount();++Index){
    FKKPrivateGpuPose Pose;
    if(Frame==0){
     if(!Session.PrivateSkinSourceRevision(Index))continue;
     FPart Prepared;
     if(!OKCapturePrivateGpuSource(Session,Index,Prepared.Source,Pose,Error)){++Missing;AddInfo(FString::Printf(TEXT("Private GPU retains CPU part=%d reason=%s"),Index,*Error));continue;}
     const int32 N=Prepared.Source.Vertices.Num();TArray<FVector> Offsets,Normals,Rest;TArray<int32> Indices;
     Offsets.Init(FVector::ZeroVector,N);Normals.SetNum(N);Rest.SetNum(N);
     for(int32 V=0;V<N;++V){const auto& S=Prepared.Source.Vertices[V];const float Native[]={S.Normal.X,S.Normal.Y,S.Normal.Z};Normals[V]=Rest[V]=Direction(Native);Indices.Add(N-1-V);}
     TArray<FOKGpuDeformDraw> Surface,Secondary;
     if(!OKBuildPrivateGpuDraw(Prepared.Source,Indices,0,Offsets,Normals,Rest,Surface,Error)){AddError(Error);return false;}
     // Reverse-index surface covers every native vertex. Secondary mapping
     // includes duplicate indices and follows the original engine quaternion.
     Indices.Reset();for(int32 V=0;V<N;V+=2){Indices.Add(V);if(V%19==0)Indices.Add(V);}
     Offsets.Init(FVector(.07,-.11,.03),N);Normals.Init(FVector(.2,.7,-.3),N);
     if(!OKBuildPrivateGpuDraw(Prepared.Source,Indices,1,Offsets,Normals,Rest,Secondary,Error)){AddError(Error);return false;}
     Prepared.Draw=MoveTemp(Surface);Prepared.Draw.Append(Secondary);
     if(!OKBuildPrivateGpuBounds(Prepared.Source,Prepared.Draw,Prepared.Bounds,Error)){AddError(Error);return false;}
     const int32 StableDrawCount=Prepared.Draw.Num();const auto StableBinding=Prepared.Draw[0].Binding;
     const int32 BadIndex=N;
     TestFalse(TEXT("Invalid draw mapping rejected"),OKBuildPrivateGpuDraw(Prepared.Source,MakeArrayView(&BadIndex,1),1,Offsets,Normals,Rest,Prepared.Draw,Error));
     TestTrue(TEXT("Failed draw retains prior mapping"),Prepared.Draw.Num()==StableDrawCount&&Prepared.Draw[0].Binding==StableBinding);
     Parts.Add(Index,MoveTemp(Prepared));
    }else{
     const FPart* Existing=Parts.Find(Index);if(!Existing)continue;
     if(!OKReadPrivateGpuPose(Session,Index,Existing->Source,Pose,Error)){AddError(Error);return false;}
    }
    const auto& Part=Parts.FindChecked(Index);OkMeshView View{};
    FBox Bounds(ForceInit);if(!OKEvaluatePrivateGpuBounds(Part.Bounds,Pose,Bounds,Error)){AddError(Error);return false;}
    if(!Session.PartAt(Index,View)||View.n_verts!=Part.Source.Vertices.Num()||!View.positions||!View.normals||!View.rest_tangents){AddError(TEXT("Native posed source unavailable"));return false;}
    auto Probe=MakeShared<FProbe,ESPMode::ThreadSafe>();Probe->Expected.SetNumUninitialized(Part.Draw.Num());
    Probe->Part=Index;Probe->Name=View.name?UTF8_TO_TCHAR(View.name):TEXT("");
    for(int32 I=0;I<Part.Draw.Num();++I){const auto& D=Part.Draw[I];const int32 V=D.Binding.X;
     const float* P=View.positions+V*3,*N=View.normals+V*3,*T=View.rest_tangents+V*4;
     FVector Position(-P[0]*100.f,P[2]*100.f,P[1]*100.f),Normal=Direction(N),Tangent=Direction(T);
     const FVector Rest(D.RestNormal.X,D.RestNormal.Y,D.RestNormal.Z),Offset(D.Offset.X,D.Offset.Y,D.Offset.Z);
     const FVector UnitRest=Rest.GetSafeNormal(),UnitCurrent=Normal.GetSafeNormal();
     const FQuat Rotation=UnitRest.IsNearlyZero()||UnitCurrent.IsNearlyZero()?FQuat::Identity:FQuat::FindBetweenNormals(UnitRest,UnitCurrent);
     Position+=Rotation.RotateVector(Offset);
     FVector SparsePosition;if(!OKEvaluatePrivateGpuPosition(Part.Source,Pose,D,SparsePosition)||!SparsePosition.Equals(Position,.001)){AddError(TEXT("Resident sparse CPU position differs from exact native query"));return false;}
     if(!Bounds.IsInsideOrOn(Position)){AddError(TEXT("Actual private source escaped conservative runtime bounds"));return false;}
     if(D.Binding.Y)Normal=Rotation.RotateVector(FVector(D.SectionNormal.X,D.SectionNormal.Y,D.SectionNormal.Z));
     Probe->Expected[I]={FVector4f(FVector3f(Position),1),FVector4f(FVector3f(Normal),0),FVector4f(FVector3f(Tangent),T[3])};
    }
    TArray<FVector4f> Rows;if(!OKPrepareGpuDeformPalette(Pose.Palette,Rows,Error)){AddError(Error);return false;}
    Probe->Readback=MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("ActualAPNGPrivateSource"));Probes.Add(Probe);
    ENQUEUE_RENDER_COMMAND(PrivateAPNGDeform)([Source=Part.Source.Vertices,Draw=Part.Draw,Rows=MoveTemp(Rows),Post=Pose.PostCorrection,Count=Part.Source.PaletteCount,Fallback=Part.Source.FallbackSlot,Probe,&Submitted](FRHICommandListImmediate& Commands){
     FRDGBuilder G(Commands);
     auto* S=CreateStructuredBuffer(G,TEXT("APNGPrivate.Source"),sizeof(FOKGpuDeformSource),Source.Num(),Source.GetData(),Source.Num()*sizeof(FOKGpuDeformSource));
     auto* D=CreateStructuredBuffer(G,TEXT("APNGPrivate.Draw"),sizeof(FOKGpuDeformDraw),Draw.Num(),Draw.GetData(),Draw.Num()*sizeof(FOKGpuDeformDraw));
     auto* P=CreateStructuredBuffer(G,TEXT("APNGPrivate.Palette"),sizeof(FVector4f),Rows.Num(),Rows.GetData(),Rows.Num()*sizeof(FVector4f));
     auto* O=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FOKGpuDeformOutput),Draw.Num()),TEXT("APNGPrivate.Output"));
     const bool Added=OKAddRuntimeGpuDeformPass(G,S,D,P,O,Draw.Num(),Count,Fallback,&Post,true);Submitted&=Added;
     if(Added)AddEnqueueCopyPass(G,Probe->Readback.Get(),O,Draw.Num()*sizeof(FOKGpuDeformOutput));G.Execute();
    });++Publications;
   }
   TArray<int32> BatchIndices;Parts.GetKeys(BatchIndices);BatchIndices.Sort();TArray<const FKKPrivateGpuSource*> BatchSources;
   for(int32 Index:BatchIndices)BatchSources.Add(&Parts.FindChecked(Index).Source);
   TArray<FKKPrivateGpuPose> BatchPoses;
   if(!TestTrue(TEXT("Actual APNG private palettes read as one batch"),OKReadPrivateGpuPoses(Session,BatchIndices,BatchSources,BatchPoses,Error)))return false;
   for(int32 I=0;I<BatchIndices.Num();++I){FKKPrivateGpuPose Serial;if(!OKReadPrivateGpuPose(Session,BatchIndices[I],*BatchSources[I],Serial,Error))return false;
    TestEqual(TEXT("Batch revision matches independent read"),BatchPoses[I].Revision,Serial.Revision);
    TestEqual(TEXT("Batch palette length matches independent read"),BatchPoses[I].Palette.Num(),Serial.Palette.Num());
    for(int32 B=0;B<Serial.Palette.Num();++B)TestTrue(TEXT("Batch palette is bit exact"),BatchPoses[I].Palette[B].Equals(Serial.Palette[B],0.f));
    TestTrue(TEXT("Batch placement is bit exact"),BatchPoses[I].PostCorrection.Equals(Serial.PostCorrection,0.f));
   }
   const auto SavedBatch=BatchPoses;
   auto DuplicateIndices=BatchIndices;auto DuplicateSources=BatchSources;DuplicateIndices.Add(BatchIndices[0]);DuplicateSources.Add(BatchSources[0]);
   TestFalse(TEXT("Duplicate native batch target rejected atomically"),OKReadPrivateGpuPoses(Session,DuplicateIndices,DuplicateSources,BatchPoses,Error));
   FKKPrivateGpuSource Changed=*BatchSources[0];++Changed.Revision;BatchSources[0]=&Changed;
   TestFalse(TEXT("Stale native batch source rejected atomically"),OKReadPrivateGpuPoses(Session,BatchIndices,BatchSources,BatchPoses,Error));
   TestEqual(TEXT("Failed batch retains previous pose count"),BatchPoses.Num(),SavedBatch.Num());
   for(int32 I=0;I<BatchPoses.Num();++I)for(int32 B=0;B<BatchPoses[I].Palette.Num();++B)TestTrue(TEXT("Failed batch retains prior palettes"),BatchPoses[I].Palette[B].Equals(SavedBatch[I].Palette[B],0.f));
   FlushRenderingCommands();if(!TestTrue(TEXT("Actual APNG GPU passes submitted"),Submitted)||Probes.IsEmpty())return false;
   bool Ready=false;const double Start=FPlatformTime::Seconds();
   while(FPlatformTime::Seconds()-Start<30.){
    ENQUEUE_RENDER_COMMAND(PrivateAPNGPoll)([Probes,&Ready](FRHICommandListImmediate&){Ready=true;for(const auto& P:Probes)Ready&=P->Readback->IsReady();});FlushRenderingCommands();
    if(Ready)break;FPlatformProcess::Sleep(.01f);
   }
   if(!TestTrue(TEXT("Actual APNG GPU readback ready"),Ready))return false;
   ENQUEUE_RENDER_COMMAND(PrivateAPNGRead)([Probes](FRHICommandListImmediate&){for(const auto& P:Probes){const uint32 Bytes=P->Expected.Num()*sizeof(FOKGpuDeformOutput);const void* Data=P->Readback->Lock(Bytes);if(Data){P->Actual.SetNumUninitialized(P->Expected.Num());FMemory::Memcpy(P->Actual.GetData(),Data,Bytes);}P->Readback->Unlock();}});FlushRenderingCommands();
   for(const auto& P:Probes){if(!TestEqual(TEXT("Every real APNG draw produced"),P->Actual.Num(),P->Expected.Num()))return false;
    double PartPosition=0,PartNormal=0,PartTangent=0;
    for(int32 I=0;I<P->Actual.Num();++I){const auto& A=P->Actual[I];const auto& E=P->Expected[I];
     const double PE=(A.Position-E.Position).Size(),NE=(A.Normal-E.Normal).Size(),TE=(FVector3f(A.Tangent)-FVector3f(E.Tangent)).Size();
     if(!FMath::IsFinite(PE)||!FMath::IsFinite(NE)||!FMath::IsFinite(TE)||A.Tangent.W!=E.Tangent.W||A.Position.W!=1||A.Normal.W!=0){AddError(TEXT("Invalid real APNG GPU streams"));return false;}
     MaxPosition=FMath::Max(MaxPosition,PE);MaxNormal=FMath::Max(MaxNormal,NE);MaxTangent=FMath::Max(MaxTangent,TE);++Checked;
     PartPosition=FMath::Max(PartPosition,PE);PartNormal=FMath::Max(PartNormal,NE);PartTangent=FMath::Max(PartTangent,TE);
    }
    if(PartPosition>.002||PartNormal>.0001||PartTangent>.0001)AddInfo(FString::Printf(TEXT("APNG GPU mismatch card=%s frame=%d part=%d name=%s position_cm=%g normal=%g tangent=%g"),*Card,Frame,P->Part,*P->Name,PartPosition,PartNormal,PartTangent));
   }
  }
  if(Parts.IsEmpty()){AddError(TEXT("Actual APNG did not exercise private sources"));return false;}
  auto& Last=Parts.CreateIterator().Value();FKKPrivateGpuPose Stable;
  const int32 PartIndex=Parts.CreateIterator().Key();if(!OKReadPrivateGpuPose(Session,PartIndex,Last.Source,Stable,Error))return false;
  const uint64 GoodRevision=Last.Source.Revision;Last.Source.Revision++;
  TestFalse(TEXT("Stale source rejects palette-only update"),OKReadPrivateGpuPose(Session,PartIndex,Last.Source,Stable,Error));TestEqual(TEXT("Rejected pose retains prior revision"),Stable.Revision,GoodRevision);Last.Source.Revision=GoodRevision;
  Session.Close();TestFalse(TEXT("Closed source rejected without dereferencing borrowed geometry"),OKReadPrivateGpuPose(Session,PartIndex,Last.Source,Stable,Error));
 }
 AddInfo(FString::Printf(TEXT("Actual APNG private GPU publications=%d draw_vertices=%lld retained_cpu_parts=%d max_position_cm=%.9g max_normal=%.9g max_tangent=%.9g"),Publications,Checked,Missing,MaxPosition,MaxNormal,MaxTangent));
 TestTrue(TEXT("Full native source detail exercised"),Checked>100000);
 TestTrue(TEXT("GPU real APNG positions match original native renderer"),MaxPosition<=.002);
 TestTrue(TEXT("GPU real APNG normals match engine secondary rotation"),MaxNormal<=.0001);
 TestTrue(TEXT("GPU real APNG tangents match native renderer"),MaxTangent<=.0001);
 return !HasAnyErrors();
}
#endif
