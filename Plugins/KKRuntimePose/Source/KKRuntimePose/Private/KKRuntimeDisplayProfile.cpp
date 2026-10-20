#include "KKRuntimeDisplayProfile.h"
#include "KKRuntimeSharedMaterial.h"
#include "Engine/Texture2D.h"
#include "Engine/Scene.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

bool OKCharacterDisplayShadowsEnabled(){
 bool Enabled=true;if(GConfig)GConfig->GetBool(TEXT("OpenKoikatsu.CharacterDisplay"),TEXT("EnableShadows"),Enabled,GGameIni);return Enabled;
}
void OKApplyCharacterMakerLighting(FKKRuntimeMaterialScene& Scene){
 // ChaShader.ChangeAmbientShaodwColor. Studio's 128 gray is a different mode.
 Scene.Ambient=FLinearColor(0.019f,0.027f,0.039f,0.f);
 // Sessions store 1-GraphicData.shadowDepth. Restore the raw maker alpha.
 Scene.StudioShadowGlobals=false;
 // CustomScene trilight, Saved/ShaderDxbc/runtime_maker_lighting.txt.
 // Unity packs unity_SHA* as (L1.x, L1.y, L1.z, L0-L2.y). main_skin has no SHB/SHC.
 const float R[]={0.08806409f,-0.080715f,0.f,0.f,0.f,0.f,-0.00642186031f};
 const float G[]={0.167791843f,-0.007285677f,0.f,0.f,0.f,0.f,0.0044066105f};
 const float B[]={0.320829839f,0.151558951f,0.f,0.f,0.f,0.f,-0.000356730074f};
 auto Pack=[](const float* C){return FLinearColor(C[3],C[1],C[2],C[0]-C[6]);};
 Scene.Uniforms.Add(TEXT("unity_SHAr"),Pack(R));
 Scene.Uniforms.Add(TEXT("unity_SHAg"),Pack(G));
 Scene.Uniforms.Add(TEXT("unity_SHAb"),Pack(B));
}
FString OKCharacterDisplayRampPath(){
 FString Path;if(GConfig)GConfig->GetString(TEXT("OpenKoikatsu.CharacterDisplay"),TEXT("RampTexture"),Path,GGameIni);
 return Path.IsEmpty()?FString():FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()/Path);
}

UTexture2D* OKLoadRuntimeDisplayLUT(const FString& Path,FString& Error){
 check(IsInGameThread());TArray<uint8> Bytes;
 if(!FFileHelper::LoadFileToArray(Bytes,*Path)||Bytes.Num()>1024*1024){Error=TEXT("Display LUT missing or oversized: ")+Path;return nullptr;}
 auto& Images=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));auto Image=Images.CreateImageWrapper(EImageFormat::PNG);TArray<uint8> Pixels;
 if(!Image||!Image->SetCompressed(Bytes.GetData(),Bytes.Num())||Image->GetWidth()!=256||Image->GetHeight()!=16||!Image->GetRaw(ERGBFormat::BGRA,8,Pixels)||Pixels.Num()!=256*16*4){Error=TEXT("Display LUT must be a 256x16 PNG color cube");return nullptr;}
 UTexture2D* LUT=UTexture2D::CreateTransient(256,16,PF_B8G8R8A8);
 if(!LUT){Error=TEXT("Display LUT texture allocation failed");return nullptr;}
 LUT->SRGB=false;LUT->LODGroup=TEXTUREGROUP_ColorLookupTable;LUT->Filter=TF_Bilinear;LUT->AddressX=LUT->AddressY=TA_Clamp;LUT->NeverStream=true;
 auto& Mip=LUT->GetPlatformData()->Mips[0];void* Data=Mip.BulkData.Lock(LOCK_READ_WRITE);FMemory::Memcpy(Data,Pixels.GetData(),Pixels.Num());Mip.BulkData.Unlock();LUT->UpdateResource();Error.Reset();return LUT;
}

void OKApplyRuntimeDisplayProfile(FPostProcessSettings& S,UTexture2D* LUT){
 S.bOverride_ToneCurveAmount=true;S.ToneCurveAmount=0;
 S.bOverride_ExpandGamut=true;S.ExpandGamut=0;
 S.bOverride_AutoExposureMethod=true;S.AutoExposureMethod=AEM_Manual;
 S.bOverride_AutoExposureBias=true;S.AutoExposureBias=0;
 S.bOverride_AutoExposureApplyPhysicalCameraExposure=true;S.AutoExposureApplyPhysicalCameraExposure=false;
 S.bOverride_AutoExposureMinBrightness=true;S.AutoExposureMinBrightness=1;
 S.bOverride_AutoExposureMaxBrightness=true;S.AutoExposureMaxBrightness=1;
 S.bOverride_MotionBlurAmount=true;S.MotionBlurAmount=0;
 S.bOverride_MotionBlurMax=true;S.MotionBlurMax=0;
 S.bOverride_VignetteIntensity=true;S.VignetteIntensity=0;
 S.bOverride_BloomIntensity=true;S.BloomIntensity=0;
 S.bOverride_ColorGradingLUT=true;S.ColorGradingLUT=LUT;
 S.bOverride_ColorGradingIntensity=true;S.ColorGradingIntensity=LUT?1:0;
}
