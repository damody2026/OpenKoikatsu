#include "KKPreviewActor.h"
#include "KKSkeletalBuilder.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ShaderCompiler.h"
#include "KKMaterialExposure.h"
#include "KKRustExtract.h"
#include "ok_extract.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "UObject/SavePackage.h"
#endif

namespace {
UMaterial* PreviewParents[3]={};
const TCHAR* PreviewNames[]={TEXT("M_PreviewOpaque"),TEXT("M_PreviewMasked"),TEXT("M_PreviewTranslucent")};
bool Ready(UMaterialInterface* Material,UWorld* World){
 if(!Material)return false;
#if WITH_EDITOR
 auto* Resource=Material->GetMaterialResource(GetFeatureLevelShaderPlatform_Checked(World?World->GetFeatureLevel():GMaxRHIFeatureLevel));
 if(Resource&&!Resource->IsGameThreadShaderMapComplete()){
  // UE's on-demand shader compilation does not submit unused parents. A
  // readiness gate must submit its own dependencies before waiting for them;
  // these parents cannot reach the renderer until this gate has succeeded.
  // SubmitCompileJobs deduplicates submissions at the same priority.
  Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::Normal);
  return false;
 }
 return Resource!=nullptr;
#else
 return true;
#endif
}
// Ready() without submitting compile jobs (OK_FIRSTLOAD=C3 submits them itself, a few per frame).
bool ShaderMapComplete(UMaterialInterface* Material,UWorld* World){
 if(!Material)return false;
#if WITH_EDITOR
 auto* Resource=Material->GetMaterialResource(GetFeatureLevelShaderPlatform_Checked(World?World->GetFeatureLevel():GMaxRHIFeatureLevel));
 return Resource&&Resource->IsGameThreadShaderMapComplete();
#else
 return true;
#endif
}
bool SubmitCompile(UMaterialInterface* Material,UWorld* World){
#if WITH_EDITOR
 if(!Material||Material->IsCompiling())return false;
 auto* Resource=Material->GetMaterialResource(GetFeatureLevelShaderPlatform_Checked(World?World->GetFeatureLevel():GMaxRHIFeatureLevel));
 if(!Resource||Resource->IsGameThreadShaderMapComplete())return false;
 Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::Normal);return true;
#else
 return false;
#endif
}
int32 PartIndex(AKKPreviewActor* Actor,UMeshComponent* Component){
 for(const auto& Tag:Component->ComponentTags){const FString Name=Tag.ToString();if(Name.StartsWith(TEXT("KKSessionPart_")))return FCString::Atoi(*Name.Mid(14));}
 for(const auto& Part:Actor->Parts)if(Part.Comp==Component)return Part.SessionIndex;
 for(const auto& Mouth:Actor->MouthSlots)if(Mouth.Comp==Component||Mouth.OutlineComp==Component||Mouth.CpuComp==Component)return Mouth.SessionPartIndex;
 return INDEX_NONE;
}
const FKKLoadedPart* PartFor(AKKPreviewActor* Actor,UMeshComponent* Component){
 const int32 Index=PartIndex(Actor,Component);
 for(const auto& Part:Actor->Parts)if(Part.SessionIndex==Index&&Index!=INDEX_NONE)return &Part;
 return nullptr;
}
UTexture2D* WhiteMask(){
 static UTexture2D* Texture=nullptr;
 if(!Texture){Texture=UTexture2D::CreateTransient(1,1,PF_B8G8R8A8);Texture->SRGB=false;
  auto& Mip=Texture->GetPlatformData()->Mips[0];auto* Pixel=static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));*Pixel=FColor::White;Mip.BulkData.Unlock();Texture->UpdateResource();Texture->AddToRoot();}
 return Texture;
}
}

