/*
 * sysinit-x86.c
 *
 * Copyright (C) 2006 Sebastian Gottschall <gottschall@dd-wrt.com>
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
 * 
 * System Initialisation for Standard PC and compatible Routers
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <sys/klog.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <bcmnvram.h>
#include <shutils.h>
#include <utils.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <linux/mii.h>
#include "devices/ethernet.c"
#include "devices/wireless.c"

#define sys_reboot() eval("sync"); eval("event","3","1","15")

static int getdiscindex(void)	// works only for squashfs 
{
	int i;

	for (i = 0; i < 10; i++) {
		char dev[64];

		sprintf(dev, "/dev/discs/disc%d/part2", i);
		FILE *in = fopen(dev, "rb");

		if (in == NULL)
			continue;	// no second partition or disc does not
		// exist, skipping
		char buf[4];

		fread(buf, 4, 1, in);
		if (buf[0] == 'h' && buf[1] == 's' && buf[2] == 'q'
		    && buf[3] == 't') {
			fclose(in);
			// filesystem detected
			return i;
		}
		fclose(in);
	}
	return -1;
}

void start_sysinit(void)
{
	time_t tm = 0;

	char dev[64];
	int index = getdiscindex();

	if (index == -1) {
		fprintf(stderr,
			"no valid dd-wrt partition found, calling shell");
		eval("/bin/sh");
		exit(0);
	}
	// sprintf (dev, "/dev/discs/disc%d/part1", index);
	// mount (dev, "/boot", "ext2", MS_MGC_VAL, NULL);

	sprintf(dev, "/dev/discs/disc%d/part3", index);
	// eval("insmod","block2mtd",dev);
	// sprintf (dev, "/dev/mtdblock/0");
	// eval ("fsck", dev); //checking nvram partition and correcting errors
	// detect jffs
	/*
	 * FILE *fp=fopen(dev,"rb"); int nojffs=0; if (getc(fp)!=0x85)nojffs=1;
	 * if (getc(fp)!=0x19)nojffs=1; fclose(fp);
	 */
	// eval("mtd","erase","mtd0");

	if (mount(dev, "/usr/local", "ext2", MS_MGC_VAL, NULL)) {
		eval("/sbin/mke2fs", "-F", "-b", "1024", dev);
		mount(dev, "/usr/local", "ext2", MS_MGC_VAL, NULL);
		eval("/bin/tar", "-xvvjf", "/etc/local.tar.bz2", "-C", "/");
	}
	eval("mkdir", "-p", "/usr/local/nvram");

	FILE *in = fopen("/usr/local/nvram/nvram.db", "rb");

	if (in != NULL) {
		fclose(in);
		eval("mkdir", "/tmp/nvram");
		eval("cp", "/etc/nvram/nvram.db", "/tmp/nvram");
		eval("cp", "/etc/nvram/offsets.db", "/tmp/nvram");
		eval("/usr/sbin/convertnvram");
		nvram_commit();
		eval("rm", "-f", "/etc/nvram/nvram.db");
		eval("rm", "-f", "/etc/nvram/offsets.db");
	}
	//recover nvram if available
	in = fopen("/usr/local/nvram/nvram.bin", "rb");
	if (in == NULL) {
		fprintf(stderr, "recover broken nvram\n");
		sprintf(dev, "/dev/discs/disc%d/disc", index);
		in = fopen(dev, "rb");
		fseek(in, 0, SEEK_END);
		long mtdlen = ftell(in);
		fseek(in, mtdlen-(65536*2), SEEK_SET);
		unsigned char *mem = malloc(65536);
		fread(mem, 65536, 1, in);
		fclose(in);
		if (mem[0] == 0x46 && mem[1] == 0x4c && mem[2] == 0x53
		    && mem[3] == 0x48) {
			fprintf(stderr, "found recovery\n");
			in = fopen("/usr/local/nvram/nvram.bin", "wb");
			if (in != NULL) {
				fwrite(mem, 65536, 1, in);
				fclose(in);
				free(mem);
				eval("sync");
				sleep(5);
				eval("event", "5", "1", "15");
			}
		}
		free(mem);
	} else {
		fclose(in);
	}

	if (!nvram_match("disable_watchdog", "1"))
		eval("watchdog");	// system watchdog

	cprintf("sysinit() setup console\n");

	/*
	 * Setup console 
	 */

	cprintf("sysinit() klogctl\n");
	klogctl(8, NULL, atoi(nvram_safe_get("console_loglevel")));
	cprintf("sysinit() get router\n");

	/*
	 * eval("insmod","md5"); eval("insmod","aes"); eval("insmod","blowfish");
	 * eval("insmod","deflate"); eval("insmod","des");
	 * eval("insmod","michael_mic"); eval("insmod","cast5");
	 * eval("insmod","crypto_null"); 
	 */

	detect_ethernet_devices();
	eval("ifconfig", "eth0", "0.0.0.0", "up");
	eval("ifconfig", "eth1", "0.0.0.0", "up");
	eval("ifconfig", "eth2", "0.0.0.0", "up");
	eval("ifconfig", "eth3", "0.0.0.0", "up");

	struct ifreq ifr;
	int s;

	if ((s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW))) {
		char eabuf[32];

		strncpy(ifr.ifr_name, "eth0", IFNAMSIZ);
		ioctl(s, SIOCGIFHWADDR, &ifr);
		nvram_set("et0macaddr_safe",
			  ether_etoa((unsigned char *)ifr.ifr_hwaddr.sa_data,
				     eabuf));
		close(s);
	}
	detect_wireless_devices();

	eval("mknod", "/dev/rtc", "c", "253", "0");
