/**
 * @file git-whoami.c
 * @author Alberto Ielpo <alberto.ielpo@gmail.com>
 * @brief CLI tool for managing and switching git user identities.
 * @license MIT
 *
 * Identities (name, email, optional signing key) are stored in
 * ~/.config/git-whoami/data as a delimited file and applied to
 * the current repository via `git config`.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_NAME 128
#define MAX_EMAIL 256
#define MAX_KEY 512
#define MAX_LINE (MAX_NAME + MAX_EMAIL + MAX_KEY + 4)
#define MAX_IDENTITIES 128
#define MAX_PATH_LEN 768

// storage file .config/git-whoami/data
#define STORAGE_CONFIG_NAME ".config"
#define STORAGE_PRJ_NAME "git-whoami"
#define STORAGE_FILE_NAME "data"
#define STORAGE_DELIMITER '|'

#define VERSION "1.1.0"

typedef struct {
    char name[MAX_NAME];
    char email[MAX_EMAIL];
    char signing_key[MAX_KEY];
} Identity;

/**
 * @brief Strip all invalid characters from a string in-place.
 * @param s Null-terminated string to sanitize; modified in place.
 */
static void sanitize(char *s) {
    char *r = s, *w = s;
    while (*r) {
        // from space to ~ all chars accepted (except pipe)
        if (*r >= 32 && *r <= 126 && *r != '|')
            *w++ = *r;
        r++;
    }
    *w = 0;
}

/**
 * @brief Create ~/.config and ~/.config/git-whoami if they do not exist.
 *
 * Exits the process on failure.
 */
static void ensure_config_dir(void) {
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "HOME is not set\n");
        exit(EXIT_FAILURE);
    }
    char path[MAX_PATH_LEN];
    int n = snprintf(path, sizeof(path), "%s/%s", home, STORAGE_CONFIG_NAME);
    if (n < 0 || n >= (int)sizeof(path)) {
        fprintf(stderr, "path too long\n");
        exit(EXIT_FAILURE);
    }
    if (mkdir(path, 0700) != 0 && errno != EEXIST) {
        perror("mkdir");
        exit(EXIT_FAILURE);
    }
    n = snprintf(path, sizeof(path), "%s/%s/%s", home, STORAGE_CONFIG_NAME, STORAGE_PRJ_NAME);
    if (n < 0 || n >= (int)sizeof(path)) {
        fprintf(stderr, "path too long\n");
        exit(EXIT_FAILURE);
    }
    if (mkdir(path, 0700) != 0 && errno != EEXIST) {
        perror("mkdir");
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Write the absolute path to the identity data file into @p buf.
 * @param buf  Output buffer.
 * @param len  Size of @p buf in bytes.
 *
 * Exits the process if the path would be truncated.
 */
static void get_data_path(char *buf, size_t len) {
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "HOME is not set\n");
        exit(EXIT_FAILURE);
    }
    int n = snprintf(buf, len, "%s/%s/%s/%s", home, STORAGE_CONFIG_NAME, STORAGE_PRJ_NAME, STORAGE_FILE_NAME);
    if (n < 0 || n >= (int)len) {
        fprintf(stderr, "path too long\n");
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Load identities from the data file into @p ids.
 * @param ids   Array of at least @c MAX_IDENTITIES entries to populate.
 * @param count Set to the number of identities loaded; 0 if the file is absent.
 * @return 0 on success (including a missing file), -1 on I/O error.
 */
static int load_identities(Identity *ids, int *count) {
    char path[MAX_PATH_LEN];
    get_data_path(path, sizeof(path));

    FILE *fp = fopen(path, "r");
    if (!fp) {
        *count = 0;
        return 0;
    }

    char line[MAX_LINE];
    *count = 0;

    while (fgets(line, sizeof(line), fp) != NULL && *count < MAX_IDENTITIES) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = 0;
        if (len == 0)
            continue;

        char *del1 = strchr(line, STORAGE_DELIMITER);
        if (!del1)
            continue;
        *del1 = 0;
        char *del2 = strchr(del1 + 1, STORAGE_DELIMITER);
        if (!del2)
            continue;
        *del2 = 0;

        Identity *id = &ids[*count];
        strncpy(id->name, line, MAX_NAME - 1);
        id->name[MAX_NAME - 1] = 0;
        strncpy(id->email, del1 + 1, MAX_EMAIL - 1);
        id->email[MAX_EMAIL - 1] = 0;
        strncpy(id->signing_key, del2 + 1, MAX_KEY - 1);
        id->signing_key[MAX_KEY - 1] = 0;
        (*count)++;
    }

    fclose(fp);
    return 0;
}

