# Stage348：身體物理重綁

通用修正，沒有按卡名或圖片內容決定分支。

## 根因與修改

Stage347 的替換身體重綁保留了普通 DynamicBone 與 Ver01 元件，但 Ver01 的有序 `m_Nodes` 仍使用來源 Transform ID。全部 ID 改變時，元件存在而粒子無法建構。

另外，重綁把所有 `m_Colliders` 移至普通物理使用的 per-chain 通道；共用角色碰撞設定會略過 Ver01，因此其碰撞器沒有進入執行列表。

依本機反編譯 `Assembly-CSharp/DynamicBone_Ver01.cs`：`SetupParticles` 按 `m_Nodes` 順序建立粒子，`UpdateParticles2` 直接使用 `m_Colliders`。現在重綁每個節點，保持順序與 rotation flag；Ver01 保留來源碰撞器通道，普通 DynamicBone 維持共用角色碰撞通道。

SESSION239／CLI349／CACHE108。沿用現有序列化欄位；pipeline/cache key 更新使舊錯誤參照失效。既有 Stage347 Acceptance 是歷史版本，不重新封成新版。

## 驗證

新增合成 rig 正控制：來源與目標的全部 Transform IDs 不同；兩版本皆保持有效粒子、碰撞器及排除參照。DBoneSet bincode roundtrip 後能重建執行狀態；更新一個 instance 不改變另一個的 Transform 與粒子歷史。

修正前 RedR2 重現舊節點 ID；只修節點後 RedR3 重現碰撞器數量 0。GreenR2 通過。所有 `dbone::` 非略過回歸：27 PASS、0 FAIL、11 ignored。忽略項目不認證為通過。

Editor build Succeeded，31.95 秒。實際 live/cache 與兩 renderer 最終測試以 `Saved/Tests/BodyRebind348/Acceptance.json` 為準；日誌保留失敗與復原過程。

新版 live/cache 數值測試通過：兩者皆保留 23 個身體加衣服普通物理元件，61 幀更新後 Transform 數值皆有限。Stage347 的固定測試輸出內容另複製至本節 LiveComponents.json，本次執行日誌為 BodyRebind348Live.log。

本版 CPU／GPU 控制皆 Success、實際程序 exit0。CPU 最大位移維持 0.932681433／1.154252291 公分；姿勢刷新後的控制、閉合復原、第二 actor 隔離與卸載清理通過。本節沒有重跑完整四項 Import suite，不將前版結果冒充本版全量回歸。Acceptance348 已封存，所有自有測試程序退出。

此節驗收一般骨骼物理與 morph 系統，沒有實作露骨性行為互動驅動，也不能據此認證該類互動功能完成。
