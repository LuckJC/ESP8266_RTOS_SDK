#include "esp_common.h"
#include "lg_tty.h"

#include<stdio.h>    
#include<string.h>
#include "uart.h"

TTY_MOD_STATE lgtty_state = ESTAT_INIT;
time_t startup_time = 0;
int do_exit = 0;

int lgtty_write(int fd, const void *buffer, uint8 len)
{
	int i;
	uint8 *buf = (uint8 *)buffer;
	
	for(i = 0; i < len; i++) {
		uart_tx_one_char(UART1, buf[i]);
	}

    return len;
}

static int check_recv(const char *buffer, int msglen);
static int modver_recv(const char *buffer, int msglen);
static int test_recv(const char *buffer, int msglen);
static int ID_recv(const char *buffer, int msglen);
static int channel_recv(const char *buffer, int msglen);
static int IO_recv(const char *buffer, int msglen);
static int check_timeout(void);

STATE_PROC state_proc_s[ESTAT_OVER+1] = {
	[ESTAT_INIT] = {NULL, NULL},
	[ESTAT_MATCH_RATE] = {NULL, NULL}, 
	[ESTAT_MATCH_WORK] = {NULL, NULL}, 
	[ESTAT_MATCH_TEST] = {test_recv, check_timeout}, 
	[ESTAT_MATCH_CHECK] = {check_recv, check_timeout}, 
	[ESTAT_MATCH_MODVER] = {modver_recv, check_timeout}, 
	[ESTAT_MATCH_ID] = {ID_recv, check_timeout}, 
	[ESTAT_MATCH_CHANNEL] = {channel_recv, check_timeout}, 
	[ESTAT_MATCH_IO] = {IO_recv, check_timeout}, 
	[ESTAT_OVER] = {NULL, NULL}
};

static void lgtty_checksum(const unsigned char *buffer, int blen, unsigned char *output);


static int check_recv(const char *buffer, int msglen)
{
	unsigned char code = buffer[3];
	if (code != CMD_CODE_A0) {
		ULOG("recv ttyfd code=0X%0X, is not 0XA0\n", code&0xff);
		return 0;
	}

	if (msglen <= 6) {
		ULOG("recv ttyfd code=0xA0, no data!\n");
		return 0;
	}
	
	ULOG("recv ttyfd code=0xA0, return data1 = 0x%0x\n", buffer[5]&0xff);
	do_exit = 1;
	return 0;
}

static int modver_recv(const char *buffer, int msglen)
{
	unsigned char code = buffer[3];
	if (code != CMD_CODE_A1) {
		ULOG("recv ttyfd code=0X%0X, is not 0XA1\n", code&0xff);
		return 0;
	}

	ULOG("recv ttyfd A1 msglen = %d\n", msglen);
	DATA_DUMP("logtty recv", (const unsigned char *)buffer, msglen);
	do_exit = 1;
	return 0;
}

static int test_recv(const char *buffer, int msglen)
{
	unsigned char code = buffer[3];
	if (code != CMD_CODE_A2) {
		ULOG("recv ttyfd code=0X%0X, is not 0XA2\n", code&0xff);
		return 0;
	}

	ULOG("recv ttyfd A2 msglen = %d\n", msglen);
	DATA_DUMP("logtty recv", (const unsigned char *)buffer, msglen);
	do_exit = 1;
	return 0;
}

static int ID_recv(const char *buffer, int msglen)
{
	unsigned char code = buffer[3];
	if (code != CMD_CODE_A3) {
		ULOG("recv ttyfd code=0X%0X, is not 0XA3\n", code&0xff);
		return 0;
	}

	ULOG("recv ttyfd A3 msglen = %d\n", msglen);
	DATA_DUMP("logtty recv", (const unsigned char *)buffer, msglen);
	do_exit = 1;
	return 0;
}

static int channel_recv(const char *buffer, int msglen)
{
	unsigned char code = buffer[3];
	if (code != CMD_CODE_A4) {
		ULOG("recv ttyfd code=0X%0X, is not 0XA4\n", code&0xff);
		return 0;
	}

	ULOG("recv ttyfd A4 msglen = %d\n", msglen);
	DATA_DUMP("logtty recv", (const unsigned char *)buffer, msglen);
	do_exit = 1;
	return 0;
}

