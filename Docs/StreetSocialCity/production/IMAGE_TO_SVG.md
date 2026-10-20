# 逐店 Grok 示意圖與 SVG

完成 176 間：96 棟街邊店屋、80 間百貨店。共 544 份原生向量 SVG；檢查 36,511 項通過。

流程：使用已完成且與現行設計相符的176張 Grok Build 原圖（原128張復用，南側新增48張） → Grok Build 實際讀取逐店圖片 → 記錄可採用材料、招牌、遮簷及家具細節與應校正事項 → 依完整公尺資料重畫向量平面與立面。原圖逐張保留，不重複生成相同的已完成工作。

圖片不含可靠施工尺寸，因此不直接描摹其中錯誤的樓梯、挑高或房間比例。轉繪保留目前驗證過的完整樓層、門扇弧線、2m淨通路及1／5／10m比例尺。百貨櫃位另附完整同層圖，電梯、樓梯及廁所按同一建築座標連接。

街邊店屋輸出：Sxx.svg（各層＋立面）及 Sxx-Fxx.svg。百貨輸出：M1-xxxx.svg（店內細部＋附屬區＋完整同層）及 M1-xxxx-detail.svg（放大店內平面）。這些檔案位於 store-planning-svg。

每店登錄圖片、讀圖回執、解讀資料與SVG的SHA。原生SVG中沒有用點陣圖冒充向量。色彩與裝修轉繪不代表正式Tripo模型或遊戲走行完成；模型製作仍暫停。

[逐店圖稿與SVG對照](image-to-svg-review.html) · [登錄](image-to-svg-register.json) · [驗收](image-to-svg-acceptance.json) · [錯誤記錄](../ERRORS_AND_PREVENTION.md)

重跑：python Tools/refresh_street_city_store_visuals.py。流程會驗證目前資料、復用相符圖稿與解讀、補做缺項，再重新產生 SVG 並驗收。

## 原始圖稿與校正後配置

image-origin-design.json與image-origin-prompt.txt保留當初生成圖稿的輸入；design-input.json及SVG保存校正後配置。圖片提供用途、材料及裝修參考，校正後的家具分布不冒稱為原始生成輸入。百貨80間店均已檢查陳列分散於前後左右各區，並保留2m通路。
