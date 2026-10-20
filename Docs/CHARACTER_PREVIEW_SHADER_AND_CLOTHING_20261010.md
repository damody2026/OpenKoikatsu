# 預覽材質與即時全脫計畫

1. 保存正式 shader 與材質計算；載入排程另外保留最終材質引用，先顯示共用的 opaque／masked／translucent 預覽材質。預覽母材質先準備，可跨角色重用。
2. 遊戲執行緒輪詢最終材質，完成後每幀有限量替換；保留原骨架與身型完成邊界，不在背景建立 UObject，不阻塞等待 shader。
3. 全脫直接隱藏衣物（含輪廓與 morph 顯示），身體衣物遮罩使用可動態替換的 texture parameter，預設值與原作完全相同。穿回還原引用與可見性，保留 Actor、動畫、身型及鏡頭。
4. 移除不必要的全脫整角色預熱；其他需要不同幾何的半脫沿用原作狀態路徑。
5. 用 KK_384914 實際驗收 shader 尚未完成時的預覽、完成後的正式材質、立即全脫／穿回、動畫與身型保存，再檢查正式外觀。錯誤與限制記錄在本檔。

## 錯誤紀錄

- Windows rg 不能將檔名 wildcard 當原生路徑。本輪搜尋再次命中此錯誤，後續使用目錄與 -g。
- 全脫不能僅隱藏衣物而忘記身體 alpha mask；衣物會遮掉身體片段。必須同時處理遮罩且穿回還原。
- 既有 shader readiness 計時只包含最後等待，不能代表前面材質建立／編譯提交的完整耗時。
- 驗收程式直接用 auto* 接 TObjectPtr 陣列索引，編譯失敗；索引值須明確 .Get()。正式實作模組已編譯，但整體建置不能因此視為通過。修正後重新建置。
- 編輯短迴圈時曾誤加多餘右括號，已於下一次編譯前改成明確區塊，避免以壓縮語法降低可讀性。
- 一致 suffix 1370 的九模組建置／imports／正式入口 DryRun 通過，但另一個直接 UBT 一般建置隨即清理熱重載 DLL 與 manifest，ParentsR1 未能啟動，不能視為 runtime 通過。未停止其他建置；等待最新正式 base 完成後，使用 receipt 恢復一致 manifest 再做實測，保留失敗紀錄。

## 實作與實際驗收

- 正式 shader 的光影、UV、顏色與輪廓公式保留。身體 `_AlphaMask` 改成 texture parameter，預設仍使用完全相同的原始貼圖；只有全脫時替換成中性白色貼圖。
- 三個預覽母材質保存於 Content/IndependentResources/PreviewMaterials，已加入 AlwaysCook；ParentsR2 exit 0／saved=1。預覽使用材質實例，不按卡重建 shader graph。
- Progressive final references 以 UPROPERTY 保留；每幀最多替換四個材質槽，2ms 軟預算；如果槽已被後續外觀操作換掉，不覆蓋新材質。
- 原作全脫／全穿兩個 unconditional action 不再讀取整份服裝規則，也不重新開啟 Session。保留衣物幾何／原始遮罩，恢復已載入狀態時直接還原；半脫等需其他幾何的狀態仍沿用原作資源路徑。不再預熱全脫的另一整隻 Actor。
- AuditR1 使用 KK_384914 原作卡、真實 Gallery 載入與 OnClothesState：exit 0／valid=true。首次 Session.Open 84.514 秒，GUI 可見約89.988秒；這是來源冷載入，不是 shader 等待時間。公開顯示時57個正式材質槽仍在準備，約19秒後全部完成替換，證實不再等所有正式 shader 才顯示。
- 32 次全脫／穿回最大 callback 0.6115ms；Actor 與 LoadGeneration 不變、不進入重新載入、實際衣物元件隱藏／還原、Session 身型與 AnimPhase 保持；三個身體材質均切到實際1×1白色遮罩，再恢復原 texture 引用。
- 另外用相同 expression graph、全新 shader-map identity 的驗收用副本，實際產生 pending=1，確認預覽代替尚未完成 shader 並最終替換；副本只用於測試，結束恢復原正式材質。
- 正式 base 引擎／專案 BuildId 為 4e55d96f-6264-4cf7-8d18-d026321dc960。首次熱重載與第一次 runtime 失敗紀錄保留，不列為通過。

## 最後驗收（完成）

