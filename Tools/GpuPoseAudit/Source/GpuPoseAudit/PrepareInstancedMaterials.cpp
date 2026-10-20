#include "CoreMinimal.h"
#include "KKImportedCharacter.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialShared.h"
#include "ShaderCompiler.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Json.h"

namespace {
FString PackageHash(UObject* Object) {
 FString File;TArray<uint8> Bytes;
 if(!FPackageName::DoesPackageExist(Object->GetOutermost()->GetName(),&File)||!FFileHelper::LoadFileToArray(Bytes,*File))return {};
 return FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num()).ToString();
}
// Compare the serialized parameter overrides, including static switches and base
// property overrides. Parent is intentionally rebound to an equivalent prepared copy.
bool SameOverrides(UMaterialInterface* Source,UMaterialInterface* Copy) {
 for(const TCHAR* Name:{TEXT("ScalarParameterValues"),TEXT("VectorParameterValues"),TEXT("DoubleVectorParameterValues"),TEXT("TextureParameterValues"),TEXT("RuntimeVirtualTextureParameterValues"),TEXT("SparseVolumeTextureParameterValues"),TEXT("FontParameterValues"),TEXT("StaticParameters"),TEXT("StaticParametersRuntime")}) {
  if(auto* Property=FindFProperty<FProperty>(Source->GetClass(),Name))
   if(!Property->Identical_InContainer(Source,Copy)){UE_LOG(LogTemp,Error,TEXT("Changed material property %s on %s"),Name,*Source->GetPathName());return false;}
 }
 if(auto* Instance=Cast<UMaterialInstanceConstant>(Source)) {
  auto Before=Instance->BasePropertyOverrides,After=CastChecked<UMaterialInstanceConstant>(Copy)->BasePropertyOverrides;
  // UpdateOverridableBaseProperties caches inherited usage into this serialized
  // struct. The one allowed change is enabling the instanced vertex factory.
  constexpr uint32 AllowedUsage=1u<<MATUSAGE_InstancedSkinnedMesh;
  After.UsageFlags=(After.UsageFlags&~AllowedUsage)|(Before.UsageFlags&AllowedUsage);
  if(Before!=After){UE_LOG(LogTemp,Error,TEXT("Changed visual base overrides on %s"),*Source->GetPathName());return false;}
 }
 const bool Same=Source->GetBlendMode()==Copy->GetBlendMode()&&Source->IsTwoSided()==Copy->IsTwoSided()&&Source->GetShadingModels()==Copy->GetShadingModels();if(!Same)UE_LOG(LogTemp,Error,TEXT("Changed blend/two-sided/shading render state on %s"),*Source->GetPathName());return Same;
}
struct FPreparation {
 FString Root,Error;
 TMap<UMaterialInterface*,UMaterialInterface*> Copies;
 TMap<UMaterialInterface*,FString> SourceHashes;
 TArray<TStrongObjectPtr<UMaterialInterface>> KeepAlive;
 TArray<TSharedPtr<FJsonValue>> Materials;
 UMaterialInterface* Prepare(UMaterialInterface* Source) {
  if(!Source){Error=TEXT("Null material requires explicit fallback policy");return nullptr;}
  if(auto** Existing=Copies.Find(Source))return *Existing;
  if(!Source->IsA<UMaterial>()&&!Source->IsA<UMaterialInstanceConstant>()){Error=TEXT("Only saved material and constant instance assets are supported");return nullptr;}
  auto Hash=PackageHash(Source);if(Hash.IsEmpty()){Error=TEXT("Source package missing or unreadable");return nullptr;}
  SourceHashes.Add(Source,Hash);
  // Stable source identity within this fresh generation; no mesh/section names
  // or character-specific rules. Shared parents are duplicated only once.
  auto Path=Source->GetPathName();FTCHARToUTF8 Utf8(*Path);
  auto Name=TEXT("M_")+FSHA1::HashBuffer(Utf8.Get(),Utf8.Length()).ToString();auto Package=Root/TEXT("Materials")/Name;
  if(FPackageName::DoesPackageExist(Package)){Error=TEXT("Prepared generation already exists; refusing overwrite");return nullptr;}
  auto* Copy=DuplicateObject<UMaterialInterface>(Source,CreatePackage(*Package),*Name);
  Copy->ClearFlags(RF_Transient);Copy->SetFlags(RF_Public|RF_Standalone);Copies.Add(Source,Copy);KeepAlive.Emplace(Copy);
  if(auto* Instance=Cast<UMaterialInstanceConstant>(Copy)) {
   auto* Parent=Prepare(Instance->Parent);if(!Parent)return nullptr;Instance->SetParentEditorOnly(Parent);
  }else CastChecked<UMaterial>(Copy)->SetMaterialUsage(MATUSAGE_InstancedSkinnedMesh);
  Copy->PostEditChange();
  if(!SameOverrides(Source,Copy)){Error=TEXT("Material overrides or render state changed during preparation");return nullptr;}
  auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("source"),Path);Row->SetStringField(TEXT("prepared"),Copy->GetPathName());Row->SetStringField(TEXT("source_package_sha1"),Hash);Materials.Add(MakeShared<FJsonValueObject>(Row));return Copy;
 }
};
}

