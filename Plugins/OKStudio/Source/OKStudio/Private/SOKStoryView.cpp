#include "SOKStoryView.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	// The look of the story screen (layout fractions and timing come from the director).
	const FLinearColor BoxColor(0.020f, 0.030f, 0.060f, 1.f);
	const FLinearColor LineColor(1.f, 1.f, 1.f, 0.22f);
	const FLinearColor TextColor(0.96f, 0.96f, 0.98f, 1.f);
	const FLinearColor DimText(0.70f, 0.72f, 0.78f, 1.f);
	const FLinearColor Accent(1.f, 0.62f, 0.78f, 1.f);
	const FLinearColor ButtonColor(0.10f, 0.13f, 0.22f, 0.92f);
	const FLinearColor ButtonDisabled(0.10f, 0.10f, 0.12f, 0.75f);

	FLinearColor OKStoryHexColor(const FString& Hex, const FLinearColor& Fallback)
	{
		if (Hex.Len() < 7 || Hex[0] != TEXT('#')) return Fallback;
		return FLinearColor(FColor::FromHex(Hex));
	}

	FSlateFontInfo OKStoryFont(float Pixels, bool bBold = false)
	{
		// Slate font sizes are points at 96 DPI.
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", FMath::Max(6, FMath::RoundToInt(Pixels * 0.75f)));
	}

	FVector2D OKStoryMeasure(const FString& Text, const FSlateFontInfo& F)
	{
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, F);
	}

	/** Lines of `Text` no wider than `Width` (latin words kept whole where they fit, any other script by character). */
	TArray<FString> OKStoryWrap(const FString& Text, const FSlateFontInfo& F, float Width)
	{
		TArray<FString> Lines;
		FString Paragraphs = Text;
		Paragraphs.ReplaceInline(TEXT("\r"), TEXT(""));
		TArray<FString> Parts;
		Paragraphs.ParseIntoArray(Parts, TEXT("\n"), false);
		for (const FString& Part : Parts)
		{
			FString Line;
			int32 LastSpace = -1;
			for (int32 I = 0; I < Part.Len(); ++I)
			{
				const FString Next = Line + FString::Chr(Part[I]);
				if (Line.Len() && OKStoryMeasure(Next, F).X > Width)
				{
					if (LastSpace > 0 && !FChar::IsWhitespace(Part[I]))
					{
						Lines.Add(Line.Left(LastSpace));
						Line = Line.Mid(LastSpace + 1) + FString::Chr(Part[I]);
					}
					else
					{
						Lines.Add(Line);
						Line = FChar::IsWhitespace(Part[I]) ? FString() : FString::Chr(Part[I]);
					}
					LastSpace = -1;
					continue;
				}
				if (FChar::IsWhitespace(Part[I])) LastSpace = Line.Len();
				Line = Next;
			}
			Lines.Add(Line);
		}
		return Lines;
	}

	FString OKStoryStr(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		FString S;
		if (O) O->TryGetStringField(Field, S);
		return S;
	}

	double OKStoryNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, double Default = 0.0)
	{
		double D = Default;
		if (O) O->TryGetNumberField(Field, D);
		return D;
	}

	bool OKStoryFlag(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		bool B = false;
		if (O) O->TryGetBoolField(Field, B);
		return B;
	}

	TSharedPtr<FJsonObject> OKStoryObj(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return O && O->TryGetObjectField(Field, Out) ? *Out : nullptr;
	}

	TArray<TSharedPtr<FJsonValue>> OKStoryArr(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
		return O && O->TryGetArrayField(Field, Out) ? *Out : TArray<TSharedPtr<FJsonValue>>();
	}

	FString OKStoryQuote(const FString& S)
	{
		return TEXT("\"") + S.ReplaceCharWithEscapedChar() + TEXT("\"");
	}
}

void SOKStoryView::Construct(const FArguments& InArgs)
{
	Source = InArgs._Source;
	SetCanTick(false);
}

void SOKStoryView::Send(const FString& Json) const
{
	if (Source.Input) Source.Input(Json);
}

const FSlateBrush* SOKStoryView::BrushFor(UObject* Resource, const FVector2D& Size) const
{
	FSlateBrush& B = Brushes.FindOrAdd(Resource);
	B.SetResourceObject(Resource);
	B.ImageSize = Size;
	B.DrawAs = ESlateBrushDrawType::Image;
	return &B;
}

