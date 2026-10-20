# 街區對話中的身體點觸反應

## 計畫與實作

1. 查原作 `TalkScene.TouchFunc`、`LoadVoice`、`AnimePlay` 的呼叫鏈。
2. 共用頭／臉／手／胸口原作反應曲線，依各 APNG 私有骨架背景建立 GPU 姿勢。
3. 取來源動作開頭 0.75 秒，結束平滑回站姿；台詞、表情、關係及重複次數規則沿用。
4. 真實射線點擊、短動作可見骨架位移、角色根節點不移動、連點阻擋及取消復位驗證。
5. 最後完整功能回歸與發布核對。

這是街區的短反應呈現規則；不是宣稱原作每筆性格 pose 都相同。
原作反應狀態包含 S／M／L 身高混合，本輪沿用共用動畫流程的 M 原始片段，
再套用每張 APNG 自己的骨架與身形，不生成角色專屬 Cook 資產。

## 原作來源

`C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/TalkScene.cs`：
TouchFunc 選部位／重複次數台詞，LoadVoice 同時設定表情並呼叫 AnimePlay。
原始曲線：`adv/motion/controller/adv/00.unity3d` 的
`f_reaction_00_02_M`、`f_reaction_00_08_M`、`f_reaction_00_09_M`、`f_reaction_00_05_M`。
共用來源與檔案雜湊存於 `Content/IndependentResources/RuntimeAnimations/touch.manifest.json`。

## 錯誤與防止重犯

- 目前 Touch 只選台詞與表情，動作仍是一般說話手勢；不能因觸碰語音已完成便宣稱身體反應完成。
- 原作反應 raw clip 約 11～12 秒，不能整段播放當作「抖一下」。加入通用非循環動作播放視窗，
  僅準備開頭 0.75 秒；一般動作沒有視窗時仍使用完整長度。
- 播放視窗必須同時作用於初始準備與 runtime streamed admission；只改初始姿勢準備不會影響真正點擊。
- 查來源時先核對資料夾與檔名；Windows rg 用目錄加 `-g`，不要把 wildcard 當 literal path。
  巨大單行 catalogue 用 Python 解析，避免 rg 印出整份 JSON。查字面括號使用 `rg -F`，避免正規式括號未閉合。
- 初步曾匯出全部性格 pose（30 個共用曲線）；短反應只需四個來源片段，正式 manifest 只載入這四個，
  不能把所有無關長姿勢塞入有界 runtime action admission。
- StreetTouch03 編譯時 TMap 的 `!=` 被 Unreal 刪除；驗收計數使用 `OrderIndependentCompareEqual`，
  不可套用標準容器的比較假設。修正後重新建置，失敗 log 保留。
- StreetTouch04Reaction 在載入前被完整性檢查擋下：CardReader 根專案模組已變更，
  不得沿用舊主機雜湊。重新準備主機設定並實際測試高模入口，完整回歸也須驗證新模組。
- StreetTouch04ReactionR2 發現 pose bank 原有契約只允許完整曲線長度，短視窗被拒絕。
  新增明確 opt-in 的 AuthoredPrefix，仍驗證視窗不超過原始長度，普通動作維持原完整長度契約。
  這批中止不算通過；重新驗證真正 streamed GPU 播放，不能只檢查 manifest 的數字。
- StreetTouch05 提醒精簡主機的 Plugins 是獨立副本，修改共用 request 型別時，必須同步
  header 與實作到主機後才建置；不能只同步街區 Source 檔。
- StreetTouch06.stage 名稱已有其他既存產物，Copy-Item 因目的地存在多包一層。
  已把本輪新複製的子資料夾移到全新 StreetBodyClick07.stage，保留原有產物。
  建置與測試使用唯一新 tag，先驗證 stage 及執行檔路徑，任何複製錯誤即停止後續步驟。

## 驗收

`Runtime-StreetBodyClick07Reaction` 二十組實際射線／GPU 反應通過，五名女角各測頭、臉、手、胸口。
原作共用曲線以私有 APNG 骨架播放 0.75 秒，觀測頭部位移 4.02～8.54 公分，
二十組根位置均不移動，連點不會重啟，取消會清除觸碰狀態。
`touch-reaction.independent.validation.json` 獨立來源／畫面／部位核對通過。
Python 測試 67 項通過，包含拒絕無骨架位移、整個角色平移、錯部位曲線及重複案例。

`StreetBodyClick07` 已完成三張來源幾何三視角、presentation、對話上下文、邀約、
四種女角自主活動及移動回歸，全部通過。

- StreetBodyClick07Full 第十三組 C068 高模主機在 renderer worker 的
  `FReadOnlyMeshDrawSingleShaderBindings::SetShaderBindings` 發生 access violation，沒有返回報告。
  父程序正確判定失敗；不得將前十二組當成完整通過，也不得忽略崩潰或自動標記成功。
  失敗 log 保留於該輪 HighInteraction/96CDD926481316565E64C49B24B6C7B3/host.log。
  同執行檔、同高模模組的 C068 獨立重測 `HighHost-StreetBodyClick07C068Retry` 通過。
  尚無證據定位成短反應程式錯誤；目前未修改渲染器或關閉平行渲染來掩蓋問題。
  停止其他驗收程序後，重新執行完整十五組與所有功能，使用新 tag StreetBodyClick07FullR2。


## 最終發布驗收

- 完整重測：`Saved/StreetSocial/Runtime-StreetBodyClick07FullR2/social.validation.json`，passed=true、errors=[]。
- 五名女角×陌生／朋友／戀人，十五組 CardReader 高模進入與返回通過；包含先前中斷的 C068 三組。
- 原作來源獨立核對：848 份語音雜湊、告白成功／普通／拒絕／親密邀請、觸碰次數與原子保存通過。
- 新增二十組真實射線／私有 GPU 骨架短反應，四種共用曲線核對通過。
- C019／C000／C044 來源幾何三視角通過，CPU／GPU 比較差異皆為零；不宣稱原作逐像素一致。
- 67 項 Python 測試通過；執行檔建置及相關 diff whitespace 檢查通過。
- `Tools/publish_street_social_appearance.py` 所有發布閘門通過，舊 receipt 已備份。
- `Tools/run_street_social_development.ps1 -ValidateOnly` 通過，正式入口 `StreetSocial.cmd` 已指向本輪版本。
- 初步三十個未使用曲線移至 `Saved/StreetSocial/Build/StreetBodyClick07.unused-touch-exports`；正式 manifest 僅四個曲線。

發布執行檔：`C:/portable/OpenKoikatsu/Saved/StreetSocial/Build/StreetBodyClick07.stage/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe`。
SHA256：`ced27009b8e0dec35d0cc3bb876826efc1ac663d847ad570b673a787993e28cf`。
高模主機模組 SHA256：`265f69c4667decad6dfb041b39c310e3b3d16824fc709d33d546614c526a4401`。
發布紀錄：`Saved/StreetSocial/Build/StreetBodyClick07.publish.log`。

前輪一次高模 renderer worker 中斷原因尚未定位；獨立重測及完整十五組重測均通過。
此為保留的偶發失敗證據，不宣稱已修復其底層原因；正式發布沒有放寬驗收或修改渲染設定。
