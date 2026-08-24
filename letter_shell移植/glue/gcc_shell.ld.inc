/* Paste into the FLASH / text output section of the GNU ld script.
 * shell.c (GNUC) uses _shell_command_start / _shell_command_end.
 */
.shellCommand : {
    PROVIDE(_shell_command_start = .);
    KEEP(*(shellCommand))
    PROVIDE(_shell_command_end = .);
} > FLASH

/* Only if SHELL_USING_VAR == 1:
.shellVariable : {
    PROVIDE(_shell_variable_start = .);
    KEEP(*(shellVariable))
    PROVIDE(_shell_variable_end = .);
} > FLASH
*/
