#include "OKStreetSocialFreeTalk.h"
#include "OKStreetSocialText.h"
#include "OKStreetSocialRules.h"
#include "HAL/PlatformMisc.h"
#include "PlatformHttp.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

static bool OKStreetFreeTalkReadJson(const FString& Path, TSharedPtr<FJsonObject>& Root) {
 FString Body;
 return FFileHelper::LoadFileToString(Body, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Root) && Root.IsValid();
}

// Write beside the target, then replace it, so a crash never leaves half a file.
static bool OKStreetFreeTalkWriteJson(const FString& Path, const TSharedRef<FJsonObject>& Root) {
 FString Out;
 FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Out));
 const FString Temp = Path + TEXT(".tmp");
 return FFileHelper::SaveStringToFile(Out, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) && IFileManager::Get().Move(*Path, *Temp, true, true);
}

FString OKStreetFreeTalkSettingsPath() {
 const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_FREE_TALK_SETTINGS"));
 const FString Project = FPaths::ProjectSavedDir() / TEXT("StreetSocial/deepseek.json");
 const FString Local = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
 const FString User = Local.IsEmpty() ? FString() : Local / TEXT("OpenKoikatsu/deepseek.json");
 auto& Files = IFileManager::Get();
 for (const FString& Path : {Env, Project, User}) {
  if (Path.IsEmpty() || !Files.FileExists(*Path)) continue;
  // The project's copy is kept at the user's too (it changes when the key is replaced).
  if (!User.IsEmpty() && Path != User && Files.GetTimeStamp(*Path) > Files.GetTimeStamp(*User)) { Files.MakeDirectory(*FPaths::GetPath(User), true); Files.Copy(*User, *Path, true, true); }
  return Path;
 }
 return Env.IsEmpty() ? Project : Env;
}

static bool OKStreetFreeTalkReadSettings(FOKFreeTalkSettings& Out, FString& Error) {
 Out.Endpoint = TEXT("https://api.deepseek.com/chat/completions");
 Out.Model = TEXT("deepseek-v4-flash");
 Out.ApiKey = FPlatformMisc::GetEnvironmentVariable(TEXT("DEEPSEEK_API_KEY"));
 // The runner passes the project path, because a staged build's Saved folder is not the project's.
 const FString Path = OKStreetFreeTalkSettingsPath();
 TSharedPtr<FJsonObject> Root;
 if (OKStreetFreeTalkReadJson(Path, Root)) {
  if (Out.ApiKey.IsEmpty()) Root->TryGetStringField(TEXT("api_key"), Out.ApiKey);
  Root->TryGetStringField(TEXT("endpoint"), Out.Endpoint);
  Root->TryGetStringField(TEXT("model"), Out.Model);
 }
 Out.ApiKey.TrimStartAndEndInline();
 if (Out.ApiKey.IsEmpty()) {
  Error = OKT(TEXT("ui.free_talk.deepseek_key_missing"));
  return false;
 }
 return true;
}

// The settings and the personas are read again at most every 2 s on the game thread (an edited key or persona file
// applies within that): reading and parsing them took 1-3 ms a call, and a reply asks several times (prompt, judge,
// every girl near for a group talk).
static constexpr double OKFreeTalkFilesFresh = 2.0;
bool OKStreetFreeTalkLoadSettings(FOKFreeTalkSettings& Out, FString& Error) {
 if (!IsInGameThread()) return OKStreetFreeTalkReadSettings(Out, Error);
 struct FEntry { FOKFreeTalkSettings Value; FString Error; bool Ok = false; double At = -1e9; };
 static FEntry Cache;
 const double Now = FPlatformTime::Seconds();
 if (Now - Cache.At > OKFreeTalkFilesFresh) { Cache.Value = FOKFreeTalkSettings(); Cache.Error.Reset(); Cache.Ok = OKStreetFreeTalkReadSettings(Cache.Value, Cache.Error); Cache.At = Now; }
 Out = Cache.Value;
 if (!Cache.Ok) Error = Cache.Error;
 return Cache.Ok;
}

static bool OKStreetFreeTalkReadPersona(const FString& Name, FOKFreeTalkPersona& Out, FString& Error);
bool OKStreetFreeTalkLoadPersona(const FString& Name, FOKFreeTalkPersona& Out, FString& Error) {
 if (!IsInGameThread()) return OKStreetFreeTalkReadPersona(Name, Out, Error);
 struct FEntry { FOKFreeTalkPersona Value; FString Error; bool Ok = false; double At = -1e9; };
 static TMap<FString, FEntry> Cache;
 const double Now = FPlatformTime::Seconds();
 FEntry& Entry = Cache.FindOrAdd(OKStreetCppText().Lang + TEXT("\n") + Name);
 if (Now - Entry.At > OKFreeTalkFilesFresh) { Entry.Value = FOKFreeTalkPersona(); Entry.Error.Reset(); Entry.Ok = OKStreetFreeTalkReadPersona(Name, Entry.Value, Entry.Error); Entry.At = Now; }
 Out = Entry.Value;
 if (!Entry.Ok) Error = Entry.Error;
 return Entry.Ok;
}

