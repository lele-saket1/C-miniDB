#include "shell.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHELL_LINE_SIZE 512u
#define SHELL_INITIAL_BATCH_CAPACITY 64u

#define SHELL_CONTINUE 0
#define SHELL_STOP     1
#define SHELL_FATAL    2

typedef enum ShellCommandType {
    SHELL_COMMAND_EMPTY = 0,
    SHELL_COMMAND_HELP,
    SHELL_COMMAND_INSERT,
    SHELL_COMMAND_INSERT_BATCH,
    SHELL_COMMAND_FIND,
    SHELL_COMMAND_DELETE,
    SHELL_COMMAND_UPDATE_NAME,
    SHELL_COMMAND_UPDATE_GPA,
    SHELL_COMMAND_RANGE,
    SHELL_COMMAND_SAVE,
    SHELL_COMMAND_SAVE_EXIT,
    SHELL_COMMAND_UNKNOWN
} ShellCommandType;

typedef struct ShellCommand {
    ShellCommandType type;
    char *arguments;
} ShellCommand;

typedef enum ShellBatchLoadStatus {
    SHELL_BATCH_LOAD_OK = 0,
    SHELL_BATCH_LOAD_OPEN_FAILED,
    SHELL_BATCH_LOAD_ALLOCATION_FAILED,
    SHELL_BATCH_LOAD_PARSE_FAILED
} ShellBatchLoadStatus;

/**
 * @brief Prints the shell prompt to standard output.
 */
static void shell_print_prompt(void);

/**
 * @brief Reads a line of input from stdin.
 * @param line      Buffer to store the read line.
 * @param line_size Maximum size of the buffer.
 * @return          `true` if a line was successfully read, `false` otherwise (e.g., EOF).
 */
static bool shell_read_line(char *line, size_t line_size);

/**
 * @brief Removes trailing newline characters from a string.
 * @param line The string to trim.
 */
static void shell_trim_newline(char *line);

/**
 * @brief Trims leading whitespace characters from a string.
 * @param text The string to trim.
 * @return     A pointer to the first non-whitespace character, or the original pointer if no leading whitespace.
 */
static char *shell_trim_left(char *text);

/**
 * @brief Trims trailing whitespace characters from a string.
 * @param text The string to trim.
 */
static void shell_trim_right(char *text);

/**
 * @brief Trims both leading and trailing whitespace characters from a string.
 * @param text The string to trim.
 * @return     A pointer to the trimmed string.
 */
static char *shell_trim(char *text);

/**
 * @brief Checks if a string contains only whitespace or is empty.
 * @param arguments The string to check.
 * @return          `true` if the string is empty or contains only whitespace, `false` otherwise.
 */
static bool shell_arguments_empty(const char *arguments);

/**
 * @brief Parses a raw input line into a `ShellCommand` structure.
 *        Separates the command word from its arguments.
 * @param line The input line to parse (will be modified).
 * @return     A `ShellCommand` structure.
 */
static ShellCommand shell_parse_command(char *line);

/**
 * @brief Converts a command word string to its corresponding `ShellCommandType` enum.
 * @param word The command word string.
 * @return     The `ShellCommandType` enum value.
 */
static ShellCommandType shell_command_type_from_word(const char *word);

/**
 * @brief Dispatches a parsed `ShellCommand` to its appropriate handler function.
 * @param engine  A pointer to the `Engine` instance.
 * @param command The `ShellCommand` to dispatch.
 * @return        `SHELL_CONTINUE` to continue the shell loop, `SHELL_STOP` to exit gracefully,
 *                or `SHELL_FATAL` to exit with an error.
 */
static int shell_dispatch_command(Engine *engine, ShellCommand command);

/**
 * @brief Handles the 'help' command, printing available commands and usage.
 * @param arguments The arguments provided to the 'help' command.
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_help(const char *arguments);

/**
 * @brief Handles the 'insert' command, inserting a single record.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'insert' command (id,gpa,name).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_insert(Engine *engine, const char *arguments);

/**
 * @brief Handles the 'insert_batch' command, inserting records from a file.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'insert_batch' command (file_path).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_insert_batch(Engine *engine, const char *arguments);

/**
 * @brief Handles the 'find' command, searching for a record by ID.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'find' command (id).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_find(Engine *engine, const char *arguments);

/**
 * @brief Handles the 'delete' command, deleting a record by ID.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'delete' command (id).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_delete(Engine *engine, const char *arguments);

/**
 * @brief Handles the 'update_name' command, updating a record's name.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'update_name' command (id,new_name).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_update_name(Engine *engine, const char *arguments);

/**
 * @brief Handles the 'update_gpa' command, updating a record's GPA.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'update_gpa' command (id,new_gpa).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_update_gpa(Engine *engine, const char *arguments);

/**
 * @brief Handles the 'range' command, querying records within a GPA range.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'range' command (min_gpa,max_gpa).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_range(Engine *engine, const char *arguments);

/**
 * @brief Handles the 'save' command, saving the current engine state to disk.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'save' command.
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_save(Engine *engine, const char *arguments);

/**
 * @brief Handles the 'save_exit' command, saving and then shutting down the engine.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'save_exit' command.
 * @return          `SHELL_STOP`.
 */
