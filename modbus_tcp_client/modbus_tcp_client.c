#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <termios.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <pthread.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include "modbus_tcp_client.h"

#define MULTIBLOCK_BUFFER_SIZE 1024

struct modbusTcpHeader {
	unsigned short transaction_id;
	unsigned short protocol_id;
	unsigned short length;
	unsigned char unit_id;
	unsigned char function_code;
};

struct modbus_tcp_client {
	int socket;
	struct timeval responseTimeout;
	unsigned short transactionId;
};

modbus_tcp_client* modbus_tcp_client_open(char* ipAddress, unsigned short port)
{
	struct modbus_tcp_client* client = malloc(sizeof(struct modbus_tcp_client));
	struct sockaddr_in sa;
	int res;
	
	client->socket = -1;
	
	res = inet_pton(AF_INET, ipAddress, &(sa.sin_addr));
	if (res < 0) return NULL;
	
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	client->transactionId = 0;

	const struct timeval default_timeout = {1, 0};
	client->responseTimeout = default_timeout;
	
	client->socket = socket(PF_INET, SOCK_STREAM, 0);
	if (client->socket < 0) {
		goto do_free;
	}
	
	res = connect(client->socket, (struct sockaddr*)&sa, sizeof(sa));
	if (res < 0) {
		close(client->socket);
		goto do_free;
	} else {
		return client;
	}
	
do_free:
	free(client);
	return NULL;
}

int modbus_tcp_client_close(modbus_tcp_client* client)
{
	if (!client) return -1;
	
	close(client->socket);
	free(client);
	
	return 0;
}

static int tcp_read(modbus_tcp_client* client, void* buffer, int length)
{
	int socket = client->socket;
	int remaining_length = length;
	fd_set rfds;
	struct timeval timeout;
	
	FD_ZERO(&rfds);
	FD_SET(socket, &rfds);
		
	while (remaining_length > 0)
	{
		int chunk_length;
		int res;
		
		timeout = client->responseTimeout;
		
		res = select(socket+1, &rfds, NULL, NULL, &timeout);
		if (res <= 0) {
			return -1;
		}
		
		chunk_length = recv(socket, buffer, remaining_length, 0);
		
		if (chunk_length <= 0) {
			return -1;
		}
		
		remaining_length -= chunk_length;
		buffer += chunk_length;
	}
	
	return length;
}

static int tcp_write(modbus_tcp_client* client, void* buffer, int length)
{
	int socket = client->socket;
	int remaining_length = length;
	
	while (remaining_length > 0)
	{
		int chunk_length = send(socket, buffer, remaining_length, MSG_NOSIGNAL);
		
		if (chunk_length <= 0) {
			return -1;
		}
		
		remaining_length -= chunk_length;
		buffer += chunk_length;
	}
	
	return length;
}

static int check_response_header(struct modbusTcpHeader request, struct modbusTcpHeader response)
{
	if(request.transaction_id != response.transaction_id) {
		printf("trid mismatch. %x <-> %x\n", request.transaction_id, response.transaction_id);
		return -1;
	}

	if(response.protocol_id != 0) {
		printf("protocol error\n");
		return -1;
	}

	if(request.unit_id != response.unit_id) {
		printf("unit_id mismatch\n");
		return -1;
	}

	if(request.function_code != response.function_code) {
		printf("function code mismatch\n");
		return -1;
	}

	return 1;
}

int modbus_tcp_read_holding_registers(modbus_tcp_client* client, unsigned short address, unsigned short len, void* buffer)
{
	struct read_registers_request {
		struct modbusTcpHeader header;
		unsigned short addr;
		unsigned short len;
	} request;
	
	struct read_registers_response {
		struct modbusTcpHeader header;
		union {
			unsigned char byte_count;
			unsigned char exception_code;
		};
	} response;
	
	unsigned short* buffer16;
	int res;
	int i;
	
	request.header.transaction_id = htons(client->transactionId++);
	request.header.protocol_id = 0;
	request.header.length = htons(6);
	request.header.unit_id = 1;
	request.header.function_code = 3;
	request.addr = htons(address);
	request.len = htons(len);
	
	res = tcp_write(client, &request, sizeof(request));
	if (res <= 0) {
		printf("error sending request\n");
		return -1;
	}
	
	res = tcp_read(client, &response, sizeof(struct modbusTcpHeader) + 1);
	if (res <= 0) {
		printf("error reading response header\n");
		return -1;
	}
	
	response.header.length = ntohs(response.header.length);
	
	res = check_response_header(request.header, response.header);
	if(res <= 0){
		return res;
	}
	
	if (response.header.function_code == (0x80 + request.header.function_code)) {
		printf("error response. error code = %02x\n", response.exception_code);
		return 0;
	}

	if (response.header.length != 3 + len*2) {
		printf("length mismatch\n");
		return -1;
	}
	
	if (response.byte_count != len*2) {
		printf("byte length mismatch\n");
		return -1;
	}
	
	res = tcp_read(client, buffer, len*2);
	if (res <= 0) {
		printf("error reading data\n");
		return -1;
	}
	
	buffer16 = buffer;
	
	for (i=0; i<len; i++) {
		buffer16[i] = ntohs(buffer16[i]);
	}
	
	return 1;
}

