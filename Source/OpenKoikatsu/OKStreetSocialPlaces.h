#pragma once
#include "OKStreetSocialRules.h"
#include "OKStreetSocialProperty.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Street places the player and a girl can go to together, and the built-in activities free talk can start.
//
// Places are modelled at runtime from the cooked StreetCube/StreetCylinder meshes and the 12 StreetMaterial
// colours (no re-cook). Solid parts get collision, so the player sweep, the companion follow and route
// planning avoid them; NPC route walking never goes near them. School, library and playground stand on an
// extension east of the cooked ground (x 3200..6400) that is spawned with them.
//
// Units are cm; +X is north (up on the minimap), +Y is east.

struct FOKStreetPlacePart {
 EOKStreetMesh Mesh = EOKStreetMesh::Cube;
 EOKStreetMaterial Material = EOKStreetMaterial::Cream;
 FVector Center = FVector::ZeroVector;
 FVector Size = FVector(100);
 bool Solid = false;
 bool Hidden = false; // collision only (the place has a model)
};

// A place model built by Tools/street_models/build_street_models.py (Blender) as .okmesh: coloured
// sections in local cm with the front toward +X, collision boxes and the door spot.
struct FOKStreetModelSection {
 FLinearColor Color = FLinearColor::White; // sRGB from the file, converted
 uint8 StreetMaterial = 3;
 bool UseStreetMaterial = false;
 bool Roof = false; // hidden while the player is inside
 uint8 Level = 0; // floor of a multi-storey building (1 = ground); 0 = always shown
 TArray<FVector> Positions, Normals;
 TArray<FVector2D> UV;
 TArray<int32> Indices;
};
struct FOKStreetModelAnchor {
 FString Name;
 FVector Position = FVector::ZeroVector;
 float Yaw = 0.f;
};
struct FOKStreetPlaceModel {
 TArray<FOKStreetModelSection> Sections;
 TArray<FBox> Boxes;
 FVector Door = FVector::ZeroVector;
 TArray<FBox> Rooms; // walkable interiors (v2)
 TArray<FOKStreetModelAnchor> Anchors; // activity spots (v2)
};

inline FString OKStreetPlaceModelDir() {
 const FString Content = FPaths::ProjectContentDir() / TEXT("IndependentResources/StreetSocialData/Models");
 if (FPaths::DirectoryExists(Content)) return Content;
 const FString Builtin = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_BUILTIN_RESOURCES"));
 if (!Builtin.IsEmpty() && FPaths::DirectoryExists(FPaths::GetPath(Builtin) / TEXT("StreetSocialData/Models"))) return FPaths::GetPath(Builtin) / TEXT("StreetSocialData/Models");
 return FPaths::ProjectDir() / TEXT("Content/IndependentResources/StreetSocialData/Models");
}

inline TSharedPtr<const FOKStreetPlaceModel> OKStreetLoadPlaceModel(const FString& Path, FString& Error) {
 TArray<uint8> Bytes;
 if (!FFileHelper::LoadFileToArray(Bytes, *Path)) { Error = TEXT("missing ") + Path; return nullptr; }
 int64 At = 0;
 auto Need = [&](int64 N) { return At + N <= Bytes.Num(); };
 auto U32 = [&]() { uint32 V = 0; if (Need(4)) FMemory::Memcpy(&V, Bytes.GetData() + At, 4); At += 4; return V; };
 auto F32 = [&]() { float V = 0; if (Need(4)) FMemory::Memcpy(&V, Bytes.GetData() + At, 4); At += 4; return V; };
 if (Bytes.Num() < 12 || FMemory::Memcmp(Bytes.GetData(), "OKPM", 4)) { Error = TEXT("not an okmesh: ") + Path; return nullptr; }
 At = 4;
 const uint32 Version = U32();
 if (Version != 1 && Version != 2) { Error = TEXT("unsupported okmesh version: ") + Path; return nullptr; }
 auto Model = MakeShared<FOKStreetPlaceModel>();
 const uint32 Count = U32();
 if (Count > 256) { Error = TEXT("too many sections: ") + Path; return nullptr; }
 for (uint32 S = 0; S < Count; ++S) {
  FOKStreetModelSection& Sec = Model->Sections.AddDefaulted_GetRef();
  const float R = F32(), G = F32(), B = F32();
  Sec.Color = FLinearColor::FromSRGBColor(FColor(uint8(FMath::Clamp(R, 0.f, 1.f) * 255), uint8(FMath::Clamp(G, 0.f, 1.f) * 255), uint8(FMath::Clamp(B, 0.f, 1.f) * 255)));
  Sec.StreetMaterial = uint8(FMath::Min<uint32>(U32(), 11));
  const uint32 Flags = U32();
  Sec.UseStreetMaterial = (Flags & 1) != 0;
  Sec.Roof = (Flags & 2) != 0;
  Sec.Level = uint8((Flags >> 8) & 0xFF);
  const uint32 Vertices = U32(), Indices = U32();
  if (Vertices > 4000000 || Indices > 12000000 || !Need(int64(Vertices) * 32 + int64(Indices) * 4)) { Error = TEXT("truncated okmesh: ") + Path; return nullptr; }
  Sec.Positions.SetNumUninitialized(Vertices);Sec.Normals.SetNumUninitialized(Vertices);Sec.UV.SetNumUninitialized(Vertices);Sec.Indices.SetNumUninitialized(Indices);
  for (uint32 V = 0; V < Vertices; ++V) { const float X = F32(), Y = F32(), Z = F32(); Sec.Positions[V] = FVector(X, Y, Z); }
  for (uint32 V = 0; V < Vertices; ++V) { const float X = F32(), Y = F32(), Z = F32(); Sec.Normals[V] = FVector(X, Y, Z); }
  for (uint32 V = 0; V < Vertices; ++V) { const float X = F32(), Y = F32(); Sec.UV[V] = FVector2D(X, Y); }
  for (uint32 I = 0; I < Indices; ++I) { const uint32 Index = U32(); if (Index >= Vertices) { Error = TEXT("bad okmesh index: ") + Path; return nullptr; } Sec.Indices[I] = int32(Index); }
 }
 const uint32 Boxes = U32();
 if (Boxes > 1024 || !Need(int64(Boxes) * 24 + 12)) { Error = TEXT("truncated okmesh boxes: ") + Path; return nullptr; }
 for (uint32 Box = 0; Box < Boxes; ++Box) {
  // One read per statement: the order of arguments in a call is unspecified (MSVC reads right to left).
  const float CX = F32(), CY = F32(), CZ = F32(), SX = F32(), SY = F32(), SZ = F32();
  const FVector C(CX, CY, CZ), S(SX, SY, SZ);
  Model->Boxes.Add(FBox(C - S * .5f, C + S * .5f));
 }
 const float DX = F32(), DY = F32(), DZ = F32();
 Model->Door = FVector(DX, DY, DZ);
 if (Version >= 2) {
  const uint32 Rooms = U32();
  if (Rooms > 64 || !Need(int64(Rooms) * 24 + 4)) { Error = TEXT("truncated okmesh rooms: ") + Path; return nullptr; }
  for (uint32 R = 0; R < Rooms; ++R) {
   const float CX = F32(), CY = F32(), CZ = F32(), SX = F32(), SY = F32(), SZ = F32();
   const FVector C(CX, CY, CZ), S(SX, SY, SZ);
   Model->Rooms.Add(FBox(C - S * .5f, C + S * .5f));
  }
  const uint32 Anchors = U32();
  if (Anchors > 64) { Error = TEXT("too many okmesh anchors: ") + Path; return nullptr; }
  for (uint32 A = 0; A < Anchors; ++A) {
   const uint32 Length = U32();
   if (Length > 64 || !Need(int64(Length) + 16)) { Error = TEXT("truncated okmesh anchor: ") + Path; return nullptr; }
   const FUTF8ToTCHAR Name(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + At), Length);
   FOKStreetModelAnchor& Anchor = Model->Anchors.AddDefaulted_GetRef();
   Anchor.Name = FString(Name.Length(), Name.Get());
   At += Length;
   const float AX = F32(), AY = F32(), AZ = F32(), Yaw = F32();
   Anchor.Position = FVector(AX, AY, AZ);Anchor.Yaw = Yaw;
  }
 }
 return Model;
}

