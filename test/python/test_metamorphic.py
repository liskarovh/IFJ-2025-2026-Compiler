# -*- coding: utf-8 -*-
import os, random, subprocess, tempfile, pytest
from contextlib import contextmanager
from pathlib import Path
from token_parser import parse_tokens_flex, as_pairs
from conftest import supports_stdin, ERROR_CODES

# Deterministická „náhoda“ (lze řídit IFJ_METASEED=...)
random.seed(int(os.getenv("IFJ_METASEED", "0")))

# ---------------- helpers ----------------

def pairs(text: str):
    return as_pairs(parse_tokens_flex(text))

def short(s: str, n: int = 160) -> str:
    s = (s or "").strip().replace("\n", "\\n")
    return s if len(s) <= n else s[:n] + "…"

def assert_no_internal(rc: int, err: str, ctx: str):
    # pád signálem (rc < 0)
    assert rc >= 0, f"{ctx}: process crashed (rc={rc})"
    # heuristika: implementace driveru při interní chybě často píše 'INTERNAL'
    if "INTERNAL" in (err or "").upper():
        pytest.fail(f"{ctx}: internal error reported (stderr='{short(err)}')")

def classify(rc: int, err: str, codes) -> str:
    """Vrátí 'ok' | 'lex' | 'internal' | 'crash' | 'other' dle ERROR_CODES a stderr."""
    if rc == codes["SUCCESS"]:
        return "ok"
    if rc < 0:
        return "crash"
    err_up = (err or "").upper()
    if rc == codes.get("ERR_INTERNAL", 99) or "INTERNAL" in err_up:
        return "internal"
    # cokoliv nenulové a ne-internal bereme jako 'lex' (některé drivere vrací jiné rc než ERR_LEX)
    return "lex"

@contextmanager
def _tmpfile_with(text: str, suffix: str = ".wren"):
    f = tempfile.NamedTemporaryFile(mode="w", suffix=suffix, delete=False, encoding="utf-8")
    try:
        f.write(text); f.flush(); f.close()
        yield Path(f.name)
    finally:
        try: os.unlink(f.name)
        except Exception: pass

def supports_line_comments_probe(BIN) -> bool:
    """
    True, pokud vložení // na *prázdné* řádky nezmění tokenový proud a rc==0.
    Použijeme minimální validní skeleton.
    """
    base_src = 'import "ifj25" for Ifj\n\nclass Main {}\n'
    linecom_src = 'import "ifj25" for Ifj\n //c \nclass Main {}\n'
    with _tmpfile_with(base_src) as p0, _tmpfile_with(linecom_src) as p1:
        rc0, out0, err0 = run_file(BIN, p0)
        rc1, out1, err1 = run_file(BIN, p1)
    if rc0 != 0 or rc1 != 0:
        return False
    return pairs(out0) == pairs(out1)

# ---------------- neutral transforms ----------------

def inject_line_comments(source: str) -> str:
    """Vkládá // komentář pouze na whitespace-only řádky (sémanticky neutrální)."""
    out, i = [], 0
    in_str = in_ml = False; block = 0
    seen_nonws_on_line = False  # od posledního EOL jsme viděli neterminální znak?

    while i < len(source):
        ch = source[i]; nxt = source[i:i+2]

        # EOL – reset příznaku a případná injekce komentáře
        if ch in ('\r', '\n'):
            if not seen_nonws_on_line:
                # vlož //c těsně před EOL a zachovej typ EOL
                if ch == '\r' and i + 1 < len(source) and source[i+1] == '\n':
                    out.append(' //c \r\n'); i += 2
                else:
                    out.append(' //c \n'); i += 1
            else:
                if ch == '\r' and i + 1 < len(source) and source[i+1] == '\n':
                    out.append('\r\n'); i += 2
                else:
                    out.append('\n'); i += 1
            seen_nonws_on_line = False
            continue

        # ochrana stringů / """ / blokových komentářů
        if not in_ml and ch == '"' and block == 0 and (i == 0 or source[i-1] != '\\'):
            in_str = not in_str; out.append(ch); i += 1; seen_nonws_on_line = True; continue
        if not in_str and block == 0 and source.startswith('"""', i):
            in_ml = not in_ml; out.append('"""'); i += 3; seen_nonws_on_line = True; continue
        if not in_str and not in_ml and nxt == '/*':
            block += 1; out.append(nxt); i += 2; seen_nonws_on_line = True; continue
        if not in_str and not in_ml and nxt == '*/' and block:
            block -= 1; out.append(nxt); i += 2; seen_nonws_on_line = True; continue

        # běžné WS 1:1 (není to prázdný řádek → komentář nevkládáme)
        if ch in (' ', '\t'):
            out.append(ch); i += 1; continue

        # neterminální znak na řádku
        seen_nonws_on_line = True
        out.append(ch); i += 1

    return ''.join(out)

