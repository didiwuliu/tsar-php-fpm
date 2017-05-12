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
#include "fcgi.h"

typedef unsigned char u_char;
#define STATS_TEST_SIZE (sizeof(struct stats_php_fpm))

#define BYTE_0(n) ((n) & 0xff)
#define BYTE_1(n) (((n) >> 8) & 0xff)
#define BYTE_2(n) (((n) >> 16) & 0xff)
#define BYTE_3(n) (((n) >> 24) & 0xff)


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

typedef enum scheme_type {
    HTTP = 1,
    TCP,
    UNIX
} scheme_type;

struct hostinfo {
    scheme_type type;
    int         port;
    char       *host;
    char       *server_name;
    char       *uri;
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
    {"   qps", SUMMARY_BIT, 0,  STATS_SUB_INTER},
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
parse_response_line(char * line, struct stats_php_fpm * st_php_fpm) 
{
    unsigned long long n;

    if (strncmp(line, "pool:", sizeof("pool:") - 1) == 0 || 
        strncmp(line, "process manager:", sizeof("process manager:") - 1) == 0 || 
        strncmp(line, "start time:", sizeof("start time:") - 1) == 0 || 
        strncmp(line, "start since:", sizeof("start since:") - 1) == 0) {
        ;
    } else if (!strncmp(line,  "accepted conn:", sizeof("accepted conn:") - 1)) {
        sscanf(line + 22, "%llu", &n);
        st_php_fpm->n_accepted_conn += n;
        //st_php_fpm.n_accepted_conn = atoll(line + 22);
    } else if (!strncmp(line,  "listen queue:", sizeof("listen queue:") - 1)) {
        st_php_fpm->n_listen_queue += atoll(line + 22);
    } else if (!strncmp(line, "max listen queue:", sizeof("max listen queue:") - 1)) {
        st_php_fpm->n_max_listen_queue += atoll(line + 22);
    } else if (!strncmp(line, "listen queue len:", sizeof("listen queue len:") - 1)) {
        st_php_fpm->n_listen_queue_len += atoll(line + 22);
    } else if (!strncmp(line, "idle processes:", sizeof("idle processes:") - 1)) {
        st_php_fpm->n_idle_processes += atoll(line + 22);
    } else if (!strncmp(line, "active processes:", sizeof("active processes:") - 1)) {
        st_php_fpm->n_active_processes += atoll(line + 22);
    } else if (!strncmp(line, "total processes:", sizeof("total processes:") - 1)) {
        st_php_fpm->n_total_processes += atoll(line + 22);
    } else if (!strncmp(line, "max active processes:", sizeof("max active processes:") - 1)) {
        st_php_fpm->n_max_active_processes += atoll(line + 22);
    } else if (!strncmp(line, "max children reached:", sizeof("max children reached:") - 1)) {
        st_php_fpm->n_max_children_reached += atoll(line + 22);
    } else if (!strncmp(line, "slow requests:", sizeof("slow requests:") - 1)) {
        st_php_fpm->n_slow_requests += atoll(line + 22);
    } else {
        ;
    }
}

static void 
read_php_fpm_stats_by_http(struct hostinfo * hinfo, struct stats_php_fpm * st_php_fpm)
{
    void               *addr;
    int                 m, sockfd, sent;
    int                 addr_len, domain;
    struct sockaddr_in  servaddr;
    struct sockaddr_un  servaddr_un;

    char                request[LEN_4096];
    char                line[LEN_4096];
    FILE               *stream = NULL;

    if (*hinfo->host == '/') {
        addr = &servaddr_un;
        addr_len = sizeof(servaddr_un);
        bzero(addr, addr_len);
        domain = AF_LOCAL;
        servaddr_un.sun_family = AF_LOCAL;
        strncpy(servaddr_un.sun_path, hinfo->host, sizeof(servaddr_un.sun_path) - 1);

    } else {
        addr = &servaddr;
        addr_len = sizeof(servaddr);
        bzero(addr, addr_len);
        domain = AF_INET;
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(hinfo->port);
        inet_pton(AF_INET, hinfo->host, &servaddr.sin_addr);
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
            hinfo->uri, hinfo->server_name);

    if ((m = connect(sockfd, (struct sockaddr *) addr, addr_len)) == -1 ) {
        goto writebuf;
    }

    if ((sent = write(sockfd, request, strlen(request))) == -1) {
        goto writebuf;
    }

    if ((stream = fdopen(sockfd, "r")) == NULL) {
        goto writebuf;
    }

    while (fgets(line, LEN_4096, stream) != NULL) {
        parse_response_line(line, st_php_fpm);
    }

writebuf:
    if (stream) {
        fclose(stream);
    }

    if (sockfd != -1) {
        close(sockfd);
    }
}

static uint32_t 
serialize_name_value(unsigned char * buffer, unsigned char * name, unsigned char *value) {
    unsigned char *p = buffer;
    uint32_t nl, vl; 
    nl = strlen((char*)name);
    vl = strlen((char*)value);

    if( nl < 128 )
        *p++ = BYTE_0(nl);
    else
    {   
        *p++ = BYTE_0(nl);
        *p++ = BYTE_1(nl);
        *p++ = BYTE_2(nl);
        *p++ = BYTE_3(nl);
    }   

    if ( vl < 128 )
        *p++ = BYTE_0(vl);
    else
    {   
        *p++ = BYTE_0(vl);
        *p++ = BYTE_1(vl);
        *p++ = BYTE_2(vl);
        *p++ = BYTE_3(vl);
    }   

    memcpy(p, name, nl);
    p+=nl;
    memcpy(p, value, vl);
    p+= vl; 

    return p - buffer;
}


static void 
read_php_fpm_stats_by_fastcgi(struct hostinfo * hinfo, struct stats_php_fpm * st_php_fpm)
{
    void               *addr;
    int                 m, sockfd, sent;
    int                 addr_len, domain;
    struct sockaddr_in  servaddr;
    struct sockaddr_un  servaddr_un;

    int                 nb;
    char               *p, *p1;
    uint16_t            req_id = 1;
    char               *token, *pos = NULL;

    FCGI_Header         header;
    FCGI_BeginRequestRecord begin_rec;

    char                request[LEN_4096] = {0}, response[LEN_4096] = {0};
    FILE               *stream = NULL;

    if (*hinfo->host == '/') {
        addr = &servaddr_un;
        addr_len = sizeof(servaddr_un);
        bzero(addr, addr_len);
        domain = AF_LOCAL;
        servaddr_un.sun_family = AF_LOCAL;
        strncpy(servaddr_un.sun_path, hinfo->host, sizeof(servaddr_un.sun_path) - 1);

    } else {
        addr = &servaddr;
        addr_len = sizeof(servaddr);
        bzero(addr, addr_len);
        domain = AF_INET;
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(hinfo->port);
        inet_pton(AF_INET, hinfo->host, &servaddr.sin_addr);
    }


    if ((sockfd = socket(domain, SOCK_STREAM, 0)) == -1) {
        goto writebuf;
    }

    if ((m = connect(sockfd, (struct sockaddr *) addr, addr_len)) == -1 ) {
        goto writebuf;
    }

    p = request;

    // send begin request record
    memset(&begin_rec, 0, sizeof(begin_rec));
    begin_rec.header.type = FCGI_BEGIN_REQUEST;
    begin_rec.header.version = FCGI_VERSION_1;
    begin_rec.header.requestIdB1 = BYTE_1(req_id);
    begin_rec.header.requestIdB0 = BYTE_0(req_id);
    begin_rec.header.contentLengthB0 = sizeof(FCGI_BeginRequestBody);
    begin_rec.body.roleB1 = BYTE_1(FCGI_RESPONDER);
    begin_rec.body.roleB0 = BYTE_0(FCGI_RESPONDER);

    memcpy(p, &begin_rec, sizeof(begin_rec));
    p += sizeof(begin_rec);

    // send fcgi params
    p1 = p;
    memcpy(&header, &begin_rec.header, sizeof(header));
    header.type = FCGI_PARAMS;

    p += sizeof(header);

#define SNV(p, k, v) \
    (p) += serialize_name_value((u_char*)(p), (u_char*) (k), (u_char*) (v));
	
    SNV(p, "SCRIPT_FILENAME", hinfo->uri);
    SNV(p, "SCRIPT_NAME", hinfo->uri);
    SNV(p, "REQUEST_URI", hinfo->uri);
    SNV(p, "SERVER_NAME", hinfo->server_name);
    SNV(p, "GATEWAY_INTERFACE", "CGI/1.1");
    SNV(p, "REQUEST_METHOD", "GET");
    //SNV(p, "DOCUMENT_ROOT", "/var/www/html");
    //SNV(p, "PHP_SELF", hinfo->uri);
    //SNV(p, "TERM", "linux");
    //SNV(p, "PATH", "");
    //SNV(p, "PHP_FCGI_CHILDREN", "2");
    //SNV(p, "PHP_FCGI_MAX_REQUESTS", "1000");
    //SNV(p, "FCGI_ROLE", "RESPONDER");
    //SNV(p, "SERVER_SOFTWARE", "tsar-php-fpm/1.0");
    //SNV(p, "SERVER_PORT", "9999");
    //SNV(p, "SERVER_ADDR", "127.0.0.1");
    //SNV(p, "PATH_INFO", "no value");
    //SNV(p, "QUERY_STRING", "");
    //SNV(p, "REDIRECT_STATUS", "200");
    //SNV(p, "SERVER_PROTOCOL", "HTTP/1.1");
    //SNV(p, "HTTP_HOST", hinfo->server_name);
    //SNV(p, "HTTP_CONNECTION", "close");
    //SNV(p, "HTTP_USER_AGENT", "tsar-php-fpm/1.0");
    //SNV(p, "HTTP_ACCEPT", "*/*");

    // set the params header
    header.contentLengthB1 = BYTE_1(p - p1 - sizeof(header));
    header.contentLengthB0 = BYTE_0(p - p1 - sizeof(header));
    header.paddingLength = (p - request) % 8;
    memcpy(p1, &header, sizeof(header));
    p += header.paddingLength;

    // send empty fcgi params
    header.contentLengthB0 = 0;
    header.contentLengthB1 = 0;
    header.paddingLength = 0;
    memcpy(p, &header, sizeof(header));
    p += sizeof(header);

    if ((sent = send(sockfd, request, p - request, 0)) == -1) {
        goto writebuf;
    }

    while (1) {
        if((nb = recv(sockfd, response, LEN_4096 - 1, 0)) == -1) {
    	    goto writebuf;
        }

        if(nb == 0) {
            break;
        }
    
        p = response;
        while ( p < response + nb) { 

            memcpy(&header, p, sizeof(header));
    
            if(header.type == FCGI_END_REQUEST) {
                break;
            } else if(header.type == FCGI_STDOUT) {
                p1 = strstr(p + sizeof(header), "\r\n\r\n");
    
                if(p1 != NULL) {
                    p1 += 4;
                    while((token = strtok_r(p1, "\n", &pos)) != NULL) {
                        parse_response_line(token, st_php_fpm);
                        p1 = NULL;
                    }
                }
            }
    
            p += sizeof(header) 
                + header.contentLengthB0 + (header.contentLengthB0 << 8) 
                + header.paddingLength;
        }
    }

writebuf:
    if (stream) {
        fclose(stream);
    }

    if (sockfd != -1) {
        close(sockfd);
    }
}

static void
read_php_fpm_stats(struct module *mod, const char *parameter)
{
    int                 origin_port, len;
    struct hostinfo     hinfo;
    char                buf[LEN_4096];
    char               *host, *sport;
    char               *token, *p, *pos;

    struct stats_php_fpm st_php_fpm;
    memset(&st_php_fpm, 0, sizeof(struct stats_php_fpm));

    init_php_fpm_host_info(&hinfo);

    host = strdup(hinfo.host);
    origin_port = hinfo.port;

    p = host;
    pos = NULL;
    while((token = strtok_r(p, ",", &pos)) != NULL) {
        // tcp://127.0.0.1:9000/
        if(strncmp(token, "tcp://", sizeof("tcp://") - 1) == 0) {
            token += sizeof("tcp://") - 1;

            sport = strchr(token, ':');  
            *sport ++ = '\0';
            
            hinfo.type = TCP;
            hinfo.host = token;
            hinfo.port = atoi(sport);
            read_php_fpm_stats_by_fastcgi(&hinfo, &st_php_fpm);

            p = NULL;
            continue;
        // unix:/path/to/unix/domain/socket
        } else if(strncmp(token, "unix:", sizeof("unix:") - 1) == 0) {
            token += sizeof("unix:") - 1;

            hinfo.type = UNIX;
            hinfo.host = token;
            read_php_fpm_stats_by_fastcgi(&hinfo, &st_php_fpm);

            p = NULL;
            continue;
        // http://127.0.0.1/
        // http://127.0.0.1:80/
        } else if(strncmp(token, "http://", sizeof("http://") - 1) == 0) {
            token += sizeof("http://") - 1;
        }

        // 127.0.0.1:80
        // 127.0.0.1
        sport = strchr(token, ':');
        if(sport != NULL) {
            *sport ++ = '\0';
            hinfo.port = atoi(sport);
        } else {
            if((sport = strchr(token, '/'))) {
                *sport = '\0';
            }
            hinfo.port = origin_port;
        }
        hinfo.type = HTTP;
        hinfo.host = token;
        read_php_fpm_stats_by_http(&hinfo, &st_php_fpm);

        p = NULL;
    }
    if(host != NULL) {
        free(host);
        host = NULL;
    }

    len = sprintf(buf, 
        "%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld",
        st_php_fpm.n_accepted_conn,
        st_php_fpm.n_listen_queue,
        st_php_fpm.n_max_listen_queue,
        st_php_fpm.n_listen_queue_len,
        st_php_fpm.n_idle_processes,
        st_php_fpm.n_active_processes,
        st_php_fpm.n_total_processes,
        st_php_fpm.n_max_active_processes,
        st_php_fpm.n_max_children_reached,
        st_php_fpm.n_accepted_conn,
        st_php_fpm.n_slow_requests
    );
    buf[len] = '\0';
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