bool PrepareInstancedMaterials(const FString& Input,const FString& AssetRoot,const FString& Root,const FString& Output) {
 FString Text;if(!Root.StartsWith(TEXT("/Game/Scenes/CrowdRoom/GpuMaterials/"))||!FPackageName::IsValidLongPackageName(Root)||!FFileHelper::LoadFileToString(Text,*Input))return false;
 FPackageName::RegisterMountPoint(TEXT("/Game/"),FPaths::ConvertRelativePathToFull(AssetRoot)/TEXT(""));
 // A fresh generation is atomic from the consumer's perspective: the manifest
 // appears only after all shader/asset checks and saves succeed.
 if(IFileManager::Get().DirectoryExists(*FPackageName::LongPackageNameToFilename(Root)))return false;
 TSharedPtr<FJsonObject> Request;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request)||!Request)return false;
 const TArray<TSharedPtr<FJsonValue>>* Libraries=nullptr;if(!Request->TryGetArrayField(TEXT("libraries"),Libraries)||Libraries->IsEmpty())return false;
 const TArray<TSharedPtr<FJsonValue>>* Expected=nullptr;Request->TryGetArrayField(TEXT("expected_characters"),Expected);
 if(Expected&&Expected->Num()!=Libraries->Num())return false;
 TArray<TStrongObjectPtr<UKKImportedCharacter>> Characters;TSet<FString> SeenCharacters;
 auto AddCharacter=[&](UKKImportedCharacter* Character){if(!Character)return false;if(!SeenCharacters.Contains(Character->GetPathName())){SeenCharacters.Add(Character->GetPathName());Characters.Emplace(Character);}return true;};
 for(int32 Index=0;Index<Libraries->Num();++Index) {
  FString Path;if(!(*Libraries)[Index]->TryGetString(Path))return false;
  TStrongObjectPtr<UKKImportedAnimationLibrary> Library(LoadObject<UKKImportedAnimationLibrary>(nullptr,*Path));
  if(!Library||!Library->Character)return false;
  if(Expected){FString Identity;if(!(*Expected)[Index]->TryGetString(Identity)||Identity!=Library->Character->GetPathName()){UE_LOG(LogTemp,Error,TEXT("Library character identity mismatch: %s"),*Path);return false;}}
  if(!AddCharacter(Library->Character))return false;
 }
 const TArray<TSharedPtr<FJsonValue>>* Extra=nullptr;
 if(Request->TryGetArrayField(TEXT("extra_characters"),Extra))for(const auto& Value:*Extra){FString Path;if(!Value->TryGetString(Path)||!AddCharacter(LoadObject<UKKImportedCharacter>(nullptr,*Path)))return false;}
 FPreparation Prep;Prep.Root=Root;TArray<TSharedPtr<FJsonValue>> MeshRows;int32 Slots=0;TSet<FString> SeenMeshes;
 for(const auto& Character:Characters) {
  for(const auto& Outfit:Character->Outfits)for(const auto& Part:Outfit.Parts)if(auto* Mesh=Part.SkinnedModel.Get()) {
   if(SeenMeshes.Contains(Mesh->GetPathName()))continue;SeenMeshes.Add(Mesh->GetPathName());
   auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("mesh"),Mesh->GetPathName());Row->SetStringField(TEXT("source_package_sha1"),PackageHash(Mesh));TArray<TSharedPtr<FJsonValue>> Bindings;
   for(int32 Slot=0;Slot<Mesh->GetMaterials().Num();++Slot) {
    auto* Source=Mesh->GetMaterials()[Slot].MaterialInterface.Get();auto* Prepared=Prep.Prepare(Source);
    if(!Prepared){UE_LOG(LogTemp,Error,TEXT("Material preparation rejected %s slot %d: %s"),*Mesh->GetPathName(),Slot,*Prep.Error);return false;}
    auto Binding=MakeShared<FJsonObject>();Binding->SetNumberField(TEXT("slot"),Slot);Binding->SetStringField(TEXT("source"),Source->GetPathName());Binding->SetStringField(TEXT("prepared"),Prepared->GetPathName());Bindings.Add(MakeShared<FJsonValueObject>(Binding));++Slots;
   }
   Row->SetArrayField(TEXT("bindings"),Bindings);MeshRows.Add(MakeShared<FJsonValueObject>(Row));
  }
 }
 if(Prep.Copies.IsEmpty())return false;
 if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
 int32 Compiled=0;
 for(const auto& Pair:Prep.Copies) {
  auto* Source=Pair.Key;auto* Copy=Pair.Value;
  if(PackageHash(Source)!=Prep.SourceHashes[Source]||!SameOverrides(Source,Copy)||!Copy->GetUsageByFlag(MATUSAGE_InstancedSkinnedMesh))return false;
  auto* Resource=Copy->GetMaterialResource(GMaxRHIShaderPlatform);
  if(!Resource||!Resource->IsCompilationFinished()||!Resource->GetCompileErrors().IsEmpty()){UE_LOG(LogTemp,Error,TEXT("Prepared shader missing or failed: %s"),*Copy->GetPathName());return false;}++Compiled;
 }
 for(const auto& Copy:Prep.KeepAlive) {
  auto* Asset=Copy.Get();auto File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
  if(IFileManager::Get().FileExists(*File))return false;IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
  FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,Args))return false;
 }
 auto Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("schema"),1);Report->SetBoolField(TEXT("passed"),true);Report->SetBoolField(TEXT("source_assets_modified"),false);Report->SetBoolField(TEXT("overrides_preserved"),true);Report->SetBoolField(TEXT("sections_merged"),false);Report->SetBoolField(TEXT("fps_evidence"),false);Report->SetStringField(TEXT("root"),Root);Report->SetNumberField(TEXT("slots"),Slots);Report->SetNumberField(TEXT("compiled_materials"),Compiled);Report->SetArrayField(TEXT("materials"),Prep.Materials);Report->SetArrayField(TEXT("meshes"),MeshRows);
 TArray<TSharedPtr<FJsonValue>> CharacterRows;for(const auto& Character:Characters)CharacterRows.Add(MakeShared<FJsonValueString>(Character->GetPathName()));Report->SetArrayField(TEXT("characters"),CharacterRows);Report->SetNumberField(TEXT("mesh_count"),MeshRows.Num());Report->SetBoolField(TEXT("library_identities_checked"),Expected!=nullptr);
 FString Json;return FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json))&&FFileHelper::SaveStringToFile(Json,*Output);
}
