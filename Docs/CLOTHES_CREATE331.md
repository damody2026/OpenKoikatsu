# Stage331 原生衣服 Create RenderTexture 通解

## 根因與實作

Stage330 的 jacket max23、sailor max4、任意旋轉 max16 並非卡片參數例外。遊戲 QualitySettings 為 ForceEnable，原始 compressed textures 保留完整 mipchain；只用 PNG level0 與 CPU bilinear 無法重現硬體非等向性取樣。獨立 D3D11 oracle 使用同一原始 DXBC，bilinear 仍錯，硬體 anisotropic 才與原生完全相同。

新的 `clothes_gpu.cpp` / `clothes_gpu.rs` 直接執行來源 bundle 的 create_topN pixel DXBC，使用原始 RGBA8、BC1、BC3、BC7 資料及全部 mip、各資源 filter/wrap/aniso/bias、原生常數與 sRGB RT。使用硬體裝置，不用 dump 作 production 輸入、不按角色名修色。輸出只在完整 GPU readback 成功後交回既有流程，保留一致的垂直方向處理。

`unity.rs` 依來源讀原始 texture payload，並檢查 shader CB256、vector offsets/dimensions、資源與 sampler registers、keywords；partial override 中沒有此 shader 時按 asset 回落 vanilla。QualitySettings stripped native class 依 Unity2019.4 實際 layout 解析，拒絕混合品質模式、縮減 textureQuality、截斷或多餘資料。ForceEnable 對非 Point 且 aniso>0 套原生 clamp；Point 優先與 aniso0 例外均由原生控制驗證。

最終 R2 版本 SESSION209 / CLI `v-sacbody-320-native-clothes-argb` / bundle cache103（首輪 p208 因 ARGB32 coverage 缺口停止）。cache key 納入 GPU adapter/driver 及 globalgamemanagers/mm_base 來源 stamps；不能把不同裝置結果混用。UE build 已成功。R2 build 已包含 ARGB32 regression 與先前 test-only 反例，之後沒有修改 production 算式。

## 嚴格來源驗證

`python Tools/GamePoseProbe/verify_clothes_gpu.py` 重新讀取 source PNG 和獨立原生 GPU readback，完整 RGBA 最大差異要求 **0**。`Saved/Logs/Clothes331GpuPixelAuditR2.json` 保存實際路徑、方向、SHA256、像素數與差值。

- 三件男衣：jacket / shirt / sailor。
- 圖樣 shift / alpha / combined。
- Point aniso0/1、Bilinear aniso0、全域 Disable / Enable 五種控制。
- 女角江之岛盾子2靴子：舊 max6 的严格回歸現在 max0，沒有放寬原 max3 門檻。

R2 加入 ARGB32 衣服 RT 後，共13項、43,057,152 pixels，最大差異全部0。此證據是 Create RenderTexture，不是最終 Forward 光照 RGB parity。驗證裝置 `10de:2b85:89ec1043:161:driver9007199255733668`；WARP 與其他 GPU 不可由此宣稱 bit-exact。

`Clothes331SourceGpuR5.log` 在 p209 上重跑六個 wrapper GPU calls 加五個直接 GPU sampler controls；不能因 log 只印六次而猜五個 fallback。所有十一項 source 成功，無 CPU fallback。Quality ambiguous/truncated controls、shader ABI counterexamples、GPU partial-input negatives、女鞋嚴格回歸各一項 test PASS。反例拒絕非法 CB/vector/sampler/keyword/inline sampler、缺 payload、17mips、0width 與 broken DXBC。

## 保留的診斷與限制

獨立 oracle：`Tools/clothes_create_oracle.cpp`、`Tools/GamePoseProbe/prepare_clothes_oracle.py`。ShirtOracleR2 / JacketOracleR4 是成功完整取樣證據；早期 R1/R2/R3 失敗仍保留。原生 SamplerNativeR3 正確複製貼圖；SamplerNative 與 R2 的空 Texture2D 控制不得當成功。

PNG override、未支援的 texture format、非 Windows/缺硬體、無法嚴格判定品質或 shader ABI 時，明確記錄 `clothes CPU fallback`，不偽裝原生 GPU exact。無 ColorMask 的既有直接 MainTex 路徑保持來源讀取。目前原生 exact 證據涵蓋上述資產與控制，並非所有可安裝 mod。

完整34女角加3男角、148四視角回歸另記 `Clothes331All37` 報告。原136正式圖片先保持，整批完成後才給視覺與 structural acceptance。SkinPlus renderer integration、Siesta 頸暗等長期項目（背髮底色已有 Stage323 通用修正）、Random uncensor 全組合仍未由這次衣服 RT 證據解決。


Stage331 R2：首輪 p208 在 KK_381623 的 ARGB32 衣服發現明確 CPU fallback，主代理驗證 owned UE PID41684 後停止，保留不當 full37 PASS。補通用 ARGB32／BGRA32 完整 mip 通道轉換；五個來源貼圖符合獨立 decoder、256² 原生衣服 RT RGBA max0。正式新部署 SESSION209／CLI320-native-clothes-argb／cache103，Clothes331UEBuildR2 成功，新完整回歸 tag Clothes331All37R2。原12項像素證據仍對同一GPU算式；新增 Clothes331ArgbPixels。


快取預建只使用已觀察的 p209 env signature：GPU_SKIN／SKELMESH／NOPOSE／UE_ANIM=1，POSE=f_aruki_00_00，其餘key空。不改key、不搬舊payload。SQLite既有WAL與30sec busy timeout處理獨立卡片寫入。Clothes331PrewarmAudit 保存25後續卡的源log；UE每張仍逐view通過所有readiness/PV/attachment gate。若warm命中，來源診斷在CLI log中，不要求UE stderr重複印出每個male body選擇。
