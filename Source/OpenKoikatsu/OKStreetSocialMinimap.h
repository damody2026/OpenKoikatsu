#pragma once
#include "OKStreetSocialText.h"
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"

inline int32 OKStreetMinimapFavorBand(float Favor){return !FMath::IsFinite(Favor)?1:Favor<0?0:Favor<25?1:Favor<50?2:Favor<75?3:4;}
// Thick ring color by relation stage (0 stranger, 1 friend, 2 lover); a lover whose favor is full turns red.
inline FLinearColor OKStreetMinimapRelationColor(int32 Stage,bool FavorFull){
 if(FavorFull)return FLinearColor(1,.12f,.12f);
 const FLinearColor Colors[]={FLinearColor(.25f,.65f,1),FLinearColor(.3f,1,.5f),FLinearColor(1,.35f,.75f)};
 return Colors[FMath::Clamp(Stage,0,2)];
}
// +X north/up, +Y east/right. Same mapping for paths, actors and facing.
inline FVector2D OKStreetMinimapOffset(const FVector& World,const FVector& Player,bool Clamp=true){
 FVector2D P((World.Y-Player.Y)/3500.f*120.f,-(World.X-Player.X)/3500.f*120.f);
 if(P.ContainsNaN())return FVector2D::ZeroVector;
 if(Clamp&&P.Size()>120)P=P.GetSafeNormal()*120;
 return P;
}
// Ten narrow display cells: five CJK characters or ten Latin characters.
// Consume surrogate pairs together so a name never ends in half a code point.
inline FString OKStreetMinimapName(const FString& Name){
 int32 Cells=0,End=0;while(End<Name.Len()){
  const uint32 C=uint16(Name[End]);const bool Pair=C>=0xD800&&C<=0xDBFF&&End+1<Name.Len()&&uint16(Name[End+1])>=0xDC00&&uint16(Name[End+1])<=0xDFFF;
  const int32 Width=Pair||(C>=0x2E80&&C<=0xA4CF)||(C>=0xF900&&C<=0xFAFF)||(C>=0xFF01&&C<=0xFF60)?2:1;
  if(Cells+Width>10)break;Cells+=Width;End+=Pair?2:1;
 }return Name.Left(End);
}
// Captions sit at a fixed offset to the right of their dot, left-aligned, and may overlap.
// Only clamp to the map so they never leave it; no free-slot search, so nothing jumps frame to frame.
inline FVector2D OKStreetMinimapLabel(const FVector2D& Anchor,FVector2D Size){
 return FVector2D(FMath::Clamp(float(Anchor.X+15),14.f,float(316-Size.X)),FMath::Clamp(float(Anchor.Y-Size.Y*.5f),32.f,float(278-Size.Y)));
}
// Each character keeps one vivid color for both dot and name.
inline FLinearColor OKStreetMinimapCharacterColor(int32 Slot){
 // One per actor slot (the male and up to ten heroines), no two alike.
 const FLinearColor Colors[]={FLinearColor(.2f,.9f,1),FLinearColor(1,.6f,.1f),FLinearColor(1,.35f,.85f),FLinearColor(.5f,1,.2f),FLinearColor(1,.95f,.2f),FLinearColor(.7f,.5f,1),FLinearColor(1,.4f,.35f),FLinearColor(.1f,.8f,.5f),FLinearColor(.35f,.55f,1),FLinearColor(1,.8f,.6f),FLinearColor(.75f,.75f,.75f)};
 return Colors[FMath::Abs(Slot)%UE_ARRAY_COUNT(Colors)];
}
struct FOKStreetMinimapMarker {int32 Slot=0;FString Name;FVector Position=FVector::ZeroVector;float Favor=0;int32 Stage=0;bool FavorFull=false;bool Selected=false;};
struct FOKStreetMinimapData {FVector Player=FVector::ZeroVector;float FacingYaw=0;bool ShowNames=true;TArray<FOKStreetMinimapMarker> Girls;TArray<TArray<FVector>> Paths;TArray<FVector> Navigation;TArray<TPair<FString,FVector>> Places;};
class SOKStreetMinimap final:public SLeafWidget {
public:
 SLATE_BEGIN_ARGS(SOKStreetMinimap){} SLATE_END_ARGS()
 TFunction<FOKStreetMinimapData()> Read;
 mutable int32 PaintedGirls=0,Paints=0,PaintedNames=0;
 mutable TArray<int32> PaintedBands;
 void Construct(const FArguments&){ForceVolatile(true);}
 virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(330,440);}
 virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool)const override{
  if(!Read)return Layer;const auto D=Read();PaintedGirls=D.Girls.Num();PaintedBands.Reset();PaintedNames=0;++Paints;
  const FSlateBrush* Brush=FCoreStyle::Get().GetBrush("WhiteBrush");
  auto Box=[&](FVector2D P,FVector2D Size,FLinearColor Color,int32 Depth=2){FSlateDrawElement::MakeBox(Out,Layer+Depth,G.ToPaintGeometry(Size,FSlateLayoutTransform(P)),Brush,ESlateDrawEffect::None,Color);};
  auto Text=[&](FVector2D P,const FString& Value,FLinearColor Color=FLinearColor::White,int32 Size=11){FSlateDrawElement::MakeText(Out,Layer+3,G.ToPaintGeometry(FVector2D(310,20),FSlateLayoutTransform(P)),Value,FCoreStyle::GetDefaultFontStyle("Regular",Size),ESlateDrawEffect::None,Color);};
  auto Line=[&](TArray<FVector2D> P,FLinearColor Color,float Width=1){FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Color,true,Width);};
  Box(FVector2D::ZeroVector,FVector2D(330,440),FLinearColor(.018f,.03f,.055f,.93f),0);
  Text(FVector2D(12,8),OKT(TEXT("ui.minimap.minimap_range_header")));
  const FVector2D Center(165,155);
  Out.PushClip(FSlateClippingZone(G.MakeChild(FVector2D(306,250),FSlateLayoutTransform(FVector2D(12,30))).GetLayoutBoundingRect()));
  for(int32 I=-3;I<=3;++I){const float V=I*120.f/3.5f;Line({Center+FVector2D(V,-120),Center+FVector2D(V,120)},FLinearColor(.15f,.2f,.26f));Line({Center+FVector2D(-120,V),Center+FVector2D(120,V)},FLinearColor(.15f,.2f,.26f));}
  for(const auto& Path:D.Paths){TArray<FVector2D> Points;for(const auto& W:Path)Points.Add(Center+OKStreetMinimapOffset(W,D.Player,false));if(Points.Num()>1)Line(Points,FLinearColor(.35f,.4f,.45f),3);}
  if(D.Navigation.Num()>1){TArray<FVector2D> P;for(const auto& W:D.Navigation)P.Add(Center+OKStreetMinimapOffset(W,D.Player,false));Line(P,FLinearColor(1,.8f,.15f),4);}
  TArray<FVector2D> Ring;for(int32 I=0;I<=48;++I){float A=I*2*PI/48;Ring.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*120);}Line(Ring,FLinearColor(.5f,.6f,.7f));
  // Filled circle as horizontal scanlines, drawn above the grid and paths.
  auto Dot=[&](FVector2D C,float R,FLinearColor Color){for(float Y=-R;Y<=R;Y+=1){const float W=FMath::Sqrt(FMath::Max(R*R-Y*Y,0.f));TArray<FVector2D> P{C+FVector2D(-W,Y),C+FVector2D(W,Y)};FSlateDrawElement::MakeLines(Out,Layer+3,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Color,true,1.5f);}};
  auto Circle=[&](FVector2D C,float R,FLinearColor Color,float Width){TArray<FVector2D> P;for(int32 I=0;I<=24;++I){const float A=I*2*PI/24;P.Add(C+FVector2D(FMath::Cos(A),FMath::Sin(A))*R);}Line(P,Color,Width);};
  const auto Font=FCoreStyle::GetDefaultFontStyle("Regular",8);const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
  for(const auto& Place:D.Places){const FVector2D At=Center+OKStreetMinimapOffset(Place.Value,D.Player);const FLinearColor Color(.95f,.82f,.5f);Box(At-FVector2D(4,4),FVector2D(8,8),Color,3);Text(At+FVector2D(6,-8),Place.Key,Color,8);}
  for(const auto& M:D.Girls){const auto Anchor=Center+OKStreetMinimapOffset(M.Position,D.Player);
   const FString Caption=D.ShowNames?FString::Printf(TEXT("%d %s"),M.Slot,*OKStreetMinimapName(M.Name)):FString::FromInt(M.Slot);
   const auto Measured=Measure->Measure(Caption,Font);const FVector2D Size(FMath::Min(float(Measured.X),290.f),FMath::Max(float(Measured.Y),14.f));
   const auto L=OKStreetMinimapLabel(Anchor,Size);
   const auto Color=OKStreetMinimapCharacterColor(M.Slot);PaintedBands.Add(OKStreetMinimapFavorBand(M.Favor));if(D.ShowNames)++PaintedNames;
   Box(L-FVector2D(2,1),Size+FVector2D(4,2),FLinearColor(.018f,.03f,.055f,.75f));
   if(M.Selected)Circle(Anchor,12,FLinearColor::White,1.5f);
   Circle(Anchor,7,OKStreetMinimapRelationColor(M.Stage,M.FavorFull),3);
   Dot(Anchor,4,Color);Text(L,Caption,Color,8);
  }

  const float A=FMath::DegreesToRadians(D.FacingYaw+90);const FVector2D F(FMath::Sin(A),-FMath::Cos(A)),R(-F.Y,F.X);
  Line({Center+F*10,Center-F*6+R*6,Center-F*3,Center-F*6-R*6,Center+F*10},FLinearColor::White,2);
  Out.PopClip();
  Text(FVector2D(12,284),OKT(TEXT("ui.minimap.minimap_legend")),FLinearColor(.85f,.9f,1),10);
  const TCHAR* Labels[]={OKT(TEXT("ui.minimap.stranger_label")),OKT(TEXT("ui.bonds.friend_stage")),OKT(TEXT("ui.bonds.lover_stage")),OKT(TEXT("ui.minimap.favor_maxed_label"))};
  for(int32 I=0;I<4;++I){const auto C=OKStreetMinimapRelationColor(I<3?I:2,I==3);Circle(FVector2D(12+I*78+6,312),5,C,3);Text(FVector2D(24+I*78,304),Labels[I],C,9);}
  Text(FVector2D(12,325),OKT(TEXT("ui.minimap.stage_favor_reset_note")),FLinearColor(.8f,.85f,.9f),9);
  // Five rows a column: more than five heroines take a second column with shorter names.
  const bool Two=D.Girls.Num()>5;
  for(int32 I=0;I<D.Girls.Num()&&I<10;++I){const auto& M=D.Girls[I];Text(FVector2D(12+(I/5)*156,344+(I%5)*17),OKMsg(TEXT("ui.minimap.minimap_slot_entry"), {{TEXT("slot"), M.Slot}, {TEXT("name"), *(D.ShowNames?M.Name.Left(Two?7:15):FString(OKT(TEXT("ui.minimap.heroine_label"))))}, {TEXT("favor"), M.Favor}}),OKStreetMinimapCharacterColor(M.Slot),10);}
  return Layer+4;
 }
};

