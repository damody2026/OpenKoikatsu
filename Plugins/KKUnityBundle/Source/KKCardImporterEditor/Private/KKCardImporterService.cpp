#include "KKCardImporterService.h"
#include "KKImportDestination.h"
#include "KKImportIndependentCardCommandlet.h"
#include "KKImportedCharacter.h"
#include "KKRustExtract.h"
#include "KKCardSource.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/OutputDevice.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
namespace KKCardImporter {
bool Validate(const FString& Card,const FString& Destination,int32 Outfit,FString& Error,const FString& RequestedRoot){
 FString GameRoot=RequestedRoot;
 Error.Reset();
 if(!GameRoot.IsEmpty()&&(GameRoot.Contains(TEXT("\""))||GameRoot.Contains(TEXT("\n"))||GameRoot.Contains(TEXT("\r"))||!IFileManager::Get().DirectoryExists(*(GameRoot/TEXT("abdata"))))){Error=TEXT("Select the KKS resource directory containing abdata.");return false;}
 if(Card.Contains(TEXT("\""))||Card.Contains(TEXT("\n"))||Card.Contains(TEXT("\r"))||Destination.Contains(TEXT("\""))||Destination.Contains(TEXT("\n"))||Destination.Contains(TEXT("\r"))){Error=TEXT("Paths cannot contain quotes or line breaks.");return false;}
 if(!IFileManager::Get().FileExists(*Card)){Error=TEXT("Select an existing independent PNG/APNG card.");return false;}
 const FString Ext=FPaths::GetExtension(Card);if(!Ext.Equals(TEXT("png"),ESearchCase::IgnoreCase)&&!Ext.Equals(TEXT("apng"),ESearchCase::IgnoreCase)){Error=TEXT("Select a PNG or APNG card.");return false;}
 if(!Destination.StartsWith(TEXT("/Game/"))||!FPackageName::IsValidLongPackageName(Destination)){Error=TEXT("Choose a new asset folder such as /Game/IndependentImports/MyCharacter.");return false;}
 if(Outfit < -1){Error=TEXT("Outfit must be -1 (all) or a non-negative index.");return false;}
 if(!OKValidateImportDestination(Destination,Error))return false;
 return true;
}
struct FImportErrors : FOutputDevice {
 FString Last;
 FImportErrors(){GLog->AddOutputDevice(this);}
 ~FImportErrors(){GLog->RemoveOutputDevice(this);}
 virtual void Serialize(const TCHAR* Text,ELogVerbosity::Type Verbosity,const FName& Category) override {if(Verbosity==ELogVerbosity::Error)Last=FString(Text).Left(4000);}
};
UKKImportedCharacter* Import(const FString& Card,const FString& Destination,int32 Outfit,FString& Error,const FString& RequestedRoot){
 FString GameRoot=RequestedRoot;
 if(!Validate(Card,Destination,Outfit,Error,GameRoot))return nullptr;
 if(GameRoot.IsEmpty()&&!OKResolveCardSource(Card,GameRoot,Error))return nullptr;
 // Use the native portable session for structured diagnostics before invoking the shared exporter.
 FOkSession Probe;if(!Probe.Open(GameRoot,Card,0,Error)||(GameRoot.IsEmpty()&&!Probe.IsPortable())){Error=(GameRoot.IsEmpty()?TEXT("Independent card required (original KKS cards need the resource directory): "):TEXT("KKS card loading failed: "))+Error;return nullptr;}
 if(Outfit>=Probe.CoordinateCount()){Error=FString::Printf(TEXT("Outfit index out of range; this card has %d outfit(s)."),Probe.CoordinateCount());return nullptr;}Probe.Close();
 FImportErrors Errors;TStrongObjectPtr<UKKImportIndependentCardCommandlet> Command(NewObject<UKKImportIndependentCardCommandlet>());
 const FString Params=FString::Printf(TEXT("-Card=\"%s\" -Destination=\"%s\" -Outfit=%d -GameRoot=\"%s\""),*Card,*Destination,Outfit,*GameRoot);
 const int32 Result=Command->Main(Params);GLog->FlushThreadedLogs();
 if(Result!=0){Error=Errors.Last.IsEmpty()?TEXT("Import failed. Check Output Log; partially saved assets are preserved. Choose a new folder for retry."):Errors.Last;return nullptr;}
 auto* Manifest=LoadObject<UKKImportedCharacter>(nullptr,*(Destination/TEXT("Character.Character")));
 if(!Manifest){Error=TEXT("Import returned success but the saved Character asset is missing. Check Output Log.");return nullptr;}
 return Manifest;
}
}
