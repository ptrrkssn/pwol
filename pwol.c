/*
** pwol.c - Send Wake-On-LAN packets
**
** Copyright (c) 2017 Peter Eriksson <pen@lysator.liu.se>
** All rights reserved.
** 
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
** 
** * Redistributions of source code must retain the above copyright notice, this
**   list of conditions and the following disclaimer.
** 
** * Redistributions in binary form must reproduce the above copyright notice,
**   this list of conditions and the following disclaimer in the documentation
**   and/or other materials provided with the distribution.
** 
** * Neither the name of the copyright holder nor the names of its
**   contributors may be used to endorse or promote products derived from
**   this software without specific prior written permission.
** 
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

#ifdef __linux__
#include <netinet/ether.h>
#elif __FreeBSD__
#include <net/ethernet.h>
#define ether_addr_octet octet
#else
#include <sys/ethernet.h>
#endif


#define DEFAULT_GLOBAL_CONFIG   "/ifm/etc/pwol.conf"
#define DEFAULT_USER_CONFIG     ".pwolrc"

#define DEFAULT_HOSTGROUP_HOSTS 64

#define DEFAULT_ADDRESS         "255.255.255.255"
#define DEFAULT_PORT            "7"
#define DEFAULT_COPIES          "1"
#define DEFAULT_SECRET          NULL
#define DEFAULT_DELAY           NULL

#define DEFAULT_PROXY_ADDRESS   "0.0.0.0"
#define DEFAULT_PROXY_PORT      "10007"

#define HEADER_SIZE             6
#define MAC_SIZE                6
#define MAC_COPIES              16
#define WOL_BODY_SIZE           (HEADER_SIZE+MAC_COPIES*MAC_SIZE)

#define SECRET_MAX_SIZE         64


#ifndef VERSION
#define VERSION __DATE__ __TIME__
#endif

char *argv0 = "pwol";
char *version = VERSION;





typedef struct secret {
  unsigned char buf[SECRET_MAX_SIZE];
  size_t size;
} SECRET;



typedef struct target {
  struct addrinfo *aip;
  int fd;

  struct target *next;
} TARGET;


typedef struct gateway {
  char *name;

  char *address;
  char *port;
  TARGET *targets;

  unsigned int copies;
  struct timespec delay;
  SECRET secret;

  int fd;

  struct gateway *next;
} GATEWAY;

GATEWAY *gateways = NULL;
GATEWAY *default_gw = NULL;


typedef struct host {
  char *name;

  struct ether_addr mac;
  GATEWAY *via;
  
  unsigned int copies;
  struct timespec delay;
  SECRET secret;
  
  struct host *next;
} HOST;

HOST *hosts = NULL;


typedef struct hostgroup {
  char *name;

  struct timespec delay;

  HOST **hv;
  size_t hs;
  size_t hc;
  
  struct hostgroup *next;
} HOSTGROUP;

HOSTGROUP *hostgroups = NULL;
HOSTGROUP *all_group = NULL;

int f_verbose = 0;
int f_ignore = 0;
int f_debug = 0;
int f_no = 0;
int f_export = 0;
int f_daemon = 0;
int f_foreground = 0;


char *f_copies  = NULL;
char *f_delay   = NULL;
char *f_secret  = NULL;

char *f_address = NULL;
char *f_port    = NULL;
char *f_gateway = NULL;

char *f_host_delay    = NULL;

char *f_proxy_address = NULL;
char *f_proxy_port    = NULL;
char *f_proxy_secret  = NULL;

char *
strdupcat(const char *str,
	...) {
  va_list ap;
  char *res, *tmp, *cp;
  int size, len;


  va_start(ap, str);

  if (!str)
    return NULL;

  size = len = strlen(str);
  res = strdup(str);
  if (!res)
    return NULL;

  while ((cp = va_arg(ap, char *)) != NULL) {
    len = strlen(cp);
    
    tmp = realloc(res, size+len+1);
    if (!tmp) {
      free(res);
      return NULL;
    }
    res = tmp;
    strcpy(res+size, cp);
    size += len;
  }
  va_end(ap);
  return res;
}


TARGET *
target_add(TARGET **targets, 
	   struct addrinfo *aip) {
  TARGET *tp = malloc(sizeof(*tp));
  
  if (!tp)
    return NULL;

  tp->aip = aip;
  tp->fd = -1;

  tp->next = *targets;
  *targets = tp;

  return tp;
}

void
target_free(TARGET *tp) {
  if (!tp)
    return;

  if (tp->next) {
    target_free(tp->next);
    tp->next = NULL;
  }

  if (tp->aip) {
    freeaddrinfo(tp->aip);
    tp->aip = NULL;
  }

  free(tp);
}


char *
sockaddr2str(struct sockaddr *sp,
	     size_t len) {
  char addr[2048];
  char port[256];


  if (!sp)
    return NULL;

  if (getnameinfo(sp, len, addr, sizeof(addr), port, sizeof(port), NI_DGRAM|NI_NUMERICHOST|NI_NUMERICSERV) != 0)
    return NULL;

  if (sp->sa_family == AF_INET6)
    return strdupcat("[", addr, "]:", port, NULL);

  return strdupcat(addr, ":", port, NULL);
}


char *
addrinfo2str(struct addrinfo *aip) {
  char addr[2048];
  char port[256];


  if (!aip)
    return NULL;

  if (getnameinfo(aip->ai_addr, aip->ai_addrlen, addr, sizeof(addr), port, sizeof(port), NI_DGRAM|NI_NUMERICHOST|NI_NUMERICSERV) != 0)
    return NULL;

  if (aip->ai_family == AF_INET6)
    return strdupcat("[", addr, "]:", port, NULL);

  return strdupcat(addr, ":", port, NULL);
}


char *
target2str(TARGET *tp) {
  return addrinfo2str(tp->aip);
}


int
str2secret(const char *secret,
	   SECRET *sp) {
  unsigned int v1, v2, v3, v4, val, i;
  char *ptr, *cp, *tbuf;


  if (sscanf(secret, "%u.%u.%u.%u", &v1, &v2, &v3, &v4) == 4) {
    /* IPv4 address */
    sp->buf[0] = v1;
    sp->buf[1] = v2;
    sp->buf[2] = v3;
    sp->buf[3] = v4;
    sp->size  = 4;
    return sp->size;
  }


  /* List of hexadecimal bytes separated by : or - */
  ptr = strdup(secret);
  if (!ptr)
    return -1;

  tbuf = ptr;
  for (i = 0; i < SECRET_MAX_SIZE && (cp = strtok_r(tbuf, ":-", &ptr)) != NULL; i++) {
    tbuf = NULL;
    
    if (strlen(cp) > 2 || sscanf(cp, "%x", &val) != 1) {
      i = 0;
      break;
    }
    sp->buf[i] = val;
  }

  if (i > 1) {
    sp->size = i;
    return sp->size;
  }

  i = strlen(secret);
  if (i > SECRET_MAX_SIZE) {
    errno = EINVAL;
    return -1;
  }

  if (i > 0)
    memcpy(sp->buf, secret, i);

  sp->size = i;
  return sp->size;
}


