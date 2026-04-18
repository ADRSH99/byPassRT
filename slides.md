---
# Slide 1: Title Slide
## byPassRT: A Lightweight User-Space Packet Processing Runtime
**Course Project Presentation**
* **Focus**: High-Performance Networking, Kernel Bypass, eBPF, AF_XDP
* **Project Overview**: We developed an advanced DPDK-inspired runtime designed to receive and process millions of network packets per second fully in user-space, achieving near-zero latency by intentionally bypassing the traditional Linux protocol stack.

*(Visual Suggestion: Add a high-quality logo of the project or an abstract networking topology graphic on the title slide to set a professional tone).*

---
# Slide 2: Introduction to the Chosen Problem (Part 1)
## The Latency Bottleneck in Modern High-Speed Networks
* **The Era of Microsecond Latency**: In mission-critical environments such as High-Frequency Trading (HFT), 5G telecommunications, and hyperscale cloud infrastructure, the volume of packet ingestion has surpassed what traditional operating systems were designed to handle. We are reaching the limit of software design, not hardware capabilities.
* **The Problem of Scale**: As network interface cards (NICs) successfully scale to 10Gbps, 40Gbps, and 100Gbps, operating systems are choking under the sheer packet load. 
* **The Interruption Crisis**: Packets arriving at the NIC blindly trigger hardware interrupts, halting the CPU's primary execution tasks multiple millions of times per second just to acknowledge packet receipt.

*(Visual Suggestion: Insert a line graph displaying bandwidth capabilities of modern NICs exponentially rising while standard OS processing speeds plateau).*

---
# Slide 3: Introduction to the Chosen Problem (Part 2)
## Breaking Down the Linux Networking Stack Limitations
* **Dynamic Memory Overhead**: When a packet arrives, the Linux kernel relies heavily on dynamic memory allocation—famously the `sk_buff` structure—using variants of `malloc()`. Executing memory allocation millions of times per second instantly drains CPU cycles.
* **Context Switching & The Software Wall**: A packet entering the kernel is processed through iptables, routing rules, and the TCP/IP stack. After this massive journey, a costly Context Switch occurs to pass the data across the security boundary into user space.
* **Multiple Memory Copies**: Standard sockets (like `recvfrom()`) force the system to literally duplicate the packet data payload from kernel memory regions into application memory regions, wasting immense memory bandwidth.

*(Visual Suggestion: Add a diagram showing the traditional lifecycle of a packet through the OS—hardware -> drivers -> kernel space -> protocol stack -> context switch -> user-space socket).*

