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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "http_admin/api.h"
#include "civetweb.h"
#include "pubsub_websocket_handler.h"
#include "hash_map.h"
#include "utils.h"


#define L_DEBUG(...) \
    celix_logHelper_log(handle->logHelper, CELIX_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define L_INFO(...) \
    celix_logHelper_log(handle->logHelper, CELIX_LOG_LEVEL_INFO, __VA_ARGS__)
#define L_WARN(...) \
    celix_logHelper_log(handle->logHelper, CELIX_LOG_LEVEL_WARNING, __VA_ARGS__)
#define L_ERROR(...) \
    celix_logHelper_log(handle->logHelper, CELIX_LOG_LEVEL_ERROR, __VA_ARGS__)

static void pubsub_websocketHandler_ready(struct mg_connection *connection, void *handler);
static void pubsub_websocketHandler_close(const struct mg_connection *connection, void *handler);
static int pubsub_websocketHandler_data(struct mg_connection *connection, int op_code, char *data, size_t length, void *handle);

//
// Entry administration
//
typedef struct psa_websocket_connection_entry {
  char *interface_url;
  char *url;
  bool connected;
  bool statically; //true if the connection is statically configured through the topic properties.
  bool passive; //true if the connection is initiated by another resource (e.g. webpage)
  struct mg_connection *sockConnection;
  int connectRetryCount;
  pubsub_protocol_message_t header;
  celix_thread_mutex_t writeMutex;
  celix_thread_mutex_t readMutex;
  pubsub_websocketHandler_t* handle;
} psa_tcp_connection_entry_t;


//
// Handle administration
//
struct pubsub_websocketHandler {
  celix_thread_rwlock_t dbLock;
  hash_map_t *connection_map;
  celix_bundle_context_t *ctx;
  celix_log_helper_t *logHelper;
  pubsub_protocol_service_t *protocol;
  celix_websocket_service_t websockSvc;
  struct mg_connection *sockConnection;
  size_t websockSvcId;
  celix_thread_t thread;
  bool running;
  char* writeBuffer;
  size_t writeBufferSize;
  pubsub_websocket_receiver_connect_message_callback_t receiverConnectMessageCallback;
  pubsub_websocket_receiver_connect_message_callback_t receiverDisconnectMessageCallback;
  void *receiverConnectPayload;
  pubsub_websocket_process_message_callback_t processMessageCallback;
  void *processMessagePayload;
};


//
// Create a handle
//
pubsub_websocketHandler_t *pubsub_websocketHandler_create(celix_bundle_context_t *ctx, pubsub_protocol_service_t *protocol, celix_log_helper_t *logHelper) {
    pubsub_websocketHandler_t *handle = calloc(sizeof(*handle), 1);
    if (handle != NULL) {
        handle->ctx = ctx;
        handle->connection_map = hashMap_create(utils_stringHash, NULL, utils_stringEquals, NULL);
        handle->logHelper = logHelper;
        handle->protocol = protocol;
        celixThreadRwlock_create(&handle->dbLock, 0);
        celixThread_create(&handle->thread, NULL, NULL, handle);
    }
    return handle;
}

//
// Destroys the handle
//
void pubsub_websocketHandler_destroy(pubsub_websocketHandler_t *handle) {
    if (handle != NULL) {
        celixThreadRwlock_readLock(&handle->dbLock);
        bool running = handle->running;
        celixThreadRwlock_unlock(&handle->dbLock);
        if (running) {
            celixThreadRwlock_writeLock(&handle->dbLock);
            handle->running = false;
            celixThreadRwlock_unlock(&handle->dbLock);
            celixThread_join(handle->thread, NULL);
        }
        free(handle->writeBuffer);
    }
}

//
// Create connection/interface entry
//
static inline psa_tcp_connection_entry_t *
pubsub_websocketHandler_createEntry(pubsub_websocketHandler_t *handle, char *url) {
    psa_tcp_connection_entry_t *entry = NULL;
    entry = calloc(sizeof(psa_tcp_connection_entry_t), 1);
    celixThreadMutex_create(&entry->writeMutex, NULL);
    celixThreadMutex_create(&entry->readMutex, NULL);
    if (url) {
        entry->url = strndup(url, 1024 * 1024);
    }
    entry->connected = false;
    entry->handle = handle;
    return entry;
}

//
// Free connection/interface entry
//
static inline void
pubsub_websocketHandler_freeEntry(psa_tcp_connection_entry_t *entry) {
    if (entry) {
        if (entry->url) free(entry->url);
        if (entry->interface_url) free(entry->interface_url);
        celixThreadMutex_destroy(&entry->writeMutex);
        celixThreadMutex_destroy(&entry->readMutex);
        free(entry);
    }
}

//
// setup listening to interface (sender) using an url
//
int pubsub_websocketHandler_listen(pubsub_websocketHandler_t *handle, char *uri) {
    int rc = 0;
    celixThreadRwlock_readLock(&handle->dbLock);
    if (uri != NULL) {
        celix_properties_t *props = celix_properties_create();
        celix_properties_set(props, WEBSOCKET_ADMIN_URI, uri);

        handle->websockSvc.handle = handle;
        handle->websockSvc.ready  = pubsub_websocketHandler_ready;
        handle->websockSvc.close  = pubsub_websocketHandler_close;
        handle->websockSvcId  = celix_bundleContext_registerService(handle->ctx, &handle->websockSvc, WEBSOCKET_ADMIN_SERVICE_NAME, props);
    } else {
        handle->websockSvcId = -1;
    }
    celixThreadRwlock_unlock(&handle->dbLock);
    return rc;
}

//
// Releases the Buffer
//
static inline void
pubsub_websocketHandler_releaseEntryBuffer(pubsub_websocketHandler_t *handle, int fd, unsigned int index __attribute__((unused))) {
    psa_tcp_connection_entry_t *entry = hashMap_get(handle->connection_map, (void *) (intptr_t) fd);
    if (entry != NULL) {
        //entry->buffer = NULL;
        //entry->bufferSize = 0;
    }
}

//
// Connect to url (receiver)
//
int pubsub_websocketHandler_connect(pubsub_websocketHandler_t *handle, char *url) {
    int rc = 0;
    psa_tcp_connection_entry_t *entry = hashMap_get(handle->connection_map, (void *) (intptr_t) url);
    if (entry == NULL) {
        char errBuf[1000] = {0};
        pubsub_utils_url_t *url_info = pubsub_utils_url_parse(url);
        pubsub_websocketHandler_createEntry(handle, url );
        entry->sockConnection = mg_connect_websocket_client(url_info->hostname,
                                                            (int) url_info->port_nr,
                                                            0, // No ssl
                                                            errBuf,
                                                            (size_t) sizeof(errBuf),
                                                            url_info->uri,
                                                            NULL,
                                                            pubsub_websocketHandler_data,
                                                            pubsub_websocketHandler_close,
                                                            entry);
        if(entry->sockConnection != NULL) {
            celixThreadRwlock_writeLock(&handle->dbLock);
            hashMap_put(handle->connection_map, entry->url, entry);
            if (handle->receiverConnectMessageCallback)
                handle->receiverConnectMessageCallback(handle->receiverConnectPayload, entry->url, false);
            entry->connected = true;
            L_INFO("[TCP Socket] Connect to %s using; %s\n", entry->url, entry->interface_url);
        }
        pubsub_utils_url_free(url_info);
    }
    return rc;
}


//
// Write large data to TCP. .
//
int pubsub_websocketHandler_write(pubsub_websocketHandler_t *handle, pubsub_protocol_message_t *message) {
    int result = 0;
    if (handle) {
        celixThreadRwlock_readLock(&handle->dbLock);
        size_t headerSize = 0;
        size_t headerBufferSize = 0;
        size_t footerSize = 0;
        handle->protocol->getHeaderSize(handle->protocol->handle, &headerSize);
        handle->protocol->getHeaderBufferSize(handle->protocol->handle, &headerBufferSize);
        handle->protocol->getFooterSize(handle->protocol->handle, &footerSize);

        size_t size = (headerBufferSize + message->payload.length + footerSize) * 4;
        // Message buffer is to small, reallocate to make it bigger
        if ((!handle->writeBuffer) && (size > handle->writeBufferSize)) {
            handle->writeBufferSize = size;
            char *buffer = realloc(handle->writeBuffer, (size_t) handle->writeBufferSize);
            if (buffer) {
                handle->writeBuffer = buffer;
            }
        }

        void *payloadData = NULL;
        size_t payloadSize = message->payload.length;
        if (message->payload.length) {
            payloadData = &handle->writeBuffer[headerBufferSize];
            handle->protocol->encodePayload(handle->protocol->handle, message, &payloadData, &payloadSize);
        }
        message->header.convertEndianess = 0;
        message->header.payloadSize = payloadSize;
        message->header.payloadPartSize = payloadSize;
        message->header.payloadOffset = 0;
        message->header.isLastSegment = 1;

        void *metadataData = NULL;
        size_t metadataSize = 0;
        if (message->metadata.metadata) {
            metadataData = &handle->writeBuffer[headerBufferSize + payloadSize];
            handle->protocol->encodeMetadata(handle->protocol->handle, message,
                                             &metadataData,
                                             &metadataSize);
        }
        message->header.metadataSize = metadataSize;

        void *footerData = NULL;
        if (footerSize) {
            footerData = &handle->writeBuffer[headerBufferSize + payloadSize + metadataSize] ;
            handle->protocol->encodeFooter(handle->protocol->handle, message,
                                           &footerData,
                                           &footerSize);
        }

        void *headerData = NULL;
        if (headerBufferSize) {
            headerData = handle->writeBuffer;
            // Encode the header, with payload size and metadata size
            handle->protocol->encodeHeader(handle->protocol->handle, message,
                                           &headerData,
                                           &headerSize);
        }

        size_t msgSize = payloadSize + metadataSize;
        if (msgSize) {
            int nbytes = 0;
            msgSize += headerBufferSize + footerSize;
            hash_map_iterator_t iter = hashMapIterator_construct(handle->connection_map);
            while (hashMapIterator_hasNext(&iter)) {
                psa_tcp_connection_entry_t *entry = hashMapIterator_nextValue(&iter);
                if (!entry->connected) continue;
                celixThreadMutex_lock(&entry->writeMutex);
                if (metadataData || payloadData) {
                    nbytes = mg_websocket_write(entry->sockConnection, MG_WEBSOCKET_OPCODE_BINARY, handle->writeBuffer, msgSize);
                } else {
                    nbytes = mg_websocket_write(entry->sockConnection, MG_WEBSOCKET_OPCODE_TEXT, handle->writeBuffer,msgSize);
                }
                if (nbytes != (int) msgSize) {
                    L_WARN("[PSA_WEBSOCKET_TS] Error sending websocket, written %d of total %lu bytes", nbytes, msgSize);
                }
                celixThreadMutex_unlock(&entry->writeMutex);
            }
        }
        celixThreadRwlock_unlock(&handle->dbLock);
    }
    return result;
}


int pubsub_websocketHandler_addMessageHandler(pubsub_websocketHandler_t *handle, void *payload,
                                        pubsub_websocket_process_message_callback_t processMessageCallback) {
    int result = 0;
    celixThreadRwlock_writeLock(&handle->dbLock);
    handle->processMessageCallback = processMessageCallback;
    handle->processMessagePayload = payload;
    celixThreadRwlock_unlock(&handle->dbLock);
    return result;
}

int pubsub_websocketHandler_addReceiverConnectionCallback(pubsub_websocketHandler_t *handle, void *payload,
                                                          pubsub_websocket_receiver_connect_message_callback_t connectMessageCallback,
                                                          pubsub_websocket_receiver_connect_message_callback_t disconnectMessageCallback) {
    int result = 0;
    celixThreadRwlock_writeLock(&handle->dbLock);
    handle->receiverConnectMessageCallback = connectMessageCallback;
    handle->receiverDisconnectMessageCallback = disconnectMessageCallback;
    handle->receiverConnectPayload = payload;
    celixThreadRwlock_unlock(&handle->dbLock);
    return result;
}

static void pubsub_websocketHandler_ready(struct mg_connection *connection, void *handler) {
    //Connection succeeded so save connection to use for sending the messages
    if (handler != NULL) {
        pubsub_websocketHandler_t *handle = (pubsub_websocketHandler_t *) handler;
        handle->sockConnection = connection;
    }
}

static void pubsub_websocketHandler_close(const struct mg_connection *connection __attribute__((unused)), void *handler) {
    //Connection closed so reset connection
    if (handler != NULL) {
        psa_tcp_connection_entry_t *entry = (psa_tcp_connection_entry_t *) handler;
        pubsub_websocketHandler_t *handle = entry->handle;
        entry->sockConnection = NULL;
        celixThreadRwlock_writeLock(&handle->dbLock);
        hashMap_remove(handle->connection_map, entry->url);
        celixThreadRwlock_unlock(&handle->dbLock);
        pubsub_websocketHandler_freeEntry(entry);
    }
}

static int pubsub_websocketHandler_data(struct mg_connection *connection __attribute__((unused)),
                                           int op_code __attribute__((unused)),
                                           char *data,
                                           size_t length,
                                           void *handler) {
    if (handler != NULL) return  1;
    psa_tcp_connection_entry_t *entry = (psa_tcp_connection_entry_t *) handler;
    pubsub_websocketHandler_t *handle = entry->handle;
    size_t headerSize = 0;
    size_t headerBufferSize = 0;
    size_t footerSize = 0;
    handle->protocol->getHeaderSize(handle->protocol->handle, &headerSize);
    handle->protocol->getHeaderBufferSize(handle->protocol->handle, &headerBufferSize);
    handle->protocol->getFooterSize(handle->protocol->handle, &footerSize);

    //Received a websocket message, append this message to the buffer of the receiver.
    if ((data != NULL) && (length > headerSize)) {
        if (handle->protocol->decodeHeader(handle->protocol->handle, data, headerSize, &entry->header) != CELIX_SUCCESS) {
            L_WARN("[TCP Socket] Failed to decode message header  (url: %s)",  entry->url);
            return 1;
        }
        if (entry->header.header.payloadSize > 0) {
            handle->protocol->decodePayload(handle->protocol->handle, &data[headerBufferSize], entry->header.header.payloadSize, &entry->header);
        }
        if (entry->header.header.metadataSize > 0) {
            handle->protocol->decodeMetadata(handle->protocol->handle, &data[headerBufferSize + entry->header.header.payloadSize], entry->header.header.metadataSize, &entry->header);
        }
        if (handle->processMessageCallback && entry->header.payload.payload != NULL && entry->header.payload.length) {
            bool releaseEntryBuffer = false;
            handle->processMessageCallback(handle->processMessagePayload, &entry->header, &releaseEntryBuffer);
            //if (releaseEntryBuffer) pubsub_websocketHandler_releaseEntryBuffer(handle, entry->fd, 0);
        }
    }
    return 1; //keep open (non-zero), 0 to close the socket
}