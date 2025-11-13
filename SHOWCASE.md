# VMWatch Showcase Guide
## Comprehensive Monitoring Demonstration

This guide demonstrates all three monitoring capabilities: RAM, Network, and Syscall monitoring.

---

## Prerequisites

1. **VM Setup**: Make sure your VM is created and running
   ```bash
   sudo ./setup.sh
   ```

2. **VM Information**: Note your VM's IP address from setup.sh output
   Example: `192.168.122.239`

3. **Update main.c**: Edit line 348 in main.c to use your VM's IP:
   ```c
   const char *vm_ip = "192.168.122.239";  // <-- YOUR VM IP HERE
   ```

4. **Compile**: Build the vmwatch executable
   ```bash
   make
   ```

---

## Test Scenarios

### Test 1: Safe Script (Baseline)
**Purpose**: Establish normal behavior baseline
**Monitoring**: All metrics should remain stable

```bash
sudo ./vmwatch test-safe.sh
```

**Expected Output**:
- Memory: Stable around baseline
- Network: Minimal activity (<100KB)
- Syscalls: Low process count (2-5)
- Result: ✓ No suspicious behavior detected

---

### Test 2: Memory Attack
**Purpose**: Demonstrate RAM spike detection
**Monitoring**: Should detect memory bloat

```bash
sudo ./vmwatch testing/forkbomb-mild.sh
```

**Expected Output**:
- Memory: Gradual increase, multiple ⚠️ RAM-SPIKE warnings
- Network: Minimal activity
- Syscalls: Moderate increase
- Result: 🚨 MALICIOUS detected - RAM spikes

**Watch for**:
- Memory changing from ~200MB → 700MB
- Percentage increases >30%
- Multiple RAM-SPIKE alerts

---

### Test 3: Network Exfiltration
**Purpose**: Demonstrate network spike detection
**Monitoring**: Should detect high network traffic

```bash
sudo ./vmwatch testing/network-exfil.sh
```

**Expected Output**:
- Memory: Relatively stable
- Network: Large RX/TX spikes (⚠️ NET-SPIKE warnings)
- Syscalls: Moderate (wget/curl processes)
- Result: 🚨 MALICIOUS detected - Network spikes

**Watch for**:
- Network RX showing +500KB to +1000KB per iteration
- Multiple NET-SPIKE alerts
- TX traffic increases

---

### Test 4: Fork Bomb (Syscall Attack)
**Purpose**: Demonstrate syscall/process spawning detection
**Monitoring**: Should detect rapid process creation

```bash
sudo ./vmwatch testing/forkbomb-medium.sh
```

**Expected Output**:
- Memory: Significant increase
- Network: Minimal
- Syscalls: Rapid fork count increase (⚠️ SYSCALL-SPIKE warnings)
- Result: 🚨 MALICIOUS detected - Multiple spike types

**Watch for**:
- Process count jumping from ~5 → 20-30
- Fork count increasing rapidly (+10-50 per iteration)
- Both RAM-SPIKE and SYSCALL-SPIKE alerts

---

### Test 5: Combined Attack
**Purpose**: Test all monitoring systems simultaneously
**Monitoring**: Should detect multiple attack vectors

```bash
sudo ./vmwatch testing/combined-attack.sh
```

**Expected Output**:
- Memory: Gradual then sharp increases
- Network: Periodic traffic spikes
- Syscalls: Process spawning activity
- Result: 🚨 MALICIOUS detected - All spike types

**Watch for**:
- Sequential phases: memory → processes → network
- Multiple alert types: RAM-SPIKE, NET-SPIKE, SYSCALL-SPIKE
- Combined threat score in final report

---

### Test 6: Extreme Attack (VM Crash)
**Purpose**: Demonstrate crash detection
**Monitoring**: Should detect VM failure
**⚠️ WARNING**: This will crash the VM!

```bash
sudo ./vmwatch testing/forkbomb.sh
```

**Expected Output**:
- Initial spikes in all metrics
- SSH connection failures
- Result: 🚨 VM CRASHED - MALICIOUS BEHAVIOR CONFIRMED

**Watch for**:
- 3 consecutive connection failures
- "VM CRASHED" message
- Automatic cleanup

---

## Understanding the Output

### Output Format
```
[001] RAM: 234.50 MB (23.5%) +5.2 MB | NET: RX +12.30 KB TX +5.67 KB | SYS: 8 procs +2 forks
```

**Components**:
- `[001]`: Iteration number (out of 60)
- `RAM`: Memory used in MB and percentage
- `+5.2 MB`: Memory change since last check
- `NET`: Network receive (RX) and transmit (TX) in KB
- `SYS`: Total process count and fork activity
- `⚠️ SPIKE` indicators for detected anomalies

### Spike Thresholds
- **RAM-SPIKE**: >30% increase OR >100MB sudden jump
- **NET-SPIKE**: >500KB transferred in 2 seconds
- **SYSCALL-SPIKE**: >50 forks OR >1000 syscall delta

### Malicious Determination
The script is flagged as malicious if ANY of:
1. RAM spikes detected
2. Network traffic spikes detected
3. Syscall/fork activity spikes detected
4. RAM usage exceeds 80%
5. VM crashes (3 consecutive SSH failures)

---

## Troubleshooting

### "Connection refused" errors
- VM may not be running: `sudo virsh list --all`
- Start VM: `sudo virsh start example-vm`
- Check IP: `sudo virsh domifaddr example-vm`

### No network spikes detected
- VM may lack internet access
- Check: `sudo virsh net-list` (default network should be active)
- The network-exfil.sh test requires external connectivity

### SSH timeout
- Wait 30 seconds after VM boot
- Verify SSH: `ssh -o ConnectTimeout=5 root@<VM_IP> 'echo ok'`
- Check keys: `ls -la ~/.ssh/id_rsa*` (setup.sh generates these)

---

## Quick Verification

Run all tests in sequence:
```bash
# Safe baseline
sudo ./vmwatch test-safe.sh

# Memory attack
sudo ./vmwatch testing/forkbomb-mild.sh

# Network attack (if internet available)
sudo ./vmwatch testing/network-exfil.sh

# Combined attack
sudo ./vmwatch testing/combined-attack.sh
```

Each test takes ~2 minutes. Total demo time: ~8-10 minutes.

---

## What's Being Monitored

1. **RAM Monitoring** (`/proc/meminfo`):
   - Total memory
   - Used memory
   - Usage percentage
   - Detects: Memory bombs, gradual allocation attacks

2. **Network Monitoring** (`/proc/net/dev`):
   - Bytes received (RX)
   - Bytes transmitted (TX)
   - Packet counts
   - Detects: Data exfiltration, DDoS, network scanning

3. **Syscall Monitoring** (`/proc/stat` + `ps`):
   - Total process count
   - Fork activity
   - Running processes
   - Detects: Fork bombs, process spawning attacks

---

## Expected Success Indicators

✓ **Compilation**: No errors, vmwatch binary created
✓ **Safe Test**: Completes without malicious detection
✓ **Memory Test**: Detects RAM spikes within 20-30 seconds
✓ **Network Test**: Detects network spikes (if internet available)
✓ **Combined Test**: Detects multiple spike types
✓ **Crash Test**: Detects and reports VM crash

---

## Clean Up After Testing

```bash
# Stop VM
sudo virsh destroy example-vm

# (Optional) Remove VM completely
sudo virsh undefine example-vm --remove-all-storage
```

To recreate VM, run `sudo ./setup.sh` again.
