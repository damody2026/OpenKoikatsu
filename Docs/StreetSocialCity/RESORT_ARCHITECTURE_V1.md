# 渡假大學城建築外觀重製

## 使用者範圍

2026-10-22 使用者指定：全城建築外觀造型重做，保留樓層與室內功能；大部分建築要有大招牌、美化設計與合理的非平頂屋頂。沿用 Romance PBR v1 的 Grok 生圖材質，避免明顯貼圖重複。使用者不允許跨聊天傳訊協調，寫圖前等待其他 Editor 釋放。

共 122 棟、427 層；PK 是戶外空間，不計入建築。實際建築資料來自 assembly-manifest 與 campus／remaining 尺寸契約；擺放依照現有地圖 Actor 的真實 transform，不沿用已過期的早期 city-plan 座標。

## 實作分工與資源

- Grok Build job `run-mvj1add4-l4flgk`，thread `b0a71144-e387-4468-a7dd-ec304139b23a`：完整 122 棟設計名冊，各棟屋頂形式、坡度、脊向、色調與細節；122 組參數皆不同。最初查閱大型契約過久的工作 `run-mvj0pv7y-zo9rsu` 已取消，沒有當作成功交付。
- 主代理：契約盤點、Rust 尺寸化外觀幾何工具、Unreal 匯入、材質接入、大招牌文字、保存、重開圖驗收與實景檢查。
- 工具：`Tools/resort-props/src/bin/resort-architecture.rs`、`Tools/street_city_resort_architecture.lua`、`Tools/street_city_resort_architecture_payload.lua`。
- 產物：`output/street-social-city-production/resort-architecture-v1/`。
- 新 UE 資源：`/Game/UniversityCity/ResortArchitectureV1`。

這個階段以外觀模型包覆和新屋頂改變輪廓，保留原有室內樓板、隔間、樓梯、電梯與設備模型。新增外觀不作可進入的新樓層；不宣稱棚架或裝飾空間可供角色行走。新增幾何不改原有行走碰撞，避免改造阻塞既有功能路徑。

## 重現與還原

從專案根目錄執行；每次真正保存前確認沒有其他 session 持有相同地圖。

```text
cargo run --release --manifest-path Tools/resort-props/Cargo.toml --bin resort-architecture -- build
Tools\lua\lua.exe Tools/street_city_resort_architecture.lua inventory
Tools\lua\lua.exe Tools/street_city_resort_architecture.lua shader
Tools\lua\lua.exe Tools/street_city_resort_architecture.lua install
Tools\lua\lua.exe Tools/street_city_resort_architecture.lua render
```

`inventory` 會建立原地圖的 Actor／元件／材質／碰撞／HISM signature 與幾何尺寸基線。`shader` 僅在 Entry 地圖建立外觀資源；`install` 先核對基線 SHA，再備份並保存一次。`render` 必須重開地圖並完成 `audit` 才拍攝實景。

還原以 `restore` 移除本次帶有 `ResortArchitecture-v1` tag 的外觀和招牌 Actor。備份僅供診斷，不能直接覆蓋含有其他 session 新工作之地圖。

## 驗收來源

以產物目錄內最新的 `geometry-manifest.json`、`shader.json`、`install.json`、`audit.json`、`render.json`、各階段 `*-exit.json` 與 `acceptance.json` 為準。已完成：保存後重新開圖驗收 8,369 checks、0 issues；13 張原生拍攝，成果頁精選 8 張取景清楚的照片。各階段原生程序退出碼皆為 0。

## 設計與幾何結果

八類屋頂：gable、shed、butterfly、cross_gable、paired_pavilion、lantern、stepped、hip。屋頂分割 743 個單元，新增外觀共 168,860 個三角面。大體量屋頂保留設備露台，亭式覆頂不是新增可進入樓層；窗眉、木托架、樓層簷帶與轉角構件留在原牆面外，所有地面入口與原有碰撞保留。

Grok 的名冊是設計意圖，實際幾何以 `geometry-manifest.json` 與實景為準；其中細節類別 `scalloped_cornice` 在此版映射成簷帶與窗眉節奏，並未生成波浪形雕飾。

文字使用既有 119 棟的三語材質，另補 S18、S19、M1 的正式建築名牌，沿用 `MPC_CityLanguage.LanguageIndex`。既有街面招牌與室內文字不改。

新屋頂與木材沿用 Romance PBR v1 的世界座標隨機三向投影；色調依 Grok 各棟指定的赤陶、湖綠與象牙選擇。貼圖 2048×2048；Grok 原始圖 1024×1024 經重採樣，Normal／ORM 為推導貼圖而非掃描資料，方法限制見 ROMANCE_PBR_V1.md。

Grok 第二輪記錄成本 US$0.1885572、262,224 total tokens（含快取）；此數字是 bridge 回報值，不包含被取消的前一輪。


最終地圖 SHA-256：`a71d39ee863af27d71d0ef883b18b835e97d95b09667e529edc44a7ecd727345`。`acceptance.json` 同時核對來源、設計名冊、幾何清單、安裝、重開圖驗收與拍攝檔案雜湊。初次旋轉參數錯誤及被剔除的攝影鏡位已如實留存，不當作驗收成果。


## 一樓招牌修正（2026-10-22）

使用者要求招牌應是一樓橫式，或從一樓延伸到更高樓層的直式。本修正取代前一版屋頂招牌配置：120 棟改為實際街面入口立面的一樓橫式簷帶；R1、R2 一樓上緣剩餘高度不足，改為轉角直式，底部 2.9m、頂部 7.7m，使用正向直排文字而非旋轉原橫式 Logo。直式牌距窗跨至少 1.35m。

橫式牌依第一層樓高、同側門高與窗頂決定高度，不跨入二樓；優先使用 street_main／grand 入口，避免放在服務出口立面。舊屋頂牌的背板、框架和支柱一起從模型移除，不只移動文字面。`signage-review.json` 對照修正前 GLB，122 棟的 roof／trim／facade／wood 表面逐項雜湊一致。

直式文字由 `Tools/street_city_vertical_sign_text.lua` 以字型重新排版，支援繁中、簡中與英文，保留既有語言切換。修正前模型、清單和成果頁已封存於 `backups/before-ground-signs-*.zip`；變更前地圖另作版本備份。最終驗收與照片應以更新後 acceptance.json／delivery.html 為準，前文舊地圖 SHA 屬上一版紀錄。
