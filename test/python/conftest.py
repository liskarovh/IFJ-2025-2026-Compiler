# -*- coding: utf-8 -*-
import os, re, pathlib, subprocess, pytest

# Kořen repa – rozumné defaulty
REPO_ROOT = pathlib.Path(__file__).resolve().parents[2] if len(pathlib.Path(__file__).resolve().parents) >= 3 else pathlib.Path.cwd()

def _candidates():
    env = os.environ.get("SCAN_BIN")
    if env:
        yield pathlib.Path(env)
    yield REPO_ROOT / "build" / "scan_dump"
    yield pathlib.Path.cwd() / "build" / "scan_dump"
    yield REPO_ROOT / "scan_dump"

def resolve_bin() -> pathlib.Path:
    for p in _candidates():
        if p.exists():
            return p
    raise FileNotFoundError("Set SCAN_BIN or build ./build/scan_dump")

@pytest.fixture(scope="session")
def BIN():
    return resolve_bin()

@pytest.fixture(scope="session")
def DATA_ROOT() -> pathlib.Path:
    # .../test/python -> parent je .../test
    return pathlib.Path(__file__).resolve().parent.parent

@pytest.fixture(scope="session")
def LEX_OK(DATA_ROOT):
    return DATA_ROOT / "lex" / "ok"

@pytest.fixture(scope="session")
def LEX_ERR(DATA_ROOT):
    return DATA_ROOT / "lex" / "err"

@pytest.fixture(scope="session")
def IFJ(DATA_ROOT):
    return DATA_ROOT / "ifj2025codes"

# Oficiální ukázky ze zadání (RC=0)
@pytest.fixture(scope="session")
def IFJ_ZADANI(DATA_ROOT):
    return DATA_ROOT / "ifj2025codes_zadani"

# ====== kódy chyb z error.h (abychom věděli, co je ERR_LEX a ERR_INTERNAL) ======
def _find_error_h():
    # zkus několik běžných umístění
    candidates = [
        (REPO_ROOT / "error.h"),
        (pathlib.Path.cwd() / "error.h"),
        (REPO_ROOT / "include" / "error.h"),
        (REPO_ROOT / "projekt" / "error.h"),
    ]
    for p in candidates:
        if p.exists():
            return p
    return None

def _parse_error_codes(path: pathlib.Path):
    # výchozí hodnoty, když nic nenajdeme
    codes = {"SUCCESS": 0, "ERR_LEX": 1, "ERR_INTERNAL": 99}
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
        for name in ("SUCCESS", "ERR_LEX", "ERR_INTERNAL"):
            m = re.search(rf"#\s*define\s+{name}\s+(\d+)", text)
            if m:
                codes[name] = int(m.group(1))
    except Exception:
        pass
    return codes

@pytest.fixture(scope="session")
def ERROR_CODES():
    hdr = _find_error_h()
    return _parse_error_codes(hdr) if hdr else {"SUCCESS": 0, "ERR_LEX": 1, "ERR_INTERNAL": 99}

# ====== detekce podpory stdin („-“) ======
def supports_stdin(bin_path) -> bool:
    """True jen pokud binárka reálně podporuje '-' (stdin): rc==0 a stderr neobsahuje fopen/usage."""
    try:
        sample = 'import "ifj25" for Ifj\n'
        p = subprocess.run([str(bin_path), "-"],
                           input=sample,
                           text=True,
                           stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE,
                           timeout=2)
        err_up = (p.stderr or "").upper()
        if p.returncode != 0:
            return False
        if "FOPEN" in err_up or "NO SUCH FILE" in err_up or "USAGE" in err_up:
            return False
        return True
    except Exception:
        return False
