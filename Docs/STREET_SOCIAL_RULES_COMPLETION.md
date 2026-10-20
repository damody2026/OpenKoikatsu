# Street social rules completion

Owner of this note: the relationship and dialogue rules in `Source/OpenKoikatsu/OKStreetSocialRules.h`. The automation wrapper `Source/OpenKoikatsu/Tests/OKStreetSocialRules.spec.cpp` only calls `OKStreetRunScenarios`. Scene, runtime, tools, and the catalogue exporter are outside this note. No engine build was run here.

`ExactOriginal` stays false on branch decisions, friendship, and confession. Pieces taken from the decompile are named below. Anything the street runtime does not have is a labelled neutral policy, not an original total.

## Source provenance

Decompile root: `C:\portable\KoikatuSunshine\kks_decomp`. Line numbers are the ones already checked against that tree. They were not re-walked for this note.

| Rule | Source | Street behavior |
| --- | --- | --- |
| Branch weights | `Info.CreateBranchADV` cases 1–4 and the default length table, `Info.cs` 2362–2424 | Same integer weights. Card attribute absent uses the source not-matched triple and is labelled neutral. |
| Preference compare | Cases 7–12 in the same function. Axes are `PreferenceValue` aggressive, diligence, kindness (`GameAssist.cs` 469–482) | Returns index 0 or 1 only. Raw table values are not clamped. Custom per-card offsets are not applied. |
| Indexer | `BranchInfo`, `Info.cs` 195–207 | Successes by `part_index` then id, then one failure, then one optional `no_interested`. A generic role is never an outcome. |
| Roll | `Info.RandomBranch` 2494–2501, then `ProbabilityCalclator.DetermineFromDict` (`Utils.cs` 921–936) | A weight of 100 wins before the roll. Otherwise ticket = `roll * sum / 100`. A ticket on a cumulative edge falls into the next bucket. |
| Case 41 | Same `CreateBranchADV` switch | Not applied. Reason `branch_rule_unsupported:41`. |
| Talk temper | Default branch when the condition id indexes `talkTemper` | Absent temper does not pick the success index (`talk_temper_absent`). |
| Friendship | `GetEndConversationADV` case 10, `Info.cs` 1911–1916. Non-NPC `TalkScene.TalkEnd` 2311–2318 adds `talkEvent` 0, sets favor 0 and `isFriend`, and does not set `isGirlfriend` | Stranger only, favor at or above loaded step-up 0. Explicit call, not a topic button and not an automatic promotion. |
| Confession chance | `TalkScene.CommandFunc` commands 5–11 decrease talk time by 1. Button 11 is command 6. `GetEventADV` relation 1 command 6, `Info.cs` 1422–1464, uses branch 0 outside map 9. `RangeValue.Get`, `GameAssist.cs` 125–131, reads stage 1 condition 0 `"20,70"` | Caller roll 0..99. Double lerp, trunc toward zero, clamp 0..100. Favor 0/50/100 is 20/45/70. Favor 70 is 55. |
| Confession offer | `ConfirmEvent` stage 1 calls `ConfirmEvent1` (`Info.cs` 1159–1168). Case 0, `Info.cs` 2592–2603: command 6 is absent, and `InverseLerp(0, stepUpDic[state], favor)` is not below 0.7 | Friend only, and `favor / StepUpFriend >= 0.7` in double math. Shipped friend cap 100 means 70.0. 69.99 is under. Favor above the cap stays eligible. |
| One attempt | The same `ConfirmEvent1` check hides the button once command 6 is in `talkCommand` | Success and failure both commit `stable\|event:confession` through `OKStreetCommit`. A second call is `duplicate_transaction` and does not decrement talks again. |
| ChangeParam | `GameAssist.cs` 432–455 | Hotel, prayer, player intellect/physical/hentai, and the attribute 1.5x multiplier are 0. The raw favor and lewdness delta is kept. |
| Favor clamp | `stepUpDic` after that correction | Loaded limits 0:75, 1:100, 2:150. Key 3 loads as `StepUpBeyond` and is not a street stage. No separate 100-point cap. Lewdness clamps at 100. |
| Occurrence confession | Heroine-initiated row, favor at least 100 and a 10% roll | Not implemented. |

