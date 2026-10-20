#include "KKRuntimeSharedMaterial.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/VolumeTexture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Json.h"
#include "Misc/PackageName.h"
struct FOKForgeOpaqueUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKForgeOpaqueTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKForgeOpaqueProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* ForgeOpaqueDefaultTexture;};
#include "SharedShaders/ForgeOpaqueBindings.inl"
#include "SharedShaders/ForgeOpaqueProgram.inl"
struct FOKForgeAlphaUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKForgeAlphaTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKForgeAlphaProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* ForgeAlphaDefaultTexture;};
#include "SharedShaders/ForgeAlphaBindings.inl"
#include "SharedShaders/ForgeAlphaProgram.inl"
struct FOKForgeUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKForgeTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKForgeProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* ForgeDefaultTexture;};
#include "SharedShaders/ForgeItemBindings.inl"
#include "SharedShaders/ForgeItemProgram.inl"
struct FOKForgeHairUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKForgeHairTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKForgeHairProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* ForgeHairDefaultTexture;};
#include "SharedShaders/ForgeHairBindings.inl"
#include "SharedShaders/ForgeHairProgram.inl"

struct FOKForgeHairFrontUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKForgeHairFrontTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKForgeHairFrontProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* ForgeHairFrontDefaultTexture;};
#include "SharedShaders/ForgeHairFrontBindings.inl"
#include "SharedShaders/ForgeHairFrontProgram.inl"
struct FOKForgeStudioAlphaUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKForgeStudioAlphaTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKForgeStudioAlphaProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* ForgeStudioAlphaDefaultTexture;};
#include "SharedShaders/ForgeStudioAlphaBindings.inl"
#include "SharedShaders/ForgeStudioAlphaProgram.inl"
struct FOKForgeEyeWhiteUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKForgeEyeWhiteTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKForgeEyeWhiteProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* ForgeEyeWhiteDefaultTexture;};
#include "SharedShaders/ForgeEyeWhiteBindings.inl"
#include "SharedShaders/ForgeEyeWhiteProgram.inl"
struct FOKForgeEyeUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKForgeEyeTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKForgeEyeProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* ForgeEyeDefaultTexture;};
#include "SharedShaders/ForgeEyeBindings.inl"
#include "SharedShaders/ForgeEyeProgram.inl"
struct FOKKoikanoEyeWhiteUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKKoikanoEyeWhiteTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKKoikanoEyeWhiteProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* KoikanoEyeWhiteDefaultTexture;};
#include "SharedShaders/KoikanoEyeWhiteBindings.inl"
#include "SharedShaders/KoikanoEyeWhiteProgram.inl"
struct FOKForgeNoseUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKForgeNoseTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKForgeNoseProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* ForgeNoseDefaultTexture;};
#include "SharedShaders/ForgeNoseBindings.inl"
#include "SharedShaders/ForgeNoseProgram.inl"
struct FOKAdditionalUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKAdditionalTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKAdditionalProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* DefaultTexture;};
#include "SharedShaders/AdditionalMaterialPrograms.inl"

struct FOKKoikanoEyeUniform {const TCHAR* Name;int32 CB,Offset,Dim,Type,Rows;};
struct FOKKoikanoEyeTexture {const TCHAR* Name;int32 Index,Sampler,Dim;};
struct FOKKoikanoEyeProperty {const TCHAR* Name;int32 Type;bool Gamma;FLinearColor Default;const TCHAR* KoikanoEyeDefaultTexture;};
#include "SharedShaders/KoikanoEyeBindings.inl"
#include "SharedShaders/KoikanoEyeProgram.inl"







