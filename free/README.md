# free - Disk Space & Large File Finder for macOS

A command-line utility that shows free disk space and lists the largest files in your home directory.

## Features

- **Instant results**: File list is cached and updated hourly in the background
- **Simple output**: Shows free space and top N largest files
- **Background daemon**: LaunchAgent updates cache automatically
- **Manual refresh**: Update cache on-demand when needed
- **Configurable**: Set how many files to cache during setup

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
- Prompt for cache size (how many files to track, default: 100)
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

Scans your home directory for the largest files (>1MB) and updates the cache. Uses the cache size from config (default: 100).

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
3. **Cache**: Stores the largest files (configurable count) for instant retrieval

### Data Storage

```
~/.config/free/
├── config.json   # Configuration (cache_count)
├── cache         # List of largest files (size + path)
└── status.json   # Cache metadata (last update timestamp)

~/Library/LaunchAgents/com.zu.freed.plist  # Daemon config
/tmp/freed.log                              # Daemon logs
```

### Configuration

`~/.config/free/config.json`:
```json
{"cache_count": 100}
```

- `cache_count`: Number of largest files to cache (default: 100)

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
