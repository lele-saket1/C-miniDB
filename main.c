#include <stdio.h>

#include "engine.h"
#include "shell.h"

/**
 * @file main.c
 * @brief Main entry point for the FRASE database shell application.
 *
 * This file initializes the FRASE engine, handles command-line arguments
 * for the database file path, and then enters the interactive shell loop.
 * It manages the engine's lifecycle from booting to shutdown.
 */

/**
 * @brief Main function for the FRASE database shell.
 * @param argc The number of command-line arguments.
 * @param argv An array of command-line argument strings.
 * @return     0 on successful execution and graceful exit, 1 on error.
 */
int main(int argc, char **argv)
{
    Engine engine;
    EngineError error;

    if (argc != 2) {
        fprintf(stderr, "Usage: frase <database_file>\n");
        return 1;
    }

    error = engine_boot(&engine, argv[1]);

    if (error != ENGINE_OK) {
        fprintf(stderr, "Failed to boot FRASE: %s\n", engine_error_string(error));
        return 1;
    }

    return shell_run(&engine);
}