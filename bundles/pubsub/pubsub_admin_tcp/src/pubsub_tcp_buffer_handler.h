/**
 *Licensed to the Apache Software Foundation (ASF) under one
 *or more contributor license agreements.  See the NOTICE file
 *distributed with this work for additional information
 *regarding copyright ownership.  The ASF licenses this file
 *to you under the Apache License, Version 2.0 (the
 *"License"); you may not use this file except in compliance
 *with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing,
 *software distributed under the License is distributed on an
 *"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 *specific language governing permissions and limitations
 *under the License.
 */
/*
 * pubsub_tcp_buffer_handler.h
 *
 *  \date       Mar 1, 2016
 *  \author    	<a href="mailto:dev@celix.apache.org">Apache Celix Project Team</a>
 *  \copyright	Apache License, Version 2.0
 */

#ifndef _PUBSUB_TCP_BUFFER_HANDLER_H_
#define _PUBSUB_TCP_BUFFER_HANDLER_H_
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "celix_threads.h"
#include "pubsub_tcp_msg_header.h"

typedef struct pubsub_tcpBufferHandler *pubsub_tcpBufferHandler_pt;
typedef void (*pubsub_tcpBufferHandler_processMessage_callback_t)(void* payload, const pubsub_tcp_msg_header_t* header, const unsigned char * buffer, size_t size, struct timespec *receiveTime);
typedef void (*pubsub_tcpBufferHandler_connectMessage_callback_t)(void* payload);

pubsub_tcpBufferHandler_pt pubsub_tcpBufferHandler_create();
int pubsub_tcpBufferHandler_create_receive_buffer(pubsub_tcpBufferHandler_pt handle, unsigned int maxNofBuffers, unsigned int bufferSize, bool bypassIpFragmentation);
void pubsub_tcpBufferHandler_destroy(pubsub_tcpBufferHandler_pt handle);
int pubsub_tcpBufferHandler_open(pubsub_tcpBufferHandler_pt handle, char* url);
int pubsub_tcpBufferHandler_close(pubsub_tcpBufferHandler_pt handle);
int pubsub_tcpBufferHandler_connect(pubsub_tcpBufferHandler_pt handle, char* url, void* payload, pubsub_tcpBufferHandler_connectMessage_callback_t connectMessageCallback, pubsub_tcpBufferHandler_connectMessage_callback_t disconnectMessageCallback);
int pubsub_tcpBufferHandler_close_connection(pubsub_tcpBufferHandler_pt handle, char* url);
int pubsub_tcpBufferHandler_listen(pubsub_tcpBufferHandler_pt handle, char* url);

int pubsub_tcpBufferHandler_sendto(pubsub_tcpBufferHandler_pt handle, int fd, void *buf, size_t count, int flags, struct sockaddr_in *dest_addr, size_t addrlen);
int pubsub_tcpBufferHandler_sendmsg(pubsub_tcpBufferHandler_pt handle, int fd, struct iovec *largeMsg_iovec, int len, int flags, struct sockaddr_in *dest_addr, size_t addrlen);

bool pubsub_tcpBufferHandler_dataAvailable(pubsub_tcpBufferHandler_pt handle, int fd, unsigned int *index, unsigned int *size);
int pubsub_tcpBufferHandler_read(pubsub_tcpBufferHandler_pt handle, unsigned int index, pubsub_tcp_msg_header_t** header, void ** buffer, unsigned int size);
int pubsub_tcpBufferHandler_handler(pubsub_tcpBufferHandler_pt handle);
int pubsub_tcpBufferHandler_write(pubsub_tcpBufferHandler_pt handle, pubsub_tcp_msg_header_t* header, void* buffer, unsigned int size, int flags);
int pubsub_tcpBufferHandler_messageHandler(pubsub_tcpBufferHandler_pt handle, void* payload, pubsub_tcpBufferHandler_processMessage_callback_t processMessageCallback);

#endif /* _PUBSUB_TCP_BUFFER_HANDLER_H_ */
