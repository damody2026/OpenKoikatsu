#include "KKImportDestination.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

bool OKValidateImportDestination(const FString& Destination,FString& Error){
 Error.Reset();
 if(!Destination.StartsWith(TEXT("/Game/"),ESearchCase::CaseSensitive)||!FPackageName::IsValidLongPackageName(Destination)){
  Error=TEXT("Choose a new asset folder under /Game/.");return false;
 }
 const FString Disk=FPackageName::LongPackageNameToFilename(Destination);
 if(IFileManager::Get().DirectoryExists(*Disk)||IFileManager::Get().FileExists(*(Disk+TEXT(".uasset")))||IFileManager::Get().FileExists(*(Disk+TEXT(".umap")))){
  Error=TEXT("Destination already exists. Choose a new folder; existing assets are never overwritten.");return false;
 }
 for(TObjectIterator<UPackage> It;It;++It){const FString Name=It->GetName();
  if(Name.Equals(Destination,ESearchCase::IgnoreCase)||Name.StartsWith(Destination+TEXT("/"),ESearchCase::IgnoreCase)){
   Error=TEXT("Destination contains loaded packages. Choose a fresh folder.");return false;
  }
 }
 return true;
}
