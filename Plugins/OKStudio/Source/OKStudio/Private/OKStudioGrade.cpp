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
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace {
// Amplify Color grades with lerp(LutTexture, LutBlendTexture, BlendAmount) (AmplifyColorBase.OnRenderImage's blend
// cache). Studio sets LutTexture to the scene's filter (SystemButtonCtrl.SetACE) and BlendAmount to aceBlend; its
// camera's LutBlendTexture is the serialized Lut_Amount (filter 0) and nothing replaces it (SunLightInfo.SetACE is
// empty). So a blend leans towards Lut_Amount, not towards the ungraded picture.
// The same pass then applies Studio's vignette (Standard Assets VignetteAndChromaticAberration, Simple mode, after
// Amplify Color on the camera): colour x (1 - dot(c, c) x k), c = (uv - 0.5) x 2, k = 1 / (1 - intensity) - 1
// (OnRenderImage's _Intensity). Its edge blur (_Blur) is not reproduced.
const TCHAR* GradeAsset=TEXT("/Game/OKStudio/Post/SourceGrade3/M_SourceGrade32");
TStrongObjectPtr<UMaterial> GradeParent;
struct FGradeInstance { TWeakObjectPtr<UTexture> Lut; float Strength=0, Exposure=0, UseLut=1, Vignette=0; TWeakObjectPtr<UMaterialInstanceDynamic> Material; };
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
float2 up=float2((min(b+1,31.0)-b)/32.0,0.0);
float3 sel=lerp(Texture2DSampleLevel(Lut,LutSampler,uv,0).rgb,Texture2DSampleLevel(Lut,LutSampler,uv+up,0).rgb,frac(q.b));
float3 blend=lerp(Texture2DSampleLevel(BlendLut,BlendLutSampler,uv,0).rgb,Texture2DSampleLevel(BlendLut,BlendLutSampler,uv+up,0).rgb,frac(q.b));
float3 mapped=UseLut>0.5?lerp(blend,sel,Strength):saturate(g);
float3 gradedLinear=lerp(mapped/12.92,pow(max((mapped+.055)/1.055,0.0),2.4),step(.04045,mapped));
float2 v=(ScreenUV-0.5)*2.0;
gradedLinear*=max(1.0-dot(v,v)*Vignette,0.0);
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
 auto* BlendLut=NewObject<UMaterialExpressionTextureObjectParameter>(Mat);BlendLut->ParameterName=TEXT("BlendLut");BlendLut->SamplerType=SAMPLERTYPE_LinearColor;
 BlendLut->Texture=DefaultLut;Add(BlendLut);Input(TEXT("BlendLut"),BlendLut);
 for(const TCHAR* N:{TEXT("Strength"),TEXT("Exposure"),TEXT("UseLut"),TEXT("Vignette")}){auto* P=NewObject<UMaterialExpressionScalarParameter>(Mat);P->ParameterName=N;P->DefaultValue=FCString::Strcmp(N,TEXT("UseLut"))==0?1.f:0.f;Add(P);Input(N,P);}
 auto* ScreenUV=NewObject<UMaterialExpressionTextureCoordinate>(Mat);ScreenUV->CoordinateIndex=0;Add(ScreenUV);Input(TEXT("ScreenUV"),ScreenUV);
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
 // Strength = 1 - aceBlend: 0 is Lut_Amount alone (Studio's blend target), not an ungraded picture; -1 filter is none.
 // Without a filter the pass still runs for the vignette, ungraded.
 const float Vignette=Post.bVignette?1.f/(1.f-FMath::Clamp(Post.SourceVignetteIntensity,0.f,.99f))-1.f:0.f;
 if(Post.ColorFilter<0&&Vignette<=0.f)return false;
 UTexture* BlendLut=FOKStudioCatalog::Get().SourceFilterLut(0);UTexture* Lut=Post.ColorFilter>=0?FOKStudioCatalog::Get().SourceFilterLut(Post.ColorFilter):BlendLut;
 const float UseLut=Post.ColorFilter>=0?1.f:0.f;
 if(!Lut||!BlendLut)return false;
 if(!GradeParent){auto* Parent=LoadObject<UMaterial>(nullptr,*(FString(GradeAsset)+TEXT(".M_SourceGrade32")));GradeParent.Reset(Parent?Parent:BuildGrade());}
 if(!GradeReady())return false;
 GradeInstances.RemoveAll([](const FGradeInstance& E){return !E.Material.IsValid();});
 const float Strength=FMath::Clamp(Post.ColorFilterIntensity,0.f,1.f);
 auto* Found=GradeInstances.FindByPredicate([&](const FGradeInstance& E){return E.Lut==Lut&&E.Strength==Strength&&E.Exposure==Post.Exposure&&E.UseLut==UseLut&&E.Vignette==Vignette;});
 if(!Found){if(GradeInstances.Num()>=32)GradeInstances.RemoveAt(0);auto& E=GradeInstances.AddDefaulted_GetRef();E.Lut=Lut;E.Strength=Strength;E.Exposure=Post.Exposure;E.UseLut=UseLut;E.Vignette=Vignette;
 auto* M=UMaterialInstanceDynamic::Create(GradeParent.Get(),GetTransientPackage());M->SetTextureParameterValue(TEXT("Lut"),Lut);M->SetTextureParameterValue(TEXT("BlendLut"),BlendLut);M->SetScalarParameterValue(TEXT("Strength"),Strength);M->SetScalarParameterValue(TEXT("Exposure"),Post.Exposure);
 M->SetScalarParameterValue(TEXT("UseLut"),UseLut);M->SetScalarParameterValue(TEXT("Vignette"),Vignette);E.Material=M;Found=&E;}
 Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f,Found->Material.Get()));return true;
}

