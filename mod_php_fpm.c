/*
 * (C) 2010-2011 Alibaba Group Holding Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */


#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include "tsar.h"

#define STATS_TEST_SIZE (sizeof(struct stats_php_fpm))

static const char *php_fpm_usage = "    --php-fpm               php-fpm information";

/*
 * pool:                 www
 * process manager:      dynamic
 * start time:           27/Nov/2016:22:08:11 +0800
 * start since:          1729303
 * accepted conn:        553392
 * listen queue:         0
 * max listen queue:     0
 * listen queue len:     0
 * idle processes:       98
 * active processes:     3
 * total processes:      101
 * max active processes: 101
 * max children reached: 0
 * slow requests:        94
 */
struct stats_php_fpm {
    unsigned long long    n_accepted_conn;
    unsigned long long    n_listen_queue;
    unsigned long long    n_max_listen_queue;
    unsigned long long    n_listen_queue_len;
    unsigned long long    n_idle_processes;
    unsigned long long    n_active_processes;
    unsigned long long    n_total_processes;
    unsigned long long    n_max_active_processes;
    unsigned long long    n_max_children_reached;
    unsigned long long    n_slow_requests;
};

struct hostinfo {
    char *host;
    int   port;
    char *server_name;
    char *uri;
};

/* Structure for tsar */
static struct mod_info php_fpm_info[] = {
    {"accept", DETAIL_BIT,  0,  STATS_SUB},
    {" queue", DETAIL_BIT,  0,  STATS_NULL},
    {"  maxq", DETAIL_BIT,  0,  STATS_NULL},
    {"  qlen", DETAIL_BIT,  0,  STATS_NULL},
    {"  idle", DETAIL_BIT,  0,  STATS_NULL},
    {"active", DETAIL_BIT,  0,  STATS_NULL},
    {" total", DETAIL_BIT,  0,  STATS_NULL},
    {"maxact", DETAIL_BIT,  0,  STATS_NULL},
    {"maxrea", DETAIL_BIT,  0,  STATS_NULL},
    {"   qps", DETAIL_BIT,  0,  STATS_SUB_INTER},
    {"  sreq", DETAIL_BIT,  0,  STATS_SUB}
};

static void
init_php_fpm_host_info(struct hostinfo *p)
{
    char *port;

    p->host = getenv("PHP_FPM_TSAR_HOST");
    p->host = p->host ? p->host : "127.0.0.1";

    port = getenv("PHP_FPM_TSAR_PORT");
    p->port = port ? atoi(port) : 80;

    p->uri = getenv("PHP_FPM_TSAR_URI");
    p->uri = p->uri ? p->uri : "/php-fpm-status";

    p->server_name = getenv("PHP_FPM_TSAR_SERVER_NAME");
    p->server_name = p->server_name ? p->server_name : "127.0.0.1";
}

