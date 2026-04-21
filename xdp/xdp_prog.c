#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

SEC("xdp")
int custom_xdp_prog(struct xdp_md *ctx) {
    // Example: Pass everything to the AF_XDP socket.
    // In the future, you could parse Ethernet/IP headers here
    // and return XDP_DROP for malicious IPs instantly!
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