namespace {
bool Automatic(const FKKRuntimeShaderUniform& U){return U.Rows||U.Name==TEXT("_Time")||U.Name==TEXT("_WorldSpaceCameraPos");}
// Unity Color properties are sRGB-authored even without [Gamma].
// [Gamma] additionally marks float/vector properties; alpha is never converted.
float Linear(float V){return V<=.04045f?V/12.92f:FMath::Pow((V+.055f)/1.055f,2.4f);}
FLinearColor Convert(FLinearColor C,bool Gamma){if(Gamma){C.R=Linear(C.R);C.G=Linear(C.G);C.B=Linear(C.B);}return C;}
FKKRuntimeShaderLayout BuildAdditionalLayout(TConstArrayView<int32> Sizes,TConstArrayView<FOKAdditionalUniform> Uniforms,TConstArrayView<FOKAdditionalTexture> Textures,TConstArrayView<FOKAdditionalProperty> Properties,EKKRuntimeMaterialRecipe Recipe){
 FKKRuntimeShaderLayout O;O.Sizes.Append(Sizes.GetData(),Sizes.Num());
 for(const auto& U:Uniforms){FKKRuntimeShaderUniform V;V.Name=U.Name;V.CB=U.CB;V.Offset=U.Offset;V.Dim=U.Dim;V.Type=U.Type;V.Rows=U.Rows;V.Default=V.Name.EndsWith(TEXT("_ST"))?FLinearColor(1,1,0,0):FLinearColor(0,0,0,0);if(V.Name==TEXT("Epsilon"))V.Default.R=1e-10f;
  for(const auto& P:Properties)if(V.Name==P.Name){V.Default=P.Default;V.Gamma=P.Gamma||P.Type==0;break;}V.Default=Convert(V.Default,V.Gamma);O.Uniforms.Add(V);
 }
 for(const auto& T:Textures){FKKRuntimeShaderTexture V;V.Name=T.Name;V.Index=T.Index;V.Sampler=T.Sampler;V.Dim=T.Dim;V.Default=TEXT("white");for(const auto& P:Properties)if(V.Name==P.Name){V.Default=P.DefaultTexture;break;}O.Textures.Add(V);}
 // These transforms belong to the original vertex stage, so pixel-only
 // reflection does not list them. Keep them in a separate common parameter CB.
 TArray<FString> VertexST;if((int32(Recipe)>=17&&int32(Recipe)<=22)||Recipe==EKKRuntimeMaterialRecipe::KoikanoLowAlphaCull||Recipe==EKKRuntimeMaterialRecipe::KoikanoLowAlpha)VertexST={TEXT("_texcoord_ST"),TEXT("_texcoord2_ST")};
 if(int32(Recipe)>=27&&int32(Recipe)<=34)VertexST={TEXT("_texcoord_ST"),TEXT("_texcoord2_ST")};
 if(int32(Recipe)>=35&&int32(Recipe)<=46)VertexST={TEXT("_texcoord_ST")};
 if(Recipe==EKKRuntimeMaterialRecipe::Goo||Recipe==EKKRuntimeMaterialRecipe::BetterMatcap)VertexST={TEXT("_texcoord_ST")};
 if(Recipe==EKKRuntimeMaterialRecipe::OilPainting)VertexST={TEXT("_MainTex_ST")};
 if(Recipe==EKKRuntimeMaterialRecipe::KKUSSItem)VertexST={TEXT("_MainTex_ST"),TEXT("_overtex1_ST"),TEXT("_overtex2_ST"),TEXT("_overtex3_ST")};
 if(Recipe==EKKRuntimeMaterialRecipe::IBLAlpha||Recipe==EKKRuntimeMaterialRecipe::IBLOpaque)VertexST={TEXT("_MainTex_ST"),TEXT("_NormalMap_ST"),TEXT("_NormalMap2_ST"),TEXT("_overtex1_ST"),TEXT("_overtex2_ST"),TEXT("_overtex3_ST")};
 if(!VertexST.IsEmpty()){const int32 CB=O.Sizes.Num();O.Sizes.Add(VertexST.Num());for(int32 I=0;I<VertexST.Num();++I){FKKRuntimeShaderUniform U;U.Name=VertexST[I];U.CB=CB;U.Offset=I*16;U.Dim=4;U.Default=FLinearColor(1,1,0,0);O.Uniforms.Add(U);}}
 return O;
}
#include "SharedShaders/OutlinePrograms.inl"
const FKKRuntimeShaderLayout* AdditionalLayout(EKKRuntimeMaterialRecipe Recipe){
 if(Recipe==EKKRuntimeMaterialRecipe::IBLDepth)return &OKRuntimeForgeOpaqueLayout();
 if(Recipe==EKKRuntimeMaterialRecipe::ForgeStudioOpaque)Recipe=EKKRuntimeMaterialRecipe::ForgeStudio;
 if(Recipe==EKKRuntimeMaterialRecipe::KoikanoClothesItemTranslucent)Recipe=EKKRuntimeMaterialRecipe::KoikanoClothesItem;
 if(Recipe==EKKRuntimeMaterialRecipe::KoikanoClothesItemAlphaTranslucent)Recipe=EKKRuntimeMaterialRecipe::KoikanoClothesItemAlpha;
#define OK_ADDITIONAL_LAYOUT(Name) case EKKRuntimeMaterialRecipe::Name:{static const auto L=BuildAdditionalLayout(MakeArrayView(G##Name##CBSize),MakeArrayView(G##Name##Uniforms),MakeArrayView(G##Name##Textures),MakeArrayView(G##Name##Properties),Recipe);return &L;}
 switch(Recipe){OK_ADDITIONAL_LAYOUT(KoikanoSkin) OK_ADDITIONAL_LAYOUT(ForgeSkin) OK_ADDITIONAL_LAYOUT(KoikanoNose) OK_ADDITIONAL_LAYOUT(KoikanoBrow) OK_ADDITIONAL_LAYOUT(KoikanoClothesOpaque) OK_ADDITIONAL_LAYOUT(KoikanoClothesColor) OK_ADDITIONAL_LAYOUT(KoikanoClothesItem) OK_ADDITIONAL_LAYOUT(KoikanoClothesItemAlpha) OK_ADDITIONAL_LAYOUT(KoikanoClothesAlphaCull) OK_ADDITIONAL_LAYOUT(KoikanoClothesAlpha) OK_ADDITIONAL_LAYOUT(IBLAlpha) OK_ADDITIONAL_LAYOUT(KoikanoHairSun0) OK_ADDITIONAL_LAYOUT(KoikanoHairSun1) OK_ADDITIONAL_LAYOUT(KoikanoHairSun2) OK_ADDITIONAL_LAYOUT(KoikanoHairSun3) OK_ADDITIONAL_LAYOUT(KoikanoHairSun4) OK_ADDITIONAL_LAYOUT(KoikanoHairSun5) OK_ADDITIONAL_LAYOUT(KoikanoHairSun6) OK_ADDITIONAL_LAYOUT(KoikanoHairSun7) OK_ADDITIONAL_LAYOUT(KoikanoGlasses0) OK_ADDITIONAL_LAYOUT(KoikanoGlasses1) OK_ADDITIONAL_LAYOUT(KoikanoGlasses2) OK_ADDITIONAL_LAYOUT(KoikanoGlasses3) OK_ADDITIONAL_LAYOUT(KoikanoGlasses4) OK_ADDITIONAL_LAYOUT(KoikanoGlasses5) OK_ADDITIONAL_LAYOUT(KoikanoGlasses6) OK_ADDITIONAL_LAYOUT(KoikanoGlasses7) OK_ADDITIONAL_LAYOUT(KoikanoGlasses8) OK_ADDITIONAL_LAYOUT(KoikanoGlasses9) OK_ADDITIONAL_LAYOUT(KoikanoGlasses10) OK_ADDITIONAL_LAYOUT(KoikanoGlasses11) OK_ADDITIONAL_LAYOUT(IBLOpaque) OK_ADDITIONAL_LAYOUT(KKUSSItem) OK_ADDITIONAL_LAYOUT(XukmiItem) OK_ADDITIONAL_LAYOUT(ForgeGlasses) OK_ADDITIONAL_LAYOUT(ForgeMDK) OK_ADDITIONAL_LAYOUT(Goo) OK_ADDITIONAL_LAYOUT(ForgeDither) OK_ADDITIONAL_LAYOUT(ForgeEmission) OK_ADDITIONAL_LAYOUT(KKUSS) OK_ADDITIONAL_LAYOUT(ForgeEyeWhiteLod) OK_ADDITIONAL_LAYOUT(OilPainting) OK_ADDITIONAL_LAYOUT(BetterMatcap) OK_ADDITIONAL_LAYOUT(KoikanoLowAlphaCull) OK_ADDITIONAL_LAYOUT(KoikanoLowAlpha) OK_ADDITIONAL_LAYOUT(ForgeStudio) default:return nullptr;}
#undef OK_ADDITIONAL_LAYOUT
}
FString AdditionalProgram(EKKRuntimeMaterialRecipe Recipe){
 if(Recipe==EKKRuntimeMaterialRecipe::IBLDepth)return TEXT("return float4(1,1,1,1);\n");
 if(Recipe==EKKRuntimeMaterialRecipe::ForgeStudioOpaque)Recipe=EKKRuntimeMaterialRecipe::ForgeStudio;
 if(Recipe==EKKRuntimeMaterialRecipe::KoikanoClothesItemTranslucent)Recipe=EKKRuntimeMaterialRecipe::KoikanoClothesItem;
 if(Recipe==EKKRuntimeMaterialRecipe::KoikanoClothesItemAlphaTranslucent)Recipe=EKKRuntimeMaterialRecipe::KoikanoClothesItemAlpha;
#define OK_ADDITIONAL_PROGRAM(Name) case EKKRuntimeMaterialRecipe::Name:return G##Name##Program;
 switch(Recipe){OK_ADDITIONAL_PROGRAM(KoikanoSkin) OK_ADDITIONAL_PROGRAM(ForgeSkin) OK_ADDITIONAL_PROGRAM(KoikanoNose) OK_ADDITIONAL_PROGRAM(KoikanoBrow) OK_ADDITIONAL_PROGRAM(KoikanoClothesOpaque) OK_ADDITIONAL_PROGRAM(KoikanoClothesColor) OK_ADDITIONAL_PROGRAM(KoikanoClothesItem) OK_ADDITIONAL_PROGRAM(KoikanoClothesItemAlpha) OK_ADDITIONAL_PROGRAM(KoikanoClothesAlphaCull) OK_ADDITIONAL_PROGRAM(KoikanoClothesAlpha) OK_ADDITIONAL_PROGRAM(IBLAlpha) OK_ADDITIONAL_PROGRAM(KoikanoHairSun0) OK_ADDITIONAL_PROGRAM(KoikanoHairSun1) OK_ADDITIONAL_PROGRAM(KoikanoHairSun2) OK_ADDITIONAL_PROGRAM(KoikanoHairSun3) OK_ADDITIONAL_PROGRAM(KoikanoHairSun4) OK_ADDITIONAL_PROGRAM(KoikanoHairSun5) OK_ADDITIONAL_PROGRAM(KoikanoHairSun6) OK_ADDITIONAL_PROGRAM(KoikanoHairSun7) OK_ADDITIONAL_PROGRAM(KoikanoGlasses0) OK_ADDITIONAL_PROGRAM(KoikanoGlasses1) OK_ADDITIONAL_PROGRAM(KoikanoGlasses2) OK_ADDITIONAL_PROGRAM(KoikanoGlasses3) OK_ADDITIONAL_PROGRAM(KoikanoGlasses4) OK_ADDITIONAL_PROGRAM(KoikanoGlasses5) OK_ADDITIONAL_PROGRAM(KoikanoGlasses6) OK_ADDITIONAL_PROGRAM(KoikanoGlasses7) OK_ADDITIONAL_PROGRAM(KoikanoGlasses8) OK_ADDITIONAL_PROGRAM(KoikanoGlasses9) OK_ADDITIONAL_PROGRAM(KoikanoGlasses10) OK_ADDITIONAL_PROGRAM(KoikanoGlasses11) OK_ADDITIONAL_PROGRAM(IBLOpaque) OK_ADDITIONAL_PROGRAM(KKUSSItem) OK_ADDITIONAL_PROGRAM(XukmiItem) OK_ADDITIONAL_PROGRAM(ForgeGlasses) OK_ADDITIONAL_PROGRAM(ForgeMDK) OK_ADDITIONAL_PROGRAM(Goo) OK_ADDITIONAL_PROGRAM(ForgeDither) OK_ADDITIONAL_PROGRAM(ForgeEmission) OK_ADDITIONAL_PROGRAM(KKUSS) OK_ADDITIONAL_PROGRAM(ForgeEyeWhiteLod) OK_ADDITIONAL_PROGRAM(OilPainting) OK_ADDITIONAL_PROGRAM(BetterMatcap) OK_ADDITIONAL_PROGRAM(KoikanoLowAlphaCull) OK_ADDITIONAL_PROGRAM(KoikanoLowAlpha) OK_ADDITIONAL_PROGRAM(ForgeStudio) default:return {};}
#undef OK_ADDITIONAL_PROGRAM
}
}
const FKKRuntimeShaderLayout& OKRuntimeForgeOpaqueLayout(){
 static const FKKRuntimeShaderLayout L=[](){FKKRuntimeShaderLayout O;O.Sizes.Append(GForgeOpaqueForwardCBSize,UE_ARRAY_COUNT(GForgeOpaqueForwardCBSize));
  for(const auto& U:GForgeOpaqueForwardUniforms){FKKRuntimeShaderUniform V;V.Name=U.Name;V.CB=U.CB;V.Offset=U.Offset;V.Dim=U.Dim;V.Type=U.Type;V.Rows=U.Rows;V.Default=V.Name.EndsWith(TEXT("_ST"))?FLinearColor(1,1,0,0):FLinearColor(0,0,0,0);
   if(V.Name==TEXT("Epsilon"))V.Default.R=1e-10f;for(const auto& P:GForgeOpaqueProperties)if(V.Name==P.Name){V.Default=P.Default;V.Gamma=P.Gamma||P.Type==0;break;}V.Default=Convert(V.Default,V.Gamma);O.Uniforms.Add(V);}
  for(const auto& T:GForgeOpaqueForwardTextures){FKKRuntimeShaderTexture V;V.Name=T.Name;V.Index=T.Index;V.Sampler=T.Sampler;V.Dim=T.Dim;V.Default=TEXT("white");for(const auto& P:GForgeOpaqueProperties)if(V.Name==P.Name){V.Default=P.ForgeOpaqueDefaultTexture;break;}O.Textures.Add(V);}return O;
 }();return L;
}
const FKKRuntimeShaderLayout& AlphaLayout(){
 static const FKKRuntimeShaderLayout L=[](){FKKRuntimeShaderLayout O;O.Sizes.Append(GForgeAlphaForwardCBSize,UE_ARRAY_COUNT(GForgeAlphaForwardCBSize));
  for(const auto& U:GForgeAlphaForwardUniforms){FKKRuntimeShaderUniform V;V.Name=U.Name;V.CB=U.CB;V.Offset=U.Offset;V.Dim=U.Dim;V.Type=U.Type;V.Rows=U.Rows;V.Default=V.Name.EndsWith(TEXT("_ST"))?FLinearColor(1,1,0,0):FLinearColor(0,0,0,0);
   if(V.Name==TEXT("Epsilon"))V.Default.R=1e-10f;for(const auto& P:GForgeAlphaProperties)if(V.Name==P.Name){V.Default=P.Default;V.Gamma=P.Gamma||P.Type==0;break;}V.Default=Convert(V.Default,V.Gamma);O.Uniforms.Add(V);}
  for(const auto& T:GForgeAlphaForwardTextures){FKKRuntimeShaderTexture V;V.Name=T.Name;V.Index=T.Index;V.Sampler=T.Sampler;V.Dim=T.Dim;V.Default=TEXT("white");for(const auto& P:GForgeAlphaProperties)if(V.Name==P.Name){V.Default=P.ForgeAlphaDefaultTexture;break;}O.Textures.Add(V);}return O;
 }();return L;
}
const FKKRuntimeShaderLayout& ItemLayout(){
 static const FKKRuntimeShaderLayout L=[](){FKKRuntimeShaderLayout O;O.Sizes.Append(GForgeForwardCBSize,UE_ARRAY_COUNT(GForgeForwardCBSize));
  for(const auto& U:GForgeForwardUniforms){FKKRuntimeShaderUniform V;V.Name=U.Name;V.CB=U.CB;V.Offset=U.Offset;V.Dim=U.Dim;V.Type=U.Type;V.Rows=U.Rows;V.Default=V.Name.EndsWith(TEXT("_ST"))?FLinearColor(1,1,0,0):FLinearColor(0,0,0,0);
   if(V.Name==TEXT("Epsilon"))V.Default.R=1e-10f;for(const auto& P:GForgeProperties)if(V.Name==P.Name){V.Default=P.Default;V.Gamma=P.Gamma||P.Type==0;break;}V.Default=Convert(V.Default,V.Gamma);O.Uniforms.Add(V);}
  for(const auto& T:GForgeForwardTextures){FKKRuntimeShaderTexture V;V.Name=T.Name;V.Index=T.Index;V.Sampler=T.Sampler;V.Dim=T.Dim;V.Default=TEXT("white");for(const auto& P:GForgeProperties)if(V.Name==P.Name){V.Default=P.ForgeDefaultTexture;break;}O.Textures.Add(V);}return O;
 }();return L;
}
const FKKRuntimeShaderLayout& HairLayout(){
 static const FKKRuntimeShaderLayout L=[](){FKKRuntimeShaderLayout O;O.Sizes.Append(GForgeHairForwardCBSize,UE_ARRAY_COUNT(GForgeHairForwardCBSize));
  for(const auto& U:GForgeHairForwardUniforms){FKKRuntimeShaderUniform V;V.Name=U.Name;V.CB=U.CB;V.Offset=U.Offset;V.Dim=U.Dim;V.Type=U.Type;V.Rows=U.Rows;V.Default=V.Name.EndsWith(TEXT("_ST"))?FLinearColor(1,1,0,0):FLinearColor(0,0,0,0);
   if(V.Name==TEXT("Epsilon"))V.Default.R=1e-10f;for(const auto& P:GForgeHairProperties)if(V.Name==P.Name){V.Default=P.Default;V.Gamma=P.Gamma||P.Type==0;break;}V.Default=Convert(V.Default,V.Gamma);O.Uniforms.Add(V);}
  for(const auto& T:GForgeHairForwardTextures){FKKRuntimeShaderTexture V;V.Name=T.Name;V.Index=T.Index;V.Sampler=T.Sampler;V.Dim=T.Dim;V.Default=TEXT("white");for(const auto& P:GForgeHairProperties)if(V.Name==P.Name){V.Default=P.ForgeHairDefaultTexture;break;}O.Textures.Add(V);}return O;
 }();return L;
}
const FKKRuntimeShaderLayout& HairFrontLayout(){
 static const FKKRuntimeShaderLayout L=[](){FKKRuntimeShaderLayout O;O.Sizes.Append(GForgeHairFrontForwardCBSize,UE_ARRAY_COUNT(GForgeHairFrontForwardCBSize));
  for(const auto& U:GForgeHairFrontForwardUniforms){FKKRuntimeShaderUniform V;V.Name=U.Name;V.CB=U.CB;V.Offset=U.Offset;V.Dim=U.Dim;V.Type=U.Type;V.Rows=U.Rows;V.Default=V.Name.EndsWith(TEXT("_ST"))?FLinearColor(1,1,0,0):FLinearColor(0,0,0,0);
   if(V.Name==TEXT("Epsilon"))V.Default.R=1e-10f;for(const auto& P:GForgeHairFrontProperties)if(V.Name==P.Name){V.Default=P.Default;V.Gamma=P.Gamma||P.Type==0;break;}V.Default=Convert(V.Default,V.Gamma);O.Uniforms.Add(V);}
  for(const auto& T:GForgeHairFrontForwardTextures){FKKRuntimeShaderTexture V;V.Name=T.Name;V.Index=T.Index;V.Sampler=T.Sampler;V.Dim=T.Dim;V.Default=TEXT("white");for(const auto& P:GForgeHairFrontProperties)if(V.Name==P.Name){V.Default=P.ForgeHairFrontDefaultTexture;break;}O.Textures.Add(V);}return O;
 }();return L;
}
const FKKRuntimeShaderLayout& StudioAlphaLayout(){
 static const FKKRuntimeShaderLayout L=[](){FKKRuntimeShaderLayout O;O.Sizes.Append(GForgeStudioAlphaForwardCBSize,UE_ARRAY_COUNT(GForgeStudioAlphaForwardCBSize));
  for(const auto& U:GForgeStudioAlphaForwardUniforms){FKKRuntimeShaderUniform V;V.Name=U.Name;V.CB=U.CB;V.Offset=U.Offset;V.Dim=U.Dim;V.Type=U.Type;V.Rows=U.Rows;V.Default=V.Name.EndsWith(TEXT("_ST"))?FLinearColor(1,1,0,0):FLinearColor(0,0,0,0);
   if(V.Name==TEXT("Epsilon"))V.Default.R=1e-10f;for(const auto& P:GForgeStudioAlphaProperties)if(V.Name==P.Name){V.Default=P.Default;V.Gamma=P.Gamma||P.Type==0;break;}V.Default=Convert(V.Default,V.Gamma);O.Uniforms.Add(V);}
  for(const auto& T:GForgeStudioAlphaForwardTextures){FKKRuntimeShaderTexture V;V.Name=T.Name;V.Index=T.Index;V.Sampler=T.Sampler;V.Dim=T.Dim;V.Default=TEXT("white");for(const auto& P:GForgeStudioAlphaProperties)if(V.Name==P.Name){V.Default=P.ForgeStudioAlphaDefaultTexture;break;}O.Textures.Add(V);}return O;
 }();return L;
}
const FKKRuntimeShaderLayout& EyeWhiteLayout(){
 static const FKKRuntimeShaderLayout L=[](){FKKRuntimeShaderLayout O;O.Sizes.Append(GForgeEyeWhiteForwardCBSize,UE_ARRAY_COUNT(GForgeEyeWhiteForwardCBSize));
  for(const auto& U:GForgeEyeWhiteForwardUniforms){FKKRuntimeShaderUniform V;V.Name=U.Name;V.CB=U.CB;V.Offset=U.Offset;V.Dim=U.Dim;V.Type=U.Type;V.Rows=U.Rows;V.Default=V.Name.EndsWith(TEXT("_ST"))?FLinearColor(1,1,0,0):FLinearColor(0,0,0,0);
   if(V.Name==TEXT("Epsilon"))V.Default.R=1e-10f;for(const auto& P:GForgeEyeWhiteProperties)if(V.Name==P.Name){V.Default=P.Default;V.Gamma=P.Gamma||P.Type==0;break;}V.Default=Convert(V.Default,V.Gamma);O.Uniforms.Add(V);}
  for(const auto& T:GForgeEyeWhiteForwardTextures){FKKRuntimeShaderTexture V;V.Name=T.Name;V.Index=T.Index;V.Sampler=T.Sampler;V.Dim=T.Dim;V.Default=TEXT("white");for(const auto& P:GForgeEyeWhiteProperties)if(V.Name==P.Name){V.Default=P.ForgeEyeWhiteDefaultTexture;break;}O.Textures.Add(V);}return O;
 }();return L;
}
const FKKRuntimeShaderLayout& NoseLayout(){
 static const FKKRuntimeShaderLayout L=[](){FKKRuntimeShaderLayout O;O.Sizes.Append(GForgeNoseForwardCBSize,UE_ARRAY_COUNT(GForgeNoseForwardCBSize));
  for(const auto& U:GForgeNoseForwardUniforms){FKKRuntimeShaderUniform V;V.Name=U.Name;V.CB=U.CB;V.Offset=U.Offset;V.Dim=U.Dim;V.Type=U.Type;V.Rows=U.Rows;V.Default=V.Name.EndsWith(TEXT("_ST"))?FLinearColor(1,1,0,0):FLinearColor(0,0,0,0);
   if(V.Name==TEXT("Epsilon"))V.Default.R=1e-10f;for(const auto& P:GForgeNoseProperties)if(V.Name==P.Name){V.Default=P.Default;V.Gamma=P.Gamma||P.Type==0;break;}V.Default=Convert(V.Default,V.Gamma);O.Uniforms.Add(V);}
  for(const auto& T:GForgeNoseForwardTextures){FKKRuntimeShaderTexture V;V.Name=T.Name;V.Index=T.Index;V.Sampler=T.Sampler;V.Dim=T.Dim;V.Default=TEXT("white");for(const auto& P:GForgeNoseProperties)if(V.Name==P.Name){V.Default=P.ForgeNoseDefaultTexture;break;}O.Textures.Add(V);}return O;
 }();return L;
}
const FKKRuntimeShaderLayout& KoikanoEyeWhiteLayout(){
 static const FKKRuntimeShaderLayout L=[](){FKKRuntimeShaderLayout O;O.Sizes.Append(GKoikanoEyeWhiteForwardCBSize,UE_ARRAY_COUNT(GKoikanoEyeWhiteForwardCBSize));
  for(const auto& U:GKoikanoEyeWhiteForwardUniforms){FKKRuntimeShaderUniform V;V.Name=U.Name;V.CB=U.CB;V.Offset=U.Offset;V.Dim=U.Dim;V.Type=U.Type;V.Rows=U.Rows;V.Default=V.Name.EndsWith(TEXT("_ST"))?FLinearColor(1,1,0,0):FLinearColor(0,0,0,0);
   if(V.Name==TEXT("Epsilon"))V.Default.R=1e-10f;for(const auto& P:GKoikanoEyeWhiteProperties)if(V.Name==P.Name){V.Default=P.Default;V.Gamma=P.Gamma||P.Type==0;break;}V.Default=Convert(V.Default,V.Gamma);O.Uniforms.Add(V);}
  for(const auto& T:GKoikanoEyeWhiteForwardTextures){FKKRuntimeShaderTexture V;V.Name=T.Name;V.Index=T.Index;V.Sampler=T.Sampler;V.Dim=T.Dim;V.Default=TEXT("white");for(const auto& P:GKoikanoEyeWhiteProperties)if(V.Name==P.Name){V.Default=P.KoikanoEyeWhiteDefaultTexture;break;}O.Textures.Add(V);}return O;
 }();return L;
}
const FKKRuntimeShaderLayout& KoikanoEyeLayout(){
 static const FKKRuntimeShaderLayout L=[](){FKKRuntimeShaderLayout O;O.Sizes.Append(GKoikanoEyeForwardCBSize,UE_ARRAY_COUNT(GKoikanoEyeForwardCBSize));
  for(const auto& U:GKoikanoEyeForwardUniforms){FKKRuntimeShaderUniform V;V.Name=U.Name;V.CB=U.CB;V.Offset=U.Offset;V.Dim=U.Dim;V.Type=U.Type;V.Rows=U.Rows;V.Default=V.Name.EndsWith(TEXT("_ST"))?FLinearColor(1,1,0,0):FLinearColor(0,0,0,0);
   if(V.Name==TEXT("Epsilon"))V.Default.R=1e-10f;for(const auto& P:GKoikanoEyeProperties)if(V.Name==P.Name){V.Default=P.Default;V.Gamma=P.Gamma||P.Type==0;break;}V.Default=Convert(V.Default,V.Gamma);O.Uniforms.Add(V);}
  for(const auto& T:GKoikanoEyeForwardTextures){FKKRuntimeShaderTexture V;V.Name=T.Name;V.Index=T.Index;V.Sampler=T.Sampler;V.Dim=T.Dim;V.Default=TEXT("white");for(const auto& P:GKoikanoEyeProperties)if(V.Name==P.Name){V.Default=P.KoikanoEyeDefaultTexture;break;}O.Textures.Add(V);}return O;
 }();return L;
}
const FKKRuntimeShaderLayout& EyeLayout(){
 static const FKKRuntimeShaderLayout L=[](){FKKRuntimeShaderLayout O;O.Sizes.Append(GForgeEyeForwardCBSize,UE_ARRAY_COUNT(GForgeEyeForwardCBSize));
  for(const auto& U:GForgeEyeForwardUniforms){FKKRuntimeShaderUniform V;V.Name=U.Name;V.CB=U.CB;V.Offset=U.Offset;V.Dim=U.Dim;V.Type=U.Type;V.Rows=U.Rows;V.Default=V.Name.EndsWith(TEXT("_ST"))?FLinearColor(1,1,0,0):FLinearColor(0,0,0,0);
   if(V.Name==TEXT("Epsilon"))V.Default.R=1e-10f;for(const auto& P:GForgeEyeProperties)if(V.Name==P.Name){V.Default=P.Default;V.Gamma=P.Gamma||P.Type==0;break;}V.Default=Convert(V.Default,V.Gamma);O.Uniforms.Add(V);}
  for(const auto& T:GForgeEyeForwardTextures){FKKRuntimeShaderTexture V;V.Name=T.Name;V.Index=T.Index;V.Sampler=T.Sampler;V.Dim=T.Dim;V.Default=TEXT("white");for(const auto& P:GForgeEyeProperties)if(V.Name==P.Name){V.Default=P.ForgeEyeDefaultTexture;break;}O.Textures.Add(V);}return O;
 }();return L;
}
const FKKRuntimeShaderLayout* OKRuntimeSharedMaterialLayout(EKKRuntimeMaterialRecipe Recipe){
 if(const auto* L=AdditionalLayout(Recipe))return L;
 if(Recipe==EKKRuntimeMaterialRecipe::ForgeNose)return &NoseLayout();if(Recipe==EKKRuntimeMaterialRecipe::KoikanoEye)return &KoikanoEyeLayout();if(Recipe==EKKRuntimeMaterialRecipe::KoikanoEyeWhite)return &KoikanoEyeWhiteLayout();if(Recipe==EKKRuntimeMaterialRecipe::ForgeEye)return &EyeLayout();if(Recipe==EKKRuntimeMaterialRecipe::ForgeEyeWhite)return &EyeWhiteLayout();if(Recipe==EKKRuntimeMaterialRecipe::ForgeStudioAlpha)return &StudioAlphaLayout();if(Recipe==EKKRuntimeMaterialRecipe::ForgeHair)return &HairLayout();if(Recipe==EKKRuntimeMaterialRecipe::ForgeHairFront)return &HairFrontLayout();if(Recipe==EKKRuntimeMaterialRecipe::ForgeItem)return &ItemLayout();if(Recipe==EKKRuntimeMaterialRecipe::ForgeOpaque)return &OKRuntimeForgeOpaqueLayout();
 if(Recipe==EKKRuntimeMaterialRecipe::ForgeAlphaMasked||Recipe==EKKRuntimeMaterialRecipe::ForgeAlphaTranslucent)return &AlphaLayout();return nullptr;
}
FString OKRuntimeSharedMaterialProgram(EKKRuntimeMaterialRecipe Recipe){
 if(int32(Recipe)>=13)return AdditionalProgram(Recipe);
 return Recipe==EKKRuntimeMaterialRecipe::ForgeNose?GForgeNoseForwardProgram:Recipe==EKKRuntimeMaterialRecipe::KoikanoEye?GKoikanoEyeForwardProgram:Recipe==EKKRuntimeMaterialRecipe::KoikanoEyeWhite?GKoikanoEyeWhiteForwardProgram:Recipe==EKKRuntimeMaterialRecipe::ForgeEye?GForgeEyeForwardProgram:Recipe==EKKRuntimeMaterialRecipe::ForgeEyeWhite?GForgeEyeWhiteForwardProgram:Recipe==EKKRuntimeMaterialRecipe::ForgeStudioAlpha?GForgeStudioAlphaForwardProgram:Recipe==EKKRuntimeMaterialRecipe::ForgeHair?GForgeHairForwardProgram:Recipe==EKKRuntimeMaterialRecipe::ForgeHairFront?GForgeHairFrontForwardProgram:Recipe==EKKRuntimeMaterialRecipe::ForgeItem?GForgeForwardProgram:Recipe==EKKRuntimeMaterialRecipe::ForgeOpaque?GForgeOpaqueForwardProgram:
 (Recipe==EKKRuntimeMaterialRecipe::ForgeAlphaMasked||Recipe==EKKRuntimeMaterialRecipe::ForgeAlphaTranslucent)?GForgeAlphaForwardProgram:FString();
}
EKKRuntimeMaterialRecipe OKRuntimeSharedMaterialRecipe(const FKKRuntimeMaterialPart& P){
 TSharedPtr<FJsonObject> AdditionalRoot;const TSharedPtr<FJsonObject>* AdditionalMat=nullptr;FString AdditionalShader;
 if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),AdditionalRoot)&&AdditionalRoot&&AdditionalRoot->TryGetObjectField(TEXT("material"),AdditionalMat)&&(*AdditionalMat)->TryGetStringField(TEXT("shader"),AdditionalShader)){
  const TArray<TSharedPtr<FJsonValue>>* Keywords=nullptr;TSet<FString> Enabled;if((*AdditionalMat)->TryGetArrayField(TEXT("keywords"),Keywords))for(const auto& K:*Keywords)Enabled.Add(K->AsString());
  if(AdditionalShader==TEXT("Koikano/main_skin"))return EKKRuntimeMaterialRecipe::KoikanoSkin;
  if(AdditionalShader==TEXT("Shader Forge/main_skin"))return EKKRuntimeMaterialRecipe::ForgeSkin;
  if(AdditionalShader==TEXT("Koikano/main_nose"))return EKKRuntimeMaterialRecipe::KoikanoNose;
  if(AdditionalShader==TEXT("Koikano/main_eyeb"))return EKKRuntimeMaterialRecipe::KoikanoBrow;
  if(AdditionalShader==TEXT("Koikano/main_clothes_opaque"))return Enabled.Contains(TEXT("_COLORMASKUSE_COLORON"))?EKKRuntimeMaterialRecipe::KoikanoClothesColor:EKKRuntimeMaterialRecipe::KoikanoClothesOpaque;
  if(AdditionalShader==TEXT("Koikano/main_clothes_item")){const bool Alpha=Enabled.Contains(TEXT("_MULTYCOLORALPHA_ON"));if(P.Blend==TEXT("translucent"))return Alpha?EKKRuntimeMaterialRecipe::KoikanoClothesItemAlphaTranslucent:EKKRuntimeMaterialRecipe::KoikanoClothesItemTranslucent;return Alpha?EKKRuntimeMaterialRecipe::KoikanoClothesItemAlpha:EKKRuntimeMaterialRecipe::KoikanoClothesItem;}
  if(AdditionalShader==TEXT("Koikano/main_clothes_alpha"))return Enabled.Contains(TEXT("_BACKCULLSWITCH_CULLON"))?EKKRuntimeMaterialRecipe::KoikanoClothesAlphaCull:EKKRuntimeMaterialRecipe::KoikanoClothesAlpha;
  if(AdditionalShader==TEXT("Koikano/low_clothes_alpha"))return Enabled.Contains(TEXT("_BACKCULLSWITCH_CULLON"))?EKKRuntimeMaterialRecipe::KoikanoLowAlphaCull:EKKRuntimeMaterialRecipe::KoikanoLowAlpha;
  // IBL requires a separate opaque depth-only component in scene integration.
  if(AdditionalShader==TEXT("IBL_Shader_alpha"))return EKKRuntimeMaterialRecipe::IBLAlpha;
  if(AdditionalShader==TEXT("IBL_Shader"))return EKKRuntimeMaterialRecipe::IBLOpaque;
  if(AdditionalShader==TEXT("KKUSSitem"))return EKKRuntimeMaterialRecipe::KKUSSItem;
  if(AdditionalShader==TEXT("xukmi/MainItemPlus"))return EKKRuntimeMaterialRecipe::XukmiItem;
  if(AdditionalShader==TEXT("Shader Forge/toon_glasses_lod0"))return EKKRuntimeMaterialRecipe::ForgeGlasses;
  // The installed MDK program is the variant without _ALPHATEST_ON: an alpha-tested material (the retro fan's guard and
  // blades) has no parent that cuts it out, so it keeps its own graph (masked by its texture alpha).
  if(AdditionalShader==TEXT("Shader Forge/main_StandardMDK_studio"))return Enabled.Contains(TEXT("_ALPHATEST_ON"))?EKKRuntimeMaterialRecipe::None:EKKRuntimeMaterialRecipe::ForgeMDK;
  // main_item_studio blends but writes depth: every item draws with the depth-writing parent (its P.Blend is always
  // "translucent" for the shader's blend state), except one the host fades (_OKItemAlpha < 1: OK Studio's item alpha),
  // which keeps the blending parent.
  if(AdditionalShader==TEXT("Shader Forge/main_item_studio")){const auto* Faded=P.Values.Find(TEXT("_OKItemAlpha"));return Faded&&Faded->X<1.f?EKKRuntimeMaterialRecipe::ForgeStudio:EKKRuntimeMaterialRecipe::ForgeStudioOpaque;}
  if(AdditionalShader==TEXT("Goo"))return EKKRuntimeMaterialRecipe::Goo;
  if(AdditionalShader==TEXT("Shader Forge/main_item_ditherd"))return EKKRuntimeMaterialRecipe::ForgeDither;
  if(AdditionalShader==TEXT("Shader Forge/main_item_emission"))return EKKRuntimeMaterialRecipe::ForgeEmission;
  if(AdditionalShader==TEXT("KKUSS"))return EKKRuntimeMaterialRecipe::KKUSS;
  if(AdditionalShader==TEXT("Shader Forge/toon_eyew_lod0"))return EKKRuntimeMaterialRecipe::ForgeEyeWhiteLod;
  if(AdditionalShader==TEXT("DA/Chara/Oil Painting"))return EKKRuntimeMaterialRecipe::OilPainting;
  if(AdditionalShader==TEXT("BetterMatcap"))return EKKRuntimeMaterialRecipe::BetterMatcap;
  if((AdditionalShader==TEXT("Koikano/hair_main_sun")||AdditionalShader==TEXT("Koikano/hair_main_sun_front"))){const int32 Variant=int32(Enabled.Contains(TEXT("_GLOSSINSHADOWONOFF_ON")))+2*int32(Enabled.Contains(TEXT("_OLDHAIR_ON")))+4*int32(Enabled.Contains(TEXT("_REVERSECOLOR01_ON")));return EKKRuntimeMaterialRecipe(27+Variant);}
  if(AdditionalShader==TEXT("Koikano/main_clothes_item_glasses")){const int32 Setting=Enabled.Contains(TEXT("_MATCAPSETTING_LINEAR"))?2:Enabled.Contains(TEXT("_MATCAPSETTING_MULTIPLY"))?1:0;const int32 Variant=2*Setting+int32(Enabled.Contains(TEXT("_MATCAP_ON")))+6*int32(Enabled.Contains(TEXT("_CULLOFF_ON")));return EKKRuntimeMaterialRecipe(35+Variant);}

 }
 if(P.ShaderKind==0){TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Mat=nullptr;FString Shader;
  if(P.Blend.Equals(TEXT("translucent"),ESearchCase::IgnoreCase)&&P.Unlit&&P.EyeDraw==1&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Root)&&Root&&Root->TryGetObjectField(TEXT("material"),Mat)&&(*Mat)->TryGetStringField(TEXT("shader"),Shader)&&Shader.Equals(TEXT("Koikano/main_eye"),ESearchCase::IgnoreCase))return EKKRuntimeMaterialRecipe::KoikanoEye;
  if(P.Blend.Equals(TEXT("translucent"),ESearchCase::IgnoreCase)&&P.Unlit&&P.EyeDraw==2&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Root)&&Root&&Root->TryGetObjectField(TEXT("material"),Mat)&&(*Mat)->TryGetStringField(TEXT("shader"),Shader)&&Shader.Equals(TEXT("Shader Forge/toon_eye_lod0"),ESearchCase::IgnoreCase))return EKKRuntimeMaterialRecipe::ForgeEye;
  if(P.Blend.Equals(TEXT("translucent"),ESearchCase::IgnoreCase)&&P.Unlit&&P.EyeDraw==0&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Root)&&Root&&Root->TryGetObjectField(TEXT("material"),Mat)&&(*Mat)->TryGetStringField(TEXT("shader"),Shader)&&Shader.Equals(TEXT("Koikano/main_eyew"),ESearchCase::IgnoreCase))return EKKRuntimeMaterialRecipe::KoikanoEyeWhite;
  if(P.Blend.Equals(TEXT("translucent"),ESearchCase::IgnoreCase)&&P.Unlit&&P.EyeDraw==0&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Root)&&Root&&Root->TryGetObjectField(TEXT("material"),Mat)&&(*Mat)->TryGetStringField(TEXT("shader"),Shader)&&Shader.Equals(TEXT("Shader Forge/toon_nose_lod0"),ESearchCase::IgnoreCase))return EKKRuntimeMaterialRecipe::ForgeNose;
  if(P.Blend.Equals(TEXT("translucent"),ESearchCase::IgnoreCase)&&P.Unlit&&P.EyeDraw==0&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Root)&&Root&&Root->TryGetObjectField(TEXT("material"),Mat)&&(*Mat)->TryGetStringField(TEXT("shader"),Shader)&&Shader.Equals(TEXT("Shader Forge/toon_eyew_lod0"),ESearchCase::IgnoreCase))return EKKRuntimeMaterialRecipe::ForgeEyeWhite;return EKKRuntimeMaterialRecipe::None;
 }
 if(P.ShaderKind==4){TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Mat=nullptr;FString Shader;
  if(P.Blend.Equals(TEXT("translucent"),ESearchCase::IgnoreCase)&&P.Unlit&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Root)&&Root&&Root->TryGetObjectField(TEXT("material"),Mat)&&(*Mat)->TryGetStringField(TEXT("shader"),Shader)&&Shader.Equals(TEXT("Shader Forge/main_item_studio_alpha"),ESearchCase::IgnoreCase))return EKKRuntimeMaterialRecipe::ForgeStudioAlpha;return EKKRuntimeMaterialRecipe::None;
 }
 if(P.ShaderKind==1){TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Mat=nullptr;FString Shader;
  if(!P.Blend.Equals(TEXT("masked"),ESearchCase::IgnoreCase)||!P.Unlit||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Root)||!Root||!Root->TryGetObjectField(TEXT("material"),Mat)||!(*Mat)->TryGetStringField(TEXT("shader"),Shader))return EKKRuntimeMaterialRecipe::None;
  if(Shader.Equals(TEXT("Shader Forge/main_hair"),ESearchCase::IgnoreCase))return EKKRuntimeMaterialRecipe::ForgeHair;
  if(Shader.Equals(TEXT("Shader Forge/main_hair_front"),ESearchCase::IgnoreCase))return EKKRuntimeMaterialRecipe::ForgeHairFront;return EKKRuntimeMaterialRecipe::None;
 }
 if(P.ShaderKind==8)return EKKRuntimeMaterialRecipe::ForgeItem;if(P.ShaderKind==2)return EKKRuntimeMaterialRecipe::ForgeOpaque;if(P.ShaderKind!=11)return EKKRuntimeMaterialRecipe::None;
 TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Mat=nullptr,*Floats=nullptr;double Opaque=0;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Root)||!Root||!Root->TryGetObjectField(TEXT("material"),Mat))return EKKRuntimeMaterialRecipe::None;
 if((*Mat)->TryGetObjectField(TEXT("floats"),Floats)&&(*Floats)->TryGetNumberField(TEXT("_OKNativeAlphaOpaque"),Opaque)&&Opaque==1)return EKKRuntimeMaterialRecipe::ForgeAlphaMasked;
 return EKKRuntimeMaterialRecipe::ForgeAlphaTranslucent;
}
FString OKRuntimeForgeOpaqueProgram(){return GForgeOpaqueForwardProgram;}
bool OKPrepareRuntimeSharedMaterialBinding(const FKKRuntimeMaterialPart& P,const FKKRuntimeMaterialScene& Scene,FKKRuntimeMaterialBinding& Out,FString& Error,bool Outline){
 auto Fail=[&](const TCHAR* Why){Error=Why;return false;};const auto Recipe=OKRuntimeSharedMaterialRecipe(P);const auto* Layout=Outline?OKRuntimeOutlineLayout(Recipe):OKRuntimeSharedMaterialLayout(Recipe);if(!Layout)return Fail(TEXT("Unsupported shared material family"));
 TSharedPtr<FJsonObject> Root;double Schema=0;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(P.SourceMetadataJson),Root)||!Root||!Root->TryGetNumberField(TEXT("schema"),Schema)||Schema!=1)return Fail(TEXT("Shared material source metadata invalid"));
 // Unity property names are case sensitive: hair prefabs carry a stale
 // `_shadowcolor` that main_hair never reads, and FString's default
 // case-insensitive map bound it as _ShadowColor (C024's near-black hair).
 struct FCaseSensitiveKeys:BaseKeyFuncs<TPair<FString,FLinearColor>,FString,false>{static const FString& GetSetKey(const TPair<FString,FLinearColor>& E){return E.Key;}static bool Matches(const FString& A,const FString& B){return A.Equals(B,ESearchCase::CaseSensitive);}static uint32 GetKeyHash(const FString& K){return FCrc::StrCrc32(*K);}};
 TMap<FString,FLinearColor,FDefaultSetAllocator,FCaseSensitiveKeys> Values;const TSharedPtr<FJsonObject>* Mat=nullptr,*Colors=nullptr,*Floats=nullptr;
 if(!Root->TryGetObjectField(TEXT("material"),Mat))return Fail(TEXT("Shared material source properties missing"));
 if(Root->TryGetObjectField(TEXT("material"),Mat)){
  if((*Mat)->TryGetObjectField(TEXT("floats"),Floats))for(const auto& F:(*Floats)->Values){double X;if(!F.Value->TryGetNumber(X)||!FMath::IsFinite(X))return Fail(TEXT("Shared material scalar nonfinite"));Values.Add(FString(F.Key),FLinearColor(X,0,0,0));}
  if((*Mat)->TryGetObjectField(TEXT("colors"),Colors))for(const auto& F:(*Colors)->Values){const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!F.Value->TryGetArray(A)||A->Num()!=4)return Fail(TEXT("Shared material color invalid"));FLinearColor V;for(int32 C=0;C<4;++C){double X;if(!(*A)[C]->TryGetNumber(X)||!FMath::IsFinite(X))return Fail(TEXT("Shared material color nonfinite"));(&V.R)[C]=X;}Values.Add(FString(F.Key),V);}
 }
 // Host overrides (FKKRuntimeMaterialPart::Overrides: OK Studio item colours) replace the source material's values.
 for(const auto& O:P.Overrides){if(!FMath::IsFinite(O.Value.R)||!FMath::IsFinite(O.Value.G)||!FMath::IsFinite(O.Value.B)||!FMath::IsFinite(O.Value.A))return Fail(TEXT("Shared material override nonfinite"));Values.Add(O.Key,O.Value);}
 // Eye streams own native neutral-gaze affine transforms; do not apply
 // their rotation/overlay ST twice in the source pixel program.
 if(Recipe==EKKRuntimeMaterialRecipe::ForgeEye||Recipe==EKKRuntimeMaterialRecipe::KoikanoEye){
  const auto* Prepared=P.Values.Find(TEXT("_OKEyeUVPrepared"));if(!Prepared||Prepared->X!=1)return Fail(TEXT("Runtime iris sampling UV contract missing"));
  const auto* Expression=P.Values.Find(TEXT("_OKEyeExpressionUVPrepared"));if(!Expression||Expression->X!=1)return Fail(TEXT("Runtime expression UV contract missing"));
  if(Recipe==EKKRuntimeMaterialRecipe::KoikanoEye){const auto* Color=P.Values.Find(TEXT("_EyeColor"));if(!Color||Color->ContainsNaN())return Fail(TEXT("Koikano resolved iris color missing/nonfinite"));Values.Add(TEXT("_Color"),FLinearColor(Color->X,Color->Y,Color->Z,Color->W));Values.Add(TEXT("_rotation1"),FLinearColor(.5,0,0,0));Values.Add(TEXT("_rotation2"),FLinearColor(.5,0,0,0));}
  Values.Add(TEXT("_rotation"),FLinearColor(0,0,0,0));Values.Add(TEXT("_overtex1_ST"),FLinearColor(1,1,0,0));Values.Add(TEXT("_overtex2_ST"),FLinearColor(1,1,0,0));
  const TCHAR* Keys[]={TEXT("_MainTex_ST"),TEXT("_EyeLayerColor0"),TEXT("_EyeLayerColor1"),TEXT("_EyeHighlight")};const TCHAR* Targets[]={TEXT("_MainTex_ST"),TEXT("_overcolor1"),TEXT("_overcolor2"),TEXT("_isHighLight")};
  for(int32 I=0;I<4;++I){const auto* V=P.Values.Find(Keys[I]);if(!V||V->ContainsNaN())return Fail(TEXT("Runtime resolved iris values missing/nonfinite"));Values.Add(Targets[I],FLinearColor(V->X,V->Y,V->Z,V->W));}
 }
 // Skin's legacy transport contains card-resolved properties; source metadata
 // is the prefab before character customization. Resolve before color conversion.
 if(Recipe==EKKRuntimeMaterialRecipe::KoikanoSkin){
  const TCHAR* Keys[]={TEXT("_OverColor1"),TEXT("_OverColor2"),TEXT("_SpecularColor"),TEXT("_ShadowExtend"),TEXT("_NormalDetailScale"),TEXT("_FaceNormal"),TEXT("_LineTexOn"),TEXT("_SpecularHeight"),TEXT("_SpecularPower"),TEXT("_SpecularPowerNail"),TEXT("_NoTexSpecular"),TEXT("_NormalDetail_ST")};
  const TCHAR* Targets[]={TEXT("_overcolor1"),TEXT("_overcolor2"),TEXT("_SpecularColor"),TEXT("_ShadowExtend"),TEXT("_DetailNormalMapScale"),TEXT("_FaceNormalG"),TEXT("_linetexon"),TEXT("_SpeclarHeight"),TEXT("_SpecularPower"),TEXT("_SpecularPowerNail"),TEXT("_notusetexspecular"),TEXT("_NormalMapDetail_ST")};
  for(int32 I=0;I<UE_ARRAY_COUNT(Keys);++I)if(const auto* V=P.Values.Find(Keys[I])){if(V->ContainsNaN())return Fail(TEXT("Resolved skin property nonfinite"));Values.Add(Targets[I],FLinearColor(V->X,V->Y,V->Z,V->W));}
 }
 FKKRuntimeMaterialBinding B;B.Scalars.Add(TEXT("__OK_RuntimeMaterialSchema"),1);B.Scalars.Add(TEXT("__OK_RuntimeMaterialFamily"),float(Recipe)+(Outline?128:0));if(Outline)B.Scalars.Add(TEXT("__OK_OutlineApprox"),OKRuntimeOutlineApprox());
 for(const auto& U:Layout->Uniforms){
  if(U.Rows&&U.Name==TEXT("unity_ProbeVolumeWorldToObject")){if(U.Rows!=4)return Fail(TEXT("Probe volume matrix shape unsupported"));for(int32 Row=0;Row<4;++Row){const FString Name=U.Name+FString::Printf(TEXT("_Row%d"),Row);FLinearColor V(0,0,0,0);(&V.R)[Row]=1;if(const auto* Override=Scene.Uniforms.Find(*Name))V=*Override;for(int32 C=0;C<4;++C)if(!FMath::IsFinite((&V.R)[C]))return Fail(TEXT("Probe volume matrix nonfinite"));B.Vectors.Add(*Name,V);B.Scalars.Add(*(Name+TEXT("__A")),V.A);}continue;}
  if(Automatic(U))continue;FLinearColor V=U.Default;
  if(const auto* Authored=Values.Find(U.Name))V=Convert(*Authored,U.Gamma);
  else if(U.Name.EndsWith(TEXT("_ST"))){const TCHAR* Axes[]={TEXT("x"),TEXT("y"),TEXT("z"),TEXT("w")};for(int32 C=0;C<4;++C)if(const auto* X=Values.Find(U.Name+TEXT("_")+Axes[C]))(&V.R)[C]=X->R;}
  if(U.Name==TEXT("_LightColor0"))V=Scene.Light;
  if(U.Name==TEXT("_WorldSpaceLightPos0"))V=FLinearColor(Scene.LightDirectionUnity.X,Scene.LightDirectionUnity.Y,Scene.LightDirectionUnity.Z,0);
  // Portable sessions retain Studio's inverted ambient-shadow alpha. Character
  // maker restores GraphicData.shadowDepth. Koikano/main_skin turns alpha 1
  // into a white indirect, so it keeps that raw depth while cast shadows are
  // off. Other families still use alpha 1 in that mode; their self-shadow is
  // removed with _ShadowColor, and alpha 0 would darken them anyway.
  if(U.Name==TEXT("_ambientshadowG")){V=Scene.Ambient;if(const auto* X=P.Values.Find(TEXT("_ShadowDepth")))V.A=Scene.StudioShadowGlobals?X->X:1-X->X;if(!Scene.EnableShadows&&Recipe!=EKKRuntimeMaterialRecipe::KoikanoSkin)V.A=1;}
  // The card-resolved _ShadowColor (ME overrides, the 0.628 shader default)
  // is what the character renderer uses; the prefab value is only its seed.
  if(U.Name==TEXT("_ShadowColor")&&Recipe!=EKKRuntimeMaterialRecipe::KoikanoSkin)if(const auto* X=P.Values.Find(TEXT("_ShadowColor")))if(!X->ContainsNaN())V=Convert(FLinearColor(X->X,X->Y,X->Z,X->W),U.Gamma);
  if(!Scene.EnableShadows&&U.Name==TEXT("_ShadowColor"))V=FLinearColor::White;
  // The card reader's item outline (KKMainItemPlus, kinds 2/8/9/11) also scales by the material's own _LineWidthS.
  if(U.Name==TEXT("__OK_OutlineWidth")){V.R=1;if(const auto* X=P.Values.Find(TEXT("_LineWidth")))V.R=X->X;if(P.ShaderKind==2||P.ShaderKind==8||P.ShaderKind==9||P.ShaderKind==11)if(const auto* S=Values.Find(TEXT("_LineWidthS")))V.R*=S->R;}
  if(U.Name==TEXT("__OK_OutlineSkinWidth")){V.R=1;if(const auto* X=P.Values.Find(TEXT("_SkinLineWidth")))V.R=X->X;}
  if(U.Name==TEXT("_linewidthG")){if(const auto* X=P.Values.Find(TEXT("_LineWidth")))V.R=X->X;}
  if(U.Name==TEXT("_LineColorG")){float Depth=0;if(const auto* X=P.Values.Find(TEXT("_LineDepth")))Depth=X->X;V=FLinearColor(.5,.5,.5,1-Depth);}
  if(U.Name==TEXT("_KanoVerShift"))if(const auto* X=P.Values.Find(TEXT("_VerticalShift")))V.R=X->X;
  if(U.Name==TEXT("unity_OcclusionMaskSelector"))V=FLinearColor(1,0,0,0);
  if(U.Name==TEXT("unity_WorldTransformParams"))V=FLinearColor(0,0,0,1);if((U.Name==TEXT("unity_SpecCube0_HDR")||U.Name==TEXT("unity_SpecCube1_HDR")))V=FLinearColor(1,1,0,1);
  if(U.Name.StartsWith(TEXT("unity_")))if(const auto* Override=Scene.Uniforms.Find(*U.Name))V=*Override;
  // Host shader globals (FKKRuntimeMaterialScene::Globals) win, converted like an authored value.
  if(const auto* Global=Scene.Globals.Find(*U.Name))V=Convert(*Global,U.Gamma);
  for(int32 C=0;C<4;++C)if(!FMath::IsFinite((&V.R)[C]))return Fail(TEXT("Shared material resolved uniform nonfinite"));
  if(U.Dim==1)B.Scalars.Add(*U.Name,V.R);else{B.Vectors.Add(*U.Name,V);if(U.Dim==4)B.Scalars.Add(*(U.Name+TEXT("__A")),V.A);}
 }
 TSet<FString> Seen;for(const auto& Slot:P.Textures){if(Seen.Contains(Slot.Property)||Slot.Texture<0)return Fail(TEXT("Shared material source slot invalid"));Seen.Add(Slot.Property);for(const auto& T:Layout->Textures)if(T.Name==Slot.Property){if(T.Dim!=2)return Fail(TEXT("Shared material source texture dimension unsupported"));B.Textures.Add(*T.Name,Slot.Texture);}}
 if(Recipe==EKKRuntimeMaterialRecipe::ForgeEye||Recipe==EKKRuntimeMaterialRecipe::KoikanoEye){for(int32 Layer=0;Layer<2;++Layer){const FName Target=Layer?TEXT("_overtex2"):TEXT("_overtex1");B.Textures.Remove(Target);for(const auto& Slot:P.Textures)if(Slot.Property==FString::Printf(TEXT("_EyeLayer%d"),Layer))B.Textures.Add(Target,Slot.Texture);}}
 Out=MoveTemp(B);Error.Reset();return true;
}
UMaterialInstanceDynamic* OKCreateRuntimeSharedMaterial(const FKKRuntimeMaterialPart& P,TConstArrayView<UTexture2D*> Textures,UMaterialInterface* Parent,const FKKRuntimeMaterialScene& Scene,FString& Error,bool Outline){
 check(IsInGameThread());FKKRuntimeMaterialBinding B;if(!OKPrepareRuntimeSharedMaterialBinding(P,Scene,B,Error,Outline))return nullptr;
 auto Fail=[&](const TCHAR* Why)->UMaterialInstanceDynamic*{Error=Why;return nullptr;};if(!Parent)return Fail(TEXT("Shared cooked material parent missing"));
 auto Recipe=OKRuntimeSharedMaterialRecipe(P);float Family=0;
 if(!Parent->GetScalarParameterValue(FMaterialParameterInfo(TEXT("__OK_RuntimeMaterialFamily")),Family))return Fail(TEXT("Shared material parent family differs"));
 // A root baked before this recipe gives its fallback's parent (OKLoadRuntimeSharedMaterialParent): that parent's
 // recipe is checked and named instead (same program and layout).
 for(auto Fallback=OKRuntimeSharedMaterialFallback(Recipe);Family!=float(Recipe)+(Outline?128:0)&&Fallback!=EKKRuntimeMaterialRecipe::None;Fallback=OKRuntimeSharedMaterialFallback(Fallback))
  if(Family==float(Fallback)+(Outline?128:0)){Recipe=Fallback;B.Scalars.Add(TEXT("__OK_RuntimeMaterialFamily"),Family);}
 if(Family!=float(Recipe)+(Outline?128:0))return Fail(TEXT("Shared material parent family differs"));
 const auto* Layout=Outline?OKRuntimeOutlineLayout(Recipe):OKRuntimeSharedMaterialLayout(Recipe);
 float Schema=0;if(!Parent->GetScalarParameterValue(FMaterialParameterInfo(TEXT("__OK_RuntimeMaterialSchema")),Schema)||Schema!=1||Parent->GetBlendMode()!=(Outline?BLEND_Masked:OKRuntimeSharedMaterialBlendMode(Recipe))||Parent->IsTwoSided()!=(Outline?true:OKRuntimeSharedMaterialTwoSided(Recipe)))return Fail(TEXT("Shared material parent schema/render state differs"));
 // Outline parents authored before the installed outline vertex program (StreetDisplay23 and older) have neither its
 // inputs nor __OK_OutlineApprox; they extrude by the approximate width and never read them, so those are skipped.
 const auto VertexOnly=[&](const FString& N){if(!Outline)return false;if(N==TEXT("__OK_OutlineApprox"))return true;const auto* V=OKRuntimeOutlineVertexLayout(Recipe);const auto* S=OutlineLayout(Recipe);
  return V&&S&&(V->Uniforms.ContainsByPredicate([&](const auto& U){return U.Name==N;})||V->Textures.ContainsByPredicate([&](const auto& T){return T.Name==N;}))
   &&!S->Uniforms.ContainsByPredicate([&](const auto& U){return U.Name==N;})&&!S->Textures.ContainsByPredicate([&](const auto& T){return T.Name==N;});};
 for(auto It=B.Scalars.CreateIterator();It;++It){float X;if(!Parent->GetScalarParameterValue(FMaterialParameterInfo(It.Key()),X)){if(VertexOnly(It.Key().ToString())){It.RemoveCurrent();continue;}return Fail(TEXT("Shared parent scalar parameter absent"));}}
 for(auto It=B.Vectors.CreateIterator();It;++It){FLinearColor X;if(!Parent->GetVectorParameterValue(FMaterialParameterInfo(It.Key()),X)){if(VertexOnly(It.Key().ToString())){It.RemoveCurrent();continue;}return Fail(TEXT("Shared parent vector parameter absent"));}}
 for(const auto& T:Layout->Textures){UTexture* X=nullptr;if(!Parent->GetTextureParameterValue(FMaterialParameterInfo(*T.Name),X)||!X){if(VertexOnly(T.Name)){B.Textures.Remove(*T.Name);continue;}return Fail(TEXT("Shared parent texture/default absent"));}if((T.Dim==2&&!Cast<UTexture2D>(X))||(T.Dim==3&&!Cast<UVolumeTexture>(X))||(T.Dim==4&&!Cast<UTextureCube>(X)))return Fail(TEXT("Shared parent texture dimension differs"));}
 for(const auto& T:B.Textures)if(T.Value>=Textures.Num()||!Textures[T.Value])return Fail(TEXT("Shared material prepared texture absent"));
 auto* M=UMaterialInstanceDynamic::Create(Parent,GetTransientPackage());if(!M)return Fail(TEXT("Runtime material instance allocation failed"));
 for(const auto& V:B.Scalars)M->SetScalarParameterValue(V.Key,V.Value);for(const auto& V:B.Vectors)M->SetVectorParameterValue(V.Key,V.Value);for(const auto& T:B.Textures)M->SetTextureParameterValue(T.Key,Textures[T.Value]);
 // OK_OUTLINE_DUMP=1: each outline instance's width inputs (diagnostic).
 static const bool OutlineDump=!FPlatformMisc::GetEnvironmentVariable(TEXT("OK_OUTLINE_DUMP")).IsEmpty();
 if(Outline&&OutlineDump){FString S;for(const TCHAR* N:{TEXT("__OK_OutlineApprox"),TEXT("_linewidthG"),TEXT("_LineWidthS"),TEXT("__OK_OutlineWidth"),TEXT("__OK_OutlineSkinWidth")}){float X=0;S+=FString::Printf(TEXT(" %s=%s"),N,M->GetScalarParameterValue(FMaterialParameterInfo(N),X)?*FString::SanitizeFloat(X):TEXT("-"));}
  UE_LOG(LogTemp,Display,TEXT("OK outline instance %s recipe=%d parent=%s%s"),*P.Name,int32(Recipe),*Parent->GetPathName(),*S);}
 Error.Reset();return M;
}

