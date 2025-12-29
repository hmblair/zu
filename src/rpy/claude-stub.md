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

## Agent Workflow for Long Jobs

1. `rpy gpu --gpus` - verify GPUs are available
2. `rpy gpu -d script.py` - launch detached, note the job ID
3. Poll `rpy gpu --status <job>` periodically (exit code 0 = still running)
4. When done (exit code 1), fetch results with `rpy gpu --log <job>`

## Notes

- The `gpu` alias is configured in `~/.config/rpy` with the correct host and Python venv
- Scripts are copied to the remote, executed, and output streams back
- For piped code: `echo 'print(torch.cuda.device_count())' | rpy gpu`
