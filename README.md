# LYRCENSOR

LYRCENSOR is a native C17 terminal application for precise, context-sensitive lyrics
redaction. It edits UTF-8 lyrics, asks a single OpenRouter Decisions model for
probabilities, and replaces selected **letters locally in C**. The model never
produces the output text. Russian and English text, `ё`, tabs, repeated spaces,
blank lines, and original line endings are preserved.

The default model is **`typesafe/jev-1.13`**. Both stages use native `noul`
questions at `POST https://openrouter.ai/api/alpha/decisions`. This application
does not use Chat Completions, JSON text generation, JSON Schema response
formats, or SSE. A model configured in Settings must support the Decisions API;
there is no automatic switch to another model or endpoint.

The policy prioritizes abusive racial/ethnic slurs, dehumanizing group insults,
severe targeted abuse, explicit threats, and exceptionally graphic expressions.
Ordinary profanity, including common Russian swear words, does not automatically
qualify. Quotation, irony, satire, history, metaphor, and artistic context matter.
Decisions are probabilistic and differ between models. Review results before use;
redaction does not guarantee acceptance by any other model or service.

## Build and installation

Dependencies: a C17 compiler, make, pkg-config, POSIX threads, ncurses **wide**,
libcurl with TLS support, jansson, utf8proc, and a system CA certificate bundle.
No scripting runtime is used by the application, build, or tests.

Void Linux:

```sh
sudo xbps-install -S base-devel pkg-config ncurses-devel libcurl-devel \
    jansson-devel libutf8proc-devel ca-certificates
make
make test
./lyrcensor
```

Debian/Ubuntu:

```sh
sudo apt install build-essential pkg-config libncurses-dev libcurl4-openssl-dev \
    libjansson-dev libutf8proc-dev ca-certificates
make
make test
```

Fedora:

```sh
sudo dnf install gcc make pkgconf-pkg-config ncurses-devel libcurl-devel \
    jansson-devel utf8proc-devel ca-certificates
make
make test
```

On other distributions install the development packages providing the
`ncursesw`, `libcurl`, `jansson`, and `libutf8proc` pkg-config modules. Check them
with `pkg-config --modversion ncursesw libcurl jansson libutf8proc`.
libcurl 7.85 or newer is required for protocol restrictions.

```sh
sudo make install                 # /usr/local/bin/lyrcensor
sudo make uninstall
make clean
```

`PREFIX` (default `/usr/local`) and `DESTDIR` are supported, for example:
`make install DESTDIR=/tmp/package PREFIX=/usr`. `CC`, `CFLAGS`, `CPPFLAGS`,
`LDFLAGS`, and `LDLIBS` can be overridden. Standard builds enable warnings, stack
protection, fortified libc calls, and RELRO with immediate symbol binding.

## Configuration and startup

Use an interactive terminal with a UTF-8 locale, at least 48 columns by 12 rows:

```sh
export OPENROUTER_API_KEY='your-openrouter-api-key'
export OPENROUTER_MODEL='typesafe/jev-1.13'  # optional: this is the default
./lyrcensor
# Or open an existing file without changing its bytes:
./lyrcensor song.txt
```

Use your shell's normal secret-management method if you do not want the key in
shell history. Keys are read only from the environment, never displayed or saved
by LYRCENSOR. Source lyrics go to OpenRouter and the selected provider when you
press F5; their data handling policies apply.

F2 Settings changes the model, network timeout, and both probability thresholds.
Settings last for the current session. Both stages of a running job use the same
frozen configuration. Model IDs are ASCII, up to 255 bytes. `OPENROUTER_MODEL`
may select another **Decisions-compatible** model, but never a conventional chat
model. An unset or empty environment model selects Jev; invalid IDs produce a
configuration error before processing or an explicit API error.

Defaults:

| Setting | Default | Allowed values |
| --- | --- | --- |
| Per-attempt network timeout | 120 seconds | 5–3600 seconds |
| Stage 1 review threshold | 0.20 | greater than 0, at most 1 |
| Stage 2 redaction threshold | 0.75 | greater than 0, at most 1 |

