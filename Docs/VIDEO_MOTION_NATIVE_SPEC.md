# 影片轉動作原生化規格（GVHMR 推論管線逐層規格）

本文件描述目前 Python 管線（`Tools/video_motion/run_gvhmr.py`、`run_gvhmr_pair.py`、`pair_tracks.py`、`pair_align.py`、`import_video.py`）實際呼叫到的每一段運算，精確到可以用 Rust 重寫、不需要 Python。計畫與階段見 `Docs/VIDEO_MOTION_NATIVE_PLAN.md`。

依據的原始碼版本（C 端 `C:\portable\OpenKoikatsu\ref\GVHMR`）：

| 元件 | 版本／位置 |
| --- | --- |
| GVHMR | `ref/GVHMR/repo/hmr4d/...`，設定 `hmr4d/configs/demo.yaml` |
| ultralytics | 8.4.175（`.venv/Lib/site-packages/ultralytics`） |
| torch / torchvision | 2.11.0+cu128 / 0.26.0 |
| timm | 0.9.12 |
| opencv-python | 5.0.0.93 |
| pycolmap | 4.2.1 |
| PyAV | 13.0.0 |
| smplx | 0.1.28 |
| pytorch3d | `ref/GVHMR/p3d_shim/pytorch3d`（純 Python 子集） |

張量形狀以 `(…)` 表示；`L` = 影片幀數，`B` = 批次。所有網路推論都是 fp32（沒有 autocast，YOLO 權重存成 fp16，但 ultralytics 以 `half=False` 轉 fp32 執行）。權重鍵名與形狀是直接從 checkpoint 的 `data.pkl` 讀出來的（見第 9 節的讀法），不是推測。

---

## 0. 全管線資料流

```
來源影片 ──PyAV 解碼 rgb24──> libx264 crf23 重新編碼成 0_input_video.mp4（逐幀複製，標記 30fps）
   │
   ├─ YOLOv8x + TrackTrack（單人）／YOLOv8x predict（雙人 pair_tracks）→ bbx_xyxy (L,4)
   │     get_bbx_xys_from_xyxy(base_enlarge=1.2) → bbx_xys (L,3) = [cx, cy, s]
   ├─ ViTPose-H（半解析度幀 → 256×256 裁切 → 中央 256×192，flip test）→ kp2d (L,17,3)
   ├─ HMR2.0a ViT-H + 交叉注意力解碼器 token → f_imgseq (L,1024)
   ├─ SimpleVO（非 -s 時）：SIFT + FLANN + pycolmap 兩視圖幾何 → T_w2c (L,4,4) → R_w2c
   │     compute_cam_angvel → cam_angvel (L,6)；-s 時 R_w2c = I
   ├─ K_fullimg：estimate_K(W,H)（或 create_camera_sensor(W,H,f_mm)）(L,3,3)
   └─ GVHMR NetworkEncoderRoPE（12 層 RoPE transformer，512 寬）
         → EnDecoder.decode → 世界座標組裝 get_smpl_params_w_Rt_v2
         → pp_static_joint（或 pp_static_joint_cam）→ process_ik（CCD）
         → motion.npz
雙人：每人各跑 ViTPose / HMR2 / GVHMR，再 pair_align（剛體 Kabsch + SMPL-X 表面距離）
```

---

## 1. 影片解碼、幀率、相機內參、靜態／SimpleVO 分支

### 1.1 工作影片 `0_input_video.mp4`

`run_gvhmr.py` 與 `run_gvhmr_pair.py`：

```python
reader = get_video_reader(video)            # iio.imiter(path, plugin="pyav") → 每幀 (H,W,3) uint8 RGB
writer = get_writer(video_path, fps=30, crf=23)  # libx264，options {"crf": "23"}
for img in reader: writer.write_frame(img)
```

重點：

- **沒有重新取樣**。每個來源幀變成一個輸出幀，只是時間戳改成 30 fps。60 fps 來源會變成 2 倍慢動作的 30 fps 序列；`motion.npz` 的 `fps` 永遠寫 `30.0`。原生版若要「相同結果」就照做；若要修正，應在解碼時重取樣到 30 fps（GVHMR 訓練資料是 30 fps，`local_transl_vel` 與 `cam_angvel` 都是每幀量）。
- 重新編碼是有損的（crf 23）。之後所有階段讀的都是這個重新編碼後的檔案，不是原始檔。原生版可以直接用原始解碼幀（誤差見第 8 節容忍度）；若要逐位元對齊黃金資料，就要同樣先經 libx264 crf 23。
- 解碼色彩轉換：PyAV `to_ndarray(format="rgb24")` 走 libswscale，色彩矩陣依串流標記（未標記時 BT.601 limited range）。原生版用 `ffmpeg -pix_fmt rgb24 -f rawvideo` 會得到相同結果（同一個 swscale）。
- `get_video_lwh(path)` 回傳 `(L, W, H)`，取自 `iio.improps(path, plugin="pyav").shape = (L,H,W,3)`。
- 若重跑時工作影片幀數與來源不同，就重做。

### 1.2 其他階段讀影片的方式

| 讀取者 | 方式 | 顏色 | 解析度 |
| --- | --- | --- | --- |
| ViTPose、HMR2（`get_batch`） | `read_video_np(path, scale=0.5)` = `iio.imread(plugin="pyav", filter_sequence=[("scale","iw*0.5:ih*0.5")])` | RGB | ffmpeg `scale` 濾鏡，預設 swscale **bicubic**，尺寸 `iw*0.5` 取整數（截斷） |
| SimpleVO | 同上，`scale=0.5` | RGB | 同上 |
| ultralytics YOLO | `cv2.VideoCapture`（OpenCV FFmpeg 後端） | **BGR**，前處理時翻成 RGB | 原解析度 |
| `pair_tracks.shot_cuts` | PyAV `frame.to_image()` → PIL `convert("L")` → `resize((64,36))`（PIL 預設 BICUBIC） | 灰階 | 64×36 |

注意 ViTPose 與 HMR2 各自整支解碼一次（相同結果），SimpleVO 再解一次。

### 1.3 相機內參 `K_fullimg`

```python
estimate_K(W, H):            f = sqrt(W² + H²)   # 對角 FOV ≈ 53.13°
                             K = [[f,0,W/2],[0,f,H/2],[0,0,1]]  (float32)
create_camera_sensor(W, H, f_mm):  f = sqrt(W²+H²) / sqrt(24²+36²) * f_mm
```

`K` 以 `.repeat(L,1,1)` 展成 `(L,3,3)`。`f_mm` 只在使用者給 `--f_mm` 時用。

### 1.4 靜態鏡頭與 SimpleVO 分支

- `-s/--static_cam`：`R_w2c = eye(3).repeat(L,1,1)`；不跑 SimpleVO；GVHMR 後處理改用 `pp_static_joint_cam`。
- 否則：`SimpleVO(video_path, scale=0.5, step=8, method="sift", f_mm=cfg.f_mm).compute()` 回傳 `(L,4,4)` float64，取 `[:, :3, :3]` 作 `R_w2c`。雙人版若 SimpleVO 丟例外就退回單位矩陣（仍用非靜態後處理）。
- **焦距不一致**：SimpleVO 在 `f_mm is None` 時用 `f_mm = 24`（`focal_length_from_mm`，`f = diag/43.267*24 ≈ 0.5547·diag`，半解析度），而 GVHMR 本體用 `estimate_K`（`f = diag`）。這是原始碼行為，原生版照抄才會一致。

---

## 2. 人物偵測與追蹤（YOLOv8x）

### 2.1 前處理（ultralytics `LetterBox` + `BasePredictor.preprocess`）

- 輸入：OpenCV 讀出的 BGR uint8 幀 `(H,W,3)`。
- `imgsz = 640`，`stride = 32`，`auto = True`（`model.predict/track` 預設 `rect=True`、單一影片尺寸固定、`.pt` 模型），`scaleup = True`，`center = True`，`padding_value = 114`。
- `r = min(640/H, 640/W)`；`new_unpad = (round(W·r), round(H·r))`；`dw = 640 - new_w`，`dh = 640 - new_h`；auto 時 `dw %= 32`、`dh %= 32`；`dw/=2`、`dh/=2`；`top = round(dh-0.1)`，`bottom = round(dh+0.1)`，`left = round(dw-0.1)`，`right = round(dw+0.1)`（Python `round` 是銀行家捨入）。
- `cv2.resize(img, new_unpad, interpolation=cv2.INTER_LINEAR)`（尺寸不同時），再 `cv2.copyMakeBorder(..., BORDER_CONSTANT, value=(114,114,114))`。
- `HWC→CHW`、通道翻轉 BGR→RGB、`/255.0`，fp32。例：1920×1080 → 640×360 → 補成 640×384。

### 2.2 網路結構（`yolov8x.yaml`，`depth_multiple=1.0`、`width_multiple=1.25`）