bool AKKPreviewActor::PreparePreviewMaterialParents(bool Save){
#if !WITH_EDITOR
 // Cooked games use the validated precompiled character shader library.
 // They never compile character shaders at runtime or need preview parents.
 return true;
#else
 bool Complete=true;
 for(int32 Index=0;Index<3;++Index){
  const FString PackageName=TEXT("/Game/IndependentResources/PreviewMaterials/")+FString(PreviewNames[Index]);
  auto*& Material=PreviewParents[Index];
  if(!Material)Material=LoadObject<UMaterial>(nullptr,*(PackageName+TEXT(".")+PreviewNames[Index]),nullptr,LOAD_NoWarn);
#if WITH_EDITOR
  if(!Material){
   auto* Package=CreatePackage(*PackageName);
   Material=NewObject<UMaterial>(Package,PreviewNames[Index],RF_Public|RF_Standalone);
   Material->BlendMode=Index==1?BLEND_Masked:Index==2?BLEND_Translucent:BLEND_Opaque;
   Material->TwoSided=true;Material->SetShadingModel(MSM_Unlit);
   auto* Texture=NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);Texture->ParameterName=TEXT("PreviewTexture");Texture->Texture=WhiteMask();Texture->SamplerType=SAMPLERTYPE_LinearColor;
   // Preview textures have mixed sRGB settings; use color sampling with a
   // stable engine white default, never change the original texture settings.
   Texture->Texture=LoadObject<UTexture2D>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));Texture->SamplerType=SAMPLERTYPE_Color;
   auto* Tint=NewObject<UMaterialExpressionVectorParameter>(Material);Tint->ParameterName=TEXT("PreviewTint");Tint->DefaultValue=FLinearColor::White;
   auto* Multiply=NewObject<UMaterialExpressionMultiply>(Material);Multiply->A.Expression=Texture;Multiply->B.Expression=Tint;
   auto* Alpha=NewObject<UMaterialExpressionMultiply>(Material);Alpha->A.Expression=Texture;Alpha->A.OutputIndex=4;Alpha->B.Expression=Tint;Alpha->B.OutputIndex=4;
   Material->GetExpressionCollection().AddExpression(Texture);Material->GetExpressionCollection().AddExpression(Tint);Material->GetExpressionCollection().AddExpression(Multiply);
   Material->GetExpressionCollection().AddExpression(Alpha);
   auto* Ed=Material->GetEditorOnlyData();Ed->EmissiveColor.Expression=Multiply;
   if(Index==1){Ed->OpacityMask.Expression=Texture;Ed->OpacityMask.OutputIndex=4;}
   if(Index==2)Ed->Opacity.Expression=Alpha;
   Material->UpdateCachedExpressionData();OKEnsureSkeletalMaterialUsage(Material);Material->PostEditChange();
  }
  if(Save&&Material){
   const FString Filename=FPaths::ProjectContentDir()/TEXT("IndependentResources/PreviewMaterials")/(FString(PreviewNames[Index])+TEXT(".uasset"));
   IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
   FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
   Complete=UPackage::SavePackage(Material->GetOutermost(),Material,*Filename,Args)&&Complete;
  }
#endif
  if(Material)Material->AddToRoot();else Complete=false;
 }
 return Complete;
#endif
}

bool AKKPreviewActor::PrewarmPreviewMaterialParents(UWorld* World){
 PreparePreviewMaterialParents();
 bool All=true;
 // Ready() submits the shader compile jobs of a parent whose shader map is not complete yet.
 for(auto* Parent:PreviewParents)All=Ready(Parent,World)&&All;
 return All;
}

