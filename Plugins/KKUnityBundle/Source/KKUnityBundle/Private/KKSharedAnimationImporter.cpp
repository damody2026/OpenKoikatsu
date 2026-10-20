#include "KKAnimationImporter.h"
#include "KKSharedAnimation.h"
#include "KKImportDestination.h"
#include "KKRustExtract.h"
#include "ok_extract.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#endif
bool KKAnimationImporter::ImportShared(const FString& Root,const FString& Source,const TArray<FString>& Clips,
 const FString& Destination,TArray<UKKSharedAnimation*>& Out,FString& Error){
 Out.Reset();Error.Reset();
#if WITH_EDITOR
 if(!OKValidateImportDestination(Destination,Error))return false;
 if(Root.IsEmpty()||Source.IsEmpty()||Clips.IsEmpty()||Clips.Num()>10000||!FOkSession::InitBindings()){
  Error=TEXT("Select a resource root, source bundle and original raw clips");return false;}
 // Validate all curve resources before creating any packages.
 TArray<TArray<uint8>> Resources;TArray<float> Durations;TSet<FString> Seen;
 for(const auto& Clip:Clips){if(Clip.IsEmpty()||Clip.Contains(TEXT("|"))||Seen.Contains(Clip)){
   Error=TEXT("Invalid or duplicate clip label");return false;}Seen.Add(Clip);
  const FString Key=Source+TEXT("|")+Clip;
  const int32 Count=ok_export_shared_clip(TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*Key),nullptr,0);
  if(Count<=0||Count>128*1024*1024){Error=TEXT("Cannot export authored raw clip (controller graphs require a separate importer): ")+Key;return false;}
  auto& Bytes=Resources.AddDefaulted_GetRef();Bytes.SetNumUninitialized(Count);
  if(ok_export_shared_clip(TCHAR_TO_UTF8(*Root),TCHAR_TO_UTF8(*Key),Bytes.GetData(),Count)!=Count){Error=TEXT("Shared curve export failed");return false;}
  const float Duration=ok_shared_clip_duration(Bytes.GetData(),Bytes.Num());if(!FMath::IsFinite(Duration)||Duration<=0.f){Error=TEXT("Invalid source duration");return false;}Durations.Add(Duration);
 }
 for(int32 I=0;I<Clips.Num();++I){const FString Name=FString::Printf(TEXT("Shared_%04d"),I);
  TStrongObjectPtr<UKKSharedAnimation> Asset(NewObject<UKKSharedAnimation>(CreatePackage(*(Destination/Name)),*Name,RF_Public|RF_Standalone));
  Asset->Source=Source;Asset->Clip=Clips[I];Asset->Duration=Durations[I];Asset->AuthoredCurves=MoveTemp(Resources[I]);
  FAssetRegistryModule::AssetCreated(Asset.Get());Asset->MarkPackageDirty();FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
  const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
  if(!UPackage::SavePackage(Asset->GetOutermost(),Asset.Get(),*File,Args)){Error=TEXT("Cannot save shared animation; preserve partial output and choose a fresh destination");return false;}Out.Add(Asset.Get());
 }
 UE_LOG(LogTemp,Display,TEXT("Shared original animations saved clips=%d character_references=0 destination=%s"),Out.Num(),*Destination);return true;
#else
 Error=TEXT("Shared animation import requires an editor build");return false;
#endif
}