static int shell_handle_save_exit(Engine *engine, const char *arguments);

/**
 * @brief Handles an End-Of-File (EOF) condition on stdin, saving and exiting.
 * @param engine A pointer to the `Engine` instance.
 * @return       `0` on successful save/exit, `1` on error.
 */
static int shell_handle_eof(Engine *engine);

/**
 * @brief Parses a string into a `uint32_t` value.
 * @param text      The string to parse.
 * @param out_value Pointer to store the parsed `uint32_t`.
 * @return          `true` on successful parsing, `false` otherwise.
 */
static bool shell_parse_u32(const char *text, uint32_t *out_value);

/**
 * @brief Parses a string into a `float` value.
 * @param text      The string to parse.
 * @param out_value Pointer to store the parsed `float`.
 * @return          `true` on successful parsing, `false` otherwise.
 */
static bool shell_parse_float(const char *text, float *out_value);

/**
 * @brief Parses a record line string (e.g., "id,gpa,name") into a `StudentRecord` structure.
 * @param line       The record line string.
 * @param out_record Pointer to store the parsed `StudentRecord`.
 * @return           `true` on successful parsing, `false` otherwise.
 */
static bool shell_parse_record_line(const char *line, StudentRecord *out_record);

/**
 * @brief Parses an "id,name" pair from arguments.
 * @param arguments The argument string.
 * @param out_id    Pointer to store the parsed ID.
 * @param out_name  Buffer to store the parsed name.
 * @param name_size Size of the `out_name` buffer.
 * @return          `true` on successful parsing, `false` otherwise.
 */
static bool shell_parse_id_name_pair(const char *arguments, uint32_t *out_id, char *out_name, size_t name_size);

/**
 * @brief Parses an "id,float_value" pair from arguments.
 * @param arguments  The argument string.
 * @param out_id     Pointer to store the parsed ID.
 * @param out_value  Pointer to store the parsed float value.
 * @return           `true` on successful parsing, `false` otherwise.
 */
static bool shell_parse_id_float_pair(const char *arguments, uint32_t *out_id, float *out_value);

/**
 * @brief Parses a "min_float,max_float" range from arguments.
 * @param arguments The argument string.
 * @param out_min   Pointer to store the parsed minimum float.
 * @param out_max   Pointer to store the parsed maximum float.
 * @return          `true` on successful parsing, `false` otherwise.
 */
static bool shell_parse_float_range(const char *arguments, float *out_min, float *out_max);

/**
 * @brief Checks if a `uint32_t` ID is valid (not the tombstone ID).
 * @param id The ID to check.
 * @return   `true` if valid, `false` otherwise.
 */
static bool shell_is_valid_id(uint32_t id);

/**
 * @brief Checks if a `float` GPA is within the valid range [0.0f, 10.0f].
 * @param gpa The GPA to check.
 * @return    `true` if valid, `false` otherwise.
 */
static bool shell_is_valid_gpa(float gpa);

/**
 * @brief Checks if a name string is valid (not NULL, not empty, and within size limits).
 * @param name The name string to check.
 * @return     `true` if valid, `false` otherwise.
 */
static bool shell_is_valid_name(const char *name);

/**
 * @brief Loads records from a batch file into a dynamically allocated array.
 * @param path         Path to the batch file.
 * @param out_records  Pointer to a `StudentRecord` array pointer to store loaded records.
 * @param out_count    Pointer to store the number of loaded records.
 * @param out_error_line Pointer to store the line number where a parsing error occurred.
 * @return             `SHELL_BATCH_LOAD_OK` on success, or an error status.
 */
static ShellBatchLoadStatus shell_load_batch_file(const char *path, StudentRecord **out_records, size_t *out_count, size_t *out_error_line);

/**
 * @brief Grows a dynamically allocated `StudentRecord` array if needed.
 * @param records      Pointer to the `StudentRecord` array pointer.
 * @param capacity     Pointer to the current capacity of the array.
 * @param needed_count The number of elements required.
 * @return             `true` on success, `false` on allocation failure.
 */
static bool shell_grow_record_array(StudentRecord **records, size_t *capacity, size_t needed_count);

/**
 * @brief Prints the details of a `StudentRecord` to standard output.
 * @param record A pointer to the `StudentRecord` to print.
 */
static void shell_print_record(const StudentRecord *record);

/**
 * @brief Prints the details of an `EngineSearchResult` to standard output.
 * @param result A pointer to the `EngineSearchResult` to print.
 */
static void shell_print_search_result(const EngineSearchResult *result);

/**
 * @brief Prints a human-readable error message for an `EngineError`.
 * @param error The `EngineError` code.
 */
static void shell_print_engine_error(EngineError error);

/**
 * @brief Prints a usage error message for a command.
 * @param usage The correct usage string for the command.
 */
static void shell_print_usage_error(const char *usage);

/**
 * @brief Prints a message for an unknown command.
 */
static void shell_print_unknown_command(void);