// Fill light for the place models: a share of the sun intensity, with a floor so they stay readable at night.
inline constexpr float OKStreetPlaceFillShare = .45f, OKStreetPlaceFillNight = .3f;
// Soine (sleeping side by side) root offset in the bed's frame, and how far her onsen sitting pose is lifted
// so she sits on the pool's step instead of its floor (dev overrides: -StreetSocialSoineShiftX/Y=, -StreetSocialOnsenLift=).
inline constexpr float OKStreetSoineShiftX = 0.f, OKStreetSoineShiftY = -50.f, OKStreetOnsenLift = 30.f;

struct FOKStreetPlace {
 const TCHAR* Id = TEXT("");
 const TCHAR* Name = TEXT("");
 const TCHAR* About = TEXT(""); // what the place is, for the model
 const TCHAR* ZhName = nullptr; const TCHAR* ZhAbout = nullptr; // the data's own (Traditional Chinese), kept when the language swaps Name/About
 FVector Door = FVector::ZeroVector; // street-side spot where the pair arrives and activities happen
 TArray<FOKStreetPlacePart> Parts;
 // With a model: where it stands and its yaw (the model's front +X turns to face the street).
 FVector Origin = FVector::ZeroVector;
 float Yaw = 0.f;
 TSharedPtr<const FOKStreetPlaceModel> Model;
 TArray<FBox> Rooms; // world, from the model
 TArray<int32> RoomLevels; // floor of each room (1 = ground), from the rooms' heights
 TArray<FOKStreetModelAnchor> Anchors; // world positions and yaw
};

// The floor (1-based) of the place's room the point stands in, or 0 when outside. 3D: an upper floor's room
// only counts at its height.
inline int32 OKStreetPlaceFloor(const FOKStreetPlace& Place, const FVector& Point) {
 for (int32 I = 0; I < Place.Rooms.Num(); ++I) {
  const FBox& Room = Place.Rooms[I];
  if (Point.X >= Room.Min.X && Point.X <= Room.Max.X && Point.Y >= Room.Min.Y && Point.Y <= Room.Max.Y && Point.Z >= Room.Min.Z - 60.f && Point.Z < Room.Max.Z - 60.f)
   return Place.RoomLevels.IsValidIndex(I) ? Place.RoomLevels[I] : 1;
 }
 return 0;
}

// The nearest bed (anchors named "bed...") within Range of a point, skipping one; with its place.
inline const FOKStreetModelAnchor* OKStreetNearestBed(const FVector& At, float Range, const FOKStreetModelAnchor* Except, const FOKStreetPlace** Where = nullptr);

// Elevator stops of a multi-storey place: anchors named "liftK:label", ordered by floor.
inline TArray<const FOKStreetModelAnchor*> OKStreetPlaceLifts(const FOKStreetPlace& Place) {
 TArray<const FOKStreetModelAnchor*> Out;
 for (const auto& A : Place.Anchors) if (A.Name.StartsWith(TEXT("lift"))) Out.Add(&A);
 Out.Sort([](const FOKStreetModelAnchor& A, const FOKStreetModelAnchor& B) { return A.Position.Z < B.Position.Z; });
 return Out;
}
// A stop's label in the game's language (the models keep the data's own Chinese; filled when the language changes).
inline TMap<FString, FString>& OKStreetLiftLabelTexts() { static TMap<FString, FString> Texts; return Texts; }
inline FString OKStreetLiftLabel(const FOKStreetModelAnchor& Lift) {
 FString Left, Right;
 const FString Own = Lift.Name.Split(TEXT(":"), &Left, &Right) ? Right : Lift.Name;
 const FString* Text = OKStreetLiftLabelTexts().Find(Own);
 return Text ? *Text : Own;
}

inline const FOKStreetModelAnchor* OKStreetPlaceAnchor(const FOKStreetPlace& Place, const TCHAR* Name) {
 if (!Name || !*Name) return nullptr;
 // "pond|fountain": the first the model has (an activity names the spot a new scene will add, and an older one till then).
 TArray<FString> Names; FString(Name).ParseIntoArray(Names, TEXT("|"));
 for (const FString& N : Names) if (const FOKStreetModelAnchor* A = Place.Anchors.FindByPredicate([&N](const FOKStreetModelAnchor& X) { return X.Name == N; })) return A;
 return nullptr;
}

// Whether a world point stands in one of the place's rooms (2D).
inline bool OKStreetInsidePlace(const FOKStreetPlace& Place, const FVector& Point) {
 for (const FBox& Room : Place.Rooms) if (Point.X >= Room.Min.X && Point.X <= Room.Max.X && Point.Y >= Room.Min.Y && Point.Y <= Room.Max.Y && Point.Z >= Room.Min.Z - 60.f && Point.Z < Room.Max.Z - 60.f) return true;
 return false;
}