static bool OKStreetFreeTalkReadPersona(const FString& Name, FOKFreeTalkPersona& Out, FString& Error) {
 FString Path = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_FREE_TALK_PERSONAS"));
 if (Path.IsEmpty()) Path = FPaths::ProjectContentDir() / TEXT("IndependentResources/StreetSocialData/free_talk_personas.json");
 TSharedPtr<FJsonObject> Root;
 if (!OKStreetFreeTalkReadJson(Path, Root)) {
  Error = OKT(TEXT("ui.free_talk.persona_file_read_error"));
  return false;
 }
 // The player's language: its own rules and personas (Localization/<lang>/personas.json, same shape) take the place of
 // the Traditional Chinese ones where it has them.
 {const FString Lang = OKStreetCppText().Lang;TSharedPtr<FJsonObject> Local;
  if (Lang != TEXT("zh-TW") && OKStreetFreeTalkReadJson(FPaths::ProjectContentDir() / TEXT("IndependentResources/StreetSocialData/Localization") / Lang / TEXT("personas.json"), Local)) {
   const TArray<TSharedPtr<FJsonValue>>* LocalRules = nullptr;if (Local->TryGetArrayField(TEXT("system_rules"), LocalRules) && LocalRules->Num()) Root->SetArrayField(TEXT("system_rules"), *LocalRules);
   const TArray<TSharedPtr<FJsonValue>>* LocalPeople = nullptr;const TArray<TSharedPtr<FJsonValue>>* BasePeople = nullptr;
   if (Local->TryGetArrayField(TEXT("personas"), LocalPeople) && Root->TryGetArrayField(TEXT("personas"), BasePeople)) {
    TArray<TSharedPtr<FJsonValue>> Merged = *LocalPeople; // a persona the language lacks keeps its Chinese one
    for (const auto& V : *BasePeople) {FString M;if (V.IsValid() && V->AsObject().IsValid()) V->AsObject()->TryGetStringField(TEXT("match_name"), M);
     if (!Merged.ContainsByPredicate([&](const TSharedPtr<FJsonValue>& X){FString N;return X.IsValid() && X->AsObject().IsValid() && X->AsObject()->TryGetStringField(TEXT("match_name"), N) && N == M;})) Merged.Add(V);}
    Root->SetArrayField(TEXT("personas"), Merged);}}}
 const TArray<TSharedPtr<FJsonValue>>* Rules = nullptr;
 if (Root->TryGetArrayField(TEXT("system_rules"), Rules))
  for (const auto& Value : *Rules) {
   FString Rule;
   if (Value.IsValid() && Value->TryGetString(Rule) && !Rule.IsEmpty()) Out.Rules.Add(Rule);
  }
 const TArray<TSharedPtr<FJsonValue>>* People = nullptr;
 if (!Root->TryGetArrayField(TEXT("personas"), People)) {
  Error = OKT(TEXT("ui.free_talk.persona_file_format_error"));
  return false;
 }
 TSharedPtr<FJsonObject> Fallback;
 for (const auto& Value : *People) {
  const TSharedPtr<FJsonObject> Persona = Value.IsValid() ? Value->AsObject() : nullptr;
  if (!Persona.IsValid()) continue;
  FString Match;
  Persona->TryGetStringField(TEXT("match_name"), Match);
  if (Match == TEXT("_default")) {
   Fallback = Persona;
   continue;
  }
  if (Match == Name) {
   Persona->TryGetStringField(TEXT("display_name"), Out.DisplayName);
   Persona->TryGetStringField(TEXT("profile"), Out.Profile);
   if (Out.DisplayName.IsEmpty()) Out.DisplayName = Name;
   return !Out.Profile.IsEmpty();
  }
 }
 if (!Fallback.IsValid()) {
  Error = OKT(TEXT("ui.free_talk.character_config_missing"));
  return false;
 }
 Fallback->TryGetStringField(TEXT("profile"), Out.Profile);
 Out.DisplayName = Name;
 return !Out.Profile.IsEmpty();
}

FString OKStreetFreeTalkSystemPrompt(const FOKFreeTalkPersona& Persona, const FString& World) {
 FString Out = OKMsg(TEXT("ui.free_talk.persona_system_prompt"), {{TEXT("persona_name"), *Persona.DisplayName}, {TEXT("persona_profile"), *Persona.Profile}});
 for (int32 I = 0; I < Persona.Rules.Num(); ++I) Out += FString::Printf(TEXT("%d. %s\n"), I + 1, *Persona.Rules[I]);
 // What she may fill in her JSON and how (favor, lines, orders, acts, promises, activities): the text tables
 // (ui.free_talk.rules_prompt), in the game's language.
 Out += OKMsg(TEXT("ui.free_talk.rules_prompt"), {{TEXT("favor_step"), int64(OKFreeTalkFavorStep)}, {TEXT("segment_chars"), int64(OKFreeTalkSegmentChars)}});
 Out += World;
 return Out;
}

static const TCHAR* OKFreeTalkDayPart(int32 Hour) {
 return Hour < 5 ? OKT(TEXT("ui.free_talk.late_night_period")) : Hour < 8 ? OKT(TEXT("ui.free_talk.early_morning_period")) : Hour < 11 ? OKT(TEXT("ui.free_talk.morning_period")) : Hour < 13 ? OKT(TEXT("ui.free_talk.noon_period")) : Hour < 17 ? OKT(TEXT("ui.free_talk.afternoon_period")) : Hour < 19 ? OKT(TEXT("ui.free_talk.evening_dusk_period")) : Hour < 23 ? OKT(TEXT("ui.free_talk.night_period")) : OKT(TEXT("ui.free_talk.late_night_period"));
}

