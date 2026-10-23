#include "OKGalleryWidget.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "OKComboBox.h"
#include "OKShapeSlider.h"
#include "OKPregnancyPresets.h"
#include "KKPreviewActor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/Button.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"

void UOKCharacterController::BuildPregnancyControls(){
 BoneCategoryCombo->AddOption(TEXT("腹部形變"));PregnancyBox=WidgetTree->ConstructWidget<UVerticalBox>();
 PregnancyEnabled=WidgetTree->ConstructWidget<UCheckBox>();auto EnabledLabel=WidgetTree->ConstructWidget<UTextBlock>();EnabledLabel->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",16));EnabledLabel->SetAutoWrapText(true);EnabledLabel->SetText(FText::FromString(TEXT("啟用腹部形變")));
 PregnancyEnabled->SetContent(EnabledLabel);PregnancyEnabled->SetIsChecked(true);PregnancyEnabled->OnCheckStateChanged.AddDynamic(this,&UOKCharacterController::OnPregnancyEnabled);PregnancyBox->AddChild(PregnancyEnabled);
 PregnancyPreset=WidgetTree->ConstructWidget<UOKComboBoxString>();for(const auto& Preset:OKPregnancyPresets)PregnancyPreset->AddOption(Preset.Label);
 PregnancyPreset->SetSelectedIndex(0);PregnancyPreset->OnSelectionChanged.AddDynamic(this,&UOKCharacterController::OnPregnancyPreset);PregnancyBox->AddChild(PregnancyPreset);
 struct FControl{const TCHAR* Key;const TCHAR* Label;float Min;float Max;};
 const FControl Controls[]={
 {TEXT("inflationSize"),TEXT("腹部大小"),0,40}, {TEXT("inflationMultiplier"),TEXT("膨脹倍率"),-2,2},
 {TEXT("inflationRoundness"),TEXT("圓潤程度"),-.75f,.75f}, {TEXT("inflationMoveY"),TEXT("上下移動"),-.375f,.375f},
 {TEXT("inflationMoveZ"),TEXT("前後移動"),-.15f,.15f}, {TEXT("inflationStretchX"),TEXT("左右伸展"),-.6f,.6f},
 {TEXT("inflationStretchY"),TEXT("上下伸展"),-.6f,.6f}, {TEXT("inflationShiftY"),TEXT("上下偏移"),-.2f,.2f},
 {TEXT("inflationShiftZ"),TEXT("前後偏移"),-.15f,.15f}, {TEXT("inflationTaperY"),TEXT("上下收束"),-.075f,.075f},
 {TEXT("inflationTaperZ"),TEXT("前後收束"),-.075f,.075f}, {TEXT("inflationDrop"),TEXT("下垂"),0,1},
 {TEXT("inflationClothOffset"),TEXT("衣服間距"),-20,20}, {TEXT("inflationFatFold"),TEXT("脂肪褶皺"),0,2},
 {TEXT("inflationFatFoldHeight"),TEXT("褶皺高度"),-1,1}, {TEXT("inflationFatFoldGap"),TEXT("褶皺間距"),-1,1}};
 for(const auto& Def:Controls){
  auto Row=WidgetTree->ConstructWidget<UHorizontalBox>();auto Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(128.f);Box->SetMinDesiredHeight(44.f);
  auto Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",16));Label->SetAutoWrapText(true);Label->SetWrapTextAt(128.f);Label->SetText(FText::FromString(Def.Label));Box->SetContent(Label);Row->AddChild(Box);
  auto Slider=WidgetTree->ConstructWidget<UOKShapeSlider>();Slider->Owner=this;Slider->PregnancyParameter=Def.Key;Slider->ModRow=Row;
  Slider->SetMinValue(Def.Min);Slider->SetMaxValue(Def.Max);Slider->SetValue(0);Slider->SetSliderBarColor(FLinearColor(.35f,.85f,.9f));
  Slider->OnValueChanged.AddDynamic(Slider,&UOKShapeSlider::OnSliderMoved);auto RowSlot=Row->AddChildToHorizontalBox(Slider);RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));RowSlot->SetVerticalAlignment(VAlign_Center);
  auto ValueBox=WidgetTree->ConstructWidget<USizeBox>();ValueBox->SetWidthOverride(48.f);Slider->ValueLabel=WidgetTree->ConstructWidget<UTextBlock>();Slider->ValueLabel->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",16));ValueBox->SetContent(Slider->ValueLabel);Row->AddChild(ValueBox);
  auto Reset=WidgetTree->ConstructWidget<UButton>();auto Text=WidgetTree->ConstructWidget<UTextBlock>();Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));Text->SetAutoWrapText(false);Text->SetText(FText::FromString(TEXT("重置")));Reset->SetContent(Text);Reset->OnClicked.AddDynamic(Slider,&UOKShapeSlider::OnResetClicked);Row->AddChild(Reset);
  PregnancyBox->AddChild(Row);BoneSliders.Add(Slider);Slider->RefreshValueLabel();
 }
 ShapeBox->AddChild(PregnancyBox);PregnancyBox->SetVisibility(ESlateVisibility::Collapsed);
}
void UOKCharacterController::OnPregnancyChanged(const FString& Parameter,float Value){
 if(bSyncingBoneControls||InteractionWidget||InteractionScene||bEnterInteractionAfterLoad||!FMath::IsFinite(Value))return;
 TSharedPtr<FJsonObject> Mods;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(EditorModsJson()),Mods))return;
 const TSharedPtr<FJsonObject>* Preg=nullptr;if(!Mods->TryGetObjectField(TEXT("pregnancy"),Preg))return;
 (*Preg)->SetNumberField(Parameter,Value);CommitEditorMods(Mods);
}
void UOKCharacterController::OnPregnancyEnabled(bool Value){
 if(bSyncingBoneControls||InteractionWidget||InteractionScene||bEnterInteractionAfterLoad)return;TSharedPtr<FJsonObject> Mods;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(EditorModsJson()),Mods))return;
 Mods->GetObjectField(TEXT("pregnancy"))->SetBoolField(TEXT("GameplayEnabled"),Value);CommitEditorMods(Mods);
}
void UOKCharacterController::OnPregnancyPreset(FString Value,ESelectInfo::Type Type){
 if(bSyncingBoneControls||InteractionWidget||InteractionScene||bEnterInteractionAfterLoad)return;TSharedPtr<FJsonObject> Mods;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(EditorModsJson()),Mods))return;
 for(const auto& Preset:OKPregnancyPresets)if(Value==Preset.Label||Value==Preset.Name){TSharedPtr<FJsonObject> Preg;
  if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Preset.Json)),Preg)){Mods->SetObjectField(TEXT("pregnancy"),Preg);CommitEditorMods(Mods);SyncBoneSliders();}break;
 }
}
void UOKCharacterController::ApplyPendingEditorMods(){
 TRACE_CPUPROFILER_EVENT_SCOPE(KK_GalleryEditorMods);
 if(!bEditorModsDirty||!PreviewActor||!bModelLayout)return;
 PreviewActor->Session.ApplyBodyPhysics(Selected.Body.BustSoftness,Selected.Body.BustWeight);
 bool GeometryChanged=false;
 if(!PreviewActor->Session.ApplyEditorMods(EditorModsJson(),OutfitIndex,&GeometryChanged))return;
 bEditorModsDirty=false;
 if(GeometryChanged){
  // Shape is baked once into GPU source data. Animation continues from the
  // same session and elapsed time, without CPU skinning on ordinary frames.
  const float Phase=PreviewActor->AnimPhase;const int32 Mode=PreviewActor->PoseMode;
  FString Error;if(!PreviewActor->RefreshEditorGeometry())PreviewActor->LoadSession(Error);PreviewActor->AnimPhase=Phase;PreviewActor->PoseMode=Mode;
  PreviewActor->ApplyShapes(Selected.Face.ShapeValueFace,Selected.Body.ShapeValueBody);
  if(!Error.IsEmpty()&&InspectorText)InspectorText->SetText(FText::FromString(Error));
 }else PreviewActor->MarkShapeDirty(Selected.Face.ShapeValueFace,Selected.Body.ShapeValueBody);
}

void UOKCharacterController::OnBodyPhysicsChanged(const FString& Parameter,float NewValue){
 if(bSyncingBoneControls||InteractionWidget||InteractionScene||bEnterInteractionAfterLoad||!FMath::IsFinite(NewValue))return;
 if(Parameter==TEXT("bustSoftness"))Selected.Body.BustSoftness=NewValue;else if(Parameter==TEXT("bustWeight"))Selected.Body.BustWeight=NewValue;else return;
 bEditorModsDirty=true;
}
