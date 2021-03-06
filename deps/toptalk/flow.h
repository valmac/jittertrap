#ifndef FLOW_H
#define FLOW_H

struct flow {
	int ethertype;
	union {
		struct {
			struct in_addr src_ip;
			struct in_addr dst_ip;
		};
		struct {
			struct in6_addr src_ip6;
			struct in6_addr dst_ip6;
		};
	};
	uint16_t sport;
	uint16_t dport;
	uint16_t proto;
};

struct flow_record {
	struct flow flow;
	uint32_t bytes;
	uint32_t packets;
};

struct flow_pkt {
	struct flow_record flow_rec;
	struct timeval timestamp;
};

#endif
