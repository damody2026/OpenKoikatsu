#include "KKRuntimePoseCache.h"
#include "KKRuntimePoseScheduler.h"
#include "KKRuntimeRenderRig.h"
#include "KKRuntimeMesh.h"

static bool MeshRemapOracle(FString& Error){
 FKKRuntimeMeshPart P;P.NativePart=0;FKKRuntimeMeshVertex A;A.Influences=3;for(int32 I=0;I<3;++I)A.Skin[I]={uint16(I),21845};P.Vertices={A,A};P.Vertices[1].Skin[2].Bone=4;TArray<FKKRuntimeMeshPart> Meshes{P};TArray<int32> Map{2,0,1,INDEX_NONE,INDEX_NONE};
 if(OKRemapRuntimeMeshBones(Meshes,Map,3,Error)||Meshes[0].Vertices[0].Skin[0].Bone!=0||Meshes[0].Vertices[1].Skin[2].Bone!=4)return false;
 Meshes[0].Vertices[1]=A;auto Duplicate=Map;Duplicate[1]=2;if(OKRemapRuntimeMeshBones(Meshes,Duplicate,3,Error)||Meshes[0].Vertices[0].Skin[0].Bone!=0)return false;
 Meshes[0].Vertices[1].Skin[2].Weight=0;if(OKRemapRuntimeMeshBones(Meshes,Map,3,Error)||Meshes[0].Vertices[0].Skin[0].Bone!=0)return false;
 Meshes[0].Vertices[1]=A;if(!OKRemapRuntimeMeshBones(Meshes,Map,3,Error))return false;
 for(const auto& V:Meshes[0].Vertices)for(int32 I=0;I<3;++I)if(V.Skin[I].Bone!=Map[I]||V.Skin[I].Weight!=21845)return false;
 Error.Reset();return true;
}

static bool RenderRigOracle(FString& Error){
 TArray<FKKRuntimePoseBone> Source;
 Source.Add({TEXT("__ok_scene"),INDEX_NONE,FTransform::Identity});
 Source.Add({TEXT("native-parent"),0,FTransform(FVector(1,2,3))});
 Source.Add({TEXT("socket"),1,FTransform(FVector(4,5,6))});
 Source.Add({TEXT("unused"),0,FTransform::Identity});
 for(int32 Axis=0;Axis<3;++Axis)Source.Add({FName(*FString::Printf(TEXT("__ok_palette_4_-3_%d"),Axis)),0,FTransform::Identity});
 FKKRuntimePalettePart P;P.NativePart=4;P.Slots={-3};TArray<FKKRuntimePalettePart> Parts{P};
 TArray<FKKRuntimePoseBone> Out;TArray<int32> Map;TArray<FName> Extra;
 if(!OKCompactRuntimeRenderRig(Source,Parts,Extra,Out,Map,Error)||Out.Num()!=4||Map[1]!=INDEX_NONE||Map[4]!=1||Out[1].Parent!=0)return false;
 Extra.Add(TEXT("socket"));if(!OKCompactRuntimeRenderRig(Source,Parts,Extra,Out,Map,Error)||Out.Num()!=6||Map[3]!=INDEX_NONE||Out[2].Parent!=1||!Out[2].Reference.Equals(Source[2].Reference,0))return false;
 const auto BeforeMap=Map;const int32 BeforeCount=Out.Num();Extra.Add(TEXT("missing"));
 if(OKCompactRuntimeRenderRig(Source,Parts,Extra,Out,Map,Error)||Map!=BeforeMap||Out.Num()!=BeforeCount)return false;
 Extra.Reset();Parts[0].Slots.Add(2);if(OKCompactRuntimeRenderRig(Source,Parts,Extra,Out,Map,Error)||Map!=BeforeMap)return false;
 Parts={P};Source[2].Parent=2;if(OKCompactRuntimeRenderRig(Source,Parts,Extra,Out,Map,Error)||Map!=BeforeMap)return false;
 Error.Reset();return true;
}