int modbus_tcp_write_multiple_registers(modbus_tcp_client* client, unsigned short address, unsigned short len, void* data)
{
	struct write_registers_request {
		struct modbusTcpHeader header;
		unsigned short addr;
		unsigned short len;
		unsigned char byte_count;
	} request;
	
	struct modbusTcpHeader responseHeader;
	struct write_registers_response {
		unsigned short addr;
		unsigned short len;
	} response;
	
	unsigned short* buffer16;
	int res;
	int i;
	
	request.header.transaction_id = htons(client->transactionId++);
	request.header.protocol_id = 0;
	request.header.length = htons(7 + len*2);
	request.header.unit_id = 1;
	request.header.function_code = 16;
	request.addr = htons(address);
	request.len = htons(len);
	request.byte_count = len*2;
	
	buffer16 = data;
	
	for (i=0; i<len; i++) {
		buffer16[i] = ntohs(buffer16[i]);
	}
	
	res = tcp_write(client, &request, sizeof(struct modbusTcpHeader) + 5);
	if (res <= 0) {
		printf("error sending request\n");
		return -1;
	}
	
	res = tcp_write(client, data, len*2);
	if (res <= 0) {
		printf("error sending request\n");
		return -1;
	}
	
	res = tcp_read(client, &responseHeader, sizeof(responseHeader));
	if (res <= 0) {
		printf("error reading response header\n");
		return -1;
	}
	
	responseHeader.length = ntohs(responseHeader.length);
	
	res = check_response_header(request.header, responseHeader);
	if(res <= 0){
		return res;
	}
	
	if (responseHeader.function_code == (0x80 + request.header.function_code)) {
		unsigned char exception_code;
		
		res = tcp_read(client, &exception_code, sizeof(exception_code));
		if (res <= 0) {
			printf("error reading exception code\n");
			return -1;
		}
		
		printf("error response. error code = %02x\n", exception_code);
		return 0;
	}

	if (responseHeader.length != 6) {
		printf("length mismatch\n");
		return -1;
	}
	
	res = tcp_read(client, &response, sizeof(response));
	if (res <= 0) {
		printf("error reading response header\n");
		return -1;
	}
	
	if (response.addr != request.addr) {
		printf("address mismatch\n");
		return -1;
	}
	
	if (response.len != request.len) {
		printf("len mismatch\n");
		return -1;
	}
	
	return 1;
}

int modbus_tcp_read_multiblock_registers(modbus_tcp_client* client, int num_of_block, unsigned short *addr, unsigned short *len, void* buffer)
{
	struct modbusTcpHeader request_header;
	struct request_block {
		unsigned short address;
		unsigned short length;
	} block;
	struct read_registers_response {
		struct modbusTcpHeader header;
		union {
			unsigned char number_of_block;
			unsigned char exception_code;
		};
	}response;
	unsigned char request_buf[MULTIBLOCK_BUFFER_SIZE];
	unsigned short request_len;
	int block_size = sizeof(block),  response_data_len = 0;
	int i, res;
	unsigned short* buffer16;

	memcpy(request_buf + sizeof(request_header), &num_of_block, sizeof(char));

	request_header.transaction_id = htons(client->transactionId++);
	request_header.protocol_id = 0;
	request_header.length = 3 + block_size * num_of_block;
	request_header.unit_id = 1;
	request_header.function_code = 0x65;
	memcpy(request_buf, &request_header, sizeof(request_header));

	for(i = 0 ; i < num_of_block ; i++)
	{
		int offset = sizeof(request_header) + 1 + (i * block_size);
		block.address = htons(addr[i]);
		block.length = htons(len[i]);
		
		memcpy(request_buf + offset, &block, block_size);
		response_data_len += len[i];
	}

	request_len = sizeof(request_header) + 1 + num_of_block * block_size;
	res = tcp_write(client, request_buf, request_len);
	if(res <= 0) {
		printf("error sending request \n");
		return -1;
	}

	res = tcp_read(client, &response, sizeof(struct modbusTcpHeader) + 1);
	if(res <= 0) {
		printf("error reading response header\n");
		return -1;
	}

	response.header.length = ntohs(response.header.length);
	
	res = check_response_header(request_header, response.header);
	if(res <= 0){
		return res;
	}

	if(response.header.function_code == (0x80 + request_header.function_code)) {
		printf("error response. error code = %02x\n", response.exception_code);
		return 0;
	}

	if(response.header.length != request_header.length + 2 * response_data_len) {
		printf("length mismatch\n");
		return -1;
	}

	if(response.number_of_block != num_of_block) {
		printf("number of block mismatch\n");
		return -1;
	}
	res = tcp_read(client, buffer, block_size * num_of_block);
	res = tcp_read(client, buffer, response_data_len * 2);
	if(res <= 0) {
		printf("error reading data\n");
		return -1;
	}

	buffer16 = buffer;
	for(i = 0 ; i < response_data_len; i++) {
		buffer16[i] = ntohs(buffer16[i]);
	}

	return 1;
}

