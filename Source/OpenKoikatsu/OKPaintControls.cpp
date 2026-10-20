#include "OKPaintSprayer.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

void AOKPaintSprayer::BuildPanel()
{
 if(!GetWorld()||!GetWorld()->GetGameViewport())return;
 auto Column=SNew(SVerticalBox);const TWeakObjectPtr<AOKPaintSprayer> Weak(this);
 auto Add=[&](const TCHAR* Name,float FOKPaintSettings::*Member,float Low,float High){
  Column->AddSlot().AutoHeight().Padding(3)[SNew(STextBlock).ColorAndOpacity(FLinearColor::White).Text_Lambda([Weak,Member,Name](){return FText::FromString(Weak.IsValid()?FString::Printf(TEXT("%s：%.2f"),Name,Weak->Settings.*Member):FString());})];
  Column->AddSlot().AutoHeight().Padding(3)[SNew(SSlider).Value_Lambda([Weak,Member,Low,High](){return Weak.IsValid()?(Weak->Settings.*Member-Low)/(High-Low):0.f;}).OnValueChanged_Lambda([Weak,Member,Low,High](float V){if(Weak.IsValid())Weak->Settings.*Member=FMath::Lerp(Low,High,V);})];
 };
 Add(TEXT("水柱不透明度"),&FOKPaintSettings::JetOpacity,0,1);
 auto AddLimit=[&](const TCHAR* Name,int32 FOKPaintSettings::*Member,int32 Low,int32 High){
  Column->AddSlot().AutoHeight().Padding(3)[SNew(STextBlock).ColorAndOpacity(FLinearColor::White).Text_Lambda([Weak,Member,Name](){return FText::FromString(Weak.IsValid()?FString::Printf(TEXT("%s：%d"),Name,Weak->Settings.*Member):FString());})];
  Column->AddSlot().AutoHeight().Padding(3)[SNew(SSlider).Value_Lambda([Weak,Member,Low,High](){return Weak.IsValid()?float(Weak->Settings.*Member-Low)/(High-Low):0.f;}).OnValueChanged_Lambda([Weak,Member,Low,High](float V){if(Weak.IsValid())Weak->Settings.*Member=FMath::RoundToInt(FMath::Lerp(float(Low),float(High),V));})];
 };
 AddLimit(TEXT("水柱粒子上限"),&FOKPaintSettings::MaxJetParticles,32,4096);
 AddLimit(TEXT("飛濺效果上限（每個12粒）"),&FOKPaintSettings::MaxImpactEffects,1,64);
 AddLimit(TEXT("碰撞與附著液滴上限"),&FOKPaintSettings::MaxSimulationParticles,8,256);
 AddLimit(TEXT("地面污漬上限"),&FOKPaintSettings::MaxFloorStains,8,512);
 Add(TEXT("噴射速度（公分／秒）"),&FOKPaintSettings::JetSpeed,200,4000);Add(TEXT("流量（立方公分／秒）"),&FOKPaintSettings::Flow,0,60);Add(TEXT("散射角 °"),&FOKPaintSettings::ScatterDegrees,0,20);Add(TEXT("黏度"),&FOKPaintSettings::Viscosity,.1,20);Add(TEXT("表面流速"),&FOKPaintSettings::SurfaceSpeed,0,50);Add(TEXT("滴落厚度（公分）"),&FOKPaintSettings::DripThickness,.005,.25);Add(TEXT("附著時間 秒"),&FOKPaintSettings::AdhesionSeconds,0,5);Add(TEXT("滯留厚度（公分）"),&FOKPaintSettings::ResidualThickness,0,.02);Add(TEXT("表面更新頻率（赫茲）"),&FOKPaintSettings::SurfaceHz,5,60);
 Column->AddSlot().AutoHeight().Padding(3)[SNew(STextBlock).ColorAndOpacity(FLinearColor::White).Text_Lambda([Weak](){return FText::FromString(Weak.IsValid()?FString::Printf(TEXT("遮罩解析度：%d（清除後生效）"),Weak->Settings.AtlasResolution):FString());})];
 Column->AddSlot().AutoHeight().Padding(3)[SNew(SSlider).Value_Lambda([Weak](){return Weak.IsValid()?float(FMath::FloorLog2(uint32(Weak->Settings.AtlasResolution))-9)/3:0.f;}).OnValueChanged_Lambda([Weak](float V){if(Weak.IsValid())Weak->Settings.AtlasResolution=1<<FMath::Clamp(9+FMath::RoundToInt(V*3),9,12);})];
 for(int32 Channel=0;Channel<3;++Channel){Column->AddSlot().AutoHeight().Padding(3)[SNew(STextBlock).ColorAndOpacity(FLinearColor::White).Text(FText::FromString(FString::Printf(TEXT("油漆 %s"),Channel==0?TEXT("紅色"):Channel==1?TEXT("綠色"):TEXT("藍色"))))];Column->AddSlot().AutoHeight().Padding(3)[SNew(SSlider).Value_Lambda([Weak,Channel](){return Weak.IsValid()?Weak->Settings.Color.Component(Channel):1.f;}).OnValueChanged_Lambda([Weak,Channel](float V){if(Weak.IsValid())Weak->Settings.Color.Component(Channel)=V;})];}
 Column->AddSlot().AutoHeight().Padding(3)[SNew(SButton).Text(FText::FromString(TEXT("清除油漆（R）"))).OnClicked_Lambda([Weak](){if(Weak.IsValid())Weak->ClearPaint();return FReply::Handled();})];
 Column->AddSlot().AutoHeight().Padding(3)[SNew(STextBlock).ColorAndOpacity(FLinearColor::White).AutoWrapText(true).Text_Lambda([Weak](){return FText::FromString(Weak.IsValid()?Weak->Statistics():FString());})];
 Panel=SNew(SOverlay)+SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(350,4,0,0))[SNew(SBox).WidthOverride(280)[SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("油漆設定：A＋左鍵噴射／R 清除"))).ColorAndOpacity(FLinearColor::White)].BodyContent()[SNew(SBox).MaxDesiredHeight(640)[SNew(SScrollBox)+SScrollBox::Slot()[Column]]]]];
 GetWorld()->GetGameViewport()->AddViewportWidgetContent(Panel.ToSharedRef(),110);
}

void AOKPaintSprayer::SetControlsVisible(bool Visible){if(Panel)Panel->SetVisibility(Visible?EVisibility::SelfHitTestInvisible:EVisibility::Collapsed);}
