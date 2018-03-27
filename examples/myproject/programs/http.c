/*File : http.c
 *Auth : sjin
 *Date : 20141206
 *Mail : 413977243@qq.com
 */
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include <string.h>

#include "http.h"


#define HTTP_POST "POST /%s HTTP/1.1\r\nHOST: %s:%d\r\nAccept: */*\r\n"\
    "Content-Type:application/x-www-form-urlencoded\r\nContent-Length: %d\r\n\r\n%s"
#define HTTP_GET "GET /%s HTTP/1.1\r\nHOST: %s:%d\r\nAccept: */*\r\n\r\n"

static int http_tcpclient_create(const char *host, int port){
    int ret;
    struct sockaddr_in server_addr; 
    int socket_fd;
    ip_addr_t target_ip;

    ret = netconn_gethostbyname(host, &target_ip);
    if(ret)
       return ret;

   server_addr.sin_family = AF_INET;
   server_addr.sin_port = htons(port);
   server_addr.sin_addr.s_addr = target_ip.addr;

    if((socket_fd = socket(AF_INET,SOCK_STREAM,0))==-1){
        return -1;
    }

    if(connect(socket_fd, (struct sockaddr *)&server_addr,sizeof(struct sockaddr)) == -1){
        return -1;
    }

    return socket_fd;
}

static void http_tcpclient_close(int socket){
    close(socket);
}

static int http_parse_url(const char *url,char *host,char *file,int *port)
{
    char *ptr1,*ptr2;
    int len = 0;
    if(!url || !host || !file || !port){
        return -1;
    }

    ptr1 = (char *)url;

    if(!strncmp(ptr1,"http://",strlen("http://"))){
        ptr1 += strlen("http://");
    }else{
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
        *port = MY_HTTP_DEFAULT_PORT;
    }

    return 0;
}

static int http_tcpclient_recv(int socket, HttpResponse *response) {
    memset(response->recv_buf, 0, BUFFER_SIZE);
    response->recv_len= recv(socket, response->recv_buf, BUFFER_SIZE,0);
    return response->recv_len;
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
    printf("http_parse_result(%d): %s\n", response->recv_len, response->recv_buf);
    response->recv_len -= (ptmp - lpbuf);
    if(response->recv_len > 0) {
        memmove(lpbuf, ptmp, response->recv_len);
        printf("http_parse_result(%d): %s\n", response->recv_len, response->recv_buf);
    }
    printf("http_parse_result ok!\n");
    return 0;
}

int http_post(const char *url,const char *post_str, HttpResponse *response)
{
    int socket_fd = -1;
    char lpbuf[BUFFER_SIZE] = {'\0'};
    char host_addr[HOST_SIZE] = {'\0'};
    char file[FILE_SIZE] = {'\0'};
    int port = 0;

    if(!url || !post_str){
        printf("      failed!\n");
        return -1;
    }

    if(http_parse_url(url,host_addr,file,&port)){
        printf("http_parse_url failed!\n");
        return -2;
    }
    //printf("host_addr : %s\tfile:%s\t,%d\n",host_addr,file,port);

    socket_fd = http_tcpclient_create(host_addr,port);
    if(socket_fd < 0){
        printf("http_tcpclient_create failed\n");
        return -3;
    }
     
    sprintf(lpbuf,HTTP_POST,file,host_addr,port,(int)strlen(post_str),post_str);

    if(http_tcpclient_send(socket_fd,lpbuf,strlen(lpbuf)) < 0){
        printf("http_tcpclient_send failed..\n");
        return -4;
    }
	//printf("发送请求:\n%s\n",lpbuf);

    /*it's time to recv from server*/
    if(http_tcpclient_recv(socket_fd, response) <= 0){
        printf("http_tcpclient_recv failed\n");
        return -5;
    }

    http_tcpclient_close(socket_fd);

    return http_parse_result(response);
}

int http_get(const char *url, HttpResponse *response)
{
    int socket_fd = -1;
    static char lpbuf[BUFFER_SIZE] = {'\0'};
    char host_addr[HOST_SIZE] = {'\0'};
    char file[FILE_SIZE] = {'\0'};
    int port = 0;

    if(!url){
        printf("      failed!\n");
        return -1;
    }

    if(http_parse_url(url,host_addr,file,&port)){
        printf("http_parse_url failed!\n");
        return -2;
    }
    //printf("host_addr : %s\tfile:%s\t,%d\n",host_addr,file,port);

    socket_fd =  http_tcpclient_create(host_addr,port);
    if(socket_fd < 0){
        printf("http_tcpclient_create failed\n");
        return -3;
    }

    sprintf(lpbuf,HTTP_GET,file,host_addr,port);

    if(http_tcpclient_send(socket_fd,lpbuf,strlen(lpbuf)) < 0){
        printf("http_tcpclient_send failed..\n");
        return -4;
    }
    if(http_tcpclient_recv(socket_fd, response) <= 0){
        printf("http_tcpclient_recv failed\n");
        return -5;
    }
    http_tcpclient_close(socket_fd);

    return http_parse_result(response);
}
