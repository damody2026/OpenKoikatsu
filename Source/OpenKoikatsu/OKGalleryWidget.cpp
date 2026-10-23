#include "OKGalleryWidget.h"
#include "OKComboBox.h"
#include "HAL/IConsoleManager.h"
#include "OKInteractionScene.h"
#include "OKPaintSprayer.h"
#include "OpenKoikatsuPlayerController.h"
#include "Components/CheckBox.h"
#include "OKCardRowButton.h"
#include "OKCardListView.h"
#include "OKShapeSlider.h"
#include "OKShapeDefaults.h"
#include "OKEditorLabelsZh.h"
#include "OKMotionComboBox.h"
#include "OKVideoMotionPanel.h"
#include "OKMotionLabelZh.h"
#include "Brushes/SlateColorBrush.h"
#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
#include "KKRustExtract.h"
#include "KKSkeletalBuilder.h"
#include "KKAnimationCatalog.h"
#include "Async/Async.h"
#include "HAL/RunnableThread.h"
#include "Tasks/Task.h"
#include "Misc/FileHelper.h"
#include "KKShapeRows.h"
#include "Kismet/GameplayStatics.h"
#include "KKCardReader.h"
#include "KKCardWriter.h"
#include "KKCardWorkspace.h"
#include "KKAnimatedPng.h"
#include "OKCardPreviewCapture.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "KKGameRoot.h"
#include "KKModIndex.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/InvalidationBox.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "HAL/FileManager.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "TimerManager.h"
#include "HighResScreenshot.h"
#include "Components/PoseableMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Engine/Engine.h"
#include "Modules/ModuleManager.h"
#include "Blueprint/WidgetTree.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"

namespace
{
	TMap<FString,FString> StudioSourceLabels,StudioLabelKeys;
    bool GameSourceMatchesSex(const FString& Source,int32 Sex){
        FString Decoded=Source;for(const FString Prefix:{FString(TEXT("states:studio@")),FString(TEXT("states:controller@"))})if(Source.StartsWith(Prefix)){
            const FString Hex=Source.Mid(Prefix.Len());if(Hex.Len()%2!=0||Hex.Len()>65536)return false;TArray<uint8> Bytes;Bytes.SetNumZeroed(Hex.Len()/2+1);if(HexToBytes(Hex,Bytes.GetData())!=Hex.Len()/2)return false;Decoded=UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()));break;
        }
        return !Decoded.Contains(Sex==0?TEXT("h/anim/female/"):TEXT("h/anim/male/"));
    }
	FString GameSourceLabel(const FString& Source)
	{
		if(Source.StartsWith(TEXT("states:studio@"))) {
			if(const FString* Existing=StudioSourceLabels.Find(Source)) return *Existing;
			const FString Hex=Source.RightChop(14);TArray<uint8> Bytes;Bytes.SetNum(Hex.Len()/2);
			if(Hex.Len()%2==0&&HexToBytes(Hex,Bytes.GetData())==Bytes.Num()) {
				const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()),Bytes.Num());
				TArray<TSharedPtr<FJsonValue>> Values;
				if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Decoded.Length(),Decoded.Get())),Values)&&Values.Num()==4) {
					const FString Owner=Values[3]->AsString().IsEmpty()?Values[1]->AsString():Values[3]->AsString();
					const FString Base=TEXT("工作室：")+Owner;FString Label=Base;int32 Suffix=2;
					while(StudioLabelKeys.Contains(Label)&&StudioLabelKeys[Label]!=Source) Label=Base+FString::Printf(TEXT(" (%d)"),Suffix++);
					StudioSourceLabels.Add(Source,Label);StudioLabelKeys.Add(Label,Source);return Label;
				}
			}
		}
		const FString Role=Source.Contains(TEXT("h/anim/male/"))?TEXT("男角："):(Source.Contains(TEXT("h/anim/female/"))?TEXT("女角："):TEXT(""));
		return Role+(Source.StartsWith(TEXT("states:"))
			? TEXT("控制器狀態：") + Source.RightChop(7) : Source);
	}

	FString GameSourceKey(const FString& Label)
	{
		if(const FString* Source=StudioLabelKeys.Find(Label)) return *Source;
		FString Value=Label;
		if(Value.StartsWith(TEXT("男角："))||Value.StartsWith(TEXT("女角："))) Value=Value.RightChop(3);
		const FString Prefix = TEXT("控制器狀態：");
		return Value.StartsWith(Prefix) ? TEXT("states:") + Value.RightChop(Prefix.Len()) : Value;
	}

	UTexture2D* TextureFromPng(const TArray<uint8>& Png)
	{
		const TArray<uint8> Preview = UKKCardWorkspace::PngPreviewBytes(Png);
		if (Png.Num() < 16)
		{
			return nullptr;
		}
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Preview.GetData(), Preview.Num()))
		{
			return nullptr;
		}
		TArray<uint8> Raw;
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
		{
			return nullptr;
		}
		UTexture2D* Tex = UTexture2D::CreateTransient(Wrapper->GetWidth(), Wrapper->GetHeight(), PF_B8G8R8A8);
		if (!Tex)
		{
			return nullptr;
		}
		void* Mip = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Mip, Raw.GetData(), Raw.Num());
		Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
		Tex->UpdateResource();
		return Tex;
	}

	FString KindName(EKKCardKind Kind)
	{
		switch (Kind)
		{
		case EKKCardKind::KKS: return TEXT("KKS");
		case EKKCardKind::KK: return TEXT("KK");
		case EKKCardKind::KKSP: return TEXT("KKSP");
		default: return TEXT("?");
		}
	}
}

TSharedRef<SWidget> UOKGalleryWidget::RebuildWidget()
{
	if (!WidgetTree)
	{
		return Super::RebuildWidget();
	}

	RootBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Root"));
    // The large reader control tree is static between input events. UMG
    // setters invalidate changed text, sliders, visibility and popup layout;
    // cache the remaining prepass/paint instead of traversing it every frame.
    auto* ControlsCache=WidgetTree->ConstructWidget<UInvalidationBox>(UInvalidationBox::StaticClass(),TEXT("ReaderControlsCache"));
    ControlsCache->SetCanCache(true);
    ControlsCache->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ControlsCache->SetContent(RootBox);
    WidgetTree->RootWidget = ControlsCache;
	RootBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);


	ShapePanel = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ShapeSize"));
	USizeBox* ShapeSize = ShapePanel;
	ShapeSize->SetWidthOverride(360.f);
	UBorder* ShapeBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ShapeBorder"));
	ShapeBorder->SetBrushColor(FLinearColor(0.12f, 0.16f, 0.18f, 0.92f));
	UVerticalBox* ShapeCol = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ShapeCol"));
	UTextBlock* ShapeTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShapeTitle"));
	ShapeTitle->SetText(FText::FromString(TEXT("身體／臉部參數")));
	// Body/face sliders and animation controls scroll independently on each side.
	MotionPanel = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("MotionSize"));
	MotionPanel->SetWidthOverride(360.f);
	UBorder* MotionBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MotionBorder"));
	MotionBorder->SetBrushColor(FLinearColor::FromSRGBColor(FColor(24, 32, 42)));
	MotionBorder->SetPadding(FMargin(8.f));
	ShapeBorder->SetPadding(FMargin(8.f));
	UVerticalBox* MotionFrame = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MotionFrame"));
	UTextBlock* MotionTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MotionTitle"));
	MotionTitle->SetText(FText::FromString(TEXT("動作與表情")));
	MotionFrame->AddChildToVerticalBox(MotionTitle)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	UScrollBox* MotionScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("MotionScroll"));
	HandMotionScroll=MotionScroll;
	MotionScroll->SetAlwaysShowScrollbar(true);
	MotionScroll->SetScrollbarThickness(FVector2D(10.f, 10.f));
	UVerticalBox* MotionCol = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MotionCol"));
	MotionScroll->AddChild(MotionCol);
	MotionFrame->AddChildToVerticalBox(MotionScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	MotionBorder->AddChild(MotionFrame);
	MotionPanel->AddChild(MotionBorder);
	UHorizontalBox* PoseNav = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PoseNav"));
	auto StylePoseButton = [](UButton* Btn)
	{
		FButtonStyle Style = Btn->GetStyle();
		Style.Normal.TintColor = FSlateColor(FLinearColor(0.16f, 0.38f, 0.48f, 1.f));
		Style.Hovered.TintColor = FSlateColor(FLinearColor(0.24f, 0.52f, 0.62f, 1.f));
		Style.Pressed.TintColor = FSlateColor(FLinearColor(0.10f, 0.28f, 0.36f, 1.f));
		Btn->SetStyle(Style);
	};
	auto StyleMotionButton = [&](UButton* Btn)
	{
		FButtonStyle Style = Btn->GetStyle();
		Style.Normal = FSlateColorBrush(FLinearColor::FromSRGBColor(FColor(24, 82, 132)));
		Style.Hovered = FSlateColorBrush(FLinearColor::FromSRGBColor(FColor(30, 100, 158)));
		Style.Pressed = FSlateColorBrush(FLinearColor::FromSRGBColor(FColor(18, 60, 100)));
		Btn->SetStyle(Style);
		if (UTextBlock* Label = Cast<UTextBlock>(Btn->GetContent()))
		{
			Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = 16;
			Label->SetFont(Font);
		}
	};
	UButton* BtnT = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("PoseT"));
	UButton* BtnW = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("PoseW"));
	UTextBlock* TxtT = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	UTextBlock* TxtW = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	TxtT->SetText(FText::FromString(TEXT("Ｔ字站姿")));
	TxtW->SetText(FText::FromString(TEXT("走路")));
	TxtT->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	TxtW->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	BtnT->AddChild(TxtT);
	BtnW->AddChild(TxtW);
	StyleMotionButton(BtnT);
	StyleMotionButton(BtnW);
	BtnT->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnPoseTpose);
	BtnW->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnPoseWalk);
	UHorizontalBox* LoadNav = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("LoadNav"));
	ShapeLoadButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ShapeLoad3D"));
	Load3DLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShapeLoad3DText"));
	Load3DLabel->SetText(FText::FromString(TEXT("載入3D")));
	Load3DLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ShapeLoadButton->AddChild(Load3DLabel);
	StylePoseButton(ShapeLoadButton);
	ShapeLoadButton->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnLoad3D);
	UButton* BackBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("BackGallery"));
	UTextBlock* BackText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BackGalleryText"));
	BackText->SetText(FText::FromString(TEXT("返回")));
	BackText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	BackBtn->AddChild(BackText);
	StylePoseButton(BackBtn);
	BackBtn->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnBackToGallery);
	USizeBox* BoxLoad = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	USizeBox* BoxBack = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	BoxLoad->SetHeightOverride(36.f);
	BoxBack->SetHeightOverride(36.f);
	BoxLoad->AddChild(ShapeLoadButton);
	BoxBack->AddChild(BackBtn);
	UHorizontalBoxSlot* SlotLoad = LoadNav->AddChildToHorizontalBox(BoxLoad);
	UHorizontalBoxSlot* SlotBack = LoadNav->AddChildToHorizontalBox(BoxBack);
	SlotLoad->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SlotBack->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SlotLoad->SetPadding(FMargin(4.f, 4.f));
	SlotBack->SetPadding(FMargin(4.f, 4.f));
	USizeBox* BoxT = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	USizeBox* BoxW = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	BoxT->SetHeightOverride(36.f);
	BoxW->SetHeightOverride(36.f);
	BoxT->AddChild(BtnT);
	BoxW->AddChild(BtnW);
	UHorizontalBoxSlot* SlotT = PoseNav->AddChildToHorizontalBox(BoxT);
	UHorizontalBoxSlot* SlotW = PoseNav->AddChildToHorizontalBox(BoxW);
	SlotT->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SlotW->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SlotT->SetPadding(FMargin(4.f, 4.f));
	SlotW->SetPadding(FMargin(4.f, 4.f));
	ShapeScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ShapeScroll"));
	ShapeScroll->SetAlwaysShowScrollbar(true);
	ShapeScroll->SetAlwaysShowScrollbarTrack(true);
	ShapeScroll->SetOrientation(EOrientation::Orient_Vertical);
	ShapeScroll->SetScrollbarThickness(FVector2D(10.f, 10.f));
	ShapeBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ShapeBox"));
	ShapeSliders.Reset();
 ShapeSeparators.Reset();
	const FLinearColor LineCol(0.25f, 0.72f, 0.92f, 0.85f);
	auto AddSep = [&]()
	{
		UBorder* Line = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Line->SetBrushColor(LineCol);
		USizeBox* LineBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		LineBox->SetHeightOverride(2.f);
		LineBox->AddChild(Line);
 ShapeSeparators.Add(LineBox);
		UVerticalBoxSlot* LSlot = ShapeBox->AddChildToVerticalBox(LineBox);
		LSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 6.f));
	};
	auto AddSlider = [&](const TCHAR* Label, int32 Index, bool bBody)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		USizeBox* LabSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		LabSize->SetWidthOverride(128.f);LabSize->SetMinDesiredHeight(36.f);
		UTextBlock* Lab = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Lab->SetText(FText::FromString(Label));
 Lab->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));Lab->SetAutoWrapText(true);Lab->SetWrapTextAt(128.f);
		Lab->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		LabSize->AddChild(Lab);
		UOKShapeSlider* Slider = WidgetTree->ConstructWidget<UOKShapeSlider>(UOKShapeSlider::StaticClass());
		Slider->SetMinValue(-1.0f);
		Slider->SetMaxValue(2.0f);
		Slider->SetValue(0.5f);
		Slider->SetSliderBarColor(FLinearColor(0.35f, 0.85f, 0.9f));
		Slider->SetSliderHandleColor(FLinearColor(0.45f, 0.92f, 1.f));
		Slider->Index = Index;
		Slider->bBody = bBody;
		Slider->Owner = this;
		Slider->OnValueChanged.AddDynamic(Slider, &UOKShapeSlider::OnSliderMoved);
		USizeBox* ValSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ValSize->SetWidthOverride(40.f);
		ValSize->SetHeightOverride(22.f);
		UBorder* ValBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		ValBorder->SetBrushColor(FLinearColor(0.08f, 0.22f, 0.38f, 1.f));
		UTextBlock* Val = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Val->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));
 Val->SetJustification(ETextJustify::Center);
		Val->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Slider->ValueLabel = Val;
		Slider->RefreshValueLabel();
		ValBorder->AddChild(Val);
		ValSize->AddChild(ValBorder);
		Row->AddChild(LabSize);
		UHorizontalBoxSlot* SSlot = Row->AddChildToHorizontalBox(Slider);
		SSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SSlot->SetVerticalAlignment(VAlign_Center);
		SSlot->SetPadding(FMargin(4.f, 2.f, 6.f, 2.f));
		UHorizontalBoxSlot* VSlot = Row->AddChildToHorizontalBox(ValSize);
		VSlot->SetVerticalAlignment(VAlign_Center);
		VSlot->SetPadding(FMargin(0.f, 2.f, 4.f, 2.f));
		UButton* Reset = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* ResetLabel = WidgetTree->ConstructWidget<UTextBlock>();
		ResetLabel->SetText(FText::FromString(TEXT("重置")));
		ResetLabel->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 12));
		ResetLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Reset->SetContent(ResetLabel);
		StylePoseButton(Reset);
		Reset->SetToolTipText(FText::FromString(TEXT("將此參數重置為原遊戲相同性別角色的預設值。")));
		Reset->OnClicked.AddDynamic(Slider, &UOKShapeSlider::OnResetClicked);
		UHorizontalBoxSlot* ResetSlot = Row->AddChildToHorizontalBox(Reset);
		ResetSlot->SetVerticalAlignment(VAlign_Center);
		ResetSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
		ShapeBox->AddChild(Row);
		ShapeSliders.Add(Slider);
	};
	for (const FKKShapeRow& R : KKShapeRows())
	{
		AddSlider(R.Label, R.Index, R.bBody);
		if (R.bSep)
		{
			AddSep();
		}
	}
	BuildBoneControls();
	ShapeScroll->AddChild(ShapeBox);
	UButton* ResetAll = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ResetAllShapes"));
	UTextBlock* ResetAllLabel = WidgetTree->ConstructWidget<UTextBlock>();
	ResetAllLabel->SetText(FText::FromString(TEXT("重設到預設值")));
	ResetAllLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ResetAll->SetContent(ResetAllLabel);
	StylePoseButton(ResetAll);
	ResetAll->SetToolTipText(FText::FromString(TEXT("一次重設全部身體與臉部參數，使用原遊戲相同性別角色的預設值。")));
	ResetAll->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnResetAllShapes);
	ShapeCol->AddChildToVerticalBox(ResetAll)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	ShapeCol->AddChild(LoadNav);
	UButton* InteractionButton=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("EditorInteraction"));
	UTextBlock* InteractionLabel=WidgetTree->ConstructWidget<UTextBlock>();
	InteractionLabel->SetText(FText::FromString(TEXT("進入1對1互動場景")));InteractionButton->SetContent(InteractionLabel);StylePoseButton(InteractionButton);
	InteractionButton->SetToolTipText(FText::FromString(TEXT("開啟獨立的一對一互動場景，返回後繼續人物編輯。")));
	InteractionButton->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnEnterInteraction);ShapeCol->AddChild(InteractionButton);
	UButton* StudioButton=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("EditorStudio"));
	UTextBlock* StudioLabel=WidgetTree->ConstructWidget<UTextBlock>();
	StudioLabel->SetText(FText::FromString(TEXT("進入工作室")));StudioButton->SetContent(StudioLabel);StylePoseButton(StudioButton);
	StudioButton->SetToolTipText(FText::FromString(TEXT("OK Studio：以目前的角色開新場景，可加入角色、道具、燈光、相機，用操作軸擺放並存成場景檔。")));
	StudioButton->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnEnterOkStudio);ShapeCol->AddChild(StudioButton);
	UButton* TimelineButton=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("EditorStudioTimeline"));
	UTextBlock* TimelineLabel=WidgetTree->ConstructWidget<UTextBlock>();
	TimelineLabel->SetText(FText::FromString(TEXT("動作時間軸")));TimelineButton->SetContent(TimelineLabel);StylePoseButton(TimelineButton);
	TimelineButton->SetToolTipText(FText::FromString(TEXT("以操作軸移動／旋轉／縮放角色，在時間軸上打關鍵幀，並可匯入影片產生動作。")));
	TimelineButton->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnEnterStudio);ShapeCol->AddChild(TimelineButton);
	UButton* ModelApngSave=WidgetTree->ConstructWidget<UButton>();
	UTextBlock* ModelApngLabel=WidgetTree->ConstructWidget<UTextBlock>();
	ModelApngLabel->SetText(FText::FromString(TEXT("另存新檔")));
	ModelApngSave->AddChild(ModelApngLabel);StylePoseButton(ModelApngSave);
	ModelApngSave->SetToolTipText(FText::FromString(TEXT("另存 APNG 新卡：原卡圖 3 秒，加上 T-pose 三視圖各 1 秒。")));
	ModelApngSave->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnSaveApng);
	ShapeCol->AddChild(ModelApngSave);
	OutfitCombo = WidgetTree->ConstructWidget<UOKComboBoxString>(UOKComboBoxString::StaticClass(), TEXT("OutfitCombo"));
	OutfitCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnOutfitSelected);
	USizeBox* OutfitBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OutfitBox"));
	OutfitBox->SetWidthOverride(180.f);
	OutfitBox->SetHeightOverride(32.f);
	OutfitBox->AddChild(OutfitCombo);
	ShapeCol->AddChild(OutfitBox);
	auto* HandsButton=WidgetTree->ConstructWidget<UButton>();auto* HandsLabel=WidgetTree->ConstructWidget<UTextBlock>();HandsLabel->SetText(FText::FromString(TEXT("左右手動作")));HandsButton->SetContent(HandsLabel);StylePoseButton(HandsButton);HandsButton->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnOpenHandControls);ShapeCol->AddChild(HandsButton);
	UTextBlock* HandTitle=WidgetTree->ConstructWidget<UTextBlock>();
	HandTitle->SetText(FText::FromString(TEXT("左右手動作（獨立設定）")));MotionCol->AddChild(HandTitle);
	HandCombos.Reset();
	HandBlendSliders.Reset();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(FText::FromString(Side == 0 ? TEXT("左手動作甲 → 乙") : TEXT("右手動作甲 → 乙")));
		MotionCol->AddChild(Label);
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		for (int32 End = 0; End < 2; ++End)
		{
			UComboBoxString* Combo = WidgetTree->ConstructWidget<UOKComboBoxString>(UOKComboBoxString::StaticClass());
			Row->AddChildToHorizontalBox(Combo)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HandCombos.Add(Combo);
		}
		MotionCol->AddChild(Row);
		USlider* Blend = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
		Blend->SetMinValue(0.f); Blend->SetMaxValue(1.f); Blend->SetValue(SelectedHandBlend[Side]);
		Blend->SetToolTipText(FText::FromString(TEXT("姿勢混合：0 為甲，1 為乙")));
		MotionCol->AddChild(Blend); HandBlendSliders.Add(Blend);
	}
	HandCombos[0]->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnLeftHandA);
	HandCombos[1]->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnLeftHandB);
	HandCombos[2]->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnRightHandA);
	HandCombos[3]->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnRightHandB);
	HandBlendSliders[0]->OnValueChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnLeftHandBlend);
	HandBlendSliders[1]->OnValueChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnRightHandBlend);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		auto* Reset = WidgetTree->ConstructWidget<UButton>();
		auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Side == 0 ? TEXT("左手跟隨動作") : TEXT("右手跟隨動作")));
		Label->SetAutoWrapText(true);
		Reset->SetContent(Label);
		StylePoseButton(Reset);
		MotionCol->AddChild(Reset);
		if (Side == 0) Reset->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnResetLeftHand);
		else Reset->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnResetRightHand);
	}
	RefreshHandControls();
	MotionCol->AddChild(PoseNav);
	{
		UTextBlock* ClothesTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ClothesStateTitle"));
		ClothesTitle->SetText(FText::FromString(TEXT("衣服")));
		ClothesTitle->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		ClothesTitle->SetAutoWrapText(true);
		ClothesTitle->SetToolTipText(FText::FromString(TEXT("穿上、半脫、全脫會按遊戲的衣服狀態重載角色。沒有對應網格的單品會自動跳到下一個可用狀態。全脫拿掉上衣、下裝、胸罩和內褲，身體遮罩打開後才看得到解碼的身體。")));
		MotionCol->AddChild(ClothesTitle);
		UHorizontalBox* Bulk = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ClothesBulk"));
		const TCHAR* BulkLabels[] = {TEXT("穿上"), TEXT("半脫"), TEXT("全脫")};
		const int32 BulkKinds[] = {100, 101, 102};
		ClothesStateButtons.Reset();
		ClothesStateLabels.Reset();
		for (int32 Index = 0; Index < 3; ++Index)
		{
			UOKClothesStateButton* Button = WidgetTree->ConstructWidget<UOKClothesStateButton>(UOKClothesStateButton::StaticClass());
			Button->Kind = BulkKinds[Index];
			Button->Owner = this;
			Button->OnClicked.AddDynamic(Button, &UOKClothesStateButton::HandleClick);
			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
			Label->SetText(FText::FromString(BulkLabels[Index]));
			Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Button->AddChild(Label);
			StyleMotionButton(Button);
			Bulk->AddChildToHorizontalBox(Button)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			ClothesStateButtons.Add(Button);
		}
		MotionCol->AddChild(Bulk);
		const TCHAR* SlotNames[] = {TEXT("上衣"), TEXT("下裝"), TEXT("胸罩"), TEXT("內褲"), TEXT("手套"), TEXT("褲襪"), TEXT("襪子"), TEXT("內鞋"), TEXT("外鞋")};
		for (int32 Kind = 0; Kind < 9; ++Kind)
		{
			UOKClothesStateButton* Button = WidgetTree->ConstructWidget<UOKClothesStateButton>(UOKClothesStateButton::StaticClass(), *FString::Printf(TEXT("ClothesState%d"), Kind));
			Button->Kind = Kind;
			Button->Owner = this;
			Button->OnClicked.AddDynamic(Button, &UOKClothesStateButton::HandleClick);
			Button->SetToolTipText(FText::FromString(FString::Printf(TEXT("切換%s：穿著、半脫、再半、全脫。"), SlotNames[Kind])));
			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
			Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Button->AddChild(Label);
			StyleMotionButton(Button);
			ClothesStateLabels.Add(Label);
			ClothesStateButtons.Add(Button);
			MotionCol->AddChild(Button);
		}
		RefreshClothesStateLabels();
	}
	UTextBlock* AnimationTitle=WidgetTree->ConstructWidget<UTextBlock>();
	AnimationTitle->SetText(FText::FromString(TEXT("選擇動畫")));
	MotionCol->AddChild(AnimationTitle);
	// Retain source controls only for internal synchronization and diagnostic
	// commands. They are not attached to the editor's visible control tree.
	GameSourceCombo = WidgetTree->ConstructWidget<UOKMotionComboBox>(UOKMotionComboBox::StaticClass(), TEXT("GameSourceCombo"));
	GameSourceCombo->AddOption(TEXT("基本動作"));
	GameSourceCombo->SetSelectedIndex(0);
	GameSourceCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnGameSourceSelected);
	GameSourceCombo->SetVisibility(ESlateVisibility::Collapsed);
	GameClipCombo = WidgetTree->ConstructWidget<UOKMotionComboBox>(UOKMotionComboBox::StaticClass(), TEXT("GameClipCombo"));
	GameClipCombo->AddOption(TEXT("選擇遊戲動作"));
	GameClipCombo->SetSelectedIndex(0);
	GameClipCombo->SetToolTipText(FText::FromString(TEXT("搜尋並選擇動畫即可播放，會自動載入對應的動畫檔。")));
	GameClipCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnGameClipSelected);
	MotionCol->AddChild(GameClipCombo);
	AnimationStatus=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("AnimationStatus"));
	AnimationStatus->SetAutoWrapText(true);MotionCol->AddChild(AnimationStatus);
	ShowAnimationStatus(TEXT("直接選擇上方的遊戲動作，會自動載入對應來源。"));
	// User video motions: own searchable list, import and delete (the built-in list above stays native-only).
	VideoMotionPanel=NewObject<UOKVideoMotionPanel>(this);
	VideoMotionPanel->Build(WidgetTree,MotionCol,static_cast<UOKCharacterController*>(this));
	StudioCategoryCombo = WidgetTree->ConstructWidget<UOKMotionComboBox>(UOKMotionComboBox::StaticClass(), TEXT("StudioCategoryCombo"));
	StudioCategoryCombo->AddOption(TEXT("工作室動作分類"));
	StudioCategoryCombo->SetSelectedIndex(0);
	StudioCategoryCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnStudioCategorySelected);
	MotionCol->AddChild(StudioCategoryCombo);
	StudioMotionCombo = WidgetTree->ConstructWidget<UOKMotionComboBox>(UOKMotionComboBox::StaticClass(), TEXT("StudioMotionCombo"));
	StudioMotionCombo->AddOption(TEXT("選擇工作室動作"));
	StudioMotionCombo->SetSelectedIndex(0);
	StudioMotionCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnStudioMotionSelected);
	MotionCol->AddChild(StudioMotionCombo);
	GameSceneCombo = WidgetTree->ConstructWidget<UOKMotionComboBox>(UOKMotionComboBox::StaticClass(), TEXT("GameSceneCombo"));
	GameSceneCombo->AddOption(TEXT("場景校正：依動作來源"));
	GameSceneCombo->AddOption(TEXT("場景校正：角色編輯器"));
	GameSceneCombo->AddOption(TEXT("場景校正：一般遊戲"));
	GameSceneCombo->AddOption(TEXT("場景校正：親密互動／美容"));
	GameSceneCombo->AddOption(TEXT("場景校正：工作室"));
	GameSceneCombo->SetSelectedIndex(0);
	GameSceneCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnGameSceneSelected);
	MotionCol->AddChild(GameSceneCombo);
	GameSourceInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("GameSourceInput"));
	GameSourceInput->SetHintText(FText::FromString(TEXT("abdata/...unity3d 或 mods/...zipmod!...unity3d")));
	GameSourceInput->SetVisibility(ESlateVisibility::Collapsed);
    auto PlaybackButton=[&](const TCHAR* Name,const TCHAR* Label){UButton* B=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),Name);UTextBlock* T=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());T->SetText(FText::FromString(Label));B->AddChild(T);StyleMotionButton(B);MotionCol->AddChild(B);return B;};
    PlaybackButton(TEXT("PausePlayback"),TEXT("暫停／繼續"))->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnPausePlayback);
    PlaybackButton(TEXT("ReplayPlayback"),TEXT("從頭重播"))->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnReplayPlayback);
    UTextBlock* PartnerMaleText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());PartnerMaleText->SetText(FText::FromString(TEXT("配對男角（點名字）")));PartnerMaleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));MotionCol->AddChild(PartnerMaleText);
    PartnerMaleCombo=WidgetTree->ConstructWidget<UOKMotionComboBox>(UOKMotionComboBox::StaticClass(),TEXT("PartnerMale"));PartnerMaleCombo->AddOption(TEXT("選擇男角"));PartnerMaleCombo->SetSelectedIndex(0);PartnerMaleCombo->SetToolTipText(FText::FromString(TEXT("也可以從這裡選男角。下面的名字按鈕同樣有效。")));PartnerMaleCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnPartnerMaleSelected);MotionCol->AddChild(PartnerMaleCombo);
    PartnerMaleList=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("PartnerMaleList"));MotionCol->AddChild(PartnerMaleList);
    PlaybackButton(TEXT("LoadPartner"),TEXT("載入配對角色"))->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnLoadPartner);
    PairStatus=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("PairStatus"));PairStatus->SetAutoWrapText(true);PairStatus->SetColorAndOpacity(FSlateColor(FLinearColor(1.f,.92f,.45f)));PairStatus->SetText(FText::FromString(TEXT("先點一位男角，再按載入配對角色。")));MotionCol->AddChild(PairStatus);
    UTextBlock* PairPoseText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());PairPoseText->SetText(FText::FromString(TEXT("做愛姿勢")));PairPoseText->SetColorAndOpacity(FSlateColor(FLinearColor::White));MotionCol->AddChild(PairPoseText);
    PairPreparationStatus=WidgetTree->ConstructWidget<UTextBlock>();PairPreparationStatus->SetAutoWrapText(true);MotionCol->AddChild(PairPreparationStatus);
    PairCategoryCombo=WidgetTree->ConstructWidget<UOKComboBoxString>();PairCategoryCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnPairCategorySelected);MotionCol->AddChild(PairCategoryCombo);
    PairCombo=WidgetTree->ConstructWidget<UOKMotionComboBox>(UOKMotionComboBox::StaticClass(),TEXT("NativePair"));PairCombo->AddOption(TEXT("選擇姿勢"));PairCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnPairSelected);MotionCol->AddChild(PairCombo);
    UHorizontalBox* PairPoseNav=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("PairPoseNav"));
    UButton* PairPrev=PlaybackButton(TEXT("PairPosePrev"),TEXT("上一個姿勢"));PairPrev->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnPairPosePrev);
    UButton* PairNext=PlaybackButton(TEXT("PairPoseNext"),TEXT("下一個姿勢"));PairNext->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnPairPoseNext);
    MotionCol->RemoveChild(PairPrev);MotionCol->RemoveChild(PairNext);PairPoseNav->AddChild(PairPrev);PairPoseNav->AddChild(PairNext);MotionCol->AddChild(PairPoseNav);
    UTextBlock* PairPhaseText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());PairPhaseText->SetText(FText::FromString(TEXT("動作階段")));MotionCol->AddChild(PairPhaseText);
    PairStateCombo=WidgetTree->ConstructWidget<UOKMotionComboBox>(UOKMotionComboBox::StaticClass(),TEXT("NativePairState"));PairStateCombo->AddOption(TEXT("選擇階段"));PairStateCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnPairStateSelected);MotionCol->AddChild(PairStateCombo);
    UTextBlock* PairSpeedText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());PairSpeedText->SetText(FText::FromString(TEXT("雙人時鐘速度（0–4 倍）")));MotionCol->AddChild(PairSpeedText);
    PairSpeedSlider=WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(),TEXT("PairSpeed"));PairSpeedSlider->SetValue(.25f);PairSpeedSlider->OnValueChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnPairSpeed);MotionCol->AddChild(PairSpeedSlider);
    {UTextBlock* PhysicsText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());PhysicsText->SetText(FText::FromString(TEXT("物理系統（即時切換）")));MotionCol->AddChild(PhysicsText);
     PhysicsBackendCombo=WidgetTree->ConstructWidget<UOKComboBoxString>(UOKComboBoxString::StaticClass(),TEXT("PhysicsBackend"));FillPhysicsBackendCombo();PhysicsBackendCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnPhysicsBackendSelected);MotionCol->AddChild(PhysicsBackendCombo);
     UTextBlock* SoftText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());SoftText->SetText(FText::FromString(TEXT("頭髮／衣物硬度（1 最軟 → 10 最硬＝原設定）")));MotionCol->AddChild(SoftText);
     PhysicsSoftnessSlider=WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(),TEXT("PhysicsSoftness"));if(IConsoleVariable* Soft=IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.Hardness"))){PhysicsSoftnessSlider->SetMinValue(1.f);PhysicsSoftnessSlider->SetMaxValue(10.f);PhysicsSoftnessSlider->SetStepSize(1.f);PhysicsSoftnessSlider->SetValue(Soft->GetFloat());}PhysicsSoftnessSlider->OnValueChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnPhysicsSoftness);MotionCol->AddChild(PhysicsSoftnessSlider);
     UTextBlock* WeightText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());WeightText->SetText(FText::FromString(TEXT("頭髮／衣物重量（1 原設定 → 10 加上完整重力）")));MotionCol->AddChild(WeightText);
     PhysicsWeightSlider=WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(),TEXT("PhysicsWeight"));if(IConsoleVariable* W=IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.Weight"))){PhysicsWeightSlider->SetMinValue(1.f);PhysicsWeightSlider->SetMaxValue(10.f);PhysicsWeightSlider->SetStepSize(1.f);PhysicsWeightSlider->SetValue(W->GetFloat());}PhysicsWeightSlider->OnValueChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnPhysicsWeight);MotionCol->AddChild(PhysicsWeightSlider);
     UTextBlock* TailText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());TailText->SetText(FText::FromString(TEXT("尾巴彈性（0 原設定 → 1 預設 → 2 最彈）")));MotionCol->AddChild(TailText);
     PhysicsTailSlider=WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(),TEXT("PhysicsTail"));if(IConsoleVariable* Tail=IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.TailBounce"))){PhysicsTailSlider->SetMinValue(0.f);PhysicsTailSlider->SetMaxValue(2.f);PhysicsTailSlider->SetStepSize(.1f);PhysicsTailSlider->SetValue(Tail->GetFloat());}PhysicsTailSlider->OnValueChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnPhysicsTail);MotionCol->AddChild(PhysicsTailSlider);}
    PlaybackButton(TEXT("StopPair"),TEXT("結束雙人播放"))->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnStopPair);
    PenisCheck=WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),TEXT("PenisVisible"));
    {UTextBlock* PenisLabel=WidgetTree->ConstructWidget<UTextBlock>();PenisLabel->SetText(FText::FromString(TEXT("顯示陰莖")));PenisCheck->SetContent(PenisLabel);PenisCheck->SetIsChecked(false);PenisCheck->SetToolTipText(FText::FromString(TEXT("顯示男角分開的陰莖與陰囊。保險套不顯示。預設關閉。")));PenisCheck->OnCheckStateChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnPenisVisible);MotionCol->AddChild(PenisCheck);}

	GameAnimationParameters=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("GameAnimationParameters"));
	MotionCol->AddChild(GameAnimationParameters);
	BuildAppearanceControls(ShapeCol);
	ShapeCol->AddChild(ShapeTitle);
 ShapeCol->AddChild(BoneCategoryCombo);
 ShapeCol->AddChild(BoneAxesCheck);
	UVerticalBoxSlot* ScrollSlot = ShapeCol->AddChildToVerticalBox(ShapeScroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ShapeBorder->AddChild(ShapeCol);
	UVerticalBox* EditorColumn=WidgetTree->ConstructWidget<UVerticalBox>();
 BuildEditorDisplayControls(EditorColumn);
 EditorContentScale=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("EditorContentScale"));
 EditorContentScale->SetStretch(EStretch::UserSpecified);
 auto* EditorScaleSlot=CastChecked<UScaleBoxSlot>(EditorContentScale->AddChild(ShapeBorder));
 EditorScaleSlot->SetHorizontalAlignment(HAlign_Fill);
 EditorScaleSlot->SetVerticalAlignment(VAlign_Fill);
 EditorColumn->AddChildToVerticalBox(EditorContentScale)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 ShapeSize->AddChild(EditorColumn);
 ApplyEditorDisplaySettings();

	CardListView = WidgetTree->ConstructWidget<UOKCardListView>(UOKCardListView::StaticClass(), TEXT("CardListView"));

	CardCol = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Left"));
	UVerticalBox* Left = CardCol;
	CardSourceCombo=WidgetTree->ConstructWidget<UOKComboBoxString>(UOKComboBoxString::StaticClass(),TEXT("CardSource"));
	CardSourceCombo->AddOption(TEXT("APNG 內嵌資源卡片"));CardSourceCombo->AddOption(TEXT("街頭女主角卡片"));CardSourceCombo->AddOption(TEXT("KKS 遊戲目錄卡片"));
	Left->AddChild(CardSourceCombo);
	CardSourceCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnCardSourceChanged);
	CardDirectoryInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("CardDirectory"));
	CardDirectoryInput->SetHintText(FText::FromString(TEXT("APNG 卡片目錄")));
	CardDirectoryInput->SetText(FText::FromString(GetDefault<UKKCardWorkspace>()->ApngCardDirectory.IsEmpty()?GetDefault<UKKCardWorkspace>()->CardDirectory:GetDefault<UKKCardWorkspace>()->ApngCardDirectory));
	UButton* OpenCards = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("OpenCardFolder"));
	UTextBlock* OpenCardsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	OpenCardsText->SetText(FText::FromString(TEXT("載入此 APNG 目錄"))); OpenCards->AddChild(OpenCardsText);
	OpenCards->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnOpenCardFolder);
	UButton* GameCards = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("UseGameCards"));
	UTextBlock* GameCardsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	GameCardsText->SetText(FText::FromString(TEXT("使用遊戲卡片目錄"))); GameCards->AddChild(GameCardsText);
	GameCards->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnUseGameCards);
	Left->AddChild(CardDirectoryInput); Left->AddChild(OpenCards); Left->AddChild(GameCards);
	CardCountLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CardCount"));
	Left->AddChild(CardCountLabel);
	Left->AddChildToVerticalBox(CardListView)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	InspectCol = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Right"));
	// Navigation remains outside the inspector's scrolling details.
	UVerticalBox* Right = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("InspectorDetails"));
	PreviewImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Preview"));
	PreviewImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	InspectorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Inspector"));
	InspectorText->SetAutoWrapText(true);
	NicknameBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("Nick"));
	UButton* Save = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Save"));
	UTextBlock* SaveText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SaveText"));
	SaveText->SetText(FText::FromString(TEXT("另存新卡（身材／名稱）")));
	Save->AddChild(SaveText);
	Save->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnSave);
	UButton* Load3D = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Load3D"));
	UTextBlock* Load3DText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Load3DText"));
	Load3DText->SetText(FText::FromString(TEXT("載入3D（人物編輯器）")));
	Load3D->SetToolTipText(FText::FromString(TEXT("載入所選角色到人物編輯器，編輯身材、臉部與服裝。")));
	Load3D->AddChild(Load3DText);
	Load3D->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnLoad3D);
	Load3DButton = Load3D;
	OutfitNav = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("OutfitNav"));
	UButton* OutfitPrev = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("OutfitPrev"));
	UButton* OutfitNext = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("OutfitNext"));
	UTextBlock* OutfitPrevText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OutfitPrevT"));
	UTextBlock* OutfitNextText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OutfitNextT"));
	OutfitPrevText->SetText(FText::FromString(TEXT("< 服裝")));
	OutfitNextText->SetText(FText::FromString(TEXT("服裝 >")));
	OutfitPrev->AddChild(OutfitPrevText);
	OutfitNext->AddChild(OutfitNextText);
	OutfitPrev->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnOutfitPrev);
	OutfitNext->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnOutfitNext);
	OutfitLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OutfitLabel"));
	OutfitLabel->SetText(FText::FromString(TEXT("服裝 0")));
	OutfitNav->AddChild(OutfitPrev);
	OutfitNav->AddChild(OutfitLabel);
	OutfitNav->AddChild(OutfitNext);
	USizeBox* PreviewSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PreviewSize"));
	PreviewSize->SetMaxDesiredHeight(220.f);
	PreviewSize->SetWidthOverride(165.f);
	PreviewSize->SetHeightOverride(220.f);
	UScaleBox* PreviewScale = WidgetTree->ConstructWidget<UScaleBox>();
	PreviewScale->SetStretch(EStretch::ScaleToFit);
	PreviewScale->AddChild(PreviewImage);
	PreviewSize->AddChild(PreviewScale);
	USizeBox* EditorEntry=WidgetTree->ConstructWidget<USizeBox>();EditorEntry->SetHeightOverride(40.f);EditorEntry->AddChild(Load3D);InspectCol->AddChild(EditorEntry);
	UButton* CardInteraction=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("CardInteraction"));auto* CardInteractionText=WidgetTree->ConstructWidget<UTextBlock>();
	CardInteractionText->SetText(FText::FromString(TEXT("進入1對1互動場景")));CardInteractionText->SetAutoWrapText(true);CardInteraction->SetContent(CardInteractionText);CardInteraction->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnEnterInteraction);
	CardInteraction->SetToolTipText(FText::FromString(TEXT("載入所選女角到獨立互動場景；不取代人物編輯器。")));
	USizeBox* InteractionEntry=WidgetTree->ConstructWidget<USizeBox>();InteractionEntry->SetHeightOverride(44.f);InteractionEntry->AddChild(CardInteraction);InspectCol->AddChild(InteractionEntry);
	UScrollBox* InspectorScroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("InspectorScroll"));InspectorScroll->SetAlwaysShowScrollbar(true);InspectorScroll->AddChild(Right);
	InspectCol->AddChildToVerticalBox(InspectorScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Right->AddChild(PreviewSize);
	Right->AddChild(InspectorText);
	Right->AddChild(NicknameBox);
	UButton* EditShapes = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("EditCardShapes"));
	UTextBlock* EditShapesText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	EditShapesText->SetText(FText::FromString(TEXT("顯示／收起身材參數"))); EditShapes->AddChild(EditShapesText);
	EditShapes->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnEditShapes); Right->AddChild(EditShapes);
	OutputDirectoryInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("OutputDirectory"));
	OutputDirectoryInput->SetHintText(FText::FromString(TEXT("另存資料夾")));
	const FString Output = GetDefault<UKKCardWorkspace>()->OutputDirectory;
	OutputDirectoryInput->SetText(FText::FromString(Output.IsEmpty() ? FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("EditedCards")) : Output));
	Right->AddChild(OutputDirectoryInput);
	CopyZipmodsCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("CopyLocalZipmods"));
	UTextBlock* CopyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	CopyText->SetText(FText::FromString(TEXT("一併複製同目錄所需模組檔"))); CopyZipmodsCheck->AddChild(CopyText);
	CopyZipmodsCheck->SetIsChecked(GetDefault<UKKCardWorkspace>()->bCopyLocalZipmods); Right->AddChild(CopyZipmodsCheck);
	Right->AddChild(Save);
	UButton* ApngSave = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* ApngSaveText = WidgetTree->ConstructWidget<UTextBlock>();
	ApngSaveText->SetText(FText::FromString(TEXT("另存新檔")));
	ApngSave->AddChild(ApngSaveText);
	ApngSave->SetToolTipText(FText::FromString(TEXT("另存 APNG 新卡：原卡圖 3 秒，加上 T-pose 三視圖各 1 秒。")));
	ApngSave->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnSaveApng);
	Right->AddChild(ApngSave);
	Right->AddChild(OutfitNav);
	// OK clothes (.okclothes): the shown outfit out, one in as the shown outfit (new card), or a game coordinate card in.
	UHorizontalBox* OkClothesRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("OkClothesRow"));
	auto OkClothesButton=[this,OkClothesRow](const TCHAR* Label,const TCHAR* Tip){
		UButton* Button=WidgetTree->ConstructWidget<UButton>();UTextBlock* Text=WidgetTree->ConstructWidget<UTextBlock>();
		Text->SetText(FText::FromString(Label));Text->SetAutoWrapText(true);Button->AddChild(Text);Button->SetToolTipText(FText::FromString(Tip));
		OkClothesRow->AddChildToHorizontalBox(Button)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		return Button;
	};
	OkClothesButton(TEXT("匯出這套服裝"),TEXT("把目前顯示的這套服裝存成 .okclothes（存到另存資料夾）。"))
		->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnExportOkClothes);
	OkClothesButton(TEXT("穿上 OK 服裝"),TEXT("選一個 .okclothes 穿成目前這套服裝，另存為新卡；原卡不變。"))
		->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnWearOkClothes);
	OkClothesButton(TEXT("轉換服裝卡"),TEXT("把遊戲的服裝卡（KoiKatuClothes）轉成 .okclothes。"))
		->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnImportClothesCard);
	Right->AddChild(OkClothesRow);
	// Expressions, gaze and hand poses belong with the motion controls.
	UHorizontalBox* MouthRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MouthRow"));
	UTextBlock* MouthLab = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	MouthLab->SetText(FText::FromString(TEXT("嘴巴開合")));
	USlider* MouthSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("MouthOpen"));
	MouthOpenSlider = MouthSlider;
	MouthSlider->SetMinValue(0.f);
	MouthSlider->SetMaxValue(1.f);
	MouthSlider->SetValue(SelectedMouthOpen);
	MouthSlider->OnValueChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnMouthChanged);
	MouthRow->AddChild(MouthLab);
	UHorizontalBoxSlot* MouthSlot = MouthRow->AddChildToHorizontalBox(MouthSlider);
	MouthSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	MotionCol->AddChild(MouthRow);
	MouthPatternCombo = WidgetTree->ConstructWidget<UOKComboBoxString>(UOKComboBoxString::StaticClass(), TEXT("MouthPatternCombo"));
	for (int32 Pattern = 0; Pattern < 54; ++Pattern)
	{
		MouthPatternCombo->AddOption(FString::Printf(TEXT("嘴型 %d"), Pattern));
	}
	MouthPatternCombo->SetSelectedIndex(SelectedMouthPattern);
	MouthPatternCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnMouthPatternSelected);
	MotionCol->AddChild(MouthPatternCombo);
	SpeechRootInput=WidgetTree->ConstructWidget<UEditableTextBox>();
	SpeechRootInput->SetText(FText::FromString(TEXT("C:/portable/KoikatuSunshine/voice_extract")));
	SpeechRootInput->SetHintText(FText::FromString(TEXT("一般對話語音資料夾")));
	MotionCol->AddChild(SpeechRootInput);
	UTextBlock* VoiceLabel=WidgetTree->ConstructWidget<UTextBlock>();
	VoiceLabel->SetText(FText::FromString(TEXT("女角聲音")));
	MotionCol->AddChild(VoiceLabel);
	SpeechVoiceCombo=WidgetTree->ConstructWidget<UOKComboBoxString>(UOKComboBoxString::StaticClass(),TEXT("SpeechVoice"));
	SpeechVoiceCombo->AddOption(TEXT("卡片性格"));
	for(int32 Personality=0;Personality<=39;++Personality) SpeechVoiceCombo->AddOption(HeroineVoiceLabel(Personality));
	SpeechVoiceCombo->SetSelectedIndex(SelectedSpeechVoice<0?0:SelectedSpeechVoice+1);
	SpeechVoiceCombo->SetToolTipText(FText::FromString(TEXT("遊戲女角聲線 c00–c39。卡片性格用這張卡自己的性格；男角沒有一般話題，請改選一條女角聲線。")));
	SpeechVoiceCombo->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnSpeechVoiceSelected);
	MotionCol->AddChild(SpeechVoiceCombo);
	SpeechCombo=WidgetTree->ConstructWidget<UOKComboBoxString>();MotionCol->AddChild(SpeechCombo);
	UHorizontalBox* SpeechButtons=WidgetTree->ConstructWidget<UHorizontalBox>();
	const TCHAR* SpeechLabels[]={TEXT("載入語音"),TEXT("說話"),TEXT("停止語音")};
	for(int32 I=0;I<3;++I){
		UButton* Button=WidgetTree->ConstructWidget<UButton>();UTextBlock* Label=WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(SpeechLabels[I]));Button->AddChild(Label);SpeechButtons->AddChild(Button);
		if(I==0)Button->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnLoadSpeech);
		else if(I==1)Button->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnSpeak);
		else Button->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnStopSpeech);
	}
	MotionCol->AddChild(SpeechButtons);
	SpeechStatus=WidgetTree->ConstructWidget<UTextBlock>();MotionCol->AddChild(SpeechStatus);
	FaceCombos.Reset(); FaceOpenSliders.Reset();
	UHorizontalBox* BlinkRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	AutomaticBlinkCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
	UTextBlock* BlinkLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	BlinkLabel->SetText(FText::FromString(TEXT("自動眨眼")));
	AutomaticBlinkCheck->AddChild(BlinkLabel);
	AutomaticBlinkCheck->OnCheckStateChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnAutomaticBlink);
	BlinkRow->AddChild(AutomaticBlinkCheck);
	UButton* BlinkButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* BlinkButtonLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	BlinkButtonLabel->SetText(FText::FromString(TEXT("眨眼一次")));
	BlinkButton->AddChild(BlinkButtonLabel);
	BlinkButton->OnClicked.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnForceBlink);
	BlinkRow->AddChild(BlinkButton);
	MotionCol->AddChild(BlinkRow);
	GazeSliders.Reset();
	for(int32 Index=0;Index<3;++Index)
	{
		UHorizontalBox* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UTextBlock* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		const TCHAR* Labels[]={TEXT("左眼視線"),TEXT("右眼視線"),TEXT("視線上下")};
		Label->SetText(FText::FromString(Labels[Index]));Row->AddChild(Label);
		USlider* Slider=WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
		Slider->SetMinValue(-1.f);Slider->SetMaxValue(1.f);Slider->SetValue(0.f);
		Row->AddChildToHorizontalBox(Slider)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		MotionCol->AddChild(Row);GazeSliders.Add(Slider);
	}
	GazeSliders[0]->OnValueChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnGazeLeft);
	GazeSliders[1]->OnValueChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnGazeRight);
	GazeSliders[2]->OnValueChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnGazeVertical);
	EyeLookCameraCheck=WidgetTree->ConstructWidget<UCheckBox>();
	{
		auto* Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetText(FText::FromString(TEXT("眼睛跟隨鏡頭")));
		EyeLookCameraCheck->SetContent(Label);MotionCol->AddChild(EyeLookCameraCheck);
		EyeLookCameraCheck->OnCheckStateChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnEyeLookCamera);
	}
	// The face pressed flat along the view (Wuthering Waves' toon faces): its strength, and the direction held still.
	{
		const IConsoleVariable* Strength=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.FaceFlatten"));
		const IConsoleVariable* Lock=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.FaceFlattenLock"));
		UHorizontalBox* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		FaceFlattenLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());Row->AddChild(FaceFlattenLabel);
		USlider* Slider=WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
		Slider->SetMinValue(0.f);Slider->SetMaxValue(.9f);Slider->SetStepSize(.05f);Slider->SetValue(Strength?Strength->GetFloat():0.f);
		Row->AddChildToHorizontalBox(Slider)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));MotionCol->AddChild(Row);
		Slider->OnValueChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnFaceFlatten);
		FaceFlattenLabel->SetText(FText::FromString(FString::Printf(TEXT("臉部壓扁 %.2f"),Strength?Strength->GetFloat():0.f)));
		UCheckBox* LockCheck=WidgetTree->ConstructWidget<UCheckBox>();
		auto* Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetText(FText::FromString(TEXT("固定壓扁方向（轉視角檢查）")));
		LockCheck->SetContent(Label);LockCheck->SetIsChecked(Lock&&Lock->GetInt()!=0);MotionCol->AddChild(LockCheck);
		LockCheck->OnCheckStateChanged.AddDynamic(static_cast<UOKCharacterController*>(this),&UOKCharacterController::OnFaceFlattenLock);
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(FText::FromString(Index == 0 ? TEXT("眼睛開合") : TEXT("眉毛開合")));
		Row->AddChild(Label);
		USlider* Slider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
		Slider->SetMinValue(0.f); Slider->SetMaxValue(1.f);
		Row->AddChildToHorizontalBox(Slider)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		MotionCol->AddChild(Row); FaceOpenSliders.Add(Slider);
		UComboBoxString* Combo = WidgetTree->ConstructWidget<UOKComboBoxString>(UOKComboBoxString::StaticClass());
		MotionCol->AddChild(Combo); FaceCombos.Add(Combo);
	}
	FaceCombos[0]->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnEyesPattern);
	FaceCombos[1]->OnSelectionChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnBrowPattern);
	FaceOpenSliders[0]->OnValueChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnEyesOpen);
	FaceOpenSliders[1]->OnValueChanged.AddDynamic(static_cast<UOKCharacterController*>(this), &UOKCharacterController::OnBrowOpen);
	RefreshFaceControls();



	UHorizontalBoxSlot* ShapeSlot = RootBox->AddChildToHorizontalBox(ShapeSize);
	ShapeSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
 BuildAppearancePanel();
	UHorizontalBoxSlot* LeftSlot = RootBox->AddChildToHorizontalBox(Left);
	LeftSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UHorizontalBoxSlot* RightSlot = RootBox->AddChildToHorizontalBox(InspectCol);
	RightSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	PreviewSpace = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PreviewSpace"));
	PreviewSpace->SetVisibility(ESlateVisibility::HitTestInvisible);
	RootBox->AddChildToHorizontalBox(PreviewSpace)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RootBox->AddChildToHorizontalBox(AppearancePanel)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	RootBox->AddChildToHorizontalBox(MotionPanel)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	SetModelLayout(bModelLayout);
	RefreshPartnerMales();

	return Super::RebuildWidget();
}

