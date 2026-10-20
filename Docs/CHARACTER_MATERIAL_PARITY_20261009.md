# KK_384914 材質差異修正

## 需求與驗證方式

使用 `C:/portable/KoikatuSunshine/UserData/chara/female/KK_384914.png`，對照使用者提供的原作畫面。修正必須依 shader 與來源參數，不能依卡名加分支。保留角色編輯及一對一互動場景的獨立用途。

## 計畫

1. 匯出卡片實際材質、貼圖、UV 與來源 shader；保存修正前正面、背面、側面畫面。
2. 補齊 GPU 皮膚輪廓 pass，並讓具有 morph 的身體輪廓跟隨同一組變形。
3. 以 shader 身分選擇前髮高光算法，核對高光貼圖、UV 和顏色空間。
4. 核對皮膚疊色參數與原作 fragment 運算，修正顏色轉換，避免任意提高飽和度。
5. 編譯、同卡同鏡頭截圖驗證；記錄仍有的差異，不能以編譯通過代替視覺驗收。

## 已確認

- 來源診斷 R1 通過，38 個可見部件。身體為 `Koikano/main_skin`，頭髮包含 `Shader Forge/main_hair` 與 `main_hair_front`。
- GPU follower 的輪廓建立只涵蓋衣物；morph 輪廓又只涵蓋臉部，身體因此漏畫。
- 前髮材質為 masked，舊高光算法以 translucent 選前髮分支，判斷條件不成立。
- 本次指定缺失已於 R14/R15 的 2 倍解析度畫面確認修復；仍有細小接縫與光照差異，未宣稱逐像素一致。

## 實作與來源證據

- `Material384914Baseline2xR4` 保存 3200×2000 的修改前畫面，退出碼 0；另有 1600×1000 的 R3。
- `Material384914NativeR1/Card0.json` 的原作 renderer snapshot 成功（error 空字串）。原作也沒有 hair_r144 / Hair_F310.001 的 UV1；不可以偽造 UV。
- 原作 hair_r144 高光貼圖為 cf_hair_00_0h_mh，ST=(5,3,0,0.65)，與卡片來源一致。來源高光 PNG 與原作 ReadPixels PNG 完全相同；細節遮罩則是翻轉後相符（RGB mean 0.000135）。原路徑對 serialized 高光漏做其他貼圖已有的方向轉換。
- 選定高光改用既有 `png_in_runtime_row_order`，保留 Sideloader PNG 的方向規則；序列化材質直接綁定高光也做相同轉換。SESSION_PIPELINE_VER=255，避免舊 session 快取掩蓋修正。
- GPU 皮膚建立輪廓 follower；morph 部件使用具有相同 morph 的輪廓元件。前髮依來源 shader 名選程式，不再由透明模式猜測。
- 皮膚 Color 疊色參數依 Unity Color 屬性做 sRGB→linear，保持 alpha 不變。
- 新增 `OK_SHOT_MATERIAL_CLOSEUPS=1`，只影響 opt-in 驗收鏡頭。
- 最終建置與視覺檢查結果見下方「本次驗收結果」；以下 R7–R13 為定位過程紀錄。

## 2 倍解析度驗證進度
- R3 一般建置成功（9.58 秒）；R7 全身及 R8 近照均為 3200×2000、4 張，程式退出碼 0，無材質編譯失敗。
- 實際檢視 R8：胸部輪廓與疊色已出現，但輪廓黑色接縫與頭髮高光仍不符，因此未通過視覺驗收。
- 後續修正：髮材質缺少 UV1 時仍明確供應 Unity 零座標（UE 垂直轉換後 0,1），避免缺省 TEXCOORD alias UV0；這不是重建不存在的 UV。身體 morph outline 不使用臉部專屬 mouth-cavity UV 剔除。R4 建置成功（37.56 秒），等待 R9 對照。

