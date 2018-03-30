#ifndef LG_TTY_H
#define LG_TTY_H
#include <time.h>

int lgtty_open(char *portname);
int lgtty_close(int fd);
int lgtty_write(int fd, const void *buffer, uint8 len);
//int lgtty_recv(int fd, char *rcv_buf, int data_len);

#define	MAX_RECV_BUFF_LEN 256
typedef struct {
	int recv_len;
	char recv_buf[MAX_RECV_BUFF_LEN];
}  LGTTY_RECVS;

#define  TTY_COMM_LEN  7
#define  TTY_ID_LEN  3
#define  TTY_DATA_LEN  2

typedef enum {
	ESTAT_INIT =0, 
	ESTAT_MATCH_RATE, 
	ESTAT_MATCH_WORK, 
	ESTAT_MATCH_TEST, 
	ESTAT_MATCH_CHECK, 
	ESTAT_MATCH_MODVER, 
	ESTAT_MATCH_ID, 
	ESTAT_MATCH_CHANNEL, 
	ESTAT_MATCH_IO, 
	ESTAT_OVER
} TTY_MOD_STATE;

typedef struct {
	int (*state_recv)(const char *, int );
	int (*state_timeout)(void);
} STATE_PROC;

extern STATE_PROC state_proc_s[ESTAT_OVER+1];

#define CMD_CODE_A0  0XA0
#define CMD_CODE_A1  0XA1
#define CMD_CODE_A2  0XA2
#define CMD_CODE_A3  0XA3
#define CMD_CODE_A4  0XA4
#define CMD_CODE_A5  0XA5
#define CMD_CODE_AE  0XAE
#define CMD_CODE_MAX 0XAF


#define ULOG(fmt, a...)  printf(fmt, ##a) 
#define DATA_DUMP(prompt, data, len)	__data_dump(prompt, data, len)

int lgtty_read(int fd, LGTTY_RECVS *rcvs);
void lgtty_timeout_callback(int fd, LGTTY_RECVS *rcvs);

extern int do_exit;
extern time_t startup_time;
extern TTY_MOD_STATE lgtty_state;

static inline int tm_before(time_t seq1, time_t seq2)
{
	return (long)((unsigned long)seq1 - (unsigned long)seq2) < 0;
}

static inline long uint_before(unsigned long long seq1, unsigned long long seq2)
{
	return (long)((unsigned long long)seq1 - (unsigned long long)seq2) < 0;
}

int lgtty_work_mode(int fd);
int lgtty_matching_rate(int fd);
int lgtty_check_cmd(int change_state, int fd, unsigned char value);
int lgtty_modver_cmd(int change_state, int fd, unsigned char value);
int lgtty_test_cmd(int change_state, int fd, int model, unsigned char *out, int vlen);
int lgtty_ID_cmd(int change_state, int fd, int op, unsigned char *out, int vlen);
void decimal_to_bcd(unsigned int value, unsigned char *out, int bytes);
int lgtty_channel_cmd(int change_state, int fd, int op, int channel);
int lgtty_io_cmd(int change_state, int fd, int op, unsigned char opcode, unsigned char addr, unsigned char data);
void __data_dump(const char *prompt, const unsigned char *data, int len);

#endif

