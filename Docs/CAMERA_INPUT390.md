# Stage390：修正 GUI NoCapture 滑鼠拖曳失效

2026-10-03。正式 BuildR2 actual0／Succeeded，UnrealEditor-OpenKoikatsu.dll 已更新。

## 根因

GUI 的 ApplyClickInputMode 與 PlayerController 保持 EMouseCaptureMode::NoCapture，以免 viewport 搶走 UMG 按鈕／滑桿點擊。UE5.8 FSceneViewport::OnMouseButtonDown 的 bProcessInputPrimary：game/PIE viewport 必須已有 capture，或是 CapturePermanently_IncludingInitialMouseDown，才送 primary InputKey；NoCapture 且無 capture 時不會轉送。PlayerTick 卻用 IsInputKeyDown 判斷三個滑鼠鍵，因此 mask 始終0。OnMouseWheel 的 axis 路徑仍轉送，符合「除了滾輪都不行」。

此邊界存在於 D:/UE5.8/Engine/Source/Runtime/Engine/Private/Slate/SceneViewport.cpp；SlateApplication 的 ProcessMouseButtonDownEvent／UpEvent 更新 GetPressedMouseButtons，不依賴 viewport capture 或 PlayerInput。對照 C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/BaseCameraControl_Ver2.cs：CheckDrag 讀 Input.GetMouseButton，先處理左右同按，另有左／右／中；Mouse ScrollWheel 非零先處理，不改既有 OrbitMouse 的原生方向、比例、branch priority。

## 通用修正

GetCameraMouseButtons 在 Slate 可用時讀真實 Slate 按鍵狀態，無 Slate 才 fallback PlayerInput。PlayerTick 呼叫同一 UpdateCameraMouse 入口：原 viewport mouse position／focus／UI hit-test，active camera owner、first-press gesture ownership、失焦／release取消與 wheel-frame優先均保留。只改按鍵來源及可驗證入口，不改 orbit 數學、不強制 capture、不註冊吞事件的 processor、不為卡／動作設特判，原按鈕／滑桿保持可點。

1. 左鍵旋轉。
2. 中鍵左右／上下平移。
3. 右鍵垂直升降，水平拉近／拉遠。
4. 左右同按左右／前後平移，優先於单左／右。
5. 滾輪縮放，原事件路徑不變。

## 驗證與覆蓋修正

舊 KksMouse 只直接呼叫 OrbitMouse、輸入抽象 mask 到 FOKCameraDrag，雖驗證七mask數學及owner，未覆蓋 NoCapture→PlayerInput 邊界；所以先前綠燈不能代表 GUI 實際滑鼠拖曳正常。本輪補 FSlateApplication 的實際 down/up event，FSceneViewport 設 PIE／NoCapture 並執行 OnMouseButtonDown／OnMouseMove，接入正式 controller 的按鍵讀取與 gesture 更新。

- Camera390ControlsR2 actual0／AutomationSuccess：四mask(左／中／右／左右同按)的 viewport primary press數確實0、PlayerInput左鍵維持false；正式 controller仍讀到 Slate mask，移動後 camera transform 改變、release清除mask，UI開始的gesture拒絕接管。log：KKS camera NoCapture routing valid=1 slate_masks=4 playerinput_presses=0 ui_preserved=1。
- 同一測試原七mask與獨立Unity數學oracle、wheel／pole、動畫／表情clock不變、focus／UIgesture／owner及prepare generation回歸通過。Camera390ControlsR1也通過，保留兩份日志。
- Camera390NativeR1 actual0／AutomationSuccess：真卡女667／男681bones、walk／skin／world、兩native active Idle／shared clock，四筆完整oldsolver IK exact；確保輸入修正未改動畫／骨架路徑。
- BuildR1／R2 actual0／Succeeded。R1測試fixture使用舊FSceneViewport raw-client建構API出現C4996；R2改shared client＋新SetViewportClient API，未改產品行為，最終該warning消失。

這是合成 Slate events＋真 SceneViewport 的自動化邊界驗證，並未宣稱由實體滑鼠全程操作截圖中的 GUI。UI hit-test的抽象輸入仍使用既有判定，數學oracle與事件邊界分別測試；不把模型呼叫測試冒稱實體GUI驗證。

## 證據

Docs/ERROR_LOG.md E268、HANDOFF3 §81；Saved/Tests/CameraInput390 保存 source diff／Build logs／receipts。fresh R1 audit／snapshot保存正式source／DLL／完整測試log與receipt，逐檔核對Stage389 R2的625項證據不變。封存工具stdout直接返回，不導向正在封存的目錄，避免E267曾遇到的自身log hash變動。
