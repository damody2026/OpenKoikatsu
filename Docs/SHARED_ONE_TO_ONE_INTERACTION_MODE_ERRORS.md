# 共用一對一互動模式錯誤紀錄

## 2026-10-09：既有架構不符合模式需求

- 症狀：StreetSocial 另開 UnrealEditor，讀卡互動的服務依賴完整 Gallery UI。
- 根因：先前以外部主機重用讀卡互動，未抽離程序內共用模式。
- 修正方向：共用模式、角色載入、配對動畫及獨立 UI；移除程序與 JSON 控制流程。
- 驗證：實作中；必須同 PID 實際播放及往返，不以外部主機成功當成通過。

## 工具使用

- 首次遊戲目標編譯失敗：舊 StreetSocialHost 刻意停用 KKUnityBundle、KKCard 與 KKSideloader，UHT 無法解析高模型別。改用獨立完整 runtime host，保留正式版本，並將高模實際使用的讀卡依賴納入 runtime 模組。

- Windows rg 的路徑參數不可假設 shell 展開 wildcard；以 rg --files 查實際路徑，或使用目錄配合 -g。

- Python 在此 Windows 使用 cp950 預設編碼，讀取繁中 C++ 失敗；所有原始碼讀寫須明確使用 utf-8-sig。該次錯誤已修正後重跑。

## 高模 runtime 編譯邊界

- 原高模模組在 Game target 仍編譯材質圖建構、動畫匯入與 Editor 測試，導致 GetEditorOnlyData/GetExpressionCollection/GetDataModelInterface 編譯錯誤。Editor 工具明確限制於 WITH_EDITOR；遊戲角色改從完整原生材質資料建立共用 cooked parent 的 MID，缺材質時回報失敗，不能默默使用灰色材質。
- 動畫精簡 snapshot 不含材質紋理，不能當作高模互動複本。新增完整原生 session 複製入口，保留原編輯器 session 與未儲存外觀。

## 同程序整合實測

- 正式遊戲保留了編輯器用 SpectatorPawn，導航 sweep 被多餘碰撞擋住；街區模式不生成該 Pawn。修正後五條導航及電話抵達通過。
- 首次 stage 使用舊插件清單，造成 Synthesis 啟動 assert／EnhancedInput、Niagara 原生標籤 ensure。改由 target receipt 產生完整插件 manifest，只啟用必要插件並附上共用原始動畫資源。
- Game 的平行 end-of-frame bounds 更新觸發只允許 GT 的共用 palette cache；worker 改走 Engine conservative bounds，GT 保留精確快取。修正後同 PID 雙人高模成功播放。
- 第一次往返驗收在恢復後下一幀模擬完成才比較，NPC 已合法繼續走動；應於返回事件完成時、下一次模擬前驗證快照，保持一般遊戲自然繼續移動。
- monolithic link 揭露兩模組同名 math helper，runtime 私有 inline 改為 internal linkage，並將外部 Editor fixture 限制在 Editor。

## 輪廓線與來源場景隔離

- 正式 cooked 輪廓 shader 已自行剔除正面，沿用 Editor 反轉三角索引造成角色塗黑。Runtime shell 必須與表面同向，Editor 保留原契約。
- Studio 初始化原本會改所有 PP volume、全域 AA 與 show flags，且相機未隨模式銷毀。共用模式改用專用相機後處理並釋放相機。
- Stage 缺 KKPreview LUT 原始資源，造成顏色變淡；封裝加入來源 LUT 目錄。
- 一次字串替換誤及其他相機函式，編譯即攔截；修正為只修改 SetupStudioView。後續同類修改必須限定函式範圍與預期匹配數。

## 驗收準備條件

