
/***************************************************************************
 * tcpip.cc -- Various functions relating to low level TCP/IP handling,    *
 * including sending raw packets, routing, printing packets, reading from  *
 * libpcap, etc.                                                           *
 *                                                                         *
 ***********************IMPORTANT NMAP LICENSE TERMS************************
 *                                                                         *
 * The Nmap Security Scanner is (C) 1996-2010 Insecure.Com LLC. Nmap is    *
 * also a registered trademark of Insecure.Com LLC.  This program is free  *
 * software; you may redistribute and/or modify it under the terms of the  *
 * GNU General Public License as published by the Free Software            *
 * Foundation; Version 2 with the clarifications and exceptions described  *
 * below.  This guarantees your right to use, modify, and redistribute     *
 * this software under certain conditions.  If you wish to embed Nmap      *
 * technology into proprietary software, we sell alternative licenses      *
 * (contact sales@insecure.com).  Dozens of software vendors already       *
 * license Nmap technology such as host discovery, port scanning, OS       *
 * detection, and version detection.                                       *
 *                                                                         *
 * Note that the GPL places important restrictions on "derived works", yet *
 * it does not provide a detailed definition of that term.  To avoid       *
 * misunderstandings, we consider an application to constitute a           *
 * "derivative work" for the purpose of this license if it does any of the *
 * following:                                                              *
 * o Integrates source code from Nmap                                      *
 * o Reads or includes Nmap copyrighted data files, such as                *
 *   nmap-os-db or nmap-service-probes.                                    *
 * o Executes Nmap and parses the results (as opposed to typical shell or  *
 *   execution-menu apps, which simply display raw Nmap output and so are  *
 *   not derivative works.)                                                *
 * o Integrates/includes/aggregates Nmap into a proprietary executable     *
 *   installer, such as those produced by InstallShield.                   *
 * o Links to a library or executes a program that does any of the above   *
 *                                                                         *
 * The term "Nmap" should be taken to also include any portions or derived *
 * works of Nmap.  This list is not exclusive, but is meant to clarify our *
 * interpretation of derived works with some common examples.  Our         *
 * interpretation applies only to Nmap--we don't speak for other people's  *
 * GPL works.                                                              *
 *                                                                         *
 * If you have any questions about the GPL licensing restrictions on using *
 * Nmap in non-GPL works, we would be happy to help.  As mentioned above,  *
 * we also offer alternative license to integrate Nmap into proprietary    *
 * applications and appliances.  These contracts have been sold to dozens  *
 * of software vendors, and generally include a perpetual license as well  *
 * as providing for priority support and updates as well as helping to     *
 * fund the continued development of Nmap technology.  Please email        *
 * sales@insecure.com for further information.                             *
 *                                                                         *
 * As a special exception to the GPL terms, Insecure.Com LLC grants        *
 * permission to link the code of this program with any version of the     *
 * OpenSSL library which is distributed under a license identical to that  *
 * listed in the included COPYING.OpenSSL file, and distribute linked      *
 * combinations including the two. You must obey the GNU GPL in all        *
 * respects for all of the code used other than OpenSSL.  If you modify    *
 * this file, you may extend this exception to your version of the file,   *
 * but you are not obligated to do so.                                     *
 *                                                                         *
 * If you received these files with a written license agreement or         *
 * contract stating terms other than the terms above, then that            *
 * alternative license agreement takes precedence over these comments.     *
 *                                                                         *
 * Source is provided to this software because we believe users have a     *
 * right to know exactly what a program is going to do before they run it. *
 * This also allows you to audit the software for security holes (none     *
 * have been found so far).                                                *
 *                                                                         *
 * Source code also allows you to port Nmap to new platforms, fix bugs,    *
 * and add new features.  You are highly encouraged to send your changes   *
 * to nmap-dev@insecure.org for possible incorporation into the main       *
 * distribution.  By sending these changes to Fyodor or one of the         *
 * Insecure.Org development mailing lists, it is assumed that you are      *
 * offering the Nmap Project (Insecure.Com LLC) the unlimited,             *
 * non-exclusive right to reuse, modify, and relicense the code.  Nmap     *
 * will always be available Open Source, but this is important because the *
 * inability to relicense code has caused devastating problems for other   *
 * Free Software projects (such as KDE and NASM).  We also occasionally    *
 * relicense the code to third parties as discussed above.  If you wish to *
 * specify special license conditions of your contributions, just say so   *
 * when you send them.                                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful, but     *
 * WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       *
 * General Public License v2.0 for more details at                         *
 * http://www.gnu.org/licenses/gpl-2.0.html , or in the COPYING file       *
 * included with Nmap.                                                     *
 *                                                                         *
 ***************************************************************************/

/* $Id$ */
#ifdef WIN32
#include "nmap_winconfig.h"
#endif
#include "portreasons.h"
#include <dnet.h>
#include <net/if_arp.h>
#include "tcpip.h"
#include "NmapOps.h"
#include "Target.h"
#include "utils.h"

#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#if HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#if HAVE_UNISTD_H
/* #include <sys/unistd.h> */
#include <unistd.h>
#endif

#if HAVE_NET_IF_H
#ifndef NET_IF_H /* why doesn't OpenBSD do this? */
#include <net/if.h>
#define NET_IF_H
#endif
#endif

#if HAVE_NETINET_IF_ETHER_H
#ifndef NETINET_IF_ETHER_H
#include <netinet/if_ether.h>
#define NETINET_IF_ETHER_H
#endif /* NETINET_IF_ETHER_H */
#endif /* HAVE_NETINET_IF_ETHER_H */

extern NmapOps o;

#ifdef WIN32
#include "pcap-int.h"

void nmapwin_init();
void nmapwin_cleanup();
void nmapwin_list_interfaces();

int if2nameindex(int ifi);
#endif

static PacketCounter PktCt;

/* These two are for eth_open_cached() and eth_close_cached() */
static char etht_cache_device_name[64];
static eth_t *etht_cache_device = NULL;

void sethdrinclude(int sd) {
#ifdef IP_HDRINCL
  int one = 1;
  setsockopt(sd, IPPROTO_IP, IP_HDRINCL, (const char *) &one, sizeof(one));
#endif
}

void set_ipoptions(int sd, void *opts, size_t optslen) {
#ifdef IP_OPTIONS
  if (sd == -1)
    return;

  setsockopt(sd, IPPROTO_IP, IP_OPTIONS, (const char *) opts, optslen);
#endif
}

void set_ttl(int sd, int ttl) {
#ifdef IP_TTL
  if (sd == -1)
    return;

  setsockopt(sd, IPPROTO_IP, IP_TTL, (const char *) &ttl, sizeof ttl);
#endif
}

// Takes a protocol number like IPPROTO_TCP, IPPROTO_UDP, or
// IPPROTO_IP and returns a ascii representation (or "unknown" if it
// doesn't recognize the number).  If uppercase is true, the returned
// value will be in all uppercase letters.  You can skip this
// parameter to use lowercase.
const char *proto2ascii(u8 proto, bool uppercase) {
  switch (proto) {
  case IPPROTO_TCP:
    return uppercase ? "TCP" : "tcp";
    break;
  case IPPROTO_UDP:
    return uppercase ? "UDP" : "udp";
    break;
  case IPPROTO_SCTP:
    return uppercase ? "SCTP" : "sctp";
    break;
  case IPPROTO_IP:
    return uppercase ? "IP" : "ip";
    break;
  default:
    return uppercase ? "UNKNOWN" : "unknown";
  }

  return NULL; // Unreached
}

static char *ll2shortascii(unsigned long long bytes, char *buf, int buflen) {
  if (buflen < 2 || !buf)
    fatal("Bogus parameter passed to %s", __func__);

  if (bytes > 1000000) {
    Snprintf(buf, buflen, "%.3fMB", bytes / 1000000.0);
  } else if (bytes > 10000) {
    Snprintf(buf, buflen, "%.3fKB", bytes / 1000.0);
  } else {
    Snprintf(buf, buflen, "%uB", (unsigned int) bytes);
  }

  return buf;
}

/* Fill buf (up to buflen -- truncate if necessary but always
   terminate) with a short representation of the packet stats.
   Returns buf.  Aborts if there is a problem. */
char *getFinalPacketStats(char *buf, int buflen) {
  char sendbytesasc[16], recvbytesasc[16];

  if (buflen <= 10 || !buf)
    fatal("%s called with woefully inadequate parameters", __func__);

  Snprintf(buf, buflen,
#if WIN32
           "Raw packets sent: %I64u (%s) | Rcvd: %I64u (%s)",
#else
           "Raw packets sent: %llu (%s) | Rcvd: %llu (%s)",
#endif
           PktCt.sendPackets,
           ll2shortascii(PktCt.sendBytes, sendbytesasc,
                         sizeof(sendbytesasc)), PktCt.recvPackets,
           ll2shortascii(PktCt.recvBytes, recvbytesasc,
                         sizeof(recvbytesasc)));
  return buf;
}

/* Takes an ARP PACKET (not including ethernet header) and
   prints it if packet tracing is enabled. The
   direction must be PacketTrace::SENT or PacketTrace::RCVD .
   Optional 'now' argument makes this function slightly more
   efficient by avoiding a gettimeofday() call. */
void PacketTrace::traceArp(pdirection pdir, const u8 *frame, u32 len,
                           struct timeval *now) {
  struct timeval tv;
  char arpdesc[128];
  char who_has[INET_ADDRSTRLEN], tell[INET_ADDRSTRLEN];

  if (pdir == SENT) {
    PktCt.sendPackets++;
    PktCt.sendBytes += len;
  } else {
    PktCt.recvPackets++;
    PktCt.recvBytes += len;
  }

  if (!o.packetTrace())
    return;

  if (now)
    tv = *now;
  else
    gettimeofday(&tv, NULL);

  if (len < 28) {
    error("Packet tracer: Arp packets must be at least 28 bytes long.  Should be exactly that length excl. ethernet padding.");
    return;
  }

  if (frame[7] == 1) { /* arp REQUEST */
    inet_ntop(AF_INET, frame + 24, who_has, sizeof(who_has));
    inet_ntop(AF_INET, frame + 14, tell, sizeof(tell));
    Snprintf(arpdesc, sizeof(arpdesc), "who-has %s tell %s", who_has, tell);
  } else { /* ARP REPLY */
    inet_ntop(AF_INET, frame + 14, who_has, sizeof(who_has));
    Snprintf(arpdesc, sizeof(arpdesc),
             "reply %s is-at %02X:%02X:%02X:%02X:%02X:%02X", who_has,
             frame[8], frame[9], frame[10], frame[11], frame[12],
             frame[13]);
  }

  log_write(LOG_STDOUT | LOG_NORMAL, "%s (%.4fs) ARP %s\n",
            (pdir == SENT) ? "SENT" : "RCVD",
            o.TimeSinceStartMS(&tv) / 1000.0, arpdesc);

  return;
}

/* Get an ASCII information about a tcp option which is pointed by
   optp, with a length of len. The result is stored in the result
   buffer. The result may look like "<mss 1452,sackOK,timestamp
   45848914 0,nop,wscale 7>" */
static void tcppacketoptinfo(u8 *optp, int len, char *result, int bufsize) {
  assert(optp);
  assert(result);
  char *p, ch;
  u8 *q;
  int opcode;
  u16 tmpshort;
  u32 tmpword1, tmpword2;

  p = result;
  *p = '\0';
  q = optp;
  ch = '<';

  while (len > 0 && bufsize > 2) {
    Snprintf(p, bufsize, "%c", ch);
    bufsize--;
    p++;
    opcode = *q++;
    if (!opcode) { /* End of List */

      Snprintf(p, bufsize, "eol");
      bufsize -= strlen(p);
      p += strlen(p);

      len--;

    } else if (opcode == 1) { /* No Op */
      Snprintf(p, bufsize, "nop");
      bufsize -= strlen(p);
      p += strlen(p);

      len--;
    } else if (opcode == 2) { /* MSS */
      if (len < 4)
        break; /* MSS has 4 bytes */

      q++;
      memcpy(&tmpshort, q, 2);

      Snprintf(p, bufsize, "mss %u", ntohs(tmpshort));
      bufsize -= strlen(p);
      p += strlen(p);

      q += 2;
      len -= 4;
    } else if (opcode == 3) { /* Window Scale */
      if (len < 3)
        break; /* Window Scale option has 3 bytes */

      q++;

      Snprintf(p, bufsize, "wscale %u", *q);
      bufsize -= strlen(p);
      p += strlen(p);

      q++;
      len -= 3;
    } else if (opcode == 4) { /* SACK permitted */
      if (len < 2)
        break; /* SACK permitted option has 2 bytes */

      Snprintf(p, bufsize, "sackOK");
      bufsize -= strlen(p);
      p += strlen(p);

      q++;
      len -= 2;
    } else if (opcode == 5) { /* SACK */
      unsigned sackoptlen = *q;
      if ((unsigned) len < sackoptlen)
        break;

      /* This would break parsing, so it's best to just give up */
      if (sackoptlen < 2)
        break;

      q++;

      if ((sackoptlen - 2) == 0 || ((sackoptlen - 2) % 8 != 0)) {
        Snprintf(p, bufsize, "malformed sack");
        bufsize -= strlen(p);
        p += strlen(p);
      } else {
        Snprintf(p, bufsize, "sack %d ", (sackoptlen - 2) / 8);
        bufsize -= strlen(p);
        p += strlen(p);
        for (unsigned i = 0; i < sackoptlen - 2; i += 8) {
          memcpy(&tmpword1, q + i, 4);
          memcpy(&tmpword2, q + i + 4, 4);
          Snprintf(p, bufsize, "{%u:%u}", tmpword1, tmpword2);
          bufsize -= strlen(p);
          p += strlen(p);
        }
      }

      q += sackoptlen - 2;
      len -= sackoptlen;
    } else if (opcode == 8) { /* Timestamp */
      if (len < 10)
        break; /* Timestamp option has 10 bytes */

      q++;
      memcpy(&tmpword1, q, 4);
      memcpy(&tmpword2, q + 4, 4);

      Snprintf(p, bufsize, "timestamp %u %u", ntohl(tmpword1),
               ntohl(tmpword2));
      bufsize -= strlen(p);
      p += strlen(p);

      q += 8;
      len -= 10;
    }

    ch = ',';
  }

  if (len > 0) {
    *result = '\0';
    return;
  }

  Snprintf(p, bufsize, ">");
}

/* Returns a buffer of ASCII information about a packet that may look
   like "TCP 127.0.0.1:50923 > 127.0.0.1:3 S ttl=61 id=39516 iplen=40
   seq=625950769" or "ICMP PING (0/1) ttl=61 id=39516 iplen=40".
   Since this is a static buffer, don't use threads or call twice
   within (say) printf().  And certainly don't try to free() it!  The
   returned buffer is NUL-terminated */
