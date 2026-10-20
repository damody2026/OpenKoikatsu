#include "OKHGShading.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static TAutoConsoleVariable<int32> CVarOKRenderStyle(TEXT("ok.RenderStyle"),0,
 TEXT("Character render style: 0 Koikatsu, 1 HG. Read when a character's materials are created."),ECVF_Default);

int32 OKRenderStyle(){return FMath::Clamp(CVarOKRenderStyle.GetValueOnGameThread(),0,1);}
void OKSetRenderStyle(int32 Style){CVarOKRenderStyle->Set(FMath::Clamp(Style,0,1),ECVF_SetByCode);}

EOKHGSurface OKHGSurfaceOf(EKKRuntimeMaterialRecipe R){
 using E=EKKRuntimeMaterialRecipe;const int32 I=int32(R);
 switch(R){
 case E::KoikanoSkin:case E::ForgeSkin:return EOKHGSurface::Skin;
 case E::ForgeHair:case E::ForgeHairFront:return EOKHGSurface::Hair;
 // Eyes, brows and the nose line are drawn, not lit; glasses, depth-only
 // and effect shaders keep their own look.
 case E::ForgeEyeWhite:case E::ForgeEye:case E::KoikanoEyeWhite:case E::KoikanoEye:case E::ForgeEyeWhiteLod:
 case E::ForgeNose:case E::KoikanoNose:case E::KoikanoBrow:case E::ForgeGlasses:case E::IBLDepth:
 case E::ForgeDither:case E::ForgeEmission:case E::OilPainting:case E::Goo:case E::BetterMatcap:case E::None:
  return EOKHGSurface::None;
 default:break;
 }
 if(I>=int32(E::KoikanoHairSun0)&&I<=int32(E::KoikanoHairSun7))return EOKHGSurface::Hair;
 if(I>=int32(E::KoikanoGlasses0)&&I<=int32(E::KoikanoGlasses11))return EOKHGSurface::None;
 return EOKHGSurface::Surface;
}

TConstArrayView<FOKHGParameter> OKHGParameters(){
 static const FOKHGParameter Parameters[]={
  {TEXT("__OK_HG_Enable"),TEXT("HG_Enable"),FLinearColor(1,0,0,0),false},
  {TEXT("__OK_HG_ViewKey"),TEXT("HG_ViewKey"),FLinearColor(1,0,0,0),false},
  {TEXT("__OK_HG_KeyDir"),TEXT("HG_KeyDir"),FLinearColor(-.5f,.55f,.65f,1),true},
  {TEXT("__OK_HG_ShadowThreshold"),TEXT("HG_ShadowThreshold"),FLinearColor(.52f,0,0,0),false},
  {TEXT("__OK_HG_ShadowSoftness"),TEXT("HG_ShadowSoftness"),FLinearColor(.035f,0,0,0),false},
  {TEXT("__OK_HG_ShadowTint"),TEXT("HG_ShadowTint"),FLinearColor(.66f,.68f,.84f,1),true},
  {TEXT("__OK_HG_SkinShadowTint"),TEXT("HG_SkinShadowTint"),FLinearColor(.97f,.74f,.72f,1),true},
  {TEXT("__OK_HG_RimStrength"),TEXT("HG_RimStrength"),FLinearColor(.32f,0,0,0),false},
  {TEXT("__OK_HG_RimPower"),TEXT("HG_RimPower"),FLinearColor(4.5f,0,0,0),false},
  {TEXT("__OK_HG_RimColor"),TEXT("HG_RimColor"),FLinearColor(1,.96f,.92f,1),true},
  {TEXT("__OK_HG_HairSpec"),TEXT("HG_HairSpec"),FLinearColor(.22f,0,0,0),false},
  {TEXT("__OK_HG_HairSpecPower"),TEXT("HG_HairSpecPower"),FLinearColor(72,0,0,0),false},
  {TEXT("__OK_HG_HairShift"),TEXT("HG_HairShift"),FLinearColor(.12f,0,0,0),false},
  {TEXT("__OK_HG_FaceFlatten"),TEXT("HG_FaceFlatten"),FLinearColor(.72f,0,0,0),false},
  {TEXT("__OK_HG_Face"),TEXT("HG_Face"),FLinearColor(0,0,0,0),false},
 };
 return MakeArrayView(Parameters);
}

static FString OKHGShaderDir(){
 const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("OKHGShading"));
 return Plugin?FPaths::Combine(Plugin->GetBaseDir(),TEXT("Shaders")):FString();
}

bool OKHGRelightSource(FString& Code,FString& Error){
 const FString Dir=OKHGShaderDir();const FString File=Dir/TEXT("Private/OKHGRelight.ush");
 if(Dir.IsEmpty()||!FFileHelper::LoadFileToString(Code,*File)){Error=TEXT("HG relight shader source missing: ")+File;return false;}
 // The source's own "return o0;" marker would be replaced with it again.
 if(Code.Contains(TEXT("return o0;"))){Error=TEXT("HG relight source must not return");return false;}
 Error.Reset();return true;
}

