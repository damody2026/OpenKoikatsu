#pragma once
#include "OKStreetSocialText.h"
#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

// Plan §15.6: the trading terminals and the scratch card are drawn by hand in OnPaint, like the minimap.

// What a chart shows. Line mode is the intraday path (one point per minute of the session); candle mode is daily,
// weekly or monthly bars. Colours follow each market: 乘風行情 red up / green down, Buoy green up / red down.
struct FOKChartData {
 bool Line = false;
 bool RedUp = true;
 bool Dark = true;
 int32 SessionPoints = 271; // full width of a line chart, so a running session fills from the left
 float PrevClose = 0.f, LimitUp = 0.f, LimitDown = 0.f;
 TArray<float> O, H, L, C, V;
 TArray<FString> Dates;
 TArray<int32> Flags; // index of a bar with a "前世記憶" flag
 FString Title;
};

inline TArray<float> OKChartMA(const TArray<float>& C, int32 N) {
 TArray<float> Out;Out.SetNumZeroed(C.Num());double Sum = 0;
 for (int32 I = 0; I < C.Num(); ++I) { Sum += C[I]; if (I >= N) Sum -= C[I - N]; Out[I] = I >= N - 1 ? float(Sum / N) : 0.f; }
 return Out;
}

class SOKMarketChart final : public SLeafWidget {
public:
 SLATE_BEGIN_ARGS(SOKMarketChart) {}
 SLATE_ARGUMENT(TSharedPtr<FOKChartData>, Data)
 SLATE_END_ARGS()
 void Construct(const FArguments& Args) { Data = Args._Data; }
 virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(760, 420); }
 virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override { Hover = G.AbsoluteToLocal(E.GetScreenSpacePosition()); return FReply::Unhandled(); }
 virtual void OnMouseLeave(const FPointerEvent&) override { Hover = FVector2D(-1, -1); }
 virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override {
  const FVector2D Size = G.GetLocalSize();
  const FOKChartData* D = Data.Get();
  const FLinearColor Bg = D && !D->Dark ? FLinearColor(.97f, .97f, .98f) : FLinearColor(.02f, .02f, .03f);
  const FLinearColor Grid = D && !D->Dark ? FLinearColor(.85f, .86f, .88f) : FLinearColor(.16f, .05f, .05f);
  const FLinearColor Text = D && !D->Dark ? FLinearColor(.2f, .2f, .25f) : FLinearColor(.75f, .75f, .8f);
  FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Bg);
  if (!D || D->C.IsEmpty()) {
   FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2D(300, 20), FSlateLayoutTransform(FVector2D(12, 12))), FString(OKT(TEXT("ui.finance_u_i.no_data"))), FCoreStyle::GetDefaultFontStyle("Regular", 11), ESlateDrawEffect::None, Text);
   return Layer + 2;
  }
  const FLinearColor Up = D->RedUp ? FLinearColor(.95f, .15f, .15f) : FLinearColor(.1f, .75f, .35f);
  const FLinearColor Down = D->RedUp ? FLinearColor(.1f, .8f, .3f) : FLinearColor(.9f, .2f, .2f);
  const float Left = 8, Right = 70, Top = 22, Volume = Size.Y * .22f, Gap = 14;
  const float PriceH = Size.Y - Top - Volume - Gap - 18;
  const int32 N = D->C.Num();
  const int32 Slots = D->Line ? FMath::Max(D->SessionPoints, N) : FMath::Max(N, 20);
  const float W = (Size.X - Left - Right) / Slots;
  float Lo = TNumericLimits<float>::Max(), Hi = TNumericLimits<float>::Lowest(), VMax = 1;
  for (int32 I = 0; I < N; ++I) {
   Lo = FMath::Min(Lo, D->Line ? D->C[I] : D->L[I]); Hi = FMath::Max(Hi, D->Line ? D->C[I] : D->H[I]);
   if (D->V.IsValidIndex(I)) VMax = FMath::Max(VMax, D->V[I]);
  }
  if (D->Line && D->PrevClose > 0) { const float Span = FMath::Max(FMath::Abs(Hi - D->PrevClose), FMath::Abs(D->PrevClose - Lo)) * 1.08f + .01f; Lo = D->PrevClose - Span; Hi = D->PrevClose + Span; }
  if (Hi - Lo < 1e-4f) { Hi += .5f; Lo -= .5f; }
  auto Y = [&](float P) { return Top + (Hi - P) / (Hi - Lo) * PriceH; };
  auto X = [&](int32 I) { return Left + (I + .5f) * W; };
  // Grid and price axis.
  for (int32 K = 0; K <= 4; ++K) {
   const float P = Lo + (Hi - Lo) * K / 4.f, YY = Y(P);
   FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), TArray<FVector2D>{FVector2D(Left, YY), FVector2D(Size.X - Right, YY)}, ESlateDrawEffect::None, Grid, true, 1.f);
   FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2D(64, 16), FSlateLayoutTransform(FVector2D(Size.X - Right + 4, YY - 8))), FString::Printf(TEXT("%.2f"), P), FCoreStyle::GetDefaultFontStyle("Mono", 9), ESlateDrawEffect::None, Text);
  }
  if (D->Line) {
   if (D->PrevClose > 0) FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), TArray<FVector2D>{FVector2D(Left, Y(D->PrevClose)), FVector2D(Size.X - Right, Y(D->PrevClose))}, ESlateDrawEffect::None, FLinearColor(.6f, .6f, .6f, .7f), true, 1.f);
   TArray<FVector2D> Points, Avg;double Sum = 0;
   for (int32 I = 0; I < N; ++I) { Points.Add(FVector2D(X(I), Y(D->C[I]))); Sum += D->C[I]; Avg.Add(FVector2D(X(I), Y(float(Sum / (I + 1))))); }
   const bool Rising = D->C.Last() >= D->PrevClose;
   FSlateDrawElement::MakeLines(Out, Layer + 3, G.ToPaintGeometry(), Points, ESlateDrawEffect::None, D->Dark ? FLinearColor(.9f, .9f, .95f) : (Rising ? Up : Down), true, 1.6f);
   FSlateDrawElement::MakeLines(Out, Layer + 3, G.ToPaintGeometry(), Avg, ESlateDrawEffect::None, FLinearColor(1.f, .8f, .1f), true, 1.f);
   for (const float Limit : {D->LimitUp, D->LimitDown}) if (Limit > Lo && Limit < Hi) FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), TArray<FVector2D>{FVector2D(Left, Y(Limit)), FVector2D(Size.X - Right, Y(Limit))}, ESlateDrawEffect::None, FLinearColor(1, .3f, .9f, .6f), true, 1.f);
  } else {
   for (int32 I = 0; I < N; ++I) {
    const bool Rise = D->C[I] >= D->O[I];const FLinearColor Col = Rise ? Up : Down;
    FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), TArray<FVector2D>{FVector2D(X(I), Y(D->H[I])), FVector2D(X(I), Y(D->L[I]))}, ESlateDrawEffect::None, Col, true, 1.f);
    const float Y0 = Y(FMath::Max(D->O[I], D->C[I])), Y1 = Y(FMath::Min(D->O[I], D->C[I]));
    FSlateDrawElement::MakeBox(Out, Layer + 3, G.ToPaintGeometry(FVector2D(FMath::Max(1.f, W * .7f), FMath::Max(1.f, Y1 - Y0)), FSlateLayoutTransform(FVector2D(X(I) - W * .35f, Y0))), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Col);
   }
   const FLinearColor MAColors[] = {FLinearColor(1, 1, 1), FLinearColor(1, .8f, .1f), FLinearColor(.9f, .3f, .9f), FLinearColor(.2f, .7f, 1)};
   const int32 MAN[] = {5, 10, 20, 60};
   for (int32 K = 0; K < 4; ++K) {
    const TArray<float> MA = OKChartMA(D->C, MAN[K]);TArray<FVector2D> P;
    for (int32 I = 0; I < N; ++I) if (MA[I] > 0) P.Add(FVector2D(X(I), Y(MA[I])));
    if (P.Num() > 1) FSlateDrawElement::MakeLines(Out, Layer + 4, G.ToPaintGeometry(), P, ESlateDrawEffect::None, MAColors[K], true, 1.f);
   }
   for (const int32 F : D->Flags) if (F >= 0 && F < N) FSlateDrawElement::MakeText(Out, Layer + 5, G.ToPaintGeometry(FVector2D(20, 16), FSlateLayoutTransform(FVector2D(X(F) - 5, Y(D->H[F]) - 18))), FString(TEXT("⚑")), FCoreStyle::GetDefaultFontStyle("Regular", 10), ESlateDrawEffect::None, FLinearColor(1, .85f, .2f));
  }
  // Volume.
  const float VTop = Top + PriceH + Gap;
  for (int32 I = 0; I < N; ++I) if (D->V.IsValidIndex(I)) {
   const float Hh = D->V[I] / VMax * Volume;const bool Rise = D->Line ? (I == 0 || D->C[I] >= D->C[I - 1]) : D->C[I] >= D->O[I];
   FSlateDrawElement::MakeBox(Out, Layer + 2, G.ToPaintGeometry(FVector2D(FMath::Max(1.f, W * .7f), FMath::Max(1.f, Hh)), FSlateLayoutTransform(FVector2D(X(I) - W * .35f, VTop + Volume - Hh))), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, (Rise ? Up : Down) * FLinearColor(1, 1, 1, .8f));
  }
  // Title and the hovered bar.
  FSlateDrawElement::MakeText(Out, Layer + 6, G.ToPaintGeometry(FVector2D(Size.X, 18), FSlateLayoutTransform(FVector2D(Left, 2))), D->Line ? D->Title : D->Title + TEXT("  MA5 MA10 MA20 MA60"), FCoreStyle::GetDefaultFontStyle("Regular", 10), ESlateDrawEffect::None, Text);
  if (Hover.X >= Left && Hover.X < Size.X - Right) {
   const int32 I = FMath::Clamp(int32((Hover.X - Left) / W), 0, N - 1);
   if (Hover.X <= X(N - 1) + W) {
    FSlateDrawElement::MakeLines(Out, Layer + 6, G.ToPaintGeometry(), TArray<FVector2D>{FVector2D(X(I), Top), FVector2D(X(I), Size.Y - 4)}, ESlateDrawEffect::None, FLinearColor(.6f, .6f, .7f, .8f), true, 1.f);
    const FString Info = D->Line ? FString::Printf(TEXT("%s  %.2f"), D->Dates.IsValidIndex(I) ? *D->Dates[I] : TEXT(""), D->C[I])
                                 : OKMsg(TEXT("ui.finance_u_i.candle_ohlc_tooltip"), {{TEXT("date"), D->Dates.IsValidIndex(I) ? *D->Dates[I] : TEXT("")}, {TEXT("open"), D->O[I]}, {TEXT("high"), D->H[I]}, {TEXT("low"), D->L[I]}, {TEXT("close"), D->C[I]}});
    FSlateDrawElement::MakeText(Out, Layer + 7, G.ToPaintGeometry(FVector2D(Size.X, 18), FSlateLayoutTransform(FVector2D(Left + 260, 2))), Info, FCoreStyle::GetDefaultFontStyle("Mono", 10), ESlateDrawEffect::None, FLinearColor(1, .85f, .3f));
   }
  }
  return Layer + 8;
 }
