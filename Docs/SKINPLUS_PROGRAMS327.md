# Stage327 SkinPlus 全部原生程式移植

已生成安裝版 `xukmi/SkinPlus` 的全部14組 D3D11程式：七個VS、七個PS。
本階段完成指令／binding與受控GPU對照，尚未接入實際 renderer 或宣稱完整場景像素一致。

## 本輪新增

- Outline VS，四個 Forward VS（基本／screen shadow／vertex lights／兩者），ShadowCaster SHADOWS_DEPTH／SHADOWS_CUBE VS。
- ShadowCaster SHADOWS_DEPTH／SHADOWS_CUBE PS。兩個原生PS都是 alpha mask cutoff 後輸出零 colour；不能自行加入猜測的 cube depth pack 公式。真正陰影深度還需VS／raster與場景接入。
- 共用 translator 支援 VS profile、複數 output registers、可選 return statement、浮點 ne all-one比較mask。原PS預設生成保持原樣，MainItemPlus五檔及原SkinPlus五PS＋schema共11檔 byte一致。

新源碼位於 `Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Shaders/SkinPlus*Vertex.hlsl`、`SkinPlusVertexBindings.json`、`SkinPlusShadowCaster{Depth,Cube}.hlsl`、`SkinPlusShadowBindings.json`。
生成器從原始 bytecode 取得立即數bits，從安裝版metadata取得CB registers／完整容量、uniforms、texture／sampler sites與keywords，從原始signature保存semantic／register／mask。
不依靠卡片名、截圖或手動位置／色值補丁。

## GPU驗證

`skinplus_vertex_oracle.cpp` 在D3D11 WARP分別执行原DXBC與生成VS。
同一個passthrough GS將已宣告的輸出分量寫入stream-output；每次確認SO statistics確實寫滿32個頂點，不能把空buffer當成PASS。
原signature中未宣告／未寫入的Outline normal／tangent等欄位不納入比較，也不虛構為零。

七組 ×32 CB controls ×32頂點＝7,168頂點、163,840分量，最大absolute error全0，門檻2e-5。
輸入包含四組不同UV、頂點RGBA、切線handedness、旋轉、非均勻縮放、鏡像scale、透視／正交VP、projection Y sign、主光point／directional狀態、正常及零shadow bias、Outline detail sample與width。
18個錯接反例全部預期FAIL：UV、object-world矩陣列、vertex colour、Outline sample座標與shadow normal-bias比較。

兩個ShadowCaster PS各64 controls／1024pixels，合計2048pixels，RGBA與discard完全吻合native。
8個UV／AlphaMask ST／sampler／取消discard反例全部預期FAIL，兩組都包含kept與discarded coverage。
前階段五PS用更新後共用runner重新驗證：320controls／5120pixels max0，21個原反例仍全部預期FAIL。
因此全部14程式都有GPU對照；不是原生材質貼圖、真實vertex stream、scene lighting／shadow或最終RGB parity。

## 測試器錯誤與反證

初版將blob11的keyword猜為SHADOWS_DEPTH／SHADOWS_NATIVE，metadata實際為SHADOWS_CUBE；生成器在不匹配時拒絕輸出完整report。
早期Generated資料夾殘留的錯誤名稱檔保留，但只有最後binding列出的七VS及兩PS會安裝／納入正式證據。

首次SO creation InvalidArgument，原因是把signature的packed register mask位置當成entry內的分量。
SO entry取整個semantic時StartComponent=0；register內zw的包裝位置不是StartComponent。
反射仍檢查分量數與contiguous mask。
參考 [Microsoft D3D11_SO_DECLARATION_ENTRY](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_so_declaration_entry)。

首次shadow-bias反例把ne與movc的非零測試同時反轉，兩者抵消而PASS；改只反轉ne。
這是錯誤的反例設計，不是production公式失敗，舊HLSL／PASS log保持原樣。

首次ShadowCaster PS coverage FAIL因沿用Forward輸入signature順序；native實際先TEXCOORD1、再SV_POSITION。
修正oracle VS／PS wrapper依原生signature配對，沒有修改原生PS運算或放寬門檻。

## 重現

```powershell
python Tools/skinplus_vertex_codegen.py Saved/Tests/Skin324NativeShader Saved/Tests/Skin327/Generated
clang++ Tools/skinplus_vertex_oracle.cpp -o Saved/Tests/Skin327/skinplus_vertex_oracle.exe -O2 -std=c++17 -ld3d11 -ld3dcompiler -ldxguid -lole32
python Tools/skinplus_vertex_oracle.py Saved/Tests/Skin327/Generated Saved/Tests/Skin324NativeShader Saved/Tests/Skin327/OracleFinal --run Saved/Tests/Skin327/skinplus_vertex_oracle.exe --report Saved/Logs/Skin327VertexOracle.json
python Tools/skinplus_program_codegen.py Saved/Tests/Skin324NativeShader Saved/Tests/Skin327/ShadowGenerated --shadow-casters
clang++ Tools/skinplus_oracle.cpp -o Saved/Tests/Skin327/skinplus_pixel_oracle.exe -O2 -std=c++17 -ld3d11 -ld3dcompiler -lole32
python Tools/skinplus_shadow_oracle.py Saved/Tests/Skin327/ShadowGenerated Saved/Tests/Skin324NativeShader Saved/Tests/Skin327/ShadowOracleFinal Saved/Tests/Skin327/skinplus_pixel_oracle.exe Saved/Logs/Skin327ShadowOracle.json
```

## 接續

1. 將Stage326来源material replay／texture resources接到body與face獨立路徑，分清create baked與draw overlay。
2. 驗證真實native COLOR／tangent／四UV、貼圖來源／row／normal encoding／sRGB／filter／wrap／mips與runtime global keywords。
3. 依已驗證VS／PS接入Unity／UE座標、camera/light matrices、world basis、Outline／ShadowCaster、main／point lights與screen shadow；不能直接改shader classification就當完成。
4. 提升runtime cache／pipeline identity，完成來源／GPU controls及全34卡136圖回歸後發布。

目前SESSION202／CLI313、正式binaries與136張圖片不變；Siesta側面頸暗及完整SkinPlus adapter仍開放。
錯誤紀錄E-203；來源索引`Saved/Logs/Skin327SourceEvidence.json`。不覆寫Stage325／326歷史snapshot。
