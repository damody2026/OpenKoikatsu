# 當前配置與實際製作驗收

來源 SHA-256：`fc9ad70c815280dc095dbb26f266c00634256b1bba30789ab3d69c23e67d5d4e`。

全城 74 棟、283 層、1,136 個室內＋20 個戶外＝1,156 個主要互動單元。
世界 [1100, 1100]m，長寬比例 0.5；建築與人尺度設施維持真實公尺。48 棟學生街店屋、[150, 100]m 百貨及鄰近 3 棟辦公樓。

## 已通過的當前功能

- 92 項單元回歸通過。
- 10,081 項街區檢查通過，包含外框、道路、球場、商業街覆蓋及百貨街廓。
- 48棟商店的 48 個沿街入口面向 1 條共用步行街；兩排店面正門相向。門位與1F圖、門前步道、立面開口工單及組裝座標一致，並以實際控制GLB檢查入口內側淨空。
- 全部 74 棟控制 GLB 通過 704,858 項實際頂點與座標檢查；產物 SHA 逐棟比對一致。
- 逐層外框／牆厚／完整分隔／門位／電梯／樓梯／公共廁所／至少 2m 通路及用途設備配置檢查通過。
- [逐層空間盤點](space-enclosure-inventory.md)：10,703筆用途與開放／封閉理由。854處開放活動分區移除實體隔牆與內門；2,415段開口通過實際控制GLB結構淨空檢查。客房、衛浴、更衣、廚房及需要隔離的用途保留隔間。
- 283層公共動線連通檢查通過；全部854處開放活動分區能經至少2m寬的相向開口與連續通道接到主要公共動線，逐區路徑已記錄。
- 宿舍 1,708 張書桌補齊同數座椅；逐房檢查唯一配對、設備碰撞與通路。
- HTML 與 CSV 全部 1,156 筆尺寸逐一核對有效規畫，誤差不超過 0.001m。
- 357 份逐層總覽／單層 SVG、566 個樓層圖框均有東西寬／南北長含牆尺寸，以及依實際圖面比例繪製的 1m、5m、10m 獨立比例尺。
- 一般門1.2m、建築／百貨店面正門2.4m、走廊淨寬2m。15,570個門符號及14,882段90度開啟弧線通過實際SVG檢查；電梯為滑門。42處開放公共門廳另列用途與尺寸。

## 真實 Tripo 模型進度

1 種實際下載模型通過靜態零件驗收；目前為：COMMON-CITY-study_desk。
通過的資產已登錄正式資產庫；嚴格組裝器仍核對原檔來源、模型 SHA、真實尺寸、品質紀錄及參考圖 SHA。局部預覽不代表完整組裝。
- COMMON-CITY-study_desk：approved_geometry_with_locally_authored_PBR。靜態零件通過尺寸、網格、UV/PBR及預留使用空間檢查；引擎碰撞及效能另驗。
  - 材質來源：Locally authored PBR image textures; NOT downloaded Tripo textures.
- COMMON-CITY-seat：generated_download_blocked。Tripo model generation completed; export success notification observed, but no STL or GLB file received. Account UI shows Pro; cause remains unconfirmed. Do not treat as downloaded or approved.

完整房間正式組裝就緒 0 間；展開配方仍需 8,025 種獨立模型。數量不代表已生成。

## 尚未驗收的範圍

全城 Tripo 模型、扶手、動態門扇／電梯、窗表、地形高度與入口高差、遊戲碰撞、導航及效能仍未完成。靜態零件通過不等於房間或遊戲完成。

全店研究與圖稿：[store-review.html](store-review.html)；店面本輪驗收：[store-redesign-acceptance.md](store-redesign-acceptance.md)。

總圖：[world-terrain.svg](../world-terrain.svg)；逐層配置：[review.html](review.html)；真實製作：[generation-progress.html](generation-progress.html)；待件：[production-queue.md](production-queue.md)；錯誤：[ERRORS_AND_PREVENTION.md](../ERRORS_AND_PREVENTION.md)。

重跑當前整合驗收：`python Tools/verify_street_city_current.py`。報告只接受現行來源 SHA 與實際檔案，不會因控制簡模存在而宣告正式模型完成。
