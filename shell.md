# QNX Nix Shell Isolation Approach

## Current Approach: Shell Wrapper Blocking Absolute Paths

For isolated shells, we use a shell wrapper that:
- Sets PATH to only the profile's bin directory
- Attempts to block execution of absolute paths outside the profile's bin
- If an absolute path is executed, the shell prints a warning and immediately kills itself (strict isolation)

Example (Bash preexec trap):

```bash
preexec() {
    case "$BASH_COMMAND" in
        $PATH/*) ;;
        /*) echo "Absolute path execution is not allowed in isolated shell, killing shell."; kill -KILL $$ ;;
    esac
}
trap preexec DEBUG
```

## Bash Trap Limitation

- The Bash `DEBUG` trap cannot truly prevent command execution; it only runs before the command.
- To enforce strict isolation, we kill the shell if an absolute path is attempted.
- This is disruptive for users, but is the only reliable enforcement in pure Bash without chroot or a patched shell.

## Why not chroot?

For simplicity and compatibility, we use a shell wrapper instead of chroot. This avoids issues with wrapper scripts as /bin/sh and dynamic linker/library resolution inside a chroot.

**If chroot is used in the future:**
- You must provide a statically linked shell binary (not a wrapper script) as /bin/sh inside the chroot.
- This is because wrapper scripts require a real shell interpreter, which cannot be a script itself in a chrooted environment.
- Statically linked shells avoid dependency and interpreter issues, ensuring true isolation.

---

