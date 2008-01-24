/*
 * AnchorFree.c
 *
 * Copyright (C) 2008 Sebastian Gottschall <gottschall@dd-wrt.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License.
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
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <bcmnvram.h>
#include <shutils.h>
#include <utils.h>
#include <syslog.h>
#include <signal.h>
#include <netdb.h>

#include "md5.h"


#ifndef HAVE_MADWIFI
#define IFPREFIX "wl"
#else
#define IFPREFIX "ath"
#endif

void
doHash (MD5_CTX * MD, char *filename)
{
  unsigned char buf[1];
  FILE *in = fopen (filename, "rb");
  if (in == NULL)
    {
      return;
    }
  while (1)
    {
      int c = getc (in);
      if (c == EOF)
	break;
      buf[0] = c;
      MD5Update (MD, buf, 1);
    }
  fclose (in);
}

static int
sockaddr_to_dotted (struct sockaddr *saddr, char *buf)
{
  buf[0] = '\0';
  if (saddr->sa_family == AF_INET)
    {
      inet_ntop (AF_INET, &((struct sockaddr_in *) saddr)->sin_addr, buf,
		 128);
      return 0;
    }
  if (saddr->sa_family == AF_INET6)
    {
      inet_ntop (AF_INET6, &((struct sockaddr_in6 *) saddr)->sin6_addr, buf,
		 128);
      return 0;
    }
  return -1;
}

void
getIPFromName (char *name, char *ip)
{
  struct addrinfo *result = NULL;
  int rc;
  struct addrinfo hint;
  memset (&hint, 0, sizeof (hint));
  hint.ai_socktype = SOCK_STREAM;
  rc = getaddrinfo (name, NULL, &hint, &result);
  if (result)
    {
      sockaddr_to_dotted (result->ai_addr, ip);
    }
  else
    sprintf (ip, "0.0.0.0");
}


void
deviceID (char *output)
{
  unsigned char key[16];
  MD5_CTX MD;
  MD5Init (&MD);
//  fprintf (stderr, "generate hash\n");
  doHash (&MD, "/dev/mtdblock/0");
  doHash (&MD, "/dev/mtdblock0");
  doHash (&MD,
	  "/sys/devices/platform/IXP4XX-I2C.0/i2c-adapter:i2c-0/0-0051/eeprom");
  doHash (&MD, "/dev/discs/disc0/part4");
  MD5Final ((unsigned char *) key, &MD);
  int i;
  for (i = 0; i < 16; i++)
    {
    unsigned int k = key[i];
    sprintf (output, "%s%X", output, k&0xff);
    }
//  fprintf (stderr, "final hash = %s\n", output);
}
unsigned char ctable[16] = { "0123456789ABCDEF" };
void
toURL (unsigned char *text, char *output)
{
  char *p = output;
  int i;
  int n = 0;
  for (i = 0; i < strlen (text); i++)
    {
      p[n++] = '%';
      p[n++] = ctable[text[i] / 16];
      p[n++] = ctable[text[i] % 16];
    }
  p[n++] = 0;
}

void
start_anchorfree (void)
{
  nvram_set ("af_dnathost", "0");
  nvram_set ("af_dnatport", "0");
  if (nvram_match ("af_serviceid", "0"))
    nvram_set ("af_registered", "0");
  nvram_set ("af_serviceid", "0");

  if (nvram_match ("af_enable", "1") && !nvram_match ("af_registered", "1"))
    {
      nvram_set ("af_registered", "1");
      syslog (LOG_INFO, "anchorfree : starting redirection\n");
      char url[1024];
      char devid[256];
      if (nvram_get ("af_hash") == NULL)
	{
	  memset (devid, 0, 256);
	  deviceID (devid);
	  nvram_set ("af_hash", devid);
	  nvram_commit ();
	}
      else
	{
	  strcpy (devid, nvram_safe_get ("af_hash"));
	}
      char email[256];
      toURL (nvram_safe_get ("af_email"), email);
      char ssid[128];
      if (nvram_match ("af_ssid", "1"))
      {
	toURL (nvram_safe_get ("af_ssid_name"), ssid);      
      }
      else
	{
	nvram_set("af_ssid_created","0");
#ifndef HAVE_MADWIFI
	  toURL (nvram_safe_get ("wl0_ssid"), ssid);
#else
	  toURL (nvram_safe_get ("ath0_ssid"), ssid);
#endif
	}
	
      if (nvram_match ("af_ssid", "1") && !nvram_match("af_ssid_created","1"))
        {
	nvram_set("af_ssid_created","1");
#ifndef HAVE_MADWIFI
	nvram_set("wl0_vifs","wl0.1");
	nvram_set("wl0.1_ssid",nvram_safe_get("af_ssid_name"));
	nvram_set("wl0.1_bridged","0");
	nvram_set("wl0.1_ipaddr","172.45.0.1");
	nvram_set("wl0.1_netmask","255.255.255.0");
	nvram_set("mdhcpd_count","1");
	nvram_set("mdhcpd","wl0.1>On>100>50>3600");
#else
	nvram_set("ath0_vifs","ath0.1");
	nvram_set("ath0.1_ssid",nvram_safe_get("af_ssid_name"));
	nvram_set("ath0.1_bridged","0");
	nvram_set("ath0.1_ipaddr","172.45.0.1");
	nvram_set("ath0.1_netmask","255.255.255.0");
	nvram_set("mdhcpd_count","1");
	nvram_set("mdhcpd","ath0.1>On>100>50>3600");
#endif
	stop_lan();
	start_lan();
	stop_dnsmasq();
	start_dnsmasq();
	}
      char addr[256];
      toURL (nvram_safe_get ("af_address"), addr);
      char addr2[256];
      toURL (nvram_safe_get ("af_address_2"), addr2);
      char city[256];
      toURL (nvram_safe_get ("af_city"), city);
      char zip[64];
      toURL (nvram_safe_get ("af_zip"), zip);
      char state[64];
      toURL (nvram_safe_get ("af_state"), state);
      char country[64];
      strcpy(country,getIsoName(nvram_safe_get ("af_country")));
      char cat[64];
      toURL (nvram_safe_get ("af_category"), cat);
      sprintf (url,
	       "wget -q -O /tmp/.anchorfree \"http://afhrp.anchorfree.com/register.php?"
	       "pid=0001&" "uid=%s&" "email=%s&" "ssid=%s&" "addr=%s&"
	       "addr2=%s&" "city=%s&" "zip=%s&" "state=%s&" "country=%s&"
	       "cat=%s&" "publish=%s\"", devid, email, ssid, addr, addr2,
	       city, zip, state, country, cat, nvram_safe_get ("af_publish"));
      eval ("rm", "-f", "/tmp/.anchorfree");
      system (url);
      FILE *response = fopen ("/tmp/.anchorfree", "rb");
      if (response == NULL)
	{
	  fprintf (stderr, "error while registration (cannot reach registration site)!\n");
    	  nvram_set ("af_servicestatus", "cannot reach registration site!");
    	  nvram_set ("af_registered", "0");
	  return;
	}
      char status[32];
      int r = fscanf (response, "status: %s\n", status);
      if (r != 1)
	{
	  fprintf (stderr, "registration failed (bad status)\n");
    	  nvram_set ("af_servicestatus", "registration failed (bad status)");
	  fclose (response);
    	  nvram_set ("af_registered", "0");
	  return;
	}
      nvram_set ("af_servicestatus", status);
      fprintf (stderr, "status: %s\n", status);
      if (strcmp (status, "OK"))
	{
	  fprintf (stderr, "registration failed\n");
	  fclose (response);
    	  nvram_set ("af_registered", "0");
	  return;
	}
      memset (status, 0, 32);
      fscanf (response, "sid: %s\n", status);
      if (r != 1)
	{
	  fprintf (stderr, "registration failed (bad sid)\n");
    	  nvram_set ("af_servicestatus", "registration failed (bad sid)");
	  fclose (response);
    	  nvram_set ("af_registered", "0");
	  return;
	}
      fprintf (stderr, "configuring service id %s\n", status);
      nvram_set ("af_serviceid", status);
      memset (status, 0, 32);
      fscanf (response, "host: %s\n", status);
      nvram_set ("af_dnathost", status);
      memset (status, 0, 32);
      fscanf (response, "port: %s\n", status);
      nvram_set ("af_dnatport", status);
      fclose (response);
    }
  return;
}



void
start_anchorfreednat (void)
{
  char dest[32];
  char source[32];

  if (nvram_match ("af_enable", "1") && !nvram_match ("af_dnathost", "0"))
    {
      char host[128];


      getIPFromName (nvram_safe_get ("af_dnathost"), host);
      sprintf (dest, "%s:%s", host, nvram_safe_get ("af_dnatport"));
//      sprintf (source, "%d/%d", nvram_safe_get ("lan_ipaddr"),
//	       getmask (nvram_safe_get ("lan_netmask")));
      sprintf (source, "0.0.0.0/0");
      if (nvram_match ("af_ssid", "1"))
      sprintf (source, "%s/%d", nvram_safe_get (IFPREFIX "0.1_ipaddr"),
	       getmask (nvram_safe_get (IFPREFIX "0.1_netmask")));

      eval ("iptables", "-t", "nat", "-D", "PREROUTING", "-s", source, "-p",
	    "tcp", "-d", nvram_safe_get ("lan_ipaddr"), "-j", "DNAT", "--to",
	    nvram_safe_get ("lan_ipaddr"));
      eval ("iptables", "-t", "nat", "-D", "PREROUTING", "-s", source, "-p",
	    "tcp", "--dport", "80", "-j", "DNAT", "--to", dest);
      eval ("iptables", "-t", "nat", "-A", "PREROUTING", "-s", source, "-p",
	    "tcp", "-d", nvram_safe_get ("lan_ipaddr"), "-j", "DNAT", "--to",
	    nvram_safe_get ("lan_ipaddr"));
      eval ("iptables", "-t", "nat", "-A", "PREROUTING", "-s", source, "-p",
	    "tcp", "--dport", "80", "-j", "DNAT", "--to", dest);
    }
}

void
stop_anchorfree_unregister (void)
{
      char url[1024];
      sprintf (url, "wget -q -O- \"http://afhrp.anchorfree.com/unregister.php?"
	       "uid=%s&"
	       "sid=%s\"", nvram_safe_get ("af_hash"),
	       nvram_safe_get ("af_serviceid"));
      eval ("rm", "-f", "/tmp/.anchorfree");
      system (url);
}

void
stop_anchorfree (void)
{
  if (!nvram_match ("af_serviceid", "0")
      && nvram_match ("af_registered", "1"))
    {
      nvram_set ("af_registered", "0");
      char dest[32];
      char source[32];
      syslog (LOG_INFO, "anchorfree : stopping redirection\n");
      char host[128];
      getIPFromName (nvram_safe_get ("af_dnathost"), host);
      sprintf (dest, "%s:%s", host, nvram_safe_get ("af_dnatport"));
//      sprintf (source, "%s/%d", nvram_safe_get ("lan_ipaddr"),
//	       getmask (nvram_safe_get ("lan_netmask")));
      sprintf (source, "0.0.0.0/0");
      if (nvram_match ("af_ssid", "1"))
      sprintf (source, "%s/%d", nvram_safe_get (IFPREFIX "0.1_ipaddr"),
	       getmask (nvram_safe_get (IFPREFIX "0.1_netmask")));

      eval ("iptables", "-t", "nat", "-D", "PREROUTING", "-s", source, "-p","tcp", "--dport", "80", "-j", "DNAT", "--to", dest);
      eval ("iptables", "-t", "nat", "-D", "PREROUTING", "-s", source, "-p","tcp", "-d", nvram_safe_get ("lan_ipaddr"), "-j", "DNAT", "--to",nvram_safe_get ("lan_ipaddr"));
      eval ("rm", "-f", "/tmp/.anchorfree");
      if (nvram_match("af_enable","0"))
        stop_anchorfree_unregister();
    }
  return;
}

