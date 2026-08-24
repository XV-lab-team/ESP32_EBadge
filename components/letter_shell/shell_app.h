#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Create the letter-shell task on USB Serial/JTAG. Safe to call once from app_main. */
void shell_app_start(void);

#ifdef __cplusplus
}
#endif
