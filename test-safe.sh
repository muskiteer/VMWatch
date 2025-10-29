#!/bin/bash
# Test script - safe version
echo "Starting safe test script..."
echo "This script does nothing harmful"
sleep 5
echo "Allocating small amount of memory..."
python3 -c "x = ' ' * 10000000; import time; time.sleep(10)"
echo "Test complete!"