int
secret_compare(SECRET *s1, 
	       SECRET *s2) {
  int rc;


  if (!s1 && s2)
    return 1;
  if (s1 && !s2)
    return -1;

  rc = s1->size - s2->size;
  if (rc)
    return rc;

  return memcmp(&s1->buf, &s2->buf, s1->size);
}

char *
secret2str(SECRET *sp) {
  char *buf;
  int i;


  if (!sp || sp->size == 0)
    return NULL;

  /* IPv4 */
  if (sp->size == 4) {
    buf = malloc(16);
    if (!buf)
      return NULL;
    snprintf(buf, 16, "%u.%u.%u.%u", sp->buf[0], sp->buf[1], sp->buf[2], sp->buf[3]);
    goto End;
  }
  
  buf = malloc(sp->size*3);
  if (!buf)
    return NULL;
  for (i = 0; i < sp->size; i++) {
    snprintf(buf+i*3, 3, "%02x", sp->buf[i]);
    if (i+1 < sp->size) {
      buf[i*3+2] = ':';
    }
  }

 End:
  for (i = 0; i < sp->size && isprint(sp->buf[i]); i++)
    ;
  
  if (i >= sp->size) {
    buf = realloc(buf, sp->size*3+3+sp->size);
    if (!buf)
      return NULL;

    memcpy(buf+sp->size*3-1, " (", 2);
    memcpy(buf+sp->size*3-1+2, sp->buf, sp->size);
    strcpy(buf+sp->size*3-1+2+sp->size, ")");
  }
    
  return buf;
}

char *
timespec2str(struct timespec *tsp) {
  char buf[2048];
  unsigned int h, m, s, ms, us, ns;
  float fs, fms, fus;


  h = tsp->tv_sec / 3600;
  m = (tsp->tv_sec - h*3600) / 60;
  s = tsp->tv_sec - h*3600 - m*60;

  ms = tsp->tv_nsec / 1000000;
  us = (tsp->tv_nsec - ms * 1000000) / 1000;
  ns = tsp->tv_nsec - ms * 1000000 - us * 1000;

  fs  = (float) s +  (float) ms / 1000.0;
  fms = (float) ms + (float) us / 1000.0;
  fus = (float) us + (float) ns / 1000.0;

  if (h)
    sprintf(buf, "%02u:%02u:%02u", h, m, s);
  else if (m)
    sprintf(buf, "%um+%gs", m, fs);
  else if (s)
    sprintf(buf, "%gs", fs);
  else if (ms)
    sprintf(buf, "%gms", fms);
  else if (us)
    sprintf(buf, "%gµs", fus);
  else
    sprintf(buf, "%uns", ns);

  return strdup(buf);
}

int
str2timespec(const char *time,
	     struct timespec *tsp) {
  float ft;
  char buf[3];


  memset(buf, 0, 3);
  if (sscanf(time, "%f%c%c", &ft, buf+0, buf+1) < 1)
    return -1;
  
  if (strcmp(buf, "s") == 0 || !*buf) {
    tsp->tv_sec  = ft;
    tsp->tv_nsec = (ft-tsp->tv_sec)*1000000000.0;
  } else if (strcmp(buf, "m") == 0) {
    tsp->tv_sec  = ft*60;
    tsp->tv_nsec = (ft-tsp->tv_sec*60)/6.0*100000000.0;;
  } else if (strcmp(buf, "h") == 0) {
    tsp->tv_sec  = ft*60*60;
    tsp->tv_nsec = 0;
  } else if (strcmp(buf, "d") == 0) {
    tsp->tv_sec  = ft*60*60*24;
    tsp->tv_nsec = 0;
  } else if (strcmp(buf, "ms") == 0) {
    tsp->tv_sec  = ft/1000.0;
    tsp->tv_nsec = ft*1000000.0;
  } else if (strcmp(buf, "us") == 0 || strcmp(buf, "µs") == 0) {
    tsp->tv_sec  = ft/1000000.0;
    tsp->tv_nsec = ft*1000.0;
  } else if (strcmp(buf, "ns") == 0) {
    tsp->tv_sec  = ft/1000000000.0;
    tsp->tv_nsec = ft;
  } else 
    return -1;

  return tsp->tv_sec;
}



GATEWAY *
gw_lookup(const char *name) {
  GATEWAY *gp;

  if (!name)
    return NULL;

  for (gp = gateways; gp && (!gp->name || strcmp(gp->name, name) != 0); gp = gp->next)
    ;

  if (!gp)
    errno = ENXIO;

  return gp;
}


int
gw_add_name(GATEWAY *gp,
	    const char *name) {
  if (!name)
    return -1;
 
  if (gp->name)
    free(gp->name);

  gp->name = strdup(name);
  return 0;
}

int
gw_add_address(GATEWAY *gp,
	       const char *address) {
  if (!address)
    return -1;

  if (gp->address)
    free(gp->address);

  gp->address = strdup(address);
  return 0;
}


