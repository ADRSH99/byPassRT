#!/bin/bash
sudo ip link add veth-bypass type veth peer name veth-peer
sudo ip link set veth-bypass up
sudo ip link set veth-peer up

echo "Veth pair created:"
echo "  - veth-bypass (to be used by byPassRT)"
echo "  - veth-peer   (to send test traffic into)"
echo ""
echo "To test: "
echo "  1. Run: sudo ./byPassRT veth-bypass"
echo "  2. In another terminal run: ping -I veth-peer 10.0.0.1"
