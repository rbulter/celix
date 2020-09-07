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

#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <pubsub/subscriber.h>
#include <memory.h>
#include <pubsub_constants.h>
#include <arpa/inet.h>
#include <celix_log_helper.h>
#include <pubsub_serializer.h>
#include <math.h>
#include "pubsub_websocket_topic_receiver.h"
#include "pubsub_psa_websocket_constants.h"
#include "pubsub_websocket_common.h"

#include <uuid/uuid.h>
#include <http_admin/api.h>
#include <jansson.h>
#include <pubsub_utils.h>
#include "pubsub_interceptors_handler.h"
#include <celix_api.h>
#include "pubsub_websocket_handler.h"

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37
#endif


#define L_DEBUG(...) \
    celix_logHelper_log(receiver->logHelper, CELIX_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define L_INFO(...) \
    celix_logHelper_log(receiver->logHelper, CELIX_LOG_LEVEL_INFO, __VA_ARGS__)
#define L_WARN(...) \
    celix_logHelper_log(receiver->logHelper, CELIX_LOG_LEVEL_WARNING, __VA_ARGS__)
#define L_ERROR(...) \
    celix_logHelper_log(receiver->logHelper, CELIX_LOG_LEVEL_ERROR, __VA_ARGS__)

typedef struct pubsub_websocket_rcv_buffer {
    celix_thread_mutex_t mutex;
    celix_array_list_t *list;     //List of received websocket messages (type: pubsub_websocket_msg_entry_t *)
} pubsub_websocket_rcv_buffer_t;

typedef struct pubsub_websocket_msg_entry {
    size_t msgSize;
    const char *msgData;
} pubsub_websocket_msg_entry_t;

struct pubsub_websocket_topic_receiver {
    celix_bundle_context_t *ctx;
    celix_log_helper_t *logHelper;
    long serializerSvcId;
    pubsub_serializer_service_t *serializer;
    long protocolSvcId;
    pubsub_protocol_service_t *protocol;
    char *scope;
    char *topic;
    pubsub_interceptors_handler_t *interceptorsHandler;
    char scopeAndTopicFilter[5];
    char *uri;
    celix_websocket_service_t sockSvc;
    long svcId;
    size_t timeout;
    pubsub_websocket_rcv_buffer_t recvBuffer;

    struct {
        celix_thread_t thread;
        celix_thread_mutex_t mutex;
        bool running;
    } thread;

    struct {
        celix_thread_mutex_t mutex;
        hash_map_t *map; //key = url (host:port), value = psa_websocket_requested_connection_entry_t*
        bool allConnected; //true if all requestedConnectection are connected
    } requestedConnections;

    long subscriberTrackerId;
    struct {
        celix_thread_mutex_t mutex;
        hash_map_t *map; //key = bnd id, value = psa_websocket_subscriber_entry_t
        bool allInitialized;
    } subscribers;
};

typedef struct psa_websocket_requested_connection_entry {
    pubsub_websocket_topic_receiver_t *parent;
    char *url;
    struct mg_connection *sockConnection;
    int connectRetryCount;
    bool connected;
    bool statically; //true if the connection is statically configured through the topic properties.
    bool passive; //true if the connection is initiated by another resource (e.g. webpage)
} psa_websocket_requested_connection_entry_t;

typedef struct psa_websocket_subscriber_entry {
    hash_map_t *msgTypes; //key = msg type id, value = pubsub_msg_serializer_t
    hash_map_t *subscriberServices; //key = servide id, value = pubsub_subscriber_t*
    bool initialized; //true if the init function is called through the receive thread
} psa_websocket_subscriber_entry_t;


static void pubsub_websocketTopicReceiver_addSubscriber(void *handle, void *svc, const celix_properties_t *props, const celix_bundle_t *owner);
static void pubsub_websocketTopicReceiver_removeSubscriber(void *handle, void *svc, const celix_properties_t *props, const celix_bundle_t *owner);
static void* psa_websocket_recvThread(void * data);
static void psa_websocket_connectToAllRequestedConnections(pubsub_websocket_topic_receiver_t *receiver);
static void psa_websocket_initializeAllSubscribers(pubsub_websocket_topic_receiver_t *receiver);
static bool psa_websocketTopicReceiver_checkVersion(version_pt msgVersion, uint16_t major, uint16_t minor);

pubsub_websocket_topic_receiver_t* pubsub_websocketTopicReceiver_create(celix_bundle_context_t *ctx,
                                                              celix_log_helper_t *logHelper,
                                                              const char *scope,
                                                              const char *topic,
                                                              const celix_properties_t *topicProperties,
                                                              pubsub_websocket_endPointStore_t *endPointStore,
                                                              long serializerSvcId,
                                                              pubsub_serializer_service_t *serializer,
                                                              long protocolSvcId,
                                                              pubsub_protocol_service_t *protocol) {
    pubsub_websocket_topic_receiver_t *receiver = calloc(1, sizeof(*receiver));
    receiver->ctx = ctx;
    receiver->logHelper = logHelper;
    receiver->serializerSvcId = serializerSvcId;
    receiver->serializer = serializer;
    receiver->protocolSvcId = protocolSvcId;
    receiver->protocol = protocol;
    receiver->scope = scope == NULL ? NULL : strndup(scope, 1024 * 1024);
    receiver->topic = strndup(topic, 1024 * 1024);
    psa_websocket_setScopeAndTopicFilter(scope, topic, receiver->scopeAndTopicFilter);

    pubsubInterceptorsHandler_create(ctx, scope, topic, &receiver->interceptorsHandler);
    receiver->uri = psa_websocket_createURI(scope, topic);

    if (receiver->uri != NULL) {
        celixThreadMutex_create(&receiver->subscribers.mutex, NULL);
        celixThreadMutex_create(&receiver->requestedConnections.mutex, NULL);
        celixThreadMutex_create(&receiver->thread.mutex, NULL);
        celixThreadMutex_create(&receiver->recvBuffer.mutex, NULL);

        receiver->subscribers.map = hashMap_create(NULL, NULL, NULL, NULL);
        receiver->requestedConnections.map = hashMap_create(utils_stringHash, NULL, utils_stringEquals, NULL);
        arrayList_create(&receiver->recvBuffer.list);
    }
    if (receiver->uri != NULL) {
        receiver->thread.running = true;
        celixThread_create(&receiver->thread.thread, NULL, psa_websocket_recvThread, receiver);
        char name[64];
        snprintf(name, 64, "WEBSOCKET TR %s/%s", scope == NULL ? "(null)" : scope, topic);
        celixThread_setName(&receiver->thread.thread, name);
    }

    //track subscribers
    if (receiver->uri != NULL) {
        int size = snprintf(NULL, 0, "(%s=%s)", PUBSUB_SUBSCRIBER_TOPIC, topic);
        char buf[size+1];
        snprintf(buf, (size_t)size+1, "(%s=%s)", PUBSUB_SUBSCRIBER_TOPIC, topic);
        celix_service_tracking_options_t opts = CELIX_EMPTY_SERVICE_TRACKING_OPTIONS;
        opts.filter.ignoreServiceLanguage = true;
        opts.filter.serviceName = PUBSUB_SUBSCRIBER_SERVICE_NAME;
        opts.filter.filter = buf;
        opts.callbackHandle = receiver;
        opts.addWithOwner = pubsub_websocketTopicReceiver_addSubscriber;
        opts.removeWithOwner = pubsub_websocketTopicReceiver_removeSubscriber;

        receiver->subscriberTrackerId = celix_bundleContext_trackServicesWithOptions(ctx, &opts);
    }

    //Register a websocket endpoint for this topic receiver
    if(receiver->uri != NULL){
        //Register a websocket svc first
        celix_properties_t *props = celix_properties_create();
        celix_properties_set(props, WEBSOCKET_ADMIN_URI, receiver->uri);
        receiver->sockSvc.handle = receiver;
        //Set callbacks to monitor any incoming connections (passive), data events or close events
        receiver->sockSvc.ready = psa_websocketTopicReceiver_ready;
        receiver->sockSvc.data = psa_websocketTopicReceiver_data;
        receiver->sockSvc.close = psa_websocketTopicReceiver_close;
        receiver->svcId = celix_bundleContext_registerService(receiver->ctx, &receiver->sockSvc,
                                                           WEBSOCKET_ADMIN_SERVICE_NAME, props);
    }

    const char *staticConnects = pubsub_getEnvironmentVariableWithScopeTopic(ctx, PUBSUB_WEBSOCKET_STATIC_CONNECT_SOCKET_ADDRESSES_FOR, topic, scope);
    if(staticConnects == NULL) {
//        staticConnects = celix_properties_get(topicProperties, PUBSUB_WEBSOCKET_STATIC_CONNECT_SOCKET_ADDRESSES, NULL);
    }
    if (staticConnects != NULL) {
        char *copy = strndup(staticConnects, 1024*1024);
        char* addr;
        char* save = copy;

        while ((addr = strtok_r(save, " ", &save))) {
            char *colon = strchr(addr, ':');
            if (colon == NULL) {
                continue;
            }

            char *sockAddr = NULL;
            asprintf(&sockAddr, "%.*s", (int)(colon - addr), addr);

            long sockPort = atol((colon + 1));

            char *key = NULL;
            asprintf(&key, "%s:%li", sockAddr, sockPort);


            if (sockPort > 0) {
                psa_websocket_requested_connection_entry_t *entry = calloc(1, sizeof(*entry));
                //entry->url = key;
                //entry->uri = strndup(receiver->uri, 1024 * 1024);
                //entry->socketAddress = sockAddr;
                //entry->socketPort = sockPort;
                entry->connected = false;
                entry->statically = true;
                entry->passive = false;
                hashMap_put(receiver->requestedConnections.map, (void *) entry->url, entry);
            } else {
                L_WARN("[PSA_WEBSOCKET_TR] Invalid static socket address %s", addr);
                free(key);
                free(sockAddr);
            }
        }
        free(copy);
    }




    if (receiver->uri == NULL) {
        if (receiver->scope != NULL) {
            free(receiver->scope);
        }
        free(receiver->topic);
        free(receiver);
        receiver = NULL;
        L_ERROR("[PSA_WEBSOCKET] Cannot create TopicReceiver for %s/%s", scope == NULL ? "(null)" : scope, topic);
    }
    return receiver;
}

void pubsub_websocketTopicReceiver_destroy(pubsub_websocket_topic_receiver_t *receiver) {
    if (receiver != NULL) {

        celixThreadMutex_lock(&receiver->thread.mutex);
        if (receiver->thread.running) {
            receiver->thread.running = false;
            celixThreadMutex_unlock(&receiver->thread.mutex);
            celixThread_join(receiver->thread.thread, NULL);
        }

        celix_bundleContext_stopTracker(receiver->ctx, receiver->subscriberTrackerId);

        celix_bundleContext_unregisterService(receiver->ctx, receiver->svcId);

        celixThreadMutex_lock(&receiver->subscribers.mutex);
        hash_map_iterator_t iter = hashMapIterator_construct(receiver->subscribers.map);
        while (hashMapIterator_hasNext(&iter)) {
            psa_websocket_subscriber_entry_t *entry = hashMapIterator_nextValue(&iter);
            if (entry != NULL)  {
                receiver->serializer->destroySerializerMap(receiver->serializer->handle, entry->msgTypes);
                hashMap_destroy(entry->subscriberServices, false, false);
                free(entry);
            }

        }
        hashMap_destroy(receiver->subscribers.map, false, false);


        celixThreadMutex_unlock(&receiver->subscribers.mutex);

        celixThreadMutex_lock(&receiver->requestedConnections.mutex);
        iter = hashMapIterator_construct(receiver->requestedConnections.map);
        while (hashMapIterator_hasNext(&iter)) {
            psa_websocket_requested_connection_entry_t *entry = hashMapIterator_nextValue(&iter);
            if (entry != NULL) {
                if(entry->connected) {
                    mg_close_connection(entry->sockConnection);
                }
                //free(entry->uri);
                //free(entry->socketAddress);
                //free(entry->key);
                //free(entry);
            }
        }
        hashMap_destroy(receiver->requestedConnections.map, false, false);
        celixThreadMutex_unlock(&receiver->requestedConnections.mutex);

        celixThreadMutex_destroy(&receiver->subscribers.mutex);
        celixThreadMutex_destroy(&receiver->requestedConnections.mutex);
        celixThreadMutex_destroy(&receiver->thread.mutex);

        celixThreadMutex_destroy(&receiver->recvBuffer.mutex);
        int msgBufSize = celix_arrayList_size(receiver->recvBuffer.list);
        while(msgBufSize > 0) {
            pubsub_websocket_msg_entry_t *msg = celix_arrayList_get(receiver->recvBuffer.list, msgBufSize - 1);
            free((void *) msg->msgData);
            free(msg);
            msgBufSize--;
        }
        celix_arrayList_destroy(receiver->recvBuffer.list);

        free(receiver->uri);
        pubsubInterceptorsHandler_destroy(receiver->interceptorsHandler);
        if (receiver->scope != NULL) {
            free(receiver->scope);
        }
        free(receiver->topic);
    }
    free(receiver);
}

const char* pubsub_websocketTopicReceiver_scope(pubsub_websocket_topic_receiver_t *receiver) {
    return receiver->scope;
}
const char* pubsub_websocketTopicReceiver_topic(pubsub_websocket_topic_receiver_t *receiver) {
    return receiver->topic;
}
const char* pubsub_websocketTopicReceiver_url(pubsub_websocket_topic_receiver_t *receiver) {
    return receiver->uri;
}

long pubsub_websocketTopicReceiver_serializerSvcId(pubsub_websocket_topic_receiver_t *receiver) {
    return receiver->serializerSvcId;
}

long pubsub_websocketTopicReceiver_protocolSvcId(pubsub_websocket_topic_receiver_t *receiver) {
    return receiver->protocolSvcId;
}

void pubsub_websocketTopicReceiver_listConnections(pubsub_websocket_topic_receiver_t *receiver, celix_array_list_t *connectedUrls, celix_array_list_t *unconnectedUrls) {
    celixThreadMutex_lock(&receiver->requestedConnections.mutex);
    hash_map_iterator_t iter = hashMapIterator_construct(receiver->requestedConnections.map);
    while (hashMapIterator_hasNext(&iter)) {
        psa_websocket_requested_connection_entry_t *entry = hashMapIterator_nextValue(&iter);
        char *url = NULL;
        asprintf(&url, "%s:%s%s", entry->url, entry->statically ? " (static)" : "", entry->passive ? " (passive)" : "");
        if (entry->connected) {
            celix_arrayList_add(connectedUrls, url);
        } else {
            celix_arrayList_add(unconnectedUrls, url);
        }
    }
    celixThreadMutex_unlock(&receiver->requestedConnections.mutex);
}


void pubsub_websocketTopicReceiver_connectTo(pubsub_websocket_topic_receiver_t *receiver, const char *url) {
    L_DEBUG("[PSA_WEBSOCKET] TopicReceiver %s/%s ('%s') connecting to websocket address %s", receiver->scope == NULL ? "(null)" : receiver->scope, receiver->topic, receiver->uri, url);

    L_DEBUG("[PSA_WEBSOCKET] TopicReceiver %s/%s connecting to websocket url %s", receiver->scope == NULL ? "(null)" : receiver->scope, receiver->topic, url);

    celixThreadMutex_lock(&receiver->requestedConnections.mutex);
    psa_websocket_requested_connection_entry_t *entry = hashMap_get(receiver->requestedConnections.map, url);
    if (entry == NULL) {
        entry = calloc(1, sizeof(*entry));
        entry->url = strndup(url, 1024 * 1024);
        entry->connected = false;
        entry->statically = false;
        entry->passive = false;
        entry->parent = receiver;
        hashMap_put(receiver->requestedConnections.map, (void*)entry->url, entry);
        receiver->requestedConnections.allConnected = false;
    }
    celixThreadMutex_unlock(&receiver->requestedConnections.mutex);

    psa_websocket_connectToAllRequestedConnections(receiver);
}

void pubsub_websocketTopicReceiver_disconnectFrom(pubsub_websocket_topic_receiver_t *receiver, const char *url) {
    L_DEBUG("[PSA_WEBSOCKET] TopicReceiver %s/%s disconnect from websocket url %s", receiver->scope == NULL ? "(null)" : receiver->scope, receiver->topic, url);

    celixThreadMutex_lock(&receiver->requestedConnections.mutex);

    psa_websocket_requested_connection_entry_t *entry = hashMap_remove(receiver->requestedConnections.map, url);
    if (entry != NULL && entry->connected) {
        mg_close_connection(entry->sockConnection);
    }
    if (entry != NULL) {
        free(entry->url);
        free(entry);
    }
    celixThreadMutex_unlock(&receiver->requestedConnections.mutex);
}

static void pubsub_websocketTopicReceiver_addSubscriber(void *handle, void *svc, const celix_properties_t *props, const celix_bundle_t *bnd) {
    pubsub_websocket_topic_receiver_t *receiver = handle;

    long bndId = celix_bundle_getId(bnd);
    long svcId = celix_properties_getAsLong(props, OSGI_FRAMEWORK_SERVICE_ID, -1);
    const char *subScope = celix_properties_get(props, PUBSUB_SUBSCRIBER_SCOPE, NULL);
    if (receiver->scope == NULL){
        if (subScope != NULL){
            return;
        }
    } else if (subScope != NULL) {
        if (strncmp(subScope, receiver->scope, strlen(receiver->scope)) != 0) {
            //not the same scope. ignore
            return;
        }
    } else {
        //receiver scope is not NULL, but subScope is NULL -> ignore
        return;
    }

    celixThreadMutex_lock(&receiver->subscribers.mutex);
    psa_websocket_subscriber_entry_t *entry = hashMap_get(receiver->subscribers.map, (void*)bndId);
    if (entry != NULL) {
        hashMap_put(entry->subscriberServices, (void*)svcId, svc);
    } else {
        //new create entry
        entry = calloc(1, sizeof(*entry));
        entry->subscriberServices = hashMap_create(NULL, NULL, NULL, NULL);
        entry->initialized = false;
        receiver->subscribers.allInitialized = false;

        hashMap_put(entry->subscriberServices, (void*)svcId, svc);

        int rc = receiver->serializer->createSerializerMap(receiver->serializer->handle, (celix_bundle_t*)bnd, &entry->msgTypes);

        if (rc == 0) {
            hashMap_put(receiver->subscribers.map, (void*)bndId, entry);
        } else {
            L_ERROR("[PSA_WEBSOCKET] Cannot create msg serializer map for TopicReceiver %s/%s", receiver->scope == NULL ? "(null)" : receiver->scope, receiver->topic);
            free(entry);
        }
    }
    celixThreadMutex_unlock(&receiver->subscribers.mutex);
}

static void pubsub_websocketTopicReceiver_removeSubscriber(void *handle, void *svc __attribute__((unused)), const celix_properties_t *props __attribute__((unused)), const celix_bundle_t *bnd) {
    pubsub_websocket_topic_receiver_t *receiver = handle;

    long bndId = celix_bundle_getId(bnd);
    long svcId = celix_properties_getAsLong(props, OSGI_FRAMEWORK_SERVICE_ID, -1);

    celixThreadMutex_lock(&receiver->subscribers.mutex);
    psa_websocket_subscriber_entry_t *entry = hashMap_get(receiver->subscribers.map, (void*)bndId);
    if (entry != NULL) {
        hashMap_remove(entry->subscriberServices, (void*)svcId);
    }
    if (entry != NULL && hashMap_size(entry->subscriberServices) == 0) {
        //remove entry
        hashMap_remove(receiver->subscribers.map, (void*)bndId);
        int rc = receiver->serializer->destroySerializerMap(receiver->serializer->handle, entry->msgTypes);
        if (rc != 0) {
            L_ERROR("[PSA_WEBSOCKET] Cannot destroy msg serializers map for TopicReceiver %s/%s", receiver->scope == NULL ? "(null)" : receiver->scope, receiver->topic);
        }
        hashMap_destroy(entry->subscriberServices, false, false);
        free(entry);
    }
    celixThreadMutex_unlock(&receiver->subscribers.mutex);
}

static inline void
processMsgForSubscriberEntry(pubsub_websocket_topic_receiver_t *receiver, psa_websocket_subscriber_entry_t *entry,
                             const pubsub_protocol_message_t *message, bool *releaseMsg) {
    //NOTE receiver->subscribers.mutex locked
    pubsub_msg_serializer_t *msgSer = hashMap_get(entry->msgTypes, (void *) (uintptr_t) (message->header.msgId));

    if (msgSer != NULL) {
        void *deSerializedMsg = NULL;
        bool validVersion = psa_websocketTopicReceiver_checkVersion(msgSer->msgVersion, message->header.msgMajorVersion,
                                                                    message->header.msgMinorVersion);
        if (validVersion) {
            struct iovec deSerializeBuffer;
            deSerializeBuffer.iov_base = message->payload.payload;
            deSerializeBuffer.iov_len = message->payload.length;
            celix_status_t status = msgSer->deserialize(msgSer->handle, &deSerializeBuffer, 1, &deSerializedMsg);
            // When received payload pointer is the same as deserializedMsg, set ownership of pointer to topic receiver
            if (message->payload.payload == deSerializedMsg) {
                *releaseMsg = true;
            }

            if (status == CELIX_SUCCESS) {
                const char *msgType = msgSer->msgName;
                uint32_t msgId = message->header.msgId;
                celix_properties_t *metadata = message->metadata.metadata;
                bool cont = pubsubInterceptorHandler_invokePreReceive(receiver->interceptorsHandler, msgType, msgId, deSerializedMsg, &metadata);
                bool release = true;
                if (cont) {
                    hash_map_iterator_t iter = hashMapIterator_construct(entry->subscriberServices);
                    while (hashMapIterator_hasNext(&iter)) {
                        pubsub_subscriber_t *svc = hashMapIterator_nextValue(&iter);
                        svc->receive(svc->handle, msgSer->msgName, msgSer->msgId, deSerializedMsg, message->metadata.metadata, &release);
                        pubsubInterceptorHandler_invokePostReceive(receiver->interceptorsHandler, msgType, msgId, deSerializedMsg, metadata);
                        if (!release && hashMapIterator_hasNext(&iter)) {
                            //receive function has taken ownership and still more receive function to come ..
                            //deserialize again for new message
                            status = msgSer->deserialize(msgSer->handle, &deSerializeBuffer, 1, &deSerializedMsg);
                            if (status != CELIX_SUCCESS) {
                                L_WARN("[PSA_WEBSOCKET_TR] Cannot deserialize msg type %s for scope/topic %s/%s",
                                       msgSer->msgName,
                                       receiver->scope == NULL ? "(null)" : receiver->scope,
                                       receiver->topic);
                                break;
                            }
                            release = true;
                        }
                    }
                    if (release) {
                        msgSer->freeDeserializeMsg(msgSer->handle, deSerializedMsg);
                    }
                    if (message->metadata.metadata) {
                        celix_properties_destroy(message->metadata.metadata);
                    }
                }
            } else {
                L_WARN("[PSA_WEBSOCKET_TR] Cannot deserialize msg type %s for scope/topic %s/%s", msgSer->msgName,
                       receiver->scope == NULL ? "(null)" : receiver->scope, receiver->topic);
            }
        }
    } else {
        L_WARN("[PSA_WEBSOCKET_TR] Cannot find serializer for type id 0x%X. Received payload size is %u.", message->header.msgId, message->payload.length);
    }
}



static inline void processMsg(void *handle, const pubsub_protocol_message_t *message, bool *release) {
    pubsub_websocket_topic_receiver_t *receiver = handle;
    celixThreadMutex_lock(&receiver->subscribers.mutex);
    hash_map_iterator_t iter = hashMapIterator_construct(receiver->subscribers.map);
    while (hashMapIterator_hasNext(&iter)) {
        psa_websocket_subscriber_entry_t *entry = hashMapIterator_nextValue(&iter);
        if (entry != NULL) {
            processMsgForSubscriberEntry(receiver, entry, message, release);
        }
    }
    celixThreadMutex_unlock(&receiver->subscribers.mutex);
}

static void *psa_websocket_recvThread(void *data) {
    pubsub_websocket_topic_receiver_t *receiver = data;

    celixThreadMutex_lock(&receiver->thread.mutex);
    bool running = receiver->thread.running;
    celixThreadMutex_unlock(&receiver->thread.mutex);

    celixThreadMutex_lock(&receiver->requestedConnections.mutex);
    bool allConnected = receiver->requestedConnections.allConnected;
    celixThreadMutex_unlock(&receiver->requestedConnections.mutex);

    celixThreadMutex_lock(&receiver->subscribers.mutex);
    bool allInitialized = receiver->subscribers.allInitialized;
    celixThreadMutex_unlock(&receiver->subscribers.mutex);

    while (running) {
        if (!allConnected) {
            psa_websocket_connectToAllRequestedConnections(receiver);
        }
        if (!allInitialized) {
            psa_websocket_initializeAllSubscribers(receiver);
        }
        usleep(receiver->timeout);

        celixThreadMutex_lock(&receiver->thread.mutex);
        running = receiver->thread.running;
        celixThreadMutex_unlock(&receiver->thread.mutex);

        celixThreadMutex_lock(&receiver->requestedConnections.mutex);
        allConnected = receiver->requestedConnections.allConnected;
        celixThreadMutex_unlock(&receiver->requestedConnections.mutex);

        celixThreadMutex_lock(&receiver->subscribers.mutex);
        allInitialized = receiver->subscribers.allInitialized;
        celixThreadMutex_unlock(&receiver->subscribers.mutex);
    } // while
    return NULL;
}


#if NOT
static void psa_websocketTopicReceiver_ready(struct mg_connection *connection, void *handle) {
    if (handle != NULL) {
        pubsub_websocket_topic_receiver_t *receiver = (pubsub_websocket_topic_receiver_t *) handle;

        //Get request info with host, port and uri information
        const struct mg_request_info *ri = mg_get_request_info(connection);
        if (ri != NULL && strcmp(receiver->uri, ri->request_uri) == 0) {
            char *key = NULL;
            asprintf(&key, "%s:%i", ri->remote_addr, ri->remote_port);

            celixThreadMutex_lock(&receiver->requestedConnections.mutex);
            psa_websocket_requested_connection_entry_t *entry = hashMap_get(receiver->requestedConnections.map, key);
            if (entry == NULL) {
                entry = calloc(1, sizeof(*entry));
                entry->key = key;
                entry->uri = strndup(ri->request_uri, 1024 * 1024);
                entry->socketAddress = strndup(ri->remote_addr, 1024 * 1024);
                entry->socketPort = ri->remote_port;
                entry->connected = true;
                entry->statically = false;
                entry->passive = true;
                hashMap_put(receiver->requestedConnections.map, (void *) entry->key, entry);
                receiver->requestedConnections.allConnected = false;
            } else {
                free(key);
            }

            celixThreadMutex_unlock(&receiver->requestedConnections.mutex);
        }
    }
}


static int psa_websocketTopicReceiver_data(struct mg_connection *connection __attribute__((unused)),
                                            int op_code __attribute__((unused)),
                                            char *data,
                                            size_t length,
                                            void *handle) {
    //Received a websocket message, append this message to the buffer of the receiver.
    if (handle != NULL) {
        pubsub_websocket_topic_receiver_t *receiver = (pubsub_websocket_topic_receiver_t *) handle;

        celixThreadMutex_lock(&receiver->recvBuffer.mutex);
        pubsub_websocket_msg_entry_t *msg = malloc(sizeof(*msg));
        const char *rcvdMsgData = malloc(length);
        memcpy((void *) rcvdMsgData, data, length);
        msg->msgData = rcvdMsgData;
        msg->msgSize = length;
        celix_arrayList_add(receiver->recvBuffer.list, msg);
        celixThreadMutex_unlock(&receiver->recvBuffer.mutex);
    }

    return 1; //keep open (non-zero), 0 to close the socket
}

static void psa_websocketTopicReceiver_close(const struct mg_connection *connection, void *handle) {
    //Reset connection for this receiver entry
    if (handle != NULL) {
        pubsub_websocket_topic_receiver_t *receiver = (pubsub_websocket_topic_receiver_t *) handle;

        //Get request info with host, port and uri information
        const struct mg_request_info *ri = mg_get_request_info(connection);
        if (ri != NULL && strcmp(receiver->uri, ri->request_uri) == 0) {
            char *key = NULL;
            asprintf(&key, "%s:%i", ri->remote_addr, ri->remote_port);

            celixThreadMutex_lock(&receiver->requestedConnections.mutex);
            psa_websocket_requested_connection_entry_t *entry = hashMap_get(receiver->requestedConnections.map, key);
            if (entry != NULL) {
                entry->connected = false;
                entry->sockConnection = NULL;
                if(entry->passive) {
                    hashMap_remove(receiver->requestedConnections.map, key);
                    free(entry->key);
                    free(entry->uri);
                    free(entry->socketAddress);
                    free(entry);
                }
            }
            celixThreadMutex_unlock(&receiver->requestedConnections.mutex);
            free(key);
        }
    }
}
#endif

static void psa_websocket_connectToAllRequestedConnections(pubsub_websocket_topic_receiver_t *receiver) {
    celixThreadMutex_lock(&receiver->requestedConnections.mutex);
    if (!receiver->requestedConnections.allConnected) {
        bool allConnected = true;
        hash_map_iterator_t iter = hashMapIterator_construct(receiver->requestedConnections.map);
        while (hashMapIterator_hasNext(&iter)) {
            psa_websocket_requested_connection_entry_t *entry = hashMapIterator_nextValue(&iter);
            if ((entry) && (!entry->connected)) {
                int rc = pubsub_websocket_handler_connect(entry->parent->socketHandler, entry->url);
                if (rc < 0) {
                    allConnected = false;
                }
            }
        }
        receiver->requestedConnections.allConnected = allConnected;
    }
    celixThreadMutex_unlock(&receiver->requestedConnections.mutex);
}

static void psa_tcp_connectHandler(void *handle, const char *url, bool lock) {
    pubsub_websocket_topic_receiver_t *receiver = handle;
    L_DEBUG("[PSA_TCP] TopicReceiver %s/%s connecting to websocket url %s",
            receiver->scope == NULL ? "(null)" : receiver->scope,
            receiver->topic,
            url);
    if (lock)
        celixThreadMutex_lock(&receiver->requestedConnections.mutex);
    psa_websocket_requested_connection_entry_t *entry = hashMap_get(receiver->requestedConnections.map, url);
    if (entry == NULL) {
        entry = calloc(1, sizeof(*entry));
        entry->parent = receiver;
        entry->url = strndup(url, 1024 * 1024);
        entry->statically = true;
        hashMap_put(receiver->requestedConnections.map, (void *) entry->url, entry);
        receiver->requestedConnections.allConnected = false;
    }
    entry->connected = true;
    if (lock)
        celixThreadMutex_unlock(&receiver->requestedConnections.mutex);
}

static void psa_tcp_disConnectHandler(void *handle, const char *url, bool lock) {
    pubsub_websocket_topic_receiver_t *receiver = handle;
    L_DEBUG("[PSA TCP] TopicReceiver %s/%s disconnect from websocket url %s",
            receiver->scope == NULL ? "(null)" : receiver->scope,
            receiver->topic,
            url);
    if (lock)
        celixThreadMutex_lock(&receiver->requestedConnections.mutex);
    psa_websocket_requested_connection_entry_t *entry = hashMap_get(receiver->requestedConnections.map, url);
    if (entry != NULL) {
        entry->connected = false;
        receiver->requestedConnections.allConnected = false;
    }
    if (lock)
        celixThreadMutex_unlock(&receiver->requestedConnections.mutex);
}


static void psa_websocket_initializeAllSubscribers(pubsub_websocket_topic_receiver_t *receiver) {
    celixThreadMutex_lock(&receiver->subscribers.mutex);
    if (!receiver->subscribers.allInitialized) {
        bool allInitialized = true;
        hash_map_iterator_t iter = hashMapIterator_construct(receiver->subscribers.map);
        while (hashMapIterator_hasNext(&iter)) {
            psa_websocket_subscriber_entry_t *entry = hashMapIterator_nextValue(&iter);
            if (!entry->initialized) {
                hash_map_iterator_t iter2 = hashMapIterator_construct(entry->subscriberServices);
                while (hashMapIterator_hasNext(&iter2)) {
                    pubsub_subscriber_t *svc = hashMapIterator_nextValue(&iter2);
                    int rc = 0;
                    if (svc != NULL && svc->init != NULL) {
                        rc = svc->init(svc->handle);
                    }
                    if (rc == 0) {
                        //note now only initialized on first subscriber entries added.
                        entry->initialized = true;
                    } else {
                        L_WARN("Cannot initialize subscriber svc. Got rc %i", rc);
                        allInitialized = false;
                    }
                }
            }
        }
        receiver->subscribers.allInitialized = allInitialized;
    }
    celixThreadMutex_unlock(&receiver->subscribers.mutex);
}

static bool psa_websocketTopicReceiver_checkVersion(version_pt msgVersion, uint16_t major, uint16_t minor) {
    bool check = false;

    if (major == 0 && minor == 0) {
        //no check
        return true;
    }

    int versionMajor;
    int versionMinor;
    if (msgVersion!=NULL) {
        version_getMajor(msgVersion, &versionMajor);
        version_getMinor(msgVersion, &versionMinor);
        if (major==((unsigned char)versionMajor)) { /* Different major means incompatible */
            check = (minor>=((unsigned char)versionMinor)); /* Compatible only if the provider has a minor equals or greater (means compatible update) */
        }
    }

    return check;
}