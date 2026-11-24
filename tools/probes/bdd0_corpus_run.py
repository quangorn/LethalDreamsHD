#!/usr/bin/env python3
"""Run the checked-in v12 bdd0_native corpus with semantic assertions.

Positive: tools/probes/bdd0/corpus/synth_v12_native.jsonl
  rc 0 AND exact per-check counts (so an all-SKIP capture cannot pass):
  native_seq 4/0/0, native_args 1/3/0, res_match 4/0/0, state_match 4/0/0,
  leaf_value 1/3/0 (листья 45c370/45c6f0 заявляют значения; out-тройка 45c6f0
  восстановлена из потока и идёт в nno, а не в pass — см. B0_POS_RESYNC_WHY).
Positive: tools/probes/bdd0/corpus/synth_band_begin_zero.jsonl
  rc 0, leaf_seq 1/0/0, leaf_value 0/1/0 — begin == 0 при end != 0: голова
  обязана быть NULL (правило TEST/JZ в 45af50), порт не идёт по длине.
Positive: tools/probes/bdd0/corpus/synth_band_rec.jsonl
  rc 0, leaf_value 1/0/0 — входная лента пуста, 45eee0 перестраивает её;
  bdd0_res несёт "band":[begin,end], записанное в bdd0_native ww [0x5c,0x60];
  реплей применяет recorded-band и claim головы СВЕРЯЕТСЯ (не SKIP).
Positive: tools/probes/bdd0/corpus/synth_band_trunc.jsonl
  rc 0, leaf_value 2/0/0 — две ленты не кратны 0x18: +25 → 1 и −25 → −1
  (усечение к нулю, не floor). Claim длины сверяется на обоих тиках.
Positive: tools/probes/bdd0/corpus/synth_pos_ctx_null.jsonl
  rc 0, leaf_value 0/1/0 — ai_ctx == 0: лист 45c6f0 идёт НУЛЕВОЙ веткой натива
  (`MOV EAX,[ECX]; XOR ECX,ECX; CMP EAX,ECX; JNZ`), out[0..2] = 0. w такой
  заявки восстановлен из потока ⇒ nno, не pass.
Positive: tools/probes/bdd0/corpus/synth_pos3_eq.jsonl
  rc 0, leaf_value 0/1/0 (skip даёт только w листа 45c6f0 — реконструкция из
  потока; ни одно поле заявки 403e80 в списке несравнимых не значится), al=0,
  out=0. Тройки ctx и result+4 равны ⇒ натив возвращает 1 (истинная ветка
  листа, в захватах не наблюдаемая).
Positive: tools/probes/bdd0/corpus/synth_pos3_neq.jsonl
  rc 0, leaf_value 0/1/0, но 403e80 возвращает 0: третья пара тройки
  различается (3.0 против 4.0), B6 уводит в b12 — это обычный вход остального
  тика, а не выход, — и B55/B56 достигаются. Вместе с pos3_eq покрывает обе
  ветки листа; реализацию «сравнил только две пары» ловит именно этот файл
  (leaf_value fail `va=0x403e80 ret port=1 native=0`).
Positive: tools/probes/bdd0/corpus/synth_pos3_nan.jsonl
  rc 0, leaf_value 0/1/0, 403e80 возвращает 0: первые две пары равны, третья —
  NaN (0x7fc00000). Маска 0x44 тестируется через PF, поэтому unordered обязан
  идти в «не равно»; прочтение «равно или unordered» даёт 1 и ловится
  (leaf_value fail, самопроверка генератора rc 2). Это единственный кейс,
  который проверяет правило unordered: модель и порт иначе просто повторяют
  друг друга.
Positive: tools/probes/bdd0/corpus/synth_ctx_pos.jsonl
  rc 0, leaf_seq 1/0/0, leaf_value 1/0/0 — v15-захват несёт независимую
  pos-тройку `bdd0_in."ctx_pos"` (`[*(unit+0x138)+0xc]`): заявка листа 45c6f0
  сверяется побитово по w (не SKIP-тавтология B0_POS_RESYNC_WHY), а сама
  тройка участвует в ветвлении 403e80 — тик всё равно воспроизводится.
Positive: tools/probes/bdd0/corpus/synth_chain_nested.jsonl
  rc 0, leaf_seq 1/0/0, `nested_leaf=1` — запись bdd0_leaf нового сайта
  0x458142 → 0x456b60 (args[0] = world, ret = узел) снята ВНУТРИ 0x458050;
  порт replay-ит 0x458050 целиком, поэтому загрузчик выносит такую запись из
  окна тика (BDD0_NESTED_SITE_GRID_ANCHOR) и поток остаётся самосогласованным.
Positive: tools/probes/bdd0/corpus/synth_eee0_missing.jsonl
  rc 0, native_seq 1/0/0, res_match 0/1/0, leaf_value 1/0/0, `eee0_synth=1` —
  запись bdd0_native для 0x45eee0 вырезана из synth_band_rec: так выглядит
  захват с LDHOOK_BATTLE_CAPTURE_E0=0. Harness подаёт пост-состояние u170 из
  независимого `bdd0_res` (единственное поле ww, читаемое телом после вызова),
  поток не двигает; u170 в res_match честно SKIP (значение стало входом).
Negatives:
  dropped/trap/order      -> --check native_seq, rc 1
  corrupt_native_args     -> --check native_args, rc 1, arg mismatch reported
  corrupt_native_ptr_unbound -> --check native_args, rc 0 but pass=0 skip=3 and
                             a visible `unbound` counter (not rc0-with-pass)
  corrupt_leaf_ported_request -> rc 1, leaf_seq fail (в потоке запись
                             портированного листа там, где порт просит лист)
  corrupt_leaf_claim_extra / _missing -> rc 1, leaf_seq fail, leaf_value не
                             fail (лишняя/пропавшая запись claim — это поток)
  corrupt_band_count      -> rc 1, leaf_value 1/0/1 (записанный ret 45af50
                             тика +25 испорчен 1→2; тик −25 остаётся PASS):
                             проверка значения длины живая
  corrupt_pos_w_count     -> --check leaf_value, rc 1: у записи 45c6f0 два
                             dword вместо трёх — счётчик out-канала живой
  corrupt_pos_w_value     -> --check leaf_value, rc 1: ai_ctx == 0 (порт пишет
                             нули), а в записи w[1] испорчен — значение
                             сверяется, когда заявка НЕ равна реконструкции
  corrupt_pos3_ret        -> --check leaf_value, rc 1, `va=0x403e80 ret port=...
                             native=0`: ret листа 403e80 — сверяемое поле даже
                             на тике с равными тройками (pos3_eq), т.е. истинная
                             ветка не вакуумна.
  corrupt_ctx_pos         -> --check leaf_value, rc 1, `va=0x45c6f0 w[0]
                             port=0x40800000 native=0x3f800000`: bdd0_in
                             "ctx_pos" испорчен (x = 4.0), запись листа несёт
                             истину (1.0). С v15 арена берёт тройку из захвата,
                             поэтому расхождение видно ПО ИМЕНИ поля — проверка
                             ctx_pos не вакуумна.
 
Build first: python3 tools/probes/bdd0_harness_build.py
Usage: python3 tools/probes/bdd0_corpus_run.py
"""
import os
import re
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