static TAutoConsoleVariable<FString> CVarOKRenderRamp(TEXT("ok.RenderRamp"),TEXT(""),
 TEXT("Koikatsu shadow ramp key from Content/KKPreview/Ramps/ramps.json; empty = each card's own."),ECVF_Default);
FString OKRenderRampKey(){return CVarOKRenderRamp.GetValueOnGameThread();}
void OKSetRenderRampKey(const FString& Key){CVarOKRenderRamp->Set(*Key,ECVF_SetByCode);}

namespace {
FString GRampCatalogue;bool GRampsLoaded=false;TArray<FOKRenderRamp> GRamps;
TMap<FString,UTexture2D*> GRampTextures; // rooted: a static strong pointer would outlive UObject shutdown
FString RampCatalogue(){return GRampCatalogue.IsEmpty()?FPaths::ProjectContentDir()/TEXT("KKPreview/Ramps/ramps.json"):GRampCatalogue;}
}
void OKSetRenderRampCatalogue(const FString& Path){if(Path!=GRampCatalogue){GRampCatalogue=Path;GRampsLoaded=false;GRamps.Reset();}}
const TArray<FOKRenderRamp>& OKRenderRamps(){
 check(IsInGameThread());if(GRampsLoaded)return GRamps;GRampsLoaded=true;
 FString Text;TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
 if(!FFileHelper::LoadFileToString(Text,*RampCatalogue())||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root||!Root->TryGetArrayField(TEXT("ramps"),Rows)){UE_LOG(LogTemp,Warning,TEXT("Render ramps: no catalogue at %s"),*RampCatalogue());return GRamps;}
 for(const auto& V:*Rows){const auto O=V->AsObject();FOKRenderRamp R;if(O&&O->TryGetStringField(TEXT("key"),R.Key)&&O->TryGetStringField(TEXT("file"),R.File)){O->TryGetStringField(TEXT("name"),R.Name);GRamps.Add(R);}}
 UE_LOG(LogTemp,Display,TEXT("Render ramps: %d from %s"),GRamps.Num(),*RampCatalogue());return GRamps;
}
UTexture2D* OKLoadRenderRamp(const FString& Key,FString& Error){
 check(IsInGameThread());if(UTexture2D* const* Hit=GRampTextures.Find(Key))return *Hit;
 const FOKRenderRamp* Ramp=OKRenderRamps().FindByPredicate([&](const FOKRenderRamp& R){return R.Key==Key;});
 if(!Ramp){Error=TEXT("Unknown render ramp: ")+Key;return nullptr;}
 const FString Path=FPaths::GetPath(RampCatalogue())/Ramp->File;TArray<uint8> Bytes;TArray<uint8> Pixels;
 auto& Images=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));auto Image=Images.CreateImageWrapper(EImageFormat::PNG);
 if(!FFileHelper::LoadFileToArray(Bytes,*Path)||Bytes.Num()>4*1024*1024||!Image||!Image->SetCompressed(Bytes.GetData(),Bytes.Num())||Image->GetWidth()<2||Image->GetWidth()>4096||Image->GetHeight()<1||Image->GetHeight()>256||!Image->GetRaw(ERGBFormat::BGRA,8,Pixels)){Error=TEXT("Render ramp PNG unreadable: ")+Path;return nullptr;}
 // mt_ramp textures are linear and clamped, as the session binds the card's own _RampG.
 UTexture2D* Texture=UTexture2D::CreateTransient(Image->GetWidth(),Image->GetHeight(),PF_B8G8R8A8);
 if(!Texture){Error=TEXT("Render ramp allocation failed");return nullptr;}
 Texture->SRGB=false;Texture->Filter=TF_Bilinear;Texture->AddressX=Texture->AddressY=TA_Clamp;Texture->NeverStream=true;
 auto& Mip=Texture->GetPlatformData()->Mips[0];void* Data=Mip.BulkData.Lock(LOCK_READ_WRITE);FMemory::Memcpy(Data,Pixels.GetData(),Pixels.Num());Mip.BulkData.Unlock();Texture->UpdateResource();
 Texture->AddToRoot();GRampTextures.Add(Key,Texture);Error.Reset();return Texture;
}

class FOKHGShadingModule:public IModuleInterface{
 void StartupModule() override{
  const FString Dir=OKHGShaderDir();if(!Dir.IsEmpty())AddShaderSourceDirectoryMapping(TEXT("/Plugin/OKHGShading"),Dir);
 }
};
IMPLEMENT_MODULE(FOKHGShadingModule,OKHGShading)
