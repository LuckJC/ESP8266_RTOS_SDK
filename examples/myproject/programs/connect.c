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

#define USER_ID_SIZE    28

#define TEST_BAIDU       "https://www.baidu.com/?tn=baiduhome_pg"
#define TEST_WEIXIN      "https://api.weixin.qq.com"


#define TEST_REPORT_CONNECT_OK          "http://ui.iot.skadiseye.wang/chip/api/test/linkwified?gateway_sn=%02X%02X%02X%02X%02X%02X"
#define TEST_GET_USER_INFO              "http://ui.iot.skadiseye.wang/chip/api/test/gwdatas?gateway_sn=%02X%02X%02X%02X%02X%02X"
#define TEST_GET_AUTHTOKEN              "http://ui.iot.skadiseye.wang/aY7iBsxI2aOOk3BI"

#define WEIXIN_PUSH_MSG                 "https://api.weixin.qq.com/cgi-bin/message/template/send?access_token="
#define WEIXIN_POST_BODY                "{\"data\":{\"device\":{\"color\":\"#173177\",\"value\":\"安防设备\"},\"first\":{\"color\":\"#173177\",\"value\":\"报警\"},\"remark\":{\"color\":\"#140101\",\"value\":\"主机 【三栋211】的安防探测器 报警\"},\"time\":{\"color\":\"#173177\",\"value\":\"2017-12-30 13:24:13\"}},\"template_id\":\"VQMra5Ii9eno5MueQg8557IPoySa4ZbzxN-E3pH5a8I\",\"topcolor\":\"#173177\",\"touser\":\"%s\",\"url\":\"\"}"


LOCAL xTaskHandle connect_handle;
LOCAL int do_con_exit;
LOCAL HttpResponse http_response;
LOCAL char req_buf[80 + 512];
LOCAL char body_buf[512];
//LOCAL char AUTHTOKEN[512];
LOCAL char USERINFO[USER_ID_SIZE + 1];
LOCAL uint8 sta_mac[6];

#if 1
LOCAL void ICACHE_FLASH_ATTR
push_weixin_msg()
{
	char *ptmp;
	int ret;
	char *AUTHTOKEN = req_buf + strlen(WEIXIN_PUSH_MSG);

	do {
		/* get user info */
		sprintf(req_buf, TEST_GET_USER_INFO, sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);
		ret = http_get(req_buf, &http_response);
		if(ret < 0) {
			vTaskDelay(2000 / portTICK_RATE_MS);
			continue;
		}
		if(http_response.recv_len) {
			ptmp = http_response.recv_buf;
			ptmp = (char*)strstr(ptmp,"\"openid\":");
			ptmp += 10;
			memcpy(USERINFO, ptmp, USER_ID_SIZE);
			USERINFO[USER_ID_SIZE] = '\0';
			printf("user id: %s\n", USERINFO);
		}
		
        /* get AUTHTOKEN */
        strcpy(req_buf, WEIXIN_PUSH_MSG);
    	ret = http_get(TEST_GET_AUTHTOKEN, &http_response);
        if(ret < 0) {
            vTaskDelay(2000 / portTICK_RATE_MS);
            continue;
        }
        if(http_response.recv_len) {
            memcpy(AUTHTOKEN, http_response.recv_buf, http_response.recv_len);
            AUTHTOKEN[http_response.recv_len] = '\0';
        }
        
        /* push weixin msg */
        sprintf(body_buf, WEIXIN_POST_BODY, USERINFO);
        http_post(req_buf, body_buf, &http_response);	
		break;
	}while(1);
}
#endif

extern xQueueHandle xQueueFrame;

LOCAL void ICACHE_FLASH_ATTR 
connect_thread(void *p)
{
    int ret;
	char *ptmp;
	lora_event_t e;
    
    char *AUTHTOKEN = req_buf + strlen(WEIXIN_PUSH_MSG);

	wifi_get_macaddr(STATION_IF, sta_mac);

    do {
        /* connect mark */
        sprintf(req_buf, TEST_REPORT_CONNECT_OK, sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);
        ret = http_get(req_buf, &http_response);
        if(ret < 0) {
            vTaskDelay(2000 / portTICK_RATE_MS);
            continue;
        }

        /* get user info */
        sprintf(req_buf, TEST_GET_USER_INFO, sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);
    	ret = http_get(req_buf, &http_response);
        if(ret < 0) {
            vTaskDelay(2000 / portTICK_RATE_MS);
            continue;
        }
    	if(http_response.recv_len) {
            ptmp = http_response.recv_buf;
            ptmp = (char*)strstr(ptmp,"\"openid\":");
            ptmp += 10;
            memcpy(USERINFO, ptmp, USER_ID_SIZE);
            USERINFO[USER_ID_SIZE] = '\0';
            printf("user id: %s\n", USERINFO);
        }
        
        break;
    } while(!do_con_exit);

	do {
		if (xQueueReceive(xQueueFrame, (void *)&e, (portTickType)portMAX_DELAY)) {
			switch (e.event) {
				case LORA_EVENT_DATA_FRAME:
					printf("recevie sensor data.\n");					
					push_weixin_msg();
					break;
		
				default:
					break;
			}
		}
	}while(!do_con_exit);

CONNECT_FAIL1:
    printf("task exit\n");
    vTaskDelete(NULL);

    return ;
}

void ICACHE_FLASH_ATTR
user_conn_init(void)
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


