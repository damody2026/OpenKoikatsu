# 全城 Unreal 整合與執行期三語切換

## 交付範圍

以 `scene-integration-v1/inventory.json` 為當前採用模型清單；歷史版本、拒絕的生成結果與已組裝進樓層的獨立元件不重複擺入世界。包含商店街 288 層、學校 89 層、百貨 10 層、地形 16 塊、水面 8 塊、校園室外 8 區，共 419 個模型 Actor；另外保留校園招牌、燈光及其他場景輔助 Actor。

專案：`C:/portable/OpenKoikatsu/StreetSocialCity/StreetSocialCity.uproject`
地圖：`/Game/UniversityCity/Maps/UniversityCity`

## 執行期介面

Runtime 插件：`StreetSocialCity/Plugins/CityLocalization`。不依賴 Python、Editor Scripting 或重新匯入模型，遊戲版也可使用。

Blueprint 搜尋 **Set City Language By Code**，World Context 接目前世界物件，Language Code 為 `zh-Hant`、`zh-Hans` 或 `en`，回傳 bool。或用 **Set City Language** 選擇 `ECityLanguage`。`bSavePreference=true` 儲存偏好；false 只影響本次遊戲，仍會跨地圖保留。

C++ 呼叫：

```cpp
#include "CityLanguage.h"
UCityLanguageLibrary::SetCityLanguageByCode(this, TEXT("en"), true);
UCityLanguageLibrary::SetCityLanguage(this, ECityLanguage::TraditionalChinese, true);
```

自己的 C++ 模組在 Build.cs 加入 `CityLocalization` 依賴。需要更新 UI 時，取得 Game Instance 的 `CityLanguageSubsystem`，綁定 `OnLanguageChanged`。`GetCityLanguage` / `GetCityLanguageCode` 讀取目前設定。

控制台：

```
City.Language zh-Hant
City.Language zh-Hans
City.Language en
```

亦支援 0／1／2、zh-TW／zh-CN。無效輸入回傳 false 並保留原語言，不使用未驗證的任意參數值。開機測試可指定 `-CityLanguage=en`（不覆寫已儲存偏好）。

同一次呼叫會設定 Unreal 的 culture 與當前世界 `MPC_CityLanguage.LanguageIndex`（0 繁、1 簡、2 英）；所有已綁定材質共用此參數，含整合式 Logo、校園外牆招牌及百貨專櫃。沒有重新載入地圖、複製建物或重綁所有材質。GameInstance 子系統保留本次選擇，World 子系統於 BeginPlay 自動套用到新世界。每個遊戲實例／用戶端各自選擇，並非多人連線全房同步設定。

偏好存在目前使用者的 GameUserSettings.ini `[CityLocalization] Language=...`。Packaging 設定包含三種文化與 Localization 內容目錄，避免 Cook 漏掉文字資產。

## 驗收

- `Tools/street_city_scene_inventory.py` 從當前採用來源產生獨立清單。
- `Tools/audit_street_city_scene_unreal.py` 重開地圖，直接比對每個實際 Actor 的來源 SHA、位置、旋轉、縮放、碰撞及材質綁定；不能只採用舊匯入收據。
- `Tools/audit_street_city_campus_signs_unreal.py` 檢查 387 層所有三語材質及 34 個校園外牆招牌。
- `Tools/run_street_city_runtime_audit.py` 在隔離 UserDir 中測試編譯後 API：三種語言、enum、控制台、無效輸入、實際 PIE 參數與 culture、跨地圖以及新遊戲實例恢復偏好。測試不改使用者真正的語言偏好。

Runtime 插件以 Unreal BuildPlugin 編譯 Editor、Win64 Development、Shipping；編譯與測試證據見 `localization-v1/runtime-plugin-build.log` 及 `scene-integration-v1/`。只有實際通過的收據才算完成；不以進度或引擎退出碼替代功能驗收。

[Unreal 材質集合執行期 API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UKismetMaterialLibrary/SetScalarParameterValue)


## 一鍵重新整合

`python Tools/run_street_city_scene_integration.py`：重新建立當前來源清單、確認 runtime 插件是否需重編、比對原生地圖、只準備並更新有差異的模型、按變更類型重套對應語言組、重開全城材質、跑真實 PIE 測試，最後產生 current-source acceptance.json。無變更時不重匯入。

本輪 223 個舊商店樓層已更新；最新地圖 419 個模型比對通過，387 層的 894 個三語材質、2,682 張原生文字貼圖與 34 個校園外牆招牌通過重開驗收。Runtime PIE 通過 30 項實際 API／參數／culture／跨地圖／偏好恢復檢查；最終 857 項整合檢查通過。最終狀態與 SHA 以 output/street-social-city-production/scene-integration-v1/acceptance.json 為準。


## 2026-10-19 城市補齊後的當前狀態

最新採用模型為495個Actor／427層／122棟，原生全來源比對、全部427層文字及34個校園招牌重開通過。新增建物／戶外模型驗收與逐層圖片見 output/street-social-city-production/world-completion-v1/index.html。全城整合1,009項、實際PIE語言切換30項通過；目前地圖SHA與來源以scene-integration-v1/acceptance.json為準。上方419／387為前次歷史驗收數量。


## 2026-10-19 Grok主題牆材質層

90張真實Grok新圖、83種已套用、7種濕區圖庫備選。全城122棟／427層共728個牆槽，281個原生材質。牆面重開10,975項、全城整合1,023項、31,480項新增建築碰撞、427層三語與34個校園招牌重開、30項PIE均通過。新增材質層不改StaticMesh、座標或碰撞；一般整合會於換模型／重套語言後恢復此層。可攜新版模型與三語版本見world-wall-grok-v1；最新地圖SHA256為bd1f6028fad593069f4b01624cdbb794d1fca670e3b42dc7afa0a23364e14abb。
