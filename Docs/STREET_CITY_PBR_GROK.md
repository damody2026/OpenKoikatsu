# 全城缺漏 PBR 補製

## 範圍及來源

使用者授權延續原生 StreetSocialCity 整合：盤點 `/Game/UniversityCity/Maps/UniversityCity` 所有 MeshComponent 實際使用的材質，包含室外、建築室內、商街、學校、百貨及三語招牌。2026-10-20 初次盤點共 2,096 個材質介面、2,045 個母材質。盤點保留於 `output/street-social-city-production/pbr-grok-v1/inventory.json`，正式結果以 audit.json、render.json 為準。

不能以「材質有接線」或「輸入上游存在任一貼圖」認定 PBR 完整：glTF 材質函式的所有輸入會共同出現在圖遍歷內，BaseColor 並不等於 Normal。此次以通道貼圖語義、既有程序法線與物理用途區分完整、待補、刻意平滑三種情況。

## Grok 與數值處理的分工

`Tools/resort-props/src/bin/pbr-maps.rs` 以 Grok Build 1.0.24、grok-4.7 的 image_gen 生成 16 類灰階微觀高度圖：粉刷、混凝土、石、木、織物、陶瓷、柏油、橡膠、金屬、塑膠、樹皮、葉片、皮革、紙／招牌塗膜、泥土、草地。每項保留完整 prompt、session、image_gen tool call、完成事件及原圖 SHA256；只有收到實際完成圖的工具證據才處理，不以 CLI 說完成為準。

Grok 生成的圖是**高度來源**。Rust 執行平鋪邊緣交叉淡化及環繞差分，產生 DirectX 切線空間法線；粗糙度以材質類別校正均值，加上微細高度變化；金屬通道依物理分類設為 0／1，不從底色明暗猜金屬。ORM：R=1（未烘焙遮蔽）、G=Roughness、B=Metallic。這些衍生圖不冒稱為 Grok 直接畫出的法線。

每張成品 1024×1024，低於使用者 2K 上限。Normal：sRGB=false、BC5/TC_NORMALMAP；ORM：sRGB=false、TC_MASKS。貼圖共享，保留 mipmap，不增加頂點、面數、細分或 displacement。

## 原生材質修補

`Tools/street_city_pbr.lua` 使用既有延後 Unreal Python bridge；UE API payload 嵌入 Lua，不新增 .py 或 .ps1。每階段有獨立 run_id，必須讀回同次成功報告。

1. 先備份全部待修改 .uasset 到 pbr-grok-v1/backups/，記錄 SHA256。
2. 依明確材質名稱、既有貼圖名稱與度假元件 surface slot 分類。所有對應及理由列於 plan.json。
3. 欠缺法線圖時加入微細法線，與 (0,0,1) 混合後正規化。紙、葉片、塑膠、陶瓷力度較低，避免招牌文字或光滑物件浮雕化。
4. 欠缺粗糙度圖時，在原本粗糙度輸出加上新圖相對均值的細微變化，再限制 0–1；保留既有粗糙度基準。
5. 原有 Metallic 接線保留。只有完全缺少輸入時接入 ORM B；木石植物預設 0，不把非金屬刷成金屬。
6. 原有 BaseColor、Emissive、Opacity、OpacityMask、混合模式、雙面設定及語言切換圖完整比對保留。車輛六色、植物顏色參數和 LOD 不重建。
7. 依使用者後續指示，玻璃、水面、鏡子與發光螢幕不新增 Normal／Metallic／Roughness 貼圖，所需均一參數以 Constant Float／Vector 設定；保留原有透光、水面及發光等專用邏輯。燈罩及透明球網也保留適合的專用材質。平滑法線與均一物理參數可以是正確設定，不強制產生無意義噪聲圖。

新貼圖位於 `/Game/UniversityCity/PBRGrokV1/Textures`。原母材質就地補接，避免換掉共享實例父層或三語選圖邏輯。`CityPBR_Normal`、`CityPBR_ORM` 為新增參數，metadata `CityPBRGrokVersion=1` 支援重跑跳過已完成項目。

## 執行及驗收

```
Tools/lua/lua.exe Tools/street_city_pbr.lua inventory
Tools/resort-props/target/release/pbr-maps.exe C:/portable/OpenKoikatsu
Tools/lua/lua.exe Tools/street_city_pbr.lua repair
Tools/lua/lua.exe Tools/street_city_pbr.lua render
Tools/resort-props/target/release/pbr-report.exe C:/portable/OpenKoikatsu --visual-reviewed
```

補接及重編譯在 Entry 空地圖執行，避免每個材質改圖都重新註冊整座城市的元件。render 使用全新 Editor 重開已保存地圖及材質，將原生 HISM 材質的 instanced usage flag 保存，避免只靠 Editor 自動開旗標而在後續 cook 遺失排列。接著逐母材質檢查新增通道與原有圖的保留，再使用既有碰撞驗證相機位置拍攝正式世界。拍攝不另改燈光，不保存臨時相機。另檢查 shader 編譯日誌、生成證據、貼圖尺寸、通道、色彩空間及保存檔雜湊。圖片仍需人工檢視；不把純 JSON 檢查當作美術驗收，也不宣稱本次完成遊戲效能基準。

回復時關閉此專案 Editor，按 backups.json 對應把原 .uasset 複製回原位置。備份與失敗紀錄保留，不遞迴刪除任何目錄。這次不修改角色資產、不打包 StreetSocial.cmd。

## 本次驗收結果

- 全城 2,096 個材質介面／2,045 個母材質：實際補接 1,164 個，保留 881 個；保留項目含 43 個平滑／專用材質。
- 16 類 Grok 高度原圖已生成，其中 14 類用於本次缺漏補製；所有輸入、工具完成事件、衍生圖雜湊均保留。
- 初版漏認 `_rm` 合併圖的 114 個材質已核對備份 SHA 後還原，最後版本支援 RM／MR／ORM／RMA 辨識；不以錯誤的初版數字作完成數。
- 全新 Editor 重開圖通過 10,534 項檢查；17 張正式世界實拍已人工檢視，包含六色載具、近遠樹木、花壇、木椅與日光床。
- 目視檢查曾發現 SceneCapture 視點與貼圖串流視點分離；最後實拍已同步 Editor 相機並等待串流，保留首輪影像與修正紀錄。
- 原生地圖幾何檔 SHA256 維持 `58a5bfedd2c2a641d823ccd588847bdc1946dd2c4cc5f6f9ee6bbd92e32af76c`；沒有增加模型面數或改動布局。最終 verification.json 另記錄全部 2,045 個原生母材質套件 SHA256。
