/*File : http.h
 *Auth : sjin
 *Date : 20141206
 *Mail : 413977243@qq.com
 */
#ifndef _MY_HTTP_H
#define _MY_HTTP_H

#define HTTP_DEFAULT_PORT   80
#define HTTPS_DEFAULT_PORT  443

#define BUFFER_SIZE         1024
#define HOST_SIZE           48
#define FILE_SIZE           28

typedef struct {
    int recv_len;
    char recv_buf[BUFFER_SIZE];
} HttpResponse;

int http_get(const char *url, HttpResponse *response);
int http_post(const char *url,const char * post_str, HttpResponse *response);

#endif