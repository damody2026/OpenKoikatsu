# Stage335：男女動畫、截圖及原生雙人播放器

2026-10-01。接續 `Saved/Tests/HPlaybackAudit20261001/report.txt` 的 A／B／C；修正共用來源、相機與播放流程，沒有按角色卡或截圖特判。已完成編譯與下列驗收；完整原生 H 場景及 IK 求解結果逐骨骼一致性仍未驗證。

## 使用方式

Gallery 載入角色後，動畫來源會依性別提供合適清單，可選動作、暫停／繼續與從頭重播。双人播放：先載入女角，從「配對男角」選單選擇一位男角（遊戲的 UserData 與 DefaultData 男角目錄），按「載入配對角色」，再選擇「原生雙人動作」及「共同狀態」。共用速度為 0–4 倍；結束雙人播放會清除配對選取並回到單角 Tpose。

## A：補上男角掃描與實際介面驗證

`anim.rs::clip_sources` 加入 `h/anim/male`，保留 raw／states 的完整來源 identity。Gallery 同時辨識直接路徑與 `states:studio@`／`states:controller@` 的編碼來源，避免男卡仍預設女角控制器。換來源、重播與暫停皆操作實際 PreviewActor；配對時操作共用 coordinator。

四卡：男 KK_224305、ill_male_01；女 KK_226176、Alice_keiyuu。C ABI 96 動作、5,856 幀全部可載入、有骨骼變動、無非有限值／API 錯誤，每卡來源 catalog male8／female16。`ok.VerifyHPlayback` 經實際 Combo 選取，四卡均通過播放、切換來源、暫停、重播、配對、速度、負例與收尾。最後 height 修正及 teardown 修正各對首組男女卡重驗通過。

## B：修正相機快取與跨幀截圖時序

首張男角的舊 camera cache 距新相機 134.907505032 cm。共用 `RequestCompletedShot` 設定 view target、更新 PlayerCameraManager，等待真實 `GFrameCounter` 至少推進兩幀，再核對 cache 位置／角度、shader、GPU、view、shadow 與 pose frozen readiness。不能只等待兩次 timer callback，因同步載入後 callback 可在同幀追補。

PNG 稽核增加全圖及中央區域非背景像素門檻，原男女灰屏負例都會被拒絕。新四卡四視角在 t=0.61／30 FPS 及 t=0.17／15 FPS 共32圖通過，實際取樣時間已由 log 確認。相機 cache 殘差0；每批 PV4、附件89筆誤差0 cm、palette 最大2.109e-6 cm。兩批完整 contact sheet 已目視：角色、衣服、配件存在，無首張灰屏。初版冷啟動 t=0 的16圖只保留為對照，不能誤標成 t=0.61。

## C：依原生資料建立雙人播放

讀 installed `h/list` 的 AnimationInfoData，依 category/id 合併，保留男女 base／override controller 及啟用 layer identity。117筆男女來源驗證控制器可解析；UI 僅提供 participants2，三人資料不混入雙人播放。

`AKKPairedPlayback` 管理共同 elapsed、speed、pause／replay；兩角色取消各自 Tick，以相同時刻取樣控制器，套用身形／ABMX，再解接觸 IK，最後更新雙方 dynamics 與顯示。共同 Actor 原點保留動畫 authored root 相對位置；相機以雙方实际顯示頂點共同構圖。

MotionIK 對齊反編譯邏輯：SetPartners 的0是自己、1是另一位；男女共用 `oo_base/p_cf_body_bone` 的 FullBodyBipedIK／FrameCorrect；`_list` 元件為直接 Transform PPtr；shape offset 依 editNo 指定身形，世界偏移用自己的 objBodyBone 旋轉；state exact match，不存在的狀態釋放 IK。HActionBase 的 height 同時寫入第一女角與男角，motion 初值0。MotionIK 二進位先驗證長度、數量、UTF-8 與有限值，快取按 asset size／mtime 失效。FFI 拒絕重複 handle、錯性別、不同 game root、非有限時間及不匹配 state／MotionIK identity。

## 驗證與可重現證據

總驗收檔：`Saved/Tests/HPlayback335/Acceptance.json`，包含證據與部署 binary／source SHA256。失败轮次保留，詳 `Docs/ERROR_LOG.md` E-212／E-213。

| 驗證 | 結果 | 證據 |
|---|---|---|
| 單角四卡 | 96動作／5856幀 PASS | `Final/SourceAndPairAudit.json`、`Final/results.txt` |
| 最新配對 C ABI | 61幀、4負例拒絕、錯誤時姿勢不變 | `Final2/results.txt` |
| 跨來源／狀態／時間配對 | 8組×4state×4time=128幀、256 active contacts、共同時鐘、height一致、finite | `PairFramesFinal.log` |
| 原生綁定／身形合約 | 16合約、64目標、384 FrameCorrect綁定，最大分量差2.3283064e-10 | `NativeContractR3.json.pair-contract.json`、`NativeContractAuditR2.log` |
| 原生重跑 | R4 runner exit0，16合約與R3 numeric delta0，probe已撤出 | `NativeContractRepeatAudit.json`、`Saved/Logs/HPlayback335NativeRunnerR4.log` |
| 實際骨架目標 | AnimationInfo_01／02 各expected8、active8 | `NativeLiveTargetsAuditR2.log` |
| Gallery四卡及最後收尾 | 全部PASS | `Saved/Logs/HPlayback335GalleryR3.Audit.json`、`HPlayback335GalleryTeardownFinal.Audit.json` |
| RHI兩種time/frame rate | 32圖、所有readiness與影像內容門檻PASS | `Rhi061Reaudit.json`、`Saved/Logs/HPlayback335RhiFinal017.Audit.json` |
| Unreal動畫回歸 | Animator1DBlends／Animator2DLayers／AnimatorMotionTimeScale 全Success | `UERegressionAudit.json`、`ue-automation-final.log` |
| 最後部署編譯 | R6 Succeeded | `Saved/Logs/HPlayback335UEBuildR6.log` |

目視合圖：`HPlayback335RhiWarmR3ContactAll.png`、`HPlayback335RhiFinal017ContactAll.png`、`PairContactAll.png`。配對四圖均有兩位角色且共同構圖正常；此目視不是原生骨骼 oracle。

可重跑：`Tools/GamePoseProbe/run_hplayback335.ps1 -Tag <fresh> -AnimTime 0.17 -MaxFPS 15`、`run_hplayback335_gallery.ps1 -Tag <fresh> -AllCards`、`run_hpair335_native.ps1 -Tag <fresh>`。後者只在沒有既有 Studio／probe 時安裝臨時 probe，結束後撤出，延遲 Quit 只停止核對過 executable 的 owned PID。

## 驗證邊界

已實作雙人播放器及接觸 IK adapter；原生 oracle 驗證的是 binding、weight、FrameCorrect／shape offset，不是全部求解後骨矩陣。仍未完成完整 H scene flow、三人互動、音效、自動轉場、所有 mod／virtual H list、任意 controller layer／參數組合與全 solver 持續狀態 parity。既有 CommonUI viewport 提示在 capture audit 單獨列出；本輪沒有修該獨立配置問題。Stage334 材質、mips、透明深度、outline／外部shadow及 Siesta整PNG差異限制仍保留，不宣稱全專案零bug。
