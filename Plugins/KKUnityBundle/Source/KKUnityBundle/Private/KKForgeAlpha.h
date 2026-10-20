#pragma once
#include "KKMainItemPlus.h"
TArray<FString> OKForgeAlphaUniformNames();
TArray<FString> OKForgeAlphaTextureNames();
bool OKForgeAlphaTextureIsColor(const FString& Property);
UMaterial* OKMakeForgeAlphaMaterial(const FOKNativeItemDraw& Draw);
