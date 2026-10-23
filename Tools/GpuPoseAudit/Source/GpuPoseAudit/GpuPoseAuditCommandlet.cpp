#include "GpuPoseAuditCommandlet.h"
#include "OKIndependentGpuPose.h"
#include "OKGpuAnimationManifest.h"
#include "ShaderCompiler.h"
#include "Modules/ModuleManager.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
bool RunShardLoadAudit(const FString& Path,const FString& Output);
bool RunRealGpuPoseAudit(const FString& Input,const FString& AssetRoot,const FString& Output,bool Resident,bool SkinPalette,bool Cache,bool ManifestMode);
bool RunRuntimeBankAudit(const FString&,const FString&);
bool MergePrivateClips(const FString&,const FString&);
bool ComposeBehaviorLibraries(const FString&,const FString&,const FString&);
bool RepairRigidBindings(const FString& Input,const FString& AssetRoot,const FString& Output);
bool InspectImportedGeometry(const FString& Input,const FString& AssetRoot,const FString& Output);
#if OK_RUNTIME_SHARED_MATERIAL_TOOL
bool PrepareRuntimeSharedMaterial(const FString&,const FString&,const FString&,bool HGShading);
bool PrepareTearMaterial(const FString& AssetRoot,const FString& MaterialPath,const FString& Output);
bool PrepareForegroundMaterials(const FString& AssetRoot,const FString& Root,const FString& Output);
bool PrepareFlatFaceMaterials(const FString& AssetRoot,const FString& Root,const FString& Output);
#endif
bool PrepareInstancedMaterials(const FString& Input,const FString& AssetRoot,const FString& Root,const FString& Output);
UGpuPoseAuditCommandlet::UGpuPoseAuditCommandlet(){IsClient=false;IsServer=false;IsEditor=true;LogToConsole=true;}
int32 UGpuPoseAuditCommandlet::Main(const FString& Params){FString Output;if(!FParse::Value(*Params,TEXT("Output="),Output)||IFileManager::Get().FileExists(*Output))return 1;FString RuntimeCache;if(FParse::Value(*Params,TEXT("RuntimeBankCache="),RuntimeCache))return RunRuntimeBankAudit(RuntimeCache,Output)?0:1;FString ShardIndex;if(FParse::Value(*Params,TEXT("ShardIndex="),ShardIndex))return RunShardLoadAudit(ShardIndex,Output)?0:1;if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();if(FParse::Param(*Params,TEXT("ManifestSynthetic")))return OKRunGpuManifestSelectionOracle(Output)?0:1;FString Input,AssetRoot;
#if OK_RUNTIME_SHARED_MATERIAL_TOOL
if(FParse::Param(*Params,TEXT("PrepareTearMaterial"))){FString Path;if(!FParse::Value(*Params,TEXT("AssetRoot="),AssetRoot)||!FParse::Value(*Params,TEXT("MaterialPath="),Path))return 1;return PrepareTearMaterial(AssetRoot,Path,Output)?0:1;}
if(FParse::Param(*Params,TEXT("PrepareFlatFaceMaterials"))){FString Root;if(!FParse::Value(*Params,TEXT("AssetRoot="),AssetRoot)||!FParse::Value(*Params,TEXT("MaterialRoot="),Root))return 1;return PrepareFlatFaceMaterials(AssetRoot,Root,Output)?0:1;}
if(FParse::Param(*Params,TEXT("PrepareForegroundMaterials"))){FString Root;if(!FParse::Value(*Params,TEXT("AssetRoot="),AssetRoot)||!FParse::Value(*Params,TEXT("MaterialRoot="),Root))return 1;return PrepareForegroundMaterials(AssetRoot,Root,Output)?0:1;}
if(FParse::Param(*Params,TEXT("PrepareRuntimeSharedMaterial"))){FString Root;if(!FParse::Value(*Params,TEXT("AssetRoot="),AssetRoot)||!FParse::Value(*Params,TEXT("MaterialRoot="),Root))return 1;return PrepareRuntimeSharedMaterial(AssetRoot,Root,Output,FParse::Param(*Params,TEXT("HGShading")))?0:1;}
#endif
if(FParse::Value(*Params,TEXT("Input="),Input)){if(FParse::Param(*Params,TEXT("MergePrivateClips")))return MergePrivateClips(Input,Output)?0:1;if(!FParse::Value(*Params,TEXT("AssetRoot="),AssetRoot))return 1;if(FParse::Param(*Params,TEXT("ComposeBehaviorLibraries")))return ComposeBehaviorLibraries(Input,AssetRoot,Output)?0:1;if(FParse::Param(*Params,TEXT("RepairRigidBindings")))return RepairRigidBindings(Input,AssetRoot,Output)?0:1;if(FParse::Param(*Params,TEXT("InspectImportedGeometry")))return InspectImportedGeometry(Input,AssetRoot,Output)?0:1;if(FParse::Param(*Params,TEXT("PrepareInstancedMaterials"))){FString Root;if(!FParse::Value(*Params,TEXT("MaterialRoot="),Root))return 1;return PrepareInstancedMaterials(Input,AssetRoot,Root,Output)?0:1;}return RunRealGpuPoseAudit(Input,AssetRoot,Output,FParse::Param(*Params,TEXT("Resident")),FParse::Param(*Params,TEXT("SkinPalette")),FParse::Param(*Params,TEXT("Cache")),FParse::Param(*Params,TEXT("Manifest")))?0:1;}return OKRunIndependentGpuPoseOracle(Output,FParse::Param(*Params,TEXT("Resident")))?0:1;}
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl,GpuPoseAudit,"GpuPoseAudit");
