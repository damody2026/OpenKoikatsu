# 人物編輯器與一對一互動場景

## 最終需求與實作

兩個場景用途與顯示路徑分開：

| 場景 | 目標 | 人物參數 | 顯示與入口 |
|---|---|---|---|
| 人物編輯器 | 容許60FPS，參數即時生效 | 身體／臉部完整編輯 | 原可變形高模路徑；卡片頁「加載3D（人物編輯器）」 |
| 一對一互動 | 120FPS目標 | 只允許肚子大小；其餘身體／臉部shape鎖定 | 獨立舞台與互動UI、private GPU播放路徑；「進入1對1互動場景」 |

互動仍可控制眼睛／頭部朝camera、嘴型、語音、動畫速度／姿勢與水槍。肚子是使用者明確保留的例外；這不等於放行編輯器整套身材控制。

卡片頁的两个入口固定在詳細資料捲動區之外，各有独立按钮／callback。普通Load3D只進人物編輯器，互動按鈕才建立InteractionScene舞台與InteractionWidget。人物編輯器保留原「加載3D」、身體／臉部面板、動作／表情面板；互動場景隱藏整個編輯器UI，使用自己的控制布局與舞台。返回按鈕明確為「返回人物編輯器」。

渲染路徑：人物編輯器使用原支援即時shape變更的高模顯示，`bUsePrivateGpuPlayback=false`；互動場景opt-in private GPU playback，退出後恢復編輯路徑。兩模式沿用APNG載入與session以免重新讀卡，不代表顯示介面或變形策略相同。這仍是同一個UE World中的兩種場景狀態，由獨立舞台Actor／Widget管理，沒有改成兩個.umap或複製載入器。

`OnShapeChanged` 在互動／進場期間阻擋編輯器callback，包含身體與臉部。互動肚子經過 `OnInteractionBellyChanged` 專用窄入口，只能改 `body[21]`，驗證finite並夾到原-1..2範圍；共同寫入helper為private，不對外開放整套調參。退出後恢复完整即時編輯，肚子修改保留。

60／120是各場景的需求预算，本輪未新增全域FPS上限，也不以畫面stat或UI修改宣稱120FPS效能達標。前輪水槍噴射仍未達120FPS；本輪針對入口、場景區分與參數權限驗收，不把前版profile轉成新版效能證據。

## 計畫

1. 保留加載3D入口，互動新增独立入口；導航固定在可捲動詳細資料之外。
2. 明確區分場景與GPU播放ownership，互動只放行肚子例外。
3. 真實APNG以按鈕callback進編輯器、修改shape、進互動、拒絕其他shape、調肚子、退出再修改、返回卡片頁；保存每個狀態的UI截圖。
4. 相關導航／控制項恢復／選卡／語音cache回歸，記錄失敗與修復，核對最後binary。

## 驗收

專用 `ok.GalleryNavigationAudit` 檢查按鈕真實cached geometry在viewport範圍內且可用，使用OnClicked／OnValueChanged delegate走產品callback。1280×720下保存Gallery、Editor、Interaction、EditorReturned、GalleryReturned五張showUI截圖，人工檢查入口文字與控制面板。

失敗原始資料保留：GalleryEditorInteractionNavigation、R2。R3導航功能PASS，但人工查到長互動標籤換行超出header，改成「進入1對1互動場景」，另用新Tag重跑。

最終完成的run：GalleryEditorInteractionNavigationFinal、GalleryEditorInteractionFinalRegression、GalleryEditorInteractionPairFinal。錯誤及修復都在 [INTERACTION_SHOWCASE_ERRORS.md](INTERACTION_SHOWCASE_ERRORS.md)。

## 最終驗收完成
- GalleryNavigationFinalBuild.log：Succeeded，12.61秒。
- GalleryEditorInteractionNavigationFinal：audit_completed=1／functional_pass=1／editor_and_interaction_separate=1。實際APNG body[21]修改、其他body／face拒絕、肚子例外有效、返回後完整shape編輯恢復；5張截圖已人工檢視，加載3D與互動短標籤完整顯示。
- GalleryEditorInteractionFinalRegression：LoadSelection／RestoreControls／SpeechCache共3/3 PASS。
- GalleryEditorInteractionPairFinal：完整APNG雙人功能PASS，54嘴型／語音／眼頭控制／肚子／雙人暫停重播／替換與失敗保留／行走loop／水槍／清除與附著均PASS。12次ready切換通過，最慢click-to-play19.515ms、切換最大幀43.894ms；最大同步控制173.097ms。
- 女角水槍89新命中／35.6cm³，男角218／36.8cm³，當前private GPU pose query error均0；不把功能audit平均6.6ms移作120fps證據。背景最大幀1209.044ms仍保留，沒有聲稱場景所有慢幀都達300ms。
- 最後三個run各自external UE0、hash穩定，四個DLL亦逐項與目前檔案相同。receipt：Saved/GalleryEditorInteractionCurrentVerification.json。diff --check通過。
