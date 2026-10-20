#include "OKCrowdRuntimePoseProbe.h"
#include "OKResidentGpuPose.h"
#include "OKGpuResidencyBudget.h"
#include "OKGpuAnimationManifest.h"
#include "KKRuntimePoseBankTypes.h"
#include "KKRuntimePoseNative.h"
#include "KKRuntimePoseCache.h"
#include "KKRuntimePoseScheduler.h"
#include "KKRuntimeRenderRig.h"
#include "KKRuntimeRenderMesh.h"
#include "KKRuntimeSharedMaterial.h"
#include "Serialization/MemoryReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"
#include "Json.h"
#include "Materials/MaterialInterface.h"
#include "ShaderCodeLibrary.h"

bool OKAuditRuntimePoseCache(FString&);
bool OKAuditRuntimeMaterials(const FKKRuntimeMaterialData&,FKKRuntimeMaterialStats&,FString&,TConstArrayView<UMaterialInterface*>);
bool OKAuditRuntimePoseOnGpu(const FKKRuntimePoseRequest&,const FKKRuntimePoseResult&,double&,FString&,const FKKRuntimePoseResult*,FOKResidentPoseStats&,FOKGpuResidencyStats&,FOKGpuClipWindowStats&,TConstArrayView<FKKRuntimeMeshPart>,double&,FKKRuntimeRenderMeshStats&);

static bool CompareCompactedPose(const FKKRuntimePoseResult& Full,const FKKRuntimePoseResult& Compact,TConstArrayView<int32> Map,FString& Error){
 if(!Full.Passed()||!Compact.Passed()||Full.Tracks.Num()!=Map.Num()||Full.Samples!=Compact.Samples||Full.Duration!=Compact.Duration||Full.CharacterIdentity!=Compact.CharacterIdentity||Full.ClipIdentity!=Compact.ClipIdentity){Error=TEXT("Compacted pose reference identity differs");return false;}
 int32 Compared=0;
 for(int32 Old=0;Old<Map.Num();++Old)if(Map[Old]!=INDEX_NONE){const int32 New=Map[Old];if(New<0||New>=Compact.Tracks.Num()){Error=TEXT("Compacted pose remap out of range");return false;}
  const auto& A=Full.Tracks[Old];const auto& B=Compact.Tracks[New];const uint32 Parent=A.Z==MAX_uint32?MAX_uint32:uint32(Map[A.Z]);
  if(A.Y!=B.Y||B.Z!=Parent||A.W!=B.W||FMemory::Memcmp(Full.Keys.GetData()+A.X*3,Compact.Keys.GetData()+B.X*3,uint64(A.Y)*3*sizeof(FVector4f))){Error=TEXT("Compacted retained pose is not bitwise equal to full private rig");return false;}++Compared;
 }
 if(Compared!=Compact.Tracks.Num()){Error=TEXT("Compacted pose remap is incomplete");return false;}Error.Reset();return true;
}

