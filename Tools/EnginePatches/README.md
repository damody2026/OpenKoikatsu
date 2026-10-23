# 引擎補丁清單

本專案使用的引擎是 `E:\PhysX\UE5.8`，分支 `UE5.8-physX-5.11-ok`（[damody2026/UnrealEngineOK](https://github.com/damody2026/UnrealEngineOK/tree/UE5.8-physX-5.11-ok)，需先把 GitHub 帳號綁定 Epic Games 帳號才看得到）（5.8.4 + GameWorks Flex / PhysX 5.11）。
下列補丁都要以 commit 形式存在於這個分支上。來源是原本 `D:\UE5.8`（分支 `5.8`）的本機 commit，用 `git format-patch` 加 `git am -3` 搬過去。

| 引擎 commit 主旨 | 給本專案的作用 | 標記（`OpenKoikatsu.Build.cs` 會檢查） |
|---|---|---|
| 修正 Hot Reload 只收集目前專案的 DLL 模組 | 熱重載 | — |
| 修正渲染執行緒上反射捕獲淡入的時鐘來源 | 反射捕獲 | — |
| 為位元壓縮動畫加入可選的常數縮放軌快取 | 群眾動畫解碼（`ConstantScaleCache.patch`） | `PrepareAnimConstantScaleCache` |
| 允許單節點骨骼元件使用加權骨骼階層作為 RequiredBones 種子 | 群眾 RequiredBones（`WeightedRequiredPoseSeed.patch`） | `SetSkeletalWeightedPoseSeed` |
| 為 CPU 蒙皮矩陣加入可選的逐位元重用快取 | 群眾蒙皮矩陣（`ExactComponentMatrixCache.patch`、`ExactRefToLocalMatrixCache.patch`） | `GetSkeletalComponentMatrixCacheStats`、`GetSkeletalFullRefMatrixCacheStats` |
| 讓骨骼除錯姿勢複本可跳過，並修正跟隨者的複製範圍 | `LeanDevelopmentDebugPose.patch`、`../Patches/DevelopmentFollowerLocalDebug.patch` | — |
| 以執行緒群組重排非 Nanite 虛擬陰影的每頁繪製命令 | `VsmQueueReuse/` | — |
| 新增非 Nanite 虛擬陰影的平行繪製開關 | `../Patches/ParallelVirtualShadowDraw.patch` | — |
| 新增動畫庫的完整蒙皮矩陣計算著色器 | `../Patches/MatrixBank/` | — |
| 不透明 base pass 可依 primitive 的 TranslucencySortPriority 排序（`r.OK.OpaqueSortPriority`） | 重合服裝層（Material Editor 複製材質、重複飾品）照 Unity 繪製順序，不再閃爍（`OpaqueSortPriority.patch`，見 [計畫](../../Docs/OPAQUE_DRAW_ORDER_ENGINE_PLAN.md)） | `OKOpaqueSortPriority` |

不搬的項目：
- `BitwiseDecodeScopes.patch`：只加 profiler 範圍，D: 也沒有套。
- D: 上 5 個「記錄 … 覆蓋層路徑」的 commit：只記錄 D: 本機的私有 staged 引擎，不適用。

建置時任何一個標記找不到，`OpenKoikatsu.Build.cs` 會直接讓建置失敗。以前會默默變成 0，群眾變慢卻沒有任何錯誤。
用原版引擎做實驗時，可以設 `OK_ALLOW_UNPATCHED_ENGINE=1` 放行。

換新的引擎版本時，照這張表重做一次：

```
git -C <舊引擎> format-patch -o <dir> <base>..<tip>
git -C <新引擎> am -3 <dir>/*.patch
```