enum class EOKStreetActivityKind : uint8 { Pair, Photo, Facility, Walk, Restroom, Sleep, Solo };

struct FOKStreetActivity {
 const TCHAR* Id;
 const TCHAR* Name;
 const TCHAR* Place; // place id, or empty: done where the pair is standing
 EOKStreetActivityKind Kind;
 int32 Code; // BeginPair kind (1 seated, 2 drink, 3/4 splash, 5 meal, 6/7 watermelon, 8 phone, 9 bath, 10 read, 11 seated on the place's own benches) or BeginFacility kind
 const TCHAR* Anchor = nullptr; // spot inside the place model where it happens (the pair walks in and faces its way)
 // Original-game curves for this place (venue.manifest.json): her clip (seated variants keep the seated benches;
 // Solo plays it standing beside him) and, for Solo, his own (else he just stands with her).
 const TCHAR* FemaleClip = nullptr;
 const TCHAR* MaleClip = nullptr;
 const TCHAR* ZhName = nullptr; // the data's own name, kept when the language swaps Name
};

// The activities come from the rules (rules/activities.toml, activities.rs `activities_table`), read with the other
// tables (OKRulesReadTables) and again when the language changes. The rows' text lives in OKStreetActivityTexts so the
// TCHAR pointers stay valid; a reload keeps the old text alive (a pointer taken before it still reads).
struct FOKStreetActivityText { FString Id, Name, ZhName, Place, Anchor, FemaleClip, MaleClip; };
inline TArray<TUniquePtr<FOKStreetActivityText>>& OKStreetActivityTexts() { static TArray<TUniquePtr<FOKStreetActivityText>> Texts; return Texts; }
inline TArray<FOKStreetActivity>& OKStreetActivitiesMutable() { static TArray<FOKStreetActivity> All; return All; }
inline const TArray<FOKStreetActivity>& OKStreetActivities() { return OKStreetActivitiesMutable(); }

inline const FOKStreetActivity* OKStreetFindActivity(const FString& Id) {
 for (const auto& A : OKStreetActivities()) if (Id == A.Id) return &A;
 return nullptr;
}
// What an activity id is called in a sentence ("他和她一起去%s", her memory): the menu name without its note in
// brackets or a leading 一起 ("一起散步（你帶路）" -> "散步"); an unknown id stays as it is.
inline const FOKStreetPlace* OKStreetFindPlace(const FString& Id); // below
inline FString OKStreetActivityName(const FString& Id) {
 // "stroll" (walking around nearby) is no menu activity: said as the walk ("went to stroll" in the rumours).
 if (Id == TEXT("stroll")) return OKStreetActivityName(TEXT("walk"));
 const FOKStreetActivity* A = OKStreetFindActivity(Id);
 // Going somewhere ("go:playground", or a bare place id): the place's name, never the id (a rumour said "went to
 // go:playground" and the listener made it a watermelon game there).
 if (!A) { const FOKStreetPlace* P = OKStreetFindPlace(Id.StartsWith(TEXT("go:")) ? Id.Mid(3) : Id); return P ? FString(P->Name) : Id; }
 FString Name = A->Name; int32 Bracket = INDEX_NONE; if (Name.FindChar(TCHAR(0xFF08), Bracket)) Name.LeftInline(Bracket);
 Name.RemoveFromStart(TEXT("就地")); Name.RemoveFromStart(TEXT("一起")); // "就地坐下聊天" -> "坐下聊天"
 // "在学生餐厅一起吃饭" -> "学生餐厅吃饭": after "一起去" the place comes first ("一起去在学生餐厅一起吃饭" in the rumours).
 if (Name.RemoveFromStart(TEXT("在"))) { const int32 Together = Name.Find(TEXT("一起")); if (Together != INDEX_NONE) Name.RemoveAt(Together, 2); }
 Name.TrimStartAndEndInline(); return Name.IsEmpty() ? FString(A->Name) : Name;
}
// The facility activity (restaurant room, hotel room, spa) of a facility index: its Code.
inline const FOKStreetActivity* OKStreetFacilityActivity(int32 Facility) {
 for (const auto& A : OKStreetActivities()) if (A.Kind == EOKStreetActivityKind::Facility && A.Code == Facility) return &A;
 return nullptr;
}

// A boxy building facing `Facing` (unit, along X or Y): body, roof slab, door, sign band, awning, windows.
inline void OKStreetBuilding(FOKStreetPlace& P, FVector2D Center, FVector Size, FVector2D Facing, EOKStreetMaterial Body, EOKStreetMaterial Roof, EOKStreetMaterial Sign, bool TwoFloors) {
 auto Add = [&P](EOKStreetMesh Mesh, EOKStreetMaterial M, FVector C, FVector S, bool Solid) {
  FOKStreetPlacePart Part;Part.Mesh = Mesh;Part.Material = M;Part.Center = C;Part.Size = S;Part.Solid = Solid;P.Parts.Add(Part);
 };
 const FVector Base(Center.X, Center.Y, 0);
 Add(EOKStreetMesh::Cube, Body, Base + FVector(0, 0, Size.Z * .5f), Size, true);
 Add(EOKStreetMesh::Cube, Roof, Base + FVector(0, 0, Size.Z + 15), FVector(Size.X + 60, Size.Y + 60, 30), false);
 const bool AlongX = FMath::Abs(Facing.X) > FMath::Abs(Facing.Y);
 const FVector Normal(Facing.X, Facing.Y, 0);
 const FVector Side = AlongX ? FVector(0, 1, 0) : FVector(1, 0, 0);
 const float Half = (AlongX ? Size.X : Size.Y) * .5f, Width = AlongX ? Size.Y : Size.X;
 auto Face = [&](EOKStreetMaterial M, float Along, float Z, float W, float H, float Depth) {
  const FVector C = Base + Normal * (Half + Depth * .5f) + Side * Along + FVector(0, 0, Z);
  Add(EOKStreetMesh::Cube, M, C, AlongX ? FVector(Depth, W, H) : FVector(W, Depth, H), false);
 };
 Face(EOKStreetMaterial::Wood, 0, 115, 170, 230, 10); // door
 Face(Sign, 0, Size.Z - 55, Width * .72f, 56, 16); // sign band
 Face(EOKStreetMaterial::Stripe, 0, 262, 260, 14, 110); // awning over the door
 for (const float Along : {-Width * .32f, Width * .32f}) Face(EOKStreetMaterial::Glass, Along, 150, Width * .2f, 120, 6);
 if (TwoFloors) for (const float Along : {-Width * .32f, 0.f, Width * .32f}) Face(EOKStreetMaterial::Glass, Along, Size.Z - 160, Width * .18f, 90, 6);
 P.Door = Base + Normal * (Half + 260);
}

