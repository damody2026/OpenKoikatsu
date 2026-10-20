#include "KKTripoPBR.h"
#include "Materials/Material.h"
#include "Engine/Texture2D.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Components/DirectionalLightComponent.h"
#include "GameFramework/Actor.h"
#endif

UMaterial* OKMakeTripoPBRMaterial(UTexture2D* Base,UTexture2D* Normal,UTexture2D* Roughness,UTexture2D* Metallic)
{
#if WITH_EDITOR
 if(!Base||!Normal||!Roughness||!Metallic)return nullptr;
 UMaterial* M=NewObject<UMaterial>(GetTransientPackage(),NAME_None,RF_Transient);
 M->bIsPreviewMaterial=true;M->MaterialDomain=MD_Surface;M->BlendMode=BLEND_Opaque;
 M->SetShadingModel(MSM_DefaultLit);M->TwoSided=true;M->bTangentSpaceNormal=true;
 auto Sample=[&](UTexture2D* T){auto* S=NewObject<UMaterialExpressionTextureSample>(M);S->Material=M;S->Texture=T;S->ConstCoordinate=0;S->SamplerType=T->SRGB?SAMPLERTYPE_Color:SAMPLERTYPE_LinearColor;M->GetExpressionCollection().AddExpression(S);return S;};
 auto* Ed=M->GetEditorOnlyData();Ed->BaseColor.Expression=Sample(Base);
 Ed->Roughness.Expression=Sample(Roughness);Ed->Roughness.OutputIndex=1;
 Ed->Metallic.Expression=Sample(Metallic);Ed->Metallic.OutputIndex=1;
 auto* N=NewObject<UMaterialExpressionCustom>(M);N->Material=M;N->OutputType=CMOT_Float3;
 N->Code=TEXT("return normalize(NormalRGB.rgb * 2.0 - 1.0);");
 auto& I=N->Inputs.AddDefaulted_GetRef();I.InputName=TEXT("NormalRGB");I.Input.Expression=Sample(Normal);
 M->GetExpressionCollection().AddExpression(N);Ed->Normal.Expression=N;
 M->SetUsageByFlag(MATUSAGE_SkeletalMesh,true);M->SetUsageByFlag(MATUSAGE_StaticMesh,true);M->SetUsageByFlag(MATUSAGE_MorphTargets,true);
 M->UpdateCachedExpressionData();M->PreEditChange(nullptr);M->PostEditChange();
 return M;
#else
 // A packaged runtime needs a separately cooked common PBR parent.
 return nullptr;
#endif
}

void OKEnsureTripoPBRPreviewLights(AActor* Owner)
{
#if WITH_EDITOR
 if(!Owner||Owner->FindComponentByTag<UDirectionalLightComponent>(TEXT("OKTripoPBRLight")))return;
 const FRotator Rotations[]={FRotator(-45,-90,0),FRotator(-25,-160,0),FRotator(-30,80,0)};
 const float Strengths[]={4.f,1.5f,1.f};
 for(int32 I=0;I<3;++I){auto* L=NewObject<UDirectionalLightComponent>(Owner,NAME_None,RF_Transient);Owner->AddInstanceComponent(L);
  L->ComponentTags.Add(TEXT("OKTripoPBRLight"));L->SetMobility(EComponentMobility::Movable);L->SetIntensity(Strengths[I]);L->SetLightColor(FLinearColor::White);
  L->LightingChannels.bChannel0=false;L->LightingChannels.bChannel1=true;L->LightingChannels.bChannel2=false;L->CastShadows=I==0;
  L->RegisterComponent();L->SetWorldRotation(Rotations[I]);}
#endif
}
