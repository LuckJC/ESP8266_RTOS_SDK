/*
 * ESPRSSIF MIT License
 *
 * Copyright (c) 2015 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS ESP8266 only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "espressif/espconn.h"
#include "espressif/airkiss.h"
#include "lwip/apps/sntp.h"
#include "lwip/apps/time.h"
#include "lwip/apps/sntp_time.h"
#include "uart.h"
#include "gpio.h"
#include "lg_tty.h"

#define server_ip "192.168.101.142"
#define server_port 9669


#define DEVICE_TYPE 		"gh_9e2cff3dfa51" //wechat public number
#define DEVICE_ID 			"122475" //model ID

#define DEFAULT_LAN_PORT 	12476

enum {
    UART_EVENT_RX_CHAR,
    UART_EVENT_MAX
};

typedef struct _os_event_ {
    uint32 event;
    uint8 buf[128];
    uint32 len;
} os_event_t;

LOCAL uint8 long_press_flag;
LOCAL esp_udp ssdp_udp;
LOCAL struct espconn pssdpudpconn;
LOCAL os_timer_t ssdp_time_serv;
//LOCAL os_timer_t keypress_timer;
LOCAL uint8 sta_mac[6];
LOCAL uint8 channel;
xTaskHandle key_task_handle;
xTaskHandle led_task_handle;
xQueueHandle xQueueFrame;

uint8  lan_buf[200];
uint16 lan_buf_len;
uint8  udp_sent_cnt = 0;
uint8 net_state = 0;

#define ESP_SYSTEM_START	0	/* blink 1 cycle: 500ms + 500ms*/
#define ESP_SMART_CONFIG	1	/* blink : 200ms + 500ms*/
#define ESP_CONNECT			2	/* on */
#define ESP_DISCONNECT		3	/* off */
#define ESP_NET_ERROR		4	/* blink : 400ms + 1000ms */

extern xTaskHandle xUartTaskHandle;
extern xQueueHandle xQueueUart;

const airkiss_config_t akconf =
{
	(airkiss_memset_fn)&memset,
	(airkiss_memcpy_fn)&memcpy,
	(airkiss_memcmp_fn)&memcmp,
	0,
};

void change_net_state(int state);

LOCAL void ICACHE_FLASH_ATTR
airkiss_wifilan_time_callback(void)
{
	uint16 i;
	airkiss_lan_ret_t ret;
	
	if ((udp_sent_cnt++) >30) {
		udp_sent_cnt = 0;
		os_timer_disarm(&ssdp_time_serv);//s
		//return;
	}

	ssdp_udp.remote_port = DEFAULT_LAN_PORT;
	ssdp_udp.remote_ip[0] = 255;
	ssdp_udp.remote_ip[1] = 255;
	ssdp_udp.remote_ip[2] = 255;
	ssdp_udp.remote_ip[3] = 255;
	lan_buf_len = sizeof(lan_buf);
	ret = airkiss_lan_pack(AIRKISS_LAN_SSDP_NOTIFY_CMD,
		DEVICE_TYPE, DEVICE_ID, 0, 0, lan_buf, &lan_buf_len, &akconf);
	if (ret != AIRKISS_LAN_PAKE_READY) {
		os_printf("Pack lan packet error!");
		return;
	}
	
	ret = espconn_sendto(&pssdpudpconn, lan_buf, lan_buf_len);
	if (ret != 0) {
		os_printf("UDP send error!");
	}
	os_printf("Finish send notify!\n");
}

LOCAL void ICACHE_FLASH_ATTR
airkiss_wifilan_recv_callbk(void *arg, char *pdata, unsigned short len)
{
	uint16 i;
	remot_info* pcon_info = NULL;
		
	airkiss_lan_ret_t ret = airkiss_lan_recv(pdata, len, &akconf);
	airkiss_lan_ret_t packret;
	
	switch (ret){
	case AIRKISS_LAN_SSDP_REQ:
		espconn_get_connection_info(&pssdpudpconn, &pcon_info, 0);
		os_printf("remote ip: %d.%d.%d.%d \r\n",pcon_info->remote_ip[0],pcon_info->remote_ip[1],
			                                    pcon_info->remote_ip[2],pcon_info->remote_ip[3]);
		os_printf("remote port: %d \r\n",pcon_info->remote_port);
      
        pssdpudpconn.proto.udp->remote_port = pcon_info->remote_port;
		memcpy(pssdpudpconn.proto.udp->remote_ip,pcon_info->remote_ip,4);
		ssdp_udp.remote_port = DEFAULT_LAN_PORT;
		
		lan_buf_len = sizeof(lan_buf);
		packret = airkiss_lan_pack(AIRKISS_LAN_SSDP_RESP_CMD,
			DEVICE_TYPE, DEVICE_ID, 0, 0, lan_buf, &lan_buf_len, &akconf);
		
		if (packret != AIRKISS_LAN_PAKE_READY) {
			os_printf("Pack lan packet error!");
			return;
		}

		os_printf("\r\n\r\n");
		for (i=0; i<lan_buf_len; i++)
			os_printf("%c",lan_buf[i]);
		os_printf("\r\n\r\n");
		
		packret = espconn_sendto(&pssdpudpconn, lan_buf, lan_buf_len);
		if (packret != 0) {
			os_printf("LAN UDP Send err!");
		}
		
		break;
	default:
		os_printf("Pack is not ssdq req!%d\r\n",ret);
		break;
	}
}

