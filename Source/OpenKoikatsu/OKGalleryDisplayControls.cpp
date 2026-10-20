#include "OKGalleryWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/SpinBox.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Misc/ConfigCacheIni.h"

namespace { constexpr const TCHAR* DisplaySection=TEXT("OpenKoikatsu.EditorDisplay"); }

void UOKCharacterController::BuildEditorDisplayControls(UVerticalBox* Parent)
{
 GConfig->GetFloat(DisplaySection,TEXT("PanelWidth"),EditorPanelWidth,GGameUserSettingsIni);
 GConfig->GetFloat(DisplaySection,TEXT("FontSize"),EditorFontSize,GGameUserSettingsIni);
 auto Row=[&](const TCHAR* Label,const TCHAR* Name,float Minimum,float Maximum,float Value){
  UHorizontalBox* Box=WidgetTree->ConstructWidget<UHorizontalBox>();
  UTextBlock* Text=WidgetTree->ConstructWidget<UTextBlock>();
  Text->SetText(FText::FromString(Label));Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",16));
  Box->AddChildToHorizontalBox(Text)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
  USpinBox* Input=WidgetTree->ConstructWidget<USpinBox>(USpinBox::StaticClass(),Name);
  Input->SetMinValue(Minimum);Input->SetMaxValue(Maximum);
  Input->SetMinSliderValue(Minimum);Input->SetMaxSliderValue(Maximum);Input->SetDelta(1.f);
  Input->SetMinFractionalDigits(0);Input->SetMaxFractionalDigits(0);Input->SetValue(Value);
  Input->SetMinDesiredWidth(100.f);Box->AddChild(Input);Parent->AddChild(Box);return Input;
 };
 EditorWidthInput=Row(TEXT("編輯欄寬度"),TEXT("EditorPanelWidth"),320,960,EditorPanelWidth);
 EditorFontInput=Row(TEXT("顯示字級"),TEXT("EditorFontSize"),12,24,EditorFontSize);
 EditorWidthInput->OnValueChanged.AddDynamic(this,&UOKCharacterController::OnEditorPanelWidthChanged);
 EditorFontInput->OnValueChanged.AddDynamic(this,&UOKCharacterController::OnEditorFontSizeChanged);
 EditorFontInput->SetToolTipText(FText::FromString(TEXT("同步調整文字與操作元件大小；字級較大時會保留欄位所需的最小寬度。")));
}

void UOKCharacterController::ApplyEditorDisplaySettings()
{
 EditorFontSize=FMath::IsFinite(EditorFontSize)?FMath::Clamp(EditorFontSize,12.f,24.f):16.f;
 const float Scale=EditorFontSize/16.f;
 EditorPanelWidth=FMath::IsFinite(EditorPanelWidth)?FMath::Clamp(EditorPanelWidth,320.f*FMath::Max(1.f,Scale),960.f):360.f;
 if(ShapePanel)ShapePanel->SetWidthOverride(EditorPanelWidth);
 // Layout scaling also scales hit targets, wrapping, row heights and popup anchors.
 // The scroll box receives the inverse-scaled available height and remains scrollable.
 if(EditorContentScale)EditorContentScale->SetUserSpecifiedScale(Scale);
 if(EditorWidthInput&&!FMath::IsNearlyEqual(EditorWidthInput->GetValue(),EditorPanelWidth))EditorWidthInput->SetValue(EditorPanelWidth);
 if(EditorFontInput&&!FMath::IsNearlyEqual(EditorFontInput->GetValue(),EditorFontSize))EditorFontInput->SetValue(EditorFontSize);
}

void UOKCharacterController::OnEditorPanelWidthChanged(float Value)
{
 EditorPanelWidth=Value;ApplyEditorDisplaySettings();
 bEditorDisplayDirty=true;EditorDisplayChangedAt=FPlatformTime::Seconds();
}

void UOKCharacterController::OnEditorFontSizeChanged(float Value)
{
 EditorFontSize=Value;ApplyEditorDisplaySettings();
 bEditorDisplayDirty=true;EditorDisplayChangedAt=FPlatformTime::Seconds();
}

void UOKCharacterController::SaveEditorDisplaySettings()
{
 GConfig->SetFloat(DisplaySection,TEXT("PanelWidth"),EditorPanelWidth,GGameUserSettingsIni);
 GConfig->SetFloat(DisplaySection,TEXT("FontSize"),EditorFontSize,GGameUserSettingsIni);
 GConfig->Flush(false,GGameUserSettingsIni);bEditorDisplayDirty=false;
}
