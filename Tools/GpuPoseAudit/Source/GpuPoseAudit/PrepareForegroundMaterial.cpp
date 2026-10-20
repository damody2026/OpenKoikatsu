#if OK_RUNTIME_SHARED_MATERIAL_TOOL
// Eyes and eyebrows drawn through the front hair, as Koikatsu does (ChaControl.SetForegroundEyesAndEyebrow).
// There the eye / brow shaders (toon_eyew_lod0, toon_eye_lod0, toon_textureanimation) write stencil 2 and the front
// hair shaders (main_hair_front, Koikano hair_main_sun_front) skip pixels with stencil 2; the card's foreground
// setting only moves the eyes / brows to a render queue before the front hair. Everything else still covers them.
//
// UE's masked front hair cannot read a stencil, so the test turns around: the scene draws each heroine's front hair a
// second time into custom depth / stencil only (stencil = her id), and these <Parent>_Fg variants of the translucent
// shared parents draw with the depth test off and test depth themselves: a pixel shows when nothing is in front of it
// (the depth test it replaced) or when what is in front is her own front hair (custom depth equals scene depth and the
// stencil is hers) and the surface faces the camera (the original's back-face cull), and nothing else of hers is in
// front of it: there the eyes depth-test against her face before they mark the stencil, so her face, head and other
// hair hide them from the side. The scene captures her body without the front hair (OKCrowdRuntimeScene OccluderGpu)
// into __OK_ForegroundOcclusion; __OK_ForegroundOcclusionOn is 0 where no capture is bound.
//
//   UnrealEditor-Cmd RuntimeMaterialBake.uproject -run=GpuPoseAudit -PrepareForegroundMaterials -AssetRoot=<Content>
//     -MaterialRoot=/Game/IndependentResources/RuntimeMaterials/<root> -Output=<report.json>
#include "KKRuntimeSharedMaterial.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionSceneDepth.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Engine/Texture2D.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Json.h"
#include "MaterialShared.h"

