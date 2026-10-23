#include "OKStorySavePortraitCommandlet.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "OKStoryCapture.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

static bool OKStorySaveAsset(UObject* Asset)
{
	UPackage* Package = Asset->GetPackage();
	Package->MarkPackageDirty();
	const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const bool bSaved = UPackage::SavePackage(Package, Asset, *File, Args);
	UE_LOG(LogTemp, Display, TEXT("OKStorySavePortrait: %s %s"), bSaved ? TEXT("saved") : TEXT("FAILED"), *File);
	return bSaved;
}

int32 UOKStorySavePortraitCommandlet::Main(const FString& Params)
{
	// The capture targets' formats (FOKStoryPortraitCapture::Update): display-encoded colour, HDR coverage.
	auto Target = [](const TCHAR* Name, ETextureRenderTargetFormat Format, bool bEncoded)
	{
		UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Game/OKStudio/Story/%s"), Name));
		UTextureRenderTarget2D* T = NewObject<UTextureRenderTarget2D>(Package, Name, RF_Public | RF_Standalone);
		T->RenderTargetFormat = Format;
		T->bForceLinearGamma = true;
		T->TargetGamma = bEncoded ? 2.2f : 0.f;
		T->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
		T->InitAutoFormat(16, 16);
		T->UpdateResourceImmediate(true);
		return T;
	};
	UTextureRenderTarget2D* Color = Target(TEXT("RT_StoryPortraitColor"), RTF_RGBA8, true);
	UTextureRenderTarget2D* Coverage = Target(TEXT("RT_StoryPortraitCoverage"), RTF_RGBA16f, false);
	UPackage* Package = CreatePackage(TEXT("/Game/OKStudio/Story/M_StoryPortrait"));
	UMaterial* Material = OKStoryBuildPortraitMaterial(Package, TEXT("M_StoryPortrait"), RF_Public | RF_Standalone, Color, Coverage);
	const bool bOk = OKStorySaveAsset(Color) && OKStorySaveAsset(Coverage) && Material && OKStorySaveAsset(Material);
	return bOk ? 0 : 1;
}
