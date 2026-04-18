#ifndef BYPASS_API_H
#define BYPASS_API_H

#include <stdint.h>

struct lite_mbuf {
    uint8_t *data;
    uint16_t data_len;
    uint16_t buf_len;
    uint64_t timestamp;
};

int lite_init(const char *ifname);
int lite_rx_burst(struct lite_mbuf **pkts, int max);
int lite_tx_burst(struct lite_mbuf **pkts, int count);
void lite_free(struct lite_mbuf *pkt);

#endif // BYPASS_API_H