HOSTGROUP *
group_lookup(const char *name) {
  HOSTGROUP *hgp;


  if (!name)
    return NULL;

  for (hgp = hostgroups; hgp && strcmp(hgp->name, name) != 0; hgp = hgp->next)
    ;

  if (!hgp)
    errno = ENXIO;

  return hgp;
}

HOSTGROUP *
group_create(const char *name) {
  HOSTGROUP *hgp, **thgp;


  if (!name)
    return NULL;

  hgp = group_lookup(name);
  if (hgp)
    return hgp;

  hgp = malloc(sizeof(*hgp));
  if (!hgp)
    return NULL;

  memset(hgp, 0, sizeof(*hgp));
  hgp->name = strdup(name);

  hgp->hs = DEFAULT_HOSTGROUP_HOSTS;
  hgp->hc = 0;
  hgp->hv = malloc(hgp->hs * sizeof(HOST *));
  if (!hgp->hv)
    return NULL;

  for (thgp = &hostgroups; *thgp; thgp = &((*thgp)->next))
    ;

  *thgp = hgp;
  return hgp;
}


GATEWAY *
gw_create(const char *name) {
  GATEWAY *gp, **tgp;


  if (name) {
    gp = gw_lookup(name);
    if (gp)
      return gp;
  }

  gp = malloc(sizeof(*gp));
  if (!gp)
    return NULL;

  memset(gp, 0, sizeof(*gp));


  if (name) {
    gp->name = strdup(name);
  
    /* Try to use gateway name as address */
    (void) gw_add_address(gp, name);
    
    /* Autocreate group name */
    (void) group_create(name);
  }

  for (tgp = &gateways; *tgp; tgp = &((*tgp)->next))
    ;
  
  *tgp = gp;
  return gp;
}

int
gw_add_port(GATEWAY *gp,
	    const char *port) {
  if (!port)
    return -1;

  gp->port = strdup(port);
  return 0;
}

int
gw_add_delay(GATEWAY *gp,
	       const char *delay) {
  if (!delay)
    return -1;

  return str2timespec(delay, &gp->delay);
}

int
gw_add_copies(GATEWAY *gp,
	      const char *copies) {
  if (copies && sscanf(copies, "%u", &gp->copies) == 1)
    return 0;

  return -1;
}

int
gw_add_secret(GATEWAY *gp,
	      const char *secret) {
  if (!secret)
    return -1;
  
  return str2secret(secret, &gp->secret);
}



HOST *
host_lookup(const char *name) {
  HOST *hp;

  if (!name)
    return NULL;
  
  for (hp = hosts; hp && strcmp(hp->name, name) != 0; hp = hp->next)
    ;

  if (!hp)
    errno = ENXIO;

  return hp;
}

int
host_add_mac(HOST *hp,
	     const char *mac) {
  struct ether_addr *ep;


  if (!mac)
    return -1;

  if (ether_hostton(mac, &hp->mac) == 0)
    return 0;

  ep = ether_aton(mac);
  if (!ep) {
    errno = EINVAL;
    return -1;
  }

  hp->mac = *ep;
  return 0;
}


HOST *
host_create(const char *name) {
  HOST *hp, **thp;


  if (!name)
    return NULL;

  hp = host_lookup(name);
  if (hp)
    return hp;

  hp = malloc(sizeof(*hp));
  if (!hp)
    return NULL;

  memset(hp, 0, sizeof(*hp));
  hp->name = strdup(name);

  /* Try to lookup mac via name in ethers file */
  (void) host_add_mac(hp, name);

#if 0
  if (gp) {
    hp->via    = gp;

    hp->copies = gp->copies;
    hp->delay  = gp->delay;
    hp->secret = gp->secret;
  }
#endif

  for (thp = &hosts; *thp; thp = &((*thp)->next))
    ;

  *thp = hp;
  return hp;
}


int
group_add_delay(HOSTGROUP *hgp,
		const char *delay) {
  if (!delay)
    return -1;
  
  return str2timespec(delay, &hgp->delay);
}



int
group_add_host(HOSTGROUP *hgp, 
	       HOST *hp) {
  int i;


  if (!hgp)
    return -1;

  if (!hp)
    return -1;

  for (i = 0; i < hgp->hc && hp != hgp->hv[i]; i++)
    ;

  if (i < hgp->hc)
    return hgp->hc;

  if (hgp->hc >= hgp->hs) {
    hgp->hs += DEFAULT_HOSTGROUP_HOSTS;
    hgp->hv = realloc(hgp->hv, hgp->hs * sizeof(HOST *));
    if (!hgp->hv)
      return -1;
  }

  hgp->hv[hgp->hc++] = hp;
  return hgp->hc;
}



int
host_add_via(HOST *hp,
	     const char *via) {
  HOSTGROUP *hgp;

  if (!via)
    return -1;

  hp->via = gw_lookup(via);
  if (!hp->via)
    return -1;

  hgp = group_lookup(hp->via->name);
  group_add_host(hgp, hp);

  return 0;
}

int
host_add_name(HOST *hp,
	      const char *name) {
  if (!name)
    return -1;
 
  if (hp->name)
    free(hp->name);

  hp->name = strdup(name);
  return 0;
}

int
host_add_delay(HOST *hp,
	       const char *delay) {
  if (!delay)
    return -1;

  return str2timespec(delay, &hp->delay);
}

int
host_add_copies(HOST *hp,
		const char *copies) {
  if (!copies || sscanf(copies, "%u", &hp->copies) != 1)
    return -1;

  return 0;
}

int
host_add_secret(HOST *hp,
		const char *secret) {
  if (!secret)
    return -1;

  return str2secret(secret, &hp->secret);
}