/**
 * @brief Runs the interactive shell for the FRASE database engine.
 *
 * This function enters a loop, displays a prompt, reads user commands,
 * parses them, and dispatches them to appropriate handlers. It continues
 * until an exit command is given or an EOF is received.
 * @param engine A pointer to the `Engine` instance to interact with.
 * @return       `0` on successful exit, `1` on fatal error.
 */
int shell_run(Engine *engine)
{
    char line[SHELL_LINE_SIZE];

    if (engine == NULL) {
        fprintf(stderr, "shell: null engine\n");
        return 1;
    }

    printf("FRASE shell started. Type 'help' for commands.\n");

    for (;;) {
        ShellCommand command;
        int result;

        shell_print_prompt();

        if (!shell_read_line(line, sizeof(line))) {
            return shell_handle_eof(engine);
        }

        shell_trim_newline(line);
        command = shell_parse_command(line);
        result = shell_dispatch_command(engine, command);

        if (result == SHELL_STOP) {
            return 0;
        }

        if (result == SHELL_FATAL) {
            return 1;
        }
    }
}

/**
 * @brief Prints the shell prompt to standard output.
 */
static void shell_print_prompt(void)
{
    printf("frase> ");
    fflush(stdout);
}

static bool shell_read_line(char *line, size_t line_size)
/**
 * @brief Reads a line of input from stdin.
 * @param line      Buffer to store the read line.
 * @param line_size Maximum size of the buffer.
 * @return          `true` if a line was successfully read, `false` otherwise (e.g., EOF).
 */
{
    if (line == NULL || line_size == 0) {
        return false;
    }

    return fgets(line, (int)line_size, stdin) != NULL;
}

/**
 * @brief Removes trailing newline characters from a string.
 * @param line The string to trim.
 */
static void shell_trim_newline(char *line)
{
    size_t length;

    if (line == NULL) {
        return;
    }

    length = strlen(line);

    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[length - 1] = '\0';
        length--;
    }
}

/**
 * @brief Trims leading whitespace characters from a string.
 * @param text The string to trim.
 * @return     A pointer to the first non-whitespace character, or the original pointer if no leading whitespace.
 */
static char *shell_trim_left(char *text)
{
    if (text == NULL) {
        return NULL;
    }

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    return text;
}

/**
 * @brief Trims trailing whitespace characters from a string.
 * @param text The string to trim.
 */
static void shell_trim_right(char *text)
{
    size_t length;

    if (text == NULL) {
        return;
    }

    length = strlen(text);

    while (length > 0 && isspace((unsigned char)text[length - 1])) {
        text[length - 1] = '\0';
        length--;
    }
}

/**
 * @brief Trims both leading and trailing whitespace characters from a string.
 * @param text The string to trim.
 * @return     A pointer to the trimmed string.
 */
static char *shell_trim(char *text)
{
    char *left;

    left = shell_trim_left(text);
    shell_trim_right(left);

    return left;
}

/**
 * @brief Checks if a string contains only whitespace or is empty.
 * @param arguments The string to check.
 * @return          `true` if the string is empty or contains only whitespace, `false` otherwise.
 */
static bool shell_arguments_empty(const char *arguments)
{
    const unsigned char *cursor;

    if (arguments == NULL) {
        return true;
    }

    cursor = (const unsigned char *)arguments;

    while (*cursor != '\0') {
        if (!isspace(*cursor)) {
            return false;
        }

        cursor++;
    }

    return true;
}

/**
 * @brief Parses a raw input line into a `ShellCommand` structure.
 *        Separates the command word from its arguments.
 * @param line The input line to parse (will be modified).
 * @return     A `ShellCommand` structure.
 */
static ShellCommand shell_parse_command(char *line)
{
    ShellCommand command;
    char *text;
    char *cursor;

    command.type = SHELL_COMMAND_EMPTY;
    command.arguments = NULL;

    if (line == NULL) {
        return command;
    }

    text = shell_trim(line);

    if (*text == '\0') {
        return command;
    }

    cursor = text;

    while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
        cursor++;
    }

    if (*cursor != '\0') {
        *cursor = '\0';
        cursor++;
        command.arguments = shell_trim(cursor);
    } else {
        command.arguments = cursor;
    }

    command.type = shell_command_type_from_word(text);

    return command;
}

/**
 * @brief Converts a command word string to its corresponding `ShellCommandType` enum.
 * @param word The command word string.
 * @return     The `ShellCommandType` enum value.
 */
static ShellCommandType shell_command_type_from_word(const char *word)
{
    if (word == NULL || *word == '\0') {
        return SHELL_COMMAND_EMPTY;
    }

    if (strcmp(word, "help") == 0) return SHELL_COMMAND_HELP;
    if (strcmp(word, "insert") == 0) return SHELL_COMMAND_INSERT;
    if (strcmp(word, "insert_batch") == 0) return SHELL_COMMAND_INSERT_BATCH;
    if (strcmp(word, "find") == 0) return SHELL_COMMAND_FIND;
    if (strcmp(word, "delete") == 0) return SHELL_COMMAND_DELETE;
    if (strcmp(word, "update_name") == 0) return SHELL_COMMAND_UPDATE_NAME;
    if (strcmp(word, "update_gpa") == 0) return SHELL_COMMAND_UPDATE_GPA;
    if (strcmp(word, "range") == 0) return SHELL_COMMAND_RANGE;
    if (strcmp(word, "save") == 0) return SHELL_COMMAND_SAVE;
    if (strcmp(word, "save_exit") == 0) return SHELL_COMMAND_SAVE_EXIT;

    return SHELL_COMMAND_UNKNOWN;
}