void UOKCharacterController::SetLoadBusy(bool bBusy)
{
	if(bBusy&&!bLoadInFlight)LoadBusyAt=FPlatformTime::Seconds();
	else if(!bBusy&&bLoadInFlight&&FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE"))==TEXT("1"))
		UE_LOG(LogTemp,Display,TEXT("OKGUI_LOAD total_ms=%.3f"),(FPlatformTime::Seconds()-LoadBusyAt)*1000.);
	bLoadInFlight = bBusy;
	if(CardSourceCombo)CardSourceCombo->SetIsEnabled(!bBusy);
	if (CardListView) CardListView->SetIsEnabled(!bBusy);
	if (OutfitNav) OutfitNav->SetIsEnabled(!bBusy);
	if (OutfitCombo) OutfitCombo->SetIsEnabled(!bBusy);
	if (Load3DLabel)
	{
		Load3DLabel->SetText(FText::FromString(bBusy ? TEXT("載入中…") : TEXT("載入3D")));
	}
	if (Load3DButton)
	{
		Load3DButton->SetIsEnabled(!bBusy);
	}
	if (ShapeLoadButton)
	{
		ShapeLoadButton->SetIsEnabled(!bBusy);
	}
	for (UButton* Button : ClothesStateButtons)
	{
		if (Button) Button->SetIsEnabled(!bBusy);
	}
}

void UOKCharacterController::ApplyClickInputMode()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}
	PC->bShowMouseCursor = true;
	PC->bEnableClickEvents = true;
	PC->bEnableMouseOverEvents = true;
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(Mode);
	if (UWorld* World = GetWorld())
	{
		if (UGameViewportClient* VP = World->GetGameViewport())
		{
			VP->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
			VP->SetHideCursorDuringCapture(false);
		}
	}
}

void UOKCharacterController::SetModelLayout(bool bEnabled)
{
	bModelLayout = bEnabled;
 if(!bEnabled)OnCloseAppearance();
	if(auto* PC=Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer()))PC->SetPaintPreviewActive(bEnabled&&IsValid(PreviewActor)&&!bLoadInFlight&&!bStudioMode);
	const ESlateVisibility Panels = bEnabled ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	const ESlateVisibility Gallery = bEnabled ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;
	if (ShapePanel) ShapePanel->SetVisibility(bEnabled || bEditShapes ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (MotionPanel) MotionPanel->SetVisibility(bEnabled&&AppearancePanel&&AppearancePanel->GetVisibility()==ESlateVisibility::Visible?ESlateVisibility::Collapsed:Panels);
	if (PreviewSpace) PreviewSpace->SetVisibility(bEnabled ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (CardCol) CardCol->SetVisibility(Gallery);
	if (InspectCol) InspectCol->SetVisibility(Gallery);
	SetVisibility(bEnabled ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Visible);
	// The studio replaces both side panels; reloads (clothes, outfit) must not bring them back.
	if (bStudioMode)
	{
		if (ShapePanel) ShapePanel->SetVisibility(ESlateVisibility::Collapsed);
		if (MotionPanel) MotionPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UOKCharacterController::OnBackToGallery()
{
 if(bAppearanceBusy)return;
	OnExitInteraction();
	SetModelLayout(false);
	ApplyClickInputMode();
}

void UOKGalleryWidget::NativeConstruct()
{
	Super::NativeConstruct();
 AKKPreviewActor::PreparePreviewMaterialParents();
	ApplyClickInputMode();

	ScanAndShow();
	// Once per process: build the native memos every open shares (lists, mods scans, pair metadata) while the
	// user picks a card, so the first open costs what later ones do.
	{
		// With the OK base library installed, OK cards and original KKS cards alike open from it (originals add only
		// the zipmods they name), so the library is what to warm in either source mode; warming the KKS folder's own
		// tree (~1 min) served no open. Without a library, the KKS folder is the root.
		static bool bPrewarmed=false;
		const bool bLibrary=FPaths::FileExists(FPaths::ProjectContentDir()/TEXT("IndependentResources/base_v1/oklib.json"));
		const UKKGameRoot* PrewarmRoot=UKKGameRoot::Get();
		const FString Root=bLibrary?FString():(PrewarmRoot?PrewarmRoot->Root:FString());
		if(!bPrewarmed&&(bLibrary||!Root.IsEmpty())){
			bPrewarmed=true;
			// Library sessions are portable: their pair menu reads the shared PairedAnimations root.
			Async(EAsyncExecution::Thread,[Root,bLibrary](){FOkSession::Prewarm(Root);if(bLibrary)FOkSession::CommonAnimationPairs();});
		}
	}
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_AUTO_3D")) == TEXT("1"))
	{
		FString Want = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_AUTO_CARD"));
		if (Want.IsEmpty())
		{
			Want = TEXT("KK_091207");
		}
		int32 Idx = 0;
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			if (Entries[i].FileName.Contains(Want) || Entries[i].Path.Contains(Want))
			{
				Idx = i;
				break;
			}
		}
		if (Entries.IsValidIndex(Idx))
		{
			SelectIndex(Idx);
			OnLoad3D();
		}
	}
}

// The street mode's heroine cards (OK library APNG cards), offered as their own source next to the APNG folder.
static FString StreetHeroineCardDirectory(){return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()/TEXT("CharacterCards/StreetHeroines"));}
static bool IsStreetHeroineCardDirectory(const FString& Directory){return !Directory.IsEmpty()&&FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(Directory),StreetHeroineCardDirectory());}

