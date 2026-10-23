#if OK_RUNTIME_SHARED_MATERIAL_TOOL
// The face flattened along the view, as Wuthering Waves draws its toon faces: the face's depth toward the camera is
// pressed toward a plane through the head, so the nose and cheeks do not stand out from the side and the face reads as
// a drawing from every angle. Only the geometry moves; the shading programs are the shared parents' own.
//
// <Parent>_Flat variants of every shared parent (and of its _Outline and _Fg): the same material plus a world position
// offset. The body keeps the plain parents (no WPO, so its shadow and coverage paths stay as they are); the scene draws
// the head parts (face, eyes, brows, nose, tears) with these. Parameters, set every frame per character
// (OKUpdateFaceFlatten): __OK_FaceFlatPivot (the head, world cm), __OK_FaceFlatDir (toward the player camera, the same
// in every pass so the shadow is cast by the face that is drawn), __OK_FaceFlatten (0 off .. 1 a plane).
// Only the part in front of the pivot moves; an outline moves with the surface it outlines.
//
//   UnrealEditor-Cmd RuntimeMaterialBake.uproject -run=GpuPoseAudit -PrepareFlatFaceMaterials -AssetRoot=<Content>
//     -MaterialRoot=/Game/IndependentResources/RuntimeMaterials/<root> -Output=<report.json>
#include "KKRuntimeSharedMaterial.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Json.h"
#include "MaterialShared.h"

bool PrepareFlatFaceMaterials(const FString& AssetRoot,const FString& Root,const FString& Output){
 if(!Root.StartsWith(TEXT("/Game/IndependentResources/RuntimeMaterials/"))||!FPackageName::IsValidLongPackageName(Root))return false;
 FPackageName::RegisterMountPoint(TEXT("/Game/"),FPaths::ConvertRelativePathToFull(AssetRoot)/TEXT(""));
 TArray<TSharedPtr<FJsonValue>> Made,Skipped;TArray<TStrongObjectPtr<UObject>> Keep;
 TArray<FString> Sources;
 for(auto Recipe:OKRuntimeSharedMaterialRecipes()){const FString Base=OKRuntimeSharedMaterialParentName(Recipe);if(Base.IsEmpty())continue;for(const TCHAR* Suffix:{TEXT(""),TEXT("_Outline"),TEXT("_Fg")})Sources.AddUnique(Base+Suffix);}
 Sources.Add(TEXT("M_OKTearLit"));
 for(const FString& Source:Sources){
  const FString Name=Source+TEXT("_Flat"),Path=Root/Name;
  const FString SourceFile=FPackageName::LongPackageNameToFilename(Root/Source,FPackageName::GetAssetPackageExtension());
  if(!IFileManager::Get().FileExists(*SourceFile)){Skipped.Add(MakeShared<FJsonValueString>(Name+TEXT(": no source")));continue;}
  const FString File=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());
  if(IFileManager::Get().FileExists(*File)){Skipped.Add(MakeShared<FJsonValueString>(Name+TEXT(": exists")));continue;}
  auto* Parent=LoadObject<UMaterial>(nullptr,*(Root/Source+TEXT(".")+Source));
  if(!Parent){UE_LOG(LogTemp,Error,TEXT("Flat face source parent missing: %s"),*Source);return false;}
  auto* Mat=DuplicateObject<UMaterial>(Parent,CreatePackage(*Path),*Name);Mat->SetFlags(RF_Public|RF_Standalone);Keep.Emplace(Mat);
  auto* Ed=Mat->GetEditorOnlyData();
  auto Register=[&](UMaterialExpression* E){E->Material=Mat;Mat->GetExpressionCollection().AddExpression(E);return E;};
  auto* Flat=NewObject<UMaterialExpressionCustom>(Mat);Flat->OutputType=CMOT_Float3;Flat->Description=TEXT("OK face flatten");Flat->Inputs.Reset();
  auto Input=[&](const TCHAR* N,UMaterialExpression* E,int32 OutputIndex=0){auto& I=Flat->Inputs.AddDefaulted_GetRef();I.InputName=N;I.Input.Expression=E;I.Input.OutputIndex=OutputIndex;};
  // The offset the parent already has (an outline's hull): the flattening applies to the point it moved to.
  if(Ed->WorldPositionOffset.Expression)Input(TEXT("Hull"),Ed->WorldPositionOffset.Expression,Ed->WorldPositionOffset.OutputIndex);
  else{auto* Zero=NewObject<UMaterialExpressionConstant3Vector>(Mat);Zero->Constant=FLinearColor(0,0,0,0);Input(TEXT("Hull"),Register(Zero));}
  auto* Position=NewObject<UMaterialExpressionWorldPosition>(Mat);Position->WorldPositionShaderOffset=WPT_ExcludeAllShaderOffsets;Input(TEXT("Position"),Register(Position));
  auto Vector=[&](const TCHAR* N){auto* V=NewObject<UMaterialExpressionVectorParameter>(Mat);V->ParameterName=N;V->DefaultValue=FLinearColor(0,0,0,0);return Register(V);};
  Input(TEXT("Pivot"),Vector(TEXT("__OK_FaceFlatPivot")));Input(TEXT("Dir"),Vector(TEXT("__OK_FaceFlatDir")));
  auto* Strength=NewObject<UMaterialExpressionScalarParameter>(Mat);Strength->ParameterName=TEXT("__OK_FaceFlatten");Strength->DefaultValue=0;Input(TEXT("Strength"),Register(Strength));
  Flat->Code=TEXT("float3 p=Position+Hull;float3 v=Dir;float len=dot(v,v);"
   "if(Strength<=0||len<.25)return Hull;v*=rsqrt(len);"
   "float d=dot(p-Pivot,v);return Hull-saturate(Strength)*max(d,0)*v;");
  Register(Flat);
  Ed->WorldPositionOffset.Expression=Flat;Ed->WorldPositionOffset.OutputIndex=0;Ed->WorldPositionOffset.Mask=0;
  Mat->UpdateCachedExpressionData();Mat->PostEditChange();Mat->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
  if(auto* Pending=Mat->GetMaterialResource(GMaxRHIShaderPlatform))Pending->FinishCompilation();
  const FMaterialResource* Resource=Mat->GetMaterialResource(GMaxRHIShaderPlatform);
  if(!Resource||!Resource->GetCompileErrors().IsEmpty()||!Resource->GetGameThreadShaderMap()){if(Resource)for(const FString& E:Resource->GetCompileErrors())UE_LOG(LogTemp,Error,TEXT("%s: %s"),*Name,*E);UE_LOG(LogTemp,Error,TEXT("%s did not compile"),*Name);return false;}
  FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;if(!UPackage::SavePackage(Mat->GetOutermost(),Mat,*File,Args))return false;
  Made.Add(MakeShared<FJsonValueString>(Name));UE_LOG(LogTemp,Display,TEXT("Flat face parent %s"),*File);
 }
 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),true);Report->SetStringField(TEXT("root"),Root);Report->SetArrayField(TEXT("made"),Made);Report->SetArrayField(TEXT("skipped"),Skipped);
 FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));return FFileHelper::SaveStringToFile(Text,*Output);
}
#endif
