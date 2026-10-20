# Stage354：原生旋轉碰撞器縮放通解

SESSION242 / CLI352 / CACHE108。原生 `DynamicBoneCollider.Collide` 使用 `m_Radius * Abs(Transform.lossyScale.z)`。此前 component 縮放已修正，但共用 collider 半徑仍取世界矩陣 z 欄長度；旋轉位於非均勻祖先下時，該長度混入剪切。

共用碰撞函式改由實際 Transform 階層計算半徑縮放。普通 authored、Ver02 共用碰撞呼叫及 character/native/generated/implanted 外部碰撞器均使用相同算法；外部 runtime tuple 同時保存世界矩陣與尺度，live 祖先更新時兩者同步刷新。未新增磁碟 schema，沒有卡別或動作補償，未移除配對 Transform guard。

原生 renderer-free 控制由五案擴充為九案：自由、球、膠囊、固定Z、非均勻、旋轉球、旋轉膠囊、旋轉內球與旋轉內膠囊。每案180幀，倍率1/.6/1.8/1，保留粒子歷史，共1,620幀與6,480粒子觀測；每幀完整6節點輸入/輸出。Rust 另以真正 `configure_character_colliders` 路徑比對外部碰撞器。

RedR1 舊算法 particle max0.009858282m、world0.009764581m、quat0.048158549，測試失敗exit101，保留報告。GreenR2及ExternalGreenR1通過原20µm/quat2e-5門檻：particle/previous7.365852µm、world1.309947µm、local0.301840µm、quat1.403689e-5。原生碰撞相對free的非零位移0.190528333m。GreenR3使用第二次原生擷取。原生兩次完整fixture均延遲Quit，核對owned PID/exe後終止，actualexit=-1，不能標clean exit0。

29項dbone測試/12ignored及既有原生飾品16觀測通過。測試log內RedR1/GreenR1舊固定文字`frames=900`是顯示錯誤，JSON實際rows=1620；後續改為實際count，不能只取log文字當覆蓋證據。

Editor build47.47秒Succeeded、CLI build36.09秒，actualexit皆0；LIB/EXE/Editor DLL部署352 marker。四卡一般走路960幀PASS455.48秒/actualexit0：兩靜態卡粒子0，兩物理女卡各48共享粒子，共23,040逐幀位置觀測，finite/isolation通過，private garment實卡粒子0。

UEGPU首跑exit1停在實卡載入，無TestCompleted、無fatal或新crash，stderr當時未保存；同時Rust GPU擷取正在執行，根因未確認，不將可能資源/快取競爭當定論。保留首跑失敗。runner改fresh tag並保存stdout/stderr，四卡完成後GPU R2與CPU R2單獨執行皆Success/actualexit0，各99衣服bounds觀測max0cm、實際renderer與雙actor隔離通過。串行成功不能追溯證明首跑原因。

詳本輪Audit.json與HANDOFF3 §46。保留舊stage封存，不重新簽署歷史報告。未驗收衣服穿插、所有Transform排列、Ver01/Ver02原生逐幀及整體場景零bug；不以finite/isolation當全需求完成。本輪未修改Animator路徑，三項Animator回歸沿用Stage353已通過證據，未冒稱本輪重跑。
