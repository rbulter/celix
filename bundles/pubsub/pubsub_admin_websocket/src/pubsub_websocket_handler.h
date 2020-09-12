/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *  KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */


#ifndef CELIX_PUBSUB_WEBSOCKET_HANDLER_H
#define CELIX_PUBSUB_WEBSOCKET_HANDLER_H

#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <celix_log_helper.h>
#include "celix_threads.h"
#include "pubsub_utils_url.h"
#include <pubsub_protocol.h>

#ifndef MAX
#define MAX(a, b) ((a>b) ? (a) : (b))
#endif

typedef struct pubsub_websocketHandler pubsub_websocketHandler_t;
typedef void(*pubsub_websocket_process_message_callback_t)
    (void *payload, const pubsub_protocol_message_t *header, bool *release);
typedef void (*pubsub_websocket_receiver_connect_message_callback_t)(void *payload, const char *url, bool lock);

pubsub_websocketHandler_t *pubsub_websocketHandler_create(celix_bundle_context_t *ctx, pubsub_protocol_service_t *protocol, celix_log_helper_t *logHelper);
void pubsub_websocketHandler_destroy(pubsub_websocketHandler_t *handle);
int pubsub_websocketHandler_connect(pubsub_websocketHandler_t *handle, char *url);
int pubsub_websocketHandler_disconnect(pubsub_websocketHandler_t *handle, char *url);
int pubsub_websocketHandler_listen(pubsub_websocketHandler_t *handle, char *url);
int pubsub_websocketHandler_write(pubsub_websocketHandler_t *handle, pubsub_protocol_message_t *message);

int pubsub_websocketHandler_addMessageHandler(pubsub_websocketHandler_t *handle,
                                               void *payload,
                                               pubsub_websocket_process_message_callback_t processMessageCallback);
int pubsub_websocketHandler_addReceiverConnectionCallback(pubsub_websocketHandler_t *handle,
                                                           void *payload,
                                                           pubsub_websocket_receiver_connect_message_callback_t connectMessageCallback,
                                                           pubsub_websocket_receiver_connect_message_callback_t disconnectMessageCallback);

#endif /* CELIX_PUBSUB_WEBSOCKET_HANDLER_H */
