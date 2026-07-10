#!/usr/bin/env python3
import argparse
import time
import sys
import os
import glob

import psutil

# ---------------------------------------------------------------------------
# GPU backend detection
# ---------------------------------------------------------------------------

_TEGRA_BASE = "/sys/devices/gpu.0"
_TEGRA_LOAD = f"{_TEGRA_BASE}/load"
_TEGRA_FREQ = f"{_TEGRA_BASE}/devfreq/17000000.gv11b/cur_freq"
_TEGRA_MAX_FREQ = f"{_TEGRA_BASE}/devfreq/17000000.gv11b/max_freq"
_TEGRA_AVAIL_FREQ = f"{_TEGRA_BASE}/devfreq/17000000.gv11b/available_frequencies"


def _find_tegra_temp_path():
    for zone in glob.glob("/sys/devices/virtual/thermal/thermal_zone*/type"):
        try:
            with open(zone) as f:
                if "GPU-therm" in f.read():
                    return zone.replace("type", "temp")
        except OSError:
            pass
    return None


def _sysfs_int(path):
    with open(path) as f:
        return int(f.read().strip())


def _init_gpu():
    # Discrete NVIDIA via pynvml
    try:
        import pynvml
        pynvml.nvmlInit()
        handle = pynvml.nvmlDeviceGetHandleByIndex(0)
        name = pynvml.nvmlDeviceGetName(handle)
        if isinstance(name, bytes):
            name = name.decode()
        return "nvml", handle, name
    except Exception:
        pass

    # Tegra / Jetson integrated GPU
    if os.path.exists(_TEGRA_LOAD):
        try:
            _sysfs_int(_TEGRA_LOAD)
            temp_path = _find_tegra_temp_path()
            max_freq_hz = _sysfs_int(_TEGRA_MAX_FREQ) if os.path.exists(_TEGRA_MAX_FREQ) else None
            avail = []
            if os.path.exists(_TEGRA_AVAIL_FREQ):
                with open(_TEGRA_AVAIL_FREQ) as f:
                    avail = [int(x) for x in f.read().split()]
            return "tegra", {"temp": temp_path, "max_freq": max_freq_hz, "avail_freq": avail}, "Tegra GPU (Jetson)"
        except OSError:
            pass

    return None, None, None


_GPU_BACKEND, _GPU_RESOURCE, _GPU_NAME = _init_gpu()


# ---------------------------------------------------------------------------
# Sampling
# ---------------------------------------------------------------------------

def sample_cpu_per_core():
    return psutil.cpu_percent(percpu=True, interval=None)


def sample_top_processes():
    rows = {}
    for p in psutil.process_iter(['pid', 'name', 'cpu_percent', 'memory_info']):
        try:
            rss = p.info['memory_info'].rss if p.info['memory_info'] else 0
            rows[p.pid] = (p.info['name'], p.info['cpu_percent'], rss)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass
    return rows


def sample_gpu():
    if _GPU_BACKEND == "nvml":
        import pynvml
        util = pynvml.nvmlDeviceGetUtilizationRates(_GPU_RESOURCE)
        mem = pynvml.nvmlDeviceGetMemoryInfo(_GPU_RESOURCE)
        temp = pynvml.nvmlDeviceGetTemperature(_GPU_RESOURCE, pynvml.NVML_TEMPERATURE_GPU)
        clk = pynvml.nvmlDeviceGetClockInfo(_GPU_RESOURCE, pynvml.NVML_CLOCK_GRAPHICS)
        return {
            "load": float(util.gpu),
            "mem_used_mb": mem.used / 1024**2,
            "mem_total_mb": mem.total / 1024**2,
            "temp_c": float(temp),
            "freq_mhz": float(clk),
        }

    if _GPU_BACKEND == "tegra":
        info = _GPU_RESOURCE
        sample = {"load": _sysfs_int(_TEGRA_LOAD) / 10.0}
        if os.path.exists(_TEGRA_FREQ):
            sample["freq_mhz"] = _sysfs_int(_TEGRA_FREQ) / 1e6
        if info["temp"] and os.path.exists(info["temp"]):
            sample["temp_c"] = _sysfs_int(info["temp"]) / 1000.0
        return sample

    return None


# ---------------------------------------------------------------------------
# Summary printing
# ---------------------------------------------------------------------------

def _stat(values):
    return min(values), max(values), sum(values) / len(values)


def summarize_cpu(per_core_samples):
    all_vals = [v for s in per_core_samples for v in s]
    mn, mx, avg = _stat(all_vals)
    print(f"Overall: min={mn:.1f}%  max={mx:.1f}%  avg={avg:.1f}%")
    n_cores = len(per_core_samples[0])
    for i in range(n_cores):
        vals = [s[i] for s in per_core_samples]
        mn, mx, avg = _stat(vals)
        print(f"  Core {i:<2}: min={mn:5.1f}%  max={mx:5.1f}%  avg={avg:5.1f}%")


