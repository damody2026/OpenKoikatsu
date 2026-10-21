#pragma once
#include "KKRuntimeMaterial.h"
#include "Engine/EngineTypes.h"
class UMaterialInterface;class UMaterialInstanceDynamic;
struct FKKRuntimeShaderUniform {FString Name;int32 CB=0,Offset=0,Dim=0,Type=0,Rows=0;FLinearColor Default=FLinearColor::Black;bool Gamma=false;};
struct FKKRuntimeShaderTexture {FString Name,Default;int32 Index=0,Sampler=0,Dim=2;};
struct FKKRuntimeShaderLayout {TArray<int32> Sizes;TArray<FKKRuntimeShaderUniform> Uniforms;TArray<FKKRuntimeShaderTexture> Textures;};
struct FKKRuntimeMaterialScene {FLinearColor Light=FLinearColor::White,Ambient=FLinearColor(128.f/255.f,128.f/255.f,128.f/255.f,1);FVector3f LightDirectionUnity=FVector3f(0,.6427876f,.7660446f);bool StudioShadowGlobals=true;bool EnableShadows=true;TMap<FName,FLinearColor> Uniforms;
 // Shader globals the host sets (Unity values, as Shader.SetGlobalColor / SetGlobalFloat: OK Studio's scene
 // _ambientshadowG, _LineColorG, _linewidthG); they replace whatever the binding resolved. Empty for cards.
 TMap<FName,FLinearColor> Globals;};
struct FKKRuntimeMaterialBinding {TMap<FName,float> Scalars;TMap<FName,FLinearColor> Vectors;TMap<FName,int32> Textures;};
KKRUNTIMEPOSE_API const FKKRuntimeShaderLayout& OKRuntimeForgeOpaqueLayout();
// Shared authoring recipe, independent of card values. Never compile at runtime.
KKRUNTIMEPOSE_API FString OKRuntimeForgeOpaqueProgram();
KKRUNTIMEPOSE_API bool OKPrepareRuntimeForgeOpaqueBinding(const FKKRuntimeMaterialPart&,
 const FKKRuntimeMaterialScene&,FKKRuntimeMaterialBinding&,FString& Error);
// Requires a cooked common parent with the exact schema and complete parameters.
KKRUNTIMEPOSE_API UMaterialInstanceDynamic* OKCreateRuntimeForgeOpaqueMaterial(
 const FKKRuntimeMaterialPart&,TConstArrayView<UTexture2D*>,UMaterialInterface* Parent,
 const FKKRuntimeMaterialScene&,FString& Error);

// Parent family identity is independent of authored card parameters. ForgeStudioOpaque is ForgeStudio's program
// (Shader Forge/main_item_studio) as a depth-writing masked parent for opaque Studio items: the translucent ForgeStudio
// parent writes no depth, so overlapping items were ordered by distance and showed through each other.
enum class EKKRuntimeMaterialRecipe:uint8 {None=0,ForgeOpaque=1,ForgeAlphaMasked=2,ForgeAlphaTranslucent=3,ForgeItem=4,ForgeHair=5,ForgeHairFront=6,ForgeStudioAlpha=7,ForgeEyeWhite=8,ForgeEye=9,KoikanoEyeWhite=10,KoikanoEye=11,ForgeNose=12,KoikanoSkin=13,ForgeSkin=14,KoikanoNose=15,KoikanoBrow=16,KoikanoClothesOpaque=17,KoikanoClothesColor=18,KoikanoClothesItem=19,KoikanoClothesItemAlpha=20,KoikanoClothesAlphaCull=21,KoikanoClothesAlpha=22,IBLAlpha=23,KoikanoClothesItemTranslucent=24,KoikanoClothesItemAlphaTranslucent=25,IBLDepth=26,KoikanoHairSun0=27,KoikanoHairSun1=28,KoikanoHairSun2=29,KoikanoHairSun3=30,KoikanoHairSun4=31,KoikanoHairSun5=32,KoikanoHairSun6=33,KoikanoHairSun7=34,KoikanoGlasses0=35,KoikanoGlasses1=36,KoikanoGlasses2=37,KoikanoGlasses3=38,KoikanoGlasses4=39,KoikanoGlasses5=40,KoikanoGlasses6=41,KoikanoGlasses7=42,KoikanoGlasses8=43,KoikanoGlasses9=44,KoikanoGlasses10=45,KoikanoGlasses11=46,IBLOpaque=47,KKUSSItem=48,XukmiItem=49,ForgeGlasses=50,ForgeMDK=51,Goo=52,ForgeDither=53,ForgeEmission=54,KKUSS=55,ForgeEyeWhiteLod=56,OilPainting=57,BetterMatcap=58,KoikanoLowAlphaCull=59,KoikanoLowAlpha=60,ForgeStudio=61,ForgeStudioOpaque=62};
KKRUNTIMEPOSE_API EKKRuntimeMaterialRecipe OKRuntimeSharedMaterialRecipe(const FKKRuntimeMaterialPart&);
KKRUNTIMEPOSE_API const FKKRuntimeShaderLayout* OKRuntimeSharedMaterialLayout(EKKRuntimeMaterialRecipe);
KKRUNTIMEPOSE_API FString OKRuntimeSharedMaterialProgram(EKKRuntimeMaterialRecipe);
KKRUNTIMEPOSE_API bool OKPrepareRuntimeSharedMaterialBinding(const FKKRuntimeMaterialPart&,const FKKRuntimeMaterialScene&,FKKRuntimeMaterialBinding&,FString& Error,bool Outline=false);
KKRUNTIMEPOSE_API UMaterialInstanceDynamic* OKCreateRuntimeSharedMaterial(const FKKRuntimeMaterialPart&,TConstArrayView<UTexture2D*>,UMaterialInterface*,const FKKRuntimeMaterialScene&,FString& Error,bool Outline=false);