// Minimap shows only the nearest places so the 35 m view is not buried in labels.
inline void OKStreetNearestPlaces(TArray<TPair<FString,FVector>>& Places,const FVector& Player,int32 Keep){
 Places.Sort([&](const TPair<FString,FVector>& A,const TPair<FString,FVector>& B){return FVector::DistSquared2D(A.Value,Player)<FVector::DistSquared2D(B.Value,Player);});
 if(Places.Num()>Keep)Places.SetNum(Keep);
}

// Whole-street map (M). Same axes as the minimap: +X north/up, +Y east/right. Clicking a place
// label or any spot hands the world point to Pick, which starts auto navigation.
class SOKStreetBigMap final:public SLeafWidget {
public:
 SLATE_BEGIN_ARGS(SOKStreetBigMap){} SLATE_END_ARGS()
 TFunction<FOKStreetMinimapData()> Read;
 TFunction<void(const FVector&,const FString&)> Pick;
 void Construct(const FArguments&){ForceVolatile(true);}
 virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(960,720);}
 // World bounds (min X/Y, max X/Y) of everything drawn, padded; fitted into the map area keeping aspect.
 struct FFrame{FVector2D Origin;float Scale=1;double MaxX=0,MinY=0;FVector2D ToMap(const FVector& W)const{return Origin+FVector2D((W.Y-MinY)*Scale,(MaxX-W.X)*Scale);}FVector ToWorld(FVector2D P)const{P-=Origin;return FVector(MaxX-P.Y/Scale,MinY+P.X/Scale,0);}};
 static FFrame Fit(const FOKStreetMinimapData& D,FVector2D Area,FVector2D Offset){
  FBox Box(ForceInit);Box+=D.Player;for(const auto& Path:D.Paths)for(const auto& P:Path)Box+=P;for(const auto& P:D.Places)Box+=P.Value;for(const auto& G:D.Girls)Box+=G.Position;
  Box=Box.ExpandBy(FVector(800,800,0));const FVector Size=Box.GetSize();
  FFrame F;F.Scale=float(FMath::Min(Area.X/FMath::Max(Size.Y,1.),Area.Y/FMath::Max(Size.X,1.)));F.MaxX=Box.Max.X;F.MinY=Box.Min.Y;
  F.Origin=Offset+(Area-FVector2D(Size.Y*F.Scale,Size.X*F.Scale))*.5f;return F;
 }
 static constexpr float Top=40,Pad=16;
 FVector2D MapArea()const{return FVector2D(960-Pad*2,720-Top-Pad);}
 virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)override{
  if(E.GetEffectingButton()!=EKeys::LeftMouseButton||!Read||!Pick)return FReply::Unhandled();
  const auto D=Read();const auto F=Fit(D,MapArea(),FVector2D(Pad,Top));const FVector2D Local=G.AbsoluteToLocal(E.GetScreenSpacePosition());
  if(Local.Y<Top)return FReply::Handled();
  // A place marker or its label wins over the bare ground under it.
  int32 Best=INDEX_NONE;float BestDistance=18;
 for(int32 I=0;I<D.Places.Num();++I){const FVector2D At=F.ToMap(D.Places[I].Value);
   const bool OnLabel=Local.X>=At.X+6&&Local.X<=At.X+10+D.Places[I].Key.Len()*13&&Local.Y>=At.Y-12&&Local.Y<=At.Y+6;
   const float Distance=OnLabel?0.f:float((Local-At).Size());if(Distance<BestDistance){BestDistance=Distance;Best=I;}}
  if(Best!=INDEX_NONE)Pick(D.Places[Best].Value,D.Places[Best].Key);else Pick(F.ToWorld(Local),FString());
  return FReply::Handled();
 }
 virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool)const override{
  if(!Read)return Layer;const auto D=Read();const auto F=Fit(D,MapArea(),FVector2D(Pad,Top));
  const FSlateBrush* Brush=FCoreStyle::Get().GetBrush("WhiteBrush");
  auto Box=[&](FVector2D P,FVector2D Size,FLinearColor Color,int32 Depth){FSlateDrawElement::MakeBox(Out,Layer+Depth,G.ToPaintGeometry(Size,FSlateLayoutTransform(P)),Brush,ESlateDrawEffect::None,Color);};
  auto Text=[&](FVector2D P,const FString& Value,FLinearColor Color,int32 Size){FSlateDrawElement::MakeText(Out,Layer+4,G.ToPaintGeometry(FVector2D(400,24),FSlateLayoutTransform(P)),Value,FCoreStyle::GetDefaultFontStyle("Regular",Size),ESlateDrawEffect::None,Color);};
  auto Line=[&](const TArray<FVector2D>& P,FLinearColor Color,float Width){FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Color,true,Width);};
  Box(FVector2D::ZeroVector,FVector2D(960,720),FLinearColor(.018f,.03f,.055f,.95f),0);
  Text(FVector2D(16,10),OKT(TEXT("ui.minimap.world_map_help")),FLinearColor(.85f,.9f,1),12);
  for(const auto& Path:D.Paths){TArray<FVector2D> P;for(const auto& W:Path)P.Add(F.ToMap(W));if(P.Num()>1)Line(P,FLinearColor(.35f,.4f,.45f),4);}
  if(D.Navigation.Num()>1){TArray<FVector2D> P;for(const auto& W:D.Navigation)P.Add(F.ToMap(W));Line(P,FLinearColor(1,.8f,.15f),4);}
  for(const auto& Place:D.Places){const FVector2D At=F.ToMap(Place.Value);const FLinearColor Color(.95f,.82f,.5f);Box(At-FVector2D(5,5),FVector2D(10,10),Color,2);Text(At+FVector2D(8,-11),Place.Key,Color,10);}
  for(const auto& M:D.Girls){const FVector2D At=F.ToMap(M.Position);const auto Color=OKStreetMinimapCharacterColor(M.Slot);Box(At-FVector2D(5,5),FVector2D(10,10),Color,3);Text(At+FVector2D(8,-11),D.ShowNames?OKStreetMinimapName(M.Name):FString::FromInt(M.Slot),Color,10);}
  const FVector2D Me=F.ToMap(D.Player);const float A=FMath::DegreesToRadians(D.FacingYaw+90);const FVector2D Fw(FMath::Sin(A),-FMath::Cos(A)),R(-Fw.Y,Fw.X);
  Line({Me+Fw*12,Me-Fw*7+R*7,Me-Fw*3,Me-Fw*7-R*7,Me+Fw*12},FLinearColor::White,2.5f);
  return Layer+5;
 }
};