void UOKCharacterController::ScanAndShow()
{
	if (bLoadInFlight) return;
	if(!bCardDirectoryInitialized){const FString EnvFolder=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_CARD_DIRECTORY"));CurrentCardDirectory=EnvFolder.IsEmpty()?GetDefault<UKKCardWorkspace>()->CardDirectory:EnvFolder;bCardDirectoryInitialized=true;if(!CurrentCardDirectory.IsEmpty()&&CardDirectoryInput)CardDirectoryInput->SetText(FText::FromString(CurrentCardDirectory));}
	const FString CardFolderSetting=CurrentCardDirectory;
	bIndependentCards = !CardFolderSetting.IsEmpty();
	if(CardSourceCombo)CardSourceCombo->SetSelectedOption(!bIndependentCards?TEXT("KKS 遊戲目錄卡片"):IsStreetHeroineCardDirectory(CardFolderSetting)?TEXT("街頭女主角卡片"):TEXT("APNG 內嵌資源卡片"));
	UKKGameRoot* Root = bIndependentCards ? nullptr : UKKGameRoot::Get();
	const FString Folder = bIndependentCards ? CardFolderSetting : (Root ? Root->Root : FString());
	Entries.Reset();CardSources.Reset();

	Selected = FKKCharacterFile(); SelectedIndex = INDEX_NONE;
	AnimatedPreviewTextures.Reset();AnimatedPreviewDurations.Reset();PendingPreviewFrames.Reset();++AnimatedPreviewGeneration;
	if(PreviewImage)PreviewImage->SetBrush(FSlateBrush());
	// Both sexes and shipped defaults use the same card import path.
	// Preserve each full path: equal filenames can identify different cards.
	if (bIndependentCards)
	{
		// Probe PNG chunk headers without decompressing every embedded model.
		// Visible rows fetch metadata asynchronously; the list never parses every model.
		TArray<FString> Files;IFileManager::Get().FindFilesRecursive(Files,*Folder,TEXT("*.png"),true,false);Files.Sort();
		for(const FString& Path:Files)if(!UKKCardWorkspace::PortableSourcePath(Path).IsEmpty()){FKKCardListEntry Entry;Entry.Path=Path;Entry.FileName=FPaths::GetCleanFilename(Path);Entries.Add(MoveTemp(Entry));}
	}
	else if (Root)
	{
		for (const TCHAR* Relative : {TEXT("UserData/chara/female"), TEXT("UserData/chara/male"),
			TEXT("DefaultData/chara/female"), TEXT("DefaultData/chara/male")})
		{
			const FString CardFolder = FPaths::Combine(Folder, Relative);
			TArray<FString> Names;
			IFileManager::Get().FindFiles(Names, *(CardFolder / TEXT("*.png")), true, false);
			Names.Sort();
			for (const FString& Name : Names)
			{
				FKKCardListEntry Entry;
				Entry.Path = CardFolder / Name;
				Entry.FileName = Name;
				Entries.Add(MoveTemp(Entry));
			}
		}
	}

	FString Err;
	if (!bIndependentCards && Root)
	{
		const FString Cache = FPaths::ProjectSavedDir() / TEXT("mod_index.json");
		if (!ModIndex.LoadCache(Cache) || ModIndex.ModsDir != Root->Mods())
		{
			ModIndex.Build(Root->Mods(), Err);
			ModIndex.SaveCache(Cache);
		}
	}
	RefreshPartnerMales();
	RefreshCardList();
	if (Entries.Num() > 0)
	{
		SelectIndex(0);
	}
	else if (InspectorText)
	{
		InspectorText->SetText(FText::FromString(
			FString::Printf(TEXT("此目录没有角色卡：%s"), *Folder)));
	}
}

void UOKCharacterController::OnOpenCardFolder()
{
	if (bLoadInFlight || !CardDirectoryInput) return;
	FString Directory = CardDirectoryInput->GetText().ToString().TrimStartAndEnd();
	Directory.RemoveFromStart(TEXT("\"")); Directory.RemoveFromEnd(TEXT("\""));
	if (Directory.IsEmpty() || !IFileManager::Get().DirectoryExists(*Directory))
	{
		if (InspectorText) InspectorText->SetText(FText::FromString(TEXT("請輸入存在的卡片資料夾。")));
		return;
	}
	UKKCardWorkspace* Settings = GetMutableDefault<UKKCardWorkspace>();
	Settings->CardDirectory = FPaths::ConvertRelativePathToFull(Directory);if(!IsStreetHeroineCardDirectory(Settings->CardDirectory))Settings->ApngCardDirectory=Settings->CardDirectory;CurrentCardDirectory=Settings->CardDirectory;bCardDirectoryInitialized=true; Settings->SaveConfig();
	CardDirectoryInput->SetText(FText::FromString(Settings->CardDirectory));
	SetModelLayout(false); ScanAndShow();
}

void UOKCharacterController::OnUseGameCards()
{
	if (bLoadInFlight) return;
	UKKCardWorkspace* Settings = GetMutableDefault<UKKCardWorkspace>(); if(bIndependentCards&&!CurrentCardDirectory.IsEmpty()&&!IsStreetHeroineCardDirectory(CurrentCardDirectory))Settings->ApngCardDirectory=CurrentCardDirectory; Settings->CardDirectory.Empty(); Settings->SaveConfig();
	CurrentCardDirectory.Empty();bCardDirectoryInitialized=true;
	if (CardDirectoryInput) CardDirectoryInput->SetText(FText::FromString(Settings->ApngCardDirectory));
	SetModelLayout(false); ScanAndShow();
}

void UOKCharacterController::OnCardSourceChanged(FString Item,ESelectInfo::Type)
{
 if(!bCardDirectoryInitialized||bLoadInFlight)return;
 if(Item==TEXT("KKS 遊戲目錄卡片")){if(bIndependentCards)OnUseGameCards();return;}
 const FString Previous=!bIndependentCards?TEXT("KKS 遊戲目錄卡片"):IsStreetHeroineCardDirectory(CurrentCardDirectory)?TEXT("街頭女主角卡片"):TEXT("APNG 內嵌資源卡片");
 if(Item==Previous||!CardDirectoryInput)return;
 if(Item==TEXT("街頭女主角卡片"))CardDirectoryInput->SetText(FText::FromString(StreetHeroineCardDirectory()));
 else if(Item==TEXT("APNG 內嵌資源卡片")){
  // Back to the remembered APNG folder (the input may still show the street folder).
  const FString Remembered=GetDefault<UKKCardWorkspace>()->ApngCardDirectory;const FString Typed=CardDirectoryInput->GetText().ToString();
  if(!Remembered.IsEmpty()&&!IsStreetHeroineCardDirectory(Remembered))CardDirectoryInput->SetText(FText::FromString(Remembered));
  else if(Typed.IsEmpty()||IsStreetHeroineCardDirectory(Typed))CardDirectoryInput->SetText(FText::FromString(FPaths::ProjectContentDir()/TEXT("CharacterCards/CrowdAPNG")));
 }
 else return;
 OnOpenCardFolder();
 // The folder did not open: show the source that is still loaded.
 const FString Now=!bIndependentCards?TEXT("KKS 遊戲目錄卡片"):IsStreetHeroineCardDirectory(CurrentCardDirectory)?TEXT("街頭女主角卡片"):TEXT("APNG 內嵌資源卡片");
 if(Now!=Item&&CardSourceCombo)CardSourceCombo->SetSelectedOption(Now);
}

void UOKCharacterController::OnEditShapes()
{
	bEditShapes = !bEditShapes; SetModelLayout(bModelLayout); ApplyClickInputMode();
}

void UOKCharacterController::RefreshCardList()
{
 if(!CardListView)return;
 ++CardListGeneration;CardMetadataAttempts.Reset();
 TArray<UObject*> Items;Items.Reserve(Entries.Num());
 for(int32 I=0;I<Entries.Num();++I){
  auto* Item=NewObject<UOKCardListItem>(this);Item->Owner=this;Item->Index=I;Item->Path=Entries[I].Path;Items.Add(Item);
 }
 CardListView->SetListItems(Items);CardListView->ScrollToTop();
 if(CardCountLabel)CardCountLabel->SetText(FText::FromString(FString::Printf(TEXT("共 %d 張卡片"),Entries.Num())));
}

void UOKCharacterController::TickCardListMetadata()
{
 if(bCardMetadataBusy||!CardListView||!CardListView->IsVisible()||bModelLayout)return;
 for(auto* Widget:CardListView->GetDisplayedEntryWidgets()){
  auto* Row=Cast<UOKCardListEntry>(Widget);auto* Item=Row?Row->Item.Get():nullptr;
  if(!Item||!Entries.IsValidIndex(Item->Index))continue;
  const int32 Index=Item->Index;
  if(Entries[Index].Kind!=EKKCardKind::Unknown||CardMetadataAttempts.Contains(Index))continue;
  CardMetadataAttempts.Add(Index);bCardMetadataBusy=true;
  const FString Path=Entries[Index].Path;const int32 Generation=CardListGeneration;
  const TWeakObjectPtr<UOKCharacterController> Weak(this);
  Async(EAsyncExecution::ThreadPool,[Weak,Path,Index,Generation](){
   FKKCharacterFile File;FString Error;const bool Ok=UKKCardReader::LoadFromFile(Path,File,Error);
   FKKCardListEntry Metadata;Metadata.Path=Path;
   if(Ok){Metadata.Kind=File.Header.Kind;Metadata.LastName=File.LastName;Metadata.FirstName=File.FirstName;Metadata.Nickname=File.Nickname;Metadata.Sex=File.Sex;Metadata.Personality=File.Personality;}
   AsyncTask(ENamedThreads::GameThread,[Weak,Metadata=MoveTemp(Metadata),Index,Generation,Ok](){
    if(auto* G=Weak.Get()){
     G->bCardMetadataBusy=false;
     if(!Ok||Generation!=G->CardListGeneration||!G->Entries.IsValidIndex(Index)||G->Entries[Index].Path!=Metadata.Path)return;
     const FString Name=G->Entries[Index].FileName;G->Entries[Index]=Metadata;G->Entries[Index].FileName=Name;
     if(auto* Row=G->CardListView->GetEntryWidgetFromItem<UOKCardListEntry>(G->CardListView->GetItemAt(Index)))Row->RefreshLabel();
    }
   });
  });return; // One large card in flight; no full-directory reads or thread-pool flood.
 }
}

void UOKCharacterController::SetWorkingCardPath(const FString& Path)
{
 if(!Entries.IsValidIndex(SelectedIndex))return;
 if(!CardSources.Contains(SelectedIndex))CardSources.Add(SelectedIndex,TPair<FString,bool>(Entries[SelectedIndex].Path,bIndependentCards));
 Entries[SelectedIndex].Path=Path;
}

// The launcher's warm processes (ok-card-warm --priority) build every outfit of the newest requested cards first,
// so opening the card being looked at and switching its outfits come from the open cache.
static void OKRequestCardWarm(const FString& CardPath)
{
	const FString File=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_WARM_PRIORITY"));
	if(File.IsEmpty()||CardPath.IsEmpty())return;
	FFileHelper::SaveStringToFile(FPaths::ConvertRelativePathToFull(CardPath)+TEXT("\n"),*File,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
}

static void OKThrottleForNativeOpen(bool bThrottle);

void UOKCharacterController::SelectIndex(int32 Index)
{
	if (bLoadInFlight || bAppearanceBusy || !Entries.IsValidIndex(Index))
	{
		return;
	}
	FString Err;
	FKKCharacterFile LoadedCard;
	const auto* Source=CardSources.Find(Index);
 const FString SourcePath=Source?Source->Key:Entries[Index].Path;
 if (!UKKCardReader::LoadFromFile(SourcePath, LoadedCard, Err))
	{
		if (InspectorText)
		{
			InspectorText->SetText(FText::FromString(Err));
		}
		return;
	}
	// Publish a selection only after parsing succeeds. A re-selection is a
	// fresh read of saved parameters, even when its geometry can be reused.
	if(Source){Entries[Index].Path=SourcePath;bIndependentCards=Source->Value;CardSources.Remove(Index);}
	SelectedIndex = Index;Selected=MoveTemp(LoadedCard);bEditorModsDirty=true;
    OutfitIndex=0;FMemory::Memzero(ClothesStates,sizeof(ClothesStates));
 OKRequestCardWarm(bIndependentCards?UKKCardWorkspace::PortableSourcePath(Entries[Index].Path):Entries[Index].Path);
 AppearanceSelectedKeys.Reset();
	AnimatedPreviewTextures.Reset();AnimatedPreviewDurations.Reset();AnimatedPreviewElapsed=0;AnimatedPreviewFrame=INDEX_NONE;
	if(PreviewImage)PreviewImage->SetBrush(FSlateBrush());

 auto& Entry=Entries[Index];Entry.Kind=Selected.Header.Kind;Entry.LastName=Selected.LastName;Entry.FirstName=Selected.FirstName;Entry.Nickname=Selected.Nickname;Entry.Sex=Selected.Sex;Entry.Personality=Selected.Personality;
 if(CardListView)if(auto* Row=CardListView->GetEntryWidgetFromItem<UOKCardListEntry>(CardListView->GetItemAt(Index)))Row->RefreshLabel();

	if (NicknameBox)
	{
		NicknameBox->SetText(FText::FromString(Selected.Nickname));
	}
	if (OutfitIndex < 0 || (Selected.Outfits.Num() > 0 && OutfitIndex >= Selected.Outfits.Num()))
	{
		OutfitIndex = 0;
	}
	RefreshOutfitList();
	FString Body;
	Body += FString::Printf(TEXT("%s %s (%s)\n"), *Selected.LastName, *Selected.FirstName, *Selected.Nickname);
	Body += FString::Printf(TEXT("种类 %s  性别 %d  性格 %d\n"), *KindName(Selected.Header.Kind), Selected.Sex, Selected.Personality);
	Body += FString::Printf(TEXT("头 %d  头发前/后/侧/扩 %d/%d/%d/%d\n"),
		Selected.Face.HeadId, Selected.Hair.Front.Id, Selected.Hair.Back.Id, Selected.Hair.Side.Id, Selected.Hair.Extension.Id);
	Body += FString::Printf(TEXT("脸滑杆 %d  身体滑杆 %d  服装0 %d\n"),
		Selected.Face.ShapeValueFace.Num(), Selected.Body.ShapeValueBody.Num(),
		Selected.Outfits.Num() > 0 ? Selected.Outfits[0].Clothes.Num() : 0);
	Body += FString::Printf(TEXT("UAR %d  服装 %d\n"), Selected.Resolves.Num(), OutfitIndex);
	if (bIndependentCards)
	{
		LocalMods.BuildForCard(Entries[Index].Path);
		for (const FKKLocalModRequirement& Item : LocalMods.Resolve(Selected.Resolves))
		{
			const TCHAR* State = Item.Status == EKKLocalModStatus::Found ? TEXT("已找到；3D 轉換尚待支援") :
				(Item.Status == EKKLocalModStatus::Conflict ? TEXT("GUID 衝突") : TEXT("缺少同目錄 zipmod"));
			Body += FString::Printf(TEXT("  [%s] %s\n"), State, *Item.Guid);
		}
		for (const FString& Diagnostic : LocalMods.Diagnostics) Body += Diagnostic + TEXT("\n");
		Body += TEXT("可編輯身材；載入 3D 後另存 APNG：原卡圖 3 秒＋T-pose 三視圖各 1 秒。\n");
	}
	TSet<FString> Seen;
	for (const FKKResolveInfo& Info : Selected.Resolves)
	{
		if (bIndependentCards) break;
		if (Seen.Contains(Info.ModID))
		{
			continue;
		}
		Seen.Add(Info.ModID);
		const bool bHave = ModIndex.Find(Info.ModID) != nullptr;
		Body += FString::Printf(TEXT("  %s %s\n"), bHave ? TEXT("[有]") : TEXT("[缺]"), *Info.ModID);
	}
	if (InspectorText)
	{
		InspectorText->SetText(FText::FromString(Body));
	}
	if (PreviewImage)
	{
		AnimatedPreviewTextures.Reset(); AnimatedPreviewDurations.Reset(); AnimatedPreviewElapsed=0; AnimatedPreviewFrame=INDEX_NONE;
		TArray<FKKPngFrame> Frames; FString AnimationError;
		const int32 PreviewGeneration=++AnimatedPreviewGeneration;PendingPreviewFrames.Reset();
		if (FKKAnimatedPng::Read(Selected.Header.PreviewPng,Frames,AnimatedPreviewPlays,AnimationError)&&!Frames.IsEmpty())
		{
			// Only the first frame is decoded here (an OK card shows it for 3 s); the others follow from a worker.
			// Until they arrive the preview stays on the first frame.
			if(UTexture2D* First=TextureFromPng(Frames[0].Png))
			{
				AnimatedPreviewTextures.Add(First);
				for(const auto& Frame:Frames)AnimatedPreviewDurations.Add(Frame.Seconds);
				for(int32 I=1;I<Frames.Num();++I)PendingPreviewFrames.Add(MoveTemp(Frames[I].Png));
				if(!PendingPreviewFrames.IsEmpty())
				{
					TWeakObjectPtr<UOKCharacterController> Weak(this);
					Async(EAsyncExecution::ThreadPool,[Weak,PreviewGeneration,Pngs=PendingPreviewFrames]()
					{
						IImageWrapperModule& Module=FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
						TArray<TArray<uint8>> Raw;TArray<FIntPoint> Sizes;
						for(const TArray<uint8>& Png:Pngs)
						{
							TSharedPtr<IImageWrapper> Wrapper=Module.CreateImageWrapper(EImageFormat::PNG);TArray<uint8> Pixels;
							if(!Wrapper.IsValid()||!Wrapper->SetCompressed(Png.GetData(),Png.Num())||!Wrapper->GetRaw(ERGBFormat::BGRA,8,Pixels))break;
							Sizes.Add(FIntPoint(Wrapper->GetWidth(),Wrapper->GetHeight()));Raw.Add(MoveTemp(Pixels));
						}
						AsyncTask(ENamedThreads::GameThread,[Weak,PreviewGeneration,Raw=MoveTemp(Raw),Sizes=MoveTemp(Sizes),Expected=Pngs.Num()]()
						{
							UOKCharacterController* G=Weak.Get();
							if(!G||G->AnimatedPreviewGeneration!=PreviewGeneration||G->PendingPreviewFrames.IsEmpty())return;
							G->PendingPreviewFrames.Reset();
							for(int32 I=0;I<Raw.Num();++I)
							{
								UTexture2D* Tex=UTexture2D::CreateTransient(Sizes[I].X,Sizes[I].Y,PF_B8G8R8A8);if(!Tex)break;
								FMemory::Memcpy(Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE),Raw[I].GetData(),Raw[I].Num());
								Tex->GetPlatformData()->Mips[0].BulkData.Unlock();Tex->UpdateResource();G->AnimatedPreviewTextures.Add(Tex);
							}
							// A frame that did not decode ends the animation at the frames that did.
							if(Raw.Num()!=Expected||G->AnimatedPreviewTextures.Num()!=G->AnimatedPreviewDurations.Num())
								G->AnimatedPreviewDurations.SetNum(G->AnimatedPreviewTextures.Num());
						});
					});
				}
			}
		}
		UTexture2D* Tex=AnimatedPreviewTextures.IsEmpty()?TextureFromPng(Selected.Header.PreviewPng):AnimatedPreviewTextures[0].Get();
		if (Tex)
		{
			PreviewImage->SetBrushFromTexture(Tex, true);
		}
		else PreviewImage->SetBrush(FSlateBrush());
	}
	SyncShapeSliders();
}

void UOKCharacterController::SyncShapeSliders()
{
	SyncBoneSliders();
	for (UOKShapeSlider* Slider : ShapeSliders)
	{
		if (!Slider)
		{
			continue;
		}
		const TArray<float>& Src = Slider->bBody ? Selected.Body.ShapeValueBody : Selected.Face.ShapeValueFace;
		const float V = Src.IsValidIndex(Slider->Index) ? Src[Slider->Index] : 0.5f;
		Slider->SetValue(V);
		Slider->RefreshValueLabel();
	}
}

void UOKCharacterController::OnSave()
{
	if(bAppearanceBusy||bPropertyBusy||bLoadInFlight)return;
	if(MaterializeAppearance([](UOKCharacterController* G){G->OnSave();}))return;
	if(bApngCaptureInFlight||bPortableSaveInFlight) return;
	if (Selected.OriginalBytes.Num() == 0 || !Entries.IsValidIndex(SelectedIndex))
	{
		return;
	}
	if (NicknameBox)
	{
		Selected.Nickname = NicknameBox->GetText().ToString();
	}
	const FString Directory = OutputDirectoryInput ? OutputDirectoryInput->GetText().ToString().TrimStartAndEnd() : FString();
	if (Directory.IsEmpty()) { if (InspectorText) InspectorText->SetText(FText::FromString(TEXT("請填寫另存資料夾。"))); return; }
	// A card saved as an OK card (no legacy okPv snapshot) is an APNG: the original card image for 3 s, then the
	// T-pose front, side and back views for 1 s each. Captured from the loaded 3D of this card and outfit first;
	// the save continues when the views are in (without a loaded 3D the card keeps its image).
	if(!bOkCardPreviewCaptured&&!OKPngHasChunk(Entries[SelectedIndex].Path,"okPv")&&IsValid(PreviewActor)
		&&PreviewCardPath==Entries[SelectedIndex].Path&&PreviewOutfitIndex==OutfitIndex)
	{
		ApplyPendingEditorMods();
		PreviewActor->ApplyShapes(Selected.Face.ShapeValueFace,Selected.Body.ShapeValueBody);
		bApngCaptureInFlight=true;SetIsEnabled(false);
		if(InspectorText)InspectorText->SetText(FText::FromString(TEXT("正在擷取 T-pose 正面、側面、背面三視圖…")));
		const int32 PoseBefore=PreviewActor->PoseMode;
		TWeakObjectPtr<UOKCharacterController> Weak(this);
		OKCardPreviewCapture::BeginTPoseViews(PreviewActor,[Weak,PoseBefore](TArray<TArray<uint8>> Views,FString Error)
		{
			UOKCharacterController* Widget=Weak.Get();if(!Widget)return;
			Widget->bApngCaptureInFlight=false;Widget->SetIsEnabled(true);Widget->RestoreAnimationAfterCapture(PoseBefore);
			TArray<uint8> Preview;
			if(Error.IsEmpty()&&FKKAnimatedPng::BuildOkCardPreview(Widget->Selected.Header.PreviewPng,Views,Preview,Error))Widget->Selected.Header.PreviewPng=MoveTemp(Preview);
			else UE_LOG(LogTemp,Warning,TEXT("OK card APNG preview not made, keeping the card image: %s"),*Error);
			Widget->bOkCardPreviewCaptured=true;Widget->OnSave();Widget->bOkCardPreviewCaptured=false;
		});
		return;
	}
	const FString OutPath = UKKCardWorkspace::NewCardPath(FPaths::ConvertRelativePathToFull(Directory), Entries.IsValidIndex(SelectedIndex) ? Entries[SelectedIndex].Path : TEXT("card.png"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPath), true);
	FString Err;
	if (bIndependentCards && CopyZipmodsCheck && CopyZipmodsCheck->IsChecked())
	{
		LocalMods.BuildForCard(Entries[SelectedIndex].Path);
		if (!LocalMods.CopyFound(Selected.Resolves, FPaths::GetPath(OutPath), Err))
		{ if (InspectorText) InspectorText->SetText(FText::FromString(Err)); return; }
	}
	const FString SourcePack = FPaths::ChangeExtension(Entries[SelectedIndex].Path, TEXT("okpreview"));
    const FString OutputPack = FPaths::ChangeExtension(OutPath, TEXT("okpreview"));
    bool bCopiedPack = false;
    if (bIndependentCards && UKKCardWorkspace::PortableSourcePath(Entries[SelectedIndex].Path) == SourcePack)
    {
        if (IFileManager::Get().Copy(*OutputPack, *SourcePack, false) != COPY_OK)
        { if (InspectorText) InspectorText->SetText(FText::FromString(TEXT("複製獨立資源包失敗；未另存卡片。"))); return; }
        bCopiedPack = true;
    }
    const bool bSaved = UKKCardWriter::SaveWithEdits(Selected, OutPath, Err);
    if (!bSaved && bCopiedPack) IFileManager::Get().Delete(*OutputPack);
	if (bSaved)
	{
		UKKCardWorkspace* Settings = GetMutableDefault<UKKCardWorkspace>(); Settings->OutputDirectory = Directory;
		Settings->bCopyLocalZipmods = CopyZipmodsCheck && CopyZipmodsCheck->IsChecked(); Settings->SaveConfig();
	}
 const FString SavedMessage=FString::Printf(TEXT("已另存 %s\n身材數值已寫入；預覽圖片保留原圖。"),*OutPath);
 // A card without a legacy okPv snapshot (an OK card, or an original KKS card) is saved as an OK card: made again from
 // the OK base library so its models list and pack match the edited card data.
 if(bSaved&&!OKPngHasChunk(OutPath,"okPv"))FinishOkCardSave(OutPath,SavedMessage);
 else if(bSaved)FinishPortableSave(Entries[SelectedIndex].Path,OutPath,SavedMessage,bIndependentCards);
 else if(InspectorText)InspectorText->SetText(FText::FromString(Err));
}

void UOKCharacterController::RestoreAnimationAfterCapture(int32 PoseBefore)
{
 if(!IsValid(PreviewActor))return;
 if(!SelectedStudioRow.IsNone()){FString StudioError;PreviewActor->SetStudioAnimation(AnimationCatalog,SelectedStudioRow,StudioError,SavedStudioOptions.X,SavedStudioOptions.Y,SavedStudioOptions.Z);}
 else if(!SelectedGameClip.IsEmpty())PreviewActor->SetGameAnimation(SelectedGameClip);
 else if(PoseBefore!=0)PreviewActor->SetPoseMode(PoseBefore);
}

void UOKCharacterController::DecodeRemainingPreviewFramesNow()
{
	if(PendingPreviewFrames.IsEmpty())return;
	++AnimatedPreviewGeneration;
	for(const TArray<uint8>& Png:PendingPreviewFrames){UTexture2D* Tex=TextureFromPng(Png);if(!Tex)break;AnimatedPreviewTextures.Add(Tex);}
	PendingPreviewFrames.Reset();
	if(AnimatedPreviewTextures.Num()!=AnimatedPreviewDurations.Num())AnimatedPreviewDurations.SetNum(AnimatedPreviewTextures.Num());
}

bool UOKCharacterController::OKPngHasChunk(const FString& Path,const ANSICHAR* Type)
{
 TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Path)||Bytes.Num()<8)return false;
 for(int64 Pos=8;Pos+12<=Bytes.Num();){
  const uint32 Len=(uint32(Bytes[Pos])<<24)|(uint32(Bytes[Pos+1])<<16)|(uint32(Bytes[Pos+2])<<8)|uint32(Bytes[Pos+3]);
  if(FMemory::Memcmp(&Bytes[Pos+4],Type,4)==0)return true;
  if(FMemory::Memcmp(&Bytes[Pos+4],"IEND",4)==0)return false;
  Pos+=12+int64(Len);
 }
 return false;
}

void UOKCharacterController::FinishOkCardSave(const FString& Output,const FString& Message)
{
 bPortableSaveInFlight=true;
 if(InspectorText)InspectorText->SetText(FText::FromString(TEXT("卡片已寫入，正在背景轉成 OK 卡…")));
 const TWeakObjectPtr<UOKCharacterController> Weak(this);
 Async(EAsyncExecution::ThreadPool,[Weak,Output,Message](){
  FString Summary;const double Start=FPlatformTime::Seconds();
  const bool Made=FOkSession::MakeOkCard(Output,Output,Summary);
  UE_LOG(LogTemp,Display,TEXT("OK card save made=%d seconds=%.3f output=%s summary=%s"),Made,FPlatformTime::Seconds()-Start,*Output,*Summary);
  const FString Note=Made?TEXT("\n已轉成 OK 卡（OK 共用資源 + 卡內資源）。"):TEXT("\nOK 卡轉換失敗：")+Summary;
  AsyncTask(ENamedThreads::GameThread,[Weak,Message,Note,Made](){
   if(auto* Widget=Weak.Get()){Widget->bPortableSaveInFlight=false;if(Widget->InspectorText)Widget->InspectorText->SetText(FText::FromString(Message+Note));}
   if(GEngine)GEngine->AddOnScreenDebugMessage(1,10.f,Made?FColor::Green:FColor::Yellow,Message+Note);
  });
 });
}

void UOKCharacterController::FinishPortableSave(const FString& Source,const FString& Output,const FString& Message,bool CopyStates)
{
 if(!CopyStates){if(InspectorText)InspectorText->SetText(FText::FromString(Message));return;}
 bPortableSaveInFlight=true;
 if(InspectorText)InspectorText->SetText(FText::FromString(TEXT("卡片已寫入，正在背景保存服裝資源…")));
 const TWeakObjectPtr<UOKCharacterController> Weak(this);
 Async(EAsyncExecution::ThreadPool,[Weak,Source,Output,Message](){
  FString Warning;const double Start=FPlatformTime::Seconds();
  const bool Copied=FOkSession::CopyPortableClothingStates(Source,Output,Warning);
  if(!Copied)Warning=TEXT("\n衣服狀態資源複製失敗：")+Warning;
  UE_LOG(LogTemp,Display,TEXT("Portable save resources copied=%d background_seconds=%.3f output=%s warning=%s"),Copied,FPlatformTime::Seconds()-Start,*Output,*Warning);
  AsyncTask(ENamedThreads::GameThread,[Weak,Message,Warning,Copied](){
   if(auto* Widget=Weak.Get()){Widget->bPortableSaveInFlight=false;if(Widget->InspectorText)Widget->InspectorText->SetText(FText::FromString(Message+Warning));}
   if(GEngine)GEngine->AddOnScreenDebugMessage(1,10.f,Copied?FColor::Green:FColor::Yellow,Message+Warning);
  });
 });
}

// OK_AUTO_SEQUENCE="~8,C041,@1,@2,C042,#3,#Demo": after the current load settles, open the named card, switch to outfit N,
// select animation menu option N or the first containing the text (#), or idle N seconds (~N), one step at a time,
// logging the wall time of each (load and animation-switch measurements without UI automation).
void UOKCharacterController::TickAutoSequence()
{
 if(!bAutoSequenceInit){
  bAutoSequenceInit=true;
  FPlatformMisc::GetEnvironmentVariable(TEXT("OK_AUTO_SEQUENCE")).ParseIntoArray(AutoSequence,TEXT(","));
  AutoSequenceAt=0;
 }
 if(AutoSequence.IsEmpty()||bLoadInFlight||bAppearanceBusy||bPropertyBusy||IsValid(PreparingPreview)||Entries.IsEmpty()||bAnimationPreparing||PendingAnimationKind>=0)return;
 const double Now=FPlatformTime::Seconds();
 if(AutoSequenceAt>0){UE_LOG(LogTemp,Display,TEXT("OKGUI_SEQ done ms=%.3f"),(Now-AutoSequenceAt)*1000.);AutoSequenceAt=-Now;return;}
 // Let the finished character render a moment before the next step.
 if(AutoSequenceAt<0&&Now+AutoSequenceAt<1.5)return;
 const FString Step=AutoSequence[0].TrimStartAndEnd();AutoSequence.RemoveAt(0);
 UE_LOG(LogTemp,Display,TEXT("OKGUI_SEQ step=%s"),*Step);
 // "~N": idle N seconds (a user looking at the card list) before the next step.
 if(Step.StartsWith(TEXT("~"))){AutoSequenceAt=-(Now+FCString::Atod(*Step+1)-1.5);return;}
 AutoSequenceAt=Now;
 if(Step.StartsWith(TEXT("@"))){
  if(!IsValid(PreviewActor)){AutoSequenceAt=0;return;}
  const int32 N=FMath::Max(Selected.Outfits.Num(),1);OutfitIndex=FMath::Clamp(FCString::Atoi(*Step+1),0,N-1);RefreshOutfitList();OnLoad3D();return;
 }
 if(Step.StartsWith(TEXT("#"))){
  if(!IsValid(PreviewActor)||!GameClipCombo||GameClipCombo->GetOptionCount()<2){UE_LOG(LogTemp,Warning,TEXT("OKGUI_SEQ no animation options"));AutoSequenceAt=0;return;}
  const FString Want=Step.Mid(1);FString Option;
  if(Want==TEXT("?")){for(int32 i=1;i<GameClipCombo->GetOptionCount();++i)UE_LOG(LogTemp,Display,TEXT("OKGUI_SEQ option %d=%s"),i,*GameClipCombo->GetOptionAtIndex(i));AutoSequenceAt=0;return;}
  if(Want.IsNumeric())Option=GameClipCombo->GetOptionAtIndex(FMath::Clamp(FCString::Atoi(*Want),1,GameClipCombo->GetOptionCount()-1));
  else for(int32 i=1;i<GameClipCombo->GetOptionCount()&&Option.IsEmpty();++i)if(GameClipCombo->GetOptionAtIndex(i).Contains(Want))Option=GameClipCombo->GetOptionAtIndex(i);
  if(Option.IsEmpty()){UE_LOG(LogTemp,Warning,TEXT("OKGUI_SEQ animation not found: %s (%d options)"),*Want,GameClipCombo->GetOptionCount());AutoSequenceAt=0;return;}
  UE_LOG(LogTemp,Display,TEXT("OKGUI_SEQ animation option=%s"),*Option);
  // The combo's selection handler starts the preparation; an unchanged selection would not fire it.
  if(GameClipCombo->GetSelectedOption()==Option)BeginAnimationPreparation(1,Option,ESelectInfo::Direct);else GameClipCombo->SetSelectedOption(Option);return;
 }
 for(int32 i=0;i<Entries.Num();++i)if(Entries[i].FileName.Contains(Step)||Entries[i].Path.Contains(Step)){SelectIndex(i);OnLoad3D();return;}
 UE_LOG(LogTemp,Warning,TEXT("OKGUI_SEQ card not found: %s"),*Step);AutoSequenceAt=0;
}

static bool OKFrameGraphEnabled()
{
    static int32 Enabled = -1;
    if (Enabled < 0) Enabled = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_FRAME_GRAPH")) == TEXT("1") ? 1 : 0;
    return Enabled == 1;
}

// Wall time is the gap since the previous tick (what stat unitgraph draws as Frame).
// Game/Draw/GPU are the engine's raw unit times for that same gap. Sampling starts
// only after the progressive materials are in place, then again across a product sweep.
static void OKNoteFrameGraph(UOKCharacterController* Self, float DeltaSeconds)
{
    if (!OKFrameGraphEnabled()) return;
    if (!IsValid(Self->PreviewActor) || !Self->PreviewActor->Session.IsOpen() || Self->bLoadInFlight) return;
    if (!Self->PreviewActor->PendingPreviewMaterials.IsEmpty()) return;
    struct FSample { float Wall, Game, Draw, Gpu; };
    static int32 Warm = 0, Phase = 0, Count = 0, Over8 = 0, Over16 = 0, Logged = 0;
    static TArray<FSample> Samples;
    if (Phase >= 3) return;
    if (Phase == 0)
    {
        if (++Warm < 180) return;
        Phase = 1; Count = 0; Over8 = 0; Over16 = 0; Logged = 0; Samples.Reset(); Samples.Reserve(400);
        UE_LOG(LogTemp, Display, TEXT("OK frame graph idle begin frame=%llu"), GFrameCounter);
    }
    FSample S;
    S.Wall = DeltaSeconds * 1000.f; S.Game = -1.f; S.Draw = -1.f; S.Gpu = -1.f;
    if (UWorld* World = Self->GetWorld())
        if (UGameViewportClient* Viewport = World->GetGameViewport())
            if (FStatUnitData* Stat = Viewport->GetStatUnitData())
            {
                S.Game = Stat->RawGameThreadTime; S.Draw = Stat->RawRenderThreadTime; S.Gpu = Stat->RawGPUFrameTime[0];
            }
    if (S.Wall > 8.f) ++Over8;
    if (S.Wall > 16.f) ++Over16;
    if (S.Wall > 12.f && Logged < 12)
    {
        ++Logged;
        UE_LOG(LogTemp, Display, TEXT("OK frame spike phase=%d i=%d wall=%.1f game=%.1f draw=%.1f gpu=%.1f"),
            Phase, Count, S.Wall, S.Game, S.Draw, S.Gpu);
    }
    Samples.Add(S);
    const int32 Need = Phase == 1 ? 400 : 500;
    if (++Count < Need) return;
    TArray<float> Wall, Game, Draw, Gpu;
    Wall.Reserve(Samples.Num()); Game.Reserve(Samples.Num()); Draw.Reserve(Samples.Num()); Gpu.Reserve(Samples.Num());
    for (const FSample& Item : Samples) { Wall.Add(Item.Wall); Game.Add(Item.Game); Draw.Add(Item.Draw); Gpu.Add(Item.Gpu); }
    Wall.Sort(); Game.Sort(); Draw.Sort(); Gpu.Sort();
    const int32 N = Wall.Num();
    const TCHAR* Name = Phase == 1 ? TEXT("idle") : TEXT("sweep");
    UE_LOG(LogTemp, Display, TEXT("OK frame graph %s n=%d wall_p50=%.2f wall_p95=%.2f wall_max=%.2f game_p95=%.2f draw_p95=%.2f gpu_p95=%.2f over8=%d over16=%d"),
        Name, N, Wall[N / 2], Wall[(N * 95) / 100], Wall.Last(), Game[(N * 95) / 100], Draw[(N * 95) / 100], Gpu[(N * 95) / 100], Over8, Over16);
    if (Phase == 1)
    {
        FString Error;
        Self->PreviewActor->SetActorTickEnabled(true);
        const bool Started = Self->PreviewActor->BeginProductCloseup(FString(), nullptr, 3.f, Error);
        UE_LOG(LogTemp, Display, TEXT("OK frame graph sweep begin ok=%d %s"), Started ? 1 : 0, *Error);
        Phase = 2; Count = 0; Over8 = 0; Over16 = 0; Logged = 0; Samples.Reset(); Samples.Reserve(500);
        return;
    }
    Phase = 3;
}

void UOKCharacterController::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
	Super::NativeTick(Geometry,DeltaSeconds);
    OKNoteFrameGraph(this, DeltaSeconds);
 TickCharacterMCP();
 TickCardListMetadata();
 // Coalesce input within a frame, never wait for the user to stop dragging.
 if(bEditorModsDirty)ApplyPendingEditorMods();
 TickPreviewPreparation();
 TickAutoSequence();
 // The first load used to pay the animation catalogue asset load (~0.6 s); take it once the reader is idle.
 if(!bAnimationCatalogPrewarmed&&!bLoadInFlight&&!IsValid(PreparingPreview)&&GetWorld()&&GetWorld()->GetTimeSeconds()>1.){
  bAnimationCatalogPrewarmed=true;if(!AnimationCatalog)AnimationCatalog=UKKAnimationCatalog::LoadDefaultCatalog();
 }
 // The progressive-material stand-in parents compile once per process; the first card waited ~0.4 s for them.
 if(!bPreviewParentsPrewarmed&&bAnimationCatalogPrewarmed&&!bLoadInFlight&&!IsValid(PreparingPreview))
  bPreviewParentsPrewarmed=AKKPreviewActor::PrewarmPreviewMaterialParents(GetWorld());
 // An OK card's animation menu lists the imported shared clips; the first listing loads those assets (~0.27 s on
 // the game thread). Load them while idle instead of inside the first card's load.
 if(!bSharedClipsPrewarmed&&bPreviewParentsPrewarmed&&!bLoadInFlight&&!IsValid(PreparingPreview)){
  bSharedClipsPrewarmed=true;FOkSession::ImportedSharedClipNames(FString());
 }
 if(IsValid(PreviewActor)){
  PreviewActor->TickProgressiveMaterials();
  if(PreviewActor->bClothingHidden)for(UMeshComponent* Component:PreviewActor->HiddenClothingComponents)if(IsValid(Component))Component->SetVisibility(false,false);
 }
 TickClothingPreviews();
 TickNeighborPreload();
 if(VideoMotionPanel)VideoMotionPanel->Tick();
 // Video pair: the partner owns the clock. A reloaded main character drops its first
 // (hitched) frame, so copy the clock every frame; both then advance by the same delta.
 if(bVideoPairActive&&IsValid(PreviewActor)&&IsValid(PartnerActor)&&PreviewActor->PoseMode==2&&PartnerActor->PoseMode==2&&PreviewActor->IsActorTickEnabled()&&PartnerActor->IsActorTickEnabled())
  PreviewActor->AnimPhase=PartnerActor->AnimPhase;
 if(AnimationStatus&&FPlatformTime::Seconds()>=VideoClipLabelAt){
  VideoClipLabelAt=FPlatformTime::Seconds()+.05;
  const FString Label=VideoClipTimeLabel();if(!Label.IsEmpty())ShowAnimationStatus(Label);
 }
 if(bEditorDisplayDirty&&FPlatformTime::Seconds()-EditorDisplayChangedAt>=.5)SaveEditorDisplaySettings();
    if(!InteractionWidget&&PreviewActor&&PreviewActor->Session.IsPortable())TryStartPreparedPair();
    AKKPreviewActor* SpeechActor=IsValid(PairedPlayback)&&Selected.Sex!=1?PartnerActor.Get():PreviewActor.Get();
    if(bSpeechWasPlaying&&IsValid(SpeechActor)){
        // Paired playback turns actor tick off and advances the female mouth itself.
        if(!IsValid(PairedPlayback)&&!SpeechActor->IsActorTickEnabled()) SpeechActor->AdvanceSpeech();
        if(!SpeechActor->IsSpeaking()){
            bSpeechWasPlaying=false;if(SpeechStatus)SpeechStatus->SetText(FText::FromString(TEXT("語音播放完畢")));
        }
    }
	if(!PreviewImage||AnimatedPreviewTextures.IsEmpty())return;
	AnimatedPreviewElapsed+=DeltaSeconds;
	const int32 Frame=FKKAnimatedPng::FrameAt(AnimatedPreviewDurations,AnimatedPreviewElapsed,AnimatedPreviewPlays);
	if(Frame!=AnimatedPreviewFrame&&AnimatedPreviewTextures.IsValidIndex(Frame))
	{AnimatedPreviewFrame=Frame;PreviewImage->SetBrushFromTexture(AnimatedPreviewTextures[Frame],true);}
}

void UOKCharacterController::OnSaveApng()
{
	if(bAppearanceBusy||bPropertyBusy||bLoadInFlight)return;
	if(MaterializeAppearance([](UOKCharacterController* G){G->OnSaveApng();}))return;
	auto Report=[this](const FString& Text){if(InspectorText)InspectorText->SetText(FText::FromString(Text));if(GEngine)GEngine->AddOnScreenDebugMessage(1,8.f,FColor::Cyan,Text);};
	if(bApngCaptureInFlight||bPortableSaveInFlight)return;
	if(!Entries.IsValidIndex(SelectedIndex)||!IsValid(PreviewActor)||PreviewCardPath!=Entries[SelectedIndex].Path||PreviewOutfitIndex!=OutfitIndex||!PreviewActor->Session.IsPortable())
	{Report(TEXT("請先載入這張獨立卡片的 3D，再匯出 APNG。"));return;}
	if(UKKCardWorkspace::PortableSourcePath(Entries[SelectedIndex].Path)!=Entries[SelectedIndex].Path)
	{Report(TEXT("請先轉換為內嵌資源的新卡，再匯出 APNG；這張卡仍使用外部 okpreview。"));return;}
	const FString Directory=OutputDirectoryInput?OutputDirectoryInput->GetText().ToString().TrimStartAndEnd():FString();
	if(Directory.IsEmpty()){Report(TEXT("請先在卡片列表填寫另存資料夾。"));return;}
	const FString Output=UKKCardWorkspace::NewCardPath(FPaths::ConvertRelativePathToFull(Directory),Entries[SelectedIndex].Path);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);
	const FString SourceCardPath=Entries[SelectedIndex].Path;
	FKKCharacterFile Card=Selected;
	if(NicknameBox)Card.Nickname=NicknameBox->GetText().ToString();
	ApplyPendingEditorMods();
 PreviewActor->ApplyShapes(Card.Face.ShapeValueFace,Card.Body.ShapeValueBody);
	// Copy the shaped session now; the remesh runs on a worker after the card is saved.
	FString LowPolyError;
	auto LowPoly=FOkLowPoly::Capture(PreviewActor->Session,LowPolyError);
	bApngCaptureInFlight=true;SetIsEnabled(false);Report(TEXT("正在擷取 T-pose 正面、側面、背面三視圖…"));
	TWeakObjectPtr<UOKCharacterController> Weak(this);
	const int32 PoseBefore=PreviewActor->PoseMode;
	OKCardPreviewCapture::BeginTPoseViews(PreviewActor,[Weak,PoseBefore,Card=MoveTemp(Card),Output,SourceCardPath,LowPoly,LowPolyError](TArray<TArray<uint8>> Views,FString Error) mutable
	{
		TArray<uint8> Preview;
		bool Saved=false;
		if(Error.IsEmpty()&&FKKAnimatedPng::BuildOkCardPreview(Card.Header.PreviewPng,Views,Preview,Error))
		{Card.Header.PreviewPng=MoveTemp(Preview);Saved=UKKCardWriter::SaveWithEdits(Card,Output,Error);}
        const FString Message=Saved?FString::Printf(TEXT("已另存 APNG：%s\n768×1024，原卡圖 3 秒，T-pose 三視圖各 1 秒，無限循環。"),*Output):Error;
        if(auto* Widget=Weak.Get()){
         Widget->bApngCaptureInFlight=false;Widget->SetIsEnabled(true);Widget->RestoreAnimationAfterCapture(PoseBefore);
         if(Saved)Widget->FinishPortableSave(SourceCardPath,Output,Message,true);
         else if(Widget->InspectorText)Widget->InspectorText->SetText(FText::FromString(Message));
        }
        if(!Saved)return;
        if(!LowPoly){UE_LOG(LogTemp,Warning,TEXT("APNG low-poly skipped: %s"),*LowPolyError);return;}
        Async(EAsyncExecution::ThreadPool,[Weak,LowPoly,Output]()
        {
            FString Summary,Error;
            const bool Good=LowPoly->Export(Output,0,Summary,Error);
            AsyncTask(ENamedThreads::GameThread,[Weak,Good,Summary,Error,Output]()
            {
                const FString Line=Good?FString::Printf(TEXT("低模已輸出：%s\n%s"),*FOkLowPoly::GlbPath(Output),*Summary)
                    :FString::Printf(TEXT("低模輸出失敗：%s"),*Error);
                UE_LOG(LogTemp,Display,TEXT("APNG low-poly %s: %s"),Good?TEXT("PASS"):TEXT("FAILED"),*Line);
                if(GEngine)GEngine->AddOnScreenDebugMessage(2,8.f,Good?FColor::Cyan:FColor::Orange,Line);
                auto* Widget=Weak.Get();
                if(Widget&&Widget->InspectorText)
                    Widget->InspectorText->SetText(FText::FromString(Widget->InspectorText->GetText().ToString()+TEXT("\n")+Line));
            });
        });
	});
}

