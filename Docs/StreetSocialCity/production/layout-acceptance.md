# 大學城當前樓層配置驗收

結果：**passed**。122 棟、427 層；當前資料 SHA-256：`fdb35f1e1c44491ef9131fd24ff9cf30f4f1c37036d01037d068dacc95b4cbb0`。

本次驗收：完整樓層分隔、2m 走廊淨寬、每一房門銜接、走廊網路連通、用途所屬與設備尺寸。這不是最終模型、門扇動畫或遊戲碰撞的驗收。

1,300 筆為主要互動單元（1280 室內＋20 室外）；物理分隔房間與後勤房間另列，不用增加主要單元數掩蓋尺寸問題。

| 檢查 | 執行次數 |
|---|---:|
| research_traceability | 28 |
| envelope | 290 |
| corridor_width | 2608 |
| corridor_connectivity | 145 |
| doors | 4450 |
| ownership | 5578 |
| equipment_bounds | 15446 |
| equipment_clearance | 2179 |
| room_bounds | 3787 |
| courtyard | 183 |
| room_corridor_separation | 77601 |
| room_overlap | 64616 |
| floor_facilities | 435 |
| wet_service_access | 9 |
| small_shop_complete_plate_and_clearance | 30092 |

未分配面積為零的樓層：427 / 427。
