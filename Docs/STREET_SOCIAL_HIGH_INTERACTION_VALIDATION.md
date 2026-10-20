# 街區與 CardReader 高模一對一往返

## 實作

街區「進入一對一場景」直接啟動 CardReader 使用的
`AOKInteractionScene`、`UOKInteractionWidget`、`AKKPreviewActor` 與原生
APNG 完整角色載入流程。男女角色使用街區目前選中的 APNG。
原有的六塊方塊房間與街區縮減角色流程已移除。

高模使用 CardReader 的既有主機，街區在原程序保留並暫停角色與操作。
互動 UI 的返回按鈕顯示「返回多人街區」；按返回、Esc 或關閉互動視窗後，
街區恢復同一個世界、人物位置、相機、導航與關係資料。
載入失敗或關閉視窗也會恢復街區，不以重建存檔代替返回。

高模主機設定由 `Tools/prepare_street_high_interaction_host.ps1` 產生，
沿用 CardReader 的引擎與專案配置。發布及啟動時核對設定與模組雜湊。
沒有把編輯器 UI 複製成另一套街區互動 UI。

## 已取得的證據

- `Saved/StreetSocial/HighHost-C019High01/result.json`：實際高模 scene/widget
  類別，38 個來源部件、91,439 個渲染頂點，男角載入成功，正常返回。
- `Saved/StreetSocial/HighHost-C019High01/high-interaction.png`：CardReader
  互動 UI 與完整角色畫面。
- StreetHigh03ContactsR2 已實際通過前幾組街區到高模再回街區；為最終
  完整回歸版本增加足夠測試時間後，主動中止這批測試，保留為未完成證據。
- StreetHigh05Full 已完成 5 名女角 × 陌生／朋友／戀人共 15 組高模往返，
  每次返回前檢查多人位置與關係資料；接續小地圖與街區完整功能回歸通過，
  錯誤清單為空，完成 15 段話題提交及 3 種設施驗證。
- Python 功能與發布防線共 62 項通過，包含拒絕低模場景冒充、載入錯誤
  女角、重複關係案例，以及高模路徑繞過電話移動檢查。

## StreetHigh05 回歸證據

下列證據皆使用 StreetHigh05 執行檔與共用 StreetOutline10 材質。

| 項目 | 證據 | 結果 |
| --- | --- | --- |
| 站姿、跑步、半透明 UI、即時數值與關係勾選 | Runtime-StreetHigh05Presentation | 通過，15 組關係控制 |
| 原作對話前後文與告白 | Runtime-StreetHigh05Context | 通過，20 組前後文、5 接受、5 拒絕 |
| 活動邀約 | Runtime-StreetHigh05Invitations | 通過，20 組 |
| 移動與碰撞 | Runtime-StreetHigh05Movement | 通過 |
| 自主手機、飲料、自拍、整理儀容 | Runtime-StreetHigh05AutoPhone／AutoDrink／AutoSelfie／AutoGroom | 各 5 名女角通過 |
| C019／C000／C044 來源幾何與三視角 | Saved/Crowd/StreetHigh05C019Source／C000Source／C044Source | 通過 |

高模證據涵蓋 C000、C019、C044、C022、C068，每名女角三種關係；
完整角色包含 34～58 個來源部件、37,270～204,895 個渲染頂點，15 次男角均載入。

## 發布結果

`Saved/StreetSocial/Build/receipt.json` 已更新，`runtime_private_high_model=true`、
`private_any_verified=15`、`full_functional_acceptance=true`，沒有沿用舊版功能證據。
話題、觸碰、電話、高模場景、邀約、原作告白與自主活動的獨立來源核對均通過。
啟動器 `Tools/run_street_social_development.ps1 -ValidateOnly` 通過。

- 執行檔：`Saved/StreetSocial/Build/StreetHigh05.stage/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe`
- 執行檔 SHA256：`67983ec7e9b60807a153ba0ecb0155a0891777112e388c7cbb48556c2dbe8bb0`
- CardReader 模組 SHA256：`d1514f646a57ef6a1ed050bd6d6b0077ba7c8b74efb82e56c27cdb32a5295f26`
- 功能回歸：`Saved/StreetSocial/Runtime-StreetHigh05Full/social.validation.json`
- 獨立高模核對：`Saved/StreetSocial/Runtime-StreetHigh05Full/contacts.independent.validation.json`
- 發布來源核對：`Saved/StreetSocial/Build/StreetHigh05.publish.log`

`full_functional_acceptance` 表示當前功能完整回歸通過；既有 `full_acceptance=false`
與 `appearance_original_game_parity=false` 保留，沒有把本輪標為原作逐像素還原或效能目標驗收。

## 錯誤與防止再犯

- 先前把街區角色搬到方塊房間，誤當成使用者指的 CardReader 高模場景。
  場景驗收必須核對實際 scene/widget 類別、載入器及幾何來源。
- 把 Gallery 的編輯器程式檔複製到精簡街區建置，造成 UHT 缺少編輯器
  類別。已撤出新增檔案，保留於 Saved 診斷資料；共用高模主機負責高模 UI。
- 只複製執行檔與 DLL，漏掉 staged Engine／Content，導致找不到專案
  descriptor。發行版必須保留完整已驗收的 stage 結構後再替換執行檔。
- 高模載入的時間必須納入完整回歸測試的時限；不能沿用先前簡化房間的
  900 秒總時限，也不能把中止的測試標成通過。
- StreetHigh04FullR2 的 15 組高模往返通過，但接續小地圖測試因將高模
  載入時間計入街區計時而逾時，整批仍是失敗。修正為共用街區活動時鐘：
  高模期間凍結街區時間，返回後排除暫停時段；電話冷卻、生活活動、對話、
  設施與驗收期限均使用同一時鐘。高模自身的 180 秒載入期限保留真實時間。
  不能只拉長小地圖測試時限掩蓋恢復後所有期限失真的問題。
- PowerShell 將資料夾複製到不存在的目的地時，可能直接以目的地名稱
  建立來源內容，不能假設會額外保留 Windows 這層名稱。建置後先驗證
  實際執行檔及 Engine／Content 路徑，再啟動相依測試；錯誤立即停止。
- Windows 的 rg 路徑參數不能直接放 `OKStreetSocial*.inl`，改為指定目錄
  並使用 `-g 'OKStreetSocial*'`，避免診斷漏搜。


## StreetBodyClick07 回歸

新增街區身體短反應後，`Runtime-StreetBodyClick07FullR2` 十五組高模往返與完整功能重測通過，啟動器完整性檢查通過。前輪一次 C068 renderer worker 中斷與獨立重測證據保留於 `Docs/STREET_SOCIAL_TOUCH_REACTION_VALIDATION.md`，未宣稱已定位其底層原因。