static const char *ippackethdrinfo(const u8 *packet, u32 len) {
  static char protoinfo[512];
  struct ip *ip = (struct ip *) packet;
  struct tcp_hdr *tcp = NULL;
  struct udp_hdr *udp = NULL;
  struct sctp_hdr *sctp = NULL;
  char ipinfo[512];
  char srchost[INET6_ADDRSTRLEN], dsthost[INET6_ADDRSTRLEN];
  char *p;
  struct in_addr saddr, daddr;
  int frag_off = 0, more_fragments = 0;
  char fragnfo[64] = "";
  if (ip->ip_v != 4)
    return "BOGUS!  IP Version in packet is not 4";

  if (len < sizeof(struct ip))
    return "BOGUS!  Packet too short.";

  saddr.s_addr = ip->ip_src.s_addr;
  daddr.s_addr = ip->ip_dst.s_addr;

  inet_ntop(AF_INET, &saddr, srchost, sizeof(srchost));
  inet_ntop(AF_INET, &daddr, dsthost, sizeof(dsthost));

  frag_off = 8 * (ntohs(ip->ip_off) & IP_OFFMASK);
  more_fragments = ntohs(ip->ip_off) & IP_MF;
  if (frag_off || more_fragments) {
    Snprintf(fragnfo, sizeof(fragnfo), " frag offset=%d%s", frag_off, more_fragments ? "+" : "");
  }


  Snprintf(ipinfo, sizeof(ipinfo), "ttl=%d id=%d iplen=%d%s %s%s%s",
           ip->ip_ttl, ntohs(ip->ip_id), ntohs(ip->ip_len), fragnfo,
           ip->ip_hl == 5 ? "" : "ipopts={",
           ip->ip_hl == 5 ? "" : print_ip_options((u8 *) ip + sizeof(struct ip), MIN((unsigned) (ip->ip_hl - 5) * 4, len - sizeof(struct ip))),
           ip->ip_hl == 5 ? "" : "}");

  if (ip->ip_p == IPPROTO_TCP) {
    char tflags[10];
    char tcpinfo[64] = "";
    char buf[32];
    char tcpoptinfo[256] = "";

    tcp = (struct tcp_hdr *) (packet + ip->ip_hl * 4);
    if (frag_off > 8 || len < (u32) ip->ip_hl * 4 + 8)
      Snprintf(protoinfo, sizeof(protoinfo), "TCP %s:?? > %s:?? ?? %s (incomplete)", srchost, dsthost, ipinfo);
    else if (frag_off == 8) { // at least we can get TCP flags and ACKn
      tcp = (struct tcp_hdr *) ((u8 *) tcp - frag_off); // ugly?
      p = tflags;
      /* These are basically in tcpdump order */
      if (tcp->th_flags & TH_SYN)
        *p++ = 'S';
      if (tcp->th_flags & TH_FIN)
        *p++ = 'F';
      if (tcp->th_flags & TH_RST)
        *p++ = 'R';
      if (tcp->th_flags & TH_PUSH)
        *p++ = 'P';
      if (tcp->th_flags & TH_ACK) {
        *p++ = 'A';
        Snprintf(tcpinfo, sizeof(tcpinfo), " ack=%lu", (unsigned long) ntohl(tcp->th_ack));
      }
      if (tcp->th_flags & TH_URG)
        *p++ = 'U';
      if (tcp->th_flags & TH_ECE)
        *p++ = 'E'; /* rfc 2481/3168 */
      if (tcp->th_flags & TH_CWR)
        *p++ = 'C'; /* rfc 2481/3168 */
      *p++ = '\0';

      if ((u32) tcp->th_off * 4 > sizeof(struct tcp_hdr)) {
        // tcp options
        if (len < (u32) ip->ip_hl * 4 + (u32) tcp->th_off * 4 - frag_off) {
          Snprintf(tcpoptinfo, sizeof(tcpoptinfo), "option incomplete");

        } else {
          tcppacketoptinfo((u8 *) tcp + sizeof(struct tcp_hdr),
                           tcp->th_off * 4 - sizeof(struct tcp_hdr),
                           tcpoptinfo, sizeof(tcpoptinfo));
        }
      }

      Snprintf(protoinfo, sizeof(protoinfo),
               "TCP %s:?? > %s:?? %s %s %s %s", srchost, dsthost, tflags,
               ipinfo, tcpinfo, tcpoptinfo);
    } else if (len < (u32) ip->ip_hl * 4 + 16) { // we can get ports and seq
      Snprintf(tcpinfo, sizeof(tcpinfo), "seq=%lu (incomplete)",
               (unsigned long) ntohl(tcp->th_seq));
      Snprintf(protoinfo, sizeof(protoinfo), "TCP %s:%d > %s:%d ?? %s %s",
               srchost, ntohs(tcp->th_sport), dsthost,
               ntohs(tcp->th_dport), ipinfo, tcpinfo);
    } else { // at least first 16 bytes of TCP header are there
      Snprintf(tcpinfo, sizeof(tcpinfo), "seq=%lu win=%hu",
               (unsigned long) ntohl(tcp->th_seq), ntohs(tcp->th_win));
      p = tflags;
      /* These are basically in tcpdump order */
      if (tcp->th_flags & TH_SYN)
        *p++ = 'S';
      if (tcp->th_flags & TH_FIN)
        *p++ = 'F';
      if (tcp->th_flags & TH_RST)
        *p++ = 'R';
      if (tcp->th_flags & TH_PUSH)
        *p++ = 'P';
      if (tcp->th_flags & TH_ACK) {
        *p++ = 'A';
        Snprintf(buf, sizeof(buf), " ack=%lu",
                 (unsigned long) ntohl(tcp->th_ack));
        strncat(tcpinfo, buf, sizeof(tcpinfo) - strlen(tcpinfo) - 1);
      }
      if (tcp->th_flags & TH_URG)
        *p++ = 'U';
      if (tcp->th_flags & TH_ECE)
        *p++ = 'E'; /* rfc 2481/3168 */
      if (tcp->th_flags & TH_CWR)
        *p++ = 'C'; /* rfc 2481/3168 */
      *p++ = '\0';

      if ((u32) tcp->th_off * 4 > sizeof(struct tcp_hdr)) {
        // tcp options
        if (len < (u32) ip->ip_hl * 4 + (u32) tcp->th_off * 4) {
          Snprintf(tcpoptinfo, sizeof(tcpoptinfo), "option incomplete");

        } else {
          tcppacketoptinfo((u8 *) tcp + sizeof(struct tcp_hdr),
                           tcp->th_off * 4 - sizeof(struct tcp_hdr),
                           tcpoptinfo, sizeof(tcpoptinfo));
        }
      }

      Snprintf(protoinfo, sizeof(protoinfo),
               "TCP %s:%d > %s:%d %s %s %s %s", srchost,
               ntohs(tcp->th_sport), dsthost, ntohs(tcp->th_dport), tflags,
               ipinfo, tcpinfo, tcpoptinfo);
    }
  } else if (ip->ip_p == IPPROTO_UDP && frag_off) {
    Snprintf(protoinfo, sizeof(protoinfo),
             "UDP %s:?? > %s:?? fragment %s (incomplete)", srchost,
             dsthost, ipinfo);
  } else if (ip->ip_p == IPPROTO_UDP) {
    udp = (struct udp_hdr *) (packet + sizeof(struct ip));

    Snprintf(protoinfo, sizeof(protoinfo), "UDP %s:%d > %s:%d %s",
             srchost, ntohs(udp->uh_sport), dsthost, ntohs(udp->uh_dport),
             ipinfo);
  } else if (ip->ip_p == IPPROTO_SCTP && frag_off) {
    Snprintf(protoinfo, sizeof(protoinfo),
             "SCTP %s:?? > %s:?? fragment %s (incomplete)", srchost,
             dsthost, ipinfo);
  } else if (ip->ip_p == IPPROTO_SCTP) {
    sctp = (struct sctp_hdr *) (packet + sizeof(struct ip));

    Snprintf(protoinfo, sizeof(protoinfo), "SCTP %s:%d > %s:%d %s",
             srchost, ntohs(sctp->sh_sport), dsthost,
             ntohs(sctp->sh_dport), ipinfo);
  } else if (ip->ip_p == IPPROTO_ICMP && frag_off) {
    Snprintf(protoinfo, sizeof(protoinfo),
             "ICMP %s > %s fragment %s (incomplete)", srchost, dsthost,
             ipinfo);
  } else if (ip->ip_p == IPPROTO_ICMP) {
    char icmptype[128];
    char *ip2dst;
    struct ip *ip2;
    struct ppkt {
      unsigned char type;
      unsigned char code;
      unsigned short checksum;
      unsigned short id;
      unsigned short seq;
    } *ping = NULL;
    unsigned pktlen = (ip->ip_hl * 4) + sizeof(struct ppkt);
    if (pktlen > len)
      goto icmpbad;
    ping = (struct ppkt *) ((ip->ip_hl * 4) + (char *) ip);
    switch (ping->type) {
    case 0:
      strcpy(icmptype, "echo reply");
      break;
    case 3:
      ip2 = (struct ip *) ((char *) ip + (ip->ip_hl * 4) + 8);
      pktlen += (ip2->ip_hl * 4);
      if (pktlen > len)
        goto icmpbad;
      if (pktlen + 8 < len) {
        tcp = (struct tcp_hdr *) ((char *) ip2 + (ip2->ip_hl * 4));
        udp = (struct udp_hdr *) ((char *) ip2 + (ip2->ip_hl * 4));
        sctp = (struct sctp_hdr *) ((char *) ip2 + (ip2->ip_hl * 4));
      }
      ip2dst = inet_ntoa(ip2->ip_dst);
      switch (ping->code) {
      case 0:
        Snprintf(icmptype, sizeof icmptype, "network %s unreachable", ip2dst);
        break;
      case 1:
        Snprintf(icmptype, sizeof icmptype, "host %s unreachable", ip2dst);
        break;
      case 2:
        Snprintf(icmptype, sizeof icmptype, "protocol %u unreachable", ip2->ip_p);
        break;
      case 3:
        if (ip2->ip_p == IPPROTO_UDP && udp)
          Snprintf(icmptype, sizeof icmptype, "port %u unreachable", ntohs(udp->uh_dport));
        else if (ip2->ip_p == IPPROTO_TCP && tcp)
          Snprintf(icmptype, sizeof icmptype, "port %u unreachable", ntohs(tcp->th_dport));
        else if (ip2->ip_p == IPPROTO_SCTP && sctp)
          Snprintf(icmptype, sizeof icmptype, "port %u unreachable", ntohs(sctp->sh_dport));
        else
          strcpy(icmptype, "port unreachable");
        break;
      case 4:
        strcpy(icmptype, "fragmentation required");
        break;
      case 5:
        strcpy(icmptype, "source route failed");
        break;
      case 6:
        Snprintf(icmptype, sizeof icmptype, "destination network %s unknown", ip2dst);
        break;
      case 7:
        Snprintf(icmptype, sizeof icmptype, "destination host %s unknown", ip2dst);
        break;
      case 8:
        strcpy(icmptype, "source host isolated");
        break;
      case 9:
        Snprintf(icmptype, sizeof icmptype, "destination network %s administratively prohibited", ip2dst);
        break;
      case 10:
        Snprintf(icmptype, sizeof icmptype, "destination host %s administratively prohibited", ip2dst);
        break;
      case 11:
        Snprintf(icmptype, sizeof icmptype, "network %s unreachable for TOS", ip2dst);
        break;
      case 12:
        Snprintf(icmptype, sizeof icmptype, "host %s unreachable for TOS", ip2dst);
        break;
      case 13:
        strcpy(icmptype,
               "communication administratively prohibited by filtering");
        break;
      case 14:
        strcpy(icmptype, "host precedence violation");
        break;
      case 15:
        strcpy(icmptype, "precedence cutoff in effect");
        break;
      default:
        strcpy(icmptype, "unknown unreachable code");
        break;
      }
      break;
    case 4:
      strcpy(icmptype, "source quench");
      break;
    case 5:
      if (ping->code == 0)
        strcpy(icmptype, "network redirect");
      else if (ping->code == 1)
        strcpy(icmptype, "host redirect");
      else
        strcpy(icmptype, "unknown redirect");
      break;
    case 8:
      strcpy(icmptype, "echo request");
      break;
    case 11:
      if (ping->code == 0)
        strcpy(icmptype, "TTL=0 during transit");
      else if (ping->code == 1)
        strcpy(icmptype, "TTL=0 during reassembly");
      else
        strcpy(icmptype, "TTL exceeded (unknown code)");
      break;
    case 12:
      if (ping->code == 0)
        strcpy(icmptype, "IP header bad");
      else
        strcpy(icmptype, "Misc. parameter problem");
      break;
    case 13:
      strcpy(icmptype, "Timestamp request");
      break;
    case 14:
      strcpy(icmptype, "Timestamp reply");
      break;
    case 15:
      strcpy(icmptype, "Information request");
      break;
    case 16:
      strcpy(icmptype, "Information reply");
      break;
    case 17:
      strcpy(icmptype, "Address mask request");
      break;
    case 18:
      strcpy(icmptype, "Address mask reply");
      break;
    case 30:
      strcpy(icmptype, "Traceroute");
      break;
    case 37:
      strcpy(icmptype, "Domain name request");
      break;
    case 38:
      strcpy(icmptype, "Domain name reply");
      break;
    case 40:
      strcpy(icmptype, "Security failures"); /* rfc 2521 */
      break;
    default:
      strcpy(icmptype, "Unknown type");
      break;
      break;
    }
    if (pktlen > len) {
    icmpbad:
      if (ping) {
        /* We still have this information */
        Snprintf(protoinfo, sizeof(protoinfo),
                 "ICMP %s > %s (type=%d/code=%d) %s", srchost, dsthost,
                 ping->type, ping->code, ipinfo);
      } else {
        Snprintf(protoinfo, sizeof(protoinfo), "ICMP %s > %s [??] %s", srchost, dsthost, ipinfo);
      }
    } else {
      Snprintf(protoinfo, sizeof(protoinfo),
               "ICMP %s > %s %s (type=%d/code=%d) %s", srchost, dsthost,
               icmptype, ping->type, ping->code, ipinfo);
    }
  } else {
    Snprintf(protoinfo, sizeof(protoinfo),
             "Unknown protocol (%d) %s > %s: %s", ip->ip_p, srchost,
             dsthost, ipinfo);
  }

  return protoinfo;
}

  /* Takes an IP PACKET and prints it if packet tracing is enabled.
     'packet' must point to the IPv4 header. The direction must be
     PacketTrace::SENT or PacketTrace::RCVD .  Optional 'now' argument
     makes this function slightly more efficient by avoiding a gettimeofday()
     call. */
void PacketTrace::trace(pdirection pdir, const u8 *packet, u32 len,
                        struct timeval *now) {
  struct timeval tv;

  if (pdir == SENT) {
    PktCt.sendPackets++;
    PktCt.sendBytes += len;
  } else {
    PktCt.recvPackets++;
    PktCt.recvBytes += len;
  }

  if (!o.packetTrace())
    return;

  if (now)
    tv = *now;
  else
    gettimeofday(&tv, NULL);

  if (len < 20) {
    error("Packet tracer: tiny packet encountered");
    return;
  }

  log_write(LOG_STDOUT | LOG_NORMAL, "%s (%.4fs) %s\n",
            (pdir == SENT) ? "SENT" : "RCVD",
            o.TimeSinceStartMS(&tv) / 1000.0, ippackethdrinfo(packet, len));

  return;
}

/* Adds a trace entry when a connect() is attempted if packet tracing
   is enabled.  Pass IPPROTO_TCP or IPPROTO_UDP as the protocol.  The
   sock may be a sockaddr_in or sockaddr_in6.  The return code of
   connect is passed in connectrc.  If the return code is -1, get the
   errno and pass that as connect_errno. */
void PacketTrace::traceConnect(u8 proto, const struct sockaddr *sock,
                               int socklen, int connectrc,
                               int connect_errno,
                               const struct timeval *now) {
  struct sockaddr_in *sin = (struct sockaddr_in *) sock;
#if HAVE_IPV6
  struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) sock;
#endif
  struct timeval tv;
  char errbuf[64] = "";
  char targetipstr[INET6_ADDRSTRLEN] = "";
  u16 targetport = 0;

  if (!o.packetTrace())
    return;

  if (now)
    tv = *now;
  else
    gettimeofday(&tv, NULL);

  assert(proto == IPPROTO_TCP || proto == IPPROTO_UDP);

  if (connectrc == 0) {
    Strncpy(errbuf, "Connected", sizeof(errbuf));
  }
#if WIN32
  else if (connect_errno == WSAEWOULDBLOCK) {
    /* Special case for WSAEWOULDBLOCK. socket_strerror returns the unwieldy
       "A non-blocking socket operation could not be completed immediately." */
    Strncpy(errbuf, "Operation now in progress", sizeof(errbuf));
  }
#endif
  else {
    Snprintf(errbuf, sizeof(errbuf), "%s", socket_strerror(connect_errno));
  }

  if (sin->sin_family == AF_INET) {
    if (inet_ntop(sin->sin_family, (char *) &sin->sin_addr, targetipstr,
                  sizeof(targetipstr)) == NULL)
      fatal("Failed to convert target IPv4 address to presentation format!?!");
    targetport = ntohs(sin->sin_port);
  } else {
#if HAVE_IPV6
    assert(sin->sin_family == AF_INET6);
    if (inet_ntop(sin->sin_family, (char *) &sin6->sin6_addr, targetipstr,
                  sizeof(targetipstr)) == NULL)
      fatal("Failed to convert target IPv6 address to presentation format!?!");
    targetport = ntohs(sin6->sin6_port);
#else
    assert(0);
#endif
  }

  log_write(LOG_STDOUT | LOG_NORMAL,
            "CONN (%.4fs) %s localhost > %s:%d => %s\n",
            o.TimeSinceStartMS(&tv) / 1000.0,
            (proto == IPPROTO_TCP) ? "TCP" : "UDP", targetipstr,
            targetport, errbuf);
}

/* Converts an IP address given in a sockaddr_storage to an IPv4 or
   IPv6 IP address string.  Since a static buffer is returned, this is
   not thread-safe and can only be used once in calls like printf() */
const char *inet_socktop(struct sockaddr_storage *ss) {
  static char buf[INET6_ADDRSTRLEN];
  struct sockaddr_in *sin = (struct sockaddr_in *) ss;
#if HAVE_IPV6
  struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) ss;
#endif

  if (inet_ntop(sin->sin_family, (sin->sin_family == AF_INET) ?
                (char *) &sin->sin_addr :
#if HAVE_IPV6
                (char *) &sin6->sin6_addr,
#else
                (char *) NULL,
#endif /* HAVE_IPV6 */
                buf, sizeof(buf)) == NULL) {
    fatal("Failed to convert target address to presentation format in %s!?!  Error: %s", __func__, strerror(socket_errno()));
  }
  return buf;
}

/* Tries to resolve the given name (or literal IP) into a sockaddr
   structure.  The af should be PF_INET (for IPv4) or PF_INET6.  Returns 0
   if hostname cannot be resolved.  It is OK to pass in a sockaddr_in or 
   sockaddr_in6 casted to a sockaddr_storage as long as you use the matching 
   pf.*/
int resolve(char *hostname, struct sockaddr_storage *ss, size_t *sslen, int pf) {
  struct addrinfo hints;
  struct addrinfo *result;
  int rc;

  assert(ss);
  assert(sslen);
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = pf;
  rc = getaddrinfo(hostname, NULL, &hints, &result);
  if (rc != 0 || result == NULL)
    return 0;
  assert(result->ai_addrlen > 0
         && result->ai_addrlen <= (int) sizeof(struct sockaddr_storage));
  *sslen = result->ai_addrlen;
  memcpy(ss, result->ai_addr, *sslen);
  freeaddrinfo(result);
  return 1;
}

int islocalhost(const struct in_addr *const addr) {
  char dev[128];
  /* If it is 0.0.0.0 or starts with 127 then it is 
     probably localhost */
  if ((addr->s_addr & htonl(0xFF000000)) == htonl(0x7F000000))
    return 1;

  if (!addr->s_addr)
    return 1;

  /* If it is the same addy as a local interface, then it is
     probably localhost */
  if (ipaddr2devname(dev, addr) != -1)
    return 1;

  /* OK, so to a first approximation, this addy is probably not
     localhost */
  return 0;
}

int isipprivate(const struct in_addr *const addr) {
  char *ipc;
  unsigned char i1, i2;

  if (!addr)
    return 0;

  ipc = (char *) &(addr->s_addr);
  i1 = ipc[0];
  i2 = ipc[1];

  /* 10.0.0.0/8 */
  if (i1 == 10)
    return 1;

  /* 172.16.0.0/12 */
  if (i1 == 172 && i2 >= 16 && i2 <= 31)
    return 1;

  /* 192.168.0.0/16 */
  if (i1 == 192 && i2 == 168)
    return 1;

  return 0;
}