No street path invents a source PCM or a voice file path. Confession `LineNode` is `INDEX_NONE`. Policy text includes `line_not_in_street_catalogue`. The friendship line is the stage-0 condition-id-10 generic farewell `p0_r110_farewell`, not greeting `p0_r6`. That farewell is not playable when its voice sequence is not fully matched.

## Exact versus neutral

Implemented from the decompile, still not labelled exact because the facilities above are skipped:

- Attribute and default `RandomBranch` weights, including weight 100 before the roll.
- Preference comparisons once that personality triple was loaded through `OKStreetSetPreference` or an optional rules object. Shipped `Content` `rules.json` has no preference. An unloaded table is `preference_evidence_missing`. A loaded table missing that personality is `preference_personality_missing`, not `{0,0,0}`.
- Friend confession gate `favor / StepUpFriend >= 0.7`. Missing or non-positive step-up, or `FavorCapIsKksStepUp == false`, disables the event with `stepup_limit_missing` and `ChancePercent == INDEX_NONE`. Below the ratio is `confession_favor_below_0_7`. The 20..70 curve stays callable through `OKStreetConfessionPercent` either way.
- Shared confession transaction for the success effect and the failure effect. Failure spends one talk, records memory `event:confession`, and does not change stage, favor, or flags. Success also sets lover, resets favor, and sets `talk_event:1` and `talk_event:2`. Preview does not mutate. Talks remaining below 1 rejects both a success roll and a failure roll as `talk_budget` with no token.
- Stranger friendship at the loaded stranger cap: stage friend, favor 0, `talk_event:0`, memory `event:friendship`, transaction `stable|event:friendship`, talk cost 0. A friend or lover is `friendship_requires_stranger`. Repeat favor wipe is not applied (`repeat_wipe_not_applied`). Reaching a cap inside `OKStreetCommit` does not promote.

Neutral, and never called exact (`OKStreetFacilityPolicy` is `hotel=0;pray=0;player_stats=0;attribute_custom=0;map9_not_used;not_exact_original`):

- Hotel confession +20, prayer, map 9 branch index 1, player stats, attribute custom offsets, attribute 1.5x.
- Occurrence-table confession.
- Branch condition 41 and any other id that is not a verified weight or preference case (`branch_rule_unsupported:<id>`).
- Topic cooldown. `OKStreetTopicCooldownIsKks` is false. `UntilDay == Day` is not cooling; the test is strict `<`. Day starts at 1. A committed topic records `topic:<id>` cooling until `Day + CooldownDays` and does not promote. Zero favor does not require step-up.
- Ordinary branch talk cost. Confession's cost of 1 is the command decrement, not a claim that every street line costs one talk in KKS.
- Catalogue condition ids other than -1 (always) and 0 (never) stay disabled. Branch roles are not chosen by the topic list (`branch_role_unsupported:<role>`).

Confession policy appends `hotel_confession_not_added;pray_not_forced;map9_branch_not_used;line_not_in_street_catalogue;confirm_event1_ratio=0.7`.

## Save schema

Schema stays 1. Schema 2 saves are rejected. Optional `topics` and `cooldowns` (`id`, `until_day`) are backward compatible: a missing field loads empty, an empty id or `until_day < 0` rejects the save as `save_relation_arrays_invalid`. Session-prefixed relations are omitted.

The confession and friendship guards persist as entries in `committed`, not as cooldown rows. A failed confession writes `heroine|event:confession`, talks remaining, stage, and favor, and writes no cooldown id. `OKStreetParseSave` replaces the store, reloads step-up when the save has it, and does not restore preference or an overridden confession curve. After a load the caller reloads those rules. The curve default that comes back is still 20..70.

Completion catalogues may carry branch objects and extra node fields. The parser keeps the schema-1 node shape (integer personality, source object, conditions object) and reads extra fields only when present. It does not reject the current 1230-node schema-1 catalogue.

## Signatures

