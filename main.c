#define _GNU_SOURCE
#include <signal.h>
#include <poll.h>
#include <stdio.h>
#include <pcap.h>
#include <time.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <ncurses.h>

#include "uthash.h"
#include "decode.h"

const char *protos[IPPROTO_MAX] = {
	[IPPROTO_TCP]  = "TCP",
	[IPPROTO_UDP]  = "UDP",
	[IPPROTO_ICMP] = "ICMP",
	[IPPROTO_IP]   = "IP"
};

struct pkt_record {
	uint32_t ts_sec;
	uint32_t ts_usec;
	uint32_t len;              /* this is cumulative in tables */
	struct {
		struct in_addr src_ip;     /* key */
		uint16_t sport;
	} src;
	struct {
		struct in_addr dst_ip;
		uint16_t dport;
	} dst;
	uint16_t proto;
	UT_hash_handle hh;         /* makes this structure hashable */
};


/* hash tables for stats */
struct pkt_record *src_table = NULL;
struct pkt_record *dst_table = NULL;

#define zero_pkt(p)                                                            \
	do {                                                                   \
		p->ts_sec = 0;                                                 \
		p->ts_usec = 0;                                                \
		p->len = 0;                                                    \
		p->proto = 0;                                                  \
		p->src.sport = 0;                                              \
		p->dst.dport = 0;                                              \
	} while (0);

void print_pkt(struct pkt_record *pkt)
{
	char ip_src[16];
	char ip_dst[16];

	sprintf(ip_src, "%s", inet_ntoa(pkt->src.src_ip));
	sprintf(ip_dst, "%s", inet_ntoa(pkt->dst.dst_ip));

	mvprintw(1, 0, "%d.%06d,  %4d, %15s, %15s, %4s %6d, %6d\n",
	       pkt->ts_sec, pkt->ts_usec, pkt->len, ip_src, ip_dst,
	       protos[pkt->proto], pkt->src.sport, pkt->dst.dport);
}

void decode_tcp(const struct hdr_tcp *packet, struct pkt_record *pkt)
{
	unsigned int size_tcp = (TH_OFF(packet) * 4);

	if (size_tcp < 20) {
		printf(" *** Invalid TCP header length: %u bytes\n", size_tcp);
		return;
	}

	pkt->proto = IPPROTO_TCP;
	pkt->src.sport = ntohs(packet->th_sport);
	pkt->dst.dport = ntohs(packet->th_dport);
}

void decode_udp(const struct hdr_udp *packet, struct pkt_record *pkt)
{
	pkt->proto = IPPROTO_UDP;
	pkt->src.sport = 0;
	pkt->dst.dport = 0;
}

void decode_icmp(const struct hdr_icmp *packet, struct pkt_record *pkt)
{
	pkt->proto = IPPROTO_ICMP;
	pkt->src.sport = 0;
	pkt->dst.dport = 0;
}

void decode_ip(const struct hdr_ip *packet, struct pkt_record *pkt)
{
	const void *next; /* IP Payload */
	unsigned int size_ip;

	size_ip = IP_HL(packet) * 4;
	if (size_ip < 20) {
		fprintf(stderr, " *** Invalid IP header length: %u bytes\n",
		        size_ip);
		return;
	}
	next = ((uint8_t *)packet + size_ip);

	pkt->src.src_ip = (packet->ip_src);
	pkt->dst.dst_ip = (packet->ip_dst);

	/* IP proto TCP/UDP/ICMP */
	switch (packet->ip_p) {
	case IPPROTO_TCP:
		decode_tcp(next, pkt);
		break;
	case IPPROTO_UDP:
		decode_udp(next, pkt);
		break;
	case IPPROTO_ICMP:
		decode_icmp(next, pkt);
		break;
	default:
		fprintf(stderr, " *** Protocol [0x%x] unknown\n", packet->ip_p);
		break;
	}
}

int bytes_cmp(struct pkt_record *p1, struct pkt_record *p2)
{
	return (p2->len - p1->len);
}

#define TOP_N_LINE_OFFSET 5
#define DEST_COL_OFFSET 40
void print_top_n(int stop)
{
	struct pkt_record *r;
	int row, rowcnt = stop;

	mvprintw(TOP_N_LINE_OFFSET, 0,
	         "%15s:%-6s %9s", "Sources", "port", "bytes");

	for(row = 1, r = src_table; r != NULL && rowcnt--; r = r->hh.next) {
		mvprintw(TOP_N_LINE_OFFSET + row++, 0,
		         "%15s:%-6d %9d",
		         inet_ntoa(r->src.src_ip), r->src.sport, r->len);
	}

	rowcnt = stop;
	mvprintw(TOP_N_LINE_OFFSET, DEST_COL_OFFSET,
	         "%15s:%-6s %9s","Destinations", "port", "bytes");

	for(row = 1, r = dst_table; r != NULL && rowcnt--; r = r->hh.next) {
		mvprintw(TOP_N_LINE_OFFSET + row++, DEST_COL_OFFSET,
		         "%15s:%-6d %9d",
		         inet_ntoa(r->dst.dst_ip), r->dst.dport, r->len);
	}
}