#ifdef WIN32
/* Convert a dnet interface name into the long pcap style.  This also caches the
   data to speed things up.  Fills out pcapdev (up to pcapdevlen) and returns
   true if it finds anything. Otherwise returns false.  This is only necessary
   on Windows. */
bool DnetName2PcapName(const char *dnetdev, char *pcapdev, int pcapdevlen) {
  static struct NameCorrelationCache {
    char dnetd[64];
    char pcapd[128];
  } *NCC = NULL;
  static int NCCsz = 0;
  static int NCCcapacity = 0;
  int i;
  char tmpdev[128];

  // Init the cache if not done yet
  if (!NCC) {
    NCCcapacity = 5;
    NCC =
        (struct NameCorrelationCache *) safe_zalloc(NCCcapacity *
                                                    sizeof(*NCC));
    NCCsz = 0;
  }
  // First check if the name is already in the cache
  for (i = 0; i < NCCsz; i++) {
    if (strcmp(NCC[i].dnetd, dnetdev) == 0) {
      Strncpy(pcapdev, NCC[i].pcapd, pcapdevlen);
      return true;
    }
  }

  // OK, so it isn't in the cache.  Let's ask dnet for it.
/* Converts a dnet interface name (ifname) to its pcap equivalent, which is stored in
pcapdev (up to a length of pcapdevlen).  Returns 0 and fills in pcapdev if successful. */
  if (eth_get_pcap_devname(dnetdev, tmpdev, sizeof(tmpdev)) != 0)
    return false;

  // We've got it.  Let's add it to the cache
  if (NCCsz >= NCCcapacity) {
    NCCcapacity <<= 2;
    NCC =
        (struct NameCorrelationCache *) safe_realloc(NCC,
                                                     NCCcapacity *
                                                     sizeof(*NCC));
  }
  Strncpy(NCC[NCCsz].dnetd, dnetdev, sizeof(NCC[0].dnetd));
  Strncpy(NCC[NCCsz].pcapd, tmpdev, sizeof(NCC[0].pcapd));
  NCCsz++;
  Strncpy(pcapdev, tmpdev, pcapdevlen);
  return true;
}
#endif

pcap_t *my_pcap_open_live(const char *device, int snaplen, int promisc, int to_ms) {
  char err0r[PCAP_ERRBUF_SIZE];
  pcap_t *pt;
  char pcapdev[128];
  int failed = 0;

  assert(device != NULL);

#ifdef WIN32
  /* Nmap normally uses device names obtained through dnet for interfaces, but
     Pcap has its own naming system.  So the conversion is done here */
  if (!DnetName2PcapName(device, pcapdev, sizeof(pcapdev))) {
    /* Oh crap -- couldn't find the corresponding dev apparently.  Let's just go
       with what we have then ... */
    Strncpy(pcapdev, device, sizeof(pcapdev));
  }
#else
  Strncpy(pcapdev, device, sizeof(pcapdev));
#endif
  do {
    pt = pcap_open_live(pcapdev, snaplen, promisc, to_ms, err0r);
    if (!pt) {
      failed++;
      if (failed >= 3) {
        fatal("Call to pcap_open_live(%s, %d, %d, %d) failed three times. Reported error: %s\nThere are several possible reasons for this, depending on your operating system:\n"
          "LINUX: If you are getting Socket type not supported, try modprobe af_packet or recompile your kernel with SOCK_PACKET enabled.\n"
          "*BSD:  If you are getting device not configured, you need to recompile your kernel with Berkeley Packet Filter support.  If you are getting No such file or directory, try creating the device (eg cd /dev; MAKEDEV <device>; or use mknod).\n"
          "*WINDOWS:  Nmap only supports ethernet interfaces on Windows for most operations because Microsoft disabled raw sockets as of Windows XP SP2.  Depending on the reason for this error, it is possible that the --unprivileged command-line argument will help.\n"
          "SOLARIS:  If you are trying to scan localhost or the address of an interface and are getting '/dev/lo0: No such file or directory' or 'lo0: No DLPI device found', complain to Sun.  I don't think Solaris can support advanced localhost scans.  You can probably use \"-Pn -sT localhost\" though.\n\n",
             pcapdev, snaplen, promisc, to_ms, err0r);
      } else {
        error("pcap_open_live(%s, %d, %d, %d) FAILED. Reported error: %s.  Will wait %d seconds then retry.", pcapdev, snaplen, promisc, to_ms, err0r, (int) pow(5.0, failed));
      }
      sleep((int) pow(5.0, failed));
    }
  } while (!pt);

#ifdef WIN32
  /* We want any responses back ASAP */
  pcap_setmintocopy(pt, 1);
#endif

  return pt;
}

/* Standard BSD internet checksum routine. Uses libdnet helper functions. */
unsigned short in_cksum(u16 *ptr, int nbytes) {
  int sum;

  sum = ip_cksum_add(ptr, nbytes, 0);

  return ip_cksum_carry(sum);
}

/* For computing TCP/UDP checksums, see RFC 1071 and TCP/IP Illustrated
   sections 3.2, 11.3, and 17.3. */
unsigned short magic_tcpudp_cksum(const struct in_addr *src,
                                  const struct in_addr *dst,
                                  u8 proto, u16 len, const void *hstart) {
  struct pseudo {
    struct in_addr src;
    struct in_addr dst;
    u8 zero;
    u8 proto;
    u16 length;
  } hdr;
  int sum;

  hdr.src = *src;
  hdr.dst = *dst;
  hdr.zero = 0;
  hdr.proto = proto;
  hdr.length = htons(len);

  /* Get the ones'-complement sum of the pseudo-header. */
  sum = ip_cksum_add(&hdr, sizeof(hdr), 0);
  /* Add it to the sum of the packet. */
  sum = ip_cksum_add(hstart, len, sum);

  /* Fold in the carry, take the complement, and return. */
  return ip_cksum_carry(sum);
}

/* LEGACY resolve() function that only supports IPv4 -- see IPv6 version
   above.  Tries to resolve given hostname and stores
   result in ip .  returns 0 if hostname cannot
   be resolved */
int resolve(char *hostname, struct in_addr *ip) {
  struct hostent *h;

  if (!hostname || !*hostname)
    fatal("NULL or zero-length hostname passed to %s()", __func__);

  if (inet_pton(AF_INET, hostname, ip))
    return 1; /* damn, that was easy ;) */
  if ((h = gethostbyname(hostname))) {
    memcpy(ip, h->h_addr_list[0], sizeof(struct in_addr));
    return 1;
  }
  return 0;
}

/* A simple function that caches the eth_t from dnet for one device,
   to avoid opening, closing, and re-opening it thousands of tims.  If
   you give a different device, this function will close the first
   one.  Thus this should never be used by programs that need to deal
   with multiple devices at once.  In addition, you MUST NEVER
   eth_close() A DEVICE OBTAINED FROM THIS FUNCTION.  Instead, you can
   call eth_close_cached() to close whichever device (if any) is
   cached.  Returns NULL if it fails to open the device. */
eth_t *eth_open_cached(const char *device) {
  if (!device)
    fatal("%s() called with NULL device name!", __func__);
  if (!*device)
    fatal("%s() called with empty device name!", __func__);

  if (strcmp(device, etht_cache_device_name) == 0) {
    /* Yay, we have it cached. */
    return etht_cache_device;
  }

  if (*etht_cache_device_name) {
    eth_close(etht_cache_device);
    etht_cache_device_name[0] = '\0';
    etht_cache_device = NULL;
  }

  etht_cache_device = eth_open(device);
  if (etht_cache_device)
    Strncpy(etht_cache_device_name, device,
            sizeof(etht_cache_device_name));

  return etht_cache_device;
}

/* See the description for eth_open_cached */
void eth_close_cached() {
  if (etht_cache_device) {
    eth_close(etht_cache_device);
    etht_cache_device = NULL;
    etht_cache_device_name[0] = '\0';
  }
  return;
}

// fill ip header. no error check.
// This function is also changing what's needed from host to network order.
static inline int fill_ip_raw(struct ip *ip, int packetlen, const u8 *ipopt,
                              int ipoptlen, int ip_tos, int ip_id,
                              int ip_off, int ip_ttl, int ip_p,
                              const struct in_addr *ip_src,
                              const struct in_addr *ip_dst) {
  ip->ip_v = 4;
  ip->ip_hl = 5 + (ipoptlen / 4);
  ip->ip_tos = ip_tos;
  ip->ip_len = htons(packetlen);
  ip->ip_id = htons(ip_id);
  ip->ip_off = htons(ip_off);
  ip->ip_ttl = ip_ttl;
  ip->ip_p = ip_p;
  ip->ip_src.s_addr = ip_src->s_addr;
  ip->ip_dst.s_addr = ip_dst->s_addr;

  if (ipoptlen)
    memcpy((u8 *) ip + sizeof(struct ip), ipopt, ipoptlen);

  // ip options source routing hack:
  if (ipoptlen && o.ipopt_firsthop && o.ipopt_lasthop) {
    u8 *ipo = (u8 *) ip + sizeof(struct ip);
    struct in_addr *newdst = (struct in_addr *) &ipo[o.ipopt_firsthop];
    struct in_addr *olddst = (struct in_addr *) &ipo[o.ipopt_lasthop];
    // our destination is somewhere else :)
    ip->ip_dst.s_addr = newdst->s_addr;

    // and last hop should be destination
    olddst->s_addr = ip_dst->s_addr;
  }

#if HAVE_IP_IP_SUM
  ip->ip_sum = 0;
  ip->ip_sum =
      in_cksum((unsigned short *) ip, sizeof(struct ip) + ipoptlen);
#endif
  return (sizeof(struct ip) + ipoptlen);
}



int send_tcp_raw_decoys(int sd, struct eth_nfo *eth,
                        const struct in_addr *victim,
                        int ttl, bool df,
                        u8 *ipopt, int ipoptlen,
                        u16 sport, u16 dport,
                        u32 seq, u32 ack, u8 reserved, u8 flags,
                        u16 window, u16 urp, u8 *options, int optlen,
                        char *data, u16 datalen) {
  int decoy;

  for (decoy = 0; decoy < o.numdecoys; decoy++)
    if (send_tcp_raw(sd, eth,
                     &o.decoys[decoy], victim,
                     ttl, df,
                     ipopt, ipoptlen,
                     sport, dport,
                     seq, ack, reserved, flags, window, urp,
                     options, optlen, data, datalen) == -1)
      return -1;

  return 0;
}

/* Builds a TCP packet (including an IP header) by packing the fields
   with the given information.  It allocates a new buffer to store the
   packet contents, and then returns that buffer.  The packet is not
   actually sent by this function.  Caller must delete the buffer when
   finished with the packet.  The packet length is returned in
   packetlen, which must be a valid int pointer. */
u8 *build_tcp_raw(const struct in_addr *source,
                  const struct in_addr *victim, int ttl, u16 ipid, u8 tos,
                  bool df, const u8 *ipopt, int ipoptlen, u16 sport, u16 dport,
                  u32 seq, u32 ack, u8 reserved, u8 flags, u16 window,
                  u16 urp, const u8 *tcpopt, int tcpoptlen, char *data,
                  u16 datalen, u32 *outpacketlen) {
  int packetlen = sizeof(struct ip) + ipoptlen + sizeof(struct tcp_hdr) + tcpoptlen + datalen;
  u8 *packet = (u8 *) safe_malloc(packetlen);
  struct ip *ip = (struct ip *) packet;
  struct tcp_hdr *tcp = (struct tcp_hdr *) ((u8 *) ip + sizeof(struct ip) + ipoptlen);
  static int myttl = 0;

  assert(victim);
  assert(source);
  assert(ipoptlen % 4 == 0);

  if (tcpoptlen % 4)
    fatal("%s() called with an option length argument of %d which is illegal because it is not divisible by 4. Just add \\0 padding to the end.", __func__, tcpoptlen);


  /* Time to live */
  if (ttl == -1) {
    myttl = (get_random_uint() % 23) + 37;
  } else {
    myttl = ttl;
  }

  /* Fill tcp header */
  memset(tcp, 0, sizeof(struct tcp_hdr));
  tcp->th_sport = htons(sport);
  tcp->th_dport = htons(dport);
  if (seq) {
    tcp->th_seq = htonl(seq);
  } else if (flags & TH_SYN) {
    get_random_bytes(&(tcp->th_seq), 4);
  }

  if (ack)
    tcp->th_ack = htonl(ack);

  if (reserved)
    tcp->th_x2 = reserved & 0x0F;
  tcp->th_off = 5 + (tcpoptlen / 4) /*words */ ;
  tcp->th_flags = flags;

  if (window)
    tcp->th_win = htons(window);
  else
    tcp->th_win = htons(1024 * (myttl % 4 + 1)); /* Who cares */

  /* Urgent pointer */
  if (urp)
    tcp->th_urp = htons(urp);

  /* And the options */
  if (tcpoptlen)
    memcpy((u8 *) tcp + sizeof(struct tcp_hdr), tcpopt, tcpoptlen);
  /* We should probably copy the data over too */
  if (data && datalen)
    memcpy((u8 *) tcp + sizeof(struct tcp_hdr) + tcpoptlen, data, datalen);

#if STUPID_SOLARIS_CHECKSUM_BUG
  tcp->th_sum = sizeof(struct tcp_hdr) + tcpoptlen + datalen;
#else
  tcp->th_sum = magic_tcpudp_cksum(source, victim, IPPROTO_TCP,
                                   sizeof(struct tcp_hdr) + tcpoptlen +
                                   datalen, (char *) tcp);
#endif

  if (o.badsum)
    --tcp->th_sum;

  fill_ip_raw(ip, packetlen, ipopt, ipoptlen,
              tos, ipid, df ? IP_DF : 0, myttl, IPPROTO_TCP,
              source, victim);

  *outpacketlen = packetlen;
  return packet;
}

/* You need to call sethdrinclude(sd) on the sending sd before calling this */
int send_tcp_raw(int sd, struct eth_nfo *eth,
                 const struct in_addr *source,
                 const struct in_addr *victim, int ttl, bool df,
                 u8 *ipops, int ipoptlen, u16 sport, u16 dport, u32 seq,
                 u32 ack, u8 reserved, u8 flags, u16 window, u16 urp,
                 u8 *options, int optlen, char *data, u16 datalen) {
  unsigned int packetlen;
  int res = -1;

  u8 *packet = build_tcp_raw(source, victim,
                             ttl, get_random_u16(), IP_TOS_DEFAULT, df,
                             ipops, ipoptlen,
                             sport, dport,
                             seq, ack, reserved, flags, window, urp,
                             options, optlen,
                             data, datalen, &packetlen);
  if (!packet)
    return -1;
  res = send_ip_packet(sd, eth, packet, packetlen);

  free(packet);
  return res;
}

/* Create and send all fragments of a pre-built IPv4 packet
 * Minimal MTU for IPv4 is 68 and maximal IPv4 header size is 60
 * which gives us a right to cut TCP header after 8th byte
 * (shouldn't we inflate the header to 60 bytes too?) */
int send_frag_ip_packet(int sd, struct eth_nfo *eth, u8 *packet,
                        unsigned int packetlen, unsigned int mtu) {
  struct ip *ip = (struct ip *) packet;
  int headerlen = ip->ip_hl * 4; // better than sizeof(struct ip)
  unsigned int datalen = packetlen - headerlen;
  int fdatalen = 0, res = 0;

  assert(headerlen <= (int) packetlen);
  assert(headerlen >= 20 && headerlen <= 60); // sanity check (RFC791)
  assert(mtu > 0 && mtu % 8 == 0); // otherwise, we couldn't set Fragment offset (ip->ip_off) correctly

  if (datalen <= mtu) {
    error("Warning: fragmentation (mtu=%i) requested but the payload is too small already (%i)", mtu, datalen);
    return send_ip_packet(sd, eth, packet, packetlen);
  }

  u8 *fpacket = (u8 *) safe_malloc(headerlen + mtu);
  memcpy(fpacket, packet, headerlen + mtu);
  ip = (struct ip *) fpacket;

  // create fragments and send them
  for (int fragment = 1; fragment * mtu < datalen + mtu; fragment++) {
    fdatalen = (fragment * mtu <= datalen ? mtu : datalen % mtu);
    ip->ip_len = htons(headerlen + fdatalen);
    ip->ip_off = htons((fragment - 1) * mtu / 8);
    if ((fragment - 1) * mtu + fdatalen < datalen)
      ip->ip_off |= htons(IP_MF);
#if HAVE_IP_IP_SUM
    ip->ip_sum = 0;
    ip->ip_sum = in_cksum((unsigned short *) ip, headerlen);
#endif
    if (fragment > 1) // copy data payload
      memcpy(fpacket + headerlen,
             packet + headerlen + (fragment - 1) * mtu, fdatalen);
    res = send_ip_packet(sd, eth, fpacket, headerlen + fdatalen);
    if (res == -1)
      break;
  }
  free(fpacket);
  return res;
}

static int Sendto(const char *functionname, int sd,
                  const unsigned char *packet, int len, unsigned int flags,
                  struct sockaddr *to, int tolen) {

  struct sockaddr_in *sin = (struct sockaddr_in *) to;
  int res;
  int retries = 0;
  int sleeptime = 0;
  static int numerrors = 0;

  do {
    if ((res = sendto(sd, (const char *) packet, len, flags, to, tolen)) == -1) {
      int err = socket_errno();

      numerrors++;
      if (o.debugging > 1 || numerrors <= 10) {
        error("sendto in %s: sendto(%d, packet, %d, 0, %s, %d) => %s",
              functionname, sd, len, inet_ntoa(sin->sin_addr), tolen,
              strerror(err));
        error("Offending packet: %s", ippackethdrinfo(packet, len));
        if (numerrors == 10) {
          error("Omitting future %s error messages now that %d have been shown.  Use -d2 if you really want to see them.", __func__, numerrors);
        }
      }
#if WIN32
      return -1;
#else
      if (retries > 2 || err == EPERM || err == EACCES
          || err == EADDRNOTAVAIL || err == EINVAL)
        return -1;
      sleeptime = 15 * (1 << (2 * retries));
      error("Sleeping %d seconds then retrying", sleeptime);
      fflush(stderr);
      sleep(sleeptime);
#endif
    }
    retries++;
  } while (res == -1);

  return res;
}