HARNESS = paths.win_path(os.path.join(_ROOT, "build", "ldhook", "bdd0_harness.exe"))
CORPUS = paths.win_path(os.path.join(_HERE, "bdd0", "corpus"))

# Expected per-check counts (pass, skip, fail) per corpus file, positives
# and negatives alike.
EXPECT = {
    "synth_v12_native.jsonl": {
        "native_seq": (4, 0, 0),
        "native_args": (1, 3, 0),
        "res_match": (4, 0, 0),
        "state_match": (4, 0, 0),
        "leaf_value": (1, 3, 0),
    },
    "synth_band_begin_zero.jsonl": {
        "leaf_seq": (1, 0, 0),
        "leaf_value": (0, 1, 0),
    },
    "synth_band_rec.jsonl": {
        "leaf_value": (1, 0, 0),
    },
    "synth_band_trunc.jsonl": {
        "leaf_value": (2, 0, 0),
    },
    "synth_pos_ctx_null.jsonl": {
        "leaf_value": (0, 1, 0),
    },
    "synth_pos3_eq.jsonl": {
        "leaf_value": (0, 1, 0),
        "res_match": (1, 0, 0),
        "state_match": (1, 0, 0),
        "leaf_seq": (1, 0, 0),
    },
    "synth_pos3_neq.jsonl": {
        "leaf_value": (0, 1, 0),
        "res_match": (1, 0, 0),
        "state_match": (1, 0, 0),
        "leaf_seq": (1, 0, 0),
    },
    "synth_pos3_nan.jsonl": {
        "leaf_value": (0, 1, 0),
        "res_match": (1, 0, 0),
        "state_match": (1, 0, 0),
        "leaf_seq": (1, 0, 0),
    },
    "corrupt_band_count.jsonl": {
        "leaf_value": (1, 0, 1),
    },
    "synth_ctx_pos.jsonl": {
        "leaf_seq": (1, 0, 0),
        "leaf_value": (1, 0, 0),
    },
    "synth_chain_nested.jsonl": {
        "leaf_seq": (1, 0, 0),
    },
    "corrupt_ctx_pos.jsonl": {
        "leaf_value": (0, 0, 1),
    },
    "synth_eee0_missing.jsonl": {
        "native_seq": (1, 0, 0),
        "res_match": (0, 1, 0),
        "leaf_value": (1, 0, 0),
    },
}