- 完整街區回歸曾在「角色可見但動作尚未準備」時開始導航驗收，誤報路線缺失。探針須等待與實際聯絡功能相同的 ContactReady 條件，不只依賴 AllReady 幾何可見。
- Reader 測試未指定卡片目錄，啟動時先掃描整個原作模組庫；改為指定本次 APNG 卡片庫。隱藏視窗驗收明確使用 RenderOffscreen/unattended。
- 雙人姿勢使用保守動態 bounds 取景會包含空白包絡；模式在切換動作時以當前實際幾何做一次精確取景，持續播放仍走 GPU。

## 精確取景不得切換 render object

- SharedInteraction34Full 在 SkeletalRenderCPUSkin 的 BoneMap 讀取崩潰。GetCPUSkinnedVertices 會把 runtime 高模切成 Engine CPU skin render object，其資料契約不適合本專案原生動態 rig。
- 改成 GT 直接以已驗證 GPU skin palette、有效非零骨重、原始頂點與 morph 計算一次取景快照。保留 GPU render object，所有索引先驗證，不呼叫 Engine CPU skin 切換。

## 使用者已驗證渲染不得擴張修改範圍

- 使用者指定 Docs/CHARACTER_MATERIAL_PARITY_20261009.md 為已修正基準；本次模式重構不應順手重做材質或畫質。
- 已撤回本次新寫的精確取景頂點計算、FramePairedPose 的精確取景旗標與共用截圖函式替換；保留使用者的髮材質缺省 UV0、前髮高光、sRGB 疊色及 morph 輪廓修正。
- 上述取景嘗試與失敗保留歷史紀錄，不代表目前仍採用。後續驗收只涵蓋模式功能，不再調整渲染效果。

## 雙人控制驗收需選擇有效的新項目

- 背景只準備好一個姿勢時按下一個會合法回到同一項，測試不能把它當作已發出不同動作。等待第二個實際可選姿勢再測切換。
- 階段選單 index 0 是提示項，測試須選 1 以上的有效階段；不可以取模切到提示項並等待播放改變。

## 不干擾使用者目前的讀卡程序

- 使用者要求先確認 KK_384914.png 渲染。以現有根專案 Editor 模組重跑四視角，R16 已實際檢視通過；證據及限制見 SHARED_INTERACTION_RENDER_PRESERVATION_20261009.md。
- 隔離 RuntimeHost 建置被根專案正在使用的 Live Coding session 攔截。本次不關閉使用者 PID 82860，也不改根專案二進位；僅對 Saved/Interaction/RuntimeHost 的獨立輸出使用 UBT 已定義的 -NoHotReloadFromIDE，避免不同專案同名模組造成的互斥誤判。
- 這個旗標不能用於覆蓋仍載入中的根專案 DLL；根專案發布必須另行確認檔案鎖與使用者工作狀態。

## KK_384914 原始卡驗收不可硬設 APNG

- ReaderShared36KK384914 的測試沿用 APNG 專用 fixture，硬設 bIndependentCards=true，不適用使用者指定的原始 KKS 卡。已中止該次驗收，不能列為通過。
- 測試改以 PortableSourcePath 檢測實際輸入來源；原始卡走既有 KKS 載入，內嵌卡走 portable 載入。測試工具接受明確 Card 參數，避免偷偷換成 C019。
- 隔離建置的 R17 擷取在尚未產生畫面前中止；明確指定 UI 卡片庫後以 R18 重跑。R18 原始 KKS session 的第一次準備仍花了數分鐘，後來正常完成四張畫面，不能把準備時間誤判為 render 失敗。

## 男女來源型態必須各自解析

- KK_384914 原始卡進入模式時，舊 LoadPartnerFromPath 依女角的 IsPortable 決定男角載入路徑，會把選定男性 APNG 誤送原始遊戲資源載入，且阻塞主執行緒。
- 改為檢測配對卡片本身：APNG 使用內嵌資源，原始卡使用已設定的遊戲目錄；兩者共用有 generation 保護的背景載入與提交。保留原本依目前角色性別選擇異性角色的行為，不依卡名分支。
- 原始女角 clone 保留原始 IsPortable=false 是正確行為；驗收應比較 clone 與來源旗標相同，不能把 APNG fixture 的 IsPortable=true 要求套在全部角色。ReaderShared37KK384914 已中止並重跑，不能列為通過。

