#pragma once

// OK Studio's look, taken from the HS2Studio6 layout spec (Docs/OK_STUDIO_HS2_LAYOUT_SPEC.md,
// Docs/StudioUI/hs2studio6_layout_spec.json): theme colours, font sizes and the shared widget pieces of the shell.
// Sizes and positions are in the spec's canvas units (1600x900 reference, scaled like Unity's CanvasScaler with match 0.5).
#include "CoreMinimal.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Types/ISlateMetaData.h"

namespace OKStudioStyle
{
	inline FLinearColor Hex(const TCHAR* Rgba) { return FLinearColor(FColor::FromHex(Rgba)); }

	// Theme (spec "theme"): panel, button, list rows, selected / on, list area, input, dark settings window, text.
	inline FLinearColor Panel() { return Hex(TEXT("163D7AF9")); }
	inline FLinearColor ButtonFill() { return Hex(TEXT("2860A8F9")); }
	inline FLinearColor RowFill() { return Hex(TEXT("2D5EA5FF")); }
	inline FLinearColor On() { return Hex(TEXT("5BC60CFF")); }
	inline FLinearColor ToolOn() { return Hex(TEXT("00A514FF")); }
	inline FLinearColor ListArea() { return Hex(TEXT("101E30FF")); }
	inline FLinearColor Input() { return Hex(TEXT("4F4F4FFF")); }
	inline FLinearColor DarkWindow() { return Hex(TEXT("1E1E23FF")); }
	inline FLinearColor Text() { return Hex(TEXT("FFFFFFFF")); }
	inline FLinearColor TextSoft() { return Hex(TEXT("E8E0CCFF")); }
	inline FLinearColor TextHint() { return Hex(TEXT("9BA3A8FF")); }
	inline FLinearColor RootIcon() { return Hex(TEXT("F39A1EFF")); }

	/** Canvas units of the reference resolution and the Unity CanvasScaler rule (match width or height 0.5). */
	constexpr float RefWidth = 1600.f, RefHeight = 900.f;
	inline float CanvasScale(const FVector2D& Viewport)
	{
		return Viewport.X > 0. && Viewport.Y > 0. ? float(FMath::Sqrt((Viewport.X / RefWidth) * (Viewport.Y / RefHeight))) : 1.f;
	}

	/** A Unity font size (pixels in canvas units) as a Slate font: Slate sizes are points at 96 DPI. */
	inline FSlateFontInfo Font(float UnityPixels, const ANSICHAR* Face = "Regular") { return FCoreStyle::GetDefaultFontStyle(Face, UnityPixels * 0.75f); }

	inline const FSlateBrush* Brush(const FLinearColor& Color)
	{
		static TMap<uint32, TSharedPtr<FSlateColorBrush>> Cache;
		const uint32 Key = Color.ToFColor(false).ToPackedARGB();
		TSharedPtr<FSlateColorBrush>& B = Cache.FindOrAdd(Key);
		if (!B) B = MakeShared<FSlateColorBrush>(Color);
		return B.Get();
	}