The comparison is `probability >= threshold`. Lower stage 1 values pass more
uncertain lines to review. Lower stage 2 values redact more readily. These
thresholds are operating choices, not guarantees of model calibration.

## Terminal controls

| Control | Action |
| --- | --- |
| Normal typing / Ctrl+Shift+V (emulator dependent) | Insert text / paste |
| Arrows, Home, End, Page Up/Down | Move by grapheme, line, or viewport |
| Backspace / Delete | Remove previous / next grapheme |
| Enter / Tab | Insert a newline / literal tab |
| F1 | Help |
| F2 | Settings; Up/Down or Tab chooses a field |
| F3 | Diagnostics: probabilities, thresholds, original tokens and decisions |
| Typing in a selected Settings field | Replace its value; Backspace edits the end |
| Enter / Esc in Settings | Validate and save / discard changes |
| F4 | Save an **uncensored draft** in the launch directory |
| F5 | Run both classification stages and save the result |
| Esc during processing | Cancel and return to the preserved editor |
| Esc in editor | Exit; press twice when text is present |
| Ctrl+C, SIGINT, SIGTERM, SIGHUP | Cancel work, restore terminal, attempt source recovery |

Editing is locked during processing. Only the main thread touches curses; a
worker thread owns the immutable source snapshot, HTTPS client, and output.
Resizing retains the cursor and viewport; terminals smaller than 48x12 show a
resize message. Tabs use eight-column stops. No mouse capture is enabled.

Bracketed paste is enabled on terminals that support it. Escape sequences inside
a bracketed paste are **never interpreted as application commands**. An entire
paste containing invalid UTF-8, NUL, C0 controls other than tab/CR/LF, DEL, or C1
controls is rejected with an error; existing lyrics remain unchanged. Rejecting
terminal control sequences also prevents them executing during final stdout
presentation. This restriction applies to loaded files too. Other Unicode text
is preserved without normalization. On older terminals without bracketed paste,
plain UTF-8 clipboard text works, but escape sequences cannot be distinguished
from actual function keys; use a terminal with bracketed paste for untrusted text.
An interrupted bracketed paste is not committed to the editor.

## Two classification stages

1. Split the immutable input into logical lines, treating LF, CRLF, and lone CR
   as line terminators. IDs are zero-based, including empty lines. Empty lines
   need no decision and are copied exactly; whitespace-only lines are submitted.
   Batch up to 24 non-empty lines (with a 128 KiB soft batch target). Each line
   has its own `noul` question and complete previous/current/next original lines.
   Pass a line to stage 2 if `answers[line_ID].noul >= line_threshold`.
2. Tokenize every candidate line locally. Each request contains its complete
   ordered word list and previous/current/next lines. Unrelated song lines are
   omitted. Ask up to 48 independent
   `noul` questions per request, one for each token, referring to its line and
   token IDs. If a line has more tokens, keep the complete context and token list
   in every question batch. Select a token if
   `answers[line_ID_token_ID].noul >= token_threshold`.

Questions use explicit policy/task instructions and true/false criteria. Lyrics
are separate untrusted `state` data. Each returned answer must have exactly
`{"type":"noul","noul":<finite number from 0 through 1>}`. Every submitted key
must be present, with no unknown keys. JSON duplicate keys, wrong types,
out-of-range probabilities, missing answers, refusals, and API error objects
fail validation. IDs are never inferred from arbitrary offsets or generated
text. Repeated answer keys are rejected rather than applied twice.

There is no third classification or rewriting stage. After all required decisions
validate, C replaces every Unicode letter (general category L) in a selected
token with one ASCII `*`, removing immediately attached combining marks (M) on
replaced bases. Digits and their own marks, punctuation, emoji, hyphens,
apostrophes, whitespace, and line terminators remain byte-for-byte intact.
For example, `ё-моё` becomes `*-***`, `abc123` becomes `***123`, and decomposed
`e` + combining acute becomes `*`. The entire input is never normalized.

