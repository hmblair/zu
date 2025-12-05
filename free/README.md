# free - Disk Space & Large File Finder for macOS

A command-line utility that shows free disk space and lists the largest files in your home directory.

## Features

- **Instant results**: File list is cached and updated hourly in the background
- **Simple output**: Shows free space and top N largest files
- **Background daemon**: LaunchAgent updates cache automatically
- **Manual refresh**: Update cache on-demand when needed

## Requirements

- macOS
- zsh

## Installation

1. Clone or copy the `free` directory to your preferred location
2. (Optional) Install the background daemon:

```bash
/path/to/free setup
```

This will:
- Create a LaunchAgent that updates the cache every hour
- The daemon runs as your user (no sudo required)

3. (Optional) Add to your PATH for easier access:

```bash
# Add to ~/.zshrc
export PATH="$PATH:/path/to/free"
```

## Usage

### Show free disk space

```bash
free
```

Output:
```
61G of 995G free
```

### Show largest files

```bash
free -n 10
```

Output:
```
61G of 995G free

Top 10 largest files (cached 2h ago)
23G      /Users/you/data/large-file.csv
16G      /Users/you/downloads/backup.zip
...
```

**Note:** Requires cache to exist. Run `free setup` or `free update` first.

### Build/refresh cache manually

```bash
free update
```

Scans your home directory for the 100 largest files (>1MB) and updates the cache.

### Install background daemon

```bash
free setup
```

Installs a LaunchAgent that refreshes the cache every hour.

### Reload daemon

```bash
free reload
```

Reloads the LaunchAgent (useful after code changes).

### Uninstall daemon

```bash
free uninstall
```

Removes the LaunchAgent. You can still use `free update` manually.

## How It Works

1. **CLI** (`free`): User-facing script that displays disk space and reads from cache
2. **Daemon** (`freed`): Runs hourly via launchd, scans home directory for large files
3. **Cache**: Stores the top 100 largest files for instant retrieval

### Data Storage

```
~/.config/free/
├── cache         # List of largest files (size + path)
└── status.json   # Cache metadata (last update timestamp)

~/Library/LaunchAgents/com.zu.freed.plist  # Daemon config
/tmp/freed.log                              # Daemon logs
```

## Troubleshooting

### Check daemon status

```bash
launchctl list | grep freed
```

### View daemon logs

```bash
cat /tmp/freed.log
```

### Cache shows old data

Run `free update` to refresh immediately, or check if the daemon is running.

### No cache found

Run `free setup` to install the daemon, or `free update` for a one-time scan.

## Files

| File | Purpose |
|------|---------|
| `free` | Main CLI script |
| `freed` | Background cache daemon |
| `README.md` | This documentation |
