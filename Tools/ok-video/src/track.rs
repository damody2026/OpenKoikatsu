//! Person boxes over time (Docs/VIDEO_MOTION_NATIVE_SPEC.md §2.4–2.6, pair_tracks.py).
//!
//! Single person: an IoU tracker stands in for ultralytics' TrackTrack (the spec's
//! recommendation; GVHMR keeps one track and smooths it twice), then `get_one_track`
//! (largest area sum, gap interpolation, two 5-tap moving averages).
//! Two people: per-frame left/right assignment exactly as pair_tracks.assign.

use crate::yolo::{iou, Det};

struct Track {
    boxes: Vec<Option<[f32; 4]>>,
    last: [f32; 4],
    missed: usize,
}

/// Main track box per frame (x1,y1,x2,y2), or an error when nobody was found.
pub fn single(dets: &[Vec<Det>], w: usize, h: usize) -> Result<Vec<[f32; 4]>, String> {
    let n = dets.len();
    let mut live: Vec<Track> = Vec::new();
    let mut done: Vec<Track> = Vec::new();
    for (f, frame) in dets.iter().enumerate() {
        let mut used = vec![false; frame.len()];
        // greedy by IoU between live tracks and detections
        let mut pairs: Vec<(f32, usize, usize)> = Vec::new();
        for (ti, t) in live.iter().enumerate() {
            for (di, d) in frame.iter().enumerate() {
                let o = iou(&t.last, &d.b);
                if o > 0.3 {
                    pairs.push((o, ti, di));
                }
            }
        }
        pairs.sort_by(|a, b| b.0.partial_cmp(&a.0).unwrap());
        let mut matched = vec![false; live.len()];
        for (_, ti, di) in pairs {
            if matched[ti] || used[di] {
                continue;
            }
            matched[ti] = true;
            used[di] = true;
            let t = &mut live[ti];
            t.boxes.resize(f, None);
            t.boxes.push(Some(frame[di].b));
            t.last = frame[di].b;
            t.missed = 0;
        }
        for (ti, t) in live.iter_mut().enumerate() {
            if !matched[ti] {
                t.missed += 1;
            }
        }
        let (keep, gone): (Vec<Track>, Vec<Track>) = live.into_iter().partition(|t| t.missed <= 30);
        live = keep;
        done.extend(gone);
        for (di, d) in frame.iter().enumerate() {
            if !used[di] && d.score > 0.7 {
                let mut boxes = vec![None; f];
                boxes.push(Some(d.b));
                live.push(Track { boxes, last: d.b, missed: 0 });
            }
        }
    }
    done.extend(live);
    let area = |t: &Track| -> f64 { t.boxes.iter().flatten().map(|b| ((b[2] - b[0]) * (b[3] - b[1])) as f64 / (w * h) as f64).sum() };
    let best = done.iter().max_by(|a, b| area(a).partial_cmp(&area(b)).unwrap()).ok_or("整段影片都沒有偵測到人物")?;
    let mut boxes: Vec<Option<[f32; 4]>> = best.boxes.clone();
    boxes.resize(n, None);
    let mut out = interpolate(&boxes).ok_or("主要人物軌跡是空的")?;
    for _ in 0..2 {
        out = moving_average(&out, 5);
    }
    Ok(out)
}

/// `linear_interpolate_frame_ids`: leading/trailing gaps take the nearest valid box.
pub fn interpolate(b: &[Option<[f32; 4]>]) -> Option<Vec<[f32; 4]>> {
    let valid: Vec<usize> = (0..b.len()).filter(|&i| b[i].is_some()).collect();
    if valid.is_empty() {
        return None;
    }
    let mut out = vec![[0f32; 4]; b.len()];
    for i in 0..b.len() {
        out[i] = match b[i] {
            Some(v) => v,
            None => {
                let prev = valid.iter().rev().find(|&&v| v < i);
                let next = valid.iter().find(|&&v| v > i);
                match (prev, next) {
                    (Some(&p), Some(&q)) => {
                        let a = (i - p) as f32 / (q - p) as f32;
                        let (bp, bq) = (b[p].unwrap(), b[q].unwrap());
                        std::array::from_fn(|k| bp[k] + a * (bq[k] - bp[k]))
                    }
                    (Some(&p), None) => b[p].unwrap(),
                    (None, Some(&q)) => b[q].unwrap(),
                    (None, None) => unreachable!(),
                }
            }
        };
    }
    Some(out)
}