### Deterministic tokenization

Tokens start with a Unicode letter (L) or number (N), and continue through L, N,
and combining marks (M). ASCII hyphen `-`, ASCII apostrophe `'`, right apostrophe
`’`, hyphen `‐` (U+2010), and nonbreaking hyphen `‑` (U+2011) are included internally
when immediately followed by L or N. Leading/trailing punctuation and repeated
hyphens separate tokens. A token must contain at least one L; pure numbers are
not word tokens. `abc123`, `1-2-abc`, `can't`, and `какое-то` are each one token;
`a--b` has two tokens. Emoji and other punctuation separate tokens.

Each token records its line ID, zero-based ID within that line, and exclusive
byte span into the immutable original source. Original token strings in requests
are derived from those spans. This is a deterministic lexical policy, not a
linguistic word segmenter for all writing systems. Editor motion uses extended
grapheme boundaries through utf8proc, including combining marks, CRLF, and emoji
join sequences. Display uses grapheme widths rather than UTF-8 byte counts.

## Network behavior and progress

Production HTTPS calls are pinned to the OpenRouter Decisions endpoint, with TLS
peer and hostname verification enabled. Redirects are disabled. There is one
reusable client and one configured model. Cancellation checks run at most every
100 ms during curl polling and retry waits, subject to platform resolver behavior.
No SSE support is assumed or requested for Decisions.

Each logical request permits at most three attempts in total, including malformed
answer retries. Retry delays are 1 then 2 seconds; a numeric `Retry-After` can
increase them up to 30 seconds. Transient connection failures, timeouts, truncated
bodies, and HTTP 408/425/429/500/502/503/504/529 can retry. Authentication,
permissions, payment, unsupported models/parameters, and other non-transient HTTP
errors fail directly. Valid JSON with an explicit error/refusal does not retry.
Retries may incur extra provider charges. No success is reported for incomplete
classification, and API errors leave the editable original intact.

Progress is based on validated line/token answers and completed candidate lines,
never elapsed time. The UI reports actual started/completed HTTP requests and
request/response activity. The summary adds model-reported input/output tokens
and actual `usage.cost` in USD when available. Counts/cost include reported
responses, including any parseable invalid-answer attempts; missing usage is
shown as unavailable, never estimated. Transport failures may have been billed
without returning usage, so reported totals can be partial.

