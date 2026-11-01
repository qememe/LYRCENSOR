# Evaluating a classification policy

`make test` checks request construction and local invariants. Its mock responses
are intentionally unrelated to the meaning of the Russian examples. Passing
those tests is **not evidence of improved semantic accuracy**. No live API calls
are made by automated tests.

The updated policy is `token-usage-v2`. It asks whether a target expression
functions as language needing selective masking, without judging the author's
beliefs or endorsement. Quotation and satire are neither a blanket exemption for
slurs nor a reason to mask neutral terms. The instructions use Russian examples
as linguistic guidance; production C code contains no keyword-based decisions.

## Optional manual comparison (uses paid API requests)

1. Build the baseline and candidate revisions in separate working directories.
   Use the same `typesafe/jev-1.13` model, account/provider settings, source bytes,
   and thresholds: stage 1 **0.20**, stage 2 **0.75**. Record the revision and date.
   Do not change thresholds while comparing instruction versions.
2. For each case below, use the exact **previous/current/next** triple in
   [tests/test_requests.c](../tests/test_requests.c), without the test harness's
   outer prologue/epilogue. The target is in the middle line (line number 2).
   Keep labels and descriptions outside the source sent to Jev. The table is
   a starting human-reference interpretation, not a semantic oracle; have a
   Russian-speaking reviewer approve or revise labels **before** reading scores.
3. Launch each revision with the three-line excerpt, press F5, and use F3/R at
   the result prompt to save its diagnostic report. If a request fails, retry
   explicitly later; exclude incomplete runs from quality measurements and
   report their count separately. Keep baseline and candidate reports separate.
4. Locate the target by its line number, token ID and original spelling. Record
   its raw `noul`, the recorded threshold, and KEEP/CENSOR decision. If stage 1
   skipped its line, record **not evaluated** for its stage 2 probability, never
   a fabricated zero. Record the stage 1 probability and REVIEW/SKIP decision too.
5. Compare labels with decisions using the confusion matrices below. Inspect
   errors individually, especially quoted slurs versus neutral identity terms
   in hostile passages. Repeat on representative, independently labeled lyrics;
   this small synthetic set is insufficient for a quality claim. Repeated runs
   can expose model variation, but repeats are not independent new examples.

Reports from this version contain `classification_policy.version`,
`shared_instructions`, and both stages' exact `task_template`,
`template_parameter`, and `criteria` once in the metadata. `%zu` expands to the
line ID; `%s` expands to the token's question ID. Older reports may have no policy
metadata; retain the baseline source revision as the authoritative prompt.
Probabilities and decisions in reports are not rescaled or reinterpreted.

F3 output and exported diagnostic reports include source text. Keep them private
as appropriate. API keys and authentication headers are not diagnostic metadata.
Normal `.md` outputs remain just the masked lyrics.

## Human-reference cases

All wording and neighboring lines are defined in `tests/test_requests.c`.
These labels concern **only the named token**, not every word in the sentence.
In particular, a neutral identity word does not become a lexical slur merely
because the sentence expresses hostility.

| Fixture | Target | Reference mask? | Reason |
| --- | --- | --- | --- |
| identity slur | пидарасами | Yes | Strong derogatory identity-based slur, inflected form |
| neutral identity | гей | No | Neutral identity label |
| informal identity | лесбиян | No | Informal identity reference, not automatically a slur |
| identity metaphor | петушками | Yes | Explicit derogatory metaphor about sexual orientation |
| literal metaphor counterpart | Петушки | No | Literal birds on a farm |
| ordinary profanity | похуй | No | Profanity alone does not qualify |
| general vulgar insult | ебанат | No | General non-identity insult |
| quoted slur | пидарасы | Yes | Quoted derogatory expression; narrator's disapproval is not an exemption |
| satirical slur | пидарасов | Yes | Offensive expression in a fictional intolerant character's speech |
| neutral term in hostile satire | геев | No | Neutral label within hostile fictional speech |
| non-graphic historical violence | войны | No | Ordinary historical reference |
| religious criticism | церковную | No | Criticism of doctrine, not identity-based abuse |
| neutral word in hostile sentence | соседей | No | Neutral noun, despite hostility elsewhere in the sentence |
| Russian spelling variant | пидорасами | Yes | Contextually derogatory spelling variant |
| ethnic slur | чурки | Yes | Derogatory ethnic label in the supplied context |
| literal ethnic-slur counterpart | чурки | No | Literal pieces of wood beside a stove |

Add independently labeled examples of direct exceptionally severe threats and
extremely graphic abuse before evaluating those policy categories. Also include
reclaimed, historical and ambiguous usage with reviewer notes; do not assume every
occurrence of an example word should be masked. Split tuning and held-out sets
if the observations will guide future policy changes.

## Measure false positives and false negatives separately

For the **end-to-end** result, a target not reaching stage 2 is unmasked. Compare
the final target decision with the human label:

| Human label | Final target decision | Count |
| --- | --- | --- |
| Mask | CENSOR | True positive (TP) |
| Preserve | CENSOR | False positive (FP) |
| Preserve | KEEP or line skipped | True negative (TN) |
| Mask | KEEP or line skipped | False negative (FN) |

Report counts first, then:

- False-positive rate: `FP / (FP + TN)`.
- False-negative rate: `FN / (FN + TP)`.
- Precision: `TP / (TP + FP)`.
- Recall: `TP / (TP + FN)`.

A zero denominator means **N/A**, not zero error. State the number of positive
and negative labeled targets. Do not report only overall accuracy: a classifier
that masks nothing can look accurate on a mostly negative corpus.

For attribution, also report:

- Stage 1 misses: positive-labeled target lines marked SKIP, divided by all
  positive-labeled target lines (deduplicate lines containing multiple targets).
- Stage 2 FP/FN among targets actually evaluated, with the evaluated positive
  and negative counts. Mark this as **conditional on stage 1**. A change in
  candidate selection can change this subset, so compare shared evaluated
  targets as well as the complete end-to-end matrix.

Retain raw scores to inspect error distributions, but do not lower thresholds
or force scores up to make the candidate policy appear better. Only new actual
Jev decisions on the labeled inputs can establish whether this policy helps.