/**
 * @brief Atomically persist @p count identities to the data file.
 *
 * Writes to a temporary file first, then renames it over the target so that
 * a crash mid-write cannot corrupt existing data.
 *
 * @param ids   Array of identities to write.
 * @param count Number of entries in @p ids.
 * @return 0 on success, -1 on error.
 */
static int save_identities(const Identity *ids, int count) {
    ensure_config_dir();
    char path[MAX_PATH_LEN];
    get_data_path(path, sizeof(path));

    char tmp[MAX_PATH_LEN];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n < 0 || n >= (int)sizeof(tmp)) {
        fprintf(stderr, "path too long\n");
        return -1;
    }

    FILE *fp = fopen(tmp, "w");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    if (fchmod(fileno(fp), 0600) != 0) {
        perror("fchmod");
        fclose(fp);
        unlink(tmp);
        return -1;
    }

    for (int ii = 0; ii < count; ii++) {
        // example: name|mail|signing_key where | is storage delimiter
        if (fprintf(fp, "%s%c%s%c%s\n", ids[ii].name, STORAGE_DELIMITER, ids[ii].email, STORAGE_DELIMITER, ids[ii].signing_key) < 0) {
            fclose(fp);
            unlink(tmp);
            return -1;
        }
    }
    if (fclose(fp) != 0) {
        unlink(tmp);
        return -1;
    }

    if (rename(tmp, path) != 0) {
        perror("rename");
        unlink(tmp);
        return -1;
    }

    return 0;
}

/**
 * @brief Read a single git config value without spawning a shell.
 *
 * Forks a child that runs `git config <key>` and captures its first output
 * line.  Uses pipe + fork + execvp to avoid shell injection.
 *
 * @param key    Git config key (e.g. "user.email").
 * @param buf    Buffer to receive the null-terminated value.
 * @param buflen Size of @p buf in bytes.
 * @return 0 if the key was found and fits in @p buf, -1 otherwise.
 */