bool PrepareForegroundMaterials(const FString& AssetRoot,const FString& Root,const FString& Output){
 if(!Root.StartsWith(TEXT("/Game/IndependentResources/RuntimeMaterials/"))||!FPackageName::IsValidLongPackageName(Root))return false;
 FPackageName::RegisterMountPoint(TEXT("/Game/"),FPaths::ConvertRelativePathToFull(AssetRoot)/TEXT(""));
 TArray<TSharedPtr<FJsonValue>> Made,Skipped;TArray<TStrongObjectPtr<UObject>> Keep;
 for(auto Recipe:OKRuntimeSharedMaterialRecipes()){
  const FString Source=OKRuntimeSharedMaterialParentName(Recipe),Name=Source+TEXT("_Fg"),Path=Root/Name;
  const FString File=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());
  // Kept when it already tests her own occlusion; an older variant is made again.
  if(IFileManager::Get().FileExists(*File)){
   auto* Existing=LoadObject<UMaterial>(nullptr,*(Path+TEXT(".")+Name));float On=0;
   if(Existing&&Existing->GetScalarParameterDefaultValue(FHashedMaterialParameterInfo(TEXT("__OK_ForegroundOcclusionOn")),On)){Skipped.Add(MakeShared<FJsonValueString>(Name+TEXT(": exists")));continue;}
   if(Existing){Existing->ClearFlags(RF_Public|RF_Standalone);Existing->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);}
  }
  auto* Parent=LoadObject<UMaterial>(nullptr,*(Root/Source+TEXT(".")+Source));
  if(!Parent){UE_LOG(LogTemp,Error,TEXT("Foreground source parent missing: %s"),*Source);return false;}
  // Only translucent parents can drop the depth test; the eye / brow recipes all are.
  if(!IsTranslucentBlendMode(Parent->BlendMode)){Skipped.Add(MakeShared<FJsonValueString>(Name+TEXT(": not translucent")));continue;}
  auto* Mat=DuplicateObject<UMaterial>(Parent,CreatePackage(*Path),*Name);Mat->SetFlags(RF_Public|RF_Standalone);Keep.Emplace(Mat);
  auto* Ed=Mat->GetEditorOnlyData();UMaterialExpression* Program=Ed->EmissiveColor.Expression;
  if(!Program||Ed->Opacity.Expression!=Program){UE_LOG(LogTemp,Error,TEXT("%s: emissive / opacity are not one program"),*Source);return false;}
  auto Register=[&](UMaterialExpression* E){E->Material=Mat;Mat->GetExpressionCollection().AddExpression(E);return E;};
  auto* Test=NewObject<UMaterialExpressionCustom>(Mat);Test->OutputType=CMOT_Float4;Test->Description=TEXT("OK foreground eyes / eyebrows");Test->Inputs.Reset();
  auto Input=[&](const TCHAR* N,UMaterialExpression* E){auto& I=Test->Inputs.AddDefaulted_GetRef();I.InputName=N;I.Input.Expression=Register(E);};
  auto& Color=Test->Inputs.AddDefaulted_GetRef();Color.InputName=TEXT("Color");Color.Input.Expression=Program;
  auto Scene=[&](ESceneTextureId Id){auto* E=NewObject<UMaterialExpressionSceneTexture>(Mat);E->SceneTextureId=Id;return E;};
  Input(TEXT("Custom"),Scene(PPI_CustomDepth));Input(TEXT("Stencil"),Scene(PPI_CustomStencil));
  Input(TEXT("Depth"),NewObject<UMaterialExpressionSceneDepth>(Mat));Input(TEXT("Pixel"),NewObject<UMaterialExpressionPixelDepth>(Mat));
  Input(TEXT("Normal"),NewObject<UMaterialExpressionVertexNormalWS>(Mat));Input(TEXT("Camera"),NewObject<UMaterialExpressionCameraVectorWS>(Mat));
  auto* Id=NewObject<UMaterialExpressionScalarParameter>(Mat);Id->ParameterName=TEXT("__OK_ForegroundStencil");Id->DefaultValue=0;Input(TEXT("Id"),Id);
  auto* Occ=NewObject<UMaterialExpressionTextureObjectParameter>(Mat);Occ->ParameterName=TEXT("__OK_ForegroundOcclusion");
  Occ->Texture=LoadObject<UTexture2D>(nullptr,TEXT("/Engine/EngineResources/Black.Black"));Occ->SamplerType=SAMPLERTYPE_LinearColor;Input(TEXT("Occ"),Occ);
  auto* On=NewObject<UMaterialExpressionScalarParameter>(Mat);On->ParameterName=TEXT("__OK_ForegroundOcclusionOn");On->DefaultValue=0;Input(TEXT("On"),On);
  Input(TEXT("Screen"),NewObject<UMaterialExpressionScreenPosition>(Mat));
  const bool Premultiplied=Mat->BlendMode==BLEND_AlphaComposite;
  // Depths in cm. Scene depth vs custom depth: the same mesh, pose and material drew both, so only precision differs.
  // Occ: the capture's scene depth (cm) of her body without the front hair, from the same camera.
  Test->Code=FString(TEXT("float open=Pixel<=Depth.x+0.05;"
   "float own=On<0.5||Pixel<=Texture2DSampleLevel(Occ,OccSampler,Screen.xy,0).r+max(0.3,Pixel*0.002);"
   "float hair=Id>0.5&&abs(Stencil.x-Id)<0.5&&abs(Custom.x-Depth.x)<=max(0.5,Depth.x*0.002)&&dot(normalize(Normal),Camera)>0&&own;"
   "float shown=max(open,hair);"))+(Premultiplied?TEXT("return Color*shown;"):TEXT("return float4(Color.rgb,Color.a*shown);"));
  Register(Test);
  Ed->EmissiveColor.Expression=Test;Ed->Opacity.Expression=Test;if(Ed->OpacityMask.Expression==Program)Ed->OpacityMask.Expression=Test;
  Mat->bDisableDepthTest=true;
  Mat->UpdateCachedExpressionData();Mat->PostEditChange();Mat->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
  if(auto* Pending=Mat->GetMaterialResource(GMaxRHIShaderPlatform))Pending->FinishCompilation();
  const FMaterialResource* Resource=Mat->GetMaterialResource(GMaxRHIShaderPlatform);
  if(!Resource||!Resource->GetCompileErrors().IsEmpty()||!Resource->GetGameThreadShaderMap()){if(Resource)for(const FString& E:Resource->GetCompileErrors())UE_LOG(LogTemp,Error,TEXT("%s: %s"),*Name,*E);UE_LOG(LogTemp,Error,TEXT("%s did not compile"),*Name);return false;}
  FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(Mat->GetOutermost(),Mat,*File,Args))return false;
  Made.Add(MakeShared<FJsonValueString>(Name));UE_LOG(LogTemp,Display,TEXT("Foreground parent %s"),*File);
 }
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),true);Report->SetStringField(TEXT("root"),Root);Report->SetArrayField(TEXT("made"),Made);Report->SetArrayField(TEXT("skipped"),Skipped);
 FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));return FFileHelper::SaveStringToFile(Text,*Output);
}
#endif
