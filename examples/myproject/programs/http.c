/*File : http.c
 *Auth : sjin
 *Date : 20141206
 *Mail : 413977243@qq.com
 */
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "openssl/ssl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "espressif/c_types.h"
#include "lwip/sockets.h"

#include <string.h>

#include "http.h"

#define OPENSSL_FRAGMENT_SIZE   8192
#define OPENSSL_LOCAL_TCP_PORT  1000
#define ADDR_CACHE_SIZE         2

#define HTTP_POST "POST /%s HTTP/1.1\r\nHOST: %s:%d\r\nAccept: */*\r\n"\
    "Content-Type:application/x-www-form-urlencoded\r\nContent-Length: %d\r\n\r\n%s"

#define HTTP_GET "GET /%s HTTP/1.1\r\nHOST: %s:%d\r\nAccept: */*\r\n\r\n"

typedef struct {
    char host[HOST_SIZE];
    u32_t addr;
    int port;
    int socket;
    SSL_CTX *ctx;
    SSL *ssl;
} addr_cache_t;

static addr_cache_t HTTP_CTX[ADDR_CACHE_SIZE];
static u8 addr_index = 0;
//LOCAL char tmp_buf[1024];

static int add_http_ctx(const char *host, u32_t ip_addr, int port, int socket, SSL_CTX *ctx, SSL *ssl) {
    if(addr_index > ADDR_CACHE_SIZE || addr_index < 0) {
        printf("address cache space no enought!\n");
        return -1;
    }

    strncpy(HTTP_CTX[addr_index].host, host, HOST_SIZE);
    HTTP_CTX[addr_index].addr = ip_addr;
    HTTP_CTX[addr_index].port = port;
    HTTP_CTX[addr_index].socket = socket;
    HTTP_CTX[addr_index].ctx = ctx;
    HTTP_CTX[addr_index].ssl = ssl;

    printf("index(%d) add %s IP is %d.%d.%d.%d\n", addr_index, host, (unsigned char)((ip_addr & 0x000000ff) >> 0),
                                                (unsigned char)((ip_addr & 0x0000ff00) >> 8),
                                                (unsigned char)((ip_addr & 0x00ff0000) >> 16),
                                                (unsigned char)((ip_addr & 0xff000000) >> 24));

    printf("port:%d socket: %d\n", port, socket);

    return addr_index++;
}

static int index_of_http_ctx(const char *host) {
    int i;
    for(i = 0; i < ADDR_CACHE_SIZE; i++) {
        if(!strncmp(HTTP_CTX[i].host, host, HOST_SIZE)) {
            return i;
        }
    }
    return -1;
}

static int get_http_ctx_tcp_info(int index, ip_addr_t *addr, int *port, int *socket) {

    if(index < 0 || index >= ADDR_CACHE_SIZE) {
        return -1;
    }
    
    //printf("index = %d, addr = %p, port = %p, socket = %p\n", index, addr, port, socket);

    if(addr)
        addr->addr = HTTP_CTX[index].addr;
    if(port)
        *port = HTTP_CTX[index].port;
    if(socket)
        *socket = HTTP_CTX[index].socket;

    /*printf("get %s IP is %d.%d.%d.%d\n", HTTP_CTX[index].host, (unsigned char)((addr->addr & 0x000000ff) >> 0),
                                            (unsigned char)((addr->addr & 0x0000ff00) >> 8),
                                            (unsigned char)((addr->addr & 0x00ff0000) >> 16),
                                            (unsigned char)((addr->addr & 0xff000000) >> 24));
    printf("port:%d socket: %d\n", *port, *socket);*/

    return index;
}

static int get_http_ctx_ssl_info(int index, SSL_CTX **ctx, SSL **ssl) {

    if(index < 0 || index > ADDR_CACHE_SIZE) {
        return -1;
    }

    if(ctx)
        *ctx = HTTP_CTX[index].ctx;
    if(ssl) {
        *ssl = HTTP_CTX[index].ssl;
        //printf("get_http_ctx_ssl_info: ssl = %p\n", *ssl);
    }

    return index;
}

