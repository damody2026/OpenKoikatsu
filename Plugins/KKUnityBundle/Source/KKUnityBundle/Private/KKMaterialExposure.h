#pragma once
#include "CoreMinimal.h"

class UMaterial;
class UMaterialExpressionCustom;
class UMaterialExpressionTextureBase;
class UTexture2D;

// Material overrides on the editor graphs (AKKPreviewActor::SetMaterialOverrides). These graphs bake every Unity
// uniform as a literal; a uniform or texture whose name is in Exposed becomes a material parameter of that name
// instead, so the part's MID can change it. A part without overrides keeps its literal graph (and its shader cost);
// the exposed names are part of the graph, so KKMaterialDedupe keys them apart.

// The custom input carrying uniform Name (a float4; a scalar uniform in .x), added to PS on first use with Default as
// the parameter default. Empty when Name is not exposed.
FString OKExposedUniformInput(UMaterial* Mat, UMaterialExpressionCustom* PS, const TSet<FString>* Exposed,
	const FString& Name, int32 Dim, const FLinearColor& Default);
// HLSL for component C of an exposed input, as the native cb word of uniform type Type (0 float, 1 int, 2 bool).
FString OKExposedComponent(const FString& Input, int32 Component, int32 Type);
// A texture object for property Name: a parameter of that name when exposed, a plain object otherwise.
UMaterialExpressionTextureBase* OKNewTextureObject(UMaterial* Mat, const TSet<FString>* Exposed, const FString& Name);
// 1x1 white linear texture (Unity "white"), the default of an exposed pattern mask.
UTexture2D* OKExposureWhiteTexture();
// The Shader Forge/main_item_studio pattern properties: an override naming any of them gives the item graph its
// three pattern slots.
bool OKIsItemPatternProperty(const FString& Name);
// Pattern slot Slot (1..3): the uniforms _Patternuv, _patternrotator, _patternclamp, _PatternMask_ST, _Color_2, then
// (with bTexture) the mask texture _PatternMask.
TArray<FString> OKItemPatternNames(int32 Slot, bool bTexture = true);
// The source material name and card source_property of a session part (empty when unknown).
void OKPartMaterialIdentity(const class FOkSession& Session, int32 Part, FString& OutMaterial, FString& OutSourceProperty);
