# blk - Website Blocker for macOS

A command-line utility that blocks websites for a fixed period of time. Once blocked, sites **cannot be unblocked** until the timer expires.

## Features

- **Enforced blocks**: No way to remove a block early - you must wait for it to expire
- **No shortening**: Existing blocks cannot be overwritten with shorter durations
- **Daily schedules**: Set recurring blocks (e.g., block social media during work hours)
- **Hidden blocks**: Blocks can be hidden from the default list view
- **1Password integration**: Unlock with Touch ID via 1Password CLI (optional)
- **Private**: Block list is AES-256 encrypted, not stored in /etc/hosts or other visible locations
- **Persistent**: Blocks survive reboots, process kills, and system restarts
- **Tamper-resistant**: Daemon re-applies rules every 5 seconds if they're cleared

## Requirements

- macOS (uses pf firewall)
- zsh
- python3 (for JSON handling)
- openssl (for encryption)
- 1Password CLI (`op`) - optional, for Touch ID unlock

## Installation

1. Clone or copy the `blk` directory to your preferred location
2. Run setup:

```bash
/path/to/blk setup
```

This will:
- Prompt you to create an encryption password
- Install a LaunchDaemon that runs at boot
- Require sudo for the daemon installation

3. (Optional) Add to your PATH for easier access:

```bash
# Add to ~/.zshrc
export PATH="$PATH:/path/to/blk"
```

## 1Password Integration (Optional)

If you have the [1Password CLI](https://developer.1password.com/docs/cli/) installed, you can unlock blk with Touch ID instead of typing your password.

### During setup

If `op` is detected during `blk setup`, you'll be asked if you want to store your password in 1Password. If you say yes, a new item named "blk" will be created with your password.

### After setup

If you didn't enable 1Password during setup, you can manually add your password:

```bash
op item create --category=password --title="blk" "password=YOUR_PASSWORD"
```

### How it works

When you run any blk command that needs your password:
1. blk checks if a "blk" item exists in 1Password
2. If found, it retrieves the password (triggering Touch ID)
3. If 1Password fails (not signed in, Touch ID cancelled, etc.), it falls back to manual password entry

## Usage

### Block a website

```bash
blk add [--hidden] <domain> <duration>
```

**Options:**
- `--hidden` - Hide this block from `blk list` (use `blk list --show-hidden` to see it)

**Duration formats:**
- `30s` - 30 seconds
- `30m` - 30 minutes
- `2h` - 2 hours
- `1d` - 1 day
- `1w` - 1 week

**Examples:**
```bash
blk add twitter.com 2h         # Block Twitter for 2 hours
blk add reddit.com 1d          # Block Reddit for 1 day
blk add youtube.com 30m        # Block YouTube for 30 minutes
blk add --hidden reddit.com 4h # Block Reddit, hidden from list
```

**Note:** If you try to add a block with a shorter duration than an existing block, it will be rejected. You can only extend blocks, not shorten them.

The domain is automatically normalized:
- `https://www.reddit.com/r/foo` becomes `reddit.com`
- Case is ignored: `Reddit.com` becomes `reddit.com`

### Wildcard patterns

Use `*` to match multiple domains at once:

```bash
blk add '*reddit*' 2h       # Blocks reddit.com, old.reddit.com, reddit.net, etc.
blk add '*.twitter.com' 1d  # Blocks all Twitter subdomains
blk add 'facebook.*' 4h     # Blocks facebook with any TLD (.com, .net, etc.)
```

**Note:** Quote patterns containing `*` to prevent shell expansion.

**How wildcards work:**
- `*reddit*` - Tries common TLDs (com, net, org, io, co) and subdomains (www, old, m, api, etc.)
- `*.example.com` - Tries common subdomains of example.com
- `example.*` - Tries example with common TLDs

### Daily schedules

Set up recurring blocks that activate at the same time every day:

```bash
blk daily [--hidden] <domain> <start>-<end>
```

**Options:**
- `--hidden` - Hide this schedule from `blk list` (use `blk list --show-hidden` to see it)

**Examples:**
```bash
blk daily '*reddit*' 09:00-17:00       # Block Reddit 9am-5pm daily
blk daily twitter.com 22:00-06:00      # Block Twitter 10pm-6am (crosses midnight)
blk daily '*.youtube.com' 08:00-18:00  # Block YouTube during work hours
blk daily --hidden reddit.com 09:00-17:00  # Hidden daily schedule
```

**Removing daily schedules:**

Daily schedules can only be removed when they are **not currently active**:

```bash
blk remove reddit.com    # Only works outside the scheduled hours
```

If you try to remove during active hours, you'll get an error.

### List active blocks

```bash
blk list [--show-hidden]
```

Shows all currently blocked domains with time remaining.

**Options:**
- `--show-hidden` - Also show blocks that were added with `--hidden`

### Check a specific domain

```bash
blk status reddit.com
```

### Uninstall

```bash
blk uninstall
```

**Note:** Uninstall only works when there are no active blocks. You must wait for all blocks to expire first.

## How It Works

1. **CLI** (`blk`): User-facing script that encrypts/decrypts the block list and communicates with the daemon
2. **Daemon** (`blkd`): Runs as root via launchd, manages pf firewall rules
3. **pf firewall**: macOS's built-in packet filter blocks traffic to blocked domains' IP addresses

### Data Storage

```
~/.blk/
├── data.enc    # AES-256 encrypted JSON block list
├── salt        # Random salt for encryption
└── trigger     # Touched to signal daemon to reload

/var/root/.blk_pass           # Daemon's copy of password (root-only)
/Library/LaunchDaemons/com.zu.blkd.plist  # Daemon config
/var/log/blkd.log             # Daemon logs
```

### Security Model

- Block list is encrypted with your password using AES-256-CBC with PBKDF2 key derivation
- Password is required for all operations (add, list, status)
- Daemon runs as root to modify firewall rules
- Rules are re-applied every 5 seconds if tampered with
- Timed blocks cannot be removed until expiry
- Daily schedules can only be modified outside active hours

**What this cannot prevent:**
- Booting into recovery mode
- Completely uninstalling the daemon (only when no active blocks)
- Using a different network interface or VPN (blocks are per-interface)

## Troubleshooting

### Check daemon status

```bash
sudo launchctl list | grep blkd
```

### View daemon logs

```bash
tail -f /var/log/blkd.log
```

### Reload daemon after code changes

```bash
sudo launchctl unload /Library/LaunchDaemons/com.zu.blkd.plist
sudo launchctl load /Library/LaunchDaemons/com.zu.blkd.plist
```

### Check pf rules

```bash
sudo pfctl -a com.zu.blk -s rules
```

### Site not being blocked?

1. Check the daemon is running: `sudo launchctl list | grep blkd`
2. Check the logs: `tail /var/log/blkd.log`
3. Verify rules exist: `sudo pfctl -a com.zu.blk -s rules`
4. The site may use multiple IPs or CDNs - the daemon resolves IPs periodically

### "Incorrect password" error

The password you enter must match the one used during `blk setup`. If you've forgotten it, you'll need to:

1. Wait for all blocks to expire
2. Run `blk uninstall`
3. Run `blk setup` again with a new password

## Files

| File | Purpose |
|------|---------|
| `blk` | Main CLI script |
| `blkd` | Background daemon |
| `test.sh` | Basic validation tests |
| `README.md` | This documentation |
