# 一對一水槍與完整分類動作驗收

## 實作

- StreetSocial 的實際 OKCrowdController 繼承共用預覽輸入控制器。一對一使用 A＋滑鼠左鍵噴射、R 清除；返回街區交回原移動控制。重複進入會重新顯示既有噴射器。
- 主模組啟動時載入 Niagara，於 World 建立前完成其初始化。沒有重新 cook／替換水槍或角色材質 patch，沒有局部補初始化模板。
- 雙人目錄保留所有雙人來源 metadata，穩定索引與準備快取分離。先選類別再選動作；未準備也會顯示並可選，背景準備後播放，準備時保留上一個動作。未知來源分類仍保留。
- 單人動作先列出目前來源的完整清單，不再只列已準備的 32 項；來源選單由 session 與共用動畫目錄建立，選取後只準備所選項。
- 背景結果檢查角色、選取及 generation；切換、取消或退出後的舊結果不會覆蓋新選取。

## 已通過證據

- Game 44 與正式根專案 Editor -1044 共用同一功能程式碼；Editor 實際 46 actions，NoEngineChanges 保留。根專案七個模組清單備份於 Saved/Interaction/RootModuleBackup44，原始 DLL SHA256 未變。
- Saved/StreetSocial/Runtime-InteractionCatalogue44Contacts/interaction.features.validation.json：99 個雙人動作，2 個現有來源分類，分類覆蓋全目錄；未載入第 2 項可選並播放。水槍命中 48 次、實際沉積 19.2，清除成功。此診斷暫停動畫，不能代替正常播放往返的時間前進證據。
- Saved/Interaction/ReaderRootCatalogue44KK384914/validation.json：正式根專案、原始 KK_384914.png，9 項通過。三次高模往返及暫停／重播／速度／共用時鐘、不同姿勢與階段、單人目錄選取後載入播放、取消與缺失資源恢復。未保存編輯、來源角色及相機保留。
- Presentation44：15 組關係勾選與同步通過；Dialogue44：20 組對話上下文通過；Touch44：20 組來源反應與 GPU 命中獨立驗收通過。
- InteractionCatalogue44C019、C044、C004：三張卡各三視角，GPU 與來源幾何檢查通過；同凍結姿勢的兩條顯示路徑逐像素差為 0。這不是原作逐像素一致性宣稱。
- Saved/Interaction/Material384914Catalogue44：KK_384914 四張 3200×2000，退出碼 0，已逐張檢視髮絲高光、輪廓與膚色光影。相對前次已確認 R19，正面／背面／側面／臉部 RGB 平均絕對差（0–255）為 0.2534／0.0938／0.0634／0.1190，99 百分位差為 2／2／1／2。保留原有細小接縫及側面環狀配件；未改 render。
- 73 項既有 StreetSocial Python 測試通過；CardReaderDevelopment 啟動器 DryRun 確認正式根專案使用 -1044 模組。

## 發布狀態

- 完整 InteractionCatalogue44Acceptance 退出碼 0，正常播放的 15 組高模往返經獨立驗收全部通過；既有電話、話題、關係、對話及街區活動驗收通過。
- 嚴格發布工具通過，live receipt 已更新為 InteractionCatalogue44.stage；runtime_interaction_mode=in_process、runtime_interaction_watergun=true、runtime_interaction_full_catalogue=true。完整執行檔 SHA256：3c5ca5c23728710249ba6d6f8744d342bc00c6844b466ef8ef5b43362ee919c8。
- 發布前 receipt 備份：C:/portable/OpenKoikatsu/Saved/StreetSocial/Build/receipt.before-20261009T112221464149Z.json。材質／map／audio patch 仍使用 StreetDisplay23.patch，沒有使用診斷中失敗的 42.patch。
- StreetSocial 啟動器 ValidateOnly 與 CardReaderDevelopment 啟動器 DryRun 皆通過，分別確認正式 packaged 44 與根專案 -1044。重新啟動既有 cmd 即使用修正版。
- 計畫五項均已實作、驗證並發布；本次錯誤與捨棄方案記錄於 Docs/SHARED_ONE_TO_ONE_INTERACTION_MODE_ERRORS.md。
