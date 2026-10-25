#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// Plan §8: the catalogue (Content/IndependentResources/StreetSocialData/catalog.json, built by
// Tools/build_street_social_catalog_json.lua) and the money rules. Pure functions; the scene applies the results.

struct FOKStreetCatalogItem {
 FString Id, Place, Name, Category, Prop;
 FString ZhName; // the data's own name, kept when the language swaps Name
 TMap<FString, int32> Price; // size ("" for one size, or M/L/S) -> ¥
 TArray<int32> Season;       // months it is sold; empty = all year
 TArray<FString> Tags, Options;
 TMap<FString, float> Effects;
 bool Giftable = false, Consumable = true;
 int32 Floor = 0;            // a building's floor it is sold on (1-based); 0 = anywhere in the place
 // A drink (served in a cup): sipped even when full, while food stops at OKStreetFullSatiety.
 bool IsDrink() const { return Prop == TEXT("cup"); }
 int32 PriceOf(const FString& Size) const { if (const int32* P = Price.Find(Size)) return *P; int32 Low = MAX_int32; for (const auto& P : Price) Low = FMath::Min(Low, P.Value); return Low == MAX_int32 ? 0 : Low; }
 FString DefaultSize() const { if (Price.Contains(TEXT(""))) return FString(); if (Price.Contains(TEXT("M"))) return TEXT("M"); TArray<FString> Keys; Price.GetKeys(Keys); return Keys.IsEmpty() ? FString() : Keys[0]; }
};

// One line of the order being built (plan §8.2). ForHer: ordered for the heroine he is with.
struct FOKStreetCartLine {
 FString ItemId, Size, Sweetness, Ice;
 bool ForHer = false;
 int32 Girl = INDEX_NONE; // ordered by another girl with them (the party): hers to eat, his to pay
 bool HerOwn = false; // she ordered it for herself (her reply): hers to keep, never his gift to her
};

// A paid order the kitchen is preparing: it reaches the table OKStreetServeSeconds after the till (Economy.inl).
// Applied: her needs are filled and the line shown; Told: she has been given the dish to react to.
constexpr double OKStreetServeSeconds = 30.0;
// What is on the table: each paid dish, 100% when it is ordered, eaten down over OKStreetEatSeconds once it is served.
// Not served yet it shows 100% and cannot be eaten. Booked: how much of it has already filled her needs.
constexpr double OKStreetEatSeconds = 70.0;
constexpr float OKStreetFullSatiety = 92.f;
// A little too much is still eaten: a dish whose rest fits under this (satiety now + what is left of it) is finished,
// so a tea egg at 80 is eaten, not left after a bite at 92. Ordering food uses the same measure.
constexpr float OKStreetOverfullSatiety = 115.f;
inline float OKStreetItemFill(const FOKStreetCatalogItem& I) { if (I.IsDrink()) return 0.f; const float* V = I.Effects.Find(TEXT("satiety")); return V ? *V : 0.f; }
struct FOKStreetDish {
 FString ItemId, Name;
 bool ForHer = false, Served = false;
 float Percent = 100.f, Booked = 0.f;
 int32 Batch = 0, Her = INDEX_NONE;
};
struct FOKStreetPendingServe {
 int32 Batch = 0;
 double At = 0;
 int32 Her = INDEX_NONE;
 FString Place, PlaceId; // the shop's name (for words) and id (where it is served)
 TArray<FOKStreetCartLine> Lines;
 bool Applied = false, Told = false;
};

struct FOKStreetCatalogPlace {
 TArray<FVector2D> Open, WeekendOpen; // hour ranges; "to" may pass 24 (overnight)
 TArray<int32> ClosedWeekdays;          // 0 = Monday
 int32 Entry = 0, Service = 0;
 double ServeSeconds = OKStreetServeSeconds; // till to table ("serve_s"): a bottle off a convenience-store shelf is not cooked
};

struct FOKStreetCatalogJob {
 FString Id, Place, Title;
 FString ZhTitle; // the data's own title, kept when the language swaps Title
 int32 Wage = 95;
 TArray<FVector2D> Shifts; // hours; "to" may pass 24
 bool StudentOnly = false;
};

struct FOKStreetCatalog {
 TArray<FOKStreetCatalogItem> Items;
 TArray<FOKStreetCatalogJob> Jobs;
 const FOKStreetCatalogJob* FindJob(const FString& Id) const { return Jobs.FindByPredicate([&](const FOKStreetCatalogJob& J) { return J.Id == Id; }); }
 TMap<FString, FOKStreetCatalogPlace> Places;
 FString Error;
 const FOKStreetCatalogItem* Find(const FString& Id) const { return Items.FindByPredicate([&](const FOKStreetCatalogItem& I) { return I.Id == Id; }); }
};

