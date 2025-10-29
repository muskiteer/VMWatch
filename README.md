# VMWatch - KVM Security Monitor

A C-based tool that launches scripts in KVM virtual machines and monitors for malicious behavior by detecting abnormal RAM usage patterns.

## Features

- ✅ Automatically starts KVM VMs
- ✅ Executes scripts inside VMs via SSH
- ✅ Real-time RAM monitoring
- ✅ Detects malicious behavior (fork bombs, memory attacks)
- ✅ Alerts on suspicious RAM spikes (>30% increase)

## Prerequisites

```bash
# Install required packages
sudo apt update
sudo apt install qemu-kvm libvirt-daemon-system libvirt-clients \
                 bridge-utils virtinst cloud-image-utils gcc \
                 pkg-config libvirt-dev
```

## Quick Start

### 1. Create a Test VM

```bash
chmod +x setup.sh
sudo ./setup.sh
```

This creates an Ubuntu VM with:
- Name: `example-vm`
- User: `ubuntu`
- Password: `password123`
- SSH enabled

### 2. Set Up SSH Keys (Optional but Recommended)

```bash
ssh-keygen -t rsa
ssh-copy-id ubuntu@<VM_IP>
```

### 3. Compile VMWatch

```bash
gcc -o vmwatch main.c $(pkg-config --cflags --libs libvirt)
```

### 4. Run VMWatch

```bash
# Test with safe script (no malicious behavior)
sudo ./vmwatch example-vm <VM_IP> ./test-safe.sh

# Test with mild memory bomb (gradual 500MB increase)
sudo ./vmwatch example-vm <VM_IP> ./forkbomb-mild.sh

# Test with medium memory bomb (2GB via 20 processes)
sudo ./vmwatch example-vm <VM_IP> ./forkbomb-medium.sh

# Test with extreme fork bomb (CRASHES VM INSTANTLY!)
sudo ./vmwatch example-vm <VM_IP> ./forkbomb.sh
```

## Usage

```bash
./vmwatch <vm-name> <vm-ip> <script-path>
```

**Arguments:**
- `vm-name`: Name of the VM in libvirt (e.g., `example-vm`)
- `vm-ip`: IP address of the VM (get with `sudo virsh domifaddr <vm-name>`)
- `script-path`: Path to script to execute in VM

## How It Works

1. **VM Management**: Connects to libvirt, starts the VM if not running
2. **Script Execution**: Copies script to VM via SCP, executes it via SSH
3. **RAM Monitoring**: Polls VM memory stats every 2 seconds for 2 minutes
4. **Threat Detection**: Calculates percentage change in RAM usage between polls
5. **Alert System**: Flags behavior as malicious if RAM increases >30% suddenly

## Detection Example

```
[001] Used: 245.32 MB (12.0%) | Change: +2.3%
[002] Used: 251.87 MB (12.3%) | Change: +2.7%
[003] Used: 412.56 MB (20.2%) | Change: +63.8% ⚠️  SPIKE DETECTED!
[004] Used: 789.23 MB (38.7%) | Change: +91.3% ⚠️  SPIKE DETECTED!

🚨 WARNING: POTENTIALLY MALICIOUS BEHAVIOR! 🚨
   - 2 RAM spike(s) detected (>30.0% increase)
   - Abnormal memory consumption
   - Possible fork bomb or memory attack
```

## Project Structure

```
vmwatch/
├── main.c               # Main C source code
├── setup.sh             # VM creation script
├── test-safe.sh         # Safe test script
├── forkbomb-mild.sh     # Mild memory bomb (gradual, 500MB total)
├── forkbomb-medium.sh   # Medium memory bomb (20 processes, 2GB)
├── forkbomb.sh          # Extreme fork bomb (CRASHES VM!)
├── Makefile             # Build automation
└── README.md            # This file
```

## Makefile

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags libvirt)
LDFLAGS = $(shell pkg-config --libs libvirt)
TARGET = vmwatch

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
```

Build with:
```bash
make
```

## VM Management Commands

```bash
# List all VMs
sudo virsh list --all

# Get VM IP
sudo virsh domifaddr example-vm

# Start VM manually
sudo virsh start example-vm

# Stop VM
sudo virsh shutdown example-vm

# Force stop
sudo virsh destroy example-vm

# Delete VM
sudo virsh undefine example-vm --remove-all-storage
```

## Customization

### Change Detection Threshold

Edit `main.c`:
```c
#define RAM_SPIKE_THRESHOLD 30.0  // Change to desired percentage
```

### Change Monitoring Duration

Edit `main.c`:
```c
#define MONITOR_ITERATIONS 60  // 60 iterations × 2 seconds = 2 minutes
```

### Change VM User

Edit `main.c` in the `main()` function:
```c
const char *vm_user = "ubuntu";  // Change to your VM username
```

## Troubleshooting

### "Failed to open connection to qemu:///system"

```bash
# Start libvirt daemon
sudo systemctl start libvirtd
sudo systemctl enable libvirtd

# Add your user to libvirt group
sudo usermod -aG libvirt $USER
# Log out and back in
```

### "Failed to copy script to VM"

- Ensure SSH is working: `ssh ubuntu@<VM_IP>`
- Set up SSH keys: `ssh-copy-id ubuntu@<VM_IP>`
- Check VM is running: `sudo virsh list`

### Can't get VM IP

```bash
# Try these commands
sudo virsh domifaddr example-vm
sudo virsh net-dhcp-leases default
sudo arp -n | grep virbr0
```

### VM won't start

```bash
# Check VM definition
sudo virsh dumpxml example-vm

# Check logs
sudo journalctl -u libvirtd -f
```

## Safety Notes

⚠️ **Test Scripts Safety Levels**:

**Safe:**
- `test-safe.sh` - Completely harmless, minimal memory usage

**Mild (Recommended for testing):**
- `forkbomb-mild.sh` - Gradually allocates 500MB over 30 seconds
- Safe to run, VM stays responsive
- Good for testing detection without crashing

**Medium (Caution):**
- `forkbomb-medium.sh` - Creates 20 processes, ~2GB memory
- May slow down VM significantly
- Use on VMs with at least 2GB RAM

**Extreme (DANGEROUS):**
- `forkbomb.sh` - Exponential fork bomb
- **WILL CRASH THE VM INSTANTLY**
- Only for testing crash detection
- Requires VM restart: `sudo virsh destroy example-vm && sudo virsh start example-vm`

**Best Practice:**
- Always test with `forkbomb-mild.sh` first
- Take VM snapshot before dangerous tests: `sudo virsh snapshot-create-as example-vm snapshot1`
- Restore if needed: `sudo virsh snapshot-revert example-vm snapshot1`

## License

MIT License - Feel free to use and modify

## Contributing

Pull requests welcome! Please test thoroughly before submitting.
