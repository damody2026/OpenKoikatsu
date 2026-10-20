# 近距離中模頭髮黑塊修復 — 2026-10-04

本次主要驗收的是近距離、每人頭髮不超過一萬面的中模。使用者操控的今汐是選單 resident44、匯入 C079；問題已在 Cook 完成的 Development 遊戲重現。

## 原因與處理

髮絲本體是雙面材質，描邊殼是單面、反轉繞序並沿法線擴張的網格。部分髮絲面向與頂點法線相反，使描邊殼朝向髮絲正面，畫出大片黑色多邊形。C079 原始資產也有方向不一致的面，不能將全部成因歸給減面，也不能當作打光問題。

修復在既有行走動畫的第0秒姿勢中判定面向，將錯向的髮型描邊三角形反轉。只處理與中模髮型共用的單面 WPO 材質；髮絲本體、臉、衣服和正常描邊保留。私有 affine palette 的 bind 座標不能直接當一般幾何座標判斷面向。

產生獨立衍生資產 NearDetailR6（近距離臉及中模頭髮）與 GameLowHairSafeR9（初始低模），保留 R5/R8 和原始匯入資產。頭髮不增加面數或貼圖解析度，也不增加每幀 CPU 頂點工作。位置、完整骨骼權重、morph 位移與三角形總數有保護檢查；UV、法線、切線、頂點顏色從原描邊角點複製。

| 驗證項目 | 結果 |
|---|---|
| C079 近距離中模 | 8,974 面；223 個描邊面改向 |
| C079 初始低模 | 340 個描邊面改向 |
| 全100人低模 | 最高29,900面，單LOD，貼圖最大256² |
| 全100人近距離頭髮 | 最高9,924面，私有骨架相容 |
| 重新計算裁切邊界 | 全100人，各501個動畫取樣 |
| 百人 Development 功能測試 | loaded/animated100、failed0、WASD及邊界、近遠距離臉/頭髮切換通過 |
| 初始載入 | 8.184秒，initial_low_only_met=true |

## 影像與證據

- C079 正面修正前：Saved/Crowd/DevelopmentHairBeforeC079R1.pose0.png
- C079 正面修正後及七個行走姿勢：Saved/Crowd/DevelopmentHairAfterC079R1.pose0..6.png
- C079 側面：Saved/Crowd/DevelopmentHairAfterSideC079R2.pose0..6.png
- C079 低模隔離測試：Saved/Crowd/DevelopmentHairAfterLowC079R3.pose0..6.png（刻意關閉近距離臉，不能當正常近景臉品質）
- 第二位 C085 正面修正前/後：DevelopmentHairBeforeC085R4.pose0.png / DevelopmentHairAfterC085R5.pose0.png。瀏海與臉旁的黑塊消除。
- 資產檢查：AuditHairNear100R4.json；裁切邊界：SectionBoundsHair100R4.json。
- 修復紀錄：HairRepairC079R1.json 加 HairRepair100R3.json（後者99人，C079沿用已完成的試點）。
- 功能測試：DevelopmentHairMovement100R6.json，正常退出0。

本輪是外觀及功能驗證，並有其他工作並行；mean8.777ms、P9511.291ms、target_met=false，不能宣稱達成120FPS。髮束重疊處仍保留一般細描邊；本次驗收的是異常大片黑面。沒有把褲襪交疊或既有長衣動畫問題列為已修復。

## 後續重建方式

共用規則已抽出至 OKCrowdHairOutline.cpp 的 OKRepairCrowdHairOutline；輸入是網格、動畫、髮型材質集合及輸出路徑，沒有角色名稱、索引或特例參數。OKBuildCrowdNearHair 在生成階段自動呼叫它；BuildNearHair 對原始面數未達上限和經過減面的頭髮都走同一入口。因此後续轉換的角色不需要手工逐卡調整。此步在匯入/預處理階段完成，正式遊戲仍直接載入 Cook 資產。

生成入口驗證：HairGeneratorSmallR5（原始5,012面，未減面）、HairGeneratorReducedR5（原始35,636面減為8,974面）、HairGeneratorMiddleC085R5（既有8,974面中模）。三者均自動改向，描邊修正階段不更動頂點與權重，第二次套用 changed=0，命令退出0。通用 BuildHairLOD 命令接受任意 SourceMesh / SourceWalk / Destination，不依賴C079或C085的角色編號；這些編號僅為測試樣本。

Tools/repair_crowd_hair_outlines.ps1 -Tag 新標籤是舊快取的批次遷移與完成狀態核對工具，並非通解本身。之後用 -QualityVariant=GameLowHairSafeR9 的 AuditNearGame / BuildSectionBounds 檢查。Cook 的 GameLowHairSafeR9 自動納入 NearDetailR6。

遊戲、標準 Development 啟動工具及 optimized 啟動工具預設使用 R9/R6，仍先載低模及256²貼圖，近距離才載中模頭髮與細緻臉。錯誤與避免重犯事項記於 Docs/CROWD_GPU_POSE_ERRORS.md。

## 生成與快取流程補強

準備工具及 BuildNearHair 現在預設直接產生 NearDetailR6；舊版R5不再作為新生成流程的輸出。既有快取須檢查原始卡片與動畫來源、實際髮型面數及共用描邊方向規則，才可重用；不符合時回報錯誤，不因檔案存在就跳過。

BuildFaceDetail / BuildNearHair 與 Tools/prepare_crowd_near_detail.ps1 支援 SourceRoot，從指定的任意 /Game 角色資產目錄讀取資料，無需依賴場景的C000..C099編號。此入口涵蓋臉及中模頭髮；身體預處理仍由既有工具處理。範例：

```powershell
Tools/prepare_crowd_near_detail.ps1 -Tag FreshTag -SourceRoot /Game/YourCards/SelectedCard -FacesOnly
```

SourceRoot的完整生成與重用測試 UniversalSourceRootC085R8 / UniversalSourceRootReuseC085R9 均正常退出；指定C085但StartIndex為0，輸出仍由指定資產目錄定位。原始35,770面生成為8,974面，1,834個描邊面自動改向，重用時 outline_issues=0。Legacy R5拒絕測試刻意返回1，檢出C079的223個異常描邊面，原始快取未被覆寫。

最終驗證：HairUniversalCache100R8正常退出0，100個不同角色的R6快取全部 outline_issues=0，最高9,924面。完整新生成的中模通過 HairUniversalGeneratedAuditC085R10（8,974面、私有骨架相容、低模貼圖256²）。最終Editor編譯 HairUniversalPipelineFinalBuildR10成功，最新DLL時間晚於修改來源；最終快取重用測試 UniversalFinalReuseC085R10通過。彙整證據：Saved/Crowd/HairUniversalPipelineEvidenceR10.json。

本輪補強均在匯入/離線準備階段；正式遊戲的載入與每幀渲染流程不增加新的方向檢查。現有已Cook的R9/R6資產不需要因這次生成工具修改而重新打包。本輪沒有重跑FPS驗收，也沒有將100人參考姿勢的幾何檢查宣稱為所有可能動畫的完整驗證；先前正面、側面及七個行走姿勢影像仍為畫面驗證證據。