bool AKKPreviewActor::BeginProgressiveMaterials(){
#if !WITH_EDITOR
 return true;
#else
 if(!PendingPreviewMaterials.IsEmpty())return true;
 const double ProgressiveStart=FPlatformTime::Seconds();int32 Submitted=0;
 // OK_FIRSTLOAD=C3: submitting every final material's compile jobs here held the first display ~0.6-0.7 s; show
 // the stand-ins now and submit a few per frame (TickProgressiveMaterials). The final look arrives as before.
 const bool Defer=FirstLoadOption(TEXT("C3"));
 auto IsReady=[&](UMaterialInterface* M){return Defer?ShaderMapComplete(M,GetWorld()):Ready(M,GetWorld());};
 DeferredCompileSubmits.Reset();ProgressiveMaterialsBegin=ProgressiveStart;
 PreparePreviewMaterialParents();
 for(auto* Parent:PreviewParents)if(!Ready(Parent,GetWorld()))return false;
 TInlineComponentArray<UMeshComponent*> Components;GetComponents(Components);
 for(auto* Component:Components)for(int32 Slot=0;Slot<Component->GetNumMaterials();++Slot){
  auto* Final=Component->GetMaterial(Slot);if(!Final||IsReady(Final))continue;
  const EBlendMode Blend=Final->GetBlendMode();const int32 Kind=Blend==BLEND_Masked?1:IsTranslucentBlendMode(Blend)?2:0;
  // Never expose an opaque stand-in for masked/translucent geometry.
  if(!Ready(PreviewParents[Kind],GetWorld()))return false;
  if(Defer){DeferredCompileSubmits.AddUnique(Final);continue;}
#if WITH_EDITOR
  if(!Final->IsCompiling())if(auto* Resource=Final->GetMaterialResource(GetFeatureLevelShaderPlatform_Checked(GetWorld()?GetWorld()->GetFeatureLevel():GMaxRHIFeatureLevel))){Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::Normal);++Submitted;}
#endif
 }
 const double SubmitDone=FPlatformTime::Seconds();
 for(auto* Component:Components)for(int32 Slot=0;Slot<Component->GetNumMaterials();++Slot){
  auto* Final=Component->GetMaterial(Slot);if(!Final||IsReady(Final))continue;
  const EBlendMode Blend=Final->GetBlendMode();const int32 Kind=Blend==BLEND_Masked?1:IsTranslucentBlendMode(Blend)?2:0;
  auto* Preview=UMaterialInstanceDynamic::Create(PreviewParents[Kind],this);
  if(const auto* Part=PartFor(this,Component)){
   if(Part->Texture)Preview->SetTextureParameterValue(TEXT("PreviewTexture"),Part->Texture);
   Preview->SetVectorParameterValue(TEXT("PreviewTint"),Part->OverColor);
  }
  // Extruded/reversed outline geometry must not become a solid shell while
  // waiting. Keep the original winding and make only its stand-in invisible.
  const auto* Part=PartFor(this,Component);
  const bool ProceduralOutline=Part&&Part->Comp==Component&&Slot>0&&Final!=Part->OverMat;
  if(Component->ComponentHasTag(TEXT("KKPreviewOutline"))||ProceduralOutline){
   Preview=UMaterialInstanceDynamic::Create(PreviewParents[2],this);
   Preview->SetVectorParameterValue(TEXT("PreviewTint"),FLinearColor(0,0,0,0));
  }
  FKKPreviewMaterialSlot Entry;Entry.Component=Component;Entry.Slot=Slot;Entry.FinalMaterial=Final;Entry.PreviewMaterial=Preview;
  PendingPreviewMaterials.Add(Entry);Component->SetMaterial(Slot,Preview);
 }
 UE_LOG(LogTemp,Display,TEXT("OK progressive material preview pending=%d submit_ms=%.1f standin_ms=%.1f submitted=%d"),PendingPreviewMaterials.Num(),
  (SubmitDone-ProgressiveStart)*1000.,(FPlatformTime::Seconds()-SubmitDone)*1000.,Submitted);
 return true;
#endif
}

