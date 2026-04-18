# byPassRT: A Lightweight User-Space Packet Processing Runtime

**byPassRT** is a DPDK-inspired, kernel-bypass networking runtime built for Linux using **AF_XDP** and **eBPF**. It shifts the burden of packet processing out of the Linux kernel and into user space, enabling ultra-fast, zero-copy packet I/O via lock-free ring buffers.

## 🚀 Project Overview

Traditional Linux networking (via standard sockets or even `AF_PACKET`) is relatively slow due to multiple memory copies, kernel-space to user-space context switches, and interrupt handling overhead. 

**byPassRT** overcomes this by utilizing modern Linux `AF_XDP` sockets. The network card driver places packets directly into a shared memory region (UMEM) accessible by our user-space C application.

### Key Features
* **True Kernel Bypass**: Uses eBPF/XDP for an ultra-fast data path.
* **Lock-Free Queues**: Utilizes single-producer/single-consumer (`SPSC`) lock-free rings for RX and TX logic.
* **Mempools & Packet Buffers**: Uses fixed-size, pre-allocated memory pools (`mbuf-lite`) to avoid expensive dynamic memory allocation (`malloc`) during the critical fast-path.
* **Polling Engine**: Processes packets in "bursts" using a tight user-space loop to eliminate interrupt overhead.

## 📁 File Structure & Roles

* **`src/main.c`**: The core application and user-space **Polling Engine**. It binds to a network interface, enters a tight infinite loop, and continuously reads, processes (e.g., swaps MAC addresses for loopback), and transmits packets in bulk.
* **`src/af_xdp.c` & `include/api.h`**: The **I/O Engine**. Manages the setup of the `AF_XDP` socket, shared memory (`UMEM`), and exposes simplified burst-processing APIs (`lite_rx_burst`, `lite_tx_burst`, `lite_free`) to the main application.
* **`src/mempool.c` & `include/mempool.h`**: The **Memory Manager**. Implementing DPDK-style packet buffer pools (`mbuf-lite`). Pre-allocates fixed memory frames and provides extremely fast `alloc()` and `free()` index operations decoupled from system calls.
* **`xdp/xdp_prog.c`**: Placeholder for custom eBPF packet-filtering programs (hardware-level DDOS dropping) for future expansions.
* **`benchmarks/run_benchmark.sh`**: A stress-testing script using flood-pings to validate high-throughput performance of our lock-free queues.
* **`tests/setup_veth.sh`**: A helper script to generate virtual ethernet interface pairs (`veth`) so that the application can be safely tested locally without needing a dedicated physical high-speed NIC.
* **`Makefile`**: The build system using `gcc` and linking against `libbpf` and `libxdp`.

## 🛠️ How to Build and Run

### 1. Dependencies
Ensure you have the latest eBPF and XDP development libraries mapping to your kernel:
```bash
sudo apt-get install libbpf-dev libxdp-dev
```

### 2. Build the Project
```bash
make clean
make
```

### 3. Testing via Virtual Interfaces (veth)
Testing AF_XDP requires attaching a packet filter to a network interface ring. To avoid locking yourself out of your actual network, use virtual pairs.

1. **Setup the test interfaces**:
```bash
chmod +x tests/setup_veth.sh
./tests/setup_veth.sh
```

2. **Run the Runtime**:
Wait for `byPassRT` to bind to the test interface (Requires sudo to attach BPF programs).
```bash
sudo ./byPassRT veth-bypass
```

3. **Generate Test Traffic (in another terminal)**:
Send ping traffic or use packet generators like `scapy` over the peer interface.
```bash
ping -I veth-peer 10.0.0.1
```

You will see live metrics tracking packet reception and transmission bursts directly on the `byPassRT` output terminal!