/**
 * @brief Dispatches a parsed `ShellCommand` to its appropriate handler function.
 * @param engine  A pointer to the `Engine` instance.
 * @param command The `ShellCommand` to dispatch.
 * @return        `SHELL_CONTINUE` to continue the shell loop, `SHELL_STOP` to exit gracefully,
 *                or `SHELL_FATAL` to exit with an error.
 */

static int shell_dispatch_command(Engine *engine, ShellCommand command)
{
    switch (command.type) {
        case SHELL_COMMAND_EMPTY:
            return SHELL_CONTINUE;
        case SHELL_COMMAND_HELP:
            return shell_handle_help(command.arguments);
        case SHELL_COMMAND_INSERT:
            return shell_handle_insert(engine, command.arguments);
        case SHELL_COMMAND_INSERT_BATCH:
            return shell_handle_insert_batch(engine, command.arguments);
        case SHELL_COMMAND_FIND:
            return shell_handle_find(engine, command.arguments);
        case SHELL_COMMAND_DELETE:
            return shell_handle_delete(engine, command.arguments);
        case SHELL_COMMAND_UPDATE_NAME:
            return shell_handle_update_name(engine, command.arguments);
        case SHELL_COMMAND_UPDATE_GPA:
            return shell_handle_update_gpa(engine, command.arguments);
        case SHELL_COMMAND_RANGE:
            return shell_handle_range(engine, command.arguments);
        case SHELL_COMMAND_SAVE:
            return shell_handle_save(engine, command.arguments);
        case SHELL_COMMAND_SAVE_EXIT:
            return shell_handle_save_exit(engine, command.arguments);
        case SHELL_COMMAND_UNKNOWN:
        default:
            shell_print_unknown_command();
            return SHELL_CONTINUE;
    }
}

