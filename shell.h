#ifndef FRASE_SHELL_H
#define FRASE_SHELL_H

#include "engine.h"

/*
    FRASE Shell

    The shell is the interactive command layer.

    It owns:
        - the command loop
        - line reading
        - command parsing
        - user input validation
        - printing results and errors
        - batch file parsing

    It does not own:
        - chunks
        - indexes
        - arenas
        - file persistence
        - database state

    The engine owns database state.
    The shell translates user text into engine function calls.
*/

/*
    shell_run

    Starts the interactive FRASE shell.

    Expected command surface:

        help
        insert <id>,<gpa>,<name>
        insert_batch <file_path>
        find <id>
        delete <id>
        update_name <id>,<new_name>
        update_gpa <id>,<new_gpa>
        range <min_gpa>,<max_gpa>
        save
        save_exit

    EOF behavior:
        If input ends, shell_run treats it like save_exit.

    Return value:
        0 for clean shell shutdown.
        nonzero for shell/runtime failure.
*/

int shell_run(Engine *engine);

#endif