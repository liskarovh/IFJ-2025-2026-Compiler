# -*- coding: utf-8 -*-
import pytest, subprocess
from token_parser import parse_tokens_flex, as_pairs

def pairs(text: str): return as_pairs(parse_tokens_flex(text))

@pytest.mark.skip(reason="enable when binary supports '-' (stdin)")
def test_chunk_equivalence(BIN, LEX_OK):
    src = next(iter(sorted(LEX_OK.glob("*.wren"))))
    text = src.read_text(encoding="utf-8")

    p = subprocess.run([str(BIN), str(src)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    rc_ref, out_ref = p.returncode, p.stdout
    assert rc_ref == 0
    ref = pairs(out_ref)

    for bs in [1,2,3,5,7,13,64,257,1024]:
        proc = subprocess.Popen([str(BIN), "-"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        for i in range(0, len(text), bs):
            proc.stdin.write(text[i:i+bs]); proc.stdin.flush()
        proc.stdin.close()
        out, err = proc.communicate()
        assert proc.returncode == 0
        assert pairs(out) == ref, f"chunk={bs} differs"
