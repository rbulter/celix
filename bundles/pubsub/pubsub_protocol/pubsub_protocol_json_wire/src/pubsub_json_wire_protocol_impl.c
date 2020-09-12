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

#include <stdlib.h>
#include <string.h>

#include "celix_properties.h"

#include "pubsub_wire_v2_protocol_impl.h"
#include "pubsub_wire_protocol_common.h"
#include <jansson.h>

struct pubsub_protocol_json_wire {
};
celix_status_t pubsubProtocol_json_wire_create(pubsub_protocol_json_wire_t **protocol) {
    celix_status_t status = CELIX_SUCCESS;

    *protocol = calloc(1, sizeof(**protocol));

    if (!*protocol) {
        status = CELIX_ENOMEM;
    }
    else {}
    return status;
}

celix_status_t pubsubProtocol_json_wire_destroy(pubsub_protocol_json_wire_t* protocol) {
    celix_status_t status = CELIX_SUCCESS;
    free(protocol);
    return status;
}

celix_status_t pubsubProtocol_json_wire_getHeaderSize(void* handle, size_t *length) {
    *length = sizeof(int) * 9 + sizeof(short) * 2; // header + sync + version = 36
    return CELIX_SUCCESS;
}

celix_status_t pubsubProtocol_json_wire_getHeaderBufferSize(void* handle, size_t *length) {
    return 0;
}

celix_status_t pubsubProtocol_json_wire_getSyncHeaderSize(void* handle,  size_t *length) {
    *length = sizeof(int);
    return CELIX_SUCCESS;
}

celix_status_t pubsubProtocol_json_wire_getSyncHeader(void* handle, void *syncHeader) {
    pubsubProtocol_writeInt(syncHeader, 0, false, PROTOCOL_WIRE_V2_SYNC_HEADER);
    return CELIX_SUCCESS;
}

celix_status_t pubsubProtocol_json_wire_getFooterSize(void* handle,  size_t *length) {
    *length = 0;
    return CELIX_SUCCESS;
}

celix_status_t pubsubProtocol_json_wire_isMessageSegmentationSupported(void* handle, bool *isSupported) {
    *isSupported = false;
    return CELIX_SUCCESS;
}

celix_status_t pubsubProtocol_json_wire_encodeHeader(void *handle, pubsub_protocol_message_t *message, void **outBuffer, size_t *outLength) {
    celix_status_t status = CELIX_SUCCESS;
    json_error_t jsError;
    json_t *jsMsg = json_object();
    json_object_set_new_nocheck(jsMsg, "msgId", json_integer(message->header.msgId));
    json_object_set_new_nocheck(jsMsg, "seqNr", json_integer(message->header.seqNr));
    json_object_set_new_nocheck(jsMsg, "major", json_integer(message->header.msgMajorVersion));
    json_object_set_new_nocheck(jsMsg, "minor", json_integer(message->header.msgMinorVersion));
    json_object_set_new_nocheck(jsMsg, "payloadSize", json_integer(message->payload.length));

    json_t *jsData;
    jsData = json_loadb((const char *)message->payload.payload, message->payload.length - 1, 0, &jsError);
    if(jsData != NULL) {
        json_object_set_new_nocheck(jsMsg, "payload", jsData);
        *outBuffer = json_dumps(jsMsg, 0);
        *outLength = strlen(*outBuffer);
        json_decref(jsData); //Decrease ref count means freeing the object
    }
    json_decref(jsMsg); //Decrease ref count means freeing the object
    if (*outBuffer == NULL) {
        status = CELIX_ENOMEM;
    }
    return status;
}

celix_status_t pubsubProtocol_json_wire_encodeFooter(void *handle, pubsub_protocol_message_t *message, void **outBuffer, size_t *outLength) {
    *outBuffer = NULL;
    return pubsubProtocol_json_wire_getFooterSize(handle,  outLength);
}

celix_status_t pubsubProtocol_json_wire_decodeHeader(void* handle, void *data, size_t length, pubsub_protocol_message_t *message) {
    celix_status_t status = CELIX_SUCCESS;
    json_error_t error;
    json_t *jsMsg = json_loadb(data, length, 0, &error);
    if(jsMsg != NULL) {
        json_t *jsMsgId = json_object_get(jsMsg, "msgId");
        json_t *jsSeqNr = json_object_get(jsMsg, "seqNr");
        json_t *jsMajor = json_object_get(jsMsg, "major");
        json_t *jsMinor = json_object_get(jsMsg, "minor");
        json_t *jsPayloadSize = json_object_get(jsMsg, "payloadSize");

        if (jsMsgId && jsMajor && jsMinor && jsSeqNr && jsPayloadSize) {
            message->header.msgId = (uint32_t) json_integer_value(jsMsgId);
            message->header.seqNr = (uint32_t) json_integer_value(jsSeqNr);
            message->header.msgMajorVersion = (uint16_t) json_integer_value(jsMajor);
            message->header.msgMinorVersion = (uint16_t) json_integer_value(jsMinor);
            message->payload.length = (uint32_t) json_integer_value(jsSeqNr);
        } else {
            status = CELIX_ILLEGAL_ARGUMENT;
        }
    } else {
        status = CELIX_ILLEGAL_ARGUMENT;
    }
    return status;
}

celix_status_t pubsubProtocol_json_wire_decodeFooter(void* handle, void *data, size_t length, pubsub_protocol_message_t *message) {
    celix_status_t status = CELIX_SUCCESS;
    return status;
}

celix_status_t pubsubProtocol_json_wire_encodePayload(void* handle __attribute__((unused)), pubsub_protocol_message_t *message, void **outBuffer, size_t *outLength) {
    celix_status_t status = CELIX_SUCCESS;
    *outBuffer = NULL;
    *outLength = 0;
    return status;
}

celix_status_t pubsubProtocol_json_wire_encodeMetadata(void* handle __attribute__((unused)), pubsub_protocol_message_t *message __attribute__((unused)), void **outBuffer, size_t *outLength) {
    celix_status_t status = CELIX_SUCCESS;
    *outBuffer = NULL;
    *outLength = 0;
    return status;
}

celix_status_t pubsubProtocol_json_wire_decodePayload(void* handle __attribute__((unused)), void *data, size_t length, pubsub_protocol_message_t *message) {
    celix_status_t status = CELIX_SUCCESS;
    json_error_t error;
    json_t *jsMsg = json_loadb(data, length, 0, &error);
    if(jsMsg != NULL) {
        json_t *jsPayload = json_object_get(jsMsg, "payload");
        message->payload.payload = json_dumps(jsPayload, 0);
        message->payload.length = strlen(message->payload.payload);
    } else {
        status = CELIX_ILLEGAL_ARGUMENT;
    }
    return status;
}

celix_status_t pubsubProtocol_json_wire_decodeMetadata(void* handle __attribute__((unused)), void *data, size_t length, pubsub_protocol_message_t *message) {
    return pubsubProtocol_decodeMetadata(data, length, message);
}