/// `moving_average_smooth(window)`, replicate padding.
pub fn moving_average(b: &[[f32; 4]], window: usize) -> Vec<[f32; 4]> {
    let r = (window / 2) as isize;
    let n = b.len() as isize;
    (0..n)
        .map(|i| {
            let mut s = [0f32; 4];
            for d in -r..=r {
                let v = b[(i + d).clamp(0, n - 1) as usize];
                for k in 0..4 {
                    s[k] += v[k] / window as f32;
                }
            }
            s
        })
        .collect()
}

/// `get_bbx_xys_from_xyxy(base_enlarge=1.2)`: square box (cx, cy, s) in 192:256 aspect.
pub fn bbx_xys(b: &[f32; 4]) -> [f64; 3] {
    let (x1, y1, x2, y2) = (b[0] as f64, b[1] as f64, b[2] as f64, b[3] as f64);
    let (cx, cy) = ((x1 + x2) / 2., (y1 + y2) / 2.);
    let (mut w, mut h) = (x2 - x1, y2 - y1);
    if w > 0.75 * h {
        h = w / 0.75;
    }
    if w < 0.75 * h {
        w = 0.75 * h;
    }
    [cx, cy, h.max(w) * 1.2]
}

pub struct Pair {
    pub left: Vec<[f32; 4]>,
    pub right: Vec<[f32; 4]>,
    pub cover_left: f64,
    pub cover_right: f64,
}

/// pair_tracks.assign: left/right roles per frame (merged two-person boxes dropped).
pub fn pair(dets: &[Vec<Det>], width: usize) -> Result<Pair, String> {
    let n = dets.len();
    let mut widths: Vec<f32> = dets.iter().flat_map(|d| d.iter().map(|b| b.b[2] - b.b[0])).collect();
    if widths.is_empty() {
        return Err("整段影片都沒有偵測到人物".into());
    }
    widths.sort_by(|a, b| a.partial_cmp(b).unwrap());
    // numpy percentile(35), linear
    let pos = 0.35 * (widths.len() - 1) as f32;
    let (lo, hi) = (pos.floor() as usize, pos.ceil() as usize);
    let single = widths[lo] + (widths[hi] - widths[lo]) * (pos - lo as f32);
    let cx = |b: &[f32; 4]| (b[0] + b[2]) / 2.;
    let mut left: Vec<Option<[f32; 4]>> = vec![None; n];
    let mut right: Vec<Option<[f32; 4]>> = vec![None; n];
    let (mut ll, mut lr): (Option<[f32; 4]>, Option<[f32; 4]>) = (None, None);
    for (f, d) in dets.iter().enumerate() {
        let mut boxes: Vec<[f32; 4]> = d.iter().map(|x| x.b).filter(|b| b[2] - b[0] < 1.45 * single).collect();
        boxes.sort_by(|a, b| cx(a).partial_cmp(&cx(b)).unwrap());
        if boxes.len() >= 2 {
            let mut best = (boxes[0], boxes[1]);
            let mut cost = f32::INFINITY;
            for i in 0..boxes.len() {
                for j in i + 1..boxes.len() {
                    let c = ll.map_or(0., |l| (cx(&boxes[i]) - cx(&l)).abs()) + lr.map_or(0., |r| (cx(&boxes[j]) - cx(&r)).abs());
                    if c < cost {
                        cost = c;
                        best = (boxes[i], boxes[j]);
                    }
                }
            }
            left[f] = Some(best.0);
            right[f] = Some(best.1);
            ll = Some(best.0);
            lr = Some(best.1);
        } else if boxes.len() == 1 && (ll.is_some() || lr.is_some()) {
            let b = boxes[0];
            let dl = ll.map(|l| (cx(&b) - cx(&l)).abs());
            let dr = lr.map(|r| (cx(&b) - cx(&r)).abs());
            let to_left = match (dl, dr) {
                (Some(a), Some(c)) => a <= c,
                (Some(_), None) => true,
                _ => false,
            };
            let d = if to_left { dl.unwrap() } else { dr.unwrap() };
            if d < 0.15 * width as f32 {
                if to_left {
                    left[f] = Some(b);
                    ll = Some(b);
                } else {
                    right[f] = Some(b);
                    lr = Some(b);
                }
            }
        }
    }
    let cover = |v: &[Option<[f32; 4]>]| v.iter().filter(|b| b.is_some()).count() as f64 / n as f64;
    let (cover_left, cover_right) = (cover(&left), cover(&right));
    Ok(Pair {
        left: interpolate(&left).ok_or("左邊的人物沒有被偵測到")?,
        right: interpolate(&right).ok_or("右邊的人物沒有被偵測到")?,
        cover_left,
        cover_right,
    })
}
