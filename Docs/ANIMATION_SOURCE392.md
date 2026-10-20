# Stage392 動畫來源選單的回饋與重載

使用者截圖的選單是 GameSourceCombo：它選擇包含多個片段的 bundle/controller。原本只更新遠在 Studio、場景校正與手動路徑下面的 GameClipCombo，不會播放；準備提示只有 InspectorText，成功後還留下「正在準備動作」。因此看起來沒有反應。這次保持明確選擇片段後才播放，不任選第一個片段。

## 修改

- 來源標題明示下一步，GameClipCombo 放在來源正下方，旁邊 AnimationStatus 顯示準備、動作個數、播放、失敗與取消。
- 所有通用動畫請求開始就停用舊 clip 清單；事件入口也阻擋舊清單切換，排隊／失敗／取消完成後恢復使用。
- 來源準備失敗不替換原清單和播放，來源 selector 回復已提交的來源；警告日誌記錄 kind/source/error。
- 手動載入直接套用已準備結果，不依靠 SetSelectedOption 的變更事件。同一來源重載、第一次手動載入都只更新一次。
- 工作仍在原 Stage391 背景佇列，保持世代與角色生命週期檢查。沒有更動資源解析、原動畫、IK、嘴巴或物理演算法。

## 原遊戲參考

`C:/portable/KoikatuSunshine/kks_decomp/Managed/CharaStudio_Data/Assembly-CSharp/Studio/OCIChar.cs` 的 LoadAnime 先按 group/category/no 取得 AnimeLoadInfo，再 LoadAnimation(bundlePath,fileName)，最後以 value3.clip 呼叫 charAnimeCtrl.Play。來源和指定動作分開處理；新介面不把任一 bundle 的首片段當成使用者想播放的動作。

## 驗證

BuildR2 使用引擎正式 ModuleWithSuffix=OpenKoikatsu,0396 路徑，actual exit 0 / Result: Succeeded；UnrealEditor.modules 指向新 DLL。另一工作仍有四個群眾資源 commandlet 使用舊 DLL，未停止它們。

Saved/Tests/AnimationSource392/GuiR1：actual exit 0、valid=true、8 cases。真實 UMG selection delegate / handler，包含相鄰位置、清楚準備提示、基本來源 530 clips、選片段後 PoseMode=2/時間前進、Studio 來源 5006 clips、同來源重載清除人為 stale 清單、缺檔手動載入、缺檔來源 selector 回復、舊清單不得取代待辦來源、快速 latest selection，以及 T-pose 取消。失敗 fixtures 是刻意測試的不存在路徑；2 行 ready=0 為預期結果。不是實體滑鼠逐項點選測試，也不宣稱每個列出來的片段皆已驗證。

GuiR1 首次 Studio 來源 worker 約13.962秒，commit 約3.023ms，期間原動畫265次時間樣本前進；基本片段 commit 約29.784ms。大量來源首次背景解析等待仍存在。

MenuRegressionR1：actual exit 0、valid=true10cases，Studio首次 commit53.834ms／重複18.744ms，clips21.006／27.747ms，replay19.772ms，H71.316ms；全部request與有效commit低於原150ms門檻。首次 Studio worker46.147秒／H27.690秒仍在背景。此輪使用 BuildR2 的配套。

追加 review 發現新狀態列在重播／Studio套用失敗會留在準備中；補重播成功／失敗與Studio錯誤提示，新增第9項重播狀態測試。最終 BuildR4 actual0 / Succeeded，三模組配套 main-0398 / plugin-0008。GuiR3 actual0 / validtrue9cases，重播提示與時間前進正常；Studio來源worker13.318秒、commit1.908ms，同來源refresh commit1.791ms，clip16.008ms、replay17.736ms。MenuRegressionR2 actual0 / validtrue10cases：Studio首次22.207／repeat19.369ms，replay18.661ms，H58.909ms；所有request及有效commit仍低於150ms。受同時匯入工作與冷controller影響，回歸首次Studio背景準備約59.5秒、H30.431秒，不以GUI更新快宣稱首次解析已消失。

Saved/Tests/AnimationSource392/R1.audit.json / R1.snapshot 封存 source、主程式／相依插件DLL與manifest、link response、成功和失敗logs/receipts/JSON/source.diff。舊Stage391420項證據hash/size保留。本輪未停止任何非本輪程序。重開Unreal即使用新manifest配套。

## 本次錯誤與保留

BuildR1 actual 6：引擎嘗試清理仍被其它 commandlet 佔用的 -0393 hot-reload DLL；改新編號編譯，不終止別人的工作。PowerShell 未加引號的 -ModuleWithSuffix=OpenKoikatsu,0396 因逗號報 parser error，已改整個參數加引號；未視為編譯成功。apply_patch stale-log context 兩次匹配失敗，未局部套用，以讀回的確切行置換並檢查 diff。搜尋曾猜錯 KoikatsuSunshine_Data 與 TargetDescriptor.cs 所在目錄，之後改 rg --files 取得真實路徑。

Stage391 R1 audit SHA256 E67EBC60014E521369388F3410B14C493B081DD96A6F4992EBE449A9606C73E2 的420項舊檔案全部 hash/size 未變；本輪使用新證據目錄，不覆寫舊測試。

BuildR3 actual0，但 GuiR2 啟動 actual3、沒有結果JSON：Unreal Cannot replace existing object of a different class / Default__KKAnimationCatalog，重複註冊同一插件。根因是只指定主模組新編號，這次插件也因其它工作變更而重建：plugin manifest指向-0007，但主程式link response還用unsuffixed KKUnityBundle.lib，啟動載入兩版。BuildR4 同時明確指定 OpenKoikatsu,0398 / KKUnityBundle,0008 / KKCardImporterEditor,0008；檢查主程式response確實連到插件-0008.lib，避免混版。不以編譯成功當啟動成功。失敗日志／actual3 receipt保留，runner增加啟動失敗而沒有JSON時的明確錯誤。

封存初次在建立snapshot/audit前因GuiR3中文JSON的UTF-16 BOM而decode失敗；helper依BOM選UTF-16或UTF-8，不改原檔，重新驗收。
