# Remote Execution with rex

When you need to run code that requires a GPU (training models, large batch inference, CUDA operations), use `rex` to execute on the remote GPU cluster. The local machine has no GPU.

## When to Use rex

- **Training neural networks** - Any PyTorch/TensorFlow training loop
- **GPU-accelerated inference** - Batch predictions on large datasets
- **CUDA operations** - Anything requiring `torch.cuda` or GPU tensors
- **Memory-intensive operations** - When local RAM is insufficient
- **Remote shell commands** - Any command needing remote execution

## Basic Workflow

```bash
# Check available GPUs first
rex gpu --gpus

# Run a script and stream output back
rex gpu train.py --epochs 10

# For long-running jobs, detach to survive disconnection
rex gpu -d train.py --epochs 100
# Returns: job ID like 20251229-161516

# Monitor detached jobs
rex gpu --status 20251229-161516  # exit 0=running, 1=done
rex gpu --log 20251229-161516     # view output
rex gpu --jobs                     # list all jobs
rex gpu --kill 20251229-161516    # stop if needed

# Wait for job to complete
rex gpu --watch 20251229-161516   # blocks until done
rex gpu --watch --last            # watch most recent job
```

## File Transfer

```bash
# Push files to remote
rex gpu --push ./data ~/data              # upload directory
rex gpu --push model.pt ~/models/model.pt # upload file

# Pull files from remote
rex gpu --pull ~/checkpoints ./checkpoints  # download directory
rex gpu --pull ~/output.log .               # download file
rex gpu --pull "~/logs/*.log" ./logs/       # glob pattern (quoted)
```

## Shell Commands

Execute arbitrary shell commands on the remote server:

```bash
# Foreground (streams output)
rex gpu --exec "ls -la ~/checkpoints"
rex gpu --exec "nvidia-smi"
rex gpu --exec "du -sh ~/data/*"

# Detached (for long-running commands)
rex gpu -d --exec "wget https://example.com/large_file.tar.gz"
rex gpu -d -n download --exec "curl -O https://..."
```

## Project Sync

Sync local Python projects to the remote server before running:

```bash
# Sync current directory to remote (mirrors path structure)
rex gpu --sync
# /Users/hmblair/projects/ml -> /home/hmblair/projects/ml

# Sync and skip pip install
rex gpu --sync --no-install

# Sync specific project
rex gpu --sync /path/to/project
```

**Auto-install:** If `pyproject.toml` or `setup.py` exists, runs `pip install -e .` after sync.

## SLURM Clusters (HPC)

For SLURM-managed clusters like Sherlock:

```bash
# Example alias in ~/.config/rex:
# sherlock = hmblair@login.sherlock.stanford.edu --slurm -m python/3.12 --partition gpu --gres gpu:1 --time 2:00:00

# Check GPU partition availability
rex sherlock --gpus

# Run with additional modules
rex sherlock -m cuda/12.0 train.py

# Detached jobs use sbatch
rex sherlock -d train.py

# Shell commands via SLURM
rex sherlock --exec "nvidia-smi"
rex sherlock -d --exec "wget https://..."
```

## Agent Workflow for Long Jobs

1. `rex gpu --gpus` - verify GPUs are available
2. `rex gpu --sync` - ensure latest code is on remote
3. `rex gpu -d script.py` - launch detached, note the job ID
4. `rex gpu --watch <job>` - wait for completion (or poll --status periodically)
5. When done, fetch results: `rex gpu --pull ~/output ./output`

## JSON Output for Automation

```bash
rex gpu --jobs --json           # list jobs as JSON array
rex gpu --status <job> --json   # {"job": "...", "status": "running|done", "pid": ...}
rex gpu --watch <job> --json    # {"job": "...", "status": "success|done", "exit_code": 0}
rex gpu --gpus --json           # GPU info as JSON array
```

## Notes

- The `gpu` alias is configured in `~/.config/rex` with the correct host and Python venv
- Scripts are copied to the remote, executed, and output streams back
- For piped code: `echo 'print(torch.cuda.device_count())' | rex gpu`
- SLURM mode uses `srun` for foreground and `sbatch` for detached jobs
