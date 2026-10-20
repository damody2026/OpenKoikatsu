#if WITH_DEV_AUTOMATION_TESTS
#include "OKRuntimeGpuDeform.h"
#include "Misc/AutomationTest.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "HAL/PlatformProcess.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKRuntimeGpuDeformTest,"OpenKoikatsu.GPU.RuntimeDeform",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKRuntimeGpuDeformTest::RunTest(const FString&){
 if(!TestTrue(TEXT("Actual GPU deform shader available"),OKGpuDeformAvailable()))return false;
 TArray<FVector4f> Rows;
 Rows.Append({FVector4f(1,0,0,0),FVector4f(0,1,0,0),FVector4f(0,0,1,0),
  FVector4f(1,.3,0,12),FVector4f(.2,1.4,.1,-3),FVector4f(0,0,.8,7),
  FVector4f(1,0,0,-2),FVector4f(0,-1,0,5),FVector4f(0,0,1,3),
  FVector4f(0,0,0,4),FVector4f(0,0,0,2),FVector4f(0,0,0,-1),
  FVector4f(.7,.1,0,21),FVector4f(0,1.2,0,-9),FVector4f(0,0,1,11)});
 constexpr uint32 PaletteCount=5,Fallback=4;
 TArray<FOKGpuDeformSource> Source;TArray<FOKGpuDeformDraw> Draw;
 for(int32 I=0;I<129;++I){
  FOKGpuDeformSource S{};S.Position=FVector4f(I*.13f,-.7f,I*.017f,1);
  S.Normal=FVector4f(.3f,.4f,.5f,0);S.Tangent=FVector4f(.8f,-.2f,.1f,I%2?-1.f:1.f);
  S.Weights=FVector4f(.1f,.2f,.3f,.4f);S.Bones=FUintVector4(0,1,2,3);
  switch(I%7){
   case 0:S.Weights=FVector4f(-.3f,.8f,.1f,0);break;
   case 1:S.Weights=FVector4f(0,0,0,0);S.Bones=FUintVector4(65535,65535,65535,65535);break;
   case 2:S.Weights=FVector4f(1,-1,0,0);break;
   case 3:S.Weights=FVector4f(1,0,0,0);S.Bones.X=2;S.Normal=FVector4f(0,1,0,0);break;
   case 4:S.Weights=FVector4f(1,0,0,0);S.Bones.X=3;break;
   case 5:S.Weights=FVector4f(-.2f,0,0,0);break;
   case 6:S.Weights=FVector4f(1,0,0,0);S.Bones.X=2;S.Normal=FVector4f(.0007f,1,.00008f,0);break;
   default:break;
  }
  if(I%13==0){S.Normal=FVector4f(0,0,0,0);S.Tangent=FVector4f(0,0,0,S.Tangent.W);}
  Source.Add(S);
  for(uint32 Section=0;Section<3;++Section){
   FOKGpuDeformDraw D{};D.Binding=FUintVector4(uint32(I),Section,0,0);
   D.Offset=Section?FVector4f(.07f,-.11f,.03f,0):FVector4f(0,0,0,0);
   D.SectionNormal=FVector4f(.2f,.7f,-.3f,0);D.RestNormal=S.Normal;
   if(I%11==0)D.RestNormal=FVector4f(0,0,0,0);
   Draw.Add(D);
  }
 }
 FString Error;
 TArray<FMatrix44f> Matrices;
 for(uint32 B=0;B<PaletteCount;++B){FMatrix44f M=FMatrix44f::Identity;for(int32 Axis=0;Axis<3;++Axis)for(int32 Column=0;Column<4;++Column)M.M[Column][Axis]=Rows[B*3+Axis][Column];Matrices.Add(M);}
 TArray<FVector4f> PreparedRows;
 TestTrue(TEXT("Full affine rows prepared"),OKPrepareGpuDeformPalette(Matrices,PreparedRows,Error));
 TestTrue(TEXT("Column-vector upload preserves all affine coefficients"),PreparedRows==Rows);
 auto InvalidMatrices=Matrices;InvalidMatrices[0].M[0][3]=.01f;
 TestFalse(TEXT("Projective palette rejected"),OKPrepareGpuDeformPalette(InvalidMatrices,PreparedRows,Error));
 TestTrue(TEXT("Rejected palette preserves previous rows"),PreparedRows==Rows);
 if(!TestTrue(TEXT("Full-detail signed/fallback source validated"),OKValidateGpuDeformSource(Source,Draw,PaletteCount,Fallback,Error)))return false;
 auto InvalidSource=Source;InvalidSource[0].Weights.X=NAN;
 TestFalse(TEXT("Nonfinite weights rejected"),OKValidateGpuDeformSource(InvalidSource,Draw,PaletteCount,Fallback,Error));
 InvalidSource=Source;InvalidSource[0].Bones.X=PaletteCount;
 TestFalse(TEXT("Active out-of-range bone rejected"),OKValidateGpuDeformSource(InvalidSource,Draw,PaletteCount,Fallback,Error));
 auto InvalidDraw=Draw;InvalidDraw[0].Binding.X=uint32(Source.Num());
 TestFalse(TEXT("Invalid draw source rejected"),OKValidateGpuDeformSource(Source,InvalidDraw,PaletteCount,Fallback,Error));
 TestFalse(TEXT("Invalid fallback rejected"),OKValidateGpuDeformSource(Source,Draw,PaletteCount,PaletteCount,Error));

 TArray<FMatrix44f> Corrections;
 Corrections.Add(FMatrix44f::Identity);
 FMatrix44f Correction=FMatrix44f::Identity;Correction.M[0][0]=.7f;Correction.M[1][0]=.2f;Correction.M[2][1]=-.1f;Correction.M[3][0]=13;Corrections.Add(Correction);
 Correction=FMatrix44f::Identity;for(int32 Axis=0;Axis<3;++Axis)Correction.M[Axis][Axis]=0;Correction.M[3][2]=11;Corrections.Add(Correction);
 TArray<TSharedPtr<FRHIGPUBufferReadback,ESPMode::ThreadSafe>> Readbacks;
 const uint32 Bytes=uint32(Draw.Num()*sizeof(FOKGpuDeformOutput));
 bool Submitted=true;
 for(int32 Case=0;Case<Corrections.Num()*2;++Case){
  auto Readback=MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("RuntimeDeformOracle"));Readbacks.Add(Readback);
  const int32 CorrectionCase=Case%Corrections.Num();const bool NativeSpace=Case>=Corrections.Num();const FMatrix44f Post=Corrections[CorrectionCase];
 ENQUEUE_RENDER_COMMAND(RuntimeDeformOracleDispatch)([Source,Draw,Rows,Readback,Bytes,CorrectionCase,NativeSpace,Post,&Submitted](FRHICommandListImmediate& Commands){
  FRDGBuilder G(Commands);
  auto* S=CreateStructuredBuffer(G,TEXT("Deform.Source"),sizeof(FOKGpuDeformSource),Source.Num(),Source.GetData(),Source.Num()*sizeof(FOKGpuDeformSource));
  auto* D=CreateStructuredBuffer(G,TEXT("Deform.Draw"),sizeof(FOKGpuDeformDraw),Draw.Num(),Draw.GetData(),Draw.Num()*sizeof(FOKGpuDeformDraw));
  auto* P=CreateStructuredBuffer(G,TEXT("Deform.Palette"),sizeof(FVector4f),Rows.Num(),Rows.GetData(),Rows.Num()*sizeof(FVector4f));
  auto* O=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FOKGpuDeformOutput),Draw.Num()),TEXT("Deform.Output"));
  FMatrix44f InvalidPost=Post;InvalidPost.M[0][0]=NAN;
  Submitted&=!OKAddRuntimeGpuDeformPass(G,S,D,P,O,Draw.Num(),PaletteCount,Fallback,&InvalidPost);
  Submitted&=!OKAddRuntimeGpuDeformPass(G,S,D,P,O,Draw.Num()+1,PaletteCount,Fallback);
  const bool Added=OKAddRuntimeGpuDeformPass(G,S,D,P,O,Draw.Num(),PaletteCount,Fallback,CorrectionCase?&Post:nullptr,NativeSpace);Submitted&=Added;
  if(Added)AddEnqueueCopyPass(G,Readback.operator->(),O,Bytes);G.Execute();
 });}
 FlushRenderingCommands();
 if(!TestTrue(TEXT("All native correction stage passes submitted"),Submitted))return false;
 bool Ready=false;const double Start=FPlatformTime::Seconds();
 while(FPlatformTime::Seconds()-Start<30.){
  ENQUEUE_RENDER_COMMAND(RuntimeDeformOraclePoll)([Readbacks,&Ready](FRHICommandListImmediate&){Ready=true;for(const auto& Readback:Readbacks)Ready&=Readback->IsReady();});FlushRenderingCommands();
  if(Ready)break;FPlatformProcess::Sleep(.01f);
 }
 if(!TestTrue(TEXT("GPU output readback completes"),Ready))return false;
 TArray<TArray<FOKGpuDeformOutput>> Actual;Actual.SetNum(Readbacks.Num());
 ENQUEUE_RENDER_COMMAND(RuntimeDeformOracleRead)([Readbacks,Bytes,&Actual](FRHICommandListImmediate&){
  for(int32 Case=0;Case<Readbacks.Num();++Case){const auto& Readback=Readbacks[Case];const void* Data=Readback->Lock(Bytes);if(Data){Actual[Case].SetNumUninitialized(Bytes/sizeof(FOKGpuDeformOutput));FMemory::Memcpy(Actual[Case].GetData(),Data,Bytes);}Readback->Unlock();}
 });FlushRenderingCommands();
 for(const auto& Vertices:Actual)if(!TestEqual(TEXT("Every draw vertex generated"),Vertices.Num(),Draw.Num()))return false;

 // Independent scalar affine accumulation and the exact engine quaternion API,
 // rather than a CPU port of the shader's quaternion implementation.
 auto Transform=[&](uint32 B,FVector V,double W){
  FVector Out;for(int32 Axis=0;Axis<3;++Axis){const auto& R=Rows[B*3+Axis];Out[Axis]=R.X*V.X+R.Y*V.Y+R.Z*V.Z+R.W*W;}return Out;
 };
 auto NativeNormal=[](FVector V){const double Length=V.Size();return Length>1.e-8?V/Length:FVector(0,0,1);};
 double MaxPosition=0,MaxNativePosition=0,MaxNormal=0,MaxTangent=0;
 for(int32 Case=0;Case<Actual.Num();++Case){const int32 CorrectionCase=Case%Corrections.Num();const bool NativeSpace=Case>=Corrections.Num();for(int32 I=0;I<Draw.Num();++I){
  const auto& D=Draw[I];const auto& S=Source[D.Binding.X];
  const FVector Point(S.Position.X,S.Position.Y,S.Position.Z),Normal(S.Normal.X,S.Normal.Y,S.Normal.Z),Tangent(S.Tangent.X,S.Tangent.Y,S.Tangent.Z);
  FVector P=FVector::ZeroVector,N=FVector::ZeroVector,T=FVector::ZeroVector;double Total=0;
  for(int32 K=0;K<4;++K){const double W=S.Weights[K];const uint32 B=S.Bones[K];if(FMath::Abs(W)<1.e-6||B>=PaletteCount)continue;
   P+=Transform(B,Point,1)*W;N+=Transform(B,Normal,0)*W;T+=Transform(B,Tangent,0)*W;Total+=W;}
  if(Total>1.e-5){P/=Total;N/=Total;T/=Total;}else{P=Transform(Fallback,Point,1);N=Transform(Fallback,Normal,0);T=Transform(Fallback,Tangent,0);}
  N=NativeNormal(N);T=NativeNormal(T);
  if(CorrectionCase){const FMatrix Post(Corrections[CorrectionCase]);P=FVector(Post.TransformPosition(P));
   auto PostNormal=[&](FVector V){const FVector Direction=Post.TransformVector(V);const double L=Direction.Size();return L>1.e-8?Direction/L:V;};
   N=PostNormal(N);T=PostNormal(T);}
  if(NativeSpace){P=FVector(-P.X,P.Z,P.Y)*100.;N=FVector(-N.X,N.Z,N.Y).GetSafeNormal();T=FVector(-T.X,T.Z,T.Y).GetSafeNormal();}
  const FVector Rest=FVector(D.RestNormal.X,D.RestNormal.Y,D.RestNormal.Z).GetSafeNormal(),Current=N.GetSafeNormal();
  const FQuat Q=Rest.IsNearlyZero()||Current.IsNearlyZero()?FQuat::Identity:FQuat::FindBetweenNormals(Rest,Current);
  P+=Q.RotateVector(FVector(D.Offset.X,D.Offset.Y,D.Offset.Z));
  if(D.Binding.Y!=0)N=Q.RotateVector(FVector(D.SectionNormal.X,D.SectionNormal.Y,D.SectionNormal.Z));
  const auto& A=Actual[Case][I];
  const double PE=(P-FVector(A.Position.X,A.Position.Y,A.Position.Z)).Size(),NE=(N-FVector(A.Normal.X,A.Normal.Y,A.Normal.Z)).Size(),TE=(T-FVector(A.Tangent.X,A.Tangent.Y,A.Tangent.Z)).Size();
  if(!FMath::IsFinite(PE)||!FMath::IsFinite(NE)||!FMath::IsFinite(TE)){AddError(TEXT("Nonfinite GPU output"));return false;}
  if(NativeSpace)MaxNativePosition=FMath::Max(MaxNativePosition,PE);else MaxPosition=FMath::Max(MaxPosition,PE);
  MaxNormal=FMath::Max(MaxNormal,NE);MaxTangent=FMath::Max(MaxTangent,TE);
  if(A.Tangent.W!=S.Tangent.W||A.Position.W!=1||A.Normal.W!=0){AddError(TEXT("GPU output stream metadata changed"));return false;}
 }}
 AddInfo(FString::Printf(TEXT("Actual GPU deform sources=%d draws=%d correction_cases=%d max_position_cm=%.9g max_native_position_cm=%.9g max_normal=%.9g max_tangent=%.9g"),Source.Num(),Draw.Num(),Actual.Num(),MaxPosition,MaxNativePosition,MaxNormal,MaxTangent));
 TestTrue(TEXT("GPU positions match affine plus engine normal-motion oracle"),MaxPosition<=.0001);
 TestTrue(TEXT("Native GPU positions match meter-to-cm conversion oracle"),MaxNativePosition<=.001);
 TestTrue(TEXT("GPU section normals match engine rotation oracle"),MaxNormal<=.0001);
 TestTrue(TEXT("GPU tangents match affine oracle"),MaxTangent<=.0001);
 return true;
}
#endif
