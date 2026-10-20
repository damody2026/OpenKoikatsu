# OK Studio 驗收與圖片證據

使用 E 線已建置成功的 Editor，從專案根目錄執行：

```powershell
.\Tools\lua\lua.exe Tools/verify_ok_studio.lua --tag <新的名稱> --editor --compare
```

`--editor` 加跑 EditorSelfTest；`--compare` 加跑 14 張原作樣本對照。不帶兩個旗標時執行 GameSelfTest、PatternTest、EyeSetTest、BloomTest。可用 `--only BloomTest` 單獨重跑泛光 GPU 檢查。每個測試使用獨立的受控 UE 子程序，保留 PID 與逾時限制；啟動引擎前必須完成建置。

## 每輪獨立保存

輸出位於 `Saved/OKStudio/Verify-<名稱>/`，名稱不能重複：

| 檔案 | 用途 |
|---|---|
| `ACCEPTANCE.md` | 自動產生測試表、記憶體結果與完整單張嵌圖 |
| `results.json` | 結果、程式版本、圖片 SHA-256、尺寸、檔案大小與記憶體 |
| `images/*.png` | 本輪生成的 1280×360 左右對照圖 |
| `<測試名稱>.log`／`.pid` | 每個子程序的執行紀錄與 PID |

工具以 `-OKStudioCompareOutput=<本輪 images 絕對路徑>` 指定實際擷取輸出，直接寫入本輪目錄，不再從共用 `Saved/OKStudio/compare` 撈圖。手動執行 `OKStudio.CompareKStudio` 未指定輸出時，仍使用既有共用目錄。

Markdown 以相對路徑嵌圖，整個驗收資料夾可一同搬移。中文、日文、空白、括號與 `#`／`%` 檔名會轉成合法的圖片連結。JSON 的 `revision` 如帶 `+worktree`，表示執行時含尚未提交的已追蹤變更；不能當成純 HEAD 的測試。

## 失敗判定

Compare 必須同時滿足程序 exit code 0、14／14 完成訊息、最後場景釋放紀錄，以及 14 張具名成功擷取紀錄。每個紀錄必須有本輪目錄內可完整解碼的 1280×360 PNG，並記錄雜湊。缺圖、重複檔名、路徑穿越、PNG 截斷、IEND 後附加資料或 OK 卡 chunks 都會讓本輪失敗；只在共用目錄找到舊圖不算通過。

報告在每項測試後更新，整套跑完才設定 `finished=true`。尚未結束時 Markdown 顯示「執行中」，不會先宣告整套通過。

**PASS 僅代表功能測試與圖片證據完整，不代表逐像素吻合。** 對照圖左側為原作場景卡內預覽縮圖，右側為本輪 Unreal 擷取；仍要逐圖判讀構圖、材質、光照、角色與後製差異。記憶體最終卸載值也不能直接稱為回到啟動基準。

## 工具回歸測試

```powershell
.\Tools\lua\lua.exe Tools/test_ok_studio_evidence.lua
.\Tools\lua\lua.exe Tools/lua/lint.lua Tools/verify_ok_studio.lua Tools/ok_studio_evidence.lua Tools/test_ok_studio_evidence.lua
```

測試在新的 `Saved/Tests/StudioEvidence/` 子目錄使用普通文件截圖，不改動正式輸出；涵蓋缺失／重複／損壞／目錄外圖片、卡片附加資料與跨語言連結。

需要提交圖文紀錄時，只提交普通截圖與文件，仍須通過 `Tools/check_no_cards.lua`。不提交原作卡、OK 卡、含角色的 Studio 場景或整個 Saved 目錄。既有實例見 [HANDOFF_E 圖文驗收紀錄](OK_STUDIO_HANDOFF_E_ACCEPTANCE_20261020.md)。

## 2026-10-20 實際驗收

`HandoffEAutoEvidence` 完整執行通過：GameSelfTest、PatternTest、EyeSetTest、EditorSelfTest 均 PASS；CompareKStudio 14／14 出圖，且 14 張圖片均通過完整解碼、尺寸、雜湊與具名紀錄核對。最後再以已提交的收集器重讀同輪 log／圖片，並用另一個 PNG 解碼器及 SHA-256 實作核對，全部一致。Editor／一般遊戲 Development 建置成功；工具 fixture 13 PASS，Lua 靜態檢查 0 problems。不存在引擎的負向案例另確認非零結束、最終 FAIL 與錯誤原因保存。

本輪擷取峰值 20.6 GB，最後卸載 12.2 GB；仍不代表回到啟動基準，也未將與前輪的差異歸因於此次工具修改。完整的自動報告、14 張圖與逐項 log 保留在本機 `Saved/OKStudio/Verify-HandoffEAutoEvidence/`。執行時程式尚未提交，故報告保留 `08d73ebfd+worktree`；實作整理為 `35344d0ec`。

下面是這輪實際產生的 0010 對照圖，已與自動報告內的原檔核對位元組一致。左原作縮圖，右 Unreal 結果；圖中的明暗／色調差異仍然存在。這次驗收確認自動保存與報告功能成功，沒有改變既有的視覺差異判定。

![本輪自動保存的 0010：左原作縮圖、右 Unreal 結果](images/OKStudio/AutoEvidence20261020/koikatu_cs0000010.png)

整合主線 `fdf43e943` 後，Editor／一般遊戲 Development 再次建置成功，`Verify-HandoffEAutoEvidenceMerged` 三項遊戲自測全 PASS；新增街區規則的 `shop::tests` 6 PASS，規則 DLL 已重建。這次主線更新沒有變更 Studio 擷取／材質程式，完整 14 圖證據仍對應前述本輪執行。
