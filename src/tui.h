#ifndef LY_TUI_H
#define LY_TUI_H
#include "classifier.h"
typedef struct { Buffer lyrics; char *filename; Progress progress; Diagnostics *diagnostics; } Result;
/* Returns 1 for saved result, 0 for user exit, -1 for initialization failure. */
int tui_run(Editor *editor, Config *config, int launch_dir, Result *result, char *error);
/* Normal-screen prompt: F3 prints diagnostics; R explicitly saves a JSON report. */
void tui_result_prompt(Diagnostics *diagnostics, int launch_dir, const char *cwd);
#endif
