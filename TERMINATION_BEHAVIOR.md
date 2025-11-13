# VMWatch - Malicious Detection and Termination

## Fixed Issues

### 1. Compilation Errors ✅
**Problem**: Missing `popen`/`pclose` declarations causing implicit function declaration errors.

**Solution**: Added `#define _POSIX_C_SOURCE 200809L` at the top of main.c to enable POSIX extensions.

**Result**: All compilation errors resolved. Program compiles cleanly with no errors.

---

### 2. Uninitialized Variables ✅
**Problem**: `net_stats` and `syscall_stats` could be used uninitialized if their getter functions failed.

**Solution**: Added fallback to use previous values if monitoring functions fail:
```c
if (get_network_stats_from_vm(vm_ip, vm_user, &net_stats) < 0) {
    net_stats = prev_net_stats;  // Use previous values on failure
}
```

**Result**: No uninitialized variable warnings. Safe operation even if monitoring temporarily fails.

---

### 3. Program Termination on Malicious Detection ✅
**Problem**: Program would continue running or return error codes instead of immediately terminating when malicious behavior was detected.

**Solution**: Added `exit(EXIT_FAILURE)` at THREE key detection points:

#### a) Critical RAM Threshold (>80%)
```c
if (stats.usage_percent > HIGH_RAM_THRESHOLD) {
    printf("\n[ACTION] Stopping VM and terminating...\n");
    stop_vm(vm_name);
    printf("\n[TERMINATED] Malicious file detected - Program exiting\n\n");
    exit(EXIT_FAILURE);  // TERMINATE IMMEDIATELY
}
```

#### b) Multiple Sustained Spikes (≥3 of any type)
```c
if (spike_count >= 3 || net_spike_count >= 3 || syscall_spike_count >= 3) {
    printf("\n[ACTION] Stopping VM and terminating...\n");
    stop_vm(vm_name);
    printf("\n[TERMINATED] Malicious file detected - Program exiting\n\n");
    exit(EXIT_FAILURE);  // TERMINATE IMMEDIATELY
}
```

#### c) VM Crash Detection (3 consecutive connection failures)
```c
if (consecutive_failures >= 3) {
    printf("\n[ACTION] Cleaning up crashed VM...\n");
    stop_vm(vm_name);
    printf("\n[TERMINATED] VM crash detected - Program exiting\n\n");
    exit(EXIT_FAILURE);  // TERMINATE IMMEDIATELY
}
```

#### d) End of Monitoring (any malicious behavior detected)
```c
if (is_malicious) {
    printf("\n[ACTION] Stopping VM and terminating...\n");
    stop_vm(vm_name);
    printf("\n[TERMINATED] Malicious file detected - Program exiting\n\n");
    exit(EXIT_FAILURE);  // TERMINATE IMMEDIATELY
}
```

**Result**: Program now **immediately terminates** when malicious behavior is confirmed. No further execution occurs.

---

## Termination Behavior Summary

### Safe File (test-safe.sh)
```
✓ No suspicious behavior detected
   - Memory usage remained stable
   - Network activity was normal
   - Syscall activity was normal

[Exit Code: 0]
```

### Malicious File (any attack detected)
```
🚨 WARNING: MALICIOUS BEHAVIOR DETECTED! 🚨
   - 5 RAM spike(s) detected (>30.0% increase)
   - 3 network spike(s) detected (>0.95 MB/s)
   - 2 syscall spike(s) detected

[ACTION] Stopping VM and terminating...
Domain 'example-vm' destroyed

[TERMINATED] Malicious file detected - Program exiting

[Exit Code: 1]
```

### Early Termination (3+ spikes before monitoring completes)
- Program stops monitoring immediately
- VM is shut down
- Program exits with code 1
- No script output is fetched

### VM Crash
- Detected after 3 consecutive SSH failures (6 seconds)
- VM is cleaned up
- Program exits immediately with code 1

---

## Exit Codes

| Code | Meaning |
|------|---------|
| 0    | File is safe - no malicious behavior detected |
| 1    | File is malicious - behavior detected and VM stopped |
| 1    | VM crashed - extremely dangerous malware |
| 1    | Fatal error - VM startup or script execution failed |

---

## Testing Termination

Run any malicious test:
```bash
# Memory attack - should terminate with exit code 1
sudo ./vmwatch example-vm 192.168.122.160 testing/run-malware-memory.sh
echo "Exit code: $?"

# Fork bomb - should terminate with exit code 1
sudo ./vmwatch example-vm 192.168.122.160 testing/run-malware-forkbomb.sh
echo "Exit code: $?"

# Network attack - should terminate with exit code 1
sudo ./vmwatch example-vm 192.168.122.160 testing/run-malware-network.sh
echo "Exit code: $?"

# Safe test - should complete with exit code 0
sudo ./vmwatch example-vm 192.168.122.160 test-safe.sh
echo "Exit code: $?"
```

---

## Behavior Changes

### Before Fix
- Program would continue monitoring even after detecting malicious behavior
- Would complete all 60 iterations before stopping VM
- Would attempt to fetch script output even from compromised VM
- Would return to main() and continue execution

### After Fix
- **Immediate termination** when malicious threshold reached
- **Early exit** on sustained attack patterns (3+ spikes)
- **Clean shutdown** of VM before termination
- **Clear messaging** about why program is terminating
- **Proper exit codes** for scripting and automation

---

## Security Improvements

1. **Faster Response**: VM is stopped as soon as danger is confirmed (not after full monitoring)
2. **Clear Intent**: `[TERMINATED]` message clearly indicates malware was found
3. **No Contamination**: Script output is not fetched from known-malicious VM
4. **Automation Ready**: Exit codes can be used in CI/CD pipelines
5. **Resource Protection**: VM is guaranteed to be stopped when malicious behavior detected

---

## Example Usage in Scripts

```bash
#!/bin/bash
# Automated malware testing

if sudo ./vmwatch example-vm 192.168.122.160 suspicious-file.sh; then
    echo "✓ File is SAFE"
    # Continue with deployment
else
    echo "✗ File is MALICIOUS"
    # Alert security team
    exit 1
fi
```

---

## Verification

Compile and test:
```bash
make clean && make
sudo ./vmwatch example-vm 192.168.122.160 testing/run-malware-memory.sh
```

You should see immediate termination when RAM spikes are detected, not after 2 minutes.
