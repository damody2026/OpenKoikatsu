#include "OKGalleryWidget.h"
#include "Hash/xxhash.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "OKComboBox.h"
#include "OKShapeSlider.h"
#include "OKBoneControls.h"
#include "KKPreviewActor.h"
#include "ok_extract.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"

namespace {
FString BoneCategoryLabel(const FString& Key){
 static const TMap<FString,FString> Names={
 {TEXT("FaceHead"),TEXT("頭部")},{TEXT("FaceEar"),TEXT("耳朵")},{TEXT("FaceChin"),TEXT("下巴")},{TEXT("FaceCheek"),TEXT("臉頰")},
 {TEXT("FaceEyebrow"),TEXT("眉毛")},{TEXT("FaceEyes2"),TEXT("眼球")},{TEXT("FaceEye01"),TEXT("眼眶")},
 {TEXT("FaceEyelashUp"),TEXT("上眼線")},{TEXT("FaceEyelashDn"),TEXT("下眼線")},{TEXT("FaceNose"),TEXT("鼻子")},
 {TEXT("FaceMouth"),TEXT("嘴")},{TEXT("FaceMouth2"),TEXT("口腔")},{TEXT("BodyAll"),TEXT("全體")},
 {TEXT("BodyBreast"),TEXT("胸部")},{TEXT("BodyBreast2"),TEXT("胸型")},{TEXT("BodyNipples"),TEXT("乳頭")},
 {TEXT("BodyUpper"),TEXT("上半身")},{TEXT("BodyUpper2"),TEXT("胸腔")},{TEXT("BodyLower"),TEXT("下半身")},
 {TEXT("BodyLower2"),TEXT("腹腔")},{TEXT("BodyArm"),TEXT("上肢")},{TEXT("BodyArm2"),TEXT("上臂")},
 {TEXT("BodyForearms"),TEXT("前臂")},{TEXT("BodyHands"),TEXT("手掌與手指")},{TEXT("BodyLeg"),TEXT("腳")},
 {TEXT("BodyThighs"),TEXT("大腿")},{TEXT("BodyFeet"),TEXT("腳掌與小腿")},{TEXT("BodySkirtScl"),TEXT("裙子")},
 {TEXT("BodyGenitals"),TEXT("私密部位")},{TEXT("BodyUnderhair"),TEXT("陰毛")}};
 return TEXT("骨骼調整：")+(Names.FindRef(Key).IsEmpty()?Key:Names.FindRef(Key));
}
TArray<TSharedPtr<FJsonValue>> Vec3(float X,float Y,float Z){return {MakeShared<FJsonValueNumber>(X),MakeShared<FJsonValueNumber>(Y),MakeShared<FJsonValueNumber>(Z)};}
FString OriginalCategory(bool Body,int32 Index){
 if(Body){if(Index<2)return TEXT("BodyAll");if(Index<4||(Index>=14&&Index<=19))return TEXT("BodyUpper");if(Index<=10)return TEXT("BodyBreast2");if(Index<=13)return TEXT("BodyNipples");if(Index==21)return TEXT("BodyLower2");if(Index<=27)return TEXT("BodyLower");if(Index<=33)return TEXT("BodyThighs");if(Index<=36)return TEXT("BodyFeet");if(Index<=42)return TEXT("BodyArm2");return TEXT("BodyForearms");}
 if(Index<=5)return TEXT("FaceHead");if(Index<=13)return TEXT("FaceChin");if(Index<=18)return TEXT("FaceCheek");if(Index<=23)return TEXT("FaceEyebrow");if(Index<=37)return TEXT("FaceEyes2");if(Index<=40)return TEXT("FaceNose");if(Index<=46)return TEXT("FaceMouth2");return TEXT("FaceEar");
}
TSharedPtr<FJsonObject> DefaultData(){auto D=MakeShared<FJsonObject>();D->SetArrayField(TEXT("scale"),Vec3(1,1,1));D->SetNumberField(TEXT("length"),1);D->SetArrayField(TEXT("pos"),Vec3(0,0,0));D->SetArrayField(TEXT("rot"),Vec3(0,0,0));return D;}
}