static int send_ip_packet_eth(struct eth_nfo *eth, u8 *packet, unsigned int packetlen);
static int send_ip_packet_sd(int sd, u8 *packet, unsigned int packetlen);

/* Send a pre-built IPv4 packet. Handles fragmentation and whether to send with
   an ethernet handle or a socket. */
int send_ip_packet(int sd, struct eth_nfo *eth, u8 *packet,
                   unsigned int packetlen) {
  struct ip *ip = (struct ip *) packet;
  int res;

  assert(packet);
  assert((int) packetlen > 0);

  // fragmentation requested && packet is bigger than MTU
  if (o.fragscan && (packetlen - ip->ip_hl * 4 > (unsigned int) o.fragscan))
    return send_frag_ip_packet(sd, eth, packet, packetlen, o.fragscan);

  if (eth)
    res = send_ip_packet_eth(eth, packet, packetlen);
  else
    res = send_ip_packet_sd(sd, packet, packetlen);

  PacketTrace::trace(PacketTrace::SENT, packet, packetlen);

  return res;
}

/* Send an IP packet over an ethernet handle. */
static int send_ip_packet_eth(struct eth_nfo *eth, u8 *packet, unsigned int packetlen) {
  eth_t *ethsd;
  u8 *eth_frame;
  int res;

  eth_frame = (u8 *) safe_malloc(14 + packetlen);
  memcpy(eth_frame + 14, packet, packetlen);
  eth_pack_hdr(eth_frame, eth->dstmac, eth->srcmac, ETH_TYPE_IP);
  if (!eth->ethsd) {
    ethsd = eth_open_cached(eth->devname);
    if (!ethsd)
      fatal("%s: Failed to open ethernet device (%s)", __func__, eth->devname);
  } else {
    ethsd = eth->ethsd;
  }
  res = eth_send(ethsd, eth_frame, 14 + packetlen);
  /* No need to close ethsd due to caching */
  free(eth_frame);

  return res;
}

/* Send an IP packet over a raw socket. */
static int send_ip_packet_sd(int sd, u8 *packet, unsigned int packetlen) {
  struct sockaddr_in sock;
  struct ip *ip = (struct ip *) packet;
  struct tcp_hdr *tcp;
  struct udp_hdr *udp;
  int res;

  assert(sd >= 0);
  memset(&sock, 0, sizeof(sock));
  sock.sin_family = AF_INET;
#if HAVE_SOCKADDR_SA_LEN
  sock.sin_len = sizeof(sock);
#endif

  /* It is bogus that I need the address and port info when sending a RAW IP 
     packet, but it doesn't seem to work w/o them */
  if (packetlen >= 20) {
    sock.sin_addr.s_addr = ip->ip_dst.s_addr;
    if (ip->ip_p == IPPROTO_TCP
        && packetlen >= (unsigned int) ip->ip_hl * 4 + 20) {
      tcp = (struct tcp_hdr *) ((u8 *) ip + ip->ip_hl * 4);
      sock.sin_port = tcp->th_dport;
    } else if (ip->ip_p == IPPROTO_UDP
               && packetlen >= (unsigned int) ip->ip_hl * 4 + 8) {
      udp = (struct udp_hdr *) ((u8 *) ip + ip->ip_hl * 4);
      sock.sin_port = udp->uh_dport;
    }
  }

  /* Equally bogus is that the IP total len and IP fragment offset
     fields need to be in host byte order on certain BSD variants.  I
     must deal with it here rather than when building the packet,
     because they should be in NBO when I'm sending over raw
     ethernet */
#if FREEBSD || BSDI || NETBSD || DEC || MACOSX
  ip->ip_len = ntohs(ip->ip_len);
  ip->ip_off = ntohs(ip->ip_off);
#endif

  res = Sendto("send_ip_packet", sd, packet, packetlen, 0,
               (struct sockaddr *) &sock,
               (int) sizeof(struct sockaddr_in));

  /* Undo the byte order switching. */
#if FREEBSD || BSDI || NETBSD || DEC || MACOSX
  ip->ip_len = htons(ip->ip_len);
  ip->ip_off = htons(ip->ip_off);
#endif

  return res;
}

/* Builds an ICMP packet (including an IP header) by packing the fields
   with the given information.  It allocates a new buffer to store the
   packet contents, and then returns that buffer.  The packet is not
   actually sent by this function.  Caller must delete the buffer when
   finished with the packet.  The packet length is returned in
   packetlen, which must be a valid int pointer.  The id/seq will be converted
   to network byte order (if it differs from HBO) */
u8 *build_icmp_raw(const struct in_addr *source,
                   const struct in_addr *victim, int ttl, u16 ipid,
                   u8 tos, bool df, u8 *ipopt, int ipoptlen, u16 seq,
                   unsigned short id, u8 ptype, u8 pcode, char *data,
                   u16 datalen, u32 *packetlen) {
  struct ppkt {
    u8 type;
    u8 code;
    u16 checksum;
    u16 id;
    u16 seq;
    u8 data[1500]; /* Note -- first 4-12 bytes can be used for ICMP header */
  } pingpkt;
  u8 *datastart = pingpkt.data;
  /* dlen is the amount of space remaining in the data buffer; it may be reduced
     depending on type. */
  int dlen = sizeof(pingpkt.data);
  int icmplen = 0;
  char *ping = (char *) &pingpkt;

  pingpkt.type = ptype;
  pingpkt.code = pcode;

  if (ptype == 8) {
    /* echo request */
    icmplen = 8;
  } else if (ptype == 13 && pcode == 0) {
    /* ICMP timestamp req */
    icmplen = 20;
    memset(datastart, 0, 12);
    datastart += 12;
    dlen -= 12;
  } else if (ptype == 17 && pcode == 0) {
    /* icmp netmask req */
    icmplen = 12;
    memset(datastart, 0, 4);
    datastart += 4;
    dlen -= 4;
  } else {
    fatal("Unknown icmp type/code (%d/%d) in %s", ptype, pcode, __func__);
  }

  if (datalen > 0) {
    icmplen += MIN(dlen, datalen);
    memset(datastart, 0, MIN(dlen, datalen));
  }

  /* Fill out the ping packet */
  pingpkt.id = htons(id);
  pingpkt.seq = htons(seq);
  pingpkt.checksum = 0;
  pingpkt.checksum = in_cksum((unsigned short *) ping, icmplen);

  if (o.badsum)
    --pingpkt.checksum;

  return build_ip_raw(source, victim, IPPROTO_ICMP, ttl, ipid, tos, df,
                      ipopt, ipoptlen, ping, icmplen, packetlen);
}

/* Builds an IGMP packet (including an IP header) by packing the fields
   with the given information.  It allocates a new buffer to store the
   packet contents, and then returns that buffer.  The packet is not
   actually sent by this function.  Caller must delete the buffer when
   finished with the packet.  The packet length is returned in packetlen,
   which must be a valid int pointer. */
u8 *build_igmp_raw(const struct in_addr *source,
                   const struct in_addr *victim, int ttl, u16 ipid,
                   u8 tos, bool df, u8 *ipopt, int ipoptlen, u8 ptype,
                   u8 pcode, char *data, u16 datalen, u32 *packetlen) {
  struct {
    u8 igmp_type;
    u8 igmp_code;
    u16 igmp_cksum;
    u32 var; /* changes between types, unused. usually group address. */
    u8 data[1500];
  } igmp;
  u32 *datastart = (u32 *) igmp.data;
  int dlen = sizeof(igmp.data);
  int igmplen = 0;
  char *pkt = (char *) &igmp;

  igmp.igmp_type = ptype;
  igmp.igmp_code = pcode;

  if (ptype == 0x11) {
    /* Membership Query */
    igmplen = 8;
  } else if (ptype == 0x12) {
    /* v1 Membership Report */
    igmplen = 8;
  } else if (ptype == 0x16) {
    /* v2 Membership Report */
    igmplen = 8;
  } else if (ptype == 0x17) {
    /* v2 Leave Group */
    igmplen = 8;
  } else if (ptype == 0x22) {
    /* v3 Membership Report */
    igmplen = 8;
  } else {
    fatal("Unknown igmp type (%d) in %s", ptype, __func__);
  }

  if (datalen > 0) {
    igmplen += MIN(dlen, datalen);
    memset(datastart, 0, MIN(dlen, datalen));
  }

  igmp.igmp_cksum = 0;
  igmp.igmp_cksum = in_cksum((unsigned short *) pkt, igmplen);

  if (o.badsum)
    --igmp.igmp_cksum;

  return build_ip_raw(source, victim, IPPROTO_IGMP, ttl, ipid, tos, df,
                      ipopt, ipoptlen, pkt, igmplen, packetlen);
}


/* A simple function I wrote to help in debugging, shows the important fields
   of a TCP packet*/
int readtcppacket(const u8 *packet, int readdata) {

  struct ip *ip = (struct ip *) packet;
  struct tcp_hdr *tcp = (struct tcp_hdr *) (packet + sizeof(struct ip));
  const unsigned char *data =
      packet + sizeof(struct ip) + sizeof(struct tcp_hdr);
  int tot_len;
  struct in_addr bullshit, bullshit2;
  char sourcehost[16];
  int i;
  int realfrag = 0;

  if (!packet) {
    error("%s: packet is NULL!", __func__);
    return -1;
  }

  bullshit.s_addr = ip->ip_src.s_addr;
  bullshit2.s_addr = ip->ip_dst.s_addr;
/* this is gay */
  realfrag = htons(ntohs(ip->ip_off) & IP_OFFMASK);
  tot_len = htons(ip->ip_len);
  strncpy(sourcehost, inet_ntoa(bullshit), 16);
  i = 4 * (ntohs(ip->ip_hl) + ntohs(tcp->th_off));
  if (ip->ip_p == IPPROTO_TCP) {
    if (realfrag)
      log_write(LOG_PLAIN, "Packet is fragmented, offset field: %u\n",
                realfrag);
    else {
      log_write(LOG_PLAIN,
                "TCP packet: %s:%d -> %s:%d (total: %d bytes)\n",
                sourcehost, ntohs(tcp->th_sport), inet_ntoa(bullshit2),
                ntohs(tcp->th_dport), tot_len);
      log_write(LOG_PLAIN, "Flags: ");
      if (!tcp->th_flags)
        log_write(LOG_PLAIN, "(none)");
      if (tcp->th_flags & TH_RST)
        log_write(LOG_PLAIN, "RST ");
      if (tcp->th_flags & TH_SYN)
        log_write(LOG_PLAIN, "SYN ");
      if (tcp->th_flags & TH_ACK)
        log_write(LOG_PLAIN, "ACK ");
      if (tcp->th_flags & TH_PUSH)
        log_write(LOG_PLAIN, "PSH ");
      if (tcp->th_flags & TH_FIN)
        log_write(LOG_PLAIN, "FIN ");
      if (tcp->th_flags & TH_URG)
        log_write(LOG_PLAIN, "URG ");
      log_write(LOG_PLAIN, "\n");

      log_write(LOG_PLAIN, "ipid: %hu ttl: %hu ", ntohs(ip->ip_id),
                ip->ip_ttl);

      if (tcp->th_flags & (TH_SYN | TH_ACK))
        log_write(LOG_PLAIN, "Seq: %u\tAck: %u\n",
                  (unsigned int) ntohl(tcp->th_seq),
                  (unsigned int) ntohl(tcp->th_ack));
      else if (tcp->th_flags & TH_SYN)
        log_write(LOG_PLAIN, "Seq: %u\n",
                  (unsigned int) ntohl(tcp->th_seq));
      else if (tcp->th_flags & TH_ACK)
        log_write(LOG_PLAIN, "Ack: %u\n",
                  (unsigned int) ntohl(tcp->th_ack));
    }
  }
  if (readdata && i < tot_len) {
    log_write(LOG_PLAIN, "Data portion:\n");
    while (i < tot_len) {
      log_write(LOG_PLAIN, "%2X%c", data[i], ((i + 1) % 16) ? ' ' : '\n');
      i++;
    }
    log_write(LOG_PLAIN, "\n");
  }

  return 0;
}

/* A simple function I wrote to help in debugging, shows the important fields
   of a UDP packet*/
int readudppacket(const u8 *packet, int readdata) {
  struct ip *ip = (struct ip *) packet;
  struct udp_hdr *udp = (struct udp_hdr *) (packet + sizeof(struct ip));
  const unsigned char *data = packet + sizeof(struct ip) + sizeof(struct udp_hdr);
  int tot_len;
  struct in_addr bullshit, bullshit2;
  char sourcehost[16];
  int i;
  int realfrag = 0;

  if (!packet) {
    error("%s: packet is NULL!", __func__);
    return -1;
  }

  bullshit.s_addr = ip->ip_src.s_addr;
  bullshit2.s_addr = ip->ip_dst.s_addr;
  /* this is gay */
  realfrag = htons(ntohs(ip->ip_off) & IP_OFFMASK);
  tot_len = htons(ip->ip_len);
  strncpy(sourcehost, inet_ntoa(bullshit), 16);
  i = 4 * (ntohs(ip->ip_hl)) + 8;
  if (ip->ip_p == IPPROTO_UDP) {
    if (realfrag)
      log_write(LOG_PLAIN, "Packet is fragmented, offset field: %u\n",
                realfrag);
    else {
      log_write(LOG_PLAIN,
                "UDP packet: %s:%d -> %s:%d (total: %d bytes)\n",
                sourcehost, ntohs(udp->uh_sport), inet_ntoa(bullshit2),
                ntohs(udp->uh_dport), tot_len);

      log_write(LOG_PLAIN, "ttl: %hu ", ip->ip_ttl);
    }
  }
  if (readdata && i < tot_len) {
    log_write(LOG_PLAIN, "Data portion:\n");
    while (i < tot_len) {
      log_write(LOG_PLAIN, "%2X%c", data[i], ((i + 1) % 16) ? ' ' : '\n');
      i++;
    }
    log_write(LOG_PLAIN, "\n");
  }
  return 0;
}

int send_udp_raw_decoys(int sd, struct eth_nfo *eth,
                        const struct in_addr *victim,
                        int ttl, u16 ipid,
                        u8 *ipops, int ipoptlen,
                        u16 sport, u16 dport, char *data, u16 datalen) {
  int decoy;

  for (decoy = 0; decoy < o.numdecoys; decoy++)
    if (send_udp_raw(sd, eth, &o.decoys[decoy], victim,
                     ttl, ipid, ipops, ipoptlen,
                     sport, dport, data, datalen) == -1)
      return -1;

  return 0;
}


/* Builds a UDP packet (including an IP header) by packing the fields
   with the given information.  It allocates a new buffer to store the
   packet contents, and then returns that buffer.  The packet is not
   actually sent by this function.  Caller must delete the buffer when
   finished with the packet.  The packet length is returned in
   packetlen, which must be a valid int pointer. */
u8 *build_udp_raw(const struct in_addr *source, const struct in_addr *victim,
                  int ttl, u16 ipid, u8 tos, bool df,
                  u8 *ipopt, int ipoptlen,
                  u16 sport, u16 dport,
                  const char *data, u16 datalen, u32 *outpacketlen) {
  int packetlen =
      sizeof(struct ip) + ipoptlen + sizeof(struct udp_hdr) + datalen;
  u8 *packet = (u8 *) safe_malloc(packetlen);
  struct ip *ip = (struct ip *) packet;
  struct udp_hdr *udp =
      (struct udp_hdr *) ((u8 *) ip + sizeof(struct ip) + ipoptlen);
  static int myttl = 0;

  /* check that required fields are there and not too silly */
  assert(victim);
  assert(source);
  assert(ipoptlen % 4 == 0);

  /* Time to live */
  if (ttl == -1) {
    myttl = (get_random_uint() % 23) + 37;
  } else {
    myttl = ttl;
  }

  udp->uh_sport = htons(sport);
  udp->uh_dport = htons(dport);
  udp->uh_sum = 0;
  udp->uh_ulen = htons(sizeof(struct udp_hdr) + datalen);

  /* We should probably copy the data over too */
  if (data)
    memcpy((u8 *) udp + sizeof(struct udp_hdr), data, datalen);

  /* OK, now we should be able to compute a valid checksum */
#if STUPID_SOLARIS_CHECKSUM_BUG
  udp->uh_sum = sizeof(struct udp_hdr) + datalen;
#else
  udp->uh_sum = magic_tcpudp_cksum(source, victim, IPPROTO_UDP,
                                   sizeof(struct udp_hdr) + datalen,
                                   (char *) udp);
#endif

  if (o.badsum) {
    --udp->uh_sum;
    if (udp->uh_sum == 0)
      udp->uh_sum = 0xffff; // UDP checksum=0 means no checksum
  }

  fill_ip_raw(ip, packetlen, ipopt, ipoptlen,
              tos, ipid, df ? IP_DF : 0, myttl, IPPROTO_UDP,
              source, victim);

  *outpacketlen = packetlen;
  return packet;
}

int send_udp_raw(int sd, struct eth_nfo *eth,
                 struct in_addr *source, const struct in_addr *victim,
                 int ttl, u16 ipid,
                 u8 *ipopt, int ipoptlen,
                 u16 sport, u16 dport, char *data, u16 datalen) {
  unsigned int packetlen;
  int res = -1;
  u8 *packet = build_udp_raw(source, victim,
                             ttl, ipid, IP_TOS_DEFAULT, false,
                             ipopt, ipoptlen,
                             sport, dport,
                             data, datalen, &packetlen);
  if (!packet)
    return -1;
  res = send_ip_packet(sd, eth, packet, packetlen);

  free(packet);
  return res;
}