namespace
{
 // The 1024x32 strip of a LUT texture's pixels (BGRA), as the grade samples it.
 bool ReadGradeLut(UTexture* Texture,TArray<FColor>& Out){
  auto* T=Cast<UTexture2D>(Texture);if(!T||!T->GetPlatformData()||T->GetPlatformData()->Mips.IsEmpty())return false;
  auto& Mip=T->GetPlatformData()->Mips[0];if(Mip.SizeX!=1024||Mip.SizeY!=32)return false;
  const void* Bytes=Mip.BulkData.LockReadOnly();const bool Ok=Bytes&&Mip.BulkData.GetBulkDataSize()>=1024*32*4;
  if(Ok){Out.SetNumUninitialized(1024*32);FMemory::Memcpy(Out.GetData(),Bytes,1024*32*4);}
  Mip.BulkData.Unlock();return Ok;
 }
 FVector3f GradeTexel(const TArray<FColor>& L,float X,float Y){
  // Bilinear with clamp at texel centres (X, Y in texels, centres at .5).
  const float FX=FMath::Clamp(X-.5f,0.f,1023.f),FY=FMath::Clamp(Y-.5f,0.f,31.f);
  const int32 X0=FMath::FloorToInt(FX),Y0=FMath::FloorToInt(FY),X1=FMath::Min(X0+1,1023),Y1=FMath::Min(Y0+1,31);const float AX=FX-X0,AY=FY-Y0;
  auto P=[&](int32 PX,int32 PY){const FColor& C=L[PY*1024+PX];return FVector3f(C.R,C.G,C.B)/255.f;};
  return FMath::Lerp(FMath::Lerp(P(X0,Y0),P(X1,Y0),AX),FMath::Lerp(P(X0,Y1),P(X1,Y1),AX),AY);
 }
 FVector3f GradeLookup(const TArray<FColor>& L,const FVector3f& G){
  const FVector3f Q(FMath::Clamp(G.X,0.f,1.f)*31.f,FMath::Clamp(G.Y,0.f,1.f)*31.f,FMath::Clamp(G.Z,0.f,1.f)*31.f);
  const float B=FMath::FloorToFloat(Q.Z);const float X=Q.X+B*32.f+.5f,Y=Q.Y+.5f,Up=(FMath::Min(B+1.f,31.f)-B)*32.f;
  return FMath::Lerp(GradeTexel(L,X,Y),GradeTexel(L,X+Up,Y),Q.Z-B);
 }
 float SrgbToLinear(float V){return V<=.04045f?V/12.92f:FMath::Pow((V+.055f)/1.055f,2.4f);}
 float LinearToSrgb(float V){V=FMath::Max(V,0.f);return V<.0031308f?12.92f*V:1.055f*FMath::Pow(V,1.f/2.4f)-.055f;}
}

FLinearColor OKStudioUngradeSourceColor(const FOKScenePost& Post,const FLinearColor& Shown,float VignetteDistance2)
{
 // The same pass on the CPU (OKStudioApplySourceGrade's material), then the colour it maps onto Shown, found by
 // fixed-point steps (the grade is monotonic and near the identity).
 const float Vignette=Post.bVignette?1.f/(1.f-FMath::Clamp(Post.SourceVignetteIntensity,0.f,.99f))-1.f:0.f;
 TArray<FColor> Lut,Blend;const bool UseLut=Post.ColorFilter>=0;
 if(UseLut&&(!ReadGradeLut(FOKStudioCatalog::Get().SourceFilterLut(Post.ColorFilter),Lut)||!ReadGradeLut(FOKStudioCatalog::Get().SourceFilterLut(0),Blend)))return Shown;
 const float Strength=FMath::Clamp(Post.ColorFilterIntensity,0.f,1.f);const float Gain=FMath::Exp2(Post.Exposure);const float Vig=FMath::Max(1.f-VignetteDistance2*Vignette,0.f);
 auto Forward=[&](const FVector3f& Srgb){
  FVector3f G(LinearToSrgb(SrgbToLinear(Srgb.X)*Gain),LinearToSrgb(SrgbToLinear(Srgb.Y)*Gain),LinearToSrgb(SrgbToLinear(Srgb.Z)*Gain));
  const FVector3f Mapped=UseLut?FMath::Lerp(GradeLookup(Blend,G),GradeLookup(Lut,G),Strength):FVector3f(FMath::Clamp(G.X,0.f,1.f),FMath::Clamp(G.Y,0.f,1.f),FMath::Clamp(G.Z,0.f,1.f));
  return FVector3f(LinearToSrgb(SrgbToLinear(Mapped.X)*Vig),LinearToSrgb(SrgbToLinear(Mapped.Y)*Vig),LinearToSrgb(SrgbToLinear(Mapped.Z)*Vig));
 };
 const FVector3f Target(Shown.R,Shown.G,Shown.B);FVector3f X=Target;
 for(int32 Step=0;Step<48;++Step){const FVector3f D=Target-Forward(X);if(D.GetAbsMax()<.5f/255.f)break;X=FVector3f(FMath::Clamp(X.X+D.X,0.f,1.f),FMath::Clamp(X.Y+D.Y,0.f,1.f),FMath::Clamp(X.Z+D.Z,0.f,1.f));}
 return FLinearColor(X.X,X.Y,X.Z,Shown.A);
}