KKRUNTIMEPOSE_API TConstArrayView<EKKRuntimeMaterialRecipe> OKRuntimeSharedMaterialRecipes();
KKRUNTIMEPOSE_API FString OKRuntimeSharedMaterialParentName(EKKRuntimeMaterialRecipe);
// The recipe whose parent stands in when a material root was baked before this recipe existed (ForgeStudioOpaque →
// ForgeStudio, same program and layout); None for a recipe every root has. Loaders that require every parent should
// accept the stand-in instead of failing on an older root.
KKRUNTIMEPOSE_API EKKRuntimeMaterialRecipe OKRuntimeSharedMaterialFallback(EKKRuntimeMaterialRecipe);
// Root/<parent>, or the fallback recipe's parent when the root lacks it; null when neither is there.
KKRUNTIMEPOSE_API UMaterialInterface* OKLoadRuntimeSharedMaterialParent(const FString& Root,EKKRuntimeMaterialRecipe,bool Outline=false);

KKRUNTIMEPOSE_API bool OKRuntimeSharedMaterialTranslucent(EKKRuntimeMaterialRecipe);
KKRUNTIMEPOSE_API bool OKRuntimeSharedMaterialTwoSided(EKKRuntimeMaterialRecipe);
// Additional installed shader families construct their own native varyings.
KKRUNTIMEPOSE_API FString OKRuntimeSharedMaterialVaryings(EKKRuntimeMaterialRecipe);
// Installed Koikano forward vertex programs supply the L2 probe term as an
// interpolated varying; it must be evaluated before rasterization.
KKRUNTIMEPOSE_API bool OKRuntimeSharedMaterialUsesVertexAmbient(EKKRuntimeMaterialRecipe);
class UMaterial;class UMaterialExpression;
KKRUNTIMEPOSE_API UMaterialExpression* OKMakeRuntimeVertexAmbient(UMaterial*,const FKKRuntimeMaterialScene&);
// Standard indirect diffuse: native vertex L2 plus pixel normal L0/L1.
KKRUNTIMEPOSE_API UMaterialExpression* OKMakeRuntimeProbeAmbient(UMaterial*);

KKRUNTIMEPOSE_API EBlendMode OKRuntimeSharedMaterialBlendMode(EKKRuntimeMaterialRecipe);

KKRUNTIMEPOSE_API const FKKRuntimeShaderLayout* OKRuntimeOutlineLayout(EKKRuntimeMaterialRecipe);
KKRUNTIMEPOSE_API FString OKRuntimeOutlineProgram(EKKRuntimeMaterialRecipe);
// The installed outline pass's vertex program (Tools/generate_runtime_outlines.lua) and its own constant layout.
KKRUNTIMEPOSE_API const FKKRuntimeShaderLayout* OKRuntimeOutlineVertexLayout(EKKRuntimeMaterialRecipe);
KKRUNTIMEPOSE_API FString OKRuntimeOutlineVertexProgram(EKKRuntimeMaterialRecipe);
// 1 when outlines use the earlier approximate width (ok.OutlineApprox / OK_OUTLINE_APPROX), else 0.
KKRUNTIMEPOSE_API float OKRuntimeOutlineApprox();
// Own-graph outline materials (OK_SHARED_PARENTS=0, OK Studio items and maps): the installed outline vertex program of
// the part's family as the graph's world position offset, the part's resolved values (OKPrepareRuntimeSharedMaterialBinding)
// written into the program, as the shared outline parents extrude. Texture(property) gives the part's texture (null: the
// property's default). Editor only (graph authoring); null with Error when the family has no installed vertex program
// or the program reads something an own graph cannot supply.
class UMaterial;class UMaterialExpression;class UTexture;
KKRUNTIMEPOSE_API UMaterialExpression* OKMakeRuntimeOutlineVertexOffset(UMaterial*,const FKKRuntimeMaterialPart&,const FKKRuntimeMaterialScene&,
 TFunctionRef<UTexture*(const FString&)> Texture,FString& Error);