/* Builds an SCTP packet (including an IP header) by packing the fields
   with the given information.  It allocates a new buffer to store the
   packet contents, and then returns that buffer.  The packet is not
   actually sent by this function.  Caller must delete the buffer when
   finished with the packet.  The packet length is returned in
   packetlen, which must be a valid int pointer. */
u8 *build_sctp_raw(const struct in_addr *source,
                   const struct in_addr *victim, int ttl, u16 ipid,
                   u8 tos, bool df, u8 *ipopt, int ipoptlen, u16 sport,
                   u16 dport, u32 vtag, char *chunks, int chunkslen,
                   char *data, u16 datalen, u32 *outpacketlen) {
  int packetlen =
      sizeof(struct ip) + ipoptlen + sizeof(struct sctp_hdr) + chunkslen +
      datalen;
  u8 *packet = (u8 *) safe_malloc(packetlen);
  struct ip *ip = (struct ip *) packet;
  struct sctp_hdr *sctp =
      (struct sctp_hdr *) ((u8 *) ip + sizeof(struct ip) + ipoptlen);
  static int myttl = 0;

  /* check that required fields are there and not too silly */
  assert(victim);
  assert(source);
  assert(ipoptlen % 4 == 0);

  /* Time to live */
  if (ttl == -1) {
    myttl = (get_random_uint() % 23) + 37;
  } else {
    myttl = ttl;
  }

  sctp->sh_sport = htons(sport);
  sctp->sh_dport = htons(dport);
  sctp->sh_sum = 0;
  sctp->sh_vtag = htonl(vtag);

  if (chunks)
    memcpy((u8 *) sctp + sizeof(struct sctp_hdr), chunks, chunkslen);

  if (data)
    memcpy((u8 *) sctp + sizeof(struct sctp_hdr) + chunkslen, data,
           datalen);

  /* RFC 2960 originally defined Adler32 checksums, which was later
   * revised to CRC32C in RFC 3309 and RFC 4960 respectively.
   * Nmap uses CRC32C by default, unless --adler32 is given. */
  if (o.adler32)
    sctp->sh_sum = htonl(nbase_adler32((unsigned char *) sctp,
                                       sizeof(struct sctp_hdr) +
                                       chunkslen + datalen));
  else
    sctp->sh_sum = htonl(nbase_crc32c((unsigned char *) sctp,
                                      sizeof(struct sctp_hdr) + chunkslen +
                                      datalen));

  if (o.badsum)
    --sctp->sh_sum;

  fill_ip_raw(ip, packetlen, ipopt, ipoptlen,
              tos, ipid, df ? IP_DF : 0, myttl, IPPROTO_SCTP,
              source, victim);

  *outpacketlen = packetlen;
  return packet;
}

/* Builds an IP packet (including an IP header) by packing the fields
   with the given information.  It allocates a new buffer to store the
   packet contents, and then returns that buffer.  The packet is not
   actually sent by this function.  Caller must delete the buffer when
   finished with the packet.  The packet length is returned in
   packetlen, which must be a valid int pointer. */
u8 *build_ip_raw(const struct in_addr *source,
                 const struct in_addr *victim, u8 proto, int ttl,
                 u16 ipid, u8 tos, bool df, u8 *ipopt, int ipoptlen,
                 char *data, u16 datalen, u32 *outpacketlen) {
  int packetlen = sizeof(struct ip) + ipoptlen + datalen;
  u8 *packet = (u8 *) safe_malloc(packetlen);
  struct ip *ip = (struct ip *) packet;
  static int myttl = 0;

/* check that required fields are there and not too silly */
  assert(source);
  assert(victim);
  assert(ipoptlen % 4 == 0);

/* Time to live */
  if (ttl == -1) {
    myttl = (get_random_uint() % 23) + 37;
  } else {
    myttl = ttl;
  }

  fill_ip_raw(ip, packetlen, ipopt, ipoptlen,
              tos, ipid, df ? IP_DF : 0, myttl, proto, source, victim);

  /* We should probably copy the data over too */
  if (data)
    memcpy((u8 *) ip + sizeof(struct ip) + ipoptlen, data, datalen);

  *outpacketlen = packetlen;
  return packet;
}

/* You need to call sethdrinclude(sd) on the sending sd before calling this */
int send_ip_raw(int sd, struct eth_nfo *eth,
                struct in_addr *source, const struct in_addr *victim,
                u8 proto, int ttl,
                u8 *ipopt, int ipoptlen, char *data, u16 datalen) {
  unsigned int packetlen;
  int res = -1;

  u8 *packet = build_ip_raw(source, victim,
                            proto,
                            ttl, get_random_u16(), IP_TOS_DEFAULT, false,
                            ipopt, ipoptlen,
                            data, datalen, &packetlen);
  if (!packet)
    return -1;

  res = send_ip_packet(sd, eth, packet, packetlen);

  free(packet);
  return res;
}

/* returns -1 if we can't use select() on the pcap device, 0 for timeout, and
 * >0 for success. If select() fails we bail out because it couldn't work with
 * the file descriptor we got from my_pcap_get_selectable_fd()
 */
int pcap_select(pcap_t *p, struct timeval *timeout) {
  int fd, ret;
  fd_set rfds;

  if ((fd = my_pcap_get_selectable_fd(p)) == -1)
    return -1;

  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);

  do {
    errno = 0;
    ret = select(fd + 1, &rfds, NULL, NULL, timeout);
    if (ret == -1) {
      if (errno == EINTR)
        error("%s: %s", __func__, strerror(errno));
      else
        fatal("Your system does not support select()ing on pcap devices (%s). PLEASE REPORT THIS ALONG WITH DETAILED SYSTEM INFORMATION TO THE nmap-dev MAILING LIST!", strerror(errno));
    }
  } while (ret == -1);

  return ret;
}

int pcap_select(pcap_t *p, long usecs) {
  struct timeval tv;

  tv.tv_sec = usecs / 1000000;
  tv.tv_usec = usecs % 1000000;

  return pcap_select(p, &tv);
}

/* Used by validatepkt() to validate the TCP header (including option lengths).
   The options checked are MSS, WScale, SackOK, Sack, and Timestamp. */
static bool validateTCPhdr(u8 *tcpc, unsigned len) {
  struct tcp_hdr *tcp = (struct tcp_hdr *) tcpc;
  unsigned hdrlen, optlen;

  hdrlen = tcp->th_off * 4;

  /* Check header length */
  if (hdrlen > len || hdrlen < sizeof(struct tcp_hdr))
    return false;

  /* Get to the options */
  tcpc += sizeof(struct tcp_hdr);
  optlen = hdrlen - sizeof(struct tcp_hdr);

  while (optlen > 0) {
    switch (*tcpc) {
    case 2: /* MSS */
      if (optlen < 4)
        return false;
      optlen -= 4;
      tcpc += 4;
      break;
    case 3: /* Window Scale */
      if (optlen < 3)
        return false;
      optlen -= 3;
      tcpc += 3;
      break;
    case 4: /* SACK Permitted */
      if (optlen < 2)
        return false;
      optlen -= 2;
      tcpc += 2;
      break;
    case 5: /* SACK */
      if (optlen < *++tcpc)
        return false;
      if (!(*tcpc - 2) || ((*tcpc - 2) % 8))
        return false;
      optlen -= *tcpc;
      tcpc += (*tcpc - 1);
      break;
    case 8: /* Timestamp */
      if (optlen < 10)
        return false;
      optlen -= 10;
      tcpc += 10;
      break;
    default:
      optlen--;
      tcpc++;
      break;
    }
  }

  return true;
}

/* Used by readip_pcap() to validate an IP packet.  It checks to make sure:
 *
 * 1) there is enough room for an IP header in the amount of bytes read
 * 2) the IP version number is correct
 * 3) the IP length fields are at least as big as the standard header
 * 4) the IP packet received isn't a fragment, or is the initial fragment
 * 5) that next level headers seem reasonable (e.g. validateTCPhdr())
 *
 * Checking the IP total length (iplen) to see if its at least as large as the
 * number of bytes read (len) does not work because things like the Ethernet
 * CRC also get captured and are counted in len.  However, since the IP total
 * length field can't be trusted, we use len instead of iplen when doing any
 * further checks on lengths.  readip_pcap fixes the length on it's end if we
 * read more than the IP header says we should have so as to not pass garbage
 * data to the caller.
 */
static bool validatepkt(u8 *ipc, unsigned len) {
  struct ip *ip = (struct ip *) ipc;
  unsigned fragoff, iphdrlen, iplen;

  if (len < sizeof(struct ip)) {
    if (o.debugging >= 3)
      error("Rejecting tiny, supposed IP packet (size %u)", len);
    return false;
  }

  if (ip->ip_v != 4) {
    if (o.debugging >= 3)
      error("Rejecting IP packet because of invalid version number %u", ip->ip_v);
    return false;
  }

  iphdrlen = ip->ip_hl * 4;

  if (iphdrlen < sizeof(struct ip)) {
    if (o.debugging >= 3)
      error("Rejecting IP packet because of invalid header length %u", iphdrlen);
    return false;
  }

  iplen = ntohs(ip->ip_len);

  if (iplen < iphdrlen) {
    if (o.debugging >= 3)
      error("Rejecting IP packet because of invalid total length %u", iplen);
    return false;
  }

  fragoff = 8 * (ntohs(ip->ip_off) & IP_OFFMASK);

  if (fragoff) {
    if (o.debugging >= 3)
      error("Rejecting IP fragment (offset %u)", fragoff);
    return false;
  }

  switch (ip->ip_p) {
  case IPPROTO_TCP:
    if (iphdrlen + sizeof(struct tcp_hdr) > len) {
      if (o.debugging >= 3)
        error("Rejecting TCP packet because of incomplete header");
      return false;
    }
    if (!validateTCPhdr(ipc + iphdrlen, len - iphdrlen)) {
      if (o.debugging >= 3)
        error("Rejecting TCP packet because of bad TCP header");
      return false;
    }
    break;
  case IPPROTO_UDP:
    if (iphdrlen + sizeof(struct udp_hdr) < len)
      break;
    if (o.debugging >= 3)
      error("Rejecting UDP packet because of incomplete header");
    return false;
  default:
    break;
  }

  return true;
}

/* Read an IP packet using libpcap .  We return the packet and take
   a pcap descriptor and a pointer to the packet length (which we set
   in the function. If you want a maximum length returned, you
   should specify that in pcap_open_live() */
/* to_usec is the timeout period in microseconds -- use 0 to skip the
   test and -1 to block forever.  Note that we don't interrupt pcap, so
   low values (and 0) degenerate to the timeout specified 
   in pcap_open_live() */
/* If rcvdtime is non-null and a packet is returned, rcvd will be
   filled with the time that packet was captured from the wire by
   pcap.  If linknfo is not NULL, linknfo->headerlen and
   linknfo->header will be filled with the appropriate values. */
/* Specifying true for validate will enable validity checks against the
   received IP packet.  See validatepkt() for a list of checks. */
char *readip_pcap(pcap_t *pd, unsigned int *len, long to_usec,
                  struct timeval *rcvdtime, struct link_header *linknfo,
                  bool validate) {
  unsigned int offset = 0;
  struct pcap_pkthdr head;
  char *p;
  int datalink;
  int timedout = 0;
  struct timeval tv_start, tv_end;
  static char *alignedbuf = NULL;
  static unsigned int alignedbufsz = 0;
  static int warning = 0;
  struct ip *iphdr;

  if (linknfo) {
    memset(linknfo, 0, sizeof(*linknfo));
  }

  if (!pd)
    fatal("NULL packet device passed to %s", __func__);

  if (to_usec < 0) {
    if (!warning) {
      warning = 1;
      error("WARNING: Negative timeout value (%lu) passed to %s() -- using 0", to_usec, __func__);
    }
    to_usec = 0;
  }

/* New packet capture device, need to recompute offset */
  if ((datalink = pcap_datalink(pd)) < 0)
    fatal("Cannot obtain datalink information: %s", pcap_geterr(pd));

  /* NOTE: IF A NEW OFFSET EVER EXCEEDS THE CURRENT MAX (24), ADJUST
     MAX_LINK_HEADERSZ in tcpip.h */
  switch (datalink) {
  case DLT_EN10MB:
    offset = 14;
    break;
  case DLT_IEEE802:
    offset = 22;
    break;
#ifdef __amigaos__
  case DLT_MIAMI:
    offset = 16;
    break;
#endif
#ifdef DLT_LOOP
  case DLT_LOOP:
#endif
  case DLT_NULL:
    offset = 4;
    break;
  case DLT_SLIP:
#ifdef DLT_SLIP_BSDOS
  case DLT_SLIP_BSDOS:
#endif
#if (FREEBSD || OPENBSD || NETBSD || BSDI || MACOSX)
    offset = 16;
#else
    offset = 24; /* Anyone use this??? */
#endif
    break;
  case DLT_PPP:
#ifdef DLT_PPP_BSDOS
  case DLT_PPP_BSDOS:
#endif
#ifdef DLT_PPP_SERIAL
  case DLT_PPP_SERIAL:
#endif
#ifdef DLT_PPP_ETHER
  case DLT_PPP_ETHER:
#endif
#if (FREEBSD || OPENBSD || NETBSD || BSDI || MACOSX)
    offset = 4;
#else
#ifdef SOLARIS
    offset = 8;
#else
    offset = 24; /* Anyone use this? */
#endif /* ifdef solaris */
#endif /* if freebsd || openbsd || netbsd || bsdi */
    break;
  case DLT_RAW:
    offset = 0;
    break;
  case DLT_FDDI:
    offset = 21;
    break;
#ifdef DLT_ENC
  case DLT_ENC:
    offset = 12;
    break;
#endif /* DLT_ENC */
#ifdef DLT_LINUX_SLL
  case DLT_LINUX_SLL:
    offset = 16;
    break;
#endif
  default:
    p = (char *) pcap_next(pd, &head);
    if (head.caplen == 0) {
      /* Lets sleep a brief time and try again to increase the chance of seeing
         a real packet ... */
      usleep(500000);
      p = (char *) pcap_next(pd, &head);
    }
    if (head.caplen > 100000) {
      fatal("FATAL: %s: bogus caplen from libpcap (%d) on interface type %d", __func__, head.caplen, datalink);
    }
    error("FATAL:  Unknown datalink type (%d). Caplen: %d; Packet:", datalink, head.caplen);
    nmap_hexdump((unsigned char *) p, head.caplen);
    exit(1);
  }

  if (to_usec > 0) {
    gettimeofday(&tv_start, NULL);
  }

  do {
#ifdef WIN32
    long to_left;

    if (to_usec > 0) {
      gettimeofday(&tv_end, NULL);
      to_left = MAX(1, (to_usec - TIMEVAL_SUBTRACT(tv_end, tv_start)) / 1000);
    } else {
      to_left = 1;
    }
    // Set the timeout (BUGBUG: this is cheating)
    PacketSetReadTimeout(pd->adapter, to_left);
#endif

    p = NULL;

    if (pcap_select(pd, to_usec) == 0)
      timedout = 1;
    else
      p = (char *) pcap_next(pd, &head);

    if (p) {
      if (head.caplen <= offset) {
        *len = 0;
        return NULL;
      }
      if (offset && linknfo) {
        linknfo->datalinktype = datalink;
        linknfo->headerlen = offset;
        assert(offset < MAX_LINK_HEADERSZ);
        memcpy(linknfo->header, p, MIN(sizeof(linknfo->header), offset));
      }
      p += offset;
    }
    if (!p || (*p & 0xF0) != 0x40) {
      /* Should we timeout? */
      if (to_usec == 0) {
        timedout = 1;
      } else if (to_usec > 0) {
        gettimeofday(&tv_end, NULL);
        if (TIMEVAL_SUBTRACT(tv_end, tv_start) >= to_usec) {
          timedout = 1;
        }
      }
    }
  } while (!timedout && (!p || (*p & 0xF0) != 0x40)); /* Go until we get IPv4 packet */

  if (timedout) {
    *len = 0;
    return NULL;
  }
  *len = head.caplen - offset;
  if (*len > alignedbufsz) {
    alignedbuf = (char *) safe_realloc(alignedbuf, *len);
    alignedbufsz = *len;
  }
  memcpy(alignedbuf, p, *len);

  if (validate) {
    /* Let's see if this packet passes inspection.. */
    if (!validatepkt((u8 *) alignedbuf, *len)) {
      *len = 0;
      return NULL;
    }

    iphdr = (struct ip *) alignedbuf;

    /* OK, since the IP header has been validated, we don't want to tell
     * the caller they have more packet than they really have.  This can
     * be caused by the Ethernet CRC trailer being counted, for example. */
    if (*len > ntohs(iphdr->ip_len))
      *len = ntohs(iphdr->ip_len);
  }
  // printf("Just got a packet at %li,%li\n", head.ts.tv_sec, head.ts.tv_usec);
  if (rcvdtime) {
    // FIXME: I eventually need to figure out why Windows head.ts time is sometimes BEFORE the time I
    // sent the packet (which is according to gettimeofday() in nbase).  For now, I will sadly have to
    // use gettimeofday() for Windows in this case
    // Actually I now allow .05 discrepancy.   So maybe this isn't needed.  I'll comment out for now.
    // Nope: it is still needed at least for Windows.  Sometimes the time from he pcap header is a 
    // COUPLE SECONDS before the gettimeofday() results :(.
#if defined(WIN32) || defined(__amigaos__)
    gettimeofday(&tv_end, NULL);
    *rcvdtime = tv_end;
#else
    rcvdtime->tv_sec = head.ts.tv_sec;
    rcvdtime->tv_usec = head.ts.tv_usec;
    assert(head.ts.tv_sec);
#endif
  }

  if (rcvdtime)
    PacketTrace::trace(PacketTrace::RCVD, (u8 *) alignedbuf, *len,
                       rcvdtime);
  else
    PacketTrace::trace(PacketTrace::RCVD, (u8 *) alignedbuf, *len);

  return alignedbuf;
}

