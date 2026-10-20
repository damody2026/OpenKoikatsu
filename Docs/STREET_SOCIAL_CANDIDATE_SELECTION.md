# 街頭交談候補：鏡頭優先與 Tab

2026-10-07。這是已核准計畫第 4 節的候補排序與明示切換。不新增好感規則，不改 schema 1 存檔，不把視線或頸部 IK 算進這次。本檔不宣稱 Development、Editor 或六人場景已經跑過。

舊說明 `STREET_SOCIAL_VOICED_CONVERSATION.md` 裡「只取 250cm 內最近者、約 0.15 秒重掃、E 不沿用舊 slot」已被這份規則取代。合格條件沒有放寬。

## 誰可以成為候補

`OKStreetTalkCandidateEligible` 仍要全部成立：

- slot 1 到 5，而且是女性 metadata
- 這一幀的 runtime 狀態有查到，不是 stale，沒有 runtime error
- Ready、Visible、ActionsReady
- 與男角的水平距離嚴格小於 `OKStreetTalkRangeCm`（250cm）；站在 250cm 上不算
- 視線沒被擋住。追蹤仍是男角與 NPC 各升高 120cm 的 `ECC_Visibility`，只在距離已小於 250cm 時做，不另讀卡片

載入中、淡入、動作未就緒、男性、被擋住、超出距離，都不進排序。

## 排序

同一批合格樣本先算鏡頭水平前向，再排一次。Tick、Tab、E 和 runtime self-test 都呼叫 `OKStreetResolveTalkSelection`。`OKStreetSelectTalkCandidate` 只留作距離相容：相等距離仍保留較早的那一筆，畫面不再用它決定提示或搭話。

鏡頭水平前向 `OKStreetTalkCameraHorizontal`：

1. 取攝影機前向的 XY。長度大於 `OKStreetTalkForwardMin`（0.0001），且 X/Y 都是有限值時，正規化成單位向量。Pitch 只留在 Z，不參加點積。
2. 否則視為退化，改用軌道 yaw。`FRotator(0, yaw, 0)` 的前向再取 XY。yaw 不是有限值時先當成 0。
3. yaw 前向仍退化時，固定用 +X。結果會標 `UsedYawFallback`。

對齊分數是這個單位前向與「男角到 NPC 的水平 Offset」單位向量的點積，範圍 [-1, 1]。Offset 的水平長度不大於 0.0001，或點積不是有限值時，分數是 `OKStreetTalkAlignDegenerate`（-2），比任何真正的背面還差。

`OKStreetOrderTalkCandidateSlots` 只收合格樣本，由好到差：

1. 對齊分數較高者優先。兩者差距的絕對值小於或等於 `OKStreetTalkAlignEpsilon`（0.0001）才算對齊相同。
2. 對齊相同時，`DistanceCm` 較小者優先。距離用精確比較，不另設距離誤差。
3. 距離也相同時，slot 數字較小者優先。
4. slot 仍相同時，輸入陣列裡較早的那一筆優先。
5. 同一個 slot 出現多次時只留下較好的那一列。

所以正前方較遠的人會贏過側邊或背後較近的人。正前方彼此的對齊落在誤差內時，才改比距離。距離與對齊都相同時，不靠掃描時機，而靠較小 slot，其次才是輸入順序。

沒有明示選擇時，每一幀把提示改成這份順序的第一名。不再用 0.15 秒計時器覆蓋結果。

## Tab、保持與 E

`OKStreetResolveTalkSelection` 的規則：

