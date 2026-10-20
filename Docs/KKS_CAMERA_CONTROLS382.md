# Stage382：KKS 滑鼠視野操作

2026-10-03。正式 BuildR4 成功，主專案 DLL 已更新；重開 Unreal 使用。

在角色的 3D 視圖按住滑鼠拖曳，動畫播放與靜態預覽均可操作。

| 按鍵 | 操作 |
| --- | --- |
| 左鍵 | 圍繞目前焦點旋轉，方向依 KKS Euler 轉換 |
| 中鍵 | 沿相機的左右、上下方向平移焦點 |
| 右鍵 | 上下拖曳升降焦點；向右拖曳拉遠，向左拖曳拉近 |
| 左、右鍵同按 | 沿相機左右、前後方向平移焦點 |
| 滾輪 | 前滾拉近、後滾拉遠，使用固定距離增量 |

按鍵優先順序為左右同按、左、右、中。滾輪優先於同幀拖曳。沒有另外加入時間倍率、旋轉俯仰硬限或最小 45／最大 2400 公分距離限制，距離依原版夾至零。零距離與穿越上下極點仍保留有限、連續的相機方向。

拖曳必須從 3D 視圖開始。在 UI 按鈕或滑桿開始的拖曳不會在移出 UI 後變成相機操作；失焦、游標離開視圖或更換相機擁有者後取消操作，放開再按才能重新開始。保留 NoCapture 的 UMG 操作設定。控制目前 Gallery 顯示且匹配 view target 的角色相機；搜尋後備角色也必須匹配 view target，避免命中殘留角色或配對角色。

## 反編譯與資源依據

- `C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/BaseCameraControl_Ver2.cs`：InputMouseProc 314–397；InputMouseWheelZoomProc 301–312；LateUpdate 568–610；CameraUpdate 626 起。旋轉速度 5、平移速度 0.05 公尺、滾輪速度 5，SetCtrlSpeed 822 起為 stub、rateAddSpeed 為 1。
- 同目錄 `Studio/CameraControl.cs` 552–600 具有相同三鍵組合分支與優先順序。
- 遊戲與 CharaStudio `globalgamemanagers` 的實際 InputManager：Mouse X、Mouse Y、Mouse ScrollWheel sensitivity 均為 0.1、invert=false。讀取證據與 SHA256 保存於 `Saved/Tests/CameraControls382/InputManagerR1.json`，未修改遊戲資源。
- 既有 Unity→UE 座標映射為 `(-x,z,y)*100`；螢幕 Y 向下，Unity Mouse Y 向上。滑鼠像素位移乘 0.1，移動速度轉為 5 公分，滾輪每單位轉為 50 公分。

本次實作滑鼠相機分支。鍵盤、觸控、個別場景的序列化相機限制、碰撞及非 identity transBase 系統不在本次驗證範圍。

## 驗證

- `Saved/Tests/CameraControls382/BuildR4.exit.txt`：0，正式編譯 Succeeded。
- `Saved/Logs/CameraControls382ControlsR3.log`／receipt：actual 0、Automation Success、mouse valid=1。七種非空按鍵組合，以獨立 Unity Euler 與座標轉換公式比對相機位置；檢查焦點、距離、滾輪、零距離、穿越極點、UI 手勢、失焦、離開視圖及目前 Gallery 相機選取。
- `Saved/Logs/CameraControls382NativeR1.log`／receipt：actual 0、Automation Success、native valid=1。March_7th 實際卡片載入後播放原版 `studio/anime/00.unity3d` 的 `walk_run / Base Layer.f_aruki_00_00_01`。七種拖曳加滾輪後，667 骨骼變換完全不變、動畫及 face clock 不變；後續 Tick 動畫正常前進至 0.3 秒，131 骨骼變動。
- UI 手勢與相機選取採確定性測試；沒有操作使用者桌面游標或宣稱硬體滑鼠端到端驗證。ControlsR1 測試 fixture crash 與 ControlsR2 桌面 hover 導致的失敗均保留，原因與修正記錄於 ERROR_LOG E260。

`Tools/run_camera_controls382.ps1` 可用新 Tag 重跑兩個 Automation 測試。`Tools/seal_camera_controls382.py` 封存 R1 source、正式 binary、說明與 logs，並核對 Stage381 所有封存檔案原樣保留。
