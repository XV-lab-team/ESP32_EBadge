; Paste inside ER_IROM1 (RO region), BEFORE .ANY (+RO).
; ARMCC/ARMClang then keeps SHELL_EXPORT_CMD objects in shellCommand.
; shell.c looks up shellCommand$$Base / shellCommand$$Limit.

*(shellCommand)
; *(shellVariable)   ; only if SHELL_USING_VAR == 1
