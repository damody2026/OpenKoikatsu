#pragma once
#include "OKStreetSocialText.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Math/RandomStream.h"
#include "Misc/DateTime.h"

// Campus records (Docs/STREET_SOCIAL_CAMPUS_PLAN.md) and the tables the scene shows; the rules are in
// Tools/ok-rules (campus.rs calendar, academics.rs everything else).

struct FOKStreetCollege {
 FString Id;           // e.g. eecs
 FString Name;         // 電資學院
 FString Building;     // place id of its five-storey building, e.g. campus_ee
 FString Code;         // EE (room numbers EE-301)
 FString BuildingName;
};

struct FOKStreetDept {
 FString Id;           // e.g. csie
 FString Name;         // 資訊工程系
 FString College;      // college id
 int32 Bar = 0;        // entrance / transfer exam bar
};

// The tables are game data in the rules (Tools/ok-rules src/academics.rs); the scene copies them when the rules open
// (OKRulesSyncDefaults) for its displays.
inline TArray<FOKStreetCollege>& OKStreetCollegesTable() { static TArray<FOKStreetCollege> All; return All; }
inline TArray<FOKStreetDept>& OKStreetDeptsTable() { static TArray<FOKStreetDept> All; return All; }
inline const TArray<FOKStreetCollege>& OKStreetColleges() { return OKStreetCollegesTable(); }
inline const TArray<FOKStreetDept>& OKStreetDepts() { return OKStreetDeptsTable(); }

inline const FOKStreetDept* OKStreetFindDept(const FString& Id) { for (const auto& D : OKStreetDepts()) if (Id == D.Id) return &D; return nullptr; }
inline const FOKStreetCollege* OKStreetFindCollege(const FString& Id) { for (const auto& C : OKStreetColleges()) if (Id == C.Id) return &C; return nullptr; }
inline const FOKStreetCollege* OKStreetCollegeOfBuilding(const FString& Place) { for (const auto& C : OKStreetColleges()) if (Place == C.Building) return &C; return nullptr; }

// ---------------------------------------------------------------------------------------------------- semesters

enum class EOKStreetTermPhase : uint8 { Break, Classes, Midterm, Final };

struct FOKStreetTerm {
 FString Key;            // "2010-1" (fall) or "2010-2" (spring, academic year 2010)
 FDateTime Start, Midterm, Final, End; // Midterm/Final = Monday of the exam week; End = the Saturday after finals
};

// When terms start, their exam weeks and fee windows are game rules (Tools/ok-rules campus.rs; OKRulesTermAt).

// ---------------------------------------------------------------------------------------------------- records

struct FOKStreetCourse {
 FString Name, Building; // building place id
 int32 Floor = 2, Room = 1, Weekday = 0, Slot = 0; // weekday 0 = Monday; slot 0 morning (09:00–12:00), 1 afternoon (13:30–16:30)
 int32 Held = 0, Attended = 0;
 float Midterm = -1.f, Final = -1.f, Grade = -1.f;
};

struct FOKStreetAcademic {
 FString Dept;                       // department id; empty = never enrolled
 FString Status = TEXT("enrolled");  // enrolled | leave | expelled | none
 FString Term;                       // term key of Courses
 TArray<FOKStreetCourse> Courses;
 TArray<FString> Transcript;         // finished courses: "2010-1|資料結構|72|85%"
 float Intelligence = 55.f;
 FString LeaveUntil;                 // term key when a leave of absence ends
 FString FeesPaidTerm;               // term key whose tuition and dorm fee are paid
 FString ScholarshipTerm;            // term key whose scholarship was settled
};

inline FString OKStreetRoomLabel(const FOKStreetCourse& C) {
 const auto* College = OKStreetCollegeOfBuilding(C.Building);
 return FString::Printf(TEXT("%s-%d%02d"), College ? *College->Code : TEXT("?"), C.Floor, C.Room);
}

inline FString OKStreetSlotLabel(const FOKStreetCourse& C) {
 return OKMsg(TEXT("ui.campus.weekday_slot"), {{TEXT("weekday_name"), OKMsg(TEXT("ui.calendar.weekday_name"), {{TEXT("d"), FMath::Clamp(C.Weekday, 0, 4)}})}, {TEXT("time_slot"), C.Slot == 0 ? TEXT("09:00–12:00") : TEXT("13:30–16:30")}});
}

// Timetables, exam scores, study gains and expulsion are game rules (Tools/ok-rules src/academics.rs).

// ---------------------------------------------------------------------------------------------------- JSON

