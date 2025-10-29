#!/bin/bash
# Medium memory bomb - creates processes that consume memory
echo "Starting medium memory bomb..."

# Create 20 background processes that allocate memory
for i in {1..20}; do
    (
        # Each process allocates ~100MB in Python
        python3 -c "import time; x = ' ' * 100000000; time.sleep(30)" &
    ) &
    sleep 1
done

echo "Memory bomb active"
wait
