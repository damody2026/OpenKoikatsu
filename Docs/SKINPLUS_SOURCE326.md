# Stage326 SkinPlus 來源材質回放

本階段完成來源數值／顏色／向量／ST 的回放與驗證，尚未接入 renderer。
正式 SESSION202／CLI313 與 Stage324 的136張截圖不變；不能用此報告宣稱頸暗、完整 SkinPlus 或全專案零 bug 已完成。

## 共用根因與修正

`ParsedBundle::material_named` 原本只讀 `m_Floats`／`m_TexEnvs`，完全漏讀 `m_Colors`。這同時遺失顏色和存放於該表的 Vector。
原生 `MaterialAPI.SetShader` 更換 shader 後保留材質已保存屬性，不是用新 ShaderLab defaults 重建整個材質。
讀取器現在保留所有已序列化顏色／向量，包括原 shader 未公開的屬性。

獨立 UnityPy `read_typetree` 核對 `chara/mm_base.unity3d` 的女性／男性 draw material。
女性資產原本就保存 `_EmissionColor=(0,0,0,1)`、`_ShadowColor=(1,.8963008,.836,1)`、`_LiquidTiling=(0,0,3.5,3.5)`。
這些是來源證據與回歸期望，production 沒有依卡名或材質名補上這些值。

新增卡片讀取欄位：body skinGlossPower／nailGlossPower、face detailPower／cheekGlossPower／lipGlossPower。
依原生 ChaFileBody／ChaFileFace constructor 使用缺欄位預設，並修正既有 body detailPower 的0.5預設為0。
最小空資料卡與 partial update 控制驗證缺欄位預設、已讀入值不被空更新重置。

`skin_source::replay` 的次序是來源 clone → ChaControl typed values／runtime status → SetShader 的保存值及缺屬性 defaults → XML explicit defaults → ME numeric／colour／ST。
Installed SkinPlus 的 XML 沒有 explicit DefaultValue，不能自行創造 XML 覆寫。
Body skinTuyaRate 與 face hohoAkaRate 由 caller 明確提供；本次 Studio controls 為0。
HohoAka 只改 draw `_overcolor2` alpha，cheekColor 是 create-face Color4；不可把 cheekColor RGB 再寫到 draw overlay。
Source object 保持不變；只接受正確 object／coordinate／slot／normalized material identity，未知屬性受 HasProperty gate 排除。

UncensorSelector 描述新增 oo_base.mm_base／material 的來源資訊與載入 helper，缺值依 sex 取原生 draw material。
目前 helper 讀 selected zipmod 或 loose abdata；尚未完整驗證任意跨 zipmod 依賴及 Sideloader override precedence。
Siesta 的 BP.profundis 仍選原始 mm_base，並非三個差值的原因；真正原因是漏讀 m_Colors。

## 驗證

- Siesta 原生 face／body：238項 numeric／colour／vector／texture ST axes、328分量，最大差1.2850341801762255e-7，嚴格門檻1e-6。
- 獨立 Python validator 重讀原生 dump，檢查完整屬性集合、每個 native 值與來源 SHA；女性 draw material 的所有16顏色／59 floats 與獨立 UnityPy 來源吻合。
- 來源回放兩項、卡片 partial update 一項、空卡 constructor 一項、Uncensor identity／metadata 兩項、原始 draw material 一項，七個專項 PASS。
- 另一次較寬的 skin_ ignored filter 包含既有 March 手動 skin 控制：四項 PASS。不能將此稱全 library suite。
- production cargo check PASS。未重建正式 CLI／UE，也沒有新 capture／publish。

## 重現

```powershell
cargo test --release --lib --manifest-path Tools/ok-extract/Cargo.toml skin_source::tests -- --ignored --nocapture --test-threads=1
cargo test --release --lib --manifest-path Tools/ok-extract/Cargo.toml skin_draw_numeric_partial_updates_preserve_loaded_fields -- --ignored --nocapture
cargo test --release --lib --manifest-path Tools/ok-extract/Cargo.toml native_skin_constructor_defaults_for_missing_card_blocks
cargo test --release --lib --manifest-path Tools/ok-extract/Cargo.toml uncensor_ -- --test-threads=1
cargo test --release --lib --manifest-path Tools/ok-extract/Cargo.toml mm_base_body_draw_material_is_resolved_independently_of_body_mesh
python Tools/GamePoseProbe/verify_skinplus_numeric.py --replay Saved/Logs/Skin326NumericReplay.json --wanted Saved/Tests/Skin325/WantedNativeInputs.json --authored Saved/Logs/Skin326AuthoredMaterialWitness.json --asset C:/portable/KoikatuSunshine/abdata/chara/mm_base.unity3d --output Saved/Logs/Skin326NumericAudit.json
```

## 仍需接入

`skin_source::replay` 與 Uncensor draw material helper 尚未被 production renderer 呼叫。
共用 reader 和 card parser 的修正已在 source，正式 binaries 未重建；下次部署必須提升 cache／pipeline identity，不能重用旧 p202 資料。
本次數值證據不涵蓋 render queue／keywords 最終 parity、貼圖像素／filter／wrap／mips、native VS／COLOR／Outline／ShadowCaster、scene-global lights／screen shadow 或最終 RGB。
七個非空資源的獨立 transport、完整 shader adapter 與34卡136圖回歸仍依 Stage325 的接續順序實作；完成前保留未移植註冊保護，不調色或按角色修圖。

來源索引 `Saved/Logs/Skin326SourceEvidence.json`；錯誤紀錄 `Docs/ERROR_LOG.md` E-202。Stage325 的歷史證據不覆寫。