bool OKPrepareRuntimeForgeOpaqueBinding(const FKKRuntimeMaterialPart& P,const FKKRuntimeMaterialScene& S,FKKRuntimeMaterialBinding& B,FString& E){
 if(P.ShaderKind!=2){E=TEXT("Unsupported opaque material family");return false;}return OKPrepareRuntimeSharedMaterialBinding(P,S,B,E);
}
UMaterialInstanceDynamic* OKCreateRuntimeForgeOpaqueMaterial(const FKKRuntimeMaterialPart& P,TConstArrayView<UTexture2D*> T,UMaterialInterface* M,const FKKRuntimeMaterialScene& S,FString& E){
 if(P.ShaderKind!=2){E=TEXT("Unsupported opaque material family");return nullptr;}return OKCreateRuntimeSharedMaterial(P,T,M,S,E);
}

TConstArrayView<EKKRuntimeMaterialRecipe> OKRuntimeSharedMaterialRecipes(){
 static const TArray<EKKRuntimeMaterialRecipe> Recipes=[](){TArray<EKKRuntimeMaterialRecipe> R;for(int32 I=1;I<=62;++I)R.Add(EKKRuntimeMaterialRecipe(I));return R;}();return MakeArrayView(Recipes);
}
EKKRuntimeMaterialRecipe OKRuntimeSharedMaterialFallback(EKKRuntimeMaterialRecipe R){
 return R==EKKRuntimeMaterialRecipe::ForgeStudioOpaque?EKKRuntimeMaterialRecipe::ForgeStudio:EKKRuntimeMaterialRecipe::None;
}
UMaterialInterface* OKLoadRuntimeSharedMaterialParent(const FString& Root,EKKRuntimeMaterialRecipe R,bool Outline){
 for(EKKRuntimeMaterialRecipe Try=R;Try!=EKKRuntimeMaterialRecipe::None;Try=OKRuntimeSharedMaterialFallback(Try)){
  const FString Base=OKRuntimeSharedMaterialParentName(Try);if(Base.IsEmpty())break;
  const FString Name=Base+(Outline?TEXT("_Outline"):TEXT(""));
  const FString Path=Root/Name+TEXT(".")+Name;
  if(!FPackageName::DoesPackageExist(Root/Name))continue;
  if(auto* Parent=LoadObject<UMaterialInterface>(nullptr,*Path))return Parent;
 }
 return nullptr;
}
FString OKRuntimeSharedMaterialParentName(EKKRuntimeMaterialRecipe R){
 if(int32(R)>=13){static const TCHAR* Names[]={TEXT("M_KoikanoSkin"),TEXT("M_ForgeSkin"),TEXT("M_KoikanoNose"),TEXT("M_KoikanoBrow"),TEXT("M_KoikanoClothesOpaque"),TEXT("M_KoikanoClothesColor"),TEXT("M_KoikanoClothesItem"),TEXT("M_KoikanoClothesItemAlpha"),TEXT("M_KoikanoClothesAlphaCull"),TEXT("M_KoikanoClothesAlpha"),TEXT("M_IBLAlpha"),TEXT("M_KoikanoClothesItemTranslucent"),TEXT("M_KoikanoClothesItemAlphaTranslucent"),TEXT("M_IBLDepth"),TEXT("M_KoikanoHairSun0"),TEXT("M_KoikanoHairSun1"),TEXT("M_KoikanoHairSun2"),TEXT("M_KoikanoHairSun3"),TEXT("M_KoikanoHairSun4"),TEXT("M_KoikanoHairSun5"),TEXT("M_KoikanoHairSun6"),TEXT("M_KoikanoHairSun7"),TEXT("M_KoikanoGlasses0"),TEXT("M_KoikanoGlasses1"),TEXT("M_KoikanoGlasses2"),TEXT("M_KoikanoGlasses3"),TEXT("M_KoikanoGlasses4"),TEXT("M_KoikanoGlasses5"),TEXT("M_KoikanoGlasses6"),TEXT("M_KoikanoGlasses7"),TEXT("M_KoikanoGlasses8"),TEXT("M_KoikanoGlasses9"),TEXT("M_KoikanoGlasses10"),TEXT("M_KoikanoGlasses11"),TEXT("M_IBLOpaque"),TEXT("M_KKUSSItem"),TEXT("M_XukmiItem"),TEXT("M_ForgeGlasses"),TEXT("M_ForgeMDK"),TEXT("M_Goo"),TEXT("M_ForgeDither"),TEXT("M_ForgeEmission"),TEXT("M_KKUSS"),TEXT("M_ForgeEyeWhiteLod"),TEXT("M_OilPainting"),TEXT("M_BetterMatcap"),TEXT("M_KoikanoLowAlphaCull"),TEXT("M_KoikanoLowAlpha"),TEXT("M_ForgeStudio"),TEXT("M_ForgeStudioOpaque")};return int32(R)<=62?FString(Names[int32(R)-13]):FString();}
 switch(R){case EKKRuntimeMaterialRecipe::ForgeOpaque:return TEXT("M_ForgeOpaque");case EKKRuntimeMaterialRecipe::ForgeAlphaMasked:return TEXT("M_ForgeAlphaMasked");case EKKRuntimeMaterialRecipe::ForgeAlphaTranslucent:return TEXT("M_ForgeAlphaTranslucent");case EKKRuntimeMaterialRecipe::ForgeItem:return TEXT("M_ForgeItem");case EKKRuntimeMaterialRecipe::ForgeHair:return TEXT("M_ForgeHair");case EKKRuntimeMaterialRecipe::ForgeHairFront:return TEXT("M_ForgeHairFront");case EKKRuntimeMaterialRecipe::ForgeStudioAlpha:return TEXT("M_ForgeStudioAlpha");case EKKRuntimeMaterialRecipe::ForgeEyeWhite:return TEXT("M_ForgeEyeWhite");case EKKRuntimeMaterialRecipe::ForgeEye:return TEXT("M_ForgeEye");case EKKRuntimeMaterialRecipe::KoikanoEyeWhite:return TEXT("M_KoikanoEyeWhite");case EKKRuntimeMaterialRecipe::KoikanoEye:return TEXT("M_KoikanoEye");case EKKRuntimeMaterialRecipe::ForgeNose:return TEXT("M_ForgeNose");default:return FString();}
}

