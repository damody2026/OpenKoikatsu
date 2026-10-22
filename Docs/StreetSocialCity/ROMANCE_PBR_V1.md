# 渡假大學城建築材質 v1

日期：2026-10-22。

本輪保留建築造型、樓層、室內配置與碰撞，重新製作並套用建築表面材質。122 棟、427 個樓層 Actor，共 2,423 個可辨識建築材質槽完成覆寫；招牌、三語文字、玻璃、燈、植栽、家具及特殊圖像材質保留。共用 StaticMesh 與原父材質不變，外觀保存在地圖元件的 material overrides。

## 材質庫

16 個系列、每個系列三張獨立 Grok 生成來源，共 48 套。每套交付 Base Color、DirectX Normal、ORM 三張 2048×2048 PNG，共 144 張。奶油灰泥、珊瑚粉、鼠尾草綠、海水藍、洞石、石灰岩、白洗木、柚木、手作陶釉、磨石子等組成柔和渡假校園配色。這次地圖實際使用 44 套、132 張原生 UE 貼圖。

**來源尺寸限制：Grok 實際回傳 1024×1024，交付檔案經 Lanczos3 重採樣為 2048×2048，並非原生 2K 細節。** 原圖、提示、原生 image_gen 收據、session ID 與 SHA 都保留在各套 generation.json；Normal 與 ORM 是從 albedo 及材質家族參數推導的估計，並非掃描量測。

原始貼圖處理對邊接縫與包裹式法線差分。UE 使用世界座標三向投影，在三角網格各頂點雜湊隨機位移與縮放，平滑混合三次取樣，配合逐棟穩定種子、實例隨機值及非週期低頻色差，減少大牆面規律重複。使用明確梯度選擇 mip，避免雜湊跳變造成接縫或過度模糊。Normal 依投影軸轉為世界空間，ORM 使用同一組座標。金屬度為零，粗糙度依材質家族校正。

## 位置與驗收

- 本機成果：`output/street-social-city-production/romance-pbr-v1/`。`delivery.html` 為原生實拍及前後比較，`index.html` 為 48 套貼圖目錄。
- Unreal：`/Game/UniversityCity/RomancePBR/`；已套用於 `/Game/UniversityCity/Maps/UniversityCity`。
- Rust 生成、接縫、尺寸、法線、ORM、來源與獨立性驗證：48 套通過，0 失敗。
- 保存後獨立重開地圖：56,260 項檢查通過，0 問題。比對 actor/component transforms、mesh 材質、碰撞、實例數與矩陣雜湊、光源、原保護材質、全部計畫覆寫及貼圖設定。
- 44 套使用中 Normal 的 DirectX green channel、線性色彩與 BC5 設定另行核對。
- 十個原生 SceneCapture 鏡位，保留原光照；鏡位不保存到正式地圖。
- 保留同時進行的草坡與植栽更新。跨聊天傳訊未經授權，因此沒有傳送訊息或停止他人的 Editor；以最新版重新盤點、保存前 SHA 檢查及等待讀取者釋放避免覆蓋。

## 重現與還原

```powershell
cargo build --release --manifest-path Tools/resort-props/Cargo.toml --bin romance-pbr
.\Tools\resort-props\target\release\romance-pbr.exe all
.\Tools\lua\lua.exe Tools/street_city_romance_pbr.lua install
.\Tools\lua\lua.exe Tools/street_city_romance_pbr.lua render
```

`all` 會重用提示與雜湊相符的生成結果；仍需本機 Grok Build 登入。`install` 先盤點目前地圖並重新規畫，再備份與保存；只有自己的新資產會建立或更新。避免在其他 Editor 正開著同一地圖時保存。`render` 先重開驗收再截圖。

`restore-manifest.json` 保存各材質槽的原始有效材質，`backups/` 保留各次地圖備份。需要回復本輪外觀時可執行 `Tools/street_city_romance_pbr.lua restore`，保留目前地圖的其他更新；不要用舊整張地圖蓋掉後續其他工作。

Grok 實作工作：job `run-mvix2p73-agr2jt`，thread `4bbb0651-0b13-48f8-8c3d-34910be7d975`。Codex 在審查後接手修正並執行實際生成、安裝及獨立驗收；每套影像的獨立 Grok session 以 generation.json 為準。沒有完整用量彙總。沒有提交或推送，本機 StreetSocialCity 不納入版控。
