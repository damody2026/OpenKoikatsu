# Tripo 頭髮接入 OpenKoikatsu APNG

日期：2026-10-09。此流程接入 OpenKoikatsu 的 portable snapshot，不是 Sunshine 的 zipmod 封裝。

## 輸入與基準

- 頭髮來源：使用者下載的 `ref/hair+wig+3d+model.zip`，Tripo 任務 `2d9ef181-060e-4793-aaeb-6b17abe05ccc`。
- 參考圖與生成方式：`output/grok-hair-three-views-2026-10-09/頭髮生成方法.md`。
- 基準卡：`Content/CharacterCards/CrowdAPNG/C076.png`，對應使用者指定靈砂卡的體型；保留原臉、體型、服裝與其他配件。
- 工作目錄：`output/grok-hair-three-views-2026-10-09/openkoikatsu-apng`。

FBX 實際只有一個頭髮網格，三角化後 **49,282 面**，沒有減面。因 UV／法線接縫展開，匯入 portable mesh 的頂點數為 32,676；面數仍是 49,282。

## 座標與權重

用 Blender 匯入 FBX 後，原始前方向為 Blender -Y。此模型轉成 OpenKoikatsu 的 Unity 座標：`[x, y, z] = [-Blender.x, Blender.z, -Blender.y]`。必須以遊戲正面瀏海與參考圖核對左右；第一版漏了 X 軸反射，遊戲畫面左右相反。反射時也要反轉三角形繞序、切線 handedness 並轉換法線，不能只翻預覽圖片。

此模型對 C076 的試作擬合：

```
world_position = source_position * [0.62, 0.62, 0.45] + [0, 0.93, -0.015]
head_local_position = inverse(cf_s_head.world) * world_position
```

這是本次網格／頭型的實測值，換模型必須重新擬合。法線使用逆轉置，切線轉換後重新正交化。保留 UV 接縫；因座標包含反射，輸出三角形的第二、第三索引互換，切線 w 取反。

每個頂點 100% 權重給 body skeleton 的 `cf_s_head`，bind pose 為 identity，輸入位置為 head local。不要把已經是 world 的位置再次當 head local，否則頭部變換會套用兩次。此版只有頭部剛性跟隨，**沒有髮尾骨架、動態碰撞或柔性變形**。

C076 的原髮型其實是名為 `Hair` 的 accessory。匯入器辨識這個欄位與一般 hair role，替換該網格並隱藏其餘舊髮型；保留其他 accessory。新名稱為 `Tripo_Golden_Hair_49282`，role 為 `hair_back`。

## 材質

第一版採用現有的 `Shader Forge/main_item`（shader kind 8），使用 Tripo basecolor 轉換的 2K PNG。`png` 欄位與 `shader_textures["_MainTex"]` 都必須寫入；只填 `png` 會在原生 shader 路徑顯示白色。

這個原生 Forge gamma-space pass 使用 `_MainTex` sampling metadata `[1,0,0,0,1]`（bilinear、repeat、repeat、不額外 sRGB decode、mip policy 1）。直接沿用 sRGB fallback 會把金色壓成深橘色。此設定僅適用此原生 pass，不是所有 UE PBR 材質的通用設定。

金色與髮束明暗來自原貼圖。原始 normal、roughness、metallic 貼圖留存在下載解壓目錄，**此版尚未接入完整 Tripo PBR 材質**。未修改共用材質資源。

## 封裝與驗證

`Tools/ok-extract/src/bin/ok-hair-pack.rs` 提供：

```
ok-hair-pack inspect BASE.png inspection.json
ok-hair-pack import BASE.png hair-head-local.json NEW.png
```

JSON 包含 positions、normals、tangents、uvs、indices、texture。匯入時檢查所有通道、有限值、索引範圍及 50,000 面上限，拒絕覆寫既有輸出。每套服裝各自替換頭髮，打包為 OKPV0003，保留共用資源引用及 IEND 後原始人物參數。

匯入後重新讀取全部七套服裝，確認頭髮網格／貼圖／權重一致，非頭髮幾何與貼圖未改變。另用頭部局部 Y 軸旋轉 30 度，逐頂點比對實際 skinning 與預期矩陣結果，誤差須小於 0.00001。

## Unreal 擷取

使用工作目錄內 `capture_apng.ps1`。目前專案 module BuildId 對應 `D:/UE5.8/Engine`；`CrowdRuntimeStable` 的 BuildId 不符，不能直接載入目前插件。

`ok.ExportApngCard` 從來源卡實際渲染臉部／正面／側面／背面，生成 768×1024、四幀、2/1/1/1 秒、5 秒無限循環 APNG。`verify_apng.py` 比對來源與 APNG 的 okPv 完全相同、人物參數 trailer 完全相同，並驗證全部 PNG chunk CRC 與動畫時間。最後對 APNG 再執行 `ok-preview-pack verify`，確認七套服裝可載入。

這張卡以 portable snapshot 保留新網格；若日後從原始 KKS 髮型 ID 重建，仍可能還原舊頭髮。正式可編輯髮型資源註冊、髮尾骨架與 PBR 接入屬於後續工作。

使用者最新指示：臉型與身材先不調整。C076 只是目前掛接測試基準，不能宣稱其原臉符合金髮女角人設。

2026-10-09 更新：另存 `Content/CharacterCards/Custom/GoldenHair_NativeDepth_APNG.png`，改用原生 `Shader Forge/main_hair`、現有陰影 ramp 與頭髮高光貼圖。縮放從 `[0.62,0.62,0.45]` 改為均勻 `[0.62,0.62,0.62]`，高光使用獨立連續 UV1，沒有走完整 PBR。詳細方法、側面對照及驗證報告在 `output/grok-hair-three-views-2026-10-09/openkoikatsu-apng/native-hair/README.md`。原始卡保留。
