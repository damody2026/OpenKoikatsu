#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class UOKCharacterController;
class UWidget;
namespace OKCharacterMCPRegistry
{
 using FRows=TArray<TSharedPtr<FJsonValue>>;
 FRows Parameters(UOKCharacterController* G);
 FRows Controls(UOKCharacterController* G,TMap<FString,UWidget*>* Widgets=nullptr);
 bool SetParameters(UOKCharacterController* G,const FRows& Edits,FString& Error);
 bool SetControl(UOKCharacterController* G,const FString& Key,TSharedPtr<FJsonValue> Value,FString& Error);
 bool Validate(const TSharedPtr<FJsonObject>& Descriptor,const TSharedPtr<FJsonValue>& Value,FString& Error);
}
