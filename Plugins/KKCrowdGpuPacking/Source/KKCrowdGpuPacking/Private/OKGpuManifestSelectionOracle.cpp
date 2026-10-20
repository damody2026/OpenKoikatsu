#include "OKGpuAnimationManifest.h"
#include "RenderingThread.h"
#include "RenderGraphBuilder.h"
#include "Misc/FileHelper.h"
#include "ReferenceSkeleton.h"

bool OKRunGpuManifestSelectionOracle(const FString& Output){
 FOKGpuAnimationCacheData D;FOKGpuAnimationManifest M;TArray<FOKGpuPosePlayback> Playback;TArray<FVector4f> Native;
 for(uint32 A=0;A<100;++A){uint32 Bones=2+A%5,Base=D.Pose.Tracks.Num(),Pose=D.Pose.Tasks.Num(),Dest=D.Palette.Items.Num(),Bind=D.Palette.InverseBindRows.Num();FOKGpuActorRecord Actor;Actor.CharacterId=FString::Printf(TEXT("synthetic:%u"),A);Actor.PoseFirst=Pose;Actor.PoseCount=Bones;for(uint32 B=0;B<Bones;++B)Actor.PoseBones.Add({FString::Printf(TEXT("bone:%u"),B),B?int32(B-1):INDEX_NONE,OKGpuBoneReferenceIdentity(FTransform::Identity)});
  for(uint32 C=0;C<20;++C){uint32 Start=D.Pose.Tracks.Num();Actor.Clips.Add({FString::Printf(TEXT("clip:%u"),C),Start,1});for(uint32 B=0;B<Bones;++B){uint32 Key=D.Pose.Keys.Num()/3;D.Pose.Tracks.Add(FUintVector4(Key,4,B?B-1:MAX_uint32,0));for(int Sample=0;Sample<4;++Sample){const float K=Sample/3.f;D.Pose.Keys.Add(FVector4f(A*.01f+C*.01f*K,B*.2f+K*.1f,C*.013f+B*.05f*K,K));D.Pose.Keys.Add(FVector4f(0,0,0,1));D.Pose.Keys.Add(FVector4f(1,1,1,0));}}}
  FOKGpuPosePlayback P;P.ClipA=A*3%20;P.ClipB=(A*7+1)%20;P.Times=FVector4f(FMath::Frac(A*.073f),FMath::Frac(A*.171f),float(A%7)/6,0);Playback.Add(P);D.Pose.Actors.Add(FUintVector4(Base+P.ClipA*Bones,Base+P.ClipB*Bones,Bones,0));D.Pose.Times.Add(P.Times);TArray<uint32> Owned;for(uint32 C=0;C<20;++C)Owned.Add(Base+C*Bones);D.OwnedClips.Add(MoveTemp(Owned));
  FVector3f Component(0,0,0);for(uint32 B=0;B<Bones;++B){uint32 At=D.Pose.InverseBindRows.Num();D.Pose.InverseBindRows.Append({FVector4f(1,0,0,0),FVector4f(0,1,0,0),FVector4f(0,0,1,0)});D.Pose.Tasks.Add(FUintVector4(B,A,At,0));uint32 Skin=D.Palette.InverseBindRows.Num();D.Palette.InverseBindRows.Append({FVector4f(1,0,0,-float(A)*.001f),FVector4f(0,1,0,0),FVector4f(0,0,1,-float(B)*.1f)});D.Palette.Items.Add(FUintVector4(Pose+B,Skin,D.Palette.Items.Num(),0));
   auto Sample=[&](uint32 C,float T){return FVector3f(A*.01f+C*.01f*T,B*.2f+T*.1f,C*.013f+B*.05f*T);};Component+=FMath::Lerp(Sample(P.ClipA,P.Times.X),Sample(P.ClipB,P.Times.Y),P.Times.Z);Native.Append({FVector4f(1,0,0,Component.X),FVector4f(0,1,0,Component.Y),FVector4f(0,0,1,Component.Z)});
  }
  for(uint32 B=0;B<Bones;++B){uint32 Bone=Bones-B-1;D.Palette.Items.Add(FUintVector4(Pose+Bone,Bind+Bone*3,D.Palette.Items.Num(),0));}FOKGpuMeshRecord Mesh;Mesh.PartId=TEXT("body");Mesh.AssetPath=TEXT("/Game/GpuPoseAudit/Synthetic.Synthetic");Mesh.Bones={Dest,Bones};Mesh.Sections.Add({Dest+Bones,Bones});Actor.Meshes.Add(MoveTemp(Mesh));M.Actors.Add(MoveTemp(Actor));
 }
 D.Palette.PoseCount=D.Pose.Tasks.Num();FString Error;TArray<uint8> Bytes;if(!OKPackConstantGpuPoseChannels(D.Pose,Error)||!M.Validate(D,Error)||!OKEncodeGpuAnimationCache(D,M.CacheIdentity,Bytes,Error))return false;M.CacheIdentity=FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num());if(!OKEncodeGpuAnimationCache(D,M.CacheIdentity,Bytes,Error))return false;FOKGpuAnimationCacheData Loaded;if(!OKDecodeGpuAnimationCache(Bytes,M.CacheIdentity,Loaded,Error))return false;D=MoveTemp(Loaded);TArray<uint8> Meta;if(!OKEncodeGpuAnimationManifest(M,Meta,Error))return false;FOKGpuAnimationManifest LoadedM;if(!OKDecodeGpuAnimationManifest(Meta,M.CacheIdentity,LoadedM,Error)||!LoadedM.Validate(D,Error))return false;M=MoveTemp(LoadedM);
 // Private runtime mesh bindings survive serialization, selection and rebasing.
 FReferenceSkeleton Ref;{FReferenceSkeletonModifier Modifier(Ref,nullptr);for(const auto& Bone:M.Actors[0].PoseBones)Modifier.Add(FMeshBoneInfo(FName(*Bone.Name),Bone.Name,Bone.Parent),FTransform::Identity);}
 TArray<FMatrix44f> Inverse;Inverse.Init(FMatrix44f::Identity,Ref.GetNum());FOKGpuSkinPaletteData Runtime;
 if(!OKBuildGpuMeshPalette(M.Actors[0],Ref,Inverse,D.Palette.PoseCount,Runtime,Error))return false;
 for(int32 B=0;B<Ref.GetNum();++B)if(Runtime.Items[B].X!=M.Actors[0].PoseFirst+B||Runtime.Items[B].Z!=B)return false;
 int BindingRejected=0;for(int Case=0;Case<4;++Case){auto Bad=M.Actors[0];switch(Case){case 0:Bad.PoseBones.Reset();break;case 1:Bad.PoseBones[1].Name=Bad.PoseBones[0].Name;break;case 2:Bad.PoseBones[1].Parent=1;break;case 3:Bad.PoseBones[0].ReferenceIdentity.Hash[0]^=1;break;}FOKGpuSkinPaletteData Sentinel;Sentinel.PoseCount=1234;FString Why;if(!OKBuildGpuMeshPalette(Bad,Ref,Inverse,D.Palette.PoseCount,Sentinel,Why)&&Sentinel.PoseCount==1234)++BindingRejected;}
 if(BindingRejected!=4)return false;

 TArray<uint32> Left,Right;for(uint32 I=0;I<100;++I)(I<50?Left:Right).Add((I*37+13)%100);FOKGpuAnimationCacheData L,R,Selected;FOKGpuAnimationManifest LM,RM,SelectedM;
 if(!OKSelectGpuAnimationActors(D,M,Left,L,LM,Error)||!OKSelectGpuAnimationActors(D,M,Right,R,RM,Error))return false;TArray<const FOKGpuAnimationCacheData*> Inputs{&R,&L};TArray<const FOKGpuAnimationManifest*> Manifests{&RM,&LM};if(!OKComposeGpuAnimationCaches(Inputs,Manifests,Selected,SelectedM,Error))return false;
 TArray<FOKGpuPosePlayback> Chosen;TArray<int> Original;for(const auto& A:SelectedM.Actors){int I=M.Actors.IndexOfByPredicate([&](const auto& S){return S.CharacterId==A.CharacterId;});if(I<0||A.Clips.Num()!=20||A.PoseBones.Num()!=M.Actors[I].PoseBones.Num())return false;for(int32 B=0;B<A.PoseBones.Num();++B)if(A.PoseBones[B].Name!=M.Actors[I].PoseBones[B].Name||A.PoseBones[B].Parent!=M.Actors[I].PoseBones[B].Parent||FMemory::Memcmp(A.PoseBones[B].ReferenceIdentity.Hash,M.Actors[I].PoseBones[B].ReferenceIdentity.Hash,20))return false;Chosen.Add(Playback[I]);Original.Add(I);}
 FOKResidentGpuPose Pose;FOKGpuSkinPalette Skin;FOKResidentPoseStats PS;FOKGpuSkinPaletteStats SS;TArray<FVector4f> Current,Previous,Palette;bool Passed=OKReadbackResidentSkinPalette(Pose,Skin,&Selected.Pose,Selected.OwnedClips,&Selected.Palette,Chosen,0,Current,Previous,Palette,PS,SS,Error);
 bool CacheQueriesPassed=false;
 ENQUEUE_RENDER_COMMAND(CheckSyntheticHistoryCache)([&](FRHICommandListImmediate& RHI){
  FRDGBuilder G(RHI);const auto Before=Pose.GetStats();FOKResidentPoseFrame Hit,Miss;
  bool Good=Pose.TryReuseCurrent(G,Chosen,Before.Generation,Hit)&&Hit.Current&&Hit.Previous==Hit.Current&&Hit.Reused;
  auto Changed=Chosen;Changed[0].Times.X+=.001f;
  Good&=!Pose.TryReuseCurrent(G,Changed,Before.Generation,Miss)&&!Miss.Current;
  Changed=Chosen;Changed[0].ClipA=(Changed[0].ClipA+1)%20;
  Good&=!Pose.TryReuseCurrent(G,Changed,Before.Generation,Miss)&&!Miss.Current;
  Good&=!Pose.TryReuseCurrent(G,Chosen,Before.Generation+1,Miss)&&!Miss.Current;
  Changed=Chosen;Changed.Pop();Good&=!Pose.TryReuseCurrent(G,Changed,Before.Generation,Miss)&&!Miss.Current;
  const auto After=Pose.GetStats();Good&=Before.Evaluations==After.Evaluations&&Before.ParameterUploadBytes==After.ParameterUploadBytes;
  // Replacing current in the same graph must preserve the borrowed resource.
  Changed=Chosen;Changed[0].Times.X+=.001f;FOKResidentPoseFrame Next;FString Why;
  Good&=Pose.Evaluate(G,Changed,1,Before.Generation,false,Next,Why)&&Next.Current!=Hit.Current&&Next.Previous==Hit.Current;
  G.Execute();Skin.Release();Pose.Release();
  FRDGBuilder ReleasedGraph(RHI);Good&=!Pose.TryReuseCurrent(ReleasedGraph,Chosen,Before.Generation,Miss)&&!Miss.Current;ReleasedGraph.Execute();
  CacheQueriesPassed=Good;
 });FlushRenderingCommands();Passed&=CacheQueriesPassed;double Max=0;int64 Elements=0;
 if(Passed)for(int A=0;A<SelectedM.Actors.Num();++A){const auto& New=SelectedM.Actors[A];const auto& Old=M.Actors[Original[A]];auto Compare=[&](float Actual,float Expected){if(!FMath::IsFinite(Actual)){Passed=false;return;}Max=FMath::Max(Max,FMath::Abs(double(Actual)-Expected));++Elements;};
  for(uint32 B=0;B<New.PoseCount*3;++B)for(int C=0;C<4;++C){Compare(Current[New.PoseFirst*3+B][C],Native[Old.PoseFirst*3+B][C]);Compare(Previous[New.PoseFirst*3+B][C],Native[Old.PoseFirst*3+B][C]);}
  for(uint32 Slot=0;Slot<New.PoseCount*2;++Slot){uint32 B=Slot<New.PoseCount?Slot:New.PoseCount*2-Slot-1;for(int Row=0;Row<3;++Row)for(int C=0;C<4;++C){float Expected=Native[(Old.PoseFirst+B)*3+Row][C];if(C==3){if(Row==0)Expected-=Original[A]*.001f;if(Row==2)Expected-=B*.1f;}uint32 At=(New.Meshes[0].Bones.First+Slot)*3+Row;Compare(Palette[At][C],Expected);Compare(Palette[Selected.Palette.Items.Num()*3+At][C],Expected);}}
 }Passed&=Elements>0&&Max<=.0005;
 FString Json=FString::Printf(TEXT("{\"passed\":%s,\"history_cache_queries_passed\":%s,\"synthetic_only\":true,\"manifest_selection\":true,\"characters\":100,\"clips_per_character\":20,\"sharing\":false,\"compared_elements\":%lld,\"max_error\":%.9g,\"tolerance\":0.0005,\"fps_evidence\":false}"),Passed?TEXT("true"):TEXT("false"),CacheQueriesPassed?TEXT("true"):TEXT("false"),Elements,Max);UE_LOG(LogTemp,Display,TEXT("GPU manifest selection oracle %s error=%s"),*Json,*Error);return FFileHelper::SaveStringToFile(Json,*Output)&&Passed;
}
