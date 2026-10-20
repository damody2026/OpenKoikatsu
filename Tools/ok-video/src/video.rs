//! Video decoding through ffmpeg (Tools/ffmpeg) as rgb24 rawvideo on a pipe
//! (Docs/VIDEO_MOTION_NATIVE_SPEC.md §1). Errors carry ffmpeg's stderr and the OS error.

use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};

pub struct Info {
    pub width: u32,
    pub height: u32,
    pub frames: usize,
    pub fps: f64,
}

/// ffmpeg / ffprobe next to each other: `OK_FFMPEG_DIR`, then the project Tools/ffmpeg.
pub fn tool(name: &str) -> PathBuf {
    let exe = format!("{name}.exe");
    if let Ok(dir) = std::env::var("OK_FFMPEG_DIR") {
        return Path::new(&dir).join(exe);
    }
    if let Ok(me) = std::env::current_exe() {
        for dir in me.ancestors().skip(1) {
            let p = dir.join("Tools").join("ffmpeg").join(&exe);
            if p.exists() {
                return p;
            }
            let p = dir.join("ffmpeg").join(&exe);
            if p.exists() {
                return p;
            }
        }
    }
    PathBuf::from("C:/portable/OpenKoikatsu/Tools/ffmpeg").join(exe)
}

fn run_error(what: &str, exe: &Path, e: std::io::Error) -> String {
    format!("{what}：無法執行 {}（{}，系統錯誤 {}）", exe.display(), e, e.raw_os_error().unwrap_or(0))
}

pub fn probe(video: &Path) -> Result<Info, String> {
    if let Err(e) = std::fs::metadata(video) {
        return Err(format!("讀不到影片 {}：{}（系統錯誤 {}）", video.display(), e, e.raw_os_error().unwrap_or(0)));
    }
    let exe = tool("ffprobe");
    let out = Command::new(&exe)
        .args(["-v", "error", "-select_streams", "v:0", "-count_packets", "-show_entries", "stream=width,height,r_frame_rate,nb_read_packets", "-of", "csv=p=0"])
        .arg(video)
        .output()
        .map_err(|e| run_error("讀取影片資訊", &exe, e))?;
    let text = String::from_utf8_lossy(&out.stdout);
    let f: Vec<&str> = text.trim().split(',').map(str::trim).collect();
    let parse = || -> Option<Info> {
        let (num, den) = f.get(2)?.split_once('/')?;
        Some(Info {
            width: f.first()?.parse().ok()?,
            height: f.get(1)?.parse().ok()?,
            fps: num.parse::<f64>().ok()? / den.parse::<f64>().ok()?.max(1.),
            frames: f.get(3)?.parse().ok()?,
        })
    };
    parse().ok_or_else(|| format!("ffprobe 無法解析 {}：{}{}", video.display(), text.trim(), String::from_utf8_lossy(&out.stderr).trim()))
}

/// Streams frames as packed RGB24, optionally scaled like `scale=iw*S:ih*S` (swscale bicubic).
pub struct Reader {
    child: Child,
    pub width: usize,
    pub height: usize,
    frame: usize,
    stderr: Option<std::thread::JoinHandle<String>>,
}

impl Drop for Reader {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

impl Reader {
    pub fn open(video: &Path, info: &Info, scale: f64) -> Result<Self, String> {
        let exe = tool("ffmpeg");
        let (w, h) = if scale == 1. { (info.width as usize, info.height as usize) } else { ((info.width as f64 * scale) as usize, (info.height as f64 * scale) as usize) };
        let mut cmd = Command::new(&exe);
        cmd.args(["-v", "error", "-nostdin", "-i"]).arg(video);
        if scale != 1. {
            cmd.args(["-vf", &format!("scale=iw*{scale}:ih*{scale}")]);
        }
        cmd.args(["-f", "rawvideo", "-pix_fmt", "rgb24", "-fps_mode", "passthrough", "-"]);
        let mut child = cmd.stdout(Stdio::piped()).stderr(Stdio::piped()).spawn().map_err(|e| run_error("解碼影片", &exe, e))?;
        let mut err = child.stderr.take().unwrap();
        let stderr = std::thread::spawn(move || {
            let mut s = String::new();
            let _ = err.read_to_string(&mut s);
            s
        });
        Ok(Reader { child, width: w, height: h, frame: 0, stderr: Some(stderr) })
    }

    /// Next frame into `buf` (w·h·3); Ok(false) at the end of the stream.
    pub fn next(&mut self, buf: &mut Vec<u8>) -> Result<bool, String> {
        buf.resize(self.width * self.height * 3, 0);
        let out = self.child.stdout.as_mut().unwrap();
        let mut got = 0;
        while got < buf.len() {
            match out.read(&mut buf[got..]) {
                Ok(0) => break,
                Ok(n) => got += n,
                Err(e) => return Err(format!("讀取解碼後的第 {} 幀失敗：{e}", self.frame)),
            }
        }
        if got == 0 {
            let status = self.child.wait().map_err(|e| e.to_string())?;
            let err = self.stderr.take().and_then(|h| h.join().ok()).unwrap_or_default();
            if !status.success() || self.frame == 0 {
                return Err(format!("ffmpeg 解碼失敗（結束碼 {:?}，已讀 {} 幀）：{}", status.code(), self.frame, err.trim()));
            }
            return Ok(false);
        }
        if got < buf.len() {
            return Err(format!("第 {} 幀資料不完整（{got}/{} bytes）", self.frame, buf.len()));
        }
        self.frame += 1;
        Ok(true)
    }

    /// Waits for ffmpeg; returns its stderr when it failed.
    pub fn finish(mut self) -> Result<usize, String> {
        let status = self.child.wait().map_err(|e| e.to_string())?;
        let err = self.stderr.take().and_then(|h| h.join().ok()).unwrap_or_default();
        if !status.success() {
            return Err(format!("ffmpeg 解碼失敗（結束碼 {:?}）：{}", status.code(), err.trim()));
        }
        Ok(self.frame)
    }
}
