#pragma once
#include "KKMainItemPlus.h"
TArray<FString> OKForgeOpaqueUniformNames();
TArray<FString> OKForgeOpaqueTextureNames();
bool OKForgeOpaqueTextureIsColor(const FString& Property);
UMaterial* OKMakeForgeOpaqueMaterial(const FOKNativeItemDraw& Draw);