def inject_block_comments(source: str) -> str:
    """Občas vloží /*c*/ jen do běhů space/tab mimo stringy/komentáře."""
    out, i = [], 0
    in_str = in_ml = False; block = 0
    while i < len(source):
        ch = source[i]; nxt = source[i:i+2]
        if not in_ml and ch == '"' and block == 0 and (i == 0 or source[i-1] != '\\'):
            in_str = not in_str; out.append(ch); i += 1; continue
        if not in_str and block == 0 and source.startswith('"""', i):
            in_ml = not in_ml; out.append('"""'); i += 3; continue
        if not in_str and not in_ml and nxt == '/*':
            block += 1; out.append(nxt); i += 2; continue
        if not in_str and not in_ml and nxt == '*/' and block:
            block -= 1; out.append(nxt); i += 2; continue
        if not in_str and not in_ml and block == 0 and ch in (' ', '\t') and random.random() < 0.02:
            out.append('/*c*/')
        out.append(ch); i += 1
    return ''.join(out)

def shuffle_ws(source: str) -> str:
    """Bezpečné míchání whitespace: zachová počet EOL; EOL může být LF/CRLF; mezery ↔ taby."""
    out, i = [], 0
    in_str = in_ml = False; block = 0
    LWS = (' ', '\t', '\r', '\n')
    while i < len(source):
        if not in_ml and source.startswith('"""', i) and not in_str and block == 0:
            in_ml = not in_ml; out.append('"""'); i += 3; continue
        ch = source[i]; nxt = source[i:i+2]
        if not in_ml and ch == '"' and block == 0 and (i == 0 or source[i-1] != '\\'):
            in_str = not in_str; out.append(ch); i += 1; continue
        if not in_str and not in_ml and nxt == '/*':
            block += 1; out.append(nxt); i += 2; continue
        if not in_str and not in_ml and nxt == '*/' and block:
            block -= 1; out.append(nxt); i += 2; continue

        if not in_str and not in_ml and block == 0 and ch in LWS:
            j = i
            while j < len(source) and source[j] in LWS:
                if source[j] in ('\r', '\n'):
                    if source[j] == '\r' and j + 1 < len(source) and source[j+1] == '\n':
                        out.append('\r\n' if random.random() < 0.5 else '\n'); j += 2
                    else:
                        out.append('\r\n' if random.random() < 0.5 else '\n'); j += 1
                else:
                    k = j
                    while k < len(source) and source[k] in (' ', '\t'):
                        k += 1
                    out.append(' ' if random.random() < 0.5 else '\t')
                    j = k
            i = j; continue

        out.append(ch); i += 1
    return ''.join(out)

def to_crlf(s: str) -> str:
    """Zachovej počet EOL, jen normalizuj typ na CRLF."""
    out, i = [], 0
    while i < len(s):
        if s[i] == '\r':
            if i + 1 < len(s) and s[i+1] == '\n':
                out.append('\r\n'); i += 2
            else:
                out.append('\r\n'); i += 1
        elif s[i] == '\n':
            out.append('\r\n'); i += 1
        else:
            out.append(s[i]); i += 1
    return ''.join(out)

# ---------------- runner ----------------

