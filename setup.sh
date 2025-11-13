#!/bin/bash
# VMWatch Setup Script - Creates a cloud-based VM for testing

set -e

VM_NAME="example-vm"
VM_USER="ubuntu"
VM_PASS="password123"

echo "╔════════════════════════════════════════════╗"
echo "║     VMWatch Setup - Creating Test VM      ║"
echo "╚════════════════════════════════════════════╝"
echo ""

# Check if already exists
if sudo virsh list --all | grep -q ${VM_NAME}; then
    echo "❌ VM '${VM_NAME}' already exists!"
    echo "To recreate, first run: sudo virsh undefine ${VM_NAME} --remove-all-storage"
    exit 1
fi

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_DIR="${SCRIPT_DIR}"

# Download cloud image to project directory
echo "[1/6] Downloading Ubuntu cloud image to project directory..."
cd "${IMAGE_DIR}"
if [ ! -f ubuntu-22.04-server-cloudimg-amd64.img ]; then
    wget -q --show-progress https://cloud-images.ubuntu.com/releases/22.04/release/ubuntu-22.04-server-cloudimg-amd64.img
fi

# Copy to libvirt images directory
echo "[2/6] Copying image to libvirt directory..."
sudo cp ubuntu-22.04-server-cloudimg-amd64.img /var/lib/libvirt/images/${VM_NAME}.qcow2

# Create cloud-init config
# Generate SSH key if it doesn't exist
if [ ! -f ~/.ssh/id_rsa ]; then
    echo "Generating SSH key..."
    ssh-keygen -t rsa -N "" -f ~/.ssh/id_rsa
fi

SSH_KEY=$(cat ~/.ssh/id_rsa.pub)

# Create user-data file
cat > /tmp/user-data << EOF
#cloud-config
users:
  - name: ${VM_USER}
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    ssh_authorized_keys:
      - ${SSH_KEY}
packages:
  - openssh-server
runcmd:
  - systemctl enable ssh
  - systemctl start ssh
EOF

cat > /tmp/meta-data << EOF
instance-id: ${VM_NAME}-001
local-hostname: ${VM_NAME}
EOF

# Create seed image
echo "[3/7] Creating seed image..."
sudo cloud-localds /var/lib/libvirt/images/seed.img /tmp/user-data /tmp/meta-data

# Create VM
echo "[4/7] Creating virtual machine..."
sudo virt-install \
  --name ${VM_NAME} \
  --ram 2048 \
  --vcpus 2 \
  --disk path=/var/lib/libvirt/images/${VM_NAME}.qcow2 \
  --disk path=/var/lib/libvirt/images/seed.img,device=cdrom \
  --os-variant ubuntu22.04 \
  --network network=default \
  --graphics none \
  --import \
  --noautoconsole

# Wait for VM to boot
echo "[5/7] Waiting for VM to boot (30 seconds)..."
sleep 30

# Get IP
echo "[6/7] Getting VM IP address..."
VM_IP=$(sudo virsh domifaddr ${VM_NAME} | grep -oP '(\d+\.){3}\d+' | head -1)

if [ -z "$VM_IP" ]; then
    echo "⚠️  Could not get IP automatically. Try:"
    echo "   sudo virsh domifaddr ${VM_NAME}"
    echo "   or: sudo virsh net-dhcp-leases default"
else
    echo ""
    echo "✅ VM Created Successfully!"
    echo ""
    echo "╔════════════════════════════════════════════╗"
    echo "║           VM Connection Details            ║"
    echo "╚════════════════════════════════════════════╝"
    echo ""
    echo "  VM Name:   ${VM_NAME}"
    echo "  IP Address: ${VM_IP}"
    echo "  Username:   ${VM_USER}"
    echo "  Password:   ${VM_PASS}"
    echo ""
    echo "Test SSH connection:"
    echo "  ssh ${VM_USER}@${VM_IP}"
    echo ""
    echo "Set up passwordless SSH:"
    echo "  ssh-keygen -t rsa (if you don't have a key)"
    echo "  ssh-copy-id ${VM_USER}@${VM_IP}"
    echo ""
    echo "Compile and run VMWatch:"
    echo "  gcc -o vmwatch main.c \$(pkg-config --cflags --libs libvirt)"
    echo "  sudo ./vmwatch ${VM_NAME} ${VM_IP} ./test.sh"
    echo ""
    echo "[7/7] Update main.c with this IP address (line 348):"
    echo "  const char *vm_ip = \"${VM_IP}\";"
    echo ""
fi
