#include "KKCardSource.h"
#if WITH_EDITOR
#include "KKCardReader.h"
#include "KKLocalMods.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
namespace {
bool Link(const FString& Target,const FString& Source){IFileManager::Get().MakeDirectory(*FPaths::GetPath(Target),true);
#if PLATFORM_WINDOWS
 const FString A=FPaths::ConvertRelativePathToFull(Target),B=FPaths::ConvertRelativePathToFull(Source);
 if(CreateHardLinkW(*A,*B,nullptr))return true;
 if(GetLastError()==ERROR_NOT_SAME_DEVICE)return CopyFileW(*B,*A,true)!=0;
 return false;
#else
 return false;
#endif
}
}
#endif
bool OKResolveCardSource(const FString& Card,FString& SourceRoot,FString& Error){
 SourceRoot.Reset();Error.Reset();
#if WITH_EDITOR
 TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Card)){Error=TEXT("Cannot read card");return false;}
 // Detect the actual ancillary chunk, never a byte-pattern inside compressed image pixels.
 if(Bytes.Num()<8){Error=TEXT("Invalid PNG");return false;}int64 P=8;bool End=false;
 while(P+12<=Bytes.Num()){const uint32 N=(uint32(Bytes[P])<<24)|(uint32(Bytes[P+1])<<16)|(uint32(Bytes[P+2])<<8)|Bytes[P+3];if(int64(N)>Bytes.Num()-P-12){Error=TEXT("Truncated PNG");return false;}
  if(FMemory::Memcmp(Bytes.GetData()+P+4,"okPv",4)==0)return true;
  if(FMemory::Memcmp(Bytes.GetData()+P+4,"IEND",4)==0){End=true;break;}P+=int64(N)+12;
 }
 if(!End){Error=TEXT("Missing PNG IEND");return false;}
 FKKCharacterFile Parsed;if(!UKKCardReader::LoadFromBytes(Bytes,Parsed,Error))return false;
 FKKLocalModIndex Mods;Mods.BuildForCard(Card);if(!Mods.Diagnostics.IsEmpty()){Error=FString::Join(Mods.Diagnostics,TEXT("\n"));return false;}
 for(const FString& Diagnostic:Mods.SkippedGameMods)UE_LOG(LogTemp,Display,TEXT("Skipped invalid game zipmod: %s"),*Diagnostic);
 for(const auto& Entry:Mods.ByGuid)if(Entry.Value.Num()>1){Error=TEXT("Conflicting import zipmod GUID: ")+Entry.Key;return false;}
 // Vanilla data comes from the OK base library (Rust: oklib::open_original_card); this view only carries the
 // card's own zipmods. No Unity base bundles (the retired native_v1 pack) and no project uncensor pack.
 if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_BASE_LIBRARY")).IsEmpty()&&!FPaths::FileExists(FPaths::ProjectContentDir()/TEXT("IndependentResources/base_v1/oklib.json")))
 {Error=TEXT("OK base library missing: Content/IndependentResources/base_v1 (or OK_BASE_LIBRARY)");return false;}
 const FString View=FPaths::ProjectSavedDir()/TEXT("NativeImportSources")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
 // Preserve every discovered archive, including body descriptors. A unique
 // basename prevents recursive game mods with identical filenames colliding.
 int32 ZipIndex=0;
 for(const FString& Path:Mods.ZipmodPaths){const FString Name=FString::Printf(TEXT("%06d_"),ZipIndex++)+FPaths::GetCleanFilename(Path);
  if(!Link(View/TEXT("mods")/Name,Path)){Error=TEXT("Cannot attach import zipmod: ")+Path;return false;}}
 IFileManager::Get().MakeDirectory(*(View/TEXT("mods")),true);if(!FFileHelper::SaveStringToFile(TEXT("1"),*(View/TEXT("require_complete_mods")))){Error=TEXT("Cannot mark strict import source");return false;}
 SourceRoot=FPaths::ConvertRelativePathToFull(View);UE_LOG(LogTemp,Display,TEXT("Standalone KKS source: OK base library + import_zipmods=%d detected_game=%s"),Mods.ZipmodPaths.Num(),Mods.GameRoot.IsEmpty()?TEXT("none"):*Mods.GameRoot);return true;
#else
 Error=TEXT("Card source preparation requires Editor");return false;
#endif
}