struct FOkAsyncLoad
{
	FString Mods;FVector2D Physics;
	// The selected card's sliders, applied on the worker so FinishPreparedPreview need not re-shape on the game thread.
	TArray<float> ShapeFace,ShapeBody;
	FString PairJson;
	FOkSession Session;
	bool bOk = false;
	FString Err;
};

FString UOKCharacterController::PortableGallerySignature(const FString& CardPath)
{
 const FString Source=UKKCardWorkspace::LoadableSourcePath(CardPath);if(Source.IsEmpty())return FString();
 const auto Card=IFileManager::Get().GetStatData(*CardPath),Pack=IFileManager::Get().GetStatData(*Source);if(!Card.bIsValid||!Pack.bIsValid)return FString();
 FString Canonical=FPaths::ConvertRelativePathToFull(Source);FPaths::NormalizeFilename(Canonical);
 return FString::Printf(TEXT("%s|%lld|%lld|%lld|%lld"),*Canonical,Card.FileSize,Card.ModificationTime.GetTicks(),Pack.FileSize,Pack.ModificationTime.GetTicks());
}

void UOKCharacterController::OnLoad3D()
{
 StartLoad3D(false);
}

void UOKCharacterController::StartLoad3D(bool bAppearanceGeometryOnly)
{
 if(bAppearanceBusy)return;
 if(MaterializeAppearance([bAppearanceGeometryOnly](UOKCharacterController* G){G->StartLoad3D(bAppearanceGeometryOnly);}))return;
	if (bIndependentCards && (!Entries.IsValidIndex(SelectedIndex) ||
        UKKCardWorkspace::LoadableSourcePath(Entries[SelectedIndex].Path).IsEmpty()))
    {
        if (InspectorText) InspectorText->SetText(FText::FromString(TEXT("請使用含獨立資源的 PNG，或先轉換同名 .okpreview 原型包。")));
        return;
    }
	KeepPartnerForReload();
	ApplyClickInputMode();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (bLoadInFlight)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(1, 2.f, FColor::Yellow, TEXT("正在加載，请稍候"));
		}
		RestorePairAfterReload();
		return;
	}
	const FString ClothesEnv = FString::Printf(TEXT("%u,%u,%u,%u,%u,%u,%u,%u,%u"),
		ClothesStates[0], ClothesStates[1], ClothesStates[2], ClothesStates[3], ClothesStates[4],
		ClothesStates[5], ClothesStates[6], ClothesStates[7], ClothesStates[8]);
	PendingPreviewResourceSignature=Entries.IsValidIndex(SelectedIndex)?PortableGallerySignature(Entries[SelectedIndex].Path):FString();
	if(PendingPreviewResourceSignature.IsEmpty()&&Entries.IsValidIndex(SelectedIndex)){
        const FString Path=FPaths::ConvertRelativePathToFull(Entries[SelectedIndex].Path);const auto Stat=IFileManager::Get().GetStatData(*Path);
        if(Stat.bIsValid)PendingPreviewResourceSignature=FString::Printf(TEXT("%s|%lld|%lld"),*Path,Stat.FileSize,Stat.ModificationTime.GetTicks());
    }
    if(!PendingPreviewResourceSignature.IsEmpty()){
        const UKKGameRoot* KeyRoot=bIndependentCards?nullptr:UKKGameRoot::Get();
        PendingPreviewResourceSignature+=FString::Printf(TEXT("|outfit=%d|mode=%s"),OutfitIndex,bIndependentCards?TEXT("portable"):(KeyRoot?*KeyRoot->Root:TEXT("native")));
        if(ClothingPreviewContext!=PendingPreviewResourceSignature)ResetClothingPreviews(PendingPreviewResourceSignature);
        PendingPreviewResourceSignature+=TEXT("|clothes=")+ClothesEnv;
    }
    if(TryClothingPreview(PendingPreviewResourceSignature)){
        RestorePairAfterReload();
        const bool Enter=bEnterInteractionAfterLoad;bEnterInteractionAfterLoad=false;if(Enter)OnEnterInteraction();return;
    }
	if(IsValid(PreviewActor)&&PreviewActor->Session.IsOpen()&&PreviewActor->Session.IsPortable()&&!PendingPreviewResourceSignature.IsEmpty()&&PreviewResourceSignature==PendingPreviewResourceSignature&&FPaths::IsSamePath(PreviewCardPath,Entries[SelectedIndex].Path)&&PreviewOutfitIndex==OutfitIndex)
	{
		if(auto* PC=Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer()))PC->SetPaintPreviewActive(false);
		bLastClothingSwitchCached=true;SetModelLayout(true);
        ApplyPendingEditorMods();
        PreviewActor->Session.ApplyBodyPhysics(Selected.Body.BustSoftness,Selected.Body.BustWeight);
        if(PreviewActor->bShapeDirty||PreviewActor->Session.ShapeFace()!=Selected.Face.ShapeValueFace||PreviewActor->Session.ShapeBody()!=Selected.Body.ShapeValueBody)
            PreviewActor->ApplyShapes(Selected.Face.ShapeValueFace,Selected.Body.ShapeValueBody);
        UE_LOG(LogTemp,Display,TEXT("OK gallery reused portable character with selected card parameters: %s"),*PreviewCardPath);
        RestorePairAfterReload();
        const bool Enter=bEnterInteractionAfterLoad;bEnterInteractionAfterLoad=false;if(Enter)OnEnterInteraction();return;
	}
	if(auto* PC=Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer()))PC->SetPaintPreviewActive(false);
	if(!bAppearanceGeometryOnly&&TryNeighborPreload())return;
	if (PreviewActor)
	{
		// Keep the current character on screen until the new outfit is ready.
		// Destroying here blanked the view for the whole extract.
		if (StalePreview && StalePreview != PreviewActor)
		{
			StalePreview->Destroy();
		}
		StalePreview = PreviewActor;
		PreviewActor = nullptr;
	}
	UKKGameRoot* Root = bIndependentCards ? nullptr : UKKGameRoot::Get();
	const FString CardPath = Entries.IsValidIndex(SelectedIndex) ?
        (bIndependentCards ? UKKCardWorkspace::LoadableSourcePath(Entries[SelectedIndex].Path) : Entries[SelectedIndex].Path) : FString();
	const FString GameRoot = !bIndependentCards && Root ? Root->Root : FString();
	OKRequestCardWarm(CardPath);
	const int32 Outfit = OutfitIndex;
	const int32 Gen = ++LoadGeneration;
	AppearanceGeometryLoadGeneration = bAppearanceGeometryOnly ? Gen : INDEX_NONE;
	SetLoadBusy(true);
	OKThrottleForNativeOpen(true);
	UE_LOG(LogTemp, Display, TEXT("OK load3d card=%s root=%s outfit=%d"), *CardPath, *GameRoot, Outfit);
	if (InspectorText)
	{
		InspectorText->SetText(FText::FromString(TEXT("正在打開角色…")));
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1, 8.f, FColor::Yellow, FString::Printf(TEXT("正在打開 %s"), *FPaths::GetCleanFilename(CardPath)));
	}
	if ((!bIndependentCards && !Root) || CardPath.IsEmpty())
	{
		SetLoadBusy(false);
		if (InspectorText)
		{
			InspectorText->SetText(FText::FromString(Root ? TEXT("没有選中角色卡") : TEXT("找不到游戏目录")));
		}
		return;
	}
	TWeakObjectPtr<UOKCharacterController> Weak(this);
	TSharedPtr<FOkAsyncLoad, ESPMode::ThreadSafe> Job = MakeShared<FOkAsyncLoad, ESPMode::ThreadSafe>();
	Job->Mods=EditorModsJson();Job->Physics=FVector2D(Selected.Body.BustSoftness,Selected.Body.BustWeight);Job->ShapeFace=Selected.Face.ShapeValueFace;Job->ShapeBody=Selected.Body.ShapeValueBody;
	const FString MenuSource=GameSourceMatchesSex(SelectedGameSource,Selected.Sex)?SelectedGameSource:FString();
	const FString MenuClip=MenuSource==SelectedGameSource?SelectedGameClip:FString();
	// Prefab nodes and the body alpha mask are chosen while the session opens.
	// Pass the requested state explicitly so background preparation cannot
    // change another card's clothing through process-global environment.
    TArray<uint8> RequestedClothes;RequestedClothes.Append(ClothesStates,9);
	FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const bool PrepareSkeleton=OKLiveGpuSkinRequested();
	Async(EAsyncExecution::Thread, [Weak, Gen, GameRoot, CardPath, Outfit, Job, MenuSource, MenuClip, RequestedClothes, PrepareSkeleton]()
	{
		FOkSession::InitBindings();
		const double WorkerStart=FPlatformTime::Seconds();
		Job->bOk = Job->Session.Open(GameRoot, CardPath, Outfit, Job->Err,RequestedClothes.GetData());
		const double OpenDone=FPlatformTime::Seconds();
		if(Job->bOk){
			// OK_FIRSTLOAD=B1: the card's textures decode on the task pool while this worker applies physics, mods and
			// shape (those change no texture; the bytes are copied first, so the session may change meanwhile).
			const bool DecodeAlongside=AKKPreviewActor::FirstLoadOption(TEXT("B1"));
			UE::Tasks::FTask TextureDecode;
			if(DecodeAlongside){
				auto Textures=AKKPreviewActor::CollectSessionTextures(Job->Session,false);
				TextureDecode=UE::Tasks::Launch(UE_SOURCE_LOCATION,[Textures](){AKKPreviewActor::PrecacheTextureJobs(*Textures);});
			}
			const double T0=FPlatformTime::Seconds();
			Job->Session.ApplyBodyPhysics(Job->Physics.X,Job->Physics.Y);const double T1=FPlatformTime::Seconds();
			// The card's mods and then its sliders (as FinishPreparedPreview applied them on the game thread, ~0.3-0.6 s
			// there), the shape built once: applying the mods alone rebuilt it with the session's values first.
			const bool Merged=!Job->Mods.IsEmpty()&&!Job->ShapeBody.IsEmpty();
			if(Merged)Job->Session.ApplyEditorModsAndShape(Job->Mods,Outfit,Job->ShapeBody,Job->ShapeFace);
			else if(!Job->Mods.IsEmpty())Job->Session.ApplyEditorMods(Job->Mods,Outfit);
			const double T2=FPlatformTime::Seconds();
			const bool ShapeDiffers=!Merged&&!Job->ShapeBody.IsEmpty()&&(Job->ShapeFace!=Job->Session.ShapeFace()||Job->ShapeBody!=Job->Session.ShapeBody());
			FString ShapeDiffText;
			if(ShapeDiffers&&FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE"))==TEXT("1")){
				// Which slider differs, and by how much (the card's values against the opened session's).
				auto Diff=[](const TArray<float>& Card,const TArray<float>& Native){int32 N=0;float Max=0;int32 First=-1;for(int32 I=0;I<FMath::Min(Card.Num(),Native.Num());++I){const float D=FMath::Abs(Card[I]-Native[I]);if(D>0){++N;if(First<0)First=I;}Max=FMath::Max(Max,D);}return FString::Printf(TEXT("n=%d/%d count_diff=%d first=%d max=%.9g"),Card.Num(),Native.Num(),N,First,Max);};
				ShapeDiffText=TEXT("face ")+Diff(Job->ShapeFace,Job->Session.ShapeFace())+TEXT(" body ")+Diff(Job->ShapeBody,Job->Session.ShapeBody());
			}
			if(ShapeDiffers)Job->Session.ApplyShape(Job->ShapeBody,Job->ShapeFace);const double T3=FPlatformTime::Seconds();
			if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE"))==TEXT("1")){
				UE_LOG(LogTemp,Display,TEXT("OKGUI_WORKER physics_ms=%.1f mods_ms=%.1f(%d) shape_ms=%.1f(%d)"),(T1-T0)*1000.,(T2-T1)*1000.,Job->Mods.IsEmpty()?0:1,(T3-T2)*1000.,ShapeDiffers?1:0);
				if(!ShapeDiffText.IsEmpty())UE_LOG(LogTemp,Display,TEXT("OKGUI_WORKER shape_diff %s"),*ShapeDiffText);
			}
			if(DecodeAlongside){
				// The skeleton reference is prepared after the shape, as PrecacheSessionTextures does it.
				if(PrepareSkeleton)Job->Session.SkelBoneCount();
				TextureDecode.Wait();
			}
			else AKKPreviewActor::PrecacheSessionTextures(Job->Session,PrepareSkeleton);
		}
		if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE"))==TEXT("1"))UE_LOG(LogTemp,Display,TEXT("OKGUI_WORKER open_ms=%.3f texture_prepare_ms=%.3f"),(OpenDone-WorkerStart)*1000.,(FPlatformTime::Seconds()-OpenDone)*1000.);
		if(Job->bOk&&!Job->Session.IsPortable()){
			// The session still belongs exclusively to this worker. Warm initial
			// menu catalogs before publishing it; FinishLoad3D must not parse them.
			Job->Session.ClipSources();Job->Session.ClipNames();
			if(!MenuSource.IsEmpty())Job->Session.ClipNamesForSource(MenuSource);
			if(!MenuClip.IsEmpty())FOkSession::PrepareAnimationClip(GameRoot,MenuClip);
		}
		// Reading the pair metadata can reopen native bundles. Keep that work on
		// the session's owning worker too; only build combo widgets on the game thread.
		const double CatalogueStart=FPlatformTime::Seconds();
		if(Job->bOk)Job->PairJson=Job->Session.NativeAnimationPairs();
		if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE"))==TEXT("1"))UE_LOG(LogTemp,Display,TEXT("OKGUI_WORKER pair_catalogue_ms=%.3f"),(FPlatformTime::Seconds()-CatalogueStart)*1000.);
		AsyncTask(ENamedThreads::GameThread, [Weak, Gen, Job]()
		{
			if (UOKCharacterController* Widget = Weak.Get())
			{
				Widget->FinishLoad3D(Gen, MoveTemp(Job->Session), Job->bOk, Job->Err,Job->Mods,Job->Physics,Job->PairJson);
			}
		});
	});
}

// While a native open runs on its worker, the reader only shows the previous character: rendering it uncapped took
// CPU the open needed (an uncached OK card opened in ~5-7 s instead of ~8-12 s at 15 fps). Restored for the UE build.
static void OKThrottleForNativeOpen(bool bThrottle)
{
 static float Saved=-1.f;
 IConsoleVariable* MaxFPS=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
 if(!MaxFPS)return;
 // A capture session stays at the reader's own frame rate. The 15 fps cap is only for an idle gallery open.
 if(bThrottle&&FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NO_CLOTHING_WARM"))==TEXT("1"))return;
 if(bThrottle&&Saved<0.f){Saved=MaxFPS->GetFloat();MaxFPS->Set(15.f,ECVF_SetByCode);}
 else if(!bThrottle&&Saved>=0.f){MaxFPS->Set(Saved,ECVF_SetByCode);Saved=-1.f;}
}

void UOKCharacterController::FinishLoad3D(int32 Generation, FOkSession Session, bool bOk, const FString& Err,const FString& PreparedMods,FVector2D PreparedPhysics,const FString& PreparedPairs)
{
 OKThrottleForNativeOpen(false);
 if(Generation!=LoadGeneration)return;
 PendingLoadPairJson=PreparedPairs;PendingLoadPairGeneration=bOk?Generation:INDEX_NONE;
 UWorld* World=GetWorld();
 const bool KeepView=AppearanceGeometryLoadGeneration==Generation&&IsValid(StalePreview)&&IsValid(StalePreview->StudioCam);
 AKKPreviewActor* Actor=World?World->SpawnActor<AKKPreviewActor>(KeepView?StalePreview->GetActorLocation():FVector(0,0,100),KeepView?StalePreview->GetActorRotation():FRotator::ZeroRotator):nullptr;
 if(!Actor){SetLoadBusy(false);PreviewActor=StalePreview;StalePreview=nullptr;if(InspectorText)InspectorText->SetText(FText::FromString(TEXT("載入立體角色失敗：無法建立預覽")));return;}
 Actor->SetActorHiddenInGame(true);Actor->SetActorTickEnabled(false);
 Actor->Session=MoveTemp(Session);
 Actor->UseGalleryPhysics();
 if(PreparedPhysics!=FVector2D(Selected.Body.BustSoftness,Selected.Body.BustWeight))Actor->Session.ApplyBodyPhysics(Selected.Body.BustSoftness,Selected.Body.BustWeight);
 const FString LoadedMods=EditorModsJson();if(LoadedMods!=PreparedMods)Actor->Session.ApplyEditorMods(LoadedMods,OutfitIndex);
 bEditorModsDirty=false;
 FString Error=Err;
 if(bOk)bOk=Actor->BeginSessionLoad(Error);
 if(!bOk){FinishPreparedPreview(Actor,false,Error);return;}
 PreparingPreview=Actor;PreparingPreviewGeneration=Generation;
}

void UOKCharacterController::TickPreviewPreparation()
{
 if(!IsValid(PreparingPreview))return;
 if(PreparingPreviewGeneration!=LoadGeneration){PreparingPreview->Destroy();PreparingPreview=nullptr;return;}
 FString Error;
 // A fixed one-part-per-frame schedule adds idle frames even when parts are
 // cheap or cached. Spend a bounded amount of this frame, then yield to Slate.
 // A single mesh build is indivisible, so this is a soft time budget. 25 ms keeps
 // the UI near 25 fps while a 50-part card no longer pays ~50 frames of overhead.
 // OK_PART_BUDGET_MS / OK_PART_STEPS (load-speed experiments; defaults 25 ms, 8 parts): a larger budget builds the
 // hidden character in fewer frames, at the cost of a less responsive UI while it loads.
 auto EnvInt=[](const TCHAR* Name,int32 Default){const int32 V=FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(Name));return V>0?V:Default;};
 static const int32 BudgetMs=EnvInt(TEXT("OK_PART_BUDGET_MS"),25),Steps=EnvInt(TEXT("OK_PART_STEPS"),8);
 const double Deadline=FPlatformTime::Seconds()+BudgetMs/1000.;
 bool Ok=true;
 for(int32 Step=0;Step<Steps;++Step){
  Ok=PreparingPreview->ContinueSessionLoad(Error);
  if(!Ok||PreparingPreview->IsSessionLoadComplete()||FPlatformTime::Seconds()>=Deadline)break;
 }
 if(Ok&&PreparingPreview->IsSessionLoadComplete()){
  PreparingPreview->SetActorTickEnabled(false);
  if(!PreparingPreview->BeginProgressiveMaterials())return;
 }
 if(!Ok||PreparingPreview->IsSessionLoadComplete()){
  AKKPreviewActor* Actor=PreparingPreview;PreparingPreview=nullptr;
  FinishPreparedPreview(Actor,Ok,Error);
 }
}

void UOKCharacterController::FinishPreparedPreview(AKKPreviewActor* Actor,bool bOk,const FString& Err)
{
 double ProfileAt=FPlatformTime::Seconds();
 const bool ProfileLoad=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_LOAD_PROFILE"))==TEXT("1");
 auto ProfileStep=[&](const TCHAR* Name){const double Now=FPlatformTime::Seconds();if(ProfileLoad)UE_LOG(LogTemp,Display,TEXT("OKGUI_PROFILE step=%s ms=%.3f"),Name,(Now-ProfileAt)*1000.);ProfileAt=Now;};
 UKKGameRoot* Root=bIndependentCards?nullptr:UKKGameRoot::Get();
 const FString CardPath=Entries.IsValidIndex(SelectedIndex)?Entries[SelectedIndex].Path:FString();
 const FString OutDir=FPaths::ProjectSavedDir()/TEXT("Converted")/FPaths::GetBaseFilename(CardPath);
 FString LocalErr=Err;
 const bool GeometryOnly=AppearanceGeometryLoadGeneration==LoadGeneration&&IsValid(StalePreview)&&IsValid(StalePreview->StudioCam);
 PreviewActor=Actor;

	if (!bOk && Root && !bIndependentCards)
	{
		if (InspectorText)
		{
			InspectorText->SetText(FText::FromString(TEXT("即時載入失敗，改用靜態模型擷取…")));
		}
		if (OKExtractCard(Root->Root, CardPath, OutDir, LocalErr, OutfitIndex))
		{
			bOk = Actor->LoadDirectory(OutDir, LocalErr);
		}
	}
	if (bOk)
	{
        // LoadSession already displays the card's saved shape. Reapplying the
        // identical values rebuilds every GPU follower for no visible change.
        if(Selected.Face.ShapeValueFace!=Actor->Session.ShapeFace() || Selected.Body.ShapeValueBody!=Actor->Session.ShapeBody()){
            if(ProfileLoad){
                const TArray<float> NF=Actor->Session.ShapeFace(),NB=Actor->Session.ShapeBody();
                auto First=[](const TArray<float>& A,const TArray<float>& B){for(int32 I=0;I<FMath::Min(A.Num(),B.Num());++I)if(A[I]!=B[I])return FString::Printf(TEXT("%d:%.9g/%.9g"),I,A[I],B[I]);return FString(TEXT("-"));};
                UE_LOG(LogTemp,Display,TEXT("OKGUI_PROFILE shape differs face=%d/%d %s body=%d/%d %s"),Selected.Face.ShapeValueFace.Num(),NF.Num(),*First(Selected.Face.ShapeValueFace,NF),
                    Selected.Body.ShapeValueBody.Num(),NB.Num(),*First(Selected.Body.ShapeValueBody,NB));
            }
            Actor->ApplyShapes(Selected.Face.ShapeValueFace, Selected.Body.ShapeValueBody);
        }
		ProfileStep(TEXT("actor_shape"));
		ApplyPenisVisible();
		if(!GeometryOnly)RefreshPairs();
		ProfileStep(TEXT("catalog_controls"));
		if(GeometryOnly){Actor->SpeechLines=StalePreview->SpeechLines;Actor->SpeechPitch=StalePreview->SpeechPitch;Actor->bSpeechDrivesMouth=StalePreview->bSpeechDrivesMouth;}
		else {SyncSpeechVoiceToCard();if(!bEnterInteractionAfterLoad)OnLoadSpeech();}
		ProfileStep(TEXT("speech"));
		Actor->BeginFaceDisplayUpdate();
		if (MouthPatternCombo)
		{
			MouthPatternCombo->ClearOptions();
			for (int32 Pattern = 0; Pattern < Actor->GetMouthPatternCount(); ++Pattern)
			{
				MouthPatternCombo->AddOption(Actor->GetMouthPatternLabel(Pattern));
			}
			SelectedMouthPattern = FMath::Clamp(SelectedMouthPattern, 0, Actor->GetMouthPatternCount() - 1);
			MouthPatternCombo->SetSelectedIndex(SelectedMouthPattern);
		}
		ProfileStep(TEXT("face_mouth_combo"));
		Actor->SetMouthPattern(SelectedMouthPattern, false);
		Actor->SetMouthOpen(SelectedMouthOpen);
		ProfileStep(TEXT("face_mouth"));
		RefreshFaceControls();
		ProfileStep(TEXT("face_controls"));
		Actor->EndFaceDisplayUpdate();
		ProfileStep(TEXT("face_display"));
		RefreshHandControls();
		ProfileStep(TEXT("face_hand"));
		if (!GeometryOnly && GameClipCombo && Actor->Session.IsOpen())
		{
			bSuppressClipCombo = true;
			if (GameSceneCombo)
			{
				GameSceneCombo->SetSelectedIndex(SelectedGameSceneMode);
				Actor->SetGameSceneMode(SelectedGameSceneMode);
			}
			if (GameSourceCombo)
			{
				if(!GameSourceMatchesSex(SelectedGameSource,Selected.Sex)) {SelectedGameSource.Empty();SelectedGameClip.Empty();SelectedStudioRow=NAME_None;}
				GameSourceCombo->ClearOptions();
				GameSourceCombo->AddOption(TEXT("基本動作"));
				for (const FString& Source : Actor->Session.ClipSources())
				{
					if(GameSourceMatchesSex(Source,Selected.Sex)) GameSourceCombo->AddOption(GameSourceLabel(Source));
				}
				ProfileStep(TEXT("animation_clip_sources"));
				AnimationCatalog = UKKAnimationCatalog::LoadDefaultCatalog();
				if (AnimationCatalog)
				{
					for (const FString& Source : AnimationCatalog->GetAnimationSources())
					{
						if(!GameSourceMatchesSex(Source,Selected.Sex)) continue;
						const FString Label = GameSourceLabel(Source);
						if (GameSourceCombo->FindOptionIndex(Label) < 0) GameSourceCombo->AddOption(Label);
					}
				}
				ProfileStep(TEXT("animation_catalog_sources"));
				RefreshStudioCatalog();
				ProfileStep(TEXT("animation_studio_catalog"));
				const FString SourceLabel = GameSourceLabel(SelectedGameSource);
				if (!SelectedGameSource.IsEmpty() && GameSourceCombo->FindOptionIndex(SourceLabel) <= 0)
				{
					GameSourceCombo->AddOption(SourceLabel);
				}
				GameSourceCombo->SetSelectedIndex(SelectedGameSource.IsEmpty() ? 0
					: GameSourceCombo->FindOptionIndex(SourceLabel));
			}
			ProfileStep(TEXT("animation_source_combo"));
			BuildAnimationLookup();
			ProfileStep(TEXT("animation_lookup"));
			const FString SelectedLabel = AnimationOptionForSelection();
			const int32 SavedClipIndex = GameClipCombo->FindOptionIndex(SelectedLabel);
			GameClipCombo->SetSelectedIndex(SavedClipIndex > 0 ? SavedClipIndex : 0);
			bSuppressClipCombo = false;
			if (SavedClipIndex > 0)
			{
				FString StudioError;
				if(SelectedStudioRow.IsNone()) Actor->SetGameAnimation(SelectedGameClip);
				else Actor->SetStudioAnimation(AnimationCatalog,SelectedStudioRow,StudioError,SavedStudioOptions.X,SavedStudioOptions.Y,SavedStudioOptions.Z);
				RefreshGameAnimationParameters();
			}
		}
	}
 if(bOk&&GeometryOnly){
  Actor->SetGameSceneMode(SelectedGameSceneMode);
  if(!SelectedGameClip.IsEmpty()){
   FString AnimationError;
   if(SelectedStudioRow.IsNone())Actor->SetGameAnimation(SelectedGameClip,false);
   else Actor->SetStudioAnimation(AnimationCatalog,SelectedStudioRow,AnimationError,SavedStudioOptions.X,SavedStudioOptions.Y,SavedStudioOptions.Z);
  }else if(Actor->PoseMode!=StalePreview->PoseMode)Actor->SetPoseMode(StalePreview->PoseMode);
  for(int32 I=0;I<StalePreview->Session.AnimationFloatCount();++I){FString Name;float Value,Min,Max;if(StalePreview->Session.AnimationFloatAt(I,Name,Value,Min,Max))Actor->Session.SetAnimationFloat(Name,Value);}
  RefreshHandControls();
  Actor->AnimationPlaybackSpeed=StalePreview->AnimationPlaybackSpeed;Actor->AnimPhase=StalePreview->AnimPhase;Actor->FaceClock=StalePreview->FaceClock;
  Actor->FollowHeadLookCamera(StalePreview->bHeadLookFollowCamera);
  Actor->ApplyPoseFrame(0.f);Actor->SetActorTickEnabled(StalePreview->IsActorTickEnabled());
 }
	ProfileStep(TEXT("animation_catalog"));
	FString Report;
	if (bOk)
	{
		if (StalePreview && StalePreview != Actor)
		{
   if(GeometryOnly){
    Actor->SetActorTransform(StalePreview->GetActorTransform());
    Actor->StudioCam=StalePreview->StudioCam;StalePreview->StudioCam=nullptr;
    Actor->OrbitFocus=StalePreview->OrbitFocus;Actor->OrbitYaw=StalePreview->OrbitYaw;Actor->OrbitPitch=StalePreview->OrbitPitch;Actor->OrbitDistance=StalePreview->OrbitDistance;
   }
			CacheClothingPreview(StalePreview,PreviewResourceSignature);
		}
		StalePreview = nullptr;
		Report = FString::Printf(TEXT("已加载3D\n"));
		PreviewCardPath=CardPath;
		PreviewResourceSignature=PendingPreviewResourceSignature;
		FMemory::Memcpy(LoadedClothesStates,ClothesStates,sizeof(ClothesStates));
		FMemory::Memcpy(Actor->DisplayClothesStates,ClothesStates,sizeof(ClothesStates));
		PreviewOutfitIndex=OutfitIndex;
		if (Actor->MissingMods.Num() > 0)
		{
			Report += TEXT("缺少模组：\n");
			for (const FString& M : Actor->MissingMods)
			{
				Report += FString::Printf(TEXT("  [缺] %s\n"), *M);
			}
		}
		SetModelLayout(true);
	}
	else
	{
		Actor->Destroy();
		if (StalePreview)
		{
			PreviewActor = StalePreview;
			StalePreview = nullptr;
		}
		Report = LocalErr.IsEmpty() ? TEXT("加載3D失败") : LocalErr;
		FMemory::Memcpy(ClothesStates,LoadedClothesStates,sizeof(ClothesStates));
		RefreshClothesStateLabels();
		SetModelLayout(IsValid(PreviewActor));
	}
	UE_LOG(LogTemp, Display, TEXT("OK load3d result ok=%d %s"), bOk ? 1 : 0, *Report);
	if (InspectorText)
	{
		InspectorText->SetText(FText::FromString(Report));
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1, 8.f, bOk ? FColor::Green : FColor::Red, Report);
	}
	if(IsValid(PreviewActor)&&(!bOk||!GeometryOnly))PreviewActor->SetupStudioView();
	if(bOk)Actor->SetActorHiddenInGame(false);
	SetLoadBusy(false);
	RestorePairAfterReload();
    if(bOk)WarmClothingPreviews();
	ProfileStep(TEXT("studio"));
	const bool Enter=bEnterInteractionAfterLoad;bEnterInteractionAfterLoad=false;
	if(bOk&&Enter)OnEnterInteraction();
}

void UOKCharacterController::RefreshOutfitList()
{
	if (!OutfitCombo)
	{
		return;
	}
	bSuppressOutfitCombo = true;
	OutfitCombo->ClearOptions();
	const int32 N = FMath::Max(Selected.Outfits.Num(), 1);
	for (int32 i = 0; i < N; ++i)
	{
		OutfitCombo->AddOption(FString::Printf(TEXT("服裝 %d"), i + 1));
	}
	const int32 Index = FMath::Clamp(OutfitIndex, 0, N - 1);
	OutfitIndex = Index;
	OutfitCombo->SetSelectedIndex(Index);
	if (OutfitLabel)
	{
		OutfitLabel->SetText(FText::FromString(OutfitCombo->GetOptionAtIndex(Index)));
	}
	bSuppressOutfitCombo = false;
}

void UOKCharacterController::OnOutfitSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bLoadInFlight || bAppearanceBusy || bPropertyBusy) { RefreshOutfitList(); return; }
	if (bSuppressOutfitCombo || SelectionType == ESelectInfo::Direct || !OutfitCombo)
	{
		return;
	}
	const int32 Index = OutfitCombo->FindOptionIndex(SelectedItem);
	if (Index == INDEX_NONE || Index == OutfitIndex)
	{
		return;
	}
	OutfitIndex = Index;
	if (OutfitLabel)
	{
		OutfitLabel->SetText(FText::FromString(SelectedItem));
	}
	if (PreviewActor)
	{
		OnLoad3D();
	}
}

void UOKCharacterController::OnOutfitPrev()
{
	if (bLoadInFlight || bAppearanceBusy || bPropertyBusy) return;
	const int32 N = FMath::Max(Selected.Outfits.Num(), 1);
	OutfitIndex = (OutfitIndex + N - 1) % N;
	RefreshOutfitList();
	if (PreviewActor)
	{
		OnLoad3D();
	}
}

void UOKCharacterController::OnOutfitNext()
{
	if (bLoadInFlight || bAppearanceBusy || bPropertyBusy) return;
	const int32 N = FMath::Max(Selected.Outfits.Num(), 1);
	OutfitIndex = (OutfitIndex + 1) % N;
	RefreshOutfitList();
	if (PreviewActor)
	{
		OnLoad3D();
	}
}

void UOKCharacterController::RefreshHandControls()
{
	if (HandCombos.Num() != 4 || HandBlendSliders.Num() != 2) return;
	bSuppressHandControls = true;
	const int32 Count = PreviewActor ? PreviewActor->GetHandShapeCount() : 0;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		UComboBoxString* A = HandCombos[Side * 2];
		UComboBoxString* B = HandCombos[Side * 2 + 1];
		A->ClearOptions(); B->ClearOptions();
		A->AddOption(TEXT("跟隨動作"));
		for (int32 Pattern = 0; Pattern < Count; ++Pattern)
		{
			const FString Name = FString::Printf(TEXT("手勢 %d"), Pattern);
			A->AddOption(Name); B->AddOption(Name);
		}
		SelectedHandA[Side] = FMath::Clamp(SelectedHandA[Side], -1, Count - 1);
		SelectedHandB[Side] = FMath::Clamp(SelectedHandB[Side], 0, FMath::Max(0, Count - 1));
		A->SetSelectedIndex(SelectedHandA[Side] + 1); B->SetSelectedIndex(SelectedHandB[Side]);
		A->SetIsEnabled(Count > 0); B->SetIsEnabled(Count > 0 && SelectedHandA[Side] >= 0);
		HandBlendSliders[Side]->SetIsEnabled(Count > 0 && SelectedHandA[Side] >= 0);
		HandBlendSliders[Side]->SetValue(SelectedHandBlend[Side]);
	}
	bSuppressHandControls = false;
	if (PreviewActor) PreviewActor->BeginHandDisplayUpdate();
	for (int32 Side = 0; Side < 2; ++Side) ApplyHandControl(Side);
	if (PreviewActor) PreviewActor->EndHandDisplayUpdate();
}

