#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>

#include "mtcp.h"
#include "config.h"
#include "tcp_in.h"
#include "arp.h"
#include "debug.h"

#define MAX_OPTLINE_LEN 1024
#define MAX_PROCLINE_LEN 1024

static const char *route_file = "config/route.conf";
static const char *arp_file = "config/arp.conf";

// TODO: merging point: keyword for counting queues in GetNumQueues()
#ifdef USE_DPDK
static const char * queue_num_keyword = "igb_uio";
#else
static const char * queue_num_keyword = "xge0-rx";
#endif
/*----------------------------------------------------------------------------*/
static int 
GetIntValue(char* value)
{
	int ret = 0;
	ret = strtol(value, (char**)NULL, 10);
	if (errno == EINVAL || errno == ERANGE)
		return -1;
	return ret;
}
/*----------------------------------------------------------------------------*/
static inline uint32_t 
MaskFromPrefix(int prefix)
{
	uint32_t mask = 0;
	uint8_t *mask_t = (uint8_t *)&mask;
	int i, j;

	for (i = 0; i <= prefix / 8 && i < 4; i++) {
		for (j = 0; j < (prefix - i * 8) && j < 8; j++) {
			mask_t[i] |= (1 << (7 - j));
		}
	}

	return mask;
}
/*----------------------------------------------------------------------------*/
static void
EnrollRouteTableEntry(char *optstr)
{
	char *daddr_s;
	char *prefix;
	char *dev;
	int ifidx;
	int ridx;
	int i;

	daddr_s = strtok(optstr, "/");
	prefix = strtok(NULL, " ");
	dev = strtok(NULL, "\n");

	assert(daddr_s != NULL);
	assert(prefix != NULL);
	assert(dev != NULL);

	ifidx = -1;
	for (i = 0; i < num_devices; i++) {
		if (strcmp(dev, devices[i].name) != 0)
			continue;
		
		ifidx = devices[i].ifindex;
		break;
	}
	if (ifidx == -1) {
		TRACE_CONFIG("Interface %s does not exist!\n", dev);
		exit(4);
	}

	ridx = CONFIG.routes++;
	CONFIG.rtable[ridx].daddr = inet_addr(daddr_s);
	CONFIG.rtable[ridx].prefix = atoi(prefix);
	if (CONFIG.rtable[ridx].prefix > 32 || CONFIG.rtable[ridx].prefix < 0) {
		TRACE_CONFIG("Prefix length should be between 0 - 32.\n");
		exit(4);
	}
	
	CONFIG.rtable[ridx].mask = MaskFromPrefix(CONFIG.rtable[ridx].prefix);
	CONFIG.rtable[ridx].masked = 
			CONFIG.rtable[ridx].daddr & CONFIG.rtable[ridx].mask;
	CONFIG.rtable[ridx].nif = ifidx;
}
/*----------------------------------------------------------------------------*/
int 
SetRoutingTableFromFile() 
{
#define ROUTES "ROUTES"

	FILE *fc;
	char optstr[MAX_OPTLINE_LEN];
	int i;

	TRACE_CONFIG("Loading routing configurations from : %s\n", route_file);

	fc = fopen(route_file, "r");
	if (fc == NULL) {
		perror("fopen");
		TRACE_CONFIG("Skip loading static routing table\n");
		return -1;
	}

	while (1) {
		char *iscomment;
		int num;

		if (fgets(optstr, MAX_OPTLINE_LEN, fc) == NULL)
			break;

		//skip comment
		iscomment = strchr(optstr, '#');
		if (iscomment == optstr)
			continue;
		if (iscomment != NULL)
			*iscomment = 0;

		if (!strncmp(optstr, ROUTES, sizeof(ROUTES) - 1)) {
			num = GetIntValue(optstr + sizeof(ROUTES));
			if (num <= 0)
				break;

			for (i = 0; i < num; i++) {
				if (fgets(optstr, MAX_OPTLINE_LEN, fc) == NULL)
					break;

				if (*optstr == '#') {
					i -= 1;
					continue;
				}
				EnrollRouteTableEntry(optstr);
			}
		}
	}

	fclose(fc);
	return 0;
}
/*----------------------------------------------------------------------------*/
void
PrintRoutingTable()
{
	int i;
	uint8_t *da;
	uint8_t *m;
	uint8_t *md;

	/* print out process start information */
	TRACE_CONFIG("Routes:\n");
	for (i = 0; i < CONFIG.routes; i++) {
		da = (uint8_t *)&CONFIG.rtable[i].daddr;
		m = (uint8_t *)&CONFIG.rtable[i].mask;
		md = (uint8_t *)&CONFIG.rtable[i].masked;
		TRACE_CONFIG("Destination: %u.%u.%u.%u/%d, Mask: %u.%u.%u.%u, "
				"Masked: %u.%u.%u.%u, Route: xge%d\n", 
				da[0], da[1], da[2], da[3], CONFIG.rtable[i].prefix, 
				m[0], m[1], m[2], m[3], md[0], md[1], md[2], md[3], 
				CONFIG.rtable[i].nif);
	}
	if (CONFIG.routes == 0)
		TRACE_CONFIG("(blank)\n");

	TRACE_CONFIG("----------------------------------------------------------"
			"-----------------------\n");
}
/*----------------------------------------------------------------------------*/
static void
ParseMACAddress(unsigned char *haddr, char *haddr_str)
{
	int i;
	char *str;
	unsigned int temp;

	str = strtok(haddr_str, ":");
	i = 0;
	while (str != NULL) {
		if (i >= ETH_ALEN) {
			TRACE_CONFIG("MAC address length exceeds %d!\n", ETH_ALEN);
			exit(4);
		}
		sscanf(str, "%x", &temp);
		haddr[i++] = temp;
		str = strtok(NULL, ":");
	}
	if (i < ETH_ALEN) {
		TRACE_CONFIG("MAC address length is less than %d!\n", ETH_ALEN);
		exit(4);
	}
}
/*----------------------------------------------------------------------------*/
static int 
ParseIPAddress(uint32_t *ip_addr, char *ip_str)
{
	if (ip_str == NULL) {
		*ip_addr = 0;
		return -1;
	}

	*ip_addr = inet_addr(ip_str);
	if (*ip_addr == INADDR_NONE) {
		TRACE_CONFIG("IP address is not valid %s\n", ip_str);
		*ip_addr = 0;
		return -1;
	}
	
	return 0;
}
/*----------------------------------------------------------------------------*/
int
SetRoutingTable() 
{
	int i, ridx;
	unsigned int c;

	CONFIG.routes = 0;
	
	CONFIG.rtable = (struct route_table *)
			calloc(MAX_DEVICES, sizeof(struct route_table));
	if (!CONFIG.rtable) 
		exit(EXIT_FAILURE);
	
	/* set default routing table */
	for (i = 0; i < CONFIG.eths_num; i ++) {

		ridx = CONFIG.routes++;
		CONFIG.rtable[ridx].daddr = CONFIG.eths[i].ip_addr & CONFIG.eths[i].netmask;
			
		CONFIG.rtable[ridx].prefix = 0;
		c = CONFIG.eths[i].netmask;
		while ((c = (c >> 1))){
			CONFIG.rtable[ridx].prefix++;
		}
		CONFIG.rtable[ridx].prefix++;

		CONFIG.rtable[ridx].mask = CONFIG.eths[i].netmask;
		CONFIG.rtable[ridx].masked = CONFIG.rtable[ridx].daddr;
		CONFIG.rtable[ridx].nif = devices[i].ifindex;
	}

	/* set additional routing table */
	SetRoutingTableFromFile();

	return 0;
}
/*----------------------------------------------------------------------------*/
int 
GetNumQueues()
{
	FILE * fp;
	char buf[MAX_PROCLINE_LEN];
	int queue_cnt;
		
	// XXX:	q: why does one count queues on /proc/interrupts?
	// 		a: according to http://goo.gl/mFRKvw :
	//			"queues are intuitively named and easy to identify in
	//			/proc/interrupts output. For unpaired queues, they will
	//			appear similar to eth1-rx-0 and eth1-tx-0, while paired
	//			queues will appear like eth1-TxRx-0"

	//			basically, this file shows the affinity between queues and
	//			CPUs. e.g. in my vbox accelerator machine, before i run
	//			our custom configs/dpdk/setup script, i have the following
	//			contents for /proc/interrupts :

	//		CPU0       CPU1
	//	 0:         44          0   IO-APIC-edge      timer
	//	 1:         10          0   IO-APIC-edge      i8042
	//	 8:          0          0   IO-APIC-edge      rtc0
	//	 9:          0          0   IO-APIC-fasteoi   acpi
	//	12:        149          0   IO-APIC-edge      i8042
	//	14:          0          0   IO-APIC-edge      ata_piix
	//	15:        233          0   IO-APIC-edge      ata_piix
	//	16:         20         81   IO-APIC-fasteoi   eth0
	//	17:        977          0   IO-APIC-fasteoi   eth1, eth2
	//	21:       2644          0   IO-APIC-fasteoi   ahci, snd_intel8x0
	//	22:         27          0   IO-APIC-fasteoi   ohci_hcd:usb1

	// after i run configs/dpdk/setup --nr_hugepages 1024 --install-kni, the
	// contents change to :

	//		CPU0       CPU1
	//	 0:         44          0   IO-APIC-edge      timer
	//	 1:         10          0   IO-APIC-edge      i8042
	//	 8:          0          0   IO-APIC-edge      rtc0
	//	 9:          0          0   IO-APIC-fasteoi   acpi
	//	12:        149          0   IO-APIC-edge      i8042
	//	14:          0          0   IO-APIC-edge      ata_piix
	//	15:        471          0   IO-APIC-edge      ata_piix
	//	16:         20        225   IO-APIC-fasteoi   igb_uio
	//	17:       1338          0   IO-APIC-fasteoi   eth2, igb_uio
	//	21:       2700          0   IO-APIC-fasteoi   ahci, snd_intel8x0
	//	22:         27          0   IO-APIC-fasteoi   ohci_hcd:usb1

	// so i guess we should be looking for lines with "igb_uio"
	fp = fopen("/proc/interrupts", "r");

	if (!fp) {
		TRACE_CONFIG("Failed to read data from /proc/interrupts!\n");
		return -1;
	}

	/* count number of NIC queues from /proc/interrupts */
	queue_cnt = 0;

	while (!feof(fp)) {

		if (fgets(buf, MAX_PROCLINE_LEN, fp) == NULL)
			break;

		/* "xge0-rx" is the keyword for counting queues */
		// basically this code reads through /proc/interrupts, checks how many
		// lines are there which match the keyword (see comments above for
		// details on this)
		// TODO: do we need to change the "xge0-rx" keyword for DPDK? in DPDK's
		// case maybe "igb_uio"?
		if (strstr(buf, queue_num_keyword)) {
			queue_cnt++;
		}
	}

	fclose(fp);

	return queue_cnt;
}
/*----------------------------------------------------------------------------*/
int
SetInterfaceInfo() 
{
	struct ifreq ifr;
	int eidx = 0;
	int i, j;
	
	TRACE_CONFIG("Loading interface setting\n");
			
	CONFIG.eths =
			(struct eth_table *) calloc(
										MAX_DEVICES,
										sizeof(struct eth_table));

	if (!CONFIG.eths) 
		exit(EXIT_FAILURE);

	// TODO: merging point: we may have to bypass some ioctl() calls by using
	// DPDK-specific calls, while still filling mTCP-specific structs

	// 1) socket for ioctl() calls
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

	if (sock == -1) {
		perror("socket");
	}

	for (i = 0; i < num_devices; i++) {

		// i) DPDK-specific call to get eth device info
		rte_eth_dev_info_get((uint8_t) i, &devices[i]);

		// ii) dev_name: update the dev_name string in CONFIG
		// TODO: have no idea where to fetch this, note that
		// devices is now an array of struct rte_eth_dev_info. maybe the
		// driver_name field?
		//strcpy(ifr.ifr_name, devices[i].name);

		// ii.1) build up a if_name using the same format as shown in pspgen.c,
		// line 1083, available here: http://goo.gl/a7ibVE
		char if_name[64] = '\0';
		snprintf(if_name, 64, "%s.%d", devices[i].driver_name, i);

		// ii.2) copy if_name to ifr.ifr_name
		strcpy(ifr.ifr_name, if_name);

		// iii) the subsequent ioctl() calls add more info to that gathered
		// via rte_eth_dev_info_get
		if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
			
			eidx = CONFIG.eths_num++;
			strcpy(CONFIG.eths[eidx].dev_name, ifr.ifr_name);

			// iv) ifindex: i? devices[i].pci_dev->addr.devid?
			//CONFIG.eths[eidx].ifindex = devices[i].ifindex;
			CONFIG.eths[eidx].ifindex = i;
			
			// v) ip_addr: not gathered in rte_eth_dev_info_get()
			if (ioctl(sock, SIOCGIFADDR, &ifr) == 0 ) {
				struct in_addr sin =
						((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr;
				CONFIG.eths[eidx].ip_addr = *(uint32_t *) &sin;
			}

			// vi) haddr: now a ether_addr struct
			if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0 ) {

//				for (j = 0; j < 6; j ++) {
//					CONFIG.eths[eidx].haddr[j] = ifr.ifr_addr.sa_data[j];
//				}
				rte_eth_macaddr_get((uint8_t) i, &CONFIG.eths[eidx].haddr);
			}
			
			// vii) netmask:
			if (ioctl(sock, SIOCGIFNETMASK, &ifr) == 0) {
				struct in_addr sin =
						((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr;
				CONFIG.eths[eidx].netmask = *(uint32_t *) &sin;
			}

			// viii) add to attached devices
			for (j = 0; j < num_devices_attached; j++) {
				if (devices_attached[j] == devices[i].ifindex) {
					break;
				}
			}

			devices_attached[num_devices_attached] = devices[i].ifindex;
			num_devices_attached++;

		} else { 

			perror("SIOCGIFFLAGS");
		}
	}

	// TODO: this will have to be changed: either (1) change num_queues to
	// hold the values returned by rte_eth_dev_info_get() or (2) pass an
	// input argument to GetNumQueues() which specifies what is the keyword
	// in /proc/interrupts we need to look for.

	// in DPDK examples, e.g. l2fwd, this value is defined as the 'number of
	// RX queues per core', it is given as an argument, default value is 1
	num_queues = GetNumQueues();

	if (num_queues <= 0) {

		TRACE_CONFIG("Failed to find NIC queues!\n");

		return -1;
	}

	if (num_queues > num_cpus) {

		TRACE_CONFIG("Too many NIC queues available.\n");

		return -1;
	}

	return 0;
}
/*----------------------------------------------------------------------------*/
void
PrintInterfaceInfo() 
{
	int i;
		
	/* print out process start information */
	TRACE_CONFIG("Interfaces:\n");
	for (i = 0; i < CONFIG.eths_num; i++) {
			
		uint8_t *da = (uint8_t *)&CONFIG.eths[i].ip_addr;
		uint8_t *nm = (uint8_t *)&CONFIG.eths[i].netmask;

		TRACE_CONFIG("name: %s, ifindex: %d, "
				"hwaddr: %02X:%02X:%02X:%02X:%02X:%02X, "
				"ipaddr: %u.%u.%u.%u, "
				"netmask: %u.%u.%u.%u\n",
				CONFIG.eths[i].dev_name, 
				CONFIG.eths[i].ifindex, 
				CONFIG.eths[i].haddr[0],
				CONFIG.eths[i].haddr[1],
				CONFIG.eths[i].haddr[2],
				CONFIG.eths[i].haddr[3],
				CONFIG.eths[i].haddr[4],
				CONFIG.eths[i].haddr[5],
				da[0], da[1], da[2], da[3],
				nm[0], nm[1], nm[2], nm[3]);
	}
	TRACE_CONFIG("Number of NIC queues: %d\n", num_queues);
	TRACE_CONFIG("----------------------------------------------------------"
			"-----------------------\n");
}
/*----------------------------------------------------------------------------*/
static void
EnrollARPTableEntry(char *optstr)
{
	char *dip_s;		/* destination IP string */
	char *prefix_s;		/* IP prefix string */
	char *daddr_s;		/* destination MAC string */

	int prefix;
	uint32_t dip_mask;
	int idx;

	dip_s = strtok(optstr, "/");
	prefix_s = strtok(NULL, " ");
	daddr_s = strtok(NULL, "\n");

	assert(dip_s != NULL);
	assert(prefix_s != NULL);
	assert(daddr_s != NULL);

	prefix = atoi(prefix_s);

	if (prefix > 32 || prefix < 0) {
		TRACE_CONFIG("Prefix length should be between 0 - 32.\n");
		return;
	}
	
	idx = CONFIG.arp.entries++;
	CONFIG.arp.entry[idx].prefix = prefix;
	ParseIPAddress(&CONFIG.arp.entry[idx].ip, dip_s);
	ParseMACAddress(CONFIG.arp.entry[idx].haddr, daddr_s);
	
	dip_mask = MaskFromPrefix(prefix);
	CONFIG.arp.entry[idx].ip_mask = dip_mask;
	CONFIG.arp.entry[idx].ip_masked = CONFIG.arp.entry[idx].ip & dip_mask;
	
/*
	int i, cnt;
	cnt = 1;
	cnt = cnt << (32 - prefix);

	for (i = 0; i < cnt; i++) {
		idx = CONFIG.arp.entries++;
		CONFIG.arp.entry[idx].ip = htonl(ntohl(ip) + i);
		memcpy(CONFIG.arp.entry[idx].haddr, haddr, ETH_ALEN);
	}
*/
}
/*----------------------------------------------------------------------------*/
int 
LoadARPTable()
{
#define ARP_ENTRY "ARP_ENTRY"

	FILE *fc;
	char optstr[MAX_OPTLINE_LEN];
	int numEntry = 0;
	int hasNumEntry = 0;

	TRACE_CONFIG("Loading ARP table from : %s\n", arp_file);

	InitARPTable();

	fc = fopen(arp_file, "r");
	if (fc == NULL) {
		perror("fopen");
		TRACE_CONFIG("Skip loading static ARP table\n");
		return -1;
	}

	while (1) {
		char *p;
		char *temp;

		if (fgets(optstr, MAX_OPTLINE_LEN, fc) == NULL)
			break;

		p = optstr;

		// skip comment
		if ((temp = strchr(p, '#')) != NULL)
			*temp = 0;
		// remove front and tailing spaces
		while (*p && isspace((int)*p))
			p++;
		temp = p + strlen(p) - 1;
		while (temp >= p && isspace((int)*temp))
			   *temp = 0;
		if (*p == 0) /* nothing more to process? */
			continue;

		if (!hasNumEntry && strncmp(p, ARP_ENTRY, sizeof(ARP_ENTRY)-1) == 0) {
			numEntry = GetIntValue(p + sizeof(ARP_ENTRY));
			if (numEntry <= 0) {
				fprintf(stderr, "Wrong entry in arp.conf: %s\n", p);
				exit(-1);
			}
#if 0
			CONFIG.arp.entry = (struct arp_entry *)
				calloc(numEntry + MAX_ARPENTRY, sizeof(struct arp_entry));
			if (CONFIG.arp.entry == NULL) {
				fprintf(stderr, "Wrong entry in arp.conf: %s\n", p);
				exit(-1);
			}
#endif
			hasNumEntry = 1;
		} else {
			if (numEntry <= 0) {
				fprintf(stderr, 
						"Error in arp.conf: more entries than "
						"are specifed, entry=%s\n", p);
				exit(-1);
			}
			EnrollARPTableEntry(p);
			numEntry--;
		}
	}

	fclose(fc);
	return 0;
}
/*----------------------------------------------------------------------------*/
static int 
ParseConfiguration(char *line)
{
	char optstr[MAX_OPTLINE_LEN];
	char *p, *q;

	strncpy(optstr, line, MAX_OPTLINE_LEN - 1);

	p = strtok(optstr, " \t=");
	if (p == NULL) {
		TRACE_CONFIG("No option name found for the line: %s\n", line);
		return -1;
	}

	q = strtok(NULL, " \t=");
	if (q == NULL) {
		TRACE_CONFIG("No option value found for the line: %s\n", line);
		return -1;
	}

	if (strcmp(p, "num_cores") == 0) {
		CONFIG.num_cores = atoi(q);
		if (CONFIG.num_cores <= 0) {
			TRACE_CONFIG("Number of cores should be larger than 0.\n");
			return -1;
		}
		if (CONFIG.num_cores > num_cpus) {
			TRACE_CONFIG("Number of cores should be smaller than "
					"# physical CPU cores.\n");
			return -1;
		}
	} else if (strcmp(p, "max_concurrency") == 0) {
		CONFIG.max_concurrency = atoi(q);
		if (CONFIG.max_concurrency < 0) {
			TRACE_CONFIG("The maximum concurrency should be larger than 0.\n");
			return -1;
		}
	} else if (strcmp(p, "max_num_buffers") == 0) {
		CONFIG.max_num_buffers = atoi(q);
		if (CONFIG.max_num_buffers < 0) {
			TRACE_CONFIG("The maximum # buffers should be larger than 0.\n");
			return -1;
		}
	} else if (strcmp(p, "rcvbuf") == 0) {
		CONFIG.rcvbuf_size = atoi(q);
		if (CONFIG.rcvbuf_size < 64) {
			TRACE_CONFIG("Receive buffer size should be larger than 64.\n");
			return -1;
		}
	} else if (strcmp(p, "sndbuf") == 0) {
		CONFIG.sndbuf_size = atoi(q);
		if (CONFIG.sndbuf_size < 64) {
			TRACE_CONFIG("Send buffer size should be larger than 64.\n");
			return -1;
		}
	} else if (strcmp(p, "tcp_timeout") == 0) {
		CONFIG.tcp_timeout = atoi(q);
		if (CONFIG.tcp_timeout > 0) {
			CONFIG.tcp_timeout = SEC_TO_USEC(CONFIG.tcp_timeout) / TIME_TICK;
		}
	} else if (strcmp(p, "tcp_timewait") == 0) {
		CONFIG.tcp_timewait = atoi(q);
		if (CONFIG.tcp_timewait > 0) {
			CONFIG.tcp_timewait = SEC_TO_USEC(CONFIG.tcp_timewait) / TIME_TICK;
		}
	} else if (strcmp(p, "stat_print") == 0) {
		int i;

		for (i = 0; i < CONFIG.eths_num; i++) {
			if (strcmp(CONFIG.eths[i].dev_name, q) == 0) {
				CONFIG.eths[i].stat_print = TRUE;
			}
		}
	} else {
		TRACE_CONFIG("Unknown option type: %s\n", line);
		return -1;
	}

	return 0;
}
/*----------------------------------------------------------------------------*/
int 
LoadConfiguration(char *fname)
{
	FILE *fp;
	char optstr[MAX_OPTLINE_LEN];

	TRACE_CONFIG("----------------------------------------------------------"
			"-----------------------\n");
	TRACE_CONFIG("Loading mtcp configuration from : %s\n", fname);

	fp = fopen(fname, "r");
	if (fp == NULL) {
		perror("fopen");
		TRACE_CONFIG("Failed to load configuration file: %s\n", fname);
		return -1;
	}

	/* set default configuration */
	CONFIG.num_cores = num_cpus;
	CONFIG.max_concurrency = 100000;
	CONFIG.max_num_buffers = 100000;
	CONFIG.rcvbuf_size = 8192;
	CONFIG.sndbuf_size = 8192;
	CONFIG.tcp_timeout = TCP_TIMEOUT;
	CONFIG.tcp_timewait = TCP_TIMEWAIT;

	while (1) {
		char *p;
		char *temp;

		if (fgets(optstr, MAX_OPTLINE_LEN, fp) == NULL)
			break;

		p = optstr;

		// skip comment
		if ((temp = strchr(p, '#')) != NULL)
			*temp = 0;
		// remove front and tailing spaces
		while (*p && isspace((int)*p))
			p++;
		temp = p + strlen(p) - 1;
		while (temp >= p && isspace((int)*temp))
			   *temp = 0;
		if (*p == 0) /* nothing more to process? */
			continue;

		if (ParseConfiguration(p) < 0)
			return -1;
	}

	fclose(fp);

	return 0;
}
/*----------------------------------------------------------------------------*/
void 
PrintConfiguration()
{
	int i;

	TRACE_CONFIG("Configurations:\n");
	TRACE_CONFIG("Number of CPU cores available: %d\n", num_cpus);
	TRACE_CONFIG("Number of CPU cores to use: %d\n", CONFIG.num_cores);
	TRACE_CONFIG("Maximum number of concurrency per core: %d\n", 
			CONFIG.max_concurrency);

	TRACE_CONFIG("Maximum number of preallocated buffers per core: %d\n", 
			CONFIG.max_num_buffers);
	TRACE_CONFIG("Receive buffer size: %d\n", CONFIG.rcvbuf_size);
	TRACE_CONFIG("Send buffer size: %d\n", CONFIG.sndbuf_size);

	if (CONFIG.tcp_timeout > 0) {
		TRACE_CONFIG("TCP timeout seconds: %d\n", 
				USEC_TO_SEC(CONFIG.tcp_timeout * TIME_TICK));
	} else {
		TRACE_CONFIG("TCP timeout check disabled.\n");
	}
	TRACE_CONFIG("TCP timewait seconds: %d\n", 
			USEC_TO_SEC(CONFIG.tcp_timewait * TIME_TICK));
	TRACE_CONFIG("NICs to print statistics:");
	for (i = 0; i < CONFIG.eths_num; i++) {
		if (CONFIG.eths[i].stat_print) {
			TRACE_CONFIG(" %s", CONFIG.eths[i].dev_name);
		}
	}
	TRACE_CONFIG("\n");
	TRACE_CONFIG("----------------------------------------------------------"
			"-----------------------\n");
}
/*----------------------------------------------------------------------------*/
