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
#define HOST_SIZE           64
#define FILE_SIZE           640

#define ERR_OK               0    /* No error, everything OK. */
#define ERR_TCP_SEND        -1    /* Out of memory error.     */
#define ERR_TCP_RCV         -2    /* Buffer error.            */
#define ERR_CTX             -3    /* Operation in progress    */
#define ERR_SSL_NEW         -4    /* Illegal value.           */
#define ERR_SSL_CON         -5    /* Timeout.                 */
#define ERR_SSL_SEND        -6    /* Timeout.                 */
#define ERR_SSL_RCV         -7    /* Routing problem.         */

typedef struct {
    int recv_len;
    char recv_buf[BUFFER_SIZE];
} HttpResponse;

int http_get(const char *url, HttpResponse *response);
int http_post(const char *url,const char * post_str, HttpResponse *response);

#endif
