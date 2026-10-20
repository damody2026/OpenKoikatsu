#pragma once
#include "CoreMinimal.h"
class UTexture;
class UTexture2D;
class UMaterial;

struct FOKNativeItemDraw
{
    TMap<FString,FLinearColor> Values;
    TMap<FString,TObjectPtr<UTexture2D>> Textures;
    float ShadowDepth=0.26f;
    float LineDepth=1.f;
    float LineWidth=0.5f;
    // Studio.SceneInfo default RGB, confirmed by live shader-global readback.
    // ShadowDepth is the effective alpha (1 - Studio scene shadow depth).
    FLinearColor AmbientShadowColorUnity=FLinearColor(128.f/255.f,128.f/255.f,128.f/255.f,1.f);
    // Studio's active layer-10 Directional Chara default, expressed as
    // direction from surface to light in Unity world space. Scene overrides
    // may replace this without changing any card/material property.
    FVector3f MainLightToSurfaceUnity=FVector3f(0.f,0.6427876f,0.7660446f);
    // Preview's common shadow camera supplies Unity's external screen visibility.
    UTexture* ForgeShadowDepth=nullptr;
    FVector ForgeShadowCenter,ForgeShadowRight,ForgeShadowUp,ForgeShadowLight;
    float ForgeShadowWidth=260.f,ForgeShadowDistance=500.f;
    // Uniforms/textures this part's material overrides name: built as parameters (KKMaterialExposure.h).
    const TSet<FString>* Exposed=nullptr;
    // The part's runtime material description and the scene's shader globals: the outline pass extrudes by the installed
    // outline vertex program (OKMakeRuntimeOutlineVertexOffset) when set, else by the earlier approximate width.
    TSharedPtr<const struct FKKRuntimeMaterialPart> OutlinePart;
    TMap<FName,FLinearColor> OutlineGlobals;
};

TArray<FString> OKMainItemPlusUniformNames();
TArray<FString> OKMainItemPlusTextureNames();
bool OKMainItemPlusTextureIsColor(const FString& Property);
UMaterial* OKMakeMainItemPlusMaterial(const FOKNativeItemDraw& Draw,bool bOutline);
