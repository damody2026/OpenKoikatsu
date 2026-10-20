use crate::{
    fitting::{Fitting, text_result},
    io::{Editor, atomic_json, id, now, owned_file, read_json, valid_id},
};
use anyhow::{Context, Result, bail, ensure};
use base64::Engine;
use rmcp::{
    ServerHandler,
    model::*,
    service::{RequestContext, RoleServer},
};
use serde_json::{Value, json};
use std::{
    collections::HashMap,
    io::Write,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
};
use tokio_util::sync::CancellationToken;

pub fn validate(value: &Value, schema: &Value, where_: &str) -> Result<()> {
    let matches_kind = |kind: &str| match kind {
        "object" => value.is_object(),
        "array" => value.is_array(),
        "string" => value.is_string(),
        "number" => value.is_number(),
        "integer" => value.is_i64() || value.is_u64(),
        "boolean" => value.is_boolean(),
        _ => false,
    };
    if let Some(kind) = schema["type"].as_str() {
        ensure!(matches_kind(kind), "{where_}: expected {kind}");
    } else if let Some(kinds) = schema["type"].as_array() {
        ensure!(
            kinds
                .iter()
                .any(|kind| kind.as_str().is_some_and(matches_kind)),
            "{where_}: expected one of {kinds:?}"
        );
    }
    if let Some(options) = schema["enum"].as_array() {
        ensure!(options.contains(value), "{where_}: invalid choice");
    }
    if let Some(v) = value.as_f64() {
        ensure!(v.is_finite(), "{where_}: nonfinite number");
        for (key, inclusive, lower) in [
            ("minimum", true, true),
            ("maximum", true, false),
            ("exclusiveMinimum", false, true),
            ("exclusiveMaximum", false, false),
        ] {
            if let Some(bound) = schema[key].as_f64() {
                ensure!(
                    if lower {
                        if inclusive { v >= bound } else { v > bound }
                    } else if inclusive {
                        v <= bound
                    } else {
                        v < bound
                    },
                    "{where_}: outside {key}"
                );
            }
        }
    }
    if let Some(v) = value.as_str() {
        ensure!(v.len() <= 4096, "{where_}: string too long");
    }
    if let Some(v) = value.as_array() {
        ensure!(
            v.len() >= schema["minItems"].as_u64().unwrap_or(0) as usize
                && v.len() <= schema["maxItems"].as_u64().unwrap_or(10000) as usize,
            "{where_}: array length outside bounds"
        );
        for item in v {
            validate(item, &schema["items"], where_)?;
        }
    }
    if let Some(v) = value.as_object() {
        if let Some(required) = schema["required"].as_array() {
            for key in required {
                ensure!(
                    v.contains_key(key.as_str().context("Invalid schema")?),
                    "{where_}: missing {key}"
                );
            }
        }
        for (key, item) in v {
            if let Some(s) = schema["properties"].get(key) {
                validate(item, s, &format!("{where_}.{key}"))?;
            } else {
                ensure!(
                    schema["additionalProperties"] != false,
                    "{where_}: unknown property {key}"
                );
            }
        }
    }
    Ok(())
}
struct Job {
    cancel: CancellationToken,
    done: AtomicBool,
    failure: Mutex<Option<String>>,
}
struct Inner {
    pub editor: Arc<dyn Editor>,
    serial: Arc<tokio::sync::Mutex<()>>,
    jobs: Mutex<HashMap<String, Arc<Job>>>,
    definitions: Vec<Value>,
}
#[derive(Clone)]
pub struct App(Arc<Inner>);
impl App {
    pub fn new(editor: Arc<dyn Editor>) -> Result<Self> {
        Ok(Self(Arc::new(Inner {
            editor,
            serial: Arc::new(tokio::sync::Mutex::new(())),
            jobs: Mutex::new(HashMap::new()),
            definitions: serde_json::from_str(include_str!("../tools.json"))?,
        })))
    }
    pub fn record_error(&self, name: &str, error: &str) {
        let path = self.0.editor.root().join("errors.md");
        if let Ok(mut file) = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(path)
        {
            let _ = writeln!(
                file,
                "\n- {} `{name}`: {}",
                now(),
                error.replace(['\n', '\r'], " ")
            );
        }
    }
    fn native(&self, action: &str, args: &Value, cancel: &CancellationToken) -> Result<Value> {
        let data = self.0.editor.rpc(action, args, cancel)?;
        let mut images = vec![];
        let paths = if !data["image"].is_null() {
            vec![data["image"].clone()]
        } else {
            data["images"].as_array().cloned().unwrap_or_default()
        };
        for item in paths {
            let filename = item
                .as_str()
                .or_else(|| item["image"].as_str())
                .context("Missing image filename")?;
            let path = self.0.editor.root().join("images").join(filename);
            let owned = owned_file(self.0.editor.root(), "images", &path, 32_000_000)?;
            ensure!(
                path.extension()
                    .is_some_and(|p| p.eq_ignore_ascii_case("png")),
                "Expected PNG image"
            );
            images.push(json!({"type":"image","mimeType":"image/png","data":base64::engine::general_purpose::STANDARD.encode(std::fs::read(owned)?)}));
        }
        Ok(text_result(data, images))
    }
    pub async fn invoke(
        &self,
        name: &str,
        args: Value,
        cancel: CancellationToken,
    ) -> Result<Value> {
        let entry = self
            .0
            .definitions
            .iter()
            .find(|e| e["definition"]["name"] == name)
            .context("Unknown tool")?;
        validate(&args, &entry["definition"]["inputSchema"], "arguments")?;
        let action = entry["action"]
            .as_str()
            .context("Invalid action")?
            .to_owned();
        if action.starts_with("job_") {
            return self.job(&action, args).await;
        }
        let guard = if action == "progress" {
            None
        } else {
            Some(
                tokio::select! {guard=self.0.serial.clone().lock_owned()=>guard,_=cancel.cancelled()=>bail!("Request cancelled while waiting for editor")},
            )
        };
        let app = self.clone();
        tokio::task::spawn_blocking(move || {
            let _guard = guard;
            if let Some(action) = action.strip_prefix("fit_") {
                Fitting {
                    editor: app.0.editor.clone(),
                }
                .call(action, &args, &cancel)
            } else {
                app.native(&action, &args, &cancel)
            }
        })
        .await?
    }
    async fn job(&self, action: &str, mut args: Value) -> Result<Value> {
        if action == "job_start" {
            let identifier = id();
            let job = Arc::new(Job {
                cancel: CancellationToken::new(),
                done: AtomicBool::new(false),
                failure: Mutex::new(None),
            });
            {
                let mut jobs = self.0.jobs.lock().unwrap();
                ensure!(
                    jobs.values()
                        .filter(|j| !j.done.load(Ordering::Acquire))
                        .count()
                        < 8,
                    "Background fit queue limit reached (8)"
                );
                jobs.retain(|_, j| !j.done.load(Ordering::Acquire));
                atomic_json(
                    &self
                        .0
                        .editor
                        .root()
                        .join(format!("fitting/job-{identifier}.json")),
                    &json!({"job":identifier,"status":"queued"}),
                )?;
                jobs.insert(identifier.clone(), job.clone());
            }
            let app = self.clone();
            let worker_id = identifier.clone();
            args["_run_id"] = identifier.clone().into();
            tokio::spawn(async move {
                let guard = app.0.serial.clone().lock_owned().await;
                let worker = app.clone();
                let job2 = job.clone();
                let identifier = worker_id.clone();
                let outcome=tokio::task::spawn_blocking(move||->Result<()>{let _guard=guard;let path=worker.0.editor.root().join(format!("fitting/job-{identifier}.json"));let result=(||->Result<Value>{ensure!(!job2.cancel.is_cancelled(),"Fit cancelled while queued");atomic_json(&path,&json!({"job":identifier,"status":"running"}))?;Fitting{editor:worker.0.editor.clone()}.call("fit",&args,&job2.cancel)})();let data=match result{Ok(result)=>json!({"job":identifier,"status":"completed","result":result}),Err(e)=>{worker.record_error("body_fit_start",&e.to_string());json!({"job":identifier,"status":if job2.cancel.is_cancelled(){"cancelled"}else{"failed"},"error":e.to_string()})}};atomic_json(&path,&data)}).await;
                let failure = match outcome {
                    Ok(Ok(())) => None,
                    Ok(Err(e)) => Some(e.to_string()),
                    Err(e) => Some(e.to_string()),
                };
                if let Some(error) = failure {
                    *job.failure.lock().unwrap() = Some(error.clone());
                    app.record_error("body_fit_start", &error);
                }
                job.done.store(true, Ordering::Release);
            });
            return Ok(text_result(
                json!({"job":identifier,"status":"queued"}),
                vec![],
            ));
        }
        let identifier = args["job"].as_str().context("Missing job")?;
        valid_id(identifier)?;
        let path = self
            .0
            .editor
            .root()
            .join(format!("fitting/job-{identifier}.json"));
        let mut data = read_json(&path, 64_000_000)?;
        let job = self.0.jobs.lock().unwrap().get(identifier).cloned();
        if action == "job_cancel"
            && job
                .as_ref()
                .is_some_and(|j| !j.done.load(Ordering::Acquire))
        {
            job.as_ref().unwrap().cancel.cancel();
            data["status"] = "cancelling".into();
        } else if matches!(data["status"].as_str(), Some("running" | "queued")) {
            match job {
                None => {
                    data["status"] = "interrupted".into();
                    data["error"] =
                        "Owning server stopped; inspect checkpoint and resume explicitly".into();
                }
                Some(j) if j.done.load(Ordering::Acquire) => {
                    data["status"] = "failed".into();
                    data["error"] = j
                        .failure
                        .lock()
                        .unwrap()
                        .clone()
                        .unwrap_or_else(|| "Worker stopped without saving final status".into())
                        .into();
                }
                _ => {}
            }
        }
        let report = path.with_file_name(format!("run-{identifier}.json"));
        if report.exists() {
            let mut progress = read_json(&report, 64_000_000)?;
            let count = progress["evaluations"].as_array().map_or(0, Vec::len);
            progress.as_object_mut().unwrap().remove("evaluations");
            progress["evaluation_count"] = count.into();
            data["progress"] = progress;
        }
        let mut images = vec![];
        if data["result"].is_object() {
            let result = data["result"].clone();
            data["result"] = serde_json::from_str(
                result["content"][0]["text"]
                    .as_str()
                    .context("Invalid job result")?,
            )?;
            if args["include_images"] == true {
                images = result["content"].as_array().context("Invalid result")?[1..].to_vec();
            }
        }
        Ok(text_result(data, images))
    }
    pub async fn shutdown(&self) {
        let jobs: Vec<_> = self.0.jobs.lock().unwrap().values().cloned().collect();
        for job in &jobs {
            job.cancel.cancel();
        }
        while jobs.iter().any(|j| !j.done.load(Ordering::Acquire)) {
            tokio::time::sleep(std::time::Duration::from_millis(25)).await;
        }
    }
}
impl ServerHandler for App {
    async fn list_tools(
        &self,
        _request: Option<PaginatedRequestParams>,
        _context: RequestContext<RoleServer>,
    ) -> Result<ListToolsResult, rmcp::ErrorData> {
        serde_json::from_value(json!({"tools":self.0.definitions.iter().map(|e|e["definition"].clone()).collect::<Vec<_>>()})).map_err(|e|rmcp::ErrorData::internal_error(e.to_string(),None))
    }
    async fn call_tool(
        &self,
        request: CallToolRequestParams,
        context: RequestContext<RoleServer>,
    ) -> Result<CallToolResponse, rmcp::ErrorData> {
        let args = Value::Object(request.arguments.unwrap_or_default());
        let result = match self.invoke(&request.name, args, context.ct).await {
            Ok(v) => v,
            Err(e) => {
                self.record_error(&request.name, &e.to_string());
                json!({"content":[{"type":"text","text":e.to_string()}],"isError":true})
            }
        };
        serde_json::from_value::<CallToolResult>(result)
            .map(Into::into)
            .map_err(|e| rmcp::ErrorData::internal_error(e.to_string(), None))
    }
    fn get_info(&self) -> ServerConfig {
        ServerConfig::new(ServerCapabilities::builder().enable_tools().build())
            .with_server_info(Implementation::new("openkoikatsu-character-editor", "2.0.0"))
            .with_instructions("Read character_state first. Use current session/revision; apply staged properties before fitting. Re-query after cancellation or timeouts.")
    }
}
