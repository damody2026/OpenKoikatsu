#include "OKHUD.h"
#include "OKGalleryWidget.h"
#include "Blueprint/UserWidget.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void AOKHUD::BeginPlay()
{
	Super::BeginPlay();
	FString Manifest;
	if(FParse::Value(FCommandLine::Get(),TEXT("StreetSocialManifest="),Manifest)||FParse::Value(FCommandLine::Get(),TEXT("CrowdRuntimeScene="),Manifest))return;
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}
	Gallery = CreateWidget<UOKGalleryWidget>(PC, UOKGalleryWidget::StaticClass());
	if (Gallery)
	{
		Gallery->AddToViewport(100);
	}
}

void AOKHUD::SetGalleryVisible(bool bVisible)
{
	if (Gallery)
	{
		Gallery->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	bShowHUD = bVisible;
}