// Vacant lots from property.json become places (navigation, PlaceHere, a heroine's workplace): the lot's ground is
// its one room; what stands on it is spawned at runtime (OKStreetSocialCompanyWorld.inl). Id and name are TCHAR
// pointers in FOKStreetPlace, so their strings are kept for the life of the program.
inline void OKStreetAppendPropertyLots(TArray<FOKStreetPlace>& Out) {
 static TArray<TUniquePtr<FString>> Strings;
 auto Keep = [](const FString& Text) -> const TCHAR* { Strings.Add(MakeUnique<FString>(Text)); return **Strings.Last(); };
 for (const FOKStreetUnitDef& U : OKStreetProperty().Units) {
  if (!U.bLot) continue;
  FOKStreetPlace& P = Out.AddDefaulted_GetRef();
  P.Id = Keep(U.Id); P.Name = Keep(U.Name.IsEmpty() ? U.Id : U.Name); P.About = Keep(TEXT("一塊空地，可以租或買下來蓋房子"));
  const bool AlongX = FMath::IsNearlyEqual(FMath::Abs(FRotator::NormalizeAxis(U.Yaw)), 0.f) || FMath::IsNearlyEqual(FMath::Abs(FRotator::NormalizeAxis(U.Yaw)), 180.f);
  const FVector Half(AlongX ? U.Size.Y * 0.5f : U.Size.X * 0.5f, AlongX ? U.Size.X * 0.5f : U.Size.Y * 0.5f, 0.f);
  const FVector Center(U.Center.X, U.Center.Y, 0.f), Front = FRotator(0, U.Yaw, 0).Vector();
  P.Door = Center + Front * ((AlongX ? Half.X : Half.Y) + 150.f);
  P.Rooms.Add(FBox(Center - FVector(Half.X, Half.Y, 0.f), Center + FVector(Half.X, Half.Y, 450.f))); P.RoomLevels.Add(1);
  // Spots for whoever works there: a counter by the front, desks behind it.
  auto Anchor = [&](const TCHAR* Name, const FVector& Local, float Yaw) { FOKStreetModelAnchor A; A.Name = Name; A.Position = Center + FRotator(0, U.Yaw, 0).RotateVector(Local); A.Yaw = FRotator::NormalizeAxis(U.Yaw + Yaw); P.Anchors.Add(A); };
  // (Anchor yaw is a character rotation: it faces yaw + 90, so -90 faces the street.)
  Anchor(TEXT("counter"), FVector(240, 0, 0), -90.f); Anchor(TEXT("seat"), FVector(-60, -200, 0), -90.f); Anchor(TEXT("seat2"), FVector(-60, 200, 0), -90.f); Anchor(TEXT("table"), FVector(0, 0, 0), -90.f);
 }
}