/**
 * @brief Handles the 'help' command, printing available commands and usage.
 * @param arguments The arguments provided to the 'help' command.
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_help(const char *arguments)
{
    if (!shell_arguments_empty(arguments)) {
        shell_print_usage_error("help");
        return SHELL_CONTINUE;
    }

    printf("Commands:\n");
    printf("  help\n");
    printf("  insert <id>,<gpa>,<name>\n");
    printf("  insert_batch <file_path>\n");
    printf("  find <id>\n");
    printf("  delete <id>\n");
    printf("  update_name <id>,<new_name>\n");
    printf("  update_gpa <id>,<new_gpa>\n");
    printf("  range <min_gpa>,<max_gpa>\n");
    printf("  save\n");
    printf("  save_exit\n");
    printf("\nExamples:\n");
    printf("  insert 101,8.75,Saket Lele\n");
    printf("  insert_batch students.txt\n");
    printf("  find 101\n");
    printf("  update_gpa 101,9.20\n");
    printf("  range 8.0,10.0\n");
    printf("\nBatch file rows use the same record format:\n");
    printf("  101,8.75,Saket Lele\n");
    printf("  102,9.10,Aarav Sharma\n");

    return SHELL_CONTINUE;
}

/**
 * @brief Handles the 'insert' command, inserting a single record.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'insert' command (id,gpa,name).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_insert(Engine *engine, const char *arguments)
{
    StudentRecord record;
    EngineError error;

    if (!shell_parse_record_line(arguments, &record)) {
        shell_print_usage_error("insert <id>,<gpa>,<name>");
        return SHELL_CONTINUE;
    }

    error = engine_insert_one(engine, record);

    if (error != ENGINE_OK) {
        shell_print_engine_error(error);
        return SHELL_CONTINUE;
    }

    printf("Inserted record %" PRIu32 ".\n", record.id);

    return SHELL_CONTINUE;
}

/**
 * @brief Handles the 'insert_batch' command, inserting records from a file.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'insert_batch' command (file_path).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_insert_batch(Engine *engine, const char *arguments)
{
    char path_buffer[SHELL_LINE_SIZE];
    char *path;
    StudentRecord *records;
    size_t count;
    size_t error_line;
    ShellBatchLoadStatus load_status;
    EngineError error;

    if (arguments == NULL || strlen(arguments) >= sizeof(path_buffer)) {
        shell_print_usage_error("insert_batch <file_path>");
        return SHELL_CONTINUE;
    }

    strcpy(path_buffer, arguments);
    path = shell_trim(path_buffer);

    if (*path == '\0') {
        shell_print_usage_error("insert_batch <file_path>");
        return SHELL_CONTINUE;
    }

    records = NULL;
    count = 0;
    error_line = 0;

    load_status = shell_load_batch_file(path, &records, &count, &error_line);

    if (load_status == SHELL_BATCH_LOAD_OPEN_FAILED) {
        printf("Could not open batch file: %s\n", path);
        return SHELL_CONTINUE;
    }

    if (load_status == SHELL_BATCH_LOAD_ALLOCATION_FAILED) {
        printf("Could not allocate memory for batch records.\n");
        return SHELL_CONTINUE;
    }

    if (load_status == SHELL_BATCH_LOAD_PARSE_FAILED) {
        printf("Invalid batch row at line %zu. Expected: <id>,<gpa>,<name>\n", error_line);
        return SHELL_CONTINUE;
    }

    if (count == 0) {
        free(records);
        printf("Batch file contains no records.\n");
        return SHELL_CONTINUE;
    }

    error = engine_insert_batch(engine, records, count);
    free(records);

    if (error != ENGINE_OK) {
        shell_print_engine_error(error);
        return SHELL_CONTINUE;
    }

    printf("Inserted %zu records.\n", count);

    return SHELL_CONTINUE;
}

/**
 * @brief Handles the 'find' command, searching for a record by ID.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'find' command (id).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_find(Engine *engine, const char *arguments)
{
    uint32_t id;
    EngineSearchResult result;
    EngineError error;

    if (!shell_parse_u32(arguments, &id) || !shell_is_valid_id(id)) {
        shell_print_usage_error("find <id>");
        return SHELL_CONTINUE;
    }

    error = engine_find_by_id(engine, id, &result);

    if (error != ENGINE_OK) {
        shell_print_engine_error(error);
        return SHELL_CONTINUE;
    }

    shell_print_search_result(&result);

    return SHELL_CONTINUE;
}

/**
 * @brief Handles the 'delete' command, deleting a record by ID.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'delete' command (id).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_delete(Engine *engine, const char *arguments)
{
    uint32_t id;
    EngineError error;

    if (!shell_parse_u32(arguments, &id) || !shell_is_valid_id(id)) {
        shell_print_usage_error("delete <id>");
        return SHELL_CONTINUE;
    }

    error = engine_delete_by_id(engine, id);

    if (error != ENGINE_OK) {
        shell_print_engine_error(error);
        return SHELL_CONTINUE;
    }

    printf("Deleted record %" PRIu32 ".\n", id);

    return SHELL_CONTINUE;
}

/**
 * @brief Handles the 'update_name' command, updating a record's name.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'update_name' command (id,new_name).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_update_name(Engine *engine, const char *arguments)
{
    uint32_t id;
    char name[FRASE_STUDENT_NAME_SIZE];
    EngineError error;

    if (!shell_parse_id_name_pair(arguments, &id, name, sizeof(name))) {
        shell_print_usage_error("update_name <id>,<new_name>");
        return SHELL_CONTINUE;
    }

    error = engine_update_name(engine, id, name);

    if (error != ENGINE_OK) {
        shell_print_engine_error(error);
        return SHELL_CONTINUE;
    }

    printf("Updated name for record %" PRIu32 ".\n", id);

    return SHELL_CONTINUE;
}

/**
 * @brief Handles the 'update_gpa' command, updating a record's GPA.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'update_gpa' command (id,new_gpa).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_update_gpa(Engine *engine, const char *arguments)
{
    uint32_t id;
    float gpa;
    EngineError error;

    if (!shell_parse_id_float_pair(arguments, &id, &gpa) || !shell_is_valid_gpa(gpa)) {
        shell_print_usage_error("update_gpa <id>,<new_gpa>");
        return SHELL_CONTINUE;
    }

    error = engine_update_gpa(engine, id, gpa);

    if (error != ENGINE_OK) {
        shell_print_engine_error(error);
        return SHELL_CONTINUE;
    }

    printf("Updated GPA for record %" PRIu32 ".\n", id);

    return SHELL_CONTINUE;
}

/**
 * @brief Handles the 'range' command, querying records within a GPA range.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'range' command (min_gpa,max_gpa).
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_range(Engine *engine, const char *arguments)
{
    float min_gpa;
    float max_gpa;
    size_t count;
    size_t out_count;
    EngineSearchResult *results;
    EngineError error;
    size_t i;

    if (!shell_parse_float_range(arguments, &min_gpa, &max_gpa)) {
        shell_print_usage_error("range <min_gpa>,<max_gpa>");
        return SHELL_CONTINUE;
    }

    count = engine_count_gpa_range(engine, min_gpa, max_gpa);

    if (count == 0) {
        printf("No records found.\n");
        return SHELL_CONTINUE;
    }

    if (count > ((size_t)-1) / sizeof(*results)) {
        printf("Too many results to allocate.\n");
        return SHELL_CONTINUE;
    }

    results = malloc(count * sizeof(*results));

    if (results == NULL) {
        printf("Could not allocate result array.\n");
        return SHELL_CONTINUE;
    }

    out_count = 0;
    error = engine_query_gpa_range(engine, min_gpa, max_gpa, results, count, &out_count);

    if (error != ENGINE_OK) {
        free(results);
        shell_print_engine_error(error);
        return SHELL_CONTINUE;
    }

    printf("Found %zu records.\n", out_count);

    for (i = 0; i < out_count; i++) {
        shell_print_search_result(&results[i]);
    }

    free(results);

    return SHELL_CONTINUE;
}

/**
 * @brief Handles the 'save' command, saving the current engine state to disk.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'save' command.
 * @return          `SHELL_CONTINUE`.
 */
