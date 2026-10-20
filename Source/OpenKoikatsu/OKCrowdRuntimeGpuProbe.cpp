#include "KKRuntimePoseBankTypes.h"
#include "KKRuntimeMesh.h"
#include "KKRuntimeRenderMesh.h"
#include "KKRuntimeMeshReduction.h"
#include "KKRuntimeMaterial.h"
#include "KKRuntimeSharedMaterial.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/VolumeTexture.h"
#include "Engine/TextureCube.h"
#include "TextureResource.h"
#include "Json.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "GPUSkinVertexFactory.h"
#include "UObject/StrongObjectPtr.h"
#include "OKGpuAnimationManifest.h"
#include "OKRuntimeGpuPlayback.h"
#include "RenderingThread.h"
#include "RenderGraphBuilder.h"
#include "OKGpuResidencyBudget.h"
#include "OKSourceRasterState.h"
#include "OKSourceMaterialMesh.h"
#include "Materials/MaterialRenderProxy.h"
#include "HAL/PlatformProcess.h"
#include "Algo/Reverse.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
namespace {
bool SharedMaterialBindingOracle(FString& Error){
 FKKRuntimeMaterialPart P;P.ShaderKind=2;
 P.SourceMetadataJson=TEXT("{\"schema\":1,\"material\":{\"floats\":{\"_alpha_a\":0,\"_MainTex_ST_x\":2,\"_MainTex_ST_y\":3,\"_MainTex_ST_z\":0.25,\"_MainTex_ST_w\":0.75},\"colors\":{\"_ShadowColor\":[0.2,0.4,0.6,0.8]}}}");
 FKKRuntimeMaterialScene Scene;FKKRuntimeMaterialBinding B;
 if(!OKPrepareRuntimeForgeOpaqueBinding(P,Scene,B,Error)||B.Scalars.FindRef(TEXT("_alpha_a"))!=0||B.Vectors.FindRef(TEXT("_MainTex_ST"))!=FLinearColor(2,3,.25,.75)||B.Scalars.FindRef(TEXT("_MainTex_ST__A"))!=.75f||B.Vectors.FindRef(TEXT("_ShadowColor"))!=FLinearColor(.2,.4,.6,.8)||B.Scalars.FindRef(TEXT("_ShadowColor__A"))!=.8f){Error=TEXT("Shared material authored numeric/ST/alpha oracle failed");return false;}
 const auto Before=B.Vectors.FindRef(TEXT("_MainTex_ST"));P.ShaderKind=0;
 if(OKPrepareRuntimeForgeOpaqueBinding(P,Scene,B,Error)||B.Vectors.FindRef(TEXT("_MainTex_ST"))!=Before){Error=TEXT("Unsupported material altered binding output");return false;}
 P.ShaderKind=2;P.Textures.Add({TEXT("_MainTex"),-1});
 if(OKPrepareRuntimeForgeOpaqueBinding(P,Scene,B,Error)||B.Vectors.FindRef(TEXT("_MainTex_ST"))!=Before){Error=TEXT("Invalid material slot altered binding output");return false;}
 P.Textures.Reset();for(const TCHAR* Invalid:{TEXT("{}"),TEXT("{\"schema\":1}"),TEXT("{\"schema\":1,\"material\":{\"colors\":{\"_ShadowColor\":[0,1]}}}")}){P.SourceMetadataJson=Invalid;if(OKPrepareRuntimeForgeOpaqueBinding(P,Scene,B,Error)||B.Vectors.FindRef(TEXT("_MainTex_ST"))!=Before){Error=TEXT("Malformed material metadata altered binding output");return false;}}
 Error.Reset();return true;
}
bool TextureMipOracle(FString& Error){
 TArray<uint8> Pixels={0,0,0,0,255,255,255,255,0,0,0,0,255,255,255,255};FKKRuntimeTextureSettings S;S.EncodedMips=true;TArray<FKKRuntimeTextureMip> M;
 if(!OKBuildRuntimeTextureMips(Pixels,2,2,S,M,Error)||M.Num()!=2||M[1].BGRA!=TArray<uint8>({127,127,127,127})){Error=TEXT("Native encoded mip alpha floor changed");return false;}
 S.EncodedMips=false;if(!OKBuildRuntimeTextureMips(Pixels,2,2,S,M,Error)||M[1].BGRA[0]<187||M[1].BGRA[0]>188||M[1].BGRA[3]!=127){Error=TEXT("Runtime sRGB/alpha filtering oracle failed");return false;}
 TArray<uint8> Odd;Odd.Init(64,3*5*4);S.SRGB=false;if(!OKBuildRuntimeTextureMips(Odd,3,5,S,M,Error)||M.Last().BGRA!=TArray<uint8>({64,64,64,64})){Error=TEXT("Odd/1xN runtime mip oracle failed");return false;}
 const auto Before=M.Last().BGRA;if(OKBuildRuntimeTextureMips(Odd,0,5,S,M,Error)||M.Last().BGRA!=Before){Error=TEXT("Bad pixel request modified mip output");return false;}
 Error.Reset();return true;
}
bool RenderPartitionOracle(FString& Error){
 TArray<FKKRuntimePoseBone> Bones;for(int32 I=0;I<6;++I)Bones.Add({FName(*FString::Printf(TEXT("fixture-%d"),I)),I?0:INDEX_NONE,FTransform::Identity});
 FKKRuntimeMeshPart Part;Part.NativePart=0;Part.Indices={0,1,2,3,4,5};
 for(int32 I=0;I<6;++I){FKKRuntimeMeshVertex V;V.Position=FVector3f(1,2,3);V.Normal=FVector3f(0,0,1);V.Influences=3;for(int32 K=0;K<3;++K)V.Skin[K]={uint16((I<3?0:3)+K),21845};Part.Vertices.Add(V);}
 TArray<FKKRuntimeMeshPart> Meshes{Part};auto F=OKPrepareRuntimeRenderMeshAsync(Meshes,Bones,3);auto D=F.Consume();
 if(!D.Error.IsEmpty()||!D.WorkerThread||D.Sections.Num()!=2||D.Vertices.Num()!=6||D.Indices.Num()!=6||D.Sections[0].BoneMap.Num()!=3||D.Sections[1].BaseVertex!=3){Error=TEXT("Runtime section partition/welding oracle failed");return false;}
 for(int32 S=0;S<2;++S)for(uint32 I=D.Sections[S].BaseVertex;I<D.Sections[S].BaseVertex+D.Sections[S].NumVertices;++I)for(int32 K=0;K<3;++K)if(D.Sections[S].BoneMap[D.Weights[I].InfluenceBones[K]]!=D.Vertices[I].Skin[K].Bone){Error=TEXT("Runtime partition local/global bone map differs");return false;}
 auto Small=OKPrepareRuntimeRenderMeshAsync(Meshes,Bones,3,1);auto Rejected=Small.Consume();if(Rejected.Error.IsEmpty()||!Rejected.Vertices.IsEmpty()){Error=TEXT("Runtime mesh preallocation gate failed");return false;}
 Meshes[0].Vertices[1].Skin[2].Bone=3;auto Bad=OKPrepareRuntimeRenderMeshAsync(Meshes,Bones,3);auto Invalid=Bad.Consume();if(Invalid.Error.IsEmpty()||!Invalid.Indices.IsEmpty()){Error=TEXT("Runtime triangle bone limit gate failed");return false;}
 Error.Reset();return true;
}

bool ReductionOracle(FString& Error){
 TArray<FKKRuntimeMeshPart> Parts;FKKRuntimeMeshPart Grid;Grid.NativePart=0;Grid.Role=TEXT("clothes");
 // Two coincident, disconnected legs with distinct private bone supports.
 for(int32 Leg=0;Leg<2;++Leg){const int32 Base=Grid.Vertices.Num();
  for(int32 Y=0;Y<=20;++Y)for(int32 X=0;X<=20;++X){FKKRuntimeMeshVertex V;V.Position=V.InitialPosition=FVector3f(X,Y,0);V.Normal=FVector3f(0,0,1);V.Tangent=FVector3f(1,0,0);V.Influences=3;for(int32 K=0;K<3;++K)V.Skin[K]={uint16(Leg*3+K),21845};for(int32 U=0;U<4;++U)V.UV[U]=FVector2f(X/20.f,Y/20.f+U);V.Color=Leg?FColor::Red:FColor::Blue;Grid.Vertices.Add(V);}
  for(int32 Y=0;Y<20;++Y)for(int32 X=0;X<20;++X){const uint32 A=Base+Y*21+X;Grid.Indices.Append({A,A+1,A+21,A+1,A+22,A+21});}
 }
 Parts.Add(Grid);FKKRuntimeMeshPart Head=Grid;Head.NativePart=1;Head.Role=TEXT("head");Parts.Add(Head);
 auto Future=OKReduceRuntimeMeshesAsync(Parts,15000,300,30000);auto R=Future.Consume();
 if(!R.Error.IsEmpty()||!R.WorkerThread||R.Meshes.Num()!=2||R.Meshes[0].Indices.Num()>=Grid.Indices.Num()||R.Meshes[1].Indices!=Head.Indices||R.Meshes[1].Vertices.Num()!=Head.Vertices.Num()){Error=TEXT("Runtime reduction/head retention oracle failed");return false;}
 for(const auto& V:R.Meshes[0].Vertices){bool Found=false;for(const auto& O:Grid.Vertices)if(V.Position==O.Position&&V.Color==O.Color){Found=V.Influences==O.Influences&&V.InitialPosition==O.InitialPosition;for(int32 U=0;U<4;++U)Found&=V.UV[U]==O.UV[U];for(int32 K=0;K<3;++K)Found&=V.Skin[K].Bone==O.Skin[K].Bone&&V.Skin[K].Weight==O.Skin[K].Weight;break;}if(!Found){Error=TEXT("Reduced endpoint attributes changed");return false;}}
 const auto& Low=R.Meshes[0];bool Left=false,Right=false;for(int32 I=0;I<Low.Indices.Num();I+=3){const auto& A=Low.Vertices[Low.Indices[I]];Left|=A.Color==FColor::Blue;Right|=A.Color==FColor::Red;for(int32 C=1;C<3;++C)if(Low.Vertices[Low.Indices[I+C]].Color!=A.Color){Error=TEXT("Reduction joined separate legs");return false;}}if(!Left||!Right){Error=TEXT("Reduction discarded leg component");return false;}
 Parts[0].Indices.Append({0,1,21});auto BadTopology=OKReduceRuntimeMeshesAsync(Parts,15000,300,30000);auto Kept=BadTopology.Consume();if(!Kept.Error.IsEmpty()||Kept.Parts[0].RetentionReason.IsEmpty()||Kept.Meshes[0].Indices!=Parts[0].Indices){Error=TEXT("Invalid topology was not retained");return false;}
 Parts[0].Indices[0]=MAX_uint32;auto Invalid=OKReduceRuntimeMeshesAsync(Parts);auto Rejected=Invalid.Consume();if(Rejected.Error.IsEmpty()||!Rejected.Meshes.IsEmpty()){Error=TEXT("Invalid reduction index was admitted");return false;}
 Error.Reset();return true;
}

bool RenderMeshOracle(TConstArrayView<FKKRuntimeMeshPart> Meshes,TConstArrayView<FKKRuntimePoseBone> Bones,FKKRuntimeRenderMeshStats& Stats,FString& Error){
 if(!RenderPartitionOracle(Error))return false;
 TArray<FKKRuntimeMeshPart> Owned;Owned.Append(Meshes.GetData(),Meshes.Num());TArray<FKKRuntimePoseBone> Rig;Rig.Append(Bones.GetData(),Bones.Num());
 const uint32 Limit=FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones();auto Future=OKPrepareRuntimeRenderMeshAsync(MoveTemp(Owned),MoveTemp(Rig),Limit);auto D=Future.Consume();
 if(!D.Error.IsEmpty()||!D.WorkerThread){Error=D.Error;return false;}
 TArray<UMaterialInterface*> Materials;TStrongObjectPtr<USkeletalMesh> Mesh(OKCreateRuntimeRenderMesh(D,Materials,Error,true));if(!Mesh)return false;
 FlushRenderingCommands();const auto* Data=Mesh->GetResourceForRendering();if(!Data||Data->LODRenderData.Num()!=1||Mesh->GetRefSkeleton().GetNum()!=Bones.Num()){Error=TEXT("Runtime mesh render resource/rig absent");return false;}const auto& L=Data->LODRenderData[0];
 if(L.GetNumVertices()!=D.Vertices.Num()||L.RenderSections.Num()!=D.Sections.Num()||!L.StaticVertexBuffers.PositionVertexBuffer.VertexBufferRHI.IsValid()||!L.SkinWeightVertexBuffer.GetDataVertexBuffer()->VertexBufferRHI.IsValid()){Error=TEXT("Runtime mesh GPU buffers not initialized");return false;}
 uint64 ExpectedTriangles=0;for(const auto& P:Meshes)ExpectedTriangles+=P.Indices.Num()/3;uint64 ActualTriangles=0;
 for(int32 S=0;S<L.RenderSections.Num();++S){const auto& Section=L.RenderSections[S];const auto& Prepared=D.Sections[S];ActualTriangles+=Section.NumTriangles;
  if(Section.BoneMap!=Prepared.BoneMap||uint32(Section.BoneMap.Num())>Limit||Section.MaterialIndex!=Prepared.Material){Error=TEXT("Runtime render section mapping differs");return false;}
  for(uint32 I=Section.BaseVertexIndex;I<Section.BaseVertexIndex+Section.NumVertices;++I){if(L.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(I)!=D.Vertices[I].Position){Error=TEXT("Runtime render position changed");return false;}for(int32 U=0;U<4;++U)if(L.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I,U)!=D.Vertices[I].UV[U]){Error=TEXT("Runtime render UV changed");return false;}
   const auto Weight=L.SkinWeightVertexBuffer.GetVertexSkinWeights(I);TMap<uint16,uint32> Expected,Actual;uint32 Total=0;
   for(int32 K=0;K<D.Vertices[I].Influences;++K)Expected.FindOrAdd(D.Vertices[I].Skin[K].Bone)+=D.Vertices[I].Skin[K].Weight;
   for(int32 K=0;K<MAX_TOTAL_INFLUENCES;++K)if(Weight.InfluenceWeights[K]){if(Weight.InfluenceBones[K]>=Section.BoneMap.Num()){Error=TEXT("Runtime render local bone outside section");return false;}Actual.FindOrAdd(Section.BoneMap[Weight.InfluenceBones[K]])+=Weight.InfluenceWeights[K];Total+=Weight.InfluenceWeights[K];}
   if(Total!=65535||Actual.Num()!=Expected.Num()){Error=TEXT("Runtime render skin influence count/total changed");return false;}for(const auto& W:Expected)if(Actual.FindRef(W.Key)!=W.Value){Error=TEXT("Runtime render skin bone/weight changed");return false;}
  }
 }
 if(ActualTriangles!=ExpectedTriangles){Error=TEXT("Runtime mesh partition discarded triangles");return false;}
 Stats.Created=true;Stats.Sections=L.RenderSections.Num();Stats.Vertices=L.GetNumVertices();Stats.Triangles=ActualTriangles;Stats.PrepareMs=D.PrepareMs;return true;
}

