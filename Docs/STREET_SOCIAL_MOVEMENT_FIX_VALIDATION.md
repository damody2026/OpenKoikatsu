# 街區移動修正驗證（2026-10-08）

## 實作

- 鋪面以共用場景 Prop 的幾何語意識別：薄、寬、Cube 裝飾鋪面。生成地圖頂面改為 Z=0.1 cm；runtime 同樣校正既有 Cook 地圖的鋪面高度與碰撞，不需重製人物 APNG。實際地面、建築、家具與樹木仍有原本的碰撞。
- `Rotations` 保存活動的目標朝向，`RenderedRotations` 保存實際提交朝向。唯一的女角 motion 提交迴圈以最短角距、45 度/秒逐幀推進，不限制動畫更新頻率。走路、跟隨、電話、對話、活動及取消恢復共用此路徑。
- 雙人及設施活動先等待實際朝向到達，再播放動畫及顯示道具。觸控偏移和打西瓜道具使用實際朝向。
- 左右滑鼠係數為 0.5（原 0.25）；上下保留 0.2。Ctrl／中鍵與焦點鎖定流程保留。

## 開發過程

- Movement02：Development 原生測試成功，9 次 capsule sweep（鋪面四邊雙向 8 次 + 建築牆阻擋 1 次）；5 名 APNG 女角 10,635 次轉向更新、最高 45.0000038147 度/秒。
- 首次 PowerShell 判斷因 float 0.2 JSON 表示誤判，已修正容差並重跑成功。
- Movement02Full 在補齊設施轉向等待後由本代理停止自己的隔離測試；不計為完整通過。
- Movement03 為最終候選版本，以下正式證據於測試完成後填入。
- 50 項 Python 測試通過，含浮點容差、角速度超標、缺少角色/牆體驗證及錯誤執行檔證據拒絕。

完整人工驗收尚未進行。本輪不作效能或 120 FPS 驗收。

## 最終候選 Movement03：移動專項已通過

- 證據：`Saved/StreetSocial/Runtime-StreetMovement03/social.validation.json`，passed=true，errors=[]。
- 校正共用鋪面 1 處；四邊雙向穿越 8 次與保留牆體阻擋 1 次，共 9 次 sweep 通過。
- 5 名實際 APNG/GPU 女角提交方向讀回通過，10040 次角速度更新，最大 45.000003814697266 度/秒。
- 左右係數 0.5，上下係數 0.20000000298023224（C++ float 的 JSON 表示；原 0.2 不變）。
- 抽看最終候選 `contacts.navigation.phone.png` 與 `street.seated.1.png`：鋪面齊平；女角轉正後坐入原有座位。這是局部畫面檢查，非完整人工驗收。

## 同一 Development 執行檔的主要回歸：已通過

- `Saved/StreetSocial/Runtime-StreetMovement03Full/social.validation.json`：passed=true，errors=[]。
- 15 段正常話題、5 次問候、5 次告別；10 次提交方向檢查（男主 5 方向 + 5 名女角）。
- 跟隨及取消恢復、3 種設施、5 名女角的合照／坐下／飲料／吃飯／手機，與 10 組打西瓜案例通過。
- 此次五名女角最大轉向速率仍為 45.000003814697266 度/秒。
- 96 張 PNG 成功解碼；抽看導航與坐下交談畫面，未進行完整人工驗收。
- 同一版本 SHA256：`79ae6bbea4e00e56da6429c25a79e4a34f69d5d6cb0555014ca0ed519ef15a34`。

## 發布：完成

- 入口仍為 `StreetSocial.cmd`，透過原本 receipt 指向 `Saved/StreetSocial/Build/StreetMovement03.stage/Windows/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe`。
- 主要回歸、移動專項、四種背景動作各 5 名女角與 20 組邀請測試均使用同一執行檔；獨立驗證通過後才原子更新 receipt，先前 receipt 已自動備份。
- 848 個來源語音雜湊、1,230 段對話與 270 組觸控來源配置驗證通過；原有背景動作與邀請功能旗標均保留。
- `Tools/run_street_social_development.ps1 -ValidateOnly` 通過；已再次驗證發布執行檔 SHA256、功能旗標與實際測試來源一致。
- 50 項 Python 檢查通過。完整人工驗收與效能驗收仍未進行，不將本輪幀時間當成效能成績。
- 啟動中的舊遊戲不會被修改或關閉；重新開啟原本 CMD 使用新版。
