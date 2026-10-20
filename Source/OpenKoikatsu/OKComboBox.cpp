#include "OKComboBox.h"
#include "Brushes/SlateColorBrush.h"

UOKComboBoxString::UOKComboBoxString(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	const FLinearColor Background = FLinearColor::FromSRGBColor(FColor(24, 32, 42));
	const FLinearColor Hover = FLinearColor::FromSRGBColor(FColor(46, 65, 85));
	const FLinearColor Selected = FLinearColor::FromSRGBColor(FColor(24, 82, 132));
	const FLinearColor SelectedHover = FLinearColor::FromSRGBColor(FColor(30, 100, 158));
	const FSlateColor Text(FLinearColor::White);

	InitForegroundColor(Text);
	FSlateFontInfo ReadableFont = GetFont();
	ReadableFont.Size = 16;
	InitFont(ReadableFont);
	SetContentPadding(FMargin(10.f, 6.f));

	FComboBoxStyle ComboStyle = GetWidgetStyle();
	FComboButtonStyle& Button = ComboStyle.ComboButtonStyle;
	Button.ButtonStyle.Normal = FSlateColorBrush(Background);
	Button.ButtonStyle.Hovered = FSlateColorBrush(Hover);
	Button.ButtonStyle.Pressed = FSlateColorBrush(Selected);
	Button.ButtonStyle.Disabled = FSlateColorBrush(Background);
	Button.ButtonStyle.NormalForeground = Text;
	Button.ButtonStyle.HoveredForeground = Text;
	Button.ButtonStyle.PressedForeground = Text;
	Button.ButtonStyle.DisabledForeground = FSlateColor(FLinearColor::FromSRGBColor(FColor(170, 180, 192)));
	Button.DownArrowImage.TintColor = Text;
	Button.MenuBorderBrush = FSlateColorBrush(Background);
	Button.MenuBorderPadding = FMargin(2.f);
	SetWidgetStyle(ComboStyle);

	FTableRowStyle Row = GetItemStyle();
	Row.TextColor = Text;
	Row.SelectedTextColor = Text;
	Row.EvenRowBackgroundBrush = FSlateColorBrush(Background);
	Row.OddRowBackgroundBrush = FSlateColorBrush(Background);
	Row.EvenRowBackgroundHoveredBrush = FSlateColorBrush(Hover);
	Row.OddRowBackgroundHoveredBrush = FSlateColorBrush(Hover);
	Row.ActiveBrush = FSlateColorBrush(Selected);
	Row.InactiveBrush = FSlateColorBrush(Selected);
	Row.ActiveHoveredBrush = FSlateColorBrush(SelectedHover);
	Row.InactiveHoveredBrush = FSlateColorBrush(SelectedHover);
	SetItemStyle(Row);
}