void UOKCharacterController::ApplyHandControl(int32 Side)
{
	if (bSuppressHandControls || !PreviewActor || HandCombos.Num() != 4 || Side < 0 || Side > 1) return;
	SelectedHandA[Side] = HandCombos[Side * 2]->GetSelectedIndex() - 1;
	SelectedHandB[Side] = FMath::Max(0, HandCombos[Side * 2 + 1]->GetSelectedIndex());
	HandCombos[Side*2+1]->SetIsEnabled(SelectedHandA[Side]>=0);
	if(HandBlendSliders.IsValidIndex(Side))HandBlendSliders[Side]->SetIsEnabled(SelectedHandA[Side]>=0);
	PreviewActor->SetHandShape(Side, SelectedHandA[Side] >= 0, FMath::Max(0, SelectedHandA[Side]),
		SelectedHandB[Side], SelectedHandBlend[Side]);
}

void UOKCharacterController::OnOpenHandControls(){if(!bModelLayout)return;OnCloseAppearance();if(HandMotionScroll)HandMotionScroll->ScrollToStart();}
void UOKCharacterController::OnResetLeftHand(){SelectedHandA[0]=-1;SelectedHandB[0]=0;SelectedHandBlend[0]=0;RefreshHandControls();}
void UOKCharacterController::OnResetRightHand(){SelectedHandA[1]=-1;SelectedHandB[1]=0;SelectedHandBlend[1]=0;RefreshHandControls();}

void UOKCharacterController::OnLeftHandA(FString Item, ESelectInfo::Type Type) { ApplyHandControl(0); }
void UOKCharacterController::OnLeftHandB(FString Item, ESelectInfo::Type Type) { ApplyHandControl(0); }
void UOKCharacterController::OnRightHandA(FString Item, ESelectInfo::Type Type) { ApplyHandControl(1); }
void UOKCharacterController::OnRightHandB(FString Item, ESelectInfo::Type Type) { ApplyHandControl(1); }
void UOKCharacterController::OnLeftHandBlend(float Value)
{
	if (bSuppressHandControls) return;
	SelectedHandBlend[0] = Value;
	if (HandBlendSliders.Num() == 2) HandBlendSliders[0]->SetValue(Value);
	ApplyHandControl(0);
}
void UOKCharacterController::OnRightHandBlend(float Value)
{
	if (bSuppressHandControls) return;
	SelectedHandBlend[1] = Value;
	if (HandBlendSliders.Num() == 2) HandBlendSliders[1]->SetValue(Value);
	ApplyHandControl(1);
}

void UOKCharacterController::RefreshFaceControls()
{
    bSuppressFaceControls = true;
    const bool Automatic = PreviewActor && PreviewActor->bAutomaticBlink;
    if (AutomaticBlinkCheck) { AutomaticBlinkCheck->SetIsChecked(Automatic); AutomaticBlinkCheck->SetIsEnabled(PreviewActor != nullptr); }
    for(int32 Index=0;Index<GazeSliders.Num();++Index) {
        GazeSliders[Index]->SetIsEnabled(PreviewActor && PreviewActor->Session.FaceRuntimeSetting(11)!=0.f);
        GazeSliders[Index]->SetValue(PreviewActor?PreviewActor->EyeGaze[Index]:0.f);
    }
    if(EyeLookCameraCheck) {
        EyeLookCameraCheck->SetIsEnabled(PreviewActor && PreviewActor->Session.EyeLookPatternCount()>0);
        EyeLookCameraCheck->SetIsChecked(PreviewActor && PreviewActor->bEyeLookFollowCamera);
    }
    for (int32 Index = 0; Index < 2; ++Index)
    {
        if (!FaceCombos.IsValidIndex(Index) || !FaceOpenSliders.IsValidIndex(Index)) continue;
        const int32 Count = PreviewActor ? PreviewActor->GetFacePatternCount(Index + 1) : 0;
        FaceCombos[Index]->ClearOptions();
        for (int32 Pattern = 0; Pattern < Count; ++Pattern)
            FaceCombos[Index]->AddOption(FString::Printf(TEXT("%s %d"),
                Index == 0 ? TEXT("眼型") : TEXT("眉型"), Pattern));
        FaceCombos[Index]->SetIsEnabled(Count > 0);
        FaceOpenSliders[Index]->SetIsEnabled(Count > 0 && !Automatic);
        SelectedFacePattern[Index] = FMath::Clamp(SelectedFacePattern[Index], 0, FMath::Max(Count - 1, 0));
        FaceCombos[Index]->SetSelectedIndex(SelectedFacePattern[Index]);
        const float Rate = SelectedFaceOpen[Index] >= 0.f ? SelectedFaceOpen[Index]
            : (PreviewActor ? PreviewActor->Session.FaceDefaultRate(Index + 1) : (Index == 0 ? .92f : 1.f));
        FaceOpenSliders[Index]->SetValue(Rate);
        if (PreviewActor && Count > 0)
        {
            if (Automatic) PreviewActor->SetFacePattern(Index + 1, SelectedFacePattern[Index], false);
            else PreviewActor->SetFaceControl(Index + 1, SelectedFacePattern[Index], Rate);
            FaceCombos[Index]->SetToolTipText(FText::FromString(
                PreviewActor->GetFacePatternLabel(Index + 1, SelectedFacePattern[Index])));
        }
    }
    bSuppressFaceControls = false;
}
void UOKCharacterController::UpdateFaceControl(int32 Index, bool bBlend)
{
    if (bSuppressFaceControls || !PreviewActor || !FaceCombos.IsValidIndex(Index)) return;
    SelectedFacePattern[Index] = FaceCombos[Index]->GetSelectedIndex();
    const float Rate = FaceOpenSliders[Index]->GetValue();
    if (PreviewActor->bAutomaticBlink) PreviewActor->SetFacePattern(Index + 1, SelectedFacePattern[Index], bBlend);
    else PreviewActor->SetFaceControl(Index + 1, SelectedFacePattern[Index], Rate, bBlend);
    FaceCombos[Index]->SetToolTipText(FText::FromString(
        PreviewActor->GetFacePatternLabel(Index + 1, SelectedFacePattern[Index])));
}
void UOKCharacterController::OnEyesPattern(FString Item, ESelectInfo::Type Type) { UpdateFaceControl(0, true); }
void UOKCharacterController::OnBrowPattern(FString Item, ESelectInfo::Type Type) { UpdateFaceControl(1, true); }
void UOKCharacterController::OnAutomaticBlink(bool bEnabled)
{
    if (bSuppressFaceControls || !PreviewActor) return;
    PreviewActor->SetAutomaticBlink(bEnabled);
    for (auto& Slider : FaceOpenSliders) Slider->SetIsEnabled(!bEnabled);
}
void UOKCharacterController::OnFaceFlatten(float Value)
{
    if(IConsoleVariable* V=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.FaceFlatten")))V->Set(Value,ECVF_SetByConsole);
    if(FaceFlattenLabel)FaceFlattenLabel->SetText(FText::FromString(FString::Printf(TEXT("臉部壓扁 %.2f"),Value)));
}
void UOKCharacterController::OnFaceFlattenLock(bool Locked)
{
    if(IConsoleVariable* V=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.FaceFlattenLock")))V->Set(Locked?1:0,ECVF_SetByConsole);
}
void UOKCharacterController::OnEyeLookCamera(bool Enabled)
{
    if(bSuppressFaceControls || !PreviewActor) return;
    if(Enabled) {
        if(!PreviewActor->FollowEyeLookCamera(PreviewActor->Session.EyeLookPatternCount()>1?1:0)) EyeLookCameraCheck->SetIsChecked(false);
    } else PreviewActor->StopEyeLookTracking();
}
void UOKCharacterController::UpdateGaze()
{
    if(PreviewActor) PreviewActor->StopEyeLookTracking();
    if(EyeLookCameraCheck) EyeLookCameraCheck->SetIsChecked(false);
    if(PreviewActor && GazeSliders.Num()==3)
        PreviewActor->SetEyeGaze(GazeSliders[0]->GetValue(),GazeSliders[1]->GetValue(),GazeSliders[2]->GetValue());
}
void UOKCharacterController::OnGazeLeft(float Value) { if(GazeSliders.Num()==3) {GazeSliders[0]->SetValue(Value);UpdateGaze();} }
void UOKCharacterController::OnGazeRight(float Value) { if(GazeSliders.Num()==3) {GazeSliders[1]->SetValue(Value);UpdateGaze();} }
void UOKCharacterController::OnGazeVertical(float Value) { if(GazeSliders.Num()==3) {GazeSliders[2]->SetValue(Value);UpdateGaze();} }
void UOKCharacterController::OnForceBlink()
{
    if (!PreviewActor) return;
    if (AutomaticBlinkCheck) AutomaticBlinkCheck->SetIsChecked(true);
    PreviewActor->ForceBlink();
}
void UOKCharacterController::OnEyesOpen(float Value)
{
    if (bSuppressFaceControls) return;
    SelectedFaceOpen[0] = Value;
    if (FaceOpenSliders.IsValidIndex(0) && !FMath::IsNearlyEqual(FaceOpenSliders[0]->GetValue(),Value)) FaceOpenSliders[0]->SetValue(Value);
    UpdateFaceControl(0);
}
void UOKCharacterController::OnBrowOpen(float Value)
{
    if (bSuppressFaceControls) return;
    SelectedFaceOpen[1] = Value;
    if (FaceOpenSliders.IsValidIndex(1) && !FMath::IsNearlyEqual(FaceOpenSliders[1]->GetValue(),Value)) FaceOpenSliders[1]->SetValue(Value);
    UpdateFaceControl(1);
}

void UOKCharacterController::OnMouthChanged(float NewValue)
{
	SelectedMouthOpen = NewValue;
	if (MouthOpenSlider && !FMath::IsNearlyEqual(MouthOpenSlider->GetValue(), NewValue))
	{
		MouthOpenSlider->SetValue(NewValue);
	}
	if (PreviewActor)
	{
		if(InteractionWidget)PreviewActor->SavedMouthOpen=NewValue;
		PreviewActor->SetMouthOpen(NewValue);
	}
}

void UOKCharacterController::OnMouthPatternSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (MouthPatternCombo) MouthPatternCombo->SetToolTipText(FText::FromString(SelectedItem));
	SelectedMouthPattern = MouthPatternCombo ? FMath::Max(0, MouthPatternCombo->FindOptionIndex(SelectedItem)) : 0;
	if (PreviewActor && MouthPatternCombo)
	{
		if(InteractionWidget){PreviewActor->bManualSpeechPattern=true;PreviewActor->SavedMouthPattern=SelectedMouthPattern;}
		PreviewActor->SetMouthPattern(SelectedMouthPattern);
	}
}

void UOKCharacterController::RefreshGameAnimationParameters()
{
    if(!GameAnimationParameters) return;
    GameAnimationParameters->ClearChildren();
    if(!PreviewActor||PreviewActor->PoseMode!=2) return;
    for(int32 Index=0;Index<PreviewActor->Session.AnimationFloatCount();++Index) {
        FString Name;float Value,Min,Max;
        if(!PreviewActor->Session.AnimationFloatAt(Index,Name,Value,Min,Max)) continue;
        UHorizontalBox* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        UTextBlock* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Label->SetText(FText::FromString(OKAnimationParameterLabel(Name,Index)));Row->AddChild(Label);
        UOKShapeSlider* Slider=WidgetTree->ConstructWidget<UOKShapeSlider>(UOKShapeSlider::StaticClass());
        Slider->AnimationParameter=Name;Slider->Owner=this;Slider->SetMinValue(Min);Slider->SetMaxValue(Max);Slider->SetValue(Value);
        UHorizontalBoxSlot* RowSlot=Row->AddChildToHorizontalBox(Slider);RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        UTextBlock* Number=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());Row->AddChild(Number);
        Slider->ValueLabel=Number;Slider->RefreshValueLabel();Slider->OnValueChanged.AddDynamic(Slider,&UOKShapeSlider::OnSliderMoved);
        GameAnimationParameters->AddChild(Row);
    }
}