// Returns whether the system supports pcap_get_selectable_fd() properly
bool pcap_selectable_fd_valid() {
#if defined(WIN32) || defined(MACOSX) || (defined(FREEBSD) && (__FreeBSD_version < 500000))
  return false;
#else
  return true;
#endif
}

/* Call this instead of pcap_get_selectable_fd directly (or your code
   won't compile on Windows).  On systems which don't seem to support
   the pcap_get_selectable_fd() function properly, returns -1,
   otherwise simply calls pcap_selectable_fd and returns the
   results.  If you just want to test whether the function is supported,
   use pcap_selectable_fd_valid() instead. */
int my_pcap_get_selectable_fd(pcap_t *p) {
#if defined(WIN32) || defined(MACOSX) || (defined(FREEBSD) && (__FreeBSD_version < 500000))
  return -1;
#else
  assert(pcap_selectable_fd_valid());
  return pcap_get_selectable_fd(p);
#endif
}

// Returns whether the packet receive time value obtained from libpcap
// (and thus by readip_pcap()) should be considered valid.  When
// invalid (Windows and Amiga), readip_pcap returns the time you called it.
bool pcap_recv_timeval_valid() {
#if defined(WIN32) || defined(__amigaos__)
  return false;
#else
  return true;
#endif
}

/* Prints stats from a pcap descriptor (number of received and dropped
   packets). */
void pcap_print_stats(int logt, pcap_t *pd) {
  struct pcap_stat stat;

  assert(pd != NULL);

  if (pcap_stats(pd, &stat) < 0) {
    error("%s: %s", __func__, pcap_geterr(pd));
    return;
  }

  log_write(logt, "pcap stats: %u packets received by filter, %u dropped by kernel.\n", stat.ps_recv, stat.ps_drop);
}

/* A trivial functon that maintains a cache of IP to MAC Address
   entries.  If the command is ARPCACHE_GET, this func looks for the
   IPv4 address in ss and fills in the 'mac' parameter and returns
   true if it is found.  Otherwise (not found), the function returns
   false.  If the command is ARPCACHE_SET, the function adds an entry
   with the given ip (ss) and mac address.  An existing entry for the
   IP ss will be overwritten with the new MAC address.  true is always
   returned for the set command. */
#define ARPCACHE_GET 1
#define ARPCACHE_SET 2
static bool NmapArpCache(int command, struct sockaddr_storage *ss, u8 *mac) {
  struct sockaddr_in *sin = (struct sockaddr_in *) ss;
  struct ArpCache {
    u32 ip; /* Network byte order */
    u8 mac[6];
  };
  static struct ArpCache *Cache = NULL;
  static int ArpCapacity = 0;
  static int ArpCacheSz = 0;
  int i;

  if (sin->sin_family != AF_INET)
    fatal("%s() can only take IPv4 addresses.  Sorry", __func__);

  if (command == ARPCACHE_GET) {
    for (i = 0; i < ArpCacheSz; i++) {
      if (Cache[i].ip == sin->sin_addr.s_addr) {
        memcpy(mac, Cache[i].mac, 6);
        return true;
      }
    }
    return false;
  }
  assert(command == ARPCACHE_SET);
  if (ArpCacheSz == ArpCapacity) {
    if (ArpCapacity == 0)
      ArpCapacity = 32;
    else
      ArpCapacity <<= 2;
    Cache = (struct ArpCache *) safe_realloc(Cache, ArpCapacity * sizeof(struct ArpCache));
  }

  /* Ensure that it isn't already there ... */
  for (i = 0; i < ArpCacheSz; i++) {
    if (Cache[i].ip == sin->sin_addr.s_addr) {
      memcpy(Cache[i].mac, mac, 6);
      return true;
    }
  }

  /* Add it to the end of the list */
  Cache[i].ip = sin->sin_addr.s_addr;
  memcpy(Cache[i].mac, mac, 6);
  ArpCacheSz++;
  return true;
}

/* Returns true if the captured frame is ARP. This function understands the
   datalink types DLT_EN10MB and DLT_LINUX_SLL. */
static bool frame_is_arp(const u8 *frame, int datalink) {
  if (datalink == DLT_EN10MB) {
    return ntohs(*((u16 *) (frame + 12))) == ETH_TYPE_ARP;
  } else if (datalink == DLT_LINUX_SLL) {
    return ntohs(*((u16 *) (frame + 2))) == ARPHRD_ETHER && /* sll_hatype */
      ntohs(*((u16 *) (frame + 4))) == 6 && /* sll_halen */
      ntohs(*((u16 *) (frame + 14))) == ETH_TYPE_ARP; /* sll_protocol */
  } else {
    return false;
  }
}

/* Attempts to read one IPv4/Ethernet ARP reply packet from the pcap
   descriptor pd.  If it receives one, fills in sendermac (must pass
   in 6 bytes), senderIP, and rcvdtime (can be NULL if you don't care)
   and returns 1.  If it times out and reads no arp requests, returns
   0.  to_usec is the timeout period in microseconds.  Use 0 to avoid
   blocking to the extent possible.  Returns
   -1 or exits if there is an error. */
int read_arp_reply_pcap(pcap_t *pd, u8 *sendermac,
                        struct in_addr *senderIP, long to_usec,
                        struct timeval *rcvdtime) {
  static int warning = 0;
  int datalink;
  struct pcap_pkthdr head;
  u8 *p;
  int timedout = 0;
  int badcounter = 0;
  unsigned int offset;
  struct timeval tv_start, tv_end;

  if (!pd)
    fatal("NULL packet device passed to %s", __func__);

  if (to_usec < 0) {
    if (!warning) {
      warning = 1;
      error("WARNING: Negative timeout value (%lu) passed to %s() -- using 0", to_usec, __func__);
    }
    to_usec = 0;
  }

  /* New packet capture device, need to recompute offset */
  if ((datalink = pcap_datalink(pd)) < 0)
    fatal("Cannot obtain datalink information: %s", pcap_geterr(pd));

  if (datalink == DLT_EN10MB) {
    offset = ETH_HDR_LEN;
  } else if (datalink == DLT_LINUX_SLL) {
    /* The datalink type is Linux "cooked" sockets. See pcap-linktype(7). */
    offset = 16;
  } else {
    fatal("%s called on interface that is datatype %d rather than DLT_EN10MB (%d) or DLT_LINUX_SLL (%d)", __func__, datalink, DLT_EN10MB, DLT_LINUX_SLL);
  }

  if (to_usec > 0) {
    gettimeofday(&tv_start, NULL);
  }

  do {
#ifdef WIN32
    if (to_usec == 0) {
      PacketSetReadTimeout(pd->adapter, 1);
    } else {
      gettimeofday(&tv_end, NULL);
      long to_left =
          MAX(1, (to_usec - TIMEVAL_SUBTRACT(tv_end, tv_start)) / 1000);
      // Set the timeout (BUGBUG: this is cheating)
      PacketSetReadTimeout(pd->adapter, to_left);
    }
#endif

    p = NULL;

    if (pcap_select(pd, to_usec) == 0)
      timedout = 1;
    else
      p = (u8 *) pcap_next(pd, &head);

    if (p && head.caplen >= offset + 28) {
      /* hw type eth (0x0001), prot ip (0x0800),
         hw size (0x06), prot size (0x04) */
      if (frame_is_arp(p, datalink) &&
        memcmp(p + offset, "\x00\x01\x08\x00\x06\x04\x00\x02", 8) == 0) {
        memcpy(sendermac, p + offset + 8, 6);
        /* I think alignment should allow this ... */
        memcpy(&senderIP->s_addr, p + offset + 14, 4);
        break;
      }
    }

    if (!p) {
      /* Should we timeout? */
      if (to_usec == 0) {
        timedout = 1;
      } else if (to_usec > 0) {
        gettimeofday(&tv_end, NULL);
        if (TIMEVAL_SUBTRACT(tv_end, tv_start) >= to_usec) {
          timedout = 1;
        }
      }
    } else {
      /* We'll be a bit patient if we're getting actual packets back, but
         not indefinitely so */
      if (badcounter++ > 50)
        timedout = 1;
    }
  } while (!timedout);

  if (timedout)
    return 0;

  if (rcvdtime) {
    // FIXME: I eventually need to figure out why Windows head.ts time is sometimes BEFORE the time I
    // sent the packet (which is according to gettimeofday() in nbase).  For now, I will sadly have to
    // use gettimeofday() for Windows in this case
    // Actually I now allow .05 discrepancy.   So maybe this isn't needed.  I'll comment out for now.
    // Nope: it is still needed at least for Windows.  Sometimes the time from he pcap header is a 
    // COUPLE SECONDS before the gettimeofday() results :(.
#if defined(WIN32) || defined(__amigaos__)
    gettimeofday(&tv_end, NULL);
    *rcvdtime = tv_end;
#else
    rcvdtime->tv_sec = head.ts.tv_sec;
    rcvdtime->tv_usec = head.ts.tv_usec;
    assert(head.ts.tv_sec);
#endif
  }
  PacketTrace::traceArp(PacketTrace::RCVD, (u8 *) p + offset, ARP_HDR_LEN + ARP_ETHIP_LEN, rcvdtime);

  return 1;
}


/* This function tries to determine the target's ethernet MAC address
   from a received packet as follows:
   1) If linkhdr is an ethernet header, grab the src mac (otherwise give up)
   2) If overwrite is 0 and a MAC is already set for this target, give up.
   3) If the packet source address is not the target, give up.
   4) Use the routing table to try to determine rather target is
      directly connected to the src host running Nmap.  If it is, set the MAC.

   This function returns 0 if it ends up setting the MAC, nonzero otherwise

   This function assumes that ip has already been verified as
   containing a complete IP header (or at least the first 20 bytes). */
int setTargetMACIfAvailable(Target *target, struct link_header *linkhdr,
                            struct ip *ip, int overwrite) {
  if (!linkhdr || !target || !ip)
    return 1;

  if (linkhdr->datalinktype != DLT_EN10MB || linkhdr->headerlen != 14)
    return 2;

  if (!overwrite && target->MACAddress())
    return 3;

  if (ip->ip_src.s_addr != target->v4host().s_addr)
    return 4;

  /* Sometimes bogus MAC address still gets through, like during some localhost scans */
  if (memcmp(linkhdr->header + 6, "\0\0\0\0\0\0", 6) == 0)
    return 5;

  if (target->ifType() == devt_ethernet && target->directlyConnected()) {
    /* Yay!  This MAC address seems valid */
    target->setMACAddress(linkhdr->header + 6);
    return 0;
  }

  return 5;
}

/* Issues an ARP request for the MAC of targetss (which will be placed
   in targetmac if obtained) from the source IP (srcip) and source mac
   (srcmac) given.  "The request is ussued using device dev to the
   broadcast MAC address.  The transmission is attempted up to 3
   times.  If none of these elicit a response, false will be returned.
   If the mac is determined, true is returned. */
static bool doArp(const char *dev, const u8 *srcmac,
                  const struct sockaddr_storage *srcip,
                  const struct sockaddr_storage *targetip,
                  u8 *targetmac) {
  /* timeouts in microseconds ... the first ones are retransmit times, while 
     the final one is when we give up */
  int timeouts[] = { 100000, 400000, 800000 };
  int max_sends = 3;
  int num_sends = 0; // How many we have sent so far 
  eth_t *ethsd;
  u8 frame[ETH_HDR_LEN + ARP_HDR_LEN + ARP_ETHIP_LEN];
  const struct sockaddr_in *targetsin = (struct sockaddr_in *) targetip;
  const struct sockaddr_in *srcsin = (struct sockaddr_in *) srcip;
  struct timeval start, now, rcvdtime;
  int timeleft;
  int listenrounds;
  int rc;
  pcap_t *pd;
  struct in_addr rcvdIP;
  bool foundit = false;

  if (targetsin->sin_family != AF_INET || srcsin->sin_family != AF_INET)
    fatal("%s can only handle IPv4 addresses", __func__);

  /* Start listening */
  pd = my_pcap_open_live(dev, 50, 1, 25);
  set_pcap_filter(dev, pd,
                  "arp and arp[18:4] = 0x%02X%02X%02X%02X and arp[22:2] = 0x%02X%02X",
                  srcmac[0], srcmac[1], srcmac[2], srcmac[3], srcmac[4],
                  srcmac[5]);

  /* Prepare probe and sending stuff */
  ethsd = eth_open_cached(dev);
  if (!ethsd)
    fatal("%s: failed to open device %s", __func__, dev);
  eth_pack_hdr(frame, ETH_ADDR_BROADCAST, *srcmac, ETH_TYPE_ARP);
  arp_pack_hdr_ethip(frame + ETH_HDR_LEN, ARP_OP_REQUEST, *srcmac,
                     srcsin->sin_addr, ETH_ADDR_BROADCAST,
                     targetsin->sin_addr);
  gettimeofday(&start, NULL);
  gettimeofday(&now, NULL);

  while (!foundit && num_sends < max_sends) {
    /* Send the sucker */
    rc = eth_send(ethsd, frame, sizeof(frame));
    if (rc != sizeof(frame)) {
      error("WARNING: %s: eth_send of ARP packet returned %u rather than expected %d bytes", __func__, rc, (int) sizeof(frame));
    }
    PacketTrace::traceArp(PacketTrace::SENT, (u8 *) frame + ETH_HDR_LEN,
      ARP_HDR_LEN + ARP_ETHIP_LEN, &now);
    num_sends++;

    listenrounds = 0;
    while (!foundit) {
      gettimeofday(&now, NULL);
      timeleft = timeouts[num_sends - 1] - TIMEVAL_SUBTRACT(now, start);
      if (timeleft < 0) {
        if (listenrounds > 0)
          break;
        else
          timeleft = 25000;
      }
      listenrounds++;
      /* Now listen until we reach our next timeout or get an answer */
      rc = read_arp_reply_pcap(pd, targetmac, &rcvdIP, timeleft,
                               &rcvdtime);
      if (rc == -1)
        fatal("%s: Received -1 response from readarp_reply_pcap", __func__);
      if (rc == 1) {
        /* Yay, I got one! But is it the right one? */
        if (rcvdIP.s_addr != targetsin->sin_addr.s_addr)
          continue; /* D'oh! */
        foundit = true; /* WOOHOO! */
      }
    }
  }

  /* OK - let's close up shop ... */
  pcap_close(pd);
  /* No need to close ethsd due to caching */
  return foundit;
}


/* This function ensures that the next hop MAC address for a target is
   filled in.  This address is the target's own MAC if it is directly
   connected, and the next hop mac otherwise.  Returns true if the
   address is set when the function ends, false if not.  This function
   firt checks if it is already set, if not it tries the arp cache,
   and if that fails it sends an ARP request itself.  This should be
   called after an ARP scan if many directly connected machines are
   involved. setDirectlyConnected() (whether true or false) should
   have already been called on target before this.  The target device
   and src mac address should also already be set.  */
bool setTargetNextHopMAC(Target *target) {
  struct sockaddr_storage targetss, srcss;
  size_t sslen;
  arp_t *a;
  u8 mac[6];
  struct arp_entry ae;

  if (target->ifType() != devt_ethernet)
    return false; /* Duh. */

  /* First check if we already have it, duh. */
  if (target->NextHopMACAddress())
    return true;

  /* For connected machines, it is the same as the target addy */
  if (target->directlyConnected() && target->MACAddress()) {
    target->setNextHopMACAddress(target->MACAddress());
    return true;
  }

  if (target->directlyConnected()) {
    target->TargetSockAddr(&targetss, &sslen);
  } else {
    if (!target->nextHop(&targetss, &sslen))
      fatal("%s: Failed to determine nextHop to target", __func__);
  }

  /* First, let us check the Nmap arp cache ... */
  if (NmapArpCache(ARPCACHE_GET, &targetss, mac)) {
    target->setNextHopMACAddress(mac);
    return true;
  }

  /* Maybe the system ARP cache will be more helpful */
  a = arp_open();
  addr_ston((sockaddr *) & targetss, &ae.arp_pa);
  if (arp_get(a, &ae) == 0) {
    NmapArpCache(ARPCACHE_SET, &targetss, ae.arp_ha.addr_eth.data);
    target->setNextHopMACAddress(ae.arp_ha.addr_eth.data);
    arp_close(a);
    return true;
  }
  arp_close(a);

  /* OK, the last choice is to send our own damn ARP request (and
     retransmissions if necessary) to determine the MAC */
  target->SourceSockAddr(&srcss, NULL);
  if (doArp(target->deviceFullName(), target->SrcMACAddress(),
            &srcss, &targetss, mac)) {
    NmapArpCache(ARPCACHE_SET, &targetss, mac);
    target->setNextHopMACAddress(mac);
    return true;
  }

  /* I'm afraid that we couldn't find it!  Maybe it doesn't exist? */
  return false;
}

/* Like to getTargetNextHopMAC(), but for arbitrary hosts (not Targets) */
bool getNextHopMAC(char *iface, u8 *srcmac, struct sockaddr_storage *srcss,
		   struct sockaddr_storage *dstss, u8 *dstmac)
{
  arp_t *a;
  struct arp_entry ae;

  /* Nmap's ARP cache */
  if (NmapArpCache(ARPCACHE_GET, dstss, dstmac))
    return true;

  /* System ARP cache */
  a = arp_open();
  addr_ston((sockaddr *) dstss, &ae.arp_pa);
  if (arp_get(a, &ae) == 0) {
    NmapArpCache(ARPCACHE_SET, dstss, ae.arp_ha.addr_eth.data);
    memcpy(dstmac, ae.arp_ha.addr_eth.data, 6);
    arp_close(a);
    return true;
  }
  arp_close(a);

  /* Send ARP */
  if (doArp(iface, srcmac, srcss, dstss, dstmac)) {
    NmapArpCache(ARPCACHE_SET, dstss, dstmac);
    return true;
  }

  return false;
}

