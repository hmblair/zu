# Remote GPU Execution with rpy

When you need to run code that requires a GPU (training models, large batch inference, CUDA operations), use `rpy` to execute on the remote GPU cluster. The local machine has no GPU.

## When to Use rpy

- **Training neural networks** - Any PyTorch/TensorFlow training loop
- **GPU-accelerated inference** - Batch predictions on large datasets
- **CUDA operations** - Anything requiring `torch.cuda` or GPU tensors
- **Memory-intensive operations** - When local RAM is insufficient

## Basic Workflow

```bash
# Check available GPUs first
rpy gpu --gpus

# Run a script and stream output back
rpy gpu train.py --epochs 10

# For long-running jobs, detach to survive disconnection
rpy gpu -d train.py --epochs 100
# Returns: job ID like 20251229-161516

# Monitor detached jobs
rpy gpu --status 20251229-161516  # exit 0=running, 1=done
rpy gpu --log 20251229-161516     # view output
rpy gpu --jobs                     # list all jobs
rpy gpu --kill 20251229-161516    # stop if needed
```

## SLURM Clusters (HPC)

For SLURM-managed clusters like Sherlock, use the `--slurm` flag or configure an alias with SLURM options:

```bash
# Example alias in ~/.config/rpy:
# sherlock = hmblair@login.sherlock.stanford.edu --slurm -m python/3.12 --partition gpu --gres gpu:1 --time 2:00:00

# Check GPU partition availability
rpy sherlock --gpus

# Run with additional modules (combines with alias modules)
rpy sherlock -m cuda/12.0 train.py

# Detached jobs use sbatch
rpy sherlock -d train.py
# Returns: job ID and SLURM job ID

# Job management uses SLURM commands (squeue, scancel)
rpy sherlock --jobs    # squeue -u $USER | grep rpy
rpy sherlock --status 20251229-161516
rpy sherlock --kill 20251229-161516
```

## Project Sync

Sync local Python projects to the remote server before running:

```bash
# Sync current directory to remote (mirrors path structure)
rpy gpu --sync
# /Users/hmblair/projects/ml → /home/hmblair/projects/ml

# Sync and skip pip install
rpy gpu --sync --no-install

# Sync specific project
rpy gpu --sync /path/to/project
```

**Auto-install:** If `pyproject.toml` or `setup.py` exists, runs `pip install -e .` after sync.

**Excludes:** `__pycache__`, `.git`, `*.egg-info`, `build/`, `dist/`, `.venv`, `.pytest_cache`

### Agent Workflow with Sync

When working on a project that needs remote execution:

1. `rpy gpu --sync` - sync project to remote (do this when files change)
2. `rpy gpu script.py` - run script (imports work because package is installed)
3. If you modify local files, sync again before running

## Agent Workflow for Long Jobs

1. `rpy gpu --gpus` - verify GPUs are available
2. `rpy gpu --sync` - ensure latest code is on remote
3. `rpy gpu -d script.py` - launch detached, note the job ID
4. Poll `rpy gpu --status <job>` periodically (exit code 0 = still running)
5. When done (exit code 1), fetch results with `rpy gpu --log <job>`

## Notes

- The `gpu` alias is configured in `~/.config/rpy` with the correct host and Python venv
- Scripts are copied to the remote, executed, and output streams back
- For piped code: `echo 'print(torch.cuda.device_count())' | rpy gpu`
- SLURM mode uses `srun` for foreground and `sbatch` for detached jobs
