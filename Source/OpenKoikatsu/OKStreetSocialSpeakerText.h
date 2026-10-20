#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "OKStreetSocialMinimap.h"

// Who says a line, by its color: each heroine has the color of her minimap dot and name (OKStreetMinimapCharacterColor,
// by her slot), the player keeps his light blue, anything else (narration, notices) is white.
inline const FLinearColor& OKStreetPlayerLineColor() { static const FLinearColor C(.65f, .85f, 1.f); return C; }

// The speaker of a caption line "Name：text" / "Name: text" / "Name「text」": the slot whose name starts the line
// (the longest name wins, so "Aki" does not take "Akiko"); INDEX_NONE when no character starts it.
inline int32 OKStreetLineSpeaker(const FString& Line, const TArray<FString>& Names) {
 int32 Best = INDEX_NONE, BestLen = 0;
 for (int32 I = 0; I < Names.Num(); ++I) {
  const FString& N = Names[I];
  if (N.Len() <= BestLen || !Line.StartsWith(N)) continue;
  const FString Rest = Line.Mid(N.Len());
  if (Rest.StartsWith(TEXT("：")) || Rest.StartsWith(TEXT(":")) || Rest.StartsWith(TEXT("「")) || Rest.StartsWith(TEXT("“")) || Rest.StartsWith(TEXT("\"")) || Rest.StartsWith(TEXT(" ")) || Rest.IsEmpty()) { Best = I; BestLen = N.Len(); }
 }
 return Best;
}

inline FLinearColor OKStreetSpeakerColor(int32 Slot) {
 if (Slot == 0) return OKStreetPlayerLineColor();
 return Slot > 0 ? OKStreetMinimapCharacterColor(Slot) : FLinearColor::White;
}

// A caption of one or more lines, each colored by its speaker; it follows Text (read every frame) and rebuilds its lines
// only when the text changes.
class SOKStreetSpeakerText final : public SCompoundWidget {
public:
 SLATE_BEGIN_ARGS(SOKStreetSpeakerText) : _FontSize(21) {}
  SLATE_ARGUMENT(int32, FontSize)
 SLATE_END_ARGS()
 TFunction<FString()> Text;
 TFunction<TArray<FString>()> Names; // slot -> character name (slot 0 is the player)
 int32 Rebuilds = 0;

 void Construct(const FArguments& Args) {
  FontSize = Args._FontSize;
  ChildSlot[SAssignNew(Lines, SVerticalBox)];
 }
 virtual void Tick(const FGeometry& Geometry, const double Now, const float Delta) override {
  SCompoundWidget::Tick(Geometry, Now, Delta);
  const FString Value = Text ? Text() : FString();
  if (Value == Shown && Rebuilds) return;
  Shown = Value; ++Rebuilds;
  Lines->ClearChildren();
  const TArray<FString> All = Names ? Names() : TArray<FString>();
  TArray<FString> Parts; Value.ParseIntoArray(Parts, TEXT("\n"), false);
  int32 Last = INDEX_NONE; // a line without a name continues the speaker above it
  for (const FString& P : Parts) {
   const int32 Who = OKStreetLineSpeaker(P, All);
   if (Who != INDEX_NONE) Last = Who;
   Lines->AddSlot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular", FontSize)).ColorAndOpacity(OKStreetSpeakerColor(Who != INDEX_NONE ? Who : Last)).Text(FText::FromString(P))];
  }
 }
private:
 TSharedPtr<SVerticalBox> Lines;
 FString Shown;
 int32 FontSize = 21;
};