基本單元：

- `Conv(c1,c2,k,s)` = `Conv2d(c1,c2,k,s,padding=k//2,bias=False)` → `BatchNorm2d(eps=0.001, momentum=0.03)` → `SiLU`。鍵：`*.conv.weight (c2,c1,k,k)`、`*.bn.{weight,bias,running_mean,running_var,num_batches_tracked}`。推論時 ultralytics 會把 BN 融進卷積（數值等價）。
- `Bottleneck(c,c,shortcut,k=(3,3),e=1.0)`：`cv1 Conv(c,c,3)` → `cv2 Conv(c,c,3)`，`add` 時加殘差。
- `C2f(c1,c2,n,shortcut)`：`c = c2/2`；`y = cv1(x)`（`Conv(c1,2c,1)`）切成兩半 `[y0,y1]`；依序 `y_{k+1} = m[k](y_k)` 共 n 個，全部 concat（`(2+n)·c` 通道）→ `cv2 Conv((2+n)c, c2, 1)`。
- `SPPF(c1,c2,k=5)`：`cv1 Conv(c1,c1/2,1)`（這個 checkpoint 的 `cv1.act` 是 SiLU，物件是舊版 pickle）→ 連續三次 `MaxPool2d(5,1,2)`，四者 concat → `cv2 Conv(2c1, c2, 1)`。無殘差（pickle 內沒有 `add`、`n` 屬性，`getattr` 預設 `n=3`、`add=False`）。
- `Upsample(scale_factor=2, mode="nearest")`；`Concat(dim=1)`。

逐層（索引即鍵名 `model.model.<i>`，已用 checkpoint 確認）：

| i | 模組 | 輸入 | 輸出通道 | 備註 / 代表鍵與形狀 |
| --- | --- | --- | --- | --- |
| 0 | Conv k3 s2 | 影像 | 80 | `0.conv.weight (80,3,3,3)` P1/2 |
| 1 | Conv k3 s2 | 0 | 160 | `(160,80,3,3)` P2/4 |
| 2 | C2f n=3 shortcut | 1 | 160 | `2.cv1 (160,160,1,1)`、`2.cv2 (160,400,1,1)`、`2.m.k.cv{1,2} (80,80,3,3)` |
| 3 | Conv k3 s2 | 2 | 320 | P3/8 |
| 4 | C2f n=6 shortcut | 3 | 320 | `4.cv2 (320,1280,1,1)` |
| 5 | Conv k3 s2 | 4 | 640 | P4/16 |
| 6 | C2f n=6 shortcut | 5 | 640 | `6.cv2 (640,2560,1,1)` |
| 7 | Conv k3 s2 | 6 | 640 | P5/32 |
| 8 | C2f n=3 shortcut | 7 | 640 | `8.cv2 (640,1600,1,1)` |
| 9 | SPPF k5 | 8 | 640 | `9.cv1 (320,640,1,1)`、`9.cv2 (640,1280,1,1)` |
| 10 | Upsample ×2 | 9 | 640 | |
| 11 | Concat | [10, 6] | 1280 | |
| 12 | C2f n=3 無殘差 | 11 | 640 | `12.cv1 (640,1280,1,1)` |
| 13 | Upsample ×2 | 12 | 640 | |
| 14 | Concat | [13, 4] | 960 | |
| 15 | C2f n=3 無殘差 | 14 | 320 | **P3 輸出**，`15.cv1 (320,960,1,1)` |
| 16 | Conv k3 s2 | 15 | 320 | |
| 17 | Concat | [16, 12] | 960 | |
| 18 | C2f n=3 無殘差 | 17 | 640 | **P4 輸出** |
| 19 | Conv k3 s2 | 18 | 640 | |
| 20 | Concat | [19, 9] | 1280 | |
| 21 | C2f n=3 無殘差 | 20 | 640 | **P5 輸出** |
| 22 | Detect | [15,18,21] | — | `nc=80`、`reg_max=16`、`stride=[8,16,32]` |

Detect（每個尺度 i）：

- 框分支 `cv2.i`：`Conv(ch,80,3)` → `Conv(80,80,3)` → `Conv2d(80,64,1)`（有 bias）。鍵 `model.model.22.cv2.0.0.conv.weight (80,320,3,3)`、`22.cv2.0.2.weight (64,80,1,1)`、`22.cv2.0.2.bias (64)`。
- 類別分支 `cv3.i`：`Conv(ch,320,3)` → `Conv(320,320,3)` → `Conv2d(320,80,1)`。鍵 `22.cv3.0.2.weight (80,320,1,1)`。
- `dfl.conv.weight (1,16,1,1)` = `arange(16)`（固定）。

解碼：

1. 每尺度 `boxes (B,64,h·w)`、`scores (B,80,h·w)`，依尺度順序串接成 `A = Σ h·w` 個 anchor。
2. `make_anchors(feats, strides, 0.5)`：每格中心 `(x+0.5, y+0.5)`（格子單位），row-major（先 y 後 x）。
3. DFL：`boxes.view(B,4,16,A)` 對 16 個 bin 做 softmax，與 `0..15` 求期望 → `(B,4,A)` = 距離 `(l,t,r,b)`（格子單位）。
4. `dist2bbox(xywh=True)`：`x1y1 = anchor - lt`，`x2y2 = anchor + rb`，輸出 `[(x1+x2)/2, (y1+y2)/2, x2-x1, y2-y1]`，乘 `stride`（像素）。
5. `scores.sigmoid()`；輸出 `(B, 84, A)`。

### 2.3 NMS（`ultralytics/utils/nms.py::non_max_suppression`）

- 參數：`conf_thres` = 單人 0.5（`Tracker.track` 傳入），雙人 0.3（`pair_tracks.detect`）；`iou_thres = 0.7`；`classes=[0]`；`agnostic=False`；`multi_label=False`；`max_det=300`；`max_nms=30000`；`max_wh=7680`。
- 流程：候選 = 任一類別分數 > conf；`xywh→xyxy`；每框取**最高類別**（`cls.max`）且 > conf；**之後**才過濾 `class == 0`（所以「人」必須是該框的最高類別）；超過 30000 依分數取前 30000；框加上 `class·7680` 偏移後呼叫 `torchvision.ops.nms(boxes, scores, 0.7)`（貪婪 NMS，IoU 嚴格大於 0.7 抑制）；取前 300。
- `scale_boxes`：`gain = min(h1/h0, w1/w0)`；`new_h = round(h0·gain)`、`new_w = round(w0·gain)`；`pad_x = round((w1-new_w)/2 - 0.1)`、`pad_y = round((h1-new_h)/2 - 0.1)`；`x = (x - pad_x)/(new_w/w0)`、`y = (y - pad_y)/(new_h/h0)`；再裁切到 `[0,W]×[0,H]`。

### 2.4 追蹤器（單人 `Tracker`）

`hmr4d/utils/preproc/tracker.py` 呼叫 `self.yolo.track(video_path, device="cuda", conf=0.5, classes=0, verbose=False, stream=True)`，**沒有指定 tracker**，ultralytics 8.4.175 的預設是 `tracktrack.yaml`（不是舊版的 botsort）：

```yaml
tracker_type: tracktrack
track_high_thresh: 0.6     track_low_thresh: 0.25    new_track_thresh: 0.7
track_buffer: 30           match_thresh: 0.7         lost_match_thr: 0.0
iou_weight: 0.5  reid_weight: 0.5  conf_weight: 0.1  angle_weight: 0.05
penalty_p: 0.2   penalty_q: 0.4    reduce_step: 0.05
tai_thr: 0.55    min_track_len: 3
gmc_method: sparseOptFlow          with_reid: False
```

TrackTrack（`ultralytics/trackers/track_tracker.py`）要點：

- 偵測分三組：`high`（score ≥ 0.6）、`low`（0.25 < score < 0.6，因 conf=0.5 只剩 0.5–0.6）、`recovered`（`dets_del`：同一份原始輸出以 NMS IoU 0.95 再跑一次，與緊 NMS 結果最大 IoU < 0.97 且 score > 0.6 的框）。
- 卡爾曼 `KalmanFilterXYWH`（狀態 `[cx,cy,w,h,vx,vy,vw,vh]`，`std_weight_position=1/20`、`std_weight_velocity=1/160`，量測雜訊乘 `max(1-score, 0.05)`，NSA-Kalman）。
- GMC `sparseOptFlow`：灰階（BGR2GRAY）、縮 1/2、`goodFeaturesToTrack(maxCorners=400, qualityLevel=0.01, minDistance=0, blockSize=3)`、`calcOpticalFlowPyrLK`、`estimateAffinePartial2D(RANSAC)`，平移 ×2；對所有軌跡的均值與共變異做仿射。
- 成本：`1 - HIoU·IoU`（HIoU = 高度方向 IoU），加 `0.1·|預測分數 - 偵測分數|`（預測分數 = 2·score - prev_score），IoU > 0.1 的配對再加 `0.05·角度距離·det_score`；IoU ≤ 0.1 設為 1；low 組加 0.2，recovered 加 0.4；裁到 [0,1]。
- 迭代指派 `_iterative_associate`：互為最近且成本 < 門檻即配對，門檻每輪減 0.05。
- 新軌跡：未配對的 high 偵測經 Track-Aware NMS（score > 0.7，與已追蹤框 IoU ≤ 0.55，彼此 IoU > 0.55 抑制）後啟動；需 `min_track_len=3` 幀才 `is_activated`（第 1 幀例外）。
- 每幀輸出 `track.result` 的框是**卡爾曼狀態**（`mean[:4]` 轉 xyxy），不是原始偵測框。尚未確認的軌跡不輸出（該幀 `boxes.id` 為 None → 視為無偵測）。

