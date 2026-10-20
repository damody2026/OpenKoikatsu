# 菲比全 outfit 共用匯入修正（Stage339）

**Stage340 更正**：新確切卡片 native probe 的 ShadowColor 也是黑色，影像髮飾仍藍紫色，因此狀態差異不能解釋 UE 全黑。已追到並修正共用 MainOpaque 简化 fragment；見 FORGE_OPAQUE340.md。以下保留 Stage339 當時的測量與推論，不將舊報告冒稱完整驗收。

接續 Stage338，按原生實際 draw inputs 檢查 outfit0/1/2/3/5/6。沒有 outfit4 dump；不依角色名稱選擇修補分支。

## 根因與實作

1. 身體／臉部繼續使用 prefab 光澤及細節強度，漏掉 `ChaControl` 的角色設定。身體 `_SpecularPower` 應為 `Mathf.Lerp(skinGlossPower, 1, Status.skinTuyaRate)`，並設定 nail gloss/body detail；臉部使用 cheek/lip gloss/face detail。現在先套角色設定，再依 MaterialEditor 的精確角色、材質及 property 身分覆寫；負值（本卡 lip=-1）保留，不擅自 clamp。新增 Status 解析及缺省、clamp、大小寫去重、ME precedence 回歸。
2. 領帶 renderer 僅存在於 `ChaClothesComponent.rendAccessory`。原生 `ChangeCustomClothes` 最後明確將 `array[0].RebuildTextureAndSetMaterial()` 寫入 `rendAccessory.material._MainTex`；Rust 原本只處理 normal01/02/03 與 alpha01，漏了 accessory。現補入 RT0，仍只寫第一個材質。子服裝 C 使用 top 第4色的既有邏輯正確，沒有為本卡重設色槽。
3. session pipeline 215→216→217、CLI extract 326→327→328，排除舊輸出快取；prefab cache 結構未更改。一次 `OK_NOCACHE` 診斷證實 renderer IDs 正確而群組僅 accessory，否定 prefab cache 遺失 ID 的猜測。

## 已有證據

- `Saved/Tests/PhoebeOutfitDump339/EffectiveInputsBefore.json`：120項中30項 FAIL；每套 body specular/nail、face detail/specular/lip 各5項。
- `Saved/Tests/PhoebeOutfitDump339Fixed/EffectiveInputs.json`：120項全部 PASS，最大誤差3.2736206e-8，門檻1e-6；含36 skin settings、36 iris source UV、48 EyeDraw color/toggle/rotation。虹膜原始串流吻合，沒有翻轉 UV。
- `Saved/Tests/PhoebeOutfitDump339Fixed/SavedCoordinates.json`：獨立讀取 PNG 後的角色資料、Coordinate block 及各 coordinate 長度前綴容器，保存7份原卡 coordinate（不等同存在7套 native dumps）。
- `Saved/DebugShots/PhoebeDraw339/Tpose`：第一輪四視角真實 UE RHI，角色、材質/GPU、凍結時鐘與附件檢查通過；目視发现白色領帶，因此並未把此輪截圖宣稱成完整圖像 parity。
- `Saved/Tests/PhoebeSubpart339Uncached/outfit0_sub2.json`：cols[0] 已取第4色；map.accessory 含領帶 ID，normal/alpha sets 為空。舊 runtime 主貼圖8×8，修正後512×512；全量最終核對及部署結果見下方驗收。

## 比對邊界及避免誤判

`KKS_SliderHighlight/SliderHighlightPlugin.cs` 的 CreateHighlightRenderer 會複製 body/head mesh，建立 `Highlight_*_rend` 供 Maker 滑桿編輯高亮。六套12筆 Highlight 屬編輯輔助，不能直接算成缺少角色材質層。其他未對應項仍保留在全量原始 audit，沒有以排除失敗的方式宣稱 all_dumps_match。

正常貼圖編碼／GPU readback 量化、已烘焙 alpha、注視 UV 與來源 metadata 都需核對實際用途。MainTex 像素一致不代表完整 Forward shader／世界姿勢／光照一致；仍不宣稱全專案零 bug。髮飾及整體陰影的圖像差異尚需同場景 draw-state 證據。

## 本轮最終驗收

最終六套輸出及逐項結果位於 `Saved/Tests/PhoebeOutfitDump339Final`；建置、回歸、截圖紀錄均以 `PhoebeOutfitDump339Final`／`PhoebeDraw339Final` 為前綴。以下在實際測試完成後記錄結果，不以匯出 exit0 代替 native parity。

- 六套真實 session 匯出1 PASS／244.49秒，176網格均有來源匹配；120項有效值全部 PASS。完整 raw audit 仍為129 different／51 matched／117 missing，`all_dumps_match=false`，差異不被隱藏。
- 可用的 sub-C 領帶 MainTex 512×512×4 與原生完全一致，最大／平均 RGBA差0（修正前8×8白色）。NormalMap 的既有讀回差異仍保留，未把 RT 通過宣稱成全部貼圖通過。
- renderer 群組／第一材質與不相干 renderer 保持回歸1 PASS；skin draw precedence 1 PASS、原始卡 Status/default parser 2 PASS。Rust release 成功，UE實際重新 link DLL，Succeeded34.03秒。
- fresh `PhoebeDraw339Final` 四圖900×1080，runner exit0，材質／每視角 GPU／凍結 pose/shadow readiness 通過；目視紅領帶恢復、角色完整，沒有全灰屏。保留原始正式 Screenshots。
- 髮飾追查：独立 PNG/KKEx 解碼 `SavedMaterialColors.json` 證明七個座標皆有 `hair_acc.ShadowColor Value=[0,0,0,1]`，ValueOriginal 是原生 dump 的藍色值 `[.8,.8662278,.9411765,1]`。runtime effective black 遵守卡片 ME；native dump 留在 original，不可為湊 dump 改掉卡片覆寫。
- `character_source.json` 明示 `charaFileName=null`；`card_resolver.json` 以 `nameIndex+shape` 自七個候選推定同名檔案，沒有原生載入卡 bytes/hash 證據。可核對幾何／材質，但不能證明 native 的完整 ME state 與硬碟卡一致。這是資料來源／當下狀態限制，並非已證實 shader bug 或已證實畫面等價。
- `Acceptance.json` 綁定來源與工具 SHA、production source、Rust staticlib、實際 UE DLL、測試紀錄及四圖。`passed=true` 僅指明列出的修正與回歸，`all_dumps_match=false` 另保留。Owned UE PID26284已結束，共享建置窗口釋放。
