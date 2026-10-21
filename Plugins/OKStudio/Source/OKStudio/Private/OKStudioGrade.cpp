#include "OKStudioPost.h"
#include "OKStudioCatalog.h"
#include "OKSceneDocument.h"
#include "Engine/Scene.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace {
const TCHAR* GradeAsset=TEXT("/Game/OKStudio/Post/SourceGrade1/M_SourceGrade32");
TStrongObjectPtr<UMaterial> GradeParent;
struct FGradeInstance { TWeakObjectPtr<UTexture> Lut; float Strength=0, Exposure=0; TWeakObjectPtr<UMaterialInstanceDynamic> Material; };
TArray<FGradeInstance> GradeInstances;
UMaterial* BuildGrade(UObject* Outer=GetTransientPackage(),FName Name=NAME_None,EObjectFlags Flags=RF_Transient) {
#if WITH_EDITORONLY_DATA
 auto* Mat=NewObject<UMaterial>(Outer,Name,Flags);Mat->MaterialDomain=MD_PostProcess;Mat->BlendableLocation=BL_SceneColorAfterDOF;
 Mat->BlendablePriority=100;Mat->bIsBlendable=false;Mat->bDisablePreExposureScale=true;
 auto Add=[Mat](UMaterialExpression* E){E->Material=Mat;Mat->GetExpressionCollection().AddExpression(E);return E;};
 auto* Code=NewObject<UMaterialExpressionCustom>(Mat);Code->OutputType=CMOT_Float3;
 Code->Code=TEXT(R"GRADE(
float3 c=max(Scene.rgb*View.OneOverPreExposure*exp2(Exposure),0.0);
float3 g=lerp(12.92*c,1.055*pow(c,1.0/2.4)-.055,step(.0031308,c));
float3 q=saturate(g)*31.0;
float b=floor(q.b);float2 uv=float2((q.r+b*32.0+.5)/1024.0,(q.g+.5)/32.0);
float3 lo=Texture2DSampleLevel(Lut,LutSampler,uv,0).rgb;
uv.x+=(min(b+1,31.0)-b)/32.0;
float3 hi=Texture2DSampleLevel(Lut,LutSampler,uv,0).rgb;
float3 mapped=lerp(g,lerp(lo,hi,frac(q.b)),Strength);
float3 gradedLinear=lerp(mapped/12.92,pow(max((mapped+.055)/1.055,0.0),2.4),step(.04045,mapped));
return gradedLinear*View.PreExposure;
)GRADE");
 auto Input=[&](const TCHAR* N,UMaterialExpression* E){FCustomInput& I=Code->Inputs.AddDefaulted_GetRef();I.InputName=N;I.Input.Expression=E;};
 auto* Scene=NewObject<UMaterialExpressionSceneTexture>(Mat);Scene->SceneTextureId=PPI_PostProcessInput0;Add(Scene);Input(TEXT("Scene"),Scene);
 auto* Lut=NewObject<UMaterialExpressionTextureObjectParameter>(Mat);Lut->ParameterName=TEXT("Lut");Lut->SamplerType=SAMPLERTYPE_LinearColor;
 auto* DefaultLut=Cast<UTexture2D>(FOKStudioCatalog::Get().SourceFilterLut(0));
 if(DefaultLut && Outer!=GetTransientPackage()) {
  auto* SavedLut=NewObject<UTexture2D>(Outer,TEXT("T_GradeDefault"),RF_Public|RF_Standalone);
  auto& Mip=DefaultLut->GetPlatformData()->Mips[0];const void* Bytes=Mip.BulkData.LockReadOnly();
  SavedLut->Source.Init(1024,32,1,1,TSF_BGRA8,static_cast<const uint8*>(Bytes));Mip.BulkData.Unlock();
  SavedLut->SRGB=false;SavedLut->NeverStream=true;SavedLut->Filter=TF_Bilinear;SavedLut->AddressX=SavedLut->AddressY=TA_Clamp;SavedLut->PostEditChange();DefaultLut=SavedLut;
 }
 Lut->Texture=DefaultLut;Add(Lut);Input(TEXT("Lut"),Lut);
 for(const TCHAR* N:{TEXT("Strength"),TEXT("Exposure")}){auto* P=NewObject<UMaterialExpressionScalarParameter>(Mat);P->ParameterName=N;Add(P);Input(N,P);}
 Add(Code);Mat->GetEditorOnlyData()->EmissiveColor.Expression=Code;Mat->PreEditChange(nullptr);Mat->PostEditChange();return Mat;
#else
 return nullptr;
#endif
}
bool GradeReady() {
 if(!GradeParent)return false;
 auto* Mat=GradeParent.Get();const FMaterialResource* R=Mat->GetMaterialResource(GMaxRHIShaderPlatform);
 if(Mat->IsCompiling()||!R||!R->GetGameThreadShaderMap())return false;
#if WITH_EDITOR
 if(R->GetCompileErrors().Num()){for(const FString& E:R->GetCompileErrors())UE_LOG(LogTemp,Error,TEXT("OK source grade: %s"),*E);return false;}
#endif
 if(!R->IsGameThreadShaderMapComplete()){Mat->CacheShaders(EMaterialShaderPrecompileMode::Default);return false;}return true;
}
FAutoConsoleCommand BuildGradeCommand(TEXT("OKStudio.BuildSourceGrade"),TEXT("Save the native 32-cube grading parent for cooked runtime."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args){
#if WITH_EDITOR
 UPackage* P=CreatePackage(GradeAsset);auto* M=BuildGrade(P,TEXT("M_SourceGrade32"),RF_Public|RF_Standalone);
 bool Pass=false;if(M){P->MarkPackageDirty();FSavePackageArgs S;S.TopLevelFlags=RF_Public|RF_Standalone;S.SaveFlags=SAVE_NoError;
 Pass=UPackage::SavePackage(P,M,*FPackageName::LongPackageNameToFilename(GradeAsset,FPackageName::GetAssetPackageExtension()),S);}
 UE_LOG(LogTemp,Display,TEXT("OK source grade parent: RESULT %s"),Pass?TEXT("PASS"):TEXT("FAIL"));
#endif
 if(Args.Contains(TEXT("exit")))FPlatformMisc::RequestExit(false);
}));
}