原生化建議：TrackTrack 完整重寫（含 GMC、兩次 NMS）成本高。GVHMR 只取一條軌跡並做兩次 5 點移動平均，因此用較簡單的追蹤器（例如 ByteTrack 式 IoU 指派 + 同樣的卡爾曼濾波）在單人影片中差異很小；但要與黃金資料逐幀比對時，框的差異容忍度要放寬（第 8 節）。雙人流程（`pair_tracks.py`）完全不用追蹤器。

### 2.5 主軌跡選取與框平滑（`Tracker.get_one_track`）

1. `track_history[f] = [{id, bbx_xyxy}]`。
2. 每個 id 的面積和：`Σ_f (w·h)/(W·H)`；**依面積和由大到小排序取第一名**（程式也算了軌跡長度排序，但沒有用到）。
3. 缺幀：`frame_id_to_mask` → `rearrange_by_mask`（缺的填 0）→ `linear_interpolate_frame_ids`：每段連續缺幀，若在序列開頭就用段後第一個有效值、在結尾就用段前最後一個有效值，否則 `prev + linspace(0,1,n+2)[1:-1]·(next-prev)` 線性內插。
4. `moving_average_smooth(window_size=5, dim=0)` 做兩次：每個座標沿時間做 `[1,1,1,1,1]/5` 卷積，兩端 `replicate` 補 2。
5. 輸出 `bbx_xyxy (L,4)` float32。

### 2.6 `get_bbx_xys_from_xyxy(bbx_xyxy, base_enlarge=1.2)`

```
cx = (x1+x2)/2, cy = (y1+y2)/2, w = x2-x1, h = y2-y1
若 w > 0.75·h: h = w/0.75        # 192/256 長寬比
若 w < 0.75·h: w = 0.75·h
s = max(h, w) · 1.2              # 等價於 1.2·max(h, w/0.75)
bbx_xys = [cx, cy, s]  (L,3) float32
```

`bbx.pt` 存 `{"bbx_xyxy": (L,4), "bbx_xys": (L,3)}`。

---

## 3. ViTPose-H 2D 關節（`VitPoseExtractor`）

### 3.1 裁切（`vitfeat_extractor.get_batch`，與 HMR2 共用）

輸入：半解析度 RGB 幀 `imgs (L,H/2,W/2,3)` uint8，`bbx_xys`（全解析度）。

1. 抗鋸齒：`d = s·0.5/256/2`；若 `d > 1.1`：`cv2.GaussianBlur(img, (5,5), sigmaX=(d-1)/2)`（sigmaY=0 → 同 sigmaX，邊界 `BORDER_REFLECT_101`）。
2. `crop_and_resize(img, c·0.5, s·0.5, dst_size=256, enlarge_ratio=1.0)`：`hs = s·0.5/2`；
   `src = [[cx-hs, cy-hs], [cx+hs, cy-hs], [cx, cy]]`（半解析度座標），
   `dst = [[0,0], [255,0], [127.5,127.5]]`；`A = cv2.getAffineTransform(src, dst)`；`cv2.warpAffine(img, A, (256,256), flags=INTER_LINEAR)`（邊界常數 0）。等價於縮放 `255/(s·0.5)` 的等向仿射。
3. 正規化：`(img/255 - [0.485,0.456,0.406]) / [0.229,0.224,0.225]`，RGB，`(L,3,256,256)` fp32。
4. 回傳的 `bbx_xys` 就是輸入值（`[c·0.5, s·0.5]/0.5`）。

ViTPose 輸入取中央 192 欄：`imgs[:, :, :, 32:224]` → `(B,3,256,192)`，批次 16。

### 3.2 骨幹 ViT-H/16（`vitpose_pytorch/.../backbones/vit.py`，與 HMR2 骨幹相同）

| 參數 | 值 |
| --- | --- |
| `img_size` | (256, 192) |
| `patch_embed.proj` | `Conv2d(3,1280,k=16,s=16,padding=2)`，有 bias。輸入 256×192 → 輸出 16×12（`floor((256+4-16)/16)+1 = 16`、`floor((192+4-16)/16)+1 = 12`），最右 2 欄、最下 2 列像素實際沒被用到 |
| token 數 | 192，flatten 順序 row-major（h 再 w） |
| 位置編碼 | `pos_embed (1,193,1280)`；`x = x + pos_embed[:,1:] + pos_embed[:,:1]`（cls 位置向量加到每個 token；沒有 cls token） |
| 深度／寬度 | 32 個 block，`embed_dim=1280`，`num_heads=16`（head_dim 80），`mlp_ratio=4`（5120） |
| Block | pre-norm：`x += attn(LN1(x))`；`x += mlp(LN2(x))`；`LayerNorm(eps=1e-6)` |
| Attention | `qkv = Linear(1280,3840,bias=True)`，reshape `(B,N,3,16,80)`；`q·80^-0.5`；softmax；`proj Linear(1280,1280)`；無遮罩 |
| MLP | `fc1 1280→5120`、`GELU`（erf 精確版）、`fc2 5120→1280` |
| 結尾 | `last_norm LayerNorm(1280, eps=1e-6)`；reshape 成 `(B,1280,16,12)` |
| drop_path 0.55 | 推論無作用 |

鍵（`vitpose-h-multi-coco.pth`，`state_dict`）：

```
backbone.pos_embed                    (1,193,1280)
backbone.patch_embed.proj.weight      (1280,3,16,16)   .bias (1280)
backbone.blocks.{0..31}.norm1.weight/bias            (1280)
backbone.blocks.{i}.attn.qkv.weight   (3840,1280)      .bias (3840)
backbone.blocks.{i}.attn.proj.weight  (1280,1280)      .bias (1280)
backbone.blocks.{i}.norm2.weight/bias                (1280)
backbone.blocks.{i}.mlp.fc1.weight    (5120,1280)      .bias (5120)
backbone.blocks.{i}.mlp.fc2.weight    (1280,5120)      .bias (1280)
backbone.last_norm.weight/bias        (1280)
```

### 3.3 熱圖頭 `TopdownHeatmapSimpleHead`

```
ConvTranspose2d(1280,256,k=4,s=2,p=1,output_padding=0,bias=False)  keypoint_head.deconv_layers.0.weight (1280,256,4,4)
BatchNorm2d(256, eps=1e-5)                                          keypoint_head.deconv_layers.1.{weight,bias,running_mean,running_var}
ReLU
ConvTranspose2d(256,256,4,2,1,bias=False)                           keypoint_head.deconv_layers.3.weight (256,256,4,4)
BatchNorm2d(256)                                                    keypoint_head.deconv_layers.4.*
ReLU
Conv2d(256,17,k=1)                                                  keypoint_head.final_layer.weight (17,256,1,1) .bias (17)
```

輸出 `(B,17,64,48)`（16×12 → 32×24 → 64×48）。注意 ConvTranspose 權重排列是 `(in, out, kh, kw)`。

### 3.4 Flip test

```
heat, heat_f = pose(cat([x, x.flip(3)])).chunk(2)
heat_f: 交換 COCO 左右對 [1,2],[3,4],[5,6],[7,8],[9,10],[11,12],[13,14],[15,16]，再 flip(3)（水平）
heat = (heat + heat_f) * 0.5        # 沒有 shift_heatmap
```

### 3.5 熱圖解碼（mmpose `keypoints_from_heatmaps(use_udp=True, post_process="default", kernel=11, target_type="GaussianHeatmap")`）