static int IO_recv(const char *buffer, int msglen)
{
	unsigned char code = buffer[3];
	if (code != CMD_CODE_A5) {
		ULOG("recv ttyfd code=0X%0X, is not 0XA5\n", code&0xff);
		return 0;
	}

	ULOG("recv ttyfd A5 msglen = %d\n", msglen);
	DATA_DUMP("logtty recv", (const unsigned char *)buffer, msglen);
	do_exit = 1;
	return 0;
}

static int check_timeout(void)
{
	/*time_t curr = time(NULL);
	time_t last = startup_time + 10;

	if (tm_before(last, curr)) {
		// A0 resp  timeout
		do_exit = 1;
		ULOG("timeout state=%u, we don't recv the resp, exit\n", lgtty_state);
	}*/
	
	return 0;
}

static void error_check(int failed)
{
	static int fail_cont = 0;
	if (failed <= 0)  {
		fail_cont = 0;  // reset it
		return;
	}

	fail_cont ++;
	if (fail_cont >= 7) {
		do_exit = 1;
		ULOG("ttyfd had failed count = %d, exit process\n", fail_cont);
	}
}

static int Y_odd_parity_check(const unsigned char *buffer, int blen, unsigned char ycheck)
{
	int j;
	int pos = 1;
	unsigned char result = 0;
	for (j = 0; j < 8; j ++, pos <<= 1) {  // calc 8 bits
		int i; unsigned int ret = 0;
		for (i = 0; i < blen; i ++) {  //
			ret = ret ^ ((buffer[i]&pos)>>j);
		}
		result = (result & ~pos) ^ (ret<<j);
	}

	if (0xff == (result ^ ycheck)) return 0;
	return -1;
}

static int X_odd_parity_check(const unsigned char *buffer, int blen, unsigned char xcheck)
{
	unsigned char result = 0;
	int i;
	for (i = 0; i < blen; i ++) { 
		int j;
		int pos = 1; unsigned int ret = 0;
		for (j = 0; j < 8; j ++, pos <<= 1) {  // calc 8 bits
			ret = ret ^ ((buffer[i]&pos)>>j);
		}

		result = (xcheck>>(7-i))&0x01;
		result = result ^ ret;
		if (1 != result) return -1;
	}

	return 0;
}

static const char *D7_info[2] = {
	[0] = "Uni-direction device", 
	[1] = "Bi-direction device"
};

static const char *D6_info[2] = {
	[0] = "stateless report", 
	[1] = "fixed time interval sending"
};

static const char *devtype_info[2][16] = {
	[0] = {  // for alarm unites
		[0x0] = "undefined device", 
		[0x1] = "IR fence", 
		[0x2] = "PIR detector", 
		[0x3] = "natural gas detector", 
		[0x4] = "panic button", 
		[0x5] = "smoke detector", 
		[0x6] = "door contact", 
		[0x7] = "glass break detector", 
		[0x8] = "vibration sensor", 
		[0x9] = "water level detector", 
		[0xa] = "high temperature sensor", 
		[0xb] = "CO sensor", 
		[0xc] = "unknown device", 
		[0xd] = "unknown device", 
		[0xe] = "unknown device", 
		[0xf] = "unknown device"
	}, 
	[1] = {  // for control unites
		[0x0] = "unknown device", 
		[0x1] = "unknown device", 
		[0x2] = "unknown device", 
		[0x3] = "unknown device", 
		[0x4] = "unknown device", 
		[0x5] = "unknown device", 
		[0x6] = "doorbell button", 
		[0x7] = "unknown device", 
		[0x8] = "unknown device", 
		[0x9] = "remotor control", 
		[0xa] = "Control panel", 
		[0xb] = "unknown device", 
		[0xc] = "wireless keypad", 
		[0xd] = "unknown device", 
		[0xe] = "wireless siren", 
		[0xf] = "remote switch"
	}
};

static int dev_frame_process(const char *buffer, int msglen)
{
	unsigned char D = buffer[3];
	//unsigned char E = buffer[4];

	unsigned char D7 = (D>>7)&0x01;
	ULOG("D7: %s\n", D7_info[D7]);

	unsigned char D6 = (D>>6)&0x01;
	ULOG("D6: %s\n", D6_info[D6]);

	unsigned char D5 = (D>>5)&0x01;
	unsigned char D1D4 = (D>>1)&0x0f;
	ULOG("device code: %s\n", devtype_info[D5][D1D4]);

	//unsigned char D0 = D&0x01;
	
	return 0;
}

#define ID_LEN  3
#define DEV_FRAME_LEN  7