def summarize_gpu(gpu_samples):
    if not gpu_samples:
        print("No samples collected.")
        return

    def col(key):
        return [s[key] for s in gpu_samples if key in s]

    load = col("load")
    if load:
        mn, mx, avg = _stat(load)
        print(f"Load   : min={mn:5.1f}%  max={mx:5.1f}%  avg={avg:5.1f}%")

    freq = col("freq_mhz")
    if freq:
        mn, mx, avg = _stat(freq)
        suffix = ""
        if _GPU_BACKEND == "tegra" and _GPU_RESOURCE.get("max_freq"):
            suffix = f"  (max {_GPU_RESOURCE['max_freq']/1e6:.0f} MHz)"
            if _GPU_RESOURCE.get("avail_freq"):
                n_steps = len(_GPU_RESOURCE["avail_freq"])
                suffix += f"  {n_steps} P-states"
        print(f"Freq   : min={mn:7.1f} MHz  max={mx:7.1f} MHz  avg={avg:7.1f} MHz{suffix}")

    temp = col("temp_c")
    if temp:
        mn, mx, avg = _stat(temp)
        print(f"Temp   : min={mn:5.1f}°C  max={mx:5.1f}°C  avg={avg:5.1f}°C")

    mem_used = col("mem_used_mb")
    mem_total = col("mem_total_mb")
    if mem_used and mem_total:
        mn, mx, avg = _stat(mem_used)
        total = mem_total[0]
        print(f"Mem    : min={mn:7.1f} MB  max={mx:7.1f} MB  avg={avg:7.1f} MB  (total {total:.0f} MB)")


def summarize_top_processes(proc_history, n=3):
    # Aggregate per pid: collect all cpu% samples and last-seen name/rss
    cpu_acc = {}   # pid -> [cpu%, ...]
    meta = {}      # pid -> (name, rss_bytes)
    for snapshot in proc_history:
        for pid, (name, cpu, rss) in snapshot.items():
            cpu_acc.setdefault(pid, []).append(cpu)
            meta[pid] = (name, rss)

    # Only include processes with at least 2 samples (first is always 0.0 baseline)
    ranked = sorted(
        ((pid, sum(vals[1:]) / len(vals[1:]) if len(vals) > 1 else 0.0) for pid, vals in cpu_acc.items()),
        key=lambda x: x[1],
        reverse=True,
    )[:n]

    if not ranked:
        print("  (no process data)")
        return

    header = f"  {'PID':>6}  {'Name':<22}  {'Avg CPU':>8}  {'RSS MB':>8}"
    print(header)
    print("  " + "-" * (len(header) - 2))
    for pid, avg_cpu in ranked:
        name, rss = meta.get(pid, ("?", 0))
        rss_mb = rss / 1024**2 if rss else 0.0
        print(f"  {pid:>6}  {name:<22}  {avg_cpu:>7.1f}%  {rss_mb:>7.1f} MB")


def _print_nvml_gpu_processes(n=5):
    import pynvml
    procs = []
    for fn in (pynvml.nvmlDeviceGetComputeRunningProcesses,
               pynvml.nvmlDeviceGetGraphicsRunningProcesses):
        try:
            procs.extend(fn(_GPU_RESOURCE))
        except pynvml.NVMLError:
            pass

    seen = set()
    rows = []
    for p in procs:
        if p.pid in seen:
            continue
        seen.add(p.pid)
        try:
            name = psutil.Process(p.pid).name()
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            name = "?"
        mem_mb = getattr(p, 'usedGpuMemory', 0) / 1024**2 if p.usedGpuMemory else 0.0
        rows.append((p.pid, name, mem_mb))

    rows.sort(key=lambda x: x[2], reverse=True)
    rows = rows[:n]

    if not rows:
        print("  (no GPU processes found)")
        return

    header = f"  {'PID':>6}  {'Name':<22}  {'GPU Mem MB':>10}"
    print(header)
    print("  " + "-" * (len(header) - 2))
    for pid, name, mem_mb in rows:
        print(f"  {pid:>6}  {name:<22}  {mem_mb:>9.1f} MB")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Sample CPU and GPU usage over a time window.")
    parser.add_argument("--duration", type=float, required=True, help="Total monitoring duration in seconds")
    parser.add_argument("--interval", type=float, required=True, help="Seconds between samples")
    args = parser.parse_args()

    if args.duration <= 0 or args.interval <= 0:
        print("error: --duration and --interval must be positive", file=sys.stderr)
        sys.exit(1)

    # Prime psutil so the first readings aren't 0.0
    psutil.cpu_percent(percpu=True, interval=None)
    sample_top_processes()  # establishes cpu_percent baseline for all processes

    per_core_samples = []
    gpu_samples = []
    proc_history = []

    n_cores = psutil.cpu_count(logical=True)
    gpu_info = _GPU_NAME if _GPU_BACKEND else "not found"
    print(f"Monitoring for {args.duration}s  interval={args.interval}s  cores={n_cores}  gpu={gpu_info}")

    deadline = time.monotonic() + args.duration
    while time.monotonic() < deadline:
        per_core_samples.append(sample_cpu_per_core())
        proc_history.append(sample_top_processes())
        g = sample_gpu()
        if g is not None:
            gpu_samples.append(g)
        remaining = deadline - time.monotonic()
        time.sleep(min(args.interval, max(remaining, 0)))

    print()
    print("=== CPU Summary ===")
    summarize_cpu(per_core_samples)
    print("Top processes by CPU:")
    summarize_top_processes(proc_history)

    print()
    print("=== GPU Summary ===")
    if _GPU_BACKEND:
        print(f"Device : {_GPU_NAME}")
        summarize_gpu(gpu_samples)
        if _GPU_BACKEND == "nvml":
            print("Top processes by GPU:")
            _print_nvml_gpu_processes()
        else:
            print("Top processes by GPU: not available on Tegra")
    else:
        print("No NVIDIA/Tegra GPU found.")

    if _GPU_BACKEND == "nvml":
        import pynvml
        pynvml.nvmlShutdown()


if __name__ == "__main__":
    main()
