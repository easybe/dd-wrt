/*
   Unix SMB/CIFS implementation.

   read a file containing DNS names, types and IP addresses

   Copyright (C) Andrew Tridgell 1994-1998
   Copyright (C) Jeremy Allison 2007
   Copyright (C) Andrew Bartlett 2009.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* The purpose of this file is to read the file generated by the samba_dnsupdate script */

#include "includes.h"
#include "lib/util/xfile.h"
#include "lib/util/util_net.h"
#include "system/filesys.h"
#include "system/network.h"
#include "libcli/nbt/libnbt.h"

/********************************************************
 Start parsing the dns_hosts_file file.
*********************************************************/

static XFILE *startdns_hosts_file(const char *fname)
{
	XFILE *fp = x_fopen(fname,O_RDONLY, 0);
	if (!fp) {
		DEBUG(4,("startdns_hosts_file: Can't open dns_hosts_file file %s. "
			"Error was %s\n",
			fname, strerror(errno)));
		return NULL;
	}
	return fp;
}

/********************************************************
 Parse the next line in the dns_hosts_file file.
*********************************************************/

static bool getdns_hosts_fileent(TALLOC_CTX *ctx, XFILE *fp, char **pp_name, char **pp_name_type,
			  char **pp_next_name, 
			  struct sockaddr_storage *pss, uint32_t *p_port)
{
	char line[1024];

	*pp_name = NULL;
	*pp_name_type = NULL;
	*pp_next_name = NULL;
	*p_port = 0;

	while(!x_feof(fp) && !x_ferror(fp)) {
		char *name_type = NULL;
		char *name = NULL;
		char *next_name = NULL;
		char *ip = NULL;
		char *port = NULL;

		const char *ptr;
		int count = 0;

		if (!fgets_slash(line,sizeof(line),fp)) {
			continue;
		}

		if (*line == '#') {
			continue;
		}

		ptr = line;

		if (next_token_talloc(ctx, &ptr, &name_type, NULL))
			++count;
		if (next_token_talloc(ctx, &ptr, &name, NULL))
			++count;
		if (strcasecmp(name_type, "A") == 0) {
			if (next_token_talloc(ctx, &ptr, &ip, NULL))
				++count;
		} else if (strcasecmp(name_type, "SRV") == 0) {
			if (next_token_talloc(ctx, &ptr, &next_name, NULL))
				++count;
			if (next_token_talloc(ctx, &ptr, &port, NULL))
				++count;
		} else if (strcasecmp(name_type, "CNAME") == 0) {
			if (next_token_talloc(ctx, &ptr, &next_name, NULL))
				++count;
		}
		if (count <= 0)
			continue;

		if (strcasecmp(name_type, "A") == 0) {
			if (count != 3) {
				DEBUG(0,("getdns_hosts_fileent: Ill formed hosts A record [%s]\n",
					 line));
				continue;
			}
			DEBUG(4, ("getdns_hosts_fileent: host entry: %s %s %s\n",
				  name_type, name, ip));
			if (!interpret_string_addr(pss, ip, AI_NUMERICHOST)) {
				DEBUG(0,("getdns_hosts_fileent: invalid address "
					 "%s.\n", ip));
			}

		} else if (strcasecmp(name_type, "SRV") == 0) {
			if (count != 4) {
				DEBUG(0,("getdns_hosts_fileent: Ill formed hosts SRV record [%s]\n",
					 line));
				continue;
			}
			*p_port = strtoul(port, NULL, 10);
			if (*p_port == UINT32_MAX) {
				DEBUG(0, ("getdns_hosts_fileent: Ill formed hosts SRV record [%s] (invalid port: %s)\n",
					  line, port));
				continue;
			}
			DEBUG(4, ("getdns_hosts_fileent: host entry: %s %s %s %u\n",
				  name_type, name, next_name, (unsigned int)*p_port));
			*pp_next_name = talloc_strdup(ctx, next_name);
			if (!*pp_next_name) {
				return false;
			}
		} else if (strcasecmp(name_type, "CNAME") == 0) {
			if (count != 3) {
				DEBUG(0,("getdns_hosts_fileent: Ill formed hosts CNAME record [%s]\n",
					 line));
				continue;
			}
			DEBUG(4, ("getdns_hosts_fileent: host entry: %s %s %s\n",
				  name_type, name, next_name));
			*pp_next_name = talloc_strdup(ctx, next_name);
			if (!*pp_next_name) {
				return false;
			}
		} else {
			DEBUG(0,("getdns_hosts_fileent: unknown type %s\n", name_type));
			continue;
		}

		*pp_name = talloc_strdup(ctx, name);
		if (!*pp_name) {
			return false;
		}

		*pp_name_type = talloc_strdup(ctx, name_type);
		if (!*pp_name_type) {
			return false;
		}
		return true;
	}