// equal the minimum cmd frame length
#define MIN_FRAME_LEN  (ID_LEN+3)

int lgtty_read(int fd, LGTTY_RECVS *rcvs)
{
	char *ptr = rcvs->recv_buf + rcvs->recv_len;
	int read_len = 0;
	char *buffer = NULL;
	int msglen = 0;
	int rfs_ret = 0;

	buffer = rcvs->recv_buf;
	ULOG("read from server totallen=%d, this readlen=%d\n", rcvs->recv_len, read_len);
	DATA_DUMP("read tty", (const unsigned char *)buffer, rcvs->recv_len);

	while (1) {
		//Device Frame Protocol format : |3 bytes ID|2 bytes data| 1 byte odd check for Y | 1 byte parity check for X |
		//Cmd Frame Protocol format : |3 bytes 0xFFFFFF|1 byte code| 1 byte length of param | N bytes param |1 byte checksum|

		if (rcvs->recv_len < ID_LEN) {
			rfs_ret = 1;
			ULOG("recv ttyfd recv_len = %d, not enough %d\n", rcvs->recv_len, rfs_ret);
			goto _RFS_END;
		}

		ULOG("buffer[0]=%02x, buffer[1]=%02x, buffer[2]=%02x\n", 
			(buffer[0]&0xff), (buffer[1]&0xff), (unsigned char)(buffer[2]));
		if (0xff == (buffer[0]&0xff) && 0xff == (buffer[1]&0xff) && 0xff == (buffer[2]&0xff)) {
			// it is cmd frame
			msglen = ID_LEN + 3;  // at least  |3 bytes 0xFFFFFF|1 byte code| 1 byte length of param |1 byte checksum|
			if (rcvs->recv_len < msglen) {
				rfs_ret = 2;
				ULOG("recv ttyfd recv_len = %d, not enough %d\n", rcvs->recv_len, rfs_ret);
				goto _RFS_END;
			}

			unsigned char code = buffer[3];
			if (code >= CMD_CODE_MAX) {
				msglen = 1;
				ULOG("recv ttyfd code=0x%02x, is invalid\n", code&0xff);
				error_check(1);
				goto _CONT_NEXT;
			}
			
			unsigned char blen = buffer[4];
			if (blen > 0) {
				msglen += blen;
				if (msglen > MAX_RECV_BUFF_LEN) {
					ULOG("recv ttyfd data len=%d %d too large\n", blen, msglen);
					msglen = 1;
					error_check(1);
					goto _CONT_NEXT;
				}
				
				if (rcvs->recv_len < msglen) {
					rfs_ret = 3;
					ULOG("recv ttyfd recv_len = %d, not enough %d\n", rcvs->recv_len, rfs_ret);
					goto _RFS_END;
				}
			}

			// check sum 
			ULOG("recv ttyfd cmd frame msglen = %d, recv_len=%d\n", msglen, rcvs->recv_len);
			DATA_DUMP("logtty recv cmd frame", (const unsigned char *)buffer, msglen);
			unsigned char cksum = 0;
			lgtty_checksum((unsigned char *)&buffer[3], msglen-3-1, &cksum);
			if (cksum != (unsigned char)buffer[msglen-1]) {
				ULOG("recv ttyfd checksum=0x%02x, is error result 0x%02x\n", buffer[msglen-1]&0xff, cksum);
				msglen = 1;
				error_check(1);
				goto _CONT_NEXT;
			}

			error_check(0);
			STATE_PROC *sproc = &state_proc_s[lgtty_state];
			if (sproc->state_recv) {
				rfs_ret = sproc->state_recv(buffer, msglen);
				if (rfs_ret < 0) {
					error_check(1);
					ULOG("call state_recv in state=%u, return %d\n", lgtty_state, rfs_ret);
					goto _CONT_NEXT;
				}
			}
		} else {
			// data frame check ...
			//Device Frame Protocol format : |3 bytes ID|2 bytes data| 1 byte odd check for Y | 1 byte parity check for X |
			int dret = 0;
			if (rcvs->recv_len < DEV_FRAME_LEN) {
				rfs_ret = 4;
				ULOG("recv ttyfd dev frame recv_len = %d, not enough %d\n", rcvs->recv_len, rfs_ret);
				goto _RFS_END;
			}

			msglen = 7;
			// checksum 
			ULOG("recv ttyfd dev frame msglen = %d, recv_len=%d\n", msglen, rcvs->recv_len);
			DATA_DUMP("logtty recv dev frame", (const unsigned char *)buffer, msglen);
			dret = Y_odd_parity_check((unsigned char *)buffer, 5, (unsigned char)buffer[5]);
			if (dret != 0) {
				ULOG("recv ttyfd ycheck=%d  error\n", dret);
				msglen = 1;
				error_check(1);
				goto _CONT_NEXT;
			}
			dret = X_odd_parity_check((unsigned char *)buffer, 5, (unsigned char)buffer[6]);
			if (dret != 0) {
				ULOG("recv ttyfd xcheck=%d  error\n", dret);
				msglen = 1;
				error_check(1);
				goto _CONT_NEXT;
			}

			error_check(0);
			rfs_ret = dev_frame_process(buffer, msglen);
		}

	_CONT_NEXT:
		rcvs->recv_len -= msglen;
		buffer += msglen;
	}

_RFS_END:
    rcvs->recv_len = 0;
	/*if (rcvs->recv_len > 0 && ((unsigned long)rcvs->recv_buf != (unsigned long)buffer)) {
		memmove(rcvs->recv_buf, buffer, rcvs->recv_len);
		ULOG("recv from ttyfd, remain data len=%d\n", rcvs->recv_len);
	}*/

	return rfs_ret;
}