void
gw_print(GATEWAY *gp) {
  TARGET *tp;
  unsigned int i;


  if (f_verbose) {
    printf("Gateway %s:\n", gp->name);
    
    if (gp->copies)
      printf("  %-10s  %u\n", "Copies", gp->copies);
    if (gp->delay.tv_sec || gp->delay.tv_nsec)
      printf("  %-10s  %s\n", "Delay",  timespec2str(&gp->delay));
    if (gp->secret.size > 0)
      printf("  %-10s  %s\n", "Secret", secret2str(&gp->secret));
    printf("  Targets:\n");
    i = 0;
    for (tp = gp->targets; tp; tp = tp->next) {
      char *dest = target2str(tp);
      printf("    %-2u        %s\n", i+1, dest ? dest : "???");
      ++i;
    }

  } else {

    printf("gateway %s", gp->name);
    if (gp->address && strcmp(gp->address, gp->name) != 0)
      printf(" address %s", gp->address);
    if (gp->port && strcmp(gp->port, default_gw->port) != 0)
      printf(" port %s", gp->port);
    if (gp->copies && gp->copies != default_gw->copies)
      printf(" copies %u", gp->copies);
    if ((gp->delay.tv_sec || gp->delay.tv_nsec) && 
	!(gp->delay.tv_sec == default_gw->delay.tv_sec && gp->delay.tv_nsec == default_gw->delay.tv_nsec))
      printf(" delay %s", timespec2str(&gp->delay));
    if (gp->secret.size > 0 && (gp->secret.size != default_gw->secret.size || memcmp(gp->secret.buf, default_gw->secret.buf, gp->secret.size) != 0))
      printf(" secret %s", secret2str(&gp->secret));
    putchar('\n');
  }
}


void
host_print(HOST *hp) {
  if (f_verbose) {
    printf("Host %s:\n", hp->name);
    
    printf("  %-10s  %s\n", "MAC", ether_ntoa(&hp->mac));
    if (hp->via)
      printf("  %-10s  %s\n", "Gateway", hp->via->name);
    if (hp->copies)
      printf("  %-10s  %u\n", "Copies", hp->copies);
    if (hp->delay.tv_sec || hp->delay.tv_nsec)
      printf("  %-10s  %s\n", "Delay",  timespec2str(&hp->delay));
    if (hp->secret.size > 0)
      printf("  %-10s  %s\n", "Secret", secret2str(&hp->secret));
  } else {
    printf("host %s", hp->name);
    printf(" mac %s", ether_ntoa(&hp->mac));
    if (hp->via && group_lookup(hp->via->name) == NULL)
      printf(" via %s", hp->via->name);
    if (hp->copies)
      printf(" copies %u", hp->copies);
    if (hp->delay.tv_sec || hp->delay.tv_nsec)
      printf(" delay %s", timespec2str(&hp->delay));
    if (hp->secret.size > 0)
      printf(" secret %s", secret2str(&hp->secret));
    putchar('\n');
  }
}

void
group_print(HOSTGROUP *hgp) {
  int i;

  if (f_verbose) {
    printf("Hostgroup %s:\n", hgp->name);
    if (hgp->delay.tv_sec || hgp->delay.tv_nsec)
      printf("  %-10s  %s\n", "Delay",  timespec2str(&hgp->delay));
    if (hgp->hc > 0)
      printf("  Hosts:\n");
    for (i = 0; i < hgp->hc; i++)
      printf("    %2d        %s\n", i+1, hgp->hv[i]->name);
  } else {
    printf("[%s]\n", hgp->name);
    if (hgp->delay.tv_sec || hgp->delay.tv_nsec)
      printf("delay %s\n", timespec2str(&hgp->delay));
    for (i = 0; i < hgp->hc; i++) {
      HOST *hp = hgp->hv[i];

      printf("host %s", hp->name);

#if 0
      if (!hp->via || (hp->via && strcmp(hp->via->name, hgp->name) == 0)) {
	if (hp->copies)
	  printf(" copies %u", hp->copies);
	if (hp->delay.tv_sec || hp->delay.tv_nsec)
	  printf(" delay %s", timespec2str(&hp->delay));
	if (hp->secret.size > 0)
	  printf(" secret %s", secret2str(&hp->secret));
      }
#endif
      
      putchar('\n');
    }
  }
}
 
 void
   buf_print(FILE *fp,
	  void *buf, 
	  size_t size) {
  unsigned char *bp = (unsigned char *) buf;
  int i;

  putc('\t', fp);
  for (i = 0; i < size; i++) {
    if (i > 0) {
      if ((i & 15) == 0) {
	putc('\n', fp);
	putc('\t', fp);
      } else
	putc(':', fp);
    }
    fprintf(fp, "%02X", (unsigned int) bp[i]);
  }
  putc('\n', fp);
}


int
mac_invalid(struct ether_addr *mac) {
  int i;


  for (i = 0; i < 6 && mac->ether_addr_octet[i] == 0; i++)
    ;

  /* All 0x00 bytes - no MAC set */
  if (i == 6) {
    errno = EFAULT;
    return -1;
  }

  return 0;
}


