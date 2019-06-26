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

typedef struct pubsub_tcpHandler *pubsub_tcpHandler_pt;
typedef void (*pubsub_tcpHandler_processMessage_callback_t)(void* payload, const pubsub_tcp_msg_header_t* header, const unsigned char * buffer, size_t size, struct timespec *receiveTime);
typedef void (*pubsub_tcpHandler_connectMessage_callback_t)(void* payload);

pubsub_tcpHandler_pt pubsub_tcpHandler_create();
void pubsub_tcpHandler_destroy(pubsub_tcpHandler_pt handle);
int pubsub_tcpHandler_open(pubsub_tcpHandler_pt handle, char* url);
int pubsub_tcpHandler_close(pubsub_tcpHandler_pt handle);
int pubsub_tcpHandler_connect(pubsub_tcpHandler_pt handle, char* url, void* payload, pubsub_tcpHandler_connectMessage_callback_t connectMessageCallback, pubsub_tcpHandler_connectMessage_callback_t disconnectMessageCallback);
int pubsub_tcpHandler_close_connection(pubsub_tcpHandler_pt handle, char* url);
int pubsub_tcpHandler_listen(pubsub_tcpHandler_pt handle, char* url);

int pubsub_tcpHandler_create_receive_buffer_store(pubsub_tcpHandler_pt handle, unsigned int maxNofBuffers, unsigned int bufferSize);
void pubsub_tcpHandler_set_timeout(pubsub_tcpHandler_pt handle, unsigned int timeout);

int pubsub_tcpHandler_dataAvailable(pubsub_tcpHandler_pt handle, int fd, unsigned int *index, unsigned int *size);
int pubsub_tcpHandler_read(pubsub_tcpHandler_pt handle, unsigned int index, pubsub_tcp_msg_header_t** header, void ** buffer, unsigned int size);
int pubsub_tcpHandler_handler(pubsub_tcpHandler_pt handle);
int pubsub_tcpHandler_write(pubsub_tcpHandler_pt handle, pubsub_tcp_msg_header_t* header, void* buffer, unsigned int size, int flags);
int pubsub_tcpHandler_messageHandler(pubsub_tcpHandler_pt handle, void* payload, pubsub_tcpHandler_processMessage_callback_t processMessageCallback);

#endif /* _PUBSUB_TCP_BUFFER_HANDLER_H_ */