static int shell_handle_save(Engine *engine, const char *arguments)
{
    EngineError error;

    if (!shell_arguments_empty(arguments)) {
        shell_print_usage_error("save");
        return SHELL_CONTINUE;
    }

    error = engine_save(engine);

    if (error != ENGINE_OK) {
        shell_print_engine_error(error);
        return SHELL_CONTINUE;
    }

    printf("Saved.\n");

    return SHELL_CONTINUE;
}

/**
 * @brief Handles the 'save_exit' command, saving and then shutting down the engine.
 * @param engine    A pointer to the `Engine` instance.
 * @param arguments The arguments for the 'save_exit' command.
 * @return          `SHELL_STOP`.
 */
static int shell_handle_save_exit(Engine *engine, const char *arguments)
{
    EngineError error;

    if (!shell_arguments_empty(arguments)) {
        shell_print_usage_error("save_exit");
        return SHELL_CONTINUE;
    }

    error = engine_shutdown(engine, true);

    if (error != ENGINE_OK) {
        shell_print_engine_error(error);
        return SHELL_CONTINUE;
    }

    printf("Saved and exited.\n");

    return SHELL_STOP;
}

/**
 * @brief Handles an End-Of-File (EOF) condition on stdin, saving and exiting.
 * @param engine A pointer to the `Engine` instance.
 * @return       `0` on successful save/exit, `1` on error.
 */
static int shell_handle_eof(Engine *engine)
{
    EngineError error;

    printf("\nEOF received. Saving and exiting.\n");

    error = engine_shutdown(engine, true);

    if (error != ENGINE_OK) {
        shell_print_engine_error(error);
        return 1;
    }

    return 0;
}

/**
 * @brief Parses a string into a `uint32_t` value.
 * @param text      The string to parse.
 * @param out_value Pointer to store the parsed `uint32_t`.
 * @return          `true` on successful parsing, `false` otherwise.
 */
static bool shell_parse_u32(const char *text, uint32_t *out_value)
{
    char buffer[SHELL_LINE_SIZE];
    char *trimmed;
    char *end;
    unsigned long value;

    if (text == NULL || out_value == NULL || strlen(text) >= sizeof(buffer)) {
        return false;
    }

    strcpy(buffer, text);
    trimmed = shell_trim(buffer);

    if (*trimmed == '\0' || *trimmed == '-') {
        return false;
    }

    errno = 0;
    value = strtoul(trimmed, &end, 10);

    if (trimmed == end || errno == ERANGE) {
        return false;
    }

    end = shell_trim_left(end);

    if (*end != '\0' || value > UINT32_MAX) {
        return false;
    }

    *out_value = (uint32_t)value;

    return true;
}

/**
 * @brief Parses a string into a `float` value.
 * @param text      The string to parse.
 * @param out_value Pointer to store the parsed `float`.
 * @return          `true` on successful parsing, `false` otherwise.
 */
static bool shell_parse_float(const char *text, float *out_value)
{
    char buffer[SHELL_LINE_SIZE];
    char *trimmed;
    char *end;
    float value;

    if (text == NULL || out_value == NULL || strlen(text) >= sizeof(buffer)) {
        return false;
    }

    strcpy(buffer, text);
    trimmed = shell_trim(buffer);

    if (*trimmed == '\0') {
        return false;
    }

    errno = 0;
    value = strtof(trimmed, &end);

    if (trimmed == end || errno == ERANGE) {
        return false;
    }

    end = shell_trim_left(end);

    if (*end != '\0') {
        return false;
    }

    *out_value = value;

    return true;
}

/**
 * @brief Parses a record line string (e.g., "id,gpa,name") into a `StudentRecord` structure.
 * @param line       The record line string.
 * @param out_record Pointer to store the parsed `StudentRecord`.
 * @return           `true` on successful parsing, `false` otherwise.
 */
static bool shell_parse_record_line(const char *line, StudentRecord *out_record)
{
    char buffer[SHELL_LINE_SIZE];
    char *text;
    char *first_comma;
    char *second_comma;
    char *id_text;
    char *gpa_text;
    char *name_text;
    uint32_t id;
    float gpa;
    size_t name_length;

    if (line == NULL || out_record == NULL || strlen(line) >= sizeof(buffer)) {
        return false;
    }

    strcpy(buffer, line);
    text = shell_trim(buffer);

    first_comma = strchr(text, ',');

    if (first_comma == NULL) {
        return false;
    }

    *first_comma = '\0';
    second_comma = strchr(first_comma + 1, ',');

    if (second_comma == NULL) {
        return false;
    }

    *second_comma = '\0';

    id_text = shell_trim(text);
    gpa_text = shell_trim(first_comma + 1);
    name_text = shell_trim(second_comma + 1);

    if (strchr(name_text, ',') != NULL) {
        return false;
    }

    if (!shell_parse_u32(id_text, &id) || !shell_parse_float(gpa_text, &gpa)) {
        return false;
    }

    if (!shell_is_valid_id(id) || !shell_is_valid_gpa(gpa) || !shell_is_valid_name(name_text)) {
        return false;
    }

    name_length = strlen(name_text);

    memset(out_record, 0, sizeof(*out_record));
    out_record->id = id;
    out_record->gpa = gpa;
    memcpy(out_record->name, name_text, name_length + 1);

    return true;
}