inline const TArray<FOKStreetPlace>& OKStreetPlaces() {
 static const TArray<FOKStreetPlace> All = [] {
  using M = EOKStreetMaterial;
  TArray<FOKStreetPlace> Out;
  auto Make = [&Out](const TCHAR* Id, const TCHAR* Name, const TCHAR* About) -> FOKStreetPlace& {
   FOKStreetPlace& P = Out.AddDefaulted_GetRef();P.Id = Id;P.Name = Name;P.About = About;return P;
  };
  OKStreetBuilding(Make(TEXT("restaurant"), TEXT("餐廳"), TEXT("家庭式餐廳，門口有座位，也有安靜的包廂")), FVector2D(2500, -2300), FVector(900, 800, 420), FVector2D(-1, 0), M::Cream, M::Terracotta, M::Leaf, false);
  OKStreetBuilding(Make(TEXT("snack"), TEXT("點心店"), TEXT("賣飲料和甜點的小店，可以外帶")), FVector2D(2500, 2300), FVector(900, 800, 380), FVector2D(-1, 0), M::Stripe, M::Cream, M::Lamp, false);
  OKStreetBuilding(Make(TEXT("bookstore"), TEXT("書局"), TEXT("文具和書的小書局，有漫畫和雜誌")), FVector2D(-2500, -2200), FVector(900, 900, 400), FVector2D(1, 0), M::Wood, M::Stone, M::Glass, false);
  OKStreetBuilding(Make(TEXT("lodge"), TEXT("會館"), TEXT("休閒會館，有休息房間和精油護理室")), FVector2D(-2500, 1900), FVector(900, 1100, 460), FVector2D(1, 0), M::Terracotta, M::Stone, M::Hedge, true);
  OKStreetBuilding(Make(TEXT("school"), TEXT("舊校舍（社團大樓）"), TEXT("大學搬到北邊新校園後留下的舊校舍，現在是社團活動和自習用的大樓：一樓教室、二樓自習教室、三樓音樂教室")), FVector2D(5400, -1700), FVector(1400, 1600, 600), FVector2D(-1, 0), M::Cream, M::Terracotta, M::Stripe, true);
  OKStreetBuilding(Make(TEXT("library"), TEXT("圖書館"), TEXT("安靜的市立圖書館，裡面要小聲說話")), FVector2D(5400, 300), FVector(1200, 1000, 500), FVector2D(-1, 0), M::Stone, M::Bark, M::Wood, true);
  {
   // Open field: grass, a running-track border and two goals (none of it blocks walking).
   FOKStreetPlace& P = Make(TEXT("playground"), TEXT("操場"), TEXT("學校旁的操場，有草地和跑道，適合玩水和打西瓜"));
   auto Add = [&P](EOKStreetMesh Mesh, EOKStreetMaterial Mat, FVector C, FVector S) {
    FOKStreetPlacePart Part;Part.Mesh = Mesh;Part.Material = Mat;Part.Center = C;Part.Size = S;P.Parts.Add(Part);
   };
   Add(EOKStreetMesh::Cube, M::Leaf, FVector(4950, 1900, 1), FVector(1700, 1300, 2));
   for (const float Y : {1220.f, 2580.f}) Add(EOKStreetMesh::Cube, M::Terracotta, FVector(4950, Y, 1.5f), FVector(1800, 70, 3));
   for (const float X : {4070.f, 5830.f}) Add(EOKStreetMesh::Cube, M::Terracotta, FVector(X, 1900, 1.5f), FVector(70, 1430, 3));
   for (const float X : {4250.f, 5650.f}) {
    for (const float Y : {1780.f, 2020.f}) Add(EOKStreetMesh::Cylinder, M::Stone, FVector(X, Y, 100), FVector(10, 10, 200));
    Add(EOKStreetMesh::Cube, M::Stone, FVector(X, 1900, 200), FVector(10, 250, 10));
   }
   P.Door = FVector(4600, 1900, 0);
  }
  // South extension (x -6400..-3200), shop fronts facing north toward the street.
  OKStreetBuilding(Make(TEXT("drinkshop"), TEXT("飲料店"), TEXT("手搖飲料店，有珍珠奶茶、水果茶和冰沙")), FVector2D(-4300, -1800), FVector(800, 900, 380), FVector2D(1, 0), M::Leaf, M::Cream, M::Lamp, false);
  OKStreetBuilding(Make(TEXT("fruitshop"), TEXT("水果店"), TEXT("賣當季水果的水果行，門口擺著一箱箱水果，也有切好的水果盒")), FVector2D(-4300, 0), FVector(800, 900, 360), FVector2D(1, 0), M::Cream, M::Leaf, M::Terracotta, false);
  OKStreetBuilding(Make(TEXT("petshop"), TEXT("寵物店"), TEXT("寵物店，櫥窗裡有小狗小貓，可以隔著玻璃看牠們玩")), FVector2D(-4300, 1800), FVector(800, 900, 400), FVector2D(1, 0), M::Stripe, M::Terracotta, M::Glass, false);
  // Behind the shops: the dormitories, with a walkway between.
  OKStreetBuilding(Make(TEXT("girls_dorm"), TEXT("校外女生宿舍"), TEXT("星街科技大學在街區裡的校外女生宿舍，走到校門約五分鐘；四層樓有陽台，一樓是交誼廳（訪客可以進），樓上房間男生不能上去")), FVector2D(-5750, -1700), FVector(900, 1500, 1260), FVector2D(1, 0), M::Cream, M::Terracotta, M::Stripe, true);
  OKStreetBuilding(Make(TEXT("boys_dorm"), TEXT("校外男生宿舍"), TEXT("星街科技大學在街區裡的校外男生宿舍，走到校門約五分鐘；門口有腳踏車停車架")), FVector2D(-5750, 1700), FVector(900, 1500, 1260), FVector2D(1, 0), M::Cream, M::Stone, M::Stripe, true);
  // West resort extension (y -6400..-2800), fronts facing the street (+Y).
  OKStreetBuilding(Make(TEXT("hotel"), TEXT("溫泉SPA飯店"), TEXT("高級溫泉SPA飯店，門口有泳池和椰子樹，裡面有SPA護理室和客房")), FVector2D(1600, -5200), FVector(1300, 1000, 1900), FVector2D(0, 1), M::Cream, M::Stone, M::Glass, true);
  OKStreetBuilding(Make(TEXT("minshuku"), TEXT("民宿"), TEXT("日式木造民宿，有緣側和小庭院，可以在房間休息")), FVector2D(-500, -4800), FVector(1000, 900, 320), FVector2D(0, 1), M::Cream, M::Wood, M::Wood, false);
  OKStreetBuilding(Make(TEXT("onsen"), TEXT("露天溫泉"), TEXT("露天溫泉，竹籬笆圍著冒熱氣的岩石浴池，旁邊有更衣小屋和石燈籠")), FVector2D(-2300, -5100), FVector(1400, 1500, 300), FVector2D(0, 1), M::Wood, M::Stone, M::Wood, false);
  // East university-town extension (y 2800..7800), fronts facing the street (-Y).
  OKStreetBuilding(Make(TEXT("conveni"), TEXT("便利商店"), TEXT("24小時便利商店，有冰箱飲料、零食和熱食")), FVector2D(-2300, 4100), FVector(1000, 800, 340), FVector2D(0, -1), M::Cream, M::Stone, M::Leaf, false);
  // Campus (Docs/STREET_SOCIAL_CAMPUS_PLAN.md): one five-storey building per college, north of the school side, fronts
  // facing south toward the street. Rows at x 8800 / 12000 / 15200.
  {struct FCampus{const TCHAR* Id;const TCHAR* Name;const TCHAR* About;FVector2D At;};
   const FCampus Campus[]={
    {TEXT("campus_t1"),TEXT("第一教學大樓 T1（共同教育委員會）"),TEXT("通識課的教室大樓，五層樓，1 樓有通識教育中心辦公室"),FVector2D(8800,-4000)},
    {TEXT("campus_rb"),TEXT("綜合研究大樓 RB（產學創新學院）"),TEXT("產學創新學院的大樓，五層樓，1 樓是各所辦公室"),FVector2D(8800,-500)},
    {TEXT("campus_ib"),TEXT("國際大樓 IB（應用科技學院）"),TEXT("應用科技學院的大樓，五層樓，1 樓是各所辦公室"),FVector2D(8800,3000)},
    {TEXT("campus_t4"),TEXT("第四教學大樓 T4（設計學院）"),TEXT("設計學院的大樓，五層樓，1 樓是設計系和建築系的系辦"),FVector2D(12000,-4000)},
    {TEXT("campus_e1"),TEXT("工程一館 E1（工程學院）"),TEXT("工程學院的大樓，五層樓，1 樓是機械、材料、營建、化工系辦"),FVector2D(12000,-500)},
    {TEXT("campus_ma"),TEXT("管理大樓 MA（管理學院）"),TEXT("管理學院的大樓，五層樓，1 樓是工管、企管、資管系辦與財金所辦"),FVector2D(12000,3000)},
    {TEXT("campus_t2"),TEXT("第二教學大樓 T2（人文社會學院）"),TEXT("人文社會學院的大樓，五層樓，1 樓是應外系和數位學習所辦"),FVector2D(15200,-2250)},
    {TEXT("campus_ee"),TEXT("電資館 EE（電資學院）"),TEXT("電資學院的大樓，五層樓，1 樓是電子、電機、資工系辦與光電所辦"),FVector2D(15200,1250)},
    {TEXT("campus_ad"),TEXT("行政大樓 AD"),TEXT("校門旁的行政大樓，五層樓，1 樓是註冊組（入學考）、教務處（轉系）、學務處（休學、復學）"),FVector2D(8800,-7600)}};
   for(const auto& C:Campus)OKStreetBuilding(Make(C.Id,C.Name,C.About),C.At,FVector(1400,2800,1700),FVector2D(-1,0),M::Cream,M::Stone,M::Glass,true);}
  OKStreetBuilding(Make(TEXT("campus_gate"), TEXT("星街科技大學 校門"), TEXT("大學的正門，石柱上掛著校名，旁邊有警衛亭和校園地圖")), FVector2D(6600, -3200), FVector(1200, 2200, 600), FVector2D(-1, 0), M::Stone, M::Stone, M::Stone, false);
  OKStreetBuilding(Make(TEXT("campus_sports"), TEXT("大學運動場"), TEXT("校園西側的運動場：跑道圍著足球場，旁邊有籃球場和看台")), FVector2D(12500, -9800), FVector(3400, 5600, 100), FVector2D(-1, 0), M::Leaf, M::Leaf, M::Leaf, false);
  // Plan §15.1: 星運彩券行, west of the convenience store (the one new place).
  OKStreetBuilding(Make(TEXT("lottery"), TEXT("星運彩券行"), TEXT("便利商店旁的彩券行，可以買星光 6/49、雙星彩、幸運刮刮卡，也能投注競技彩；牆上有開獎和賠率螢幕，門口有紅榜")), FVector2D(-3500, 4050), FVector(700, 700, 340), FVector2D(0, -1), M::Terracotta, M::Stone, M::Lamp, false);
  OKStreetBuilding(Make(TEXT("cafe"), TEXT("咖啡廳"), TEXT("紅磚咖啡廳，有吧台、沙發和小圓桌，適合坐下來慢慢聊")), FVector2D(2300, 4100), FVector(1000, 900, 380), FVector2D(0, -1), M::Terracotta, M::Stone, M::Wood, false);
  OKStreetBuilding(Make(TEXT("lecture"), TEXT("大學講堂"), TEXT("大學的紅磚講堂，門口有鐘塔，裡面是一排排課桌和黑板")), FVector2D(-5000, 4700), FVector(1600, 1400, 600), FVector2D(0, -1), M::Terracotta, M::Stone, M::Cream, true);
  OKStreetBuilding(Make(TEXT("cafeteria"), TEXT("學生餐廳"), TEXT("大學的學生餐廳，有取餐檯和長桌，便宜又大碗")), FVector2D(5000, 4600), FVector(1400, 1200, 420), FVector2D(0, -1), M::Cream, M::Leaf, M::Stripe, false);
  OKStreetBuilding(Make(TEXT("park"), TEXT("中央公園"), TEXT("大學城的中央公園，有噴水池、花圃、長椅和大樹")), FVector2D(0, 4700), FVector(200, 200, 100), FVector2D(0, -1), M::Leaf, M::Leaf, M::Leaf, false);
  OKStreetBuilding(Make(TEXT("shrine"), TEXT("神社"), TEXT("公園後面的小神社，有紅色鳥居和石燈籠，可以參拜許願")), FVector2D(0, 6800), FVector(500, 700, 300), FVector2D(0, -1), M::Wood, M::Leaf, M::Wood, false);
  OKStreetBuilding(Make(TEXT("department"), TEXT("大學城百貨"), TEXT("十層樓的百貨公司，搭電梯上下樓：1F化妝品・服務台、2F女裝、3F男裝、4F鞋包配件、5F家居生活、6F書店文具、7F電器、8F玩具、9F美食街、10F景觀餐廳")), FVector2D(-5300, 7200), FVector(1800, 2000, 4000), FVector2D(0, -1), M::Glass, M::Stone, M::Lamp, true);
  OKStreetBuilding(Make(TEXT("aquarium"), TEXT("海洋水族館"), TEXT("大學城的海洋水族館，有整面牆的大水槽、水母柱和小水槽，大水槽前有長椅")), FVector2D(4800, 6600), FVector(1400, 2000, 600), FVector2D(0, -1), M::Glass, M::Stone, M::Glass, false);
  // Public restrooms by the big public places: separate men's and women's rooms, each with its own door.
  const TCHAR* RestroomAbout = TEXT("公共廁所，男廁和女廁分開，各有自己的入口");
  OKStreetBuilding(Make(TEXT("restroom_park"), TEXT("公廁（中央公園旁）"), RestroomAbout), FVector2D(2300, 5600), FVector(500, 700, 300), FVector2D(-1, 0), M::Cream, M::Leaf, M::Cream, false);
  OKStreetBuilding(Make(TEXT("restroom_campus"), TEXT("公廁（大學講堂旁）"), RestroomAbout), FVector2D(-3500, 5600), FVector(700, 500, 300), FVector2D(0, -1), M::Cream, M::Leaf, M::Cream, false);
  OKStreetBuilding(Make(TEXT("restroom_north"), TEXT("公廁（學校操場旁）"), RestroomAbout), FVector2D(3500, -2300), FVector(500, 700, 300), FVector2D(1, 0), M::Cream, M::Leaf, M::Cream, false);
  OKStreetBuilding(Make(TEXT("restroom_resort"), TEXT("公廁（度假區）"), RestroomAbout), FVector2D(500, -5400), FVector(700, 500, 300), FVector2D(0, 1), M::Cream, M::Leaf, M::Cream, false);
  // Models replace the box buildings: same spot, the model's front (+X) turned to the street.
  struct FPlacement { const TCHAR* Id; FVector Origin; float Yaw; };
  const FPlacement Placements[] = {
   {TEXT("restaurant"), FVector(2500, -2300, 0), 180}, {TEXT("snack"), FVector(2500, 2300, 0), 180},
   {TEXT("bookstore"), FVector(-2500, -2200, 0), 0}, {TEXT("lodge"), FVector(-2500, 1900, 0), 0},
   {TEXT("school"), FVector(5400, -1700, 0), 180}, {TEXT("library"), FVector(5400, 300, 0), 180},
   {TEXT("playground"), FVector(4950, 1900, 0), 0},
   {TEXT("drinkshop"), FVector(-4300, -1800, 0), 0}, {TEXT("fruitshop"), FVector(-4300, 0, 0), 0}, {TEXT("petshop"), FVector(-4300, 1800, 0), 0},
   {TEXT("girls_dorm"), FVector(-5750, -1700, 0), 0}, {TEXT("boys_dorm"), FVector(-5750, 1700, 0), 0},
   {TEXT("hotel"), FVector(1600, -5200, 0), 90}, {TEXT("minshuku"), FVector(-500, -4800, 0), 90}, {TEXT("onsen"), FVector(-2300, -5100, 0), 90},
   {TEXT("conveni"), FVector(-2300, 4100, 0), 270}, {TEXT("lottery"), FVector(-3500, 4050, 0), 270},
   {TEXT("campus_t1"), FVector(8800, -4000, 0), 180}, {TEXT("campus_rb"), FVector(8800, -500, 0), 180}, {TEXT("campus_ib"), FVector(8800, 3000, 0), 180},
   {TEXT("campus_t4"), FVector(12000, -4000, 0), 180}, {TEXT("campus_e1"), FVector(12000, -500, 0), 180}, {TEXT("campus_ma"), FVector(12000, 3000, 0), 180},
   {TEXT("campus_t2"), FVector(15200, -2250, 0), 180}, {TEXT("campus_ee"), FVector(15200, 1250, 0), 180}, {TEXT("campus_ad"), FVector(8800, -7600, 0), 180}, {TEXT("campus_gate"), FVector(6600, -3200, 0), 180}, {TEXT("campus_sports"), FVector(12500, -9800, 0), 180}, {TEXT("cafe"), FVector(2300, 4100, 0), 270}, {TEXT("lecture"), FVector(-5000, 4700, 0), 270},
   {TEXT("cafeteria"), FVector(5000, 4600, 0), 270}, {TEXT("park"), FVector(0, 4700, 0), 270}, {TEXT("shrine"), FVector(0, 6800, 0), 270},
   {TEXT("aquarium"), FVector(4800, 6600, 0), 270}, {TEXT("department"), FVector(-5300, 7200, 0), 270}, {TEXT("restroom_park"), FVector(2300, 5600, 0), 180}, {TEXT("restroom_campus"), FVector(-3500, 5600, 0), 270},
   {TEXT("restroom_north"), FVector(3500, -2300, 0), 0}, {TEXT("restroom_resort"), FVector(500, -5400, 0), 90},
  };
  const FString Dir = OKStreetPlaceModelDir();
  for (const FPlacement& At : Placements) {
   FOKStreetPlace* P = Out.FindByPredicate([&](const FOKStreetPlace& X) { return FCString::Strcmp(X.Id, At.Id) == 0; });
   if (!P) continue;
   FString Error;
   const auto Model = OKStreetLoadPlaceModel(Dir / FString(At.Id) + TEXT(".okmesh"), Error);
   if (!Model) { UE_LOG(LogTemp, Warning, TEXT("Street place model: %s (keeping the box building)"), *Error); continue; }
   const FTransform T(FRotator(0, At.Yaw, 0), At.Origin);
   P->Model = Model;P->Origin = At.Origin;P->Yaw = At.Yaw;P->Door = T.TransformPosition(Model->Door);P->Parts.Reset();
   for (const FBox& Room : Model->Rooms) P->Rooms.Add(Room.TransformBy(T));
   // An open-air place (park, shrine, playground) has no rooms in its model: its ground is the model's footprint, so
   // standing at the park's bench is "in the park" (PlaceHere was empty there, and so was every rule that asks).
   if (Model->Rooms.IsEmpty()) {
    FBox Ground(ForceInit);
    for (const FOKStreetModelSection& Sec : Model->Sections) for (const FVector& V : Sec.Positions) Ground += V;
    if (Ground.IsValid) P->Rooms.Add(FBox(FVector(Ground.Min.X, Ground.Min.Y, 0.f), FVector(Ground.Max.X, Ground.Max.Y, 450.f)).TransformBy(T));
   }
   // Floors from the rooms' heights: the lowest is 1.
   TArray<float> Heights;for (const FBox& Room : P->Rooms) Heights.AddUnique(FMath::RoundToFloat(Room.Min.Z));Heights.Sort();
   for (const FBox& Room : P->Rooms) P->RoomLevels.Add(Heights.IndexOfByKey(FMath::RoundToFloat(Room.Min.Z)) + 1);
   for (const auto& Anchor : Model->Anchors) { FOKStreetModelAnchor World = Anchor;World.Position = T.TransformPosition(Anchor.Position);World.Yaw = FRotator::NormalizeAxis(Anchor.Yaw + At.Yaw);P->Anchors.Add(World); }
   // Collision footprints (axis aligned after a quarter-turn yaw).
   for (const FBox& Box : Model->Boxes) {
    const FBox World = Box.TransformBy(T);
    FOKStreetPlacePart Part;Part.Center = World.GetCenter();Part.Size = World.GetSize();Part.Solid = true;Part.Hidden = true;P->Parts.Add(Part);
   }
  }
  OKStreetAppendPropertyLots(Out); // his company's vacant lots are places too (OKStreetSocialProperty.h data)
  return Out;
 }();
 return All;
}