static int update_http_ctx_tcp_info(int index, u32_t ip_addr, int port, int socket) {
    if(index > ADDR_CACHE_SIZE || index < 0) {
        printf("update address cache failed: error index(%d)!\n", index);
        return -1;
    }

    HTTP_CTX[index].addr = ip_addr;
    HTTP_CTX[index].port = port;
    HTTP_CTX[index].socket = socket;
    printf("index(%d) update %s IP is %d.%d.%d.%d\n", index, HTTP_CTX[index].host, (unsigned char)((ip_addr & 0x000000ff) >> 0),
                                                (unsigned char)((ip_addr & 0x0000ff00) >> 8),
                                                (unsigned char)((ip_addr & 0x00ff0000) >> 16),
                                                (unsigned char)((ip_addr & 0xff000000) >> 24));

    printf("port:%d socket: %d\n", port, socket);

    return index;
}

static int update_http_ctx_ssl_info(int index, SSL_CTX *ctx, SSL *ssl) {
    if(index > ADDR_CACHE_SIZE || index < 0) {
        printf("update address cache failed: error index(%d)!\n", index);
        return -1;
    }

    if(HTTP_CTX[index].ssl) {
        SSL_shutdown(HTTP_CTX[index].ssl);
        SSL_free(HTTP_CTX[index].ssl);        
    }
    HTTP_CTX[index].ssl = ssl;
    //printf("update_http_ctx_ssl_info: ssl = %p\n", ssl);

    if(HTTP_CTX[index].ctx) {
        SSL_CTX_free(HTTP_CTX[index].ctx);
    }
    HTTP_CTX[index].ctx = ctx;
    //printf("update_http_ctx_ssl_info: ctx = %p\n", ctx);

    printf("index(%d) update_http_ctx_ssl_info\n", index);

    return index;
}