/**
 * @brief Parses an "id,name" pair from arguments.
 * @param arguments The argument string.
 * @param out_id    Pointer to store the parsed ID.
 * @param out_name  Buffer to store the parsed name.
 * @param name_size Size of the `out_name` buffer.
 * @return          `true` on successful parsing, `false` otherwise.
 */
static bool shell_parse_id_name_pair(const char *arguments, uint32_t *out_id, char *out_name, size_t name_size)
{
    char buffer[SHELL_LINE_SIZE];
    char *text;
    char *comma;
    char *id_text;
    char *name_text;
    uint32_t id;
    size_t name_length;

    if (arguments == NULL || out_id == NULL || out_name == NULL || name_size == 0 || strlen(arguments) >= sizeof(buffer)) {
        return false;
    }

    strcpy(buffer, arguments);
    text = shell_trim(buffer);
    comma = strchr(text, ',');

    if (comma == NULL) {
        return false;
    }

    *comma = '\0';
    id_text = shell_trim(text);
    name_text = shell_trim(comma + 1);

    if (strchr(name_text, ',') != NULL) {
        return false;
    }

    if (!shell_parse_u32(id_text, &id) || !shell_is_valid_id(id) || !shell_is_valid_name(name_text)) {
        return false;
    }

    name_length = strlen(name_text);

    if (name_length >= name_size) {
        return false;
    }

    memset(out_name, 0, name_size);
    memcpy(out_name, name_text, name_length + 1);
    *out_id = id;

    return true;
}

/**
 * @brief Parses an "id,float_value" pair from arguments.
 * @param arguments  The argument string.
 * @param out_id     Pointer to store the parsed ID.
 * @param out_value  Pointer to store the parsed float value.
 * @return           `true` on successful parsing, `false` otherwise.
 */
static bool shell_parse_id_float_pair(const char *arguments, uint32_t *out_id, float *out_value)
{
    char buffer[SHELL_LINE_SIZE];
    char *text;
    char *comma;
    char *id_text;
    char *value_text;
    uint32_t id;
    float value;

    if (arguments == NULL || out_id == NULL || out_value == NULL || strlen(arguments) >= sizeof(buffer)) {
        return false;
    }

    strcpy(buffer, arguments);
    text = shell_trim(buffer);
    comma = strchr(text, ',');

    if (comma == NULL) {
        return false;
    }

    *comma = '\0';
    id_text = shell_trim(text);
    value_text = shell_trim(comma + 1);

    if (!shell_parse_u32(id_text, &id) || !shell_parse_float(value_text, &value)) {
        return false;
    }

    if (!shell_is_valid_id(id)) {
        return false;
    }

    *out_id = id;
    *out_value = value;

    return true;
}

/**
 * @brief Parses a "min_float,max_float" range from arguments.
 * @param arguments The argument string.
 * @param out_min   Pointer to store the parsed minimum float.
 * @param out_max   Pointer to store the parsed maximum float.
 * @return          `true` on successful parsing, `false` otherwise.
 */
static bool shell_parse_float_range(const char *arguments, float *out_min, float *out_max)
{
    char buffer[SHELL_LINE_SIZE];
    char *text;
    char *comma;
    char *min_text;
    char *max_text;
    float min_gpa;
    float max_gpa;

    if (arguments == NULL || out_min == NULL || out_max == NULL || strlen(arguments) >= sizeof(buffer)) {
        return false;
    }

    strcpy(buffer, arguments);
    text = shell_trim(buffer);
    comma = strchr(text, ',');

    if (comma == NULL) {
        return false;
    }

    *comma = '\0';
    min_text = shell_trim(text);
    max_text = shell_trim(comma + 1);

    if (!shell_parse_float(min_text, &min_gpa) || !shell_parse_float(max_text, &max_gpa)) {
        return false;
    }

    if (!shell_is_valid_gpa(min_gpa) || !shell_is_valid_gpa(max_gpa)) {
        return false;
    }

    if (min_gpa > max_gpa) {
        return false;
    }

    *out_min = min_gpa;
    *out_max = max_gpa;

    return true;
}

/**
 * @brief Checks if a `uint32_t` ID is valid (not the tombstone ID).
 * @param id The ID to check.
 * @return   `true` if valid, `false` otherwise.
 */
static bool shell_is_valid_id(uint32_t id)
{
    return id != FRASE_TOMBSTONE_ID;
}

