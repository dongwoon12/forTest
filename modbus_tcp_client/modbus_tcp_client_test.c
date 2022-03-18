/*
 * Accura DSP control application example
 *
 * Copyright : 2009 Rootech Inc.
 * Author : bgmoon@rootech.com (2009 - )
 */

#include <stdio.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "modbus_tcp_client.h"

typedef float          float32_t;
typedef int            int32_t;
typedef unsigned int   uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char  uint8_t;

struct command;

int proc_open(struct command* cmd, char** parameter, const char* option);
int proc_rd(struct command* cmd, char** parameter, const char* option);
int proc_wr(struct command* cmd, char** parameter, const char* option);
int proc_quit(struct command* cmd, char** parameter, const char* option);
int proc_multi_rd(struct command* cmd, char** parameter, const char* option);
int proc_help(struct command* cmd, char** parameter, const char* option);

struct command {
	const char* cmd;
	int param_cnt;
	const char* option;
	int (*proc_func)(struct command*, char**, const char*);
	const char* usage;
} command[] = {
	{"open",	1, "502",	proc_open,		"open <ipString> (port)"},
	{"wr",   	1, "",		proc_wr,		"wr <addr> (value)"},
	{"rd",   	1, "125",	proc_rd,		"rd <addr> (length = 125)"},
    {"multi", 0, "1200",	proc_multi_rd,	"multi <addr> (length = 1200)"},
	{"quit", 	0, "",		proc_quit,		"quit"},
	{"help", 	0, "",		proc_help,		"help"},
};

#define NUM_OF_COMMAND (sizeof(command) / sizeof(struct command))

static modbus_tcp_client* client = NULL;

int proc_open(struct command* cmd, char** parameter, const char* option)
{
	unsigned int port;
	
	if (client) {
		modbus_tcp_client_close(client);
	}
		
	sscanf(option, "%d", &port);
	client = modbus_tcp_client_open(parameter[0], port);
	
	if (client) {
		printf("%s:%d open\n", parameter[0], port);
	} else {
		printf("open fail\n");
	}
	
	return 0;
}

int proc_rd(struct command* cmd, char** parameter, const char* option)
{
	unsigned short addr;
	unsigned short len;
	unsigned short data[128];
	struct stat st;
	int res;
	int i;
	
	if (!client) {
		printf("client not opened\n");
		return 0;
	}
	
	sscanf(parameter[0], "%hu", &addr);
	sscanf(option, "%hu", &len);
		
	res = modbus_tcp_read_holding_registers(client, addr - 1, len, data);
	if (res < 1) {
		printf("error modbus_tcp_read_registers(%x, %d)\n", addr, len);

		return res;
	} 

	printf("modbus_tcp_read_registers response message : %d\n", res);	
	
	for (i=0; i<len;) {
		int j;
		
		printf("%04x :", addr + i);
		
		for (j=0; j<16 && i<len; j++, i++) {
			printf(" %04x", data[i]);
		}
		
		printf("\n");
	}
	
	return 0;
}

int proc_wr(struct command* cmd, char** parameter, const char* option)
{
	unsigned short addr;
	unsigned short value;
	int res = 0;
	
	if (!client) {
		printf("client not opened\n");
		return 0;
	}
	
	sscanf(parameter[0], "%hu", &addr);
	
	if (option[0] == 0) {
		unsigned short buffer[100];
		int end_of_request = 0;
		int count = 0;
		
		do {
			char line[100];
			char* res;
			
			res = fgets(line, sizeof(line), stdin);
			if (res == NULL || line[0] == 0 || line[0] == '\n') {
				end_of_request = 1;
			} else {
				sscanf(line, "%hu", &buffer[count]);
				count++;
			}
		} while(!end_of_request);
		
		if (count > 0) {
			res = modbus_tcp_write_multiple_registers(client, addr - 1, count, buffer);
			if (res < 1) {
				printf("error modbus_tcp_write_registers(%x, count=%d)\n", addr, count);
				return res;
			}
				
			printf("modbus_tcp_write_registers response : %d\n", res);	
		}
	} else {
		sscanf(option, "%hu", &value);
		
		printf("addr=%u, value=%u\n", addr, value);
		
		res = modbus_tcp_write_multiple_registers(client, addr - 1, 1, &value);
		if (res < 1) {
			printf("error modbus_tcp_write_registers(%hu, %hu)\n", addr, value);
			return res;
		} 

		printf("modbus_tcp_write_registers response : %d\n", res);	
	}
	
	return res;
}