bool OKRuntimeSharedMaterialTranslucent(EKKRuntimeMaterialRecipe R){return R==EKKRuntimeMaterialRecipe::ForgeStudio||R==EKKRuntimeMaterialRecipe::KoikanoLowAlphaCull||R==EKKRuntimeMaterialRecipe::KoikanoLowAlpha||R==EKKRuntimeMaterialRecipe::ForgeEyeWhiteLod||R==EKKRuntimeMaterialRecipe::Goo||R==EKKRuntimeMaterialRecipe::BetterMatcap||R==EKKRuntimeMaterialRecipe::XukmiItem||R==EKKRuntimeMaterialRecipe::ForgeGlasses||R==EKKRuntimeMaterialRecipe::IBLAlpha||R==EKKRuntimeMaterialRecipe::KoikanoClothesItemTranslucent||R==EKKRuntimeMaterialRecipe::KoikanoClothesItemAlphaTranslucent||R==EKKRuntimeMaterialRecipe::KoikanoNose||R==EKKRuntimeMaterialRecipe::KoikanoBrow||R==EKKRuntimeMaterialRecipe::KoikanoClothesAlphaCull||R==EKKRuntimeMaterialRecipe::KoikanoClothesAlpha||R==EKKRuntimeMaterialRecipe::ForgeAlphaTranslucent||R==EKKRuntimeMaterialRecipe::ForgeStudioAlpha||R==EKKRuntimeMaterialRecipe::ForgeEyeWhite||R==EKKRuntimeMaterialRecipe::ForgeEye||R==EKKRuntimeMaterialRecipe::KoikanoEyeWhite||R==EKKRuntimeMaterialRecipe::KoikanoEye||R==EKKRuntimeMaterialRecipe::ForgeNose;}
bool OKRuntimeSharedMaterialTwoSided(EKKRuntimeMaterialRecipe R){return R!=EKKRuntimeMaterialRecipe::KoikanoLowAlphaCull&&R!=EKKRuntimeMaterialRecipe::Goo&&R!=EKKRuntimeMaterialRecipe::BetterMatcap&&R!=EKKRuntimeMaterialRecipe::ForgeEyeWhiteLod&&R!=EKKRuntimeMaterialRecipe::ForgeGlasses&&R!=EKKRuntimeMaterialRecipe::ForgeMDK&&R!=EKKRuntimeMaterialRecipe::IBLDepth&&R!=EKKRuntimeMaterialRecipe::IBLAlpha&&R!=EKKRuntimeMaterialRecipe::KoikanoSkin&&R!=EKKRuntimeMaterialRecipe::KoikanoNose&&R!=EKKRuntimeMaterialRecipe::KoikanoBrow&&R!=EKKRuntimeMaterialRecipe::KoikanoClothesAlphaCull&&R!=EKKRuntimeMaterialRecipe::ForgeStudioAlpha&&R!=EKKRuntimeMaterialRecipe::ForgeEyeWhite&&R!=EKKRuntimeMaterialRecipe::ForgeEye&&R!=EKKRuntimeMaterialRecipe::KoikanoEyeWhite&&R!=EKKRuntimeMaterialRecipe::KoikanoEye&&R!=EKKRuntimeMaterialRecipe::ForgeNose;}
FString OKRuntimeSharedMaterialVaryings(EKKRuntimeMaterialRecipe R){
 if(R==EKKRuntimeMaterialRecipe::ForgeStudioOpaque)R=EKKRuntimeMaterialRecipe::ForgeStudio;
 if(int32(R)<13||int32(R)>61||R==EKKRuntimeMaterialRecipe::IBLDepth)return {};
 if(R==EKKRuntimeMaterialRecipe::KoikanoClothesItemTranslucent)R=EKKRuntimeMaterialRecipe::KoikanoClothesItem;
 if(R==EKKRuntimeMaterialRecipe::KoikanoClothesItemAlphaTranslucent)R=EKKRuntimeMaterialRecipe::KoikanoClothesItemAlpha;
 const FString Common=TEXT("float3 wp=float3(-Position.x,Position.z,Position.y)*.01;float3 wn=normalize(float3(-Normal.x,Normal.z,Normal.y));float4 v7=0,v8=0,v9=0,v10=0,v11=0,v12=0,v13=0,v14=0,v15=0;float front=asfloat(FaceSign>0?0xffffffffu:0u);v7.x=v8.x=v9.x=front;\n");
 if(R==EKKRuntimeMaterialRecipe::ForgeSkin)return Common+TEXT("v1.zw=float2(SecondaryUV.x,1-SecondaryUV.y);v2=float4(ThirdUV.x,1-ThirdUV.y,FourthUV.x,1-FourthUV.y);v3=float4(wp,1);v4=float4(wn,0);v5=float4(normalize(t),0);v6=float4(normalize(b),0);v7=VertexColor;\n");
 if(R==EKKRuntimeMaterialRecipe::KoikanoNose)return Common+TEXT("v2=float4(wn,0);v3=float4(wp,1);\n");
 if(R==EKKRuntimeMaterialRecipe::IBLAlpha||R==EKKRuntimeMaterialRecipe::IBLOpaque){const int32 CB=AdditionalLayout(R)->Sizes.Num()-1;FString Code=Common;const TCHAR* Dest[]={TEXT("v1.xy"),TEXT("v1.zw"),TEXT("v2.xy"),TEXT("v2.zw"),TEXT("v3.xy"),TEXT("v3.zw")};for(int32 I=0;I<6;++I)Code+=FString::Printf(TEXT("%s=float2(UV.x,1-UV.y)*cb%d[%d].xy+cb%d[%d].zw;\n"),Dest[I],CB,I,CB,I);return Code+TEXT("v4=float4(t.x,b.x,wn.x,wp.x);v5=float4(t.y,b.y,wn.y,wp.y);v6=float4(t.z,b.z,wn.z,wp.z);v7=0;\n");}
 if(int32(R)>=27&&int32(R)<=46){const int32 CB=AdditionalLayout(R)->Sizes.Num()-1;FString Code=Common+TEXT("v2=float4(t.x,b.x,wn.x,wp.x);v3=float4(t.y,b.y,wn.y,wp.y);v4=float4(t.z,b.z,wn.z,wp.z);v5=0;v6=0;\n");Code+=FString::Printf(TEXT("v1.xy=float2(UV.x,1-UV.y)*cb%d[0].xy+cb%d[0].zw;\n"),CB,CB);if(int32(R)<=34)Code+=FString::Printf(TEXT("v1.zw=float2(SecondaryUV.x,1-SecondaryUV.y)*cb%d[1].xy+cb%d[1].zw;\n"),CB,CB);return Code;}
 if(R==EKKRuntimeMaterialRecipe::KKUSSItem){const int32 CB=AdditionalLayout(R)->Sizes.Num()-1;FString Code=Common;const TCHAR* Inputs[]={TEXT("UV"),TEXT("SecondaryUV"),TEXT("ThirdUV"),TEXT("FourthUV")};const TCHAR* Dest[]={TEXT("v1.xy"),TEXT("v1.zw"),TEXT("v2.xy"),TEXT("v2.zw")};for(int32 I=0;I<4;++I)Code+=FString::Printf(TEXT("%s=float2(%s.x,1-%s.y)*cb%d[%d].xy+cb%d[%d].zw;\n"),Dest[I],Inputs[I],Inputs[I],CB,I,CB,I);return Code+TEXT("v3=float4(t.x,b.x,wn.x,wp.x);v4=float4(t.y,b.y,wn.y,wp.y);v5=float4(t.z,b.z,wn.z,wp.z);v6=VertexColor;v7=0;v8=0;v9=0;\n");}
 if(R==EKKRuntimeMaterialRecipe::XukmiItem||R==EKKRuntimeMaterialRecipe::ForgeMDK)return Common+TEXT("v2=float4(wp,1);v3=float4(wn,0);v4=float4(normalize(t),sign(dot(cross(wn,t),b)));v5=float4(normalize(b),0);v6=0;\n");
 if(R==EKKRuntimeMaterialRecipe::Goo||R==EKKRuntimeMaterialRecipe::BetterMatcap){const int32 CB=AdditionalLayout(R)->Sizes.Num()-1;return Common+FString::Printf(TEXT("v1.xy=float2(UV.x,1-UV.y)*cb%d[0].xy+cb%d[0].zw;v2=float4(wn,0);v3=float4(wp,1);v4=0;v5=0;\n"),CB,CB);}
 if(R==EKKRuntimeMaterialRecipe::OilPainting){const int32 CB=AdditionalLayout(R)->Sizes.Num()-1;return Common+FString::Printf(TEXT("v1.xy=float2(UV.x,1-UV.y)*cb%d[0].xy+cb%d[0].zw;\n"),CB,CB);}
 if(R==EKKRuntimeMaterialRecipe::ForgeStudio)return Common+TEXT("v2=float4(wp,1);v3=float4(wn,0);v4=float4(normalize(t),0);v5=float4(normalize(b),0);\n");
 if(R==EKKRuntimeMaterialRecipe::ForgeEmission)return Common+TEXT("v2=float4(wp,1);v3=float4(wn,0);v4=float4(normalize(t),0);v5=float4(normalize(b),0);v6=float4(front,0,0,0);\n");
 if(R==EKKRuntimeMaterialRecipe::ForgeEyeWhiteLod)return Common+TEXT("v2=float4(wp,1);v3=0;\n");
 if(R==EKKRuntimeMaterialRecipe::KKUSS)return Common+TEXT("v1=float4(t.x,b.x,wn.x,wp.x);v2=float4(t.y,b.y,wn.y,wp.y);v3=float4(t.z,b.z,wn.z,wp.z);v4=VertexColor;v6=float4(UV.x,1-UV.y,SecondaryUV.x,1-SecondaryUV.y);v7=float4(ThirdUV.x,1-ThirdUV.y,FourthUV.x,1-FourthUV.y);v8=float4(wp,1);v9=float4(wn,0);v10=float4(normalize(t),0);v11=float4(normalize(b),0);v13=0;\n");
 if(R==EKKRuntimeMaterialRecipe::ForgeDither)return Common+TEXT("float2 ndc=(Parameters.SvPosition.xy-ResolvedView.ViewRectMin.xy)*ResolvedView.ViewSizeAndInvSize.zw*2-1;v2=float4(ndc.x,-ndc.y,0,1);\n");
 if(R==EKKRuntimeMaterialRecipe::ForgeGlasses)return Common+TEXT("v1.zw=float2(SecondaryUV.x,1-SecondaryUV.y);v2=float4(ThirdUV.x,1-ThirdUV.y,0,0);v3=float4(wp,1);v4=float4(wn,0);v5=float4(normalize(t),0);v6=float4(normalize(b),0);v7=0;\n");
 FString Packed=Common+TEXT("v2=float4(t.x,b.x,wn.x,wp.x);v3=float4(t.y,b.y,wn.y,wp.y);v4=float4(t.z,b.z,wn.z,wp.z);v5=0;v6=0;\n");
 if(R==EKKRuntimeMaterialRecipe::KoikanoSkin)Packed=Common+TEXT("v1.zw=float2(SecondaryUV.x,1-SecondaryUV.y);v2=float4(ThirdUV.x,1-ThirdUV.y,FourthUV.x,1-FourthUV.y);v3=float4(t.x,b.x,wn.x,wp.x);v4=float4(t.y,b.y,wn.y,wp.y);v5=float4(t.z,b.z,wn.z,wp.z);v6=VertexColor;v7=0;\n");
 if((int32(R)>=17&&int32(R)<=22)||R==EKKRuntimeMaterialRecipe::KoikanoLowAlphaCull||R==EKKRuntimeMaterialRecipe::KoikanoLowAlpha){const int32 CB=AdditionalLayout(R)->Sizes.Num()-1;Packed+=FString::Printf(TEXT("v1.xy=float2(UV.x,1-UV.y)*cb%d[0].xy+cb%d[0].zw;\n"),CB,CB);if(R!=EKKRuntimeMaterialRecipe::KoikanoClothesItem&&R!=EKKRuntimeMaterialRecipe::KoikanoClothesItemAlpha)Packed+=FString::Printf(TEXT("v1.zw=float2(SecondaryUV.x,1-SecondaryUV.y)*cb%d[1].xy+cb%d[1].zw;\n"),CB,CB);}
 return Packed;
}

