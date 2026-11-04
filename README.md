# OpenKoikatsu → 已搬到 OK Studio

本專案的原始碼已經搬到 **<https://github.com/damody2026/OK_Studio>**，請到那裡下載：

```powershell
git clone https://github.com/damody2026/OK_Studio.git
```

OK Studio 是用 Unreal Engine 5.8、C++ 與 Rust 做的非官方 Koikatsu Sunshine 人物卡 3D 工具與遊戲專案：讀取人物卡組出 3D 角色、OK Studio 場景編輯、街區交流 demo、一對一互動等。

## 倉庫不含版權物

OK Studio 只有程式碼與專案自己做的內容。原作（Koikatsu Sunshine）的模型、動畫、音效、貼圖，第三方 mod，以及角色卡都不在倉庫內。程式需要的原作資料（共用資源）用下面任一種方式取得：

| 方式 | 做法 |
|---|---|
| 有自己的 Koikatsu Sunshine | 指定遊戲安裝位置，由腳本轉換產生：`Tools\lua\lua.exe Tools\original_data.lua restore --game D:/KoikatuSunshine` |
| 加入 QQ 群 **1127259437** | 下載開發資源，`Tools\lua\lua.exe Tools\original_data.lua restore --archive <zip 路徑>` |

詳細步驟（可以直接交給 AI 照著執行）：[取得共用資源](https://github.com/damody2026/OK_Studio/blob/master/Docs/SHARED_RESOURCES_SETUP.md)。

## 一起開發

歡迎加入 QQ 群 **1127259437**：討論、下載開發資源、一起開發遊戲。

請只使用自己合法取得的遊戲與 mod。