UTexture* OKStudioIdentityGradeLut() {
 static TStrongObjectPtr<UTexture2D> Identity;
 if(!Identity){auto* T=UTexture2D::CreateTransient(256,16,PF_B8G8R8A8);auto* P=static_cast<FColor*>(T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
 for(int G=0;G<16;++G)for(int B=0;B<16;++B)for(int R=0;R<16;++R)P[G*256+B*16+R]=FColor(R*17,G*17,B*17,255);
 T->GetPlatformData()->Mips[0].BulkData.Unlock();T->SRGB=false;T->LODGroup=TEXTUREGROUP_ColorLookupTable;T->NeverStream=true;T->Filter=TF_Bilinear;T->AddressX=T->AddressY=TA_Clamp;T->UpdateResource();Identity.Reset(T);}
 return Identity.Get();
}

bool OKStudioApplySourceGrade(const FOKScenePost& Post,FPostProcessSettings& Settings) {
 Settings.WeightedBlendables.Array.RemoveAll([](const FWeightedBlendable& B){auto* M=Cast<UMaterialInstanceDynamic>(B.Object);return M&&GradeParent&&M->Parent==GradeParent.Get();});
 if(Post.ColorFilter<0||Post.ColorFilterIntensity<=0)return false;
 UTexture* Lut=FOKStudioCatalog::Get().SourceFilterLut(Post.ColorFilter);if(!Lut)return false;
 if(!GradeParent){auto* Parent=LoadObject<UMaterial>(nullptr,*(FString(GradeAsset)+TEXT(".M_SourceGrade32")));GradeParent.Reset(Parent?Parent:BuildGrade());}
 if(!GradeReady())return false;
 GradeInstances.RemoveAll([](const FGradeInstance& E){return !E.Material.IsValid();});
 const float Strength=FMath::Clamp(Post.ColorFilterIntensity,0.f,1.f);
 auto* Found=GradeInstances.FindByPredicate([&](const FGradeInstance& E){return E.Lut==Lut&&E.Strength==Strength&&E.Exposure==Post.Exposure;});
 if(!Found){if(GradeInstances.Num()>=32)GradeInstances.RemoveAt(0);auto& E=GradeInstances.AddDefaulted_GetRef();E.Lut=Lut;E.Strength=Strength;E.Exposure=Post.Exposure;
 auto* M=UMaterialInstanceDynamic::Create(GradeParent.Get(),GetTransientPackage());M->SetTextureParameterValue(TEXT("Lut"),Lut);M->SetScalarParameterValue(TEXT("Strength"),Strength);M->SetScalarParameterValue(TEXT("Exposure"),Post.Exposure);E.Material=M;Found=&E;}
 Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f,Found->Material.Get()));return true;
}

