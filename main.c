/* This program is for some kind of proxying 
 * It takes three arguments 1. "interface" 2. "source" 3. "destination"
 * Than it sends igmp join message to "source" via "interface" and received
 * packets sent to "destination"
 * 
 * This is Stable Version
 */

#include	<stdio.h>
#include	<netgraph.h>
#include	<stdlib.h>
#include	<err.h>
#include	<errno.h>
#include	<sys/time.h>
#include	<sys/types.h>
#include	<sys/socket.h>
#include 	<netinet/in.h>
#include 	<arpa/inet.h>
#include	<sys/stat.h>
#include	<netgraph.h>
#include	<netgraph/ng_message.h>
#include	<netgraph/ng_socket.h>
#include	<netgraph/ng_ksocket.h>
#include	<netgraph/ng_hub.h>
#include	<netdb.h>
#include	<string.h>
#include	<strings.h>
#include	<signal.h>
#include	<getopt.h>
#include	<syslog.h>
#include	<stdarg.h>
#include	<unistd.h>
#include 	<sys/ioctl.h>
#include	<sysexits.h>
#include	<net/if.h>
#include <regex.h>

#define	IP_LEN	17
#define	PORT_LEN	6
#define DEFAULT_PORT "1234"
#define DEFAULT_TTL 32
#define	MAX_ERROR_MSG 512

/*
What we do in ngctl syntax

ngctl -f- <<-SEQ
mkpeer . tee up left2right 
name .:up main_tee
mkpeer main_tee: ksocket left inet/dgram/udp
mkpeer main_tee: ksocket right inet/dgram/udp
name main_tee:left upstream
name main_tee:right downstream
shutdown main_tee:
msg downstream: bind inet/192.168.166.10:1234
msg downstream: connect inet/239.0.8.3:1234
msg upstream: bind inet/239.125.10.3:1234
SEQ

*/

// Internal Functions

void usage(const char *progname);
void signal_handler(int sig);
void shut_node(char path[NG_PATHSIZ]);
int keyword(const char *cp);
int add_route(int argc, char **argv);
void del_route(int argc, char **argv);
int parse_src(const char *phrase);
int parse_dst(const char *phrase);
int get_if_addr(const char *ifname, struct sockaddr_in *ip);
void dot_remove(char *p);
void show_routes(void);
char *ret_dot(char *str);

// External Functions

// Global Variables
char mip[IP_LEN], iip[IP_LEN];
int csock, dsock, srv_num;
char name[NG_PATHSIZ];
char ip[IP_LEN];
char ifname[IP_LEN];
uid_t	uid;
struct keytab {
    const char  *kt_cp;
    int kt_i;
} keywords[] = {
#define K_ADD   1
    {"add", K_ADD},
#define K_DEL   6
    {"del", K_DEL},
#define	K_SHOW	7
    {"show", K_SHOW},
    {0, 0}

};
// struct configuration
struct cfg {
	struct sockaddr_in src;
	struct sockaddr_in dst;
	struct sockaddr_in srcifip;
	struct sockaddr_in dstif;
	char up_name[NG_NODESIZ];
	char down_name[NG_NODESIZ];
} cfg;

// Main Program
int main(int argc, char **argv)
{
	int  one, ch, iflag;
	one = 1;
	iflag = 0;

	if (argc < 2)
        usage(NULL);

	bzero(&cfg, sizeof(cfg));


	// Handling Ctrl + C and other signals
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGKILL, signal_handler);
	signal(SIGQUIT, signal_handler);
	


	//get_if_addr(ifname, ip);
	// Create control socket node 
	uid = getuid();
	if (uid != 0) {
		errx(EX_NOPERM, "must be root to alter mcastrouting table");	
	}

	argc--;
	argv++;
	if (*argv != NULL)
        switch (keyword(*argv)) {
		case K_ADD:
			add_route(argc, argv);
			break;
		case K_DEL:
			del_route(argc, argv);
			break;
		case K_SHOW:
			show_routes();
			break;
		default:
			usage(*argv);
		}
	close(csock);
	close(dsock);

}

