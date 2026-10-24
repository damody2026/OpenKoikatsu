#pragma once
#include "CoreMinimal.h"

// Free talk ("自由對話") with a DeepSeek-backed roleplay persona. Each reply also carries the model's
// judgement of the player's line (favor_delta), which the scene applies to her favor.
//
// Requests are laid out for DeepSeek's prefix cache: the system prompt (persona + rules + output
// format) never changes for a girl, her memory is append-only and is trimmed in large steps, and the
// changing values (relationship, stats) go only into the last user message.

struct FOKFreeTalkTurn {
 FString Role, Content;
};

struct FOKFreeTalkSettings {
 FString ApiKey, Endpoint, Model;
};

struct FOKFreeTalkPersona {
 FString DisplayName, Profile;
 TArray<FString> Rules;
};

// Her current values as the game shows them (debug panel "女角數值").
struct FOKFreeTalkStatus {
 FString Relation;
 float Favor = 0.f, FavorLimit = 150.f, Lewdness = 0.f, SexDesire = 0.f;
 int32 TalksRemaining = 0;
 FString Doing, Nearby; // what the pair is doing, and nearby places with distance and direction
 FString Now, StreetTime; // calendar date/weekday/time (2010 calendar), and the street's (24x) sky clock
 FString Memories; // what she remembers that bears on this moment (heroine mind recall), may be empty
 FString Axes, Inner; // plan §4.2 axes with exact numbers, and the §10 inner line (fixed phrases); may be empty
 FString Choices; // plan §7.1 open choices with her, one per line; may be empty
 FString Menu; // plan §8.2 what this place sells (id, name, price), when they stand in a shop; may be empty
 FString Scene, Needs, Recent; // plan §7 [場景], her visible needs (§4.3), and what just happened (§9 reactions)
 FString Invite; // plan §12.2 this turn's answer to each kind of invitation, decided by the rules
 FString Heard; // plan §4.6 rumours she has heard, with how sure she is
 FString Today; // plan §11.3 holiday or exam week, weather now, her birthday
 FString Promises; // the meetings with him the game really has (time, place, what), or a line saying there are none
};
// Real date, weekday, time and part of day, in Chinese, for the model.
FString OKStreetFreeTalkDateTime(const FDateTime& Time);
// Part of day and HH:MM of the street sky clock (hours).
FString OKStreetFreeTalkClock(double Hours);

struct FOKFreeTalkReply {
 TArray<FString> Lines; // spoken one TTS request at a time, each at most OKFreeTalkSegmentChars
 FString Reply, Reason; // Reply = the lines joined, for captions, the log and memory
 FString Action; // activity id or "go:<place>" she agreed to start now; empty for none
 FString Emotion; // one of the TTS emotion set, empty if not given
 FString Gesture; // a gesture key of motions.toml she makes with this reply (wave, shy, ...); empty for none
 // Her face this turn ("face": {"expr", "blush", "tears", "sweat", "gaze", "eyes_open"}): an expression of the source
 // catalogue, cheek blush / sweat 0..1 (-1 not given), tears 0..2, where she looks (him / away / down / up / closed).
 FString FaceExpr, FaceGaze, FaceBrow, FaceEyes, FaceMouth;float FaceBlush=-1.f,FaceSweat=-1.f,FaceEyesOpen=-1.f;int32 FaceTears=-1;bool FaceGiven=false;
 TSharedPtr<class FJsonObject> RelDelta; // {"trust": 2, ...}; applied only through the rules' validator (Tools/ok-rules mind.rs rel_delta)
 // Plan §7.1: the player's answer to an open choice as the model read it, her own proposal, and an action word.
 FString ResolveId, ResolveOption, ProposeId, ProposeOption, ActKind, ActArg;
 // Plan §13.3 a new promise: day ("+1" or YYYY-MM-DD), time HH:MM, place id, what.
 FString PromiseDay, PromiseTime, PromisePlace, PromiseWhat;
 // Who pays, as she reads what they settled ("him" | "her" | "split"; empty: not talked about).
 FString Payer;
 // Plan §15: a prediction he made, a trade he asked for at the computer, a bet at the lottery shop (raw objects).
 TSharedPtr<class FJsonObject> Predict, Trade, Bet;
 float ResolveInsist = 0.5f, ProposeInsist = 0.5f;
 // Plan §8.2 ordering by chat: catalogue item, size, drink options, and whether it is for her.
 struct FOrder { FString Id, Size, Sweetness, Ice; bool ForHer = false; };
 TArray<FOrder> Orders;
 int32 FavorDelta = 0;
};

// A "face" object of a reply (expr, blush, tears, sweat, gaze, eyes_open) into the reply's face fields.
void OKStreetFreeTalkParseFace(const class FJsonObject& Face, FOKFreeTalkReply& Out);

// One spoken line of free talk, kept across sessions (free-talk-log.json beside the relationship save).
struct FOKFreeTalkLine {
 int64 Serial = 0;
 FString CharacterId; // OKStreetStableId of the girl the line belongs to
 FString Speaker, Text, Voice, Time;
 bool Player = false;
};