static bool SchedulerOracle(FString& Error){
 TArray<TSharedPtr<TPromise<FKKRuntimePoseResult>>> Promises;TArray<FKKRuntimePoseRequest> Launched;
 auto Request=[](const TCHAR* Clip){FKKRuntimePoseRequest R;R.CharacterIdentity=TEXT("private-rig");R.ClipIdentity=Clip;R.Duration=1;R.Bones.SetNum(1);R.Bones[0].Name=TEXT("root");return R;};
 const uint64 Unit=FKKRuntimePoseCache::Estimate(Request(TEXT("A")));
 FKKRuntimePoseScheduler S(Unit*2,2,3,[&](FKKRuntimePoseRequest R){auto P=MakeShared<TPromise<FKKRuntimePoseResult>>();auto F=P->GetFuture();Promises.Add(P);Launched.Add(MoveTemp(R));return F;});
 auto Finish=[&](int32 I){FKKRuntimePoseResult R;R.CharacterIdentity=Launched[I].CharacterIdentity;R.ClipIdentity=Launched[I].ClipIdentity;R.PreparationIdentity=OKRuntimePoseRecipeIdentity(Launched[I]);R.Duration=1;R.Keys={FVector4f(0,0,0,0),FVector4f(0,0,0,1),FVector4f(1,1,1,0)};R.Tracks={FUintVector4(0,1,MAX_uint32,1)};Promises[I]->SetValue(MoveTemp(R));};
 uint64 A=0,B=0,C=0,D=0;FKKRuntimePoseCompletion Out;
 if(!S.Submit(Request(TEXT("A")),A,Error)||!S.Submit(Request(TEXT("B")),B,Error)||!S.Submit(Request(TEXT("C")),C,Error))return false;
 if(S.Submit(Request(TEXT("D")),D,Error))return false;
 S.Tick();if(S.GetStats().Running!=2||S.GetStats().Queued!=1||Launched.Num()!=2)return false;
 const uint64 Generation=S.BeginSelection();
 if(S.GetCacheStats().ReservedBytes!=Unit*2||S.GetStats().Running!=2||S.GetStats().Queued!=0)return false;
 if(!S.Submit(Request(TEXT("A")),D,Error))return false;S.Tick();if(Launched.Num()!=2)return false;
 Finish(0);Finish(1);S.Tick();
 if(S.Take(A,Out)||S.Take(B,Out)||S.Take(C,Out)||Launched.Num()!=3||S.GetStats().Discarded!=3)return false;
 Finish(2);S.Tick();if(!S.Take(D,Out)||!Out.Data||Out.Selection!=Generation||S.GetCacheStats().PendingJobs||S.GetStats().PeakRunning!=2)return false;
 uint64 Hit=0;if(!S.Submit(Request(TEXT("A")),Hit,Error)||!S.Take(Hit,Out)||!Out.Data||S.GetStats().CacheHits!=1||Launched.Num()!=3)return false;
 auto Large=Request(TEXT("large"));Large.Duration=34;if(S.Submit(MoveTemp(Large),Hit,Error))return false;
 uint64 Cancelled=0;if(!S.Submit(Request(TEXT("cancel")),Cancelled,Error)||!S.Cancel(Cancelled))return false;S.Tick();if(S.GetStats().Running||S.GetStats().Queued||Launched.Num()!=3)return false;
 Error.Reset();return true;
}

bool OKAuditRuntimePoseCache(FString& Error){
 if(!MeshRemapOracle(Error)){Error=TEXT("Runtime mesh transactional remap oracle failed: ")+Error;return false;}
 if(!RenderRigOracle(Error)){Error=TEXT("Runtime render rig closure oracle failed: ")+Error;return false;}
 if(!SchedulerOracle(Error)){Error=TEXT("Runtime scheduler concurrency/cancellation/generation oracle failed: ")+Error;return false;}

 auto Data=[](const TCHAR* Clip){auto D=MakeUnique<FKKRuntimePoseResult>();D->CharacterIdentity=TEXT("private-rig");D->ClipIdentity=Clip;D->PreparationIdentity=TEXT("recipe-v1");D->Duration=1;D->Keys={FVector4f(0,0,0,0),FVector4f(0,0,0,1),FVector4f(1,1,1,0)};D->Tracks={FUintVector4(0,1,MAX_uint32,1)};return D;};
 auto Example=Data(TEXT("A"));const uint64 Unit=Example->Keys.GetAllocatedSize()+Example->Tracks.GetAllocatedSize();Example.Reset();FKKRuntimePoseCache C(Unit*3);
 const FKKRuntimePoseCacheKey A{TEXT("private-rig"),TEXT("A"),TEXT("recipe-v1")},B{TEXT("private-rig"),TEXT("B"),TEXT("recipe-v1")},Z{TEXT("private-rig"),TEXT("C"),TEXT("recipe-v1")};
 FKKRuntimePoseReservation R;
 if(!C.Reserve(A,Unit*2,R,Error)||!C.Publish(R,Data(TEXT("A")),Error))return false;auto PinA=C.Acquire(A);
 if(!C.Reserve(B,Unit*2,R,Error)||!C.Publish(R,Data(TEXT("B")),Error))return false;auto PinB=C.Acquire(B);
 const auto Before=C.GetStats();if(C.Reserve(Z,Unit*2,R,Error)||C.GetStats().ResidentBytes!=Before.ResidentBytes||C.GetStats().Evictions)return false;
 PinB.Reset();if(!C.Reserve(Z,Unit*2,R,Error)||C.Acquire(B).IsValid()||!PinA.IsValid()||C.GetStats().Evictions!=1)return false;
 FKKRuntimePoseReservation Duplicate;if(C.Reserve(Z,Unit*2,Duplicate,Error))return false;
 auto Stale=R;++Stale.Ticket;if(C.Publish(Stale,Data(TEXT("C")),Error)||C.Cancel(Stale))return false;
 auto Wrong=Data(TEXT("C"));Wrong->PreparationIdentity=TEXT("other-recipe");if(C.Publish(R,MoveTemp(Wrong),Error)||C.GetStats().PendingJobs!=1)return false;
 if(!C.Publish(R,Data(TEXT("C")),Error)||C.Cancel(R)||!C.Acquire(A)||!C.Acquire(Z)||C.GetStats().ReservedBytes)return false;
 PinA.Reset();if(!C.Reserve(B,Unit*2,R,Error)||!C.Cancel(R)||C.Cancel(R)||C.GetStats().PendingJobs)return false;
 if(C.GetStats().PeakBytes>Unit*3)return false;Error.Reset();return true;
}