FString OKStreetFreeTalkDateTime(const FDateTime& Time) {
 return OKMsg(TEXT("ui.free_talk.date_time_label"), {{TEXT("year"), Time.GetYear()}, {TEXT("month"), Time.GetMonth()}, {TEXT("day"), Time.GetDay()}, {TEXT("weekday"), OKMsg(TEXT("ui.calendar.weekday_name"), {{TEXT("d"), int32(Time.GetDayOfWeek())}})}, {TEXT("hour"), Time.GetHour()}, {TEXT("minute"), Time.GetMinute()}, {TEXT("day_part"), OKFreeTalkDayPart(Time.GetHour())}});
}

FString OKStreetFreeTalkClock(double Hours) {
 const int32 Minutes = FMath::Clamp(FMath::FloorToInt32(Hours * 60.0), 0, 24 * 60 - 1);
 static const TCHAR* const SkyKeys[] = {TEXT("ui.free_talk.sky_night"), TEXT("ui.free_talk.sky_dawn"), TEXT("ui.free_talk.sky_day"), TEXT("ui.free_talk.sky_sunset")};
 return OKMsg(TEXT("ui.free_talk.street_sky_label"), {{TEXT("day_part"), OKFreeTalkDayPart(Minutes / 60)}, {TEXT("hour"), Minutes / 60}, {TEXT("minute"), Minutes % 60}, {TEXT("sky"), OKT(SkyKeys[int32(OKStreetSkyAt(Hours))])}, {TEXT("rise"), int32(OKStreetSunriseHour)}, {TEXT("set"), int32(OKStreetSunsetHour)}});
}

// The date and time live here, not in the system prompt, so the cached prefix stays the same between turns.
FString OKStreetFreeTalkUserMessage(const FOKFreeTalkStatus& Status, const FString& PlayerText) {
 return OKMsg(TEXT("ui.free_talk.context_status_prompt"), {{TEXT("now_time"), *Status.Now}, {TEXT("street_sky"), *Status.StreetTime}, {TEXT("relation"), *Status.Relation}, {TEXT("favor"), Status.Favor}, {TEXT("favor_limit"), Status.FavorLimit}, {TEXT("lewdness"), Status.Lewdness}, {TEXT("sex_desire"), Status.SexDesire}, {TEXT("current_activity"), *Status.Doing}, {TEXT("nearby_places"), *Status.Nearby}, {TEXT("promises_block"), *(OKT(TEXT("ui.free_talk.header_promises")) + Status.Promises + TEXT("\n"))}, {TEXT("status_block"), *((Status.Today.IsEmpty() ? FString() : OKT(TEXT("ui.free_talk.header_today")) + Status.Today + TEXT("\n")) + (Status.Scene.IsEmpty() ? FString() : OKT(TEXT("ui.free_talk.header_scene")) + Status.Scene + TEXT("\n")) + (Status.Invite.IsEmpty() ? FString() : OKT(TEXT("ui.free_talk.header_invite")) + Status.Invite + TEXT("\n")) + (Status.Recent.IsEmpty() ? FString() : OKT(TEXT("ui.free_talk.header_recent")) + Status.Recent + TEXT("\n")) + (Status.Heard.IsEmpty() ? FString() : OKT(Status.Relation.StartsWith(OKT(TEXT("ui.bonds.lover_stage")))?TEXT("ui.free_talk.header_heard_lover"):TEXT("ui.free_talk.header_heard")) + Status.Heard + TEXT("\n")) + (Status.Needs.IsEmpty() ? FString() : OKT(TEXT("ui.free_talk.header_needs")) + Status.Needs + TEXT("\n")) + (Status.Menu.IsEmpty() ? FString() : OKT(TEXT("ui.free_talk.header_menu")) + Status.Menu + TEXT("\n")) + (Status.Choices.IsEmpty() ? FString() : OKT(TEXT("ui.free_talk.header_choices")) + Status.Choices + TEXT("\n")) + (Status.Axes.IsEmpty() ? FString() : OKT(TEXT("ui.free_talk.header_axes")) + Status.Axes + TEXT("\n")) + (Status.Inner.IsEmpty() ? FString() : OKT(TEXT("ui.free_talk.header_inner")) + Status.Inner + TEXT("\n"))
   + (Status.Memories.IsEmpty() ? FString() : OKT(TEXT("ui.free_talk.header_memories")) + Status.Memories + TEXT("\n")))}, {TEXT("player_text"), *PlayerText}});
}

TArray<TSharedPtr<FJsonValue>> OKStreetFreeTalkMessages(const FString& System, const TArray<FOKFreeTalkTurn>& Memory, const FString& UserMessage) {
 TArray<TSharedPtr<FJsonValue>> Messages;
 auto Add = [&Messages](const FString& Role, const FString& Content) {
  TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
  Message->SetStringField(TEXT("role"), Role);
  Message->SetStringField(TEXT("content"), Content);
  Messages.Add(MakeShared<FJsonValueObject>(Message));
 };
 Add(TEXT("system"), System);
 for (const auto& Turn : Memory) Add(Turn.Role, Turn.Content);
 Add(TEXT("user"), UserMessage);
 return Messages;
}