void ICACHE_FLASH_ATTR
airkiss_start_discover(void)
{
	ssdp_udp.local_port = DEFAULT_LAN_PORT;
	pssdpudpconn.type = ESPCONN_UDP;
	pssdpudpconn.proto.udp = &(ssdp_udp);
	espconn_regist_recvcb(&pssdpudpconn, airkiss_wifilan_recv_callbk);
	espconn_create(&pssdpudpconn);

	os_timer_disarm(&ssdp_time_serv);
	os_timer_setfn(&ssdp_time_serv, (os_timer_func_t *)airkiss_wifilan_time_callback, NULL);
	os_timer_arm(&ssdp_time_serv, 1000, 1);//1s
}


void ICACHE_FLASH_ATTR
smartconfig_done(sc_status status, void *pdata)
{
    switch(status) {
        case SC_STATUS_WAIT:
            printf("SC_STATUS_WAIT\n");
            break;
        case SC_STATUS_FIND_CHANNEL:
            printf("SC_STATUS_FIND_CHANNEL\n");
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
            printf("SC_STATUS_GETTING_SSID_PSWD\n");
            sc_type *type = pdata;
            if (*type == SC_TYPE_ESPTOUCH) {
                printf("SC_TYPE:SC_TYPE_ESPTOUCH\n");
            } else {
                printf("SC_TYPE:SC_TYPE_AIRKISS\n");
            }
            break;
        case SC_STATUS_LINK:
            printf("SC_STATUS_LINK\n");
            struct station_config *sta_conf = pdata;
	
	        wifi_station_set_config(sta_conf);
	        wifi_station_disconnect();
	        wifi_station_connect();
            break;
        case SC_STATUS_LINK_OVER:
            printf("SC_STATUS_LINK_OVER\n");
            if (pdata != NULL) {
				//SC_TYPE_ESPTOUCH
                uint8 phone_ip[4] = {0};

                memcpy(phone_ip, (uint8*)pdata, 4);
                printf("Phone ip: %d.%d.%d.%d\n",phone_ip[0],phone_ip[1],phone_ip[2],phone_ip[3]);
            } else {
            	//SC_TYPE_AIRKISS - support airkiss v2.0
				airkiss_start_discover();
			}
            smartconfig_stop();
            break;
    }
	
}

void ICACHE_FLASH_ATTR
wifi_handle_event_cb(System_Event_t	*evt)
{
    struct timeval t;
    time_t t2;
    int count = 3;
	char *date;

	printf("event %x\n", evt->event_id);

	switch (evt->event_id) {
		case EVENT_STAMODE_CONNECTED:
		printf("connect to ssid %s, channel %d\n",
			evt->event_info.connected.ssid,
			evt->event_info.connected.channel);
		break;
			
	case EVENT_STAMODE_DISCONNECTED:
		printf("disconnect from	ssid %s, reason %d\n",	
			evt->event_info.disconnected.ssid,
			evt->event_info.disconnected.reason);
        user_conn_destroy();
		change_net_state(ESP_DISCONNECT);
		break;
		
	case EVENT_STAMODE_AUTHMODE_CHANGE:
		printf("mode: %d -> %d\n",
			evt->event_info.auth_change.old_mode,
			evt->event_info.auth_change.new_mode);
		break;
		
	case EVENT_STAMODE_GOT_IP:
		printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
			IP2STR(&evt->event_info.got_ip.ip),
			IP2STR(&evt->event_info.got_ip.mask),
			IP2STR(&evt->event_info.got_ip.gw));
		printf("\n");
        #if 0
        configTime(8, 0, "cn.pool.ntp.org", "ntp1.aliyun.com", "ntp2.aliyun.com", 0);
        do {
            gettimeofday(&t, NULL);
            //time(&t2);
			date = sntp_get_real_time(t.tv_sec);//sntp_get_real_time(t2);
			printf("Date: %s\n", date);
            vTaskDelay(1000 / portTICK_RATE_MS);
        }while(count--);
        //user_conn_init();
        #endif
		lgtty_check_cmd(1, UART0, 0xA8);
		vTaskDelay(60 / portTICK_RATE_MS);
		lgtty_ID_cmd(1, UART0, 1, NULL, 0);
		vTaskDelay(60 / portTICK_RATE_MS);
		lgtty_channel_cmd(1, UART0, 1, 0);
		vTaskDelay(60 / portTICK_RATE_MS);
		change_net_state(ESP_CONNECT);
        user_conn_init();
		break;

	case EVENT_SOFTAPMODE_STACONNECTED:
		printf("station: " MACSTR "join, AID = %d\n",	
			MAC2STR(evt->event_info.sta_connected.mac),
			evt->event_info.sta_connected.aid);
		break;
		
	case EVENT_SOFTAPMODE_STADISCONNECTED:
		printf("station: " MACSTR "leave, AID = %d\n",	
			MAC2STR(evt->event_info.sta_disconnected.mac),
			evt->event_info.sta_disconnected.aid);
		break;
		
	default:
		break;
	}
}