inline const FOKStreetPlace* OKStreetFindPlace(const FString& Id) {
 for (const auto& P : OKStreetPlaces()) if (Id == P.Id) return &P;
 return nullptr;
}

// Extra route points for walking to places (never added to NPC routes): the gap east of the plaza, the
// east extension's walkway, and every door.
inline TArray<FVector> OKStreetPlaceRoutePoints() {
 TArray<FVector> Points = {FVector(3150, 50, 0), FVector(3150, -1550, 0), FVector(3150, 1650, 0), FVector(3800, -1700, 0), FVector(3800, 300, 0), FVector(3800, 1900, 0),
                           FVector(1750, -1700, 0), FVector(1750, 1750, 0), FVector(-1750, -1650, 0), FVector(-1750, 1300, 0),
                           // South extension: through the gaps beside the south block, then along the shop fronts.
                           FVector(-1750, -1340, 0), FVector(-2500, -1340, 0), FVector(-3400, -1340, 0), FVector(-1750, 990, 0), FVector(-2500, 990, 0), FVector(-3400, 990, 0),
                           FVector(-3450, -1800, 0), FVector(-3450, 0, 0), FVector(-3450, 1800, 0),
                           // Dorm walkway behind the shops, reached through the alleys between them.
                           FVector(-3450, -900, 0), FVector(-5040, -900, 0), FVector(-3450, 900, 0), FVector(-5040, 900, 0), FVector(-5040, -1700, 0), FVector(-5040, 1700, 0),
                           // West resort: past the restaurant terrace or the bookstore, then along y = -3300.
                           FVector(1650, -2400, 0), FVector(1650, -3300, 0), FVector(-1100, -1600, 0), FVector(-1100, -2400, 0), FVector(-1100, -3300, 0),
                           FVector(-2300, -3300, 0), FVector(-500, -3300, 0), FVector(1800, -3300, 0),
                           // East university town: past the snack shop or the lodge, then along y = 3300; the shrine
                           // is reached along the park's east edge (the fountain is in the middle).
                           FVector(1750, 3200, 0), FVector(-1750, 3200, 0), FVector(-5000, 3300, 0), FVector(-2300, 3300, 0), FVector(0, 3300, 0),
                           FVector(1550, 3300, 0), FVector(2300, 3300, 0), FVector(5000, 3300, 0), FVector(1550, 6000, 0),
                           // Aquarium behind the cafeteria (past its east side), the restrooms' approaches.
                           FVector(6100, 3300, 0), FVector(6100, 5600, 0), FVector(1550, 5600, 0), FVector(-3500, 3300, 0), FVector(500, -3300, 0),
                           // Department store behind the lecture hall: up the gap between the hall and the campus restroom.
                           FVector(-4000, 3300, 0), FVector(-4000, 5900, 0),
                           // Campus: in from the school side along y = -3200 or from the east along y = 3300, then the lanes
                           // south of each row of buildings and the gaps between them.
                           FVector(3800, -3200, 0), FVector(7400, -3200, 0), FVector(7400, 3300, 0),
                           FVector(7400, -5800, 0), FVector(7400, -2250, 0), FVector(7400, 1250, 0), FVector(7400, 4800, 0),
                           FVector(10300, -5800, 0), FVector(10300, -2250, 0), FVector(10300, 1250, 0), FVector(10300, 4800, 0),
                           FVector(13500, -5800, 0), FVector(13500, -2250, 0), FVector(13500, 1250, 0), FVector(13500, 4800, 0),
                           FVector(16200, -5800, 0), FVector(16200, -2250, 0), FVector(16200, 1250, 0), FVector(16200, 4800, 0),
                           // Administration building and the sports ground south-west of the classrooms.
                           FVector(7400, -7600, 0), FVector(7400, -9800, 0), FVector(10300, -9800, 0)};
 for (const auto& P : OKStreetPlaces()) Points.Add(P.Door);
 return Points;
}