EBlendMode OKRuntimeSharedMaterialBlendMode(EKKRuntimeMaterialRecipe R){if(R==EKKRuntimeMaterialRecipe::ForgeStudio||R==EKKRuntimeMaterialRecipe::ForgeEyeWhiteLod||R==EKKRuntimeMaterialRecipe::Goo||R==EKKRuntimeMaterialRecipe::BetterMatcap||R==EKKRuntimeMaterialRecipe::XukmiItem||R==EKKRuntimeMaterialRecipe::ForgeGlasses)return BLEND_AlphaComposite;return OKRuntimeSharedMaterialTranslucent(R)?BLEND_Translucent:BLEND_Masked;}

const FKKRuntimeShaderLayout* OKRuntimeOutlineLayout(EKKRuntimeMaterialRecipe R){
 static TMap<EKKRuntimeMaterialRecipe,FKKRuntimeShaderLayout> Layouts;
 const auto* Source=OutlineLayout(R);if(!Source)return nullptr;
 if(const auto* Found=Layouts.Find(R))return Found;
 auto L=*Source;const int32 CB=L.Sizes.Num();int32 Offset=0;
 for(const TCHAR* N:{TEXT("__OK_OutlineWidth"),TEXT("__OK_OutlineSkinWidth"),TEXT("_DetailMask_ST"),TEXT("_AlphaMask_ST"),TEXT("_MainTex_ST"),TEXT("_texcoord_ST"),TEXT("_texcoord2_ST"),TEXT("_texcoord3_ST"),TEXT("_texcoord4_ST"),TEXT("_LineMask_ST")}){
  if(L.Uniforms.ContainsByPredicate([&](const auto& U){return U.Name==N;}))continue;
  FKKRuntimeShaderUniform U;U.Name=N;U.CB=CB;U.Offset=Offset++*16;U.Dim=U.Name.StartsWith(TEXT("__OK"))?1:4;U.Default=FLinearColor(1,1,0,0);L.Uniforms.Add(U);
 }
 // The installed outline vertex program's material inputs (_linewidthG, _LineWidthS, _alpha_a, ...): parameters of
 // the outline parent like the fragment's, so the hull extrudes exactly as the game's. Engine values (camera,
 // matrices) are supplied by the parent itself.
 if(const auto* Vertex=OutlineVertexLayout(R)){
  for(const auto& V:Vertex->Uniforms){
   if(Automatic(V)||V.Name.StartsWith(TEXT("unity_"))||L.Uniforms.ContainsByPredicate([&](const auto& U){return U.Name==V.Name;}))continue;
   FKKRuntimeShaderUniform U=V;U.CB=CB;U.Offset=Offset++*16;L.Uniforms.Add(U);
  }
  for(const auto& V:Vertex->Textures)if(!L.Textures.ContainsByPredicate([&](const auto& T){return T.Name==V.Name;})){FKKRuntimeShaderTexture T=V;T.Index=0;for(const auto& E:L.Textures)T.Index=FMath::Max(T.Index,E.Index+1);T.Sampler=T.Index;L.Textures.Add(T);}
 }
 if(Offset)L.Sizes.Add(Offset);
 for(const TCHAR* N:{TEXT("_DetailMask"),TEXT("_AlphaMask")})if(!L.Textures.ContainsByPredicate([&](const auto& T){return T.Name==N;})){FKKRuntimeShaderTexture T;T.Name=N;T.Default=TEXT("white");T.Index=0;for(const auto& V:L.Textures)T.Index=FMath::Max(T.Index,V.Index+1);T.Sampler=T.Index;L.Textures.Add(T);}
 return &Layouts.Add(R,MoveTemp(L));
}
FString OKRuntimeOutlineProgram(EKKRuntimeMaterialRecipe R){return OutlineProgram(R);}
const FKKRuntimeShaderLayout* OKRuntimeOutlineVertexLayout(EKKRuntimeMaterialRecipe R){return OutlineVertexLayout(R);}
FString OKRuntimeOutlineVertexProgram(EKKRuntimeMaterialRecipe R){return OutlineVertexProgram(R);}
// ok.OutlineApprox 1: outlines extrude by the earlier hand-written per-family width (and the hair minimum pixel
// width) instead of the installed outline vertex program. Read when a material instance is bound.
static TAutoConsoleVariable<int32> CVarOKOutlineApprox(TEXT("ok.OutlineApprox"),0,
 TEXT("0: outline hull width from the game's outline vertex program (default). 1: the earlier approximate per-family width."));
float OKRuntimeOutlineApprox(){
 static const int32 Env=[]{const FString V=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_OUTLINE_APPROX"));return V.IsEmpty()?-1:FCString::Atoi(*V);}();
 return (Env>=0?Env:CVarOKOutlineApprox.GetValueOnAnyThread())!=0?1.f:0.f;
}