1. `_get_max_preds`：每個熱圖 argmax（numpy 取第一個最大值，row-major），`px = idx % 48`、`py = idx // 48`，`maxval = max`；`maxval ≤ 0` 時座標設 -1。
2. `post_dark_udp(preds, heatmaps, kernel=11)`（就地修改熱圖副本）：
   - 每張 `cv2.GaussianBlur(hm, (11,11), 0)`（sigma 由 OpenCV 依 ksize 推得 = 0.3·((11-1)·0.5-1)+0.8 = 2.0，`BORDER_REFLECT_101`）；
   - `clip(0.001, 50)`，`log`；
   - 每邊 `edge` 補 1 格；在整數峰值 `(x,y)` 取：
     `dx = 0.5(I[x+1,y]-I[x-1,y])`、`dy = 0.5(I[x,y+1]-I[x,y-1])`、
     `dxx = I[x+1,y]-2I+I[x-1,y]`、`dyy = I[x,y+1]-2I+I[x,y-1]`、
     `dxy = 0.5(I[x+1,y+1]-I[x+1,y]-I[x,y+1]+2I-I[x-1,y]-I[x,y-1]+I[x-1,y-1])`；
   - `coords -= inv(H + ε·I₂)·[dx,dy]ᵀ`，`ε = float32 eps (1.1920929e-07)`。
3. `transform_preds(use_udp=True)`，`center = bbx[:2]`，`scale·200 = (0.75·s, s)`，熱圖大小 `[W,H]=[48,64]`：
   `x = px·(0.75s/47) + cx - 0.375s`，`y = py·(s/63) + cy - 0.5s`。
4. `kp2d = concat(preds, maxvals)` → `(L,17,3)` float32，`[x, y, conf]`，全解析度像素；conf 是**模糊前**的平均熱圖最大值。

---

## 4. HMR2 影像特徵（`Extractor`）

- 裁切與正規化同 3.1（共用 `get_batch`，256×256），批次 16。
- `HMR2.forward(batch, feat_mode=True)`：`x = img[:, :, :, 32:-32]` → `(B,3,256,192)`；骨幹同 3.2（ViT-H/16，`backbone.*`，結構與鍵名完全相同，權重不同）；輸出 `(B,1280,16,12)` → `rearrange "b c h w -> b (h w) c"` → context `(B,192,1280)`。
- `SMPLTransformerDecoderHead`（`MODEL.SMPL_HEAD`：`TRANSFORMER_INPUT` 預設 `zero`，所以 `token_dim=1`）：

| 部件 | 規格 | 鍵（前綴 `smpl_head.transformer.`） |
| --- | --- | --- |
| 輸入 token | `zeros(B,1,1)` → `to_token_embedding Linear(1,1024)`（結果就是 bias） | `to_token_embedding.weight (1024,1)`、`.bias (1024)` |
| 位置 | `+ pos_embedding (1,1,1024)`（dropout 無作用） | `pos_embedding (1,1,1024)` |
| 6 層 `TransformerCrossAttn` | 每層三個 PreNorm 殘差：self-attn、cross-attn、FF；`LayerNorm(1024, eps=1e-5)` | `transformer.layers.{0..5}.{0,1,2}.norm.weight/bias (1024)` |
| self-attn | heads 8、dim_head 64（inner 512）；`to_qkv Linear(1024,1536,bias=False)`；`·64^-0.5`；`to_out.0 Linear(512,1024)` | `layers.i.0.fn.to_qkv.weight (1536,1024)`、`layers.i.0.fn.to_out.0.weight (1024,512)`、`.bias (1024)` |
| cross-attn | `to_q Linear(1024,512,bias=False)`；`to_kv Linear(1280,1024,bias=False)` 作用在**未正規化**的 context；`to_out.0 Linear(512,1024)` | `layers.i.1.fn.to_q.weight (512,1024)`、`layers.i.1.fn.to_kv.weight (1024,1280)`、`layers.i.1.fn.to_out.0.*` |
| FF | `net.0 Linear(1024,1024)` → GELU（erf）→ `net.3 Linear(1024,1024)` | `layers.i.2.fn.net.0.*`、`layers.i.2.fn.net.3.*` |

- 輸出特徵：最後一層後的 token（**沒有**最終 LayerNorm），`squeeze(1)` → `(B,1024)`。`f_imgseq = (L,1024)` float32。
- 只有 1 個 query token，self-attn 的 softmax 恆為 1（輸出 = v 投影），可照算不必特判。
- `decpose/decshape/deccam`、`init_*`、`smpl.*`、`discriminator.*` 在特徵模式不用。`load_hmr2` 只載入 `backbone.*` 與 `smpl_head.*`（strict）。

---

## 5. SimpleVO 與 `compute_cam_angvel`

### 5.1 取樣

`frames = read_video_np(scale=0.5)`（RGB）；`sample_idxs = arange(0, L, 8)`，最後一幀不在其中就補上 `L-1`。

### 5.2 特徵與匹配（`CV2SIFTMather`）

- 灰階：`cv2.cvtColor(img, COLOR_BGR2GRAY)`，但輸入是 **RGB**，所以實際是 `Y = 0.114·R + 0.587·G + 0.299·B`（原始碼行為，照抄）。
- `cv2.SIFT_create()` 預設：`nfeatures=0, nOctaveLayers=3, contrastThreshold=0.04, edgeThreshold=10, sigma=1.6`；`detectAndCompute(gray, None)`。
- `FlannBasedMatcher(KDTREE, trees=5)`，`checks=50`，`knnMatch(k=2)`；Lowe ratio `m.distance < 0.7·n.distance`。
- 輸出 `pts0, pts1 (N,2)` float32（半解析度像素）。

### 5.3 兩視圖幾何（`PycolmapRansacTwoViewGeometrySolver`）

- 相機 `SIMPLE_PINHOLE`，`params = [f, W/2, H/2]`，`f = focal_length_from_mm(W, H, f_mm or 24)`（W、H 為半解析度）。
- `pycolmap.estimate_calibrated_two_view_geometry(cam, pts0(f64), cam, pts1(f64), matches=[(i,i)], options)`；`TwoViewGeometryOptions(min_num_inliers=10, min_E_F_inlier_ratio=0.8, max_H_inlier_ratio=0.9, compute_relative_pose=True)`，其餘為 COLMAP 預設（RANSAC `max_error=4.0` px、`confidence=0.999`、`max_num_trials=10000`、`min_inlier_ratio=0.25`）。COLMAP 同時估 E（五點）、F（七點）、H（四點），依內點比例判定組態（calibrated / planar / panoramic …），再由對應模型分解出 `cam2_from_cam1`（含 cheirality 檢查）。
- `T_delta = [cam2_from_cam1.matrix(); 0 0 0 1]`（前一取樣幀 → 目前取樣幀）。估計失敗時 pycolmap 回傳預設 `Rigid3d`（單位旋轉）——待以黃金資料確認（見不確定處）。
- 累積：`T_w2c[0] = I`，`T_w2c[k] = T_delta_k @ T_w2c[k-1]`。平移只有方向（尺度任意），GVHMR **只用旋轉**。
- RANSAC 不確定性：pycolmap 的 RANSAC 有亂數，同一輸入兩次執行可能略有不同。

### 5.4 補齊未取樣幀（`interpolate_missing_frames`）

- 平移線性內插；旋轉轉四元數 `[w,x,y,z]`（`rotation_matrix_to_quaternion`，依 trace 分支，未正規化正負號），`slerp`：`dot<0` 時翻 `q1`；`dot > 0.9995` 改線性並正規化；否則 `θ0 = acos(dot)`，`s0 = cos(tθ0) - dot·sin(tθ0)/sin θ0`，`s1 = sin(tθ0)/sin θ0`。
- 取樣幀直接用原值。輸出 `(L,4,4)` float64。

### 5.5 `compute_cam_angvel(R_w2c)`

```
R_rel[t] = R_w2c[t+1] @ R_w2c[t]ᵀ            (L-1,3,3)
cam_angvel[t] = matrix_to_rotation_6d(R_rel) = R_rel[:2,:].reshape(6)   # 前兩「列」
cam_angvel[L-1] = cam_angvel[L-2]                                       # 補最後一幀
→ (L,6) float32
```

靜態鏡頭時每幀都是 `[1,0,0,0,1,0]`。

---

## 6. GVHMR 網路與後處理

### 6.1 輸入組裝（`DemoPL.predict`）

```
obs        = normalize_kp2d(kp2d, bbx_xys)[None]      (1,L,17,3)
bbx_xys    = bbx_xys[None]                            (1,L,3)
K_fullimg  = K[None]                                  (1,L,3,3)
cam_angvel = cam_angvel[None]                         (1,L,6)
f_imgseq   = f_imgseq[None]                           (1,L,1024)
length     = tensor([L])
```

`normalize_kp2d`：框外（`x < cx - s/2`、`x > cx + s/2`、`y` 同理）的關節 conf 設 0；`xy_n = 2·(xy - c)/s`；輸出 `[xy_n, conf]`。

`Pipeline.forward` 的條件：

```
f_cliffcam   = compute_bbox_info_bedlam(bbx_xys, K) = [(cx - K02)/K00, (cy - K12)/K00, s/K00]   (1,L,3)
f_cam_angvel = (cam_angvel - [1,0,0,0,1,0]) / [0.001,0.1,0.1,0.1,0.001,0.1]                 (1,L,6)
```

### 6.2 `NetworkEncoderRoPE`（`hmr4d/network/gvhmr/relative_transformer.py`）