static int git_config_get(const char *key, char *buf, size_t buflen) {
    // fds[0] = read end, fds[1] = write end; kernel connects them
    int fds[2];
    if (pipe(fds) != 0)
        return -1;

    // both ends of the pipe exist in both processes after fork
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        // child: redirect stdout into the write end so git's output flows through the pipe
        dup2(fds[1], STDOUT_FILENO);
        // suppress git's error messages (e.g. "not a git repository") from reaching the terminal
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        // close the raw fds — stdout is now the alias for fds[1]
        close(fds[0]);
        close(fds[1]);
        const char *args[] = {"git", "config", key, NULL};
        execvp("git", (char *const *)args);
        // _exit() terminates immediately without flushing or running handlers (forked child)
        _exit(127);
    }

    // parent: close the write end so fgets sees EOF when the child exits;
    // if left open the read would block forever waiting for a writer that never closes
    close(fds[1]);
    FILE *fp = fdopen(fds[0], "r");
    if (!fp) {
        close(fds[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }

    // read the first (and only expected) line from git's stdout
    int got = (fgets(buf, (int)buflen, fp) != NULL);
    fclose(fp);

    // reap the child to avoid a zombie; also gives us the exit code
    int status;
    if (waitpid(pid, &status, 0) < 0)
        return -1;

    // strip the trailing newline that git config always appends
    if (got) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[len - 1] = 0;
    }

    // treat a missing key (non-zero exit) or empty read as failure
    if (!got || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1;

    return 0;
}

/**
 * @brief Fork and exec a process described by a NULL-terminated argv array.
 * @param argv NULL-terminated argument vector; argv[0] is the executable name.
 * @return Exit status of the child process, or -1 on fork/wait failure.
 */
static int run_argv(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        // child: replace this process image with the requested command;
        // stdout is inherited unchanged — output goes straight to the terminal
        execvp(argv[0], argv);
        // _exit() terminates immediately without flushing or running handlers (forked child)
        _exit(127);
    }
    // parent: wait for the child and forward its exit code; no output to capture
    int status;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/**
 * @brief Apply an identity to the current repository's git config.
 *
 * Sets user.name and user.email unconditionally.  If the identity has a
 * signing key it is set; otherwise user.signing_key is unset (errors ignored
 * because the key may not exist).
 *
 * @param id Identity to apply.
 * @return 0 on success, -1 if any required git-config call fails.
 */
static int apply_identity(const Identity *id) {
    char name[MAX_NAME], email[MAX_EMAIL], signing_key[MAX_KEY];
    snprintf(name, sizeof(name), "%s", id->name);
    snprintf(email, sizeof(email), "%s", id->email);
    snprintf(signing_key, sizeof(signing_key), "%s", id->signing_key);

    const char *set_name[] = {"git", "config", "user.name", name, NULL};
    const char *set_email[] = {"git", "config", "user.email", email, NULL};

    if (run_argv((char *const *)set_name) != 0)
        return -1;
    if (run_argv((char *const *)set_email) != 0)
        return -1;

    if (signing_key[0] != 0) {
        const char *set_key[] = {"git", "config", "user.signingkey", signing_key, NULL};
        if (run_argv((char *const *)set_key) != 0)
            return -1;
        const char *set_fmt[] = {"git", "config", "gpg.format", "ssh", NULL};
        if (run_argv((char *const *)set_fmt) != 0)
            return -1;
        const char *set_sign[] = {"git", "config", "commit.gpgsign", "true", NULL};
        if (run_argv((char *const *)set_sign) != 0)
            return -1;
    } else {
        const char *unset_key[] = {"git", "config", "--unset", "user.signingkey", NULL};
        run_argv((char *const *)unset_key);
        const char *unset_fmt[] = {"git", "config", "--unset", "gpg.format", NULL};
        run_argv((char *const *)unset_fmt);
        const char *unset_sign[] = {"git", "config", "--unset", "commit.gpgsign", NULL};
        run_argv((char *const *)unset_sign);
    }

    return 0;
}

/**
 * @brief Resolve a command-line argument to an identity index.
 *
 * If @p arg is a decimal integer in the range [1, count] it is treated as a
 * 1-based list index.  Otherwise the identities are searched by exact email
 * match.
 *
 * @param ids   Array of loaded identities.
 * @param count Number of entries in @p ids.
 * @param arg   User-supplied string (1-based index or email address).
 * @return 0-based index into @p ids, or -1 if not found.
 */
static int find_by_arg(const Identity *ids, int count, const char *arg) {
    char *end;
    errno = 0;
    long idx = strtol(arg, &end, 10);
    if (errno != ERANGE && *end == 0 && idx >= 1 && idx <= (long)count) {
        return (int)(idx - 1);
    }
    for (int ii = 0; ii < count; ii++) {
        if (strcmp(ids[ii].email, arg) == 0)
            return ii;
    }
    return -1;
}

/**
 * @brief Print the email address currently configured in git (no-arg default).
 */
static void cmd_show_current(void) {
    char email[MAX_EMAIL] = {0};
    if (git_config_get("user.email", email, sizeof(email)) == 0) {
        printf("%s\n", email);
    }
}

/**
 * @brief Print all stored identities, one per line, with 1-based indices.
 * @param ids   Array of identities.
 * @param count Number of entries in @p ids.
 */
static void cmd_list(const Identity *ids, int count) {
    for (int ii = 0; ii < count; ii++) {
        printf("| %d | %s | %s | %s |\n",
               ii + 1, ids[ii].name, ids[ii].email, ids[ii].signing_key);
    }
}

/**
 * @brief Interactively create or update an identity and apply it.
 *
 * Prompts the user for name, email, and an optional signing key.  If an
 * identity with the same email already exists it is overwritten; otherwise a
 * new entry is appended.  The updated list is saved and the identity is
 * applied to the current repository.
 *
 * @param ids   Identity array (may be modified).
 * @param count Current identity count; incremented when a new entry is added.
 * @return 1 on success, 0 on validation failure or I/O error.
 */
static int cmd_create(Identity *ids, int *count) {
    Identity newid = {0};

    printf("Insert display name\n> ");
    fflush(stdout);
    if (!fgets(newid.name, sizeof(newid.name), stdin))
        return 0;
    size_t len = strlen(newid.name);
    if (len > 0 && newid.name[len - 1] == '\n')
        newid.name[--len] = 0;
    sanitize(newid.name);
    if (strlen(newid.name) == 0) {
        fprintf(stderr, "Name cannot be empty\n");
        return 0;
    }

    printf("Insert email\n> ");
    fflush(stdout);
    if (!fgets(newid.email, sizeof(newid.email), stdin))
        return 0;
    len = strlen(newid.email);
    if (len > 0 && newid.email[len - 1] == '\n')
        newid.email[--len] = 0;
    sanitize(newid.email);
    if (strlen(newid.email) == 0) {
        fprintf(stderr, "Email cannot be empty\n");
        return 0;
    }

    printf("Would you like to configure a signing key? y(es)/n(o)\n> ");
    fflush(stdout);
    char ans[8] = {0};
    int want_key = 0;
    if (fgets(ans, sizeof(ans), stdin)) {
        want_key = (ans[0] == 'y' || ans[0] == 'Y');
        if (!strchr(ans, '\n')) {
            int c;
            // drains and discards
            while ((c = getchar()) != '\n' && c != EOF)
                ;
        }
    }
    if (want_key) {
        printf("Enter signing key (SSH public key path)\n> ");
        fflush(stdout);
        if (!fgets(newid.signing_key, sizeof(newid.signing_key), stdin))
            return 0;
        len = strlen(newid.signing_key);
        if (len > 0 && newid.signing_key[len - 1] == '\n')
            newid.signing_key[--len] = 0;
        sanitize(newid.signing_key);
        if (strlen(newid.signing_key) == 0) {
            fprintf(stderr, "Signing key cannot be empty\n");
            return 0;
        }
    }

    int existing = -1;
    for (int ii = 0; ii < *count; ii++) {
        if (strcmp(ids[ii].email, newid.email) == 0) {
            existing = ii;
            break;
        }
    }
    if (existing >= 0) {
        ids[existing] = newid;
    } else {
        if (*count >= MAX_IDENTITIES) {
            fprintf(stderr, "Identity limit reached\n");
            return 0;
        }
        ids[(*count)++] = newid;
    }

    if (save_identities(ids, *count) != 0)
        return 0;
    if (apply_identity(&newid) != 0)
        return 0;
    return 1;
}

/**
 * @brief Switch the current repository to a stored identity.
 * @param ids   Array of loaded identities.
 * @param count Number of entries in @p ids.
 * @param arg   1-based index or email address identifying the target identity.
 * @return 1 on success, 0 if the identity was not found or could not be applied.
 */
static int cmd_switch(const Identity *ids, int count, const char *arg) {
    int idx = find_by_arg(ids, count, arg);
    if (idx < 0)
        return 0;
    return apply_identity(&ids[idx]) == 0 ? 1 : 0;
}

/**
 * @brief Remove a stored identity and persist the updated list.
 * @param ids   Identity array (modified in place).
 * @param count Current identity count; decremented on success.
 * @param arg   1-based index or email address identifying the entry to remove.
 * @return 1 on success, 0 if the identity was not found or the save failed.
 */
static int cmd_delete(Identity *ids, int *count, const char *arg) {
    int idx = find_by_arg(ids, *count, arg);
    if (idx < 0)
        return 0;
    for (int ii = idx; ii < *count - 1; ii++)
        ids[ii] = ids[ii + 1];
    (*count)--;
    return save_identities(ids, *count) == 0 ? 1 : 0;
}

/**
 * @brief print command result. If @p res is > 0 it's considered ok
 * @param res
 */
static void print_cmd_res(int res) {
    if (res > 0) {
        printf("OK\n");
        return;
    }
    printf("FAIL\n");
}

/**
 * @brief print help
 */
static void print_help(void) {
    printf("git-whoami v%s\n", VERSION);
    printf("\nManage multiple git identities (user.name + user.email) and switch between them.\n");
    printf("\nUSAGE\n");
    printf("  git-whoami [command] [argument]\n");
    printf("\nCOMMANDS\n");
    printf("  (none)               Show the active git identity for the current repository\n");
    printf("  list,    ls          List all saved identities with their index\n");
    printf("  create,  c           Interactively create and save a new identity\n");
    printf("  switch,  s <id>      Switch the current repository to the identity at <index> or <email>\n");
    printf("  delete,  d <id>      Delete the identity at <index> or <email>\n");
    printf("  help,    h           Show this help message\n");
    printf("\nARGUMENTS\n");
    printf("  <index>              1-based position shown by the list command\n");
    printf("  <email>              Email address of a saved identity\n");
    printf("\nEXAMPLES\n");
    printf("  git-whoami                  # show current identity\n");
    printf("  git-whoami list             # list all identities\n");
    printf("  git-whoami create           # add a new identity\n");
    printf("  git-whoami switch 2         # switch to identity #2\n");
    printf("  git-whoami switch a@b.com   # switch by email\n");
    printf("  git-whoami delete 1         # delete identity #1\n");
    printf("  git-whoami delete a@b.com   # delete by email\n");
}

/**
 * @brief Dispatch a subcommand based on the command-line arguments.
 * @param argc  Argument count as received by main().
 * @param argv  Argument vector as received by main().
 * @param ids   Pre-loaded identity array.
 * @param count Number of loaded identities.
 * @return 0 on success, 1 on usage error or subcommand failure.
 */
static int run(int argc, char *argv[], Identity *ids, int *count) {
    if (argc == 1) {
        cmd_show_current();
        return EXIT_SUCCESS;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
        print_help();
        return EXIT_SUCCESS;
    }

    if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "list") == 0) {
        cmd_list(ids, *count);
        return EXIT_SUCCESS;
    }

    if (strcmp(cmd, "c") == 0 || strcmp(cmd, "create") == 0) {
        int res = cmd_create(ids, count);
        print_cmd_res(res);
        return res ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (strcmp(cmd, "s") == 0 || strcmp(cmd, "switch") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: git-whoami switch <index|email>\n");
            return 1;
        }
        int res = cmd_switch(ids, *count, argv[2]);
        print_cmd_res(res);
        return res ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (strcmp(cmd, "d") == 0 || strcmp(cmd, "delete") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: git-whoami delete <index|email>\n");
            return 1;
        }
        int res = cmd_delete(ids, count, argv[2]);
        print_cmd_res(res);
        return res ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    fprintf(stderr, "git-whoami v%s\n", VERSION);
    fprintf(stderr, "Usage: git-whoami help\n");
    return EXIT_FAILURE;
}

/**
 * @brief Program entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 on error.
 */
int main(int argc, char *argv[]) {
    Identity *ids = calloc(MAX_IDENTITIES, sizeof(Identity));
    if (!ids) {
        fprintf(stderr, "out of memory\n");
        return EXIT_FAILURE;
    }
    int count = 0;
    load_identities(ids, &count);
    int exit_value = run(argc, argv, ids, &count);
    free(ids);
    return exit_value;
}