constexpr int32 OKFreeTalkLogLimit = 500;
constexpr int32 OKFreeTalkReplayCount = 20;
constexpr double OKFreeTalkQuietSeconds = 180.0; // real seconds his silence is welcome after she asks him to keep quiet (about 72 street minutes)
// Per-girl memory budget in characters, and what a trim keeps (trimming in one large step keeps the
// cached prefix valid for many turns instead of shifting it on every message).
constexpr int32 OKFreeTalkMemoryChars = 10000;
constexpr int32 OKFreeTalkMemoryKeepChars = 7000;
constexpr int32 OKFreeTalkFavorStep = 5;
// The model splits her reply into short lines so local TTS can start speaking after the first one
// (synthesis takes ~0.07 s per character and a clip only plays once it is complete).
constexpr int32 OKFreeTalkSegmentChars = 15;

// Cuts text into lines of at most OKFreeTalkSegmentChars: after sentence ends, else at a comma, else hard.
void OKStreetFreeTalkSplitLines(const FString& Text, TArray<FString>& Out);
void OKStreetFreeTalkSplitTexts(const TArray<FString>& Texts, TArray<FString>& Out); // one reply's lines together (how a line with no stop ends depends on whether it is the last)
// The segments as one line (no spaces in Chinese; spaces in English).
FString OKStreetFreeTalkJoinLines(const TArray<FString>& Lines);

// Key comes from DEEPSEEK_API_KEY, or Saved/StreetSocial/deepseek.json (gitignored).
// Where the DeepSeek settings (api_key, endpoint, model) are read from: OK_FREE_TALK_SETTINGS, the project's
// Saved/StreetSocial/deepseek.json, or the user's own copy (%LOCALAPPDATA%/OpenKoikatsu/deepseek.json), the first that
// exists. The key is a secret, so it is never in Content or git; the user's copy (refreshed from the project's)
// survives Saved/ being wiped.
FString OKStreetFreeTalkSettingsPath();
bool OKStreetFreeTalkLoadSettings(FOKFreeTalkSettings& Out, FString& Error);
// Matches the manifest display name against free_talk_personas.json, else _default.
bool OKStreetFreeTalkLoadPersona(const FString& Name, FOKFreeTalkPersona& Out, FString& Error);
// Stable for a girl: persona, rules and the reply format only.
// `World` is the street catalogue (places and activities), also stable.
FString OKStreetFreeTalkSystemPrompt(const FOKFreeTalkPersona& Persona, const FString& World);
// The last user message: her current values, then what the player said.
FString OKStreetFreeTalkUserMessage(const FOKFreeTalkStatus& Status, const FString& PlayerText);
// The chat messages for her turn: the system prompt, her conversation so far, this turn (sent through ok-mind).
TArray<TSharedPtr<FJsonValue>> OKStreetFreeTalkMessages(const FString& System, const TArray<FOKFreeTalkTurn>& Memory, const FString& UserMessage);
// False with Retry set when DeepSeek's JSON mode answered with blank content (worth one more try).
bool OKStreetFreeTalkParseContent(const FString& Content, FOKFreeTalkReply& Out, FString& Error, bool& Retry);
// Her turn as stored in memory: the same condensed JSON object the model is asked to produce. Plain-text
// assistant turns next to JSON mode make DeepSeek return blank replies.
FString OKStreetFreeTalkMemoryReply(const FOKFreeTalkReply& Reply);
// Prompt cache statistics from the response usage block, for the log.

// Per-girl memory, keyed by OKStreetStableId (free-talk-memory.json beside the relationship save).
bool OKStreetFreeTalkLoadMemory(const FString& Path, TMap<FString, TArray<FOKFreeTalkTurn>>& Out);
bool OKStreetFreeTalkSaveMemory(const FString& Path, const TMap<FString, TArray<FOKFreeTalkTurn>>& Memory, FString& Error);
// Over OKFreeTalkMemoryChars: drop the oldest exchanges down to OKFreeTalkMemoryKeepChars.
void OKStreetFreeTalkTrimMemory(TArray<FOKFreeTalkTurn>& Memory);

bool OKStreetFreeTalkLoadLog(const FString& Path, TArray<FOKFreeTalkLine>& Out);
bool OKStreetFreeTalkSaveLog(const FString& Path, const TArray<FOKFreeTalkLine>& Lines, FString& Error);

// Voice input goes through the local ok-asr server (Tools/ok-asr): OK_ASR_ENDPOINT or http://127.0.0.1:47861.
FString OKStreetFreeTalkAsrEndpoint();
// Hands-free silence: what is sent in place of the player's words when he says nothing for a while. The model
// sees it as his turn; the game shows and logs no line for him.
FString OKStreetFreeTalkNudge(int32 SilentSeconds, bool Leaving);
bool OKStreetFreeTalkIsNudge(const FString& Text);
// Starts the local ok-asr server (OK_ASR_SERVER, with the microphone from OK_ASR_SETTINGS) when it does not
// answer; at most once every 20 s. The server writes its own log beside its exe.
void OKStreetFreeTalkEnsureAsrServer();
// ok-asr replies {"ok", "text", "speech", "error"}; false when ok is false or the body is not JSON.
bool OKStreetFreeTalkParseAsr(const FString& Body, FString& Text, bool& Speech, FString& Error);