// Ground for the north (school side) and south (shops) extensions, top at z=0 like the cooked ground.
inline TArray<FOKStreetPlacePart> OKStreetPlaceGround() {
 TArray<FOKStreetPlacePart> Out;
 for (const float X : {4800.f, -4800.f}) {
  FOKStreetPlacePart G;G.Material = EOKStreetMaterial::Checker;G.Center = FVector(X, 0, -20);G.Size = FVector(3200, 5600, 40);Out.Add(G);
 }
 FOKStreetPlacePart West;West.Material = EOKStreetMaterial::Checker;West.Center = FVector(0, -4600, -20);West.Size = FVector(6400, 3600, 40);Out.Add(West);
 FOKStreetPlacePart East;East.Material = EOKStreetMaterial::Checker;East.Center = FVector(0, 5600, -20);East.Size = FVector(12800, 5600, 40);Out.Add(East);
 // The campus north of everything, with lawns between the rows of buildings (visual only).
 FOKStreetPlacePart Campus;Campus.Material = EOKStreetMaterial::Plaza;Campus.Center = FVector(11600, -500, -20);Campus.Size = FVector(10400, 12200, 40);Out.Add(Campus);
 FOKStreetPlacePart South;South.Material = EOKStreetMaterial::Plaza;South.Center = FVector(11300, -9900, -20);South.Size = FVector(9800, 6600, 40);Out.Add(South);
 for (const float X : {10300.f, 13500.f}) for (const float Y : {-4000.f, -500.f, 3000.f}) { FOKStreetPlacePart L;L.Material = EOKStreetMaterial::Leaf;L.Center = FVector(X, Y, 1);L.Size = FVector(900, 2000, 2);Out.Add(L); }
 return Out;
}