static int http_tcpclient_create(const char *host, int port){
    int ret;
    struct sockaddr_in sock_addr; 
    int socket_fd;
    ip_addr_t target_ip;
    int index;
    int get_port;
    SSL_CTX *ctx;
    SSL *ssl;
    static int bind_port = 1000;

    target_ip.addr = 0;
    socket_fd = -1;

    if(port != HTTPS_DEFAULT_PORT) {
        index = get_http_ctx_tcp_info(index_of_http_ctx(host), &target_ip, &get_port, &socket_fd);
        if(index >= 0 && socket_fd >= 0)
            return index;
    } else {
        index = get_http_ctx_ssl_info(index_of_http_ctx(host), &ctx, &ssl);
        if(index >= 0 && ssl != 0)
            return index;
    }    

    if(index == -1 || target_ip.addr == 0) {
        ret = netconn_gethostbyname(host, &target_ip);
        if(ret < 0) {
            printf("netconn_gethostbyname failed!\n");
            return -1;
        }
        printf("reget %s IP is %d.%d.%d.%d\n", host, (unsigned char)((target_ip.addr & 0x000000ff) >> 0),
                                                    (unsigned char)((target_ip.addr & 0x0000ff00) >> 8),
                                                    (unsigned char)((target_ip.addr & 0x00ff0000) >> 16),
                                                    (unsigned char)((target_ip.addr & 0xff000000) >> 24));

    }

    if(index >= 0)
        update_http_ctx_tcp_info(index, target_ip.addr, port, socket_fd);
    else
        index = add_http_ctx(host, target_ip.addr, port, -1, NULL, NULL);

    printf("create socket ......");
    socket_fd = socket(AF_INET,SOCK_STREAM,0);
    if(socket_fd < 0){
        printf("failed\n");
        return -2;
    }
    printf("OK\n");

    if(port == HTTPS_DEFAULT_PORT) {
        printf("bind socket ......");
        memset(&sock_addr, 0, sizeof(sock_addr));
        sock_addr.sin_family = AF_INET;
        sock_addr.sin_addr.s_addr = 0;
        sock_addr.sin_port = htons(bind_port++);//htons(OPENSSL_LOCAL_TCP_PORT);
        ret = bind(socket_fd, (struct sockaddr*)&sock_addr, sizeof(sock_addr));
        if (ret) {
            printf("failed(%d)\n", bind_port);
            ret = -3;
            goto BIND_FAIL;
        }
        printf("OK\n");
    }

    printf("socket connect to remote ......");
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(port);
    sock_addr.sin_addr.s_addr = target_ip.addr;
    ret = connect(socket_fd, (struct sockaddr *)&sock_addr,sizeof(struct sockaddr));
    if(ret){
        printf("failed\n");
        ret = -4;
        goto BIND_FAIL;
    }
    printf("OK\n");

    update_http_ctx_tcp_info(index, target_ip.addr, port, socket_fd);

    if(port == HTTPS_DEFAULT_PORT) {
    	printf("create SSL context ......");
        ctx = SSL_CTX_new(TLSv1_1_client_method());
        if (!ctx) {
            printf("failed\n");
            ret = ERR_CTX;
            goto NEW_CTX_FAIL;
        }
        printf("OK\n");

        printf("set SSL context read buffer size ......");
        SSL_CTX_set_default_read_buffer_len(ctx, OPENSSL_FRAGMENT_SIZE);
        printf("OK\n");

        printf("create SSL ......");
        ssl = SSL_new(ctx);
        if (!ssl) {
            printf("failed\n");
            ret = ERR_SSL_NEW;
            goto NEW_SSL_FAIL;
        }
        printf("OK\n");

        SSL_set_fd(ssl, socket_fd);

        printf("SSL connected to %s port %d ......", host, port);
        ret = SSL_connect(ssl);
        if (!ret) {
            printf("failed, return [-0x%x]\n", -ret);
            ret = ERR_SSL_CON;
            goto SSL_CON_FAIL;
        }
        printf("OK\n");
        update_http_ctx_ssl_info(index, ctx, ssl);
    }

    printf("http_tcpclient_create ok(%d)\n", index);

    return index;
failed5:
failed4:
    SSL_shutdown(ssl);
SSL_CON_FAIL:
    SSL_free(ssl);
NEW_SSL_FAIL:
    SSL_CTX_free(ctx);
NEW_CTX_FAIL:
    update_http_ctx_tcp_info(index, target_ip.addr, port, -1);
BIND_FAIL:
    close(socket_fd);
    
    return ret;
}

static void http_tcpclient_close(int socket){
    close(socket);
}

static int http_parse_url(const char *url,char *host,char *file,int *port)
{
    char *ptr1,*ptr2;
    int len = 0;
    int secure_flag = 0;
    if(!url || !host || !file || !port){
        return -1;
    }

    ptr1 = (char *)url;

    if(!strncmp(ptr1,"http://",strlen("http://"))){
        ptr1 += strlen("http://");
    } else if(!strncmp(ptr1,"https://",strlen("https://"))) {
        ptr1 += strlen("https://");
        secure_flag = 1;
    } else {
        return -1;
    }

    ptr2 = strchr(ptr1,'/');
    if(ptr2){
        len = strlen(ptr1) - strlen(ptr2);
        memcpy(host,ptr1,len);
        host[len] = '\0';
        if(*(ptr2 + 1)){
            memcpy(file,ptr2 + 1,strlen(ptr2) - 1 );
            file[strlen(ptr2) - 1] = '\0';
        }
    }else{
        memcpy(host,ptr1,strlen(ptr1));
        host[strlen(ptr1)] = '\0';
    }
    //get host and ip
    ptr1 = strchr(host,':');
    if(ptr1){
        *ptr1++ = '\0';
        *port = atoi(ptr1);
    }else{
        if(secure_flag)
            *port = HTTPS_DEFAULT_PORT;
        else
            *port = HTTP_DEFAULT_PORT;
    }

    return 0;
}

