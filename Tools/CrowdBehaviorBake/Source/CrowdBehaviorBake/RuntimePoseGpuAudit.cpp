#include "KKRuntimePoseBank.h"
#include "OKGpuAnimationManifest.h"
#include "OKResidentGpuPose.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"

bool AuditRuntimePoseOnGpu(const FKKRuntimePoseRequest& Request,const FKKRuntimePoseResult& Result,double& MaxError,FString& Error){
 if(!FParse::Param(FCommandLine::Get(),TEXT("AllowCommandletRendering"))){Error=TEXT("GPU audit requires AllowCommandletRendering");return false;}
 if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
 if(!Result.Passed())return false;FOKGpuActorRecord Actor;Actor.CharacterId=Result.CharacterIdentity;Actor.PoseCount=Request.Bones.Num();
 FOKGpuSkinPaletteData Palette;Palette.PoseCount=Actor.PoseCount;
 for(int32 B=0;B<Request.Bones.Num();++B){const auto& Bone=Request.Bones[B];Actor.PoseBones.Add({Bone.Name.ToString(),Bone.Parent,OKGpuBoneReferenceIdentity(Bone.Reference)});const uint32 Bind=Palette.InverseBindRows.Num();Palette.InverseBindRows.Append({FVector4f(1,0,0,0),FVector4f(0,1,0,0),FVector4f(0,0,1,0)});Palette.Items.Add(FUintVector4(B,Bind,B,0));}
 FOKGpuMeshRecord Mesh;Mesh.PartId=TEXT("runtime-pose-diagnostic");Mesh.AssetPath=TEXT("/Game/RuntimePoseDiagnostic/PrivateMesh");Mesh.Bones={0,Actor.PoseCount};Actor.Meshes.Add(MoveTemp(Mesh));
 FOKRuntimeGpuClip Clip;Clip.Id=Result.ClipIdentity;Clip.Duration=Result.Duration;Clip.Keys=Result.Keys;Clip.Tracks=Result.Tracks;
 FOKGpuAnimationBundle Bank;if(!OKBuildRuntimeGpuActorBank(MoveTemp(Actor),{MoveTemp(Clip)},MoveTemp(Palette),Bank,Error))return false;
 FOKResidentGpuPose Pose;FOKResidentPoseStats Stats;TArray<TArray<uint32>> Owned=Bank.Data.OwnedClips;TArray<FOKGpuPosePlayback> Playback;Playback.AddDefaulted();Playback[0].ClipB=0;MaxError=0;bool Passed=true;
 uint64 Frame=0;for(float Phase:{0.f,.137f,.37f,.731f,.99f,1.f}){
  const float Time=Phase*Result.Duration;Playback[0].Times=FVector4f(Time,Time,0,0);TArray<FVector4f> Current,Previous;
  if(!OKReadbackResidentGpuPose(Pose,Frame==0?&Bank.Data.Pose:nullptr,Owned,Playback,Frame,false,Current,Previous,Stats,Error)){Passed=false;break;}
  if(Current.Num()!=Request.Bones.Num()*3){Error=TEXT("Runtime GPU pose row count differs");Passed=false;break;}
  ++Frame;
  TArray<FTransform> World;World.SetNum(Request.Bones.Num());
  for(int32 B=0;B<World.Num();++B){const auto& Track=Result.Tracks[B];uint32 Lo=0,Hi=Track.Y-1;
   while(Hi-Lo>1){const uint32 Mid=(Lo+Hi)/2;if(Result.Keys[(Track.X+Mid)*3].W<=Time)Lo=Mid;else Hi=Mid;}
   const auto* A=&Result.Keys[(Track.X+Lo)*3];const auto* Z=&Result.Keys[(Track.X+Hi)*3];const float Alpha=Hi==Lo?0.f:FMath::Clamp((Time-A[0].W)/(Z[0].W-A[0].W),0.f,1.f);
   const FVector P=FMath::Lerp(FVector(A[0].X,A[0].Y,A[0].Z),FVector(Z[0].X,Z[0].Y,Z[0].Z),Alpha),S=FMath::Lerp(FVector(A[2].X,A[2].Y,A[2].Z),FVector(Z[2].X,Z[2].Y,Z[2].Z),Alpha);
   const FQuat Q=FQuat::FastLerp(FQuat(A[1].X,A[1].Y,A[1].Z,A[1].W),FQuat(Z[1].X,Z[1].Y,Z[1].Z,Z[1].W),Alpha).GetNormalized();
   World[B]=FTransform(Q,P,S);const int32 Parent=Request.Bones[B].Parent;if(Parent>=0)World[B]=World[B]*World[Parent];
   const FMatrix M=World[B].ToMatrixWithScale();for(int32 R=0;R<3;++R)for(int32 C=0;C<4;++C){const double Actual=Current[B*3+R][C];if(!FMath::IsFinite(Actual)){Passed=false;Error=TEXT("Runtime GPU pose is nonfinite");}MaxError=FMath::Max(MaxError,FMath::Abs(Actual-M.M[C][R]));}
  }
 }
 ENQUEUE_RENDER_COMMAND(ReleaseRuntimePoseAudit)([&](FRHICommandListImmediate&){Pose.Release();});FlushRenderingCommands();
 if(MaxError>.001){Error=TEXT("Runtime GPU component matrix error exceeds 0.001");Passed=false;}return Passed;
}