```
FOKStreetBranchDecision OKStreetDecideBranch(const FOKStreetCatalogue&, const FOKStreetStore&, const FOKStreetCharacterMeta&, const FOKStreetBranchQuery&, int32 Roll0to99);
FOKStreetBranchDecision OKStreetDecideNode(const FOKStreetCatalogue&, const FOKStreetStore&, const FOKStreetCharacterMeta&, int32 NodeIndex, int32 Roll0to99);
FOKStreetCommitResult OKStreetCommitDecision(FOKStreetStore&, const FOKStreetCharacterMeta&, const FOKStreetBranchDecision&, int32 Budget);
FOKStreetEventResult OKStreetPreviewFriendship(const FOKStreetStore&, const FOKStreetCharacterMeta&, int32 Budget, const FOKStreetCatalogue*);
FOKStreetEventResult OKStreetCommitFriendship(FOKStreetStore&, const FOKStreetCharacterMeta&, int32 Budget, const FOKStreetCatalogue*);
int32 OKStreetConfessionPercent(const FOKStreetStore&, float Favor);
bool OKStreetConfessionRatioMet(const FOKStreetStore&, float Favor);
FOKStreetEventResult OKStreetPreviewConfession(const FOKStreetStore&, const FOKStreetCharacterMeta&, int32 Budget, int32 Roll0to99);
FOKStreetEventResult OKStreetCommitConfession(FOKStreetStore&, const FOKStreetCharacterMeta&, int32 Budget, int32 Roll0to99);
int32 OKStreetWeightIndex(const TArray<int32>& Weights, int32 Roll0to99, FString& Reason);
FOKStreetCommitResult OKStreetCommit(FOKStreetStore&, const FString& StableId, const FString& Transaction, const FOKStreetEffect&, int32 Budget);
FString OKStreetWriteSave(const FOKStreetStore&);
EOKStreetLoad OKStreetParseSave(const FString& Json, FOKStreetStore&, FString& Error);
bool OKStreetTopicCooling(const FOKStreetRelation&, const FString& Id);
```

`OKStreetCommitConfession` previews first. If the preview is not resolved it returns that result and does not touch the store. Otherwise it calls `OKStreetCommit` once with `OKStreetTransactionId(Stable, "event:confession")`. There is no untracked `TalksRemaining` decrement.

## Tests

`OKStreetSocialRules.spec.cpp` is unchanged. `FOKStreetSocialRulesTest` runs `OKStreetRunScenarios`, which includes `OKStreetCheckRelationship`. This pass did not launch the automation test or an engine build.

Checks that lock this slice:

- Weight edges for 20/40/40, roll 100 rejected, and a weight of 100 selected before the roll, including a later 100.
- Confession percent 0/50/100 is 20/45/70 with no hotel +20, including on a store whose step-up rules are missing. Percent at 70 is 55.
- Missing step-up disables confession and does not mutate. Favor 0, 50, and 69.99 are `confession_favor_below_0_7` with `ChancePercent == INDEX_NONE`, talks unchanged, and no token. Favor 70 previews success at 55 percent, `LineNode == INDEX_NONE`, not playable, not exact, and does not mutate.
- Talks remaining 0 rejects roll 0 and roll 99 as `talk_budget` with no token. A failure at favor 70 roll 80 spends one talk through `heroine|event:confession`, stays friend at favor 70, and sets no flags. The next failure is `duplicate_transaction` and does not decrement again. That token round-trips in a schema-1 save and does not create a cooldown id.
- A separate store at favor 70 roll 0 becomes lover, favor 0, both talk events, same transaction, one talk spent. A later call does not wipe the restored lover favor or spend another talk. A stranger creates no relation.
- An overridden 100/100 curve at favor 70 succeeds on roll 99. An overridden 0/0 curve at favor 70 fails, spends one talk through the same transaction, and leaves favor at 70.
- Favor 100 roll 80 still previews failure at 70 percent. Friendship preview and commit use `p0_r110_farewell`, do not spend a talk, and do not promote a friend or lover. Commit to the stranger cap does not promote by itself.
- Topic cooldown blocks eligibility, loses to talk budget in the reason order, survives schema 1, and ends when day reaches `until_day`. Topic 33 committed on day 1 with two cooldown days round-trips cooling until day 3. An old save without topics or cooldowns still loads. Schema 2 is rejected.
- Preference without evidence does not pick a success. Another loaded personality is not a zero row. A generic multi-part container is `not_a_branch`. Passing a success node does not force that success. The topic list still refuses a success role.

### Primary integration correction
The helper now requires an explicit AttemptTransaction. Duplicate protection applies to one confirmed attempt, not every confession over a character's lifetime. A later attempt after rejection may succeed; the scene already uses a fresh GUID per confirmation. Added a test for this distinction. Grok's fixed `heroine|event:confession` token is a fixture token, not a production lifetime key.
