# Stage305 新增 dump 角色測試（2026-09-29）

## 範圍與結果

`C:/portable/KoikatuSunshine/mesh_dump` 實際新增三個目錄，因此三張一起測。
目錄各只有一張遊戲 JPG，沒有新增的頂點／材質 mesh dump；本輪是實際 Unreal
載入與畫面比較，不是逐頂點來源或遊戲像素驗收。

| 卡片 | Unreal 載入 | 參考照比對發現的問題 |
| --- | --- | --- |
| KK_388205.png | 成功 | 鼻部灰色矩形區域、虹膜變成很小的淺藍圓片；衣服與頭飾有顯示，材質與來源仍有差異。 |
| KK_346095.png | 成功 | 領口白色部件變成藍色，胸前有灰色三角區域；臉頰紅暈與皮膚細節明顯強於參考照。側髮與白藍髮飾有顯示。 |
| 弹丸论破 - 江之岛盾子2.png | 成功 | 眼鏡未見、虹膜顯示不正確、熊髮飾與靴子材質不同；遠離角色的零碎幾何使全身取景縮小，正臉取景也錯位。 |

三張選擇 Studio walk 0.4 秒時都可見手臂異常伸長。
這是公開畫面的實際失敗，不能因來源 binding 找得到或 palette audit 通過而宣稱動作正確。
鼻部／虹膜／服裝材質與手臂問題的完整根因仍未定位，本輪沒有加入卡片特例修補。
下巴輪廓線依先前使用者指示不處理。

## 已完成的測試

- 同一正式 Development binary，三卡各 T-pose／Studio walk 0.4s × 全身／正臉／側臉，共18張900×1080 PNG。
- outfit0；mouth pattern0、open0.8；睜眼固定。walk 使用已匯入 Studio row `r00000022`，來源 `studio/anime/00.unity3d`、`walk_run`、`f_aruki_00_00_01`，scene4。
- 首次 T-pose `SetPoseMode(0)` 重新開啟 Tick，導致不同取景時間的眨眼狀態不同。只修改截圖測試分支，在建立中立姿勢後停止 Actor Tick；正式互動、動畫與眨眼功能未修改。修正後重拍全部18圖。
- 初次18圖在 `Saved/DebugShots/NewCards305/old/BeforeFreeze`；原 log 有 `BeforeFreeze` 後綴，不能當固定表情的最終圖。
- 最終 T-pose／Walk 各3次 PV audit、各27個 runtime attachment vertices audit。結果與既有 runtime buffer 一致，但不代表來源綁定、蒙皮权重或材質正確。
- 江之島盾子另以全新 Unreal 程序單卡 T-pose 拍3張：飄散幾何仍存在，排除「上一張卡殘留」這個原因。單卡的領結／頸部材質也有差異，尚未把原因歸於任何 mod。
- 所有測試程序正常退出；原 `Saved/Screenshots` 的15張 Stage286 T-pose SHA256維持。

## 產物

- 比較圖：`Saved/Tests/NewCards305/ContactSheet.png`（每列：遊戲參考／UE T-pose／UE walk／UE正臉）。
- 最終原圖：`Saved/DebugShots/NewCards305/Tpose`、`Walk`。
- 獨立單卡：`Saved/DebugShots/NewCards305Solo/Tpose`。
- `Saved/Logs/NewCards305Audit.json` 保存卡片／參考照／18截圖 SHA256、尺寸與audit結果；`visual_parity_passed=false`。
- `Saved/Logs/NewCards305_Tpose.log`、`NewCards305_Walk.log` 及各自 `Stderr.log`；另有 `NewCards305Solo_Tpose.log`／`Stderr.log`。
- build：`Saved/Logs/NewCards305UnrealBuild.log`，Succeeded（10.86秒）。
- 共用工具：`Tools/GamePoseProbe/run_dump_cards.ps1`、`report_dump_cards.py`。測試輸入可換成任意卡片檔名，loader没有卡名分支。

重現：

```powershell
& ./Tools/GamePoseProbe/run_dump_cards.ps1 -CardNames @('KK_388205.png','KK_346095.png','弹丸论破 - 江之岛盾子2.png') -Tag NewCards305
python ./Tools/GamePoseProbe/report_dump_cards.py 'KK_388205.png' 'KK_346095.png' '弹丸论破 - 江之岛盾子2.png' --tag NewCards305
& ./Tools/GamePoseProbe/run_dump_cards.ps1 -CardNames @('弹丸论破 - 江之岛盾子2.png') -Poses @('Tpose') -Tag NewCards305Solo
```

## 下一步定位順序

1. 走路：逐一比較 shoulder／elbow／wrist 的來源 world TRS、Session shared palette、實際手部頂點與 bind matrix；來源73個body binding已找到，不以漏 clip 推論。
2. 江之島盾子：定位每片飄散幾何的 Session part／renderer／parent／bone indices，不以忽略遠處幾何或改相機距離掩蓋載入問題。
3. 虹膜／鼻部／服裝：核對各卡實際 shader family、MaterialEditor覆寫、原貼圖／UV／normal與來源公式；不從照片直接擬合顏色。

詳細錯誤紀錄：`Docs/ERROR_LOG.md` E-161。