- 最新正式 base 的 AuditR2 再次 exit 0／valid=true，32 次全脫／穿回最大 callback 0.9068ms，三個遮罩、Actor／generation／身型／時鐘與實際衣物元件檢查通過。GUI可見8.893秒；此為另一程序的暖來源測試，非同程序再次載入，不以冷暖差異宣稱本次整體加速。公開顯示時仍有57個正式材質槽在準備，另行編譯副本 pending=1 的替換測試亦通過。
- `OpenKoikatsu.Gallery.LoadSelection` 最後一次實際自動化 Result={Success}、exit 0：載入 busy gate、同卡身型修改及失效 generation 保護通過。
- `Saved/Tests/Material384914ShaderPreview46/capture-report.json`：exit 0／passed=true，正面、側面、背面、臉部四張均3200×2000，已逐張目視檢查。髮色、髮絲亮帶、黑色輪廓與蝴蝶結保留。原有小腿粉色色帶與側邊配件沿用既有 render 基準，本輪未處理，未聲稱逐像素一致。
- `Saved/Tests/PreviewShaderClothing46/AuditR1.png` 已檢視，32次操作後服裝完整恢復，正式材質髮色、高光、衣服顏色與輪廓正常。此圖是恢復後畫面；全脫與編譯中替換由實際元件／shader readiness／參數檢查驗證。
- 正式 `Tools/run_card_reader_development.ps1 -DryRun` 通過，使用 base `UnrealEditor-OpenKoikatsu.dll`。最終正常建置包含本輪程式；1370 的完整編譯與 imports 驗證紀錄、首次 runtime 啟動失敗以及兩輪最終 runtime 結果分開保存，未把中間失敗列為成功。
- 共用場景的載入入口與讀卡入口使用相同方法；進入一對一場景時依 UI 全脫狀態套用可見性。此輪未重新打包發布 StreetSocial 的既有 cooked executable，正式 runtime 驗收範圍為目前 Development 讀卡入口。
- 測試僅於自身程序暫設引擎既有 `UE_SKIP_UBT_SDK_SETUP=1` 並還原，未改使用者環境、引擎或正式啟動器。既有 CommonUI viewport 訊息不屬本輪變更；無新 shader compile error、assert 或 fatal。

限制：預覽要等 Session 解壓、骨架與身型完成；尚非身體／頭髮資源分開串流。分幀替換只限制遊戲執行緒每次替換數量，不能保證驅動 shader／PSO 建立、貼圖上傳或單件模型建立完全沒有尖峰。全脫為立即隱藏；半脫與切換其他幾何資源仍可能需要載入。

## StreetSocial 正式整合續作計畫

1. 在 Saved 內建立獨立 build host，編譯目前共用程式的 Game target，避免其他建置清理 root DLL。
2. Cook 三個共用預覽母材質，建立新 patch 集合；保留 StreetDisplay23 原檔與 render 公式。
3. 在新 stage 驗收多人／高模場景往返、動作目錄與水槍，檢查正式 shader 無缺漏。
4. 以完整性與既有功能門檻發布正式入口，保留舊版本與證據；將實際結果與失敗記在本檔。

### 正式整合修正與失敗紀錄