# name, expected rc, single check to run (None = all)
CASES = [
    ("synth_v12_native.jsonl", 0, None),
    ("synth_band_begin_zero.jsonl", 0, None),
    ("synth_band_rec.jsonl", 0, None),
    ("synth_band_trunc.jsonl", 0, None),
    ("synth_pos_ctx_null.jsonl", 0, None),
    ("synth_pos3_eq.jsonl", 0, None),
    ("synth_pos3_neq.jsonl", 0, None),
    ("synth_pos3_nan.jsonl", 0, None),
    ("synth_ctx_pos.jsonl", 0, None),
    ("synth_chain_nested.jsonl", 0, None),
    ("synth_eee0_missing.jsonl", 0, None),
    ("corrupt_ctx_pos.jsonl", 1, "leaf_value"),
    ("corrupt_native_dropped.jsonl", 1, "native_seq"),
    ("corrupt_native_trap.jsonl", 1, "native_seq"),
    ("corrupt_native_order.jsonl", 1, "native_seq"),
    ("corrupt_native_args.jsonl", 1, "native_args"),
    ("corrupt_native_ptr_unbound.jsonl", 0, "native_args"),
    ("corrupt_band_count.jsonl", 1, None),
    ("corrupt_leaf_ported_request.jsonl", 1, None),
    ("corrupt_leaf_claim_extra.jsonl", 1, None),
    ("corrupt_leaf_claim_missing.jsonl", 1, None),
    ("corrupt_pos_w_count.jsonl", 1, "leaf_value"),
    ("corrupt_pos_w_value.jsonl", 1, "leaf_value"),
    ("corrupt_pos3_ret.jsonl", 1, "leaf_value"),
]

# negatives that must FAIL leaf_value itself, with the out-channel diagnostic
LEAF_VALUE_NEG = {
    "corrupt_pos_w_count.jsonl": "w count",
    "corrupt_pos_w_value.jsonl": "w[1]",
    "corrupt_pos3_ret.jsonl": "va=0x403e80 ret",
    "corrupt_ctx_pos.jsonl": "va=0x45c6f0 w[0]",
}

# negatives whose failure must be attributed to leaf_seq, not leaf_value
LEAF_SEQ_NEG = {
    "corrupt_leaf_ported_request.jsonl",
    "corrupt_leaf_claim_extra.jsonl",
    "corrupt_leaf_claim_missing.jsonl",
}


def run(name, check):
    path = os.path.join(CORPUS, name)
    cmd = [HARNESS, "--impl", "port"]
    if check:
        cmd += ["--check", check]
    cmd.append(path)
    p = subprocess.run(cmd, capture_output=True)
    text = (p.stdout + p.stderr).decode("utf-8", "replace")
    counts = {}
    for m in re.finditer(r"^\s+(\w+)\s+pass=(\d+) skip=(\d+) fail=(\d+)\s*$",
                         text, re.M):
        counts[m.group(1)] = (int(m.group(2)), int(m.group(3)),
                              int(m.group(4)))
    return p.returncode, text, counts


def main():
    if not os.path.isfile(HARNESS):
        sys.exit("no %s -- run: python3 tools/probes/bdd0_harness_build.py"
                 % HARNESS)
    bad = 0
    for name, want_rc, check in CASES:
        rc, text, counts = run(name, check)
        ok = (rc == want_rc)
        detail = "rc=%d" % rc
        if name in EXPECT:
            for k, v in EXPECT[name].items():
                got = counts.get(k)
                detail += " %s=%s" % (k, got)
                if got != v:
                    ok = False
            if name == "synth_chain_nested.jsonl":
                # запись вложенного сайта 0x458142 действительно снята и
                # вынесена из окна тика (а не отсутствует вовсе)
                has_nested = "nested_leaf=1" in text
                ok = ok and has_nested
                detail += " nested_leaf=%s" % ("1" if has_nested else "0")
            if name == "synth_eee0_missing.jsonl":
                # запись 0x45eee0 отсутствует → мост из bdd0_res сработал,
                # res_match честно SKIP (u170 не считается независимым)
                has_synth = "eee0_synth=1" in text
                ok = ok and has_synth
                detail += " eee0_synth=%s" % ("1" if has_synth else "0")
        elif check == "native_args":
            na = counts.get("native_args", (0, 0, 0))
            if name == "corrupt_native_args.jsonl":
                ok = ok and na[2] >= 1 and "arg[" in text
                detail += " native_args=%s" % (na,)
            else:
                ok = ok and na[0] == 0 and na[1] >= 1 and na[2] == 0 \
                     and "unbound" in text
                detail += " native_args=%s unbound=%s" % (
                    na, "yes" if "unbound" in text else "no")
        elif name in LEAF_SEQ_NEG:
            ls = counts.get("leaf_seq", (0, 0, 0))
            lv = counts.get("leaf_value", (0, 0, 0))
            ok = ok and ls[2] >= 1 and lv[2] == 0
            detail += " leaf_seq=%s leaf_value=%s" % (ls, lv)
        elif name in LEAF_VALUE_NEG:
            lv = counts.get("leaf_value", (0, 0, 0))
            tag = LEAF_VALUE_NEG[name]
            ok = ok and lv[2] >= 1 and (tag in text)
            detail += " leaf_value=%s diag=%s" % (
                lv, "yes" if tag in text else "no")
        print("  %-36s %s  %s" % (name, "PASS" if ok else "FAIL", detail))
        if not ok:
            bad += 1
    print("bdd0 corpus: %s (%d failures)"
          % ("OK" if bad == 0 else "FAILED", bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())