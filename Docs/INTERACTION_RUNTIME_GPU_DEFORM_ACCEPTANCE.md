# Runtime GPU 頂點生成核心驗收

## 完成的實作
- 共用 KKCrowdGpuPacking 新增 RuntimeDeform compute shader／RDG pass，輸入為 owned immutable source、draw→source mapping、secondary offsets與本幀 affine palette。保留原float signed weights，不減面、不轉TRS／uint16權重。輸出包含position／normal／tangent與handedness；不觸碰原UV／color／indices／material。
- secondary draws 使用 UE FindBetweenNormals 同等的最短旋轉，包含相反方向、零rest normal。保留section原法線，不把outline偏移當普通affine向量蒙皮。
- 支援獨立 post-skin placement correction：先skin／normalize，再apply_mat／rot_normal；collapsed direction保留進入correction前的向量。不能合併為單一矩陣而丟失native零向量fallback。
- source一次驗證完整finite屬性／draw mapping／有效bone；palette每次驗證finite／affine後原子準備rows。pass拒絕非法correction、buffer stride／capacity或dispatch尺寸時不提交pass、不覆蓋舊GPU output。正式pass無CPU readback；readback只存在於測試。
- native增加ok_part_private_skin_stages與FOkSession::PrivateSkinStages。private原始skin palette、獨立correction、identity fallback一起匯出；成功才提交，capacity hint避免暖機後重複匯出。Close／move生命週期隔離與錯誤保留所有輸出已驗證。原flattened palette ABI／條件fallback保持原語意。

## 編譯與實際 GPU 證據
Saved/InteractionGpuDeformFinalBuild.log：明確Result Succeeded，14.61秒。加入接近反向測例後 Saved/InteractionGpuDeformOppositeProbeBuild.log 再次明確Result Succeeded，15.46秒。最新完整 GpuDeformStableOppositeRegression：20/20 Success、exit0、四個DLL穩定、external Unreal0。Shader-only 修正由此新 UE 程序實際編譯及 dispatch 驗證。

實際D3D RHI dispatch／readback：129 source vertices、387 draw vertices、3 correction cases（無post、shear post、全零scale post），共1161個輸出。包含signed／zero-sum／negative-sum／unweighted fallback、反射、零scale、opposite及near-opposite normal、zero rest normal、surface及兩種secondary draws。

| 比對值 | 最大誤差 | 原門檻 |
|---|---:|---:|
| position | 0.00000377855 cm | 0.0001 cm |
| normal | 0.000000419208 | 0.0001 |
| tangent | 0.0000000516468 | 0.0001 |

獨立CPU oracle使用scalar affine accumulation與實際UE FQuat::FindBetweenNormals／RotateVector，並非複製shader quaternion公式。handedness／position.W／normal.W metadata逐點一致。非法來源／palette／correction／draw count拒絕測試也通過。

GpuDeformNearOppositeProbe 曾失敗（position 0.0173789304cm／normal 0.0827190339），已保留原 log。FP32 `1+dot(A,B)` 在近反向處相消而跨過 UE 的 `1e-6` 門檻；修正採用單位向量等價式 `0.5*dot(A+B,A+B)`，不變更分支門檻與反向旋轉軸。最新全部回歸驗證修正成功。

## 實際 APNG native 契約
Rust release ignored fixture tests由顯式APNG路徑啟動，不依賴source game、不產生角色專用資產。三個private dynamics時間步，將staged矩陣重新計算之頂點／法線／切線對照原render_solved renderer（不另一次求解particles）：

| APNG | part比對 | 頂點比對 | FFI可匯出parts | 缺stream part次數 | 最大誤差 |
|---|---:|---:|---:|---:|---:|
| C001.png | 111 | 133440 | 37 | 3 | 0 |
| InteractionMaleAPNGR65.png | 102 | 174294 | 34 | 0 | 0 |

缺stream按原native輸出覆蓋：沒有tangent的part不製造不存在的native tangent；其positions與存在的normal仍比對。GPU source installer需提供原section authored屬性或保持原renderer，不能刪掉part。ABI不足capacity拒絕保留所有outputs、identity fallback、null／非法handles測試通過。