- 首次 Game target 使用 -UsePrecompiled 失敗：獨立 target 缺 ImageCore 等 precompiled manifest。保留 build.log／exit=6，改用正常 Game 編譯，不修改引擎模組規則。
- Cooked game 沒有動態 shader 編譯；不應將 Editor 的預覽母材質變成所有舊 cooked 套件的強制依賴。WITH_EDITOR=0 直接沿用既有預編譯材質；Editor 的先預覽、分幀替換行為保持。無需更換 StreetDisplay23 shader patch。
- 共用場景 feature audit 增加32次真正的服裝 UI callback：檢查元件可見性、同 Actor／generation、身型／動畫、白色 body mask 與 callback 耗時，實際結果待新 Game build 驗收，未先宣稱通過。
- 本輪 patch 格式曾不完整而遭 apply_patch 拒絕，未改檔；已改成具體上下文後套用。Windows rg 再次誤用 wildcard path，後續搜尋一律目錄配 -g。
- Reader47 首次往返驗收加上 InteractionFeatureAudit 後失敗：兩個獨立測試同時選動作，Ready 剛成立時 feature audit 又切到另一動作，導致 reader state oracle 看到重置中的選單。已有兩次返回／播放控制通過，但整輪 passed=false，不能算成功。讀卡往返改單獨執行；水槍／目錄／全脫另在 StreetSocial feature audit 驗證，避免測試互相改寫選擇。
- ReaderShaderClothing47R2KK384914：實際 exit 0／passed=true，9項全部通過，包括三次高模往返保存未儲存身型／名字／鏡頭、三次暫停／重播／倍速／男女同步、單人目錄按需播放、載入取消、缺失資源返回。首輪測試衝突仍保留為失敗，未覆蓋。
- PreviewShaderClothing47Host 的 Game build-r2 完整643 actions成功，exit 0，841.43秒；首次 -UsePrecompiled 失敗仍保留。新執行檔已 staged 至 PreviewShaderClothing47.stage；正式 receipt 此時尚未切換，等實際驗收。
- 新 Game 的第一個高模 feature case 已 returned／error空：32次全脫／穿回、可見性、同Actor／generation、身型／動畫、實際白色遮罩全部通過，max callback 0.0849ms。99動作／2來源分類完整覆蓋，未載入動作可選並按需播放；水槍48命中、沉積19.2、清除成功。此處只記該case結果；整體多人街區驗收仍繼續。
- PreviewShaderClothing47Contacts 整輪 exit 0，15個高模 returned 結果；verify_interaction_features.py 實際重新讀取新 executable 的 log／PID／結果後 passed=true，包含32次衣物切換0.0849ms、99動作／2分類、水槍48命中／19.2沉積／清除。feature case 刻意暫停動畫，不能用該case證明正常播放時間；正常播放另由完整 Acceptance 驗收。
- PreviewShaderClothing47Acceptance 整輪 exit 0／passed=true：新 Game 的15組正常播放高模往返與既有街區完整功能檢查通過。此輪未使用 InteractionFeatureAudit，正常播放時間證據與刻意暫停的 feature case 分開。
- 最後以真正 StreetSocial.cmd（Windows PowerShell）驗收才發現 Get-FileHash 工具模組無法發現；pwsh 直接執行的 ValidateOnly 不能代替 CMD 入口。已改為 .NET SHA256 helper，保留每一項原有完整性檢查，不停用驗證、不修改系統 PowerShell。此失敗 log 保留，重新從 CMD 驗收。

## StreetSocial 最終驗收與發布（完成）

- Contacts、Acceptance、Presentation、Dialogue、Touch 五輪均 exit 0／passed=true。15組正常播放高模往返、15組關係 checkbox／同步、5站姿／6跑步、20組來源對話／5成功告白／5拒絕、20點擊反應通過。
- 三張卡 C019／C044／C004 各正面／側面／背面，CPU與GPU同姿比對全部max error=0；來源骨架／幾何／shader library檢查通過。已目視C019三視角、C044與C004正面。
- 額外比較前一正式44版與本版：C019三視角逐像素完全相同。其他卡有極少邊緣像素差異（最大23／255，最大單通道平均0.000168／255），不宣稱全部九張舊新版逐像素一致，也不宣稱原作逐像素一致。完整資料 previous-render-comparison.json。
- 沿用 StreetDisplay23.patch 的三個原 pak，SHA256全部與原receipt相同；未重建或更換render shader patch、未修改正式髮高光／輪廓／光影公式。
- 嚴格發布工具重算獨立來源／對話／聯絡人／觸碰／水槍目錄證據後成功，正式 receipt 指向 PreviewShaderClothing47.stage，full_functional_acceptance=true、runtime_instant_clothing_visibility=true；保留舊receipt與stage可回復。執行檔 SHA256 b8d54ee0ef7a76110e90f5843377ae01856f7f7108443355e9a6adca2fb40fb0。
- 真正 CardReaderDevelopment.cmd -DryRun 通過；真正 StreetSocial.cmd -ValidateOnly 於.NET hash修正後通過。Windows PowerShell原失敗log另存，不以pwsh檢查冒充CMD通過。
- Saved/Interaction/PreviewShaderClothing47Host/final-seal.json 的最終assert全部通過：新exe與實測exe同hash、原render patch未變、5輪exit／passed、三卡比對、讀卡9case、兩份關鍵source與凍結build host一致。啟動器hash另記，沒有修改系統環境。

本次當前功能已實作並發布：Editor先顯示共用預覽，shader完成後分幀還原正式材質；全脫／穿回直接操作已載入衣物與身體遮罩；StreetSocial共用高模模式與讀卡入口已驗收。既有首次解壓、資源／貼圖／GPU PSO建立及其他幾何狀態切換成本仍適用前述限制，不將callback時間當成完整frame時間。
