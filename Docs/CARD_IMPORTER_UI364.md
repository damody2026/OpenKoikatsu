# Unreal Editor 獨立角色卡匯入介面

重新開啟 OpenKoikatsu 專案後，可以從以下任一入口開啟匯入視窗：

- 上方 **Tools → Import Independent Character Card...**。
- Content Browser 的 **Add／空白處右鍵 → Import Independent Character Card...**，位於 OpenKoikatsu 區段。

這是專用入口。原有 **Import to Current Folder** 仍是 Unreal 一般資產匯入，不要用它匯入角色 PNG；拖曳 PNG 也不會自動判成角色卡。

## 操作

1. 點 **Browse...** 選擇已轉換的獨立 PNG／APNG，也可貼上完整路徑。
2. 在 **New destination folder** 輸入新 Unreal 資產路徑，例如 `/Game/IndependentImports/MyCharacter`。必須是未存在的新資料夾，不能覆寫既有資產；目前預設路徑不隨 Content Browser 所選資料夾變動。
3. **Outfit index** 使用 `-1` 匯入所有服裝，或 `0` 匯入第一套；其他值對應該卡的服裝索引。
4. 點 **Import Character Assets**。匯入期間會顯示等待視窗；目前同步執行、沒有取消功能，資產較多時可能需要數分鐘。
5. 成功後 Content Browser 會選中輸出的 `Character` Data Asset，模型、骨架、材質與貼圖位於同資料夾的 `Resources`。

要組裝角色，把 `KKImportedCharacterActor` 放入關卡，設定 `Character` 指向剛匯入的清單及 `Outfit`。Actor 目前仍使用保存的 Static snapshot；另外保存的 Skeletal Mesh 可供 Unreal 工具使用，不表示已新增完整動畫控制器。

## 輸入與失敗處理

接受已轉換的独立 PNG／APNG；共用型卡需要內建 `Content/IndependentResources/v1` pack。原始未轉換 KKS PNG 不能在缺少來源資源時還原模型，介面會顯示原生 portable session 的錯誤，不回退遊戲安裝目錄。

不存在檔案、非 PNG/APNG、引號／換行、不合法或非 `/Game/` 路徑、已存在資料夾、目的地已有載入中的子 package、負數非法服裝與超出卡片範圍的服裝均拒絕。相似名稱的兄弟資料夾不會因 prefix 被誤拒絕。保存中途失敗可能留下部分資源；保留 Output Log，改用新資料夾重試。錯誤文字區可捲動。

## 實作與驗收

新增 `KKCardImporterEditor` Editor-only 模組。ToolMenus／Slate／DesktopPlatform／ContentBrowser 相依只在此模組；runtime 資產插件與匯入演算法不變。介面先用空 GameRoot 的原生 session 做結構化預檢，再呼叫既有 `KKImportIndependentCard` commandlet 共用保存流程，不另寫模型、材質或骨架轉換公式。匯出錯誤捕捉後先 flush log，再讀取診斷；Character 保存成功才定位資產。

- 最終完整 Editor BuildR7 Succeeded／actual0，6.07 秒；模組由完整建置自動登錄。
- InterfaceR5：Tools／Content Browser 選單註冊、執行選單動作建立 Slate 視窗及 PNG 截圖、輸入拒絕、原始卡與越界服裝診斷、實際單套匯入、禁止覆寫，Automation Success／actual0。
- 最新介面測試輸出 `/Game/IndependentImports/Stage364InterfaceR5/Character.Character`：單套、273 個資產。九套最新完整資產仍為 Stage361AllR1，未覆寫。
- VerifyUIR1：新程序、實際 RHI、無來源卡重開，actual0；46 Static／11 Skeletal／125 材質／89 貼圖／1 Skeleton／1 Character，Static 140,997 三角形，Skin 25,168 三角形；四 UV、頂點色、36 條原 mip 鏈／434 Source 層、312 個模型內 morph 項目／96,012 個有效位置 delta（tiny0）通過。
- 截圖 `Saved/Tests/AssetImport364/ImporterUIR5.png` 已檢查；來源、部署 DLL、失敗／成功 log、輸入與資產雜湊見新 `Saved/Tests/AssetImport364/Audit.json`。

保存事故：BuildR1 DLL 被原有 Editor 鎖定失敗6；BuildR2 私有 UToolMenu::FindEntry 編譯錯誤6；單模組 BuildR3 成功但不更新 binary manifest，InterfaceR1 未找到模組、啟動失敗後只終止該測試程序，original Editor 未終止。測試暫補 module manifest，使用者關閉 Editor 後 BuildR4 已重新由完整建置產生 manifest。InterfaceR3 actual0 但 Automation Fail：FUIAction 不適用 TryExecuteToolUIAction，且預期拒絕日誌與診斷擷取未處理完整；BuildR5 又誤取 private Action 失敗6。改用原生 FToolUIAction context delegate、結構化 session 預檢及正確的預期錯誤測試後 R4／R5 通過。所有事故保留，不能用退出碼0或一行 passed 當驗收。

這輪未驗收所有卡、GPU 像素、完整 controller、私有 rig、原始任意 KKS 卡免來源還原或整個應用 Shipping／安裝包；Stage355 的 279 個未通過案例不變。使用者上一輪未完成的可選骨架 Actor 模式未在此功能加入。