// The message content alone: what the heroine mind (ok_mind.dll) hands back.
// Her "face" object (a talk reply's, or one girl's in a group reply): the expression, blush, tears, sweat, gaze.
void OKStreetFreeTalkParseFace(const FJsonObject& Face, FOKFreeTalkReply& Out) {
 Out.FaceGiven = true; Face.TryGetStringField(TEXT("expr"), Out.FaceExpr); Face.TryGetStringField(TEXT("gaze"), Out.FaceGaze);
 Face.TryGetStringField(TEXT("brow"), Out.FaceBrow); Face.TryGetStringField(TEXT("eyes"), Out.FaceEyes); Face.TryGetStringField(TEXT("mouth"), Out.FaceMouth);
 double N = 0;
 if (Face.TryGetNumberField(TEXT("blush"), N) && FMath::IsFinite(N)) Out.FaceBlush = FMath::Clamp(float(N), 0.f, 1.f);
 if (Face.TryGetNumberField(TEXT("sweat"), N) && FMath::IsFinite(N)) Out.FaceSweat = FMath::Clamp(float(N), 0.f, 1.f);
 if (Face.TryGetNumberField(TEXT("eyes_open"), N) && FMath::IsFinite(N)) Out.FaceEyesOpen = FMath::Clamp(float(N), 0.f, 1.f);
 if (Face.TryGetNumberField(TEXT("tears"), N) && FMath::IsFinite(N)) Out.FaceTears = FMath::Clamp(int32(N), 0, 2);
}