	return false;
}

/********************************************************
 Finish parsing the dns_hosts_file file.
*********************************************************/

static void enddns_hosts_file(XFILE *fp)
{
	x_fclose(fp);
}

/********************************************************
 Resolve via "dns_hosts" method.
*********************************************************/

static NTSTATUS resolve_dns_hosts_file_as_sockaddr_recurse(const char *dns_hosts_file, 
							   const char *name, bool srv_lookup,
							   int level, uint32_t port, 
							   TALLOC_CTX *mem_ctx, 
							   struct sockaddr_storage **return_iplist,
							   int *return_count)
{
	/*
	 * "dns_hosts" means parse the local dns_hosts file.
	 */

	XFILE *fp;
	char *host_name = NULL;
	char *name_type = NULL;
	char *next_name = NULL;
	struct sockaddr_storage return_ss;
	uint32_t srv_port;
	NTSTATUS status = NT_STATUS_OBJECT_NAME_NOT_FOUND;
	TALLOC_CTX *ctx = NULL;
	TALLOC_CTX *ip_list_ctx = NULL;

	/* Don't recurse forever, even on our own flat files */
	if (level > 11) {

	}

	*return_iplist = NULL;
	*return_count = 0;

	DEBUG(3,("resolve_dns_hosts: "
		"Attempting dns_hosts lookup for name %s\n",
		name));

	fp = startdns_hosts_file(dns_hosts_file);

	if ( fp == NULL )
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;

	ip_list_ctx = talloc_new(mem_ctx);
	if (!ip_list_ctx) {
		enddns_hosts_file(fp);
		return NT_STATUS_NO_MEMORY;
	}

	ctx = talloc_new(ip_list_ctx);
	if (!ctx) {
		talloc_free(ip_list_ctx);
		enddns_hosts_file(fp);
		return NT_STATUS_NO_MEMORY;
	}

	while (getdns_hosts_fileent(ctx, fp, &host_name, &name_type, &next_name, &return_ss, &srv_port)) {
		if (!strequal(name, host_name)) {
			TALLOC_FREE(ctx);
			ctx = talloc_new(mem_ctx);
			if (!ctx) {
				enddns_hosts_file(fp);
				return NT_STATUS_NO_MEMORY;
			}

			continue;
		}

		if (srv_lookup) {
			if (strcasecmp(name_type, "SRV") == 0) {
				/* we only accept one host name per SRV entry */
				enddns_hosts_file(fp);
				status = resolve_dns_hosts_file_as_sockaddr_recurse(dns_hosts_file, next_name, 
										    false, 
										    level + 1, srv_port, 
										    mem_ctx, return_iplist, 
										    return_count);
				talloc_free(ip_list_ctx);
				return status;
			} else {
				continue;
			}
		} else if (strcasecmp(name_type, "CNAME") == 0) {
			/* we only accept one host name per CNAME */
			enddns_hosts_file(fp);
			status = resolve_dns_hosts_file_as_sockaddr_recurse(dns_hosts_file, next_name, false, 
									    level + 1, port, 
									    mem_ctx, return_iplist, return_count);
			talloc_free(ip_list_ctx);
			return status;
		} else if (strcasecmp(name_type, "A") == 0) {
			/* Set the specified port (possibly from a SRV lookup) into the structure we return */
			set_sockaddr_port((struct sockaddr *)&return_ss, port);

			/* We are happy to keep looking for other possible A record matches */
			*return_iplist = talloc_realloc(ip_list_ctx, (*return_iplist), 
							struct sockaddr_storage,
							(*return_count)+1);

			if ((*return_iplist) == NULL) {
				TALLOC_FREE(ctx);
				enddns_hosts_file(fp);
				DEBUG(3,("resolve_dns_hosts: talloc_realloc fail !\n"));
				return NT_STATUS_NO_MEMORY;
			}
			
			(*return_iplist)[*return_count] = return_ss;
			*return_count += 1;
			
			/* we found something */
			status = NT_STATUS_OK;
		}
	}

	talloc_steal(mem_ctx, *return_iplist);
	TALLOC_FREE(ip_list_ctx);
	enddns_hosts_file(fp);
	return status;
}

/********************************************************
 Resolve via "dns_hosts" method.
*********************************************************/

NTSTATUS resolve_dns_hosts_file_as_sockaddr(const char *dns_hosts_file, 
					    const char *name, bool srv_lookup,
					    TALLOC_CTX *mem_ctx, 
					    struct sockaddr_storage **return_iplist,
					    int *return_count)
{
	return resolve_dns_hosts_file_as_sockaddr_recurse(dns_hosts_file, name, srv_lookup, 
							  0, 0, 
							  mem_ctx, return_iplist, return_count);
}
