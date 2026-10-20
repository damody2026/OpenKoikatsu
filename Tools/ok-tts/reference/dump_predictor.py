"""Development only: step the code predictor eagerly for frame 0 of the reference and dump per-step logits.

    E:\\TTS\\.venv-triton\\Scripts\\python.exe Tools/ok-tts/reference/dump_predictor.py <ref_dir>
"""
import json
import os
import sys

os.environ.setdefault("HF_HOME", r"E:\TTS\hf-cache")
import numpy as np
import torch
from transformers import DynamicCache


def main():
    ref = sys.argv[1]
    from qwen_tts import Qwen3TTSModel
    base = Qwen3TTSModel.from_pretrained("Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign", device_map="cuda", torch_dtype=torch.bfloat16, attn_implementation="sdpa")
    talker = base.model.talker
    cp = talker.code_predictor
    past = torch.from_numpy(np.fromfile(os.path.join(ref, "prefill_hidden_last.bin"), dtype="<f4")).to("cuda", torch.bfloat16).view(1, 1, -1)
    first = int(np.fromfile(os.path.join(ref, "codes_greedy.bin"), dtype="<i8")[0])
    out = {}
    with torch.inference_mode():
        tok = torch.tensor([[first]], device="cuda")
        h = cp.small_to_mtp_projection(torch.cat([past, talker.get_input_embeddings()(tok)], dim=1))
        cache = DynamicCache()
        o = cp.model(inputs_embeds=h, past_key_values=cache, use_cache=True, cache_position=torch.arange(2, device="cuda"))
        logits = cp.lm_head[0](o.last_hidden_state[:, -1])
        toks = [int(logits.argmax(-1))]
        out["step0"] = logits.float().cpu().numpy().reshape(-1)
        for cb in range(1, 15):
            e = cp.small_to_mtp_projection(cp.model.codec_embedding[cb - 1](torch.tensor([[toks[-1]]], device="cuda")))
            o = cp.model(inputs_embeds=e, past_key_values=cache, use_cache=True, cache_position=torch.tensor([cb + 1], device="cuda"))
            logits = cp.lm_head[cb](o.last_hidden_state[:, -1])
            out[f"step{cb}"] = logits.float().cpu().numpy().reshape(-1)
            toks.append(int(logits.argmax(-1)))
    for k, v in out.items():
        v.astype("<f4").tofile(os.path.join(ref, f"pred_{k}.bin"))
    json.dump({"first": first, "tokens": toks}, open(os.path.join(ref, "pred_steps.json"), "w"))
    print("eager predictor tokens", toks)


if __name__ == "__main__":
    main()