`demo.yaml` 用 `network: gvhmr/relative_transformer` = `NetworkEncoderRoPE` 的預設參數：`output_dim=151, max_len=120, cliffcam_dim=3, cam_angvel_dim=6, imgseq_dim=1024, latent_dim=512, num_layers=12, num_heads=8, mlp_ratio=4.0, pred_cam_dim=3, static_conf_dim=6, dropout=0.1（推論無作用）, avgbeta=True`。checkpoint 的 247 個鍵與此類別完全對應（`siga24_release.yaml` 寫的 `NetworkEncoderRoPEV2` 沒有被 demo 使用）。所有鍵前綴 `pipeline.denoiser3d.`。

timm `Mlp(in, hidden=in, out)` = `fc1 → act → fc2`（act 預設 `nn.GELU` erf 版，dropout 無作用）。

**(a) 2D 觀測嵌入**

```
visible = obs[...,2] > 0.5                      (1,L,17)
obs[~visible] = 0
f = learned_pos_linear(obs[..., :2])            Linear(2,32)          learned_pos_linear.weight (32,2) .bias (32)
f = f·visible + learned_pos_params[j]·(~visible)                       learned_pos_params (17,32)
x = embed_noisyobs(f.view(1,L,544))             Mlp 544→1024→512, GELU embed_noisyobs.fc1.weight (1024,544), fc2.weight (512,1024)
```

544 的排列是關節優先：`index = j·32 + c`。

**(b) 條件相加**（`x += …`，三個都加）

```
cliffcam_embedder:  Linear(3,512) → SiLU → Dropout → Linear(512,512)     cliffcam_embedder.0.weight (512,3), .3.weight (512,512)
cam_angvel_embedder: Linear(6,512) → SiLU → Dropout → Linear(512,512)    cam_angvel_embedder.0.weight (512,6), .3.*
imgseq_embedder:    LayerNorm(1024, eps=1e-5) → Linear(1024,512)        imgseq_embedder.0.weight/bias (1024), .1.weight (512,1024)
```

**(c) 遮罩**

- `pmask = ~length_to_mask(length, L)`：B=1、length=L 時全 False（無填補）。
- `L > 120` 時建 `attnmask (L,L)` bool（True = 遮掉）：對每個 i，
  `lo = max(0, i-60)`，`hi = min(L, i+60)`，`hi = max(120, hi)`，`lo = min(L-120, lo)`，`attnmask[i, lo:hi] = False`。每個 query 恰好看 120 個 key 的滑動視窗（邊界處視窗貼齊序列端點）。`L ≤ 120` 時不遮。
- 遮罩以 `masked_fill(-inf)` 套在分數上，再 softmax。

**(d) 12 個 `EncoderRoPEBlock`**（鍵 `blocks.{0..11}.`）

```
h = LN1(x)                          norm1 LayerNorm(512, eps=1e-6, affine)     blocks.i.norm1.weight/bias (512)
q,k,v = query(h), key(h), value(h)  Linear(512,512) 各有 bias                 blocks.i.attn.{query,key,value}.weight (512,512)
reshape (B,8,L,64)
q,k = RoPE(q), RoPE(k)
a = softmax(q·kᵀ/8 + mask)·v        # /sqrt(64)
o = proj(a 合併 head)                Linear(512,512)                          blocks.i.attn.proj.*
x = x + gate_msa ⊙ o                gate_msa (1,1,512)                        blocks.i.gate_msa
x = x + gate_mlp ⊙ mlp(LN2(x))      norm2 eps=1e-6；mlp fc1 512→2048, GELU(approximate="tanh"), fc2 2048→512
                                    blocks.i.mlp.fc1.weight (2048,512), fc2.weight (512,2048), gate_mlp (1,1,512)
```

RoPE（`hmr4d/network/base_arch/embeddings/rotary_embedding.py`）：

- `d = 64`（head_dim），`inv_freq[k] = 10000^(-2k/64)`，`k = 0..31`；`freqs[t, 2k] = freqs[t, 2k+1] = t·inv_freq[k]`（**交錯**排列，`repeat "i j -> i (j r)", r=2`），`t = 0..L-1` 為絕對幀索引。
- `rotate_half`：相鄰成對 `(x_{2k}, x_{2k+1}) → (-x_{2k+1}, x_{2k})`。
- `x' = x·cos(freqs) + rotate_half(x)·sin(freqs)`，全部 64 維都旋轉（`scale=1`）。
- 預先快取 4096 個位置；`L > 4096` 時即時重算。q、k 都旋轉，v 不旋轉。點積只依相對位移，等價於相對位置編碼。

**(e) 輸出頭**

```
sample = final_layer(x)            Mlp 512→512→151, GELU     final_layer.fc1.weight (512,512), fc2.weight (151,512)
avgbeta: betas = mean_t sample[..., 126:136]（對有效幀平均）並廣播回每幀
pred_cam = pred_cam_head(x)        Mlp 512→512→3            pred_cam_head.fc2.weight (3,512)
pred_cam = pred_cam·[0.1784,0.0956,0.0764] + [1.0606,-0.0027,0.2702]；pred_cam[...,0] = max(·, 0.25)
static_conf_logits = static_conf_head(x)   Mlp 512→512→6    static_conf_head.fc2.weight (6,512)
```

`static_conf_logits` 的 6 個通道對應關節 `[7,10,8,11,20,21]` = `[L_Ankle, L_Foot, R_Ankle, R_Foot, L_Wrist, R_Wrist]`。

注意輸出頭作用在**最後一個 block 的輸出**上，沒有最終 LayerNorm。

### 6.3 `EnDecoder.decode`（`stats_name = MM_V1_AMASS_LOCAL_BEDLAM_CAM`）

`x = x_norm·std + mean`，151 維的 mean/std 由 `hmr4d/model/gvhmr/utils/stats_compose.py` 依序串接：

| 區段 | 維度 | 統計來源 |
| --- | --- | --- |
| `body_pose_r6d` 0:126 | 21 關節 × 6D | `body_pose_r6d["amass"]`（126 個 mean、126 個 std，從檔案原樣複製） |
| `betas` 126:136 | 10 | `betas["amass"]`：mean `[0.2310,0.1750,0.2931,-0.1859,-1.1163,-1.1028,-0.2573,0.3555,0.3732,0.2852]`，std `[0.8831,0.7965,1.0899,1.1788,1.2128,1.1081,0.9780,1.1434,0.8498,1.1462]` |
| `global_orient_r6d`（incam）136:142 | 6 | `global_orient_c_r6d["bedlam"]`：mean `[-4.9862e-03,-8.7136e-04,-1.4187e-03,1.4825e-02,-9.4419e-01,-5.1653e-02]`，std `[0.7048,0.1713,0.6884,0.1548,0.1546,0.2403]` |
| `global_orient_gv_r6d` 142:148 | 6 | `global_orient_gv_r6d["bedlam"]`：mean `[3.6018e-04,-2.2327e-04,2.2316e-03,-4.4879e-02,-9.7435e-01,1.0021e-01]`，std `[0.6070,0.5355,0.5873,0.6285,0.2336,0.7675]` |
| `local_transl_vel` 148:151 | 3 | `local_transl_vel["amass"]`：mean `[-0.0002,-0.0006,0.0069]`，std `[0.0064,0.0070,0.0138]` |

這些 buffer 是非持久的（不在 checkpoint 內），原生版要內建或從 `stats_compose.py` 轉出一次。

6D → 旋轉矩陣（pytorch3d `rotation_6d_to_matrix`，**列**慣例）：`b1 = normalize(a1)`，`b2 = normalize(a2 - (b1·a2)b1)`，`b3 = b1 × b2`，矩陣 = `stack(b1,b2,b3, dim=-2)`（b 為列）。`normalize` 是 `F.normalize`（`max(‖v‖, 1e-12)`）。

旋轉矩陣 → 軸角（`matrix_to_axis_angle`，非 fast 路徑）：`matrix_to_quaternion`（四個候選取 `q_abs` 最大者，分母下限 0.1，最後 `standardize_quaternion` 使 w ≥ 0）→ `quaternion_to_axis_angle`：`half = atan2(‖xyz‖, w)`，`aa = xyz / (0.5·sinc(half/π))`。軸角 → 矩陣（`axis_angle_to_matrix`，非 fast）：經四元數 `[cos(θ/2), aa·0.5·sinc(θ/2π)]` 再 `quaternion_to_matrix`（`two_s = 2/‖q‖²`）。

decode 輸出：`body_pose (1,L,63)`、`betas (1,L,10)`、`global_orient (1,L,3)`（incam）、`global_orient_gv (1,L,3)`、`local_transl_vel (1,L,3)`。

### 6.4 相機座標參數