static int http_tcpclient_recv(int socket, HttpResponse *response) {
    int ret;   
    
    ret = recv(socket, response->recv_buf, BUFFER_SIZE-1,0);
    if(ret <= 0) {
        response->recv_len = 0;
        lwip_strerr(ret);
        return ERR_TCP_RCV;
    }

    response->recv_buf[ret] = '\0';
    response->recv_len = ret;
    
    return ret;
}

static int http_tcpclient_send(int socket,char *buff,int size){
    int sent=0,tmpres=0;

    while(sent < size){
        tmpres = send(socket,buff+sent,size-sent,0);
        if(tmpres == -1){
            lwip_strerr(tmpres);
            return ERR_TCP_SEND;
        }
        sent += tmpres;
    }
    return sent;
}

static int https_transmit(SSL *ssl, const char *requst, int len, HttpResponse *response){

    int ret;

    //printf("https_transmit ssl = %p\n", ssl);
    ret = SSL_write(ssl, requst, len);
    if (ret <= 0) {
        printf("SSL_write failed, return [-0x%x]\n", -ret);
        ret = ERR_SSL_SEND;
        goto SSL_WRITE_FAIL;
    }
    
    ret = SSL_read(ssl, response->recv_buf, BUFFER_SIZE - 1);
    if (ret <= 0) {
        ret = ERR_SSL_RCV;
        response->recv_len = 0;
        goto SSL_READ_FAIL;
    }
    
    response->recv_buf[ret] = '\0';
    response->recv_len = ret;
#if 0
    printf("SSL_read = %d\n", ret);
    do {
        ret = SSL_read(ssl, tmp_buf, 1023);
        if (ret <= 0) {
            break;
        }
        printf("%s", tmp_buf);
    } while (1);
#endif

    return 0;
SSL_WRITE_FAIL:
SSL_READ_FAIL:
  

    return ret;
}

static int http_parse_result(HttpResponse *response)
{
    char *ptmp = NULL; 
    char *lpbuf = response->recv_buf;

    //printf("http_parse_result:\n%s\n", lpbuf);
    ptmp = (char*)strstr(lpbuf, "HTTP/1.1");
    if(!ptmp){
        printf("http/1.1 not faind\n");
        return -1;
    }
    if(atoi(ptmp + 9)!=200){
        printf("result:\n%s\n",lpbuf);
        return -1;
    }

    ptmp = (char*)strstr(lpbuf,"\r\n\r\n");
    if(!ptmp){
        printf("ptmp is NULL\n");
        return -1;
    }
    ptmp += 4;
    response->recv_len -= (ptmp - lpbuf);
    if(response->recv_len > 0) {
        memmove(lpbuf, ptmp, response->recv_len);
        lpbuf[response->recv_len] = '\0';
    }
    return 0;
}