LOCAL void gpio_intr_handler(void *arg)
{
	uint32 gpio_status;
	
	gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
	GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
	
	if((gpio_status & GPIO_Pin_5) || (gpio_status & GPIO_Pin_13))
		xTaskResumeFromISR(key_task_handle);
}

LOCAL void ICACHE_FLASH_ATTR
key_long_press_time_callback(void)
{
	long_press_flag = 1;
    user_conn_destroy();
    smartconfig_stop();
    os_timer_disarm(&ssdp_time_serv);
	change_net_state(ESP_SMART_CONFIG);
	smartconfig_start(smartconfig_done);
}


LOCAL void ICACHE_FLASH_ATTR
key_intr_task(void *pvParameters)
{
	uint32 tick_count, tick_count_cur;
	uint8 key1_state = 0, key2_state = 0;	
	os_timer_t keypress_timer;

	os_timer_setfn(&keypress_timer, (os_timer_func_t *)key_long_press_time_callback, NULL);

	while(1) {
		vTaskSuspend(NULL);

		_xt_isr_mask(1 << ETS_GPIO_INUM);
		vTaskDelay(100 / portTICK_RATE_MS);
		if(GPIO_INPUT_GET(GPIO_ID_PIN(5)) == 0) {
			if(key1_state == 0) {
				key1_state = 1;
				printf("key1 down\n");
				long_press_flag = 0;
				os_timer_disarm(&keypress_timer);				
				os_timer_arm(&keypress_timer, 3000, 0);//3s
			}
		} else {
			if(key1_state == 1) {
				key1_state = 0;
				printf("key1 up\n");
				if(!long_press_flag) {
					os_timer_disarm(&keypress_timer);
					/* send ID */
					lgtty_ID_cmd(1, UART0, 0, sta_mac, 3);
					vTaskDelay(60 / portTICK_RATE_MS);
					lgtty_channel_cmd(1, UART0, 0, channel);
					vTaskDelay(60 / portTICK_RATE_MS);
				}
			}
		}
		
		if(GPIO_INPUT_GET(GPIO_ID_PIN(13)) == 0) {
			if(key2_state == 0) {
				key2_state = 1;
				printf("key2 down\n");
				//os_timer_disarm(&keypress_timer);				
				//os_timer_arm(&keypress_timer, 3000, 0);//3s
			}
		} else {
			if(key2_state == 1) {
				key2_state = 0;
				printf("key2 up\n");
				//lgtty_check_cmd(1, UART0, 0xA8);
				//os_timer_disarm(&keypress_timer);
			}
		}
		_xt_isr_unmask(1 << ETS_GPIO_INUM);
	}

    vTaskDelete(NULL);
}

LOCAL void ICACHE_FLASH_ATTR
uart_task(void *pvParameters)
{
    int seq = 0;
    os_event_t e;
    portTickType delay = portMAX_DELAY;
    LGTTY_RECVS rcvs;
    char *ptr = rcvs.recv_buf;

    rcvs.recv_len = 0;
    xQueueFrame = xQueueCreate(16, sizeof(lora_event_t));

    for (;;) {
        if (xQueueReceive(xQueueUart, (void *)&e, delay)) {
            switch (e.event) {
                case UART_EVENT_RX_CHAR:
                    ptr = rcvs.recv_buf + rcvs.recv_len;
                    memcpy(ptr, e.buf, e.len);
                    rcvs.recv_len += e.len;
                    delay = 40/portTICK_RATE_MS;
                    break;

                default:
                    break;
            }
        } else {
            seq++;
            printf("seq: %d\n", seq);
            delay = portMAX_DELAY;
			lgtty_read(0, &rcvs, xQueueFrame);
        }
    }

    vTaskDelete(NULL);
}

