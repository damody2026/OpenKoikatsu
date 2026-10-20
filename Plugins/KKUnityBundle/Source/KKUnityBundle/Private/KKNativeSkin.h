#pragma once
#include "KKRuntimeSharedMaterial.h"
class UMaterial;
// Editor preview uses the same installed program and bindings as cooked runtime.
UMaterial* OKMakeNativeSkinMaterial(const FKKRuntimeMaterialPart& Part,
    const TMap<FString,TObjectPtr<UTexture2D>>& Textures,
    const FKKRuntimeMaterialScene& Scene,FString& Error,const TSet<FString>* Exposed=nullptr);
