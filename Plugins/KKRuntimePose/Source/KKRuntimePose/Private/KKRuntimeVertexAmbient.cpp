#include "KKRuntimeSharedMaterial.h"
#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionPixelNormalWS.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "MaterialShared.h"
#endif

bool OKRuntimeSharedMaterialUsesVertexAmbient(EKKRuntimeMaterialRecipe Recipe)
{
    return Recipe == EKKRuntimeMaterialRecipe::KoikanoSkin
        || (int32(Recipe) >= 17 && int32(Recipe) <= 22)
        || (int32(Recipe) >= 27 && int32(Recipe) <= 34)
        || Recipe == EKKRuntimeMaterialRecipe::KoikanoClothesItemTranslucent
        || Recipe == EKKRuntimeMaterialRecipe::KoikanoClothesItemAlphaTranslucent;
}

UMaterialExpression* OKMakeRuntimeVertexAmbient(UMaterial* Material,const FKKRuntimeMaterialScene& Scene)
{
#if WITH_EDITOR
    auto Register = [&](UMaterialExpression* E) { E->Material=Material; Material->GetExpressionCollection().AddExpression(E); };
    auto* Eval=NewObject<UMaterialExpressionCustom>(Material);
    Eval->OutputType=CMOT_Float3;
    Eval->Description=TEXT("Installed forward vertex ambient L2");
    auto Add=[&](FName Name,UMaterialExpression* E) { Register(E); auto& I=Eval->Inputs.AddDefaulted_GetRef(); I.InputName=Name; I.Input.Expression=E; };
    Add(TEXT("Normal"),NewObject<UMaterialExpressionVertexNormalWS>(Material));
    const TCHAR* Names[]={TEXT("unity_SHBr"),TEXT("unity_SHBg"),TEXT("unity_SHBb"),TEXT("unity_SHC")};
    const TCHAR* Inputs[]={TEXT("SHBr"),TEXT("SHBg"),TEXT("SHBb"),TEXT("SHC")};
    for(int32 I=0;I<4;++I)
    {
        const FLinearColor* Authored=Scene.Uniforms.Find(Names[I]);
        const FLinearColor V=Authored?*Authored:FLinearColor(0,0,0,0);
        auto* RGB=NewObject<UMaterialExpressionVectorParameter>(Material); RGB->ParameterName=Names[I]; RGB->DefaultValue=V; Register(RGB);
        auto* A=NewObject<UMaterialExpressionScalarParameter>(Material); A->ParameterName=FName(FString(Names[I])+TEXT("__A")); A->DefaultValue=V.A; Register(A);
        auto* RGBA=NewObject<UMaterialExpressionAppendVector>(Material); RGBA->A.Expression=RGB; RGBA->B.Expression=A; Add(Inputs[I],RGBA);
    }
    // The source VS normalizes inverse-transpose normals, then evaluates:
    // (x*y,y*z,z*z,z*x) dot SHB + (x*x-y*y) * SHC.
    // VertexInterpolator preserves the source order: interpolate the evaluated
    // term, rather than evaluating a quadratic on interpolated pixel normals.
    Eval->Code=TEXT("precise float3 n=normalize(float3(-Normal.x,Normal.z,Normal.y));\nprecise float4 q=n.xyzz;\nprecise float4 p=q.ywzx*q.xyzw;\nprecise float k=n.x*n.x-n.y*n.y;\nreturn float3(dot(SHBr,p),dot(SHBg,p),dot(SHBb,p))+SHC.xyz*k;");
    Register(Eval);
    auto* Interpolator=NewObject<UMaterialExpressionVertexInterpolator>(Material); Interpolator->Input.Expression=Eval; Register(Interpolator);
    return Interpolator;
#else
    return nullptr;
#endif
}

UMaterialExpression* OKMakeRuntimeProbeAmbient(UMaterial* Material)
{
#if WITH_EDITOR
    FKKRuntimeMaterialScene Empty;
    auto Register=[&](UMaterialExpression* E){E->Material=Material;Material->GetExpressionCollection().AddExpression(E);};
    auto* Eval=NewObject<UMaterialExpressionCustom>(Material); Eval->OutputType=CMOT_Float3;
    Eval->Description=TEXT("Native Standard unlightmapped probe diffuse");
    auto Add=[&](FName Name,UMaterialExpression* E){Register(E);auto& I=Eval->Inputs.AddDefaulted_GetRef();I.InputName=Name;I.Input.Expression=E;};
    {auto& I=Eval->Inputs.AddDefaulted_GetRef();I.InputName=TEXT("L2");I.Input.Expression=OKMakeRuntimeVertexAmbient(Material,Empty);}
    Add(TEXT("Normal"),NewObject<UMaterialExpressionPixelNormalWS>(Material));
    const TCHAR* Names[]={TEXT("unity_SHAr"),TEXT("unity_SHAg"),TEXT("unity_SHAb")};
    const TCHAR* Inputs[]={TEXT("SHAr"),TEXT("SHAg"),TEXT("SHAb")};
    for(int32 I=0;I<3;++I){
        auto* RGB=NewObject<UMaterialExpressionVectorParameter>(Material);RGB->ParameterName=Names[I];RGB->DefaultValue=FLinearColor(0,0,0,0);Register(RGB);
        auto* A=NewObject<UMaterialExpressionScalarParameter>(Material);A->ParameterName=FName(FString(Names[I])+TEXT("__A"));A->DefaultValue=0;Register(A);
        auto* V=NewObject<UMaterialExpressionAppendVector>(Material);V->A.Expression=RGB;V->B.Expression=A;Add(Inputs[I],V);
    }
    Eval->Code=TEXT("float4 n=float4(normalize(float3(-Normal.x,Normal.z,Normal.y)),1);return max(L2+float3(dot(SHAr,n),dot(SHAg,n),dot(SHAb,n)),0);");
    Register(Eval);return Eval;
#else
    return nullptr;
#endif
}