int
send_wol_host(HOST *hp) {
  int i, j;
  int rc;
  char *msg;
  size_t msg_size;
  GATEWAY *gp = NULL;
  SECRET *sp = NULL;
  TARGET *tp = NULL;
  struct timespec delay;
  int copies;


  if (!hp)
    return -1;

  if (mac_invalid(&hp->mac)) {
    errno = EINVAL;
    return -1;
  }

  if (f_gateway) {
    gp = gw_lookup(f_gateway);
    if (!gp) {
      errno = EINVAL;
      return -1;
    }
  } else {
      gp = hp->via;
  }
  if (!gp)
    gp = default_gw;
  hp->via = gp;


  if (f_copies) {
    if (host_add_copies(hp, f_copies) < 0) {
      errno = EINVAL;
      return -1;
    }
  }
  copies = hp->copies;
  if (!copies && hp->via)
    copies = hp->via->copies;
  if (!copies)
    copies = 1;

  if (f_delay) {
    if (host_add_delay(hp, f_delay) < 0) {
      errno = EINVAL;
      return -1;
    }
  }
  delay = hp->delay;
  if (delay.tv_sec == 0 && delay.tv_nsec == 0 && hp->via)
    delay = hp->via->delay;

  if (f_secret) {
    if (host_add_secret(hp, f_secret) < 0) {
      errno = EINVAL;
      return -1;
    }
  }
  sp = &hp->secret;
  if (sp->size == 0 && hp->via)
    sp = &hp->via->secret;
  
  msg_size = WOL_BODY_SIZE;
  if (sp)
    msg_size += sp->size;

  msg = malloc(msg_size);
  if (!msg)
    return -3;
    
  memset(msg, 0xFF, MAC_SIZE);

  for (i = 0; i < MAC_COPIES; i++)
    memcpy(msg+HEADER_SIZE+i*MAC_SIZE, &hp->mac, MAC_SIZE);

  if (sp)
    memcpy(msg+HEADER_SIZE+i*MAC_SIZE, sp->buf, sp->size);

  if (f_debug) {
    fprintf(stderr, "[%s (%s)", hp->name, ether_ntoa(&hp->mac));
    if (sp && sp->size)
      fprintf(stderr, " with secret %s", secret2str(sp));
    fprintf(stderr, "]\n");
  }

  if (f_debug > 2) {
    fprintf(stderr, "UDP Packet:\n");
    buf_print(stderr, msg, msg_size);
  }

  if (f_verbose && !f_debug) {
    printf("%s (%s)", hp->name, ether_ntoa(&hp->mac));
    fflush(stdout);
  }

  for (tp = gp->targets; tp; tp = tp->next) {
    struct addrinfo *aip = tp->aip;

    if (!aip)
      continue;

    for (j = 0; j < copies; j++) {
      if (j > 0 && (delay.tv_sec || delay.tv_nsec)) {
	/* Inter-packet delay */
	struct timespec t_delay = delay;
	
	if (f_debug)
	  fprintf(stderr, "(Sleeping %s)\n", timespec2str(&delay));
	
	while ((rc = nanosleep(&t_delay, &t_delay)) < 0 && errno == EINTR) {
	  if (f_debug)
	    fprintf(stderr, "(Sleeping %s more)\n", timespec2str(&delay));
	}

	if (rc < 0)
	  return -1;
      }
      
      if (f_debug) {
	char *dest;

	dest = target2str(tp);
	fprintf(stderr, "Sending packet %u/%u via %s\n", j+1, copies, dest ? dest : "???");
      }

      if (!f_no) {
	while ((rc = sendto(tp->fd, msg, msg_size, 0, aip->ai_addr, aip->ai_addrlen)) < 0 && errno == EINTR)
	  ;
	
	if (rc < 0)
	  return -1;
      }

      if (f_verbose && !f_debug) {
	putc('.', stdout);
	fflush(stdout);
      }
    }

    if (f_verbose && !f_debug)
      puts(" Done");
  }
  return 0;
}


int
send_wol(const char *name) {
  HOSTGROUP *hgp;
  HOST *hp;
  int rc, i;

  
  if ((hgp = group_lookup(name)) != NULL) {
    for (i = 0; i < hgp->hc; i++) {
      if (i > 0 && (hgp->delay.tv_sec || hgp->delay.tv_nsec)) {
	/* Inter-host delay */
	struct timespec delay = hgp->delay;
	
	if (f_debug)
	  fprintf(stderr, "(Sleeping %s)\n", timespec2str(&delay));
	
	while ((rc = nanosleep(&delay, &delay)) < 0 && errno == EINTR) {
	  if (f_debug)
	    fprintf(stderr, "(Sleeping %s more)\n", timespec2str(&delay));
	}
	
	if (rc < 0)
	  return -1;
      }

      rc = send_wol_host(hgp->hv[i]);
      if (rc) 
	return rc;
    }
    return 0;
  }
  
  hp = host_lookup(name);
  if (!hp) {
    hp = host_create(name);
    if (!hp)
      return -1;
    
    if (host_add_mac(hp, name) < 0)
      return -1;
  }

  return send_wol_host(hp);
}

void
header(FILE *fp) {
  fprintf(fp, "[pwol %s (%s %s) - (c) Peter Eriksson <pen@lysator.liu.se>]\n",
	  version, __DATE__, __TIME__);
}


void
trim(char *buf) {
  char *cp;


  /* Remove comments */
  cp = strchr(buf, ';');
  if (cp)
    *cp = '\0';
  cp = strchr(buf, '#');
  if (cp)
    *cp = '\0';

  for (cp = buf; *cp && isspace(*cp); ++cp)
    ;

  if (cp > buf)
    strcpy(buf, cp);

  for (cp = buf+strlen(buf); cp > buf && isspace(cp[-1]); --cp)
    ;
  *cp = '\0';
}