bool OKStreetFreeTalkParseContent(const FString& Content, FOKFreeTalkReply& Out, FString& Error, bool& Retry) {
 Retry = false;
 // JSON mode returns {"lines", "favor_delta", "reason"}; a single "reply" or a plain sentence is cut locally.
 if (Content.TrimStartAndEnd().IsEmpty()) {
  Error = OKT(TEXT("ui.free_talk.deepseek_empty_reply"));
  Retry = true;
  return false;
 }
 // The model now and then wraps the object: in a ```json fence, inside a JSON string, escaped (\"), or with prose
 // around it. Dig the object out before falling back to treating the content as a plain sentence.
 TSharedPtr<FJsonObject> Reply;
 auto TryObject = [&Reply](const FString& Text) { Reply.Reset(); return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Reply) && Reply.IsValid(); };
 FString Body = Content.TrimStartAndEnd();
 if (Body.StartsWith(TEXT("```"))) { int32 Eol = INDEX_NONE; if (Body.FindChar(TEXT('\n'), Eol)) Body = Body.Mid(Eol + 1); Body.RemoveFromEnd(TEXT("```")); Body.TrimStartAndEndInline(); }
 bool Parsed = TryObject(Body);
 if (!Parsed && Body.StartsWith(TEXT("\""))) {
  TArray<TSharedPtr<FJsonValue>> Wrapped; FString Inner;
  if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("[") + Body + TEXT("]")), Wrapped) && Wrapped.Num() == 1 && Wrapped[0]->TryGetString(Inner)) { Body = Inner.TrimStartAndEnd(); Parsed = TryObject(Body); }
 }
 if (!Parsed && Body.Contains(TEXT("\\\""))) { const FString Unescaped = Body.Replace(TEXT("\\\""), TEXT("\"")); if (TryObject(Unescaped)) { Body = Unescaped; Parsed = true; } }
 if (!Parsed) {
  const int32 Open = Body.Find(TEXT("{")), Close = Body.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
  if (Open != INDEX_NONE && Close > Open && TryObject(Body.Mid(Open, Close - Open + 1))) Parsed = true;
 }
 // Broken JSON (a stray key, cut off): save the spoken part — the "lines" array or the "reply" string — on its
 // own. Unsalvageable JSON is asked again rather than read out as code.
 if (!Parsed && Body.StartsWith(TEXT("{"))) {
  const int32 Key = Body.Find(TEXT("\"lines\""));
  const int32 Start = Key == INDEX_NONE ? INDEX_NONE : Body.Find(TEXT("["), ESearchCase::CaseSensitive, ESearchDir::FromStart, Key);
  const int32 End = Start == INDEX_NONE ? INDEX_NONE : Body.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
  TArray<TSharedPtr<FJsonValue>> Lines;
  if (End != INDEX_NONE && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body.Mid(Start, End - Start + 1)), Lines))
   for (const auto& Value : Lines) { FString Line; if (Value.IsValid() && Value->TryGetString(Line)) OKStreetFreeTalkSplitLines(Line, Out.Lines); }
  if (Out.Lines.IsEmpty()) {
   const int32 ReplyKey = Body.Find(TEXT("\"reply\""));const int32 Colon = ReplyKey == INDEX_NONE ? INDEX_NONE : Body.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ReplyKey);
   TArray<TSharedPtr<FJsonValue>> One;
   if (Colon != INDEX_NONE) { const int32 Q = Body.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Colon); int32 E = Q;
    while (E != INDEX_NONE) { E = Body.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, E + 1); if (E == INDEX_NONE || Body[E - 1] != TEXT('\\')) break; }
    FString Single; if (Q != INDEX_NONE && E != INDEX_NONE && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("[") + Body.Mid(Q, E - Q + 1) + TEXT("]")), One) && One.Num() == 1 && One[0]->TryGetString(Single)) OKStreetFreeTalkSplitLines(Single, Out.Lines); }
  }
  if (Out.Lines.IsEmpty()) { Error = OKT(TEXT("ui.free_talk.deepseek_bad_format")); Retry = true; return false; }
  Out.Reply = OKStreetFreeTalkJoinLines(Out.Lines);
  return true;
 }
 if (Parsed) {
  double Delta = 0;
  const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
  // All of them at once: whether a line is the reply's last decides how one with no stop of its own ends.
  if (Reply->TryGetArrayField(TEXT("lines"), Lines)) {
   TArray<FString> Texts;
   for (const auto& Value : *Lines) { FString Line; if (Value.IsValid() && Value->TryGetString(Line)) Texts.Add(Line); }
   OKStreetFreeTalkSplitTexts(Texts, Out.Lines);
  }
  FString Single;
  if (Out.Lines.IsEmpty() && Reply->TryGetStringField(TEXT("reply"), Single)) OKStreetFreeTalkSplitLines(Single, Out.Lines);
  Reply->TryGetStringField(TEXT("reason"), Out.Reason);
  Reply->TryGetStringField(TEXT("action"), Out.Action);
  Reply->TryGetStringField(TEXT("emotion"), Out.Emotion);
  Reply->TryGetStringField(TEXT("gesture"), Out.Gesture);
  const TSharedPtr<FJsonObject>* Face = nullptr;
  if (Reply->TryGetObjectField(TEXT("face"), Face) && Face->IsValid()) OKStreetFreeTalkParseFace(**Face, Out);
  const TSharedPtr<FJsonObject>* Rel = nullptr;
  if (Reply->TryGetObjectField(TEXT("rel_delta"), Rel)) Out.RelDelta = *Rel;
  auto ReadChoice = [&](const TCHAR* Field, FString& Id, FString& Option, float& Insist) {
   const TSharedPtr<FJsonObject>* C = nullptr;
   if (!Reply->TryGetObjectField(Field, C)) return;
   (*C)->TryGetStringField(TEXT("choice_id"), Id); (*C)->TryGetStringField(TEXT("option"), Option);
   double V = 0.5; if ((*C)->TryGetNumberField(TEXT("insist"), V) && FMath::IsFinite(V)) Insist = FMath::Clamp(float(V), 0.f, 1.f);
  };
  ReadChoice(TEXT("resolve"), Out.ResolveId, Out.ResolveOption, Out.ResolveInsist);
  ReadChoice(TEXT("propose"), Out.ProposeId, Out.ProposeOption, Out.ProposeInsist);
  const TSharedPtr<FJsonObject>* Promise = nullptr;
  Reply->TryGetStringField(TEXT("payer"), Out.Payer);
  if (Reply->TryGetObjectField(TEXT("promise"), Promise)) { (*Promise)->TryGetStringField(TEXT("day"), Out.PromiseDay); (*Promise)->TryGetStringField(TEXT("time"), Out.PromiseTime); (*Promise)->TryGetStringField(TEXT("place"), Out.PromisePlace); (*Promise)->TryGetStringField(TEXT("what"), Out.PromiseWhat); }
  for (const auto& F : {TPair<const TCHAR*, TSharedPtr<FJsonObject>*>(TEXT("predict"), &Out.Predict), TPair<const TCHAR*, TSharedPtr<FJsonObject>*>(TEXT("trade"), &Out.Trade), TPair<const TCHAR*, TSharedPtr<FJsonObject>*>(TEXT("bet"), &Out.Bet)}) {
   const TSharedPtr<FJsonObject>* O = nullptr;
   if (Reply->TryGetObjectField(F.Key, O) && O->IsValid()) *F.Value = *O;
  }
  const TSharedPtr<FJsonObject>* Act = nullptr;
  // The act may also come as the "action" field itself ({"action": {"kind": "lend", "arg": 300}}): a loan she took was dropped.
  if (Reply->TryGetObjectField(TEXT("act"), Act) || Reply->TryGetObjectField(TEXT("action"), Act)) { (*Act)->TryGetStringField(TEXT("kind"), Out.ActKind); (*Act)->TryGetStringField(TEXT("arg"), Out.ActArg); }
  const TArray<TSharedPtr<FJsonValue>>* Orders = nullptr;
  if (Reply->TryGetArrayField(TEXT("order"), Orders)) for (const auto& V : *Orders) {
   const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr; if (!O) continue;
   FOKFreeTalkReply::FOrder Order; FString For;
   O->TryGetStringField(TEXT("id"), Order.Id); O->TryGetStringField(TEXT("size"), Order.Size); O->TryGetStringField(TEXT("sweetness"), Order.Sweetness); O->TryGetStringField(TEXT("ice"), Order.Ice); O->TryGetStringField(TEXT("for"), For);
   Order.ForHer = For == TEXT("her");
   if (!Order.Id.IsEmpty() && Out.Orders.Num() < 8) Out.Orders.Add(Order);
  }
  Out.Action.TrimStartAndEndInline();
  if (Out.Action.Equals(TEXT("none"), ESearchCase::IgnoreCase)) Out.Action.Reset();
  // An act word in the action field ({"action": "hold_hands"} to "let's walk hand in hand"): it is the act. The judge then
  // starts the walk, and the hand holding she agreed to used to be dropped. (eat / feed / share_food keep their own path.)
  if (Out.ActKind.IsEmpty()) for (const TCHAR* Word : {TEXT("hold_hands"), TEXT("head_pat"), TEXT("hug"), TEXT("kiss"), TEXT("lap_pillow"), TEXT("loud"), TEXT("phone_call"), TEXT("lie_down"), TEXT("alcohol")})
   if (Out.Action == Word) { Out.ActKind = Out.Action; Out.Action.Reset(); break; }
  if (Reply->TryGetNumberField(TEXT("favor_delta"), Delta)) Out.FavorDelta = FMath::Clamp(FMath::RoundToInt(Delta), -OKFreeTalkFavorStep, OKFreeTalkFavorStep);
 } else {
  OKStreetFreeTalkSplitLines(Content, Out.Lines);
 }
 Out.Reply = OKStreetFreeTalkJoinLines(Out.Lines);
 Out.Reason.TrimStartAndEndInline();
 if (Out.Reply.IsEmpty()) {
  Error = OKT(TEXT("ui.free_talk.deepseek_no_line"));
  return false;
 }
 return true;
}