- Tab 在這份順序裡往下一人，最後一人回到第一人，並把選擇標成明示。目前的人已經不在名單裡時，Tab 落在新的第一名，同樣標成明示。只有一人時 Tab 仍停在她身上。
- 明示選擇只要還在這一幀的合格名單裡，鏡頭轉向或別人變得更近都不會換人。
- 她不再合格時，明示標記清掉，提示改成新的自動第一名；沒有任何人時提示回到沒有對象。
- E 只重新檢查「這一幀按下之前，提示上的那個 slot」是否仍在剛算好的合格名單。還在，就只把這個 slot 交給 `BeginTalk`。不在，就刷新提示、不承認搭話，而且同一下按鍵不再順便 Tab。
- `BeginTalk` 仍會自己再查一次 runtime 狀態並重做視線追蹤。這是最後一道准入。它拒絕時不會改去和另一個人說話。可見但動作未就緒時，仍只顯示「這位角色的動作仍在背景準備，請稍候。」
- 交談真正結束（`EndTalk` 時已有對象）才清掉明示選擇。Esc 結束交談也走這條路。沒有對象時按 Esc 不會清掉 Tab 選定。

提示文案：

- 一位：`E 與 {姓名} 交談`
- 兩位以上：`E 與 {姓名} 交談；Tab 切換`
- 零位：保留原本的載入或就緒句子；只有從已有對象掉回無人時，才改回移動說明

狀態只加三個欄位：`Candidate`（目前 slot）、`CandidateCount`（合格人數）、`CandidateExplicit`。報告多一個 `candidate_selection_checked`。存檔 schema 不變。

## Runtime self-test

`-StreetSocialSelfTest` 在原本 15 個話題之前，用實際五名 NPC 與場景攝影機走同一組 helper：

1. 男角放在出生點，yaw 90、pitch 0。slot 1 在前方 200cm，slot 2 在背後 80cm，slot 3 在側邊 80cm，另外兩人移出 250cm。三人動作就緒後才判斷。
2. 這一幀 Tick 已經用鏡頭前向選人。必須選中較遠的 slot 1，合格人數 3。舊的最近 helper 在這組距離上仍會得到 slot 2。
3. 把 slot 1 標成明示，再把 yaw 轉到 0。下一幀自動第一名變成側邊的 slot 3，但 Tick 仍保持 slot 1。
4. 同一 helper 從明示的 slot 1 連做三次 Tab。yaw 0 的順序是 slot 3、2、1；第一次就是從名單最後一人繞回第一人。
5. 把 slot 1 移出 250cm。樣本必須不再合格。對她按 E 只刷新、不呼叫會開始另一個人的 `BeginTalk`。
6. 把她放回前方，E 重新核准 slot 1，`BeginTalk(1)` 的對象必須是 1。接著立刻 `EndTalk`。
7. 比對關係字串、存檔檔案有沒有被建立或改寫、交易、好感提交、完成句數、開場／道別、分支、婉拒與表情拒絕次數。然後把男角、五人位置、Held、移動模式、鏡頭 yaw／pitch／距離與旋轉放回探測前。

通過後 `candidate_selection_checked` 才是 true。探測失敗會留下錯誤並還原位置，既有 15 個話題仍繼續；最終 `passed` 不會因為這個旗標被假裝成成功。`-StreetSocialRetargetSelfTest` 不做這段，也不要求這個旗標。

`Tools/test_street_social_runtime.ps1` 的非 retarget 分支要求 `candidate_selection_checked` 為 true。`RuntimeGpuCacheDirectory`、`RetargetSelfTest`、臉部、陰影與 15 個話題的檢查都還在。

## 測試名稱

新的自動化測試是 `OpenKoikatsu.StreetSocial.TalkCandidateCamera`。既有 `OpenKoikatsu.StreetSocial` 篩選會多這一項。先前 10 項的組合現在是 11 項：StreetSocial 5（含原本的 TalkCandidateEligibility）、Face 2、SkinEnvelope 1、ShadowSubmit 3。建議 `-MinimumTests 11`。

`OKStreetCheckTalkCandidateCamera` 覆蓋：前方勝近側與近背、對齊誤差內改比距離、同距離由較小 slot 決定而最近 helper 仍留較早樣本、Tab 循環與回繞、鏡頭改變只移動自動選擇、明示選擇在鏡頭改變後保留、失效的自動與明示 slot 被排除、E 不承認失效提示也不改認別人、單人與無人、垂直鏡頭改走 yaw、非有限前向與 yaw 落到 +X、重疊在同一點的 Offset 不會靠對齊獲勝。阻擋、載入中與男性樣本不會贏過合格的側邊女性。

