# StreetSocial 渲染修正錯誤紀錄

## 2026-10-09

- **把 runtime CPU/GPU 一致誤當外觀還原完成。** 兩條路徑可以共用同一個錯誤。之後必須另外比對 CardReader／原作的材質輸入及畫面，完成前不宣稱完全還原。
- **未完成外觀差異便結束工作。** 泛白與掛點改善後仍有高光、描邊、衣服陰影差異。將這些項目列為本次必須完成的驗收條件。
- **貼圖製作上限與上傳檢查各自寫死。** 提高到 1024 後，上傳仍拒絕超過 256。已改為共用常數；品質限制須由同一處定義並驗證完整 mip chain。
- **把中立剛性頂點當成已套動畫的頂點。** 錯誤反向套用初始掛點，造成固定偏移。剛性與蒙皮資料須區分，掛點差值只套一次。
- **只處理 Gamma flag，漏掉 Color 型別。** 色彩空間必須依來源屬性型別與實際遊戲流程驗證，不能用單張角色的補色值修補。
- **同名預覽貼圖搶先綁定。** 原始 shader 貼圖現在優先於 legacy preview streams；必須保留 sampler、sRGB、wrap、mip 的來源設定。
- **診斷 ShotPreview 未限制服裝數。** 單卡預設拍攝所有服裝，造成額外等待。單服裝診斷明確設定 `OK_OUTFITS_LIMIT=1`，只終止本工作建立且已核對命令列的程序。
- **PowerShell 路徑萬用字元交給 rg。** `rg path/*.cpp` 不可靠；改用搜尋目錄搭配 `-g '*.cpp'`。讀取單行巨型 JSON 要先解析再輸出摘要，不能直接 Get-Content。
- **錯用 Python `utf8-sig`。** 正確名稱是 `utf-8-sig`。相對路徑以工具指定的工作目錄為準。
- **語音回歸曾逾時。** Appearance04 Contacts 在一段 greeting 停住；Appearance05 同項重測通過全部 15 組。保留失敗紀錄，完整驗收再次確認，不以重測結果刪除失敗證據。

## 本輪診斷紀錄

### 本輪驗證與修正

- CardReader 自動 ShotPreview 的相機落在巨大幾何內，並有相機 assertion；該批圖不能作為正常 CardReader 的外觀依據，保留原圖與 log。
- Outline06：來源 shader 可以沒有貼圖，C++ 零長度陣列無法編譯。產生器改用可為空的容器。
- Outline07：只反射像素 shader，漏掉頂點膨脹使用的 MainTex。共用 layout 必須合併頂點與像素 shader 的貼圖參數及來源預設。
- Outline08：無暫存器的常色 shader 沒有 dcl_temps，輸出宣告不能依賴它。轉譯器已將輸出宣告獨立處理。
- PowerShell 的 ConvertFrom-Json 會拒絕 `_LineColor` 與 `_linecolor` 這類同字不同大小寫鍵。材質診斷使用 Python JSON 或 `-AsHashtable`。
- Python 讀寫程式碼一律指定 UTF-8；有 BOM 時使用 `utf-8-sig`。工具改變工作目錄後，檔案路徑必須使用已核對的絕對路徑。
- StreetSocial 相關 Python 測試目前 53 項通過；正式執行檔及畫面驗證仍須另做。
- Outline10 完整驗收重現一對一 greeting 逾時，不能用早先單次 Contacts 通過結案。GPU 角色移動的是 instance transform，附掛在群組 component 的聲音沒有跟著角色移動，會在遠處場景被距離裁切。語音改為每幀跟隨實際角色 transform，並使用 PlayWhenSilent 保留聲音生命週期；須以新版完整驗收確認。

- Outline12：本體 shader 的全域 `_linewidthG` 先前沒有綁定來源 `_LineWidth`，已補上，避免只修 outline 而漏掉衣服本體的細線。
- Outline11 完整測試因最後的線寬修正而主動中止；此批標示為未完成，不算通過，也不當成程式失敗。
- 診斷參數 VisualClip 最小為 1，不能用 0 選 idle；改為 `VisualBlend=0` 使用第一段 idle。
- Windows Python 終端可能是 CP950；輸出含日文或 replacement character 的 log 前設定 `PYTHONIOENCODING=utf-8`，不要再次依賴系統編碼。
- `python -m unittest Tools/test_shader_program_codegen.py` 的匯入路徑不包含 Tools；正確方式是 `python Tools/test_shader_program_codegen.py`，11 項測試通過。錯誤的執行紀錄保留，不列為產品測試失敗。
- 發布驗證拒絕 C000／C044 第一批圖片，因該批使用一般幾何縮減模式，不符合來源幾何驗證條件。另以 `PreserveSourceGeometry` 拍攝完整三視角；保留一般模式證據，不能修改 receipt 假裝已用來源幾何。
- runtime 測試腳本結束只代表場景自我檢查通過，不會自動執行所有 Python 獨立來源核對。新版發布工具現在主動重新驗證話題、觸碰、手機、一對一進場、邀約與自主活動來源，避免漏跑或讀到舊的 independent JSON。