```
smpl_params_incam = {body_pose, betas, global_orient, transl = compute_transl_full_cam(pred_cam, bbx_xys, K)}
compute_transl_full_cam: s,tx,ty = pred_cam；sb = s·bbx_s
   tz = 2·f/(sb+1e-9)；X = tx + 2(cx_bbx - K02)/(sb+1e-9)；Y = ty + 2(cy_bbx - K12)/(sb+1e-9)
```

### 6.5 世界座標組裝（`get_smpl_params_w_Rt_v2`，fp32）

```
R_t_to_tp1 = rotation_6d_to_matrix(cam_angvel)          # 用未正規化的 cam_angvel
as_identity: ‖matrix_to_axis_angle(R)‖ < 1e-5 的矩陣換成精確 I
R_gv = aa2mat(global_orient_gv)；R_c = aa2mat(global_orient_c)
R_c2gv = R_gv @ R_cᵀ；view = R_c2gv[:, :, 2]（第 3 欄）
R_cnext2gv = R_c2gv @ R_t_to_tp1ᵀ；view_next = R_cnext2gv[..., 2]
v1 = normalize(view with y=0)；v2 = normalize(view_next with y=0)
aa = normalize(v2 × v1) · acos(clamp(v1·v2, -1, 1))       # normalize 對零向量回傳零
aa = gaussian_smooth(aa, sigma=3, dim=時間)
R_tp1_to_t = aa2mat(aa)ᵀ
R_t_to_0[0] = I；R_t_to_0[i] = R_t_to_0[i-1] @ R_tp1_to_t[i]（i ≥ 1，注意用的是第 i 幀的值）
R_t_to_0 = as_identity(R_t_to_0)
global_orient = mat2aa(R_t_to_0 @ R_gv)
transl = rollout_local_transl_vel(local_transl_vel, global_orient)：
    v_w[t] = aa2mat(global_orient[t]) @ local_transl_vel[t]
    transl[0] = 0；transl[t] = Σ_{k<t} v_w[k]
get_tgtcoord_rootparam(tsf="any->ay")：R = aa2mat([0,0,π]) = diag(-1,-1,1)
    global_orient = mat2aa(R @ aa2mat(global_orient))；transl = R @ transl
```

`gaussian_smooth(x, sigma, dim)`：核 = scipy `_gaussian_kernel1d(sigma, order=0, radius=int(4σ+0.5))` = `exp(-0.5·k²/σ²)` 正規化到總和 1（σ=3 → 半徑 12、25 點；σ=5 → 半徑 20），沿時間 `replicate` 補邊後一維卷積。

`smpl_params_global = {body_pose, betas, global_orient, transl}`（body_pose 與 betas 同 incam）。

### 6.6 後處理（`postproc=True`）

共用的 FK：`EnDecoder.fk_v2(body_pose, betas, global_orient, transl)`：

```
skeleton = J_template + J_shapedirs·betas            (…,55,3) 取前 22    # J_template = J_regressor@v_template；J_shapedirs = J_regressor@shapedirs[:,:,:10]
local[0] = skeleton[0] + transl；local[j] = skeleton[j] - skeleton[parent[j]]
mat_j = [R_j | local_j]；world_j = normalized_matrix(world_parent @ mat_j)   # normalized_matrix：旋轉部分每欄除以 (‖欄‖+1e-9)
joints = world[..., :3, 3]   (B,L,22,3)
parents[:22] = [-1,0,0,0,1,2,3,4,5,6,7,8,9,9,9,12,13,14,16,17,18,19]
```

不含 pose blend shape（只用關節位置）。SMPL-X 模型檔：`inputs/checkpoints/body_models/smplx/SMPLX_NEUTRAL.npz`。

**非靜態鏡頭 `pp_static_joint`**：

```
J = fk_v2(global params)；Js = J[:, :, [7,10,8,11,20,21]]
disp = Js[1:] - Js[:-1]                              (L-1,6,3)
lg = static_conf_logits[:-1]；lab = lg > 0
w = softmax_over_6(lg - (~lab)·1e6)
is_static = any(lab)
pred_disp = Σ_j disp·w · is_static                   (L-1,3)
d = transl[1:] - transl[:-1] - pred_disp
post = cumsum([transl[0], d])
post[...,0] = gaussian_smooth(post[...,0], σ=3)；post[...,2] = 同上
post_J = J - transl + post
post[...,1] -= min(post_J[...,1])     # 全序列、22 關節的最低 y 貼地
```

**靜態鏡頭 `pp_static_joint_cam`**（`-s`）：incam `transl` 先 `gaussian_smooth(σ=5)`；以第 0 幀的 `R_c2w = R_gv0 @ R_c0ᵀ` 與根關節位置求 `T_c2w`；逐幀 `cp_diff = J_w[i,0] - J_c→w[i,0]`，只保留 |分量| ≥ 0.25 的部分並裁到 ±0.02，累減到第 i 幀以後；再用 `sigmoid(logits) > 0.8` 的靜止關節平均位移（y 分量歸零）累減；最後貼地。

**`process_ik`**（兩種情況都跑）：

```
c = sigmoid(static_conf_logits)                       (1,L,6)
J, local_mat, world_mat = fk_v2(global params(已換成後處理 transl), get_intermediate=True)
target = J.clone()
for i in 1..L-1: target[i, ids] = target[i-1, ids]·c[i-1] + J[i, ids]·(1 - c[i-1])     ids=[7,10,8,11,20,21]
左腿 chain [0,1,4,7,10]，target_ind [3,4]，目標位置 target[:, :, [7,10]]
右腿 chain [0,2,5,8,11]，target_ind [3,4]，目標 [8,11]
左手 chain [9,13,16,18,20]，target_ind [4]，目標 [20]
右手 chain [9,14,17,19,21]，target_ind [4]，目標 [21]
依序（左腿、右腿、左手、右手）各跑一次 CCD_IK(max_iter=2, pos_weight=1, rot_weight=0)，每次以上一次的 local_mat 為輸入
body_pose = mat2aa(local_mat[:, :, 1:22, :3, :3]).flatten → (1,L,63)
```

`CCD_IK`（`hmr4d/utils/ik/ccd_ik.py`）：

- 把 chain 抽出：`chain_local[0] = 世界矩陣[chain[0]]`，其餘為原本的局部矩陣；鏈內 parent = `[-1,0,1,2,3]`；`J_N = len(chain) = 5`。
- `solve`：重複 `max_iter=2` 次 `optimize(1)`；`optimize(i)` 在 `i == J_N-1` 時結束，否則處理第 i 節後遞迴 `optimize(i+1)`（所以只更新鏈索引 1、2、3）。
- 第 i 節：`pos, quat` = 該節世界位置與旋轉（`matrix_to_quaternion`）；對每個 `j ∈ target_ind` 且 `i < j`：
  `q* = qslerp(quat, qmul(qbetween(end_j - pos, target_j - pos), quat), t=(i+1)/J_N)`；累加 `qrot(q*, x̂)`、`qrot(q*, ŷ)`，count += 1。
- `x = normalize(Σx/count)`，`y = normalize(Σy/count)`，`z = x × y`（**y 不再正交化**），`R = [x y z]`（欄）。
- `local_i = normalized_matrix(inverse(parent_world_rot)) @ R`（`get_mat_BtoA`，3×3），寫回後重算整條鏈 FK（每步 `normalized_matrix`）。
- 四元數函式（`hmr4d/utils/geo/quaternion.py`，wxyz）：`qbetween(v0,v1) = normalize([√(|v0|²|v1|²) + v0·v1, v0×v1])`（反向特例換成 y 軸）；`qmul` 為標準 Hamilton 乘積；`qinv` 共軛；`qslerp(q0,q1,t) = qpow(q1·q0⁻¹, t)·q0`，`qpow`：`θ0 = acos(w)`（θ0≈0 時用 1e-9），`[cos(tθ0), v/sin θ0 · sin(tθ0)]`；`qrot(q,v) = v + 2(w·(u×v) + u×(u×v))`；`matrix.normalize(x) = x/max(‖x‖,1e-9)`。

最後 `pred_smpl_params_global.body_pose` 與 `pred_smpl_params_incam.body_pose` 都換成 IK 後的值。

### 6.7 寫出的 npz

`run_gvhmr.py` → `motion.npz`：

| 鍵 | 形狀 | dtype | 內容 |
| --- | --- | --- | --- |
| `global_orient` | (L,3) | float32 | 世界（y 朝上、第 0 幀為原點的重力座標）根旋轉軸角 |
| `body_pose` | (L,63) | float32 | IK 後 21 關節軸角 |
| `transl` | (L,3) | float32 | 後處理後的世界平移（SMPL 慣例：根關節 = J0(betas) + transl） |
| `betas` | (L,10) | float32 | 每幀相同（avgbeta） |
| `fps` | () | float64 | 恆為 30.0 |

`run_gvhmr_pair.py` → `motion_male.npz`、`motion_female.npz`：上表全部，再加：

