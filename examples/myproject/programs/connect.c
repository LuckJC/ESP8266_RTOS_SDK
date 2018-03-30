#include <stddef.h>
#include "openssl/ssl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "espressif/c_types.h"
#include "lwip/sockets.h"
#include "lwip/apps/sntp.h"
#include "lwip/apps/time.h"
#include "lwip/apps/sntp_time.h"
#include "esp_common.h"
#include "http.h"

#define TEST_CONNECT_OK  "http://ui.iot.skadiseye.wang/chip/api/test/linkwified?gateway_sn=%02X%02X%02X%02X%02X%02X"
#define TEST_BAIDU          "https://www.baidu.com"

LOCAL xTaskHandle connect_handle;

HttpResponse http_response;

LOCAL void connect_thread(void *p)
{
    int ret;
    char rst_buf[256];
    uint8 sta_mac[6];
    char req_buf[48];
    //char *date;
    //struct timeval t;

    do {
        wifi_get_macaddr(STATION_IF, sta_mac);
        //ret = gettimeofday(&t, NULL);
        //date = sntp_get_real_time(t.tv_sec);
        //printf("Date: %s\n", date);
        sprintf(req_buf, TEST_CONNECT_OK, 
            sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);
        printf(req_buf);
        printf("\n");
        ret = http_get(req_buf, &http_response);
        printf("......http test ok\n");
        if (ret < 0) {
            //goto CONNECT_FAIL1;
            printf("......http test failed(%d)\n", ret);
            continue;
        }
        vTaskDelay(2000 / portTICK_RATE_MS);
        ret = http_get(TEST_BAIDU, &http_response);
        if (ret < 0) {
            //goto CONNECT_FAIL1;
            printf("......https test failed(%d)\n", ret);
            continue;
        }
        if(http_response.recv_len) {
            printf("%s\n", http_response.recv_buf);
        }
        printf("......https test ok\n");
        vTaskDelay(2000 / portTICK_RATE_MS);
    } while(1);

CONNECT_FAIL1:
    os_printf("task exit\n");
    vTaskDelete(NULL);

    return ;
}

void user_conn_init(void)
{
    int ret;

    ret = xTaskCreate(connect_thread,
                      "connect",
                      2048,
                      NULL,
                      6,
                      &connect_handle);
    if (ret != pdPASS)  {
        os_printf("create thread connect failed\n");
        return ;
    }
}

