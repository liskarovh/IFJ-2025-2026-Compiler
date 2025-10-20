# IFJ25 Lexer Test Harness (Python-only)

Balíček obsahuje pouze **pytest** skripty (bez vstupních `.wren` souborů).  
Očekává se adresářová struktura `test/` na stejné úrovni:

```
test/
├─ lex/
│  ├─ ok/                  # lexikálně korektní vstupy
│  └─ err/                 # lexikální chyby (musí selhat)
├─ ifj2025codes/           # delší ukázkové kódy (OK i ERR)
├─ ifj2025codes_zadani/    # ukázky ze zadání (vše OK)
└─ python/                 # tento balíček testů
```

---

## Spuštění

1. Přeložit binárku dumperu tokenů. Výchozí hledané cesty:
    - `./build/scan_dump` nebo `./scan_dump`

2. Spustit testy z kořene projektu:
   ```bash
   python3 -m pytest -q test/python
   ```
   Detailní výstup: `python3 -m pytest -vv -rA test/python`

Interpretace symbolů pytestu: `.` = prošel, `s` = přeskočen, `F` = fail, `E` = error.

---

## Co testy ověřují

### Metamorfní invariance (platí pro OK vstupy)
Pro každý vstup se sejme **baseline** výpis tokenů z binárky a následně se na zdroj aplikuje několik **neutrálních transformací**:
- `ws` – míchání mezer a tabulátorů; počet EOL je zachován, měnit se může LF/CRLF,
- `linecom` – vložení `//` **pouze** na prázdné řádky,
- `blockcom` – občasné `/*c*/` mezi tokeny (mimo stringy/komentáře),
- `crlf` – normalizace konců řádků na CRLF; počet EOL zůstává stejný,
- `stdin` – volitelně, pokud binárka podporuje `-` jako vstup ze standardního vstupu.

**Po každé transformaci musí být sekvence tokenů identická s baseline.**

### Sada `lex/err`
Každý vstup v `lex/err` musí skončit **lexikální chybou** (nikoli pádem či interní chybou).  
Po všech transformacích (`ws`, `linecom`*, `blockcom`, `crlf`) musí zůstat **lexikální chyba** a **číselný návratový kód** musí být **stabilní** (shodný s baseline).  
\* `linecom` je podmíněno sondou podpory `//`.

### Sada `ifj2025codes_zadani` (vše OK)
Všechny `.wren` soubory musí projít (`rc==0`) a splnit invarianci tokenového proudu.

### Rozlišení typů chyb
Testy načítají `SUCCESS`, `ERR_LEX`, `ERR_INTERNAL` z `error.h`.  
Klasifikace výsledků:
- `rc == SUCCESS` → **ok**
- `rc < 0` → **crash** (pád signálem)
- `rc == ERR_INTERNAL` nebo `stderr` obsahuje „INTERNAL“ → **internal**
- jakékoliv jiné nenulové `rc` → **lex**

---

## Jak testovací soubory fungují (Python)

| Soubor | Funkce / Fixtura | Popis |
|---|---|---|
| `conftest.py` | `BIN()` | Vyhledá binárku (`SCAN_BIN`, `./build/scan_dump`, `./scan_dump`). |
|  | `LEX_OK()`, `LEX_ERR()`, `IFJ()`, `IFJ_ZADANI()` | Vrací cesty k datovým složkám. |
|  | `ERROR_CODES()` | Parsuje `error.h` a načte číselné hodnoty `SUCCESS/ERR_LEX/ERR_INTERNAL` (fallback 0/1/99). |
|  | `supports_stdin(bin)` | Ověří, zda binárka akceptuje `-` jako stdin bez chyb. |
| `token_parser.py` | `parse_tokens_flex(text)` | Parsuje výstup `scan_dump` (blok `== TOKENS == … == COUNT: N ==`) do struktury tokenů. |
|  | `as_pairs(tokens)` | Redukuje na dvojice `(TYP, lexém)` pro přesné porovnání. |
| `test_metamorphic.py` | `test_ok_token_stream_is_invariant_all` | Pro `lex/ok`: baseline `rc==0`, porovnání invariancí (`ws`, podmíněně `linecom`, `blockcom`, `crlf`, volitelně `stdin`) s baseline. |
|  | `test_err_stays_err_under_ws_all` | Pro `lex/err`: baseline musí být **lex**; varianty také **lex**; návratový kód invariantní; interní chyby/pády jsou zakázány. |
|  | `test_ifj_zadani_ok_metamorphic_all` | Pro `ifj2025codes_zadani`: vše `rc==0` a invariance tokenů. |
| `test_chunking.py` | `test_chunk_equivalence` | (Aktuálně `SKIP`, dokud není podporováno `-`) – shoda tokenů při posílání zdroje po blocích přes `stdin` vs. čtení ze souboru. |

---

## Přehled `.wren` vstupů a očekávání

### `test/lex/ok` — musí projít (RC=0) + invariance

