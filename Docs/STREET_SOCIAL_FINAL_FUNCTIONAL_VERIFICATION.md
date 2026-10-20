# StreetSocial 本階段功能核對 2026-10-07

範圍：正式1男+5名女角APNG街區demo，使用者要求最後再完整驗收；本紀錄不把自動功能測試當成正式視窗人工驗收。

已跑：Python資料／表情26項全部通過；Unreal Automation-FinalAudit01的8項規則／臉部／骨架範圍／陰影提交通過；正式街道和百人launcher預檢通過。

正式Submit03執行檔與StreetQueueReuse01.patch，FinalCold01使用全新獨立Saved/StreetSocial/AcceptanceColdCache20261007，沒有清空或覆寫使用者GPU快取。全部6人就緒12.245秒；平均3.341ms、P955.747ms；15話題+5開場+5道別、語音嘴型、來源表情轉換、取消／關係存檔、自有APNGGPU與shadow代理斷言通過，GPU錯誤0，VSM overflow0。Grok審查同時執行，這不是受控效能達標證據。

測試工具test_street_social_runtime.ps1新增可選RuntimeGpuCacheDirectory，讓冷／暖測試隔離使用者快取；不修改預設CMD、角色數、画質與4GiB預算。預设行为不变。

完整驗收仍需實際視窗鍵盤／滑鼠與遮擋操作、逐角度CPU/GPU／臉／飾品外觀對照、取樣人工聽音訊語意、跨次啟動與長時間資源穩定及至少三輪負載可重現profile。現有平均/P95不足以證明所有幀120FPS，更不是百人街區驗收。

Grok read-only implementation audit job run-muy6a9eh-s3fgkq，thread6c4f5e75-1cc5-4417-bf76-c5eab7a48434，報告與root獨立決定待完成。


## Native identity retarget / active-actor interruption accepted

Grok Build grok-4.7-build-fast worker `run-muy6p4gy-ypl7cr`, thread `6c4f5e75-1cc5-4417-bf76-c5eab7a48434`, completed in 27m01s. Reported input452338 / cached7493504 / output155787 tokens, total8101629, USD3.88186636. Root reviewed the bounded 805-line diff against pre-worker copies; no acceptance was based on the worker's completion claim.

- Development `StreetRetarget02` and Editor `StreetRetargetEditor01`: succeeded. First Development attempt failed on an unrelated transient Rust nested-Arc mismatch; no Rust repair was applied by this agent. Details in STREET_SOCIAL_ERRORS.md.
- Python26 tests passed; Unreal `Automation-StreetRetarget01` nine tests passed, including native IdentityRetarget.
- Runtime-StreetRetarget01: native manifest requests, atomic fixture migration, every relation field preserved, one trace retained, in-process replay idempotent, six APNG GPU actors ready in10.730s. No per-character Cook asset loaded.
- Runtime-StreetRetargetDialogue01: five female NPCs + male,15 topics/5 greetings/5 farewells, source face transitions/private actions/audio/mouth updates, one active-actor fault cancellation with zero extra commits; relationships save roundtrip passed, GPU failures0, VSM overflow0.
- RetargetPersistence-StreetRetarget02: three separate native launches passed (persisted replay, request-ID mismatch, destination collision). Fixtures remained byte-for-byte and timestamp unchanged; rejected requests never admitted runtime actors. A simulated later saved favor13.5 survived replay. User relationships.json remained absent. Initial harness assumption about UE startup exit code corrected and recorded; failure reports are authoritative alongside no admission.

Current published executable: `C:/portable/OpenKoikatsu/Saved/StreetSocial/Build/StreetRetarget02.stage/Windows/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe`. SHA256 `fced5dea6eceb553341e27b0fb276609506044777d271a96e0f30318fc5842f4`. Public `StreetSocial.cmd` and optional `-RetargetFile` preflight passed; existing hundred-person CMD preflight passed unchanged. Default launch does not reassociate cards automatically. Core save and migration execution are native runtime; Python launcher only prepares the selection and explicit request file.

New Development six-person1920x1080 offscreen test: all visible11.968s;10000 frame samples, mean5.851ms/P9511.037ms, GameThread2.900ms, RenderThread5.724ms, RHI1.878ms, GPU2.088ms. This is a functional run with other workspace activity, not a controlled performance comparison. P95 exceeds8.33ms; do not claim stable120FPS or hundred-person acceptance.

Current functionality is accepted. Full GUI/long-run/content/visual/performance acceptance remains deferred per user instructions; `full_acceptance=false` remains correct.
