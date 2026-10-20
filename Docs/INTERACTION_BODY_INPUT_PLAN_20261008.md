# 一對一場景：身體操作整合計畫

日期：2026-10-08。狀態：原作一般點觸反應已實作且功能驗證通過；完整拖曳／互動層／事件語音及效能完整驗收尚未完成。

## 目標與現況

使用者要求一對一互動像原作一樣能直接操作角色身體，不能只有配對姿勢與水槍。編輯場景身體調參與互動場景維持分離，肚子大小沿用既有入口。

已讀本機原作 CharaStudio_Data/Assembly-CSharp/HandCtrl.cs 的 Proc/LateProc、LayerBlend、HitReaction 呼叫，以及 HitReaction.cs 的 HitPoint/EffectorLink。原作不是單一DynamicBone外力：包括部位碰撞辨識、手勢狀態、依motion允許部位、動畫層過渡、IK反應與音效／語音排程。現有專案已支援多層取樣、layer weight、epoch、Restart trigger及crossfade，但既有Docs亦明確記錄完整HandCtrl scheduler尚未完成。

## 實作順序與驗收

1. 資源契約：從共享原作資源讀取可互動部位、原作motion-enable表、反應曲線、effector links與動畫層映射；APNG只供角色骨架／身形／物理。資料runtime解析，不能新增卡片專用預烘焙檔或猜測layer index。缺少映射需明確回報不可用，不能用骨骼角度假動作替代。
2. 身體命中：使用當幀角色碰撞資料／可見表面與近深度遮擋確認部位，排除地板、塗層與其他角色遮擋；命中契約需回傳actor、部位、穩定source revision與局部接觸點。按下建立anchor；持續操作避免每幀全高模重建或GPU readback。卡片／姿勢／component replacement使anchor安全失效。
3. 操作狀態：Idle、Hover、Pressed、Dragging、Released/Cancelled；按下起點在UI不得偷接入3D，失焦／返回／角色替換／姿勢不允許立即取消。水槍與攝影機手勢需獨立仲裁，不可一個左鍵同時噴水、轉鏡頭與觸碰。UI顯示操作模式與可用部位。
4. 反應與原作次序：部位輸入映射原作共享動畫／反應資料；body/item layer分開排程，滑鼠輸入只影響反應層，不重設base paired clock。HitReaction原曲線與crossfade加入FinalIK effector positionOffset，依原LateProc/IK次序求解，再進入body/private dynamics與當幀GPU publication。不得直接修改永久shape、跳過physics或整角色平移假裝局部互動。
5. 反饋：接觸、持續、放開事件與反應動畫同步，語音／嘴型沿用既有播放與角色語音來源，避免每幀重播音檔；沒有對應資料時不宣稱原作完整等價。
6. 驗收：原作狀態／曲線／layer weight oracle；同輸入兩種FPS、不同播放速度、快速換姿勢、兩角色遮擋、UI起按、失焦、退出、APNG replacement、無效映射、解除後回原動畫；實際APNG畫面與骨架／particles檢查。最後最新binary完整功能及獨立無其他build/test的Present測量，包含水槍共存，平均<=8ms／120FPS、切換<=300ms；乾<=6ms。功能與性能分別報告，未過不得標記完成。

## 實作限制

本項優先專案／Plugin／Rust修改，不需為此修改D:/UE5.8原始碼。原引擎差異不歸本項亦不擅自還原。先前未完成的效能分析與功能測試需恢復並核對既有session/log，不能將中斷當完成。

## 2026-10-09 第一批實作
已確認shared h/list/01.unity3d本來已有ReactionData，不應籠統說所有互動設定都缺少；尚缺HitReaction的effectorHit曲線／link與互動用collider／layer操作整合。先落實可獨立驗收的手勢狀態機與原作HitPoint核心。手勢以target revision綁定、由UI／失焦／資源替換取消、拖曳輸入為位移而非每幀累加速度；未接完整反應前不顯示可用互動按鈕。HitPoint保留原crossFadeTime、timer<1重觸發、force/up曲線與effector link加總，用既有DynamicBone曲線數學，空曲線值必須0而不是DynamicBone參數曲線的預設1。不修改UE引擎。
原作scene缺少typetree，將解析依賴TypeTreeGeneratorAPI放在專案Saved/Tools/BodyInteractionDeps（只供匯出工具，不修改系統Python或引擎），由原作Managed DLL還原序列化欄位，尋找實際effectorHit資料。共享設定可匯出，角色與模擬準備仍runtime。

## 2026-10-09 第二批整合（驗收中）
- 已匯出原作共享 HitReaction 設定及 h_bone_tag_layer／ReactionData／AnimationInfo 對應，保留原資料，不含角色 pose。原作骨架 primitive 與表面蒙皮 collider 是不同用途；一般點觸使用前者。
- Runtime 每個 session 有獨立反應時鐘、crossfade 與 standalone solver。雙人直接接既有 FinalIK solve 前，原作 release-effector 權重依反應進度恢復，不另開第二次 paired solve。既有 APNG portable layout 不改。
- 左鍵手勢仲裁、原作可用部位、當幀骨架 capsule 查詢與 scene mode 已接線。只以已成功提交的 pair identity 設定反應，背景換動作準備期間仍使用當前姿勢的對應。原作一般反應為 click，不能把此階段宣稱包含完整拖曳／表面動畫 scheduler 或語音事件。
- Rust R4 5 tests PASS；接線後 Rust／Editor／實際 APNG 尚待驗證。暫停、失焦、退出、最後一幀復位及 GPU publication 都須驗，不因單元測試通過就標記完整完成。