| Soubor | Co se ověřuje (edge case) | RC | Invariance (WS/komentáře/LF↔CRLF/stdin) |
|---|---|---:|---|
| `eol_mix_crlf_cr_lf.wren` | Mix LF/CR/CRLF | 0 | Ano |
| `ids_basic.wren` | Běžné identifikátory | 0 | Ano |
| `ids_global.wren` | Globální ID s `__` | 0 | Ano |
| `ids_kw_shadow.wren` | ID podobná klíčovým slovům (case-sensitive) | 0 | Ano |
| `ifj_namespace_spaces.wren` | `Ifj . write` s mezerami | 0 | Ano |
| `numbers_exp_ok.wren` | Exponentové literály (`1e-1`, `1E3`) | 0 | Ano |
| `numbers_float_ok.wren` | Reálná čísla s tečkou | 0 | Ano |
| `numbers_hex_ok.wren` | Hex `0x...` (smíšené case) | 0 | Ano |
| `numbers_int_dec.wren` | Celočíselná desítková | 0 | Ano |
| `numbers_int_zero.wren` | Nula | 0 | Ano |
| `operators_and_delims.wren` | Operátory/oddělovače `+ - * / <= >= == != ( ) { } ,` | 0 | Ano |
| `ws_around_tokens.wren` | Libovolné whitespace kolem tokenů | 0 | Ano |

### `test/lex/err` — musí selhat (LEX) a zůstat chybové

| Soubor | Co se ověřuje | RC (typ) | Stabilita |
|---|---|---:|---|
| `bad_escape_in_string.wren` | Neplatný escape `\\q` | LEX | RC shodný s baseline |
| `forbidden_char.wren` | Nepovolený znak `@` | LEX | Bez crash/internal |
| `global_id_just_underscores.wren` | Identifikátor pouze `__` | LEX | Stabilní |
| `hex_escape_wrong_length.wren` | Špatné délky `\\x` (`\\x1`, `\\x123`, `\\x`) | LEX | Stabilní |
| `numbers_leading_zeros_and_bad_forms.wren` | Zakázané/špatné tvary čísel (`00`, `.5`, `1.`, `1e`, `1e+`, `0x`) | LEX | Stabilní |
| `unclosed_block_comment.wren` | Neuzavřený blokový komentář | LEX | Stabilní |
| `unclosed_multiline_string.wren` | Neuzavřený multiline string `"""` | LEX | Stabilní |
| `unclosed_string.wren` | Neuzavřený řetězec `"...` | LEX | Stabilní |

### `test/ifj2025codes` — delší programy (OK i ERR)

| Soubor | Co se ověřuje | RC | Invariance / Trvalá chyba |
|---|---|---:|---|
| `ok_comments_nested_block.wren` | Vnořené blokové komentáře `/* ... /* ... */ ... */` | 0 | Invariance |
| `ok_eol_variants_crlf_cr_lf.wren` | Mix EOL ve větším kódu | 0 | Invariance |
| `ok_factorial_from_spec.wren` | Delší příklad se řízením toku | 0 | Invariance |
| `ok_ifj_ord_call_shapes.wren` | Volání `Ifj.ord("ahoj", u)` | 0 | Invariance |
| `ok_ifj_write_with_spaces.wren` | `Ifj   .   write("ok")` | 0 | Invariance |
| `ok_keywords_and_ids.wren` | Kombinace klíčových slov a ID (vč. `__glob`) | 0 | Invariance |
| `ok_line_comment_is_eol.wren` | Řádkový komentář ukončený EOL | 0 | Invariance |
| `ok_long_identifiers_strings.wren` | Dlouhá ID a dlouhé řetězce | 0 | Invariance |
| `ok_multiline_string_trimming.wren` | Multiline string `""" ... """` | 0 | Invariance |
| `ok_numbers_all_forms.wren` | Mix int/hex/float/exponent | 0 | Invariance |
| `ok_overloading_headers_lex.wren` | Hlavičky metod (přetížení názvů) | 0 | Invariance |
| `ok_prolog_minimal.wren` | Minimální skeleton s `import "ifj25" for Ifj` | 0 | Invariance |
| `err_bad_escape_in_string.wren` | Neplatný `\\q` ve „spec“ kódu | LEX | Trvale ERR |
| `err_hex_escape_wrong_length.wren` | Špatné délky `\\x` ve „spec“ kódu | LEX | Trvale ERR |
| `err_numbers_leading_zeros_and_bad_forms.wren` | Chybné tvary čísel ve „spec“ kódu | LEX | Trvale ERR |
| `err_unclosed_block_comment.wren` | Neuzavřený blokový komentář ve „spec“ | LEX | Trvale ERR |
| `err_unclosed_multiline_string.wren` | Neuzavřený `"""` ve „spec“ | LEX | Trvale ERR |
| `err_unclosed_string.wren` | Neuzavřený řetězec ve „spec“ | LEX | Trvale ERR |

### `test/ifj2025codes_zadani` — ukázky ze zadání (vše OK)
Všechny `.wren` v této složce musí **projít** (`rc==0`) a splnit invarianci tokenů.
