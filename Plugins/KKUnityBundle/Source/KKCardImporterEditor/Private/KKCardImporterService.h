#pragma once
#include "CoreMinimal.h"
class UKKImportedCharacter;
namespace KKCardImporter {
 bool Validate(const FString& Card,const FString& Destination,int32 Outfit,FString& Error,const FString& GameRoot=FString());
 UKKImportedCharacter* Import(const FString& Card,const FString& Destination,int32 Outfit,FString& Error,const FString& GameRoot=FString());
}
