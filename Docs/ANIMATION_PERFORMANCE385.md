# Stage385：每次刷新共用身體碰撞器姿勢

2026-10-03。正式 BuildR1 actual 0 / Succeeded，主專案 DLL 已更新。

## 根因與通解

Stage384 已解決動畫切換反覆重建 DynamicBone／240 步暖機，但雙人每幀 CPU 更新仍約 47 ms。本輪確認普通飾品物理鏈逐次綁定相同身體碰撞器，每個 renderer／chain 重複搜尋骨骼名稱及計算相同世界矩陣、lossyScale。

`CharacterColliderFrame` 在 session 一次動態附件刷新內延遲建立，包含 native 碰撞器、普通 skirt／non-skirt 碰撞器的有序姿勢資料。主身體與 coordinated clothes 使用各自的 frame；角色、幀、資源之間不共用可變資料。沒有 DynamicBone 的 renderer 不主動建立 frame。

- 名稱索引保留大小寫敏感、原 HashMap 同名第一項、缺失名稱略過與 authored collider 順序。
- world／lossyScale 仍走原計算順序及每碰撞器的 ancestor 運算，沒有改浮點乘法結合順序。
- implanted collider 仍依每個附件私有骨架逐次計算；保留 native → implanted → ordinary 的順序與 implanted offset。
- 每次更新重新準備當前 body pose／flags，無跨幀 stale cache。init 與 live 更新使用相同準備資料；首次 240 步暖機、粒子歷史、freeze axis、Ver01 排除及 Ver02 既有流程保留。
- 一般 CPU geometry 與 shared-asset palette-only 路徑使用同一物理流程，不增加 palette-only CPU 蒙皮。

依據 `C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/DynamicBone.cs` 及插件 `plugins/KKS_Plugins/KKS_Colliders/KK_Plugins/Colliders.cs:221,357` 的普通 collider／skirt 更新。此處只消除相同 Transform 姿勢的重複準備，沒有更換 solver、降低物理／陰影／IK 頻率、修改持久化格式或加入卡名特判。

## 正確性驗證

`Saved/Tests/AnimationPerf385/` 保存命令 receipt、完整日志與相對 Stage384 snapshot 的源碼 diff。

- BindingR1 actual 0：獨立原 `.find` oracle 核對同名／大小寫／缺失、負縮放、重複 collider、更新後新 pose；1 test passed。
- CollidersR1 actual 0，最終 InstalledR1 actual 0：真實 March_7th 原版 walk 的 21 個私有 solver，各四種 flags，共 **84 cases**。獨立保留舊綁定實作，逐項 exact 比較 collider 順序、world、lossyScale、implanted offset、freeze axis；step 1/60 後 particle pos／prev exact。
- InstalledR1 同時通過動畫切換粒子生命週期：有效 clip 保留 state、無效 clip 保留時鐘／state、floor flags＋delta0 不重做暖機、explicit reset 仍清除。2 tests passed。
- Perf385PairR1 actual 0 / Automation Success：正式 DLL、March_7th outfit0 667 bones＋ill_male_02 outfit0 681 bones；原版 walk 與兩個原生雙人 metadata 選項，既有幾何／矩陣 oracle、共同時鐘及背景準備期間舊播放通過。
- Perf385ControlsR1 actual 0 / Automation Success：KKS 七種滑鼠組合、相機 owner／gesture／clock 與 generation cancel 回歸。

## 性能及限制

| 同一 API 測量 | Stage384 PairR5 | Stage385 PairR1 |
| --- | ---: | ---: |
| 第一個配對 commit | 369.098 ms | 305.412 ms |
| 第二個配對 commit | 93.045 ms | 83.074 ms |
| 第一個配對 30 frames CPU 平均 | 47.106 ms | 45.981 ms |
| 第二個配對 30 frames CPU 平均 | 47.603 ms | 45.301 ms |

本輪單角色 quiet 20 ticks 26.547 ms，雙角色獨立 walk 60 ticks 38.538 ms。配對 prepare 分別 20.938 秒、12.732 秒；第二次背景準備期間舊配對前進 249 frames，最大一次 CPU 更新 101.918 ms。

單次跨輪數值僅供觀察，不把小幅差異當固定提升比例；冷解析耗時、每幀約45–46 ms仍存在。不是整體 GPU／場景 FPS、完整 Gallery 滑鼠點擊延遲或全卡／全姿勢零 bug 認證。此輪完成碰撞器重複準備的通用修正與回歸；後續性能調查須繼續分段量測，不能把所有 dynamics 成本都歸因於碰撞器。

錯誤及限制見 Docs/ERROR_LOG.md E263；封存 `Saved/Tests/AnimationPerf385/R1.audit.json`／snapshot，核對 Stage384 全部舊證據不變。
