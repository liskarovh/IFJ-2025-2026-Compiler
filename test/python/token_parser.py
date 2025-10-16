# -*- coding: utf-8 -*-
import json, re

def try_parse_json(text: str):
    t = text.strip()
    if t.startswith('[') and t.endswith(']'):
        try:
            arr = json.loads(t)
            out = []
            for it in arr:
                lx = it.get('lexeme', it.get('value', it.get('text', '')))
                typ = it.get('type', '')
                line = it.get('line', it.get('row', None))
                col  = it.get('col',  it.get('column', None))
                out.append({'type': str(typ), 'lexeme': str(lx), 'line': line, 'col': col})
            return out
        except Exception:
            return None
    return None

_LINE_PATTERNS = [
    re.compile(r'type\s*=\s*(?P<type>\w+).*?lexeme\s*=\s*"(?P<lex>(?:\\.|[^"\\])*)"'),
    re.compile(r'^(?P<type>\w+).*?"(?P<lex>(?:\\.|[^"\\])*)"'),
    re.compile(r'"(?P<lex>(?:\\.|[^"\\])*)".*?(?P<type>\w+)$'),
]

def parse_tokens_flex(text: str):
    arr = try_parse_json(text)
    if arr is not None:
        return arr
    out = []
    for line in text.splitlines():
        s = line.strip()
        if not s:
            continue
        m = None
        for pat in _LINE_PATTERNS:
            m = pat.search(s)
            if m:
                break
        if not m:
            out.append({'type': s, 'lexeme': ''})
        else:
            out.append({'type': m.groupdict().get('type',''), 'lexeme': m.groupdict().get('lex','')})
    return out

def as_pairs(tokens):
    return [(t.get('type',''), t.get('lexeme','')) for t in tokens]