void update_stats_tables(struct pkt_record *pkt)
{
	struct pkt_record *table_entry;

	print_pkt(pkt);

	/* Update the Source accounting table */
	/* id already in the hash? */
	HASH_FIND_INT(src_table, &(pkt->src), table_entry);
	if (!table_entry) {
		table_entry = (struct pkt_record*)malloc(sizeof(struct pkt_record));
		memcpy(table_entry, pkt, sizeof(struct pkt_record));
		HASH_ADD_INT(src_table, src, table_entry);
	} else {
		table_entry->len += pkt->len;
	}

	HASH_SORT(src_table, bytes_cmp);

	/* Update the destination accounting table */
	table_entry = NULL;
	/* id already in the hash? */
	HASH_FIND_INT(dst_table, &(pkt->dst), table_entry);
	if (!table_entry) {
		table_entry = (struct pkt_record*)malloc(sizeof(struct pkt_record));
		memcpy(table_entry, pkt, sizeof(struct pkt_record));
		HASH_ADD_INT(dst_table, dst, table_entry);
	} else {
		table_entry->len += pkt->len;
	}

	HASH_SORT(dst_table, bytes_cmp);

}

void decode_packet(uint8_t *user, const struct pcap_pkthdr *h,
                   const uint8_t *packet)
{
	const struct hdr_ethernet *ethernet;
	const struct hdr_ip *ip; /* The IP header */
	uint32_t size_ether;
	struct pkt_record *pkt;

	pkt = malloc(sizeof(struct pkt_record));
	zero_pkt(pkt);

	pkt->ts_sec = h->ts.tv_sec;
	pkt->ts_usec = h->ts.tv_usec;
	pkt->len = h->len;

	/* Ethernet header */
	ethernet = (struct hdr_ethernet *)packet;

	switch (ntohs(ethernet->type)) {
	case ETHERTYPE_IP:
		size_ether = HDR_LEN_ETHER;
		break;
	case ETHERTYPE_VLAN:
		size_ether = HDR_LEN_ETHER_VLAN;
		break;
	case ETHERTYPE_IPV6:
		printf("IPv6 ignored\n");
		return;
	case ETHERTYPE_ARP:
		printf("ARP ignored\n");
		return;
	default:
		/* we don't know how to decode other types right now. */
		fprintf(stderr, "EtherType [0x%04x] ignored\n",
		        ntohs(ethernet->type));
		return;
	}

	/* IP header */
	ip = (struct hdr_ip *)(packet + size_ether);

	decode_ip(ip, pkt);

	update_stats_tables(pkt);

	free(pkt);

	print_top_n(5);
}

void grab_packets(int fd, pcap_t *handle)
{
	struct timespec timeout_ts = {.tv_sec = 0, .tv_nsec = 1E8 };
	struct pollfd fds[] = {
		{.fd = fd, .events = POLLIN, .revents = POLLHUP }
	};

	int ch;

	while (1) {
		if (ppoll(fds, 1, &timeout_ts, NULL)) {
			pcap_dispatch(handle, 0, decode_packet, NULL);
		}

		if ((ch = getch()) == ERR) {
			/* normal case - no input */
			;
		}
		else {
			switch (ch) {
			case 'q':
				endwin();  /* End curses mode */
				return;
			}
		}
		refresh(); /* ncurses screen update */
	}
}

void init_curses()
{
	initscr();            /* Start curses mode              */
	raw();                /* Line buffering disabled        */
	keypad(stdscr, TRUE); /* We get F1, F2 etc..            */
	noecho();             /* Don't echo() while we do getch */
	nodelay(stdscr, TRUE);
}

int main(int argc, char *argv[])
{
	char *dev, errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *handle;
	int selectable_fd;

	if (argc == 2) {
		dev = argv[1];
	} else {
		dev = pcap_lookupdev(errbuf);
	}

	if (dev == NULL) {
		fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
		return (2);
	}

	handle = pcap_open_live(dev, BUFSIZ, 1, 0, errbuf);
	if (handle == NULL) {
		fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
		return (2);
	}

	if (pcap_datalink(handle) != DLT_EN10MB) {
		fprintf(stderr, "Device %s doesn't provide Ethernet headers - "
		                "not supported\n",
		        dev);
		return (2);
	}

	if (pcap_setnonblock(handle, 1, errbuf) != 0) {
		fprintf(stderr, "Non-blocking mode failed: %s\n", errbuf);
		return (2);
	}

	selectable_fd = pcap_get_selectable_fd(handle);
	if (-1 == selectable_fd) {
		fprintf(stderr, "pcap handle not selectable.\n");
		return (2);
	}

	init_curses();
	mvprintw(0, 0, "Device: %s\n", dev);

	grab_packets(selectable_fd, handle);

	/* And close the session */
	pcap_close(handle);
	return 0;
}