void UOKCharacterController::NativeDestruct(){CancelCharacterMCP();++LoadGeneration;if(IsValid(PreparingPreview))PreparingPreview->Destroy();PreparingPreview=nullptr;if(bEditorDisplayDirty)SaveEditorDisplaySettings();ResetClothingPreviews();ResetNeighborPreload();CancelPairCatalogue();OnExitInteraction();if(auto* PC=Cast<AOpenKoikatsuPlayerController>(GetOwningPlayer()))PC->SetPaintPreviewActive(false);OnStopPair();Super::NativeDestruct();}
void UOKCharacterController::CancelPairPreparation(){
    PairRequestAt=0.;
    bPairAutoStartPending=false;
    ++AnimationPrepareGeneration;PendingAnimationKind=-1;
    ++PairPrepareGeneration;if(bPairPreparing){bPairPreparing=false;ShowPairStatus(TEXT("已取消動畫切換。"),true);}
}
void UOKCharacterController::OnStopPair(){
    bVideoPairActive=false;PairRestoreKind=0;
    if(bStudioMode){
        // The studio keeps both characters; only native pair playback ends.
        CancelPairPreparation();
        if(PairedPlayback){PairedPlayback->Stop();PairedPlayback->Destroy();PairedPlayback=nullptr;}
        if(IsValid(PreviewActor))PreviewActor->SetActorTickEnabled(true);
        return;
    }
    ++PartnerLoadGeneration;bPartnerLoadInFlight=false;PendingPartnerPath.Empty();
    CancelPairPreparation();
    bPairBreathArmed=false;
    if(IsValid(PreviewActor)){PreviewActor->SetSpeechPaused(false);PreviewActor->StopHBreath();}
    if(IsValid(PartnerActor)){PartnerActor->SetSpeechPaused(false);PartnerActor->StopHBreath();}
    PartnerPersonality=-1;
    const bool HadPair=IsValid(PairedPlayback);
    if(PairedPlayback){PairedPlayback->Stop();PairedPlayback->Destroy();PairedPlayback=nullptr;}
    if(PartnerActor){PartnerActor->Destroy();PartnerActor=nullptr;}
    if(HadPair&&IsValid(PreviewActor)){
        PreviewActor->SetActorTickEnabled(true);if(!bAnimationPrepared)PreviewActor->SetPoseMode(0);SelectedGameClip.Empty();SelectedStudioRow=NAME_None;
        if(GameClipCombo){const bool Previous=bSuppressClipCombo;bSuppressClipCombo=true;GameClipCombo->SetSelectedIndex(0);bSuppressClipCombo=Previous;}
        RefreshGameAnimationParameters();
    }
    if(GameAnimationParameters)GameAnimationParameters->SetVisibility(ESlateVisibility::Visible);
    FemalePairStates.Empty();MalePairStates.Empty();
    if(PairStateCombo){bSuppressPairControls=true;PairStateCombo->ClearOptions();PairStateCombo->AddOption(TEXT("選擇階段"));PairStateCombo->SetSelectedIndex(0);bSuppressPairControls=false;}
}
void UOKCharacterController::OnPairSpeed(float Value){
    if(!PairedPlayback)return;
    if(!PairedPlayback->SetSpeed(Value*4.f))return;
    // Still and moving thrusts use different breath rows. Pause only freezes the current moan.
    if(!bPairBreathArmed||PairedPlayback->IsPaused())return;
    const bool bMoving=Value*4.f>0.01f;
    if(bMoving!=bPairBreathMoving){bPairBreathMoving=bMoving;auto* Female=Selected.Sex==1?PreviewActor.Get():PartnerActor.Get();if(Female)Female->QueueHBreathCues(bMoving?PairMovingBreaths:PairStillBreaths);}
}
void UOKCharacterController::OnPausePlayback(){CancelPairPreparation();if(PairedPlayback){PairedPlayback->SetPaused(!PairedPlayback->IsPaused());const bool bPaused=PairedPlayback->IsPaused();if(PreviewActor)PreviewActor->SetSpeechPaused(bPaused);if(PartnerActor)PartnerActor->SetSpeechPaused(bPaused);}else if(PreviewActor){const bool bRun=!PreviewActor->IsActorTickEnabled();PreviewActor->SetActorTickEnabled(bRun);if(bVideoPairActive&&IsValid(PartnerActor))PartnerActor->SetActorTickEnabled(bRun);}}
void UOKCharacterController::OnReplayPlayback(){
 if(!bAnimationPrepared&&!PairedPlayback&&PreviewActor&&!SelectedGameClip.IsEmpty()){BeginAnimationPreparation(4,SelectedGameClip,ESelectInfo::Direct);return;}
 CancelPairPreparation();FString Error;
 if(PairedPlayback){
  if(!PairedPlayback->Restart(Error)){PairedPlayback->Stop();if(InspectorText)InspectorText->SetText(FText::FromString(Error));ShowAnimationStatus(Error,true);return;}
  StartPairBreath();ShowAnimationStatus(TEXT("正在重播雙人動作。"));
 }else if(PreviewActor&&!SelectedGameClip.IsEmpty()){
  PreviewActor->SetActorTickEnabled(true);
  const bool Ready=SelectedStudioRow.IsNone()?PreviewActor->SetGameAnimation(SelectedGameClip):PreviewActor->SetStudioAnimation(AnimationCatalog,SelectedStudioRow,Error,SavedStudioOptions.X,SavedStudioOptions.Y,SavedStudioOptions.Z);
  ShowAnimationStatus(Ready?TEXT("正在重播：")+SelectedGameClip:(Error.IsEmpty()?TEXT("動作重播失敗。"):Error),!Ready);
 }
}
void UOKCharacterController::RefreshPairs(){
    CancelPairCatalogue();ReadyPairs.Reset();CancelPairPreparation();
    NativePairs.Empty();if(!PairCombo||!PreviewActor)return;bSuppressPairControls=true;PairCombo->ClearOptions();PairCombo->AddOption(TEXT("選擇姿勢"));
    TArray<TSharedPtr<FJsonValue>> Rows;
    const bool Prepared=PendingLoadPairGeneration==LoadGeneration;
    const FString Json=Prepared?MoveTemp(PendingLoadPairJson):PreviewActor->Session.NativeAnimationPairs();
    PendingLoadPairGeneration=INDEX_NONE;
    if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Rows)){for(const auto& Row:Rows){auto P=Row->AsObject();double Participants=0;if(!P||!P->TryGetNumberField(TEXT("participants"),Participants)||Participants!=2)continue;NativePairs.Add(P);}}
    TMap<FString,int32> Counts;TArray<FString> Labels;Labels.Reserve(NativePairs.Num());
    for(const TSharedPtr<FJsonObject>& P:NativePairs){
        const FString Category=P->GetStringField(TEXT("category"));
        const FString Mode=Category==TEXT("AnimationInfo_01")?TEXT("奉仕"):Category==TEXT("AnimationInfo_02")?TEXT("插入"):Category;
        const FString Name=P->GetStringField(TEXT("name"));
        const FString Label=Name.IsEmpty()?FString::Printf(TEXT("%s · 編號 %d"),*Mode,int32(P->GetNumberField(TEXT("id")))):FString::Printf(TEXT("%s · %s"),*Mode,*Name);
        Counts.FindOrAdd(Label)++;Labels.Add(Label);
    }
    for(int32 I=0;I<Labels.Num();++I)if(Counts.FindRef(Labels[I])>1)Labels[I]+=FString::Printf(TEXT("（%d）"),int32(NativePairs[I]->GetNumberField(TEXT("id"))));
    if(PreviewActor->Session.IsPortable()){
        PreparePairCatalogue(NativePairs,Labels);bSuppressPairControls=false;OnPairCategorySelected(TEXT("全部"),ESelectInfo::Direct);return;
    }
    PreparePairCatalogue(NativePairs,Labels);bSuppressPairControls=false;OnPairCategorySelected(TEXT("全部"),ESelectInfo::Direct);
}
void UOKPartnerMaleButton::HandleClick(){if(Owner)Owner->SelectPartnerMale(Index);}
void UOKClothesStateButton::HandleClick(){if(Owner)Owner->OnClothesState(Kind);}
void UOKCharacterController::OnClothesState(int32 Kind)
{
	const double RequestAt=FPlatformTime::Seconds();
    bLastClothingSwitchCached=false;
	// Programmatic callbacks must obey the same busy gate as disabled buttons.
	if (bLoadInFlight||bAppearanceBusy) return;
 if(MaterializeAppearance([Kind](UOKCharacterController* G){G->OnClothesState(Kind);}))return;
    if(bEditorModsDirty)ApplyPendingEditorMods();
 if(!Entries.IsValidIndex(SelectedIndex))return;
 UKKGameRoot* Root=bIndependentCards?nullptr:UKKGameRoot::Get();uint8 Next[9];FString Error;
 // These two native actions are unconditional; rule discovery is unnecessary
 // and can parse the whole mod catalogue on the input callback.
 if(Kind==100||Kind==102)FMemory::Memset(Next,Kind==102?3:0,sizeof(Next));
 else if(!FOkSession::ClothingTransition(Root?Root->Root:FString(),Entries[SelectedIndex].Path,OutfitIndex,ClothesStates,Kind,Next,Error)){
  if(InspectorText)InspectorText->SetText(FText::FromString(TEXT("無法切換服裝狀態：")+Error));return;
 }
 FMemory::Memcpy(ClothesStates,Next,sizeof(ClothesStates));
	RefreshClothesStateLabels();
 bool VisibilityOnly=false;
 if(IsValid(PreviewActor)&&Kind==102)VisibilityOnly=PreviewActor->SetClothingHidden(true);
 else if(IsValid(PreviewActor)){
  bool SameBase=true;for(int32 Index=0;Index<9;++Index)SameBase&=Next[Index]==PreviewActor->DisplayClothesStates[Index];
  if(SameBase)VisibilityOnly=PreviewActor->SetClothingHidden(false);
 }
 if(VisibilityOnly){FMemory::Memcpy(LoadedClothesStates,Next,sizeof(Next));bLastClothingSwitchCached=true;}
 else if (IsValid(PreviewActor))OnLoad3D();
    LastClothingSwitchMs=(FPlatformTime::Seconds()-RequestAt)*1000.;
    UE_LOG(LogTemp,Display,TEXT("OK clothing switch cached=%d callback_ms=%.3f"),bLastClothingSwitchCached,LastClothingSwitchMs);
}
void UOKCharacterController::RefreshClothesStateLabels()
{
	const TCHAR* Names[] = {TEXT("上衣"), TEXT("下裝"), TEXT("胸罩"), TEXT("內褲"), TEXT("手套"), TEXT("褲襪"), TEXT("襪子"), TEXT("內鞋"), TEXT("外鞋")};
	const TCHAR* States[] = {TEXT("穿著"), TEXT("半脫"), TEXT("再半"), TEXT("全脫")};
	for (int32 Index = 0; Index < ClothesStateLabels.Num() && Index < 9; ++Index)
	{
		if (!ClothesStateLabels[Index]) continue;
		const int32 State = FMath::Clamp<int32>(ClothesStates[Index], 0, 3);
		ClothesStateLabels[Index]->SetText(FText::FromString(FString::Printf(TEXT("%s：%s"), Names[Index], States[State])));
	}
}
void UOKCharacterController::ShowPairStatus(const FString& Text,bool bOnScreen){
    const FString Shown=OKMotionLabelDisplay(Text);
    UE_LOG(LogTemp,Display,TEXT("OK pair %s"),*Shown);
    if(PairStatus)PairStatus->SetText(FText::FromString(Shown));
    if(InspectorText)InspectorText->SetText(FText::FromString(Shown));
    if(bOnScreen&&GEngine)GEngine->AddOnScreenDebugMessage(91,8.f,FColor::Yellow,Shown);
}
void UOKCharacterController::RebuildPartnerMaleButtons(){
    if(!PartnerMaleList)return;PartnerMaleList->ClearChildren();
    for(int32 I=0;I<PartnerMalePaths.Num();++I){
        UOKPartnerMaleButton* Button=WidgetTree->ConstructWidget<UOKPartnerMaleButton>(UOKPartnerMaleButton::StaticClass());
        Button->Index=I;Button->Owner=this;Button->OnClicked.AddDynamic(Button,&UOKPartnerMaleButton::HandleClick);
        FButtonStyle Style=Button->GetStyle();
        const bool bSelected=I==SelectedPartnerMale;
        const FLinearColor Tint=bSelected?FLinearColor(0.35f,0.62f,0.28f,1.f):FLinearColor(0.16f,0.38f,0.48f,1.f);
        Style.Normal.TintColor=FSlateColor(Tint);Style.Hovered.TintColor=FSlateColor(Tint*1.15f);Style.Pressed.TintColor=FSlateColor(Tint*0.8f);Button->SetStyle(Style);
        UTextBlock* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        const FString Who=PartnerMaleLabels.IsValidIndex(I)?PartnerMaleLabels[I]:FPaths::GetBaseFilename(PartnerMalePaths[I]);
        Label->SetText(FText::FromString(bSelected?TEXT("● ")+Who:Who));Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));Button->AddChild(Label);
        PartnerMaleList->AddChild(Button);
    }
}
void UOKCharacterController::SelectPartnerMale(int32 Index){
    if(!PartnerMalePaths.IsValidIndex(Index))return;
    SelectedPartnerMale=Index;
    if(PartnerMaleCombo&&PartnerMaleCombo->GetSelectedIndex()!=Index+1){bSuppressPartnerMale=true;PartnerMaleCombo->SetSelectedIndex(Index+1);bSuppressPartnerMale=false;}
    if(PartnerMaleList){
        for(int32 I=0;I<PartnerMaleList->GetChildrenCount();++I){
            UOKPartnerMaleButton* Button=Cast<UOKPartnerMaleButton>(PartnerMaleList->GetChildAt(I));if(!Button)continue;
            const bool bSelected=Button->Index==SelectedPartnerMale;
            FButtonStyle Style=Button->GetStyle();
            const FLinearColor Tint=bSelected?FLinearColor(0.35f,0.62f,0.28f,1.f):FLinearColor(0.16f,0.38f,0.48f,1.f);
            Style.Normal.TintColor=FSlateColor(Tint);Style.Hovered.TintColor=FSlateColor(Tint*1.15f);Style.Pressed.TintColor=FSlateColor(Tint*0.8f);Button->SetStyle(Style);
            if(UTextBlock* Label=Cast<UTextBlock>(Button->GetContent())){const FString Who=PartnerMaleLabels.IsValidIndex(Button->Index)?PartnerMaleLabels[Button->Index]:FPaths::GetBaseFilename(PartnerMalePaths[Button->Index]);Label->SetText(FText::FromString(bSelected?TEXT("● ")+Who:Who));}
        }
    }
    const FString Who=PartnerMaleLabels.IsValidIndex(Index)?PartnerMaleLabels[Index]:FPaths::GetBaseFilename(PartnerMalePaths[Index]);
    ShowPairStatus(FString::Printf(TEXT("已選男角：%s。再按「載入配對角色」。"),*Who),true);
}
void UOKCharacterController::OnPartnerMaleSelected(FString Item,ESelectInfo::Type Type){
    if(bSuppressPartnerMale||!PartnerMaleCombo)return;
    const int32 Index=PartnerMaleCombo->FindOptionIndex(Item)-1;
    if(!PartnerMalePaths.IsValidIndex(Index)){SelectedPartnerMale=INDEX_NONE;RebuildPartnerMaleButtons();ShowPairStatus(TEXT("請點下面一位男角的名字。"),true);return;}
    if(Index!=SelectedPartnerMale)SelectPartnerMale(Index);
}
void UOKCharacterController::RefreshPartnerMales(){
    if(!PartnerMaleCombo)return;
    FString Keep;if(PartnerMalePaths.IsValidIndex(SelectedPartnerMale))Keep=PartnerMalePaths[SelectedPartnerMale];
    PartnerMalePaths.Reset();PartnerMaleLabels.Reset();
    bSuppressPartnerMale=true;PartnerMaleCombo->ClearOptions();PartnerMaleCombo->AddOption(TEXT("選擇男角"));
    if(bIndependentCards&&!CurrentCardDirectory.IsEmpty()){
        TArray<FString> Files;IFileManager::Get().FindFilesRecursive(Files,*CurrentCardDirectory,TEXT("*.png"),true,false);Files.Sort();
        for(const auto& Path:Files){FKKCharacterFile Card;FString Error;if(UKKCardWorkspace::PortableSourcePath(Path).IsEmpty()||!UKKCardReader::LoadFromFile(Path,Card,Error)||Card.Sex!=0)continue;
            FString Label=FString::Printf(TEXT("%s %s（%s）"),*Card.LastName,*Card.FirstName,*FPaths::GetBaseFilename(Path));PartnerMalePaths.Add(Path);PartnerMaleLabels.Add(Label);PartnerMaleCombo->AddOption(Label);
        }
    }
    // Game males are also offered with independent cards (LoadPartnerFromPath opens
    // raw game cards through the game root): video pair motions need any male partner.
    // Native pair poses still need both from the same source (reported at pair time).
    UKKGameRoot* Root=UKKGameRoot::Get();
    if(Root&&!Root->Root.IsEmpty()){
        for(const TCHAR* Relative:{TEXT("UserData/chara/male"),TEXT("DefaultData/chara/male")}){
            const FString Folder=FPaths::Combine(Root->Root,Relative);TArray<FString> Names;IFileManager::Get().FindFiles(Names,*(Folder/TEXT("*.png")),true,false);Names.Sort();
            for(const FString& Name:Names){
                const FString Path=Folder/Name;FKKCharacterFile Card;FString Error;if(!UKKCardReader::LoadFromFile(Path,Card,Error)||Card.Sex!=0)continue;
                FString Who=FString::Printf(TEXT("%s %s"),*Card.LastName,*Card.FirstName).TrimStartAndEnd();if(Who.IsEmpty())Who=FPaths::GetBaseFilename(Name);else Who+=FString::Printf(TEXT("（%s）"),*FPaths::GetBaseFilename(Name));
                if(bIndependentCards)Who+=TEXT("（遊戲卡）");
                PartnerMaleCombo->AddOption(Who);PartnerMalePaths.Add(Path);PartnerMaleLabels.Add(Who);
            }
        }
    }
    SelectedPartnerMale=INDEX_NONE;for(int32 I=0;I<PartnerMalePaths.Num();++I)if(PartnerMalePaths[I]==Keep){SelectedPartnerMale=I;break;}
    PartnerMaleCombo->SetSelectedIndex(SelectedPartnerMale==INDEX_NONE?0:SelectedPartnerMale+1);bSuppressPartnerMale=false;
    RebuildPartnerMaleButtons();
    if(PartnerMalePaths.IsValidIndex(SelectedPartnerMale))ShowPairStatus(FString::Printf(TEXT("已選男角：%s。再按「載入配對角色」。"),*PartnerMaleLabels[SelectedPartnerMale]),false);
    else ShowPairStatus(PartnerMalePaths.Num()?FString::Printf(TEXT("男角 %d 位。點名字，再按「載入配對角色」。"),PartnerMalePaths.Num()):TEXT("找不到男性卡片：目前資料夾沒有男性獨立卡，遊戲目錄的 UserData/chara/male、DefaultData/chara/male 也沒有（或尚未設定遊戲目錄）。可在互動場景輸入男性卡片路徑。"),false);
}
void UOKCharacterController::OnPenisVisible(bool bEnabled){(void)bEnabled;ApplyPenisVisible();}
void UOKCharacterController::ApplyPenisVisible(){
    const bool bShow=PenisCheck && PenisCheck->IsChecked();
    if(PreviewActor)PreviewActor->SetGenitalVisible(bShow);
    if(PartnerActor)PartnerActor->SetGenitalVisible(bShow);
    if(!bShow||!InspectorText)return;
    const int32 Meshes=(PreviewActor?PreviewActor->GenitalPrimitives.Num():0)+(PartnerActor?PartnerActor->GenitalPrimitives.Num():0);
    if(Meshes==0)InspectorText->SetText(FText::FromString(TEXT("沒有分開的陰莖與陰囊可開關。若已做進身體網格，這個選項切不到。")));
}
void UOKCharacterController::OnLoadPartner(){
    UE_LOG(LogTemp,Display,TEXT("OK pair click load selected=%d combo=%d males=%d independent=%d actor=%d busy=%d"),SelectedPartnerMale,PartnerMaleCombo?PartnerMaleCombo->GetSelectedIndex():-2,PartnerMalePaths.Num(),bIndependentCards?1:0,PreviewActor?1:0,bLoadInFlight?1:0);

    if(!PreviewActor){ShowPairStatus(TEXT("請先按「加載3D」，再選男角。"),true);return;}
    if(bLoadInFlight){ShowPairStatus(TEXT("角色還在載入，請稍候。"),true);return;}
    int32 Index=SelectedPartnerMale;
    if(PartnerMaleCombo){const int32 ComboIndex=PartnerMaleCombo->GetSelectedIndex()-1;if(PartnerMalePaths.IsValidIndex(ComboIndex))Index=ComboIndex;}
    if(!PartnerMalePaths.IsValidIndex(Index)){ShowPairStatus(PartnerMalePaths.Num()?TEXT("請先點一位男角的名字。"):TEXT("此目錄沒有男性卡片；請輸入男性 APNG 路徑。"),true);return;}
    const FString Who=PartnerMaleLabels.IsValidIndex(Index)?PartnerMaleLabels[Index]:FPaths::GetBaseFilename(PartnerMalePaths[Index]);
    ShowPairStatus(FString::Printf(TEXT("正在載入 %s…"),*Who),true);
    LoadPartnerFromPath(PartnerMalePaths[Index]);
}
void UOKCharacterController::LoadPartnerFromPath(const FString& Path){
    if(PreviewActor){
        if(bLoadInFlight){ShowPairStatus(TEXT("女角仍在載入"),true);return;}
        const FString PortablePath=UKKCardWorkspace::PortableSourcePath(Path);
        const FString CardPath=PortablePath.IsEmpty()?Path:PortablePath;
        const auto* NativeRoot=PortablePath.IsEmpty()?UKKGameRoot::Get():nullptr;
        const FString GameRoot=NativeRoot?NativeRoot->Root:FString();
        if(PortablePath.IsEmpty()&&GameRoot.IsEmpty()){ShowPairStatus(TEXT("原始男性卡片需要設定遊戲資源目錄"),true);return;}
        if(PartnerLoadsPending>0){++PartnerLoadGeneration;PendingPartnerPath=Path;bPartnerLoadInFlight=true;ShowPairStatus(TEXT("已更新男性選擇；正在完成上一個背景載入"),false);return;}
        const FString MetadataPath=Path;
        const uint8 RequiredSex=Selected.Sex==0?1:0;
        const int32 Gen=++PartnerLoadGeneration;bPartnerLoadInFlight=true;++PartnerLoadsPending;
        TWeakObjectPtr<UOKCharacterController> Weak(this);TWeakObjectPtr<AKKPreviewActor> Female=PreviewActor;
        auto Job=MakeShared<FOkAsyncLoad,ESPMode::ThreadSafe>();
        ShowPairStatus(TEXT("背景載入男性角色；目前互動繼續播放"),false);
        const bool PrepareSkeleton=OKLiveGpuSkinRequested();
        Async(EAsyncExecution::Thread,[Weak,Female,Gen,CardPath,GameRoot,MetadataPath,RequiredSex,Job,PrepareSkeleton](){
            FKKCharacterFile Card;
            Job->bOk=UKKCardReader::LoadFromFile(MetadataPath,Card,Job->Err)&&Card.Sex==RequiredSex;
            if(Job->bOk)Job->bOk=Job->Session.Open(GameRoot,CardPath,0,Job->Err);
            if(Job->bOk)AKKPreviewActor::PrecacheSessionTextures(Job->Session,PrepareSkeleton);
            Card.OriginalBytes.Reset();Card.RawBlocks.Empty();
            AsyncTask(ENamedThreads::GameThread,[Weak,Female,Gen,Job,Card,MetadataPath](){
                auto* G=Weak.Get();if(!G)return;--G->PartnerLoadsPending;
                FString Next=MoveTemp(G->PendingPartnerPath);G->PendingPartnerPath.Empty();
                if(!Next.IsEmpty()){G->bPartnerLoadInFlight=false;G->LoadPartnerFromPath(Next);return;}
                if(Gen!=G->PartnerLoadGeneration||G->PreviewActor!=Female.Get()||!Female.IsValid())return;
                G->bPartnerLoadInFlight=false;
                if(!Job->bOk){G->ShowPairStatus(Job->Err.IsEmpty()?TEXT("請選擇有效的男性角色卡片"):Job->Err,true);return;}
                auto* Partner=G->GetWorld()->SpawnActor<AKKPreviewActor>();if(!Partner){G->ShowPairStatus(TEXT("無法生成男性角色"),true);return;}
                Partner->bPreserveSourceWorld=Female->bPreserveSourceWorld;Partner->SetActorTransform(Female->GetActorTransform());Partner->Session=MoveTemp(Job->Session);Partner->UseGalleryPhysics();Partner->bShareIdenticalPose=bool(G->InteractionWidget);Partner->bUsePrivateGpuPlayback=bool(G->InteractionWidget);
                FString Error;if(!Partner->LoadSession(Error)){Partner->Destroy();G->ShowPairStatus(Error,true);return;}
                Partner->PreparePlaybackMaterials(false);
                if(G->bStudioMode&&IsValid(G->PartnerActor))G->PartnerActor->Destroy();  // studio OnStopPair keeps partners
                G->OnStopPair();G->PartnerActor=Partner;G->PartnerCardPath=MetadataPath;G->PartnerPersonality=Card.Personality;G->PartnerVoiceRate=Card.VoiceRate;
                Partner->SetActorTickEnabled(false);G->ApplyPenisVisible();
                if(G->bStudioMode&&G->PendingVideoPair.Num()!=3){G->ShowPairStatus(TEXT("男角已載入工作室"),false);return;}
                if(G->PendingVideoPair.Num()==3){
                    // Auto-loaded for a video pair motion: play it instead of a native pair pose.
                    const TArray<FString> Pending=MoveTemp(G->PendingVideoPair);G->PendingVideoPair.Reset();
                    G->ShowPairStatus(G->PlayVideoPair(Pending[0],Pending[1],Pending[2]),false);
                    return;
                }
                if(G->NativePairs.IsEmpty()&&!G->bPairCataloguePreparing)G->RefreshPairs();
                G->bPairAutoStartPending=true;G->bPairPreparing=true;G->TryStartPreparedPair();
                if(!G->PairedPlayback){
                    if(G->PairCombo&&G->PairCombo->GetOptionCount()>1)G->ShowPairStatus(TEXT("男角已載入；正在背景準備顯示材質"),false);
                    else if(G->bPairCataloguePreparing)G->ShowPairStatus(TEXT("男角已載入；正在背景準備雙人姿勢"),false);
                    else if(G->NativePairs.IsEmpty()){G->bPairAutoStartPending=false;G->bPairPreparing=false;G->ShowPairStatus(TEXT("男角已載入；缺少共用雙人動畫資源，請安裝 PairedAnimations"),true);}
                }
                UE_LOG(LogTemp,Display,TEXT("OK interaction portable partner committed actor=%s pairs=%d"),*Partner->GetName(),G->NativePairs.Num());
            });
        });return;
    }

    ShowPairStatus(TEXT("請先按「加載3D」，再載入配對角色。"),true);
}
void UOKCharacterController::OnPairPosePrev(){StepPairPose(-1);}
void UOKCharacterController::OnPairPoseNext(){StepPairPose(1);}
void UOKCharacterController::TryStartPreparedPair(){
    if(!bPairAutoStartPending||!PreviewActor||!PartnerActor||PairedPlayback||bPartnerLoadInFlight||!PairCombo
        ||PairCombo->GetOptionCount()<2||(PairStateCombo&&PairStateCombo->GetSelectedIndex()>0))return;
    // Advertised ready rows and an automatic first selection use the same
    // boundary as the enabled UI. Do not request a switch during shader work.
    const bool FemaleReady=PreviewActor->PreparePlaybackMaterials(false);
    const bool MaleReady=PartnerActor->PreparePlaybackMaterials(false);
    if(!FemaleReady||!MaleReady)return;
    if(bPairPreparing&&bPairCataloguePreparing)return;
    bPairAutoStartPending=false;
    const int32 Pose=FMath::Max(1,PairCombo->GetSelectedIndex());
    if(PairCombo->GetSelectedIndex()==Pose)OnPairSelected(PairCombo->GetSelectedOption(),ESelectInfo::Direct);
    else PairCombo->SetSelectedIndex(Pose);
}
void UOKCharacterController::StepPairPose(int32 Delta){
    if(!PreviewActor){ShowPairStatus(TEXT("請先按「加載3D」。"),true);return;}
    if(!PartnerActor){ShowPairStatus(TEXT("請先點一位男角，再按「載入配對角色」。"),true);return;}
    if(PreviewActor->Session.IsPortable()&&(!PreviewActor->PreparePlaybackMaterials(false)||!PartnerActor->PreparePlaybackMaterials(false))){ShowPairStatus(TEXT("正在背景準備雙方顯示材質"),false);return;}
    if(!PairCombo||PairCombo->GetOptionCount()<2){ShowPairStatus(TEXT("沒有可播放的做愛姿勢。"),true);return;}
    int32 Index=PairCombo->GetSelectedIndex();
    if(Index<1)Index=Delta<0?PairCombo->GetOptionCount()-1:1;
    else{Index+=Delta;if(Index<1)Index=PairCombo->GetOptionCount()-1;else if(Index>=PairCombo->GetOptionCount())Index=1;}
    PairCombo->SetSelectedIndex(Index);
}
void UOKCharacterController::OnPairSelected(FString Item,ESelectInfo::Type Type){
    if(Type!=ESelectInfo::Direct)NoteAction(TEXT("pair"),Item);
    if(bPairAutoStartPending){bPairAutoStartPending=false;bPairPreparing=false;}
    ++AnimationPrepareGeneration;PendingAnimationKind=-1;
    if(bSuppressPairControls||!PreviewActor||!PairCombo)return;
    const int32 Index=SelectedPairCatalogueIndex();if(!NativePairs.IsValidIndex(Index)){if(Item!=TEXT("選擇姿勢"))ShowPairStatus(TEXT("這個做愛姿勢沒有對應資料。"),true);return;}
    if(!PartnerActor){ShowPairStatus(TEXT("請先點一位男角，再按「載入配對角色」，才能換做愛姿勢。"),true);bSuppressPairControls=true;PairCombo->SetSelectedIndex(0);bSuppressPairControls=false;return;}
    CancelPairCatalogue();CancelPairPreparation();
    PairRequestAt=FPlatformTime::Seconds();
    if(ReadyPairs.IsValidIndex(Index)&&ReadyPairs[Index].IsValid()&&ReadyPairs[Index]->Progress&&ReadyPairs[Index]->Progress->bCancelled&&!ReadyPairs[Index]->Progress->bFinished)ReadyPairs[Index].Reset();
    if(PreviewActor->Session.IsPortable()&&!(ReadyPairs.IsValidIndex(Index)&&ReadyPairs[Index].IsValid())){PreparePairSelection(Index);return;}
    if(!bPairCataloguePrepared&&!(ReadyPairs.IsValidIndex(Index)&&ReadyPairs[Index].IsValid())){
        auto* Game=UKKGameRoot::Get();if(!Game){ShowPairStatus(TEXT("找不到遊戲資源目錄。"),true);return;}
        const int32 Gen=++PairPrepareGeneration;const FString Root=PreviewActor->Session.IsPortable()?FOkSession::CommonPairRoot():Game->Root;
        const FString FemaleSource=NativePairs[Index]->GetStringField(TEXT("female_source")),MaleSource=NativePairs[Index]->GetStringField(TEXT("male_source"));
        TWeakObjectPtr<UOKCharacterController> Weak(this);TWeakObjectPtr<AKKPreviewActor> Preview=PreviewActor,Partner=PartnerActor;
        bPairPreparing=true;ShowPairStatus(TEXT("正在準備動畫…"),true);
        Async(EAsyncExecution::Thread,[Weak,Preview,Partner,Gen,Index,Item,Type,Root,FemaleSource,MaleSource](){
            const double Start=FPlatformTime::Seconds();
            const bool Ready=FOkSession::PrepareAnimationSource(Root,FemaleSource)&&FOkSession::PrepareAnimationSource(Root,MaleSource);
            const double Seconds=FPlatformTime::Seconds()-Start;
            AsyncTask(ENamedThreads::GameThread,[Weak,Preview,Partner,Gen,Index,Item,Type,Ready,Seconds](){
                auto* G=Weak.Get();if(!G||Gen!=G->PairPrepareGeneration)return;G->bPairPreparing=false;
                if(G->PreviewActor!=Preview.Get()||G->PartnerActor!=Partner.Get()||!Preview.IsValid()||!Partner.IsValid()||!G->PairCombo||G->SelectedPairCatalogueIndex()!=Index){G->ShowPairStatus(TEXT("已取消動畫切換。"),true);return;}
                UE_LOG(LogTemp,Display,TEXT("OK animation catalogue worker ready=%d seconds=%.6f"),Ready?1:0,Seconds);
                if(!Ready){G->ShowPairStatus(TEXT("動畫控制器無有效狀態。"),true);return;}
                G->bPairCataloguePrepared=true;G->OnPairSelected(Item,Type);G->bPairCataloguePrepared=false;
            });
        });return;
    }
    // Keep the current shared player running while the replacement curves
    // prepare. Start() stops it only when both roles can be committed.
    FemalePairStates.Empty();MalePairStates.Empty();
    auto P=NativePairs[Index];AKKPreviewActor* Female=Selected.Sex==1?PreviewActor.Get():PartnerActor.Get();AKKPreviewActor* Male=Selected.Sex==0?PreviewActor.Get():PartnerActor.Get();
    auto Collect=[](FOkSession& Session,const FString& Source,TMap<FString,FString>& Out){TSet<FString> Duplicates;for(const auto& Label:Session.ClipNamesForSource(Source)){const FString Prefix=TEXT(" / [Controller layer 0] Base Layer.");int32 At=Label.Find(Prefix);if(At==INDEX_NONE)continue;FString State=Label.Mid(At+Prefix.Len());if(State.Contains(TEXT(".")))continue;if(Out.Contains(State))Duplicates.Add(State);else Out.Add(State,Source+TEXT("|")+Label);}for(const auto& State:Duplicates)Out.Remove(State);};
    if((ReadyPairs.IsValidIndex(Index)&&ReadyPairs[Index].IsValid())){FemalePairStates=ReadyPairs[Index]->FemaleStates;MalePairStates=ReadyPairs[Index]->MaleStates;}
    else{Collect(Female->Session,P->GetStringField(TEXT("female_source")),FemalePairStates);Collect(Male->Session,P->GetStringField(TEXT("male_source")),MalePairStates);}
    bSuppressPairControls=true;PairStateCombo->ClearOptions();PairStateCombo->AddOption(TEXT("選擇階段"));TArray<FString> States;FemalePairStates.GetKeys(States);States.Sort();for(const auto& State:States)if(MalePairStates.Contains(State))PairStateCombo->AddOption(State);PairStateCombo->SetSelectedIndex(0);bSuppressPairControls=false;
    if(PairStateCombo->GetOptionCount()<2){ShowPairStatus(TEXT("這個姿勢沒有雙方都有的動作階段。"),true);return;}
    FString State=PairStateCombo->FindOptionIndex(TEXT("WLoop"))>0?TEXT("WLoop"):PairStateCombo->FindOptionIndex(TEXT("Idle"))>0?TEXT("Idle"):PairStateCombo->GetOptionAtIndex(1);
    if(ReadyPairs.IsValidIndex(Index)&&ReadyPairs[Index].IsValid()&&ReadyPairs[Index]->Progress&&ReadyPairs[Index]->Progress->StateOf(State)!=1){
        // States are still being prepared: start with one that is ready (the first one always is).
        for(int32 Option=1;Option<PairStateCombo->GetOptionCount();++Option)if(ReadyPairs[Index]->Progress->StateOf(PairStateCombo->GetOptionAtIndex(Option))==1){State=PairStateCombo->GetOptionAtIndex(Option);break;}
    }
    if(PairStateCombo->GetSelectedOption()==State)OnPairStateSelected(State,ESelectInfo::Direct);else PairStateCombo->SetSelectedOption(State);
}
void UOKCharacterController::OnPairStateSelected(FString Item,ESelectInfo::Type Type){
    if(Type!=ESelectInfo::Direct)NoteAction(TEXT("state"),Item);
    ++AnimationPrepareGeneration;PendingAnimationKind=-1;
    if(bSuppressPairControls||!PartnerActor||!PreviewActor||!FemalePairStates.Contains(Item)||!MalePairStates.Contains(Item))return;
    const int32 Index=SelectedPairCatalogueIndex();if(!NativePairs.IsValidIndex(Index))return;auto P=NativePairs[Index];
    auto Female=Selected.Sex==1?PreviewActor.Get():PartnerActor.Get();auto Male=Selected.Sex==0?PreviewActor.Get():PartnerActor.Get();
    const FString FemaleKey=(ReadyPairs.IsValidIndex(Index)&&ReadyPairs[Index].IsValid())?ReadyPairs[Index]->FemaleKeys.FindRef(Item):Female->Session.NativePairClip(FemalePairStates[Item],int32(P->GetNumberField(TEXT("female_layer"))));
    const FString MaleKey=(ReadyPairs.IsValidIndex(Index)&&ReadyPairs[Index].IsValid())?ReadyPairs[Index]->MaleKeys.FindRef(Item):Male->Session.NativePairClip(MalePairStates[Item],int32(P->GetNumberField(TEXT("male_layer"))));
    if(FemaleKey.IsEmpty()||MaleKey.IsEmpty()){ShowPairStatus(TEXT("此狀態的原生動畫層尚無可驗證的對應。"),true);return;}
    if(PairRequestAt==0.)PairRequestAt=FPlatformTime::Seconds();
    if(ReadyPairs.IsValidIndex(Index)&&ReadyPairs[Index].IsValid()&&ReadyPairs[Index]->Progress){
        const TSharedPtr<FOKPairProgress,ESPMode::ThreadSafe> Progress=ReadyPairs[Index]->Progress;
        const int32 StateReady=Progress->StateOf(Item);
        if(StateReady<0){ShowPairStatus(TEXT("此階段載入失敗；請選其他階段"),true);return;}
        if(StateReady==0){
            // Still in the background queue: move it to the front and start it as soon as it is ready.
            Progress->Bump(Item);
            const int32 Gen=PairPrepareGeneration;
            TWeakObjectPtr<UOKCharacterController> Weak(this);
            bPairPreparing=true;ShowPairStatus(TEXT("正在準備階段：")+Item+TEXT("；目前動作繼續播放"),false);
            FTimerHandle Handle;GetWorld()->GetTimerManager().SetTimer(Handle,[Weak,Gen,Index,Item,Type,Progress](){
                auto* G=Weak.Get();if(!G||Gen!=G->PairPrepareGeneration)return;
                G->bPairPreparing=false;
                if(!G->PairCombo||G->SelectedPairCatalogueIndex()!=Index||!G->PairStateCombo||G->PairStateCombo->GetSelectedOption()!=Item)return;
                if(Progress->bCancelled&&Progress->StateOf(Item)==0){G->ShowPairStatus(TEXT("已取消動畫切換。"),true);return;}
                G->OnPairStateSelected(Item,Type);
            },.1f,false);return;
        }
    }
    if(!bPairClipPrepared&&!(ReadyPairs.IsValidIndex(Index)&&ReadyPairs[Index].IsValid())){
        auto* Game=UKKGameRoot::Get();if(!Game){ShowPairStatus(TEXT("找不到遊戲資源目錄。"),true);return;}
        const int32 Gen=++PairPrepareGeneration;const FString Root=PreviewActor->Session.IsPortable()?FOkSession::CommonPairRoot():Game->Root;
        const FString FemaleMotion=P->GetStringField(TEXT("female_motion")),MaleMotion=P->GetStringField(TEXT("male_motion"));
        TWeakObjectPtr<UOKCharacterController> Weak(this);TWeakObjectPtr<AKKPreviewActor> Preview=PreviewActor,Partner=PartnerActor;
        bPairPreparing=true;ShowPairStatus(TEXT("正在準備動畫片段…"),true);
        Async(EAsyncExecution::Thread,[Weak,Preview,Partner,Gen,Index,Item,Type,Root,FemaleKey,MaleKey,FemaleMotion,MaleMotion](){
            const double Start=FPlatformTime::Seconds();
            const bool Ready=FOkSession::PrepareAnimationClip(Root,FemaleKey)&&FOkSession::PrepareAnimationClip(Root,MaleKey)&&FOkSession::PrepareAnimationPair(Root,Item,FemaleMotion,MaleMotion);
            const double Seconds=FPlatformTime::Seconds()-Start;
            AsyncTask(ENamedThreads::GameThread,[Weak,Preview,Partner,Gen,Index,Item,Type,Ready,Seconds](){
                auto* G=Weak.Get();if(!G||Gen!=G->PairPrepareGeneration)return;G->bPairPreparing=false;
                if(G->PreviewActor!=Preview.Get()||G->PartnerActor!=Partner.Get()||!Preview.IsValid()||!Partner.IsValid()||!G->PairCombo||G->SelectedPairCatalogueIndex()!=Index||!G->PairStateCombo||G->PairStateCombo->GetSelectedOption()!=Item){G->ShowPairStatus(TEXT("已取消動畫切換。"),true);return;}
                UE_LOG(LogTemp,Display,TEXT("OK animation clip worker ready=%d seconds=%.6f"),Ready?1:0,Seconds);
                if(!Ready){G->ShowPairStatus(TEXT("動畫片段準備失敗。"),true);return;}
                G->bPairClipPrepared=true;G->OnPairStateSelected(Item,Type);G->bPairClipPrepared=false;
            });
        });return;
    }
    // Curve preparation alone does not make a newly loaded role's shaders
    // ready. Poll both roles before Start can reach its synchronous boundary.
    const bool FemaleReady=Female->PreparePlaybackMaterials(false);
    const bool MaleReady=Male->PreparePlaybackMaterials(false);
    if(!FemaleReady||!MaleReady){
        const int32 Gen=PairPrepareGeneration;
        TWeakObjectPtr<UOKCharacterController> Weak(this);TWeakObjectPtr<AKKPreviewActor> Preview=PreviewActor,Partner=PartnerActor;
        bPairPreparing=true;ShowPairStatus(TEXT("正在背景準備雙方顯示材質；目前動作繼續播放"),false);
        FTimerHandle Handle;GetWorld()->GetTimerManager().SetTimer(Handle,[Weak,Preview,Partner,Gen,Index,Item,Type](){
            auto* G=Weak.Get();if(!G||Gen!=G->PairPrepareGeneration||G->PreviewActor!=Preview.Get()||G->PartnerActor!=Partner.Get()||!Preview.IsValid()||!Partner.IsValid())return;
            G->bPairPreparing=false;
            if(!G->PairCombo||G->SelectedPairCatalogueIndex()!=Index||!G->PairStateCombo||G->PairStateCombo->GetSelectedOption()!=Item)return;
            G->bPairClipPrepared=true;G->OnPairStateSelected(Item,Type);G->bPairClipPrepared=false;
        },.05f,false);return;
    }
    const double CommitStart=FPlatformTime::Seconds();
    if(!PairedPlayback)PairedPlayback=GetWorld()->SpawnActor<AKKPairedPlayback>();if(!PairedPlayback){ShowPairStatus(TEXT("無法開始雙人播放。"),true);return;}FString Error;
    if(!PairedPlayback->Start(Selected.Sex==1?PreviewActor.Get():PartnerActor.Get(),Selected.Sex==0?PreviewActor.Get():PartnerActor.Get(),FemaleKey,MaleKey,Item,P->GetStringField(TEXT("female_motion")),P->GetStringField(TEXT("male_motion")),Error)){ShowPairStatus(Error.IsEmpty()?TEXT("這個姿勢播放失敗。"):Error,true);PairedPlayback->Destroy();PairedPlayback=nullptr;return;}
    const double PlaybackDone=FPlatformTime::Seconds();
    ActiveBodyPairKey=FString::Printf(TEXT("%s:%d"),*P->GetStringField(TEXT("category")),P->GetIntegerField(TEXT("id")));
    OnPairSpeed(PairSpeedSlider?PairSpeedSlider->GetValue():.25f);
    if(GameAnimationParameters)GameAnimationParameters->SetVisibility(ESlateVisibility::Collapsed);
    ShowPairStatus(FString::Printf(TEXT("%s · %s"),*PairCombo->GetSelectedOption(),*Item),true);PreviewActor->FramePairedPose(PartnerActor);
    const double FramingDone=FPlatformTime::Seconds();
    StartPairBreath();
    UE_LOG(LogTemp,Display,TEXT("OK pair commit playback_ms=%.3f framing_ms=%.3f speech_ms=%.3f"),(PlaybackDone-CommitStart)*1000.,(FramingDone-PlaybackDone)*1000.,(FPlatformTime::Seconds()-FramingDone)*1000.);
    LastPairSwitchMs=(FPlatformTime::Seconds()-PairRequestAt)*1000.;PairRequestAt=0.;
    UE_LOG(LogTemp,Display,TEXT("OK animation game-thread commit seconds=%.6f click_to_play_ms=%.3f"),FPlatformTime::Seconds()-CommitStart,LastPairSwitchMs);
}

void UOKCharacterController::OnGameAnimationParameter(const FString& Name,float Value)
{
    if(!PreviewActor||bSuppressClipCombo||!PreviewActor->SetGameAnimationFloat(Name,Value)) return;
    if(SelectedStudioRow.IsNone()) return;
    TSet<FString> Names;
    for(int32 I=0;I<PreviewActor->Session.AnimationFloatCount();++I) {FString N;float V,Min,Max;if(PreviewActor->Session.AnimationFloatAt(I,N,V,Min,Max)) Names.Add(N);}
    const FString Height=Names.Contains(TEXT("height1"))?TEXT("height1"):TEXT("height");
    const FString Breast=Names.Contains(TEXT("Breast1"))?TEXT("Breast1"):(Names.Contains(TEXT("Breast"))?TEXT("Breast"):TEXT("breast"));
    if(Name==Height) SavedStudioOptions.X=Value;
    if(Name==Breast) SavedStudioOptions.Y=Value;
    if(Name==TEXT("motion")) SavedStudioOptions.Z=Value;
}

// All workers own only immutable strings/arrays; no live session/UObject access.
struct FOKAnimationMenuJob {
 int32 Kind=0,Generation=0,LoadGeneration=0;ESelectInfo::Type Type=ESelectInfo::Direct;
 FString Item,Root,Source,Key,YureSource,YureOwner,Error;FName Row=NAME_None;
 TArray<FString> Names,StateNames;bool Ready=false,StudioH=false;double Start=0.;
 TWeakObjectPtr<AKKPreviewActor> Actor;
};
void UOKCharacterController::BeginAnimationPreparation(int32 Kind,const FString& Item,ESelectInfo::Type Type){
 // A new single selection also supersedes an in-flight paired selection,
 // while the already committed pair continues until this request is ready.
 CancelPairPreparation();
 ++AnimationPrepareGeneration;PendingAnimationKind=Kind;PendingAnimationItem=Item;PendingAnimationType=Type;
 // Preparation owns immutable data; the next selection may supersede it.
 if(GameClipCombo)GameClipCombo->SetIsEnabled(true);
 ShowAnimationStatus((Kind==0||Kind==3)?TEXT("正在讀取動作清單…仍可選擇其他動作。") : TEXT("正在準備：")+Item+TEXT("（仍可改選其他動作）"));
 if(!bAnimationPreparing)LaunchAnimationPreparation();
}
void UOKCharacterController::LaunchAnimationPreparation(){
 if(PendingAnimationKind<0)return;
 auto* Game=UKKGameRoot::Get();if(!Game||!PreviewActor||!PreviewActor->Session.IsOpen()){PendingAnimationKind=-1;bAnimationPreparing=false;if(GameClipCombo)GameClipCombo->SetIsEnabled(true);ShowAnimationStatus(TEXT("請先載入角色，再選擇動作。"),true);return;}
 auto Job=MakeShared<FOKAnimationMenuJob>();Job->Kind=PendingAnimationKind;Job->Item=PendingAnimationItem;Job->Type=PendingAnimationType;PendingAnimationKind=-1;
 Job->Generation=AnimationPrepareGeneration;Job->LoadGeneration=LoadGeneration;Job->Root=Game->Root;Job->Actor=PreviewActor;Job->Start=FPlatformTime::Seconds();
 if(Job->Kind==0)Job->Source=Job->Item==TEXT("基本動作")?FString():GameSourceKey(Job->Item);
 else if(Job->Kind==1){if(const FName* Row=GameMotionRows.Find(Job->Item))Job->Row=*Row;else {Job->Key=GameRawClipKeys.FindRef(Job->Item);if(Job->Key.IsEmpty()){ShowAnimationStatus(TEXT("找不到所選動畫，請重新選擇。"),true);if(GameClipCombo)GameClipCombo->SetIsEnabled(true);return;}FString Source,Clip;Job->Source=Job->Key.Split(TEXT("|"),&Source,&Clip)?Source:FString();}}
 else if(Job->Kind==3)Job->Source=GameSourceKey(Job->Item.TrimStartAndEnd());
 else if(Job->Kind==4){Job->Source=SelectedGameSource;Job->Key=SelectedGameClip;Job->Row=SelectedStudioRow;}
 else if(Job->Kind==2){const int32 Index=StudioMotionCombo?StudioMotionCombo->FindOptionIndex(Job->Item)-1:INDEX_NONE;if(!StudioMotionRows.IsValidIndex(Index)){Job->Error=TEXT("找不到所選動作。請重新選擇。");FinishAnimationPreparation(Job);return;}Job->Row=StudioMotionRows[Index];}
 if(!Job->Row.IsNone()){
  bool Motion=false;int32 Layer=0;if(!AnimationCatalog||!AnimationCatalog->StudioSettings(Job->Row,Job->StudioH,Layer,Motion)){Job->Error=TEXT("動作目錄無效。請重新選擇。");FinishAnimationPreparation(Job);return;}Job->Source=AnimationCatalog->StudioSource(Job->Row);AnimationCatalog->StudioYureSource(Job->Row,Job->YureSource,Job->YureOwner);
 }
 bAnimationPreparing=true;if(InspectorText)InspectorText->SetText(FText::FromString(TEXT("正在準備動作…")));
 // Portable resources are Unreal assets: enumerate/load on the game thread,
 // without invoking the native source-file preparation worker.
 if(PreviewActor->Session.IsPortable()&&!Job->Key.StartsWith(TEXT("states:"))){
  Job->Names=Job->Source.IsEmpty()?PreviewActor->Session.ClipNames():PreviewActor->Session.ClipNamesForSource(Job->Source);
  if(!Job->Row.IsNone())Job->Error=TEXT("離線卡片請選已匯入的共用原始片段；此控制器狀態尚未匯入。");
  else Job->Ready=(Job->Kind==0||Job->Kind==3)?!Job->Names.IsEmpty():PreviewActor->Session.HasAnimationClip(Job->Key);
  FinishAnimationPreparation(Job);return;
 }
 // Common original controllers are decoded/prepared off the game thread,
 // just like native game animations. Imported Unreal clips use the path above.
 if(PreviewActor->Session.IsPortable())Job->Root=FOkSession::CommonPairRoot();
 TWeakObjectPtr<UOKCharacterController> Weak(this);
 Async(EAsyncExecution::Thread,[Weak,Job](){
  // Exact catalogue keys already identify their source and state. Prepare in
  // one native scope so source indexing and curve loading reuse the same bytes.
  // Only source browsing / Studio row resolution needs a separate names list.
  if(!Job->Key.IsEmpty()&&Job->Row.IsNone()&&Job->Kind!=0&&Job->Kind!=3){
   Job->Ready=FOkSession::PrepareAnimationClip(Job->Root,Job->Key);
   UE_LOG(LogTemp,Display,TEXT("OK animation menu phase=prepare generation=%d ms=%.3f ready=%d key=%s"),Job->Generation,(FPlatformTime::Seconds()-Job->Start)*1000.,Job->Ready,*Job->Key);
   AsyncTask(ENamedThreads::GameThread,[Weak,Job](){if(auto* G=Weak.Get())G->FinishAnimationPreparation(Job);});return;
  }
  const double LabelsStart=FPlatformTime::Seconds();
  Job->Names=FOkSession::AnimationSourceNames(Job->Root,Job->Source);
  UE_LOG(LogTemp,Display,TEXT("OK animation menu phase=labels generation=%d ms=%.3f names=%d key=%s"),Job->Generation,(FPlatformTime::Seconds()-LabelsStart)*1000.,Job->Names.Num(),*Job->Key);
  if(Job->Kind==3&&!Job->Source.StartsWith(TEXT("states:")))Job->StateNames=FOkSession::AnimationSourceNames(Job->Root,TEXT("states:")+Job->Source);
  AsyncTask(ENamedThreads::GameThread,[Weak,Job](){
   auto* G=Weak.Get();if(!G)return;
   if(Job->Generation!=G->AnimationPrepareGeneration||Job->Actor.Get()!=G->PreviewActor||Job->LoadGeneration!=G->LoadGeneration){G->FinishAnimationPreparation(Job);return;}
   if(!Job->Row.IsNone()&&(!G->AnimationCatalog||!G->AnimationCatalog->ResolveStudioAnimation(Job->Row,Job->Names,Job->Key,Job->Error))){G->FinishAnimationPreparation(Job);return;}
   if(Job->Kind==0||Job->Kind==3){Job->Ready=!Job->Names.IsEmpty();G->FinishAnimationPreparation(Job);return;}
   G->ShowAnimationStatus(TEXT("正在載入動作：")+Job->Item+TEXT("（仍可改選其他動作）"));
   Async(EAsyncExecution::Thread,[Weak,Job](){
    Job->Ready=(Job->Row.IsNone()||!Job->StudioH)?FOkSession::PrepareAnimationClip(Job->Root,Job->Key):FOkSession::PrepareStudioAnimation(Job->Root,Job->Key,Job->YureSource,Job->YureOwner);
    AsyncTask(ENamedThreads::GameThread,[Weak,Job](){if(auto* G=Weak.Get())G->FinishAnimationPreparation(Job);});
   });
  });
 });
}
void UOKCharacterController::FinishAnimationPreparation(TSharedRef<FOKAnimationMenuJob> Job){
 const bool Current=Job->Generation==AnimationPrepareGeneration&&Job->LoadGeneration==LoadGeneration&&Job->Actor.IsValid()&&Job->Actor.Get()==PreviewActor&&(Job->Kind!=3||(GameSourceInput&&GameSourceKey(GameSourceInput->GetText().ToString().TrimStartAndEnd())==Job->Source));
 if(Current){
  if(Job->Ready){
   PreparedAnimationNames=MoveTemp(Job->Names);PreparedAnimationStateNames=MoveTemp(Job->StateNames);bAnimationPrepared=true;
   const double Commit=FPlatformTime::Seconds();
   switch(Job->Kind){case 0:OnGameSourceSelected(Job->Item,Job->Type);break;case 1:OnGameClipSelected(Job->Item,Job->Type);break;case 2:OnStudioMotionSelected(Job->Item,Job->Type);break;case 3:OnLoadGameSource();break;case 4:OnReplayPlayback();break;}
   LastAnimationCommitMs=(FPlatformTime::Seconds()-Commit)*1000.;
   UE_LOG(LogTemp,Display,TEXT("OK animation menu ready=1 kind=%d worker_total_ms=%.3f commit_ms=%.3f"),Job->Kind,(Commit-Job->Start)*1000.,(FPlatformTime::Seconds()-Commit)*1000.);
   bAnimationPrepared=false;PreparedAnimationNames.Reset();PreparedAnimationStateNames.Reset();
  }else {
   const FString Error=Job->Error.IsEmpty()?((Job->Kind==0||Job->Kind==3)?TEXT("此來源沒有可播放的動作，請檢查資源路徑。"):TEXT("動作準備失敗，保留目前播放。")):Job->Error;
   ShowAnimationStatus(Error,true);if(InspectorText)InspectorText->SetText(FText::FromString(Error));
   // The failed source was never committed: keep its prior list and selector consistent.
   if(Job->Kind==0&&GameSourceCombo){bSuppressClipCombo=true;GameSourceCombo->SetSelectedOption(SelectedGameSource.IsEmpty()?TEXT("基本動作"):GameSourceLabel(SelectedGameSource));bSuppressClipCombo=false;}
   UE_LOG(LogTemp,Warning,TEXT("OK animation menu ready=0 kind=%d source=%s error=%s"),Job->Kind,*Job->Source,*Error);
  }
 }else {
  UE_LOG(LogTemp,Display,TEXT("OK animation menu stale=1 kind=%d generation=%d"),Job->Kind,Job->Generation);
  if(PendingAnimationKind<0)ShowAnimationStatus(TEXT("已取消動作準備。請重新選擇動畫。"));
 }
 bAnimationPreparing=false;
 if(PendingAnimationKind>=0)LaunchAnimationPreparation();
 if(GameClipCombo)GameClipCombo->SetIsEnabled(true);
}