| 鍵 | 形狀 | dtype |
| --- | --- | --- |
| `incam_global_orient` | (L,3) | float32 |
| `incam_transl` | (L,3) | float32 |
| `K` | (3,3) | float32 |
| `kp_conf` | (L,17) | float32（ViTPose conf） |

中間檔：`preprocess/bbx.pt`、`vitpose.pt (L,17,3)`、`vit_features.pt (L,1024)`、`slam_results.pt (L,4,4) float64`；雙人版 `preprocess/detections.pkl`、`preprocess/<role>/vitpose.pt`、`vit_features.pt`、`preprocess/slam.pt`、`tracks.json`。

---

## 7. 雙人流程與 SMPL-X 前向

### 7.1 `pair_tracks.py`

- `detect(video, yolo, conf=0.3)`：`yolo.predict(..., classes=0, stream=True)`（第 2.1–2.3 節，無追蹤），每幀 `boxes.xyxy (N,4)`。
- `assign(dets, width)`：
  1. 所有框寬度的第 35 百分位數 `single`（`np.percentile` 線性內插）。
  2. 每幀丟掉寬度 ≥ `1.45·single` 的框（兩人合併框），依 `cx` 排序。
  3. ≥ 2 框：在所有 `i<j` 中選 `|cx_i - cx(last_left)| + |cx_j - cx(last_right)|` 最小者（`last` 為 None 時該項為 0；平手取第一個），左 = i、右 = j。
  4. 1 框且之前有指派：分給 `cx` 最接近的角色，距離 < `0.15·width` 才接受。
  5. 覆蓋率 `cover_* = 有值幀比例`；缺值用 `np.interp`（兩端保持端點值）逐座標內插。
- `shot_cuts(threshold=0.35)`：64×36 灰階 /255，與前一幀平均絕對差 > `0.35·0.25 = 0.0875` 就記一次切換。
- 覆蓋率 < 0.3 就報錯；預設畫面左 = 男。之後每人 `get_bbx_xys_from_xyxy(base_enlarge=1.2)`（**不做**移動平均平滑）。

### 7.2 `pair_align.py`

`world_to_camera(motion, smplx)`：

```
j0 = J_regressor[0] @ (v_template + shapedirs[:,:,:nb] @ mean_t(betas))     # nb = min(10, 400)
Rg = aa2mat(global_orient)，Rc = aa2mat(incam_global_orient)                # aa_to_mat 為 Rodrigues，θ 下限 1e-12
pg = transl + j0，pc = incam_transl + j0
M = Σ_l Rc_l @ Rg_lᵀ；U,S,Vt = svd(M)；R = U·diag(1,1,det(U·Vt))·Vt
t = mean_l(pc - R·pg)
殘差：旋轉 = median ‖mat2aa((R·Rg)ᵀ·Rc)‖（度），位置 = median ‖pc - (R·pg + t)‖
```

`mat_to_aa`：`ang = arccos(clip((tr-1)/2, -1, 1))`，`v = [m21-m12, m02-m20, m10-m01]`，`sin(ang) > 1e-6` 時 `v/(2 sin)·ang`，否則 `v/2`（接近 π 時不精確，原始碼行為）。

`main(anchor=female, other=male)`：

```
A = Ra.T @ Ro，b = Ra.T @ (to - ta)
other.global_orient = mat2aa(A @ aa2mat(other.global_orient))   (float32)
pel = (other.transl + j0_other) @ A.T + b；other.transl = pel - j0_other
pair_gap = surface_gaps(anchor, aligned)                         (L,) float32
輸出 = other 的所有鍵 + 上述三鍵覆寫/新增
```

### 7.3 `surface_gaps` 用到的 SMPL-X 前向（`smplx` 0.1.28）

`smplx.create(<body_models>, model_type="smplx", gender="neutral", use_pca=False, flat_hand_mean=True, batch_size=1)`，其餘預設：`num_betas=10`、`num_expression_coeffs=10`、`use_face_contour=False`；jaw/leye/reye/hand pose 全 0，`flat_hand_mean=True` 時手部平均姿勢**不加**；`transl` 加在最後。

SMPLX_NEUTRAL.npz 內容（已讀 npy 標頭）：

| 陣列 | 形狀 | dtype | 備註 |
| --- | --- | --- | --- |
| `v_template` | (10475,3) | f4 | |
| `shapedirs` | (10475,3,400) | **f8** | 0:300 體型，300:400 表情；smplx 套件用 `[:, :, :10]` 與 `[:, :, 300:310]` |
| `posedirs` | (10475,3,486) | f4 | 486 = 54 關節 × 9 |
| `J_regressor` | (55,10475) | f4 | **fortran_order=True** |
| `weights` | (10475,55) | f4 | LBS 權重 |
| `kintree_table` | (2,55) | i8 | 第 0 列為 parent（`parents[0] = -1`） |
| `f` | (20908,3) | u4 | 面 |
| `hands_meanl/r` | (45,) | f4 | 本流程不用 |
| `part2num`、`joint2num` | () | object pickle | 跳過 |

LBS（`smplx.lbs.lbs`）：

```
v_shaped = v_template + Σ_k betas_k·shapedirs[:,:,k]（再加表情，為 0）
J = J_regressor @ v_shaped                            (55,3)
full_pose = [global_orient, body_pose(21), jaw, leye, reye, lhand(15), rhand(15)] = 55 關節軸角
R = batch_rodrigues(full_pose)                        # θ = ‖aa + 1e-8‖
pose_feature = (R[1:] - I).reshape(486)
v_posed = v_shaped + (pose_feature @ posedirs.reshape(486, V·3)).reshape(V,3)   # smplx 讀檔時 posedirs 轉成 (486, V·3)
batch_rigid_transform：rel_J[j] = J[j] - J[parent[j]]；G_j = G_parent·[R_j | rel_J_j]；A_j = G_j - [0 | G_j·J_j]
T_v = Σ_j weights[v,j]·A_j；verts = T_v·[v_posed;1] + transl
```

`surface_gaps`：每幀兩人頂點各取 `[::6]`（1746 個），`torch.cdist` 的最小歐氏距離。可用 KD-tree 或 GPU 暴力法；結果是單一最小值，很容易逐位元對上。

---

## 8. 需要原生對應的第三方運算與驗證容忍度

### 8.1 第三方運算清單

| 運算 | 用處 | 原生化注意 |
| --- | --- | --- |
| FFmpeg 解碼（PyAV / OpenCV） | 所有階段 | `ffmpeg.exe -f rawvideo -pix_fmt rgb24`；YOLO 端要轉成 BGR 再照 2.1 翻回 RGB（等價於直接用 RGB） |
| libx264 crf 23 重新編碼 | 工作影片 | 可略過（會造成小差異） |
| ffmpeg `scale=iw*0.5:ih*0.5`（swscale bicubic） | ViTPose/HMR2/SimpleVO 半解析度 | 用 ffmpeg 同一濾鏡輸出半解析度第二路，最省事 |
| `cv2.resize(INTER_LINEAR)`、`copyMakeBorder(114)` | YOLO letterbox | OpenCV 雙線性：像素中心對齊（`(x+0.5)·scale - 0.5`），uint8 定點運算有 ±1 誤差 |
| `torchvision.ops.nms` | YOLO | 貪婪 NMS，依分數降序；IoU > 0.7 抑制 |
| `cv2.GaussianBlur((5,5), σ)`、`((11,11), 0)` | 裁切抗鋸齒、DARK | 可分離核，`BORDER_REFLECT_101`；uint8 輸入有捨入 |
| `cv2.getAffineTransform`、`cv2.warpAffine(INTER_LINEAR, BORDER_CONSTANT 0)` | 256×256 裁切 | 逆映射、整數像素中心、OpenCV 用 `INTER_BITS=5` 的定點權重（uint8 誤差 ±1） |
| Conv2d、ConvTranspose2d、BatchNorm2d、MaxPool2d(5,1,2)、Upsample nearest ×2、SiLU、ReLU | YOLO、ViTPose 頭 | candle 都有 |
| LayerNorm（eps 1e-6 / 1e-5）、GELU（erf 與 tanh 兩種）、softmax、矩陣乘 | 三個 transformer | GVHMR block 內 MLP 是 tanh 版，其餘 Mlp 是 erf 版，不能混用 |
| RoPE（交錯式） | GVHMR | 見 6.2(d) |
| `cv2.SIFT`、`FlannBasedMatcher`（KD-tree 5 棵、checks 50） | SimpleVO | 沒有純 Rust 等價實作；FLANN 為近似最近鄰，結果與暴力 kNN 不完全相同 |
| `pycolmap.estimate_calibrated_two_view_geometry` | SimpleVO | 需五點 E + H + F 的 RANSAC 與組態判斷；只需要旋轉 |
| TrackTrack（Kalman、GMC 光流 `goodFeaturesToTrack`/`calcOpticalFlowPyrLK`/`estimateAffinePartial2D`） | 單人追蹤 | 見 2.4 建議 |
| scipy `_gaussian_kernel1d`、`F.pad(replicate)`+`conv1d` | 平滑 | 簡單 |
| numpy `linalg.inv`（2×2）、`svd`（3×3）、`percentile`、`interp` | DARK、pair | 簡單 |
| pytorch3d 旋轉轉換（6D、四元數、軸角） | GVHMR | 依 6.3 的確切公式（含 standardize、sinc） |
| smplx LBS、`torch.cdist` | pair_align | 見 7.3 |
| PIL `convert("L")`、`resize(BICUBIC)` | shot_cuts | 只用於提示，容忍度寬 |

