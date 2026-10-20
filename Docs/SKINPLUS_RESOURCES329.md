# Stage329 SkinPlus 靜態資源來源與共用傳遞

新 authored_textures 以 SkinPlus ShaderLab schema 的15個texture properties逐槽保留來源，兩材質30槽。Bound攜帶獨立PNG level0與 filter/wrapU/wrapV/sRGB/mip_count；Default保留 native white/black/bump token；Unresolved表示非空來源找不到貼圖或sampler，不能當空槽、借其他貼圖或填白色。transport_textures在任何Unresolved時先拒絕，不留下半套更新；成功則精確傳PNG與sampler，null清理舊binding。

ChaControl.cs:3368 ChangeTexture透過GetTexture後必定SetTexture，null也覆蓋原binding。replay_overlay_textures依ChaControl.ChangeSettingLip/EyeShadow/Nip/Underhair的403/401/423/424目錄與欄位，沿card KKEx resolver載入來源，id0不一律忽略，目錄的bundle/asset=0才是null。skin_catalog_texture保留資產自身取樣設定、依hiPoly取_low資產；找不到row/asset/sampler先報错，不冒充runtime成功。uncensor_draw_bundle共用原有selectedzip→loose mm_base來源選擇；跨zip優先序仍未全面證明。

Saved/Logs/Skin329AuthoredResources.json由實際Siesta selected head與selectedbody drawmaterial產生；獨立 verify_skinplus_resources.py重讀native materials.json。十靜態binding名稱、尺寸、filter、wrap、mip_count一致：body liquid、liquidmask、nip、underhair；face NormalMask、liquid、liquidnormal、liquidmask、lip、blush。另由Skin328NativeR2的原生activeTextureColorSpace確認body nip/underhair Linear、face lip/blush Gamma共四控制。

face lip512²／blush1024²未壓縮level0共1310720像素，原生匯出座標垂直翻轉後RGBA完全一致。PNG方向比對處理只在oracle；沒有改production貼圖或為單卡修圖。DXT5 nip/underhair原生PNG包含GPU BC decode與sRGB readback，尚未宣稱exact parity：nip直接flip最大75，轉readback色彩後仍各通道max[1,6,3,1]；underhairflip最大4。實際GL.sRGBWrite／Blit/ReadPixels路徑可見Probe.cs WriteTexture，不把顯示差異用productiongamma補丁消除。

五個資產錯接、sampler錯接、mipcount錯、遺失slot、sRGB錯反例全部拒絕。失敗資源無半套mutation的unit control、原numeric replay238項/328分量回歸與release cargo check通過。報告：Skin329ResourceAudit/ResourceNegatives/PixelDiagnostics/NumericAudit/SourceEvidence。

重現：
```powershell
cargo test --manifest-path Tools/ok-extract/Cargo.toml native_skinplus_authored_resource_transport -- --ignored --nocapture
python Tools/GamePoseProbe/verify_skinplus_resources.py --source Saved/Logs/Skin329AuthoredResources.json --output Saved/Logs/Skin329ResourceAudit.json
cargo test --manifest-path Tools/ok-extract/Cargo.toml skin_texture_transport_rejects_missing_assets_without_partial_mutation
cargo check --release --manifest-path Tools/ok-extract/Cargo.toml
```

此輪只完成靜態資源與overlay選圖來源／傳遞helper，未接正式session／UE renderer，不移除SkinPlus註冊保護。動態MainTex/NormalMap/NormalMapDetail/DetailMask/LineMask重建與完整MaterialEditor texture replacement replay仍開放；純PNG不含native mipchain，mip_count一致不能算mip內容已傳遞。十資源只有四個sRGB獨立native controls，其他取自authoredmetadata仍需驗證；sampler bias/aniso與PPtr跨bundle/duplicate name、Sideloader PNGoverride選擇也待補。UV0 heuristic、GPU default COLOR、normal/tangent/world basis、runtime keywords/global light/shadow、完整adapter與34卡回歸未完成。正式SESSION202/CLI313/cache102三binaries及136圖未動，部署前必須新identity。