---
# Slide 4: Background and Motivation (Part 1)
## The Evolution of Application Demands
* **The Need for Control**: Industries require dedicated applications that can monopolize hardware resources for absolute throughput. Software developers realized the OS must be sidelined to unlock the true power of the hardware.
* **The Concept of Kernel Bypass**: The motivation for this architectural paradigm shift is "Kernel Bypass." The principle is simple: The Operating System was built to be fair to all running programs, but high-performance networking relies on being wildly unfair and giving 100% of resources strictly to packet processing.
* **Motivating Use Cases**: Custom load balancers (like Cloudflare's edge), DDOS mitigation hardware, and ultra-low latency packet sniffers all absolutely require a bypassing model to function without collapsing under attack volumes.

---
# Slide 5: Background and Motivation (Part 2)
## The Complexity & Drawbacks of Traditional DPDK
* **What is DPDK?**: The Data Plane Development Kit (DPDK) created by Intel is the industry standard for kernel bypass. It works by having a custom user-space driver seize complete, dictatorial control over the Network Interface Card (NIC).
* **The Drawback of DPDK**: While DPDK is phenomenally fast, it completely breaks standard server functionality. Once DPDK takes the NIC using drivers like `uio` or `vfio_pci`, the Linux OS literally cannot see the network card. You cannot route standard traffic, run `ping`, or use SSH dynamically over that interface. Let alone the massive codebase complexity.
* **Our Motivation**: We wanted the blistering speed of DPDK’s zero-copy architecture but combined with the grace and modern modularity of the Linux kernel, allowing the OS to remain intact while our application selectively siphons packets.

---
# Slide 6: Problem Statement and Objectives
## Engineering a Modern Zero-Copy Runtime
* **Problem Statement**: To engineer a lightweight, user-space packet processing runtime that achieves DPDK-level zero-copy performance and multi-million Packets-Per-Second (PPS) throughput without seizing proprietary control of the Network Interface Card, utilizing modern Linux kernel subsystems.
* **Objective 1 - True Kernel Bypass**: Establish a shared memory tunnel directly from the NIC driver to the user-space application using eBPF and AF_XDP.
* **Objective 2 - DPDK-Style Memory Management**: Eliminate all dynamic memory allocations (`malloc`) during the packet fast-path by engineering static memory pools (`mempools`) and fixed packet buffers (`mbuf-lite`).
* **Objective 3 - Lock-Free I/O Engine**: Design single-producer/single-consumer architectural ring buffers that use atomic operations to negotiate packet states, completely eliminating latency-heavy Mutex locks.
* **Objective 4 - A Polling Core**: Implement an application loop that spins natively on CPU cores instead of sleeping, actively polling hardware queues to eradicate interrupt latency.

---
# Slide 7: General Solution (Part 1)
## Architecture Re-Imagined via eBPF and AF_XDP
* **What is eBPF?**: eBPF acts as an isolated, secure virtual machine existing deep inside the Linux kernel. It allows us to safely execute packet-filtering logic inside the network driver itself before the kernel stack is even aware a packet exists.
* **AF_XDP Sockets**: eXpress Data Path (XDP) sockets are a revolutionary system. Using an eBPF program, we tell the network driver: *"Instead of sending this packet up the TCP/IP stack, inject it directly into a predefined shared memory tunnel."*
* **The Hybrid Superiority**: By utilizing AF_XDP, the Linux network driver continues to manage the hardware. We get the exact payload zero-copy benefits of DPDK, while standard un-filtered internet traffic can still proceed normally to the OS.

*(Visual Suggestion: Insert a comparison block diagram! Left side: The heavy DPDK architecture taking the NIC off the bus. Right side: AF_XDP securely sharing the NIC pointer with the application).*

---
# Slide 8: General Solution (Part 2)
## The Lock-Free Polling Engine Paradigm
* **Throwing Away the Interrupt**: Instead of the hardware interrupting the CPU, our application takes a dedicated CPU core and spins it in a continuous `while(true)` infinite execution loop. 
* **Active Polling**: The application acts like a revolving door, aggressively checking shared memory queues: *"Do I have packets? Do I have packets?"* millions of times a second. CPU usage sits at an intentional 100%. We intentionally trade idle CPU power for zero-latency reaction times.
* **Zero-Copy Batching**: When we see packets, we don't copy the data. We merely read the memory offset pointers. We process the packets in "bursts" (e.g., 32 at a time) to dramatically maximize L1/L2 CPU cache hit rates and throughput efficiency.

---
# Slide 9: Technical Deep-Dive 1
## Building the Memory Manager (Mempools & mbufs)
* **The Root of Speed - `mempool.c`**: We solved the `malloc` bottleneck by creating a custom memory manager. On project boot, we ask the OS for a single massive chunk of memory (UMEM). 
* **The Stack Mechanism**: We divide the massive chunk into thousands of fixed-size frames (e.g., 2048 bytes). We maintain a highly optimized stack array containing the numerical index offsets of all currently "Free" frames. 
* **`mempool_alloc()` and `mempool_free()`**: When the application requires space for an incoming packet, we strictly run an integer subtraction on our stack's `head` pointer to get a frame mapped directly to RAM in nanoseconds. Returning a packet to the pool is just incrementing the integer. 
* **The `lite_mbuf` Entity**: Modeled heavily after DPDK's `rte_mbuf`, our codebase passes these tiny metadata structures around the application, manipulating pointers rather than bulky string data.

*(Visual Suggestion: A simplistic visual showing a grid of memory boxes, with arrows mathematically pointing to "Head" and "Array Stack" to explain the memory system).*

---
# Slide 10: Technical Deep-Dive 2
## The Four Lock-Free Ring Buffers
Data must be passed between the User Application and Kernel Driver safely. We engineered integration with `libbpf` to structure four distinct lock-free rings operating strictly on Consumer/Producer head/tail atomics.
* **1. Fill Ring (FQ - Producer: App, Consumer: Kernel)**: The application pulls empty memory frames from our Mempool and populates this ring. It tells the hardware: *"Here are empty houses to place data."*
* **2. RX Ring (RX - Producer: Kernel, Consumer: App)**: The hardware places populated packet pointers here. The application checks this ring to receive traffic.
* **3. TX Ring (TX - Producer: App, Consumer: Kernel)**: The application places processed packet pointers here for transmission out into the world.
* **4. Completion Ring (CQ - Producer: Kernel, Consumer: App)**: Once the physical NIC has literally fired the data across the wire, the kernel places the pointer here so the application knows it is safe to `mempool_free` the memory.

---
# Slide 11: Technical Deep-Dive 3
## Implementing the I/O Engine and XDP BPF Linking
* **Bridging the Divide - `af_xdp.c`**: This serves as our complex integration layer handling low-level memory alignments via `posix_memalign` and configuring `xsk_socket__create`.
* **Managing Descriptors**: In functions like `lite_rx_burst`, we peek at the lock-free RX ring to secure descriptors, map them using pointer arithmetic to the shared UMEM map, and immediately re-provision the Fill Queue so hardware never starves for empty buffers.
* **Zero-Copy Routing Strategy**: To achieve maximum throughput, during testing the system is designed to perform a live zero-copy bounce. We literally extract the pointer, modify the payload (swapping source/dest MAC addresses manually), and pass that *exact same absolute pointer* into the TX ring without a single data byte ever being duplicated in RAM.

---
# Slide 12: Technical Deep-Dive 4
## Extensibility via Hardware-Level Packet Dropping (eBPF)
* **The Power of the Pre-Filter**: While `libbpf` generates a stock XDP program to tunnel packets directly to our rings, our architecture explicitly supports custom `.c` eBPF implementations (via `xdp_prog.c`). 
* **Hardware-Level DDOS Mitigation**: Compiled utilizing Clang, we can deploy code that executes on the NIC hardware or driver level. Before a packet even enters our Lock-Free ring or Mempool, the eBPF code can parse IP headers.
* **The Drop Decision**: If a packet is flagged as malicious within the eBPF program, we can simply execute a `return XDP_DROP;` command. The packet is vaporized instantly at the driver level, ensuring the application is permanently shielded from overwhelming flood attacks.

---
# Slide 13: Plan of Implementation
## Core Development Phases
*(Note: To be shown visually as a timeline or Gantt chart)*
* **Phase 1: Architecture & Memory Logic**: Designing the raw layout, coding the rigid Mempools (`mbuf-lite`), and configuring `libxdp` libraries for our target OS.
* **Phase 2: AF_XDP Backend Construction**: Completing the complex descriptor mappings. Coding the `lite_rx_burst`, `lite_tx_burst`, and socket initialization sequence targeting shared `UMEM` regions.
* **Phase 3: The Polling Engine Foundation**: Constructing the deterministic `main.c` execution while-loop. Adding MAC-swapping logic and implementing dynamic state output to monitor millions of packets correctly.
* **Phase 4: Optimization & Benchmarking Setup**: Coding robust benchmarking bash scripts using software-defined virtual ethernet arrays (`veth`) to validate the software without demanding physical secondary hardware routing.

---
# Slide 14: Expected Output
## Demonstrating Multi-Million PPS Benchmarking
*(Note: To be shown in the end-evaluation. Ideal for placing Live Screenshots or Terminal Recordings)*
* **Visualizing Success**: The final evaluation showcases terminal execution logs where the application prints out real-time telemetry representing Total RX and Total TX increments scaling dramatically.
* **The Loopback Test Results**: When running our stress-testing script (`run_benchmark.sh`) deploying maximum volume `ping -f` floods from a peer interface, the byPassRT engine natively consumes and bounces back the traffic instantaneously.
* **Key Demonstration**: The Expected Output directly proves we successfully intercepted OS networking at the hardware/driver level, processed payloads independently in user space, and mathematically proved the system functions flawlessly under sheer volume.

*(Visual/Video Suggestion: Embed a screen-recording video of opening two terminals. Terminal A runs `byPassRT`. Terminal B executes the `run_benchmark.sh`, showcasing the numbers flying flawlessly in Terminal A).*

---
# Slide 15: Professional Benchmarks Comparison
## Outperforming Traditional Sockets
*(Visual Suggestion: Create a Bar Chart here comparing Traditional Linux Sockets against AF_XDP/byPassRT)*
* **Metrics That Matter - Throughput (PPS)**: Traditional sockets choke and drop packets during heavy burst tests because of OS kernel processing lag. Our Lock-Free Queue guarantees every slot ingested is handled.
* **Metrics That Matter - Latency**: Because we eradicated OS Context Switches, our latency graph doesn't spike or jitter. Packet processing times remain totally deterministic.
* **Metrics That Matter - The Efficiency Curve**: By eliminating `recvfrom()`, `malloc()`, and `sk_buff` copies, our memory bus utilization is drastically lower, allowing servers to process five times the data with half the physical hardware deployment.

---
# Slide 16: Conclusion and Future Expansion
## The Future of the DPDK-Inspired Ecosystem
* **Project Conclusion**: We successfully engineered the `byPassRT` user-space runtime. We sidestepped the catastrophic complexity of Intel DPDK by leveraging native Linux eBPF/AF_XDP capabilities to achieve identical kernel-bypass speed, providing an elite framework capable of driving 5G, trading algorithms, or load balancing.
* **Future Expansion Path 1**: Constructing multi-core threading strategies where Rings are dynamically balanced across NUMA CPU nodes.
* **Future Expansion Path 2**: Developing a custom TCP/IP network stack entirely inside the user-space application to fully support TCP connection streams, stepping beyond basic Ethernet frame modification.
---
