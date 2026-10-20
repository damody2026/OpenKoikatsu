# Minimalist 與關閉陰影

## 計畫
1. 從原作清單確認 Minimalist 的資源類型與來源。
2. 把它保存為共用資源；原始卡與獨立 APNG 使用相同場景設定。
3. 關閉角色陰影強度與即時陰影深度擷取，保留輪廓和材質高光。
4. 編譯，使用穿衣角色做 3200×2000 四方向驗證，記錄實際結果。

## 確認來源
- Minimalist 是 mt_ramp，不是 Amplify Color LUT。原作 resolved id=100011612，名稱 Minimalist，bundle=chara/Augh/minimalist_shadow.unity3d，texture=minimalist。
- 原作設定使用 rampGUID=Augh.MinimalShadow、local rampId=1；不能直接拿 local id 與 resolved id 比較。
- 原作探針清單：Saved/Tests/MinimalistNativeClothedR2/Card0.json.ramps.tsv。
- 共用資源由 Tools/extract_character_ramp.lua 直接解碼原始 zipmod 的 Texture2D，保留 Linear 空間，不反推濾鏡。PNG 與來源 hash 記於 Content/KKPreview/MinimalistRamp.json。

## 實作
- Config/DefaultGame.ini 的 OpenKoikatsu.CharacterDisplay 設定 RampTexture=KKPreview/MinimalistRamp.png、EnableShadows=False。
- 預覽與 cooked 高模材質共用此場景偏好；不改 APNG 卡片或角色材質的來源色值。
- 無陰影模式將 Koikano 全域陰影 alpha 設為1，且跳過動態與截圖的陰影深度擷取。保留清空為遠距離的深度目標，使舊 Forge 材質仍有合法可取樣資源。
- 共用 Ramp 隨打包 stage，不依賴原作安裝路徑；只在載入時解碼，逐幀不讀設定或解碼。
- 尚待編譯及實際截圖；不得把編輯檔案成功當作驗收通過。

## 驗證進度
- 原始 Minimalist Ramp 與原作實際 _RampG 讀回（解碼 RGB sRGB transfer 後）MAE=0.007064/255，最大誤差1.229/255；是原始共用資源，不是近似 LUT。
- Editor R1 模組編譯成功38.16秒；獨立 APNG 的四方向3200×2000截圖成功，日誌確認 ramp=MinimalistRamp.png、shadows=0、shadow capture skipped。
- R1 背面裙褶仍出現材質自身陰影色，因此補上無陰影模式的 _ShadowColor=white 場景覆寫；Preview 與 cooked binder 同步。停止陰影擷取只消除投影遮蔽，不會自動消除 shader 的明暗色混合。
- 原始卡 R1 截圖成功；R2 編譯及新圖待驗證。

## 功能驗證結果
- Editor R2 編譯成功19.79秒。獨立 APNG 與原始卡 R2 各完成正面、背面、側面、臉部3200×2000截圖，exit=0，沒有材質編譯失敗或 runtime fatal。
- 兩組日誌均確認 MinimalistRamp.png、shadows=0、四次 shadow capture skipped；共用 Ramp 不依賴原作目錄，APNG 使用 -Portable 載入成功。
- 已檢查穿衣 APNG 的四方向：R1 背面衣褶殘留陰影色在 R2 消除，輪廓、高光與來源髮色層次保留。外觀由使用者最後驗收。
- 機器驗證報告：Saved/Tests/MinimalistNoShadowAPNG2xR2/acceptance.json。
- 非 Editor R3 模組編譯成功15.66秒，包含具名初始化修正；此驗證不等同打包遊戲完整執行。
- Editor R3 物件編譯完成，但 DLL 重新連結被另一個正在執行的 CharacterMCP/LiveR3 背景測試鎖定。既有 R2 DLL 已包含本次 Minimalist／無陰影功能並完成上述實際截圖；不終止無關背景測試。
- 額外全專案 Editor build 被 -NoEngineChanges 阻擋：會修改引擎 Android/IOS UnrealEditor.modules，因此保留保護，沒有修改引擎檔案。
