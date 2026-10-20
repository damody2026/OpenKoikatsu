#include "CoreMinimal.h"
#include "KKImportedCharacter.h"
#include "Animation/AnimSequence.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Json.h"
namespace {
bool SaveBehaviorAsset(UObject* Asset){const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());if(IFileManager::Get().FileExists(*File))return false;IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs A;A.TopLevelFlags=RF_Public|RF_Standalone;return UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,A);}
}
bool ComposeBehaviorLibraries(const FString& Input,const FString& AssetRoot,const FString& Output){
 FString Text;if(!FFileHelper::LoadFileToString(Text,*Input))return false;FPackageName::RegisterMountPoint(TEXT("/Game/"),AssetRoot/TEXT(""));TSharedPtr<FJsonObject> Request;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request)||!Request)return false;
 const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(!Request->TryGetArrayField(TEXT("libraries"),Rows)||Rows->IsEmpty())return false;
 TArray<TSharedPtr<FJsonValue>> Receipts;
 for(const auto& Value:*Rows){CollectGarbage(RF_NoFlags);const auto Row=Value->AsObject();if(!Row)return false;
  const FString Destination=Row->GetStringField(TEXT("destination"));
  if(!Destination.StartsWith(TEXT("/Game/Scenes/CrowdRoom/Characters/"))||!FPackageName::IsValidLongPackageName(Destination)||FPackageName::DoesPackageExist(Destination/TEXT("Animations")))return false;
  TStrongObjectPtr<UKKImportedAnimationLibrary> Walk(LoadObject<UKKImportedAnimationLibrary>(nullptr,*Row->GetStringField(TEXT("walk"))));
  TStrongObjectPtr<UKKImportedAnimationLibrary> Actions(LoadObject<UKKImportedAnimationLibrary>(nullptr,*Row->GetStringField(TEXT("actions"))));
  if(!Walk||!Actions||!Walk->Character||!Actions->Character||Walk->Animations.IsEmpty()||Actions->Animations.IsEmpty()||Walk->Character->SourceHash!=Actions->Character->SourceHash)return false;
  auto* Character=Walk->Character.Get();if(Character->Schema!=1017||Character->Outfits.Num()!=1)return false;
  auto* Library=NewObject<UKKImportedAnimationLibrary>(CreatePackage(*(Destination/TEXT("Animations"))),TEXT("Animations"),RF_Public|RF_Standalone);TStrongObjectPtr<UKKImportedAnimationLibrary> Hold(Library);
  Library->Character=Character;Library->Animations=Walk->Animations;
  TArray<TStrongObjectPtr<UAnimSequence>> Saved;
  for(int32 I=0;I<Actions->Animations.Num();++I){const auto& Entry=Actions->Animations[I];
   if(!Entry.Sequence||Entry.Outfit!=0||Entry.Sequence->GetSkeleton()!=Character->Outfits[0].Skeleton){UE_LOG(LogTemp,Error,TEXT("Behavior source private skeleton mismatch"));return false;}
   if(Library->Animations.ContainsByPredicate([&](const auto& E){return E.Source==Entry.Source&&E.Clip==Entry.Clip;}))return false;
   const FString Path=Destination/FString::Printf(TEXT("Action_%04d"),I);if(FPackageName::DoesPackageExist(Path))return false;
   auto* Sequence=Cast<UAnimSequence>(StaticDuplicateObject(Entry.Sequence,CreatePackage(*Path),*FPackageName::GetLongPackageAssetName(Path)));if(!Sequence)return false;
   Saved.Emplace(Sequence);Sequence->SetPreviewMesh(nullptr);Sequence->WaitOnExistingCompression();
   auto Copy=Entry;Copy.Sequence=Sequence;Library->Animations.Add(Copy);
  }
  for(const auto& Sequence:Saved)if(!SaveBehaviorAsset(Sequence.Get()))return false;if(!SaveBehaviorAsset(Library))return false;
  auto Receipt=MakeShared<FJsonObject>();Receipt->SetStringField(TEXT("library"),Library->GetPathName());Receipt->SetStringField(TEXT("character"),Character->GetPathName());Receipt->SetNumberField(TEXT("clips"),Library->Animations.Num());Receipt->SetBoolField(TEXT("preview_mesh_references_removed"),true);Receipts.Add(MakeShared<FJsonValueObject>(Receipt));
 }
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),true);Report->SetBoolField(TEXT("source_assets_modified"),false);Report->SetBoolField(TEXT("geometry_materials_modified"),false);Report->SetArrayField(TEXT("libraries"),Receipts);FString Json;
 return FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json))&&FFileHelper::SaveStringToFile(Json,*Output);
}