int32 SOKStoryView::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	Hits.Reset();
	bPanelOpen = false;
	const TSharedPtr<FJsonObject> Frame = Source.Frame ? Source.Frame() : nullptr;
	if (!Frame) return Layer;
	const FVector2D Size = Geo.GetLocalSize();
	const double W = Size.X, H = Size.Y;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const double Now = FPlatformTime::Seconds();
	auto Box = [&](int32 L, FVector2D Pos, FVector2D Extent, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(Out, L, Geo.ToPaintGeometry(FVector2f(Extent), FSlateLayoutTransform(FVector2f(Pos))), White, ESlateDrawEffect::None, Color);
	};
	auto Text = [&](int32 L, const FString& S, FVector2D Pos, float Pixels, const FLinearColor& Color, bool bBold = false)
	{
		const FSlateFontInfo F = OKStoryFont(Pixels, bBold);
		// A soft shadow keeps text readable over bright scenes.
		FSlateDrawElement::MakeText(Out, L, Geo.ToPaintGeometry(FVector2f(float(W), Pixels * 1.6f), FSlateLayoutTransform(FVector2f(Pos + FVector2D(1.5, 1.5)))), S, F, ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.6f * Color.A));
		FSlateDrawElement::MakeText(Out, L + 1, Geo.ToPaintGeometry(FVector2f(float(W), Pixels * 1.6f), FSlateLayoutTransform(FVector2f(Pos))), S, F, ESlateDrawEffect::None, Color);
	};
	auto Button = [&](int32 L, FVector2D Pos, FVector2D Extent, const FString& Label, const FString& Event, bool bEnabled, bool bOn, float Pixels)
	{
		Box(L, Pos, Extent, bEnabled ? (bOn ? FLinearColor(0.42f, 0.22f, 0.34f, 0.95f) : ButtonColor) : ButtonDisabled);
		Box(L + 1, Pos + FVector2D(0, Extent.Y - 2), FVector2D(Extent.X, 2), bOn ? Accent : LineColor);
		const FVector2D T = OKStoryMeasure(Label, OKStoryFont(Pixels));
		Text(L + 2, Label, Pos + (Extent - T) * 0.5, Pixels, bEnabled ? TextColor : DimText);
		if (bEnabled && !Event.IsEmpty()) Hits.Add({ Pos, Pos + Extent, Event });
	};
	const FString Screen = OKStoryStr(Frame, TEXT("screen"));
	const bool bHide = OKStoryFlag(Frame, TEXT("hide_ui"));
	const TSharedPtr<FJsonObject> LayoutObj = OKStoryObj(Frame, TEXT("layout"));
	const double DialogH = OKStoryNum(LayoutObj, TEXT("dialog_height"), 0.26) * H;
	const double TextLeft = OKStoryNum(LayoutObj, TEXT("text_left"), 0.275) * W;
	const TSharedPtr<FJsonObject> Dialog = OKStoryObj(Frame, TEXT("dialog"));
	const float Opacity = float(OKStoryNum(Dialog, TEXT("opacity"), OKStoryNum(LayoutObj, TEXT("dialog_opacity"), 0.86)));
	const double BoxTop = H - DialogH;
	int32 L = Layer;

	// The dialog box under the portrait.
	const bool bDialog = OKStoryFlag(Dialog, TEXT("visible")) && !bHide;
	if (bDialog)
	{
		Box(L, FVector2D(0, BoxTop), FVector2D(W, DialogH), FLinearColor(BoxColor.R, BoxColor.G, BoxColor.B, Opacity));
		Box(L + 1, FVector2D(0, BoxTop), FVector2D(W, 2), LineColor);
	}
	L += 2;

	// The live portrait, fading into the dialog box at its foot.
	const TSharedPtr<FJsonObject> Portrait = OKStoryObj(Frame, TEXT("portrait"));
	if (Portrait && OKStoryFlag(Portrait, TEXT("visible")) && !bHide)
	{
		const TArray<TSharedPtr<FJsonValue>> R = OKStoryArr(Portrait, TEXT("rect"));
		if (R.Num() == 4)
		{
			const FVector2D Pos(R[0]->AsNumber() * W, R[1]->AsNumber() * H), Ext(R[2]->AsNumber() * W, R[3]->AsNumber() * H);
			const float Alpha = float(OKStoryNum(Portrait, TEXT("alpha"), 1.0));
			// Kept left of the text column: long twin tails swinging out of the picture lay behind the name and the line.
			const double ClipRight = FMath::Max(Pos.X + 1.0, TextLeft - W * 0.01);
			Out.PushClip(FSlateClippingZone(Geo.GetLayoutBoundingRect(FSlateRect(Pos.X, Pos.Y, ClipRight, Pos.Y + Ext.Y))));
			if (UMaterialInstanceDynamic* Mid = Source.Portrait ? Source.Portrait() : nullptr)
				FSlateDrawElement::MakeBox(Out, L, Geo.ToPaintGeometry(FVector2f(Ext), FSlateLayoutTransform(FVector2f(Pos))), BrushFor(Mid, Ext), ESlateDrawEffect::None, FLinearColor::White);
			Out.PopClip();
			const double From = OKStoryNum(Portrait, TEXT("fade_from"), 0.5);
			const FVector2D GPos(Pos.X, Pos.Y + Ext.Y * From), GExt(Ext.X, Ext.Y * (1.0 - From));
			if (bDialog && GExt.Y > 1)
			{
				// Only the part over the dialog box gets the box's colour; above it the portrait stays clear.
				const double Start = FMath::Max(GPos.Y, BoxTop + DialogH * 0.3);
				const FVector2D S(GPos.X, Start), E(GExt.X, Pos.Y + Ext.Y - Start);
				TArray<FSlateGradientStop> Stops;
				Stops.Add(FSlateGradientStop(FVector2D(0, 0), FLinearColor(BoxColor.R, BoxColor.G, BoxColor.B, 0.f)));
				Stops.Add(FSlateGradientStop(FVector2D(0, E.Y), FLinearColor(BoxColor.R, BoxColor.G, BoxColor.B, Opacity * 0.75f * Alpha)));
				FSlateDrawElement::MakeGradient(Out, L + 1, Geo.ToPaintGeometry(FVector2f(E), FSlateLayoutTransform(FVector2f(S))), Stops, Orient_Horizontal);
			}
		}
	}
	L += 2;

	// Name plate and text.
	if (bDialog)
	{
		const FString Name = OKStoryStr(Dialog, TEXT("name"));
		const double NamePx = DialogH * 0.15, TextPx = DialogH * 0.135;
		double Y = BoxTop + DialogH * 0.10;
		if (!Name.IsEmpty())
		{
			const FLinearColor C = OKStoryHexColor(OKStoryStr(Dialog, TEXT("name_color")), Accent);
			Text(L, Name, FVector2D(TextLeft, Y), float(NamePx), C, true);
			const double NameW = OKStoryMeasure(Name, OKStoryFont(NamePx, true)).X;
			Box(L, FVector2D(TextLeft, Y + NamePx * 1.35), FVector2D(FMath::Max(NameW + NamePx, W * 0.12), 2), C * FLinearColor(1, 1, 1, 0.8f));
		}
		Y += NamePx * 1.75;
		const FSlateFontInfo F = OKStoryFont(TextPx);
		const TArray<FString> Lines = OKStoryWrap(OKStoryStr(Dialog, TEXT("text_visible")), F, float(W * 0.95 - TextLeft));
		double LastX = TextLeft, LastY = Y;
		for (const FString& Line : Lines)
		{
			Text(L, Line, FVector2D(TextLeft, Y), float(TextPx), TextColor);
			LastX = TextLeft + OKStoryMeasure(Line, F).X;
			LastY = Y;
			Y += TextPx * 1.5;
		}
		const FString Waiting = OKStoryStr(Dialog, TEXT("waiting"));
		if (Waiting == TEXT("click") || Waiting == TEXT("auto") || Waiting == TEXT("ended"))
		{
			const float Bob = float(FMath::Sin(Now * 5.0) * TextPx * 0.12);
			Text(L, TEXT("▼"), FVector2D(LastX + TextPx * 0.4, LastY + Bob), float(TextPx * 0.7), Waiting == TEXT("auto") ? Accent : TextColor);
		}
	}
	L += 3;

	// Quick buttons along the bottom of the dialog box.
	if (bDialog && Screen != TEXT("ended"))
	{
		const double Px = FMath::Max(12.0, DialogH * 0.075), BH = Px * 1.7;
		double X = W - Px;
		const TArray<TSharedPtr<FJsonValue>> Buttons = OKStoryArr(Frame, TEXT("buttons"));
		for (int32 I = Buttons.Num() - 1; I >= 0; --I)
		{
			const TSharedPtr<FJsonObject> B = Buttons[I]->AsObject();
			const FString Label = OKStoryStr(B, TEXT("label"));
			const double BW = OKStoryMeasure(Label, OKStoryFont(Px)).X + Px * 1.4;
			X -= BW;
			Button(L, FVector2D(X, H - BH - Px * 0.5), FVector2D(BW, BH), Label, FString::Printf(TEXT("{\"type\":\"button\",\"id\":%s}"), *OKStoryQuote(OKStoryStr(B, TEXT("id")))), true, OKStoryFlag(B, TEXT("on")), float(Px));
			X -= Px * 0.4;
		}
	}
	L += 3;

	// Choices above the dialog box.
	const TArray<TSharedPtr<FJsonValue>> Choices = OKStoryArr(Frame, TEXT("choices"));
	if (Choices.Num() && !bHide)
	{
		const double CH = H * 0.062, Gap = H * 0.016, CW = W * 0.44, Px = CH * 0.36;
		double Y = BoxTop - H * 0.04 - Choices.Num() * (CH + Gap);
		for (int32 I = 0; I < Choices.Num(); ++I)
		{
			const TSharedPtr<FJsonObject> C = Choices[I]->AsObject();
			const bool bEnabled = OKStoryFlag(C, TEXT("enabled"));
			Button(L, FVector2D((W - CW) * 0.5, Y), FVector2D(CW, CH), OKStoryStr(C, TEXT("text")), FString::Printf(TEXT("{\"type\":\"choose\",\"index\":%d}"), I), bEnabled, false, float(Px));
			const FString Hint = OKStoryStr(C, TEXT("hint"));
			if (!Hint.IsEmpty()) Text(L + 3, Hint, FVector2D((W - CW) * 0.5 + Px, Y + CH - Px * 0.2), float(Px * 0.6), FLinearColor(1.f, 0.6f, 0.6f, 1.f));
			Y += CH + Gap;
		}
	}
	L += 4;

	// The act title sliding in from the left.
	const TSharedPtr<FJsonObject> Title = OKStoryObj(Frame, TEXT("title"));
	if (Title)
	{
		const float A = float(OKStoryNum(Title, TEXT("alpha"), 1.0));
		const double X = OKStoryNum(Title, TEXT("x")) * W, Px = H * 0.06;
		const FString S = OKStoryStr(Title, TEXT("text"));
		const double TW = OKStoryMeasure(S, OKStoryFont(Px, true)).X + Px * 2.0;
		Box(L, FVector2D(X, H * 0.36), FVector2D(TW + W * 0.06, Px * 1.9), FLinearColor(0.f, 0.f, 0.f, 0.55f * A));
		Box(L + 1, FVector2D(X, H * 0.36 + Px * 1.9 - 3), FVector2D(TW + W * 0.06, 3), FLinearColor(Accent.R, Accent.G, Accent.B, A));
		Text(L + 2, S, FVector2D(X + W * 0.06, H * 0.36 + Px * 0.35), float(Px), FLinearColor(1, 1, 1, A), true);
	}
	L += 4;

	// Loading, toast, free camera hint.
	if (OKStoryFlag(Frame, TEXT("loading")))
	{
		const int32 Dots = int32(Now * 3.0) % 4;
		Text(L, FString::ChrN(Dots + 1, TEXT('●')), FVector2D(W - H * 0.12, H - H * 0.06), float(H * 0.02), DimText);
	}
	const FString Toast = OKStoryStr(Frame, TEXT("toast"));
	if (!Toast.IsEmpty())
	{
		const double Px = H * 0.024, TW = OKStoryMeasure(Toast, OKStoryFont(Px)).X + Px * 2;
		Box(L, FVector2D((W - TW) * 0.5, H * 0.04), FVector2D(TW, Px * 2.2), FLinearColor(0, 0, 0, 0.7f));
		Text(L + 1, Toast, FVector2D((W - TW) * 0.5 + Px, H * 0.04 + Px * 0.5), float(Px), TextColor);
	}
	const FString Edit = OKStoryStr(Frame, TEXT("camera_edit"));
	if (!Edit.IsEmpty())
	{
		const double Px = H * 0.022;
		Box(L, FVector2D(Px, Px), FVector2D(OKStoryMeasure(Edit, OKStoryFont(Px)).X + Px * 2, Px * 2.2), FLinearColor(0, 0, 0, 0.7f));
		Text(L + 1, Edit, FVector2D(Px * 2, Px * 1.5), float(Px), FLinearColor(1.f, 0.9f, 0.4f, 1.f));
		const double C = H * 0.02;
		Box(L, FVector2D(W * 0.5 - C, H * 0.5 - 1), FVector2D(C * 2, 2), FLinearColor::White);
		Box(L, FVector2D(W * 0.5 - 1, H * 0.5 - C), FVector2D(2, C * 2), FLinearColor::White);
	}
	L += 3;

	// Panels.
	const TSharedPtr<FJsonObject> Panel = OKStoryObj(Frame, TEXT("panel"));
	if (Panel && Screen != TEXT("play") && Screen != TEXT("ended"))
	{
		Box(L, FVector2D(0, 0), Size, FLinearColor(0, 0, 0, 0.55f));
		PanelMin = FVector2D(W * 0.14, H * 0.10);
		PanelMax = FVector2D(W * 0.86, H * 0.90);
		bPanelOpen = true;
		const FVector2D PS = PanelMax - PanelMin;
		Box(L + 1, PanelMin, PS, FLinearColor(0.045f, 0.06f, 0.10f, 0.97f));
		Box(L + 2, PanelMin, FVector2D(PS.X, 3), Accent);
		const double Px = H * 0.026;
		L += 3;
		Text(L, OKStoryStr(Panel, TEXT("title")), PanelMin + FVector2D(Px, Px), float(Px * 1.3), TextColor, true);
		const FString Close = OKStoryStr(Panel, TEXT("close"));
		if (!Close.IsEmpty())
		{
			const double BW = OKStoryMeasure(Close, OKStoryFont(Px)).X + Px * 2;
			Button(L, FVector2D(PanelMax.X - BW - Px, PanelMin.Y + Px * 0.8), FVector2D(BW, Px * 1.9), Close, TEXT("{\"type\":\"button\",\"id\":\"close\"}"), true, false, float(Px));
		}
		const FVector2D Body = PanelMin + FVector2D(Px, Px * 4.0);
		const FVector2D BodySize = PanelMax - FVector2D(Px, Px) - Body;
		if (Screen == TEXT("menu"))
		{
			const TArray<TSharedPtr<FJsonValue>> Items = OKStoryArr(Panel, TEXT("items"));
			const double BH = Px * 2.4, BW = BodySize.X * 0.4;
			for (int32 I = 0; I < Items.Num(); ++I)
			{
				const TSharedPtr<FJsonObject> It = Items[I]->AsObject();
				Button(L, FVector2D(Body.X + (BodySize.X - BW) * 0.5, Body.Y + I * (BH + Px * 0.6)), FVector2D(BW, BH), OKStoryStr(It, TEXT("label")),
					FString::Printf(TEXT("{\"type\":\"button\",\"id\":%s}"), *OKStoryQuote(OKStoryStr(It, TEXT("id")))), true, false, float(Px));
			}
		}
		else if (Screen == TEXT("save") || Screen == TEXT("load"))
		{
			const TArray<TSharedPtr<FJsonValue>> Slots = OKStoryArr(Panel, TEXT("slots"));
			const int32 Cols = 3;
			const double Gap = Px * 0.8, SW = (BodySize.X - Gap * (Cols - 1)) / Cols, SH = (BodySize.Y - Px * 3.0 - Gap) / 2.0;
			for (int32 I = 0; I < Slots.Num(); ++I)
			{
				const TSharedPtr<FJsonObject> S = Slots[I]->AsObject();
				const FVector2D Pos = Body + FVector2D((I % Cols) * (SW + Gap), (I / Cols) * (SH + Gap));
				const bool bEmpty = OKStoryFlag(S, TEXT("empty"));
				const bool bUsable = Screen == TEXT("save") ? OKStoryStr(S, TEXT("id")) != TEXT("auto") : !bEmpty;
				Box(L, Pos, FVector2D(SW, SH), bUsable ? ButtonColor : ButtonDisabled);
				const double ThumbH = FMath::Min(SH * 0.62, SW * 9.0 / 16.0), ThumbW = ThumbH * 16.0 / 9.0;
				const FVector2D TPos = Pos + FVector2D((SW - ThumbW) * 0.5, Px * 0.4);
				if (UTexture2D* T = Source.Thumbnail ? Source.Thumbnail(OKStoryStr(S, TEXT("thumb"))) : nullptr)
					FSlateDrawElement::MakeBox(Out, L + 1, Geo.ToPaintGeometry(FVector2f(float(ThumbW), float(ThumbH)), FSlateLayoutTransform(FVector2f(TPos))), BrushFor(T, FVector2D(ThumbW, ThumbH)), ESlateDrawEffect::None, FLinearColor::White);
				else Box(L + 1, TPos, FVector2D(ThumbW, ThumbH), FLinearColor(0, 0, 0, 0.5f));
				double Y = TPos.Y + ThumbH + Px * 0.3;
				Text(L + 2, OKStoryStr(S, TEXT("label")) + TEXT("  ") + OKStoryStr(S, TEXT("when")), FVector2D(Pos.X + Px * 0.5, Y), float(Px * 0.75), Accent);
				Y += Px * 1.1;
				const FString Summary = (OKStoryStr(S, TEXT("name")).IsEmpty() ? FString() : OKStoryStr(S, TEXT("name")) + TEXT("：")) + OKStoryStr(S, TEXT("summary"));
				const TArray<FString> Lines = OKStoryWrap(Summary, OKStoryFont(Px * 0.7), float(SW - Px));
				for (int32 J = 0; J < Lines.Num() && Y < Pos.Y + SH - Px * 0.8; ++J, Y += Px * 1.0) Text(L + 2, Lines[J], FVector2D(Pos.X + Px * 0.5, Y), float(Px * 0.7), TextColor);
				if (bUsable) Hits.Add({ Pos, Pos + FVector2D(SW, SH), FString::Printf(TEXT("{\"type\":\"button\",\"id\":\"slot:%s\"}"), *OKStoryStr(S, TEXT("id"))) });
			}
			const int32 Page = int32(OKStoryNum(Panel, TEXT("page"))), Pages = int32(OKStoryNum(Panel, TEXT("pages"), 1));
			const FVector2D Foot(Body.X + BodySize.X * 0.5, PanelMax.Y - Px * 2.6);
			Button(L, Foot - FVector2D(Px * 6, 0), FVector2D(Px * 2.4, Px * 1.9), TEXT("‹"), FString::Printf(TEXT("{\"type\":\"button\",\"id\":\"page:%d\"}"), FMath::Max(0, Page - 1)), Page > 0, false, float(Px));
			Text(L, FString::Printf(TEXT("%d / %d"), Page + 1, Pages), Foot - FVector2D(Px * 1.6, -Px * 0.3), float(Px), DimText);
			Button(L, Foot + FVector2D(Px * 3.6, 0), FVector2D(Px * 2.4, Px * 1.9), TEXT("›"), FString::Printf(TEXT("{\"type\":\"button\",\"id\":\"page:%d\"}"), Page + 1), Page + 1 < Pages, false, float(Px));
		}
		else if (Screen == TEXT("backlog"))
		{
			const TArray<TSharedPtr<FJsonValue>> Entries = OKStoryArr(Panel, TEXT("entries"));
			const FSlateFontInfo F = OKStoryFont(Px * 0.85);
			// Laid out from the newest at the bottom, scrolled up by the wheel.
			double Y = PanelMax.Y - Px * 1.5 + BacklogScroll;
			for (int32 I = Entries.Num() - 1; I >= 0 && Y > Body.Y; --I)
			{
				const TSharedPtr<FJsonObject> E = Entries[I]->AsObject();
				TArray<FString> Lines = OKStoryWrap(OKStoryStr(E, TEXT("text")), F, float(BodySize.X - Px * 4));
				const FString Chose = OKStoryStr(E, TEXT("chose"));
				if (!Chose.IsEmpty()) Lines.Add(TEXT("→ ") + Chose);
				const double EH = Px * 1.2 + Lines.Num() * Px * 1.25 + Px * 0.6;
				Y -= EH;
				if (Y > PanelMax.Y) continue;
				const FString Name = OKStoryStr(E, TEXT("name"));
				if (Y >= Body.Y)
				{
					if (!Name.IsEmpty()) Text(L, Name, FVector2D(Body.X, Y), float(Px * 0.85), OKStoryHexColor(OKStoryStr(E, TEXT("color")), Accent), true);
					for (int32 J = 0; J < Lines.Num(); ++J) Text(L, Lines[J], FVector2D(Body.X + Px, Y + Px * 1.2 + J * Px * 1.25), float(Px * 0.85), J == Lines.Num() - 1 && !Chose.IsEmpty() ? Accent : TextColor);
					if (OKStoryFlag(E, TEXT("voice")))
						Button(L, FVector2D(PanelMax.X - Px * 3.6, Y), FVector2D(Px * 2.2, Px * 1.6), TEXT("♪"), FString::Printf(TEXT("{\"type\":\"button\",\"id\":\"replay:%d\"}"), int32(OKStoryNum(E, TEXT("index")))), true, false, float(Px * 0.8));
				}
			}
		}
		else if (Screen == TEXT("outline"))
		{
			// Acts and their lines from the top (scrolled by the wheel); a click on a line plays from it.
			const TArray<TSharedPtr<FJsonValue>> Entries = OKStoryArr(Panel, TEXT("entries"));
			const double RowH = Px * 1.35;
			double Y = Body.Y - OutlineScroll;
			for (const TSharedPtr<FJsonValue>& V : Entries)
			{
				const TSharedPtr<FJsonObject> E = V->AsObject();
				if (Y + RowH > Body.Y && Y < PanelMax.Y - Px)
				{
					if (OKStoryFlag(E, TEXT("header")))
						Text(L, OKStoryStr(E, TEXT("act")) + TEXT("  ") + OKStoryStr(E, TEXT("title")), FVector2D(Body.X, Y), float(Px * 0.9), Accent, true);
					else
					{
						const bool bCurrent = OKStoryFlag(E, TEXT("current"));
						if (bCurrent) Box(L, FVector2D(Body.X, Y - Px * 0.1), FVector2D(BodySize.X, RowH), FLinearColor(0.42f, 0.22f, 0.34f, 0.7f));
						const FString Name = OKStoryStr(E, TEXT("name"));
						const FString Row = FString::Printf(TEXT("%s  %s%s%s"), *OKStoryStr(E, TEXT("id")), *Name, Name.IsEmpty() ? TEXT("") : TEXT("："), *OKStoryStr(E, TEXT("text")))
							+ (OKStoryNum(E, TEXT("choices")) > 0 ? TEXT("  ◆") : TEXT(""));
						Text(L + 1, Row, FVector2D(Body.X + Px, Y), float(Px * 0.8), OKStoryFlag(E, TEXT("read")) || bCurrent ? TextColor : DimText);
						Hits.Add({ FVector2D(Body.X, Y), FVector2D(Body.X + BodySize.X, Y + RowH), FString::Printf(TEXT("{\"type\":\"button\",\"id\":\"jump:%s\"}"), *OKStoryStr(E, TEXT("id"))) });
					}
				}
				Y += RowH;
			}
		}
		else if (Screen == TEXT("settings"))
		{
			const TArray<TSharedPtr<FJsonValue>> Items = OKStoryArr(Panel, TEXT("items"));
			const double RowH = Px * 2.3;
			for (int32 I = 0; I < Items.Num(); ++I)
			{
				const TSharedPtr<FJsonObject> It = Items[I]->AsObject();
				const double Y = Body.Y + I * RowH;
				const FString Id = OKStoryStr(It, TEXT("id"));
				Text(L, OKStoryStr(It, TEXT("label")), FVector2D(Body.X, Y + Px * 0.3), float(Px), TextColor);
				const double CX = Body.X + BodySize.X * 0.55;
				if (OKStoryStr(It, TEXT("kind")) == TEXT("toggle"))
				{
					const bool bValue = OKStoryFlag(It, TEXT("value"));
					Button(L, FVector2D(CX, Y), FVector2D(Px * 5, Px * 1.8), bValue ? OKStoryStr(Panel, TEXT("on")) : OKStoryStr(Panel, TEXT("off")),
						FString::Printf(TEXT("{\"type\":\"button\",\"id\":\"set:%s\",\"value\":%s}"), *Id, bValue ? TEXT("false") : TEXT("true")), true, bValue, float(Px));
				}
				else
				{
					const double V = OKStoryNum(It, TEXT("value")), Step = OKStoryNum(It, TEXT("step"), 1), Lo = OKStoryNum(It, TEXT("min")), Hi = OKStoryNum(It, TEXT("max"), 1);
					auto Set = [&](double To) { return FString::Printf(TEXT("{\"type\":\"button\",\"id\":\"set:%s\",\"value\":%g}"), *Id, FMath::Clamp(To, Lo, Hi)); };
					Button(L, FVector2D(CX, Y), FVector2D(Px * 2, Px * 1.8), TEXT("−"), Set(V - Step), V > Lo, false, float(Px));
					// A bar showing where the value sits.
					const double BarX = CX + Px * 2.6, BarW = Px * 9;
					Box(L, FVector2D(BarX, Y + Px * 0.8), FVector2D(BarW, Px * 0.25), LineColor);
					Box(L + 1, FVector2D(BarX, Y + Px * 0.8), FVector2D(BarW * (Hi > Lo ? (V - Lo) / (Hi - Lo) : 0), Px * 0.25), Accent);
					Text(L, Step < 1 ? FString::Printf(TEXT("%.2f"), V) : FString::Printf(TEXT("%.0f"), V), FVector2D(BarX + BarW + Px * 0.6, Y + Px * 0.3), float(Px), DimText);
					Button(L, FVector2D(BarX + BarW + Px * 3.6, Y), FVector2D(Px * 2, Px * 1.8), TEXT("+"), Set(V + Step), V < Hi, false, float(Px));
				}
			}
		}
	}
	return L + 4;
}

