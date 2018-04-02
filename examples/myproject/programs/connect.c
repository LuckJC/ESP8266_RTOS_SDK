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
#include "lg_tty.h"


#define TEST_BAIDU       "https://www.baidu.com"
#define TEST_WEIXIN      "https://api.weixin.qq.com"


#define TEST_REPORT_CONNECT_OK          "http://ui.iot.skadiseye.wang/chip/api/test/linkwified?gateway_sn=%02X%02X%02X%02X%02X%02X"
#define TEST_GET_USER_INFO              "http://ui.iot.skadiseye.wang/chip/api/test/gwdatas"
#define TEST_GET_AUTHTOKEN              "http://ui.iot.skadiseye.wang/aY7iBsxI2aOOk3BI"

#define WEIXIN_PUSH_MSG                 "https://api.weixin.qq.com/cgi-bin/message/template/send?access_token=%s"
#define WEIXIN_POST_BODY                "{\"data\":{\"device\":{\"color\":\"#173177\",\"value\":\"安防设备\"},\"first\":{\"color\":\"#173177\",\"value\":\"报警\"},\"remark\":{\"color\":\"#140101\",\"value\":\"主机 【三栋211】的安防探测器 报警\"},\"time\":{\"color\":\"#173177\",\"value\":\"2017-12-30 13:24:13\"}},\"template_id\":\"VQMra5Ii9eno5MueQg8557IPoySa4ZbzxN-E3pH5a8I\",\"topcolor\":\"#173177\",\"touser\":\"oBFWE1RXdmhkQ047x1Ct8JxTKtT0\",\"url\":\"\"}"


LOCAL xTaskHandle connect_handle;
LOCAL int do_con_exit;
HttpResponse http_response;
char AUTHTOKEN[512];

extern xQueueHandle xQueueFrame;

LOCAL void connect_thread(void *p)
{
    int ret;
    char rst_buf[256];
    uint8 sta_mac[6];
    char req_buf[640];
	lora_event_t e;
	
	wifi_get_macaddr(STATION_IF, sta_mac);
    sprintf(req_buf, TEST_REPORT_CONNECT_OK, sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);
    //do {
        ret = http_get(req_buf, &http_response);
    //} while(!ret && !do_con_exit);
	
	http_get(TEST_GET_AUTHTOKEN, &http_response);
	if(http_response.recv_len) {
        memcpy(AUTHTOKEN, http_response.recv_buf, http_response.recv_len);
        AUTHTOKEN[http_response.recv_len] = '\0';
        printf("%s\n", AUTHTOKEN);
    }

    sprintf(req_buf, WEIXIN_PUSH_MSG, AUTHTOKEN);
    printf(req_buf);
    printf("\n");
    http_post(req_buf, WEIXIN_POST_BODY, &http_response);

    for(;;) {
        http_post(req_buf, WEIXIN_POST_BODY, &http_response);
        vTaskDelay(2000 / portTICK_RATE_MS);
    }

#if 0
    for (;;) {
        if (xQueueReceive(xQueueFrame, (void *)&e, portMAX_DELAY)) {
            switch (e.event) {
                case LORA_EVENT_DATA_FRAME:
                    //dev_frame_process(e.buf, e.len);
                    break;

                default:
                    break;
            }
        }
    }
#endif

    do {
        sprintf(req_buf, TEST_REPORT_CONNECT_OK, 
            sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);
        ret = http_get(req_buf, &http_response);
        printf("......http test ok\n");
        if (ret < 0) {
            //goto CONNECT_FAIL1;
            printf("......http test failed(%d)\n", ret);
            continue;
        }
        vTaskDelay(2000 / portTICK_RATE_MS);
        ret = http_get(TEST_WEIXIN, &http_response);
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
    } while(!do_con_exit);

CONNECT_FAIL1:
    printf("task exit\n");
    vTaskDelete(NULL);

    return ;
}

void user_conn_init(void)
{
    int ret;

    do_con_exit = 0;

    ret = xTaskCreate(connect_thread,
                      "connect",
                      4096,
                      NULL,
                      6,
                      &connect_handle);
    if (ret != pdPASS)  {
        printf("create thread connect failed\n");
        return ;
    }
}

void user_conn_destroy(void)
{
    do_con_exit = 1;  
}