FTransform SampleLocal(const FKKRuntimePoseResult& R,int32 Bone,float Time){
 const auto& Track=R.Tracks[Bone];uint32 Lo=0,Hi=Track.Y-1;
 while(Hi-Lo>1){const uint32 Mid=(Lo+Hi)/2;if(R.Keys[(Track.X+Mid)*3].W<=Time)Lo=Mid;else Hi=Mid;}
 const auto* A=&R.Keys[(Track.X+Lo)*3];const auto* B=&R.Keys[(Track.X+Hi)*3];const float Alpha=Hi==Lo?0.f:FMath::Clamp((Time-A[0].W)/(B[0].W-A[0].W),0.f,1.f);
 const FVector P=FMath::Lerp(FVector(A[0].X,A[0].Y,A[0].Z),FVector(B[0].X,B[0].Y,B[0].Z),Alpha),S=FMath::Lerp(FVector(A[2].X,A[2].Y,A[2].Z),FVector(B[2].X,B[2].Y,B[2].Z),Alpha);
 const FQuat Q=FQuat::FastLerp(FQuat(A[1].X,A[1].Y,A[1].Z,A[1].W),FQuat(B[1].X,B[1].Y,B[1].Z,B[1].W),Alpha).GetNormalized();return FTransform(Q,P,S);
}
bool WindowGpuOracle(const FOKGpuAnimationBundle& Bank,const FKKRuntimePoseRequest& Request,const FKKRuntimePoseResult& Result,double& Maximum,FString& Error){
 FOKResidentGpuPose Pose;FOKResidentPoseStats Stats;TArray<FOKGpuPosePlayback> Play;Play.AddDefaulted();uint64 Frame=0;bool Passed=true;Maximum=0;
 for(float Phase:{0.f,.137f,.37f,.731f,.99f,1.f}){
  Play[0].ClipA=Play[0].ClipB=0;Play[0].Times=FVector4f(Phase*Result.Duration,Phase*Result.Duration,0,0);TArray<FVector4f> Current,Previous;
  if(!OKReadbackResidentGpuPose(Pose,Frame==0?&Bank.Data.Pose:nullptr,Bank.Data.OwnedClips,Play,Frame,false,Current,Previous,Stats,Error)){Passed=false;break;}++Frame;
  if(Current.Num()!=Request.Bones.Num()*3){Passed=false;break;}TArray<FTransform> World;World.SetNum(Request.Bones.Num());
  for(int32 B=0;B<World.Num();++B){World[B]=SampleLocal(Result,B,Play[0].Times.X);if(Request.Bones[B].Parent>=0)World[B]=World[B]*World[Request.Bones[B].Parent];const FMatrix M=World[B].ToMatrixWithScale();for(int32 R=0;R<3;++R)for(int32 C=0;C<4;++C){if(!FMath::IsFinite(Current[B*3+R][C]))Passed=false;Maximum=FMath::Max(Maximum,FMath::Abs(double(Current[B*3+R][C])-M.M[C][R]));}}
 }
 ENQUEUE_RENDER_COMMAND(ReleaseWindowOracle)([&](FRHICommandListImmediate&){Pose.Release();});FlushRenderingCommands();
 if(!Passed||Maximum>.001){Error=TEXT("Selected clip window differs from private CPU pose");return false;}return true;
}
bool HundredClipWindowOracle(FString& Error){
 // Synthetic ownership/index oracle; not twenty real authored animations.
 auto Source=MakeShared<FOKGpuAnimationBundle,ESPMode::ThreadSafe>();auto& D=Source->Data;D.Palette.PoseCount=100;TArray<FOKGpuClipWindow> Windows;
 for(uint32 I=0;I<100;++I){FOKGpuActorRecord A;A.CharacterId=FString::Printf(TEXT("window-fixture-%u"),I);A.PoseFirst=I;A.PoseCount=1;A.PoseBones.Add({TEXT("root"),INDEX_NONE,OKGpuBoneReferenceIdentity(FTransform::Identity)});FOKGpuMeshRecord M;M.PartId=TEXT("fixture");M.AssetPath=TEXT("/Game/WindowFixture/Mesh");M.Bones={I,1};A.Meshes.Add(MoveTemp(M));TArray<uint32> Offsets;
  for(uint32 C=0;C<20;++C){const uint32 Base=D.Pose.Tracks.Num(),Key=D.Pose.Keys.Num()/3;const FString Id=FString::Printf(TEXT("style-%02u"),C);A.Clips.Add({Id,Base,1});Offsets.Add(Base);D.Pose.Tracks.Add(FUintVector4(Key,1,MAX_uint32,1));D.Pose.Keys.Append({FVector4f(float(I*100+C),0,0,0),FVector4f(0,0,0,1),FVector4f(1,1,1,0)});}
  D.Pose.Actors.Add(FUintVector4(Offsets[0],Offsets[0],1,1));D.Pose.Times.Add(FVector4f(0,0,0,0));D.OwnedClips.Add(MoveTemp(Offsets));D.Pose.Tasks.Add(FUintVector4(0,I,I*3,0));D.Palette.Items.Add(FUintVector4(I,I*3,I,0));
  const TArray<FVector4f> Identity={FVector4f(1,0,0,0),FVector4f(0,1,0,0),FVector4f(0,0,1,0)};D.Pose.InverseBindRows.Append(Identity);D.Palette.InverseBindRows.Append(Identity);
  FOKGpuClipWindow W;W.CharacterId=A.CharacterId;for(uint32 C:{I%20,(I+7)%20,(I+13)%20})W.Clips.Add(FString::Printf(TEXT("style-%02u"),C));Windows.Add(MoveTemp(W));Source->Manifest.Actors.Add(MoveTemp(A));
 }
 Algo::Reverse(Windows);auto Pending=OKSelectGpuClipWindowAsync(Source,MoveTemp(Windows));auto Selected=Pending.Consume();if(!Selected.WorkerThread||!Selected.Bundle){Error=Selected.Error;return false;}
 const auto& B=*Selected.Bundle;if(B.Data.Pose.Keys.Num()!=900||B.Data.Pose.Tracks.Num()!=300){Error=TEXT("Hundred private clip windows retained wrong ranges");return false;}
 TArray<FOKGpuPosePlayback> Play;Play.SetNum(100);for(auto& P:Play){P.ClipA=0;P.ClipB=1;P.Times=FVector4f(.5,.8,.25,0);}
 FOKResidentGpuPose Pose;FOKResidentPoseStats Stats;TArray<FVector4f> Current,Previous;bool Passed=OKReadbackResidentGpuPose(Pose,&B.Data.Pose,B.Data.OwnedClips,Play,0,false,Current,Previous,Stats,Error);
 if(Passed){Passed=Current.Num()==300;for(uint32 I=0;Passed&&I<100;++I){const float X=float(I*100)+.75f*float(I%20)+.25f*float((I+7)%20);for(uint32 R=0;R<3;++R)for(uint32 C=0;C<4;++C){const float Expected=C==3?(R==0?X:0):(C==R?1:0);if(!FMath::IsFinite(Current[I*3+R][C])||FMath::Abs(Current[I*3+R][C]-Expected)>.001f)Passed=false;}}}
 ENQUEUE_RENDER_COMMAND(ReleaseHundredWindowOracle)([&](FRHICommandListImmediate&){Pose.Release();});FlushRenderingCommands();if(!Passed&&Error.IsEmpty())Error=TEXT("Hundred private clip windows mixed character ownership");return Passed;
}
bool ClockOracle(FString& Error){
 TArray<FOKRuntimeGpuPlaybackClip> Catalog;for(int32 I=0;I<20;++I)Catalog.Add({FString::Printf(TEXT("private-style-%d"),I),float(2.+I*.1),true});Catalog[19].Loop=false;
 FOKRuntimeGpuPlayback C;if(!C.Initialize(Catalog,0,1.9,Error)||!C.Advance(.2,Error)||FMath::Abs(C.Parameters().Times.X-.1)>.00001)return false;
 C.SetPaused(true);const auto Frozen=C.Parameters();if(!C.Advance(2.,Error)||C.Parameters().Times!=Frozen.Times)return false;C.SetPaused(false);
 if(!C.SetRate(2,Error)||!C.Advance(.25,Error)||FMath::Abs(C.Parameters().Times.X-.6)>.00001)return false;
 if(!C.SetRate(-1,Error)||!C.Advance(.9,Error)||FMath::Abs(C.Parameters().Times.X-1.7)>.00001||!C.SetRate(2,Error))return false;
 if(!C.RequestClip(1,.4,.2,Error)||!C.Advance(.1,Error)||FMath::Abs(C.Parameters().Times.Z-.25)>.00001)return false;
 if(!C.RequestClip(2,.2,0,Error)||!C.RequestClip(3,.2,0,Error)||!C.Advance(.6,Error)||C.IsTransitioning()||C.Parameters().ClipA!=3)return false;
 const auto Before=C.Parameters();if(C.RequestClip(20,0,0,Error)||C.Advance(-1,Error)||C.SetRate(NAN,Error)||C.Parameters().Times!=Before.Times)return false;
 if(!C.RequestClip(19,0,0,Error)||!C.Advance(10,Error)||FMath::Abs(C.Parameters().Times.X-Catalog[19].Duration)>.00001)return false;
 // Independent clocks can use all20 indices/phases without sharing a pose.
 for(int32 I=0;I<100;++I){FOKRuntimeGpuPlayback Private;if(!Private.Initialize(Catalog,I%20,I*.137,Error)||!Private.Advance(.5,Error)||Private.Parameters().ClipA!=uint32(I%20))return false;}
 Error.Reset();return true;
}
}
bool OKAuditRuntimeMaterials(const FKKRuntimeMaterialData& D,FKKRuntimeMaterialStats& Stats,FString& Error,TConstArrayView<UMaterialInterface*> SharedParents){
 if(!TextureMipOracle(Error))return false;if(!D.WorkerThread||D.Parts.IsEmpty()||D.Textures.IsEmpty()){Error=TEXT("Owned runtime materials absent");return false;}
 TSet<int32> Parts;TArray<TStrongObjectPtr<UTexture2D>> Hold;
 for(const auto& P:D.Parts){if(Parts.Contains(P.NativePart)){Error=TEXT("Runtime material part duplicated");return false;}Parts.Add(P.NativePart);TSharedPtr<FJsonObject> Metadata;
  if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Metadata)||Metadata->GetIntegerField(TEXT("schema"))!=1){Error=TEXT("Owned source material metadata invalid");return false;}
  TSet<FString> Names;for(const auto& Slot:P.Textures){if(Slot.Texture<0||Slot.Texture>=D.Textures.Num()||Names.Contains(Slot.Property)){Error=TEXT("Runtime material texture slot invalid");return false;}Names.Add(Slot.Property);++Stats.Slots;}
  for(const auto& N:Metadata->GetArrayField(TEXT("shader_textures")))if(!Names.Contains(N->AsString())){Error=TEXT("Runtime shader texture slot lost");return false;}
 }
 for(const auto& T:D.Textures){auto* Tex=OKCreateRuntimePreparedTexture(T,Error);if(!Tex)return false;Hold.Emplace(Tex);}
 const auto CacheBefore=OKRuntimeTextureCacheStats();
 for(int32 I=0;I<D.Textures.Num();++I){auto Copy=D.Textures[I];Copy.SourceHash=TEXT("different-PNG-container-same-prepared-pixels");if(OKCreateRuntimePreparedTexture(Copy,Error)!=Hold[I].Get()){Error=TEXT("Cross-card immutable texture was duplicated");return false;}}
 const auto CacheAfter=OKRuntimeTextureCacheStats();if(CacheAfter.Hits-CacheBefore.Hits!=uint64(D.Textures.Num())||CacheAfter.Created!=CacheBefore.Created){Error=TEXT("Cross-card cache accounting differs");return false;}
 TArray<TStrongObjectPtr<UTexture2D>> CacheControls;
 auto PixelChange=D.Textures[0];PixelChange.Mips[0].BGRA[0]^=1;auto* Changed=OKCreateRuntimePreparedTexture(PixelChange,Error);if(!Changed||Changed==Hold[0].Get()){Error=TEXT("Prepared pixel change reused another texture");return false;}CacheControls.Emplace(Changed);
 for(int32 Field=0;Field<5;++Field){auto Change=D.Textures[0];if(Field==0)Change.Settings.Filter=(Change.Settings.Filter+1)%3;else if(Field==1)Change.Settings.WrapU=(Change.Settings.WrapU+1)%3;else if(Field==2)Change.Settings.WrapV=(Change.Settings.WrapV+1)%3;else if(Field==3)Change.Settings.SRGB=!Change.Settings.SRGB;else Change.Settings.EncodedMips=!Change.Settings.EncodedMips;auto* Control=OKCreateRuntimePreparedTexture(Change,Error);if(!Control||Control==Hold[0].Get()){Error=TEXT("Different sampler/color provenance shared a texture");return false;}CacheControls.Emplace(Control);}
 FlushRenderingCommands();uint64 Bytes=0;
 for(int32 I=0;I<Hold.Num();++I){const auto* Tex=Hold[I].Get();const auto& T=D.Textures[I];const auto* Resource=Tex->GetResource();const auto* Platform=Tex->GetPlatformData();
  if(!Resource||!Resource->TextureRHI.IsValid()||Resource->TextureRHI->GetNumMips()!=T.Mips.Num()||Platform->Mips.Num()!=T.Mips.Num()||Tex->SRGB!=T.Settings.SRGB){Error=TEXT("Runtime texture RHI/settings mismatch");return false;}
  const auto ExpectedFilter=T.Settings.Filter==0?TF_Nearest:T.Settings.Filter==2?TF_Trilinear:TF_Bilinear;auto Wrap=[](int32 V){return V==1?TA_Clamp:V==2?TA_Mirror:TA_Wrap;};
  if(Tex->Filter!=ExpectedFilter||Tex->AddressX!=Wrap(T.Settings.WrapU)||Tex->AddressY!=Wrap(T.Settings.WrapV)){Error=TEXT("Runtime texture sampler changed");return false;}
  for(int32 L=0;L<T.Mips.Num();++L){const auto& M=T.Mips[L];auto& Actual=Platform->Mips[L];if(Actual.SizeX!=M.Width||Actual.SizeY!=M.Height||Actual.BulkData.GetBulkDataSize()!=M.BGRA.Num()){Error=TEXT("Runtime texture dimensions differ");return false;}
   const void* Raw=Actual.BulkData.LockReadOnly();const bool Same=Raw&&!FMemory::Memcmp(Raw,M.BGRA.GetData(),M.BGRA.Num());Actual.BulkData.Unlock();if(!Same){Error=TEXT("Runtime texture platform pixels differ");return false;}Bytes+=M.BGRA.GetAllocatedSize();++Stats.Mips;
  }
 }
 if(Bytes!=D.PixelBytes){Error=TEXT("Runtime prepared texture accounting differs");return false;}
 if(!SharedParents.IsEmpty()){
  TArray<FColor> SourcePixels;if(!OKVerifySourceRasterGpu(SourcePixels,Error))return false;Stats.SourceRasterVerified=true;
  bool MeshShadersReady=true;
  ENQUEUE_RENDER_COMMAND(VerifyCookedSourceMeshShaders)([&](FRHICommandListImmediate&){
   for(const auto* Parent:SharedParents){
    const auto* Material=Parent->GetRenderProxy()->GetMaterialNoFallback(GMaxRHIFeatureLevel);
    if(!Material){MeshShadersReady=false;break;}
    for(const auto* VFName:{TEXT("FLocalVertexFactory"),TEXT("TGPUSkinVertexFactoryDefault"),TEXT("TGPUSkinVertexFactoryUnlimited"),TEXT("FOKSourceOracleVF")})
     MeshShadersReady=MeshShadersReady&&OKSourceMaterialMeshShadersAvailable(*Material,FindVertexFactoryType(FName(VFName)));
   }
  });FlushRenderingCommands();
  if(!MeshShadersReady){Error=TEXT("Cooked source mesh shader pair missing for local/default/unlimited GPU skin vertex factory");return false;}
  Stats.SourceMeshShadersVerified=true;
  if(!SharedMaterialBindingOracle(Error))return false;
  TSet<FString> ParentNames;int32 RecipeIndex=0;for(auto R:OKRuntimeSharedMaterialRecipes()){if(int32(R)!=++RecipeIndex||OKRuntimeSharedMaterialParentName(R).IsEmpty()||ParentNames.Contains(OKRuntimeSharedMaterialParentName(R))||!OKRuntimeSharedMaterialLayout(R)||OKRuntimeSharedMaterialProgram(R).IsEmpty()){Error=TEXT("Runtime recipe catalog identity invalid");return false;}ParentNames.Add(OKRuntimeSharedMaterialParentName(R));}
  TArray<UTexture2D*> Textures;for(const auto& T:Hold)Textures.Add(T.Get());TArray<TStrongObjectPtr<UMaterialInstanceDynamic>> Instances;
  for(const auto& P:D.Parts){const auto Recipe=OKRuntimeSharedMaterialRecipe(P);if(Recipe==EKKRuntimeMaterialRecipe::None){++Stats.UnsupportedParts;continue;}
   if(SharedParents.Num()!=OKRuntimeSharedMaterialRecipes().Num()){Error=TEXT("Shared material parent catalog incomplete");return false;}auto* SharedParent=SharedParents[int32(Recipe)-1];
   FKKRuntimeMaterialBinding Binding;FKKRuntimeMaterialScene Scene;if(!OKPrepareRuntimeSharedMaterialBinding(P,Scene,Binding,Error))return false;
   auto* Instance=OKCreateRuntimeSharedMaterial(P,Textures,SharedParent,Scene,Error);if(!Instance)return false;Instances.Emplace(Instance);if(Instance->Parent!=SharedParent){Error=TEXT("Runtime MID has a private/crossed parent");return false;}
   for(const auto& V:Binding.Scalars){float X;if(!Instance->GetScalarParameterValue(FMaterialParameterInfo(V.Key),X)||X!=V.Value){Error=TEXT("Runtime shared material scalar differs");return false;}}
   for(const auto& V:Binding.Vectors){FLinearColor X;if(!Instance->GetVectorParameterValue(FMaterialParameterInfo(V.Key),X)||X!=V.Value){Error=TEXT("Runtime shared material vector differs");return false;}}
   for(const auto& T:Binding.Textures){UTexture* X=nullptr;if(!Instance->GetTextureParameterValue(FMaterialParameterInfo(T.Key),X)||X!=Textures[T.Value]){Error=TEXT("Runtime shared material texture differs");return false;}}
   if(OKCreateRuntimeSharedMaterial(P,Textures,UMaterial::GetDefaultMaterial(MD_Surface),Scene,Error)){Error=TEXT("Wrong shared material parent was admitted");return false;}++Stats.SharedInstances;if(Recipe==EKKRuntimeMaterialRecipe::ForgeOpaque)++Stats.OpaqueInstances;else if(Recipe==EKKRuntimeMaterialRecipe::ForgeAlphaMasked)++Stats.AlphaMaskedInstances;else if(Recipe==EKKRuntimeMaterialRecipe::ForgeAlphaTranslucent)++Stats.AlphaTranslucentInstances;else if(Recipe==EKKRuntimeMaterialRecipe::ForgeItem)++Stats.ItemInstances;else if(Recipe==EKKRuntimeMaterialRecipe::ForgeHair)++Stats.HairInstances;else if(Recipe==EKKRuntimeMaterialRecipe::ForgeHairFront)++Stats.HairFrontInstances;else if(Recipe==EKKRuntimeMaterialRecipe::ForgeStudioAlpha)++Stats.StudioAlphaInstances;else if(Recipe==EKKRuntimeMaterialRecipe::ForgeEyeWhite)++Stats.EyeWhiteInstances;else if(Recipe==EKKRuntimeMaterialRecipe::ForgeEye)++Stats.EyeInstances;else if(Recipe==EKKRuntimeMaterialRecipe::KoikanoEyeWhite)++Stats.KoikanoEyeWhiteInstances;else if(Recipe==EKKRuntimeMaterialRecipe::KoikanoEye)++Stats.KoikanoEyeInstances;else if(Recipe==EKKRuntimeMaterialRecipe::ForgeNose||Recipe==EKKRuntimeMaterialRecipe::KoikanoNose)++Stats.NoseInstances;else ++Stats.AdditionalInstances;
  }
  // Exercise both Alpha render states even when a source card contains only one.
  for(int32 Alpha=1;Alpha<3;++Alpha){FKKRuntimeMaterialPart P;P.ShaderKind=11;P.SourceMetadataJson=Alpha==1?TEXT("{\"schema\":1,\"material\":{\"floats\":{\"_OKNativeAlphaOpaque\":1}}}"):TEXT("{\"schema\":1,\"material\":{\"floats\":{\"_OKNativeAlphaOpaque\":0}}}");
   auto* I=OKCreateRuntimeSharedMaterial(P,Textures,SharedParents[Alpha],FKKRuntimeMaterialScene{},Error);if(!I)return false;Instances.Emplace(I);
   if(OKCreateRuntimeSharedMaterial(P,Textures,SharedParents[Alpha==1?2:1],FKKRuntimeMaterialScene{},Error)){Error=TEXT("Mismatched Alpha parent state admitted");return false;}
  }
  FKKRuntimeMaterialPart Nose;Nose.ShaderKind=0;Nose.Unlit=1;Nose.Blend=TEXT("translucent");Nose.SourceMetadataJson=TEXT("{\"schema\":1,\"material\":{\"shader\":\"Shader Forge/toon_nose_lod0\",\"floats\":{\"_MainTex_ST_x\":2,\"_MainTex_ST_y\":3,\"_MainTex_ST_z\":0.125,\"_MainTex_ST_w\":0.25},\"colors\":{\"_Color\":[0.2,0.4,0.6,0.375]}}}");
  FKKRuntimeMaterialBinding NoseBinding;if(!OKPrepareRuntimeSharedMaterialBinding(Nose,FKKRuntimeMaterialScene{},NoseBinding,Error)||NoseBinding.Vectors.FindRef(TEXT("_Color"))!=FLinearColor(.2,.4,.6,.375)||NoseBinding.Scalars.FindRef(TEXT("_Color__A"))!=.375f||NoseBinding.Vectors.FindRef(TEXT("_MainTex_ST"))!=FLinearColor(2,3,.125,.25)){Error=TEXT("Nose authored color/UV transform oracle failed");return false;}
  auto* NoseMID=OKCreateRuntimeSharedMaterial(Nose,Textures,SharedParents[11],FKKRuntimeMaterialScene{},Error);if(!NoseMID||NoseMID->IsTwoSided()||NoseMID->GetBlendMode()!=BLEND_Translucent){Error=TEXT("Nose parent render state differs");return false;}Instances.Emplace(NoseMID);
  UTexture* NoseDefault=nullptr;if(!NoseMID->GetTextureParameterValue(FMaterialParameterInfo(TEXT("_MainTex")),NoseDefault)||!Cast<UTexture2D>(NoseDefault)){Error=TEXT("Nose black default texture absent");return false;}
  FKKRuntimePreparedTexture Fixture;Fixture.SourceWidth=4;Fixture.SourceHeight=1;Fixture.Settings.SRGB=false;Fixture.Settings.EncodedMips=true;Fixture.Settings.Filter=0;Fixture.Settings.WrapU=Fixture.Settings.WrapV=1;
  if(!OKBuildRuntimeTextureMips({0,0,0,255,0,0,255,255,0,255,0,255,0,0,0,0},4,1,Fixture.Settings,Fixture.Mips,Error))return false;
  TStrongObjectPtr<UTexture2D> FixtureTexture(OKCreateRuntimePreparedTexture(Fixture,Error));if(!FixtureTexture.IsValid())return false;
  NoseMID->SetTextureParameterValue(TEXT("_MainTex"),FixtureTexture.Get());NoseMID->SetVectorParameterValue(TEXT("_Color"),FLinearColor(.2,.4,.6,.5));NoseMID->SetScalarParameterValue(TEXT("_Color__A"),.5);
  NoseMID->SetVectorParameterValue(TEXT("_MainTex_ST"),FLinearColor(1,1,0,0));NoseMID->SetScalarParameterValue(TEXT("_MainTex_ST__A"),0);
  FlushRenderingCommands();TArray<FColor> MeshPixels;if(!OKVerifySourceMaterialMeshPixels(NoseMID,MeshPixels,Error))return false;
  const FColor ExpectedPixels[]={FColor(26,51,77,64),FColor(0,0,0,64),FColor(128,128,128,64),FColor(0,0,0,0)};
  if(MeshPixels.Num()!=4)return false;
  for(int32 I=0;I<4;++I)for(int32 C=0;C<4;++C)if(FMath::Abs(int32(MeshPixels[I].DWColor()>>(C*8)&255)-int32(ExpectedPixels[I].DWColor()>>(C*8)&255))>1){Error=FString::Printf(TEXT("Actual source material mesh pixel mismatch cell=%d actual=%s expected=%s"),I,*MeshPixels[I].ToString(),*ExpectedPixels[I].ToString());return false;}
  Stats.SourceMeshPixelsVerified=true;
  if(!OKVerifySourceMaterialMeshPixels(NoseMID,MeshPixels,Error,true)||MeshPixels.Num()!=16)return false;
  const FColor RepeatedPixels[]={FColor(38,77,115,96),FColor(0,0,0,96),FColor(192,192,192,96),FColor(0,0,0,0)};
  for(int32 Case=0;Case<4;++Case)for(int32 I=0;I<4;++I){const auto& Expected=(Case==1||Case==3)?RepeatedPixels[I]:ExpectedPixels[I];for(int32 C=0;C<4;++C)if(FMath::Abs(int32(MeshPixels[Case*4+I].DWColor()>>(C*8)&255)-int32(Expected.DWColor()>>(C*8)&255))>1){Error=FString::Printf(TEXT("Actual source mesh depth/stencil pixel mismatch case=%d cell=%d actual=%s expected=%s"),Case,I,*MeshPixels[Case*4+I].ToString(),*Expected.ToString());return false;}}
  Stats.SourceMeshRasterPixelsVerified=true;
  if(OKCreateRuntimeSharedMaterial(Nose,Textures,SharedParents[7],FKKRuntimeMaterialScene{},Error)){Error=TEXT("Eye-white parent admitted for nose family");return false;}
  Nose.EyeDraw=1;if(OKRuntimeSharedMaterialRecipe(Nose)!=EKKRuntimeMaterialRecipe::None){Error=TEXT("Iris draw admitted as nose");return false;}Nose.EyeDraw=0;Nose.Blend=TEXT("masked");if(OKRuntimeSharedMaterialRecipe(Nose)!=EKKRuntimeMaterialRecipe::None){Error=TEXT("Masked nose admitted as translucent");return false;}Nose.Blend=TEXT("translucent");Nose.SourceMetadataJson=TEXT("{\"schema\":1,\"material\":{\"shader\":\"Koikano/main_nose\"}}");if(OKRuntimeSharedMaterialRecipe(Nose)!=EKKRuntimeMaterialRecipe::KoikanoNose){Error=TEXT("Koikano nose recipe classification differs");return false;}
  FKKRuntimeMaterialPart Item;Item.ShaderKind=8;Item.SourceMetadataJson=TEXT("{\"schema\":1,\"material\":{\"colors\":{\"_Color\":[0.2,0.4,0.6,0.8],\"_Color2\":[0.8,0.6,0.4,0.2]}}}");
  FKKRuntimeMaterialBinding ItemBinding;if(!OKPrepareRuntimeSharedMaterialBinding(Item,FKKRuntimeMaterialScene{},ItemBinding,Error)||ItemBinding.Vectors.FindRef(TEXT("_Color"))!=FLinearColor(.2,.4,.6,.8)||ItemBinding.Scalars.FindRef(TEXT("_Color__A"))!=.8f||ItemBinding.Vectors.FindRef(TEXT("_Color2"))!=FLinearColor(.8,.6,.4,.2)){Error=TEXT("Item authored recolor oracle failed");return false;}
  auto* ItemMID=OKCreateRuntimeSharedMaterial(Item,Textures,SharedParents[3],FKKRuntimeMaterialScene{},Error);if(!ItemMID)return false;Instances.Emplace(ItemMID);
  if(OKCreateRuntimeSharedMaterial(Item,Textures,SharedParents[0],FKKRuntimeMaterialScene{},Error)){Error=TEXT("Opaque parent admitted for Item recolor family");return false;}
  for(int32 Front=0;Front<2;++Front){FKKRuntimeMaterialPart Hair;Hair.ShaderKind=1;Hair.Unlit=1;Hair.Blend=TEXT("masked");Hair.SourceMetadataJson=Front?TEXT("{\"schema\":1,\"material\":{\"shader\":\"Shader Forge/main_hair_front\"}}"):TEXT("{\"schema\":1,\"material\":{\"shader\":\"Shader Forge/main_hair\"}}");
   FKKRuntimeMaterialBinding B;if(!OKPrepareRuntimeSharedMaterialBinding(Hair,FKKRuntimeMaterialScene{},B,Error)||FMath::Abs(B.Scalars.FindRef(TEXT("_SpeclarHeight"))-(Front?.985f:.85f))>1e-6f||FMath::Abs(B.Vectors.FindRef(TEXT("_Color2")).R-.7843137f)>1e-6f){Error=TEXT("Hair/front default or family oracle failed");return false;}
   auto* I=OKCreateRuntimeSharedMaterial(Hair,Textures,SharedParents[4+Front],FKKRuntimeMaterialScene{},Error);if(!I)return false;Instances.Emplace(I);
   if(OKCreateRuntimeSharedMaterial(Hair,Textures,SharedParents[4+(1-Front)],FKKRuntimeMaterialScene{},Error)){Error=TEXT("Wrong hair/front parent admitted");return false;}
   Hair.Blend=TEXT("translucent");if(OKRuntimeSharedMaterialRecipe(Hair)!=EKKRuntimeMaterialRecipe::None){Error=TEXT("Unsupported hair blend admitted");return false;}
  }
  FKKRuntimeMaterialPart Studio;Studio.ShaderKind=4;Studio.Unlit=1;Studio.Blend=TEXT("translucent");Studio.SourceMetadataJson=TEXT("{\"schema\":1,\"material\":{\"shader\":\"Shader Forge/main_item_studio_alpha\",\"floats\":{\"_alpha\":0.375,\"_patternrotator1\":0.5},\"colors\":{\"_Patternuv1\":[0.1,0.2,2,3]}}}");
  FKKRuntimeMaterialBinding StudioBinding;if(!OKPrepareRuntimeSharedMaterialBinding(Studio,FKKRuntimeMaterialScene{},StudioBinding,Error)||StudioBinding.Scalars.FindRef(TEXT("_alpha"))!=.375f||StudioBinding.Scalars.FindRef(TEXT("_patternrotator1"))!=.5f||StudioBinding.Vectors.FindRef(TEXT("_Patternuv1"))!=FLinearColor(.1,.2,2,3)||StudioBinding.Scalars.FindRef(TEXT("_Patternuv1__A"))!=3.f){Error=TEXT("Studio alpha/pattern oracle failed");return false;}
  auto* StudioMID=OKCreateRuntimeSharedMaterial(Studio,Textures,SharedParents[6],FKKRuntimeMaterialScene{},Error);if(!StudioMID||StudioMID->IsTwoSided()||StudioMID->GetBlendMode()!=BLEND_Translucent)return false;Instances.Emplace(StudioMID);
  if(OKCreateRuntimeSharedMaterial(Studio,Textures,SharedParents[2],FKKRuntimeMaterialScene{},Error)){Error=TEXT("Wrong translucent family parent admitted");return false;}
  Studio.Blend=TEXT("opaque");if(OKRuntimeSharedMaterialRecipe(Studio)!=EKKRuntimeMaterialRecipe::None){Error=TEXT("Unsupported studio blend admitted");return false;}
  FKKRuntimeMaterialPart EyeWhite;EyeWhite.ShaderKind=0;EyeWhite.Unlit=1;EyeWhite.Blend=TEXT("translucent");EyeWhite.SourceMetadataJson=TEXT("{\"schema\":1,\"material\":{\"shader\":\"Shader Forge/toon_eyew_lod0\",\"colors\":{\"_Color\":[0.2,0.4,0.6,0.375]}}}");
  FKKRuntimeMaterialBinding EB;if(!OKPrepareRuntimeSharedMaterialBinding(EyeWhite,FKKRuntimeMaterialScene{},EB,Error)||EB.Vectors.FindRef(TEXT("_Color"))!=FLinearColor(.2,.4,.6,.375)||EB.Scalars.FindRef(TEXT("_Color__A"))!=.375f){Error=TEXT("Eye white independent color alpha oracle failed");return false;}
  auto* EM=OKCreateRuntimeSharedMaterial(EyeWhite,Textures,SharedParents[7],FKKRuntimeMaterialScene{},Error);if(!EM||EM->IsTwoSided()||EM->GetBlendMode()!=BLEND_Translucent)return false;Instances.Emplace(EM);
  if(OKCreateRuntimeSharedMaterial(EyeWhite,Textures,SharedParents[6],FKKRuntimeMaterialScene{},Error)){Error=TEXT("Crossed eye white/studio parent admitted");return false;}
  EyeWhite.EyeDraw=2;if(OKRuntimeSharedMaterialRecipe(EyeWhite)!=EKKRuntimeMaterialRecipe::None){Error=TEXT("Iris misclassified as eye white");return false;}
  FKKRuntimeMaterialPart Iris;Iris.ShaderKind=0;Iris.Unlit=1;Iris.EyeDraw=2;Iris.Blend=TEXT("translucent");Iris.SourceMetadataJson=TEXT("{\"schema\":1,\"material\":{\"shader\":\"Shader Forge/toon_eye_lod0\",\"floats\":{\"_rotation\":0.25,\"_isHighLight\":0},\"colors\":{\"_overcolor1\":[1,1,1,1]}}}");
  Iris.Values.Add(TEXT("_OKEyeExpressionUVPrepared"),FVector4f(1,0,0,0));Iris.Values.Add(TEXT("_OKEyeUVPrepared"),FVector4f(1,0,0,0));Iris.Values.Add(TEXT("_MainTex_ST"),FVector4f(2,3,.25,.75));Iris.Values.Add(TEXT("_EyeLayerColor0"),FVector4f(.2,.4,.6,.375));Iris.Values.Add(TEXT("_EyeLayerColor1"),FVector4f(.6,.4,.2,.625));Iris.Values.Add(TEXT("_EyeHighlight"),FVector4f(1,0,0,0));
  Iris.Textures.Add({TEXT("_overtex1"),1});Iris.Textures.Add({TEXT("_EyeLayer0"),0});Iris.Textures.Add({TEXT("_overtex2"),1});
  FKKRuntimeMaterialBinding IB;if(!OKPrepareRuntimeSharedMaterialBinding(Iris,FKKRuntimeMaterialScene{},IB,Error)||IB.Scalars.FindRef(TEXT("_rotation"))!=0||IB.Scalars.FindRef(TEXT("_isHighLight"))!=1||IB.Vectors.FindRef(TEXT("_overtex1_ST"))!=FLinearColor(1,1,0,0)||IB.Vectors.FindRef(TEXT("_MainTex_ST"))!=FLinearColor(2,3,.25,.75)||IB.Scalars.FindRef(TEXT("_overcolor1__A"))!=.375f||!IB.Textures.Contains(TEXT("_overtex1"))||IB.Textures.FindRef(TEXT("_overtex1"))!=0||IB.Textures.Contains(TEXT("_overtex2"))){Error=TEXT("Iris prepared UV/independent alpha/resolved texture oracle failed");return false;}
  auto* IM=OKCreateRuntimeSharedMaterial(Iris,Textures,SharedParents[8],FKKRuntimeMaterialScene{},Error);if(!IM||IM->IsTwoSided()||IM->GetBlendMode()!=BLEND_Translucent)return false;Instances.Emplace(IM);
  if(OKCreateRuntimeSharedMaterial(Iris,Textures,SharedParents[7],FKKRuntimeMaterialScene{},Error)){Error=TEXT("Iris admitted eye-white parent");return false;}
  Iris.Values.Remove(TEXT("_OKEyeExpressionUVPrepared"));const auto PreviousExpressionBinding=IB.Vectors.FindRef(TEXT("_MainTex_ST"));if(OKPrepareRuntimeSharedMaterialBinding(Iris,FKKRuntimeMaterialScene{},IB,Error)||IB.Vectors.FindRef(TEXT("_MainTex_ST"))!=PreviousExpressionBinding){Error=TEXT("Missing Forge expression contract admitted/changed output");return false;}Iris.Values.Add(TEXT("_OKEyeExpressionUVPrepared"),FVector4f(1,0,0,0));
  const auto PreviousIris=IB.Vectors.FindRef(TEXT("_MainTex_ST"));Iris.Values.Remove(TEXT("_OKEyeUVPrepared"));if(OKPrepareRuntimeSharedMaterialBinding(Iris,FKKRuntimeMaterialScene{},IB,Error)||IB.Vectors.FindRef(TEXT("_MainTex_ST"))!=PreviousIris){Error=TEXT("Missing iris sampling contract admitted/changed output");return false;}
  FKKRuntimeMaterialPart KanoEye;KanoEye.ShaderKind=0;KanoEye.Unlit=1;KanoEye.Blend=TEXT("translucent");KanoEye.SourceMetadataJson=TEXT("{\"schema\":1,\"material\":{\"shader\":\"Koikano/main_eyew\",\"colors\":{\"_Color\":[0.3,0.5,0.7,0.375]},\"floats\":{\"_Cutoff\":0.25}}}");KanoEye.Values.Add(TEXT("_VerticalShift"),FVector4f(.625,0,0,0));
  FKKRuntimeMaterialScene KanoScene;KanoScene.Uniforms.Add(TEXT("unity_ProbeVolumeWorldToObject_Row2"),FLinearColor(0,0,2,.75));KanoScene.Uniforms.Add(TEXT("unity_SHAr"),FLinearColor(.1,.2,.3,.4));
  FKKRuntimeMaterialBinding KB;if(!OKPrepareRuntimeSharedMaterialBinding(KanoEye,KanoScene,KB,Error)||KB.Scalars.FindRef(TEXT("_KanoVerShift"))!=.625f||KB.Scalars.FindRef(TEXT("_Color__A"))!=.375f||KB.Scalars.FindRef(TEXT("_Cutoff"))!=.25f||KB.Vectors.FindRef(TEXT("unity_ProbeVolumeWorldToObject_Row2"))!=FLinearColor(0,0,2,.75)||KB.Scalars.FindRef(TEXT("unity_ProbeVolumeWorldToObject_Row2__A"))!=.75f||KB.Vectors.FindRef(TEXT("unity_SHAr"))!=FLinearColor(.1,.2,.3,.4)){Error=TEXT("Koikano eye source alpha/scene matrix oracle failed");return false;}
  auto* KM=OKCreateRuntimeSharedMaterial(KanoEye,Textures,SharedParents[9],KanoScene,Error);if(!KM||KM->IsTwoSided()||KM->GetBlendMode()!=BLEND_Translucent)return false;Instances.Emplace(KM);
  UTexture* Probe=nullptr;if(!KM->GetTextureParameterValue(FMaterialParameterInfo(TEXT("unity_ProbeVolumeSH")),Probe)||!Cast<UVolumeTexture>(Probe)){Error=TEXT("Shared probe volume missing/wrong type");return false;}
  FlushRenderingCommands();if(!Probe->GetResource()||!Probe->GetResource()->TextureRHI.IsValid()||Probe->GetResource()->TextureRHI->GetDesc().Dimension!=ETextureDimension::Texture3D){Error=TEXT("Cooked common probe volume RHI unavailable");return false;}
  if(OKCreateRuntimeSharedMaterial(KanoEye,Textures,SharedParents[7],KanoScene,Error)){Error=TEXT("Koikano eye admitted Forge eye parent");return false;}
  const auto SavedKB=KB.Vectors.FindRef(TEXT("_Color"));KanoScene.Uniforms.Add(TEXT("unity_ProbeVolumeWorldToObject_Row2"),FLinearColor(0,0,NAN,0));if(OKPrepareRuntimeSharedMaterialBinding(KanoEye,KanoScene,KB,Error)||KB.Vectors.FindRef(TEXT("_Color"))!=SavedKB){Error=TEXT("Nonfinite scene matrix admitted/changed output");return false;}
  FKKRuntimeMaterialPart KanoIris=Iris;KanoIris.EyeDraw=1;KanoIris.SourceMetadataJson=TEXT("{\"schema\":1,\"material\":{\"shader\":\"Koikano/main_eye\",\"floats\":{\"_rotation\":0.25,\"_rotation1\":0.125,\"_rotation2\":0.75,\"_rotation3\":0.8},\"colors\":{\"_overcolor3\":[0.2,0.3,0.4,0.625]}}}");KanoIris.Values.Add(TEXT("_OKEyeUVPrepared"),FVector4f(1,0,0,0));KanoIris.Values.Add(TEXT("_OKEyeExpressionUVPrepared"),FVector4f(1,0,0,0));KanoIris.Values.Add(TEXT("_EyeColor"),FVector4f(.3,.4,.5,.25));KanoIris.Values.Add(TEXT("_VerticalShift"),FVector4f(.375,0,0,0));
  FKKRuntimeMaterialScene IrisScene;IrisScene.Uniforms.Add(TEXT("unity_SpecCube0_HDR"),FLinearColor(2,1,0,1));FKKRuntimeMaterialBinding KIB;
  if(!OKPrepareRuntimeSharedMaterialBinding(KanoIris,IrisScene,KIB,Error)||KIB.Scalars.FindRef(TEXT("_rotation"))!=0||KIB.Scalars.FindRef(TEXT("_rotation1"))!=.5f||KIB.Scalars.FindRef(TEXT("_rotation2"))!=.5f||KIB.Scalars.FindRef(TEXT("_rotation3"))!=.8f||KIB.Scalars.FindRef(TEXT("_Color__A"))!=.25f||KIB.Scalars.FindRef(TEXT("_overcolor3__A"))!=.625f||KIB.Vectors.FindRef(TEXT("unity_SpecCube0_HDR"))!=FLinearColor(2,1,0,1)){Error=TEXT("Koikano iris resolved/third-layer/scene HDR oracle failed");return false;}
  const auto* KL=OKRuntimeSharedMaterialLayout(EKKRuntimeMaterialRecipe::KoikanoEye);int32 CubeSamplers=0;for(const auto& T:KL->Textures)if(T.Dim==4&&T.Sampler==0)++CubeSamplers;if(CubeSamplers!=2){Error=TEXT("Native shared cubemap sampler contract differs");return false;}
  auto* KI=OKCreateRuntimeSharedMaterial(KanoIris,Textures,SharedParents[10],IrisScene,Error);if(!KI)return false;Instances.Emplace(KI);FlushRenderingCommands();for(const auto* Name:{TEXT("unity_SpecCube0"),TEXT("unity_SpecCube1")}){UTexture* Cube=nullptr;if(!KI->GetTextureParameterValue(FMaterialParameterInfo(Name),Cube)||!Cast<UTextureCube>(Cube)||!Cube->GetResource()||!Cube->GetResource()->TextureRHI.IsValid()||Cube->GetResource()->TextureRHI->GetDesc().Dimension!=ETextureDimension::TextureCube){Error=TEXT("Runtime common reflection cube RHI missing/wrong dimension");return false;}}
  if(OKCreateRuntimeSharedMaterial(KanoIris,Textures,SharedParents[8],IrisScene,Error)){Error=TEXT("Koikano iris admitted Forge iris parent");return false;}
  KanoIris.Values.Remove(TEXT("_OKEyeExpressionUVPrepared"));if(OKPrepareRuntimeSharedMaterialBinding(KanoIris,IrisScene,KIB,Error)){Error=TEXT("Missing expression UV contract admitted");return false;}
  FVector2f Expression;if(!OKRuntimeEyeExpressionUV(FVector2f(.7,.8),.25,Expression)||!Expression.Equals(FVector2f(.2,.7),1.e-6f)){Error=TEXT("Native clockwise eye expression UV inverse oracle failed");return false;}const auto PreviousExpression=Expression;if(OKRuntimeEyeExpressionUV(FVector2f(.2,.4),NAN,Expression)||Expression!=PreviousExpression){Error=TEXT("Invalid expression UV changed output");return false;}




  FlushRenderingCommands();Stats.SharedParentUsed=true;
 }
 auto Bad=D.Textures[0];Bad.Mips[0].BGRA.Reset();if(OKCreateRuntimePreparedTexture(Bad,Error)){Error=TEXT("Invalid runtime texture admitted");return false;}
 auto Duplicate=D.Textures[0];const auto Extra=Duplicate.Mips.Last();Duplicate.Mips.Add(Extra);if(OKCreateRuntimePreparedTexture(Duplicate,Error)){Error=TEXT("Duplicate terminal runtime mip admitted");return false;}
 Stats.Parts=D.Parts.Num();Stats.Textures=D.Textures.Num();Stats.PixelBytes=Bytes;Stats.PrepareMs=D.PrepareMs;Error.Reset();return true;
}
bool OKAuditRuntimePoseOnGpu(const FKKRuntimePoseRequest& Request,const FKKRuntimePoseResult& Result,double& MaxError,FString& Error,const FKKRuntimePoseResult* First,FOKResidentPoseStats& OutStats,FOKGpuResidencyStats& OutBudgetStats,FOKGpuClipWindowStats& OutWindow,TConstArrayView<FKKRuntimeMeshPart> Meshes,double& MeshError,FKKRuntimeRenderMeshStats& MeshStats){
#if WITH_EDITOR
 if(!FParse::Param(FCommandLine::Get(),TEXT("AllowCommandletRendering"))){Error=TEXT("GPU audit requires AllowCommandletRendering");return false;}
 if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
 if(!ReductionOracle(Error))return false;
 TArray<FKKRuntimeMeshPart> OwnedMeshes;OwnedMeshes.Append(Meshes.GetData(),Meshes.Num());auto LowFuture=OKReduceRuntimeMeshesAsync(MoveTemp(OwnedMeshes));auto Low=LowFuture.Consume();
 if(!Low.Error.IsEmpty()||!Low.WorkerThread){Error=Low.Error;return false;}
 Meshes=Low.Meshes;MeshStats.ReductionMs=Low.PrepareMs;MeshStats.BudgetsMet=Low.BudgetsMet;
 for(const auto& P:Low.Parts){MeshStats.Groups[P.Group]+=P.Triangles;MeshStats.RetainedParts+=!P.RetentionReason.IsEmpty();MeshStats.Reduced|=P.Triangles<P.SourceTriangles;UE_LOG(LogTemp,Display,TEXT("Runtime low part=%d group=%d original=%u target=%u actual=%u retention=%s"),P.NativePart,P.Group,P.SourceTriangles,P.TargetTriangles,P.Triangles,*P.RetentionReason);}
 if(!RenderMeshOracle(Meshes,Request.Bones,MeshStats,Error))return false;
 if(!ClockOracle(Error)){if(Error.IsEmpty())Error=TEXT("Runtime playback clock oracle failed");return false;}
 if(!HundredClipWindowOracle(Error))return false;
 if(!Result.Passed()||(First&&!First->Passed()))return false;
 TArray<const FKKRuntimePoseResult*> Results;if(First)Results.Add(First);Results.Add(&Result);
 FOKGpuActorRecord Actor;Actor.CharacterId=Result.CharacterIdentity;Actor.PoseCount=Request.Bones.Num();FOKGpuSkinPaletteData Palette;Palette.PoseCount=Actor.PoseCount;
 for(int32 B=0;B<Request.Bones.Num();++B){const auto& Bone=Request.Bones[B];Actor.PoseBones.Add({Bone.Name.ToString(),Bone.Parent,OKGpuBoneReferenceIdentity(Bone.Reference)});const uint32 Bind=Palette.InverseBindRows.Num();Palette.InverseBindRows.Append({FVector4f(1,0,0,0),FVector4f(0,1,0,0),FVector4f(0,0,1,0)});Palette.Items.Add(FUintVector4(B,Bind,B,0));}
 FOKGpuMeshRecord Mesh;Mesh.PartId=TEXT("runtime-pose-diagnostic");Mesh.AssetPath=TEXT("/Game/RuntimePoseDiagnostic/PrivateMesh");Mesh.Bones={0,Actor.PoseCount};Actor.Meshes.Add(MoveTemp(Mesh));
 TArray<FOKRuntimeGpuClip> Clips;TArray<FOKRuntimeGpuPlaybackClip> Catalog;
 for(const auto* R:Results){if(R->CharacterIdentity!=Result.CharacterIdentity){Error=TEXT("Runtime clips have different private rigs");return false;}Clips.Add({R->ClipIdentity,R->Duration,R->Keys,R->Tracks});Catalog.Add({R->ClipIdentity,R->Duration,true});}
 FOKGpuAnimationBundle Bank;if(!OKBuildRuntimeGpuActorBank(MoveTemp(Actor),MoveTemp(Clips),MoveTemp(Palette),Bank,Error)||!OKPackConstantGpuPoseChannels(Bank.Data.Pose,Error))return false;
 // Diagnostic only: wait for the worker selector here; gameplay consumes its future asynchronously.
 auto Source=MakeShared<FOKGpuAnimationBundle,ESPMode::ThreadSafe>();*Source=MoveTemp(Bank);
 TArray<FOKGpuClipWindow> OneSelection;OneSelection.Add({Result.CharacterIdentity,{Results.Last()->ClipIdentity}});
 auto OneFuture=OKSelectGpuClipWindowAsync(Source,OneSelection);auto One=OneFuture.Consume();
 if(!One.WorkerThread||!One.Bundle){Error=One.Error;return false;}
 const auto& Expected=*Results.Last();const auto& Selected=One.Bundle->Data.Pose;
 if(Selected.Tracks.Num()!=Expected.Tracks.Num()||Selected.Keys.Num()>Source->Data.Pose.Keys.Num()){Error=TEXT("Clip window retained unselected tracks/keys");return false;}
 for(int32 B=0;B<Selected.Tracks.Num();++B){const auto& T=Selected.Tracks[B];const auto& E=Expected.Tracks[B];if(T.Y!=E.Y||T.Z!=E.Z||(T.W&1)!=E.W){Error=TEXT("Clip selection altered private track");return false;}for(uint32 K=0;K<T.Y;++K)if(Selected.Keys[OKGpuTrackPositionRow(T,K)]!=Expected.Keys[(E.X+K)*3]||Selected.Keys[OKGpuTrackRotationRow(T,K)]!=Expected.Keys[(E.X+K)*3+1]||Selected.Keys[OKGpuTrackScaleRow(T,K)]!=Expected.Keys[(E.X+K)*3+2]){Error=TEXT("Lossless packed clip selection altered key values");return false;}}
 FOKGpuAnimationBundle Unchanged;Unchanged.Data.Pose.Keys.Add(FVector4f(7,8,9,10));auto Reject=[&](TArray<FOKGpuClipWindow> Invalid,uint64 Budget){FString Why;return !OKSelectGpuClipWindow(*Source,Invalid,Unchanged,Why,Budget)&&!Why.IsEmpty()&&Unchanged.Data.Pose.Keys.Num()==1&&Unchanged.Data.Pose.Keys[0]==FVector4f(7,8,9,10);};
 auto Invalid=OneSelection;Invalid[0].CharacterId=TEXT("stale-character");if(!Reject(Invalid,MAX_int32))return false;
 Invalid=OneSelection;Invalid[0].Clips={TEXT("missing-clip")};if(!Reject(Invalid,MAX_int32))return false;
 Invalid=OneSelection;const FString Duplicate=Invalid[0].Clips[0];Invalid[0].Clips.Add(Duplicate);if(!Reject(Invalid,MAX_int32)||!Reject(OneSelection,1))return false;
 Invalid=OneSelection;Invalid[0].Clips.Init(Expected.ClipIdentity,4);if(!Reject(Invalid,MAX_int32))return false;
 TArray<FOKGpuPosePlayback> InputPlay,Remapped;InputPlay.AddDefaulted();InputPlay[0].ClipA=Results.Num()-1;InputPlay[0].ClipB=0;InputPlay[0].Times=FVector4f(.37*Expected.Duration,.731*Results[0]->Duration,0,0);
 if(!OKRemapGpuClipWindowPlayback(Source->Manifest,One.Bundle->Manifest,InputPlay,Remapped,Error)||Remapped[0].ClipA!=0||Remapped[0].ClipB!=0||Remapped[0].Times!=InputPlay[0].Times)return false;
 if(Results.Num()>1){InputPlay[0].Times.Z=.5f;const auto Saved=Remapped[0];FString Why;if(OKRemapGpuClipWindowPlayback(Source->Manifest,One.Bundle->Manifest,InputPlay,Remapped,Why)||Why.IsEmpty()||Remapped.Num()!=1||Remapped[0].Times!=Saved.Times)return false;}
 double WindowError=0;if(!WindowGpuOracle(*One.Bundle,Request,Expected,WindowError,Error))return false;
 TArray<FOKGpuClipWindow> Reverse;Reverse.Add({Result.CharacterIdentity,{}});for(int32 I=Results.Num()-1;I>=0;--I)Reverse[0].Clips.Add(Results[I]->ClipIdentity);
 auto ReorderedFuture=OKSelectGpuClipWindowAsync(Source,MoveTemp(Reverse));auto Reordered=ReorderedFuture.Consume();if(!Reordered.WorkerThread||!Reordered.Bundle){Error=Reordered.Error;return false;}
 InputPlay[0].Times.Z=.5f;if(!OKRemapGpuClipWindowPlayback(Source->Manifest,Reordered.Bundle->Manifest,InputPlay,Remapped,Error)||Remapped[0].ClipA!=0||Remapped[0].ClipB!=uint32(Results.Num()-1)||Remapped[0].Times!=InputPlay[0].Times)return false;
 OutWindow=One.Window;Bank=MoveTemp(*Reordered.Bundle);Algo::Reverse(Results);Algo::Reverse(Catalog);
 TArray<FOKGpuPosePlayback> Samples;for(float Phase:{0.f,.137f,.37f,.731f,.99f,1.f}){FOKGpuPosePlayback P;P.ClipA=P.ClipB=0;P.Times=FVector4f(Phase*Results[0]->Duration,Phase*Results[0]->Duration,0,0);Samples.Add(P);}
 FOKGpuPosePlayback InitialMesh;InitialMesh.ClipA=InitialMesh.ClipB=Results.Num()-1;InitialMesh.Times=FVector4f(0,0,0,0);const int32 MeshSample=Samples.Num();Samples.Add(InitialMesh);
 if(Results.Num()>1){FOKRuntimeGpuPlayback Clock;if(!Clock.Initialize(Catalog,0,.37*Results[0]->Duration,Error)||!Clock.RequestClip(1,.4,.137*Results[1]->Duration,Error))return false;
  Samples.Add(Clock.Parameters());for(int32 I=0;I<5;++I){if(!Clock.Advance(.1,Error))return false;Samples.Add(Clock.Parameters());}
  Clock.SetPaused(true);if(!Clock.Advance(1.,Error))return false;Samples.Add(Clock.Parameters());Clock.SetPaused(false);
  if(!Clock.RequestClip(0,.2,0,Error)||!Clock.Advance(.1,Error))return false;Samples.Add(Clock.Parameters());if(!Clock.Advance(.15,Error))return false;Samples.Add(Clock.Parameters());
 }
 const auto& D=Bank.Data.Pose;const uint64 BankBytes=uint64(D.Keys.Num())*16+uint64(D.Tracks.Num())*16+uint64(D.Actors.Num())*32+uint64(D.Tasks.Num())*112+uint64(D.InverseBindRows.Num())*16;
 const uint64 SharedPrivateBytes=uint64(D.Tasks.Num())*96+uint64(D.Actors.Num())*16;
 auto Manager=MakeShared<FOKGpuResidencyBudget,ESPMode::ThreadSafe>(BankBytes*2+SharedPrivateBytes);
 bool CancellationVerified=false;
 ENQUEUE_RENDER_COMMAND(RuntimeUnusedLeaseAudit)([&](FRHICommandListImmediate&){uint64 Ticket=0;FString Why;uint64 Child=0;CancellationVerified=Manager->Reserve(BankBytes,Ticket,Why)&&Manager->ReserveShared(Ticket,SharedPrivateBytes,Child,Why)&&!Manager->CancelUnused(Ticket)&&Manager->CancelUnused(Child)&&!Manager->CancelUnused(Child)&&Manager->CancelUnused(Ticket)&&!Manager->CancelUnused(Ticket)&&Manager->GetStats().ChargedBytes==0;});FlushRenderingCommands();
 if(!CancellationVerified){Error=TEXT("GPU unused reservation cancellation failed");return false;}
 FOKResidentGpuPose Secondary,SharedHistory;bool SharedReady=false;
 ENQUEUE_RENDER_COMMAND(RuntimeSecondBank)([&](FRHICommandListImmediate& R){FRDGBuilder G(R);SharedReady=Secondary.InitializeManaged(G,D,Bank.Data.OwnedClips,Manager,Error)&&SharedHistory.InitializeShared(Secondary,Error);G.Execute();});FlushRenderingCommands();
 if(!SharedReady)return false;
 FOKResidentGpuPose Pose;FOKResidentPoseStats Stats;const auto& Owned=Bank.Data.OwnedClips;TArray<FOKGpuPosePlayback> Playback;Playback.AddDefaulted();MaxError=WindowError;bool Passed=true;uint64 Frame=0;TArray<FVector4f> LastRows;
 for(const auto& Sample:Samples){Playback[0]=Sample;TArray<FVector4f> Current,Previous;
  if(!OKReadbackResidentGpuPose(Pose,Frame==0?&Bank.Data.Pose:nullptr,Owned,Playback,Frame,false,Current,Previous,Stats,Error,Manager)){Passed=false;break;}++Frame;
  if(Current.Num()!=Request.Bones.Num()*3){Error=TEXT("Runtime GPU pose row count differs");Passed=false;break;}
  if(Frame==uint64(MeshSample+1)&&!OKAuditRuntimeMeshInitialPose(Meshes,Current,MeshError,Error)){Passed=false;break;}
  LastRows=Current;
  TArray<FTransform> World;World.SetNum(Request.Bones.Num());
  for(int32 B=0;B<World.Num();++B){const auto A=SampleLocal(*Results[Sample.ClipA],B,Sample.Times.X),Z=SampleLocal(*Results[Sample.ClipB],B,Sample.Times.Y);const float Mix=Sample.Times.Z;
   World[B]=FTransform(FQuat::Slerp(A.GetRotation(),Z.GetRotation(),Mix).GetNormalized(),FMath::Lerp(A.GetTranslation(),Z.GetTranslation(),Mix),FMath::Lerp(A.GetScale3D(),Z.GetScale3D(),Mix));
   const int32 Parent=Request.Bones[B].Parent;if(Parent>=0)World[B]=World[B]*World[Parent];const FMatrix M=World[B].ToMatrixWithScale();
   for(int32 R=0;R<3;++R)for(int32 C=0;C<4;++C){const double Actual=Current[B*3+R][C];if(!FMath::IsFinite(Actual)){Passed=false;Error=TEXT("Runtime GPU pose is nonfinite");}MaxError=FMath::Max(MaxError,FMath::Abs(Actual-M.M[C][R]));}
  }
 }
 const FString SampleFailure=Passed?FString():Error;
 // Reject replacement before uploading, preserve generation and the original pose.
 bool BudgetVerified=false;
 ENQUEUE_RENDER_COMMAND(RuntimeGpuBudgetAudit)([&](FRHICommandListImmediate& R){
  FRDGBuilder G(R);const auto Before=Pose.GetStats();FString Why;
  const bool Rejected=!Pose.Initialize(G,Bank.Data.Pose,Owned,Why,1);
  const auto After=Pose.GetStats();
  BudgetVerified=Rejected&&!Why.IsEmpty()&&After.Generation==Before.Generation&&After.Initializations==Before.Initializations&&After.ImmutableUploadBytes==Before.ImmutableUploadBytes&&After.BudgetRejections==Before.BudgetRejections+1&&After.ResidentBufferBytes>0&&After.ReservedPeakBufferBytes<=512ull*1024*1024;
  FOKResidentGpuPose Third;const auto BeforeBudget=Manager->GetStats();FString WhyAggregate;
  const bool AggregateRejected=!Third.InitializeManaged(G,D,Owned,Manager,WhyAggregate);
  const auto AfterBudget=Manager->GetStats();
  BudgetVerified=BudgetVerified&&AggregateRejected&&!WhyAggregate.IsEmpty()&&BeforeBudget.LiveBanks==3&&AfterBudget.ChargedBytes==BeforeBudget.ChargedBytes&&AfterBudget.Rejected==BeforeBudget.Rejected+1;
  G.Execute();
 });FlushRenderingCommands();
 if(Passed&&BudgetVerified){TArray<FVector4f> Current,Previous;Playback[0]=Samples.Last();if(!OKReadbackResidentGpuPose(Pose,nullptr,Owned,Playback,Frame,false,Current,Previous,Stats,Error))Passed=false;else if(Current.Num()!=LastRows.Num()||FMemory::Memcmp(Current.GetData(),LastRows.GetData(),Current.Num()*sizeof(FVector4f))){Error=TEXT("Budget rejection altered retained GPU pose rows");Passed=false;}}
 if(!BudgetVerified){Error=TEXT("Runtime GPU budget rejection changed resident resources");Passed=false;}
 OutStats=Stats;
 ENQUEUE_RENDER_COMMAND(ReleaseRuntimePoseAudit)([&](FRHICommandListImmediate& R){Pose.Release();Secondary.Release();SharedHistory.Release();const auto Retiring=Manager->GetStats();if(Retiring.LiveBanks!=0||Retiring.RetiringBanks!=3||Retiring.ChargedBytes!=BankBytes*2+SharedPrivateBytes)BudgetVerified=false;R.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);});FlushRenderingCommands();
 // Diagnostic-only polling; gameplay Collect never waits for the GPU.
 bool Collected=false;const double RetirementStart=FPlatformTime::Seconds();while(FPlatformTime::Seconds()-RetirementStart<10){ENQUEUE_RENDER_COMMAND(CollectRuntimeBanks)([&](FRHICommandListImmediate&){Manager->Collect();const auto S=Manager->GetStats();OutBudgetStats=S;Collected=S.ChargedBytes==0&&S.RetiringBanks==0;});FlushRenderingCommands();if(Collected)break;FPlatformProcess::Sleep(.01f);}
 if(!Collected||!BudgetVerified){Error=TEXT("Aggregate GPU bank retirement/capacity oracle failed");Passed=false;}
 if(Passed&&Results.Num()>1&&!Stats.ReusedFrames){Error=TEXT("Paused runtime clock failed to reuse resident GPU pose");Passed=false;}
 if(MaxError>.001){Error=TEXT("Runtime GPU component matrix error exceeds 0.001");Passed=false;}if(!SampleFailure.IsEmpty())Error=SampleFailure;return Passed;
}