### 8.2 驗證容忍度建議（fp32、同一輸入對同一階段）

| 階段 | 比對量 | 建議容忍 |
| --- | --- | --- |
| 解碼 | rgb24 像素 | 同一 ffmpeg：完全相同；不同解碼器：平均 |Δ| ≤ 1、最大 ≤ 3 |
| 半解析度縮放 | 像素 | 同上 |
| YOLO 網路 | 原始 `(84,A)` | 框 |Δ| < 0.05 px（letterbox 座標）、分數 |Δ| < 1e-4 |
| YOLO + NMS | 偵測框 | 框 |Δ| < 0.5 px，偵測數相同（分數接近 0.5 / 0.3 門檻者除外） |
| 追蹤 + 平滑後 `bbx_xys` | (L,3) | 同追蹤器：< 0.5 px；換成簡化追蹤器：中位數 < 2 px、最大 < 1% 影像對角 |
| 256×256 裁切 | 正規化影像 | 平均 |Δ| < 0.01（≈ 1 灰階級 / 0.229） |
| ViTPose 熱圖 | (B,17,64,48) | 最大 |Δ| < 1e-3（熱圖峰值約 0.5–1） |
| ViTPose `kp2d` | (L,17,3) | 位置 < 0.5 px（全解析度）、conf < 1e-3；可見度（conf>0.5）判定一致 |
| HMR2 特徵 | (L,1024) | 每幀 cosine > 0.9999，最大 |Δ| < 1e-3·‖f‖∞ |
| SimpleVO | 每幀 R_w2c | 測地角 < 0.5°（RANSAC 有亂數，Python 自身重跑也會差這麼多）；`cam_angvel` |Δ| < 1e-3 |
| GVHMR 網路 | `pred_x`、`pred_cam`、`static_conf_logits` | 相同輸入下 |Δ| < 1e-4（L > 120 時注意遮罩） |
| decode / 世界組裝 | 軸角、transl | 每關節 < 0.1°；transl < 1 mm |
| 後處理 + IK | 最終 npz | body_pose 每關節 < 0.5°（CCD 對小差異敏感）；transl < 5 mm；betas < 1e-4 |
| 端到端（從影片） | 22 關節世界位置 | MPJPE < 2 cm、根軌跡漂移 < 5 cm/10 s、關節角中位數 < 2° |
| SMPL-X LBS | 頂點 | < 1e-5 m |
| pair_align | R、t、`pair_gap` | R 測地 < 0.01°、t < 0.1 mm、`pair_gap` < 0.5 mm |

在 GPU 上以 TF32 矩陣乘（Ampere 以上 cuBLAS 預設可能開啟）會讓 ViT-H 32 層的特徵誤差放大到 ~1e-2；驗證時應關閉 TF32，或把特徵容忍度放寬到 cosine > 0.999。

---

## 9. 權重讀取（不需 Python）

四個 checkpoint 都是 PyTorch zip 格式：

| 檔案 | zip 內 pickle | tensor 數 | 頂層鍵 |
| --- | --- | --- | --- |
| `gvhmr/gvhmr_siga24_release.ckpt` | `e419-s158340/data.pkl` | 247 | `epoch, global_step, pytorch-lightning_version, state_dict` |
| `hmr2/epoch=10-step=25000.ckpt` | `epoch=10-step=25000/data.pkl` | 576 | `…, state_dict, lr_schedulers, NativeMixedPrecisionPlugin` |
| `vitpose/vitpose-h-multi-coco.pth` | `archive/data.pkl` | 403 | `meta, state_dict` |
| `yolo/yolov8x.pt` | `yolov8x/data.pkl` | 595 | `date, version, license, docs, epoch, best_fitness, model, ema, updates, optimizer, train_args` |

- 前三者的 `state_dict` 是 `OrderedDict[str → tensor]`，tensor 由 `torch._utils._rebuild_tensor_v2(storage, offset, size, stride, requires_grad, hooks)` 重建；storage 為 `BINPERSID` 的 `('storage', <torch.FloatStorage>, key, location, numel)`，資料在 `<prefix>/data/<key>`，小端序。
- `yolov8x.pt` 的 `model` 是整個 `DetectionModel` 物件的 pickle：要走訪 `__dict__`（`BUILD` 狀態）裡的 `_modules` / `_parameters` / `_buffers` 組出鍵名 `model.model.<i>....`；`Parameter` 由 `torch._utils._rebuild_parameter(tensor, requires_grad, hooks)` 包裝；dtype 為 `HalfStorage`（fp16），BN 的 `eps=0.001`、`momentum=0.03` 是物件屬性（BINFLOAT）。
- 用到的 pickle opcode：`PROTO FRAME STOP MARK EMPTY_DICT EMPTY_LIST EMPTY_TUPLE TUPLE TUPLE1/2/3 BINPUT LONG_BINPUT MEMOIZE BINGET LONG_BINGET BINUNICODE SHORT_BINUNICODE GLOBAL STACK_GLOBAL BININT BININT1 BININT2 LONG1 NONE NEWTRUE NEWFALSE BINFLOAT REDUCE BUILD NEWOBJ SETITEM SETITEMS APPEND APPENDS BINPERSID`（另有 `BINBYTES/SHORT_BINBYTES`，出現在 ultralytics 物件中）。製作本文件時用約 150 行的 Lua pickle VM 就把四個檔案的鍵與形狀全部讀出，Rust 版同樣簡單。
- GVHMR 的 ckpt 不含非持久 buffer：`pred_cam_mean/std`、RoPE `encoding`、`EnDecoder.mean/std`、SMPL-X 相關 buffer、`cam_angvel_mean/std`，都要原生內建（值見 6.2、6.3、6.1）。
- HMR2 ckpt 另含 `smpl.*`（10 個）、`discriminator.*`（62 個）、`initialized`，不用載入。

---

## 10. 參數量

由 checkpoint 形狀直接加總：

| 模型 | 部件 | 參數量 |
| --- | --- | --- |
| YOLOv8x | 全部（不含 BN running stats） | 68,229,648 |
| ViTPose-H | 骨幹 ViT-H/16 | 630,912,000 |
| ViTPose-H | 熱圖頭（不含 BN running stats） | 6,296,849 |
| HMR2.0a（特徵用） | 骨幹 ViT-H/16 | 630,912,000 |
| HMR2.0a（特徵用） | `smpl_head.transformer`（6 層交叉注意力解碼器） | 39,386,112 |
| HMR2.0a | `smpl_head` 全部（含 dec*、init_*） | 39,547,194 |
| GVHMR | `pipeline.denoiser3d` 全部 | 40,852,256 |

推論需要的總權重約 1.42 G 參數（fp32 約 5.7 GB，fp16 約 2.8 GB），其中兩個 ViT-H 佔 89%。

---

## 11. 尚未確定、需以黃金資料確認的地方

1. pycolmap 4.2.1 在兩視圖估計失敗（內點不足、退化組態）時 `cam2_from_cam1` 的實際值（推測為單位旋轉）；以及 panoramic / planar 組態下旋轉是由 H 還是 E 分解而來。
2. ffmpeg `scale=iw*0.5:ih*0.5` 遇到奇數尺寸的取整方式，以及 PyAV filter graph 是在 yuv 還是 rgb 上縮放（影響色度取樣，誤差在 1 灰階級內）。
3. TrackTrack 在 `conf=0.5` 下的實際行為（low 組只剩 0.5–0.6 的偵測）；簡化追蹤器與它的框差異需要在實際影片上量。
4. `get_smpl_params_w_Rt_v2` 中 `R_t_to_0` 的累乘使用 `R_tp1_to_t[:, i]`（第 i 幀而非第 i-1 幀）且 `R_tp1_to_t = aa2mat(aa)ᵀ`，命名與數學看似不一致，但原生版必須照抄原始碼；已註明。
5. CCD IK 解出的 `[x y z]` 不保證正交（y 未重新正交化），之後的 `normalized_matrix` 只做欄正規化；`matrix_to_axis_angle` 對非正交矩陣的結果依 `matrix_to_quaternion` 的候選選擇而定，原生版要用同一公式才會一致。
6. 工作影片「逐幀複製、標成 30 fps」的行為對非 30 fps 來源是否為刻意設計，需要產品端決定原生版是否改成真正重取樣。
