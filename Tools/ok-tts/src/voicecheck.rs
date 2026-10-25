//! `ok-tts voice-check`: do the voices stored in heroine cards (okVo) each sound like their own reference and not like
//! one another? Every card speaks the same corpus with its own voice (Base, x-vector clone, as in the game); every clip
//! and every card's reference recording is embedded with the Base speaker encoder; the report gives each heroine's
//! lines' similarity to every reference (cosine after subtracting the mean of all embeddings, as in `consistency`) and
//! a confusion matrix (which reference each line is closest to). A local listening page (index.html) lists every clip.
//! Auxiliary evidence: the acceptance is the listener's ear (TTS plan §4, §20.4).
//!
//!   ok-tts voice-check --cards DIR_OR_PNGS --corpus lines.txt --out DIR [--base DIR] [--seed N]
//!
//! Clips and the page go to --out (local only: the voices are not versioned and are not published).

use std::path::PathBuf;

use anyhow::{bail, Context, Result};
use serde_json::json;

use crate::audio::{decode_ogg, encode_ogg, peak_normalize, trim_silence, write_atomic};
use crate::engine::{Engine, SAMPLE_RATE};
use crate::speaker::{cosine, SpeakerEncoder};
use crate::talker::GenConfig;
use crate::text::clean_line;
use crate::weights::Store;

pub struct VoiceCheckArgs {
    pub cards: Vec<PathBuf>,
    pub corpus: PathBuf,
    pub out: PathBuf,
    pub base: PathBuf,
    pub voices: PathBuf,
    pub seed: u64,
    pub positions: usize,
}

struct Heroine {
    name: String,
    reference: Vec<f32>,
    clone_embedding: Vec<f32>,
    clips: Vec<Vec<f32>>,
    rtf: Vec<f64>,
}

fn html_escape(s: &str) -> String {
    s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;")
}

