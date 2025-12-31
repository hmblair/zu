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

# Run a script on login node (streams output)
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
# Foreground (streams output, runs on login node)
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

For SLURM-managed clusters, use `-s/--slurm` to run on compute nodes:

```bash
# Run on login node (default)
rex sherlock train.py
rex sherlock --exec "ls /scratch"

# Run via SLURM (-s flag)
rex sherlock -s train.py              # srun (foreground)
rex sherlock -s -d train.py           # sbatch (detached)
rex sherlock -s --exec "nvidia-smi"   # shell command via srun

# SLURM options
rex sherlock -s --partition gpu --gres gpu:1 --time 2:00:00 train.py

# Check GPU partition availability
rex sherlock --gpus
```

### Project Config (.rex.toml)

For HPC workflows, create `.rex.toml` in your project root:

```toml
host = "hmblair@login.sherlock.stanford.edu"
code_dir = "/home/groups/rhiju/hmblair/myproject"
run_dir = "/scratch/users/hmblair/myproject"

modules = ["system", "git/2.45.1", "python/3.12", "cuda/12.0"]

[slurm]
build_partition = "biochem"
run_partition = "gpu"
gres = "gpu:1"
time = "2:00:00"
```

With project config:

```bash
# From project directory - host is inferred from .rex.toml
rex --sync                    # sync to code_dir
rex --build                   # update venv (incremental, keeps .venv)
rex --build --clean           # full rebuild (deletes .venv first)
rex --build --wait            # wait for build to complete
rex -s train.py               # run with SLURM defaults from config
rex --exec "which python3"    # loads modules from config
```

**Build notes:**
- `--build` is incremental by default - only runs `pip install -e .` if `.venv` exists
- Uses `--only-binary :all:` to avoid compiling packages from source
- Runs on `build_partition` via sbatch (30 min time limit)

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
- All commands run on login node by default; use `-s` for SLURM compute nodes
- SLURM mode uses `srun` for foreground and `sbatch` for detached jobs
- `--exec` loads modules from `.rex.toml` or `-m` flags
