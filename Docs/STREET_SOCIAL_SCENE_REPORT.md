# StreetSocial 五女角版本：實作與功能證據

2026-10-07。已用 Grok Build grok-4.7-build-fast 分工實作，主代理獨立修正、整合、編譯及實測。當前功能測試通過，完整驗收尚未進行。

## 啟動與操作

專案根目錄 `StreetSocial.cmd`。公開 receipt 已更新為 StreetSubmit03 Development executable、StreetCook07 共用地圖及 RuntimeV2 共用對話資料；未改百人 CrowdRoom CMD。

- WASD 男角移動、Shift 跑步；右鍵鏡頭、滾輪距離。
- 靠近已就緒 NPC 按 E；1／2／3 或 UI 選日常、戀愛、性愛話題。
- L 切換中文優先／日文語音，同句字幕保留；Esc 立即取消，道別按鈕播放對應結束語音。
- 友誼進展與告白按鈕先預覽；確認後保存，取消不提交。友誼需達初識上限；告白需朋友且好感至少為朋友上限的70%。
- N 次日恢復交流額度；F9 切換原有 runtime CPU／GPU 功能。

正式關係存檔 `Saved/StreetSocial/relationships.json`。所有自動測試使用獨立存檔；測試用門檻設定不會進入正式玩家存檔。

## 本次完成

1. 獨立街區、男性第三人稱、五名女 NPC 走停避讓及交談停步／面向玩家。
2. 六張 APNG 全部 runtime 建立私人角色及 GPU 姿勢，逐個淡入；沒有卡片專屬 Cook，沿用4GiB共享快取。
3.1230原始節點與705分支，來源順序、偏好、機率及回應效果；完整語音播放才提交效果，拒絕可減少好感。一般街區備援回應的+1為明確自訂效果；來源分支不額外加+1。
4. 真正開場與道別，不扣話題次數、不附加獎勵。道別依原作 GetEndConversationADV 規則而非普通台詞條件。
5. 兩個有界私人 action stream，男女可同時播放；不同類別共用街區手勢00/03/07/12；動作切換、取消、代次保護、背景準備與音訊嘴型。
6. 關係預覽確認、原子存檔、每次確認獨立交易、重放保護、階段上限、冷卻及跨次保存。音訊 Failed／Cancelled 均不提交對話效果。
7. 可攜共用日文 OGG及中文對应資料；不再依賴 KKS 安裝位置播放。851個獨立日文檔，889來源引用對照無序列／SHA差異。

## 獨立驗證

- Development Completion10及Editor completion16編譯成功。
- Python catalogue測試21項通過。
- UE AutomationCompletion04：RulesAndRealCatalogue成功；涵蓋真實可攜資料、成功／拒絕、來源門檻、取消預覽、重複交易、存檔與道別預算。
- Runtime-CompletionR10與R11均通過：五名NPC各三話題，加五次開場、五次道別，合計25次完成語音；15來源分支、7次拒絕，全部5名實際audio render與嘴型更新。
- R11男女雙串流同時播放 34.01 秒；男角18次stream installs，NPC各4～5次，全部stream_errors=0；GPU failures=0；無Cook角色。
- 已驗證播放中取消、立即重進、不提交取消效果；關係門檻／取消／重複確認／拒絕後再次接受／保存回讀。檢視原生1920×1080畫面。

R11六人全部顯示 **7.73秒**，10,000幀、1920×1080 RenderOffscreen：

| 指標 | 平均ms | P95ms |
|---|---:|---:|
| 整幀 | 3.130 | 3.610 |
| Game Thread | 0.925 | 1.142 |
| Render Thread | 3.116 | 3.586 |
| RHI Thread | 0.993 | 1.206 |
| GPU | 1.718 | 1.991 |

各階段重疊，不能相加。R10平均3.879ms、P955.038ms，期間有編譯；不當作獨立性能基準。R11開始前本輪編譯與規則測試已結束，但未宣稱全系統無其他負載。這是六人功能流程量測，不是百人120FPS正式驗收。先前R4/R5/R6證據保留原目錄。

## 明確限制與後續完整驗收