/* Set a pcap filter */
void set_pcap_filter(const char *device, pcap_t *pd, const char *bpf, ...) {
  va_list ap;
  char buf[3072];
  struct bpf_program fcode;
#ifndef __amigaos__
  unsigned int localnet, netmask;
#else
  bpf_u_int32 localnet, netmask;
#endif
  char err0r[256];

  // Cast below is becaue OpenBSD apparently has a version that takes a
  // non-const device (hopefully they don't actually write to it).
  if (pcap_lookupnet((char *) device, &localnet, &netmask, err0r) < 0)
    fatal("Failed to lookup subnet/netmask for device (%s): %s", device, err0r);

  va_start(ap, bpf);
  if (Vsnprintf(buf, sizeof(buf), bpf, ap) >= (int) sizeof(buf))
    fatal("%s called with too-large filter arg\n", __func__);
  va_end(ap);

  /* Due to apparent bug in libpcap */
  /* Maybe this bug no longer exists ... I'll comment out for now 
   *      if (islocalhost(target->v4hostip()))
   *      buf[0] = '\0'; */

  if (o.debugging)
    log_write(LOG_STDOUT, "Packet capture filter (device %s): %s\n",
              device, buf);

  if (pcap_compile(pd, &fcode, buf, 0, netmask) < 0)
    fatal("Error compiling our pcap filter: %s", pcap_geterr(pd));
  if (pcap_setfilter(pd, &fcode) < 0)
    fatal("Failed to set the pcap filter: %s\n", pcap_geterr(pd));
  pcap_freecode(&fcode);
}

/* The 'dev' passed in must be at least 32 bytes long. Returns 0 on success. */
int ipaddr2devname(char *dev, const struct in_addr *addr) {
  struct interface_info *ifaces;
  struct sockaddr_in *sin;
  int numifaces;
  int i;

  ifaces = getinterfaces(&numifaces);

  if (ifaces == NULL)
    return -1;

  for (i = 0; i < numifaces; i++) {
    sin = (struct sockaddr_in *) &ifaces[i].addr;
    if (sin->sin_family != AF_INET)
      continue;
    if (addr->s_addr == sin->sin_addr.s_addr) {
      Strncpy(dev, ifaces[i].devname, 32);
      return 0;
    }
  }

  return -1;
}

int devname2ipaddr(char *dev, struct in_addr *addr) {
  struct interface_info *mydevs;
  struct sockaddr_in *s;
  int numdevs;
  int i;
  mydevs = getinterfaces(&numdevs);

  if (!mydevs)
    return -1;

  for (i = 0; i < numdevs; i++) {
    s = (struct sockaddr_in *) &mydevs[i].addr;
    if (s->sin_family != AF_INET) /* Currently we only support IPv4 */
      continue;
    if (!strcmp(dev, mydevs[i].devfullname)) {
      memcpy(addr, (char *) &s->sin_addr, sizeof(struct in_addr));
      return 0;
    }
  }
  return -1;
}

/* This struct is abused to carry either routes or interfaces, depending on the
   function it's used in. */
struct dnet_collector_route_nfo {
  struct sys_route *routes;
  int numroutes;
  int capacity; /* Capacity of routes or ifaces, depending on context */
  struct interface_info *ifaces;
  int numifaces;
};

#if WIN32
static int collect_dnet_interfaces(const struct intf_entry *entry, void *arg) {
  struct dnet_collector_route_nfo *dcrn = (struct dnet_collector_route_nfo *) arg;
  bool primary_done;
  int num_aliases_done;

  primary_done = false;
  num_aliases_done = 0;
  while (!primary_done || num_aliases_done < entry->intf_alias_num) {
    /* Make sure we have room for the new route */
    if (dcrn->numifaces >= dcrn->capacity) {
      dcrn->capacity <<= 2;
      dcrn->ifaces = (struct interface_info *) safe_realloc(dcrn->ifaces, dcrn-> capacity * sizeof(struct interface_info));
    }

    /* The first time through the loop we add the primary interface record. After
       that we add the aliases one at a time. */
    if (!primary_done) {
      if (entry->intf_addr.addr_type == ADDR_TYPE_IP) {
        addr_ntos(&entry->intf_addr, (struct sockaddr *) &dcrn->ifaces[dcrn->numifaces].addr);
        dcrn->ifaces[dcrn->numifaces].netmask_bits = entry->intf_addr.addr_bits;
      }
      primary_done = true;
    } else if (num_aliases_done < (int) entry->intf_alias_num) {
      if (entry->intf_alias_addrs[num_aliases_done].addr_type == ADDR_TYPE_IP) {
        addr_ntos(&entry->intf_alias_addrs[num_aliases_done], (struct sockaddr *) &dcrn->ifaces[dcrn->numifaces].addr);
        dcrn->ifaces[dcrn->numifaces].netmask_bits = entry->intf_alias_addrs[num_aliases_done].addr_bits;
      }
      num_aliases_done++;
    }

    /* OK, address/netmask found.  Let's get the name */
    Strncpy(dcrn->ifaces[dcrn->numifaces].devname, entry->intf_name,
            sizeof(dcrn->ifaces[dcrn->numifaces].devname));
    Strncpy(dcrn->ifaces[dcrn->numifaces].devfullname, entry->intf_name,
            sizeof(dcrn->ifaces[dcrn->numifaces].devfullname));

    /* Interface type */
    if (entry->intf_type == INTF_TYPE_ETH) {
      dcrn->ifaces[dcrn->numifaces].device_type = devt_ethernet;
      /* Collect the MAC address since this is ethernet */
      memcpy(dcrn->ifaces[dcrn->numifaces].mac,
             &entry->intf_link_addr.addr_eth.data, 6);
    } else if (entry->intf_type == INTF_TYPE_LOOPBACK)
      dcrn->ifaces[dcrn->numifaces].device_type = devt_loopback;
    else if (entry->intf_type == INTF_TYPE_TUN)
      dcrn->ifaces[dcrn->numifaces].device_type = devt_p2p;
    else
      dcrn->ifaces[dcrn->numifaces].device_type = devt_other;

    /* Is the interface up and running? */
    dcrn->ifaces[dcrn->numifaces].device_up =
        (entry->intf_flags & INTF_FLAG_UP) ? true : false;

    /* For the rest of the information, we must open the interface directly ... */
    dcrn->numifaces++;
  }
  return 0;
}

/* Get a list of interfaces using dnet and intf_loop. */
static struct interface_info *getinterfaces_dnet(int *howmany) {
  struct dnet_collector_route_nfo dcrn;
  intf_t *it;

  dcrn.routes = NULL;
  dcrn.numroutes = 0;
  dcrn.numifaces = 0;

  /* Initialize the interface array. */
  dcrn.capacity = 16;
  dcrn.ifaces = (struct interface_info *) safe_zalloc(sizeof(struct interface_info) * dcrn.capacity);

  it = intf_open();
  if (!it)
    fatal("%s: intf_open() failed", __func__);
  if (intf_loop(it, collect_dnet_interfaces, &dcrn) != 0)
    fatal("%s: intf_loop() failed", __func__);
  intf_close(it);

  *howmany = dcrn.numifaces;
  return dcrn.ifaces;
}

#else /* !WIN32 */

/* Get a list of interfaces using ioctl(SIOCGIFCONF). */
static struct interface_info *getinterfaces_siocgifconf(int *howmany) {
  struct interface_info *devs;
  int count = 0;
  int capacity = 0;
  struct ifconf ifc;
  struct ifreq *ifr;
  int sd;
  int len;

  capacity = 16;
  devs = (struct interface_info *) safe_zalloc(sizeof(struct interface_info) * capacity);

  /* Dummy socket for ioctl */
  sd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sd < 0)
    pfatal("socket in %s", __func__);

  ifc.ifc_len = 20480;
  ifc.ifc_buf = (char *) safe_zalloc(ifc.ifc_len);
  /* Returns an array of struct ifreq in ifc.ifc_req, which is a union with
     ifc.ifc_buf. */
  if (ioctl(sd, SIOCGIFCONF, &ifc) < 0)
    fatal("Failed to determine your configured interfaces!\n");
  if (ifc.ifc_len == 0)
    fatal("%s: SIOCGIFCONF claims you have no network interfaces!\n", __func__);
  ifr = ifc.ifc_req;

  for (ifr = ifc.ifc_req;
       ifr && ifr->ifr_name[0] && (void *) ifr < (char *) ifc.ifc_buf + ifc.ifc_len;
       ifr = (struct ifreq *) ((char *) ifr + len)) {
    struct sockaddr_in *sin;
    struct ifreq tmpifr;
    u16 ifflags;
    int rc;
    char *p;

    len = sizeof(struct ifreq);
#if HAVE_SOCKADDR_SA_LEN
    /* Some platforms (such as FreeBSD) have an sa_len member that may make the
       ifr longer than sizeof(struct ifreq). */
    if (ifr->ifr_addr.sa_len > sizeof(ifr->ifr_ifru))
      len += ifr->ifr_addr.sa_len - sizeof(ifr->ifr_ifru);
#endif

    /* skip any device with no name */
    if (ifr->ifr_name[0] == '\0')
      continue;

    /* We currently only handle IPv4 */
    sin = (struct sockaddr_in *) &ifr->ifr_addr;
    if (sin->sin_family != AF_INET)
      continue;

    /* Make room for this new interface if necessary. */
    if (count >= capacity) {
      capacity <<= 2;
      devs = (struct interface_info *) safe_realloc(devs, sizeof(struct interface_info) * capacity);
    }

    /* We know the address, put it in the array. */
    memcpy(&(devs[count].addr), sin, MIN(sizeof(devs[count].addr), sizeof(*sin)));
    Strncpy(devs[count].devname, ifr->ifr_name, sizeof(devs[count].devname));
    Strncpy(devs[count].devfullname, ifr->ifr_name, sizeof(devs[count].devfullname));
    /* devname isn't allowed to have alias qualification */
    p = strchr(devs[count].devname, ':');
    if (p != NULL)
      *p = '\0';

    /* Use tmpifr for further ioctl requests. We're going to make a bunch of
       ioctl calls to learn about the interface and set fields in devs[count].

       The Linux netdevice(7) man page says that you only have to set ifr_name
       before making the ioctl, but perhaps other platforms need ifr_addr to be
       set too. ifr_name will persist but ifr_addr is in a union with the ioctl
       return value, so it has to be reset before every call. The general
       pattern is memcpy, then ioctl. */
    Strncpy(tmpifr.ifr_name, ifr->ifr_name, sizeof(tmpifr.ifr_name));

    /* Look up the netmask. Note setting of ifr_addr. */
    memcpy(&tmpifr.ifr_addr, sin, MIN(sizeof(tmpifr.ifr_addr), sizeof(*sin)));
    rc = ioctl(sd, SIOCGIFNETMASK, &tmpifr);
    if (rc < 0 && errno != EADDRNOTAVAIL)
      pfatal("Failed to determine the netmask of %s!", tmpifr.ifr_name);
    else if (rc < 0)
      devs[count].netmask_bits = 32;
    else {
      /* We would use ifr_netmask, but that's only on Linux, so use ifr_addr
         which shares the same memory space in a union. */
      addr_stob(&(tmpifr.ifr_addr), &devs[count].netmask_bits);
    }

    /* Now we need to determine the device type ... this technique is kinda iffy
       ... may not be portable. */
    /* Get the flags. */
    memcpy(&tmpifr.ifr_addr, sin, MIN(sizeof(tmpifr.ifr_addr), sizeof(*sin)));
    rc = ioctl(sd, SIOCGIFFLAGS, &tmpifr);
    if (rc < 0)
      pfatal("Failed to get IF Flags for device %s", ifr->ifr_name);

    ifflags = tmpifr.ifr_flags;

    if (ifflags & IFF_LOOPBACK) {
      devs[count].device_type = devt_loopback;
    } else if (ifflags & IFF_BROADCAST) {
      devs[count].device_type = devt_ethernet;

      /* If the device type is ethernet, get the MAC address. */
#ifdef SIOCGIFHWADDR
      memcpy(&tmpifr.ifr_addr, sin, MIN(sizeof(tmpifr.ifr_addr), sizeof(*sin)));
      rc = ioctl(sd, SIOCGIFHWADDR, &tmpifr);
      if (rc < 0 && errno != EADDRNOTAVAIL)
        pfatal("Failed to determine the MAC address of %s!", tmpifr.ifr_name);
      else if (rc >= 0)
        memcpy(devs[count].mac, &tmpifr.ifr_addr.sa_data, 6);
#else
      /* Let's just let libdnet handle it ... */
      eth_t *ethsd = eth_open_cached(devs[count].devname);
      eth_addr_t ethaddr;

      if (!ethsd) {
        error("Warning: Unable to open interface %s -- skipping it.", devs[count].devname);
        continue;
      }
      if (eth_get(ethsd, &ethaddr) != 0) {
        error("Warning: Unable to get hardware address for interface %s -- skipping it.", devs[count].devname);
        continue;
      }
      memcpy(devs[count].mac, ethaddr.data, 6);
#endif /*SIOCGIFHWADDR*/
    } else if (ifflags & IFF_POINTOPOINT) {
      devs[count].device_type = devt_p2p;
    } else {
      devs[count].device_type = devt_other;
    }

    if (ifflags & IFF_UP)
      devs[count].device_up = true;
    else
      devs[count].device_up = false;

    /* All done with this interface. Increase the count. */
    count++;
  }
  free(ifc.ifc_buf);
  close(sd);

  *howmany = count;
  return devs;
}
#endif

/* Returns an allocated array of struct interface_info representing the
   available interfaces. The number of interfaces is returned in *howmany. This
   function just does caching of results; the real work is done in
   getinterfaces_dnet or getinterfaces_siocgifconf. */
struct interface_info *getinterfaces(int *howmany) {
  static bool initialized = 0;
  static struct interface_info *mydevs;
  static int numifaces = 0;

  if (!initialized) {
#if WIN32
    /* On Win32 we just use Dnet to determine the interface list */
    mydevs = getinterfaces_dnet(&numifaces);
#else
    mydevs = getinterfaces_siocgifconf(&numifaces);
#endif
    initialized = 1;
  }

  if (howmany)
    *howmany = numifaces;
  return mydevs;
}

/* Looks for an interface with the given name (iname), and returns the
   corresponding interface_info if found.  Will accept a match of
   devname or devfullname.  Returns NULL if none found */
struct interface_info *getInterfaceByName(char *iname) {
  struct interface_info *ifaces;
  int numifaces = 0;
  int ifnum;

  ifaces = getinterfaces(&numifaces);

  for (ifnum = 0; ifnum < numifaces; ifnum++) {
    if (strcmp(ifaces[ifnum].devfullname, iname) == 0 ||
        strcmp(ifaces[ifnum].devname, iname) == 0)
      return &ifaces[ifnum];
  }

  return NULL;
}

pcap_if_t *getpcapinterfaces() {
#ifndef WIN32
  return NULL;
#endif
  pcap_if_t *p_ifaces;

  if ((pcap_findalldevs(&p_ifaces, NULL)) == -1) {
    fatal("pcap_findalldevs() : Cannot retrieve pcap interfaces");
    return NULL;
  }
  return p_ifaces;
}


/* A trivial function used with qsort to sort the routes by netmask */
static int nmaskcmp(const void *a, const void *b) {
  struct sys_route *r1 = (struct sys_route *) a;
  struct sys_route *r2 = (struct sys_route *) b;
  if (r1->netmask == r2->netmask) {
    /* Compare addresses of equal elements to make the sort stable, as suggested
       by the Glibc manual. */
    if (a < b)
      return -1;
    else if (a > b)
      return 1;
    else
      return 0;
  }
  if (ntohl(r1->netmask) > ntohl(r2->netmask))
    return -1;
  else
    return 1;
}

/* Read system routes from a handle to a /proc/net/route file. */
static struct sys_route *getsysroutes_proc(FILE *routefp, int *howmany) {
  struct sys_route *routes = NULL;
  int route_capacity = 128;
  struct interface_info *ifaces;
  char buf[1024];
  char iface[16];
  char *p, *endptr;
  struct interface_info *ii;
  u32 mask;
  struct sockaddr_in *sin;
  int numifaces = 0, numroutes = 0;
  int i;

  ifaces = getinterfaces(&numifaces);
  routes = (struct sys_route *) safe_zalloc(route_capacity * sizeof(struct sys_route));

