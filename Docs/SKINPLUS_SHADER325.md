# Stage325 SkinPlus 原生 pixel program 與來源缺口

2026-10-01。這一階段完成可重現的原生運算移植與來源診斷。正式渲染仍是 SESSION202／CLI313；SkinPlus 尚未接入，Siesta 頸暗尚未修好。

## 已實作

- `Tools/skinplus_program_codegen.py` 從安裝版 Vanilla Plus 1.7.2 的 ParsedForm／DXBC 產生 Outline 與四組 Forward pixel program。立即數保留 DXBC bits；共用 translator 新增浮點 `eq` 的 all-one bit mask 語義。
- Checked-in 程式在 `Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Shaders/SkinPlus*.hlsl` 與 `SkinPlusBindings.json`。沒有連到 UE 材質建構或更改 shader classification。
- CB register 讀取 `m_ConstantBufferBindings`，同時保留完整 metadata 容量與 DXBC 實際宣告容量；保留 `unity_LightColor` 的八個 float4、紋理 register、shared sampler、keywords。未知 shader／keyword／維度／越界／未支援指令直接失敗。
- `Tools/skinplus_oracle.cpp` 在 D3D11 WARP 同時跑原始 DXBC 與移植 HLSL，使用同一輸入、四組不同 UV、vertex colour、正反面、normal detail、shadow 及四個 point lights。sampler 各有不同 filter／wrap，避免把 register 錯接藏在同一 sampler state 下。
- 最終五組 ×64 controls＝320 cases／5120 pixels：RGBA max error 全0；都有 keep／discard 正負控制。21個錯接反例全部按預期失敗：UV0／UV1、vertex colour X／Z、sampler register、NormalMap ST、point-light equality。
- MainItemPlus 五個 generated files 重新生成 SHA 逐檔相同。Rust read-only cache 診斷1項 PASS，production `cargo check --release --lib` PASS；不是全 library suite PASS，也沒有本輪 UE build／新 capture。

## 實際來源範圍

`Tools/GamePoseProbe/report_skinplus_inputs.py` 讀34卡 inventory 的目前 dump，找到兩個 SkinPlus 材質：Siesta 的 face／body。三張缺新 dump 的舊控制明列，不能推成完整 native family coverage。

`cached_skinplus_source_inputs` 使用上一版全34卡 passing comparison 的**確切 p202 default keys**，不重新按 LIKE 挑 cache，也不寫入 cache。原生 Mesh／material 身分選取兩個 source parts，保留四組 UV、vertex colours、raw source material、legacy display values、各來源 PNG。

來源報告 `Saved/Logs/Skin325SourceCoverage.json` 明確是 `native_parity_passed=false`：

- body 是 cached `Koikano/main_skin`／kind5，face 是 cached `Shader Forge/main_skin`／kind7；native 皆 `xukmi/SkinPlus`。`me_shader_registration` 目前只將已實作 MainItemPlus 的 nested DXBC family 接入，新 Vanilla Plus families 仍被保護。body／face 亦有獨立 create／emit 路徑；不能只移除註冊保護就宣稱移植完成。
- 缺七個 native 非空資源的獨立 transport：body `_Texture2`／`_liquidmask`；face `_Texture2`／`_Texture3`／`_liquidmask`／`_overtex1`／`_overtex2`。它們可能因係數為0而無可見貢獻，或由 legacy 路徑 baked；缺欄位不等於已證明七個畫面 bug。
- raw source material 亦有 numeric 差异。既有 typed display fields 與新 shader defaults 必須另核對；不能把所有 raw missing 值當成 effective material 值為0。
- 這些 mesh dump 未提供原生 vertex-colour stream；`overlayVertexColors=null` 不能證明預設顏色。Material keyword list 也不能推得所有 scene-global keywords，不得只據它選 Forward variant。

## 重現

在專案根目錄執行；輸入 `Skin324NativeShader` 是上一輪抽出的原生來源，本輪未重新執行遊戲。

```powershell
python Tools/skinplus_program_codegen.py Saved/Tests/Skin324NativeShader Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Shaders
python Tools/skinplus_oracle.py Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Shaders Saved/Tests/Skin324NativeShader Saved/Tests/Skin325/Oracle
clang++ Tools/skinplus_oracle.cpp -o Saved/Tests/Skin325/skinplus_oracle.exe -O2 -std=c++17 -ld3d11 -ld3dcompiler -lole32
python Tools/skinplus_oracle.py Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Shaders Saved/Tests/Skin324NativeShader Saved/Tests/Skin325/Oracle --run Saved/Tests/Skin325/skinplus_oracle.exe --report Saved/Logs/Skin325PixelOracle.json
python Tools/GamePoseProbe/report_skinplus_inputs.py Saved/Logs/Hair323Inventory.json Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Shaders/SkinPlusBindings.json Saved/Tests/Skin325/WantedNativeInputs.json
cargo test --release --lib --manifest-path Tools/ok-extract/Cargo.toml cached_skinplus_source_inputs -- --ignored --nocapture --test-threads=1
python Tools/GamePoseProbe/report_skinplus_inputs.py Saved/Logs/Hair323Inventory.json Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Shaders/SkinPlusBindings.json Saved/Tests/Skin325/WantedNativeInputs.json --cached Saved/Tests/Skin325/CachedInputs/inputs.json --output Saved/Logs/Skin325SourceCoverage.json
```

## 接續實作順序

1. 依 ChaControl → MaterialEditor 原生呼叫順序重現 body／face draw material：SetShader 的 defaults／保留值、card display values、ME float／colour／texture／ST；共用身份規則，沒有角色值補丁。
2. 補各 shader resource 的原始 PNG、row boundary、normal runtime encoding、sRGB／filter／wrap／mips；分辨 create RT 已 baked 的資料與 draw overlay，不能重複合成 lip／blush。
3. 移植 native VS、Outline／ShadowCaster 幾何與實際 COLOR attribute、世界基底／tangent／binormal；取 native globals 確認 keyword、主光、四燈、screen shadow。現在 GPU oracle 只覆蓋 pixel math，沒有驗證這些來源或完整 mips。
4. 接入完整 native SkinPlus adapter 後才開啟註冊，跑實際 native material／sampling／vertex controls，再做全34卡136圖數值與原尺寸目視回歸。全部通過才發布，不替换單張。

錯誤／反例紀錄見 `Docs/ERROR_LOG.md` E-201；最終來源索引 `Saved/Logs/Skin325SourceEvidence.json`。
