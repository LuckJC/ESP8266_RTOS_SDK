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

#define HTTP_POST "POST /%s HTTP/1.1\r\nHOST: %s:%d\r\nAccept: */*\r\nConnection: Close\r\n"\
    "Content-Type:application/x-www-form-urlencoded\r\nContent-Length: %d\r\n\r\n%s"
#define HTTP_GET "GET /%s HTTP/1.1\r\nHOST: %s:%d\r\nAccept: */*\r\nConnection: Close\r\n\r\n"

static int http_tcpclient_create(const char *host, int port){
    int ret;
    struct sockaddr_in sock_addr; 
    int socket_fd;
    ip_addr_t target_ip;

    ret = netconn_gethostbyname(host, &target_ip);
    if(ret)
       return ret;

    if((socket_fd = socket(AF_INET,SOCK_STREAM,0))==-1){
        return -1;
    }

    if(port == HTTPS_DEFAULT_PORT) {
        memset(&sock_addr, 0, sizeof(sock_addr));
        sock_addr.sin_family = AF_INET;
        sock_addr.sin_addr.s_addr = 0;
        sock_addr.sin_port = htons(OPENSSL_LOCAL_TCP_PORT);
        ret = bind(socket_fd, (struct sockaddr*)&sock_addr, sizeof(sock_addr));
        if (ret) {
            printf("failed\n");
            ret = -2;
            goto BIND_FAIL;
        }
    }

    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(port);
    sock_addr.sin_addr.s_addr = target_ip.addr;

    if(connect(socket_fd, (struct sockaddr *)&sock_addr,sizeof(struct sockaddr)) == -1){
        ret = -3;
        goto BIND_FAIL;
    }

    return socket_fd;

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
        return ret;
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
            return -1;
        }
        sent += tmpres;
    }
    return sent;
}

static int https_transmit(int socket_fd, const char *requst, int len, HttpResponse *response){

    int ret;
    SSL_CTX *ctx;
    SSL *ssl;
    int recv_bytes = 0;

	printf("create SSL context ......");
    ctx = SSL_CTX_new(TLSv1_1_client_method());
    if (!ctx) {
        printf("failed\n");
        ret = -1;
        goto failed1;
    }
    printf("OK\n");

    printf("set SSL context read buffer size ......");
    SSL_CTX_set_default_read_buffer_len(ctx, OPENSSL_FRAGMENT_SIZE);
    printf("OK\n");

    printf("create SSL ......");
    ssl = SSL_new(ctx);
    if (!ssl) {
        printf("failed\n");
        ret = -2;
        goto failed2;
    }
    printf("OK\n");

    SSL_set_fd(ssl, socket_fd);

    ret = SSL_connect(ssl);
    if (!ret) {
        printf("failed, return [-0x%x]\n", -ret);
        ret = -3;
        goto failed3;
    }

    ret = SSL_write(ssl, requst, len);
    if (ret <= 0) {
        printf("failed, return [-0x%x]\n", -ret);
        ret = -5;
        goto failed4;
    }
    
    ret = SSL_read(ssl, response->recv_buf, BUFFER_SIZE - 1);
    if (ret <= 0) {
        ret = -6;
        response->recv_len = 0;
        goto failed5;
    }
    
    response->recv_buf[ret] = '\0';
    response->recv_len = ret;
failed5:
failed4:
    SSL_shutdown(ssl);
failed3:
    SSL_free(ssl);
failed2:
    SSL_CTX_free(ctx);
failed1:   

    return ret;
}

static int http_parse_result(HttpResponse *response)
{
    char *ptmp = NULL; 
    char *lpbuf = response->recv_buf;
    
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

    if(!url || !post_str){
        printf("      failed!\n");
        ret = -1;
        goto HTTP_POST_FAIL;
    }

    if(http_parse_url(url,host_addr,file,&port)){
        printf("http_parse_url failed!\n");
        ret = -2;
        goto HTTP_POST_FAIL;

    }
    //printf("host_addr : %s\tfile:%s\t,%d\n",host_addr,file,port);

    socket_fd = http_tcpclient_create(host_addr,port);
    if(socket_fd < 0){
        printf("http_tcpclient_create failed\n");
        ret = -3;
        goto HTTP_POST_FAIL;

    }
     
    sprintf(lpbuf,HTTP_POST,file,host_addr,port,(int)strlen(post_str),post_str);

    if(port == HTTPS_DEFAULT_PORT) {
        if(https_transmit(socket_fd, lpbuf, strlen(lpbuf), response) < 0) {
            printf("https_transmit failed\n");
            ret = -4;
            goto HTTP_POST_FAIL2;
        }
    } else {
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

    ret = http_parse_result(response);

HTTP_POST_FAIL2:
    http_tcpclient_close(socket_fd);
HTTP_POST_FAIL:
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
    socket_fd =  http_tcpclient_create(host_addr,port);
    if(socket_fd < 0){
        printf("http_tcpclient_create failed\n");
        ret = -3;
        goto HTTP_GET_FAIL1;
    }
    
    sprintf(lpbuf,HTTP_GET,file,host_addr,port);
    
    if(port == HTTPS_DEFAULT_PORT) {
        if(https_transmit(socket_fd, lpbuf, strlen(lpbuf), response) < 0) {
            printf("https_transmit failed\n");
            ret = -4;
            goto HTTP_GET_FAIL2;
        }
    } else {
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
    
   ret = http_parse_result(response);

HTTP_GET_FAIL2:
    http_tcpclient_close(socket_fd);
HTTP_GET_FAIL1:
    return ret;
}