bool OKStartCrowdRuntimePoseProbe(const FString& Recipe,const FString& Apng,const FString& Output,const FString& Curves){
 check(IsInGameThread());FString CacheOracleError;if(!OKAuditRuntimePoseCache(CacheOracleError)){UE_LOG(LogTemp,Error,TEXT("Pose cache oracle: %s"),*CacheOracleError);return false;}TArray<uint8> Bytes;FKKRuntimePoseRequest R;
 if(Curves.IsEmpty()){
 if(IFileManager::Get().FileSize(*Recipe)>8*1024*1024||!FFileHelper::LoadFileToArray(Bytes,*Recipe))return false;
 FMemoryReader A(Bytes,true);uint32 Magic=0;int32 Count=0;A<<Magic<<Count;if(Magic!=0x52504b4b||Count<=0||Count>65536)return false;
 R.Bones.SetNum(Count);for(auto& B:R.Bones){FString Name;A<<Name<<B.Parent<<B.Reference;B.Name=FName(*Name);}
 A<<Count;if(Count<0||Count>4096||A.IsError())return false;R.Parts.SetNum(Count);for(auto& P:R.Parts)A<<P.NativePart<<P.Slots<<P.RigidFollow<<P.RigidBindWorld;
 A<<R.AuthoredCurves<<R.Duration<<R.ClipIdentity;if(A.IsError()||A.Tell()!=A.TotalSize())return false;
 }else{
  if(IFileManager::Get().FileSize(*Curves)>128*1024*1024||!FFileHelper::LoadFileToArray(R.AuthoredCurves,*Curves))return false;
  R.Duration=OKRuntimeSharedClipDuration(R.AuthoredCurves);if(!FMath::IsFinite(R.Duration)||R.Duration<=0)return false;
  R.ClipIdentity=FPaths::GetCleanFilename(Curves);
 }
 FString SecondPath;TArray<uint8> SecondBytes;float SecondDuration=0;
 if(FParse::Value(FCommandLine::Get(),TEXT("CrowdRuntimePoseSecondCurves="),SecondPath)){
  if(IFileManager::Get().FileSize(*SecondPath)>128*1024*1024||!FFileHelper::LoadFileToArray(SecondBytes,*SecondPath))return false;
  SecondDuration=OKRuntimeSharedClipDuration(SecondBytes);if(!FMath::IsFinite(SecondDuration)||SecondDuration<=0)return false;
 }
 auto Second=MakeShared<TArray<uint8>>(MoveTemp(SecondBytes));auto First=MakeShared<TSharedPtr<const FKKRuntimePoseResult,ESPMode::ThreadSafe>>();
 auto Scheduler=MakeShared<FKKRuntimePoseScheduler>(128ull*1024*1024,2,8);auto Ticket=MakeShared<uint64>(0);auto SecondTicket=MakeShared<uint64>(0);auto FullTicket=MakeShared<uint64>(0);auto Full=MakeShared<TSharedPtr<const FKKRuntimePoseResult,ESPMode::ThreadSafe>>();auto Remap=MakeShared<TArray<int32>>();
 const bool Fixture=Curves.IsEmpty();
 const bool CheckGpu=FParse::Param(FCommandLine::Get(),TEXT("CrowdRuntimePoseGpuCheck"));
 auto GpuRequest=MakeShared<FKKRuntimePoseRequest>();auto Meshes=MakeShared<TArray<FKKRuntimeMeshPart>>();auto Materials=MakeShared<FKKRuntimeMaterialData>();
 auto Snapshot=MakeShared<TFuture<FKKRuntimeSnapshotResult>>(OKOpenRuntimeCharacterSnapshotAsync(Apng,0,R.AuthoredCurves));
 auto Request=MakeShared<FKKRuntimePoseRequest>(MoveTemp(R));
 
 auto OpenMs=MakeShared<double>(0.);auto SnapshotWorker=MakeShared<bool>(false);
 FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Snapshot,Request,Output,OpenMs,SnapshotWorker,Fixture,CheckGpu,GpuRequest,Second,SecondDuration,SecondPath,First,Scheduler,Ticket,SecondTicket,FullTicket,Full,Remap,Meshes,Materials](float){
  Scheduler->Tick();
  if(!*Ticket){if(!Snapshot->IsReady())return true;auto Prepared=Snapshot->Consume();*OpenMs=Prepared.PrepareMs;*SnapshotWorker=Prepared.WorkerThread;
   if(!Prepared.Error.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("Runtime APNG worker failed: %s"),*Prepared.Error);
    auto Report=MakeShared<FJsonObject>();auto Histogram=MakeShared<FJsonObject>();TMap<int32,int32> ShaderCounts;for(const auto& P:Materials->Parts)++ShaderCounts.FindOrAdd(P.ShaderKind);for(const auto& Count:ShaderCounts)Histogram->SetNumberField(FString::FromInt(Count.Key),Count.Value);Report->SetObjectField(TEXT("runtime_material_shader_counts"),Histogram);Report->SetBoolField(TEXT("passed"),false);Report->SetBoolField(TEXT("metadata_fixture_used"),Fixture);Report->SetBoolField(TEXT("snapshot_worker"),Prepared.WorkerThread);Report->SetBoolField(TEXT("native_development"),!WITH_EDITOR);Report->SetStringField(TEXT("error"),Prepared.Error);FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*Output);
    FPlatformMisc::RequestExitWithStatus(false,1);return false;}
   if(!Fixture){Request->Bones=MoveTemp(Prepared.Bones);Request->Parts=MoveTemp(Prepared.Parts);}
   Request->Snapshot=MoveTemp(Prepared.Snapshot);Request->CharacterIdentity=Prepared.RigIdentity;
   FString AdmissionError;auto FullRequest=*Request;TArray<FKKRuntimePoseBone> CompactBones;TArray<FName> Extra;
   if(!OKCompactRuntimeRenderRig(Request->Bones,Request->Parts,Extra,CompactBones,*Remap,AdmissionError)){UE_LOG(LogTemp,Error,TEXT("Runtime render rig: %s"),*AdmissionError);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
   *Meshes=MoveTemp(Prepared.Meshes);*Materials=MoveTemp(Prepared.Materials);
   if(!OKRemapRuntimeMeshBones(*Meshes,*Remap,CompactBones.Num(),AdmissionError)){UE_LOG(LogTemp,Error,TEXT("Runtime mesh remap: %s"),*AdmissionError);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
   Request->Bones=MoveTemp(CompactBones);if(CheckGpu)GpuRequest->Bones=Request->Bones;

   if(!Scheduler->Submit(*Request,*Ticket,AdmissionError)){UE_LOG(LogTemp,Error,TEXT("Runtime pose reservation: %s"),*AdmissionError);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
   if(!Second->IsEmpty()){
    auto Next=*Request;Next.AuthoredCurves=*Second;Next.Duration=SecondDuration;Next.ClipIdentity=FPaths::GetCleanFilename(SecondPath);
    if(!Scheduler->Submit(MoveTemp(Next),*SecondTicket,AdmissionError)){UE_LOG(LogTemp,Error,TEXT("Runtime second pose queue: %s"),*AdmissionError);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
   }
   if(!Scheduler->Submit(MoveTemp(FullRequest),*FullTicket,AdmissionError)){UE_LOG(LogTemp,Error,TEXT("Full reference pose queue: %s"),*AdmissionError);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
   Scheduler->Tick();return true;
  }
  if(!Full->IsValid()){FKKRuntimePoseCompletion Reference;if(!Scheduler->Take(*FullTicket,Reference))return true;
   if(!Reference.Data){UE_LOG(LogTemp,Error,TEXT("Full reference preparation: %s"),*Reference.Error);FPlatformMisc::RequestExitWithStatus(false,1);return false;}*Full=Reference.Data;
  }
  FKKRuntimePoseCompletion Completion;if(!Scheduler->Take(*Ticket,Completion))return true;
  auto Current=Completion.Data;FKKRuntimePoseResult Failed;Failed.Error=Completion.Error;
  const FKKRuntimePoseResult& Result=Current?*Current:Failed;
  const bool FirstClip=!First->IsValid();FString RigError;const bool RigPassed=!FirstClip||CompareCompactedPose(**Full,Result,*Remap,RigError);
  if(Result.Passed()&&RigPassed&&!Second->IsEmpty()){
   *First=Current;Second->Reset();*Ticket=*SecondTicket;return true;
  }
  auto Report=MakeShared<FJsonObject>();auto Histogram=MakeShared<FJsonObject>();TMap<int32,int32> ShaderCounts;for(const auto& P:Materials->Parts)++ShaderCounts.FindOrAdd(P.ShaderKind);for(const auto& Count:ShaderCounts)Histogram->SetNumberField(FString::FromInt(Count.Key),Count.Value);Report->SetObjectField(TEXT("runtime_material_shader_counts"),Histogram);TArray<TSharedPtr<FJsonValue>> SourceFamilies;for(const auto& P:Materials->Parts){auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("part"),P.NativePart);Row->SetNumberField(TEXT("kind"),P.ShaderKind);Row->SetStringField(TEXT("role"),P.Role);Row->SetStringField(TEXT("blend"),P.Blend);Row->SetNumberField(TEXT("unlit"),P.Unlit);Row->SetNumberField(TEXT("eye"),P.EyeDraw);TSharedPtr<FJsonObject> Raw;const TSharedPtr<FJsonObject>* Mat=nullptr;FString Shader;if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Raw)&&Raw&&Raw->TryGetObjectField(TEXT("material"),Mat))(*Mat)->TryGetStringField(TEXT("shader"),Shader);Row->SetStringField(TEXT("shader"),Shader);SourceFamilies.Add(MakeShared<FJsonValueObject>(Row));}Report->SetArrayField(TEXT("runtime_material_sources"),SourceFamilies);
  FKKRuntimeMaterialStats MaterialStats;FString MaterialError;FString ParentPath,Library;TArray<UMaterialInterface*> Parents;bool ParentReady=true;
  if(FParse::Value(FCommandLine::Get(),TEXT("CrowdRuntimeMaterialParent="),ParentPath)){
   ParentReady=FParse::Value(FCommandLine::Get(),TEXT("CrowdRuntimeMaterialLibrary="),Library)&&FShaderCodeLibrary::OpenLibrary(*Library,FPaths::ProjectContentDir());
   if(ParentReady){const FString Root=FPaths::GetPath(ParentPath);for(auto Recipe:OKRuntimeSharedMaterialRecipes()){const FString Name=OKRuntimeSharedMaterialParentName(Recipe);auto* M=LoadObject<UMaterialInterface>(nullptr,*(Root/FString(Name)+TEXT(".")+Name));Parents.Add(M);ParentReady&=M!=nullptr;}}
   if(!ParentReady)MaterialError=TEXT("Cooked common material parent catalog/library unavailable");
  }
  const bool MaterialsPassed=ParentReady&&(!CheckGpu||OKAuditRuntimeMaterials(*Materials,MaterialStats,MaterialError,Parents));
  double MatrixError=0.,MeshError=0.;FString GpuError;FOKResidentPoseStats GpuStats;FOKGpuResidencyStats ResidencyStats;FOKGpuClipWindowStats WindowStats;FKKRuntimeRenderMeshStats MeshStats;const bool GpuPassed=!CheckGpu||OKAuditRuntimePoseOnGpu(*GpuRequest,Result,MatrixError,GpuError,First->IsValid()?First->Get():nullptr,GpuStats,ResidencyStats,WindowStats,*Meshes,MeshError,MeshStats);
  Report->SetBoolField(TEXT("passed"),Result.Passed()&&GpuPassed&&RigPassed&&MaterialsPassed);Report->SetNumberField(TEXT("private_clips"),First->IsValid()?2:1);Report->SetNumberField(TEXT("first_clip_prepare_ms"),First->IsValid()?(*First)->PrepareMs:0.);Report->SetNumberField(TEXT("total_key_bytes"),(uint64(First->IsValid()?(*First)->Keys.Num():0)+Result.Keys.Num())*16);Report->SetNumberField(TEXT("first_clip_duration"),First->IsValid()?(*First)->Duration:0.);Report->SetNumberField(TEXT("last_clip_duration"),Result.Duration);Report->SetBoolField(TEXT("runtime_clock_verified"),CheckGpu&&GpuPassed);Report->SetBoolField(TEXT("gpu_readback_verified"),CheckGpu&&GpuPassed);Report->SetBoolField(TEXT("visual_equivalence_verified"),false);Report->SetNumberField(TEXT("gpu_max_matrix_error"),MatrixError);Report->SetStringField(TEXT("gpu_error"),GpuError);Report->SetBoolField(TEXT("native_development"),!WITH_EDITOR);Report->SetBoolField(TEXT("worker"),Result.WorkerThread);Report->SetBoolField(TEXT("snapshot_worker"),*SnapshotWorker);
  Report->SetBoolField(TEXT("external_kks_root_used"),false);Report->SetBoolField(TEXT("cooked_character_loaded"),false);Report->SetBoolField(TEXT("metadata_fixture_used"),Fixture);
  Report->SetBoolField(TEXT("runtime_clip_window_verified"),CheckGpu&&GpuPassed);Report->SetBoolField(TEXT("clip_window_100_actor_oracle_verified"),CheckGpu&&GpuPassed);Report->SetNumberField(TEXT("clip_window_source_key_bytes"),WindowStats.SourceKeyBytes);Report->SetNumberField(TEXT("clip_window_selected_key_bytes"),WindowStats.SelectedKeyBytes);Report->SetNumberField(TEXT("clip_window_selection_ms"),WindowStats.SelectionMs);Report->SetBoolField(TEXT("gpu_aggregate_retirement_verified"),CheckGpu&&GpuPassed);Report->SetNumberField(TEXT("gpu_aggregate_peak_bytes"),ResidencyStats.PeakBytes);Report->SetNumberField(TEXT("gpu_aggregate_final_charged_bytes"),ResidencyStats.ChargedBytes);Report->SetNumberField(TEXT("gpu_aggregate_final_retiring_banks"),ResidencyStats.RetiringBanks);Report->SetBoolField(TEXT("gpu_buffer_admission_verified"),CheckGpu&&GpuPassed&&GpuStats.BudgetRejections==1);Report->SetNumberField(TEXT("gpu_resident_buffer_bytes"),GpuStats.ResidentBufferBytes);Report->SetNumberField(TEXT("gpu_reserved_peak_buffer_bytes"),GpuStats.ReservedPeakBufferBytes);Report->SetNumberField(TEXT("gpu_budget_rejections"),GpuStats.BudgetRejections);
  Report->SetBoolField(TEXT("pose_cache_verified"),true);const auto SS=Scheduler->GetStats();Report->SetBoolField(TEXT("runtime_scheduler_verified"),SS.Started==(First->IsValid()?3:2)&&SS.Running==0&&SS.Queued==0);Report->SetNumberField(TEXT("runtime_scheduler_started"),SS.Started);Report->SetNumberField(TEXT("runtime_scheduler_peak_running"),SS.PeakRunning);const auto CS=Scheduler->GetCacheStats();Report->SetNumberField(TEXT("pose_cache_resident_bytes"),CS.ResidentBytes);Report->SetNumberField(TEXT("pose_cache_peak_reserved_bytes"),CS.PeakBytes);Report->SetNumberField(TEXT("pose_cache_pending_jobs"),CS.PendingJobs);
  Report->SetBoolField(TEXT("render_rig_compaction_verified"),RigPassed&&Full->IsValid());Report->SetStringField(TEXT("render_rig_error"),RigError);Report->SetNumberField(TEXT("full_rig_bones"),(*Full)->Tracks.Num());Report->SetNumberField(TEXT("compact_rig_bones"),Result.Tracks.Num());Report->SetNumberField(TEXT("full_idle_key_bytes"),uint64((*Full)->Keys.Num())*16);Report->SetNumberField(TEXT("compact_idle_key_bytes"),uint64(First->IsValid()?(*First)->Keys.Num():Result.Keys.Num())*16);
  uint64 MeshVertices=0,MeshTriangles=0,MeshBytes=0;int32 RigidParts=0;for(const auto& P:*Meshes){MeshVertices+=P.Vertices.Num();MeshTriangles+=P.Indices.Num()/3;MeshBytes+=P.Vertices.GetAllocatedSize()+P.Indices.GetAllocatedSize();}for(const auto& P:Request->Parts)RigidParts+=!P.RigidFollow.IsEmpty();
  Report->SetBoolField(TEXT("runtime_mesh_binding_verified"),CheckGpu&&GpuPassed);Report->SetNumberField(TEXT("runtime_mesh_max_position_error_cm"),MeshError);Report->SetNumberField(TEXT("runtime_mesh_parts"),Meshes->Num());Report->SetNumberField(TEXT("runtime_mesh_vertices"),MeshVertices);Report->SetNumberField(TEXT("runtime_mesh_triangles"),MeshTriangles);Report->SetNumberField(TEXT("runtime_mesh_buffer_bytes"),MeshBytes);Report->SetNumberField(TEXT("runtime_mesh_rigid_parts"),RigidParts);Report->SetBoolField(TEXT("runtime_mesh_reduced"),MeshStats.Reduced);
  Report->SetBoolField(TEXT("runtime_render_mesh_created"),MeshStats.Created);Report->SetNumberField(TEXT("runtime_render_mesh_sections"),MeshStats.Sections);Report->SetNumberField(TEXT("runtime_render_mesh_vertices"),MeshStats.Vertices);Report->SetNumberField(TEXT("runtime_render_mesh_triangles"),MeshStats.Triangles);Report->SetNumberField(TEXT("runtime_render_mesh_prepare_ms"),MeshStats.PrepareMs);
  Report->SetBoolField(TEXT("runtime_reduction_verified"),CheckGpu&&GpuPassed);Report->SetBoolField(TEXT("runtime_reduction_budgets_met"),MeshStats.BudgetsMet);Report->SetNumberField(TEXT("runtime_reduction_ms"),MeshStats.ReductionMs);Report->SetNumberField(TEXT("runtime_reduction_retained_parts"),MeshStats.RetainedParts);for(int32 G=0;G<4;++G)Report->SetNumberField(FString::Printf(TEXT("runtime_low_group_%d_triangles"),G),MeshStats.Groups[G]);
  Report->SetBoolField(TEXT("runtime_texture_resources_verified"),CheckGpu&&MaterialsPassed);Report->SetStringField(TEXT("runtime_material_error"),MaterialError);Report->SetNumberField(TEXT("runtime_material_parts"),MaterialStats.Parts);Report->SetNumberField(TEXT("runtime_texture_slots"),MaterialStats.Slots);Report->SetNumberField(TEXT("runtime_unique_textures"),MaterialStats.Textures);Report->SetNumberField(TEXT("runtime_texture_mips"),MaterialStats.Mips);Report->SetNumberField(TEXT("runtime_texture_pixel_bytes"),MaterialStats.PixelBytes);Report->SetNumberField(TEXT("runtime_texture_prepare_ms"),MaterialStats.PrepareMs);
  Report->SetNumberField(TEXT("runtime_shared_hair_instances"),MaterialStats.HairInstances);Report->SetNumberField(TEXT("runtime_shared_hair_front_instances"),MaterialStats.HairFrontInstances);Report->SetNumberField(TEXT("runtime_shared_studio_alpha_instances"),MaterialStats.StudioAlphaInstances);Report->SetNumberField(TEXT("runtime_shared_item_instances"),MaterialStats.ItemInstances);Report->SetNumberField(TEXT("runtime_shared_opaque_instances"),MaterialStats.OpaqueInstances);Report->SetNumberField(TEXT("runtime_shared_alpha_masked_instances"),MaterialStats.AlphaMaskedInstances);Report->SetNumberField(TEXT("runtime_shared_alpha_translucent_instances"),MaterialStats.AlphaTranslucentInstances);Report->SetBoolField(TEXT("runtime_shared_parent_verified"),MaterialsPassed&&MaterialStats.SharedParentUsed);Report->SetNumberField(TEXT("runtime_shared_material_instances"),MaterialStats.SharedInstances);Report->SetNumberField(TEXT("runtime_shared_material_unsupported_parts"),MaterialStats.UnsupportedParts);Report->SetStringField(TEXT("runtime_shared_parent"),ParentPath);
  Report->SetNumberField(TEXT("runtime_shared_eye_white_instances"),MaterialStats.EyeWhiteInstances);Report->SetNumberField(TEXT("runtime_shared_eye_instances"),MaterialStats.EyeInstances);Report->SetNumberField(TEXT("runtime_shared_koikano_eye_white_instances"),MaterialStats.KoikanoEyeWhiteInstances);Report->SetNumberField(TEXT("runtime_shared_koikano_eye_instances"),MaterialStats.KoikanoEyeInstances);Report->SetNumberField(TEXT("runtime_shared_nose_instances"),MaterialStats.NoseInstances);Report->SetNumberField(TEXT("runtime_shared_additional_instances"),MaterialStats.AdditionalInstances);
  Report->SetBoolField(TEXT("runtime_source_mesh_shaders_verified"),MaterialStats.SourceMeshShadersVerified);
  Report->SetBoolField(TEXT("runtime_source_mesh_pixels_verified"),MaterialStats.SourceMeshPixelsVerified);
  Report->SetBoolField(TEXT("runtime_source_mesh_raster_pixels_verified"),MaterialStats.SourceMeshRasterPixelsVerified);
  Report->SetBoolField(TEXT("runtime_source_raster_state_verified"),MaterialStats.SourceRasterVerified);Report->SetBoolField(TEXT("scene_integration_verified"),false);Report->SetNumberField(TEXT("apng_open_snapshot_ms"),*OpenMs);Report->SetNumberField(TEXT("pose_prepare_ms"),Result.PrepareMs);Report->SetNumberField(TEXT("key_bytes"),uint64(Result.Keys.Num())*16);Report->SetNumberField(TEXT("bones"),Result.Tracks.Num());Report->SetStringField(TEXT("error"),Result.Error);
  FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));const bool Saved=FFileHelper::SaveStringToFile(Json,*Output);
  FPlatformMisc::RequestExitWithStatus(false,Saved&&Result.Passed()&&GpuPassed&&RigPassed&&MaterialsPassed?0:1);return false;
 }));return true;
}