// Compass words for the model: +X north, +Y east.
inline FString OKStreetCompass(const FVector& From, const FVector& To) {
 const FVector D = To - From;
 if (D.Size2D() < 300) return OKT(TEXT("ui.compass.here"));
 const float Angle = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)); // 0 = north, 90 = east
 static const TCHAR* Keys[] = {TEXT("ui.compass.n"), TEXT("ui.compass.ne"), TEXT("ui.compass.e"), TEXT("ui.compass.se"), TEXT("ui.compass.s"), TEXT("ui.compass.sw"), TEXT("ui.compass.w"), TEXT("ui.compass.nw")};
 const int32 Index = FMath::RoundToInt(FMath::Fmod(Angle + 360.f, 360.f) / 45.f) % 8;
 return OKT(Keys[Index]);
}

// Stable catalogue for the system prompt (cacheable): places and what can be done there.
inline const FOKStreetModelAnchor* OKStreetNearestBed(const FVector& At, float Range, const FOKStreetModelAnchor* Except, const FOKStreetPlace** Where) {
 const FOKStreetModelAnchor* Best = nullptr;float BestDistance = Range;
 for (const auto& P : OKStreetPlaces()) for (const auto& A : P.Anchors) {
  if (&A == Except || !A.Name.StartsWith(TEXT("bed")) || FMath::Abs(A.Position.Z - At.Z) > 150.f) continue;
  const float Distance = FVector::Dist2D(A.Position, At);
  if (Distance < BestDistance) { BestDistance = Distance;Best = &A;if (Where) *Where = &P; }
 }
 return Best;
}

// The floor height under a point: the bottom of the place room it stands in, else the street (0).
inline float OKStreetFloorZ(const FVector& At) {
 for (const auto& P : OKStreetPlaces()) for (const FBox& Room : P.Rooms)
  if (At.X >= Room.Min.X && At.X <= Room.Max.X && At.Y >= Room.Min.Y && At.Y <= Room.Max.Y && At.Z >= Room.Min.Z - 60.f && At.Z < Room.Max.Z - 60.f) return Room.Min.Z;
 return 0.f;
}

inline FString OKStreetPlaceCatalogue() {
 FString Out = TEXT("【街區地點】\n");
 for (const auto& P : OKStreetPlaces()) {
  Out += FString::Printf(TEXT("- %s（id: %s）：%s"), P.Name, P.Id, P.About);
  TArray<FString> Here;
  for (const auto& A : OKStreetActivities()) if (FCString::Strcmp(A.Place, P.Id) == 0) Here.Add(A.Name);
  if (!Here.IsEmpty()) Out += TEXT("。可以：") + FString::Join(Here, TEXT("、"));
  Out += TEXT("\n");
 }
 Out += TEXT("【可以一起做的活動】（id：名稱，地點）\n");
 for (const auto& A : OKStreetActivities()) {
  const FOKStreetPlace* P = OKStreetFindPlace(A.Place);
  Out += FString::Printf(TEXT("- %s：%s，%s\n"), A.Id, A.Name, P ? *FString::Printf(TEXT("在%s"), P->Name) : TEXT("就地進行"));
 }
 return Out;
}
