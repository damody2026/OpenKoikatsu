#if OK_RUNTIME_SHARED_MATERIAL_TOOL
// The heroines' tears (cf_O_namida_S/M/L) drawn lit: the shared parents are unlit programs, so a tear could only be
// as bright as its own colour times the scene light. This parent is Default Lit and translucent with per-pixel
// surface lighting, so the engine's specular puts the wet highlight on it. Colour and alpha stay the tear's own texture
// (no tint); TearVisible shows or hides it (the scene's tears level), TearOpacity / TearSpecular / TearRoughness tune it.
//
//   UnrealEditor-Cmd RuntimeMaterialBake.uproject -run=GpuPoseAudit -PrepareTearMaterial -AssetRoot=<Content>
//     -MaterialPath=/Game/IndependentResources/RuntimeMaterials/<root>/M_OKTearLit -Output=<report.json>
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Engine/Texture2D.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Json.h"
#include "MaterialShared.h"

bool PrepareTearMaterial(const FString& AssetRoot,const FString& MaterialPath,const FString& Output){
 if(!MaterialPath.StartsWith(TEXT("/Game/IndependentResources/RuntimeMaterials/"))||!FPackageName::IsValidLongPackageName(MaterialPath))return false;
 FPackageName::RegisterMountPoint(TEXT("/Game/"),FPaths::ConvertRelativePathToFull(AssetRoot)/TEXT(""));
 const FString File=FPackageName::LongPackageNameToFilename(MaterialPath,FPackageName::GetAssetPackageExtension());
 if(IFileManager::Get().FileExists(*File)){UE_LOG(LogTemp,Error,TEXT("Tear material already exists: %s"),*File);return false;}
 const FString Name=FPackageName::GetShortName(MaterialPath);
 auto* Mat=NewObject<UMaterial>(CreatePackage(*MaterialPath),*Name,RF_Public|RF_Standalone);TStrongObjectPtr<UMaterial> Keep(Mat);
 Mat->MaterialDomain=MD_Surface;Mat->BlendMode=BLEND_Translucent;Mat->SetShadingModel(MSM_DefaultLit);Mat->TwoSided=true;
 // Per-pixel surface lighting: translucency with the engine's specular (the volume modes are diffuse only).
 Mat->TranslucencyLightingMode=TLM_SurfacePerPixelLighting;
 for(auto Usage:{MATUSAGE_SkeletalMesh,MATUSAGE_StaticMesh,MATUSAGE_MorphTargets,MATUSAGE_InstancedSkinnedMesh})Mat->SetUsageByFlag(Usage,true);
 auto Register=[&](UMaterialExpression* E){E->Material=Mat;Mat->GetExpressionCollection().AddExpression(E);return E;};
 auto Scalar=[&](const TCHAR* N,float V){auto* E=NewObject<UMaterialExpressionScalarParameter>(Mat);E->ParameterName=N;E->DefaultValue=V;Register(E);return E;};
 // The tear's texture with its source tiling, read as the runtime's other parents read a Unity texture (V flipped).
 auto* Tex=NewObject<UMaterialExpressionTextureObjectParameter>(Mat);Tex->ParameterName=TEXT("TearTex");
 Tex->Texture=LoadObject<UTexture2D>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));Tex->SamplerType=SAMPLERTYPE_Color;Register(Tex);
 auto* ST=NewObject<UMaterialExpressionVectorParameter>(Mat);ST->ParameterName=TEXT("TearTex_ST");ST->DefaultValue=FLinearColor(1,1,0,0);Register(ST);
 auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(Mat);Register(UV);
 auto* Sample=NewObject<UMaterialExpressionCustom>(Mat);Sample->OutputType=CMOT_Float4;Sample->Description=TEXT("OK tear sample");
 Sample->Inputs.Reset();
 auto Input=[&](const TCHAR* N,UMaterialExpression* E,int32 Output=0){auto& I=Sample->Inputs.AddDefaulted_GetRef();I.InputName=N;I.Input.Expression=E;I.Input.OutputIndex=Output;};
 // The vector parameter's RGBA output (5): its first output is RGB only, and the tiling needs zw.
 Input(TEXT("UV"),UV);Input(TEXT("Tex"),Tex);Input(TEXT("ST"),ST,5);
 Sample->Code=TEXT("float2 uv=float2(UV.x,1-UV.y)*ST.xy+ST.zw;uv.y=1-uv.y;return Texture2DSample(Tex,TexSampler,uv);");Register(Sample);
 auto* Rgb=NewObject<UMaterialExpressionComponentMask>(Mat);Rgb->Input.Expression=Sample;Rgb->R=Rgb->G=Rgb->B=true;Rgb->A=false;Register(Rgb);
 auto* Alpha=NewObject<UMaterialExpressionComponentMask>(Mat);Alpha->Input.Expression=Sample;Alpha->R=Alpha->G=Alpha->B=false;Alpha->A=true;Register(Alpha);
 auto Multiply=[&](UMaterialExpression* A,UMaterialExpression* B){auto* E=NewObject<UMaterialExpressionMultiply>(Mat);E->A.Expression=A;E->B.Expression=B;Register(E);return E;};
 auto* Opacity=Multiply(Multiply(Alpha,Scalar(TEXT("TearOpacity"),1.f)),Scalar(TEXT("TearVisible"),0.f));
 auto* Ed=Mat->GetEditorOnlyData();
 Ed->BaseColor.Expression=Rgb;Ed->Opacity.Expression=Opacity;
 Ed->Specular.Expression=Scalar(TEXT("TearSpecular"),1.f);Ed->Roughness.Expression=Scalar(TEXT("TearRoughness"),.08f);Ed->Metallic.Expression=Scalar(TEXT("TearMetallic"),0.f);
 Mat->UpdateCachedExpressionData();Mat->PostEditChange();Mat->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
 // A graph error would only show at cook time (the game then draws the default material): stop here instead.
 if(auto* Pending=Mat->GetMaterialResource(GMaxRHIShaderPlatform))Pending->FinishCompilation();
 const FMaterialResource* Resource=Mat->GetMaterialResource(GMaxRHIShaderPlatform);
 if(!Resource||!Resource->GetCompileErrors().IsEmpty()||!Resource->GetGameThreadShaderMap()){if(Resource)for(const FString& E:Resource->GetCompileErrors())UE_LOG(LogTemp,Error,TEXT("M_OKTearLit: %s"),*E);UE_LOG(LogTemp,Error,TEXT("M_OKTearLit did not compile"));return false;}
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
 FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(Mat->GetOutermost(),Mat,*File,Args))return false;
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),true);Report->SetStringField(TEXT("material"),MaterialPath);Report->SetStringField(TEXT("file"),File);
 FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));return FFileHelper::SaveStringToFile(Text,*Output);
}
#endif