int
parse_config(const char *path) {
  char buf[2048], *bptr, *cptr;
  FILE *fp;
  int line = 0;
  int len, rc = -1;

  GATEWAY *gp = NULL;
  GATEWAY *group_gp = NULL;
  HOST *hp = NULL;
  HOSTGROUP *hgp = NULL;


  gp = gw_lookup("default");
  gw_add_address(gp, DEFAULT_ADDRESS);
  gw_add_port(gp, DEFAULT_PORT);

  if (f_debug > 1)
    fprintf(stderr, "[Parsing config: %s]\n", path);

  fp = fopen(path, "r");
  if (!fp)
    return -1;

  while (fgets(buf, sizeof(buf), fp)) {
    char *key, *val;

    ++line;

    trim(buf);

    if (!*buf)
      continue;

    bptr = buf;
    hp = NULL;

    while ((key = strtok_r(bptr, " \t", &cptr)) != NULL) {
      bptr = NULL;
      val = strtok_r(bptr, " \t", &cptr);

      if (f_debug > 1) 
	fprintf(stderr, "Got: %s %s\n", key, val ? val : "");

      if (key[0] == '[' && key[len = strlen(key)-1] == ']') {
	key[len] = '\0';
	
	if (f_debug > 2)
	  fprintf(stderr, "[Switching hostgroup to %s]\n", key+1);
	
	hgp = group_create(key+1);
	if (!hgp) {
	  fprintf(stderr, "%s: %s#%u: %s: Invalid hostgroup\n", argv0, path, line, key+1);
	  exit(1);
	}
	
	/* Try to lookup gateway with same name as hostgroup */
	group_gp = gw_lookup(key+1);
	continue;
      }

      rc = 0;
      if (strcmp(key, "host") == 0) {
	hp = host_create(val);
	if (!hp) {
	  fprintf(stderr, "%s: %s#%u: %s: Invalid host name\n",
		  argv0, path, line, val);
	  exit(1);
	}

	if (hp->via == NULL && group_gp)
	  hp->via = group_gp;

	if (hgp)
	  group_add_host(hgp, hp);

	if (all_group)
	  group_add_host(all_group, hp);

      } else if (strcmp(key, "gateway") == 0) {
	if (hgp) {
	  fprintf(stderr, "%s: %s#%u: Can not define gateways in groups\n",
		  argv0, path, line);
	  exit(1);
	}
	gp = gw_create(val);
	if (!gp) {
	  fprintf(stderr, "%s: %s#%u: %s: Invalid gateway name\n",
		  argv0, path, line, val);
	  exit(1);
	}
	hp = NULL;

      } else if (strcmp(key, "name") == 0) {
	if (hgp && !hp)
	  goto InvalidOpt;
	if (hp)
	  rc = host_add_name(hp, val);
	else
	  rc = gw_add_name(gp, val);
      } else if (strcmp(key, "mac") == 0) {
	if (hp)
	  rc = host_add_mac(hp, val);
	else
	  goto InvalidOpt;
      } else if (strcmp(key, "via") == 0) {
	if (hp) {
	  rc = host_add_via(hp, val);
	}
	else
	  goto InvalidOpt;
      }	else if (strcmp(key, "copies") == 0) {
	if (hgp && !hp)
	  goto InvalidOpt;
	if (hp)
	  rc = host_add_copies(hp, val);
	else
	  rc = gw_add_copies(gp, val);
      } else if (strcmp(key, "secret") == 0) {
	if (hgp && !hp)
	  goto InvalidOpt;
	if (hp)
	  rc = host_add_secret(hp, val);
	else
	  rc = gw_add_secret(gp, val);
      } else if (strcmp(key, "delay") == 0) {
	if (hgp && !hp)
	  rc = group_add_delay(hgp, val);
	else if (hp)
	  rc = host_add_delay(hp, val);
	else
	  rc = gw_add_delay(gp, val);
      } else if (strcmp(key, "address") == 0) {
	if (hp)
	  goto InvalidOpt;
	rc = gw_add_address(gp, val);
      } else if (strcmp(key, "port") == 0) {
	if (hp)
	  goto InvalidOpt;
	rc = gw_add_port(gp, val);
      } else {
      InvalidOpt:
	fprintf(stderr, "%s: %s#%u: %s: Invalid option\n", argv0, path, line, key);
	exit(1);
      }

      if (rc < 0) {
	fprintf(stderr, "%s: %s#%u: %s: Invalid value for %s (rc=%d)\n", argv0, path, line, val, key, rc);
	exit(1);
      }
    }
  }
  
  fclose(fp);
  return 0;
}


void
export_all(void) {
  GATEWAY *gp;
  HOST *hp;
  HOSTGROUP *hgp;
  int n;


  if (!f_verbose) {
    puts("; pwol configuration");
    putchar('\n');
    puts("; Gateways:");
  }
  for (gp = gateways; gp; gp = gp->next) {

    if (!gp->name)
      continue; /* Skip over anonymous gateways */

    gw_print(gp);
  }

  putchar('\n');
  if (!f_verbose) {
    puts("; Hosts:");
  }
  for (hp = hosts; hp; hp = hp->next) {
    host_print(hp);
  }

  putchar('\n');
  if (!f_verbose) {
    puts("; Groups:");
  }
  n = 0;
  for (hgp = hostgroups; hgp; hgp = hgp->next) {
    if (!f_verbose && strcmp(hgp->name, "all") == 0)
      continue;
    
    if (hgp->hc == 0)
      continue;

    if (n++ > 0 && !f_verbose)
      putchar('\n');
    group_print(hgp);
  }
}


