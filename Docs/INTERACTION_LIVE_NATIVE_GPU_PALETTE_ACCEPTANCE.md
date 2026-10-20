# Live native GPU palette 銜接驗收

## 實作
- UKKRuntimePoseableMeshComponent 新增 PublishExactSkinPalette／PublishNativeSkinPalette；直接發布 reference-to-local 矩陣，保留 shear、反射、零 scale，native source 不轉 TRS。
- Ordered slot adapter 一次轉換來源 palette；保留 signed vertex virtual slot、unweighted fallback 與原公式。16個native係數仍驗證 finite；未使用 homogeneous row 沿用原3x4轉換語意。
- 全部輸入驗證完成後才發布 owned snapshot／carrier；UE雙緩衝一併 finalize／flip。engine refresh 更新 carrier 時重新取消 carrier，不沿用舊 override。錯誤保留上次成功畫面，舊 render snapshot 不變。
- 資產更換／清除停止 direct palette；private palette 不混接 shared leader。bounds 使用同份發布矩陣。
- 修正 portable FOkSession::InstallSharedClip 成功後未標記 bSharedAnimation 的問題：直接安裝共用 bytes 後 ApplyAnimFrame 現在確實更新動畫。
- 回歸工具新增顯式 -PaletteCards；資料來自 APNG 與既有共用 walk_01.okanim，不讀源遊戲、不製作角色專用 assets。

## 最新編譯／回歸
InteractionLivePaletteAnimatedBuild.log：Result Succeeded，15.16秒。LivePaletteAnimatedRegression：18/18 Success，四個DLL穩定，externalUE0，exit0。

LiveNativePalette 以兩張真實 APNG、三個動畫時間點及 Unreal 實際 GetCurrentRefToLocalMatrices 比對独立原公式：

| APNG | 成功發布 | private發布 | 抽樣矩陣 | 最大係數誤差 | 未使用native row偏差 |
|---|---:|---:|---:|---:|---:|
| C001.png | 165 | 111 | 495 | 0 | 2.38419e-7 |
| InteractionMaleAPNGR65.png | 159 | 102 | 477 | 0 | 1.19209e-7 |

Native矩陣實際隨動畫時間改變（不是static/no-op）。每次抽樣三個ordered slots；此證據不能誇大為所有骨骼係數或新GPU renderer完整畫面比對。其餘回歸包含 carrier刷新、舊snapshot不可變、非finite／projective UE matrix／不完整binding／closed session拒絕、資產清除及更換、bounds包含source deformed vertices、shared leader、stream upload、嘴型與語音等。

## 功能與性能證據
PairEyesSpeedLivePaletteFinal（較早binary，對應Native3x4修正版）：functional_pass1，entry6454.638ms、firstpair73.927ms，12switch最大click18.593ms／最大frame33.591ms；13speed phase通過、callback最大0.062600ms；54嘴型／語音／walking loop／shared clock／eyes head belly／male replacement preservation／return與shadow oracle通過。五張原眼睛PNG已逐張檢視：正常open、tracking、closed、reopened、playing，stat fps也正常显示。混合階段最大1180.225ms仍保留，不能當作所有操作無卡頓證據。

PairEyesSpeedLivePaletteAnimated（最新binary）：continuous speed phase9預算未通過，functional_pass0。其餘倍率門檻通過；entry6041.018ms、firstpair71.663ms、12switch最大click34.056ms／frame60.670ms；callback最大0.098299ms。phase8/9/10 Advance6.970347／13.232908／12.940269ms、whole9.970352／19.179420／18.901708ms；paused phase11也12.484537ms，根因尚未確認，不能直接歸因於callback或用舊版PASS代替最新驗收。初始混合階段最大1288.401ms同樣不隱藏。

PairProfileLivePaletteAnimated（最新binary）：entry6318.114ms、firstpair238.837ms；quiet兩段whole9.785106／9.700491ms，實際Present102.189845／103.089836FPS，Advance6.843821／6.771707ms、GPU1.363138／1.361631ms。detail logging第三段13.141640ms不用來宣稱quiet效能。全幀8ms／Present120FPS仍FAIL。4DLL穩定／externalUE0；水槍SkipPaint，未測水槍。

同binary PairEyesSpeedLivePaletteAnimatedRepeat：functional_pass1，entry6213.607ms、firstpair82.590ms，12switch最大click23.513ms／最大frame41.288ms；13speed phase全部原門檻PASS、callback最大0.042800ms、68requests。54嘴型、語音、walking兩輪、shared clock／pause、camera head／eyes／belly、male replacement failure preservation、return與shadow oracle通過。五張最新眼睛原PNG逐張人工檢視正常，stat fps顯示正常；eye-visual-review.json保存。初始混合階段最大1224.309ms，不能宣稱所有背景操作小於300ms。首次continuous-speed失敗仍保留，重測PASS不是完全排除性能波動的證據。18項回歸、首次功能失敗、正常RHI profile及功能重測四輪的四DLL hash全部一致，externalUE0。沒有降低門檻，未測水槍；所有本輪build／audit已結束。

## 尚未完成的整合
現有男女互動 renderer 還未將 private 服裝／頭髮全數改成GPU geometry。直接palette介面是必要 bridge，尚無新renderer的像素驗收；原common Crowd male emblem recipe缺漏也沒有因matrix測試而解決。

native SkinPalette count/copy 每次仍有頂點weights／indices驗證，尚未達完整O(bones)求解／提交路線。後續須以source generation契約去除重複 immutable validation，建立原material／secondary normal-motion offsets／source binding的GPU等价路線，通過image oracle後才停CPU skin/packing。眼睛、任意morph、語音、shadow和按需CPU查詢維持原能力。runtime背景準備、shader ready後原子切換；不改APNG、不預烘焙角色專用資源。