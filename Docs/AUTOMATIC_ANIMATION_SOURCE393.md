# Stage393 選動作自動載入與同步來源

使用者要直接在「選擇遊戲動作」選動作，自動選對應動畫來源。原清單只包含目前來源的片段，必須先手動找bundle。現在將已匯入 studio_catalog 的 anime / hanime 資料列加入遊戲動作清單，初次載入及任何來源重新列片段後都可選這些目錄動作。

目錄項目保存完整資料列身份，不靠片段名稱猜bundle。選取時 background job 先按 StudioSource(Row) 取得base/owner/override controller組合，再 ResolveStudioAnimation 選定state，保留原生H動畫layer/Yure準備。GameClip與Studio選單共用ApplyStudioMotion，使用既有SetStudioAnimation；成功提交才同步來源、目前片段、Studio row與scene。來源選單只是可選的原始片段瀏覽入口。

不先打開每個controller掃描全遊戲：清單使用已匯入目錄的metadata，cold解析仍在選定動作後的worker完成。基本／手動來源的原始片段維持原scoped key。

目錄同名不同列保存獨立key，包含category/group/name及必要重名suffix。原始clip若與目錄項目顯示文字衝突，另外顯示原始片段suffix並用GameRawClipKeys保存完整來源／真clip key，避免名稱相撞錯播或漏列。

## 驗證

最終 BuildR5 actual0 / Succeeded，main-0403 / KKUnityBundle-0013 / Importer-0013 同一配套。AutoR3 actual0 / validtrue10cases，8751目錄項；普通首次21.332ms／repeat25.417ms，跨來源基本raw19.020ms，H67.520ms，所有request及有效commit低於150ms，H等待期間615worldframes／512舊動畫時間樣本。FeedbackR3 actual0 / validtrue9cases，同來源重載、缺檔恢復、raw舊清單guard、最新請求、重播與取消保持正常。測試以真實GameClipCombo事件直接選ordinary/H row，不先操作來源或Studio分類；驗證來源/row同步、重播、快切、取消、源清單與raw/catalog衝突。這不是全部遊戲片段或實體滑鼠點擊驗證。

## Review與錯誤紀錄

原raw舊清單guard會擋所有busy事件；目錄row有獨立且穩定的來源，允許它取代pending請求，raw仍須遵守停用guard。這個review修正發生於BuildR1完成後，所以另做fresh BuildR2，不能用R1代表最終程式。沿用Stage392三個模組explicit suffix，避免plugin/main import混版，未停止其它資源匯入程序。

原Stage392320項封存證據需要在新audit逐項核對並保留。

性能驗收AutoR1actual0/validfalse（H524.767ms）；AutoR2actual0/validfalse（H151.856ms），兩次皆未提高150ms預算，全部原始日志JSON保留。補worker exact Studio零值options預熱、optional motion None/Some(0)，不改native setter的參數／Yure政策；OK_MENU_PROFILE僅粗分parameters/yure/commit。R1未分項不能斷言493ms皆parameters；R2parameters2.402/yure0.587/commit67.395ms。

目錄動作切換現在不重建8751目錄選項。原始片段持有自己的scoped key，不再依當下selector拼來源，故global自動來源切換後舊raw仍選對來源；source worker也從保存key推導source。初載／手動選來源更新raw，其餘保留目錄，成功才同步来源。額外case4跨來源raw驗證與最終BuildR5。

最終證據 Saved/Tests/AnimationAutoSource393/R1.audit.json / R1.snapshot，包含成功／失敗日志、receipts、JSON、source.diff、源碼與三模組DLL/manifest/import response；舊Stage392320项保留。首次controller冷解析仍有背景等待；metadata目錄8751項不代表8751個動作全部逐幀驗證，測試使用真實UMG事件／March原生session，不是實體滑鼠全操作。沒有更改角色卡資源、減少物理更新或提高性能門檻。重開Unreal即可直接選動作自動同步來源。