#define TYPE_READ	0xC3C3
#define TYPE_WRITE	0x3C3C

int modbus_tcp_read_write_multiblock_registers(modbus_tcp_client* client, modbus_tcp_multiblock_request_t* requests, int num_of_requests)
{
	struct request {
		struct modbusTcpHeader header;
		unsigned short num_of_requests;
	} request, response;

	struct {
		unsigned short option;
		unsigned short page;
		unsigned short address;
		unsigned short length;
	} req_block;

	int byte_len = 4;
	int res, i;

	for (i=0 ; i<num_of_requests ; i++) {
		modbus_tcp_multiblock_request_t* req = &requests[i];
		byte_len += sizeof(req_block);
		if (req->option == MODBUS_TCP_RW_WRITE) {
			byte_len += req->length * 2;
		}
	}

	request.header.transaction_id = htons(client->transactionId++);
	request.header.protocol_id = 0;
	request.header.length = htons(byte_len);
	request.header.unit_id = 1;
	request.header.function_code = 0x68;
	request.num_of_requests = htons(num_of_requests);


	res = tcp_write(client, &request, sizeof(request));
	if (res <= 0) {
		printf("error sending request\n");
		return -1;
	}

	for (i=0; i<num_of_requests; i++) {
		modbus_tcp_multiblock_request_t* req = &requests[i];
		if (req->option == MODBUS_TCP_RW_READ) {
			req_block.option = htons(0xC3C3);
		} else {
			req_block.option = htons(0x3C3C);
		}

		req_block.page = htons(req->page);
		req_block.address = htons(req->address);
		req_block.length = htons(req->length);

		res = tcp_write(client, &req_block, sizeof(req_block));
		if (res <= 0) {
			printf("error sending block %d\n", i);
			return -1;
		}

		if (req->option == MODBUS_TCP_RW_WRITE) {
			unsigned short buffer[req->length];
			int n;

			for (n=0; n<req->length; n++) {
				buffer[n] = htons(req->buffer[n]);
			}

			res = tcp_write(client, &buffer, req->length * 2);
			if (res <= 0) {
				printf("error sending block data %d\n", i);
				return -1;
			}
		}
	}

	res = tcp_read(client, &response, sizeof(response));
	if (res <= 0) {
		printf("error reading response\n");
		return -1;
	}

	response.header.length = ntohs(response.header.length);
	response.num_of_requests = ntohs(response.num_of_requests);

	if (response.header.transaction_id != request.header.transaction_id
	 || response.header.protocol_id != 0
	 || response.header.length != 4
	 || response.header.unit_id != request.header.unit_id
	 || response.header.function_code != request.header.function_code
	 || response.num_of_requests != request.num_of_requests) {
		printf("multi response error %d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n"
		    , response.header.transaction_id
		    , request.header.transaction_id
		    , response.header.protocol_id
		    , response.header.length
		    , response.header.unit_id
		    , request.header.unit_id
		    , response.header.function_code
		    , request.header.function_code
		    , response.num_of_requests
		    , request.num_of_requests
		);
		return -1;
	}

	for (i=0 ; i<num_of_requests ; i++) {
		modbus_tcp_multiblock_request_t* req = &requests[i];
		unsigned short ack;

		res = tcp_read(client, &ack, sizeof(ack));
		if (res <= 0) {
			printf("error reading ack %d\n", i);
			return -1;
		}

		ack = ntohs(ack);

		if (ack != 0) {
			return -1;
		}

		if (req->option == MODBUS_TCP_RW_READ) {
			res = tcp_read(client, req->buffer, req->length * 2);
			if (res <= 0) {
				printf("error reading data %d\n", i);
				return -1;
			}

			int n;
			for (n=0 ; n<req->length ; n++) {
				req->buffer[n] = ntohs(req->buffer[n]);
			}
		}
	}

	return 1;
}

void modbus_tcp_client_set_response_timeout(modbus_tcp_client* client, unsigned short timeout_msec)
{
	unsigned int usec = (unsigned int)timeout_msec * 1000;
	client->responseTimeout.tv_sec = usec / 1000000;
	client->responseTimeout.tv_usec = usec % 1000000;
}
