#!/bin/bash
# Mild memory bomb - gradually allocates memory
echo "Starting mild memory bomb..."

# Allocate memory in steps
for i in {1..10}; do
    echo "Iteration $i: Allocating 50MB..."
    # Allocate 50MB and keep it in memory
    dd if=/dev/zero of=/tmp/memfill_$i bs=1M count=50 2>/dev/null &
    sleep 3
done

echo "Memory allocation complete"
wait
