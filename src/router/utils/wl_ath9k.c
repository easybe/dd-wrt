#include <stdio.h>
#include <malloc.h>
#include <wlutils.h>
#include <shutils.h>
#include <bcmnvram.h>


void
showinterface (char *ifname)
{
	struct mac80211_info *mac80211_info;
	struct wifi_client_info *wc;
	mac80211_info = mac80211_assoclist("ath0");
	for (wc = mac80211_info->wci ; wc ; wc = wc->next) {
         fprintf (stdout, "assoclist %s\n",wc->mac);
	}
	free_wifi_clients(mac80211_info->wci);
	free(mac80211_info);
}
int
main (int argc, char *argv[])
{
  if (argc < 2)
    {
      fprintf (stderr, "invalid argument\n");
      return 0;
    }
  char *ifname = "ath0";
  int i;
  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-i"))
	{
	  ifname = argv[++i];
	  continue;
	}
      if (!strcmp (argv[i], "assoclist"))
	{
	  if (strcmp (argv[1], "-i") == 0)
	    showinterface (ifname);
	  else
	    {
	    showinterface ("ath0");
	    }

	}
      if (!strcmp (argv[i], "rssi"))
	{
	  unsigned char rmac[6];
	  ether_atoe (argv[++i], rmac);
	    int ifcount = getifcount ("wifi");
	  int rssi=-1;
	  if (strcmp (argv[1], "-i") == 0)
	    rssi = getRssi (ifname, rmac);
	  else
	    {
	      int c = 0;
	      for (c = 0; c < ifcount; c++)
		{
		  char interface[32];
		  sprintf (interface, "ath%d", c);
		  rssi = getRssi(interface, rmac);
		        if (rssi!=0 && rssi!=-1)
			    {
			    fprintf (stdout, "rssi is %d\n", rssi);
			    return 0;
			    }
		  char vif[32];
		  sprintf (vif, "%s_vifs", interface);
		  char var[80], *next;
		  char *vifs = nvram_safe_get (vif);
		  if (vifs != NULL)
		    {
		      foreach (var, vifs, next)
		      {
		        rssi = getRssi(var, rmac);
		        if (rssi!=0 && rssi!=-1)
			    {
			    fprintf (stdout, "rssi is %d\n", rssi);
			    return 0;
			    }

		      }
		    }
		}
	    }

	  fprintf (stdout, "rssi is %d\n", rssi);
	}
      if (!strcmp (argv[i], "noise"))
	{
	  unsigned char rmac[6];
	  ether_atoe (argv[++i], rmac);
	    int ifcount = getifcount ("wifi");
	  int rssi=-1;
	  if (strcmp (argv[1], "-i") == 0)
	    rssi = getNoise (ifname, rmac);
	  else
	    {
	      int c = 0;
	      for (c = 0; c < ifcount; c++)
		{
		  char interface[32];
		  sprintf (interface, "ath%d", c);
		  
		        rssi = getNoise(interface, rmac);
		        if (rssi!=0 && rssi!=-1)
			    {
			    fprintf (stdout, "noise is %d\n", rssi);
			    return 0;
			    }
		  char vif[32];
		  sprintf (vif, "%s_vifs", interface);
		  char var[80], *next;
		  char *vifs = nvram_safe_get (vif);
		  if (vifs != NULL)
		    {
		      foreach (var, vifs, next)
		      {
		        rssi = getNoise(var, rmac);
		        if (rssi!=0 && rssi!=-1)
			    {
			    fprintf (stdout, "noise is %d\n", rssi);
			    return 0;
			    }
		      }
		    }
		}
	    }
	  fprintf (stdout, "noise is %d\n", rssi);
	}
      if (!strcmp (argv[i], "uptime"))
	{
	  unsigned char rmac[6];
	  ether_atoe (argv[++i], rmac);
	    int ifcount = getifcount ("wifi");
	  int uptime=-1;
	  if (strcmp (argv[1], "-i") == 0)
	    uptime = getUptime (ifname, rmac);
	  else
	    {
	      int c = 0;
	      for (c = 0; c < ifcount; c++)
		{
		  char interface[32];
		  sprintf (interface, "ath%d", c);
		  
		        uptime = getUptime(interface, rmac);
		        if (uptime!=0 && uptime!=-1)
			    {
			    fprintf (stdout, "uptime is %d\n", uptime);
			    return 0;
			    }
		  char vif[32];
		  sprintf (vif, "%s_vifs", interface);
		  char var[80], *next;
		  char *vifs = nvram_safe_get (vif);
		  if (vifs != NULL)
		    {
		      foreach (var, vifs, next)
		      {
		        uptime = getUptime(var, rmac);
		        if (uptime!=0 && uptime!=-1)
			    {
			    fprintf (stdout, "uptime is %d\n", uptime);
			    return 0;
			    }
		      }
		    }
		}
	    }
	  fprintf (stdout, "uptime is %d\n", uptime);
	}
    }
}