static bool shell_is_valid_gpa(float gpa)
{
/**
 * @brief Checks if a `float` GPA is within the valid range [0.0f, 10.0f].
 * @param gpa The GPA to check.
 * @return    `true` if valid, `false` otherwise.
 */
    return gpa >= 0.0f && gpa <= 10.0f;
}

static bool shell_is_valid_name(const char *name)
{
    if (name == NULL) {
        return false;
    }

    if (*name == '\0') {
        return false;
    }

    return strlen(name) < FRASE_STUDENT_NAME_SIZE;
/**
 * @brief Checks if a name string is valid (not NULL, not empty, and within size limits).
 * @param name The name string to check.
 * @return     `true` if valid, `false` otherwise.
 */



}

static ShellBatchLoadStatus shell_load_batch_file(const char *path, StudentRecord **out_records, size_t *out_count, size_t *out_error_line)
{
    FILE *file;
    char line[SHELL_LINE_SIZE];
    StudentRecord *records;
    size_t capacity;
    size_t count;
    size_t line_number;

/**
 * @brief Loads records from a batch file into a dynamically allocated array.
 * @param path         Path to the batch file.
 * @param out_records  Pointer to a `StudentRecord` array pointer to store loaded records.
 * @param out_count    Pointer to store the number of loaded records.
 * @param out_error_line Pointer to store the line number where a parsing error occurred.
 * @return             `SHELL_BATCH_LOAD_OK` on success, or an error status.
 */
    if (path == NULL || out_records == NULL || out_count == NULL || out_error_line == NULL) {
        return SHELL_BATCH_LOAD_PARSE_FAILED;
    }

    file = fopen(path, "r");

    if (file == NULL) {
        return SHELL_BATCH_LOAD_OPEN_FAILED;
    }

    capacity = SHELL_INITIAL_BATCH_CAPACITY;
    count = 0;
    line_number = 0;

    records = malloc(capacity * sizeof(*records));

    if (records == NULL) {
        fclose(file);
        return SHELL_BATCH_LOAD_ALLOCATION_FAILED;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *trimmed;
        StudentRecord record;

        line_number++;
        shell_trim_newline(line);
        trimmed = shell_trim(line);

        if (*trimmed == '\0') {
            continue;
        }

        if (!shell_parse_record_line(trimmed, &record)) {
            free(records);
            fclose(file);
            *out_error_line = line_number;
            return SHELL_BATCH_LOAD_PARSE_FAILED;
        }

        if (!shell_grow_record_array(&records, &capacity, count + 1)) {
            free(records);
            fclose(file);
            return SHELL_BATCH_LOAD_ALLOCATION_FAILED;
        }

        records[count] = record;
        count++;
    }

    if (ferror(file)) {
        free(records);
        fclose(file);
        return SHELL_BATCH_LOAD_PARSE_FAILED;
    }

    fclose(file);

    *out_records = records;
    *out_count = count;
    *out_error_line = 0;

    return SHELL_BATCH_LOAD_OK;
}

static bool shell_grow_record_array(StudentRecord **records, size_t *capacity, size_t needed_count)
{
    StudentRecord *new_records;
    size_t new_capacity;

    if (records == NULL || capacity == NULL || *records == NULL) {
        return false;
    }

    if (needed_count <= *capacity) {
        return true;
    }

    new_capacity = *capacity;

    while (new_capacity < needed_count) {
        if (new_capacity > ((size_t)-1) / 2u) {
            return false;
        }

        new_capacity *= 2u;
    }

    if (new_capacity > ((size_t)-1) / sizeof(**records)) {
        return false;
    }

    new_records = realloc(*records, new_capacity * sizeof(**records));

    if (new_records == NULL) {
        return false;
    }

    *records = new_records;
    *capacity = new_capacity;

    return true;
}

/**
 * @brief Prints the details of a `StudentRecord` to standard output.
 * @param record A pointer to the `StudentRecord` to print.
 */
static void shell_print_record(const StudentRecord *record)
{
    if (record == NULL) {
        return;
    }

    printf("ID: %" PRIu32 "\n", record->id);
    printf("GPA: %.2f\n", record->gpa);
    printf("Name: %s\n", record->name);
}

/**
 * @brief Prints the details of an `EngineSearchResult` to standard output.
 * @param result A pointer to the `EngineSearchResult` to print.
 */
static void shell_print_search_result(const EngineSearchResult *result)
{
    if (result == NULL || result->record == NULL) {
        return;
    }

    shell_print_record(result->record);
}

/**
 * @brief Prints a human-readable error message for an `EngineError`.
 * @param error The `EngineError` code.
 */
static void shell_print_engine_error(EngineError error)
{
    printf("Engine error: %s\n", engine_error_string(error));
}

/**
 * @brief Prints a usage error message for a command.
 * @param usage The correct usage string for the command.
 */
static void shell_print_usage_error(const char *usage)
{
    printf("Usage: %s\n", usage);
}

/**
 * @brief Prints a message for an unknown command.
 */
static void shell_print_unknown_command(void)
{
    printf("Unknown command. Type 'help' for commands.\n");
}