	/** Flat button: fill, a lighter hover and a darker press, no frame. */
	inline const FButtonStyle& ButtonStyle(const FLinearColor& Fill)
	{
		static TMap<uint32, TSharedPtr<FButtonStyle>> Cache;
		const uint32 Key = Fill.ToFColor(false).ToPackedARGB();
		TSharedPtr<FButtonStyle>& S = Cache.FindOrAdd(Key);
		if (!S)
		{
			S = MakeShared<FButtonStyle>(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button"));
			const FLinearColor Hover = FMath::Lerp(Fill, FLinearColor::White, 0.18f);
			const FLinearColor Press = FMath::Lerp(Fill, FLinearColor::Black, 0.25f);
			S->SetNormal(FSlateColorBrush(Fill)).SetHovered(FSlateColorBrush(Hover)).SetPressed(FSlateColorBrush(Press))
				.SetDisabled(FSlateColorBrush(FMath::Lerp(Fill, FLinearColor(0.2f, 0.2f, 0.2f, Fill.A), 0.6f)))
				.SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
		}
		return *S;
	}

	/** Label in the theme's text colour. */
	inline TSharedRef<STextBlock> Label(const TAttribute<FText>& Text, float Size = 11.f, const FLinearColor& Color = OKStudioStyle::Text())
	{
		return SNew(STextBlock).Text(Text).Font(Font(Size)).ColorAndOpacity(Color);
	}

	/** Transparent button over a fill: a light hover and a dark press drawn over the fill. */
	inline const FButtonStyle& FlatButton()
	{
		static FButtonStyle S = []
		{
			FButtonStyle B = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
			B.SetNormal(FSlateColorBrush(FLinearColor::Transparent)).SetHovered(FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.16f)))
				.SetPressed(FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.25f))).SetDisabled(FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.35f)))
				.SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
			return B;
		}();
		return S;
	}

	/** Number box, text box, check box, drop-down and list rows in the theme (dark input fill, blue active, green on). */
	inline const FSpinBoxStyle& SpinStyle()
	{
		static FSpinBoxStyle S = []
		{
			FSpinBoxStyle B = FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox");
			B.SetBackgroundBrush(FSlateColorBrush(Input())).SetHoveredBackgroundBrush(FSlateColorBrush(FMath::Lerp(Input(), FLinearColor::White, 0.12f)))
				.SetActiveFillBrush(FSlateColorBrush(ButtonFill())).SetInactiveFillBrush(FSlateColorBrush(FMath::Lerp(Input(), ButtonFill(), 0.45f)))
				.SetForegroundColor(FSlateColor(Text())).SetTextPadding(FMargin(3.f, 0.f));
			return B;
		}();
		return S;
	}
	inline const FEditableTextBoxStyle& TextBoxStyle()
	{
		static FEditableTextBoxStyle S = []
		{
			FEditableTextBoxStyle B = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
			B.SetBackgroundImageNormal(FSlateColorBrush(Input())).SetBackgroundImageHovered(FSlateColorBrush(FMath::Lerp(Input(), FLinearColor::White, 0.1f)))
				.SetBackgroundImageFocused(FSlateColorBrush(FMath::Lerp(Input(), ButtonFill(), 0.35f))).SetBackgroundImageReadOnly(FSlateColorBrush(FMath::Lerp(Input(), FLinearColor::Black, 0.3f)))
				.SetForegroundColor(FSlateColor(Text())).SetPadding(FMargin(3.f, 1.f));
			B.SetFont(Font(10.f));
			return B;
		}();
		return S;
	}
	inline const FCheckBoxStyle& CheckStyle()
	{
		static FCheckBoxStyle S = []
		{
			FCheckBoxStyle B = FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox");
			const FVector2f Box(13.f, 13.f);
			B.SetUncheckedImage(FSlateRoundedBoxBrush(Input(), 2.f, TextHint(), 1.f, Box)).SetUncheckedHoveredImage(FSlateRoundedBoxBrush(FMath::Lerp(Input(), FLinearColor::White, 0.15f), 2.f, Text(), 1.f, Box))
				.SetUncheckedPressedImage(FSlateRoundedBoxBrush(ButtonFill(), 2.f, Text(), 1.f, Box))
				.SetCheckedImage(FSlateRoundedBoxBrush(On(), 2.f, Text(), 1.f, Box)).SetCheckedHoveredImage(FSlateRoundedBoxBrush(FMath::Lerp(On(), FLinearColor::White, 0.15f), 2.f, Text(), 1.f, Box))
				.SetCheckedPressedImage(FSlateRoundedBoxBrush(ToolOn(), 2.f, Text(), 1.f, Box))
				.SetUndeterminedImage(FSlateRoundedBoxBrush(TextHint(), 2.f, Text(), 1.f, Box)).SetForegroundColor(FSlateColor(Text()));
			return B;
		}();
		return S;
	}
	inline const FTableRowStyle& RowStyle()
	{
		static FTableRowStyle S = []
		{
			FTableRowStyle B = FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
			const FSlateColorBrush Clear(FLinearColor::Transparent), Hover(FLinearColor(1.f, 1.f, 1.f, 0.10f)), Sel(RowFill()), SelHover(FMath::Lerp(RowFill(), FLinearColor::White, 0.15f));
			B.SetEvenRowBackgroundBrush(Clear).SetOddRowBackgroundBrush(Clear).SetEvenRowBackgroundHoveredBrush(Hover).SetOddRowBackgroundHoveredBrush(Hover)
				.SetActiveBrush(Sel).SetActiveHoveredBrush(SelHover).SetInactiveBrush(Sel).SetInactiveHoveredBrush(SelHover)
				.SetSelectorFocusedBrush(Clear).SetTextColor(FSlateColor(Text())).SetSelectedTextColor(FSlateColor(Text()));
			return B;
		}();
		return S;
	}
	inline const FComboBoxStyle& ComboStyle()
	{
		static FComboBoxStyle S = []
		{
			FComboBoxStyle B = FCoreStyle::Get().GetWidgetStyle<FComboBoxStyle>("ComboBox");
			FComboButtonStyle Button = B.ComboButtonStyle;
			FButtonStyle Face = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
			Face.SetNormal(FSlateColorBrush(Input())).SetHovered(FSlateColorBrush(FMath::Lerp(Input(), FLinearColor::White, 0.12f))).SetPressed(FSlateColorBrush(ButtonFill()))
				.SetNormalPadding(FMargin(3.f, 1.f)).SetPressedPadding(FMargin(3.f, 1.f));
			Button.SetButtonStyle(Face).SetMenuBorderBrush(FSlateColorBrush(ListArea())).SetMenuBorderPadding(FMargin(1.f));
			B.SetComboButtonStyle(Button);
			return B;
		}();
		return S;
	}

	/** A fill that turns the "selected / on" colour while bOn holds. */
	inline TSharedRef<SWidget> Fill(TAttribute<bool> bOn, const FLinearColor& Off, const TSharedRef<SWidget>& Content, const FLinearColor& OnColor = On())
	{
		return SNew(SBorder).Padding(0.f).BorderImage_Lambda([bOn, Off, OnColor] { return Brush(bOn.Get() ? OnColor : Off); })[Content];
	}

	/** A themed button of a fixed size; bOn (optional) turns it the "selected / on" green. */
	inline TSharedRef<SWidget> Button(const TAttribute<FText>& Text, TFunction<void()> Do, float Width, float Height, TAttribute<bool> bOn = false,
		float FontSize = 11.f, const FLinearColor& FillColor = ButtonFill(), TAttribute<bool> bEnabled = true, const FLinearColor& OnFill = On())
	{
		return SNew(SBox).WidthOverride(Width > 0.f ? FOptionalSize(Width) : FOptionalSize()).HeightOverride(Height)
		[
			Fill(bOn, FillColor,
				SNew(SButton).ButtonStyle(&FlatButton()).IsEnabled(bEnabled)
				.HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(FMargin(1.f, 0.f))
				.OnClicked_Lambda([Do] { if (Do) Do(); return FReply::Handled(); })
				[Label(Text, FontSize)], OnFill)
		];
	}
	/** Drags its content's window: reports the mouse movement in the widget's own units while the left button is held. */
	class SDragArea : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDragArea) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_ARGUMENT(TFunction<void(const FVector2D&)>, OnDrag)
			SLATE_ARGUMENT(TFunction<void()>, OnDragEnd)
		SLATE_END_ARGS()
		void Construct(const FArguments& Args) { OnDrag = Args._OnDrag; OnDragEnd = Args._OnDragEnd; ChildSlot[Args._Content.Widget]; }
		virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& E) override
		{
			if (!OnDrag || E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override
		{
			if (!HasMouseCapture() || !OnDrag) return FReply::Unhandled();
			const float Scale = G.Scale > 0.f ? G.Scale : 1.f;
			OnDrag(E.GetCursorDelta() / Scale);
			return FReply::Handled();
		}
		virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
		{
			if (!HasMouseCapture()) return FReply::Unhandled();
			if (OnDragEnd) OnDragEnd();
			return FReply::Handled().ReleaseMouseCapture();
		}
	private:
		TFunction<void(const FVector2D&)> OnDrag;
		TFunction<void()> OnDragEnd;
	};

	/** Calls OnFront when any mouse button goes down inside (before its children see it): the window comes to the front. */
	class SFrontArea : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFrontArea) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_ARGUMENT(TFunction<void()>, OnFront)
		SLATE_END_ARGS()
		void Construct(const FArguments& Args) { OnFront = Args._OnFront; ChildSlot[Args._Content.Widget]; }
		virtual FReply OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent&) override { if (OnFront) OnFront(); return FReply::Unhandled(); }
		// A click on the window's empty parts stays in the window (it does not reach the scene view behind).
		virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
	private:
		TFunction<void()> OnFront;
	};

	/** Line icons (16x16 design units), drawn in the text colour: the studio has no picture assets of its own. */
	class SIcon : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SIcon) : _Size(16.f), _Color(Text()) {}
			SLATE_ARGUMENT(FName, Icon)
			SLATE_ARGUMENT(float, Size)
			SLATE_ATTRIBUTE(FLinearColor, Color)
		SLATE_END_ARGS()
		void Construct(const FArguments& Args) { Icon = Args._Icon; Size = Args._Size; Color = Args._Color; }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
		{
			const FVector2D Local = G.GetLocalSize();
			const float K = FMath::Min(Local.X, Local.Y) / 16.f;
			const FVector2f O = FVector2f((Local.X - 16.f * K) * 0.5f, (Local.Y - 16.f * K) * 0.5f);
			const FLinearColor C = Color.Get();
			auto P = [&](float X, float Y) { return O + FVector2f(X, Y) * K; };
			auto Line = [&](std::initializer_list<FVector2f> Pts) { TArray<FVector2f> A; for (const FVector2f& V : Pts) A.Add(P(V.X, V.Y)); FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), A, ESlateDrawEffect::None, C, true, FMath::Max(1.2f, 1.5f * K)); };
			auto Circle = [&](float X, float Y, float R, float From = 0.f, float To = 360.f)
			{
				TArray<FVector2f> A;
				for (int32 I = 0; I <= 20; ++I) { const float T = FMath::DegreesToRadians(From + (To - From) * I / 20.f); A.Add(P(X + R * FMath::Cos(T), Y + R * FMath::Sin(T))); }
				FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), A, ESlateDrawEffect::None, C, true, FMath::Max(1.2f, 1.5f * K));
			};
			const FString N = Icon.ToString();
			if (N == TEXT("add")) { Line({ {8, 2}, {8, 14} }); Line({ {2, 8}, {14, 8} }); }
			else if (N == TEXT("chara")) { Circle(8, 4.5f, 2.5f); Line({ {8, 7}, {8, 11.5f} }); Line({ {4, 9}, {12, 9} }); Line({ {5, 15}, {8, 11.5f}, {11, 15} }); }
			else if (N == TEXT("sound")) { Circle(5.5f, 12, 2.2f); Line({ {7.7f, 12}, {7.7f, 2.5f}, {13, 4.5f}, {13, 7}, {7.7f, 5} }); }
			else if (N == TEXT("system")) { Circle(8, 8, 3.f); for (int32 I = 0; I < 8; ++I) { const float T = I * UE_PI / 4.f; Line({ {8 + 4.5f * FMath::Cos(T), 8 + 4.5f * FMath::Sin(T)}, {8 + 6.5f * FMath::Cos(T), 8 + 6.5f * FMath::Sin(T)} }); } }
			else if (N == TEXT("tree")) { Line({ {2, 3}, {9, 3} }); Line({ {4, 3}, {4, 13}, {7, 13} }); Line({ {4, 8}, {7, 8} }); Line({ {9, 8}, {14, 8} }); Line({ {9, 13}, {14, 13} }); }
			else if (N == TEXT("move")) { Line({ {8, 1}, {8, 15} }); Line({ {1, 8}, {15, 8} }); Line({ {6, 3}, {8, 1}, {10, 3} }); Line({ {6, 13}, {8, 15}, {10, 13} }); Line({ {3, 6}, {1, 8}, {3, 10} }); Line({ {13, 6}, {15, 8}, {13, 10} }); }
			else if (N == TEXT("rotate")) { Circle(8, 8, 5.5f, 40.f, 330.f); Line({ {13.5f, 2.5f}, {13.2f, 6.6f}, {9.5f, 5.5f} }); }
			else if (N == TEXT("scale")) { Line({ {2, 6}, {2, 14}, {10, 14}, {10, 6}, {2, 6} }); Line({ {7, 9}, {14, 2} }); Line({ {10, 2}, {14, 2}, {14, 6} }); }
			else if (N == TEXT("color")) { Circle(5.5f, 6, 2.6f); Circle(10.5f, 6, 2.6f); Circle(8, 10.5f, 2.6f); }
			else if (N == TEXT("options")) { Line({ {2, 4}, {14, 4} }); Line({ {2, 8}, {14, 8} }); Line({ {2, 12}, {14, 12} }); Circle(5, 4, 1.5f); Circle(11, 8, 1.5f); Circle(7, 12, 1.5f); }
			else if (N == TEXT("camera")) { Line({ {1.5f, 5}, {14.5f, 5}, {14.5f, 13}, {1.5f, 13}, {1.5f, 5} }); Line({ {5, 5}, {6, 3}, {10, 3}, {11, 5} }); Circle(8, 9, 2.5f); }
			else if (N == TEXT("groups")) { Circle(5.5f, 5, 2.f); Circle(11, 5, 2.f); Line({ {2, 13}, {3, 9}, {8, 9}, {9, 13} }); Line({ {9, 9}, {13.5f, 9}, {14.5f, 13} }); }
			else if (N == TEXT("menu")) { Line({ {2, 4}, {14, 4} }); Line({ {2, 8}, {14, 8} }); Line({ {2, 12}, {14, 12} }); }
			else if (N == TEXT("folder")) { Line({ {1.5f, 4}, {6, 4}, {7.5f, 5.5f}, {14.5f, 5.5f}, {14.5f, 13}, {1.5f, 13}, {1.5f, 4} }); }
			else if (N == TEXT("light")) { Circle(8, 6.5f, 4.f, 140.f, 400.f); Line({ {5.5f, 9.5f}, {6, 12}, {10, 12}, {10.5f, 9.5f} }); Line({ {6.3f, 14}, {9.7f, 14} }); }
			else if (N == TEXT("effect")) { Line({ {8, 1.5f}, {9.6f, 6.2f}, {14.5f, 6.4f}, {10.6f, 9.4f}, {12, 14.3f}, {8, 11.4f}, {4, 14.3f}, {5.4f, 9.4f}, {1.5f, 6.4f}, {6.4f, 6.2f}, {8, 1.5f} }); }
			else if (N == TEXT("map")) { Line({ {1, 14}, {6, 5}, {9, 10}, {11, 7}, {15, 14}, {1, 14} }); Circle(12, 3.5f, 1.5f); }
			else if (N == TEXT("eye")) { Line({ {1, 8}, {4, 4.5f}, {8, 3.5f}, {12, 4.5f}, {15, 8}, {12, 11.5f}, {8, 12.5f}, {4, 11.5f}, {1, 8} }); Circle(8, 8, 2.2f); }
			else if (N == TEXT("copy")) { Line({ {5, 5}, {14, 5}, {14, 14}, {5, 14}, {5, 5} }); Line({ {2, 11}, {2, 2}, {11, 2} }); }
			else if (N == TEXT("delete")) { Line({ {2, 4}, {14, 4} }); Line({ {6, 4}, {6, 2}, {10, 2}, {10, 4} }); Line({ {3.5f, 4}, {4.5f, 14.5f}, {11.5f, 14.5f}, {12.5f, 4} }); Line({ {8, 7}, {8, 12} }); }
			else if (N == TEXT("root")) { Line({ {8, 14}, {8, 3} }); Line({ {4, 7}, {8, 3}, {12, 7} }); Line({ {2, 14}, {14, 14} }); }
			else if (N == TEXT("timeline")) { Line({ {1, 12}, {15, 12} }); Line({ {5, 2}, {5, 15} }); Line({ {9, 9}, {10.5f, 7.5f}, {12, 9}, {10.5f, 10.5f}, {9, 9} }); Line({ {1, 5}, {15, 5} }); }
			else if (N == TEXT("search")) { Circle(6.5f, 6.5f, 4.5f); Line({ {10, 10}, {15, 15} }); }
			else if (N == TEXT("pose")) { Circle(9, 2.8f, 1.8f); Line({ {9, 4.6f}, {7.5f, 9}, {4, 15} }); Line({ {7.5f, 9}, {11, 15} }); Line({ {3, 6}, {8.5f, 6.5f}, {13, 3} }); }
			else if (N == TEXT("save")) { Line({ {2, 2}, {12, 2}, {14, 4}, {14, 14}, {2, 14}, {2, 2} }); Line({ {5, 2}, {5, 6}, {11, 6}, {11, 2} }); Line({ {5, 14}, {5, 10}, {11, 10}, {11, 14} }); }
			else if (N == TEXT("load")) { Line({ {2, 10}, {2, 14}, {14, 14}, {14, 10} }); Line({ {8, 2}, {8, 11} }); Line({ {4.5f, 7.5f}, {8, 11}, {11.5f, 7.5f} }); }
			else if (N == TEXT("key")) { Line({ {8, 2}, {14, 8}, {8, 14}, {2, 8}, {8, 2} }); }
			else if (N == TEXT("play")) { Line({ {4, 2}, {13, 8}, {4, 14}, {4, 2} }); }
			else if (N == TEXT("pause")) { Line({ {5, 2}, {5, 14} }); Line({ {11, 2}, {11, 14} }); }
			else if (N == TEXT("prev")) { Line({ {11, 3}, {5, 8}, {11, 13} }); }
			else if (N == TEXT("next")) { Line({ {5, 3}, {11, 8}, {5, 13} }); }
			else if (N == TEXT("first")) { Line({ {12, 3}, {6, 8}, {12, 13} }); Line({ {4, 3}, {4, 13} }); }
			else if (N == TEXT("last")) { Line({ {4, 3}, {10, 8}, {4, 13} }); Line({ {12, 3}, {12, 13} }); }
			else if (N == TEXT("star")) { Line({ {8, 1.5f}, {9.6f, 6.2f}, {14.5f, 6.4f}, {10.6f, 9.4f}, {12, 14.3f}, {8, 11.4f}, {4, 14.3f}, {5.4f, 9.4f}, {1.5f, 6.4f}, {6.4f, 6.2f}, {8, 1.5f} }); }
			else if (N == TEXT("close")) { Line({ {3, 3}, {13, 13} }); Line({ {13, 3}, {3, 13} }); }
			else if (N == TEXT("undo")) { Circle(8.5f, 9, 5.f, 180.f, 400.f); Line({ {1, 6}, {3.5f, 9}, {6, 6} }); }
			else if (N == TEXT("redo")) { Circle(7.5f, 9, 5.f, 140.f, 360.f); Line({ {10, 6}, {12.5f, 9}, {15, 6} }); }
			else if (N == TEXT("bone")) { Circle(3.5f, 3.5f, 2.f); Circle(12.5f, 12.5f, 2.f); Line({ {5, 5}, {11, 11} }); }
			else if (N == TEXT("frame")) { Line({ {1.5f, 2.5f}, {14.5f, 2.5f}, {14.5f, 13.5f}, {1.5f, 13.5f}, {1.5f, 2.5f} }); Line({ {4, 5}, {12, 5}, {12, 11}, {4, 11}, {4, 5} }); }
			else if (N == TEXT("mirror")) { Line({ {8, 1}, {8, 15} }); Line({ {6, 4}, {2, 8}, {6, 12} }); Line({ {10, 4}, {14, 8}, {10, 12} }); }
			else Circle(8, 8, 5.f);
			return Layer + 1;
		}
	private:
		FName Icon;
		float Size = 16.f;
		TAttribute<FLinearColor> Color;
	};

	/** A themed icon button with a tooltip. */
	inline TSharedRef<SWidget> IconButton(FName Icon, const FString& Tip, TFunction<void()> Do, float Width, float Height, TAttribute<bool> bOn = false,
		const FLinearColor& FillColor = ButtonFill(), const FLinearColor& OnFill = On(), TAttribute<bool> bEnabled = true)
	{
		return SNew(SBox).WidthOverride(Width).HeightOverride(Height).ToolTipText(FText::FromString(Tip))
		[
			Fill(bOn, FillColor,
				// The tooltip is also the button's "tip:" tag: the control channel finds icon buttons by it without running
				// tooltip lambdas (the box keeps its own tag from SOKStudioPanel::Tagged).
				SNew(SButton).ButtonStyle(&FlatButton()).IsEnabled(bEnabled).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(FMargin(0.f))
				.AddMetaData<FTagMetaData>(FName(TEXT("tip:") + Tip))
				.OnClicked_Lambda([Do] { if (Do) Do(); return FReply::Handled(); })
				[SNew(SIcon).Icon(Icon).Size(FMath::Min(Width, Height) * 0.62f)], OnFill)
		];
	}

	/** Window frame: panel fill, a title row (12px; dragging it moves the window) and content. */
	inline TSharedRef<SWidget> Window(const TAttribute<FText>& Title, const TSharedRef<SWidget>& Content, TFunction<void()> Close = nullptr, const FLinearColor& Fill = Panel(),
		TFunction<void(const FVector2D&)> OnDrag = nullptr, TFunction<void()> OnDragEnd = nullptr)
	{
		TSharedRef<SHorizontalBox> TitleRow = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(6.f, 0.f)[Label(Title, 12.f)];
		if (Close)
			TitleRow->AddSlot().AutoWidth().Padding(2.f)[IconButton(TEXT("close"), TEXT("關閉"), Close, 18.f, 18.f)];
		return SNew(SBorder).BorderImage(Brush(Fill)).Padding(0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(23.f)[SNew(SDragArea).OnDrag(OnDrag).OnDragEnd(OnDragEnd)[TitleRow]]]
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(3.f, 0.f, 3.f, 3.f)[Content]
		];
	}
}
