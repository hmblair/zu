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
| envs | Persistent environment variable manager |
| findrm | Recursively find and remove files/directories by pattern |
| free | Disk space analyzer with caching daemon |
| homeserver | Docker Compose service manager |
| l | Enhanced directory listing with icons and tree view |
| monitor | Remote server resource monitor via SSH |
| path | PATH environment management |
| photosort | Photo organizer by EXIF date |

### Utilities

| Command | Description |
|---------|-------------|
| byebye | Clean up Trash, Downloads, and scratch directories |
| c | Clear terminal screen |
| cl | Clear screen and list directory |
| copy | Cross-platform clipboard copy |
| len | String/line length calculator |
| mine | Recursively claim file ownership |
| renumber | Batch rename files sequentially |
| substr | Extract substring from string |
| whereami | Output user@host:path for SSH/rsync |

## License

MIT
