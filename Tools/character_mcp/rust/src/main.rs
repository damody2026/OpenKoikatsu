use anyhow::{Context, Result, ensure};
use clap::{Parser, Subcommand};
use ok_character_mcp::{
    app::App,
    io::{Bridge, Editor},
};
use rmcp::ServiceExt;
use serde_json::{Value, json};
use std::{path::PathBuf, sync::Arc};
use tokio_util::sync::CancellationToken;

#[derive(Parser)]
#[command(
    version,
    about = "OpenKoikatsu character editor MCP server and body fitting CLI"
)]
struct Args {
    #[arg(long, global = true, default_value = "Saved/CharacterMCP")]
    mailbox: PathBuf,
    #[arg(long, global = true, default_value_t = 1800.0)]
    timeout: f64,
    #[command(subcommand)]
    command: Option<Command>,
}
#[derive(Subcommand)]
enum Command {
    Fit(Box<FitArgs>),
}
#[derive(Parser)]
struct FitArgs {
    #[arg(long)]
    reference: Option<String>,
    #[arg(long)]
    front: Option<PathBuf>,
    #[arg(long)]
    side: Option<PathBuf>,
    #[arg(long)]
    back: Option<PathBuf>,
    #[arg(long,default_value="mask",value_parser=["mask","alpha","background"])]
    mode: String,
    #[arg(long,default_value="right",value_parser=["left","right"])]
    side_view: String,
    #[arg(long, num_args = 3)]
    background: Option<Vec<u8>>,
    #[arg(long)]
    physical_height: Option<f64>,
    #[arg(long,num_args=1..)]
    keys: Option<Vec<String>>,
    #[arg(long, default_value_t = 160)]
    max_evaluations: u64,
    #[arg(long, default_value_t = 1800.0)]
    max_seconds: f64,
    #[arg(long, default_value_t = 0.025)]
    step_fraction: f64,
    #[arg(long, default_value_t = 0.002)]
    target_loss: f64,
    #[arg(long)]
    resume: Option<String>,
    #[arg(long)]
    sensitivity: bool,
    #[arg(long, default_value = "MCP fitted")]
    save_name: String,
}
fn raw(v: Value) -> Result<Value> {
    Ok(serde_json::from_str(
        v["content"][0]["text"].as_str().context("Invalid result")?,
    )?)
}
async fn fit(
    app: App,
    editor: Arc<dyn Editor>,
    args: FitArgs,
    cancel: CancellationToken,
) -> Result<Value> {
    let current = raw(app
        .invoke("character_state", json!({}), cancel.clone())
        .await?)?;
    ensure!(
        current["busy"] == false && current["pending_properties"].as_u64() == Some(0),
        "Wait for current work and apply/reset pending properties before fitting"
    );
    app.invoke("editor_action",json!({"session":current["session"],"revision":current["revision"],"command":"clothing","clothing_kind":102}),cancel.clone()).await?;
    let state = raw(app
        .invoke("character_state", json!({}), cancel.clone())
        .await?)?;
    app.invoke("capture_configure",json!({"session":state["session"],"revision":state["revision"],"tpose":true,"hide_hair":true,"hide_accessories":true}),cancel.clone()).await?;
    let state = raw(app
        .invoke("character_state", json!({}), cancel.clone())
        .await?)?;
    let reference = if let Some(reference) = args.reference {
        reference
    } else {
        let mut request =
            json!({"session":state["session"],"mode":args.mode,"side_view":args.side_view});
        for (name, path) in [
            ("front", args.front),
            ("side", args.side),
            ("back", args.back),
        ] {
            request[name] = std::path::absolute(
                path.context("Supply --reference or all three --front/--side/--back images")?,
            )?
            .display()
            .to_string()
            .into();
        }
        if let Some(background) = args.background {
            request["background"] = json!(background);
        }
        if let Some(height) = args.physical_height {
            request["physical_height"] = json!(height);
        }
        raw(app
            .invoke("reference_prepare", request, cancel.clone())
            .await?)?["reference"]
            .as_str()
            .context("Missing reference")?
            .into()
    };
    let mut request = json!({"session":state["session"],"revision":state["revision"],"reference":reference,"step_fraction":args.step_fraction});
    if let Some(keys) = args.keys {
        request["keys"] = json!(keys);
    }
    if !args.sensitivity {
        request["max_evaluations"] = args.max_evaluations.into();
        request["max_seconds"] = args.max_seconds.into();
        request["target_loss"] = args.target_loss.into();
        if let Some(resume) = args.resume {
            request["resume"] = resume.into();
        }
    }
    let mut result = raw(app
        .invoke(
            if args.sensitivity {
                "body_sensitivity"
            } else {
                "body_fit"
            },
            request,
            cancel.clone(),
        )
        .await?)?;
    if !args.sensitivity {
        let state = raw(app
            .invoke("character_state", json!({}), cancel.clone())
            .await?)?;
        let saved=raw(app.invoke("character_save",json!({"session":state["session"],"revision":state["revision"],"name":args.save_name}),cancel).await?)?;
        result["saved_card"] = saved["saved_card"].clone();
    }
    let _ = editor;
    Ok(result)
}
#[tokio::main]
async fn main() -> Result<()> {
    let args = Args::parse();
    ensure!(
        args.timeout.is_finite() && args.timeout > 0.,
        "Timeout must be positive"
    );
    let editor: Arc<dyn Editor> = Arc::new(Bridge::new(args.mailbox, args.timeout)?);
    let app = App::new(editor.clone())?;
    if let Some(Command::Fit(args)) = args.command {
        let token = CancellationToken::new();
        let signal = token.clone();
        let listener = tokio::spawn(async move {
            if tokio::signal::ctrl_c().await.is_ok() {
                signal.cancel();
            }
        });
        let outcome = fit(app.clone(), editor, *args, token).await;
        listener.abort();
        if let Err(e) = &outcome {
            app.record_error("fit CLI", &e.to_string());
        }
        println!("{}", serde_json::to_string_pretty(&outcome?)?);
    } else {
        let service = app.clone().serve(rmcp::transport::stdio()).await?;
        let outcome = service.waiting().await;
        app.shutdown().await;
        outcome?;
    }
    Ok(())
}
