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

#ifndef PUBSUB_PROTOCOL_JSON_WIRE_H_
#define PUBSUB_PROTOCOL_JSON_WIRE_H_

#include "pubsub_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PUBSUB_JSON_WIRE_PROTOCOL_TYPE "envelope-json"

typedef struct pubsub_protocol_json_wire pubsub_protocol_json_wire_t;

celix_status_t pubsubProtocol_json_wire_create(pubsub_protocol_json_wire_t **protocol);
celix_status_t pubsubProtocol_json_wire_destroy(pubsub_protocol_json_wire_t* protocol);

celix_status_t pubsubProtocol_json_wire_getHeaderSize(void *handle, size_t *length);
celix_status_t pubsubProtocol_json_wire_getHeaderBufferSize(void *handle, size_t *length);
celix_status_t pubsubProtocol_json_wire_getSyncHeaderSize(void *handle, size_t *length);
celix_status_t pubsubProtocol_json_wire_getSyncHeader(void* handle, void *syncHeader);
celix_status_t pubsubProtocol_json_wire_getFooterSize(void* handle,  size_t *length);
celix_status_t pubsubProtocol_json_wire_isMessageSegmentationSupported(void* handle, bool *isSupported);

celix_status_t pubsubProtocol_json_wire_encodeHeader(void *handle, pubsub_protocol_message_t *message, void **outBuffer, size_t *outLength);
celix_status_t pubsubProtocol_json_wire_encodeFooter(void *handle, pubsub_protocol_message_t *message, void **outBuffer, size_t *outLength);

celix_status_t pubsubProtocol_json_wire_decodeHeader(void* handle, void *data, size_t length, pubsub_protocol_message_t *message);
celix_status_t pubsubProtocol_json_wire_decodeFooter(void* handle, void *data, size_t length, pubsub_protocol_message_t *message);

celix_status_t pubsubProtocol_json_wire_encodePayload(void* handle, pubsub_protocol_message_t *message, void **outBuffer, size_t *outLength);
celix_status_t pubsubProtocol_json_wire_encodeMetadata(void* handle, pubsub_protocol_message_t *message, void **outBuffer, size_t *outLength);
celix_status_t pubsubProtocol_json_wire_decodePayload(void* handle, void *data, size_t length, pubsub_protocol_message_t *message);
celix_status_t pubsubProtocol_json_wire_decodeMetadata(void* handle, void *data, size_t length, pubsub_protocol_message_t *message);

#ifdef __cplusplus
}
#endif

#endif /* PUBSUB_PROTOCOL_JSON_WIRE_H_ */
