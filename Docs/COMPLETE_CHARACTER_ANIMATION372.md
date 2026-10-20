# Stage372 完整部件原作動畫

完整部件原作骨骼動畫已完成本輪回歸驗收。驗收範圍是下列男女原始卡、普通行走/站立與所有可見 renderer；不宣稱所有卡片、完整原遊戲 controller、最終 GPU 像素或 Shipping 打包均已驗證。

## 通解

保存原始 SMR slot 的幾何、權重及 bindPose，沿用 native effective_palette，包括私有髮骨、植入/刪除骨與完整掛點矩陣。任意 affine world*bind 矩陣用三個等權 rank-one TRS 骨表達，不把 shear 分解成單一 TRS。原作骨架保留原名及父子；額外 scene root 與 palette 骨供標準 Unreal 蒙皮。所有可見部件保存 SK，rigid 使用原始 rigid_follow 矩陣差，不按卡名或 role 猜掛點。

四個原始影響最多展開成 12 個，使用 16bit index/weight；各 slot 三影響相同，總和 65535。模型重建後逐頂點查 section BoneMap 驗證，避免 Unreal 8bit 預設量化破壞等權配置。保存全部區段、材質、四 UV、頂點顏色與 mesh-local morph。角度採連續 column swing，避免固定軸四元數表示在反向位置跳轉。

原卡 SHA1、renderer/index/role、幾何/權重/bind SHA256、實際使用 slot 和原骨架階層嚴格核對。來源載入以 scoped CPU SMR 模式保留原始資料，結束還原 renderer 環境；預覽的預烘焙頭部資料不能混用。初始化順序為 ApplyAnimFrame(0,0) → Skin → LoadSession。

## 最終證據

- BuildR12 actual0；MathR1 AutomationSuccess actual0，包含 affine/shear/reflection/zero scale 的真壓縮讀回及匯入環境完整還原。
- 男卡 ill_male_01.png：MaleCardR5，17 SM+17 SK、1661骨、108資產。女卡 KK_398530.png：FemaleCardR3，57 SM+57 SK、2561骨、359資產。所有可見部件都有 SK；原始667骨名/階層保留在 scene root 下。
- MaleCardVerifyR1/FemaleCardVerifyR1 actual0：79572/195914個 skin triangles；全部材質區段、四 UV、顏色、morph、原生 mip、參照與重複組裝檢查通過。
- 男行走 mc_m_walk_00_00_01 / 站立 m_tachi_00_02，女行走 f_aruki_00_00_01 / 站立 f_tachi_00_00；source皆studio/anime/00.unity3d，四clips共1607樣本。
- 最終動畫 MaleFloatR2/FemaleFloatR2，各5 uassets（兩個 UAnimSequence、兩個獨立壓縮設定、一個 library），只複製原始 TRS/四元數軌道，排除 Control Rig/Sequencer 物件圖。使用 translation/quaternion/scale ACF_None；新增匯入同用此設定。
- MaleNativeR5/FemaleNativeR3 actual0：真RHI、實際壓縮動畫、標準USkeletalMeshComponent CPUskin；每clip六個原始樣本時間、全17/57部件及所有sections，643392/1536012次頂點比較。最大誤差 .006479431/.001669370cm，均低於原先 .05cm 門檻，未放寬。
- MaleVerifyR1/FemaleVerifyR1：兩卡全部部件播放/中間時間seek、頂點有限值與有效compressed資料通過。
- DetachedR2 AutomationSuccess actual0：僅KKImportedAssets及輸出資產；無KKUnityBundle parser、無共用來源包。四clips、148 skin instances、363234vertices、每個renderer完整覆蓋、Unreal播放clock advances。
- MaleFreshR1 / FreshVerifyR1 actual0：使用最後一般匯入路徑，重新產生並重開原作行走動畫，17SK/53616vertices；不是僅離線重編碼測試。
- WrongCardR1、ExistingR1預期actual1：拒絕錯卡SHA1及已存在輸出，不產生錯卡資料、不覆寫資產。

## 在 Unreal 使用

透過既有 Tools 動畫匯入介面選擇原始 KKS 卡、匯入的 Character、動畫 Source 與 clips，輸出到新資料夾。較早只含11個SK的角色須重新匯入原始卡，才能完整播放衣服/髮/飾品。將 KKImportedCharacterActor 放入場景，指定 Character、AnimationLibrary、Outfit、AnimationIndex，勾選 PlaySkeletalAnimation；SetAnimationPosition 支援定位。

可直接檢查的最後 library：

- /Game/IndependentImports/Stage372MaleFloatR2/Animations.Animations
- /Game/IndependentImports/Stage372FemaleFloatR2/Animations.Animations

移到其他 UE5.8 專案時，攜帶 KKImportedAssets runtime plugin 與完整輸出資料夾，啟用 r.GPUSkin.UnlimitedBoneInfluences=True、r.GPUSkin.Support16BitBoneIndex=True。每個 SK 已保存16bit權重設定；runtime不讀原卡、zipmod、遊戲或來源包。隔離範例專案位於 Saved/Tests/AssetImport372/DetachedR2/DetachedAssets.uproject。

## 重現與事故

Tools/run_assets372.ps1 支援 Card / Animation / Verify / CardVerify / NativeVerify / Recompress。NativeVerify 保留原始 .05cm門檻，使用同renderer主surface計算section offset；不同初始化pose不得當作offset。Tools/run_detached_animations372.ps1 可建立fresh tag的隔離host。Tools/run_animation_math372.ps1 驗證矩陣與renderer環境。

Docs/ERROR_LOG.md E250 留存所有失敗：Modifier未析構、未初始化private rig、CPU/GPU來源hash不一致、CPUskin更新順序、8bit量化、oracle錯用初始化pose、有損動畫codec、整個duplicate動畫帶入editor依賴及nested codec outer載入循環。失敗log/asset/host皆保留。

Saved/Tests/AssetImport372/Audit.json 封存本輪來源snapshot、測試紀錄與最後資產。Stage370 Audit原JSON的SHA256仍為A971FD53B22E94CBB82DBC688CE7CBB9CFCBC896C846CBD6CF79539579DBD733。其570列重新核对，僅8個live MD/來源因後續工作正常變動，旧uassets和封存Sources/Binaries均一致；不重新封存或改舊Audit。
