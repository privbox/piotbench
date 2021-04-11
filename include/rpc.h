#ifndef __BENCHMARK_RPC_H
#define __BENCHMARK_RPC_H

#include <sys/socket.h>
#include <sys/un.h>
#include <openssl/sha.h>

#include "queue.h"

#define REQUEST_LENGTH 1020

struct request_t {
	unsigned int id;
	unsigned char data[REQUEST_LENGTH];
};

struct response_t {
	unsigned int id;
	unsigned char sha[SHA_DIGEST_LENGTH];
};

struct message_t {
	struct request_t req;
	struct response_t res;
};


#endif // __BENCHMARK_RPC_H