def run_file(BIN: Path, path: Path):
    p = subprocess.run([str(BIN), str(path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return p.returncode, p.stdout, p.stderr

def run_stdin(BIN: Path, text: str):
    p = subprocess.run([str(BIN), "-"], input=text, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return p.returncode, p.stdout, p.stderr

def baseline(BIN, src: Path):
    rc, out, err = run_file(BIN, src)
    return rc, pairs(out), err

# ---------------- tests ----------------

# 1) 'lex/ok' musí projít (rc=0), bez interních chyb, a být invariantní
def test_ok_token_stream_is_invariant_all(BIN, LEX_OK, tmp_path):
    files = sorted(LEX_OK.glob('*.wren'))
    if not files:
        pytest.skip("No files in lex/ok")
    for src in files:
        rc0, base, err0 = baseline(BIN, src)
        assert rc0 == 0, f'{src.name}: expected RC=0 (stderr="{short(err0)}")'
        text = src.read_text(encoding='utf-8')

        variants = [('ws', shuffle_ws(text))]
        if supports_line_comments_probe(BIN):
            variants.append(('linecom', inject_line_comments(text)))
        variants.extend([
            ('blockcom', inject_block_comments(text)),
            ('crlf', to_crlf(text)),
        ])
        if supports_stdin(BIN):
            variants.append(('stdin', text))

        for tag, variant in variants:
            if tag == 'stdin':
                rc, out, err = run_stdin(BIN, variant)
            else:
                vf = tmp_path / f"{src.stem}.{tag}.wren"
                vf.write_text(variant, encoding='utf-8')
                rc, out, err = run_file(BIN, vf)
            assert_no_internal(rc, err, f'{src.name}/{tag}')
            assert rc == 0, f'{src.name}/{tag}: RC should be 0 (stderr="{short(err)}")'
            got = pairs(out)
            assert got == base, f'{src.name}/{tag}: token stream differs from baseline'

# 2) 'lex/err': baseline MUSÍ být lexikální chyba (ne interní), varianty také lex
def test_err_stays_err_under_ws_all(BIN, LEX_ERR, ERROR_CODES, tmp_path):
    files = sorted(LEX_ERR.glob('*.wren'))
    if not files:
        pytest.skip("No files in lex/err")
    for src in files:
        rc0, out0, err0 = run_file(BIN, src)
        kind0 = classify(rc0, err0, ERROR_CODES)
        assert kind0 == "lex", f'{src.name}: expected LEX error on baseline, got kind={kind0} rc={rc0} (stderr="{short(err0)}")'
        text = src.read_text(encoding='utf-8')

        xforms = [('ws', shuffle_ws), ('blockcom', inject_block_comments), ('crlf', to_crlf)]
        if supports_line_comments_probe(BIN):
            xforms.insert(1, ('linecom', inject_line_comments))

        for tag, tf in xforms:
            variant = tf(text)
            vf = tmp_path / f"{src.stem}.{tag}.wren"
            vf.write_text(variant, encoding='utf-8')
            rc, out, err = run_file(BIN, vf)
            assert_no_internal(rc, err, f'{src.name}/{tag}')
            k = classify(rc, err, ERROR_CODES)
            assert k == "lex", f'{src.name}/{tag}: expected LEX error, got kind={k} rc={rc} (stderr="{short(err)}")'
            # stabilita konkrétního rc (pokud rozlišujete lex vs. jiné chyby číslem)
            assert rc == rc0, f'{src.name}/{tag}: expected rc={rc0}, got rc={rc}'

# 3) ifj2025codes_zadani — všechny musí projít (rc=0), bez interních chyb, a být invariantní
def test_ifj_zadani_ok_metamorphic_all(BIN, IFJ_ZADANI, tmp_path):
    if IFJ_ZADANI is None or not IFJ_ZADANI.exists():
        pytest.skip("ifj2025codes_zadani not present")
    files = sorted(IFJ_ZADANI.glob('*.wren'))
    if not files:
        pytest.skip("No files in ifj2025codes_zadani")
    for src in files:
        rc0, base, err0 = baseline(BIN, src)
        assert rc0 == 0, f'{src.name}: expected RC=0 (stderr="{short(err0)}")'
        text = src.read_text(encoding='utf-8')

        variants = [('ws', shuffle_ws(text))]
        if supports_line_comments_probe(BIN):
            variants.append(('linecom', inject_line_comments(text)))
        variants.extend([
            ('blockcom', inject_block_comments(text)),
            ('crlf', to_crlf(text)),
        ])
        if supports_stdin(BIN):
            variants.append(('stdin', text))

        for tag, variant in variants:
            if tag == 'stdin':
                rc, out, err = run_stdin(BIN, variant)
            else:
                vf = tmp_path / f"{src.stem}.{tag}.wren"
                vf.write_text(variant, encoding='utf-8')
                rc, out, err = run_file(BIN, vf)
            assert_no_internal(rc, err, f'{src.name}/{tag}')
            assert rc == 0, f'{src.name}/{tag}: RC should be 0 (stderr="{short(err)}")'
            got = pairs(out)
            assert got == base, f'{src.name}/{tag}: token stream differs from baseline'