// Cutting her reply into spoken lines and joining them back is language work: the rules do it (locale.rs reply_lines,
// reply_join). Without them a text stays one line.
FString OKStreetFreeTalkJoinLines(const TArray<FString>& Lines) {
 if (FOKStreetRules* Rules = OKStreetActiveRules()) {
  auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("rule"), TEXT("reply_join"));
  TArray<TSharedPtr<FJsonValue>> Values; for (const FString& L : Lines) Values.Add(MakeShared<FJsonValueString>(L));
  Request->SetArrayField(TEXT("lines"), Values); FString Error, Out;
  if (const TSharedPtr<FJsonObject> A = Rules->Eval(Request, Error)) if (A->TryGetStringField(TEXT("reply"), Out)) return Out;
 }
 return FString::Join(Lines, TEXT(""));
}

void OKStreetFreeTalkSplitTexts(const TArray<FString>& Texts, TArray<FString>& Out) {
 if (FOKStreetRules* Rules = OKStreetActiveRules()) {
  auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("rule"), TEXT("reply_lines"));
  TArray<TSharedPtr<FJsonValue>> Values; for (const FString& T : Texts) Values.Add(MakeShared<FJsonValueString>(T));
  Request->SetArrayField(TEXT("texts"), Values); Request->SetNumberField(TEXT("max"), OKFreeTalkSegmentChars);
  FString Error; const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
  if (const TSharedPtr<FJsonObject> A = Rules->Eval(Request, Error)) if (A->TryGetArrayField(TEXT("lines"), Lines)) { for (const auto& L : *Lines) Out.Add(L->AsString()); return; }
 }
 for (const FString& T : Texts) { const FString Clean = T.TrimStartAndEnd(); if (!Clean.IsEmpty()) Out.Add(Clean); }
}

void OKStreetFreeTalkSplitLines(const FString& Text, TArray<FString>& Out) {
 if (FOKStreetRules* Rules = OKStreetActiveRules()) {
  auto Request = MakeShared<FJsonObject>(); Request->SetStringField(TEXT("rule"), TEXT("reply_lines"));
  Request->SetArrayField(TEXT("texts"), {MakeShared<FJsonValueString>(Text)}); Request->SetNumberField(TEXT("max"), OKFreeTalkSegmentChars);
  FString Error; const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
  if (const TSharedPtr<FJsonObject> A = Rules->Eval(Request, Error)) if (A->TryGetArrayField(TEXT("lines"), Lines)) { for (const auto& L : *Lines) Out.Add(L->AsString()); return; }
 }
 const FString Clean = Text.TrimStartAndEnd(); if (!Clean.IsEmpty()) Out.Add(Clean);
}

FString OKStreetFreeTalkMemoryReply(const FOKFreeTalkReply& Reply) {
 TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
 TArray<TSharedPtr<FJsonValue>> Lines;
 for (const auto& Line : Reply.Lines) Lines.Add(MakeShared<FJsonValueString>(Line));
 O->SetArrayField(TEXT("lines"), Lines);
 O->SetNumberField(TEXT("favor_delta"), Reply.FavorDelta);
 O->SetStringField(TEXT("action"), Reply.Action.IsEmpty() ? FString(TEXT("none")) : Reply.Action);
 O->SetStringField(TEXT("reason"), Reply.Reason);
 FString Out;
 FJsonSerializer::Serialize(O, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
 return Out;
}


// free-talk-memory.json: {"schema", "chars_per_character", "characters": [{"character": id, "turns": [{"role", "content"}]}]}
bool OKStreetFreeTalkLoadMemory(const FString& Path, TMap<FString, TArray<FOKFreeTalkTurn>>& Out) {
 Out.Reset();
 TSharedPtr<FJsonObject> Root;
 const TArray<TSharedPtr<FJsonValue>>* Girls = nullptr;
 if (!OKStreetFreeTalkReadJson(Path, Root) || !Root->TryGetArrayField(TEXT("characters"), Girls)) return false;
 for (const auto& GirlValue : *Girls) {
  const TSharedPtr<FJsonObject> Girl = GirlValue.IsValid() ? GirlValue->AsObject() : nullptr;
  FString Id;
  const TArray<TSharedPtr<FJsonValue>>* Turns = nullptr;
  if (!Girl.IsValid() || !Girl->TryGetStringField(TEXT("character"), Id) || Id.IsEmpty() || !Girl->TryGetArrayField(TEXT("turns"), Turns)) continue;
  auto& Memory = Out.FindOrAdd(Id);
  for (const auto& Value : *Turns) {
   const TSharedPtr<FJsonObject> O = Value.IsValid() ? Value->AsObject() : nullptr;
   FOKFreeTalkTurn Turn;
   if (!O.IsValid() || !O->TryGetStringField(TEXT("role"), Turn.Role) || !O->TryGetStringField(TEXT("content"), Turn.Content) || (Turn.Role != TEXT("user") && Turn.Role != TEXT("assistant"))) continue;
   if (Turn.Role == TEXT("assistant") && !Turn.Content.Contains(TEXT("\"lines\""))) {
    FOKFreeTalkReply Old;
    TSharedPtr<FJsonObject> J;
    FString Text = Turn.Content;
    if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Turn.Content), J) && J.IsValid()) {
     double Delta = 0;
     J->TryGetStringField(TEXT("reply"), Text);
     J->TryGetStringField(TEXT("reason"), Old.Reason);
     if (J->TryGetNumberField(TEXT("favor_delta"), Delta)) Old.FavorDelta = FMath::RoundToInt(Delta);
    }
    OKStreetFreeTalkSplitLines(Text, Old.Lines);
    Turn.Content = OKStreetFreeTalkMemoryReply(Old);
   }
   Memory.Add(MoveTemp(Turn));
  }
  OKStreetFreeTalkTrimMemory(Memory);
 }
 return true;
}