bool AKKPreviewActor::TickProgressiveMaterials(){
 // OK_FIRSTLOAD=C3: the deferred compile submissions, ~3 ms of them per frame (at least one).
 const bool Defer=FirstLoadOption(TEXT("C3"));
 if(!DeferredCompileSubmits.IsEmpty()){
  const double SubmitDeadline=FPlatformTime::Seconds()+.003;
  while(!DeferredCompileSubmits.IsEmpty()){
   auto Material=DeferredCompileSubmits.Pop(EAllowShrinking::No);
   SubmitCompile(Material.Get(),GetWorld());
   if(FPlatformTime::Seconds()>=SubmitDeadline)break;
  }
 }
 const double Deadline=FPlatformTime::Seconds()+.002;int32 Replaced=0;
 for(int32 Index=PendingPreviewMaterials.Num()-1;Index>=0;--Index){
  auto& Entry=PendingPreviewMaterials[Index];
  if(!IsValid(Entry.Component)){PendingPreviewMaterials.RemoveAtSwap(Index);continue;}
  if(!(Defer?ShaderMapComplete(Entry.FinalMaterial,GetWorld()):Ready(Entry.FinalMaterial,GetWorld())))continue;
  // A later appearance change owns its slot; never restore a stale material.
  if(Entry.Component->GetMaterial(Entry.Slot)==Entry.PreviewMaterial)Entry.Component->SetMaterial(Entry.Slot,Entry.FinalMaterial);
  PendingPreviewMaterials.RemoveAtSwap(Index);
  if(++Replaced>=4||FPlatformTime::Seconds()>=Deadline)break;
 }
 if(Replaced)UE_LOG(LogTemp,Display,TEXT("OK progressive material replace count=%d pending=%d"),Replaced,PendingPreviewMaterials.Num());
 // Overrides went to the pending final materials already; once all are in place, apply again in case a final
 // material was swapped in some other way (C3 defers the compile of finals made after the overrides).
 if(Replaced&&PendingPreviewMaterials.IsEmpty()&&!MaterialOverrides.IsEmpty())ApplyMaterialOverrides();
 // When the last final material is in place: the time from the stand-ins to the final look.
 if(Replaced&&PendingPreviewMaterials.IsEmpty()&&ProgressiveMaterialsBegin>0){UE_LOG(LogTemp,Display,TEXT("OK progressive material final_ms=%.1f"),(FPlatformTime::Seconds()-ProgressiveMaterialsBegin)*1000.);ProgressiveMaterialsBegin=0;}
 return PendingPreviewMaterials.IsEmpty();
}

bool AKKPreviewActor::SetClothingHidden(bool Hidden){
 if(Hidden==bClothingHidden)return true;
 if(!Hidden){
  for(UMeshComponent* Component:HiddenClothingComponents)if(IsValid(Component))Component->SetVisibility(true,false);
  for(int32 Index=0;Index<ClothingMaskMaterials.Num();++Index)if(IsValid(ClothingMaskMaterials[Index]))ClothingMaskMaterials[Index]->SetTextureParameterValue(TEXT("_AlphaMask"),ClothingOriginalMasks[Index]);
  HiddenClothingComponents.Reset();ClothingMaskMaterials.Reset();ClothingOriginalMasks.Reset();bClothingHidden=false;return true;
 }
 TInlineComponentArray<UMeshComponent*> Components;GetComponents(Components);
 for(auto* Component:Components){
  const auto* Part=PartFor(this,Component);if(!Part)continue;
  if(Part->Role.StartsWith(TEXT("clothes"))){if(Component->IsVisible()){HiddenClothingComponents.Add(Component);Component->SetVisibility(false,false);}continue;}
  if(Part->Role!=TEXT("body"))continue;
  for(int32 Slot=0;Slot<Component->GetNumMaterials();++Slot){
   UMaterialInterface* Material=Component->GetMaterial(Slot);FKKPreviewMaterialSlot* Pending=nullptr;
   for(auto& Entry:PendingPreviewMaterials)if(Entry.Component==Component&&Entry.Slot==Slot){Pending=&Entry;Material=Entry.FinalMaterial;break;}
   UTexture* Mask=nullptr;if(!Material||!Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("_AlphaMask")),Mask)||!Mask)continue;
   auto* Dynamic=Cast<UMaterialInstanceDynamic>(Material);if(!Dynamic)Dynamic=UMaterialInstanceDynamic::Create(Material,this);
   if(Pending)Pending->FinalMaterial=Dynamic;else Component->SetMaterial(Slot,Dynamic);
   if(!ClothingMaskMaterials.Contains(Dynamic)){ClothingMaskMaterials.Add(Dynamic);ClothingOriginalMasks.Add(Mask);Dynamic->SetTextureParameterValue(TEXT("_AlphaMask"),WhiteMask());}
  }
 }
 bClothingHidden=true;UE_LOG(LogTemp,Display,TEXT("OK clothing visibility hidden=%d components=%d masks=%d"),Hidden,HiddenClothingComponents.Num(),ClothingMaskMaterials.Num());return true;
}