#ifdef HAVE_CPUTEMP
	// insmod("nsc_gpio");
	// insmod("scx200_gpio");
	// insmod("scx200_i2c");
	// insmod("scx200_acb");
	// insmod("lm77");
#endif

	nvram_set("wl0_ifname", "ath0");
	eval("mknod", "/dev/crypto", "c", "10", "70");
	/*
	 * Set a sane date 
	 */
	stime(&tm);
	cprintf("done\n");
	return;
}

int check_cfe_nv(void)
{
	nvram_set("portprio_support", "0");
	return 0;
}

int check_pmon_nv(void)
{
	return 0;
}

void start_overclocking(void)
{
}

void enable_dtag_vlan(int enable)
{

}

void start_backup(void)
{
	char drive[64];
	sprintf(drive, "/dev/discs/disc%d/disc", getdiscindex());
	//backup nvram
	fprintf(stderr, "backup nvram\n");
	FILE *in = fopen("/usr/local/nvram/nvram.bin", "rb");
	if (in) {
		char *mem = malloc(65536);
		fread(mem, 65536, 1, in);
		fclose(in);
		in = fopen(drive, "r+b");
		fseek(in, 0, SEEK_END);
		long mtdlen = ftell(in);
		fseek(in, mtdlen-(65536*2), SEEK_SET);
		fwrite(mem, 65536, 1, in);
		fclose(in);
		eval("sync");
		fprintf(stderr, "reread for sync disc\n");
		in = fopen(drive, "rb");
		fseek(in, mtdlen-(65536*2), SEEK_SET);
		fread(mem, 65536, 1, in);
		fprintf(stderr, "%X%X%X%X\n", mem[0] & 0xff, mem[1] & 0xff,
			mem[2] & 0xff, mem[3] & 0xff);
		fclose(in);
		free(mem);
	}

}

void start_recover(void)
{
	FILE *in;
	char dev[64];
	fprintf(stderr, "recover broken nvram\n");
	sprintf(dev, "/dev/discs/disc%d/disc", getdiscindex());
	in = fopen(dev, "rb");
	fseek(in, 0, SEEK_END);
	long mtdlen = ftell(in);
	fseek(in, mtdlen-(65536*2), SEEK_SET);

	unsigned char *mem = malloc(65536);
	fread(mem, 65536, 1, in);
	fclose(in);
	if (mem[0] == 0x46 && mem[1] == 0x4c && mem[2] == 0x53
	    && mem[3] == 0x48) {
		fprintf(stderr, "found recovery\n");
		in = fopen("/usr/local/nvram/nvram.bin", "wb");
		if (in != NULL) {
			fwrite(mem, 65536, 1, in);
			fclose(in);
		}
	}
	free(mem);

}