int daemon_run(GATEWAY *gp) {
  TARGET *tp;
  struct addrinfo *aip;
  int n, i;
  struct pollfd *pfdv;
  struct ether_addr *ep;
  HOST *hp;


  n = 0;
  for (tp = gp->targets; tp; tp = tp->next)
    ++n;

  if (n == 0) {
    errno = ENOENT;
    if (f_debug)
      fprintf(stderr, "*** daemon_run: No listening ports\n");
    return -1;
  }

  pfdv = malloc(n * sizeof(struct pollfd *));
  if (!pfdv)
    return -1;

  if (f_debug)
    fprintf(stderr, "[Creating and binding daemon sockets]\n");

  i = 0;
  for (tp = gp->targets; tp; tp = tp->next) {
    aip = tp->aip;
    tp->fd = socket((aip->ai_family == AF_INET ? PF_INET : PF_INET6), SOCK_DGRAM, IPPROTO_UDP);
    if (tp->fd < 0)
      return -1;

    if (bind(tp->fd, aip->ai_addr, aip->ai_addrlen) < 0)
      return -2;

    pfdv[i++].fd = tp->fd;
  }

  if (f_debug)
    fprintf(stderr, "[Entering daemon main loop]\n");
  
  while (1) {
    int i, rc;


    for (i = 0; i < n; i++) {
      pfdv[i].events = POLLIN;
      pfdv[i].revents = 0;
    }

    do {
      if (f_debug)
	fprintf(stderr, "(Waiting for messages on %u FDs)\n", n);
      rc = poll(&pfdv[0], n, -1);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0)
      return -1;

    for (i = 0; i < n; i++) {
      ssize_t rlen;
      struct sockaddr_storage peer;
      socklen_t peer_len;
      unsigned char buf[2048];
      size_t secret_size;


      if (pfdv[i].revents & POLLIN) {
	if (f_debug)
	  fprintf(stderr, "[Data available on FD #%u]\n", pfdv[i].fd);

	peer_len = sizeof(peer);
	while ((rlen = recvfrom(pfdv[i].fd, buf, sizeof(buf), 0, (struct sockaddr *) &peer, &peer_len)) < 0 && errno == EINTR)
	  ;
	
	if (rlen < 0)
	  return -1;

	if (f_debug)
	  fprintf(stderr, "[Message on FD #%u received from %s]\n", pfdv[i].fd, sockaddr2str((struct sockaddr *) &peer, peer_len));

	if (f_debug)
	  buf_print(stderr, buf, rlen);

	if (rlen < WOL_BODY_SIZE) {
	  if (f_debug)
	    fprintf(stderr, "*** Invalid WoL message (too short: %d bytes)\n", (int) rlen);
	  continue;
	}

	for (i = 0; i < HEADER_SIZE && buf[i] == 0xFF; i++)
	  ;
	if (i < HEADER_SIZE) {
	  if (f_debug)
	    fprintf(stderr, "*** Invalid WoL message (invalid header)\n");
	  continue;
	}

	for (i = 1; i < MAC_COPIES && memcmp(buf+HEADER_SIZE, buf+HEADER_SIZE+i*MAC_SIZE, MAC_SIZE) == 0; i++)
	  ;
	if (i < MAC_COPIES) {
	  if (f_debug)
	    fprintf(stderr, "*** Invalid WoL message (invalid MAC content copies)\n");
	  continue;
	}

	secret_size = rlen-WOL_BODY_SIZE;
	if (secret_size > 0) {
	  SECRET secret;

	  secret.size = secret_size;
	  memcpy(secret.buf, buf+WOL_BODY_SIZE, secret_size);
	  
	  if (f_debug)
	    fprintf(stderr, "*** Received secret: %s\n", secret2str(&secret));
	  
	  if (secret_compare(&gp->secret, &secret) != 0) {
	    if (f_debug)
	      fprintf(stderr, "*** Invalid received secret: %s\n", secret2str(&secret));
	    continue;
	  }
	}

	ep = (struct ether_addr *) (buf+HEADER_SIZE);
	for (hp = hosts; hp && memcmp(ep, &hp->mac, MAC_SIZE) != 0; hp = hp->next)
	  ;

	if (!hp) {
	  if (f_debug)
	    fprintf(stderr, "*** Unknown MAC (no such host): %s\n", ether_ntoa(ep));
	  continue;
	}

	printf("Got WoL for host: %s (%s)\n", hp->name, ether_ntoa(ep));
	if (send_wol_host(hp) < 0) {
	  if (f_debug)
	    fprintf(stderr, "*** Error send WoL message to %s (%s)\n", hp->name, ether_ntoa(ep));
	}
      }
    }
  }
}