pub fn run(a: &VoiceCheckArgs) -> Result<()> {
    let lines: Vec<String> = std::fs::read_to_string(&a.corpus).with_context(|| format!("read {}", a.corpus.display()))?
        .lines().map(str::trim).filter(|l| !l.is_empty() && !l.starts_with('#'))
        .map(|l| clean_line(l.rsplit('\t').next().unwrap_or(l))).filter(|l| !l.is_empty()).collect();
    if lines.is_empty() {
        bail!("the corpus has no lines");
    }
    // The cards: given one by one, or every PNG in a folder; only those with a voice of their own.
    let mut cards = Vec::new();
    for c in &a.cards {
        if c.is_dir() {
            let mut found: Vec<PathBuf> = std::fs::read_dir(c)?.flatten().map(|e| e.path())
                .filter(|p| p.extension().is_some_and(|x| x.eq_ignore_ascii_case("png"))).collect();
            found.sort();
            cards.extend(found);
        } else {
            cards.push(c.clone());
        }
    }
    let mut heroines = Vec::new();
    for card in &cards {
        let Some(voice) = crate::cardvoice::read_file(card)? else {
            crate::log!("ok-tts: voice-check: {} has no voice of its own, skipped", card.display());
            continue;
        };
        let name = card.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
        heroines.push(Heroine { name, reference: decode_ogg(&voice.ref_ogg)?, clone_embedding: voice.embedding, clips: Vec::new(), rtf: Vec::new() });
    }
    if heroines.len() < 2 {
        bail!("need at least two cards with a voice (found {})", heroines.len());
    }
    std::fs::create_dir_all(&a.out)?;

    // 1. Every heroine speaks the corpus in her card's voice.
    let engine = Engine::load(&a.base, &a.voices, a.positions)?;
    for h in heroines.iter_mut() {
        let dir = a.out.join(&h.name);
        std::fs::create_dir_all(&dir)?;
        write_atomic(&dir.join("ref.ogg"), &encode_ogg(&h.reference, SAMPLE_RATE)?)?;
        for (i, line) in lines.iter().enumerate() {
            let s = engine.speak_clone(line, &h.clone_embedding, a.seed, GenConfig::default())?;
            let audio = s.samples.len().max(1) as f64 / SAMPLE_RATE as f64;
            h.rtf.push(s.gen_seconds / audio);
            let mut samples = s.samples;
            trim_silence(&mut samples, SAMPLE_RATE);
            peak_normalize(&mut samples);
            write_atomic(&dir.join(format!("{i:02}.ogg")), &encode_ogg(&samples, SAMPLE_RATE)?)?;
            h.clips.push(samples);
        }
        crate::log!("ok-tts: voice-check: {} spoke {} lines", h.name, lines.len());
    }
    drop(engine);

    // 2. Embeddings: each reference and each clip; centred on the mean of all of them.
    let store = Store::open(&a.base.join("model.safetensors"))?;
    let encoder = SpeakerEncoder::load(&store).context("speaker encoder")?;
    drop(store);
    let refs: Vec<Vec<f32>> = heroines.iter().map(|h| encoder.embed(&h.reference)).collect::<Result<_>>()?;
    let clips: Vec<Vec<Vec<f32>>> = heroines.iter().map(|h| h.clips.iter().map(|c| encoder.embed(c)).collect::<Result<Vec<_>>>()).collect::<Result<_>>()?;
    let all: Vec<&Vec<f32>> = refs.iter().chain(clips.iter().flatten()).collect();
    let dim = refs[0].len();
    let mean: Vec<f32> = (0..dim).map(|d| all.iter().map(|e| e[d]).sum::<f32>() / all.len() as f32).collect();
    let centre = |e: &[f32]| -> Vec<f32> { e.iter().zip(&mean).map(|(a, b)| a - b).collect() };
    let refs_c: Vec<Vec<f32>> = refs.iter().map(|e| centre(e)).collect();

    // 3. Each heroine's lines against every reference: mean centred cosine and how often each reference is the closest.
    let n = heroines.len();
    let mut score = vec![vec![0f64; n]; n];
    let mut confusion = vec![vec![0usize; n]; n];
    let mut per_line = Vec::new();
    for (i, hc) in clips.iter().enumerate() {
        let mut rows = Vec::new();
        for (k, e) in hc.iter().enumerate() {
            let c = centre(e);
            let s: Vec<f64> = refs_c.iter().map(|r| cosine(&c, r)).collect();
            let best = (0..n).max_by(|&x, &y| s[x].total_cmp(&s[y])).unwrap();
            confusion[i][best] += 1;
            for j in 0..n {
                score[i][j] += s[j] / hc.len() as f64;
            }
            rows.push(json!({"line": k, "own": s[i], "closest": heroines[best].name, "closest_score": s[best]}));
        }
        per_line.push(rows);
    }
    let names: Vec<&str> = heroines.iter().map(|h| h.name.as_str()).collect();
    let summary: Vec<serde_json::Value> = (0..n).map(|i| {
        let other = (0..n).filter(|&j| j != i).map(|j| score[i][j]).fold(f64::NEG_INFINITY, f64::max);
        let rtf = heroines[i].rtf.iter().sum::<f64>() / heroines[i].rtf.len().max(1) as f64;
        json!({"name": names[i], "own": score[i][i], "best_other": other, "margin": score[i][i] - other,
               "correct": confusion[i][i], "lines": clips[i].len(), "rtf_mean": rtf})
    }).collect();
    let report = json!({"corpus": a.corpus, "seed": a.seed, "names": names, "lines": lines, "score": score, "confusion": confusion,
                        "summary": summary, "per_line": per_line,
                        "note": "score[i][j]: mean centred cosine of heroine i's lines to heroine j's reference; confusion[i][j]: lines of i closest to j's reference"});
    write_atomic(&a.out.join("report.json"), serde_json::to_string_pretty(&report)?.as_bytes())?;

    // 4. The listening page.
    let mut h = String::from("<!doctype html><html lang=\"zh-Hant\"><head><meta charset=\"utf-8\"><title>女主聲線驗收</title><style>\
        body{font-family:system-ui,sans-serif;margin:16px;max-width:1100px}table{border-collapse:collapse;margin:8px 0}td,th{border:1px solid #bbb;padding:3px 6px;text-align:right}\
        th{background:#eee}.own{background:#d8f0d8}.bad{background:#f6d0d0}audio{height:28px;vertical-align:middle}h2{margin-top:28px}.t{text-align:left}</style></head><body>");
    h += "<h1>女主卡片聲線驗收</h1><p>每位女主用卡片裡的聲線唸同一組台詞。分數是去平均後的聲紋相似度（同一模型的 speaker encoder，只是輔助指標，最後以耳朵為準）。綠色格是自己，紅色格表示有台詞比較像別人。</p>";
    h += "<h2>各人對每位參考音的平均相似度</h2><table><tr><th></th>";
    for name in &names {
        h += &format!("<th>{}</th>", html_escape(name));
    }
    h += "<th>答對</th></tr>";
    for i in 0..n {
        h += &format!("<tr><th>{}</th>", html_escape(names[i]));
        for j in 0..n {
            let class = if i == j { "own" } else if score[i][j] >= score[i][i] { "bad" } else { "" };
            h += &format!("<td class=\"{class}\">{:+.3}</td>", score[i][j]);
        }
        h += &format!("<td>{}/{}</td></tr>", confusion[i][i], clips[i].len());
    }
    h += "</table>";
    for (i, hero) in heroines.iter().enumerate() {
        h += &format!("<h2>{}</h2><p>參考音：<audio controls preload=\"none\" src=\"{}/ref.ogg\"></audio>　自己 {:+.3}，最像的別人 {:+.3}</p><table><tr><th>#</th><th class=\"t\">台詞</th><th>試聽</th><th>自己</th><th>最像</th></tr>",
            html_escape(&hero.name), html_escape(&hero.name), score[i][i], summary[i]["best_other"].as_f64().unwrap_or(0.0));
        for (k, line) in lines.iter().enumerate() {
            let row = &per_line[i][k];
            let closest = row["closest"].as_str().unwrap_or("");
            let class = if closest == hero.name { "" } else { "bad" };
            h += &format!("<tr><td>{k}</td><td class=\"t\">{}</td><td><audio controls preload=\"none\" src=\"{}/{k:02}.ogg\"></audio></td><td>{:+.3}</td><td class=\"{class}\">{}</td></tr>",
                html_escape(line), html_escape(&hero.name), row["own"].as_f64().unwrap_or(0.0), html_escape(closest));
        }
        h += "</table>";
    }
    h += "</body></html>";
    write_atomic(&a.out.join("index.html"), h.as_bytes())?;
    println!("{}", serde_json::to_string_pretty(&json!({"summary": summary, "confusion": confusion}))?);
    println!("wrote {}", a.out.join("index.html").display());
    Ok(())
}
