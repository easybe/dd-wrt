/*
 * pptpclient.c
 *
 * Copyright (C) 2009 Sebastian Gottschall <gottschall@dd-wrt.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * $Id:
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>		/* AhMan March 18 2005 */
#include <sys/socket.h>
#include <sys/mount.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <wait.h>
#include <net/route.h>		/* AhMan March 18 2005 */
#include <sys/types.h>
#include <signal.h>

#include <bcmnvram.h>
#include <bcmconfig.h>
#include <netconf.h>
#include <shutils.h>
#include <utils.h>
#include <cy_conf.h>
#include <code_pattern.h>
#include <rc.h>
#include <wlutils.h>
#include <nvparse.h>
#include <syslog.h>
#include <services.h>

#ifdef HAVE_PPTP

static void create_pptp_config(char *servername, char *username)
{

	FILE *fp;

	mkdir("/tmp/ppp", 0777);
	symlink("/sbin/rc", "/tmp/ppp/ip-up");
	symlink("/sbin/rc", "/tmp/ppp/ip-down");
	symlink("/dev/null", "/tmp/ppp/connect-errors");

	/*
	 * Generate options file 
	 */
	if (!(fp = fopen("/tmp/ppp/options", "w"))) {
		perror("/tmp/ppp/options");
		return;
	}
	fprintf(fp, "defaultroute\n");	// Add a default route to the 
	// system routing tables, using the peer as the gateway
	fprintf(fp, "usepeerdns\n");	// Ask the peer for up to 2 DNS
	// server addresses
	fprintf(fp, "pty 'pptp %s --nolaunchpppd", servername);

	if (nvram_match("pptp_reorder", "1"))
		fprintf(fp, " --nobuffer");

	// PPTP client also supports synchronous mode.
	// This should improve the speeds.
	if (nvram_match("pptp_synchronous", "1"))
		fprintf(fp, " --sync'\nsync\n");
	else
		fprintf(fp, "'\n");

	fprintf(fp, "user '%s'\n", username);
	// fprintf(fp, "persist\n"); // Do not exit after a connection is terminated.

	if (nvram_match("mtu_enable", "1"))
		fprintf(fp, "mtu %s\n", nvram_safe_get("wan_mtu"));

	if (nvram_match("ppp_demand", "1")) {	// demand mode
		fprintf(fp, "idle %d\n",
			nvram_match("ppp_demand",
				    "1") ?
			atoi(nvram_safe_get("ppp_idletime")) * 60 : 0);
		fprintf(fp, "demand\n");	// Dial on demand
		fprintf(fp, "persist\n");	// Do not exit after a connection is
		// terminated.
		fprintf(fp, "%s:%s\n", PPP_PSEUDO_IP, PPP_PSEUDO_GW);	// <local 
		// IP>:<remote 
		// IP>
		fprintf(fp, "ipcp-accept-remote\n");
		fprintf(fp, "ipcp-accept-local\n");
		fprintf(fp, "connect true\n");
		fprintf(fp, "noipdefault\n");	// Disables the default
		// behaviour when no local IP 
		// address is specified
		fprintf(fp, "ktune\n");	// Set /proc/sys/net/ipv4/ip_dynaddr
		// to 1 in demand mode if the local
		// address changes
	} else {		// keepalive mode
		start_redial();
	}
	if (nvram_match("pptp_encrypt", "0")) {
		fprintf(fp, "nomppe\n");	// Disable mppe negotiation
		fprintf(fp, "noccp\n");	// Disable CCP (Compression Control
		// Protocol)
	} else {
		fprintf(fp, "mppe required,stateless\n");
	}
	fprintf(fp, "default-asyncmap\n");	// Disable asyncmap negotiation
	fprintf(fp, "nopcomp\n");	// Disable protocol field compression
	fprintf(fp, "noaccomp\n");	// Disable Address/Control compression
	fprintf(fp, "novj\n");	// Disable Van Jacobson style TCP/IP header compression
	fprintf(fp, "nobsdcomp\n");	// Disables BSD-Compress compression
	fprintf(fp, "nodeflate\n");	// Disables Deflate compression
	fprintf(fp, "lcp-echo-failure 6\n");
	fprintf(fp, "lcp-echo-interval 3\n"); // echo-request frame to the peer
	fprintf(fp, "noipdefault\n");
	fprintf(fp, "lock\n");
	fprintf(fp, "noauth\n");
	fprintf(fp, "debug\n");
	fprintf(fp, "logfd 2\n");

	if (nvram_invmatch("pptp_extraoptions", ""))
		fwritenvram("pptp_extraoptions", fp);

	fclose(fp);

}

