#if WITH_DEV_AUTOMATION_TESTS
#include "OKResidentGpuDeform.h"
#include "Misc/AutomationTest.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "DynamicMeshBuilder.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "HAL/PlatformProcess.h"

static bool WaitDeformReadbacks(const TArray<TSharedPtr<FRHIGPUBufferReadback,ESPMode::ThreadSafe>>& Readbacks){
 const double Deadline=FPlatformTime::Seconds()+30;
 while(FPlatformTime::Seconds()<Deadline){bool Ready=true;
  ENQUEUE_RENDER_COMMAND(CheckDeformReadbacks)([&](FRHICommandListImmediate&){for(const auto& R:Readbacks)Ready&=R->IsReady();});
  FlushRenderingCommands();if(Ready)return true;FPlatformProcess::Sleep(.01f);
 }return false;
}
static FRDGBufferRef DeformVertexBuffer(FRDGBuilder& G,uint32 Bytes,const TCHAR* Name){
 auto Desc=FRDGBufferDesc::CreateByteAddressDesc(Bytes);Desc.Usage|=EBufferUsageFlags::VertexBuffer;return G.CreateBuffer(Desc,Name);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKGpuVertexPackTest,"OpenKoikatsu.GPU.VertexPack",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKGpuVertexPackTest::RunTest(const FString&){
 if(!TestTrue(TEXT("Actual vertex packing shader available"),OKGpuPackAvailable()))return false;
 TArray<FOKGpuDeformOutput> Vertices;
 for(int32 I=0;I<259;++I){
  FOKGpuDeformOutput V;V.Position=FVector4f(I*.13f,-3.7f,I*.017f,1);
  V.Normal=FVector4f(FMath::Sin(I*.27f),FMath::Cos(I*.31f),FMath::Sin(I*.43f),0);
  V.Tangent=FVector4f(FMath::Cos(I*.19f),FMath::Sin(I*.41f),FMath::Cos(I*.61f),I%2?-1:1);
  if(I%13==0){V.Normal=FVector4f(0,0,1,0);V.Tangent=FVector4f(0,0,1,-1);}
  if(I%13==1){V.Normal=FVector4f(0,0,0,0);V.Tangent=FVector4f(0,0,0,-1);}
  if(I%13==2){V.Normal=FVector4f(-1.5f/127,2.5f/127,-.5f/127,0);V.Tangent=FVector4f(.5f/127,-2.5f/127,1.5f/127,-1);}
  if(I%13==3){V.Normal=FVector4f(-1.5f,1.5f,0,0);V.Tangent=FVector4f(1.5f,-1.5f,0,-1);}
  if(I%13==4){V.Normal=FVector4f(1.f/127,3.f/127,0,0);V.Tangent=FVector4f(3.f/127,9.f/127,0,-1);}
  Vertices.Add(V);
 }
 // Quantized parallel bases across all small signed 3D integer directions.
 // Float unpacking may break integer collinearity; the UE oracle decides sign.
 for(int32 X=-8;X<=8;++X)for(int32 Y=-8;Y<=8;++Y)for(int32 Z=-8;Z<=8;++Z){
  if(!X&&!Y&&!Z)continue;
  for(int32 Multiple=1;Multiple<=4;++Multiple){FOKGpuDeformOutput V;
   V.Position=FVector4f(X,Y,Z,1);V.Normal=FVector4f(X/127.f,Y/127.f,Z/127.f,0);
   V.Tangent=FVector4f(X*Multiple/127.f,Y*Multiple/127.f,Z*Multiple/127.f,-1);Vertices.Add(V);
  }
 }
 TArray<TSharedPtr<FRHIGPUBufferReadback,ESPMode::ThreadSafe>> Readbacks;
 Readbacks.Add(MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("PackPositions")));
 Readbacks.Add(MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("PackTangents")));bool Submitted=false,Rejected=false;
 ENQUEUE_RENDER_COMMAND(VertexPackOracle)([&](FRHICommandListImmediate& C){
  FRDGBuilder G(C);auto* Input=CreateStructuredBuffer(G,TEXT("Pack.OracleInput"),Vertices);
  auto* P=DeformVertexBuffer(G,Vertices.Num()*12,TEXT("Pack.Positions"));auto* T=DeformVertexBuffer(G,Vertices.Num()*8,TEXT("Pack.Tangents"));
  Rejected=!OKAddRuntimeGpuPackPass(G,Input,P,T,Vertices.Num()+1)&&!OKAddRuntimeGpuPackPass(G,Input,P,P,Vertices.Num());
  Submitted=OKAddRuntimeGpuPackPass(G,Input,P,T,Vertices.Num());
  if(Submitted){AddEnqueueCopyPass(G,Readbacks[0].Get(),P,Vertices.Num()*12);AddEnqueueCopyPass(G,Readbacks[1].Get(),T,Vertices.Num()*8);}G.Execute();
 });FlushRenderingCommands();
 if(!TestTrue(TEXT("GPU pack submitted and invalid capacity/alias rejected"),Submitted&&Rejected)||!TestTrue(TEXT("GPU pack readbacks ready"),WaitDeformReadbacks(Readbacks)))return false;
 TArray<FVector3f> Positions;Positions.SetNumUninitialized(Vertices.Num());
 using FPacked=TStaticMeshVertexTangentDatum<FPackedNormal>;static_assert(sizeof(FPacked)==8);
 TArray<FPacked> Tangents;Tangents.SetNumUninitialized(Vertices.Num());
 ENQUEUE_RENDER_COMMAND(ReadPackOracle)([&](FRHICommandListImmediate&){
  FMemory::Memcpy(Positions.GetData(),Readbacks[0]->Lock(Positions.Num()*12),Positions.Num()*12);Readbacks[0]->Unlock();
  FMemory::Memcpy(Tangents.GetData(),Readbacks[1]->Lock(Tangents.Num()*8),Tangents.Num()*8);Readbacks[1]->Unlock();
 });FlushRenderingCommands();
 int32 Mismatches=0;
 for(int32 I=0;I<Vertices.Num();++I){const auto& V=Vertices[I];const FVector3f ExpectedPosition(V.Position.X,V.Position.Y,V.Position.Z);
  TestTrue(TEXT("Position stream preserves float bits"),FMemory::Memcmp(&Positions[I],&ExpectedPosition,12)==0);
  FDynamicMeshVertex Legacy;Legacy.TangentX=FVector3f(V.Tangent.X,V.Tangent.Y,V.Tangent.Z);Legacy.TangentZ=FVector3f(V.Normal.X,V.Normal.Y,V.Normal.Z);Legacy.TangentZ.Vector.W=V.Tangent.W<0?-127:127;
  FPacked Expected;Expected.SetTangents(Legacy.TangentX.ToFVector3f(),Legacy.GetTangentY(),Legacy.TangentZ.ToFVector3f());
  if(FMemory::Memcmp(&Tangents[I],&Expected,8)){++Mismatches;AddError(FString::Printf(TEXT("Packed tangent mismatch vertex=%d GPU=%08x/%08x CPU=%08x/%08x"),I,Tangents[I].TangentX.Vector.Packed,Tangents[I].TangentZ.Vector.Packed,Expected.TangentX.Vector.Packed,Expected.TangentZ.Vector.Packed));}
 }
 AddInfo(FString::Printf(TEXT("Actual GPU LocalVertexFactory packing vertices=%d byte_mismatches=%d"),Vertices.Num(),Mismatches));return Mismatches==0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKFusedDeformTest,"OpenKoikatsu.GPU.FusedDeform",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKFusedDeformTest::RunTest(const FString&){
 if(!TestTrue(TEXT("Fused shader available"),OKGpuDeformPackedAvailable()))return false;
 TArray<FOKGpuDeformSource> Source;TArray<FOKGpuDeformDraw> Draw;
 for(int32 I=0;I<259;++I){
  FOKGpuDeformSource S;S.Position=FVector4f(I*.013f,-.17f,I*.007f,1);
  S.Normal=FVector4f(FMath::Sin(I*.27f),FMath::Cos(I*.31f),FMath::Sin(I*.43f),0);
  S.Tangent=FVector4f(FMath::Cos(I*.19f),FMath::Sin(I*.41f),FMath::Cos(I*.61f),I%2?-1:1);
  S.Bones=FUintVector4(0,1,2,3);S.Weights=I%7?FVector4f(.7f,-.2f,.3f,.2f):FVector4f(0,0,0,0);
  if(I%13==0){S.Normal=FVector4f(0,0,1,0);S.Tangent=FVector4f(0,0,1,-1);}
  if(I%13==1){S.Normal=FVector4f(0,0,0,0);S.Tangent=FVector4f(0,0,0,-1);}
  Source.Add(S);FOKGpuDeformDraw D;D.Binding=FUintVector4(I,I%3==0?1:0,0,0);D.Offset=FVector4f(.03f,-.02f,.01f,0);D.RestNormal=FVector4f(0,0,-1,0);Draw.Add(D);
 }
 TArray<FMatrix44f> Palette;Palette.Init(FMatrix44f::Identity,4);Palette[0].M[0][0]=-1.7f;Palette[0].M[1][1]=.23f;Palette[0].M[3][0]=.2f;
 Palette[1].M[2][2]=0;Palette[1].M[1][0]=.6f;Palette[2].M[0][0]=.01f;Palette[2].M[1][1]=3.1f;
 TArray<FVector4f> Rows;FString Error;if(!OKPrepareGpuDeformPalette(Palette,Rows,Error))return false;
 for(int32 Mode=0;Mode<4;++Mode){
  FMatrix44f Post=FMatrix44f::Identity;if(Mode%2){Post.M[0][0]=0;Post.M[1][1]=0;Post.M[2][2]=0;Post.M[3][2]=.13f;}
  TArray<TSharedPtr<FRHIGPUBufferReadback,ESPMode::ThreadSafe>> Readbacks;
  for(int32 I=0;I<4;++I)Readbacks.Add(MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("FusedOracle")));
  bool Submitted=false,Rejected=false;
  ENQUEUE_RENDER_COMMAND(FusedDeformOracle)([&](FRHICommandListImmediate& C){
   FRDGBuilder G(C);auto* S=CreateStructuredBuffer(G,TEXT("Fused.Source"),Source);auto* D=CreateStructuredBuffer(G,TEXT("Fused.Draw"),Draw);auto* P=CreateStructuredBuffer(G,TEXT("Fused.Palette"),Rows);
   auto* V=G.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FOKGpuDeformOutput),Draw.Num()),TEXT("Fused.LegacyOutput"));
   FRDGBufferRef Outputs[4];for(int32 I=0;I<4;++I)Outputs[I]=DeformVertexBuffer(G,Draw.Num()*(I%2?8:12),TEXT("Fused.Stream"));
   Rejected=!OKAddRuntimeGpuDeformPackedPass(G,S,D,P,Outputs[0],Outputs[0],Draw.Num(),Palette.Num(),3)
    &&!OKAddRuntimeGpuDeformPackedPass(G,S,D,P,Outputs[2],Outputs[3],Draw.Num()+1,Palette.Num(),3);
   Submitted=OKAddRuntimeGpuDeformPass(G,S,D,P,V,Draw.Num(),Palette.Num(),3,&Post,Mode>=2)
    &&OKAddRuntimeGpuPackPass(G,V,Outputs[0],Outputs[1],Draw.Num())
    &&OKAddRuntimeGpuDeformPackedPass(G,S,D,P,Outputs[2],Outputs[3],Draw.Num(),Palette.Num(),3,&Post,Mode>=2);
   if(Submitted)for(int32 I=0;I<4;++I)AddEnqueueCopyPass(G,Readbacks[I].Get(),Outputs[I],Draw.Num()*(I%2?8:12));G.Execute();
  });FlushRenderingCommands();
  if(!TestTrue(TEXT("Legacy/fused submitted, invalid alias/capacity rejected"),Submitted&&Rejected)||!TestTrue(TEXT("Fused readbacks ready"),WaitDeformReadbacks(Readbacks)))return false;
  bool PositionEqual=false,TangentEqual=false;
  ENQUEUE_RENDER_COMMAND(CompareFusedDeform)([&](FRHICommandListImmediate&){
   const uint32 PositionBytes=Draw.Num()*12,TangentBytes=Draw.Num()*8;
   const void* LegacyP=Readbacks[0]->Lock(PositionBytes);const void* LegacyT=Readbacks[1]->Lock(TangentBytes);const void* FusedP=Readbacks[2]->Lock(PositionBytes);const void* FusedT=Readbacks[3]->Lock(TangentBytes);
   PositionEqual=FMemory::Memcmp(LegacyP,FusedP,PositionBytes)==0;TangentEqual=FMemory::Memcmp(LegacyT,FusedT,TangentBytes)==0;
   for(auto& R:Readbacks)R->Unlock();
  });FlushRenderingCommands();
  TestTrue(FString::Printf(TEXT("Fused positions byte-identical mode %d"),Mode),PositionEqual);TestTrue(FString::Printf(TEXT("Fused packed tangents byte-identical mode %d"),Mode),TangentEqual);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKResidentDeformTest,"OpenKoikatsu.GPU.ResidentDeform",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKResidentDeformTest::RunTest(const FString&){
 TArray<FOKGpuDeformSource> Source;TArray<FOKGpuDeformDraw> Draw;
 for(int32 I=0;I<129;++I){FOKGpuDeformSource V;V.Position=FVector4f(I*.125f,-2,I*.25f,1);V.Weights=FVector4f(1,0,0,0);Source.Add(V);FOKGpuDeformDraw D;D.Binding.X=I;Draw.Add(D);}
 TArray<FMatrix44f> Palette;Palette.Add(FMatrix44f::Identity);
 TArray<TSharedPtr<FRHIGPUBufferReadback,ESPMode::ThreadSafe>> Readbacks;
 for(int32 I=0;I<3;++I)Readbacks.Add(MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("ResidentDeformPositions")));
 bool Passed=true;FOKResidentDeformStats Stats;
 ENQUEUE_RENDER_COMMAND(ResidentDeformOracle)([&](FRHICommandListImmediate& C){
  FOKResidentGpuDeform Resident;FString Error;TRefCountPtr<FRDGPooledBuffer> Previous;auto UploadedSource=Source;
  for(int32 Frame=0;Frame<3;++Frame){FRDGBuilder G(C);FOKResidentDeformFrame Output;
   Palette[0].M[3][0]=Frame*7;
   const bool Ok=Frame==0?Resident.Install(G,UploadedSource,Draw,Palette,0,nullptr,0,Output,Error):Resident.Update(G,Palette,nullptr,1,Output,Error);
   Passed&=Ok;if(!Ok){G.Execute();continue;}
   Passed&=Output.Generation==1&&Output.DrawCount==129;
   Passed&=(Frame==0?Output.PreviousPositions==Output.Positions:Output.PreviousPositions!=Output.Positions);
   if(Frame)Passed&=Output.PreviousPositions==G.RegisterExternalBuffer(Previous);
   AddEnqueueCopyPass(G,Readbacks[Frame].Get(),Output.Positions,Source.Num()*12);
   FOKResidentDeformFrame Sentinel=Output;auto Invalid=Palette;Invalid[0].M[0][0]=NAN;
   Passed&=!Resident.Update(G,Invalid,nullptr,1,Sentinel,Error)&&Sentinel.Positions==Output.Positions;
   Passed&=!Resident.Update(G,Palette,nullptr,0,Sentinel,Error);
   auto WrongTopology=Palette;WrongTopology.Add(FMatrix44f::Identity);
   Passed&=!Resident.Update(G,WrongTopology,nullptr,1,Sentinel,Error)&&Sentinel.Positions==Output.Positions;
   auto InvalidDraw=Draw;InvalidDraw[0].Binding.X=Source.Num();
   Passed&=!Resident.Install(G,Source,InvalidDraw,Palette,0,nullptr,1,Sentinel,Error)&&Sentinel.Positions==Output.Positions;
   Passed&=!Resident.Install(G,Source,Draw,Palette,0,nullptr,1,Sentinel,Error,1);
   FOKResidentDeformFrame Current;Passed&=Resident.GetCurrent(G,1,Current)&&Current.Positions==Output.Positions;
   Previous=G.ConvertToExternalBuffer(Output.Positions);G.Execute();if(Frame==0)UploadedSource[0].Position.X+=1000;
  }
  Stats=Resident.GetStats();Passed&=Stats.Installations==1&&Stats.Evaluations==3&&Stats.ImmutableUploadBytes==uint64(Source.Num())*80+uint64(Draw.Num())*64&&Stats.PaletteUploadBytes==144;
  {FRDGBuilder G(C);FOKResidentDeformFrame Output;Passed&=Resident.Install(G,Source,Draw,Palette,0,nullptr,1,Output,Error);Passed&=Output.Generation==2&&Output.PreviousPositions==Output.Positions;
   Passed&=!Resident.Update(G,Palette,nullptr,1,Output,Error);G.Execute();}
  Resident.Release();{FRDGBuilder G(C);FOKResidentDeformFrame Output;Passed&=!Resident.GetCurrent(G,3,Output)&&!Resident.Update(G,Palette,nullptr,2,Output,Error);G.Execute();}
 });FlushRenderingCommands();
 if(!TestTrue(TEXT("Resident generation, ownership, failure retention and buffer history"),Passed)||!TestTrue(TEXT("Resident GPU readbacks ready"),WaitDeformReadbacks(Readbacks)))return false;
 float MaxError=0;
 ENQUEUE_RENDER_COMMAND(ReadResidentDeform)([&](FRHICommandListImmediate&){
  for(int32 Frame=0;Frame<3;++Frame){const auto* Actual=static_cast<const FVector3f*>(Readbacks[Frame]->Lock(Source.Num()*12));
   for(int32 I=0;I<Source.Num();++I){const FVector3f Expected(Source[I].Position.X+Frame*7,Source[I].Position.Y,Source[I].Position.Z);MaxError=FMath::Max(MaxError,(Actual[I]-Expected).GetAbsMax());}Readbacks[Frame]->Unlock();}
 });FlushRenderingCommands();
 TestTrue(TEXT("GPU output updates across three separately executed graphs"),MaxError<=1.e-5f);
 AddInfo(FString::Printf(TEXT("Actual resident GPU deform frames=3 vertices=129 immutable_bytes=%llu palette_bytes=%llu max_error_cm=%.9g"),Stats.ImmutableUploadBytes,Stats.PaletteUploadBytes,MaxError));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKNativeResidentDeformTest,"OpenKoikatsu.GPU.NativeResidentDeform",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKNativeResidentDeformTest::RunTest(const FString&){
 FOKGpuDeformSource S;S.Position=FVector4f(1,2,3,1);S.Normal=FVector4f(0,0,0,0);S.Tangent=FVector4f(0,0,0,1);S.Weights.X=1;
 FOKGpuDeformDraw D;TArray<FMatrix44f> Palette;Palette.Add(FMatrix44f::Identity);
 FMatrix44f Post=FMatrix44f::Identity;Post.M[1][1]=Post.M[2][2]=0;Post.M[3][0]=4;Post.M[3][1]=5;Post.M[3][2]=6;
 TArray<TSharedPtr<FRHIGPUBufferReadback,ESPMode::ThreadSafe>> Readbacks;
 for(int32 I=0;I<6;++I)Readbacks.Add(MakeShared<FRHIGPUBufferReadback,ESPMode::ThreadSafe>(TEXT("NativeResidentStreams")));
 bool Passed=true;FOKResidentDeformStats Stats;
 ENQUEUE_RENDER_COMMAND(NativeResidentOracle)([&](FRHICommandListImmediate& C){FOKResidentGpuDeform Resident;FString Error;
  for(int32 Frame=0;Frame<3;++Frame){FRDGBuilder G(C);FOKResidentDeformFrame Output;Palette[0].M[3][0]=Frame*.125f;
   const bool Ok=Frame?Resident.Update(G,Palette,&Post,1,Output,Error):Resident.Install(G,MakeArrayView(&S,1),MakeArrayView(&D,1),Palette,0,&Post,0,Output,Error,512ull*1024*1024,true);
   Passed&=Ok;if(Ok){AddEnqueueCopyPass(G,Readbacks[Frame*2].Get(),Output.Positions,12);AddEnqueueCopyPass(G,Readbacks[Frame*2+1].Get(),Output.Tangents,8);}G.Execute();
  }Stats=Resident.GetStats();
 });FlushRenderingCommands();
 if(!TestTrue(TEXT("Native resident install/update submitted"),Passed)||!TestTrue(TEXT("Native resident GPU buffers ready"),WaitDeformReadbacks(Readbacks)))return false;
 ENQUEUE_RENDER_COMMAND(ReadNativeResident)([&](FRHICommandListImmediate&){for(int32 Frame=0;Frame<3;++Frame){
  const auto* Position=static_cast<const FVector3f*>(Readbacks[Frame*2]->Lock(12));Passed&=*Position==FVector3f(-500-Frame*12.5f,600,500);Readbacks[Frame*2]->Unlock();
  // Native zero-direction fallback is Z. Collapsed post retains Z; final
  // basis must map it to UE Y, across both resident palette-only updates.
  using FPacked=TStaticMeshVertexTangentDatum<FPackedNormal>;const auto* Tangent=static_cast<const FPacked*>(Readbacks[Frame*2+1]->Lock(8));
  FDynamicMeshVertex Reference;Reference.SetTangents(FVector3f(0,1,0),FVector3f::ZeroVector,FVector3f(0,1,0));FPacked Expected;Expected.SetTangents(Reference.TangentX.ToFVector3f(),Reference.GetTangentY(),Reference.TangentZ.ToFVector3f());
  Passed&=FMemory::Memcmp(Tangent,&Expected,8)==0;Readbacks[Frame*2+1]->Unlock();
 }});FlushRenderingCommands();
 TestTrue(TEXT("Resident retains native space and collapsed direction semantics across frames"),Passed);
 TestTrue(TEXT("Native resident immutable data uploaded once"),Stats.Installations==1&&Stats.Evaluations==3&&Stats.ImmutableUploadBytes==144&&Stats.PaletteUploadBytes==144);
 AddInfo(TEXT("Native resident frames=3 position exact, direction bytes exact, immutable144 palette144"));return !HasAnyErrors();
}
#endif
