# tutors

Interactive terminal cheatsheets for tools I use regularly.

## Included tutors

- `bututor` — GitButler commands and workflows;
- `gitutor` — Git commands and workflows;
- `nvimtutor` — Neovim motions, commands, plugins, and workflows;
- `zshtutor` — Zsh syntax and shell usage.

Each tutor is a small standalone C program with a keyboard-driven terminal UI.

## Build

Requirements: a C compiler (`clang` by default) and `make`.

```bash
make build
```

Build only one tutor:

```bash
make build gitutor
```

## Install

The default prefix is `/usr/local`:

```bash
sudo make install
```

To install into a user-owned prefix:

```bash
make install PREFIX="$HOME/.local"
```

Other useful commands:

```bash
make clean
make help
```

## License

MIT