#define FILL_CMD_FRAME_HEADER(hdr, val) \
{ \
	hdr[0] = (val); \
	hdr[1] = (val); \
	hdr[2] = (val); \
}

// Calculate the checksum to @output
static void lgtty_checksum(const unsigned char *buffer, int blen, unsigned char *output)
{
	int i;
	unsigned int checksum = 0;
	for (i = 0; i < blen; i ++) {
		checksum += (unsigned int)buffer[i];
	}

	*output = (unsigned char)checksum;
}

static int switch_matching_rate(int fd)
{
	int ret;
	unsigned char sendbuf[8];
	FILL_CMD_FRAME_HEADER(sendbuf, 0xFF);

	// fill cmd, param, checksum
	sendbuf[3] = 0xAE;  // cmd param
	sendbuf[4] = 0x01;  // cmd length
	sendbuf[5] = 0x01;  // cmd param
	lgtty_checksum(&sendbuf[3], 3, &sendbuf[6]);
	
	ret = lgtty_write(fd, sendbuf, 7);
	if (7 == ret) 
		return 0;
	return -1;
}

static int switch_work_mode(int fd)
{
	int ret;
	unsigned char sendbuf[8];
	FILL_CMD_FRAME_HEADER(sendbuf, 0xFF);

	// fill cmd, param, checksum
	sendbuf[3] = 0xAE;  // cmd param
	sendbuf[4] = 0x01;  // cmd length
	sendbuf[5] = 0x00;  // cmd param
	lgtty_checksum(&sendbuf[3], 3, &sendbuf[6]);
	
	ret = lgtty_write(fd, sendbuf, 7);
	if (7 == ret) 
		return 0;
	return -1;
}

// Begin Matching rate  (in 5s )
int lgtty_matching_rate(int fd)
{
	int ret =0, trycnt = 0;

	while (trycnt ++ < 3) {
		// Switch to matching rate mode
		ret = switch_matching_rate(fd);
		if (0 == ret) goto SUCC_MATCH_RATE;
        vTaskDelay(40 / portTICK_RATE_MS);
	}
	return -1;

SUCC_MATCH_RATE:	
	lgtty_state = ESTAT_MATCH_RATE;
    vTaskDelay(5000 / portTICK_RATE_MS);

	// Recovery to normal work mode
	trycnt = 0;
	while (trycnt ++ < 3) {	
		ret = switch_work_mode(fd);
		if (0 == ret) {
			lgtty_state = ESTAT_MATCH_WORK;
			return 0;
		}
		vTaskDelay(40 / portTICK_RATE_MS);
	}
	return -2;
}

// Switch to normal work mode
int lgtty_work_mode(int fd)
{
	int ret =0, trycnt = 0;

	while (trycnt ++ < 3) {	
		ret = switch_work_mode(fd);
		if (0 == ret) {
			lgtty_state = ESTAT_MATCH_WORK;
			return 0;
		}
		vTaskDelay(40 / portTICK_RATE_MS);
	}

	return -2;
}