### 後續以 snapshot 契約修正上述旗標推論

ReaderShared38KK384914 實測男角完成後姿勢數仍為零。檢查 Rust 實作確認 clone 以 to_preview_bytes / from_preview_bytes 建立自足資源 session，game_root 不隨原始 KKS session 保留。上述「應保留 IsPortable=false」推論是錯誤的：新 session 必須標記為 portable 並使用共用動畫庫；原始來源 session 的旗標與資源保持不變。已修正 CloneForInteraction 的載入旗標並重跑同卡，沒有修改 shader、材質或渲染算法。

返回讀卡 UI 同時改為恢復進入前實際 ESlateVisibility，驗收比較原值，避免把 SelfHitTestInvisible 一律改成 Visible。

## 正式讀卡建置不得覆蓋使用中的 DLL 或修改引擎

- 使用新的 20261009 DLL suffix 建置正式根專案，原 DLL 與使用者 PID 82860 保留，模組清單先備份於 Saved/Interaction/RootModuleBackup39。
- 前兩次整個 target 建置被 -NoEngineChanges 擋下，因會連帶重建現有 NetCore 與引擎模組清單；沒有移除此保護，也沒有修改引擎。
- 改以 UBT 已定義的 -Module= 僅建置專案及六個自有 plugin 模組，配合 suffix 與預編譯引擎，避免與本次功能無關的引擎建置。正式入口仍維持原根專案，沒有改成另開外部互動主機。

## 最終驗收與入口更新

- SharedInteraction39Full 完整回歸退出碼 0，15 組同程序高模往返及全部現有完整驗收通過。Presentation、Dialogue、Touch 與三張來源卡外觀證據均來自同一正式執行檔。
- 根專案 ReaderRootShared40KK384914 通過 8 項，R19 四視角逐張確認。原始 DLL 雜湊未變，PID 82860 未中斷。
- 嚴格發布工具通過後更新 live receipt，備份 receipt.before-20261009T100503454282Z.json；新入口為 SharedInteraction39.stage，runtime_interaction_mode=in_process，無舊 host 欄位。兩個啟動器 DryRun 均通過。
- 驗收過程中的 PowerShell rg wildcard 路徑錯誤再次出現：原生命令不展開檔案參數中的 *；統一使用目錄參數加 -g 篩選，不以無輸出推定程式碼不存在。

## 街區一對一水槍與完整動作目錄缺失

- 上次驗收只有高模播放及返回，沒有測街區控制器的水槍命中。AOKCrowdController 原本繼承 APlayerController，ActivateInteraction 的共用控制器 cast 失敗，無法啟用水槍；必須由同一控制器能力支援所有入口，並加 packaged 噴射實測。
- 動作清單以準備完成順序逐項加入，只顯示 ready 動作，與完整可選目錄不同。分類與穩定來源索引須獨立於曲線快取；未載入項目選取後再準備，不能以停用選單掩蓋缺失。
- 本次搜尋再次將 wildcard 直接傳給 rg，Windows 不展開。後續搜尋統一用目錄加 -g；失敗搜尋不作為程式不存在的證據。

### 本次建置檢查

- 首次誤猜 bundled dotnet 8.0.412 路徑；實際 UE5.8 使用 DotNet/10.0/win-x64。先以 rg --files 查實際 runtime，不依過往版本猜路徑。
- 編譯將區域變數 Role、Selected 視為遮蔽 AActor／控制器成員的錯誤；已改為 PaintRole、PriorOption，保留編譯警告規則，Game 建置成功。
- 已確認正式 StreetSocial 地圖實際 Game class 為 OKCrowdGameMode，使用 AOKCrowdController；原先只驗讀卡控制器不足。街區控制器改繼承共用互動控制器，共用 PlayerTick 在未進入預覽時交回街區邏輯。