inline FOKStreetCatalog OKStreetLoadCatalog(const FString& Path) {
 FOKStreetCatalog Out;
 FString Body;
 TSharedPtr<FJsonObject> Root;
 if (!FFileHelper::LoadFileToString(Body, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Root) || !Root.IsValid()) { Out.Error = TEXT("catalog unreadable: ") + Path; return Out; }
 auto Ranges = [](const TSharedPtr<FJsonObject>& O, const TCHAR* Field, TArray<FVector2D>& To) {
  const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
  if (O->TryGetArrayField(Field, Rows)) for (const auto& R : *Rows) { const auto& Pair = R->AsArray(); if (Pair.Num() == 2) To.Add(FVector2D(Pair[0]->AsNumber(), Pair[1]->AsNumber())); }
 };
 const TSharedPtr<FJsonObject>* Places = nullptr;
 if (Root->TryGetObjectField(TEXT("places"), Places)) for (const auto& P : (*Places)->Values) {
  const TSharedPtr<FJsonObject> O = P.Value->AsObject(); if (!O) continue;
  FOKStreetCatalogPlace Place; Ranges(O, TEXT("open"), Place.Open); Ranges(O, TEXT("weekend_open"), Place.WeekendOpen);
  const TArray<TSharedPtr<FJsonValue>>* Closed = nullptr; if (O->TryGetArrayField(TEXT("closed_weekdays"), Closed)) for (const auto& D : *Closed) { /* a name ("mon") or a number (0 = Monday), as the rules read it (catalog.rs weekday_of) */ static const TCHAR* Days[] = {TEXT("mon"),TEXT("tue"),TEXT("wed"),TEXT("thu"),TEXT("fri"),TEXT("sat"),TEXT("sun")}; FString Name; double N = 0; if (D->TryGetString(Name)) { for (int32 K = 0; K < 7; ++K) if (Name.Equals(Days[K], ESearchCase::IgnoreCase)) Place.ClosedWeekdays.Add(K); } else if (D->TryGetNumber(N)) Place.ClosedWeekdays.Add(int32(N)); }
  double V = 0; if (O->TryGetNumberField(TEXT("entry"), V)) Place.Entry = int32(V); if (O->TryGetNumberField(TEXT("service"), V)) Place.Service = int32(V); if (O->TryGetNumberField(TEXT("serve_s"), V)) Place.ServeSeconds = V;
  Out.Places.Add(FString(*P.Key), Place);
 }
 const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
 if (Root->TryGetArrayField(TEXT("items"), Items)) for (const auto& Row : *Items) {
  const TSharedPtr<FJsonObject> O = Row->AsObject(); if (!O) continue;
  FOKStreetCatalogItem I;
  O->TryGetStringField(TEXT("id"), I.Id); O->TryGetStringField(TEXT("place"), I.Place); O->TryGetStringField(TEXT("name"), I.Name);
  O->TryGetStringField(TEXT("category"), I.Category); O->TryGetStringField(TEXT("prop"), I.Prop);
  O->TryGetBoolField(TEXT("giftable"), I.Giftable); O->TryGetBoolField(TEXT("consumable"), I.Consumable);
  { double Floor = 0; if (O->TryGetNumberField(TEXT("floor"), Floor)) I.Floor = int32(Floor); }
  const TSharedPtr<FJsonObject>* Price = nullptr; if (O->TryGetObjectField(TEXT("price"), Price)) for (const auto& P : (*Price)->Values) I.Price.Add(FString(*P.Key), int32(P.Value->AsNumber()));
  const TSharedPtr<FJsonObject>* Effects = nullptr; if (O->TryGetObjectField(TEXT("effects"), Effects)) for (const auto& E : (*Effects)->Values) I.Effects.Add(FString(*E.Key), float(E.Value->AsNumber()));
  const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
  if (O->TryGetArrayField(TEXT("season"), List)) for (const auto& M : *List) I.Season.Add(int32(M->AsNumber()));
  if (O->TryGetArrayField(TEXT("tags"), List)) for (const auto& T : *List) I.Tags.Add(T->AsString());
  if (O->TryGetArrayField(TEXT("options"), List)) for (const auto& T : *List) I.Options.Add(T->AsString());
  if (!I.Id.IsEmpty() && !I.Price.IsEmpty()) Out.Items.Add(MoveTemp(I));
 }
 const TArray<TSharedPtr<FJsonValue>>* Jobs = nullptr;
 if (Root->TryGetArrayField(TEXT("jobs"), Jobs)) for (const auto& Row : *Jobs) {
  const TSharedPtr<FJsonObject> O = Row->AsObject(); if (!O) continue;
  FOKStreetCatalogJob J; double V = 95;
  O->TryGetStringField(TEXT("id"), J.Id); O->TryGetStringField(TEXT("place"), J.Place); O->TryGetStringField(TEXT("title"), J.Title);
  if (O->TryGetNumberField(TEXT("wage"), V)) J.Wage = int32(V); O->TryGetBoolField(TEXT("student_only"), J.StudentOnly);
  Ranges(O, TEXT("shifts"), J.Shifts);
  if (!J.Id.IsEmpty()) Out.Jobs.Add(J);
 }
 if (Out.Items.IsEmpty()) Out.Error = TEXT("catalog has no items: ") + Path;
 return Out;
}

inline TArray<const FOKStreetCatalogItem*> OKStreetItemsAt(const FOKStreetCatalog& C, const FString& Place, int32 Month) {
 TArray<const FOKStreetCatalogItem*> Out;
 for (const auto& I : C.Items) if (I.Place == Place && (I.Season.IsEmpty() || I.Season.Contains(Month))) Out.Add(&I);
 return Out;
}

// Tastes, who pays and gifts are game rules (Tools/ok-rules catalog.rs / shop.rs).

inline int32 OKStreetProfileInt(const TSharedPtr<FJsonObject>& Profile, const TCHAR* Section, const TCHAR* Field, int32 Default) {
 const TSharedPtr<FJsonObject>* S = nullptr; double V = Default;
 if (Profile.IsValid() && Profile->TryGetObjectField(Section, S)) (*S)->TryGetNumberField(Field, V);
 return int32(V);
}