void
become_daemon(void) {
 
  pid_t pid;
  int i, fd;


  pid = fork();
  if (pid < 0) {
    syslog(LOG_ERR, "fork() failed: %m");
    exit(EXIT_FAILURE);
  }
  else if (pid > 0)
    exit(EXIT_SUCCESS);

  signal(SIGTTOU, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
#ifdef SIGTTSP
  signal(SIGTTSP, SIG_IGN);
#endif

#ifdef HAVE_SETSID
  setsid();
#endif

  chdir("/");
  umask(0);

  fd = open("/dev/null", O_RDWR);

  for (i = 0; i < 3; i++)
    close(i);

  dup2(fd, 0);
  dup2(fd, 1);
  dup2(fd, 2);
}


int
main(int argc,
     char *argv[])
{
  char *home = getenv("HOME");
  char *home_config = strdupcat(home, "/", DEFAULT_USER_CONFIG, NULL);
  char *cp;
  int i, j;
  GATEWAY *gp, *proxy_gp = NULL;
  TARGET *tp;
  HOSTGROUP *hgp;


  argv0 = argv[0];

  /* Create default */
  
  default_gw = gw_create("default");
  if (!default_gw) {
    fprintf(stderr, "%s: Internal error #1458934\n", argv0);
    exit(1);
  }
  gw_add_address(default_gw, DEFAULT_ADDRESS);
  gw_add_port(default_gw, DEFAULT_PORT);
  gw_add_copies(default_gw, DEFAULT_COPIES);
  gw_add_delay(default_gw, DEFAULT_DELAY);
  gw_add_secret(default_gw, DEFAULT_SECRET);

  all_group = group_create("all");

  parse_config(DEFAULT_GLOBAL_CONFIG);
  if (home_config)
    parse_config(home_config);

  for (i = 1; i < argc && argv[i][0] == '-'; i++) {
    for (j = 1; argv[i][j]; j++)
      switch (argv[i][j]) {
      case 'V':
	header(stdout);
	exit(0);

      case 'v':
	++f_verbose;
	break;
		
      case 'd':
	++f_debug;
	break;
		
      case 'n':
	f_no = !f_no;
	break;
		
      case 'e':
	++f_export;
	break;
		
      case 'i':
	++f_ignore;
	break;
		
      case 'D':
	++f_daemon;
	break;

      case 'F':
	++f_foreground;
	break;

      case 'a':
	cp = argv[i]+j+1;
	if (!*cp && i+1 < argc) {
	  cp = argv[++i];
	}
	if (cp)
	  f_address = strdup(cp);
	goto NextArg;

      case 'g':
	cp = argv[i]+j+1;
	if (!*cp && i+1 < argc) {
	  cp = argv[++i];
	}
	if (cp)
	  f_gateway = strdup(cp);
	goto NextArg;

      case 'p':
	cp = argv[i]+j+1;
	if (!*cp && i+1 < argc) {
	  cp = argv[++i];
	}
	if (cp)
	  f_port = strdup(cp);
	goto NextArg;

      case 't':
	cp = argv[i]+j+1;
	if (!*cp && i+1 < argc) {
	  cp = argv[++i];
	}
	if (cp)
	  f_delay = strdup(cp);
	goto NextArg;

      case 'T':
	cp = argv[i]+j+1;
	if (!*cp && i+1 < argc) {
	  cp = argv[++i];
	}
	if (cp)
	  f_host_delay = strdup(cp);
	goto NextArg;

      case 'c':
	cp = argv[i]+j+1;
	if (!*cp && i+1 < argc) {
	  cp = argv[++i];
	}
	if (cp)
	  f_copies = strdup(cp);
	goto NextArg;
		
      case 's':
	cp = argv[i]+j+1;
	if (!*cp && i+1 < argc) {
	  cp = argv[++i];
	}
	if (cp)
	  f_secret = strdup(cp);
	goto NextArg;

      case 'f':
	cp = argv[i]+j+1;
	if (!*cp && i+1 < argc) {
	  cp = argv[++i];
	}
	if (cp)
	  parse_config(cp);
	goto NextArg;

      case 'A':
	cp = argv[i]+j+1;
	if (!*cp && i+1 < argc) {
	  cp = argv[++i];
	}
	if (cp) {
	  f_proxy_address = strdup(cp);
	}
	goto NextArg;

      case 'P':
	cp = argv[i]+j+1;
	if (!*cp && i+1 < argc) {
	  cp = argv[++i];
	}
	if (cp) {
	  if (!proxy_gp)
	    proxy_gp = gw_create(NULL);
	  gw_add_port(proxy_gp, cp);
	  f_proxy_port = strdup(cp);
	}
	goto NextArg;

      case 'S':
	cp = argv[i]+j+1;
	if (!*cp && i+1 < argc) {
	  cp = argv[++i];
	}
	if (cp)
	  f_proxy_secret = strdup(cp);
	goto NextArg;



      case 'h':
	printf("Usage: %s [<options>] [<host|group> [.. <host|group>]]\n",
	       argv[0]);
	puts("\nOptions:");
	puts("  -h           Display this information");
	puts("  -v           Increase verbosity");
	puts("  -d           Increase debug level");
	puts("  -i           Ignore errors");
	puts("  -n           Toggle \"no\" send mode");
	puts("  -e           Export configuration");
	puts("  -f <path>    Configuration file");
	puts("");
	puts("  -g <name>    Destination gateway");
	puts("  -a <addr>    Destination address");
	puts("  -p <port>    Destination port");
	puts("  -t <time>    Inter-packet delay");
	puts("  -T <time>    Inter-host delay");
	puts("  -c <count>   Packet copies to send");
	puts("  -s <secret>  Force WoL secret");
	puts("");
	puts("  -D           Run as proxy daemon");
	puts("  -F           Run proxy daemon in foreground");
	puts("  -A <addr>    Proxy daemon listen address");
	puts("  -P <port>    Proxy daemon listen port");
	puts("  -S <secret>  Proxy daemon secret");
	puts("");
	puts("If no hosts/groups specified on the command line pwol will read them from stdin");
	exit(0);

      case '-':
	++i;
	goto EndArg;

      default:
	fprintf(stderr, "%s: -%c: Invalid switch\n",
		argv[0], argv[i][j]);
	exit(1);
      }
  NextArg:;
  }
 EndArg:

  if (f_verbose)
    header(stdout);

  if (f_daemon) {
    proxy_gp = gw_create(NULL);
    gw_add_address(proxy_gp, f_proxy_address ? f_proxy_address : DEFAULT_PROXY_ADDRESS);
    gw_add_port(proxy_gp, f_proxy_port ? f_proxy_port : DEFAULT_PROXY_PORT);
    if (f_proxy_secret)
      gw_add_secret(proxy_gp, f_proxy_secret);
  }

  /* Open sockets for sending packets via the gateway targets */
  for (gp = gateways; gp; gp = gp->next) {
    struct addrinfo hints, *aip;
    int rc;
    char *addr, *port;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    
    aip = NULL;

    addr = f_address;
    if (!addr)
      addr = gp->address;
    if (!addr)
      addr = DEFAULT_ADDRESS;

    port = f_port;
    if (!port)
      port = gp->port;
    if (!port)
      port = DEFAULT_PORT;

    if ((rc = getaddrinfo(addr, port, &hints, &aip)) != 0) {
      fprintf(stderr, "%s: %s port %s: Invalid target\n", argv[0], addr, port);
      exit(1);
    }

    for (; aip; aip = aip->ai_next) {
      int one = 1;

      tp = target_add(&gp->targets, aip);
      if (!tp) {
	fprintf(stderr, "%s: %s port %s: Internal Error #3345921\n", argv[0], gp->address, gp->port);
	exit(1);
      }

      tp->fd = socket((aip->ai_family == AF_INET ? PF_INET : PF_INET6), SOCK_DGRAM, IPPROTO_UDP);
      if (tp->fd < 0) {
	fprintf(stderr, "%s: %s port %s: socket: %s\n", argv[0], gp->address, gp->port, strerror(errno));
	exit(1);
      }
#ifdef SO_BROADCAST
      (void) setsockopt(tp->fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
#endif
    }
  }
  
  if (f_host_delay) {
    for (hgp = hostgroups; hgp; hgp = hgp->next)
      group_add_delay(hgp, f_host_delay);
  }

  if (f_export) {
    export_all();
    exit(0);
  }

  if (f_daemon) {
    if (!f_foreground)
      become_daemon();

    if (daemon_run(proxy_gp) < 0) {
      fprintf(stderr, "%s: daemon_run: %s\n", argv[0], strerror(errno));
      exit(1);
    }
      
    exit(0);
  }

  if (i < argc) {
    for (;i < argc; i++) {
      if (send_wol(argv[i]) < 0 && !f_ignore) {
	fprintf(stderr, "%s: %s: Sending WoL packet failed: %s\n",
		argv[0], argv[i], strerror(errno));
	exit(1);
      }
    }
  } else {
    char lbuf[80], *lp, *lptr, *cp;

	
    if ((f_verbose || f_debug) && isatty(0)) {
      fputs("Enter hosts or groups:\n", stderr);
      fflush(stderr);
    }

    while (fgets(lbuf, sizeof(lbuf), stdin)) {
      trim(lbuf);
      if (!*lbuf)
	continue;

      lp = lbuf;
      while ((cp = strtok_r(lp, " \t", &lptr)) != NULL) {
	lp = NULL;

	if (send_wol(cp) < 0) {
	  fprintf(stderr, "%s: %s: Sending WoL packet failed: %s\n",
		  argv[0], cp, strerror(errno));
	  
	  if (!f_ignore)
	    exit(1);
	}
      }
    }
  }

  exit(0);
}