static TSharedRef<FJsonObject> OKStreetFreeTalkMemoryRoot(const TMap<FString, TArray<FOKFreeTalkTurn>>& Memory) {
 TArray<TSharedPtr<FJsonValue>> Girls;
 for (const auto& Girl : Memory) {
  TArray<TSharedPtr<FJsonValue>> Turns;
  for (const auto& Turn : Girl.Value) {
   TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
   O->SetStringField(TEXT("role"), Turn.Role);
   O->SetStringField(TEXT("content"), Turn.Content);
   Turns.Add(MakeShared<FJsonValueObject>(O));
  }
  TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
  Entry->SetStringField(TEXT("character"), Girl.Key);
  Entry->SetArrayField(TEXT("turns"), Turns);
  Girls.Add(MakeShared<FJsonValueObject>(Entry));
 }
 TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
 Root->SetNumberField(TEXT("schema"), 1);
 Root->SetNumberField(TEXT("chars_per_character"), OKFreeTalkMemoryChars);
 Root->SetArrayField(TEXT("characters"), Girls);
 return Root;
}
FString OKStreetFreeTalkMemoryJson(const TMap<FString, TArray<FOKFreeTalkTurn>>& Memory) {
 FString Out;FJsonSerializer::Serialize(OKStreetFreeTalkMemoryRoot(Memory), TJsonWriterFactory<>::Create(&Out));return Out;
}
bool OKStreetFreeTalkSaveMemory(const FString& Path, const TMap<FString, TArray<FOKFreeTalkTurn>>& Memory, FString& Error) {
 if (!OKStreetFreeTalkWriteJson(Path, OKStreetFreeTalkMemoryRoot(Memory))) {
  Error = OKT(TEXT("ui.free_talk.memory_save_failed")) + Path;
  return false;
 }
 return true;
}

void OKStreetFreeTalkTrimMemory(TArray<FOKFreeTalkTurn>& Memory) {
 int32 Total = 0;
 for (const auto& Turn : Memory) Total += Turn.Content.Len();
 if (Total <= OKFreeTalkMemoryChars) return;
 // Drop whole exchanges from the front, and always restart on a user turn.
 int32 Drop = 0;
 while (Drop < Memory.Num() && (Total > OKFreeTalkMemoryKeepChars || Memory[Drop].Role != TEXT("user"))) Total -= Memory[Drop++].Content.Len();
 Memory.RemoveAt(0, Drop);
}

bool OKStreetFreeTalkLoadLog(const FString& Path, TArray<FOKFreeTalkLine>& Out) {
 Out.Reset();
 TSharedPtr<FJsonObject> Root;
 const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
 if (!OKStreetFreeTalkReadJson(Path, Root) || !Root->TryGetArrayField(TEXT("lines"), Lines)) return false;
 for (const auto& Value : *Lines) {
  const TSharedPtr<FJsonObject> O = Value.IsValid() ? Value->AsObject() : nullptr;
  if (!O.IsValid()) continue;
  FOKFreeTalkLine Line;
  double Serial = 0;
  O->TryGetNumberField(TEXT("serial"), Serial);
  Line.Serial = (int64)Serial;
  O->TryGetStringField(TEXT("character"), Line.CharacterId);
  O->TryGetStringField(TEXT("speaker"), Line.Speaker);
  O->TryGetStringField(TEXT("text"), Line.Text);
  O->TryGetStringField(TEXT("voice"), Line.Voice);
  O->TryGetStringField(TEXT("time"), Line.Time);
  O->TryGetBoolField(TEXT("player"), Line.Player);
  if (!Line.Text.IsEmpty()) Out.Add(MoveTemp(Line));
 }
 if (Out.Num() > OKFreeTalkLogLimit) Out.RemoveAt(0, Out.Num() - OKFreeTalkLogLimit);
 return true;
}

