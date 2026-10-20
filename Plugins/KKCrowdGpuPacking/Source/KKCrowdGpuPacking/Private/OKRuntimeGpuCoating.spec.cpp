#if WITH_DEV_AUTOMATION_TESTS
#include "OKRuntimeGpuCoating.h"
#include <limits>
#include "Misc/AutomationTest.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "HAL/PlatformProcess.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKRuntimeGpuCoatingTest,"OpenKoikatsu.GPU.RuntimeCoating",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKRuntimeGpuCoatingTest::RunTest(const FString&){
 if(!TestTrue(TEXT("Actual coating shader available"),OKGpuCoatingAvailable()))return false;
 TArray<FOKGpuCoatingBinding> Bindings;
 for(uint32 I=0;I<129;++I){FOKGpuCoatingBinding B;B.Vertices=FUintVector4(I%7,I%7,(I%5)?((I+1)%7):(I%7),0);B.Offset=FVector4f(.01f,-.02f,.025f,0);B.Normal=FVector4f(0,0,1,0);Bindings.Add(B);}
 FString Error;FMatrix44f M=FMatrix44f::Identity;
 TestTrue(TEXT("Runtime binding validates"),OKValidateGpuCoatingBindings(Bindings,7,M,Error));
 auto Invalid=Bindings;Invalid[0].Vertices.Z=7;TestFalse(TEXT("Out of range edge rejected"),OKValidateGpuCoatingBindings(Invalid,7,M,Error));
 Invalid=Bindings;Invalid[0].Offset.Y=std::numeric_limits<float>::quiet_NaN();TestFalse(TEXT("Nonfinite offset rejected"),OKValidateGpuCoatingBindings(Invalid,7,M,Error));
 Invalid=Bindings;Invalid[0].Normal.Z=std::numeric_limits<float>::infinity();TestFalse(TEXT("Nonfinite normal rejected"),OKValidateGpuCoatingBindings(Invalid,7,M,Error));
 Invalid=Bindings;Invalid[0].Vertices.W=1;TestFalse(TEXT("Reserved index rejected"),OKValidateGpuCoatingBindings(Invalid,7,M,Error));
 M.M[0][3]=.1f;TestFalse(TEXT("Projective transform rejected"),OKValidateGpuCoatingBindings(Bindings,7,M,Error));
 TArray<FMatrix44f> Transforms;Transforms.Add(FMatrix44f::Identity);
 M=FMatrix44f::Identity;M.M[0][0]=-2;M.M[1][1]=.3f;M.M[2][2]=1.7f;M.M[1][0]=.4f;M.M[2][1]=-.2f;M.M[3][0]=17;M.M[3][2]=-9;Transforms.Add(M);
 TArray<TArray<FVector3f>> Sources;
 TArray<TSharedPtr<FRHIGPUBufferReadback,ESPMode::ThreadSafe>> Readbacks;
 bool Submitted=true;const uint32 Bytes=Bindings.Num()*sizeof(FOKGpuDeformOutput);
 for(int32 Frame=0;Frame<4;++Frame){
  TArray<FVector3f> Source;for(int32 I=0;I<7;++I)Source.Add(FVector3f(I*.13f+Frame*.17f,I*I*.07f-Frame*.11f,Frame*.3f-I*.19f));Sources.Add(Source);
  auto Readback=MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("RuntimeCoatingOracle"));Readbacks.Add(Readback);const FMatrix44f Transform=Transforms[Frame%2];
  ENQUEUE_RENDER_COMMAND(CoatingOracleDispatch)([Source,Bindings,Transform,Readback,Bytes,&Submitted](FRHICommandListImmediate& Commands){
   FRDGBuilder G(Commands);auto* S=G.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(Source.Num()*12),TEXT("Coating.Source"));G.QueueBufferUpload(S,Source.GetData(),Source.Num()*12);
   auto* B=CreateStructuredBuffer(G,TEXT("Coating.Bindings"),sizeof(FOKGpuCoatingBinding),Bindings.Num(),Bindings.GetData(),Bindings.Num()*sizeof(FOKGpuCoatingBinding));
   auto* O=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FOKGpuDeformOutput),Bindings.Num()),TEXT("Coating.Output"));
   Submitted&=!OKAddRuntimeGpuCoatingPass(G,S,B,O,Source.Num(),Bindings.Num()+1,Transform);
   Submitted&=!OKAddRuntimeGpuCoatingPass(G,S,B,S,Source.Num(),Bindings.Num(),Transform);
   Submitted&=!OKAddRuntimeGpuCoatingPass(G,S,B,O,Source.Num()+1,Bindings.Num(),Transform);
   FMatrix44f Bad=Transform;Bad.M[0][3]=1;Submitted&=!OKAddRuntimeGpuCoatingPass(G,S,B,O,Source.Num(),Bindings.Num(),Bad);
   const bool Added=OKAddRuntimeGpuCoatingPass(G,S,B,O,Source.Num(),Bindings.Num(),Transform);Submitted&=Added;
   if(Added)AddEnqueueCopyPass(G,Readback.operator->(),O,Bytes);G.Execute();
  });
 }
 FlushRenderingCommands();if(!TestTrue(TEXT("Valid dispatch and invalid transactions rejected"),Submitted))return false;
 bool Ready=false;const double Start=FPlatformTime::Seconds();
 while(FPlatformTime::Seconds()-Start<30){ENQUEUE_RENDER_COMMAND(CoatingOraclePoll)([Readbacks,&Ready](FRHICommandListImmediate&){Ready=true;for(const auto& R:Readbacks)Ready&=R->IsReady();});FlushRenderingCommands();if(Ready)break;FPlatformProcess::Sleep(.01f);}
 if(!TestTrue(TEXT("Readback ready"),Ready))return false;
 TArray<TArray<FOKGpuDeformOutput>> Actual;Actual.SetNum(Readbacks.Num());
 ENQUEUE_RENDER_COMMAND(CoatingOracleRead)([Readbacks,Bytes,&Actual](FRHICommandListImmediate&){for(int32 I=0;I<Readbacks.Num();++I){const void* Data=Readbacks[I]->Lock(Bytes);if(Data){Actual[I].SetNumUninitialized(Bytes/sizeof(FOKGpuDeformOutput));FMemory::Memcpy(Actual[I].GetData(),Data,Bytes);}Readbacks[I]->Unlock();}});FlushRenderingCommands();
 double MaxPosition=0,MaxTangent=0;
 for(int32 Frame=0;Frame<Actual.Num();++Frame){if(!TestEqual(TEXT("Every overlay vertex generated"),Actual[Frame].Num(),Bindings.Num()))return false;const FMatrix Transform(Transforms[Frame%2]);for(int32 I=0;I<Bindings.Num();++I){const auto& B=Bindings[I];const auto& A=Actual[Frame][I];
  const FVector P=Transform.TransformPosition(FVector(Sources[Frame][B.Vertices.X]));const FVector Expected=P+FVector(B.Offset.X,B.Offset.Y,B.Offset.Z);
  const FVector Edge=Transform.TransformVector(FVector(Sources[Frame][B.Vertices.Z])-FVector(Sources[Frame][B.Vertices.Y]));const FVector Tangent=Edge.GetSafeNormal();
  MaxPosition=FMath::Max(MaxPosition,(Expected-FVector(A.Position.X,A.Position.Y,A.Position.Z)).Size());MaxTangent=FMath::Max(MaxTangent,(Tangent-FVector(A.Tangent.X,A.Tangent.Y,A.Tangent.Z)).Size());
  if(A.Normal!=B.Normal||A.Position.W!=1||A.Tangent.W!=1){AddError(TEXT("Overlay fixed normal or metadata changed"));return false;}
 }}
 AddInfo(FString::Printf(TEXT("GPU coating frames=%d vertices=%d max_position_cm=%.9g max_tangent=%.9g"),Actual.Num(),Bindings.Num(),MaxPosition,MaxTangent));
 TestTrue(TEXT("GPU transformed positions match independent double affine oracle"),MaxPosition<.0001);TestTrue(TEXT("GPU edge tangent matches double oracle including degenerate edges"),MaxTangent<.0001);return true;
}
#endif

