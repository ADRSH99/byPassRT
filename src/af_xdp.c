#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <poll.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <xdp/xsk.h>
#include <linux/if_link.h>

#include "config.h"
#include "api.h"
#include "mempool.h"

struct xsk_umem_info
{
    struct xsk_ring_prod fq;
    struct xsk_ring_cons cq;
    struct xsk_umem *umem;
    void *buffer;
};

struct xsk_socket_info
{
    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;
    struct xsk_umem_info *umem;
    struct xsk_socket *xsk;
};

static struct xsk_socket_info *g_xsk = NULL;
static struct mempool *g_mempool = NULL;

static struct xsk_umem_info *configure_xsk_umem(void *buffer, uint64_t size)
{
    struct xsk_umem_info *umem;
    int ret;

    umem = calloc(1, sizeof(*umem));
    if (!umem)
        return NULL;

    ret = xsk_umem__create(&umem->umem, buffer, size, &umem->fq, &umem->cq, NULL);
    if (ret)
    {
        fprintf(stderr, "Error: xsk_umem__create failed: %s\n", strerror(-ret));
        free(umem);
        return NULL;
    }
    umem->buffer = buffer;
    return umem;
}

static struct xsk_socket_info *xsk_configure_socket(struct xsk_umem_info *umem, const char *ifname, uint32_t queue_id)
{
    struct xsk_socket_config xsk_cfg;
    struct xsk_socket_info *xsk_info;
    uint32_t idx;
    int ret;

    xsk_info = calloc(1, sizeof(*xsk_info));
    if (!xsk_info)
        return NULL;

    xsk_info->umem = umem;
    xsk_cfg.rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
    xsk_cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
    xsk_cfg.libbpf_flags = 0;
    xsk_cfg.xdp_flags = XDP_FLAGS_SKB_MODE; // Use SKB mode for compatibility/testing
    xsk_cfg.bind_flags = 0;

    ret = xsk_socket__create(&xsk_info->xsk, ifname, queue_id, umem->umem, &xsk_info->rx, &xsk_info->tx, &xsk_cfg);
    if (ret)
    {
        fprintf(stderr, "Error: xsk_socket__create failed: %s\n", strerror(-ret));
        free(xsk_info);
        return NULL;
    }

    // Populate fill ring
    ret = xsk_ring_prod__reserve(&umem->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS, &idx);
    if (ret != XSK_RING_PROD__DEFAULT_NUM_DESCS)
    {
        fprintf(stderr, "Error: Could not reserve entries in fill ring\n");
        return NULL;
    }
    for (int i = 0; i < XSK_RING_PROD__DEFAULT_NUM_DESCS; i++)
    {
        bool success;
        uint32_t offset = mempool_alloc(g_mempool, &success);
        if (!success)
        {
            fprintf(stderr, "Mempool alloc failed during init\n");
            return NULL;
        }
        *xsk_ring_prod__fill_addr(&umem->fq, idx++) = offset;
    }
    xsk_ring_prod__submit(&umem->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS);

    return xsk_info;
}

int lite_init(const char *ifname)
{
    void *bufs;

    // Allocate UMEM buffer
    if (posix_memalign(&bufs, getpagesize(), UMEM_SIZE))
    {
        fprintf(stderr, "Error: Could not allocate UMEM buffer\n");
        return -1;
    }

    g_mempool = mempool_init(NUM_FRAMES, FRAME_SIZE);
    if (!g_mempool)
    {
        fprintf(stderr, "Error: Could not allocate mempool\n");
        return -1;
    }

    struct xsk_umem_info *umem = configure_xsk_umem(bufs, UMEM_SIZE);
    if (!umem)
        return -1;

    g_xsk = xsk_configure_socket(umem, ifname, 0);
    if (!g_xsk)
        return -1;

    printf("AF_XDP socket initialized on %s (SKB mode)\n", ifname);
    return 0;
}

int lite_rx_burst(struct lite_mbuf **pkts, int max)
{
    uint32_t idx_rx;
    int rcvd;

    if (!g_xsk)
        return 0;

    rcvd = xsk_ring_cons__peek(&g_xsk->rx, max, &idx_rx);
    if (!rcvd)
        return 0;

    for (int i = 0; i < rcvd; i++)
    {
        const struct xdp_desc *desc = xsk_ring_cons__rx_desc(&g_xsk->rx, idx_rx++);
        struct lite_mbuf *pkt = malloc(sizeof(struct lite_mbuf)); // user space allocation
        if (!pkt)
            continue;

        uint64_t addr = desc->addr;
        uint32_t len = desc->len;

        // Obtain actual pointer into memory using offset
        pkt->data = (uint8_t *)g_xsk->umem->buffer + addr;
        pkt->data_len = len;
        pkt->buf_len = FRAME_SIZE;
        pkt->timestamp = 0;

        pkts[i] = pkt;
    }
    xsk_ring_cons__release(&g_xsk->rx, rcvd);

    // Replenish the fill ring with fresh buffers from mempool immediately
    uint32_t idx_fq;
    int ret = xsk_ring_prod__reserve(&g_xsk->umem->fq, rcvd, &idx_fq);
    while (ret != rcvd)
    {
        if (ret < 0)
            break;
        ret = xsk_ring_prod__reserve(&g_xsk->umem->fq, rcvd, &idx_fq);
    }
    if (ret == rcvd)
    {
        for (int i = 0; i < rcvd; i++)
        {
            bool success;
            uint32_t offset = mempool_alloc(g_mempool, &success);
            if (success)
            {
                *xsk_ring_prod__fill_addr(&g_xsk->umem->fq, idx_fq++) = offset;
            }
        }
        xsk_ring_prod__submit(&g_xsk->umem->fq, rcvd);
    }

    return rcvd;
}

int lite_tx_burst(struct lite_mbuf **pkts, int count)
{
    uint32_t tx_idx;
    int sent = 0;

    if (!g_xsk || count == 0)
        return 0;

    // Reserve TX slots
    sent = xsk_ring_prod__reserve(&g_xsk->tx, count, &tx_idx);
    if (sent == 0)
        return 0;

    for (int i = 0; i < sent; i++)
    {
        struct xdp_desc *desc = xsk_ring_prod__tx_desc(&g_xsk->tx, tx_idx++);
        // Determine offset from UMEM pointer
        uint64_t offset = (uint8_t *)pkts[i]->data - (uint8_t *)g_xsk->umem->buffer;

        desc->addr = offset;
        desc->len = pkts[i]->data_len;

        // Free the user-space metadata, as the packet is now owned by TX ring
        free(pkts[i]);
    }
    xsk_ring_prod__submit(&g_xsk->tx, sent);

    // Process completion ring
    uint32_t cq_idx;
    int completed = xsk_ring_cons__peek(&g_xsk->umem->cq, XSK_RING_CONS__DEFAULT_NUM_DESCS, &cq_idx);
    if (completed > 0)
    {
        for (int i = 0; i < completed; i++)
        {
            uint64_t addr = *xsk_ring_cons__comp_addr(&g_xsk->umem->cq, cq_idx++);
            mempool_free(g_mempool, addr);
        }
        xsk_ring_cons__release(&g_xsk->umem->cq, completed);
    }

    return sent;
}

void lite_free(struct lite_mbuf *pkt)
{
    if (!pkt)
        return;
    if (g_xsk && g_xsk->umem && g_mempool)
    {
        uint64_t offset = (uint8_t *)pkt->data - (uint8_t *)g_xsk->umem->buffer;
        mempool_free(g_mempool, offset);
    }
    free(pkt);
}