void ICACHE_FLASH_ATTR
smartconfig_task(void *pvParameters)
{
    smartconfig_start(smartconfig_done);
    
    vTaskDelete(NULL);
}

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/
uint32 user_rf_cal_sector_set(void)
{
    flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;
        case FLASH_SIZE_64M_MAP_1024_1024:
            rf_cal_sec = 2048 - 5;
            break;
        case FLASH_SIZE_128M_MAP_1024_1024:
            rf_cal_sec = 4096 - 5;
            break;
        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}

LOCAL void ICACHE_FLASH_ATTR
key_init()
{
	GPIO_ConfigTypeDef pin_config;
	pin_config.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_13;
	pin_config.GPIO_Mode = GPIO_Mode_Input;
	pin_config.GPIO_Pullup = GPIO_PullUp_DIS;
	pin_config.GPIO_IntrType = GPIO_PIN_INTR_ANYEDGE;
	gpio_config(&pin_config);

	_xt_isr_unmask(1 << ETS_GPIO_INUM);

	//gpio_pin_wakeup_enable(GPIO_Pin_0, GPIO_PIN_INTR_ANYEDGE);
	gpio_intr_handler_register(gpio_intr_handler, NULL);
}

LOCAL void ICACHE_FLASH_ATTR
led_init()
{
	GPIO_ConfigTypeDef pin_config;
	pin_config.GPIO_Pin = GPIO_Pin_14;
	pin_config.GPIO_Mode = GPIO_Mode_Output;
	pin_config.GPIO_Pullup = GPIO_PullUp_DIS;
	gpio_config(&pin_config);
}

LOCAL inline void ICACHE_FLASH_ATTR
set_led_on()
{
	GPIO_OUTPUT_SET(GPIO_ID_PIN(14), 0);
}

LOCAL inline void ICACHE_FLASH_ATTR
set_led_off()
{
	GPIO_OUTPUT_SET(GPIO_ID_PIN(14), 1);
}

LOCAL void ICACHE_FLASH_ATTR
buzz_init()
{
	GPIO_ConfigTypeDef pin_config;
	pin_config.GPIO_Pin = GPIO_Pin_12;
	pin_config.GPIO_Mode = GPIO_Mode_Output;
	pin_config.GPIO_Pullup = GPIO_PullUp_DIS;
	gpio_config(&pin_config);
}

LOCAL inline void ICACHE_FLASH_ATTR
set_buzz_on()
{
	GPIO_OUTPUT_SET(GPIO_ID_PIN(12), 1);
}

LOCAL inline void ICACHE_FLASH_ATTR
set_buzz_off()
{
	GPIO_OUTPUT_SET(GPIO_ID_PIN(12), 0);
}

void change_net_state(int state)
{
	net_state = state;
}

LOCAL void ICACHE_FLASH_ATTR
led_task(void *pvParameters)
{
	led_init();
	buzz_init();
	for(;;) {
		switch (net_state) {
			case ESP_SYSTEM_START:
				set_led_on();
				set_buzz_on();
				vTaskDelay(1000 / portTICK_RATE_MS);
				set_led_off();
				set_buzz_off();
				vTaskDelay(1000 / portTICK_RATE_MS);
				net_state = 3;
				break;
			case ESP_SMART_CONFIG:
				set_led_on();
				vTaskDelay(200 / portTICK_RATE_MS);
				set_led_off();
				vTaskDelay(500 / portTICK_RATE_MS);
				break;
			case ESP_CONNECT:
				set_led_on();
				vTaskDelay(2000 / portTICK_RATE_MS);
				break;
			case ESP_DISCONNECT:
				set_led_off();
				vTaskDelay(2000 / portTICK_RATE_MS);
				break;
			case ESP_NET_ERROR:
				set_led_on();
				vTaskDelay(400 / portTICK_RATE_MS);
				set_led_off();
				vTaskDelay(1000 / portTICK_RATE_MS);
				break;
		}
	}
	vTaskDelete(NULL);
}

/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_init(void)
{
	//UART_SetBaudrate(0, BIT_RATE_921600);
	uart_init_new();
    printf("SDK version:%s\n", system_get_sdk_version());

    lgtty_work_mode(UART0);

	wifi_get_macaddr(STATION_IF, sta_mac);

	key_init();

	/* need to set opmode before you set config */
    wifi_set_opmode(STATION_MODE);
	wifi_set_event_handler_cb(wifi_handle_event_cb);

	xTaskCreate(key_intr_task, "key_intr_task", 128, NULL, 1, &key_task_handle);
	xTaskCreate(led_task, "led_task", 128, NULL, tskIDLE_PRIORITY + 3, &led_task_handle);
	xTaskCreate(uart_task, (uint8 const *)"uTask", 512, NULL, tskIDLE_PRIORITY + 2, &xUartTaskHandle);
}