static TSharedRef<FJsonObject> OKStreetFreeTalkLogRoot(const TArray<FOKFreeTalkLine>& Lines) {
 TArray<TSharedPtr<FJsonValue>> Values;
 for (const auto& Line : Lines) {
  TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
  O->SetNumberField(TEXT("serial"), (double)Line.Serial);
  O->SetStringField(TEXT("character"), Line.CharacterId);
  O->SetStringField(TEXT("speaker"), Line.Speaker);
  O->SetStringField(TEXT("text"), Line.Text);
  O->SetStringField(TEXT("voice"), Line.Voice);
  O->SetStringField(TEXT("time"), Line.Time);
  O->SetBoolField(TEXT("player"), Line.Player);
  Values.Add(MakeShared<FJsonValueObject>(O));
 }
 TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
 Root->SetNumberField(TEXT("schema"), 1);
 Root->SetArrayField(TEXT("lines"), Values);
 return Root;
}
FString OKStreetFreeTalkLogJson(const TArray<FOKFreeTalkLine>& Lines) {
 FString Out;FJsonSerializer::Serialize(OKStreetFreeTalkLogRoot(Lines), TJsonWriterFactory<>::Create(&Out));return Out;
}
bool OKStreetFreeTalkSaveLog(const FString& Path, const TArray<FOKFreeTalkLine>& Lines, FString& Error) {
 if (!OKStreetFreeTalkWriteJson(Path, OKStreetFreeTalkLogRoot(Lines))) {
  Error = OKT(TEXT("ui.free_talk.transcript_save_failed")) + Path;
  return false;
 }
 return true;
}

FString OKStreetFreeTalkNudge(int32 SilentSeconds, bool Leaving) {
 return Leaving
  ? OKMsg(TEXT("ui.free_talk.silent_leave_prompt"), {{TEXT("silent_seconds"), SilentSeconds}})
  : OKMsg(TEXT("ui.free_talk.silent_prompt_speak"), {{TEXT("silent_seconds"), SilentSeconds}});
}

bool OKStreetFreeTalkIsNudge(const FString& Text) {
 // Silence nudges, order notices, a scene set up (the card game dealt): sent to the model as his turn, but not shown or
 // logged as his words. Any whole line in full-width parentheses is such a direction, not only the ones naming a field.
 const FString T=Text.TrimStartAndEnd();
 return T.StartsWith(OKT(TEXT("ui.free_talk.player_paren_open")))||(T.StartsWith(OKT(TEXT("ui.finance_apps.open_paren")))&&(T.EndsWith(OKT(TEXT("ui.finance_apps.close_paren")))||T.Contains(TEXT("favor_delta"))));
}

FString OKStreetFreeTalkAsrEndpoint() {
 FString Endpoint = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ASR_ENDPOINT"));
 return Endpoint.IsEmpty() ? FString(TEXT("http://127.0.0.1:47861")) : Endpoint;
}

void OKStreetFreeTalkEnsureAsrServer() {
 static double LastStart = -1e9;
 const double Now = FPlatformTime::Seconds();
 if (Now - LastStart < 20.0) return;
 const FString Exe = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ASR_SERVER"));
 if (Exe.IsEmpty() || !FPaths::FileExists(Exe)) { UE_LOG(LogTemp, Warning, TEXT("Free talk: ok-asr is not answering and OK_ASR_SERVER is not set")); return; }
 LastStart = Now;
 FString Device, Text;TSharedPtr<FJsonObject> Root;
 const FString Settings = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ASR_SETTINGS"));
 if (!Settings.IsEmpty() && FFileHelper::LoadFileToString(Text, *Settings) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid()) Root->TryGetStringField(TEXT("device"), Device);
 // --parent-pid: the server exits with the game, so a closed game never leaves it (and the microphone) running.
 // On the port the game polls: started without it, it took the default 47861 (another checkout's) while this game kept
 // calling its own.
 const FString Parent = FString::Printf(TEXT(" --port %d --parent-pid %u"), int32(FPlatformHttp::GetUrlPort(OKStreetFreeTalkAsrEndpoint()).Get(47861)), FPlatformProcess::GetCurrentProcessId());
 const FString Params = (Device.IsEmpty() ? FString(TEXT("serve")) : FString::Printf(TEXT("serve --device \"%s\""), *Device.Replace(TEXT("\""), TEXT("")))) + Parent;
 FProcHandle Proc = FPlatformProcess::CreateProc(*Exe, *Params, true, true, true, nullptr, 0, *FPaths::GetPath(Exe), nullptr);
 UE_LOG(LogTemp, Display, TEXT("Free talk: ok-asr was not answering; started %s %s (%s)"), *Exe, *Params, Proc.IsValid() ? TEXT("ok") : TEXT("failed"));
 FPlatformProcess::CloseProc(Proc);
}

bool OKStreetFreeTalkParseAsr(const FString& Body, FString& Text, bool& Speech, FString& Error) {
 TSharedPtr<FJsonObject> Root;
 if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Root) || !Root.IsValid()) {
  Error = OKT(TEXT("ui.free_talk.speech_server_bad_format"));
  return false;
 }
 bool Ok = false;
 Root->TryGetBoolField(TEXT("ok"), Ok);
 if (!Ok) {
  FString Detail;
  Root->TryGetStringField(TEXT("error"), Detail);
  Error = OKT(TEXT("ui.free_talk.voice_input_failed")) + Detail;
  return false;
 }
 Root->TryGetStringField(TEXT("text"), Text);
 Root->TryGetBoolField(TEXT("speech"), Speech);
 Text.TrimStartAndEndInline();
 return true;
}
