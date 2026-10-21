#include "KKRuntimeSharedMaterial.h"
#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionAppendVector.h"
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
        const FLinearColor V=Scene.Uniforms.FindRef(Names[I]);
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