## 原生功能驗證 R6
Saved/InteractionBodyHitCoreTestsR6.log：7 PASS。新增實際 C001 APNG（空 game root）測試，共享設定 runtime 建 solver、120 個 1/120s 更新有主骨架位移、clear 後主骨架回復、shape_body 完全相同。新增 release-effector 進度與取消測試。資料稽核 Saved/InteractionBodyCatalogAuditR1.json：128 reactions／15,792 rows／140 pose mappings 全部引用合法。仍須 Unreal 射線／畫面／雙人／暫停與最後完整驗收。

## Unreal 第一輪功能預檢
InteractionBodyUEPreflightR1 的 BodyGesture／BodyTouchAPNG／RestoreControls 均 Success（3 tests）。APNG 真實碰撞命中與原生反應啟動可用，換到沒有 mapping 的 state 會取消並禁用。此輪僅預檢：當時整體 build 有其他驗收行程鎖定 DLL，而且後續施力座標／entry preparation 有更新，不能當最後新版完整驗收。R7 Rust 7 tests 再次 PASS。新增 Unreal 暫停雙人實際 APNG 測試與 actor 旋轉／縮放命中一致性測試，正在執行。

## Unreal 功能 R2
InteractionBodyUEFunctionalR2：4 Success、exit=0、module_hashes_stable=1。實際 APNG 點觸、actor 旋轉／uniform scale 命中一致性、暫停雙人動畫仍有可見主骨架位移／反應完成／動畫時鐘不前進且雙方同步、返回控制還原均通過。external_unreal_observed=1，此為功能證據，不是 FPS 證據。每角色 solver／catalog 準備移至 scene entry（冷準備約0.87–1.38s；不能放到第一個互動點擊）。完整 rendered scene 回歸遇到舊的顯示標籤假設，已修 audit identity 比較但被外部 DLL lock 阻擋重建。完整表面拖曳、動畫層／語音觸發 scheduler 尚未完成。

## 單人入口補齊與新版驗證 R8
新增「單人點觸待機」，從共享 AnimationInfo_00 找到實際 solo Idle controller，背景準備後才允許提交。既有 pairs API 繼續只列雙人／多人，新增 interaction poses API 納入單人，避免污染配對選單。點觸仍依原作 fileReaction 與 Idle state 判斷可用部位，不用任意 Studio clip 代替原作單人動作。
新增 BodyTouchSolo：實際空 game root APNG、共享 solo controller、凍結動畫時間時反應仍更新／完成、身形不變。等待新版 Editor build 後執行；不能只因 UI 按鈕存在便宣稱成功。
Rust InteractionBodyHitCoreTestsR8：7 PASS、0 failed。仍需新版 Unreal 五項回歸及 rendered scene。完整表面拖曳與動畫層／音效語音事件排程維持待完成。

## 操作與邊界
進入一對一場景後，等待「單人點觸待機」背景準備完成，按下即切換原作 solo Idle 並啟用「身體點觸反應」；雙人播放則依當前實際提交姿勢／state 決定是否可用。左鍵在可用部位按下觸發原曲線，反應播放中不重啟；水槍模式與UI起按不會同時觸發。返回、失焦、換不允許姿勢或換角色會取消。一般點觸不改 shape_body，肚子大小仍由既有獨立入口調整。
R3 發現 standalone batched solver 缺 world publication，已修正；paused UI 改為反應使用真實 dt、base clock 凍結。需 R4 完整驗證後才標示此階段完成。

## 最新功能驗證結果
- InteractionBodyEditorBuildR8：完整 Succeeded，exit 0。
- InteractionBodyHitCoreTestsR9：7 PASS／0 failed，含實際 APNG、曲線、釋放權重、取消復位、60／120Hz。
- InteractionBodyUEFunctionalR4：5 Success、exit 0、module_hashes_stable=1。BodyGesture、BodyTouchAPNG、BodyTouchPausedPair、BodyTouchSolo、RestoreControls 全通過。單人測試走 Actor.ApplyPoseFrame(dt,false)，驗證凍結 base clock 仍有反應主骨架位移，反應正常完成，身形不變。
- InteractionBodySceneFunctionalR2：functional_pass=1、audit_completed=1、exit 0、module_hashes_stable=1。實際 APNG 進場／背景 solo Idle ready、原本走路循環／速度／語音嘴型、雙人多姿勢切換／男性替換失敗保留、即時頭／嘴／肚子控制與返回流程通過；切換低於300ms。
- 本輪 external_unreal_observed=1，performance_evidence_isolated=0。報表的平均4.909ms只屬功能流程混合取樣，不是雙人點觸或120FPS合格證據。水槍本輪未測。entry 12.848s 超過10s，依使用者先優化執行的優先序未處理，不能宣稱全部需求完成。

## 剩餘工作
1. 完整表面拖曳、原作 body/item layer scheduler 與接觸／放開事件語音排程；現階段僅一般 click HitReaction，不能以手勢 Drag 狀態存在代替實作。
2. 第二角色與場景遮擋的完整接觸規則，以及更多動作／任意APNG角色驗收。
3. 無外部工作干擾的雙人乾動畫／點觸／水槍 Present 與CPU／GPU profile，確認乾<=6ms、整體<=8ms／120FPS。最後再做完整驗收。
