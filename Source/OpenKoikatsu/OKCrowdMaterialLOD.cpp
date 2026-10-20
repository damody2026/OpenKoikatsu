#include "OKCrowdMaterialLOD.h"
#if WITH_EDITOR
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2DArray.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "MaterialBakingStructures.h"
#include "IMaterialBakingModule.h"
#include "Modules/ModuleManager.h"
#include "SkeletalMeshAttributes.h"
#include "AssetCompilingManager.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Misc/PackageName.h"

bool OKBuildCrowdMaterialLOD(USkeletalMesh* Mesh,const FString& Root,TArray<TObjectPtr<UObject>>& Assets){
 const FMeshDescription* Input=Mesh->GetMeshDescription(0);if(!Input)return false;
 FMeshDescription Desc=*Input;FSkeletalMeshAttributes A(Desc);A.GetVertexInstanceUVs().SetNumChannels(FMath::Max(2,A.GetVertexInstanceUVs().GetNumChannels()));
 const auto OriginalMaterials=Mesh->GetMaterials();const int32 Layers=OriginalMaterials.Num();if(Layers<1||Layers>256)return false;
 // Each authored material becomes one 256x256 slice. UV0 remains unchanged,
 // UV1 stores a constant layer per section, including wrapping UV islands.
 TArray64<uint8> Pixels;Pixels.SetNumZeroed(int64(256)*256*4*Layers);
 auto& Baker=FModuleManager::LoadModuleChecked<IMaterialBakingModule>(TEXT("MaterialBaking"));Baker.SetEmissiveHDR(false);Baker.SetLinearBake(false);
 TArray<EBlendMode> Modes;TMap<EBlendMode,FPolygonGroupID> Groups;TMap<FVertexInstanceID,int32> InstanceLayers;
 for(int32 Layer=0;Layer<Layers;++Layer){auto* Material=OriginalMaterials[Layer].MaterialInterface.Get();if(!Material)return false;
  const EBlendMode Blend=Material->GetBlendMode();const bool Translucent=Blend!=BLEND_Opaque&&Blend!=BLEND_Masked;Modes.Add(Translucent?BLEND_Translucent:BLEND_Masked);
  const EMaterialProperty ColorProperty=Material->GetShadingModels().HasShadingModel(MSM_Unlit)?MP_EmissiveColor:MP_BaseColor;
  FMaterialData Settings;Settings.Material=Material;Settings.PropertySizes.Add(ColorProperty,FIntPoint(256));Settings.bPerformShrinking=false;Settings.bPerformBorderSmear=false;Settings.BackgroundColor=FColor::Black;
  const EMaterialProperty AlphaProperty=Translucent?MP_Opacity:MP_OpacityMask;if(Blend!=BLEND_Opaque)Settings.PropertySizes.Add(AlphaProperty,FIntPoint(256));
  FMeshData Quad;TArray<FMaterialData*> Materials{&Settings};TArray<FMeshData*> Meshes{&Quad};TArray<FBakeOutput> Output;Baker.BakeMaterials(Materials,Meshes,Output);
  if(Output.Num()!=1)return false;const auto* Colors=Output[0].PropertyData.Find(ColorProperty);const auto* Alpha=Output[0].PropertyData.Find(AlphaProperty);if(!Colors||Colors->Num()!=256*256||(Alpha&&Alpha->Num()!=256*256))return false;
  // This is an inexpensive distant material: bake its view-dependent toon
  // result once, retaining color and cutout but no dynamic highlights/outlines.
  for(int32 Pixel=0;Pixel<256*256;++Pixel){FColor Color=(*Colors)[Pixel];Color.A=Alpha?(*Alpha)[Pixel].R:255;FMemory::Memcpy(Pixels.GetData()+(int64(Layer)*256*256+Pixel)*4,&Color,4);}
 }
 const FString ArrayPath=Root/TEXT("Atlas")/Mesh->GetName()/TEXT("Layers");if(FPackageName::DoesPackageExist(ArrayPath))return false;
 auto* Array=NewObject<UTexture2DArray>(CreatePackage(*ArrayPath),TEXT("Layers"),RF_Public|RF_Standalone);Assets.Add(Array);Array->Source.Init(256,256,Layers,1,TSF_BGRA8,Pixels.GetData());Array->SRGB=true;Array->MipGenSettings=TMGS_SimpleAverage;Array->CompressionSettings=TC_Default;Array->NeverStream=false;Array->PostEditChange();
 Mesh->GetMaterials().Reset();TMap<EBlendMode,int32> Slots;
 for(EBlendMode Mode:{BLEND_Masked,BLEND_Translucent})if(Modes.Contains(Mode)){
  const FString Name=Mode==BLEND_Masked?TEXT("Cutout"):TEXT("Translucent"),Path=Root/TEXT("Atlas")/Mesh->GetName()/Name;if(FPackageName::DoesPackageExist(Path))return false;
  auto* Material=NewObject<UMaterial>(CreatePackage(*Path),*Name,RF_Public|RF_Standalone);Assets.Add(Material);Material->BlendMode=Mode;Material->TwoSided=true;Material->SetShadingModel(MSM_Unlit);Material->SetUsageByFlag(MATUSAGE_SkeletalMesh,true);Material->SetUsageByFlag(MATUSAGE_MorphTargets,true);Material->OpacityMaskClipValue=.5f;
  auto* Sample=NewObject<UMaterialExpressionCustom>(Material);Sample->OutputType=CMOT_Float4;Sample->Code=TEXT("return Images.Sample(ImagesSampler,float3(frac(UV),Layer.x));");
  auto Add=[&](const TCHAR* InputName,UMaterialExpression* Expression){Expression->Material=Material;Material->GetExpressionCollection().AddExpression(Expression);auto& In=Sample->Inputs.AddDefaulted_GetRef();In.InputName=InputName;In.Input.Expression=Expression;};
  auto* Texture=NewObject<UMaterialExpressionTextureObject>(Material);Texture->Texture=Array;Texture->SamplerType=SAMPLERTYPE_Color;Add(TEXT("Images"),Texture);
  auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(Material);Add(TEXT("UV"),UV);auto* Layer=NewObject<UMaterialExpressionTextureCoordinate>(Material);Layer->CoordinateIndex=1;Add(TEXT("Layer"),Layer);
  Sample->Material=Material;Material->GetExpressionCollection().AddExpression(Sample);auto* Ed=Material->GetEditorOnlyData();Ed->EmissiveColor.Expression=Sample;Ed->EmissiveColor.Mask=1;Ed->EmissiveColor.MaskR=Ed->EmissiveColor.MaskG=Ed->EmissiveColor.MaskB=1;
  auto& Opacity=Mode==BLEND_Masked?Ed->OpacityMask:Ed->Opacity;Opacity.Expression=Sample;Opacity.Mask=1;Opacity.MaskA=1;Material->UpdateCachedExpressionData();Material->PostEditChange();
  const FName Slot(*Name);Slots.Add(Mode,Mesh->GetMaterials().Num());Mesh->GetMaterials().Add(FSkeletalMaterial(Material,true,false,Slot,Slot));const auto Group=Desc.CreatePolygonGroup();A.GetPolygonGroupMaterialSlotNames()[Group]=Slot;Groups.Add(Mode,Group);
 }
 TArray<FPolygonID> Polygons;for(auto Polygon:Desc.Polygons().GetElementIDs())Polygons.Add(Polygon);
 for(auto Polygon:Polygons){const auto Group=Desc.GetPolygonPolygonGroup(Polygon);const FName Name=A.GetPolygonGroupMaterialSlotNames()[Group];const int32 Layer=OriginalMaterials.IndexOfByPredicate([&](const auto& Slot){return Slot.MaterialSlotName==Name||Slot.ImportedMaterialSlotName==Name;});if(!Modes.IsValidIndex(Layer))return false;
  for(auto Instance:Desc.GetPolygonVertexInstances(Polygon)){if(const auto* Existing=InstanceLayers.Find(Instance);Existing&&*Existing!=Layer)return false;InstanceLayers.Add(Instance,Layer);A.GetVertexInstanceUVs().Set(Instance,1,FVector2f(float(Layer),0));}
  Desc.SetPolygonPolygonGroup(Polygon,Groups[Modes[Layer]]);
 }
 TArray<FPolygonGroupID> EmptyGroups;for(auto Group:Desc.PolygonGroups().GetElementIDs())if(Desc.GetPolygonGroupPolygons(Group).IsEmpty())EmptyGroups.Add(Group);for(auto Group:EmptyGroups)Desc.DeletePolygonGroup(Group);
 // Skeletal import uses polygon-group indices as material indices. Removing
 // old groups leaves sparse IDs (e.g. 69,70) that select the fallback material.
 FElementIDRemappings Remappings;Desc.Compact(Remappings);Mesh->GetLODInfo(0)->LODMaterialMap.Reset();
 Mesh->CreateMeshDescription(0,MoveTemp(Desc));Mesh->CommitMeshDescription(0);Mesh->Build();Mesh->PostEditChange();FAssetCompilingManager::Get().FinishAllCompilation();
 for(const auto& Section:Mesh->GetResourceForRendering()->LODRenderData[0].RenderSections)if(!Mesh->GetMaterials().IsValidIndex(Section.MaterialIndex)){UE_LOG(LogTemp,Error,TEXT("Crowd material LOD has an invalid section material index=%d"),Section.MaterialIndex);return false;}
 UE_LOG(LogTemp,Display,TEXT("Crowd material LOD baked layers=%d resolution=256 slots=%d sections=%d"),Layers,Mesh->GetMaterials().Num(),Mesh->GetResourceForRendering()->LODRenderData[0].RenderSections.Num());return true;
}
#endif
