"""Development only: dump Python reference tensors so ok-tts (Rust) can be checked layer by layer.

Run with the TTS venv:
    E:\\TTS\\.venv-triton\\Scripts\\python.exe Tools/ok-tts/reference/dump_reference.py <out_dir>

Writes raw little-endian f32 / i64 files plus index.json (name -> dtype, shape).
Generation is greedy (talker and code predictor) so the codes are deterministic.
"""
import json
import os
import sys

os.environ.setdefault("HF_HOME", r"E:\TTS\hf-cache")
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

import numpy as np
import torch

MODEL_ID = "Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign"
TEXT = "这里是全年有许多人来访的观光地。"
INSTRUCT = "女性，女声，不是男生，不是男声。用普通话说话，咬字像北京普通话。音色偏厚，音高中低，语速慢，咬字清楚，句尾平平收住。"
TOKENIZER_CASES = [
    "<|im_start|>assistant\n这里是全年有许多人来访的观光地。<|im_end|>\n<|im_start|>assistant\n",
    "<|im_start|>user\n女性，女声。音色偏厚，Hello world 123！<|im_end|>\n",
    "  spaces   and\ttabs\n\nnewlines… 「引号」♡～",
]


def main():
    out = os.path.abspath(sys.argv[1])
    os.makedirs(out, exist_ok=True)
    index = {}

    def put(name, tensor):
        array = tensor.detach().float().cpu().numpy() if torch.is_tensor(tensor) and tensor.is_floating_point() else np.asarray(tensor.detach().cpu() if torch.is_tensor(tensor) else tensor)
        if array.dtype.kind == "f":
            array = array.astype("<f4")
        else:
            array = array.astype("<i8")
        array.tofile(os.path.join(out, name + ".bin"))
        index[name] = {"dtype": str(array.dtype), "shape": list(array.shape)}

    from faster_qwen3_tts import FasterQwen3TTS
    from faster_qwen3_tts.generate import fast_generate

    tts = FasterQwen3TTS.from_pretrained(MODEL_ID)
    tts.predictor_graph.do_sample = False
    base = tts.model

    tokenizer = base.processor.tokenizer
    index["tokenizer_cases"] = [{"text": t, "ids": tokenizer(t)["input_ids"]} for t in TOKENIZER_CASES]

    m, talker, config, tie, tam, tth, tpe = tts._prepare_generation_custom(text=TEXT, language="chinese", speaker=None, instruct=INSTRUCT)
    index["assistant_ids"] = base._tokenize_texts([base._build_assistant_text(TEXT)])[0][0].tolist()
    index["instruct_ids"] = base._tokenize_texts([base._build_instruct_text(INSTRUCT)])[0][0].tolist()
    put("talker_input_embeds", tie[0])
    put("trailing_text_hiddens", tth[0])
    put("tts_pad_embed", tpe.reshape(-1))

    with torch.inference_mode():
        out_prefill = talker.forward(inputs_embeds=tie, attention_mask=tam, use_cache=True, output_hidden_states=True,
                                     return_dict=True, trailing_text_hidden=tth, tts_pad_embed=tpe,
                                     generation_step=None, past_hidden=None, past_key_values=None)
        put("prefill_logits_last", out_prefill.logits[0, -1])
        put("prefill_hidden_last", out_prefill.past_hidden.reshape(-1))
        talker.rope_deltas = None
        codes, _ = fast_generate(talker=talker, talker_input_embeds=tie, attention_mask=tam, trailing_text_hiddens=tth,
                                 tts_pad_embed=tpe, config=config, predictor_graph=tts.predictor_graph,
                                 talker_graph=tts.talker_graph, max_new_tokens=400, min_new_tokens=2,
                                 temperature=0.65, top_k=30, do_sample=False, repetition_penalty=1.05)
    put("codes_greedy", codes)

    decoder = m.speech_tokenizer.model.decoder
    captured = {}

    def hook(name):
        def fn(_module, _inputs, output):
            value = output.last_hidden_state if hasattr(output, "last_hidden_state") else output
            captured[name] = value
        return fn

    decoder.pre_conv.register_forward_hook(hook("codec_pre_conv"))
    decoder.pre_transformer.register_forward_hook(hook("codec_pre_transformer"))
    decoder.upsample.register_forward_hook(hook("codec_upsample"))
    decoder.upsample[1].register_forward_hook(hook("codec_upsample_out"))
    decoder.decoder[0].register_forward_hook(hook("codec_decoder0"))
    decoder.decoder[1].register_forward_hook(hook("codec_block0"))
    decoder.decoder[4].register_forward_hook(hook("codec_block3"))
    quantized = {}
    decoder.quantizer.rvq_first.register_forward_hook(lambda *a: None)
    with torch.inference_mode():
        codes_bt = codes.unsqueeze(0).transpose(1, 2)
        quantized["q"] = decoder.quantizer.decode(codes_bt)
        wav = decoder(codes_bt)
    index["codec_dtype"] = str(next(decoder.parameters()).dtype)
    put("codec_quantized", quantized["q"][0])
    for name, value in captured.items():
        if torch.is_tensor(value):
            put(name, value[0])
    put("codec_wav", wav.reshape(-1))

    with open(os.path.join(out, "index.json"), "w", encoding="utf-8") as handle:
        json.dump(index, handle, ensure_ascii=False, indent=1)
    print("codes", tuple(codes.shape), "wav", tuple(wav.shape), "codec dtype", index["codec_dtype"])


if __name__ == "__main__":
    main()