inline TSharedRef<FJsonObject> OKStreetAcademicJson(const FOKStreetAcademic& A) {
 auto O = MakeShared<FJsonObject>();
 O->SetStringField(TEXT("dept"), A.Dept); O->SetStringField(TEXT("status"), A.Status); O->SetStringField(TEXT("term"), A.Term);
 O->SetNumberField(TEXT("intelligence"), A.Intelligence); O->SetStringField(TEXT("leave_until"), A.LeaveUntil);
 O->SetStringField(TEXT("fees_paid"), A.FeesPaidTerm); O->SetStringField(TEXT("scholarship"), A.ScholarshipTerm);
 TArray<TSharedPtr<FJsonValue>> Courses;
 for (const auto& C : A.Courses) {
  auto X = MakeShared<FJsonObject>();
  X->SetStringField(TEXT("name"), C.Name); X->SetStringField(TEXT("building"), C.Building);
  X->SetNumberField(TEXT("floor"), C.Floor); X->SetNumberField(TEXT("room"), C.Room); X->SetNumberField(TEXT("weekday"), C.Weekday); X->SetNumberField(TEXT("slot"), C.Slot);
  X->SetNumberField(TEXT("held"), C.Held); X->SetNumberField(TEXT("attended"), C.Attended);
  X->SetNumberField(TEXT("midterm"), C.Midterm); X->SetNumberField(TEXT("final"), C.Final); X->SetNumberField(TEXT("grade"), C.Grade);
  Courses.Add(MakeShared<FJsonValueObject>(X));
 }
 O->SetArrayField(TEXT("courses"), Courses);
 TArray<TSharedPtr<FJsonValue>> Lines; for (const auto& L : A.Transcript) Lines.Add(MakeShared<FJsonValueString>(L));
 O->SetArrayField(TEXT("transcript"), Lines);
 return O;
}

inline void OKStreetReadAcademic(const TSharedPtr<FJsonObject>& O, FOKStreetAcademic& A) {
 if (!O) return;
 double V = 0;
 O->TryGetStringField(TEXT("dept"), A.Dept); O->TryGetStringField(TEXT("status"), A.Status); O->TryGetStringField(TEXT("term"), A.Term); O->TryGetStringField(TEXT("leave_until"), A.LeaveUntil);
 O->TryGetStringField(TEXT("fees_paid"), A.FeesPaidTerm); O->TryGetStringField(TEXT("scholarship"), A.ScholarshipTerm);
 if (O->TryGetNumberField(TEXT("intelligence"), V) && FMath::IsFinite(V)) A.Intelligence = FMath::Clamp(float(V), 0.f, 100.f);
 // The lists are replaced, not appended to: A is often a copy of the stored record (a candidate store), and appending
 // doubled the courses on every change until the save was hundreds of MB. Duplicates already saved are dropped here.
 const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
 TSet<FString> SeenCourses;
 if (O->TryGetArrayField(TEXT("courses"), Rows)) A.Courses.Reset();
 if (Rows) for (const auto& R : *Rows) {
  const auto X = R.IsValid() ? R->AsObject() : nullptr; if (!X) continue;
  FOKStreetCourse C; X->TryGetStringField(TEXT("name"), C.Name); X->TryGetStringField(TEXT("building"), C.Building);
  auto Int = [&](const TCHAR* K, int32& Out) { double N = 0; if (X->TryGetNumberField(K, N) && FMath::IsFinite(N)) Out = int32(N); };
  auto Num = [&](const TCHAR* K, float& Out) { double N = 0; if (X->TryGetNumberField(K, N) && FMath::IsFinite(N)) Out = float(N); };
  Int(TEXT("floor"), C.Floor); Int(TEXT("room"), C.Room); Int(TEXT("weekday"), C.Weekday); Int(TEXT("slot"), C.Slot); Int(TEXT("held"), C.Held); Int(TEXT("attended"), C.Attended);
  Num(TEXT("midterm"), C.Midterm); Num(TEXT("final"), C.Final); Num(TEXT("grade"), C.Grade);
  bool Seen = false; SeenCourses.Add(FString::Printf(TEXT("%s|%d|%d"), *C.Name, C.Weekday, C.Slot), &Seen);
  if (!C.Name.IsEmpty() && !Seen) A.Courses.Add(C);
 }
 const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
 if (O->TryGetArrayField(TEXT("transcript"), Lines)) { A.Transcript.Reset(); for (const auto& L : *Lines) { FString S; if (L.IsValid() && L->TryGetString(S)) A.Transcript.AddUnique(S); } }
}