FString UOKCharacterController::EditorModsJson() const {
 // Decoding the card's KKEx block (ABMX, MaterialEditor, ...) took up to ~40 ms, and every load, clothing warm-up and
 // cached preview asks for it: the last result is kept for identical bytes.
 TRACE_CPUPROFILER_EVENT_SCOPE(KK_GalleryEditorModsJson);
 static const TArray<uint8> Empty;const TArray<uint8>* Block=Selected.RawBlocks.Find(TEXT("KKEx"));if(!Block)Block=&Empty;
 static FXxHash64 LastHash;static int32 LastSize=-1;static FString LastJson;
 const FXxHash64 Hash=FXxHash64::HashBuffer(Block->GetData(),Block->Num());
 if(Block->Num()!=LastSize||Hash!=LastHash){LastJson=FOkSession::DecodeEditorMods(*Block);LastHash=Hash;LastSize=Block->Num();}
 return LastJson;
}
void UOKCharacterController::BuildBoneControls(){
 BoneSliders.Reset();
 BoneCategoryCombo=WidgetTree->ConstructWidget<UOKComboBoxString>();BoneCategoryCombo->AddOption(TEXT("原遊戲參數"));
 BoneCategoryCombo->AddOption(TEXT("骨骼調整：全部"));
 BoneAxesCheck=WidgetTree->ConstructWidget<UCheckBox>();auto AxesText=WidgetTree->ConstructWidget<UTextBlock>();AxesText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));AxesText->SetAutoWrapText(true);
 AxesText->SetText(FText::FromString(TEXT("進階：X／Y／Z、位置與旋轉")));BoneAxesCheck->SetContent(AxesText);
 BoneAxesCheck->OnCheckStateChanged.AddDynamic(this,&UOKCharacterController::OnBoneAxesChanged);
 for(const auto& Pair:{TPair<FString,FString>(TEXT("bustSoftness"),TEXT("胸部柔軟度")),TPair<FString,FString>(TEXT("bustWeight"),TEXT("胸部重量"))}){
  auto Row=WidgetTree->ConstructWidget<UHorizontalBox>();auto Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(128.f);Box->SetMinDesiredHeight(44.f);
  auto Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",16));Label->SetAutoWrapText(true);Label->SetWrapTextAt(128.f);Label->SetText(FText::FromString(Pair.Value));Box->SetContent(Label);Row->AddChild(Box);
  auto Slider=WidgetTree->ConstructWidget<UOKShapeSlider>();Slider->Owner=this;Slider->BodyParameter=Pair.Key;Slider->ModCategory=BoneCategoryLabel(TEXT("BodyBreast2"));Slider->ModRow=Row;
  Slider->SetMinValue(0);Slider->SetMaxValue(1);Slider->SetValue(.5f);Slider->OnValueChanged.AddDynamic(Slider,&UOKShapeSlider::OnSliderMoved);
  auto PhysicsSlot=Row->AddChildToHorizontalBox(Slider);PhysicsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));PhysicsSlot->SetVerticalAlignment(VAlign_Center);
  Slider->ValueLabel=WidgetTree->ConstructWidget<UTextBlock>();Slider->ValueLabel->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",16));Row->AddChild(Slider->ValueLabel);
  auto Reset=WidgetTree->ConstructWidget<UButton>();auto Text=WidgetTree->ConstructWidget<UTextBlock>();Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));Text->SetAutoWrapText(false);Text->SetText(FText::FromString(TEXT("重置")));Reset->SetContent(Text);Reset->OnClicked.AddDynamic(Slider,&UOKShapeSlider::OnResetClicked);Row->AddChild(Reset);
  ShapeBox->AddChild(Row);BoneSliders.Add(Slider);Slider->RefreshValueLabel();
 }
 for(const auto& Def:OKBoneControls){
  const FString Category=BoneCategoryLabel(Def.Category);
  if(BoneCategoryCombo->FindOptionIndex(Category)==INDEX_NONE)BoneCategoryCombo->AddOption(Category);
  auto Row=WidgetTree->ConstructWidget<UHorizontalBox>();
  auto LabelBox=WidgetTree->ConstructWidget<USizeBox>();LabelBox->SetWidthOverride(128.f);LabelBox->SetMinDesiredHeight(44.f);
  auto Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",16));Label->SetAutoWrapText(true);Label->SetWrapTextAt(128.f);Label->SetText(FText::FromString(Def.Label));
  Label->SetColorAndOpacity(FSlateColor(FLinearColor(1.f,.72f,.35f)));LabelBox->SetContent(Label);Row->AddChild(LabelBox);
  auto Slider=WidgetTree->ConstructWidget<UOKShapeSlider>();Slider->Owner=this;Slider->ModBone=Def.Bone;Slider->ModRight=Def.Right;
  Slider->ModAxis=Def.Axis;Slider->ModCategory=Category;Slider->ModCoordinateSpecific=FString(Def.Category)==TEXT("BodySkirtScl");
  Slider->ModRow=Row;Slider->SetMinValue(Def.Min);Slider->SetMaxValue(Def.Max);Slider->SetValue(Def.Axis>=4?0.f:1.f);
  Slider->SetSliderBarColor(FLinearColor(.35f,.85f,.9f));Slider->SetSliderHandleColor(FLinearColor(.45f,.92f,1));
  Slider->OnValueChanged.AddDynamic(Slider,&UOKShapeSlider::OnSliderMoved);
  auto RowSlot=Row->AddChildToHorizontalBox(Slider);RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));RowSlot->SetVerticalAlignment(VAlign_Center);
  auto ValueBox=WidgetTree->ConstructWidget<USizeBox>();ValueBox->SetWidthOverride(42.f);Slider->ValueLabel=WidgetTree->ConstructWidget<UTextBlock>();Slider->ValueLabel->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",16));
  Slider->ValueLabel->SetJustification(ETextJustify::Center);ValueBox->SetContent(Slider->ValueLabel);Row->AddChild(ValueBox);
  if(!Slider->ModRight.IsEmpty()){
   Slider->ModSide=WidgetTree->ConstructWidget<UOKComboBoxString>();for(const TCHAR* Side:{TEXT("雙側"),TEXT("左"),TEXT("右")})Slider->ModSide->AddOption(Side);
   Slider->ModSide->SetSelectedIndex(0);Slider->ModSide->OnSelectionChanged.AddDynamic(Slider,&UOKShapeSlider::OnModSideChanged);Row->AddChild(Slider->ModSide);
  }
  auto Reset=WidgetTree->ConstructWidget<UButton>();auto Text=WidgetTree->ConstructWidget<UTextBlock>();Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));Text->SetAutoWrapText(false);Text->SetText(FText::FromString(TEXT("重置")));Reset->SetContent(Text);
  Slider->ModReset=Reset;
  Reset->OnClicked.AddDynamic(Slider,&UOKShapeSlider::OnResetClicked);Row->AddChild(Reset);
  ShapeBox->AddChild(Row);BoneSliders.Add(Slider);Row->SetVisibility(ESlateVisibility::Collapsed);
 }
 BuildPregnancyControls();
 BoneCategoryCombo->SetSelectedIndex(0);BoneCategoryCombo->OnSelectionChanged.AddDynamic(this,&UOKCharacterController::OnBoneCategoryChanged);
}
void UOKCharacterController::OnBoneAxesChanged(bool Value){OnBoneCategoryChanged(FString(),ESelectInfo::Direct);}
void UOKCharacterController::OnBoneCategoryChanged(FString Value,ESelectInfo::Type Type){
 const FString Category=BoneCategoryCombo?BoneCategoryCombo->GetSelectedOption():TEXT("原遊戲參數");
 for(UWidget* Line:ShapeSeparators)if(Line)Line->SetVisibility(Category==TEXT("原遊戲參數")?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 for(UOKShapeSlider* Slider:ShapeSliders)if(Slider&&Slider->GetParent())Slider->GetParent()->SetVisibility((Category==TEXT("原遊戲參數")||Category==BoneCategoryLabel(OriginalCategory(Slider->bBody,Slider->Index)))?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 for(UOKShapeSlider* Slider:BoneSliders){
  if(!Slider->PregnancyParameter.IsEmpty())continue;
  if(!Slider->BodyParameter.IsEmpty()){Slider->ModRow->SetVisibility((Category==TEXT("原遊戲參數")||Category==Slider->ModCategory)?ESlateVisibility::Visible:ESlateVisibility::Collapsed);continue;}
  const bool Group=Category==TEXT("骨骼調整：全部")||Slider->ModCategory==Category;
  const bool Axes=BoneAxesCheck&&BoneAxesCheck->IsChecked();
  bool HasUniform=false;if(Slider->ModAxis>=0&&Slider->ModAxis<3)for(UOKShapeSlider* Other:BoneSliders)if(Other->ModBone==Slider->ModBone&&Other->ModAxis==-1){HasUniform=true;break;}
  const bool Show=Group&&(Slider->ModAxis==3||(Axes?Slider->ModAxis!=-1:Slider->ModAxis==-1||(Slider->ModAxis<3&&!HasUniform)));
  Slider->ModRow->SetVisibility(Show?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 }
 if(PregnancyBox)PregnancyBox->SetVisibility(Category==TEXT("腹部形變")?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 SyncBoneSliders();
}
void UOKCharacterController::SyncBoneSliders(){
 if(bSyncingBoneControls)return;TGuardValue<bool> Guard(bSyncingBoneControls,true);
 TSharedPtr<FJsonObject> Mods;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(EditorModsJson()),Mods))return;
 const TArray<TSharedPtr<FJsonValue>>* Bones=nullptr;if(!Mods->TryGetArrayField(TEXT("abm"),Bones))return;
 const TSharedPtr<FJsonObject>* Preg=nullptr;Mods->TryGetObjectField(TEXT("pregnancy"),Preg);
 if(Preg&&PregnancyEnabled){bool Enabled=true;(*Preg)->TryGetBoolField(TEXT("GameplayEnabled"),Enabled);PregnancyEnabled->SetIsChecked(Enabled);}
 TSet<FString> Available;
 if(PreviewActor&&PreviewActor->Session.IsOpen()){for(int32 I=0;I<PreviewActor->Session.SkelBoneCount();++I){OkBoneView Bone{};if(PreviewActor->Session.SkelBoneAt(I,Bone)&&Bone.name)Available.Add(UTF8_TO_TCHAR(Bone.name));}}
 TArray<UOKShapeSlider*> Usable,Unusable;
 for(UOKShapeSlider* Slider:BoneSliders){
  if(!Slider->BodyParameter.IsEmpty()){Slider->SetValue(Slider->BodyParameter==TEXT("bustSoftness")?Selected.Body.BustSoftness:Selected.Body.BustWeight);Slider->RefreshValueLabel();continue;}
  if(!Slider->PregnancyParameter.IsEmpty()){double Value=0.;if(Preg)(*Preg)->TryGetNumberField(Slider->PregnancyParameter,Value);Slider->SetValue(Value);Slider->RefreshValueLabel();continue;}
  const FString Name=Slider->ModSide&&Slider->ModSide->GetSelectedIndex()==2?Slider->ModRight:Slider->ModBone;
  const bool Both=Slider->ModSide&&Slider->ModSide->GetSelectedIndex()==0;
  const bool Supported=Available.IsEmpty()||(Available.Contains(Name)&&(!Both||Available.Contains(Slider->ModRight)));
  if(Slider->ModRow){
   const bool AnySide=Available.IsEmpty()||Available.Contains(Slider->ModBone)||(!Slider->ModRight.IsEmpty()&&Available.Contains(Slider->ModRight));
   // Keep the side selector usable when only one side exists in a custom rig.
   Slider->ModRow->SetIsEnabled(AnySide);Slider->SetIsEnabled(Supported);if(Slider->ModReset)Slider->ModReset->SetIsEnabled(Supported);
   (AnySide?Usable:Unusable).Add(Slider);
   if(auto Row=Cast<UPanelWidget>(Slider->ModRow))if(auto LabelBox=Cast<USizeBox>(Row->GetChildAt(0)))if(auto Label=Cast<UTextBlock>(LabelBox->GetContent()))
    Label->SetColorAndOpacity(FSlateColor(AnySide?FLinearColor(1.f,.72f,.35f):FLinearColor(.45f,.45f,.45f)));
   Slider->ModRow->SetToolTipText(FText::FromString(Supported?TEXT("雙側顯示左側數值；拖動會同步雙側。左右可各自調整。") : AnySide?TEXT("選擇模型現有的左側或右側後即可調整。") : TEXT("目前模型沒有此骨骼。")));
  }
  float Value=Slider->ModAxis>=4?0.f:1.f;
  for(const auto& B:*Bones){auto Bone=B->AsObject();if(!Bone.IsValid()||Bone->GetStringField(TEXT("name"))!=Name||Bone->GetIntegerField(TEXT("location"))>=10)continue;
   const auto& Coordinates=Bone->GetArrayField(TEXT("coord"));const int32 Coord=Coordinates.Num()==1?0:OutfitIndex;
   if(!Coordinates.IsValidIndex(Coord))break;auto D=Coordinates[Coord]->AsObject();if(!D.IsValid())break;
   if(Slider->ModAxis==3)Value=D->GetNumberField(TEXT("length"));else{const TCHAR* Field=Slider->ModAxis>=7?TEXT("rot"):Slider->ModAxis>=4?TEXT("pos"):TEXT("scale");const auto& Scale=D->GetArrayField(Field);const int32 Axis=Slider->ModAxis>=4?(Slider->ModAxis-4)%3:FMath::Max(0,Slider->ModAxis);if(Scale.Num()==3)Value=Scale[Axis]->AsNumber();}break;
  }
  Slider->SetValue(Value);Slider->RefreshValueLabel();
 }
 // Rows for bones this model lacks change nothing: keep them greyed out after the usable ones.
 FString Order;for(UOKShapeSlider* Slider:Unusable)Order+=FString::Printf(TEXT("%s|%s|%d;"),*Slider->ModBone,*Slider->ModRight,Slider->ModAxis);
 if(ShapeBox&&Order!=BoneUnusableOrder){
  BoneUnusableOrder=Order;
  for(UOKShapeSlider* Slider:Usable)ShapeBox->RemoveChild(Slider->ModRow);for(UOKShapeSlider* Slider:Unusable)ShapeBox->RemoveChild(Slider->ModRow);if(PregnancyBox)ShapeBox->RemoveChild(PregnancyBox);
  for(UOKShapeSlider* Slider:Usable)ShapeBox->AddChild(Slider->ModRow);for(UOKShapeSlider* Slider:Unusable)ShapeBox->AddChild(Slider->ModRow);if(PregnancyBox)ShapeBox->AddChild(PregnancyBox);
 }
}
bool UOKCharacterController::CommitEditorMods(const TSharedPtr<FJsonObject>& Mods){
 FString Json;FJsonSerializer::Serialize(Mods.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));TArray<uint8> Extended;
 if(!FOkSession::EncodeEditorMods(Selected.RawBlocks.FindRef(TEXT("KKEx")),Json,Extended)){
  if(InspectorText)InspectorText->SetText(FText::FromString(TEXT("模組資料版本或格式不支援，未覆寫角色資料。")));
  SyncBoneSliders();return false;
 }
 Selected.RawBlocks.Add(TEXT("KKEx"),MoveTemp(Extended));
 if(!Selected.Blocks.ContainsByPredicate([](const FKKBlockInfo& B){return B.Name==TEXT("KKEx");})){FKKBlockInfo B;B.Name=TEXT("KKEx");Selected.Blocks.Add(B);}
 bEditorModsDirty=true;
 return true;
}
void UOKCharacterController::OnBoneChanged(UOKShapeSlider* Slider,float Value){
 if(bSyncingBoneControls||!Slider||!FMath::IsFinite(Value)||InteractionWidget||InteractionScene||bEnterInteractionAfterLoad)return;
 TSharedPtr<FJsonObject> Mods;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(EditorModsJson()),Mods))return;
 auto Bones=Mods->GetArrayField(TEXT("abm"));TArray<FString> Names;
 const int32 Side=Slider->ModSide?Slider->ModSide->GetSelectedIndex():1;
 if(Side!=2)Names.Add(Slider->ModBone);if(Side!=1&&!Slider->ModRight.IsEmpty())Names.Add(Slider->ModRight);
 for(const auto& Name:Names){
  TSharedPtr<FJsonObject> Bone;for(const auto& B:Bones)if(B->AsObject()->GetStringField(TEXT("name"))==Name&&B->AsObject()->GetIntegerField(TEXT("location"))<10){Bone=B->AsObject();break;}
  if(!Bone.IsValid()){Bone=MakeShared<FJsonObject>();Bone->SetStringField(TEXT("name"),Name);Bone->SetNumberField(TEXT("location"),1);Bone->SetArrayField(TEXT("coord"),{MakeShared<FJsonValueObject>(DefaultData())});Bones.Add(MakeShared<FJsonValueObject>(Bone));}
  auto Coordinates=Bone->GetArrayField(TEXT("coord"));
  if(Slider->ModCoordinateSpecific&&Coordinates.Num()==1){const auto Initial=Coordinates[0];while(Coordinates.Num()<FMath::Max(7,OutfitIndex+1)){
    auto Copy=MakeShared<FJsonObject>();Copy->Values=Initial->AsObject()->Values;Coordinates.Add(MakeShared<FJsonValueObject>(Copy));}}
  const int32 Coord=Coordinates.Num()==1?0:OutfitIndex;while(Coordinates.Num()<=Coord)Coordinates.Add(MakeShared<FJsonValueObject>(DefaultData()));
  auto Data=Coordinates[Coord]->AsObject();
  if(Slider->ModAxis==3)Data->SetNumberField(TEXT("length"),Value);else{const TCHAR* Field=Slider->ModAxis>=7?TEXT("rot"):Slider->ModAxis>=4?TEXT("pos"):TEXT("scale");auto Scale=Data->GetArrayField(Field);if(Slider->ModAxis==-1)Scale=Vec3(Value,Value,Value);else Scale[Slider->ModAxis>=4?(Slider->ModAxis-4)%3:Slider->ModAxis]=MakeShared<FJsonValueNumber>(Value);Data->SetArrayField(Field,Scale);}
  Bone->SetArrayField(TEXT("coord"),Coordinates);
 }
 Mods->SetArrayField(TEXT("abm"),Bones);if(CommitEditorMods(Mods))SyncBoneSliders();
}