FReply SOKStoryView::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& E)
{
	const FVector2D At = Geo.AbsoluteToLocal(E.GetScreenSpacePosition());
	const bool bLeft = E.GetEffectingButton() == EKeys::LeftMouseButton;
	if (bLeft)
		for (int32 I = Hits.Num() - 1; I >= 0; --I)
			if (At.X >= Hits[I].Min.X && At.Y >= Hits[I].Min.Y && At.X <= Hits[I].Max.X && At.Y <= Hits[I].Max.Y)
			{
				Send(Hits[I].Event);
				return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
			}
	if (bPanelOpen)
	{
		const bool bInside = At.X >= PanelMin.X && At.Y >= PanelMin.Y && At.X <= PanelMax.X && At.Y <= PanelMax.Y;
		if (!bInside || !bLeft) Send(TEXT("{\"type\":\"button\",\"id\":\"close\"}"));
	}
	else Send(FString::Printf(TEXT("{\"type\":\"click\",\"button\":\"%s\"}"), bLeft ? TEXT("left") : TEXT("right")));
	return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SOKStoryView::OnMouseWheel(const FGeometry& Geo, const FPointerEvent& E)
{
	const TSharedPtr<FJsonObject> Frame = Source.Frame ? Source.Frame() : nullptr;
	const FString Screen = Frame ? OKStoryStr(Frame, TEXT("screen")) : FString();
	if (Screen == TEXT("backlog"))
	{
		// Scrolling the log is the view's own business.
		BacklogScroll = FMath::Max(0.f, BacklogScroll + E.GetWheelDelta() * float(Geo.GetLocalSize().Y) * 0.05f);
		return FReply::Handled();
	}
	if (Screen == TEXT("outline"))
	{
		OutlineScroll = FMath::Max(0.f, OutlineScroll - E.GetWheelDelta() * float(Geo.GetLocalSize().Y) * 0.05f);
		return FReply::Handled();
	}
	BacklogScroll = 0.f;
	Send(FString::Printf(TEXT("{\"type\":\"wheel\",\"delta\":%g}"), E.GetWheelDelta()));
	return FReply::Handled();
}

FReply SOKStoryView::OnMouseMove(const FGeometry& Geo, const FPointerEvent& E)
{
	const FVector2D Size = Geo.GetLocalSize();
	const FVector2D At = Geo.AbsoluteToLocal(E.GetScreenSpacePosition());
	if (Size.X > 0 && Size.Y > 0)
	{
		const FVector2D N(At.X / Size.X, At.Y / Size.Y);
		if (FVector2D::Distance(N, LastPointer) > 0.002) { LastPointer = N; Send(FString::Printf(TEXT("{\"type\":\"mouse\",\"x\":%.4f,\"y\":%.4f}"), N.X, N.Y)); }
	}
	const TSharedPtr<FJsonObject> Frame = Source.Frame ? Source.Frame() : nullptr;
	if (Frame && !OKStoryStr(Frame, TEXT("camera_edit")).IsEmpty() && !E.GetCursorDelta().IsNearlyZero())
		Send(FString::Printf(TEXT("{\"type\":\"mouse_delta\",\"dx\":%g,\"dy\":%g}"), E.GetCursorDelta().X, E.GetCursorDelta().Y));
	return FReply::Unhandled();
}

FString SOKStoryView::KeyEvent(const FKeyEvent& Event, bool bDown)
{
	return FString::Printf(TEXT("{\"type\":\"key\",\"key\":%s,\"down\":%s}"), *OKStoryQuote(Event.GetKey().GetFName().ToString()), bDown ? TEXT("true") : TEXT("false"));
}

FReply SOKStoryView::OnKeyDown(const FGeometry& Geo, const FKeyEvent& E)
{
	// The console stays reachable.
	if (E.GetKey() == EKeys::Tilde) return FReply::Unhandled();
	if (!E.IsRepeat()) Send(KeyEvent(E, true));
	return FReply::Handled();
}

FReply SOKStoryView::OnKeyUp(const FGeometry& Geo, const FKeyEvent& E)
{
	if (E.GetKey() == EKeys::Tilde) return FReply::Unhandled();
	Send(KeyEvent(E, false));
	return FReply::Handled();
}