The native request/response contract follows the official
[OpenRouter Decisions API reference](https://openrouter.ai/docs/api/api-reference/alphadecisions/submit-a-decisions-questions-and-answers-request)
and the [Jev model page](https://openrouter.ai/typesafe/jev-1.13). The API is alpha;
recheck its contract when updating dependencies or the model.

## F3 classification diagnostics

Every validated line and token probability is retained in memory against the
immutable source snapshot, whether or not F3 was opened before processing.
F3 opens a read-only diagnostic screen before or during processing and after a
failed/cancelled run. Tokens sort by **descending original `noul` probability**;
ties sort by line ID and token ID. Each entry shows the one-based line number,
zero-based token ID within that line, probability, KEEP/CENSOR decision, original
token, and exclusive byte span in the original UTF-8 input. Lines show REVIEW/SKIP.
The displayed thresholds belong to that run; subsequent F2 changes do not change
the recorded decisions. The default thresholds remain 0.20 and 0.75.

Use Up/Down and Page Up/Down to scroll, Left/Right to view long entries, Home to
return to the start, and Tab to switch between token and line decisions. F3 or
Esc returns to the preceding screen. To cancel a running request from this view,
close the view and then press Esc. A snapshot from an interrupted or ongoing run
is explicitly marked partial. Edited lyrics do not alter the previous run's
diagnostic source.

After a successful run the complete censored lyrics still print on the normal
terminal screen. At the final prompt, **F3 prints all decisions to stderr**, with
tokens highest first, without clearing the lyrics or reentering curses. Enter
exits as before. No extra API calls are made for diagnostics.

Press **R** inside the diagnostic view or at the final prompt to explicitly save
`LYRCENSOR diagnostics.json` in the launch directory. Existing reports get numeric
suffixes. Reports use `0600` permissions and contain all validated line/token
decisions, raw probabilities, run model and thresholds, original strings, source
spans, exact question IDs, and `classification_complete`. Tokens are sorted by
descending probability in the report too. A report during processing captures
only decisions validated so far. Reports are separate from the lyrics Markdown
file and are never saved automatically. They contain source lyrics; application
credentials and HTTP authentication headers are never included. Report failures
do not change the source or the saved censorship result. Diagnostic record storage
and serialized reports each have a 16 MiB limit; recording failure stops processing
explicitly rather than silently losing decisions.

### Investigating zero redactions

`noul` is **P(YES)**, as documented in the
[OpenRouter Jev guide](https://openrouter.ai/docs/guides/community/jev).
The code uses that value directly, never `1 - noul`, and selects when
`probability >= threshold`, including equality. Stage 1's true criterion asks
whether a line *potentially* needs review. Stage 2's true criterion asks whether
the identified token needs minimal redaction in context; false means preserve.
The stage 2 task explicitly states YES=redact and NO=preserve. The existing policy
and true/false criteria retain the distinction between severe abuse and ordinary
profanity. No threshold is adjusted automatically.

For a run with many candidates but no redactions, press F3 and compare the first
(highest-scoring) token with the recorded stage 2 threshold. If it is below the
threshold, zero redactions follow from the model's returned scores. Review the
token/line decisions and policy before manually considering a different threshold.
This does not prove whether the model's semantic judgments were right. Previous
versions did not retain these probabilities, so old summary counts cannot reveal
why that particular run selected nothing; rerun it with this version.

Returned answer keys are matched to the submitted question keys before their
probabilities are read. Token IDs and spans are checked against their candidate
line before sending requests. The diagnostic record and local replacement use
the same token span. Tests shuffle answer order, use repeated identical words
at different offsets, and verify byte-for-byte output to check this mapping.

## Files and copying results

At startup LYRCENSOR captures both the launch directory path and an open directory
descriptor. Result files are created there, even if the executable lives elsewhere.
The first **original**, uncensored line supplies the filename. Unicode whitespace
is trimmed only for the filename. Separators and unsafe filename characters are
replaced, leading dots are made visible, trailing dots are removed, and the name
is shortened only at UTF-8 boundaries to leave room for `.md` and numeric suffixes.
An empty/unsafe first line uses `lyrics.md`.

Existing files and symlinks are never overwritten: `Title.md`, `Title (1).md`,
`Title (2).md`, etc. Creation uses a pinned directory descriptor and exclusive
creation, with permissions `0600`. Write, sync, and close errors are checked;
failed writes are removed. File contents are **only the exact censored lyrics**,
without headings, metadata, timestamps, or code fences. A crash or power loss
while writing can leave a partial newly created file; an existing file is never
replaced. Draft/recovery files use separate `LYRCENSOR draft` and
`LYRCENSOR recovery` names and the same collision protection.

After successful save, curses closes and the complete lyrics are printed to
stdout on the normal terminal screen. Start/end status markers, the path, and
usage summary go to stderr, outside the lyrics. The program waits for Enter (with
F3 diagnostics and R report export available) and
never clears the result. Use normal terminal selection and Ctrl+Shift+C; no X11,
Wayland, OSC 52, clipboard daemon, or shortcut interception is required. The
printed lyrics and saved bytes are identical. Terminals can visually wrap long
lines and have finite scrollback; the saved file preserves authoritative bytes
if the emulator's selection adds wraps or truncates scrollback.

## Tests

`make test` needs no account, API key, internet connection, or scripting language.
It runs:

- Core tests for ASCII/Russian/mixed text, `ё`, hyphens/apostrophes, embedded digits,
  combining marks, emoji, CRLF/lone CR, repeated blank lines/spaces/tabs, missing
  final newline, no/all redaction, 1 MiB lines, exact preservation, token IDs,
  malformed/duplicate/missing answers, thresholds, cancellation, safe filenames,
  filesystem failures, collisions, and symlinks.
- The real libcurl client against a loopback C HTTP fixture: native request
  format, fragmented responses, non-retryable errors, 429/503 retries, invalid
  JSON/answers, truncated responses, and cancellation.
- PTY tests of the real TUI: Unicode clipboard paste and editing, navigation,
  settings, resize, rejected escape-sequence paste, missing credentials, API
  failure/cancel recovery, SIGTERM recovery, and the full two-stage
  classification/save/normal-screen output path.

Only test executables built with `LY_TEST_HTTP` allow a loopback endpoint override;
that code and environment-variable override do not exist in the production
`lyrcensor` binary. Fixture responses are never used by production code.
Diagnostics tests cover threshold boundaries, YES/NO direction, shuffled answer
keys, source spans and replacements, exact probability retention, sorted reports,
no credential disclosure, report collisions, and a synthetic
58-line/31-candidate/149-token run with zero redactions because every token score
is below the unchanged stage 2 threshold. PTY tests exercise F3 during processing
and at the final prompt, report export, and preservation of the saved lyrics.

Authenticated provider/model behavior requires a separate live check with your
own OpenRouter account. Offline tests validate the documented protocol and local
invariants, not a model's semantic accuracy.

## Troubleshooting and limitations

- **Missing package/header:** install the development packages above and check
  pkg-config. On Void the Unicode package is `libutf8proc-devel`.
- **Invalid locale:** run with a configured UTF-8 locale, e.g.
  `LC_ALL=C.UTF-8 ./lyrcensor`. Font/emulator Unicode versions can differ from
  utf8proc; some emoji cluster widths may appear different. Try a current terminal.
- **F keys intercepted:** configure the terminal to send xterm/VT function keys.
  The help/settings/processing keys are F1/F2/F5, not Ctrl+Shift clipboard shortcuts.
- **Missing key:** export it before launch. It cannot be changed from Settings.
  F4 can save the source so it can be reopened after restarting.
- **HTTP 401/403/402:** check credentials, account permissions, or credits.
- **HTTP 400/404/422:** verify the exact Decisions-compatible model ID. Chat models
  cannot serve as a fallback for Jev. API error messages deliberately do not echo
  untrusted provider bodies, source lyrics, or authentication headers.
- **HTTP 429/5xx or timeout:** bounded retries are automatic. Adjust timeout in F2
  or try later. Esc cancels; no file is reported as successfully censored on failure.
- **Invalid/missing answers:** processing fails after bounded retries instead of
  guessing that the text is safe. The source remains editable.
- **Large inputs:** source/paste and response buffers are limited to 16 MiB.
  Line indexes and token storage each have a separate 16 MiB capacity budget. Very long lines are not
  silently truncated; a provider context limit or oversized serialized request
  produces an error. Repeating the complete token list in question batches can
  increase cost for unusually long candidate lines. There is no automatic
  context truncation or splitting of a logical line into semantic fragments.
- **File errors:** check launch-directory permissions and disk space. Failed
  saves leave the source in the editor. F4 saves an uncensored draft explicitly.
- **Termination:** handled signals attempt a draft recovery without overwriting
  files, but recovery can fail if memory or filesystem space is exhausted.
  SIGKILL, kernel crashes, and power loss cannot be handled. There is no undo
  history or continuous autosave; save important drafts with F4.
- **Interrupted paste:** wait for the terminal's closing bracketed-paste marker;
  a signal exits safely, retaining only text already committed to the editor.
- **Network cancellation:** client cancellation closes the local connection;
  upstream execution and billing cancellation depend on OpenRouter/provider.
- **Selection and Markdown rendering:** copy from the normal terminal, or read
  the saved file. A Markdown renderer may visually collapse ordinary whitespace;
  the file still contains the exact UTF-8 bytes without added Markdown markup.

See [LICENSE](LICENSE) for the MIT license.