static void
read_php_fpm_stats(struct module *mod, const char *parameter)
{
    int                 addr_len, domain;
    int                 m, sockfd, send, pos;
    void               *addr;
    char                buf[LEN_4096], request[LEN_4096], line[LEN_4096];
    FILE               *stream = NULL;

    struct sockaddr_in  servaddr;
    struct sockaddr_un  servaddr_un;
    struct hostinfo     hinfo;

    init_php_fpm_host_info(&hinfo);
    if (atoi(parameter) != 0) {
       hinfo.port = atoi(parameter);
    }
    struct stats_php_fpm st_php_fpm;
    memset(&st_php_fpm, 0, sizeof(struct stats_php_fpm));

    if (*hinfo.host == '/') {
        addr = &servaddr_un;
        addr_len = sizeof(servaddr_un);
        bzero(addr, addr_len);
        domain = AF_LOCAL;
        servaddr_un.sun_family = AF_LOCAL;
        strncpy(servaddr_un.sun_path, hinfo.host, sizeof(servaddr_un.sun_path) - 1);

    } else {
        addr = &servaddr;
        addr_len = sizeof(servaddr);
        bzero(addr, addr_len);
        domain = AF_INET;
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(hinfo.port);
        inet_pton(AF_INET, hinfo.host, &servaddr.sin_addr);
    }


    if ((sockfd = socket(domain, SOCK_STREAM, 0)) == -1) {
        goto writebuf;
    }

    sprintf(request,
            "GET %s HTTP/1.0\r\n"
            "User-Agent: taobot\r\n"
            "Host: %s\r\n"
            "Accept:*/*\r\n"
            "Connection: Close\r\n\r\n",
            hinfo.uri, hinfo.server_name);

    if ((m = connect(sockfd, (struct sockaddr *) addr, addr_len)) == -1 ) {
        goto writebuf;
    }

    if ((send = write(sockfd, request, strlen(request))) == -1) {
        goto writebuf;
    }

    if ((stream = fdopen(sockfd, "r")) == NULL) {
        goto writebuf;
    }

    while (fgets(line, LEN_4096, stream) != NULL) {
        if (strncmp(line, "pool:", sizeof("pool:") - 1) == 0 || 
            strncmp(line, "process manager:", sizeof("process manager:") - 1) == 0 || 
            strncmp(line, "start time:", sizeof("start time:") - 1) == 0 || 
            strncmp(line, "start since:", sizeof("start since:") - 1) == 0) {
            ;
        } else if (!strncmp(line,  "accepted conn:", sizeof("accepted conn:") - 1)) {
            sscanf(line + 22, "%llu", &st_php_fpm.n_accepted_conn);
            //st_php_fpm.n_accepted_conn = atoll(line + 22);
        } else if (!strncmp(line,  "listen queue:", sizeof("listen queue:") - 1)) {
            st_php_fpm.n_listen_queue = atoll(line + 22);
        } else if (!strncmp(line, "max listen queue:", sizeof("max listen queue:") - 1)) {
            st_php_fpm.n_max_listen_queue = atoll(line + 22);
        } else if (!strncmp(line, "listen queue len:", sizeof("listen queue len:") - 1)) {
            st_php_fpm.n_listen_queue_len = atoll(line + 22);
        } else if (!strncmp(line, "idle processes:", sizeof("idle processes:") - 1)) {
            st_php_fpm.n_idle_processes = atoll(line + 22);
        } else if (!strncmp(line, "active processes:", sizeof("active processes:") - 1)) {
            st_php_fpm.n_active_processes = atoll(line + 22);
        } else if (!strncmp(line, "total processes:", sizeof("total processes:") - 1)) {
            st_php_fpm.n_total_processes = atoll(line + 22);
        } else if (!strncmp(line, "max active processes:", sizeof("max active processes:") - 1)) {
            st_php_fpm.n_max_active_processes = atoll(line + 22);
        } else if (!strncmp(line, "max children reached:", sizeof("max children reached:") - 1)) {
            st_php_fpm.n_max_children_reached = atoll(line + 22);
        } else if (!strncmp(line, "slow requests:", sizeof("slow requests:") - 1)) {
            st_php_fpm.n_slow_requests = atoll(line + 22);
        } else {
            ;
        }
    }

writebuf:
    if (stream) {
        fclose(stream);
    }

    if (sockfd != -1) {
        close(sockfd);
    }

    pos = sprintf(buf, 
        "%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld",
        st_php_fpm.n_accepted_conn,
        st_php_fpm.n_listen_queue,
        st_php_fpm.n_max_listen_queue,
        st_php_fpm.n_listen_queue_len,
        st_php_fpm.n_idle_processes,
        st_php_fpm.n_active_processes,
        st_php_fpm.n_total_processes,
        st_php_fpm.n_max_active_processes,
        st_php_fpm.n_max_children_reached,
        st_php_fpm.n_slow_requests
    );
    buf[pos] = '\0';
    set_mod_record(mod, buf);
}

static void
set_php_fpm_record(struct module *mod, double st_array[],
    U_64 pre_array[], U_64 cur_array[], int inter)
{
    int i;
    for (i = 0; i < 1; i ++) {
        if(cur_array[i] >= pre_array[i]) {
            st_array[i] = cur_array[i] - pre_array[i];
        } else {
            st_array[i] = 0;
        }
    }
    for (i = 1; i < 9; i++) {
        st_array[i] = cur_array[i];
    }
    for (i = 9; i < 10; i ++) {
        if(cur_array[i] >= pre_array[i]) {
            st_array[i] = (cur_array[i] - pre_array[i]) / inter;
        } else {
            st_array[i] = 0;
        }
    }
    for (i = 10; i < 11; i ++) {
        if(cur_array[i] >= pre_array[i]) {
            st_array[i] = cur_array[i] - pre_array[i];
        } else {
            st_array[i] = 0;
        }
    }
}

/* register mod to tsar */
void
mod_register(struct module *mod)
{
    register_mod_fields(mod, "--php-fpm", php_fpm_usage, php_fpm_info, 11, read_php_fpm_stats, set_php_fpm_record);
}
