# zu

A collection of zsh utilities for macOS.

## Installation

```bash
git clone https://github.com/hmblair/zu.git ~/.local/share/zu
~/.local/share/zu/configure
```

Add to your PATH:
```bash
export PATH="$HOME/.local/share/zu/bin:$PATH"
```

## Commands

### Tools

| Command | Description |
|---------|-------------|
| bkup | Borg backup CLI wrapper for encrypted remote backups |
| blk | Website blocker using pf firewall |
| buzz | Keep Mac awake using caffeinate |
| cpnotify | Clipboard notification daemon (plays sound on copy) |
| envs | Persistent environment variable manager |
| findrm | Recursively find and remove files/directories by pattern |
| free | Disk space analyzer with caching daemon |
| homeserver | Docker Compose service manager |
| llm | Local LLM wrapper using llama-cli |
| mediasort | Audiobook organizer into Author/Book structure |
| monitor | Remote server resource monitor via SSH |
| path | PATH environment management |
| photosort | Photo organizer by EXIF date |
| png | Convert images and PDFs to PNG format |

### Utilities

| Command | Description |
|---------|-------------|
| byebye | Clean up Trash, Downloads, and scratch directories |
| c | Clear terminal screen |
| cl | Clear screen and list directory |
| give | Cross-platform clipboard paste |
| len | String/line length calculator |
| mine | Recursively claim file ownership |
| renumber | Batch rename files sequentially |
| substr | Extract substring from string |
| take | Cross-platform clipboard copy |
| whereami | Output user@host:path for SSH/rsync |

## License

MIT