UE真實APNG＋共用walk_01三时间点：female111／male102次staged publication，組合回flattened native palette最大誤差0.00000562797／0.00000616133cm（浮點組合誤差，門檻0.001cm）；原direct bridge抽樣972矩陣誤差0且動畫矩陣實際改變。move後原session拒絕，移轉後正常，Close後保留最後輸出；舊來源不被重新讀檔。

## 完整場景與未完成項目
PairEyesSpeedGpuDeformCore：audit_completed1／functional_pass1，entry6114.198ms、首個ready walking8020.085ms、walking兩輪仍播放。首個ready pair73.745ms；12次ready switch最大click22.791ms／最大frame40.272ms，300ms門檻PASS。13速度階段及暫停原CPU／frame門檻全部PASS，68次調速、callback最大0.051301ms。54嘴型／語音、shared clock、head／eyes／belly、male replace及失敗保留、return與shadow depth oracle通過。五張眼睛原PNG逐張view_image確認正常，eye-visual-review.json保存；stat fps可見。

混合階段平均6.652ms不能當穩態120FPS證據；最大1185.751ms仍保留，不能宣稱全部背景操作小於300ms。部分baseline phase也有波動（1x phase6 Advance10.616348ms、whole15.061820ms），不直接歸因於倍率或shader核心。水槍SkipPaint，保留功能，不量效能。正常RHI PairProfileGpuDeformCore另行同binary取樣，完成後填入報告。

本輪完成的是GPU生成核心與native來源契約，尚未把private服裝／頭髮stream proxy正式切換為GPU output；所以沒有新renderer畫面oracle或120FPS達標的宣稱。仍需實作persistent UAV position／packed tangent buffers、原LocalVertexFactory／materials binding、barrier與draw順序、source revision／背景准备与原子切換、同palette bounds與CPU按需查詢，畫面oracle通過後才停止native CPU geometry。GPU本輪readback資料與APNG native oracle是兩種證據，不能混稱已完成實際APNG GPU draw。

PairProfileGpuDeformCore已完成、四DLLstable／external Unreal0／exit0。entry6186.986ms、first ready pair186.090ms。quiet兩段全幀17.156550／12.966999ms，Present58.298971／77.108305FPS；Advance11.897411／8.848360ms、GPU1.441245／1.459347ms、GT16.956802／12.525998ms。最大幀28.377298／336.226497ms，phase1也未通過300ms幀預算。verbose第三段14.230854ms不作quiet比較。8ms／120FPS與全部幀300ms仍FAIL；CPU／Render／RHI變動的根因尚未確認，不直接歸因於沒有接入場景的GPU shader或倍率。與前一輪約9.84ms差異不能作為新核心已提升／降低FPS的結論。

## 修正近反向計算後的最後完整功能驗收
`PairEyesSpeedStableGpuFinal`：audit_completed=1、functional_pass=1、exit=0、四個 DLL hashes stable、external Unreal=0。APNG entry 6164.855ms、walking first ready 8141.494ms（10秒內）、walking 兩輪正常；first pair ready 73.063ms。12 次 ready pose switches 最大 click-to-play 22.417ms、最大 frame 39.173ms，300ms 切換門檻通過。13 個速度階段含暫停／連續調速之原 CPU／frame budget 通過，69 次調速 callback 最大 0.041399ms；未宣稱每一幀耗時完全相同。
54 mouth patterns／speech active／voice cancel-and-replacement／paired shared clock／head-eyes-belly／male replacement-and-failure preservation／return／stable shadow depth oracle，完整 functional audit 通過。五張 PairEyesNeutral／Tracking／Closed／Reopened／Playing 原 PNG 已逐張 view_image 確認眼球／iris／highlight 可見、閉眼和睜眼正常，`eye-visual-review.json` 已保存。stat fps 可見；單張畫面的 129～137 FPS 是局部截圖數值，不能作為播放穩態120FPS證明。
混合階段 mean 6.777ms、P95約7.8ms、最大1273.247ms：包含載入／背景準備，最大值仍未達300ms，沒有以平均值掩蓋。調速各播放段 whole mean 約9.73～10.42ms，pause 約8.29ms；穩態120FPS仍未完成。新 GPU 核心尚未正式取代 private 衣髮 CPU geometry，不把本版功能 PASS 誤稱為 renderer 整合或性能目標 PASS。水槍仍 SkipPaint。
