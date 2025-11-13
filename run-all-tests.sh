#!/bin/bash
# Quick test runner for all malware types

VM_NAME="example-vm"
VM_IP="192.168.122.160"

echo "╔════════════════════════════════════════════╗"
echo "║    VMWatch Malware Test Suite             ║"
echo "╚════════════════════════════════════════════╝"
echo ""

# Test 1: Safe baseline
echo "═══════════════════════════════════════"
echo "Test 1: Safe Baseline (test-safe.sh)"
echo "═══════════════════════════════════════"
sudo ./vmwatch "$VM_NAME" "$VM_IP" test-safe.sh
echo ""
echo "Exit code: $?"
echo ""
read -p "Press Enter to continue to next test..."
echo ""

# Test 2: Memory attack
echo "═══════════════════════════════════════"
echo "Test 2: Memory Bomb Attack"
echo "═══════════════════════════════════════"
sudo ./vmwatch "$VM_NAME" "$VM_IP" testing/run-malware-memory.sh
echo ""
echo "Exit code: $?"
echo ""
read -p "Press Enter to continue to next test..."
echo ""

# Test 3: Fork bomb
echo "═══════════════════════════════════════"
echo "Test 3: Fork Bomb Attack"
echo "═══════════════════════════════════════"
sudo ./vmwatch "$VM_NAME" "$VM_IP" testing/run-malware-forkbomb.sh
echo ""
echo "Exit code: $?"
echo ""
read -p "Press Enter to continue to next test..."
echo ""

# Test 4: Network attack
echo "═══════════════════════════════════════"
echo "Test 4: Network Flooding Attack"
echo "═══════════════════════════════════════"
sudo ./vmwatch "$VM_NAME" "$VM_IP" testing/run-malware-network.sh
echo ""
echo "Exit code: $?"
echo ""

echo "╔════════════════════════════════════════════╗"
echo "║    All Tests Complete                     ║"
echo "╚════════════════════════════════════════════╝"