void start_pptp(int status)
{
	int ret;
	FILE *fp;
	char *pptp_argv[] = { "pppd",
		NULL
	};
	char username[80], passwd[80];

	stop_dhcpc();
#ifdef HAVE_PPPOE
	stop_pppoe();
#endif
	stop_vpn_modules();

	snprintf(username, sizeof(username), "%s",
		 nvram_safe_get("ppp_username"));
	snprintf(passwd, sizeof(passwd), "%s", nvram_safe_get("ppp_passwd"));

	if (status != REDIAL) {
		create_pptp_config(nvram_safe_get("pptp_server_name"),
				   username);
		/*
		 * Generate pap-secrets file 
		 */
		if (!(fp = fopen("/tmp/ppp/pap-secrets", "w"))) {
			perror("/tmp/ppp/pap-secrets");
			return;
		}
		fprintf(fp, "\"%s\" * \"%s\" *\n", username, passwd);
		fclose(fp);
		chmod("/tmp/ppp/pap-secrets", 0600);

		/*
		 * Generate chap-secrets file 
		 */
		if (!(fp = fopen("/tmp/ppp/chap-secrets", "w"))) {
			perror("/tmp/ppp/chap-secrets");
			return;
		}
		fprintf(fp, "\"%s\" * \"%s\" *\n", username, passwd);
		fclose(fp);
		chmod("/tmp/ppp/chap-secrets", 0600);

		/*
		 * Enable Forwarding 
		 */
		if ((fp = fopen("/proc/sys/net/ipv4/ip_forward", "r+"))) {
			fputc('1', fp);
			fclose(fp);
		} else
			perror("/proc/sys/net/ipv4/ip_forward");
	}
	char *wan_ifname = nvram_safe_get("wan_ifname");

	if (isClient()) {
		wan_ifname = getSTA();
	}

	nvram_set("pptp_ifname", wan_ifname);
	/*
	 * Bring up WAN interface 
	 */
	if (nvram_match("pptp_use_dhcp", "1")) {
		// pid_t pid;
		// char *wan_ipaddr;
		// char *wan_netmask;
		// char *wan_gateway;

		// char *pptp_server_ip = nvram_safe_get ("pptp_server_ip");
		// char *wan_hostname = nvram_safe_get ("wan_hostname");

		nvram_set("wan_get_dns", "");
		nvram_unset("dhcpc_done");
		//dirty hack
		start_dhcpc(wan_ifname, NULL, NULL, 1);
		int timeout;

		for (timeout = 60; !nvram_match("dhcpc_done", "1") && timeout > 0; --timeout) {	/* wait for info from dhcp server */
			sleep(1);
		}
		stop_dhcpc();	/* we don't need dhcp client anymore */
		create_pptp_config(nvram_safe_get("pptp_server_ip"), username);

	} else {
		ifconfig(wan_ifname, IFUP, nvram_safe_get("wan_ipaddr"),
			 nvram_safe_get("wan_netmask"));
		if (!nvram_match("wan_gateway", "0.0.0.0"))
			route_add(wan_ifname, 0,
				  nvram_safe_get("pptp_server_ip"),
				  nvram_safe_get("wan_gateway"),
				  "255.255.255.255");
	}
	ret = _evalpid(pptp_argv, NULL, 0, NULL);

	if (nvram_match("ppp_demand", "1")) {
		/*
		 * Trigger Connect On Demand if user press Connect button in Status
		 * page 
		 */
		if (nvram_match("action_service", "start_pptp")
		    || nvram_match("action_service", "start_l2tp")) {
			start_force_to_dial();
			// force_to_dial(nvram_safe_get("action_service"));
			nvram_unset("action_service");
		}
		/*
		 * Trigger Connect On Demand if user ping pptp server 
		 */
		else {
			eval("listen", nvram_safe_get("lan_ifname"));
		}
	}
	stop_wland();
	start_wshaper();
	start_wland();
	cprintf("done\n");
	return;
}

void stop_pptp(void)
{

	route_del(nvram_safe_get("wan_ifname"), 0,
		  nvram_safe_get("pptp_server_ip"), NULL, NULL);

	unlink("/tmp/ppp/link");
	stop_process("pppd", "PPP daemon");
	stop_process("pptp", "PPTP daemon");
	stop_process("listen", "activity daemon");

	cprintf("done\n");
	return;
}

#endif