原作告白／友誼 ADV 劇情尚未移植，沒有普通PCM的事件以明確文字確認，未用錯誤語音替代。940列原始手勢沒有精確共用clip，命名表情和眼頸look pattern仍保留來源資料，未假稱已重製；現版有街區手勢與聲音嘴型。缺原世界輸入的規則採已記錄街區中立策略或停用未知條件。語音目前涵蓋性格0、18、17、27、37。

R11仍出現一次VSM non-Nanite marking overflow。街區49個幾何元件已換為相同形狀的共用Nanite資產、12材質usage已修正，沒有default-material錯誤；尚需追查角色動態shadow bounds。未調高硬體佇列或隱藏警告。正式視窗全操作、逐角度CPU/GPU畫面、所有卡片／性格、冷暖啟動、長時間及陰影性能，留到最後完整驗收。

分工報告：`STREET_SOCIAL_RULES_COMPLETION.md`、`STREET_SOCIAL_CONTENT_COMPLETION.md`、`STREET_SOCIAL_RUNTIME_COMPLETION.md`。根因與失敗測試均記錄 `STREET_SOCIAL_ERRORS.md`。

## Runtime表情補完（2026-10-07）
公開receipt更新StreetFace03 Development與Runtime-FaceR2證據。背景取得每張APNG自己的眉眼嘴morph，不需角色Cook或共用pose；40性格3440命名表情共用表，當前所有來源名稱覆蓋。26次套用、5NPC更新與復原、零錯誤，取消／存檔／雙角色action不回歸。全部顯示8.269秒，離屏1080p10000samples平均3.247ms、P955.417ms；僅六人功能測試，非百人或完整畫質驗收。詳見STREET_SOCIAL_FACE_RUNTIME.md；原百人CMD不啟用全表情預計算。

## Runtime表情過渡補完
2026-10-07：公開CMD已更新StreetBlend03。Source blend旗標採0.15s私人通道權重過渡，快速重定向有界、嘴型與眨眼不中斷、舊百人預設不變。Development／Editor、Python26、UE4及Runtime-BlendR2均通過；64開始／64完成、245中間更新、5NPC最終復原；六人離屏平均3.404ms／P956.122ms／全顯示8.657s。詳見STREET_SOCIAL_FACE_TRANSITIONS.md，完整驗收仍延後。

## 陰影診斷進行中
2026-10-07：ShadowBoundsR1 Development通過所有既有功能，全部顯示8.419秒，1080p六人離屏平均3.069ms／P955.077ms。UE頁面診斷直接指向GPU角色，不是Nanite街區；串流動作使保守bounds擴大（最大半邊長14.31m）。目前不是已解決的陰影驗收；正在改為權重與各軸縮放感知的背景保守計算，無畫質／佇列容量調降或單一卡例外。

## StreetShadow03 root acceptance
Development StreetShadow03、Editor-streetshadow22、Automation-Shadow02五項（含包含範圍與階層交叉淡化）、Python26、Runtime-ShadowTightR1/R2全通過。公開StreetSocial CMD receipt已原子更新，ValidateOnly成功。R2六人1080p離屏10000samples平均4.956ms／P958.304ms／全部顯示10.244s；GT2.134／RT4.934／RHI1.404／GPU1.904ms，階段不能相加。相較前次存在負載與測試波動，不宣稱受控FPS提升。串流bounds修正已實測有效；VSM overflow仍記錄一次（引擎只報一次不代表只發生一幀）。完整視窗／CPU-GPU外觀及陰影提交驗收仍未完成。原百人CMD未改。

## Runtime陰影提交功能
已通過UE8／Python26、Development／Editor及SubmitR4五女角完整功能測試。專用代理只在有收益時使用；154潛在投影section降至144，缺省通道表情回歸已修正。公开CMD更新StreetSubmit03/Cook07；六人離屏平均5.034ms、P9510.281ms、全顯示8.464秒。VSM警告及完整120FPS／外觀驗收仍未完成，未宣稱受控性能提升。詳見STREET_SOCIAL_SHADOW_SUBMISSION.md。
