# 人物 PNG 格式

Koikatsu / Koikatsu Sunshine 人物卡是 **合法 PNG + IEND 之後的二進位 payload**。

1. PNG 預覽圖，直到 IEND chunk（含 CRC）。
2. `int32 LE` `product_no`（100）。
3. .NET 7-bit 長度字串：標記 `【KoiKatuChara】` / `【KoiKatuCharaSun】` / `【KoiKatuCharaSP】`。
4. 版本字串。
5. `int32` 臉部 PNG 長度 + bytes。
6. `int32` block header 長度 + MessagePack（`lstInfo[]`：`name` / `pos` / `size`）。
7. `int64` payload 總長度。
8. payload 切片：`Custom`、`Coordinate`、`Parameter`、`Status`、可選 `About`、可選 `KKEx`。

`KKEx` 裡 `com.bepis.sideloader.universalautoresolver` 的 `info[]` 是 MessagePack `ResolveInfo`（`ModID`、`Slot`、`LocalSlot`、`Property`、`CategoryNo`）。

沒改過的檔必須能位元組一致寫回。只改暱稱時 `Custom` block 必須維持原樣。