FString UOKCharacterController::AnimationOptionForSelection() const
{
 if (!SelectedStudioRow.IsNone())
  for (const FString& Option : GameMotionOptions)
   if (GameMotionRows.FindRef(Option) == SelectedStudioRow) return Option;
 return GameClipLabels.FindRef(SelectedGameClip);
}

void UOKCharacterController::AddAnimationSourceClips(const FString& Source, const TArray<FString>& Clips)
{
 for (const FString& Clip : Clips)
 {
  const FString Key = Source.IsEmpty() ? Clip : Source + TEXT("|") + Clip;
  if (GameClipLabels.Contains(Key)) continue;
  FString Bundle, Name;
  if (!Key.Split(TEXT("|"), &Bundle, &Name)) Name = Clip;
  if (!GameSourceMatchesSex(Bundle, Selected.Sex)) continue;
  FString Group = TEXT("基本動作");
  if (Bundle == TEXT("video:")) Group = TEXT("影片動作");
  else if (Bundle.Contains(TEXT("studio"))) Group = TEXT("Studio 片段");
  else if (Bundle.Contains(TEXT("h/anim/"))) Group = TEXT("H 動作");
  else if (Bundle.Contains(TEXT("esthetic/"))) Group = TEXT("美容動作");
  else if (Bundle.Contains(TEXT("adv/"))) Group = TEXT("對話動作");
  else if (Bundle.Contains(TEXT("action/"))) Group = TEXT("日常動作");
  else if (Bundle.Contains(TEXT("custom/"))) Group = TEXT("角色編輯動作");
  if (Bundle.StartsWith(TEXT("states:"))) Group += TEXT("狀態");
  const FString Base = Group + TEXT("：") + Name;
  FString Option = Base;
  int32 Suffix = 1;
  // Duplicate names remain independently selectable; only the lookup stores
  // their source paths. Never resolve playback by a bare display name.
  while (GameRawClipKeys.Contains(Option) || GameMotionRows.Contains(Option))
   Option = Base + FString::Printf(TEXT(" (%d)"), ++Suffix);
  GameRawClipKeys.Add(Option, Key);
  GameClipLabels.Add(Key, Option);
 }
}

void UOKCharacterController::PopulateGameClips(const TArray<FString>& Clips,const FString& Selection)
{
 if (!GameClipCombo) return;
 TGuardValue<bool> Suppress(bSuppressClipCombo, true);
 AddAnimationSourceClips(SelectedGameSource, Clips);
 GameClipCombo->ClearOptions();
 GameClipCombo->AddOption(TEXT("選擇遊戲動作"));
 TArray<FString> RawOptions;
 GameRawClipKeys.GetKeys(RawOptions);
 RawOptions.Sort();
 for (const FString& Option : DemoAnimationOptions) GameClipCombo->AddOption(Option);
 for (const FString& Option : RawOptions) if(!DemoAnimationOptions.Contains(Option)) GameClipCombo->AddOption(Option);
 for (const FString& Option : GameMotionOptions) GameClipCombo->AddOption(Option);
 FString Option = Selection.IsEmpty() ? AnimationOptionForSelection() : Selection;
 if (GameClipCombo->FindOptionIndex(Option) < 0)
  Option = GameClipLabels.FindRef(SelectedGameSource.IsEmpty() ? Selection : SelectedGameSource + TEXT("|") + Selection);
 const int32 Index = GameClipCombo->FindOptionIndex(Option);
 GameClipCombo->SetSelectedIndex(Index > 0 ? Index : 0);
}

void UOKCharacterController::AddDemoAnimations()
{
 FString Json;
 TSharedPtr<FJsonObject> Catalog;
 if(!FFileHelper::LoadFileToString(Json,*(FOkSession::CommonPairRoot()/TEXT("editor-demos.json")))||
    !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Catalog)||!Catalog.IsValid())return;
 const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
 if(!Catalog->TryGetArrayField(TEXT("demos"),Rows))return;
 for(const auto& Value:*Rows)
 {
  const auto Row=Value->AsObject();if(!Row.IsValid())continue;
  bool Enabled=false;FString Key,Label;double Id=0;
  if(!Row->TryGetBoolField(Selected.Sex==0?TEXT("male"):TEXT("female"),Enabled)||!Enabled||
     !Row->TryGetStringField(TEXT("key"),Key)||!Row->TryGetStringField(TEXT("label"),Label)||
     !Row->TryGetNumberField(TEXT("id"),Id)||!Key.StartsWith(TEXT("states:controller@")))continue;
  FString Option=TEXT("Demo：")+Label;
  if(GameRawClipKeys.Contains(Option)||GameMotionRows.Contains(Option))Option+=FString::Printf(TEXT(" (%d)"),int32(Id));
  GameRawClipKeys.Add(Option,Key);GameClipLabels.Add(Key,Option);DemoAnimationOptions.Add(Option);
 }
}

void UOKCharacterController::BuildAnimationLookup()
{
 if (!PreviewActor || !PreviewActor->Session.IsOpen()) return;
 const int32 Generation = ++AnimationLookupGeneration;
 const int32 ActorGeneration = LoadGeneration;
 GameRawClipKeys.Reset();
 GameClipLabels.Reset();
 DemoAnimationOptions.Reset();
 AddDemoAnimations();
 // Imported portable clips already carry exact bundle identity. Native basic
 // clips retain the game's built-in aliases, independently of the last source.
 AddAnimationSourceClips(FString(), PreviewActor->Session.ClipNames());
 if (!SelectedGameClip.IsEmpty()) AddAnimationSourceClips(FString(), {SelectedGameClip});
 if (PreviewActor->Session.IsPortable())
 {
  // A portable card can play the imported shared clips without a game install.
  GameMotionRows.Reset(); GameMotionOptions.Reset();
  if (StudioCategoryCombo) StudioCategoryCombo->SetVisibility(ESlateVisibility::Collapsed);
  if (StudioMotionCombo) StudioMotionCombo->SetVisibility(ESlateVisibility::Collapsed);
  bAnimationLookupBuilding = false;
  PopulateGameClips({});
  return;
 }
 if (StudioCategoryCombo) StudioCategoryCombo->SetVisibility(ESlateVisibility::Visible);
 if (StudioMotionCombo) StudioMotionCombo->SetVisibility(ESlateVisibility::Visible);
 PopulateGameClips({});
 // Parsing every animation bundle here saturates disk and the allocator for minutes
 // and shows up as stepped bars on stat unitgraph. A capture does not need the list.
 if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NO_CLOTHING_WARM")) == TEXT("1"))
 {
  bAnimationLookupBuilding = false;
  UE_LOG(LogTemp, Display, TEXT("OK animation lookup skipped (capture)"));
  return;
 }
 TArray<FString> Sources = PreviewActor->Session.ClipSources();
 Sources.Sort();
 const FString Root = UKKGameRoot::Get()->Root;
 const int32 Sex = Selected.Sex;
 bAnimationLookupBuilding = true;
 ShowAnimationStatus(TEXT("正在整理動畫清單，已列出的動畫可以直接選擇播放…"));
 TWeakObjectPtr<UOKCharacterController> Weak(this);
 Async(EAsyncExecution::Thread, [Weak, Generation, ActorGeneration, Root, Sex, Sources = MoveTemp(Sources)]()
 {
  if (FRunnableThread* Thread = FRunnableThread::GetRunnableThread()) Thread->SetThreadPriority(TPri_Lowest);
  FOkSession::SetThreadBackground(true);
  TArray<TPair<FString, TArray<FString>>> Entries;
  for (const FString& Source : Sources)
   if (GameSourceMatchesSex(Source, Sex))
   {
    Entries.Emplace(Source, FOkSession::AnimationSourceNames(Root, Source));
    // One bundle can be hundreds of megabytes. Yield so the view's frame does not step.
    FPlatformProcess::Sleep(0.002f);
   }
  AsyncTask(ENamedThreads::GameThread, [Weak, Generation, ActorGeneration, Entries = MoveTemp(Entries)]()
  {
   auto* G = Weak.Get();
   if (!G || G->AnimationLookupGeneration != Generation || G->LoadGeneration != ActorGeneration) return;
   for (const auto& Entry : Entries) G->AddAnimationSourceClips(Entry.Key, Entry.Value);
   G->bAnimationLookupBuilding = false;
   G->PopulateGameClips({});
   if (!G->bAnimationPreparing && G->SelectedGameClip.IsEmpty())
    G->ShowAnimationStatus(TEXT("搜尋並選擇動畫即可播放。"));
   UE_LOG(LogTemp, Display, TEXT("OK animation lookup ready=1 sources=%d raw=%d catalog=%d"), Entries.Num(), G->GameRawClipKeys.Num(), G->GameMotionRows.Num());
  });
 });
}
FString UOKCharacterController::VideoClipTimeLabel() const{
 if(!IsValid(PreviewActor)||PreviewActor->PoseMode!=2||!FOkSession::IsVideoClipKey(SelectedGameClip))return FString();
 const float Duration=FMath::Max(PreviewActor->AnimDuration,1.e-3f);
 const float Time=PreviewActor->bGameAnimLooping?FMath::Fmod(PreviewActor->AnimPhase,Duration):FMath::Min(PreviewActor->AnimPhase,Duration);
 // Video clips are written at 30 fps (Tools/video_motion); the frame matches motion.npz rows.
 return FString::Printf(TEXT("影片動作 %s  t=%.2fs / %.2fs  第 %d 幀"),*SelectedGameClip.Mid(7),Time,Duration,FMath::FloorToInt(Time*30.f+1.e-3f));
}
bool UOKCharacterController::PlayVideoMotion(const FString& Key,const FString& Label){
 PendingVideoPair.Reset();  // a newer selection replaces a pair waiting for its partner
 if(!PreviewActor||!PreviewActor->Session.IsOpen()){ShowAnimationStatus(TEXT("請先載入角色，再選擇動作。"),true);return false;}
 OnStopPair();
 PreviewActor->SetActorTickEnabled(true);
 if(!PreviewActor->SetGameAnimation(Key)){ShowAnimationStatus(TEXT("影片動作套用失敗：")+Label,true);return false;}
 SelectedStudioRow=NAME_None;SelectedGameClip=Key;SelectedGameSource=TEXT("video:");
 {TGuardValue<bool> Suppress(bSuppressClipCombo,true);if(GameClipCombo)GameClipCombo->SetSelectedIndex(0);}
 RefreshGameAnimationParameters();ShowAnimationStatus(TEXT("正在播放影片動作：")+Label);
 return true;
}
FString UOKCharacterController::PlayVideoPair(const FString& FemaleKey,const FString& MaleKey,const FString& Label){
 if(!PreviewActor||!PreviewActor->Session.IsOpen())return TEXT("請先載入角色，再選擇動作。");
 // Keep the loaded partner (OnStopPair would destroy it); only stop native pair playback.
 CancelPairPreparation();
 if(IsValid(PairedPlayback)){PairedPlayback->Stop();PairedPlayback->Destroy();PairedPlayback=nullptr;}
 const bool bMain=Selected.Sex!=0;  // the main character plays the female part unless it is male
 AKKPreviewActor* Female=bMain?PreviewActor.Get():PartnerActor.Get();
 AKKPreviewActor* Male=bMain?PartnerActor.Get():PreviewActor.Get();
 const bool bPartner=IsValid(PartnerActor)&&PartnerActor->Session.IsOpen();
 if(bPartner)PartnerActor->SetActorTransform(PreviewActor->GetActorTransform());
 auto Play=[](AKKPreviewActor* A,const FString& Key){if(!IsValid(A)||!A->Session.IsOpen())return false;A->SetActorTickEnabled(true);return A->SetGameAnimation(Key);};
 const bool F=bMain||bPartner?Play(Female,FemaleKey):false;
 const bool M=!bMain||bPartner?Play(Male,MaleKey):false;
 SelectedStudioRow=NAME_None;SelectedGameClip=bMain?FemaleKey:MaleKey;SelectedGameSource=TEXT("video:");
 {TGuardValue<bool> Suppress(bSuppressClipCombo,true);if(GameClipCombo)GameClipCombo->SetSelectedIndex(0);}
 FString Text;bool bError=false;
 if(!bPartner&&bMain&&!bPartnerLoadInFlight&&PartnerMalePaths.Num()>0){
  // No partner yet: load the selected (or first) male card and start the pair when it is ready.
  const int32 Index=PartnerMalePaths.IsValidIndex(SelectedPartnerMale)?SelectedPartnerMale:0;
  PendingVideoPair={FemaleKey,MaleKey,Label};
  LoadPartnerFromPath(PartnerMalePaths[Index]);
  Text=FString::Printf(TEXT("女方已開始；正在自動載入男角「%s」，載入後兩人同步播放【雙人】%s。"),*PartnerMaleLabels[Index],*Label);
  bError=!F;
 }
 else if(!bPartner){Text=FString::Printf(TEXT("只播放%s方：%s"),bMain?TEXT("女"):TEXT("男"),
  bMain?TEXT("找不到可用的男性卡片（遊戲目錄 UserData/chara/male 或目前資料夾的男性獨立卡）。"):TEXT("主角色是男性，請先載入一位女性配對角色。"));bError=!(bMain?F:M);}
 else if(F&&M){Text=FString::Printf(TEXT("雙人播放中：%s（兩人同一原點、同時開始）"),*Label);bVideoPairActive=true;}
 else {Text=FString::Printf(TEXT("雙人動作套用失敗：女方%s、男方%s"),F?TEXT("成功"):TEXT("失敗"),M?TEXT("成功"):TEXT("失敗"));bError=true;}
 ShowAnimationStatus(Text,bError);
 return Text;
}
void UOKCharacterController::KeepPartnerForReload(){
 // A clothes/outfit change reloads the same card; only a different card ends the pair.
 const bool bSameCard=IsValid(PreviewActor)&&IsValid(PartnerActor)&&Entries.IsValidIndex(SelectedIndex)&&FPaths::IsSamePath(PreviewCardPath,Entries[SelectedIndex].Path);
 if(!bSameCard){OnStopPair();return;}
 PendingVideoPair.Reset();CancelPairPreparation();
 // Native pair playback drives the old preview actor; restart it on the reloaded one.
 if(IsValid(PairedPlayback)){PairedPlayback->Stop();PairedPlayback->Destroy();PairedPlayback=nullptr;PairRestoreKind=2;}
 else PairRestoreKind=bVideoPairActive?1:0;
}
void UOKCharacterController::RestorePairAfterReload(){
 const int32 Kind=PairRestoreKind;PairRestoreKind=0;
 if(!Kind||!IsValid(PreviewActor)||!IsValid(PartnerActor))return;
 PartnerActor->SetActorTransform(PreviewActor->GetActorTransform());
 if(Kind==1){
  // The reloaded character restarts its clip; take the partner's clock so both stay in step.
  if(PreviewActor->PoseMode==2&&PartnerActor->PoseMode==2){PreviewActor->AnimPhase=PartnerActor->AnimPhase;PreviewActor->ApplyPoseFrame(0.f,false);}
  return;
 }
 if(PairStateCombo&&PairStateCombo->GetSelectedIndex()>0)OnPairStateSelected(PairStateCombo->GetSelectedOption(),ESelectInfo::Direct);
}
void UOKCharacterController::ShowAnimationStatus(const FString& Text,bool bError){
 if(AnimationStatus){AnimationStatus->SetText(FText::FromString(Text));AnimationStatus->SetColorAndOpacity(FSlateColor(bError?FLinearColor(1.f,.35f,.25f):FLinearColor(1.f,.92f,.45f)));}
}

void UOKCharacterController::OnGameClipSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType != ESelectInfo::Direct) NoteAction(TEXT("clip"), SelectedItem);
	if (!bSuppressClipCombo && PreviewActor && GameClipCombo && GameClipCombo->FindOptionIndex(SelectedItem) > 0)
	{
		PendingVideoPair.Reset();bVideoPairActive=false;
		if(!bAnimationPrepared){BeginAnimationPreparation(1,SelectedItem,SelectionType);return;}
		if(const FName* Row=GameMotionRows.Find(SelectedItem)){ApplyStudioMotion(*Row,SelectedItem);return;}
		OnStopPair();
		PreviewActor->SetActorTickEnabled(true);
		const FString Key=GameRawClipKeys.FindRef(SelectedItem);
		if(Key.IsEmpty()){ShowAnimationStatus(TEXT("找不到所選動畫，請重新選擇。"),true);return;}
		if(DemoAnimationOptions.Contains(SelectedItem)){
			SelectedGameSceneMode=1;
			TGuardValue<bool> Suppress(bSuppressClipCombo,true);
			if(GameSceneCombo)GameSceneCombo->SetSelectedIndex(1);
			PreviewActor->SetGameSceneMode(1,false);
		}
		if (PreviewActor->SetGameAnimation(Key)) {
            SelectedStudioRow=NAME_None;SelectedGameClip=Key;
            FString Source,Clip;SelectedGameSource=Key.Split(TEXT("|"),&Source,&Clip)?Source:FString();
            const bool Previous=bSuppressClipCombo;bSuppressClipCombo=true;
            const FString Label=SelectedGameSource.IsEmpty()?TEXT("基本動作"):GameSourceLabel(SelectedGameSource);
            if(GameSourceCombo->FindOptionIndex(Label)<0)GameSourceCombo->AddOption(Label);
            GameSourceCombo->SetSelectedOption(Label);bSuppressClipCombo=Previous;
            RefreshGameAnimationParameters();ShowAnimationStatus(TEXT("正在播放：")+SelectedItem);
        }
		else ShowAnimationStatus(TEXT("動作套用失敗，請改選其他動作。"),true);
	}
}

void UOKCharacterController::OnGameSourceSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressClipCombo || !PreviewActor || !PreviewActor->Session.IsOpen() || !GameClipCombo)
	{
		return;
	}
	if(!bAnimationPrepared){BeginAnimationPreparation(0,SelectedItem,SelectionType);return;}
	CancelPairPreparation();
	SelectedGameSource = SelectedItem == TEXT("基本動作") ? FString() : GameSourceKey(SelectedItem);
	// Source browsing keeps the current animation until a prepared clip is chosen.
	RefreshGameAnimationParameters();
	bSuppressClipCombo = true;
	const TArray<FString>& Clips = PreparedAnimationNames;
	PopulateGameClips(Clips);
	bSuppressClipCombo = false;
	ShowAnimationStatus(FString::Printf(TEXT("已載入 %d 個原始片段。亦可直接選目錄動作，自動載入來源。"),Clips.Num()));
	if(InspectorText)InspectorText->SetText(FText::FromString(TEXT("動作來源已載入，請選擇遊戲動作播放。")));
}
void UOKCharacterController::SyncGameAnimationSelection(const FString& Key)
{
    FString Source,Clip;if(!Key.Split(TEXT("|"),&Source,&Clip)){Source.Reset();Clip=Key;}
    SelectedGameClip=Key;SelectedGameSource=Source;SelectedStudioRow=NAME_None;
    TGuardValue<bool> Suppress(bSuppressClipCombo,true);
    if(GameSourceCombo){const FString Label=Source.IsEmpty()?TEXT("基本動作"):GameSourceLabel(Source);if(GameSourceCombo->FindOptionIndex(Label)<0)GameSourceCombo->AddOption(Label);GameSourceCombo->SetSelectedOption(Label);}
    AddAnimationSourceClips(FString(), {Key});
    PopulateGameClips({});
    RefreshGameAnimationParameters();
}

void UOKCharacterController::OnGameSceneSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType != ESelectInfo::Direct) NoteAction(TEXT("scene"), SelectedItem);
	if (bSuppressClipCombo || !GameSceneCombo)
	{
		return;
	}
	const int32 Mode = GameSceneCombo->FindOptionIndex(SelectedItem);
	if (Mode < 0 || Mode > 4)
	{
		return;
	}
	OnStopPair();
	SelectedGameSceneMode = Mode;
	if (PreviewActor && PreviewActor->Session.IsOpen())
	{
		PreviewActor->SetGameSceneMode(Mode);
	}
}

void UOKCharacterController::OnLoadGameSource()
{
	if (!PreviewActor || !PreviewActor->Session.IsOpen() || !GameSourceInput || !GameSourceCombo)
	{
		return;
	}
	const FString Source = GameSourceKey(GameSourceInput->GetText().ToString().TrimStartAndEnd());
	if (Source.IsEmpty()) { return; }
	if(!bAnimationPrepared){BeginAnimationPreparation(3,Source,ESelectInfo::Direct);return;}
	CancelPairPreparation();
	const TArray<FString>& Clips = PreparedAnimationNames;
	if (Clips.IsEmpty())
	{
		if (InspectorText) InspectorText->SetText(FText::FromString(TEXT("此來源找不到動畫片段，請檢查資源包路徑")));
		return;
	}
	const FString SourceLabel = GameSourceLabel(Source);
	if (GameSourceCombo->FindOptionIndex(SourceLabel) < 0)
	{
		GameSourceCombo->AddOption(SourceLabel);
	}
	if (!Source.StartsWith(TEXT("states:")))
	{
		const FString StateSource = TEXT("states:") + Source;
		if (!PreparedAnimationStateNames.IsEmpty())
		{
			const FString StateLabel = GameSourceLabel(StateSource);
			if (GameSourceCombo->FindOptionIndex(StateLabel) < 0) GameSourceCombo->AddOption(StateLabel);
		}
	}
	// SetSelectedOption emits no event for an unchanged option. Apply the prepared
	// list explicitly so reloading the same source refreshes without a second worker.
	bSuppressClipCombo=true;GameSourceCombo->SetSelectedOption(SourceLabel);bSuppressClipCombo=false;
	OnGameSourceSelected(SourceLabel,ESelectInfo::Direct);
}

void UOKCharacterController::RefreshStudioCatalog()
{
	if (!StudioCategoryCombo || !StudioMotionCombo) return;
	StudioCategoryRows.Reset(); StudioMotionRows.Reset();GameMotionRows.Reset();GameMotionOptions.Reset();
	StudioCategoryCombo->ClearOptions(); StudioCategoryCombo->AddOption(TEXT("工作室動作分類"));
	StudioMotionCombo->ClearOptions(); StudioMotionCombo->AddOption(TEXT("選擇工作室動作"));
	const FKKExportCatalogTable* Table = AnimationCatalog ? AnimationCatalog->FindTable(TEXT("studio_catalog")) : nullptr;
	if (Table)
	{
		for (FName Row : Table->RowOrder)
		{
			const FString Kind = AnimationCatalog->GetCell(Table->Name, Row, TEXT("kind"));
			if (Kind != TEXT("anime") && Kind != TEXT("hanime")) continue;
			const FString Group = AnimationCatalog->GetCell(Table->Name, Row, TEXT("group_name"));
			const FString Category = AnimationCatalog->GetCell(Table->Name, Row, TEXT("category_name"));
			const FString Label = (Kind == TEXT("hanime") ? TEXT("親密互動：") : TEXT("工作室："))
				+ Group + TEXT(" / ") + Category;
			StudioCategoryRows.FindOrAdd(Label).Add(Row);
            const FString Base=Label+TEXT(" / ")+AnimationCatalog->GetCell(Table->Name,Row,TEXT("name"));
            FString Option=Base;int32 Suffix=1;
            while(GameMotionRows.Contains(Option))Option=Base+FString::Printf(TEXT(" (%d)"),++Suffix);
            GameMotionRows.Add(Option,Row);GameMotionOptions.Add(Option);
		}
		TArray<FString> Labels; StudioCategoryRows.GetKeys(Labels); Labels.Sort();
		for (const FString& Label : Labels) StudioCategoryCombo->AddOption(Label);
	}
	StudioCategoryCombo->SetSelectedIndex(0); StudioMotionCombo->SetSelectedIndex(0);
}

void UOKCharacterController::OnStudioCategorySelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bSuppressClipCombo || !StudioMotionCombo || !AnimationCatalog) return;
	++AnimationPrepareGeneration;PendingAnimationKind=-1;
	bSuppressClipCombo = true;
	StudioMotionRows.Reset(); StudioMotionCombo->ClearOptions();
	StudioMotionCombo->AddOption(TEXT("選擇工作室動作"));
	if (const TArray<FName>* Rows = StudioCategoryRows.Find(SelectedItem))
	{
		TSet<FString> Used;Used.Add(TEXT("選擇工作室動作"));
		StudioMotionRows = *Rows;
		for (FName Row : *Rows)
		{
			const FString Label = AnimationCatalog->GetCell(TEXT("studio_catalog"), Row, TEXT("name"));
			// Preserve distinct source rows whose translated names collide.
			FString Unique = Label; int32 Suffix = 1;
			while (Used.Contains(Unique)) Unique = FString::Printf(TEXT("%s (%d)"), *Label, ++Suffix);
			Used.Add(Unique);StudioMotionCombo->AddOption(Unique);
		}
	}
	StudioMotionCombo->SetSelectedIndex(0);
	bSuppressClipCombo = false;
}

void UOKCharacterController::OnStudioMotionSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType != ESelectInfo::Direct) NoteAction(TEXT("motion"), SelectedItem);
	if (bSuppressClipCombo || !AnimationCatalog || !PreviewActor || !PreviewActor->Session.IsOpen()) return;
	const int32 Index = StudioMotionCombo->FindOptionIndex(SelectedItem) - 1;
	if (!StudioMotionRows.IsValidIndex(Index)) return;
	if(!bAnimationPrepared){BeginAnimationPreparation(2,SelectedItem,SelectionType);return;}
    ApplyStudioMotion(StudioMotionRows[Index],SelectedItem);
}
void UOKCharacterController::ApplyStudioMotion(FName Row,const FString& SelectedItem)
{
    OnStopPair();
	const FString Source = AnimationCatalog->StudioSource(Row);
	FString Key, Error;
	if (!AnimationCatalog->ResolveStudioAnimation(Row, PreparedAnimationNames, Key, Error)
		|| !PreviewActor->SetStudioAnimation(AnimationCatalog,Row,Error))
	{
		if (InspectorText) InspectorText->SetText(FText::FromString(TEXT("無法載入這個工作室動作：") + Error));
		ShowAnimationStatus(TEXT("無法載入這個工作室動作：")+Error,true);
		return;
	}
	SelectedGameClip = Key; SelectedGameSource = Source; SelectedGameSceneMode = 4;
	SelectedStudioRow=Row;SavedStudioOptions=FVector::ZeroVector;
	// SetStudioAnimation already sets native scene=4 and publishes frame zero.
	bSuppressClipCombo = true;
	GameSceneCombo->SetSelectedIndex(4);
	const FString Label = GameSourceLabel(Source);
	if (GameSourceCombo->FindOptionIndex(Label) < 0) GameSourceCombo->AddOption(Label);
	GameSourceCombo->SetSelectedOption(Label);
    // Both the global menu and category menu select the same persistent row.
    // Never replace the unified list with clips from the last selected file.
    GameClipCombo->SetSelectedOption(AnimationOptionForSelection());
	bSuppressClipCombo = false;
	RefreshGameAnimationParameters();
	ShowAnimationStatus(TEXT("正在播放工作室動作：")+SelectedItem);
}

void UOKCharacterController::OnPoseTpose()
{
	OnStopPair();
	SelectedStudioRow=NAME_None;
	SelectedGameClip.Empty();
	if (GameClipCombo) { bSuppressClipCombo = true; GameClipCombo->SetSelectedIndex(0); bSuppressClipCombo = false; }
	if (PreviewActor)
	{
		PreviewActor->SetPoseMode(0);
		RefreshGameAnimationParameters();
	}
}

void UOKCharacterController::OnPoseWalk()
{
	OnStopPair();
	SelectedStudioRow=NAME_None;
	SelectedGameClip.Empty();
	if (GameClipCombo) { bSuppressClipCombo = true; GameClipCombo->SetSelectedIndex(0); bSuppressClipCombo = false; }
	if (PreviewActor)
	{
		PreviewActor->SetPoseMode(1);
		RefreshGameAnimationParameters();
		if (PreviewActor->PoseMode == 2)
		{
			SelectedGameClip = TEXT("states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01");
		}
	}
}

void UOKCharacterController::OnShapeChanged(bool bBody, int32 Index, float Value)
{
	// Interaction owns immutable body/face parameters. Block callbacks too,
	// including stale editor input queued during the scene transition.
	if(InteractionWidget||InteractionScene||bEnterInteractionAfterLoad)return;
	ApplyShapeValue(bBody,Index,Value);
}

void UOKCharacterController::ResetShape(bool bBody, int32 Index)
{
	if (InteractionWidget || InteractionScene || bEnterInteractionAfterLoad || Index < 0 || Index >= (bBody ? 44 : 52)) return;
	OnShapeChanged(bBody, Index, OKShapeDefaults::Value(Selected.Sex, bBody, Index));
	SyncShapeSliders();
}

void UOKCharacterController::OnResetAllShapes()
{
	if (InteractionWidget || InteractionScene || bEnterInteractionAfterLoad) return;
	TSharedPtr<FJsonObject> Mods;
 if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(EditorModsJson()),Mods)){
  TArray<TSharedPtr<FJsonValue>> Bones;
  const TArray<TSharedPtr<FJsonValue>>* Existing=nullptr;
  if(Mods->TryGetArrayField(TEXT("abm"),Existing))for(const auto& Item:*Existing){
   auto Bone=Item->AsObject();if(!Bone.IsValid())continue;
   if(Bone->GetIntegerField(TEXT("location"))>=10){Bones.Add(Item);continue;}
   // Reset only body/head modifiers; accessory editors keep their own data.
  }
  Mods->SetArrayField(TEXT("abm"),Bones);auto Preg=MakeShared<FJsonObject>();Preg->SetBoolField(TEXT("GameplayEnabled"),true);Mods->SetObjectField(TEXT("pregnancy"),Preg);CommitEditorMods(Mods);
 }
 // Batch updates so the preview rebuilds once with both complete arrays.
	Selected.Body.BustSoftness=.5f;Selected.Body.BustWeight=.5f;
 Selected.Body.ShapeValueBody.SetNum(44);
	Selected.Face.ShapeValueFace.SetNum(52);
	for (int32 Index = 0; Index < 44; ++Index)
		Selected.Body.ShapeValueBody[Index] = OKShapeDefaults::Value(Selected.Sex, true, Index);
	for (int32 Index = 0; Index < 52; ++Index)
		Selected.Face.ShapeValueFace[Index] = OKShapeDefaults::Value(Selected.Sex, false, Index);
	SyncShapeSliders();
	if (PreviewActor && bModelLayout)
		PreviewActor->MarkShapeDirty(Selected.Face.ShapeValueFace, Selected.Body.ShapeValueBody);
}

void UOKCharacterController::OnInteractionBellyChanged(float Value)
{
	if(!InteractionWidget||!InteractionScene||!IsValid(PreviewActor)||!FMath::IsFinite(Value))return;
	ApplyShapeValue(true,21,FMath::Clamp(Value,-1.f,2.f));
}

void UOKCharacterController::ApplyShapeValue(bool bBody, int32 Index, float Value)
{
	if(Index<0||!FMath::IsFinite(Value))return;
	if (bBody)
	{
		if (!Selected.Body.ShapeValueBody.IsValidIndex(Index))
		{
			const int32 OldCount = Selected.Body.ShapeValueBody.Num();
			Selected.Body.ShapeValueBody.SetNum(Index + 1);
			for (int32 I = OldCount; I <= Index; ++I) Selected.Body.ShapeValueBody[I] = 0.5f;
		}
		Selected.Body.ShapeValueBody[Index] = Value;
	}
	else
	{
		if (!Selected.Face.ShapeValueFace.IsValidIndex(Index))
		{
			const int32 OldCount = Selected.Face.ShapeValueFace.Num();
			Selected.Face.ShapeValueFace.SetNum(Index + 1);
			for (int32 I = OldCount; I <= Index; ++I) Selected.Face.ShapeValueFace[I] = 0.5f;
		}
		Selected.Face.ShapeValueFace[Index] = Value;
	}
	if (PreviewActor && bModelLayout)
	{
		PreviewActor->MarkShapeDirty(Selected.Face.ShapeValueFace, Selected.Body.ShapeValueBody);
	}
}

static UOKCharacterController* FindGallery()
{
	for (TObjectIterator<UOKCharacterController> It; It; ++It)
	{
		if (IsValid(*It) && It->GetWorld())
		{
			return *It;
		}
	}
	return nullptr;
}

static void ShotGallery(const TCHAR* Name)
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / Name;
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogTemp, Display, TEXT("ok.VerifyGallery shot %s"), Name);
}

static void VerifyGalleryNow()
{
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.World() && (Ctx.WorldType == EWorldType::Game || Ctx.WorldType == EWorldType::PIE))
			{
				World = Ctx.World();
				break;
			}
		}
	}
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("ok.VerifyGallery: no world"));
		return;
	}
	TSharedRef<int32> Step = MakeShared<int32>(0);
	TSharedRef<int32> Waits = MakeShared<int32>(0);
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([World, Step, Waits]()
	{
		UOKCharacterController* Gallery = FindGallery();
		AKKPreviewActor* Actor = Gallery ? Gallery->PreviewActor : nullptr;
		if (!Gallery || !Actor || Actor->SkelFollowers.Num() == 0)
		{
			*Waits += 1;
			if (*Waits > 90)
			{
				UE_LOG(LogTemp, Error, TEXT("ok.VerifyGallery: gallery never loaded"));
				FPlatformMisc::RequestExit(false);
			}
			return;
		}
		UPoseableMeshComponent* Comp = Actor->SkelFollowers[0];
		const int32 StepNow = *Step;
		*Step += 1;
		if (StepNow == 0)
		{
			UE_LOG(LogTemp, Display, TEXT("ok.VerifyGallery sliders=%d followers=%d"),
				Gallery->ShapeSliders.Num(), Actor->SkelFollowers.Num());
			Gallery->OnShapeChanged(true, 0, 1.f);
			Actor->ApplyPoseFrame(0.f);
			const FVector Hip = Comp->GetBoneLocation(TEXT("cf_j_hips"), EBoneSpaces::ComponentSpace);
			UE_LOG(LogTemp, Display, TEXT("ok.VerifyGallery height hipZ=%.3f"), Hip.Z);
			ShotGallery(TEXT("gallery_tall.png"));
		}
		else if (StepNow == 1)
		{
			Gallery->OnPoseWalk();
			Actor->AnimPhase = 0.25f;
			Actor->ApplyPoseFrame(0.f);
			const FVector HandL = Comp->GetBoneLocation(TEXT("cf_j_hand_L"), EBoneSpaces::ComponentSpace);
			const FVector HandR = Comp->GetBoneLocation(TEXT("cf_j_hand_R"), EBoneSpaces::ComponentSpace);
			UE_LOG(LogTemp, Display, TEXT("ok.VerifyGallery walk len=%.3f pose=%d L=%s R=%s"),
				Actor->WalkSeq ? Actor->WalkSeq->GetPlayLength() : -1.f,
				Actor->PoseMode,
				*HandL.ToString(),
				*HandR.ToString());
			if (Actor->StudioCam)
			{
				const FVector Look = Actor->GetActorLocation() + FVector(0.f, 0.f, 90.f);
				const FVector Cam = Look + FVector(420.f, 280.f, 40.f);
				Actor->StudioCam->SetActorLocationAndRotation(Cam, (Look - Cam).Rotation());
			}
			ShotGallery(TEXT("gallery_walk.png"));
		}
		else if (StepNow == 2)
		{
			Gallery->OnPoseTpose();
			Actor->ApplyPoseFrame(0.f);
			UE_LOG(LogTemp, Display, TEXT("ok.VerifyGallery tpose pose=%d"), Actor->PoseMode);
			ShotGallery(TEXT("gallery_tpose.png"));
		}
		else
		{
			FPlatformMisc::RequestExit(false);
		}
	}), 1.0f, true);
}