int lgtty_check_cmd(int change_state, int fd, unsigned char value)
{
	int ret =0, trycnt = 0;
	unsigned char sendbuf[8];
	FILL_CMD_FRAME_HEADER(sendbuf, 0xFF);

	// fill cmd, param, checksum
	sendbuf[3] = 0xA0;  // cmd param
	sendbuf[4] = 0x01;  // cmd length
	sendbuf[5] = value;  // cmd param
	lgtty_checksum(&sendbuf[3], 3, &sendbuf[6]);

	while (trycnt ++ < 3) {	
		ret = lgtty_write(fd, sendbuf, 7);
		if (7 == ret) {
			 goto _CHECK_RCV;
		}

		vTaskDelay(40 / portTICK_RATE_MS);
	}

	ULOG("lgtty send check cmd failed ret = %d\n", ret);
	return -1;

_CHECK_RCV:
	// recv ttyfd  response, next step ...
	if (change_state != 0)
		lgtty_state = ESTAT_MATCH_CHECK;
	return 0;
}

int lgtty_modver_cmd(int change_state, int fd, unsigned char value)
{
	int ret = 0, trycnt = 0, msglen = 0;
	unsigned char sendbuf[16];
	FILL_CMD_FRAME_HEADER(sendbuf, 0xFF);

	// fill cmd, param, checksum
	sendbuf[3] = 0xA1;	// cmd param
	if (value == 0) {
		sendbuf[4] = 0x00;	// cmd length
		msglen = 5;
	} else {
		sendbuf[4] = 0x01;	// cmd length
		if (1 == value) {
			sendbuf[5] = 0x00;
		} else if (2 == value) {
			sendbuf[5] = 0x01;
		} else { 
			return -1;
		}
		msglen = 6;
	}
	
	lgtty_checksum(&sendbuf[3], msglen-3, &sendbuf[msglen]);
	msglen ++;

	while (trycnt ++ < 3) { 
		ret = lgtty_write(fd, sendbuf, msglen);
		if (msglen == ret) {
			 goto _CHECK_RCV;
		}

		vTaskDelay(40 / portTICK_RATE_MS);
	}

	ULOG("lgtty send modver cmd failed ret = %d\n", ret);
	return -1;

_CHECK_RCV:
	// recv ttyfd  response, next step ...
	if (change_state != 0)
		lgtty_state = ESTAT_MATCH_MODVER;
	return 0;

}

// A2 cmd 
// @model: 3-- quit test state, 0-- quit test state, 1-- recv test, 2-- send test; 
int lgtty_test_cmd(int change_state, int fd, int model, unsigned char *out, int vlen)
{
	int ret =0, trycnt = 0, msglen = 0;
	unsigned char sendbuf[16];
	FILL_CMD_FRAME_HEADER(sendbuf, 0xFF);

	// fill cmd, param, checksum
	sendbuf[3] = 0xA2;	// cmd param

	if (model == 3) {
		// query test state of major module, no parameters
		sendbuf[4] = 0x00;	// cmd length
		msglen = 5;
	} else if (model == 0) {
		// quit test state
		sendbuf[4] = 0x01;	// cmd length
		sendbuf[5] = model;
		msglen = 6;
	} else {
		// In the requested channel and FEI to enter the recv/send mode
		sendbuf[4] = vlen + 1;	// cmd length
		sendbuf[5] = model;
		memcpy(&sendbuf[6], out, vlen);
		msglen = vlen + 6; 
	}

	lgtty_checksum(&sendbuf[3], msglen-3, &sendbuf[msglen]);
	msglen ++;

	while (trycnt ++ < 3) { 
		ret = lgtty_write(fd, sendbuf, msglen);
		if (msglen == ret) {
			 goto _CHECK_RCV;
		}

		vTaskDelay(40 / portTICK_RATE_MS);
	}

	ULOG("lgtty send test cmd failed ret = %d\n", ret);
	return -1;

_CHECK_RCV:
	// recv ttyfd  response, next step ...
	if (change_state != 0)
		lgtty_state = ESTAT_MATCH_TEST;
	return 0;	
}