int proc_multi_rd(struct command* cmd, char** parameter, const char* option)
{
	unsigned short addr = 1999 , i;
	unsigned short value;
	unsigned short data[2000] = {0, };
	unsigned short len = 1200;
	int res = 0;

	if (!client) {
		printf("client not opened\n");
		return 0;
	}

	// sscanf(parameter[0], "%hu", &addr);
	// sscanf(option, "%hu", &len);
	
	res = modbus_tcp_read_multiblock_registers(client, 1, &addr, &len, data);
	if (res < 1) {
		printf("error modbus_tcp_read_registers(%x, %d)\n", addr, len);

		return res;
	}

	printf("modbus_tcp_read_registers response message : %d\n", res);
	
	printf("address 0 : ");
	for (i=0; i<len ; i++) {
		printf(" %04x", data[i]);
		// if(len % )
	}
	printf("\n");

	// printf("address 1119 : ");
	// for (i = 1000; i<1916 ; i++) {
	// 	printf(" %04x", data[i]);
	// }
	// printf("\n");
	// for (i=0; i<len;) {
	// 	int j;

	// 	printf("%04x :", addr + i);

	// 	for (j=0; j<16 && i<len; j++, i++) {
	// 		printf(" %04x", data[i]);
	// 	}

	// 	printf("\n");

	// 	for (j=1000; j<1016 && i<len; j++, i++) {
	// 		printf(" %04x", data[i]);
	// 	}
	// }

	return res;
}

int proc_quit(struct command* cmd, char** parameter, const char* option)
{
	if (client) {
		modbus_tcp_client_close(client);
	}
	
	exit(1);
	
	return 0;
}

int proc_help(struct command* cmd, char** parameter, const char* option)
{
	int i;
	
	printf("usage :\n");
	for (i=0; i<NUM_OF_COMMAND; i++) {
		printf("   %s\n", command[i].usage);
	}
	
	return 0;
}

int get_word(const char* string, int str_size, char* word, int word_size, int *idx)
{
	int word_len = 0;
	int word_idx = 0;
	
	while (*idx < str_size && string[*idx] != '\0' && isspace(string[*idx])) (*idx) ++;
	
	while (*idx < str_size && string[*idx] != '\0' && !isspace(string[*idx]) && word_idx < word_size-1) {
		word[word_idx++] = string[(*idx)++];
		word_len ++;
	}
	
	word[word_idx] = '\0';
	
	return word_len;
}
//get word는 결국 읽기용 str을 word라는 그냥 rw str으로 반환해 주는 역할
int main(int argc, char* argv[])
{
	while (1) {
		char line[100];
		char word[100];
		int idx;
		int i;
		
		//그냥 쉘에서 확인하는 부분들.
	next_cmd:
		printf("modbusTcp> ");
		
		if (fgets(line, sizeof(line), stdin) == NULL) {
			break;
		}//표준입력, 즉 쉘에서 문자 입력 시 break
		//이때의 null 값은 읽기 실패를 말한다.
		idx = 0;
		
		if (get_word(line, sizeof(line), word, sizeof(word), &idx) == 0) {
			continue;
		}//line을 버퍼로 쓰고 word에 해당 ㄱ
		
		for (i=0; i<NUM_OF_COMMAND; i++) {
			if (strcmp(word, command[i].cmd) == 0) {
				int param_idx;
				char *param_list[10];
				char param[10][100];
				char option[100];
				
				for (param_idx=0; param_idx < command[i].param_cnt; param_idx++) {
					if (get_word(line, sizeof(line), param[param_idx], sizeof(param[param_idx]), &idx) == 0) {
						printf("usage : %s\n", command[i].usage);
						goto next_cmd;
					}
					
					param_list[param_idx] = param[param_idx];
				}
				
				if (get_word(line, sizeof(line), option, sizeof(option), &idx) == 0) {
					(command[i].proc_func)(&command[i], param_list, command[i].option);
				} else {
					(command[i].proc_func)(&command[i], param_list, option);
				}
				
				break;
			}
		}
		
		if (i == NUM_OF_COMMAND) {
			printf("undefined command. type help for command list\n");
		}
	}
	
	return 0;
}