private:
 TSharedPtr<FOKChartData> Data;
 FVector2D Hover = FVector2D(-1, -1);
};

// 幸運刮刮卡 (plan §15.7.3): drag the mouse to scratch the silver coating off nine amounts; past 60% it reveals all.
// OnStart fires on the first scratch (the card's result is decided then); OnDone when it is fully shown.
struct FOKScratchState {
 TArray<int64> Cells; // filled by OnStart
 int64 Win = 0;
 FString Theme;
 int32 Price = 20;
 bool Started = false, Revealed = false;
 TArray<uint8> Cover; // 36 x 24 coating cells, 1 = still covered
};

class SOKScratchCard final : public SLeafWidget {
public:
 SLATE_BEGIN_ARGS(SOKScratchCard) {}
 SLATE_ARGUMENT(TSharedPtr<FOKScratchState>, Card)
 SLATE_EVENT(FSimpleDelegate, OnStart)
 SLATE_EVENT(FSimpleDelegate, OnDone)
 SLATE_END_ARGS()
 static constexpr int32 CW = 36, CH = 24;
 void Construct(const FArguments& Args) { Card = Args._Card; OnStart = Args._OnStart; OnDone = Args._OnDone; if (Card.IsValid() && Card->Cover.Num() != CW * CH) Card->Cover.Init(1, CW * CH); }
 virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(540, 360); }
 virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override { Down = true; Scratch(G, E); return FReply::Handled().CaptureMouse(SharedThis(this)); }
 virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override { Down = false; return FReply::Handled().ReleaseMouseCapture(); }
 virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override { if (Down) Scratch(G, E); return FReply::Handled(); }
 void RevealAll() { if (!Card.IsValid()) return; if (!Card->Started) { Card->Started = true; OnStart.ExecuteIfBound(); } Card->Cover.Init(0, CW * CH); Finish(); }
 virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override {
  const FVector2D S = G.GetLocalSize();const auto* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
  FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), Brush, ESlateDrawEffect::None, FLinearColor(.75f, .08f, .1f));
  FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2D(S.X - 20, S.Y - 70), FSlateLayoutTransform(FVector2D(10, 60))), Brush, ESlateDrawEffect::None, FLinearColor(1, .93f, .7f));
  const FString Title = Card.IsValid() ? OKMsg(TEXT("ui.finance_u_i.lucky_scratch_card_title"), {{TEXT("theme"), *Card->Theme}, {TEXT("price"), Card->Price}}) : FString();
  FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2D(S.X, 30), FSlateLayoutTransform(FVector2D(14, 14))), Title, FCoreStyle::GetDefaultFontStyle("Bold", 14), ESlateDrawEffect::None, FLinearColor(1, .9f, .3f));
  const FVector2D Area(S.X - 20, S.Y - 70), Origin(10, 60);
  if (Card.IsValid() && Card->Cells.Num() == 9)
   for (int32 I = 0; I < 9; ++I) {
    const FVector2D C = Origin + FVector2D((I % 3 + .5f) * Area.X / 3, (I / 3 + .5f) * Area.Y / 3);
    const bool Winning = Card->Win > 0 && Card->Cells[I] == Card->Win;
    FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2D(160, 40), FSlateLayoutTransform(C - FVector2D(60, 18))), FString::Printf(TEXT("$%lld"), Card->Cells[I]), FCoreStyle::GetDefaultFontStyle("Bold", 22), ESlateDrawEffect::None, Winning ? FLinearColor(.85f, .1f, .1f) : FLinearColor(.2f, .2f, .25f));
   }
  if (Card.IsValid())
   for (int32 Y = 0; Y < CH; ++Y) for (int32 X = 0; X < CW; ++X) if (Card->Cover[Y * CW + X]) {
    const float Shade = .62f + .08f * float(((X * 7 + Y * 13) % 5)) / 5.f;
    FSlateDrawElement::MakeBox(Out, Layer + 3, G.ToPaintGeometry(FVector2D(Area.X / CW + .5f, Area.Y / CH + .5f), FSlateLayoutTransform(Origin + FVector2D(X * Area.X / CW, Y * Area.Y / CH))), Brush, ESlateDrawEffect::None, FLinearColor(Shade, Shade, Shade + .04f));
   }
  if (Card.IsValid() && !Card->Started)
   FSlateDrawElement::MakeText(Out, Layer + 4, G.ToPaintGeometry(FVector2D(300, 30), FSlateLayoutTransform(Origin + FVector2D(Area.X / 2 - 110, Area.Y / 2 - 12))), FString(OKT(TEXT("ui.finance_u_i.hold_drag_to_scratch"))), FCoreStyle::GetDefaultFontStyle("Bold", 16), ESlateDrawEffect::None, FLinearColor(.3f, .3f, .35f));
  return Layer + 5;
 }
private:
 void Scratch(const FGeometry& G, const FPointerEvent& E) {
  if (!Card.IsValid() || Card->Revealed) return;
  if (!Card->Started) { Card->Started = true; OnStart.ExecuteIfBound(); }
  const FVector2D S = G.GetLocalSize();const FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition()) - FVector2D(10, 60);
  const FVector2D Cell((S.X - 20) / CW, (S.Y - 70) / CH);
  const int32 CX = int32(P.X / Cell.X), CY = int32(P.Y / Cell.Y);
  for (int32 Y = CY - 1; Y <= CY + 1; ++Y) for (int32 X = CX - 1; X <= CX + 1; ++X) if (X >= 0 && Y >= 0 && X < CW && Y < CH) Card->Cover[Y * CW + X] = 0;
  int32 Left = 0;for (uint8 C : Card->Cover) Left += C;
  if (Left < CW * CH * .4f) { Card->Cover.Init(0, CW * CH); Finish(); }
 }
 void Finish() { if (Card.IsValid() && !Card->Revealed) { Card->Revealed = true; OnDone.ExecuteIfBound(); } }
 TSharedPtr<FOKScratchState> Card;
 FSimpleDelegate OnStart, OnDone;
 bool Down = false;
};