namespace {
bool OverrideMatches(const FOKMaterialOverride& O,int32 SessionIndex,const FString& Role,const FString& Material,const FString& SourceProperty){
 return (O.Slot==INDEX_NONE||O.Slot==SessionIndex)&&(O.Role.IsEmpty()||O.Role.Equals(Role,ESearchCase::IgnoreCase))
  &&(O.Material.IsEmpty()||O.Material.Equals(Material,ESearchCase::IgnoreCase))&&(O.SourceProperty.IsEmpty()||O.SourceProperty==SourceProperty);
}
bool HasParameter(UMaterialInterface* M,const FOKMaterialOverride& O){
 const FMaterialParameterInfo Info(O.Parameter);
 if(O.Kind==EOKMaterialOverrideKind::Texture){UTexture* T=nullptr;return M->GetTextureParameterValue(Info,T);}
 if(O.Kind==EOKMaterialOverrideKind::Scalar){float S=0;return M->GetScalarParameterValue(Info,S);}
 FLinearColor V;return M->GetVectorParameterValue(Info,V);
}
FString Describe(const FOKMaterialOverride& O){
 return FString::Printf(TEXT("%s role=%s source=%s slot=%d material=%s"),*O.Parameter.ToString(),*O.Role,*O.SourceProperty,O.Slot,*O.Material);
}
}

TSet<FString> AKKPreviewActor::MaterialOverrideNames(int32 SessionIndex,const FString& PartRole,const FString& Material,const FString& SourceProperty) const{
 TSet<FString> Names;
 for(const auto& O:MaterialOverrides)if(!O.Parameter.IsNone()&&OverrideMatches(O,SessionIndex,PartRole,Material,SourceProperty))Names.Add(O.Parameter.ToString());
 return Names;
}

int32 AKKPreviewActor::SetMaterialOverrides(const TArray<FOKMaterialOverride>& Overrides,TArray<FString>* OutUnapplied){
 // Put back what the previous list changed, so a removed override leaves the part as it was built.
 for(int32 Index=MaterialOverrideOriginals.Num()-1;Index>=0;--Index){
  const auto& R=MaterialOverrideOriginals[Index];auto* M=R.Material.Get();if(!IsValid(M))continue;
  if(R.Kind==EOKMaterialOverrideKind::Texture)M->SetTextureParameterValue(R.Parameter,R.Texture);
  else if(R.Kind==EOKMaterialOverrideKind::Scalar)M->SetScalarParameterValue(R.Parameter,R.Scalar);
  else M->SetVectorParameterValue(R.Parameter,R.Vector);
 }
 MaterialOverrideOriginals.Reset();
 MaterialOverrides=Overrides;
 return ApplyMaterialOverrides(OutUnapplied);
}