- R10 3200×2000 近照、退出碼 0：取消已整合疊色皮膚的舊 1 cm PixelDepthOffset 後，胸前及下身大片黑色穿插消失，胸部輪廓保留；仍有細小接縫，不能宣稱完整一致。
- 來源診斷 R4 通過（1 test、19.12 秒），新增切線匯出。獨立原作 boneWorld×bindpose 比對 hair_r144 的 runtime basis：法線平均 dot=0.997890、切線平均 dot=0.998391，handedness 全數一致；最差 dot 約 0.95，因此不能當作精確逐頂點一致，也不支持整體翻轉符號。
- R6 建置成功（83.17 秒）；opt-in OK_HAIR_AUDIT=gloss 只顯示實際高光取樣，繼續定位髮絲亮帶差異。

- R11 高光取樣診斷 3200×2000 通過退出碼檢查；上方亮帶缺少，並非只被最終 diffuse 合成壓掉。
- R7 建置成功（18.05 秒），R12 近照成功。高光 Custom 改成明確 Transform(Tangent→World) 輸入，讓編譯器宣告 bUsesTransformVector；但實際 R12 髮亮帶仍不足，不能把這項依賴修正宣稱為視覺根因已解。
- 原作探針新增 opt-in OK_PROBE_MATERIAL_CAPTURE_2X=1：材質 controls 1800×2160，額外保留 source-back；預設仍 900×1080。重新取得獨立原作成像，避免依假設改符號或亮度。

## 已找到髮亮帶根因：缺省 vertex UV binding
- Native2xR2 的同卡原作背面 1800×2160 確實有上方亮帶。
- Native2xR3 在 cloned mesh 控制 UV1：保持缺省 vs 明確 UV0，RGB 平均誤差 0.000000943/255、最大 1/255；明確零值平均誤差 0.508605/255、最大 33/255。verify_missing_uv_binding.py 判定通過。
- **推翻前面零值推論**：Mesh.uv2 為空不代表 shader 讀零；本來源的 Unity vertex binding 回用 UV0。先前 R9 明確填零是錯誤方向，不採用為最後版本。
- 最後實作保留來源 UV1；缺少時，髮材質在 procedural 及 skeletal TEXCOORD2 綁定 UV0。來源 mesh、卡片資料不修改，也不增加卡名分支。R8 一般建置成功（14.84 秒），正在擷取 R13 驗證。
- 原作探針兩次皆完成 snapshot、error 為空；Application.Quit 延遲時只停止該次啟動的 PID，finally 移除臨時 probe DLL。

## 本次驗收結果

- 最終一般 Editor 建置 R9 成功（16.28 秒，退出碼 0）；未修改 D:/UE5.8 引擎原始碼。
- `Saved/Tests/Material384914FixedCloseup2xR14`：正、側、背與臉部共 4 張 3200×2000 近照，退出碼 0，capture-report.json 通過，無材質編譯失敗。
- `Saved/Tests/Material384914Fixed2xR15`：同 baseline 鏡頭的 4 張 3200×2000 全身／臉部畫面，退出碼 0，capture-report.json 通過。已實際檢視四張畫面；自動報告只代表截圖與解析度有效，不代替視覺判斷。
- 視覺確認：胸部與身體輪廓恢復，乳頭／皮膚疊色恢復；髮頂及下段髮絲亮帶恢復。取消整合皮膚疊色後不再需要的舊 1 cm PixelDepthOffset，胸前與手指的大片黑色穿插消失。
- 髮亮帶根因以原作 cloned mesh A/B 測試確認：缺省 UV1 的成像與 UV0 綁定近乎一致，與零座標有明確差異。一般 procedural 與 skeletal 髮材質均採相同缺省規則，保留來源既有 UV1；不依 KK_384914 卡名特化。
- 尚存差異：胸骨與臀部有小型深色接縫；小腿粉紅條紋在 baseline 已存在；側面的環狀配件原先即存在。整體光照與原作仍有差距，因此本次只確認上述指定功能修復，不宣稱完整像素一致。
- 本輪為材質修正與離屏畫面驗證，沒有重新量測一對一互動場景的 120fps。
