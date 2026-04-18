#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <poll.h>

#include "config.h"
#include "api.h"

static volatile int keep_running = 1;

void handle_signal(int sig) {
    (void)sig;
    keep_running = 0;
}

// Simple function to swap MAC addresses for loopback test
static void swap_mac(uint8_t *data) {
    uint8_t tmp[6];
    memcpy(tmp, data, 6);
    memcpy(data, data + 6, 6);
    memcpy(data + 6, tmp, 6);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <ifname>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *ifname = argv[1];
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("Starting byPassRT on interface: %s\n", ifname);

    if (lite_init(ifname) < 0) {
        fprintf(stderr, "Failed to initialize AF_XDP backend\n");
        return EXIT_FAILURE;
    }

    struct lite_mbuf *pkts[BURST_SIZE];
    uint64_t total_rx = 0;
    uint64_t total_tx = 0;

    printf("Entering polling loop...\n");
    while (keep_running) {
        int rcvd = lite_rx_burst(pkts, BURST_SIZE);
        if (rcvd > 0) {
            total_rx += rcvd;
            
            // Loopback application: swap MACs and send them right back
            for (int i = 0; i < rcvd; i++) {
                if (pkts[i]->data_len >= 14) { // Minimum Ethernet frame
                    swap_mac(pkts[i]->data);
                }
            }

            int sent = lite_tx_burst(pkts, rcvd);
            total_tx += sent;
            
            // Free packets that failed to send
            for (int i = sent; i < rcvd; i++) {
                lite_free(pkts[i]);
            }
            
            printf("\rTotal RX: %lu, Total TX: %lu", total_rx, total_tx);
            fflush(stdout);
        }
    }

    printf("\nShutting down byPassRT...\n");
    printf("Total RX: %lu, Total TX: %lu\n", total_rx, total_tx);

    return 0;
}
