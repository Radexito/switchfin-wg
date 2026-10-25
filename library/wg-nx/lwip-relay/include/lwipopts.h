/* SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 xlanor
 * This file is part of wg-nx. See COPYING and COPYING.LESSER.
 */

#ifndef LWIP_HDR_LWIPOPTS_H
#define LWIP_HDR_LWIPOPTS_H

#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0

#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (32 * 1024)
#define MEMP_NUM_PBUF                   32
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_TCP_PCB                16
#define MEMP_NUM_TCP_PCB_LISTEN         4
#define MEMP_NUM_TCP_SEG                32
#define PBUF_POOL_SIZE                  40
#define PBUF_POOL_BUFSIZE               1600

#define LWIP_SUPPORT_CUSTOM_PBUF        1

#define LWIP_TCP                        1
#define TCP_MSS                         1460
#define TCP_SND_BUF                     (8 * TCP_MSS)
#define TCP_SND_QUEUELEN                (4 * TCP_SND_BUF / TCP_MSS)
#define TCP_WND                         (8 * TCP_MSS)

#define LWIP_UDP                        1

#define LWIP_IPV4                       1
#define LWIP_IPV6                       0

#define LWIP_DHCP                       0
#define LWIP_DNS                        0
#define LWIP_ICMP                       1
#define LWIP_RAW                        0
#define LWIP_IGMP                       0
#define LWIP_ARP                        0
#define LWIP_ETHERNET                   0

#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_CHECK_IP               1
#define CHECKSUM_CHECK_UDP              1
#define CHECKSUM_CHECK_TCP              1

#define LWIP_STATS                      0
#define LWIP_DEBUG                      0

/* Heap sentinels: the process kept dying inside free() in code that has
 * nothing to do with the tunnel, which points at a stray write somewhere.
 * These make lwIP scream at the exact block instead of much later. */
#define MEM_SANITY_CHECK                1
#define MEM_OVERFLOW_CHECK              2
#define MEMP_OVERFLOW_CHECK             2

#endif
