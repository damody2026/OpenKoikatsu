# 樹形多樣化 v2

依使用者「樹可以再多種豐富一點」的要求，沿用 v1 的 LOD、Impostor、remesh 與原生保存驗收流程，從4款擴充為12款模型。原有1,475個可見種植實例保持原位，不以增加密度代替樹形差異。

## 新增內容

| 款式 | 造型重點 | 近景三角面 |
|---|---|---:|
| 榕樹式寬冠 | 低分枝、較寬支架與深綠葉冠 | 4,628 |
| 分層欖仁 | 高主幹、四周輪生且逐層縮小的平展枝層 | 3,552 |
| 直立常綠樹 | 窄幅直立輪廓，與寬冠樹形成高寬差 | 3,324 |
| 落羽松式尖冠 | 尖錐輪廓、立體不透明葉簇 | 5,288 |
| 垂柳式樹形 | 彎垂側枝、朝下的葉簇，避免仍長成上揚圓冠 | 3,522 |
| 羽狀棕櫚 | 彎幹、放射狀弧形葉軸與獨立成對小葉 | 4,594 |
| 黃花景觀樹 | 金黃立體花冠 | 3,788 |
| 白花景觀樹 | 米白立體花冠 | 3,788 |

以上是為既有場景尺寸製作的樹形風格，不宣稱成熟樹的植物學精確重建。加上 v1 的三款樟樹與粉花樹，合計12款。花樹仍是少數，粉／黃／白合計288棵；綠樹合計1,187棵。

新款每種均有四級 LOD，遠景仍是8三角面的16視角替身，貼圖上限2K。新增32組網格全部實際執行指定 Rust remesh；原有16組沿用已通過的同版本來源與輸出，正式匯入／重開再核對全部48組 gate 證據。

## 區域搭配

- 校園：樟樹、寬冠、分層及直立樹交錯，維持主要行道樹的序列感。
- 山林：常綠寬冠、尖冠、直立輪廓混合。
- 河岸：提高垂柳、棕櫚與分層樹比例。
- 渡假區：棕櫚為主，搭配寬冠、垂柳與花樹。
- 商店街：分層、直立、寬冠與少量棕櫚搭配。

配置先依現有群組實例的平均世界座標分區，再以 Actor／元件穩定雜湊選樹形；相同行道群組保留相同樹種，避免每次重跑亂變。未改動原有旋轉、縮放、葉色變化、碰撞、陰影及裁切設定。新增樹形保持在原有綠樹8m、花樹6m及既有水平範圍內；窄冠可縮小樹冠，不擴張建築／步道占地。

## 參考

- [台科大校園植物解說專區](https://www.general.ntust.edu.tw/p/412-1054-7458.php?Lang=zh-tw)：校園植栽氣氛與多樣性參考，未將遊戲配置冒稱為台科大實測植栽清單。
- [成功大學總務處：小葉欖仁](https://serv-oga.ncku.edu.tw/p/412-1057-10813.php?Lang=zh-tw)：輪生枝條與枝端葉叢的造型參考。
- [農業部農業知識入口網：小葉欖仁](https://kmweb.moa.gov.tw/theme_data.php?id=380&theme=plant_illustration)。
- [桃園區農業改良場：落羽松](https://www.tydares.gov.tw/ws.php?id=8629)：尖冠與細密葉簇風格參考。

## 重現及驗收

工具共用 `Tools/street_city_tree_species.lua` 定義。設定 `STREET_CITY_TREE_SERIES=v2` 後，產物及收據寫至 `output/street-social-city-production/tree-upgrade-v2/`，新原生資產位於 `/Game/UniversityCity/TreeUpgradeV2/`。v1產物及原有模型保留。

```powershell
$env:STREET_CITY_TREE_SERIES='v2'
.\Tools\lua\lua.exe Tools/street_city_tree_upgrade.lua tree_probe
.\Tools\lua\lua.exe Tools/build_street_city_tree_upgrade.lua build 5 12
.\Tools\lua\lua.exe Tools/build_street_city_tree_upgrade.lua remesh 13 36
.\Tools\lua\lua.exe Tools/bake_street_city_tree_impostors.lua 5 12
.\Tools\lua\lua.exe Tools/street_city_tree_upgrade.lua tree_import
.\Tools\lua\lua.exe Tools/street_city_tree_upgrade.lua tree_install
.\Tools\lua\lua.exe Tools/street_city_tree_upgrade.lua tree_compare
.\Tools\lua\lua.exe Tools/street_city_tree_upgrade.lua tree_preview
.\Tools\lua\lua.exe Tools/street_city_tree_upgrade.lua tree_asset_audit
.\Tools\lua\lua.exe Tools/publish_street_city_tree_upgrade.lua
```

安裝前記錄 v1 正式地圖與元件基準，原地圖副本保存在新產物目錄；已有 install.json 後拒絕覆寫基準。保存前驗證所有12款都有實際可見實例、總量不變及2,952個非樹元件完整保留；保存後獨立重開驗收。原生對照包含五區、每區近中遠景，共15鏡位，在同一程序逐鏡位交錯 v1／v2，最後恢復 v2，不於比較程序保存地圖。

實際分布見 `tree_audit.json`，模型與來源見 `tree_import.json`，量測見 `performance.json`，照片圖庫見 `index.html`。效能結果逐鏡位列正負差異，不用低面數代替原生量測，也不將 Editor 測試當成打包遊戲 FPS。

本輪完成驗收：12款全部出現、合計1,475棵；3,685項原生檢查通過，含逐元件模型與材質綁定。最終圖庫包含五區15組前後對照、共30張原生照片；河岸局部重拍已合併，其餘已通過鏡位未重測。Blueprint 元件綁定使用完整材質陣列一次指定，重建後重新取得元件並核對，不保留失效引用。
