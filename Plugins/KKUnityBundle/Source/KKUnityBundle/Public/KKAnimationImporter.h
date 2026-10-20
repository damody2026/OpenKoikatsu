#pragma once
#include "CoreMinimal.h"
class UKKImportedAnimationLibrary;
class UKKSharedAnimation;
namespace KKAnimationImporter {
KKUNITYBUNDLE_API bool ImportShared(const FString& NativeRoot,const FString& Source,const TArray<FString>& Clips,
 const FString& Destination,TArray<UKKSharedAnimation*>& Out,FString& Error);
// Clip labels must be qualified by Source. Never resolve ambiguous bare names.
KKUNITYBUNDLE_API UKKImportedAnimationLibrary* Import(const FString& Card,const FString& CharacterPath,
 const FString& Source,const TArray<FString>& Clips,const FString& Destination,int32 Outfit,int32 FPS,FString& Error,const FString& NativeRoot=FString());
}
