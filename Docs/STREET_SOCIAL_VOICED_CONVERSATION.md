# 街頭語音交談與好感回饋

2026-10-07。這是候補統一與實際好感回饋，不是完整 GUI 驗收。本檔不宣稱 Development、Editor 或六人場景已經跑過。

## 候補

提示與 E 使用同一個純判斷 `OKStreetTalkCandidateEligible`，再由 `OKStreetSelectTalkCandidate` 取嚴格小於 `OKStreetTalkRangeCm`（250cm）的最近者。距離相同保留較早的候補。視線仍是角色高度 120cm 的 `ECC_Visibility` 追蹤；沒有改成依鏡頭方向排序。

不合格：女性 metadata 無效、狀態查詢失敗、runtime error、stale、尚未 Ready 的載入、未 Visible、動作未就緒、超出或恰在交談距離、視線被擋。提示約每 0.15 秒重算，只用該幀已經查過的 runtime 狀態，不另讀卡片檔。按下 E 用同一幀狀態再選一次，不沿用舊提示 slot。`BeginTalk` 仍對該 slot 重新查狀態並重做視線追蹤。

可見但動作未就緒時，`BeginTalk` 仍顯示「這位角色的動作仍在背景準備，請稍候。」提示與 E 不會選這種角色。

## 完成與好感

Tick 仍只在 `EOKRuntimeSpeechState::Completed` 時呼叫 `FinishLine`。`FinishLine` 會再查一次；不是 Completed 就走「Dialogue did not complete audio」並 `EndTalk`，不存檔、不加好感。

原生狀態：Preparing 開始解碼，Playing 開始播放，Completed 只在已有音訊 frame、音訊元件存在且不再播放的同一刻清掉 wave。Cancel 在還有 job 或 wave 時標 Cancelled。解碼失敗或沒有音訊裝置標 Failed。過期 speech epoch 被丟棄，不會變成 Completed。`!SpeechActive` 本身不是完成。

開場、道別、取消、存檔失敗與語音失敗不加話題獎勵，也不顯示增減。成功原子存檔的日常、戀愛、性愛話題才把 Message 換成實際關係前後值，不是列上的 effect、也不是剛才的字幕。街道非分支 +1 與分支原效果都沒改，也不強迫成功骰子。

關係事件仍走原提交。它不是話題獎勵清單的一員。

## social.validation.json

既有欄位保留。新增：

- `positive_favor_commits`：清單中實際好感上升的筆數。
- `favor_save_verified`：五人話題階段存檔回讀與清單一致時才為 true。關係自我測試會再改第一位 NPC，所以這個旗標在那之前判定。
- `voiced_topic_effects`：最多 32 筆。每筆有 `slot`、`id`、`node`、`category`、`transaction`、`voice_ids`、`favor_before`、`favor_after`、`lewdness_before`、`lewdness_after`、`audio_completion`。只有 Completed 的日常、戀愛、性愛話題。開場、道別、取消與失敗不在內。

schema 1 關係存檔格式不變。`full_acceptance` 仍是 false。

## 測試

新自動化測試：`OpenKoikatsu.StreetSocial.TalkCandidateEligibility`。既有 `OpenKoikatsu.StreetSocial` 篩選會多這一項。建議 `-MinimumTests 10`（原 3 個 StreetSocial、2 個 Face、1 個 SkinEnvelope、3 個 ShadowSubmit，再加這 1 個）。

Runtime 證明在非 retarget 分支：15 筆話題、至少一次實際好感上升、計數與差值一致、交易存在於 `selftest.relationships.json`、slot 2 到 5 的最終好感／親密與最後一筆一致。slot 1 之後會被既有關係事件改寫，所以不拿最終檔比它的話題後值。Retarget 路徑不要求這些話題欄位。

## Primary validation update

The earlier unbuilt status refers to the worker handoff. Primary Development/Editor builds, Python26 and Unreal10 tests, and the real six-APNG runtime now passed. Fifteen voiced topics matched their source audio; ten increased favor, with saved roundtrip verified. Public CMD now uses StreetVoiced01. See STREET_SOCIAL_VOICED_CONVERSATION_VALIDATION.md for evidence and limits. Full acceptance remains deferred.
