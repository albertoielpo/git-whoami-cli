# Git whoami cli

A CLI tool to manage and switch between multiple Git identities.

This project is the CLI companion to the [git-whoami](https://github.com/albertoielpo/git-whoami) VS Code extension. As I moved away from VS Code, I created this CLI to provide the same identity management features in a standalone, editor-agnostic form.

## How it works

Under the hood, it reads and writes the following Git config values:

```bash
git config user.name
git config user.email
git config user.signingKey
```

## Data storage

Identities are stored persistently at `~/.config/git-whoami/data`. The email address is used as the unique key — saving a profile with an existing email will overwrite it.

```md
| index | name          | email                    | signingkey        |
|-------|---------------|--------------------------|-------------------|
| 1     | Alberto Ielpo | onemail@fake.mail        | ~/folder/keyfile  |
| 2     | Alberto Ielpo | anothermail@fake.mail    |                   |
```

## Commands

### Create

Interactively create a new identity and apply it to the current repository.

```bash
git-whoami create
Insert display name
> Alberto Ielpo
Insert email
> onemail@fake.mail
Would you like to configure a signing key? y(es)/n(o)
> y
Select private key
> ~/folder_name/file
```

Returns `1` on success, `0` on failure.

### List

```bash
# Show the current identity
git-whoami
onemail@fake.mail

# List all saved identities
git-whoami ls
1 Alberto Ielpo onemail@fake.mail ~/folder/keyfile
2 Alberto Ielpo anothermail@fake.mail 
```

### Switch

Apply a saved identity to the current repository using an index or email address.

```bash
git-whoami switch 2
1

git-whoami switch anothermail@fake.mail
1
```

Returns `1` on success, `0` on failure.

### Delete

Remove an identity from the config file. Does not affect the local Git config.

```bash
git-whoami delete 1
1

git-whoami delete anothermail@fake.mail
1

git-whoami delete fake@mail.com
0
```

Returns `1` on success, `0` if the identity was not found.

## Requirements

- gcc

## Build

```bash
gcc -Wall -Wextra -Wpedantic -O2 -g -std=c99 -o git-whoami git-whoami.c
```

## Development

### Code style

```c
// Struct types: PascalCase
typedef struct {
    int x;
    int y;
} Point;

// Simple type aliases: lowercase_t
typedef uint32_t port_t;
```

### Formatting

This project uses `clang-format`. To format all source files:

```sh
find . -name "*.c" -o -name "*.h" | xargs clang-format -i
```

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.
