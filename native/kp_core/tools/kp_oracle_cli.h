/* The subcommands of kp_oracle (kp_oracle_main.c the games', kp_oracle_cli_ui.c the interface's): each takes the
 * arguments after its name and returns the exit status; files are read relative to the working directory (the
 * repository root). */
#ifndef KP_ORACLE_CLI_H
#define KP_ORACLE_CLI_H

#include "kp_oracle_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

int kp_cli_hud_check(int argc, char **argv);
int kp_cli_input_check(int argc, char **argv);
int kp_cli_menus_replay(int argc, char **argv);
int kp_cli_room_replay(int argc, char **argv);
int kp_cli_result_replay(int argc, char **argv);
int kp_cli_score_replay(int argc, char **argv);

/* A failure of a subcommand: the message on stderr ("kp_oracle: ..."), exit status 1. */
int kp_cli_fail(const char *fmt, ...);
/* A file's bytes (NUL terminated), or a failure. */
uint8_t *kp_cli_read(const char *path, size_t *size);

#ifdef __cplusplus
}
#endif

#endif