static FAutoConsoleCommand VerifyGalleryCmd(
	TEXT("ok.VerifyGallery"),
	TEXT("Load the gallery card, move the height slider, play the Unreal walk, return to Ｔ字站姿"),
	FConsoleCommandDelegate::CreateStatic(&VerifyGalleryNow));

static TArray<FTransform> SnapshotGalleryAnimator(AKKPreviewActor* Actor)
{
    TArray<FTransform> Pose;
    if(Actor && !Actor->SkelFollowers.IsEmpty() && Actor->SkelFollowers[0]) {
        UPoseableMeshComponent* Comp=Actor->SkelFollowers[0];
        for(int32 Bone=0;Bone<Comp->GetNumBones();++Bone) Pose.Add(Comp->GetBoneTransform(Bone));
    }
    return Pose;
}

static int32 ChangedGalleryAnimatorBones(AKKPreviewActor* Actor,const TArray<FTransform>& Before)
{
    const TArray<FTransform> After=SnapshotGalleryAnimator(Actor);
    if(Before.IsEmpty() || Before.Num()!=After.Num()) return 0;
    int32 Changed=0;
    for(int32 Bone=0;Bone<Before.Num();++Bone) Changed+=!Before[Bone].Equals(After[Bone],1.e-5);
    return Changed;
}

// Exercise the actual Gallery combo events, pause/replay buttons and pair UI.
static void VerifyHPlaybackNow(){
    UWorld* World=nullptr;if(GEngine)for(const FWorldContext& C:GEngine->GetWorldContexts())if(C.World()&&(C.WorldType==EWorldType::Game||C.WorldType==EWorldType::PIE)){World=C.World();break;}
    if(!World){UE_LOG(LogTemp,Error,TEXT("ok.VerifyHPlayback no world"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    struct FCheck{int32 Step=0,Waits=0;uint64 LastFrame=MAX_uint64;float Held=0;TArray<FTransform> Before,PartnerBefore;};auto Check=MakeShared<FCheck>();
    FTimerHandle Timer;World->GetTimerManager().SetTimer(Timer,FTimerDelegate::CreateLambda([World,Check](){
        UOKCharacterController* G=FindGallery();AKKPreviewActor* A=G?G->PreviewActor.Get():nullptr;
        auto Fail=[](const TCHAR* Why){UE_LOG(LogTemp,Error,TEXT("ok.VerifyHPlayback FAIL %s"),Why);FPlatformMisc::RequestExitWithStatus(false,1);};
        if(!A||!A->Session.IsOpen()||A->SkelFollowers.IsEmpty()){if(++Check->Waits>480)Fail(TEXT("Gallery load timeout"));return;}
        auto SelectSource=[&](const TCHAR* File){FString Source=FString::Printf(TEXT("states:h/anim/%s/%s.unity3d"),G->Selected.Sex==0?TEXT("male"):TEXT("female"),File);FString Label=GameSourceLabel(Source);if(G->GameSourceCombo->FindOptionIndex(Label)<0)return false;G->GameSourceCombo->SetSelectedOption(Label);for(int32 I=1;I<G->GameClipCombo->GetOptionCount();++I){const FString Clip=G->GameClipCombo->GetOptionAtIndex(I);if(Clip.EndsWith(TEXT(" / Base Layer.L_SLoop1"))){G->GameClipCombo->SetSelectedIndex(I);return A->PoseMode==2;}}return false;};
        if(Check->LastFrame==GFrameCounter)return;Check->LastFrame=GFrameCounter;
        const int32 Step=Check->Step++;
        if(Step==0){
            int32 RoleSources=0;for(int32 I=0;I<G->GameSourceCombo->GetOptionCount();++I){const FString Source=GameSourceKey(G->GameSourceCombo->GetOptionAtIndex(I));if(!GameSourceMatchesSex(Source,G->Selected.Sex)){Fail(TEXT("opposite role leaked into combo"));return;}if(Source.Contains(G->Selected.Sex==0?TEXT("h/anim/male/"):TEXT("h/anim/female/")))++RoleSources;}
            if(RoleSources<(G->Selected.Sex==0?8:16)||!SelectSource(TEXT("01_01_00"))){Fail(TEXT("automatic role source/selection"));return;}Check->Before=SnapshotGalleryAnimator(A);UE_LOG(LogTemp,Display,TEXT("ok.VerifyHPlayback role=%d auto_sources=%d selected=%s"),G->Selected.Sex,RoleSources,*G->SelectedGameClip);
        }else if(Step==1){if(A->AnimPhase<=0||ChangedGalleryAnimatorBones(A,Check->Before)==0){Fail(TEXT("single role did not advance"));return;}G->OnPausePlayback();Check->Held=A->AnimPhase;Check->Before=SnapshotGalleryAnimator(A);
        }else if(Step==2){if(A->IsActorTickEnabled()||A->AnimPhase!=Check->Held||ChangedGalleryAnimatorBones(A,Check->Before)!=0){Fail(TEXT("single pause drift"));return;}G->OnReplayPlayback();if(A->AnimPhase!=0||!A->IsActorTickEnabled()){Fail(TEXT("single replay clock"));return;}
        }else if(Step==3){if(!SelectSource(TEXT("02_01_00"))){Fail(TEXT("source switch failed"));return;}Check->Before=SnapshotGalleryAnimator(A);
        }else if(Step==4){if(A->AnimPhase<=0||ChangedGalleryAnimatorBones(A,Check->Before)==0){Fail(TEXT("switched source did not advance"));return;}const FString Partner=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_PAIR_CARD"));if(Partner.IsEmpty()){Fail(TEXT("missing pair test card"));return;}auto SameCard=[](const FString& A,const FString& B){auto Norm=[](FString P){return FPaths::ConvertRelativePathToFull(P).Replace(TEXT("\\"),TEXT("/"));};return Norm(A).Equals(Norm(B),ESearchCase::IgnoreCase);};int32 MaleIndex=INDEX_NONE;for(int32 I=0;I<G->PartnerMalePaths.Num();++I)if(SameCard(G->PartnerMalePaths[I],Partner)){MaleIndex=I;break;}const bool bMaleFile=Partner.Replace(TEXT("\\"),TEXT("/")).Contains(TEXT("/chara/male/"));if(bMaleFile&&MaleIndex==INDEX_NONE){Fail(TEXT("male partner missing from dropdown"));return;}if(MaleIndex!=INDEX_NONE){G->PartnerMaleCombo->SetSelectedIndex(MaleIndex+1);G->OnLoadPartner();}else G->LoadPartnerFromPath(Partner);if(!G->PartnerActor||G->PairCombo->GetOptionCount()<2){Fail(TEXT("native pair catalog/partner"));return;}G->PairCombo->SetSelectedIndex(1);const FString State=G->PairStateCombo->FindOptionIndex(TEXT("Idle"))>0?TEXT("Idle"):G->PairStateCombo->GetOptionAtIndex(1);G->PairStateCombo->SetSelectedOption(State);if(!G->PairedPlayback){Fail(TEXT("native pair start"));return;}Check->Before=SnapshotGalleryAnimator(A);Check->PartnerBefore=SnapshotGalleryAnimator(G->PartnerActor);
        }else if(Step==5){if(!G->PairedPlayback||G->PairedPlayback->GetElapsed()<=0||A->AnimPhase!=G->PartnerActor->AnimPhase||A->IsActorTickEnabled()||G->PartnerActor->IsActorTickEnabled()){Fail(TEXT("shared clock ownership"));return;}G->OnPausePlayback();Check->Held=G->PairedPlayback->GetElapsed();Check->Before=SnapshotGalleryAnimator(A);Check->PartnerBefore=SnapshotGalleryAnimator(G->PartnerActor);
        }else if(Step==6){if(G->PairedPlayback->GetElapsed()!=Check->Held||ChangedGalleryAnimatorBones(A,Check->Before)||ChangedGalleryAnimatorBones(G->PartnerActor,Check->PartnerBefore)){Fail(TEXT("paired pause drift"));return;}G->OnReplayPlayback();if(G->PairedPlayback->GetElapsed()!=0||A->AnimPhase!=0||G->PartnerActor->AnimPhase!=0){Fail(TEXT("paired replay clock"));return;}
        }else if(Step==7){auto P=G->PairedPlayback;P->SetActorTickEnabled(false);FString Error;if(!P->Restart(Error)){Fail(*Error);return;}P->SetActorTickEnabled(false);if(!P->Advance(.61f,Error)||!P->SetSpeed(2.f)||!P->Advance(.1f,Error)||!FMath::IsNearlyEqual(P->GetElapsed(),.81f)){Fail(TEXT("paired deterministic speed"));return;}const float Held=P->GetElapsed();if(P->Advance(-1.f,Error)||P->GetElapsed()!=Held||P->SetSpeed(-1.f)){Fail(TEXT("invalid input changed pair clock"));return;}UE_LOG(LogTemp,Display,TEXT("ok.VerifyHPlayback pair shared=%.6f female=%.6f male=%.6f pause/replay/speed/invalid PASS"),P->GetElapsed(),P->FemaleActor->AnimPhase,P->MaleActor->AnimPhase);A->StudioCam->SetActorRotation(FRotator(0,0,0));A->FramePairedPose(G->PartnerActor);
        }else if(Step==8){FString ShotDir=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_H_VERIFY_SHOT_DIR"));if(ShotDir.IsEmpty())ShotDir=FPaths::ProjectSavedDir()/TEXT("Tests/HPlaybackGallery")/FGuid::NewGuid().ToString(EGuidFormats::Digits);IFileManager::Get().MakeDirectory(*ShotDir,true);const FString Path=ShotDir/(FPaths::GetBaseFilename(G->Entries[G->SelectedIndex].Path)+TEXT("_pair.png"));FScreenshotRequest::RequestScreenshot(Path,false,false);
        }else if(Step==9){G->PairCombo->SetSelectedIndex(2);if(G->PairStateCombo->GetOptionCount()<2){Fail(TEXT("pair switch common states"));return;}G->PairStateCombo->SetSelectedIndex(1);if(!G->PairedPlayback){Fail(TEXT("pair switch failed"));return;}
        }else if(Step==10){if(A->AnimPhase!=G->PartnerActor->AnimPhase){Fail(TEXT("switched pair clock drift"));return;}G->OnStopPair();if(G->PartnerActor||G->PairedPlayback||A->PoseMode!=0||!G->SelectedGameClip.IsEmpty()){Fail(TEXT("pair teardown"));return;}UE_LOG(LogTemp,Display,TEXT("ok.VerifyHPlayback PASS role=%d single/pair selection tick pause replay source-switch speed teardown"),G->Selected.Sex);FPlatformMisc::RequestExitWithStatus(false,0);}
    }),.3f,true);
}
static FAutoConsoleCommand VerifyHPlaybackCmd(TEXT("ok.VerifyHPlayback"),TEXT("Verify role sources and native paired Gallery playback"),FConsoleCommandDelegate::CreateStatic(&VerifyHPlaybackNow));

static void VerifyAnimationCatalogNow()
{
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{ World = Context.World(); break; }
		}
	}
	if (!World) { UE_LOG(LogTemp, Error, TEXT("ok.VerifyAnimationCatalog: no world")); return; }
	TSharedRef<int32> Waits = MakeShared<int32>(0);
	TSharedRef<int32> Step = MakeShared<int32>(0);
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([World, Waits, Step]()
	{
		UOKCharacterController* Gallery = FindGallery();
		AKKPreviewActor* Actor = Gallery ? Gallery->PreviewActor : nullptr;
		if (!Gallery || !Actor || Gallery->bLoadInFlight || !Gallery->AnimationCatalog)
		{
			if (++*Waits > 120) { UE_LOG(LogTemp, Error, TEXT("ok.VerifyAnimationCatalog: load timeout")); FPlatformMisc::RequestExitWithStatus(false, 1); }
			return;
		}
		if (*Step == 0)
		{
			const TArray<FName> Rows = Gallery->AnimationCatalog->FindRows(TEXT("studio_catalog"), TEXT("clip"), TEXT("f_aruki_00_00_01"));
			if (Rows.IsEmpty()) { UE_LOG(LogTemp, Error, TEXT("catalog walk row missing")); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
			FString Category;
			for (const auto& Pair : Gallery->StudioCategoryRows) if (Pair.Value.Contains(Rows[0])) { Category = Pair.Key; break; }
			Gallery->StudioCategoryCombo->SetSelectedOption(Category);
			const int32 Motion = Gallery->StudioMotionRows.Find(Rows[0]);
			Gallery->StudioMotionCombo->SetSelectedIndex(Motion + 1);
			if (Motion < 0 || Actor->PoseMode != 2 || !Gallery->SelectedGameClip.Contains(TEXT("walk_run /")))
			{ UE_LOG(LogTemp, Error, TEXT("catalog gallery selection did not play")); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
			Gallery->MouthPatternCombo->SetSelectedIndex(8);
			Gallery->OnMouthChanged(0.8f);
			if (!Gallery->MouthOpenSlider || !FMath::IsNearlyEqual(Gallery->MouthOpenSlider->GetValue(), 0.8f))
			{ UE_LOG(LogTemp, Error, TEXT("catalog mouth slider did not synchronize")); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
			Actor->SetActorTickEnabled(false);
			Actor->AnimPhase = 0.4f;
			Actor->ApplyPoseFrame(1.f / 60.f);
			Actor->AdvanceFaceAnimation(.15f);
            if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_ANIM_BLEND"))==TEXT("1")) {
                Gallery->GameSourceCombo->SetSelectedOption(GameSourceLabel(TEXT("states:action/animator/00.unity3d")));
                Gallery->OnLoadGameSource();
                Gallery->GameClipCombo->SetSelectedOption(TEXT("base / Base Layer.Main.Locomotion_Anger"));
                if(Actor->PoseMode!=2||!Gallery->GameAnimationParameters||Gallery->GameAnimationParameters->GetChildrenCount()!=2) {
                    UE_LOG(LogTemp,Error,TEXT("KK animator gallery parameter row missing"));FPlatformMisc::RequestExitWithStatus(false,1);return;
                }
                Actor->SetActorTickEnabled(false);Actor->AnimPhase=.4f;Actor->ApplyPoseFrame(0.f);
                const float Phase=Actor->AnimPhase/Actor->AnimDuration,FaceTime=Actor->FaceClock;
                UOKShapeSlider* Slider=nullptr;
                UOKShapeSlider* ClockSlider=nullptr;
                for(UWidget* Child:Gallery->GameAnimationParameters->GetAllChildren()) {
                    UHorizontalBox* Row=Cast<UHorizontalBox>(Child);
                    UOKShapeSlider* Candidate=Row?Cast<UOKShapeSlider>(Row->GetChildAt(1)):nullptr;
                    if(Candidate&&Candidate->AnimationParameter==TEXT("Speed")) Slider=Candidate;
                    if(Candidate&&Candidate->AnimationParameter==TEXT("MotionSpeed")) ClockSlider=Candidate;
                }
                if(!Slider||Slider->AnimationParameter!=TEXT("Speed")) {
                    UE_LOG(LogTemp,Error,TEXT("KK animator gallery actual slider missing"));FPlatformMisc::RequestExitWithStatus(false,1);return;
                }
                const TArray<FTransform> Before=SnapshotGalleryAnimator(Actor);
                Slider->SetValue(.37f);Slider->OnValueChanged.Broadcast(.37f);
                const int32 DisplayChanged=ChangedGalleryAnimatorBones(Actor,Before);
                FString Name;float Value,Min,Max;
                bool ValueFound=false;
                for(int32 Index=0;Index<Actor->Session.AnimationFloatCount();++Index)
                    if(Actor->Session.AnimationFloatAt(Index,Name,Value,Min,Max)&&Name==TEXT("Speed")) {ValueFound=true;break;}
                if(!ValueFound||!FMath::IsNearlyEqual(Value,.37f)||
                    !FMath::IsNearlyEqual(Actor->AnimPhase/Actor->AnimDuration,Phase,1.e-6f)||Actor->FaceClock!=FaceTime||DisplayChanged==0) {
                    UE_LOG(LogTemp,Error,TEXT("KK animator gallery callback/phase/face clock failed"));FPlatformMisc::RequestExitWithStatus(false,1);return;
                }
                UE_LOG(LogTemp,Display,TEXT("KK animator gallery verified source_callback=1 state_callback=1 slider_callback=1 value=0.37 phase_preserved=1 face_clock_unchanged=1 display_changed_bones=%d"),DisplayChanged);
                const float ClockPhase=Actor->AnimPhase/Actor->AnimDuration,ClockDuration=Actor->AnimDuration;
                const TArray<FTransform> ClockBefore=SnapshotGalleryAnimator(Actor);
                if(!ClockSlider) {UE_LOG(LogTemp,Error,TEXT("KK animator clock slider missing"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
                ClockSlider->SetValue(1.6f);ClockSlider->OnValueChanged.Broadcast(1.6f);
                if(!FMath::IsNearlyEqual(Actor->AnimDuration,ClockDuration/1.6f,1.e-5f)||
                    !FMath::IsNearlyEqual(Actor->AnimPhase/Actor->AnimDuration,ClockPhase,1.e-6f)||Actor->FaceClock!=FaceTime||
                    ChangedGalleryAnimatorBones(Actor,ClockBefore)!=0) {
                    UE_LOG(LogTemp,Error,TEXT("KK animator clock callback/duration/phase failed"));FPlatformMisc::RequestExitWithStatus(false,1);return;
                }
                Actor->AnimPhase+=.23f;Actor->ApplyPoseFrame(0.f);
                const int32 ClockChanged=ChangedGalleryAnimatorBones(Actor,ClockBefore);
                if(ClockChanged==0) {UE_LOG(LogTemp,Error,TEXT("KK animator clock did not advance displayed bones"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
                UE_LOG(LogTemp,Display,TEXT("KK animator clock gallery verified multiplier=1.6 phase_preserved=1 face_clock_unchanged=1 immediate_pose_unchanged=1 duration_ratio=0.625 advanced_display_bones=%d"),ClockChanged);
            }
            if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_ANIM_CARTESIAN"))==TEXT("1")) {
                Gallery->GameSourceCombo->SetSelectedOption(GameSourceLabel(TEXT("states:studio/anime/00.unity3d")));
                Gallery->OnLoadGameSource();
                Gallery->GameClipCombo->SetSelectedOption(TEXT("khs3_f_base3 / 17:Te_siri_D_momu_L.Te_siri_D_momu_L"));
                if(Actor->PoseMode!=2||!Gallery->GameAnimationParameters||Gallery->GameAnimationParameters->GetChildrenCount()!=3) {
                    UE_LOG(LogTemp,Error,TEXT("KK Cartesian gallery parameter rows missing"));FPlatformMisc::RequestExitWithStatus(false,1);return;
                }
                Actor->SetActorTickEnabled(false);Actor->AnimPhase=.4f;Actor->ApplyPoseFrame(0.f);
                const float Phase=Actor->AnimPhase/Actor->AnimDuration,FaceTime=Actor->FaceClock;
                // Ver02 writes cf_j_siri_L after Animator from its reference
                // chain. Isolate this physics-owned channel while verifying
                // the UI -> evaluated pose -> displayed palette connection.
                // Restore the real physics pose before capturing the Gallery.
                const FString OldPhysics=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_NODB"));
                struct FRestorePhysics {FString Value;~FRestorePhysics(){FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),Value.IsEmpty()?nullptr:*Value);}} RestorePhysics{OldPhysics};
                FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),TEXT("1"));
                Actor->ApplyPoseFrame(0.f);
                int32 Changed=0,DisplayChanges=0;
                for(int32 Index=0;Index<Gallery->GameAnimationParameters->GetChildrenCount();++Index) {
                    UHorizontalBox* Row=Cast<UHorizontalBox>(Gallery->GameAnimationParameters->GetChildAt(Index));
                    UOKShapeSlider* Slider=Row?Cast<UOKShapeSlider>(Row->GetChildAt(1)):nullptr;
                    if(!Slider) {UE_LOG(LogTemp,Error,TEXT("KK Cartesian actual slider missing"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
                    if(Slider->AnimationParameter==TEXT("siriL_X")||Slider->AnimationParameter==TEXT("siriL_Y")) {
                        const float Value=Slider->AnimationParameter==TEXT("siriL_X")?.31f:.79f;
                        const TArray<FTransform> Before=SnapshotGalleryAnimator(Actor);
                        const TArray<FTransform> BeforeLocal=Actor->SkelFollowers.IsEmpty()?TArray<FTransform>():Actor->SkelFollowers[0]->BoneSpaceTransforms;
                        Slider->SetValue(Value);Slider->OnValueChanged.Broadcast(Value);++Changed;
                        const int32 DisplayChanged=ChangedGalleryAnimatorBones(Actor,Before);
                        int32 LocalChanged=0;
                        if(!Actor->SkelFollowers.IsEmpty()) {
                            const auto& Locals=Actor->SkelFollowers[0]->BoneSpaceTransforms;
                            for(int32 Bone=0;Bone<FMath::Min(BeforeLocal.Num(),Locals.Num());++Bone) LocalChanged+=!BeforeLocal[Bone].Equals(Locals[Bone],1.e-5);
                        }
                        UE_LOG(LogTemp,Display,TEXT("KK Cartesian slider response parameter=%s world_changed=%d local_changed=%d external_flag=%s"),*Slider->AnimationParameter,DisplayChanged,LocalChanged,*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_UE_ANIM")));
                        if(DisplayChanged==0) {UE_LOG(LogTemp,Error,TEXT("KK Cartesian slider did not change displayed skeleton: %s"),*Slider->AnimationParameter);FPlatformMisc::RequestExitWithStatus(false,1);return;}
                        DisplayChanges+=DisplayChanged;
                    }
                }
                int32 Verified=0;
                for(int32 Index=0;Index<Actor->Session.AnimationFloatCount();++Index) {
                    FString Name;float Value,Min,Max;
                    if(!Actor->Session.AnimationFloatAt(Index,Name,Value,Min,Max)) continue;
                    if((Name==TEXT("siriL_X")&&FMath::IsNearlyEqual(Value,.31f))||(Name==TEXT("siriL_Y")&&FMath::IsNearlyEqual(Value,.79f))) ++Verified;
                }
                if(Changed!=2||Verified!=2||!FMath::IsNearlyEqual(Actor->AnimPhase/Actor->AnimDuration,Phase,1.e-6f)||Actor->FaceClock!=FaceTime) {
                    UE_LOG(LogTemp,Error,TEXT("KK Cartesian callbacks/phase/clock failed"));FPlatformMisc::RequestExitWithStatus(false,1);return;
                }
                FPlatformMisc::SetEnvironmentVar(TEXT("OK_NODB"),OldPhysics.IsEmpty()?nullptr:*OldPhysics);
                Actor->ApplyPoseFrame(0.f);
                UE_LOG(LogTemp,Display,TEXT("KK Cartesian gallery verified source_callback=1 state_callback=1 slider_callbacks=2 rows=3 phase_preserved=1 face_clock_unchanged=1 display_changed_bones=%d physics_isolated=1 physics_restored=1"),DisplayChanges);
            }
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_HAND_SHAPES")) == TEXT("1"))
			{
				const float Time = Actor->AnimPhase;
				if (Gallery->HandCombos.Num() != 4 || Gallery->HandBlendSliders.Num() != 2
					|| Actor->GetHandShapeCount() != 24 || Gallery->HandCombos[0]->GetOptionCount() != 25)
				{ UE_LOG(LogTemp, Error, TEXT("hand gallery controls/count missing")); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
				Gallery->HandCombos[0]->SetSelectedIndex(2); // authored key 1
				Gallery->HandCombos[1]->SetSelectedIndex(23);
				Gallery->OnLeftHandBlend(0.3f);
				Gallery->HandCombos[2]->SetSelectedIndex(24); // authored key 23
				Gallery->HandCombos[3]->SetSelectedIndex(1);
				Gallery->OnRightHandBlend(0.8f);
				if (Actor->AnimPhase != Time || Gallery->SelectedHandA[0] != 1 || Gallery->SelectedHandA[1] != 23
					|| !FMath::IsNearlyEqual(Gallery->HandBlendSliders[0]->GetValue(), 0.3f)
					|| !FMath::IsNearlyEqual(Gallery->HandBlendSliders[1]->GetValue(), 0.8f))
				{ UE_LOG(LogTemp, Error, TEXT("hand gallery selection/blend/clock gate failed")); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
				UE_LOG(LogTemp, Display, TEXT("KK hand gallery verified count=24 left=1->23 t=0.3 right=23->1 t=0.8 clock=%.6f"), Time);
			}
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_FACE_CONTROLS")) == TEXT("1"))
			{
				Gallery->AutomaticBlinkCheck->SetIsChecked(false);
				// SetIsChecked changes the widget value without broadcasting its event.
				Gallery->OnAutomaticBlink(false);
				const float Time = Actor->AnimPhase;
				if (Gallery->FaceCombos.Num() != 2 || Gallery->FaceOpenSliders.Num() != 2
					|| Gallery->FaceCombos[0]->GetOptionCount() != Actor->GetFacePatternCount(1)
					|| Gallery->FaceCombos[1]->GetOptionCount() != Actor->GetFacePatternCount(2))
				{ UE_LOG(LogTemp, Error, TEXT("face gallery controls/count missing")); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
				Gallery->FaceCombos[0]->SetSelectedIndex(1); Gallery->OnEyesOpen(.61337f);
				Gallery->FaceCombos[1]->SetSelectedIndex(2); Gallery->OnBrowOpen(.8f);
				if (Actor->AnimPhase != Time || Actor->FacePattern[0] != 1 || Actor->FacePattern[1] != 2
					|| !FMath::IsNearlyEqual(Actor->FaceOpen01[0], .61337f) || !FMath::IsNearlyEqual(Actor->FaceOpen01[1], .8f)
					|| !FMath::IsNearlyEqual(Gallery->FaceOpenSliders[0]->GetValue(), .61337f))
				{ UE_LOG(LogTemp, Error, TEXT("face gallery selection/rate/clock gate failed")); FPlatformMisc::RequestExitWithStatus(false, 1); return; }
				UE_LOG(LogTemp, Display, TEXT("KK face gallery verified eyes=%d brow=%d patterns=(1,2) rates=(0.61337,0.8) clock=%.6f"),
					Actor->GetFacePatternCount(1), Actor->GetFacePatternCount(2), Time);
				Actor->AdvanceFaceAnimation(.15f);
			}
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_FACE_TIME")) == TEXT("1"))
			{
				const float Time=Actor->AnimPhase;
				Gallery->AutomaticBlinkCheck->SetIsChecked(true);
				Gallery->FaceCombos[0]->SetSelectedIndex(2);
				Actor->AdvanceFaceAnimation(.05f);
				const float Alpha=Actor->FaceTransitions[1].Alpha();
				Gallery->FaceCombos[0]->SetSelectedIndex(0);
				const int32 Back=Actor->FaceTransitions[1].Back;
				Actor->AdvanceFaceAnimation(.15f);
				Gallery->OnForceBlink();
				Actor->AdvanceFaceAnimation(Actor->FaceBlink.CalcSpeed*.5f);
				if (!Actor->bAutomaticBlink || Gallery->FaceOpenSliders[0]->GetIsEnabled()
					|| Back!=2 || !FMath::IsNearlyEqual(Alpha,1.f/3.f,1e-6f) || Actor->AnimPhase!=Time
					|| !FMath::IsNearlyEqual(Actor->FaceBlink.OpenRate,.5f,1e-5f))
				{ UE_LOG(LogTemp,Error,TEXT("KK face time gallery failed")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
				UE_LOG(LogTemp,Display,TEXT("KK face time gallery verified alpha=%.6f interrupted_back=%d blink=%.6f eyes=%.6f brow=%.6f clock=%.6f"),
					Alpha,Back,Actor->FaceBlink.OpenRate,Actor->EffectiveFaceRate(1),Actor->EffectiveFaceRate(2),Time);
			}
			Actor->FrameCurrentPose();
			if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_EYE_GAZE"))==TEXT("1"))
			{
				const float Time=Actor->AnimPhase;
				Gallery->AutomaticBlinkCheck->SetIsChecked(true);
				Actor->FaceBlink.FixedFlags=1;Actor->BlinkEyeRate=1.f;Actor->BlinkBrowRate=1.f;
				Gallery->OnGazeLeft(.6f);Gallery->OnGazeRight(-.4f);Gallery->OnGazeVertical(-.4f);
				const float Correct=OKEyeCorrectOpenMax(Actor->Session.FaceDefaultRate(1),.6f,-.4f,Actor->Session.FaceRuntimeSetting(8),Actor->Session.FaceRuntimeSetting(9),Actor->Session.FaceRuntimeSetting(10));
				if(Actor->AnimPhase!=Time || !FMath::IsNearlyEqual(Actor->EffectiveFaceRate(1),Correct,1e-6f)
					|| Actor->EyeGaze[0]!=.6f || Actor->EyeGaze[1]!=-.4f || Actor->EyeGaze[2]!=-.4f)
				{UE_LOG(LogTemp,Error,TEXT("KK gaze gallery callback/rate/clock failed"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
				UE_LOG(LogTemp,Display,TEXT("KK gaze gallery verified rates=(0.6,-0.4,-0.4) eyes=%.6f clock_unchanged=1"),Correct);
			}
			UE_LOG(LogTemp, Display, TEXT("KK catalog gallery sources=%d categories=%d clips=%d selected=%s mouth=%s"),
// source eye look camera UI verification runs without advancing the clip clock.
				Gallery->GameSourceCombo->GetOptionCount(), Gallery->StudioCategoryCombo->GetOptionCount(),
				Gallery->GameClipCombo->GetOptionCount(), *Gallery->SelectedGameClip,
				*Gallery->MouthPatternCombo->GetSelectedOption());
			if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_EYE_LOOK"))==TEXT("1"))
			{
				const float Time=Actor->AnimPhase,Clock=Actor->FaceClock;
				Gallery->OnEyeLookCamera(true);
				bool Good=Actor->bEyeLookFollowCamera && Actor->AdvanceEyeLook(.05f);
				Gallery->OnEyeLookCamera(false);Good&=!Actor->bEyeLookTracking;
				Gallery->OnEyeLookCamera(true);Good&=Actor->bEyeLookFollowCamera;
				Good&=Actor->AnimPhase==Time && Actor->FaceClock==Clock;
				if(!Good) {UE_LOG(LogTemp,Error,TEXT("KK eye look gallery callbacks failed"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
				Gallery->EyeLookCameraCheck->SetIsChecked(true);
				UE_LOG(LogTemp,Display,TEXT("KK eye look gallery verified camera_tracking=1 clock_unchanged=1"));
			}
			++*Step;
		}
		else if (*Step == 1)
		{
#if WITH_EDITOR
			if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
			{
				if (++*Waits > 120) { UE_LOG(LogTemp, Error, TEXT("catalog gallery shader timeout")); FPlatformMisc::RequestExitWithStatus(false, 1); }
				return;
			}
#endif
			GAreScreenMessagesEnabled = false;
			const bool HandControl = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_HAND_SHAPES")) == TEXT("1");
			const bool FaceControl = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_FACE_CONTROLS")) == TEXT("1");
			const bool FaceTime = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_FACE_TIME")) == TEXT("1");
			const bool EyeGaze=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_EYE_GAZE"))==TEXT("1");
			const bool EyeLook=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_EYE_LOOK"))==TEXT("1");
			const FString Folder = FPaths::ProjectSavedDir() / (EyeLook ? TEXT("DebugShots/EyeLook285UI") : (EyeGaze ? TEXT("DebugShots/EyeGaze284UI") : (FaceTime ? TEXT("DebugShots/FaceTime283UI") : (FaceControl ? TEXT("DebugShots/FaceControls282UI") : (HandControl ? TEXT("DebugShots/HandShapes275UI") : TEXT("DebugShots/AnimationCatalog274UI"))))));
            const FString CustomCaptureFolder=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_ANIM_OUTPUT_DIR"));
            const FString CaptureFolder=!CustomCaptureFolder.IsEmpty()?CustomCaptureFolder:
                FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_ANIM_CARTESIAN"))==TEXT("1")
                ?FPaths::ProjectSavedDir()/TEXT("DebugShots/Animator290UI"):
                (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_VERIFY_ANIM_BLEND"))==TEXT("1")?FPaths::ProjectSavedDir()/TEXT("DebugShots/Animator289UI"):Folder);
			IFileManager::Get().MakeDirectory(*CaptureFolder, true);
			FScreenshotRequest::RequestScreenshot(CaptureFolder / (FaceControl ? TEXT("gallery_face_controls.png") : (HandControl ? TEXT("gallery_hand_shapes.png") : TEXT("gallery_imported_studio.png"))), true, false);
			UE_LOG(LogTemp, Display, TEXT("KK catalog gallery captured after shader completion"));
			++*Step;
		}
		else FPlatformMisc::RequestExit(false);
	}), 1.f, true);
}

static FAutoConsoleCommand VerifyAnimationCatalogCmd(TEXT("ok.VerifyAnimationCatalog"),
	TEXT("Select an imported Studio row through the gallery and capture actual mouth channel labels"),
	FConsoleCommandDelegate::CreateStatic(&VerifyAnimationCatalogNow));

void UOKCharacterController::RefreshInteractionMotionSources(){
    if(!PreviewActor||!GameSourceCombo)return;
    TGuardValue<bool> Suppress(bSuppressClipCombo,true);GameSourceCombo->ClearOptions();InteractionMotionSources.Reset();
    TArray<FString> Sources=PreviewActor->Session.ClipSources();AnimationCatalog=UKKAnimationCatalog::LoadDefaultCatalog();
    if(AnimationCatalog)for(const auto& Source:AnimationCatalog->GetAnimationSources())Sources.AddUnique(Source);
    for(const auto& Source:Sources)if(GameSourceMatchesSex(Source,Selected.Sex)){
        const FString Label=GameSourceLabel(Source);InteractionMotionSources.Add(Label,Source);GameSourceCombo->AddOption(Label);
    }
    if(!FOkSession::VideoClipNames().IsEmpty()){InteractionMotionSources.Add(TEXT("影片動作"),TEXT("video:"));GameSourceCombo->AddOption(TEXT("影片動作"));}
    const int32 Existing=GameSourceCombo->FindOptionIndex(GameSourceLabel(SelectedGameSource));
    GameSourceCombo->SetSelectedIndex(Existing>=0?Existing:0);
    if(const auto* Source=InteractionMotionSources.Find(GameSourceCombo->GetSelectedOption()))SelectedGameSource=*Source;
}
