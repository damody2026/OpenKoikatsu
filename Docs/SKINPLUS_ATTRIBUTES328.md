# Stage328 原生 vertex attribute 來源驗證

共用修正：Unity mesh UV1/2/3 按 serialized file 的 UnityVersion 與宣告 channel index 解碼。2018 以前 UV0 起於3，2018起於4；不可把缺失通道壓縮排序，也不可按範圍、跨度、零值數量猜通道或額外偏移。第四 UV 已移除 decode_overtex3_uv heuristic。float3/4 TexCoord 保留前兩分量，合法 UV 可等於1、超出[0,1]或非常小，不作截斷。

Siesta 選中的頭部 chara/head_mod.unity3d / p_cf_head_00 是5.6.2f1，身體來源為 UncensorSelector 選中的資產；來源解析仍沿既有 mesh parser。head 依 card KKEx 與原生 catalog resolver 選取，沒有卡名修圖分支。UV0 舊 heuristic 尚未全面移除，這輪僅證明兩個控制的 UV0 相符。

GamePoseProbe 新增 post-plugin sharedMesh normals/tangents/float COLOR/Color32/四UV dump；ProbeJson 增補 byte 序列化。第一次 Color32 的 {} 是 probe 缺少 byte 分支，不能拿來推論 production 顏色錯誤。第二次 capture Skin328NativeR2 才是 Color32 依據。兩次 runner 完整輸出53 renderer rows，但 owned Studio 延遲退出最終由 runner 停止，exit=-1；不宣稱正常退出0。

獨立 verify_skinplus_attributes.py：兩網格12518 vertices、100144 UV components，四UV逐項1e-6門檻；最大5.915069589690347e-8。9823身體Color32逐筆完全一致；face空 COLOR stream 與來源相符，但 GPU 未綁定COLOR的預設值仍未證明。normals/tangents已有 capture，尚未逐項來源／世界基底 parity。

重現：

```powershell
cargo test --manifest-path Tools/ok-extract/Cargo.toml skinplus_source_declared_vertex_attributes -- --ignored --nocapture
python Tools/GamePoseProbe/verify_skinplus_attributes.py --source Saved/Logs/Skin328SourceAttributes.json --native Saved/Tests/Skin328NativeR2 --output Saved/Logs/Skin328AttributeAudit.json
cargo test --manifest-path Tools/ok-extract/Cargo.toml declared_sparse_uv_channels_do_not_shift_or_guess_data
cargo test --manifest-path Tools/ok-extract/Cargo.toml mesh_float16_uv_streams_preserve_authored_coordinates
cargo check --release --manifest-path Tools/ok-extract/Cargo.toml
```

Skin328AttributeNegatives 四個錯接／少頂點／非有限／色彩破壞 report 必須拒絕。舊 R1/R2/R3/R4 log 保留，不能拿 source exporter 的 test PASS 冒充 native parity；獨立 audit 才是判定來源。

此輪 source 修正未部署正式 renderer；SESSION202／CLI313／cache102 三 binaries 與136官方截圖仍維持 Stage324。任何後續部署必须新 cache／pipeline identity。SkinPlus texture resources/samplers/mips、native default COLOR、tangent/world basis、global keywords/light/shadow、完整 adapter、34卡回歸與發布仍未完成；不宣稱 Siesta頸暗或全專案零bug。詳 Skin328SourceEvidence.json。
