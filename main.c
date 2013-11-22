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
#include	<netinet/in.h>
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

#define	IP_LEN	17
#define	PORT_LEN	6
#define DEFAULT_PORT "1234"

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
void get_if_addr(const char* ifname, char ip[IP_LEN]);
int keyword(const char *cp);
int add_route(int argc, char **argv);
void del_route(int argc, char **argv);
// External Functions

// Global Variables
char mip[IP_LEN], iip[IP_LEN];
int csock, dsock, srv_num;
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
    {0, 0}
};
// Main Program
int main(int argc, char **argv)
{
	extern int csock, dsock;
	int  one, ch, iflag;
	char name[NG_PATHSIZ];
	one = 1;
	iflag = 0;

	if (argc < 2)
        usage(NULL);

	while ((ch = getopt(argc, argv, "i:?")) != -1)
        switch(ch) {
        case 'i':
            iflag = 1;
			sprintf(ifname, "%s", optarg); 
            break;
        case '?':
        default:
            usage(argv[0]);
        }
    argc -= optind;
    argv += optind;


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
	if (NgMkSockNode(name, &csock, &dsock) < 0)
	{
		fprintf(stderr, "main(): Creation of Ngsocket Failed: %s\n",
				strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (*argv != NULL)
        switch (keyword(*argv)) {
		case K_ADD:
			if (iflag == 0) {
				printf("-i key is nesessery when adding mroute\n");
				usage(NULL);
			}
			add_route(argc, argv);
			break;
		case K_DEL:
			del_route(argc, argv);
			break;
		default:
			usage(*argv);
		}
	close(csock);
	close(dsock);

}
// Function make ip address in form char *s[] = "239.0.0.1"
void get_if_addr(const char *ifname, char ip[IP_LEN]) {
	int fd;
	struct ifreq ifr;
	char value[IP_LEN];
	
	memset(value, 0, sizeof(value));
	fd = socket(AF_INET, SOCK_DGRAM, 0);

	/* I want to get an IPv4 IP address */
	ifr.ifr_addr.sa_family = AF_INET;

	/* I want IP address attached to "eth0" */
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);

	ioctl(fd, SIOCGIFADDR, &ifr);

	close(fd);

	/* display result */
	sprintf(ip, "%s", inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

}
// Add route It`s not acctualy routing it just creates 
// two ksocket nodes connect`s it and send igmp join to one of them 
int add_route(int argc, char **argv) {
	char path[NG_PATHSIZ], name[NG_PATHSIZ], pth[NG_PATHSIZ];
	char down_name[NG_PATHSIZ], up_name[NG_PATHSIZ];
	char down_ip[IP_LEN], up_ip[IP_LEN];
	char src_port[PORT_LEN], dst_port[PORT_LEN];
	char *ourhook, *peerhook;
	int one = 1;
	struct ngm_mkpeer mkp;
	struct ngm_connect con;
	struct sockaddr_in addr;
	union
	{
	    u_char buf[sizeof(struct ng_ksocket_sockopt) + sizeof(struct ip_mreq)];
	    struct ng_ksocket_sockopt sockopt;
	} sockopt_buf;
	struct ng_ksocket_sockopt * const sockopt = &sockopt_buf.sockopt;
	struct ip_mreq ip_mreq;
	// Args is vlan9 239.125.10.3:1234 239.0.8.3
	//printf("argc = %d, argv = %s\n", argc, *argv);
	if (argc != 3) {
		usage(NULL);
	}
	
	memset(src_port, 0, sizeof(src_port));
	memset(dst_port, 0, sizeof(dst_port));
	memset(down_ip, 0, sizeof(down_ip));
	memset(up_ip, 0, sizeof(up_ip));
	memset(down_name, 0, sizeof(down_name));
	memset(up_name, 0, sizeof(up_name));
	
	//Read ip port to variables argument processing 
	int i, j, portflag;
	j = i = portflag = 0;
	--argc;
	while (i < strlen(argv[argc])) {
		switch (argv[argc][i]) {
		case '.':
			down_name[i] = '-';
			down_ip[i] = argv[argc][i];
			break;
		case ':':
			portflag = 1;
			break;
		default:
			if (portflag == 1) {
				dst_port[j] = argv[argc][i];
				j++;
			} else {
				down_name[i] = down_ip[i] = argv[argc][i];
			}
		}
		i++;
	}
	--argc;
	j = i = portflag = 0;
	while (i < strlen(argv[argc])) {
		switch (argv[argc][i]) {
		case '.':
			up_name[i] = '-';
			up_ip[i] = argv[argc][i];
			break;
		case ':':
			portflag = 1;
			break;
		default:
			if (portflag == 1) {
				src_port[j] = argv[argc][i];
				j++;
			} else {
				up_name[i] = up_ip[i] = argv[argc][i];
			}
		}
		i++;
	}
	if (strlen(src_port) == 0) {
		sprintf(src_port, "%s", DEFAULT_PORT);
	}
	if (strlen(dst_port) == 0) {
		sprintf(dst_port, "%s", DEFAULT_PORT);
	}
	sprintf(name, "mcastroute%d", getpid());
	
	get_if_addr(ifname, mip);	
	// Shutdown the node to prevent conflicts
	shut_node(up_name);
	shut_node(down_name);
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
    sprintf(name, "%s", up_name);
    if (NgNameNode(csock, path, "%s", name) < 0)
    {
        fprintf(stderr, "main(): Naming Node %s failed: %s\n",
                name, strerror(errno));
    }
	// name  ksocket_node upstream
    sprintf(path, "%s", "temp_tee:right");
    sprintf(name, "%s", down_name);
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
	sprintf(path, "%s:", up_name);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(src_port));
	addr.sin_addr.s_addr = inet_addr(up_ip); 
	addr.sin_len = sizeof(addr);

    if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_BIND,
            (struct sockaddr*) &addr, sizeof(addr)) < 0)
    {
        //NgAllocRecvMsg(csock, &m, pth);
        fprintf(stderr, "main(): BIND FAILED %s\n",
                strerror(errno));
        return 0;
    }
	
	// msg downstream: bind inet/192.168.166.10:1234
	sprintf(path, "%s:", down_name);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(dst_port));
	addr.sin_addr.s_addr = inet_addr(mip); 
	addr.sin_len = sizeof(addr);

    if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_BIND,
            (struct sockaddr*) &addr, sizeof(addr)) < 0)
    {
        //NgAllocRecvMsg(csock, &m, pth);
        fprintf(stderr, "main(): BIND FAILED %s\n",
                strerror(errno));
        return 0;
	}
	// msg downstream connect inet/239.0.8.3:1234
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(dst_port));
    addr.sin_addr.s_addr = inet_addr(down_ip);
    addr.sin_len = sizeof(addr);

    if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_CONNECT,
            (struct sockaddr*) &addr, sizeof(addr)) < 0)
    {
        //NgAllocRecvMsg(csock, &m, pth);
        fprintf(stderr, "main(): CONNECT FAILED %s\n",
                strerror(errno));
        return 0;
    }

	// UPSTREAM REUSEADDR REUSEPORT
    // setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) < 0)
	sprintf(path, "%s:", up_name);
    memset(&sockopt_buf, 0, sizeof(sockopt_buf));

    sockopt->level = SOL_SOCKET;
    sockopt->name = SO_REUSEADDR;
    memcpy(sockopt->value, &one, sizeof(int));
    if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_SETOPT, sockopt,
            sizeof(sockopt_buf)) == -1)
    {
        fprintf(stderr, "Sockopt set failed : %s\n", strerror(errno));
        return 0;
    }
    // setsockopt(fd,SOL_SOCKET,SO_REUSEPORT,&one,sizeof(int)) < 0)
    sockopt->name = SO_REUSEPORT;
    memcpy(sockopt->value, &one, sizeof(int));
    if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_SETOPT, sockopt,
            sizeof(sockopt_buf)) == -1)
    {
        fprintf(stderr, "Sockopt set failed : %s" ,strerror(errno));
        return 0;
    }

	// DOWNSTREAM REUSEADDR REUSEPORT
    // setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) < 0)
	sprintf(path, "%s:", down_name);
    memset(&sockopt_buf, 0, sizeof(sockopt_buf));

    sockopt->level = SOL_SOCKET;
    sockopt->name = SO_REUSEADDR;
    memcpy(sockopt->value, &one, sizeof(int));
    if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_SETOPT, sockopt,
            sizeof(sockopt_buf)) == -1)
    {
        fprintf(stderr, "Sockopt SO_REUSEADDR set failed : %s\n", strerror(errno));
        return 0;
    }
    // setsockopt(fd,SOL_SOCKET,SO_REUSEPORT,&one,sizeof(int)) < 0)
    sockopt->name = SO_REUSEPORT;
    memcpy(sockopt->value, &one, sizeof(int));
    if (NgSendMsg(csock, path, NGM_KSOCKET_COOKIE, NGM_KSOCKET_SETOPT, sockopt,
            sizeof(sockopt_buf)) == -1)
    {
        fprintf(stderr, "Sockopt SO_REUSEPORT set failed : %s" ,strerror(errno));
        return 0;
    }
	// Send igmp join to upstream ksocket node
	memset(iip, 0, sizeof(iip));
	memset(mip, 0, sizeof(mip));
	sprintf(path, "%s:", up_name);
	//sprintf(mip, "%s", "192.168.200.10");
	get_if_addr(ifname, mip);	
	sprintf(iip, "%s", up_ip);
	
    memset(&sockopt_buf, 0, sizeof(sockopt_buf));
    memset(&ip_mreq, 0, sizeof(ip_mreq));

    sockopt->level = IPPROTO_IP;
    sockopt->name = IP_ADD_MEMBERSHIP;
    ip_mreq.imr_multiaddr.s_addr = inet_addr((const char *) iip);
    ip_mreq.imr_interface.s_addr = inet_addr((const char *) mip);
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
	//sprintf(name, "%s:", name);
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
		fprintf(stderr, "signal_handler(): Caught SIGTERM shutting down\n");
		exit(1);
		break;
	default:
		fprintf(stderr, "signal_handler(): %s signal catched closing all\n",
				strsignal(sig));
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
Example: mcastroute -i vlan9 add 239.125.10.3:1234 239.0.8.3\n\
\t mcastroute del 239.0.8.3\n\
interface will be used to send igmp join\n");
 
	exit(EX_USAGE);
}