### 水槍資源打包驗收揭露的問題

- InteractionCatalogue41Contacts 在首次建立水槍時於 UNiagaraStatelessEmitter::Serialize 崩潰，template modules 為空。這次不將其歸咎角色 render；先隔離重新 cook 六個共同水槍資源與獨立 PaintInteractionBake shader library，保留全部角色材質與原 patch。
- UnrealPak 使用相對路徑會在 Engine/Binaries/Win64 解析，且 PowerShell 未將 -Create 參數視為完整字串；第一次產生空 pak。已移除該明確空檔，改用絕對路徑及完整字串參數並檢查檔案大小；未使用空 pak 進行驗收。

### Niagara 根因確認與捨棄方案

- 重新 cook 的 42 測試仍在同一 Serialize 崩潰，且水槍材質有 fallback；42.patch 不發布，角色共用材質 patch 未替換。
- 43 以模板初始化診斷確認 Default__NiagaraStatelessEmitterDefault 初始 modules=0，補初始化後可命中並沉積；但 Niagara WorldInit 從未執行、FX 警告持續。這不是合格通用解，已中止 43 測試且移除局部模板補初始化。
- 最終修正方向改成主遊戲模組 StartupModule 明確載入 Niagara，讓模板初始化與 World lifecycle 一起在 World 建立前完成；保留原水槍資源與所有角色 render，避免繞過模組生命周期。
- 更正建置診斷：UBT 原始碼明確定義 -Module= 使用 + 分隔；先前判斷不支援是錯誤推論。根專案 partial hot reload 多次回報成功卻為 0 action 且沒有新 DLL，不可視為發布成功。改用已隔離的 RuntimeHost Editor 建置並核對實際輸出；保留 NoEngineChanges。
- PowerShell 未加引號的 -ModuleWithSuffix=Name,Number 會解析失敗；參數須傳完整字串。新 Editor target 仍是 0 action，未解決問題，不作為產品依賴。

### 功能診斷與正常播放驗收的時間差

- 44 水槍診斷會暫停配對、選擇新動作再噴射，結束時自動返回已到期，第一組 report 的新動作 elapsed=0。水槍與未載入選取證據有效，但這組診斷不可作為「正常動作播放時間有前進」的完整往返證據。已中止同 flags 的 Full 測試，保留紀錄，重新執行無診斷暫停的 44Acceptance 完整回歸；兩種證據分別驗證，不放寬原完整驗收。
- 已定位 UBT partial hot reload：PatchActionGraphWithNames 更新 action 輸出，首次指定新 suffix 卻未同步 ModuleNameToOutputItems，導致 -Module filter 找不到 action。只在專案 HotReloadState 預先映射全套自有模組的新 suffix，讓既有 ApplyState 同步 action 和輸出；已開始實際 46 actions。沒有修改引擎、沒有改正式啟動器去隔離測試專案。

- 本次最後查驗等待期間再度把 Source/OpenKoikatsu/OKStreetSocial* 當 rg 路徑，原生命令不展開；已立即改為 Source/OpenKoikatsu 目錄加 -g OKStreetSocial*.cpp／.h。後續命令在送出前逐項確認沒有路徑 wildcard。

### 本次正式發布結論

- 44Acceptance 無診斷暫停的完整測試通過，獨立驗證 15 組播放時鐘前進與同程序返回。水槍／分類的診斷使用另外的實際結果，沒有拿 elapsed=0 的診斷替代正常往返證據。
- 根專案 -1044 實際建置與 KK_384914 九項測試通過；原始 DLL hash 保留。角色四視角保留檢查及三張 packaged 來源卡外觀檢查通過。
- 嚴格發布工具成功，StreetSocial live receipt 已切到 InteractionCatalogue44.stage；兩個啟動器完整性檢查通過。新發布門檻保留 watergun/catalogue 證據與單人按需播放證據，後續發布不能遺漏已驗證的功能。
