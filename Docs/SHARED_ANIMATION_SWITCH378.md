# Stage378 共用動畫切換的失敗復原

## 已確認的根因與修正

`PlaySharedAnimation` 原本只以候選 driver 做 Initialize，成功後即重設時間並呼叫 RebuildCharacter。Rebuild 先銷毀舊部件、重建第二份 driver，首次 Evaluate 若失敗，舊 renderer／solver 已無法復原。初始化成功不代表完整 palette 與 render targets 可以求解；因此不是卡片或某份動畫的特例。

現在候選 driver 只初始化一次，舊 driver 與 renderer 保留。暫時移除舊部件的 ImportedCard tag，建立候選部件並求解完整零秒姿態；Evaluate 成功才提交新 driver、時間及部件，最後套用獨立嘴型。失敗則銷毀候選、還原舊 tags／資源引用，不改舊時鐘、播放狀態、姿態或嘴型權重。舊曲線以強引用保護整個切換期間。

Palette slot 檢查改為 `Slot >= Matrices.Num()/16`，避免先乘 16 的整數溢位，並回報 renderer 名稱。沒有修改 Rust 動態骨步進、曲線／相位、FBS 求解、資產 wire format 或卡別補丁。原作曲線仍是共用 immutable 資源，每個角色保有自己的 solver；此處修正 Unreal 封裝的生命週期。

## 驗證方式

真實保存的男女兩卡，共用原作 walk／stand 曲線，在沒有原遊戲、內建資源包、Tools 或 Source 的 detached fixture 測試。刻意把一個非 rigid part 的 palette slot 改成 1000000，使 Initialize 成功、首次 Evaluate 拒絕，呼叫後立即還原 fixture metadata。檢查舊資源、clock、playing、嘴型 curve、renderer 物件、完整 component-space pose、全部 mouth morph 權重以及既有 solver 的 seek；再切換不同 authored curves 並繼續播放。

RedSourceR1／DetachedRedR1 保存修改前的來源及 DLL。Stage378DetachedRedR1 Automation 失敗，確認原部件遭銷毀、clock 改變與既有 solver 不可 seek；程序 actual exit 0 不能當作測試通過。

## 編譯與部署狀態

執行時偵測到主專案 Unreal Editor（PID 52396，OpenKoikatsu.uproject -skipcompile）仍在使用主 DLL，因此當時未關閉或覆寫該 Editor。使用獨立 BuildHostR1 編譯原 plugin C++，Rust library 為既有檔案的 byte-identical 副本；本輪沒有修改 Rust。BuildRedR1 被 Unreal Live Coding 的同 executable 全域 mutex 擋下；經查 UE HotReload.cs，僅獨立輸出使用 -NoHotReloadFromIDE，沒有繞過主專案的 DLL 使用保護。BuildRedR2、BuildGreenR1／R2 皆成功。

隨後偵測該 Editor 已結束，正常執行主專案 Development Editor BuildProductionR1（不使用 -NoHotReloadFromIDE），actual0。主專案 DLL 已重新編譯；DetachedProductionR1 保存同一組正式 DLL 的副本。

## 最終回歸

- BuildGreenR2、BuildProductionR1 actual0。
- DetachedWalkR1／DetachedStandR1：兩種來源 clip，各切換至另一份不同 authored curves，Automation Success、actual0。
- ProductionWalkR1／ProductionStandR2：使用正式主專案 DLL，兩份不同卡共用同一資源，80 SK 部件；首次 pose failure 不破壞 renderer／pose／clock／playing／mouth；健康候選能 commit 與 seek。單次播放末尾嘴型 clamp、duration mismatch 拒絕、獨立 clock、loop、pause 與曲線 immutable 回歸也全部通過。
- 保存 R1.audit.json／R1.snapshot，核對 Stage376 與 Stage377 舊封存保持不變。沒有重寫既有卡片或 uasset。

重新開啟主專案即可使用修正；用 PlaySharedAnimation 切換資源仍保留獨立 SetMouthControl／MouthOpenCurve。failure 回傳 false 並在 AnimationError 提供原因。此 transaction 保護 PlaySharedAnimation API，並未宣稱直接修改 Character／Outfit 然後 RebuildCharacter 也有 rollback；後者仍是使用者要求重建本身。

這不是全部卡片、controller graph、最終 GPU pixels 或 Shipping 的無 bug 宣告。原作 raw clip 預覽的 loop 慣例維持既有邏輯，完整 controller graph 尚不在共用資源的支援範圍。

ProductionStandR1 曾以 actual1 提前結束，無完成／失敗斷言／崩潰原因，因此不列驗收。ProductionStandR2 使用 runner 明確 -LongTest，僅測試停用 heartbeat thread，外層十分鐘 watchdog 與所有驗收條件未變。不能由重跑成功推論首次退出原因已確認。
