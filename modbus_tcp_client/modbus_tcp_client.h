#ifndef _MODBUS_TCP_CLIENT_H_
#define _MODBUS_TCP_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct modbus_tcp_client modbus_tcp_client;

modbus_tcp_client* modbus_tcp_client_open(char* ipAddress, unsigned short port);
int modbus_tcp_client_close(modbus_tcp_client* client);

int modbus_tcp_read_holding_registers(modbus_tcp_client* client, unsigned short address, unsigned short len, void* buffer);
int modbus_tcp_write_multiple_registers(modbus_tcp_client* client, unsigned short address, unsigned short len, void* data);
int modbus_tcp_read_multiblock_registers(modbus_tcp_client* client, int num_of_block, unsigned short *addr, unsigned short *len, void* buffer);

enum multiblock_option{
	MODBUS_TCP_RW_READ,
	MODBUS_TCP_RW_WRITE,
};

typedef struct {
	unsigned short option;
	unsigned short page;
	unsigned short address;
	unsigned short length;
	unsigned short* buffer;
} modbus_tcp_multiblock_request_t;

int modbus_tcp_read_write_multiblock_registers(modbus_tcp_client* client, modbus_tcp_multiblock_request_t* requests, int num_of_requests);

void modbus_tcp_client_set_response_timeout(modbus_tcp_client* client, unsigned short timeout_msec);

#ifdef __cplusplus
}
#endif

#endif