void OKAddUnityTranslucentDepthTest(UMaterial* Material)
{
#if WITH_EDITOR
    if(!Material||!IsTranslucentBlendMode(Material->BlendMode))return;
    auto* Ed=Material->GetEditorOnlyData();if(!Ed)return;
    auto Register=[&](UMaterialExpression* E){E->Material=Material;Material->GetExpressionCollection().AddExpression(E);};
    // Graphs without an opacity (additive, modulate) draw at 1; the test still needs an output to run in.
    if(!Ed->Opacity.Expression){auto* One=NewObject<UMaterialExpressionConstant>(Material);One->R=1;Register(One);Ed->Opacity.Expression=One;}
    auto* Test=NewObject<UMaterialExpressionCustom>(Material);Test->OutputType=CMOT_Float1;
    Test->Description=TEXT("Unity translucent draw order depth test");
    {auto& I=Test->Inputs.AddDefaulted_GetRef();I.InputName=TEXT("Opacity");I.Input=Ed->Opacity;}
    auto Add=[&](FName Name,UMaterialExpression* E,bool R){Register(E);auto& I=Test->Inputs.AddDefaulted_GetRef();I.InputName=Name;I.Input.Expression=E;if(R)I.Input.SetMask(1,1,0,0,0);};
    auto* Depth=NewObject<UMaterialExpressionSceneTexture>(Material);Depth->SceneTextureId=PPI_CustomDepth;Add(TEXT("CustomDepth"),Depth,true);
    auto* Stencil=NewObject<UMaterialExpressionSceneTexture>(Material);Stencil->SceneTextureId=PPI_CustomStencil;Add(TEXT("CustomStencil"),Stencil,true);
    Add(TEXT("Depth"),NewObject<UMaterialExpressionPixelDepth>(Material),false);
    auto* Rank=NewObject<UMaterialExpressionScalarParameter>(Material);Rank->ParameterName=TEXT("__OK_UnityDrawRank");Rank->DefaultValue=0;
    Rank->bUseCustomPrimitiveData=true;Rank->PrimitiveDataIndex=OKUnityDrawRankIndex;Add(TEXT("Rank"),Rank,false);
    // The ranks hold for the view they were made for: a scene capture (mirror, screen) looks from elsewhere and tests nothing.
    auto* Eye=NewObject<UMaterialExpressionVectorParameter>(Material);Eye->ParameterName=TEXT("__OK_UnityDrawRankEye");Eye->DefaultValue=FLinearColor(0,0,0,0);
    Eye->bUseCustomPrimitiveData=true;Eye->PrimitiveDataIndex=OKUnityDrawRankIndex+1;Add(TEXT("Eye"),Eye,false);
    Add(TEXT("Camera"),NewObject<UMaterialExpressionCameraPositionWS>(Material),false);
    const FString Drop=TEXT("if(Rank>.5&&distance(Camera,Eye.xyz)<1&&CustomStencil>.5&&CustomStencil<Rank-.5&&CustomDepth<Depth*.999-.5)clip(-1);\n");
    // The custom depth pass itself reads no scene textures (far depth, stencil 0), so a writer never drops itself there.
    Test->Code=Drop+TEXT("return Opacity;");
    Register(Test);
    Ed->Opacity.Expression=Test;Ed->Opacity.OutputIndex=0;Ed->Opacity.SetMask(0,0,0,0,0);
    // Modulate (and additive) blending reads no opacity, so the compiler drops the test there: the emissive colour also
    // runs it (a second node returning 1 after the same test).
    if(Ed->EmissiveColor.Expression){
     auto* One=NewObject<UMaterialExpressionCustom>(Material);One->OutputType=CMOT_Float1;One->Description=Test->Description;
     for(const FCustomInput& I:Test->Inputs)if(I.InputName!=TEXT("Opacity"))One->Inputs.Add(I);
     One->Code=Drop+TEXT("return 1;");Register(One);
     auto* Times=NewObject<UMaterialExpressionMultiply>(Material);Times->A=Ed->EmissiveColor;Times->B.Expression=One;Register(Times);
     Ed->EmissiveColor.Expression=Times;Ed->EmissiveColor.OutputIndex=0;Ed->EmissiveColor.SetMask(0,0,0,0,0);
    }
    // Unity's ZWrite writes every fragment the program does not discard, whatever its alpha (a moon billboard's faint
    // disk hides the sky drawn after it); UE's translucent custom depth pass keeps only opacity above the clip value.
    Material->AllowTranslucentCustomDepthWrites=true;Material->OpacityMaskClipValue=0;
#endif
}