  /* Kill the first line (column headers) */
  errno = 0;
  if (fgets(buf, sizeof(buf), routefp) == NULL) {
    if (errno)
      error("Read error in /proc/net/route");
    else
      error("Premature EOF in /proc/net/route");
    goto done;
  }
  while (fgets(buf, sizeof(buf), routefp)) {
    p = strtok(buf, " \t\n");
    if (!p) {
      error("Could not find interface in /proc/net/route line");
      continue;
    }
    if (*p == '*')
      continue; /* Deleted route -- any other valid reason for a route to start with an asterict? */
    Strncpy(iface, p, sizeof(iface));
    p = strtok(NULL, " \t\n");
    if (!p) {
      error("Could not find destination in /proc/net/route line");
      continue;
    }
    endptr = NULL;
    routes[numroutes].dest = strtoul(p, &endptr, 16);
    if (!endptr || *endptr) {
      error("Failed to determine Destination from /proc/net/route");
      continue;
    }

    /* Now for the gateway */
    p = strtok(NULL, " \t\n");
    if (!p) {
      error("Could not find gateway in /proc/net/route line");
      continue;
    }
    endptr = NULL;
    routes[numroutes].gw.s_addr = strtoul(p, &endptr, 16);
    if (!endptr || *endptr) {
      error("Failed to determine gw for %s from /proc/net/route", iface);
    }
    for (i = 0; i < 5; i++) {
      p = strtok(NULL, " \t\n");
      if (!p)
        break;
    }
    if (!p) {
      error("Failed to find field %d in /proc/net/route", i + 2);
      continue;
    }
    endptr = NULL;
    routes[numroutes].netmask = strtoul(p, &endptr, 16);
    if (!endptr || *endptr) {
      error("Failed to determine mask from /proc/net/route");
      continue;
    }
    for (i = 0; i < numifaces; i++) {
      if (!strcmp(iface, ifaces[i].devfullname)) {
        routes[numroutes].device = &ifaces[i];
        break;
      }
    }
    /* If device name in the route file does not match the full name (including
       alias extension) of any interface, then try to find at least an alias of
       the proper interface. */
    if (i == numifaces) {
      for (i = 0; i < numifaces; i++) {
        if (!strcmp(iface, ifaces[i].devname)) {
          routes[numroutes].device = &ifaces[i];
          break;
        }
      }
    }
    if (i == numifaces) {
      error("Failed to find device %s which was referenced in /proc/net/route", iface);
      continue;
    }

    /* Now to deal with some alias nonsense ... at least on Linux
       this file will just list the short name, even though IP
       information (such as source address) from an alias must be
       used.  So if the purported device can't reach the gateway,
       try to find a device that starts with the same short
       devname, but can (e.g. eth0 -> eth0:3) */
    ii = &ifaces[i];
    mask = htonl((unsigned long) (0 - 1) << (32 - ii->netmask_bits));
    sin = (struct sockaddr_in *) &ii->addr;
    if (routes[numroutes].gw.s_addr && (sin->sin_addr.s_addr & mask) !=
        (routes[numroutes].gw.s_addr & mask)) {
      for (i = 0; i < numifaces; i++) {
        if (ii == &ifaces[i])
          continue;
        if (strcmp(ii->devname, ifaces[i].devname) == 0) {
          sin = (struct sockaddr_in *) &ifaces[i].addr;
          if ((sin->sin_addr.s_addr & mask) ==
              (routes[numroutes].gw.s_addr & mask)) {
            routes[numroutes].device = &ifaces[i];
          }
        }
      }
    }

    numroutes++;
    if (numroutes >= route_capacity) {
      route_capacity <<= 2;
      routes = (struct sys_route *) safe_realloc(routes, route_capacity * sizeof(struct sys_route));
    }
  }

done:
  *howmany = numroutes;
  return routes;
}

/* This is the callback for the call to route_loop in getsysroutes_dnet. It
   takes a route entry and adds it into the dnet_collector_route_nfo struct. */
static int collect_dnet_routes(const struct route_entry *entry, void *arg) {
  struct dnet_collector_route_nfo *dcrn = (struct dnet_collector_route_nfo *) arg;
  /* Make sure that it is the proper type of route ... */
  if (entry->route_dst.addr_type != ADDR_TYPE_IP || entry->route_gw.addr_type != ADDR_TYPE_IP)
    return 0; /* Not interested in IPv6 routes at the moment ... */

  /* Make sure we have room for the new route */
  if (dcrn->numroutes >= dcrn->capacity) {
    dcrn->capacity <<= 2;
    dcrn->routes = (struct sys_route *) safe_realloc(dcrn->routes, dcrn->capacity * sizeof(struct sys_route));
  }

  /* Now for the important business */
  dcrn->routes[dcrn->numroutes].dest = entry->route_dst.addr_ip;
  addr_btom(entry->route_dst.addr_bits,
            &dcrn->routes[dcrn->numroutes].netmask,
            sizeof(dcrn->routes[dcrn->numroutes].netmask));
  dcrn->routes[dcrn->numroutes].gw.s_addr = entry->route_gw.addr_ip;
  dcrn->numroutes++;

  return 0;
}

/* This is a helper for getsysroutes_dnet. Once the table of routes is in
   place, this function assigns each to an interface and removes any routes
   that can't be assigned. */
static struct dnet_collector_route_nfo *sysroutes_dnet_find_interfaces(struct dnet_collector_route_nfo *dcrn) 
{
  struct interface_info *ifaces;
  u32 mask;
  struct sockaddr_in *sin;
  int numifaces = 0;
  int i, j;

  ifaces = getinterfaces(&numifaces);
  for (i = 0; i < dcrn->numroutes; i++) {
    /* First we match up routes whose gateway address directly matches the
       address of an interface. */
    for (j = 0; j < numifaces; j++) {
      sin = (struct sockaddr_in *) &ifaces[j].addr;
      mask =
          htonl((unsigned long) (0 - 1) << (32 - ifaces[j].netmask_bits));
      if ((sin->sin_addr.s_addr & mask) ==
          (dcrn->routes[i].gw.s_addr & mask)) {
        dcrn->routes[i].device = &ifaces[j];
        break;
      }
    }
  }

  /* Find any remaining routes that don't yet have an interface, and try to
     match them up with the interface of another route. This handles "two-step"
     routes like sometimes exist with PPP, where the gateway address of the
     default route doesn't match an interface address, but the gateway address
     goes through another route that does have an interface. */
  bool changed;
  do {
    changed = false;
    for (i = 0; i < dcrn->numroutes; i++) {
      if (dcrn->routes[i].device != NULL)
        continue;
      /* Does this route's gateway go through another route with an assigned
         interface? */
      for (j = 0; j < dcrn->numroutes; j++) {
        if (dcrn->routes[i].gw.s_addr == dcrn->routes[j].dest
            && dcrn->routes[j].device != NULL) {
          dcrn->routes[i].device = dcrn->routes[j].device;
          changed = true;
        }
      }
    }
  } while (changed);

  /* Cull any routes that still don't have an interface. */
  i = 0;
  while (i < dcrn->numroutes) {
    if (dcrn->routes[i].device == NULL) {
      error("WARNING: Unable to find appropriate interface for system route to %s", inet_ntoa(dcrn->routes[i].gw));
      /* Remove this entry from the table. */
      memmove(dcrn->routes + i, dcrn->routes + i + 1, sizeof(dcrn->routes[0]) * (dcrn->numroutes - i - 1));
      dcrn->numroutes--;
    } else {
      i++;
    }
  }

  return dcrn;
}

/* Read system routes via libdnet. */
static struct sys_route *getsysroutes_dnet(int *howmany) {
  struct dnet_collector_route_nfo dcrn;

  dcrn.capacity = 128;
  dcrn.routes = (struct sys_route *) safe_zalloc(dcrn.capacity * sizeof(struct sys_route));
  dcrn.numroutes = 0;
  dcrn.ifaces = NULL;
  dcrn.numifaces = 0;
  route_t *dr = route_open();
  if (!dr)
    fatal("%s: route_open() failed", __func__);
  if (route_loop(dr, collect_dnet_routes, &dcrn) != 0) {
    fatal("%s: route_loop() failed", __func__);
  }
  route_close(dr);

  /* Now match up the routes to interfaces. */
  sysroutes_dnet_find_interfaces(&dcrn);

  *howmany = dcrn.numroutes;
  return dcrn.routes;
}

/* Parse the system routing table, converting each route into a
   sys_route entry.  Returns an array of sys_routes.  numroutes is set
   to the number of routes in the array.  The routing table is only
   read the first time this is called -- later results are cached.
   The returned route array is sorted by netmask with the most
   specific matches first. */
struct sys_route *getsysroutes(int *howmany) {
  static struct sys_route *routes = NULL;
  static int numroutes = 0;
  FILE *routefp;

  if (!howmany)
    fatal("NULL howmany ptr passed to %s()", __func__);

  if (routes != NULL) {
    /* We have it cached. */
    *howmany = numroutes;
    return routes;
  }

  /* First let us try Linux-style /proc/net/route */
  routefp = fopen("/proc/net/route", "r");
  if (routefp) {
    routes = getsysroutes_proc(routefp, howmany);
    fclose(routefp);
  } else {
    routes = getsysroutes_dnet(howmany);
  }

  numroutes = *howmany;

  /* Ensure that the route array is sorted by netmask */
  qsort(routes, numroutes, sizeof(routes[0]), nmaskcmp);

  return routes;
}

static bool match_netmask(u32 addr1, u32 addr2, u32 mask) {
  return (addr1 & mask) == (addr2 & mask);
}

static bool match_netmask_bits(u32 addr1, u32 addr2, int bits) {
  return match_netmask(addr1, addr2, htonl((unsigned long) (0 - 1) << (32 - bits)));
}

static bool match_netmask_bits(const struct sockaddr_in *addr1,
  const struct sockaddr_in *addr2, int bits) {
  return match_netmask_bits(addr1->sin_addr.s_addr, addr2->sin_addr.s_addr, bits);
}

static struct interface_info *find_loopback_iface(struct interface_info *ifaces,
  int numifaces) {
  int i;

  for (i = 0; i < numifaces; i++) {
    if (ifaces[i].device_type == devt_loopback)
      return &ifaces[i];
  }

  return NULL;
}

/* Takes a destination address (dst) and tries to determine the
   source address and interface necessary to route to this address.
   If no route is found, false is returned and rnfo is undefined.  If
   a route is found, true is returned and rnfo is filled in with all
   of the routing details.  This function takes into account -S and -e
   options set by user (o.spoofsource, o.device) */
bool route_dst(const struct sockaddr_storage * const dst,
               struct route_nfo *rnfo) {
  struct interface_info *ifaces;
  struct interface_info *iface;
  int numifaces = 0;
  struct sys_route *routes;
  struct sockaddr_storage spoofss;
  size_t spoofsslen;
  int numroutes = 0;
  int i;
  struct sockaddr_in *ifsin, *dstsin;

  if (!dst)
    fatal("%s passed a NULL dst address", __func__);
  dstsin = (struct sockaddr_in *) dst;

  if (dstsin->sin_family != AF_INET)
    fatal("Sorry -- %s currently only supports IPv4", __func__);

  if (o.spoofsource) {
    o.SourceSockAddr(&spoofss, &spoofsslen);
    //throughout the rest of this function we only change rnfo->srcaddr if the source isnt spoofed
    memcpy(&rnfo->srcaddr, &spoofss, sizeof(rnfo->srcaddr));
    /* The device corresponding to this spoofed address should already have been
       set elsewhere. */
    assert(o.device[0] != '\0');
  }

  if (*o.device) {
    iface = getInterfaceByName(o.device);
    if (!iface)
      fatal("Could not find interface %s which was specified by -e", o.device);
  } else {
    iface = NULL;
  }

  routes = getsysroutes(&numroutes);
  ifaces = getinterfaces(&numifaces);

  /* First check if dst is one of the localhost's own addresses. We need to use
     a localhost device for these. */
  for (i = 0; i < numifaces; i++) {
    struct interface_info *loopback;

    ifsin = (struct sockaddr_in *) &ifaces[i].addr;
    if (dstsin->sin_addr.s_addr != ifsin->sin_addr.s_addr)
      continue;
    if (iface != NULL && strcmp(ifaces[i].devname, iface->devname) != 0)
      continue;

    if (ifaces[i].device_type == devt_loopback)
      loopback = &ifaces[i];
    else
      loopback = find_loopback_iface(ifaces, numifaces);
    if (loopback == NULL)
      /* Hmmm ... no localhost -- move on to the routing table. */
      break;

    rnfo->ii = *loopback;
    rnfo->direct_connect = true;
    /* But the source address we want to use is the target address. */
    if (!o.spoofsource)
      rnfo->srcaddr = ifaces[i].addr;

    return true;
  }

  /* Go through the routing table and take the first match. getsysroutes sorts
     so more-specific routes come first. */
  for (i = 0; i < numroutes; i++) {
    if (!match_netmask(dstsin->sin_addr.s_addr, routes[i].dest, routes[i].netmask))
      continue;
    /* Ignore routes that aren't on the device we specified. */
    if (iface != NULL && strcmp(routes[i].device->devname, iface->devname) != 0)
      continue;

    rnfo->ii = *routes[i].device;
    /* At this point we don't whether this route is direct or indirect ("G" flag
       in netstat). We guess that a route is direct when the gateway address is
       0.0.0.0, when it exactly matches the interface address, or when it
       exactly matches the destination address. */
    rnfo->direct_connect = (routes[i].gw.s_addr == 0) ||
      (routes[i].gw.s_addr == ((struct sockaddr_in *) &routes[i].device->addr)->sin_addr.s_addr) ||
      (routes[i].gw.s_addr == dstsin->sin_addr.s_addr);
    if (!o.spoofsource)
      rnfo->srcaddr = routes[i].device->addr;
    ifsin = (struct sockaddr_in *) &rnfo->nexthop;
    ifsin->sin_family = AF_INET;
    ifsin->sin_addr = routes[i].gw;

    return true;
  }

  /* No match on routes. Try interfaces directly. */
  for (i = 0; i < numifaces; i++) {
    if (ifaces[i].addr.ss_family != AF_INET)
      continue;
    ifsin = (struct sockaddr_in *) &ifaces[i].addr;
    if (!match_netmask_bits(dstsin, ifsin, ifaces[i].netmask_bits))
      continue;
    if (iface != NULL && strcmp(ifaces[i].devname, iface->devname) != 0)
      continue;

    rnfo->ii = ifaces[i];
    rnfo->direct_connect = true;
    if (!o.spoofsource)
      rnfo->srcaddr = ifaces[i].addr;

    return true;
  }

  return false;
}


/* Maximize the receive buffer of a socket descriptor (up to 500K) */
void max_rcvbuf(int sd) {
  int optval = 524288; /* 2^19 */
  recvfrom6_t optlen = sizeof(int);

#ifndef WIN32
  if (setsockopt (sd, SOL_SOCKET, SO_RCVBUF, (const char *) &optval, optlen))
    if (o.debugging)
      perror("Problem setting large socket receive buffer");
  if (o.debugging) {
    getsockopt(sd, SOL_SOCKET, SO_RCVBUF, (char *) &optval, &optlen);
    log_write(LOG_STDOUT, "Our buffer size is now %d\n", optval);
  }
#endif /* WIN32 */
}

/* Maximize the open file descriptor limit for this process go up to the
   max allowed  */
int max_sd() {
#ifndef WIN32
  struct rlimit r;
  static int maxfds = -1;

  if (maxfds > 0)
    return maxfds;

#if(defined(RLIMIT_NOFILE))
  if (!getrlimit(RLIMIT_NOFILE, &r)) {
    r.rlim_cur = r.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &r))
      if (o.debugging)
        perror("setrlimit RLIMIT_NOFILE failed");
    if (!getrlimit(RLIMIT_NOFILE, &r)) {
      maxfds = r.rlim_cur;
      return maxfds;
    } else
      return 0;
  }
#endif
#if(defined(RLIMIT_OFILE) && !defined(RLIMIT_NOFILE))
  if (!getrlimit(RLIMIT_OFILE, &r)) {
    r.rlim_cur = r.rlim_max;
    if (setrlimit(RLIMIT_OFILE, &r))
      if (o.debugging)
        perror("setrlimit RLIMIT_OFILE failed");
    if (!getrlimit(RLIMIT_OFILE, &r)) {
      maxfds = r.rlim_cur;
      return maxfds;
    } else
      return 0;
  }
#endif
#endif /* WIN32 */
  return 0;
}

/* Give broadcast permission to a socket */
void broadcast_socket(int sd) {
  int one = 1;
#ifdef WIN32
  if (sd == 501)
    return;
#endif
  if (setsockopt (sd, SOL_SOCKET, SO_BROADCAST, (const char *) &one, sizeof(int)) != 0) {
    error("Failed to secure socket broadcasting permission");
    perror("setsockopt");
  }
}

/* Do a receive (recv()) on a socket and stick the results (up to
   len) into buf .  Give up after 'seconds'.  Returns the number of
   bytes read (or -1 in the case of an error.  It only does one recv
   (it will not keep going until len bytes are read).  If timedout is
   not NULL, it will be set to zero (no timeout occured) or 1 (it
   did). */
int recvtime(int sd, char *buf, int len, int seconds, int *timedout) {

  int res;
  struct timeval timeout;
  fd_set readfd;

  timeout.tv_sec = seconds;
  timeout.tv_usec = 0;
  FD_ZERO(&readfd);
  FD_SET(sd, &readfd);
  if (timedout)
    *timedout = 0;
  res = select(sd + 1, &readfd, NULL, NULL, &timeout);
  if (res > 0) {
    res = recv(sd, buf, len, 0);
    if (res >= 0)
      return res;
    gh_perror("recv in %s", __func__);
    return 0;
  } else if (!res) {
    if (timedout)
      *timedout = 1;
    return 0;
  }
  gh_perror("select() in %s", __func__);
  return -1;
}

/* Examines the given tcp packet and obtains the TCP timestamp option
   information if available.  Note that the CALLER must ensure that
   "tcp" contains a valid header (in particular the th_off must be the
   true packet length and tcp must contain it).  If a valid timestamp
   option is found in the header, nonzero is returned and the
   'timestamp' and 'echots' parameters are filled in with the
   appropriate value (if non-null).  Otherwise 0 is returned and the
   parameters (if non-null) are filled with 0.  Remember that the
   correct way to check for errors is to look at the return value
   since a zero ts or echots could possibly be valid. */
int gettcpopt_ts(struct tcp_hdr *tcp, u32 *timestamp, u32 *echots) {

  unsigned char *p;
  int len = 0;
  int op;
  int oplen;

  /* first we find where the tcp options start ... */
  p = ((unsigned char *) tcp) + 20;
  len = 4 * tcp->th_off - 20;
  while (len > 0 && *p != 0 /* TCPOPT_EOL */ ) {
    op = *p++;
    if (op == 0 /* TCPOPT_EOL */ )
      break;
    if (op == 1 /* TCPOPT_NOP */ ) {
      len--;
      continue;
    }
    oplen = *p++;
    if (oplen < 2)
      break; /* No infinite loops, please */
    if (oplen > len)
      break; /* Not enough space */
    if (op == 8 /* TCPOPT_TIMESTAMP */  && oplen == 10) {
      /* Legitimate ts option */
      if (timestamp) {
        memcpy((char *) timestamp, p, 4);
        *timestamp = ntohl(*timestamp);
      }
      p += 4;
      if (echots) {
        memcpy((char *) echots, p, 4);
        *echots = ntohl(*echots);
      }
      return 1;
    }
    len -= oplen;
    p += oplen - 2;
  }

  /* Didn't find anything */
  if (timestamp)
    *timestamp = 0;
  if (echots)
    *echots = 0;
  return 0;
}