// A3 cmd 
// @op:  1: query ID,  2: set ID;  
int lgtty_ID_cmd(int change_state, int fd, int op, unsigned char *out, int vlen)
{
	int ret =0, trycnt = 0, msglen = 0;
	unsigned char sendbuf[16];
	FILL_CMD_FRAME_HEADER(sendbuf, 0xFF);

	// fill cmd, param, checksum
	sendbuf[3] = 0xA3;	// cmd param

	if (op == 1) {
		// query ID
		sendbuf[4] = 0x00;	// cmd length
		msglen = 5;
	} else {
		// set ID
		sendbuf[4] = vlen;	// cmd length
		memcpy(&sendbuf[5], out, vlen);
		msglen = vlen + 5;
	}

	lgtty_checksum(&sendbuf[3], msglen-3, &sendbuf[msglen]);
	msglen ++;

	while (trycnt ++ < 3) { 
		ret = lgtty_write(fd, sendbuf, msglen);
		if (msglen == ret) {
			 goto _CHECK_RCV;
		}

		vTaskDelay(40 / portTICK_RATE_MS);
	}

	ULOG("lgtty send ID cmd failed ret = %d\n", ret);
	return -1;

_CHECK_RCV:
	// recv ttyfd  response, next step ...
	if (change_state != 0)
		lgtty_state = ESTAT_MATCH_ID;
	return 0;	
}

// decimal  to  bcd code 
//	if @vlaue = 99999, then @bytes = 3;  if @vlaue = 199999, then @bytes = 4
void decimal_to_bcd(unsigned int value, unsigned char *out, int bytes)
{
	int i = bytes - 1;
	unsigned int wei = 10;
	unsigned int ret = 0;
	for (; i >= 0; i --) {
		//unsigned char *ob = &out[i];
		ret = value % wei;
		out[i] = ret & 0x0f;
		value = value/wei;
		
		ret = value % wei;
		ret &= 0x0f;
		ret <<= 4;
		out[i] = (out[i]&0x0f) ^ (ret&0xf0);
		value = value/wei;
	}
}

// A4 cmd 
// @op:  1: query channel,  2: set channel;  
int lgtty_channel_cmd(int change_state, int fd, int op, int channel)
{
	int ret =0, trycnt = 0, msglen = 0;
	unsigned char sendbuf[16];
	FILL_CMD_FRAME_HEADER(sendbuf, 0xFF);

	// fill cmd, param, checksum
	sendbuf[3] = 0xA4;	// cmd param

	if (op == 1) {
		// query channel
		sendbuf[4] = 0x00;	// cmd length
		msglen = 5;
	} else {
		// set channel
		sendbuf[4] = 1;	// cmd length
		sendbuf[5] = (unsigned char)channel;
		msglen = 6;
	}

	lgtty_checksum(&sendbuf[3], msglen-3, &sendbuf[msglen]);
	msglen ++;

	while (trycnt ++ < 3) { 
		ret = lgtty_write(fd, sendbuf, msglen);
		if (msglen == ret) {
			 goto _CHECK_RCV;
		}

		vTaskDelay(40 / portTICK_RATE_MS);
	}

	ULOG("lgtty send ID cmd failed ret = %d\n", ret);
	return -1;

_CHECK_RCV:
	// recv ttyfd  response, next step ...
	if (change_state != 0)
		lgtty_state = ESTAT_MATCH_CHANNEL;
	return 0;	
}

// A5 cmd 
// @op:  1: query all I/O,  2: set all I/O; 
// @opcode:  0x00: set input or output model  for I/O,  0x01: set high or low bit for I/O 
int lgtty_io_cmd(int change_state, int fd, int op, unsigned char opcode, unsigned char addr, unsigned char data)
{
	int ret = 0, trycnt = 0, msglen = 0;
	unsigned char sendbuf[16];
	FILL_CMD_FRAME_HEADER(sendbuf, 0xFF);

	// fill cmd, param, checksum
	sendbuf[3] = 0xA5;	// cmd param

	if (op == 1) {
		// query channel
		sendbuf[4] = 0x00;	// cmd length
		msglen = 5;
	} else {
		// set channel
		sendbuf[4] = 3;	// cmd length
		sendbuf[5] = (unsigned char)opcode;
		sendbuf[6] = (unsigned char)addr;
		sendbuf[7] = (unsigned char)data;
		msglen = 8;
	}

	lgtty_checksum(&sendbuf[3], msglen-3, &sendbuf[msglen]);
	msglen ++;

	while (trycnt ++ < 3) { 
		ret = lgtty_write(fd, sendbuf, msglen);
		if (msglen == ret) {
			 goto _CHECK_RCV;
		}

		vTaskDelay(40 / portTICK_RATE_MS);
	}

	ULOG("lgtty send IO cmd failed ret = %d\n", ret);
	return -1;

_CHECK_RCV:
	// recv ttyfd  response, next step ...
	if (change_state != 0)
		lgtty_state = ESTAT_MATCH_IO;
	return 0;	
}

