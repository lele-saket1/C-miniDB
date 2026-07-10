#include <stdio.h>

#include "engine.h"
#include "shell.h"

/**
 * @file main.c
 * @brief Main entry point for the FRASE database shell application.
 *
 * This file initializes the FRASE engine, handles command-line arguments
 * for the database file path (or default if not specified), and then enters the interactive shell loop.
 * It manages the engine's lifecycle from booting to shutdown.
 */

int main(int argc, char **argv)
{
    Engine engine;
    EngineError error;
    const char *db_path = NULL;

    // If a command-line argument is given, use it. 
    // Otherwise, fallback to a sensible default hardcoded filename.
    if (argc == 2) {
        db_path = argv[1];
    } else if (argc == 1) {
        db_path = "my_database.db";
        printf("No database file specified. Defaulting to '%s'\n", db_path);
    } else {
        // If they provide 3 or more arguments, show usage instructions.
        fprintf(stderr, "Usage: frase [<database_file>]\n");
        return 1;
    }

    error = engine_boot(&engine, db_path);

    if (error != ENGINE_OK) {
        fprintf(stderr, "Failed to boot FRASE: %s\n", engine_error_string(error));
        return 1;
    }

    return shell_run(&engine);
}