int32 AKKPreviewActor::ApplyMaterialOverrides(TArray<FString>* OutUnapplied){
 if(OutUnapplied)OutUnapplied->Reset();
 if(MaterialOverrides.IsEmpty())return 0;
 TArray<bool> Applied;Applied.SetNumZeroed(MaterialOverrides.Num());int32 Writes=0;
 TMap<int32,TPair<FString,FString>> Identity; // session part -> material name, source_property
 TInlineComponentArray<UMeshComponent*> Components;GetComponents(Components);
 for(auto* Component:Components){
  const int32 Index=PartIndex(this,Component);if(Index==INDEX_NONE)continue;
  const auto* Part=PartFor(this,Component);
  FString PartRole=Part?Part->Role:FString();
  if(PartRole.IsEmpty()){OkMeshView View{};if(Session.PartAt(Index,View)&&View.role)PartRole=UTF8_TO_TCHAR(View.role);}
  if(!Identity.Contains(Index)){FString Material,Source;OKPartMaterialIdentity(Session,Index,Material,Source);Identity.Add(Index,{Material,Source});}
  const auto& Id=Identity[Index];
  for(int32 Slot=0;Slot<Component->GetNumMaterials();++Slot){
   // A slot still showing its progressive stand-in gets the override on the final material it will swap to.
   FKKPreviewMaterialSlot* Pending=nullptr;UMaterialInterface* Material=Component->GetMaterial(Slot);
   for(auto& Entry:PendingPreviewMaterials)if(Entry.Component==Component&&Entry.Slot==Slot){Pending=&Entry;Material=Entry.FinalMaterial;break;}
   if(!Material)continue;
   UMaterialInstanceDynamic* Dynamic=nullptr;
   for(int32 O=0;O<MaterialOverrides.Num();++O){
    const auto& Override=MaterialOverrides[O];
    if(Override.Parameter.IsNone()||!OverrideMatches(Override,Index,PartRole,Id.Key,Id.Value)||!HasParameter(Material,Override))continue;
    // Shared (deduplicated) materials are never edited: each part slot writes its own MID.
    if(!Dynamic){
     Dynamic=Cast<UMaterialInstanceDynamic>(Material);
     if(!Dynamic){Dynamic=UMaterialInstanceDynamic::Create(Material,this);if(Pending)Pending->FinalMaterial=Dynamic;else Component->SetMaterial(Slot,Dynamic);}
    }
    const FMaterialParameterInfo Info(Override.Parameter);
    const bool Known=MaterialOverrideOriginals.ContainsByPredicate([&](const FOKMaterialOverrideOriginal& R){return R.Material==Dynamic&&R.Parameter==Override.Parameter;});
    FOKMaterialOverrideOriginal Original;Original.Material=Dynamic;Original.Parameter=Override.Parameter;
    Original.Kind=Override.Kind==EOKMaterialOverrideKind::Color?EOKMaterialOverrideKind::Vector:Override.Kind;
    if(Override.Kind==EOKMaterialOverrideKind::Texture){
     UTexture* Before=nullptr;Dynamic->GetTextureParameterValue(Info,Before);Original.Texture=Before;
     Dynamic->SetTextureParameterValue(Override.Parameter,Override.Texture);
    }else if(Override.Kind==EOKMaterialOverrideKind::Scalar){
     Dynamic->GetScalarParameterValue(Info,Original.Scalar);
     Dynamic->SetScalarParameterValue(Override.Parameter,Override.Scalar);
    }else{
     Dynamic->GetVectorParameterValue(Info,Original.Vector);
     FLinearColor V=Override.Vector;
     if(Override.Kind==EOKMaterialOverrideKind::Color)for(float* C:{&V.R,&V.G,&V.B})*C=*C<=.04045f?*C/12.92f:FMath::Pow((*C+.055f)/1.055f,2.4f);
     Dynamic->SetVectorParameterValue(Override.Parameter,V);
     // Cooked shared parents also read a float4 uniform's alpha from <name>__A.
     const FName Alpha(*(Override.Parameter.ToString()+TEXT("__A")));float A=0;
     if(Dynamic->GetScalarParameterValue(FMaterialParameterInfo(Alpha),A)){
      if(!MaterialOverrideOriginals.ContainsByPredicate([&](const FOKMaterialOverrideOriginal& R){return R.Material==Dynamic&&R.Parameter==Alpha;})){
       FOKMaterialOverrideOriginal AlphaOriginal;AlphaOriginal.Material=Dynamic;AlphaOriginal.Parameter=Alpha;AlphaOriginal.Kind=EOKMaterialOverrideKind::Scalar;AlphaOriginal.Scalar=A;MaterialOverrideOriginals.Add(AlphaOriginal);
      }
      Dynamic->SetScalarParameterValue(Alpha,V.A);
     }
    }
    if(!Known)MaterialOverrideOriginals.Add(Original);
    Applied[O]=true;++Writes;
   }
  }
 }
 TArray<FString> Missing;for(int32 O=0;O<MaterialOverrides.Num();++O)if(!Applied[O])Missing.Add(Describe(MaterialOverrides[O]));
 UE_LOG(LogTemp,Display,TEXT("OK material overrides=%d writes=%d unapplied=%d%s%s"),MaterialOverrides.Num(),Writes,Missing.Num(),Missing.Num()?TEXT(" first="):TEXT(""),Missing.Num()?*Missing[0]:TEXT(""));
 if(OutUnapplied)*OutUnapplied=MoveTemp(Missing);
 return Writes;
}

#if WITH_EDITOR
static FAutoConsoleCommand SavePreviewParents(TEXT("ok.SavePreviewMaterials"),TEXT("Author the reusable preview parents without changing character materials"),FConsoleCommandDelegate::CreateLambda([](){
 const bool Saved=AKKPreviewActor::PreparePreviewMaterialParents(true);
 if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
 UE_LOG(LogTemp,Display,TEXT("OK preview parents saved=%d"),Saved);
 FPlatformMisc::RequestExitWithStatus(false,Saved?0:1);
}));
#endif
