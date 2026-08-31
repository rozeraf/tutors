# tutors

Interactive terminal cheatsheets for tools I use regularly.

## Included tutors

- `gitutor` — Git commands and workflows;
- `nvimtutor` — Neovim motions, commands, plugins, and workflows;
- `zshtutor` — Zsh syntax and shell usage.

Each tutor is a small standalone binary with a keyboard-driven terminal UI.
They share the terminal engine in `core/`; tutor-specific reference material lives
in each tutor's `content.c`.

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

GNU General Public License v3.0