int get_if_addr(const char *ifname, struct sockaddr_in *ip)
{
	int fd;
	struct ifreq ifr;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1)
	{
		fprintf(stderr, "%s: an error has occured while opening fd: %s",
				__FUNCTION__, strerror(errno));
		return(0);
	}
	/* I want to get an IPv4 IP address */
	ifr.ifr_addr.sa_family = AF_INET;

	/* I want IP address attached to "eth0" */
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);


	if (ioctl(fd, SIOCGIFADDR, &ifr) == -1)
	{
		fprintf(stderr, "%s: An error has occured while trying get ip address of interface %s : %s",
				__FUNCTION__, ifname, strerror(errno));
		return 0;
	}

	close(fd);


	/* display result */
	//printf("%s: inet_ntoa = %s\n", __FUNCTION__, inet_ntoa(((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr));
	//ip->sin_addr = ((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr;
	ip->sin_addr.s_addr = (((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr).s_addr;

	return 1;
}

// parse src section
int parse_src(const char *phrase) {
	char *p;
	char string[82];
	char buf[82];

	memset(string, 0, sizeof(string));
	memset(buf, 0, sizeof(buf));
	strcpy(string, phrase);
	strcpy(buf, phrase);

	p = strsep((char **) &phrase, "@");
	if (phrase != NULL )
	{
		if (!inet_aton(p, &cfg.srcifip.sin_addr))
		{
			if (!get_if_addr(p, &cfg.srcifip))
			{
				fprintf(stderr, "%s: error : %s is not either a valid ip address or interface name\n",
						__FUNCTION__, p);
				return(0);
			}
		}
	}
	else
	{
		phrase = string;
	}
	// parse ip
	p = strsep((char **)&phrase, ":");
	//fprintf(stderr, "%s: parsed src_ip = %s line = %s\n", __FUNCTION__, p, buf);
	if (phrase == NULL)
	{
		fprintf(stderr, "%s: Port not specified for src",
				__FUNCTION__);
		return(0);
	}


	cfg.src.sin_family = AF_INET;

	if (!inet_aton(p, &cfg.src.sin_addr))
	{
		fprintf(stderr, "%s: fatal error: %s is not a valid ip address\n",
					__FUNCTION__, p);
		return(0);
	}
	strcpy(cfg.up_name, p);
	dot_remove(cfg.up_name);
	sprintf(cfg.up_name, "%s-up", cfg.up_name);
	/*
	fprintf(stderr, "%s: server_cfg[%d].src.sin_addr = %s\n", __FUNCTION__,
			srv_count, inet_ntoa(server_cfg[srv_count].src.sin_addr));

	*/
	if (phrase == NULL)
		cfg.src.sin_port = htons(atoi(DEFAULT_PORT));
	else
		cfg.src.sin_port = htons(atoi(phrase));
	cfg.src.sin_len = sizeof(struct sockaddr_in);
	return(1);

}

// parse dst section of server`s line
int parse_dst(const char *phrase)
{
	char *p;

	char string[82];
	char buf[82];

	memset(string, 0, sizeof(string));
	memset(buf, 0, sizeof(buf));
	strcpy(string, phrase);
	strcpy(buf, phrase);

	p = strsep((char **) &phrase, "@");
	if (phrase != NULL )
	{
		if (!inet_aton(p, &cfg.dstif.sin_addr))
		{
			if (!get_if_addr(p, &cfg.dstif))
			{
				fprintf(stderr,
						"%s: error : %s is not either a valid ip address or interface name\n",
						__FUNCTION__, p);
				return (0);
			}
			cfg.dstif.sin_family = AF_INET;
			//cfg.dstif.sin_port = 0;
			cfg.dstif.sin_port = htons(atoi(DEFAULT_PORT));
			cfg.dstif.sin_len = sizeof(struct sockaddr_in);
		}
	}
	else
	{
		phrase = string;
	}
	
	p = strsep( (char **)&phrase, ":");
	
	cfg.dst.sin_family = AF_INET;
	strcpy(cfg.down_name, p);
	dot_remove(cfg.down_name);
	if(!inet_aton(p, &cfg.dst.sin_addr))
	{
		fprintf(stderr, "%s: fatal error: %s is not a valid ip address\n",
					__FUNCTION__, p);
		return(0);
	}
	if (phrase == NULL)
		cfg.dst.sin_port = htons(atoi(DEFAULT_PORT));
	else
		cfg.dst.sin_port = htons(atoi(phrase));

	cfg.dst.sin_len = sizeof(struct sockaddr_in);
	return(1);
}

void dot_remove (char *p)
{
	int i = 0;
	char buf[NG_NODESIZ];
	bzero(buf, sizeof(buf));

	while (i < strlen(p))
	{
		if (p[i] == '.')
			buf[i] = '-';
		else
			buf[i] = p[i];
		i++;
	}
	strcpy(p, buf);
}
// Add route It`s not acctualy routing it just creates 
// two ksocket nodes connect`s it and send igmp join to one of them 
int add_route(int argc, char **argv) {
	char path[NG_PATHSIZ], name[NG_PATHSIZ], pth[NG_PATHSIZ];
	char *ourhook, *peerhook;
	int one = 1;
	u_char ttl;
	struct ngm_mkpeer mkp;
	struct ngm_connect con;

	union
	{
	    u_char buf[sizeof(struct ng_ksocket_sockopt) + sizeof(struct ip_mreq)];
	    struct ng_ksocket_sockopt sockopt;
	} sockopt_buf;

	union
	{
		u_char buf[sizeof(struct ng_ksocket_sockopt) + sizeof(u_char)];
		struct ng_ksocket_sockopt sockopt;
	} new_sockopt_buf;

	struct ng_ksocket_sockopt * const sockopt = &sockopt_buf.sockopt;
	struct ng_ksocket_sockopt *const opt = &new_sockopt_buf.sockopt;

	struct ip_mreq ip_mreq;
	// Args is vlan9 239.125.10.3:1234 239.0.8.3
	//printf("argc = %d, argv = %s\n", argc, *argv);
	if (argc != 3) {
		usage(NULL);
	}

	//Read ip port to variables argument processing 
	int i, j, portflag;
	j = i = portflag = 0;
	--argc;
	parse_dst(argv[argc]);
	--argc;
	parse_src(argv[argc]);

	sprintf(name, "mcastroute%d", getpid());
	if (NgMkSockNode(name, &csock, &dsock) < 0)
	{
		fprintf(stderr, "%s(): Creation of Ngsocket Failed: %s\n",
				__FUNCTION__, strerror(errno));
		exit(EXIT_FAILURE);
	}
	shut_node(cfg.up_name);
	shut_node(cfg.down_name);
	/* Create two ksocket nodes for restream purposes
	*  make them konnected via via tee
	*/
	
	// Create ng_tee with lef2right socket
    sprintf(path, ".");
	memset(&mkp, 0, sizeof(mkp));
    sprintf(mkp.type, "%s", "tee");
    sprintf(mkp.ourhook,  "%s", "tmp");
    sprintf(mkp.peerhook, "%s", "left2right");

    if (NgSendMsg(csock, path, NGM_GENERIC_COOKIE, NGM_MKPEER, &mkp,
            sizeof(mkp)) < 0)
    {
        fprintf(stderr,
                "mkpeer . hub tmp tmp Creating and connecting node error: %s\n"
				, strerror(errno));
        return(0);
    }
	// name  temp_tee node
	sprintf(path, "%s", ".:tmp");
	sprintf(name, "%s", "temp_tee");
	if (NgNameNode(csock, path, "%s", name) < 0)
    {
        fprintf(stderr, "Naming Node %s failed: %s\n", 
                name, strerror(errno));
	}
	// mkpeer temp_tee: ksocket left inet/dgram/udp
	
	sprintf(path, "temp_tee:");
	memset(&mkp, 0, sizeof(mkp));
    sprintf(mkp.type, "%s", "ksocket");
    sprintf(mkp.ourhook, "%s", "left");
    sprintf(mkp.peerhook, "%s", "inet/dgram/udp");
	if (NgSendMsg(csock, path, NGM_GENERIC_COOKIE, NGM_MKPEER, &mkp,
           sizeof(mkp)) < 0)
    {
        fprintf(stderr,
                "mkpeer error  :%s\n"
                , strerror(errno));
        return(0);
    }


	// Connect ng_tee right to another ksocket
	sprintf(path, "temp_tee:");
    memset(&mkp, 0, sizeof(mkp));
    sprintf(mkp.type, "%s", "ksocket");
    sprintf(mkp.ourhook, "%s", "right");
    sprintf(mkp.peerhook, "%s", "inet/dgram/udp");
    if (NgSendMsg(csock, path, NGM_GENERIC_COOKIE, NGM_MKPEER, &mkp,
           sizeof(mkp)) < 0)
    {
        fprintf(stderr,
                "main(): mkpeer error  :%s\n"
                , strerror(errno));
        return(0);
    }
	// name  ksocket_node upstream
    sprintf(path, "%s", "temp_tee:left");
    sprintf(name, "%s", cfg.up_name);
    if (NgNameNode(csock, path, "%s", name) < 0)
    {
        fprintf(stderr, "main(): Naming Node %s failed: %s\n",
                name, strerror(errno));
    }
	// name  ksocket_node upstream
    sprintf(path, "%s", "temp_tee:right");
    sprintf(name, "%s", cfg.down_name);
    if (NgNameNode(csock, path, "%s", name) < 0)
    {
        fprintf(stderr, "Naming Node %s failed: %s\n",
                name, strerror(errno));
    }

	// Shutdown ng_tee node to make ksockets upstream and downstream connected
	sprintf(path, "%s", "temp_tee:");
	shut_node(path);
	// Bind ksocket nodes to particular multicast addresses
	// msg upstream: bind inet/239.125.10.3:1234
	sprintf(path, "%s:", cfg.up_name);


    if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_BIND,
            (struct sockaddr*) &cfg.src, sizeof(struct sockaddr)) < 0)
    {
        //NgAllocRecvMsg(csock, &m, pth);
        fprintf(stderr, "main(): BIND FAILED %s\n",
                strerror(errno));
        return 0;
    }
	
	// msg downstream: bind inet/192.168.166.10:1234
	//=================================================================================


    sprintf(path, "%s:", cfg.down_name);
	// DOWNSTREAM REUSEADDR REUSEPORT
	// setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) < 0)
	memset(&sockopt_buf, 0, sizeof(sockopt_buf));

	sockopt->level = SOL_SOCKET;
	sockopt->name = SO_REUSEPORT;
	memcpy(sockopt->value, &one, sizeof(int));
	if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_SETOPT, sockopt,
			sizeof(sockopt_buf)) == -1)
	{
		fprintf(stderr, "Sockopt SO_REUSEPORT set failed : %s",
				strerror(errno));
		return 0;
	}
	// And now bind to socket
    if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_BIND,
            (struct sockaddr*) &cfg.dstif, sizeof(struct sockaddr)) < 0)
    {
        fprintf(stderr, "%s: bind to %s:%d failed : %s\n",
                __FUNCTION__, inet_ntoa(cfg.dstif.sin_addr),
               ntohs(cfg.dstif.sin_port), strerror(errno));
        return 0;
	}

	// Set ttl of outgoing packets for downstream

    sockopt->level = IPPROTO_IP;
	sockopt->name = IP_MULTICAST_TTL;
	ttl = DEFAULT_TTL;
	memcpy(sockopt->value, &ttl, sizeof(u_char));

	if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_SETOPT, sockopt,
			sizeof(sockopt_buf)) == -1)
	{
		fprintf(stderr, "Sockopt IP_MULTICAST_TTL set failed : %s\n",
				strerror(errno));
		return 0;
	}

	sockopt->name = IP_MULTICAST_IF;
	memcpy(sockopt->value, &cfg.dstif.sin_addr, sizeof(struct in_addr));

	if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_SETOPT, sockopt,
			sizeof(sockopt_buf)) == -1)
	{
		fprintf(stderr, "Sockopt IP_MULTICAST_IF set failed : %s\n",
				strerror(errno));
		return 0;
	}
    //================================================================================
	// msg downstream connect inet/239.0.8.3:1234

    if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_CONNECT,
            (struct sockaddr*) &cfg.dst, sizeof(struct sockaddr)) < 0)
    {
        fprintf(stderr, "%s: CONNECT to %s:%d FAILED %s\n",
                __FUNCTION__, inet_ntoa(cfg.dst.sin_addr),
                cfg.dst.sin_port, strerror(errno));
        return 0;
    }

	// UPSTREAM REUSEADDR REUSEPORT
    // setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) < 0)
	sprintf(path, "%s:", cfg.up_name);
    memset(&sockopt_buf, 0, sizeof(sockopt_buf));

    sockopt->level = SOL_SOCKET;
    sockopt->name = SO_REUSEPORT;
    memcpy(sockopt->value, &one, sizeof(int));
    if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_SETOPT, sockopt,
            sizeof(sockopt_buf)) == -1)
    {
        fprintf(stderr, "Sockopt set failed : %s" ,strerror(errno));
        return 0;
    }


	sprintf(path, "%s:", cfg.up_name);
	
    memset(&sockopt_buf, 0, sizeof(sockopt_buf));
    memset(&ip_mreq, 0, sizeof(ip_mreq));

    sockopt->level = IPPROTO_IP;
    sockopt->name = IP_ADD_MEMBERSHIP;
    ip_mreq.imr_multiaddr.s_addr = cfg.src.sin_addr.s_addr;
    ip_mreq.imr_interface.s_addr = cfg.srcifip.sin_addr.s_addr;
    memcpy(sockopt->value, &ip_mreq, sizeof(ip_mreq));

    if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_SETOPT,
            sockopt, sizeof(sockopt_buf)) < 0)
    {
        fprintf(stderr, "main(): Failed ADD MEMBERSHIP %s to %s: %s\n",
                 path, iip, strerror(errno));
        fprintf(stderr,
                "main(): ip_mreq.imr_multiaddr.s_addr = %s ip_mreq.imr_interface.s_addr = %s\n",
                 iip, mip);
        return EXIT_FAILURE;
    }
    /*
	fprintf(stderr,
            "main(): Register in mgroup = %s success interface = %s\n",
             iip, mip);
    */
	return EXIT_SUCCESS;
}