int http_post(const char *url,const char *post_str, HttpResponse *response)
{
    int ret;
    int socket_fd = -1;
    char lpbuf[BUFFER_SIZE] = {'\0'};
    char host_addr[HOST_SIZE] = {'\0'};
    char file[FILE_SIZE] = {'\0'};
    int port = 0;
    int index;
    //SSL_CTX *ctx;
    SSL *ssl;

    if(!url || !post_str){
        printf("      failed!\n");
        ret = -1;
        goto HTTP_POST_FAIL1;
    }

    if(http_parse_url(url,host_addr,file,&port)){
        printf("http_parse_url failed!\n");
        ret = -2;
        goto HTTP_POST_FAIL1;

    }
    //printf("host_addr : %s\tfile:%s\t,%d\n",host_addr,file,port);

    index = http_tcpclient_create(host_addr,port);
    if(index < 0){
        printf("http_tcpclient_create failed\n");
        ret = -3;
        goto HTTP_POST_FAIL1;
    }
     
    sprintf(lpbuf,HTTP_POST,file,host_addr,port,(int)strlen(post_str),post_str);
    //printf("plbuf = %s\n", lpbuf);

    if(port == HTTPS_DEFAULT_PORT) {
        get_http_ctx_ssl_info(index, NULL, &ssl);
        ret = https_transmit(ssl, lpbuf, strlen(lpbuf), response);
        if(ret < 0) {
            printf("https_transmit failed\n");
            ret = -4;
            goto HTTP_POST_FAIL3;
        }
    } else {
        get_http_ctx_tcp_info(index, NULL, NULL, &socket_fd);
        printf("http_post: socket_fd = %d\n", socket_fd);
        if(http_tcpclient_send(socket_fd,lpbuf,strlen(lpbuf)) < 0){
            printf("http_tcpclient_send failed..\n");
            ret = -4;
            goto HTTP_POST_FAIL2;

        }
        
        /*it's time to recv from server*/
        if(http_tcpclient_recv(socket_fd, response) <= 0){
            printf("http_tcpclient_recv failed\n");
            ret = -5;
            goto HTTP_POST_FAIL2;

        }
    }

    return http_parse_result(response);

HTTP_POST_FAIL3:
    update_http_ctx_ssl_info(index, NULL, NULL);
HTTP_POST_FAIL2:
    update_http_ctx_tcp_info(index, 0, -1, -1);
    http_tcpclient_close(socket_fd);
HTTP_POST_FAIL1:
    return ret;
}

int http_get(const char *url, HttpResponse *response)
{
    int ret = 0;
    int socket_fd = -1;
    static char lpbuf[BUFFER_SIZE] = {'\0'};
    char host_addr[HOST_SIZE] = {'\0'};
    char file[FILE_SIZE] = {'\0'};
    int port = 0;
    int index;
    //SSL_CTX *ctx;
    SSL *ssl;

    if(!url){
        printf("      failed!\n");
        ret = -1;
        goto HTTP_GET_FAIL1;
    }

    if(http_parse_url(url,host_addr,file,&port)){
        printf("http_parse_url failed!\n");
        ret = -2;
        goto HTTP_GET_FAIL1;
    }
    //printf("host_addr : %s\tfile:%s\t,%d\n",host_addr,file,port);
    index = http_tcpclient_create(host_addr,port);
    if(index < 0){
        printf("http_tcpclient_create failed\n");
        ret = -3;
        goto HTTP_GET_FAIL1;
    }
    
    sprintf(lpbuf,HTTP_GET,file,host_addr,port);
    
    if(port == HTTPS_DEFAULT_PORT) {
        get_http_ctx_ssl_info(index, NULL, &ssl);
        ret = https_transmit(ssl, lpbuf, strlen(lpbuf), response);
        if(ret < 0) {
            printf("https_transmit failed\n");
            ret = -4;
            goto HTTP_GET_FAIL3;
        }
    } else {
        get_http_ctx_tcp_info(index, NULL, NULL, &socket_fd);
        printf("http_get: socket_fd = %d\n", socket_fd);
        if(http_tcpclient_send(socket_fd,lpbuf,strlen(lpbuf)) < 0){
            printf("http_tcpclient_send failed..\n");
            ret = -5;
            goto HTTP_GET_FAIL2;

        }
        if(http_tcpclient_recv(socket_fd, response) <= 0){
            printf("http_tcpclient_recv failed\n");
            ret = -6;
            goto HTTP_GET_FAIL2;
        }
    }

    return http_parse_result(response);

HTTP_GET_FAIL3:
    update_http_ctx_ssl_info(index, NULL, NULL);
HTTP_GET_FAIL2:
    update_http_ctx_tcp_info(index, 0, -1, -1);
    http_tcpclient_close(socket_fd);
HTTP_GET_FAIL1:
    return ret;
}