## 驗收範圍

- 編輯紀錄時兩次使用了不完整的段落作為 apply_patch context，導致整批 patch 被拒絕。修改前先讀取實際檔案尾段；每個檔案使用完整且存在的 context，避免夾帶沒有修改內容的第二檔案 patch。
- 最終 StreetOutline12 完整回歸、告白前後文、presentation、movement、邀約及四類自主活動全部通過；獨立來源驗證重新產生並通過。57 項 StreetSocial 與 11 項 shader 測試通過。新版已發布，啟動器完整性驗證通過。

原始 outline pass 已以共用預編譯材質接入，來源涵蓋 10 個 shader 家族、22 種 recipe；沒有針對 C019 補色或平移掛點。來源與 APNG 的髮絲貼圖、UV 通道、陰影深度與線寬已比對。C019、C000、C044 的來源幾何三視角與一般縮減模式分別保留證據。

CPU／GPU 一致只證明 runtime 兩條路徑一致；原作的相機、光照與後處理不同，不能將此標為原作逐像素還原。正式版本以當前功能完整回歸通過為發布條件。

## CardReader 高模場景需求修正

先前把街區縮減角色放入另一個房間，沒有符合「CardReader 高模一對一」的實際需求。
已改接 CardReader 原本的場景、互動 UI 與完整 APNG 載入器，保留原街區程序供返回。
驗收必須核對場景類別、實際角色卡、男女載入與完整幾何證據，不能只檢查房間切換。
建置、stage 路徑、未完成測試等錯誤與防止重犯方式記於
`Docs/STREET_SOCIAL_HIGH_INTERACTION_VALIDATION.md`。

StreetHigh05 最終完整功能回歸、十五組高模往返、獨立來源核對與啟動器完整性驗證通過，
已更新正式 receipt。StreetHigh04 的小地圖逾時保留為失敗證據；修正街區共用活動時鐘後
重新跑完全部功能。62 項 Python 測試通過。


## 對話身體點擊反應

短反應須使用明確的來源曲線前段視窗；不可把完整十二秒曲線加速到短時間，或只平移人物根位置假裝抖動。共用型別修改須同步精簡主機的 Plugins 副本，stage 使用唯一名稱並先核對執行檔路徑。本輪實測錯誤與修正完整記於 `Docs/STREET_SOCIAL_TOUCH_REACTION_VALIDATION.md`。

StreetBodyClick07 最終完整重測、二十組身體反應、十五組高模往返、三張來源幾何外觀與正式啟動器完整性檢查均通過，已發布。前輪高模渲染中斷仍保留為未定位的偶發失敗證據。

## 共用色彩與髮絲描邊（2026-10-09）

詳細的調查、失敗原因與驗收證據集中記於 `Docs/STREET_SOCIAL_COLOR_PROFILE_VALIDATION.md`。
使用者已確認的光影保持不變；缺少髮束線條的根因是 runtime 將沒有 `_OUTLINE_ON` keyword 的 legacy hair 當成禁止 outline，和 CardReader 的無條件 hair pass 不一致。
修正採用共用 shader 家族規則，不依角色名、卡號或髮色判斷。
發布檢查新增每個 hair part 的 outline_enabled 證據及 shader fallback 拒絕條件，並禁止同時掛載兩版 `ShaderArchive-RuntimeMaterialBake`。
不能只憑 CPU/GPU 一致、其他部位有 outline 或材質成功編譯，就宣布頭髮已修好；必須檢查實際髮束畫面。

StreetColorProfile24 已完成並發布：保留使用者確認的光影、高光，修正 legacy hair 無條件描邊政策，正式畫面採FXAA與150%內部渲染。三張來源卡九視角、完整街區整合、二十組原作對話前後文、二十組身體反應、十五組關係控制及最終凍結高模十五次往返均通過。主機改為獨立二進位快照，避免其他 editor 建置覆蓋已驗證版本。七十項 StreetSocial、十一項 shader 測試及正式啟動器完整性檢查通過；完整失敗／修正／驗收證據見 `Docs/STREET_SOCIAL_COLOR_PROFILE_VALIDATION.md`。

告白完成仍維持朋友的原因與自動保存修正，記於 [STREET_SOCIAL_RELATIONSHIP_AUTO_COMMIT.md](STREET_SOCIAL_RELATIONSHIP_AUTO_COMMIT.md)。五種性格接受／拒絕、取消及重讀測試已通過；本次完整回歸首次另遇共享 Unreal Build.bat 暫存鎖等待，失敗證據保留，使用測試專屬 TMP/TEMP 重跑。

StreetRelationship25 已完成完整驗收並發布：告白完整對話結束即自動套用與保存，移除額外確認按鈕並保留最後原作對白；五位女角接受／拒絕、取消／重讀／重複提交防護、十五次高模往返、所有共同活動、二十組身體反應、十五組關係控制及三張卡九視角均通過。完整紀錄見 STREET_SOCIAL_RELATIONSHIP_AUTO_COMMIT.md。