// Del route 
void del_route(int argc, char **argv) {
	char name[NG_PATHSIZ];
	int i, j, portflag;
	if (NgMkSockNode(name, &csock, &dsock) < 0)
	{
		fprintf(stderr, "%s(): Creation of Ngsocket Failed: %s\n",
				__FUNCTION__, strerror(errno));
		exit(EXIT_FAILURE);
	}
    j = i = portflag = 0;
	memset(name, 0, sizeof(name));
    --argc;
    while (i < strlen(argv[argc])) {
        switch (argv[argc][i]) {
        case '.':
            name[i] = '-';
            break;
        default:
           name[i] = argv[argc][i];
        }
        i++;
    }
	sprintf(name, "%s-up:", name);
	shut_node(name);
	exit(EXIT_SUCCESS);
}

// Keyword
int
keyword(const char *cp)
{
    struct keytab *kt = keywords;

    while (kt->kt_cp != NULL && strcmp(kt->kt_cp, cp) != 0)
        kt++;
    return (kt->kt_i);
}
// Print active routes
void show_routes(void)
{
	struct ng_mesg *resp, *resp1;
	struct namelist *nlist;
	struct hooklist *llist;
	struct nodeinfo *ninfo, *linfo;
	char *string, path[NG_PATHSIZ];
	int i;
	regex_t *preg;
	regmatch_t pmatch[2];

	/*
	 int regcomp(regex_t *preg, const char *regex, int cflags);
	 int regexec(const regex_t *preg, const char *string, size_t nmatch,
	 regmatch_t pmatch[], int eflags);

	 */
	char name[NG_PATHSIZ];
	char strCopy[32], strCopy2[32];
	char result[32];
	bzero(path, sizeof(path));
	preg = (regex_t *) malloc(sizeof(regex_t));
	string = "([0-9]{1,3}(-[0-9]{1,3}){3})-up";

	int status = regcomp(preg, string, REG_EXTENDED);
	if (status != 0)
	{
		char error_message[MAX_ERROR_MSG];
		regerror(status, preg, error_message, MAX_ERROR_MSG);
		printf("Regex error compiling '%s': %s\n", string, error_message);
		exit(EXIT_FAILURE);
	}

	bzero(name, sizeof(name));
	bzero(strCopy, sizeof(strCopy));
	bzero(strCopy2, sizeof(strCopy2));
	sprintf(name, "listsock-%d", getpid());

	if (NgMkSockNode(name, &csock, &dsock) < 0)
	{
		fprintf(stderr, "%s: Creation of Ngsocket Failed: %s\n", __FUNCTION__,
				strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (NgSendMsg(csock, ".", NGM_GENERIC_COOKIE, NGM_LISTNODES, NULL, 0) < 0)
	{
		printf("%s : send msg failed: %s\n", __FUNCTION__, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (NgAllocRecvMsg(csock, &resp, NULL) < 0)
	{
		printf("%s : Failed to receive response: %s\n", __FUNCTION__,
				strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Show each node */

	nlist = (struct namelist *) resp->data;
	//printf("There are %d total %snodes:\n",
	//    nlist->numnames, "");
	ninfo = nlist->nodeinfo;

	while (nlist->numnames > 0)
	{
		if (regexec(preg, ninfo->name, 2, pmatch, 0) == 0)
		{
			sprintf(path, "[%08x]:", ninfo->id);
			if (NgSendMsg(csock, path, NGM_GENERIC_COOKIE, NGM_LISTHOOKS, NULL,
					0) < 0)
			{
				printf("%s(): error %s\n", __FUNCTION__, strerror(errno));
				exit(EXIT_FAILURE);
			}
			if (NgAllocRecvMsg(csock, &resp1, NULL) < 0)
			{
				printf("%s(): error %s\n", __FUNCTION__, strerror(errno));
				exit(EXIT_FAILURE);
			}

			llist = (struct hooklist *) resp1->data;
			linfo = &llist->nodeinfo;
			
			strcpy(strCopy, ninfo->name);
			strCopy[pmatch[1].rm_eo] = 0;
			printf("%s -> %s \n", ret_dot(strCopy),
					ret_dot(llist->link[0].nodeinfo.name), pmatch[1].rm_so, pmatch[1].rm_eo);
			free(resp1);
		}
		nlist->numnames--;
		ninfo++;
	}
	free(resp);

}
// Group membership report send
int add_mgroup(int srv_num)
{

    /*
     setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

     */
	/*
    union
    {
        u_char buf[sizeof(struct ng_ksocket_sockopt) + sizeof(struct ip_mreq)];
        struct ng_ksocket_sockopt sockopt;
    } sockopt_buf;
    struct ng_ksocket_sockopt * const sockopt = &sockopt_buf.sockopt;
    struct ip_mreq ip_mreq;
    char servsock[NG_PATHSIZ];

    sprintf(servsock, "%s-upstream:", names[srv_num]);

    memset(&sockopt_buf, 0, sizeof(sockopt_buf));
    memset(&ip_mreq, 0, sizeof(ip_mreq));

    sockopt->level = IPPROTO_IP;
    sockopt->name = IP_ADD_MEMBERSHIP;
    ip_mreq.imr_multiaddr.s_addr = inet_addr((const char *) inp_ip[srv_num]);
    ip_mreq.imr_interface.s_addr = inet_addr((const char *) mifip[srv_num]);
    memcpy(sockopt->value, &ip_mreq, sizeof(ip_mreq));

    if (NgSendMsg(csock, servsock, NGM_KSOCKET_COOKIE, NGM_KSOCKET_SETOPT,
            sockopt, sizeof(sockopt_buf)) < 0)
    {
        fprintf(stderr, "add_mgroup(%d): Failed ADD MEMBERSHIP %s to %s: %s\n",
                srv_num, servsock, inp_ip[srv_num], strerror(errno));
        fprintf(stderr,
                "add_mgroup(%d): ip_mreq.imr_multiaddr.s_addr = %s ip_mreq.imr_interface.s_addr = %s\n",
                srv_num, inp_ip[srv_num], mifip[srv_num]);
        return EXIT_FAILURE;
    }
    fprintf(stderr,
            "add_mgroup(%d): Register in mgroup = %s success interface = %s\n",
            srv_num, inp_ip[srv_num], mifip[srv_num]);
    return EXIT_SUCCESS;
	*/
	return EXIT_SUCCESS;
}

/*
 Subs to handle signals
 */
void signal_handler(int sig)
{
	switch (sig)
	{
	case SIGTERM:
		fprintf(stderr, "%s(): Caught SIGTERM shutting down\n", __FUNCTION__);
		exit(1);
		break;
	default:
		fprintf(stderr, "%s(): %s signal catched closing all\n",
				__FUNCTION__, strsignal(sig));
		exit(1);
		break;
	}

}

// Shutdown Single node
void shut_node(char path[NG_PATHSIZ])
{
	char name[NG_PATHSIZ];
	int i = 0;
	memset(name, 0, sizeof(name));
	while (i < strlen(path)) {
		name[i] = path[i];
		i++;
	}

	if (name[strlen(name)-1] != ':') {
		sprintf(name, "%s:", name);
	}
	//NgSetDebug(4);
	if (NgSendMsg(csock, name, NGM_GENERIC_COOKIE, NGM_SHUTDOWN, NULL, 0) < 0)
	{
		if (errno == ENOENT) {
			
		} else {
			fprintf(stderr, "shut_node(): Error shutdowning %s: %s\n", 
				name, strerror(errno));
			//return void;	
		}
	}
	//return void;
}
// USAGE Subroutine
void usage(const char *cp)
{
	if (cp != NULL) {
		warnx("bad keyword: %s", cp);	
	}
	fprintf(stderr, "\
mcastroute -i vlanXX (add|del) SRC_IP:PORT DST_IP\n\
Example: mcastroute  add vlan2007@239.0.3.5:1234 vlan9@239.0.8.3:1122\n\
get traffic from vlan2007 239.0.3.5:1234 and send it to 239.0.8.3:1122 with source vlan9\n\
\t mcastroute del 239.0.8.3\n");
 
	exit(EX_USAGE);
}

char *ret_dot(char *str)
{
	int i;
	for (i = 0; i < strlen(str); i++)
	{
		if (str[i] == '-')
			str[i] = '.';
	}
	return str;
}
