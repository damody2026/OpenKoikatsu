#pragma once
#include "KKMainItemPlus.h"
TArray<FString> OKForgeItemUniformNames();
TArray<FString> OKForgeItemTextureNames();
bool OKForgeItemTextureIsColor(const FString& Property);
UMaterial* OKMakeForgeItemMaterial(const FOKNativeItemDraw& Draw);
