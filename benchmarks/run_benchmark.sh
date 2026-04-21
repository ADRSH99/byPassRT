#!/bin/bash
echo "Starting Flood Ping Benchmark against byPassRT..."
echo "This requires byPassRT to be actively running on veth-bypass!"
echo ""
echo "Running 100 packets as fast as possible to stress test the ring buffers..."
sudo ping -f -c 100 -I veth-peer 10.0.0.1
echo ""
echo "Check the byPassRT terminal for total RX/TX updates!"