## 這次沒有做

沒有編譯、沒有啟動 Unreal、沒有 Cook、沒有改引擎、shader、插件、Rust 或其他來源。沒有視線／頸部 IK，也沒有把這次當成街區最終驗收。Root 仍負責 Development isolated host、Editor、原生自動化與實際六人 runtime。

## Primary UI focus correction
自由行走時常駐語音按鈕不取得鍵盤焦點，讓 Tab 用於候選切換；開始交談後保留按鈕既有鍵盤焦點能力。滑鼠與 L 語音切換維持。這是來源審查發現的輸入路由保護，人工視窗操作仍在最後驗收。

## Primary verification completed — 2026-10-07

Primary reviewed the 559-line incremental worker diff against saved before-files, plus the bounded UI focus correction. Candidate ranking, Tab, E and live self-test share the same helper. Final runtime success supersedes the earlier worker-only statement.

- Development StreetCandidate01 and StreetCandidate02 compiled; final Editor StreetCandidateEditor02 compiled (8.83 seconds). Python 26 tests passed independently. Native automation StreetCandidate01 and final StreetCandidate02 each passed 11 tests, including TalkCandidateCamera, eligibility and identity regressions.
- Runtime-StreetCandidate01 and final Runtime-StreetCandidate02 passed on actual six APNG actors in the new scene, with RequireFaceTransitions, RequireShadowProxy and RequireCleanShadows. No private per-card Cook assets were introduced. The final report has candidate_selection_checked=true, 15 completed topics, five greetings, five farewells, 11 positive favor commits, favor_save_verified=true, active_actor_interrupt_commits=0 and no errors.
- Independently verified all 15 topic APNG identities, personality/category and exact voice-ID sequences against prepared Chinese/Japanese catalogues. Evidence: Saved/StreetSocial/Runtime-StreetCandidate02/voice.reward.validation.json. Candidate selection itself did not create/rewrite the relationship file or submit a transaction; interrupted speech committed no reward. Tests use isolated saves, not the user's relationship save.
- Final six-person 1920x1080 offscreen run, 10000 frame samples: all visible 8.3463 seconds; mean frame 4.0153 ms, P95 8.6407 ms; GameThread 1.9494 ms, RenderThread 3.9533 ms, RHI 1.2888 ms, GPU 1.7978 ms. These stages overlap and must not be summed. The cache was populated. P95 exceeds 8.33 ms; this is functional evidence, not stable 120 FPS, a fresh cold load, a controlled comparison or hundred-person acceptance. No build/worker job was running concurrently with this final test, but unrelated system load was not controlled.
- Published StreetCandidate02.stage using the successful runtime report; public StreetSocial.cmd ValidateOnly passed. SHA256: fc607cf63d4e3b8935412292127869e0db8a219f5fe732564017e37584c6093d. Receipt full_acceptance=false. Original hundred-person CMD and identity retarget paths preserved. No commit/push or unrelated source changes.
- Manual Tab input/GUI, manual listening, CPU/GPU visual comparisons, long-run and full performance acceptance remain for the final acceptance stage. The native live probe exercises production candidate logic; it is not an OS-key injection or a claim of manual GUI completion.

### Grok job and review

Grok Build grok-4.7-build-fast job run-muy94js7-099t9x; thread 8581e5d8-75a0-447d-b556-c3144f185544; terminal completed, 19m11s. Input 320981 tokens, cached input 3505536, full input 3826517, output 114000 (reasoning 94231), total 3940517, 35 calls/turns; reported cost USD 2.13262756. Worker edited bounded source/tests/docs and ran Python/syntax checks; primary independently reviewed, corrected free-roam button focus, compiled, validated and published. Review decision: current functionality accepted, final acceptance deferred.
