# Street Social facial transitions

Per-channel blend for the private brow, eye, and mouth patterns already on the runtime face stream. This is the source `blend` flag and the 0.15s linear pattern change. It is not a port of blush, tears, highlight, gaze, or neck IK. This note does not claim that the module was compiled or that a scene was run. Root owns automation, the Development host, and runtime assertions.

## Parse

`OKStreetParseExpressions` reads `brow`, `eyes`, and `mouth` objects.

- The object is present and `blend` is absent: that channel's flag is true.
- The object is present and `blend` is a JSON bool: that value is kept.
- `blend` with any other JSON type fails the catalogue. The error is `Expression blend invalid`. Pattern, opening, and blink checks are unchanged.
- A missing channel stays pattern `-1` and blend false. Openings are still plain numbers and do not turn a blend flag on.

`FKKRuntimeFaceState::MouthBlend`, `EyeBlend`, and `BrowBlend` default to false. Callers that never set them keep an immediate apply. The street scene passes the parsed `Expression->Face` through the existing expression request, so only a real pattern object asks for a transition.

## Clock

`OKRuntimeFaceTransitionSeconds` is 0.15. Each channel has its own clock. A true flag eases that channel. A false flag snaps it. The same pattern, opening, blend, and catalog does not restart the clock, including while it is still moving. A real change samples the current weight and the remaining legacy contribution, then replaces that channel's from/to pair. Earlier endpoints are not kept.

A hitch is capped at one second inside `OKAdvanceRuntimeFaceTransition`. A nonfinite or negative dt leaves the transition, the upload plan, and the counters unchanged.

## Weights

The stored value is two coefficients per catalog morph, not a stack of poses. For morph `i`, the live coefficient is `A[i] + L * B[i]`.

- Mouth: `L` is the current audio opening and the scale is speech gain. `MouthOpening >= 0` is baked into `B`; otherwise the expression scale is 1. Gain 0 contributes no mouth delta.
- Eyes: `L` is the current blink. `EyeOpening >= 0` uses open amount `EyeOpening * (1 - blink)`. `EyeOpening < 0` blinks from the card toward the selected close.
- Brows: `L` is unused. The open amount is `BrowOpening`, or 1 when that opening is absent. Blink does not move brows.

Pattern `-1`, or a close/open pair the catalog does not contain, stores an empty weight. The evaluator then writes no morph for that channel, which is the current card-channel policy. It does not hold the previous expression. While a channel is actually easing, its weight is the lerp of the from and to coefficients at the live `L`. A channel that has already snapped or settled uses the existing authored evaluator.

Order stays eyebrow, then eyes, then mouth. The same native morph keeps the later controller. Body skin stays on the GPU path. One evaluation fills the position buffer that `OKUpdateRuntimeMouthStream` copies to both the color mesh and the depth mesh.

Leaving the disabled face route is not the source `backPtn = NowPtn` snap. A blend that starts while playback is off eases from the historical `nullptr` contribution: `lerp(MouthClosed, MouthOpen, audio) * gain` for the mouth and `BlinkClosed * blink` for the eyes. Brows have no historical delta. Those legacy terms fade only on channels that are easing. A blend-false channel, including an omitted pattern `-1`, drops its historical term immediately. At elapsed 0 a blend from that route matches the `nullptr` face. After it settles, the positions match the authored face and not the `nullptr` face.

## Upload and counters

The crowd tick still quantizes with `SceneTime * 60`. While any channel is timing, a new bucket requests an upload even if the mouth, blink, and gain did not move. The same bucket does not upload again unless the expression is dirty, the gain changed, or the endpoint is waiting. The frame that reaches 0.15s uploads that endpoint once. After `OKNoteRuntimeFaceUpload` consumes it, a settled face returns to the old rule: bucket changes alone do not upload.

`OKPlanRuntimeFaceUpload` advances the clock before the scene uploads. `started` and `completed` count channels. `intermediate` counts an upload that still has a moving channel whose elapsed time is already past 0. `cancelled` counts a reset that finds any channel still moving. An identical request does not increment `requests` or `started`. `face_expression_updates` still counts only a dirty expression upload, not every speech sample.

JSON names, on each runtime character and as scene totals, are `face_transition_started`, `face_transition_intermediate`, `face_transition_completed`, and `face_transition_cancelled`. `FOKRuntimeFaceStatus` exposes the same four counters. Reset clears the transition, sets playback disabled, and the next upload uses the `nullptr` face. There is no callback to retire.

## Limits

- At most 512 morphs per catalog control (`OKRuntimeFaceMorphLimit`). A larger set, or a close/open index outside that baseline, rejects the request and leaves playback, buffers, and counters unchanged.
- If the stream's catalog pointers change, the new blend starts from zero weights or from the historical face. Old morph indices are not remapped.
- A failed GPU upload can happen after the clock has already stepped. The scene treats that as a character failure.
- No new import, persistence, or precompute pass was added. Private card morphs stay on the runtime stream.
- Compile and the street-social automation and runtime passes were not run here.

## 主代理整合與實測（2026-10-07）
已完成獨立審查、Development StreetBlend03及Editor-streetblend20編譯。Python26項通過；Automation-Blend02四項（FacePatterns、FaceTransitions、SourceExpressions、RulesAndRealCatalogue）全部通過。
主代理修正Unreal TryGetBoolField的數字轉bool問題：blend必須先HasTypedField<EJson::Boolean>才讀取。首輪Automation-Blend01的失敗及StreetBlend02遇到外部Rust語法錯誤，均保留證據並記入STREET_SOCIAL_ERRORS.md；未發布失敗版本。
Runtime-BlendR2於正式StreetSocial map使用六張APNG runtime建立，沒有卡片Cook：26次來源表情、零缺漏；64次通道過渡開始及完成，245次實際中間更新，五NPC全部有過渡並最終active=false。15話題+5開場+5道別、拒絕、語音嘴型、男女私人action、播放取消與關係保存回讀皆通過。
此流程中的音訊取消發生於表情過渡完成後，runtime face_transition_cancelled=0是正常結果；過渡中的取消、重定向連續性及非法請求原子拒絕已由純FaceTransitions測試驗證，不宣稱本轮畫面流程實測了過渡中取消。
六人1920x1080 RenderOffscreen、10000樣本：整幀平均3.404ms、P956.122ms，Game1.445ms、Render3.376ms、RHI1.112ms、GPU1.722ms；各階段重疊不可相加。全部顯示8.657秒。這是功能流程量測，非百人、正式視窗或無背景負載的性能驗收，亦不足以單次比較宣稱效能改善。
已檢視street.dialogue.png，未見本輪造成的大面積頂點爆開；角度與距離有限，不取代近景／逐角度CPU-GPU完整外觀驗收。
Tools/publish_street_social_receipt.py已依實際通過的證據備份並原子更新公開receipt。StreetSocial.cmd使用StreetBlend03.stage；原百人CMD未修改。正式視窗、冷暖啟動、長時間、所有性格與CPU/GPU畫面仍按原計畫最後驗收；腮紅／淚／高光／視線／頸IK與完整ADV不在本輪補完範圍。
Grok Build grok-4.7-build-fast：job run-muxuzp0m-0ucu8q，thread0e7daa46-ccc0-4845-991d-e7d30340e027，16m12s；input220077、cached2899456、output109581、reasoning83068、total3229114 tokens，24 calls，reportedUSD1.77911256。前次15m02s無進展worker由主代理取消，usage不可得，未當作成功。
