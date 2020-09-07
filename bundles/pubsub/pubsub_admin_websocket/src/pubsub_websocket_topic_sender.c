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

#include <pubsub_serializer.h>
#include <stdlib.h>
#include <memory.h>
#include <pubsub_constants.h>
#include <pubsub/publisher.h>
#include <utils.h>
#include <zconf.h>
#include <celix_log_helper.h>
#include "pubsub_websocket_topic_sender.h"
#include "pubsub_psa_websocket_constants.h"
#include "pubsub_websocket_common.h"
#include <uuid/uuid.h>
#include <jansson.h>
#include "celix_constants.h"
#include "http_admin/api.h"
#include "civetweb.h"
#include <pubsub_utils.h>
#include "pubsub_interceptors_handler.h"

#define FIRST_SEND_DELAY_IN_SECONDS             2

#define L_DEBUG(...) \
    celix_logHelper_log(sender->logHelper, CELIX_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define L_INFO(...) \
    celix_logHelper_log(sender->logHelper, CELIX_LOG_LEVEL_INFO, __VA_ARGS__)
#define L_WARN(...) \
    celix_logHelper_log(sender->logHelper, CELIX_LOG_LEVEL_WARNING, __VA_ARGS__)
#define L_ERROR(...) \
    celix_logHelper_log(sender->logHelper, CELIX_LOG_LEVEL_ERROR, __VA_ARGS__)

struct pubsub_websocket_topic_sender {
    celix_bundle_context_t *ctx;
    celix_log_helper_t *logHelper;
    long serializerSvcId;
    pubsub_serializer_service_t *serializer;
    long protocolSvcId;
    pubsub_protocol_service_t *protocol;
    uuid_t fwUUID;
    bool metricsEnabled;
    pubsub_websocket_handler_t *socketHandler;
    pubsub_websocket_handler_t *sharedSocketHandler;
    pubsub_interceptors_handler_t *interceptorsHandler;

    celix_websocket_service_t websockSvc;
    long websockSvcId;
    struct mg_connection *sockConnection;

    char *scope;
    char *topic;
    char scopeAndTopicFilter[5];
    char *url;
    bool isStatic;
    bool verbose;


    struct {
        long svcId;
        celix_service_factory_t factory;
    } publisher;

    struct {
        celix_thread_mutex_t mutex;
        hash_map_t *map;  //key = bndId, value = psa_websocket_bounded_service_entry_t
    } boundedServices;
};

typedef struct psa_websocket_send_msg_entry {
    uint32_t type; //msg type id (hash of fqn)
    uint8_t major;
    uint8_t minor;
    unsigned char originUUID[16];
    pubsub_msg_serializer_t *msgSer;
    pubsub_protocol_service_t *protSer;
    struct iovec *serializedIoVecOutput;
    size_t serializedIoVecOutputLen;
    unsigned int seqNr;
    celix_thread_mutex_t sendLock; //protects send & header(.seqNr)
} psa_websocket_send_msg_entry_t;

typedef struct psa_websocket_bounded_service_entry {
    pubsub_websocket_topic_sender_t *parent;
    pubsub_publisher_t service;
    long bndId;
    hash_map_t *msgTypes; //key = msg type id, value = pubsub_msg_serializer_t
    hash_map_t *msgTypeIds; //key = msg name, value = msg type id
    hash_map_t *msgEntries; //key = msg type id, value = psa_websocket_send_msg_entry_t
    int getCount;
} psa_websocket_bounded_service_entry_t;

static int psa_websocket_localMsgTypeIdForMsgType(void* handle __attribute__((unused)), const char* msgType, unsigned int* msgTypeId);
static void* psa_websocket_getPublisherService(void *handle, const celix_bundle_t *requestingBundle, const celix_properties_t *svcProperties);
static void psa_websocket_ungetPublisherService(void *handle, const celix_bundle_t *requestingBundle, const celix_properties_t *svcProperties);
static void delay_first_send_for_late_joiners(pubsub_websocket_topic_sender_t *sender);

static int psa_websocket_topicPublicationSend(void* handle, unsigned int msgTypeId, const void *msg, celix_properties_t *metadata);


pubsub_websocket_topic_sender_t* pubsub_websocketTopicSender_create(
        celix_bundle_context_t *ctx,
        celix_log_helper_t *logHelper,
        const char *scope,
        const char *topic,
        const celix_properties_t *topicProperties,
        pubsub_websocket_endPointStore_t *endPointStore,
        long serializerSvcId,
        pubsub_serializer_service_t *ser,
        long protocolSvcId,
        pubsub_protocol_service_t *protocol) {
    pubsub_websocket_topic_sender_t *sender = calloc(1, sizeof(*sender));
    sender->ctx = ctx;
    sender->logHelper = logHelper;
    sender->serializerSvcId = serializerSvcId;
    sender->serializer = ser;
    sender->protocolSvcId = protocolSvcId;
    sender->protocol = protocol;
    const char *uuid = celix_bundleContext_getProperty(ctx, OSGI_FRAMEWORK_FRAMEWORK_UUID, NULL);
    if (uuid != NULL) {
        uuid_parse(uuid, sender->fwUUID);
    }
    psa_websocket_setScopeAndTopicFilter(scope, topic, sender->scopeAndTopicFilter);
    sender->url = psa_websocket_createURI(scope, topic);
    pubsubInterceptorsHandler_create(ctx, scope, topic, &sender->interceptorsHandler);

    bool isEndpoint = false;
    char *uris = NULL;
    const char *uri = celix_bundleContext_getProperty(ctx, PUBSUB_WEBSOCKET_BIND_URI, NULL);
    const char *staticClientEndPointUirs = NULL;
    const char *staticServerEndPointUri = NULL;


    if (topicProperties != NULL) {
        /* Check if it's a static endpoint */
        const char *endPointType = celix_properties_get(topicProperties, PUBSUB_WEBSOCKET_STATIC_ENDPOINT_TYPE, NULL);
        if (endPointType != NULL) {
            isEndpoint = true;
            if (strncmp(PUBSUB_WEBSOCKET_STATIC_ENDPOINT_TYPE_CLIENT, endPointType,
                        strlen(PUBSUB_WEBSOCKET_STATIC_ENDPOINT_TYPE_CLIENT)) == 0) {
                staticClientEndPointUirs = celix_properties_get(topicProperties, PUBSUB_WEBSOCKET_STATIC_CONNECT_URLS, NULL);
            }
            if (strncmp(PUBSUB_WEBSOCKET_STATIC_ENDPOINT_TYPE_SERVER, endPointType,
                        strlen(PUBSUB_WEBSOCKET_STATIC_ENDPOINT_TYPE_SERVER)) == 0) {
                staticServerEndPointUri = uri;
            }
        }
    }

    /* When it's an endpoint share the socket with the receiver */
    if ((staticClientEndPointUirs != NULL) || (staticServerEndPointUri)) {
        celixThreadMutex_lock(&endPointStore->mutex);
        const char *endPointUri = (staticClientEndPointUirs) ? staticClientEndPointUirs : staticServerEndPointUri;
        pubsub_websocket_handler_t *entry = hashMap_get(endPointStore->map, endPointUri);
        if (entry == NULL) {
            if (sender->socketHandler == NULL)
                sender->socketHandler = pubsub_websocket_handler_create(ctx, sender->protocol, sender->logHelper);
            entry = sender->socketHandler;
            sender->sharedSocketHandler = sender->socketHandler;
            hashMap_put(endPointStore->map, (void *) endPointUri, entry);
        } else {
            sender->socketHandler = entry;
            sender->sharedSocketHandler = entry;
        }
        celixThreadMutex_unlock(&endPointStore->mutex);
    } else {
        sender->socketHandler = pubsub_websocket_handler_create(ctx, sender->protocol, sender->logHelper);
    }

    if ((sender->socketHandler != NULL) && (topicProperties != NULL)) {
        long prio = celix_properties_getAsLong(topicProperties, PUBSUB_WEBSOCKET_THREAD_REALTIME_PRIO, -1L);
        const char *sched = celix_properties_get(topicProperties, PUBSUB_WEBSOCKET_THREAD_REALTIME_SCHED, NULL);
        pubsub_websocket_handler_set_thread_name(sender->socketHandler, topic, scope);
        pubsub_websocket_handler_set_thread_priority(sender->socketHandler, prio, sched);
    }

    //setting up tcp socket for TCP TopicSender
    if (staticClientEndPointUirs != NULL) {
        // Store url for client static endpoint
        sender->url = strndup(staticClientEndPointUirs, 1024 * 1024);
        sender->isStatic = true;
    }
    else if (uri != NULL) {
        uris = strndup(uri, 1024 * 1024);
    } else {
        uris = psa_websocket_createURI(scope, topic);
    }
    if (!sender->url) {
        char *urlsCopy = strndup(uris, 1024 * 1024);
        char *url;
        char *save = urlsCopy;
        while ((url = strtok_r(save, " ", &save))) {
            pubsub_utils_url_t *urlInfo = pubsub_utils_url_parse(url);
            int rc = pubsub_websocket_handler_listen(sender->socketHandler, urlInfo->uri);
            if (rc < 0) {
                L_WARN("Error for tcp_bind using dynamic bind url '%s'. %s", urlInfo->url, strerror(errno));
            } else {
                url = NULL;
            }
            pubsub_utils_url_free(urlInfo);
        }
        free(urlsCopy);
        //sender->url = pubsub_tcpHandler_get_interface_url(sender->socketHandler);
    }
    if (uris)
        free(uris);

    if (sender->url != NULL) {
        sender->scope = scope == NULL ? NULL : strndup(scope, 1024 * 1024);
        sender->topic = strndup(topic, 1024 * 1024);

        celixThreadMutex_create(&sender->boundedServices.mutex, NULL);
        sender->boundedServices.map = hashMap_create(NULL, NULL, NULL, NULL);
    }

    //register publisher services using a service factory
    if (sender->url != NULL) {
        sender->publisher.factory.handle = sender;
        sender->publisher.factory.getService = psa_websocket_getPublisherService;
        sender->publisher.factory.ungetService = psa_websocket_ungetPublisherService;

        celix_properties_t *props = celix_properties_create();
        celix_properties_set(props, PUBSUB_PUBLISHER_TOPIC, sender->topic);
        if (sender->scope != NULL) {
            celix_properties_set(props, PUBSUB_PUBLISHER_SCOPE, sender->scope);
        }

        celix_service_registration_options_t opts = CELIX_EMPTY_SERVICE_REGISTRATION_OPTIONS;
        opts.factory = &sender->publisher.factory;
        opts.serviceName = PUBSUB_PUBLISHER_SERVICE_NAME;
        opts.serviceVersion = PUBSUB_PUBLISHER_SERVICE_VERSION;
        opts.properties = props;

        sender->publisher.svcId = celix_bundleContext_registerServiceWithOptions(ctx, &opts);
    }


    if (!sender->socketHandler) {
        free(sender);
        sender = NULL;
    }

    return sender;
}

void pubsub_websocketTopicSender_destroy(pubsub_websocket_topic_sender_t *sender) {
    if (sender != NULL) {
        celix_bundleContext_unregisterService(sender->ctx, sender->publisher.svcId);

        celixThreadMutex_lock(&sender->boundedServices.mutex);
        hash_map_iterator_t iter = hashMapIterator_construct(sender->boundedServices.map);
        while (hashMapIterator_hasNext(&iter)) {
            psa_websocket_bounded_service_entry_t *entry = hashMapIterator_nextValue(&iter);
            if (entry != NULL) {
                sender->serializer->destroySerializerMap(sender->serializer->handle, entry->msgTypes);
                hash_map_iterator_t iter2 = hashMapIterator_construct(entry->msgEntries);
                while (hashMapIterator_hasNext(&iter2)) {
                    psa_websocket_send_msg_entry_t *msgEntry = hashMapIterator_nextValue(&iter2);
                    free(msgEntry);

                }
                hashMap_destroy(entry->msgEntries, false, false);

                free(entry);
            }
        }
        hashMap_destroy(sender->boundedServices.map, false, false);
        celixThreadMutex_unlock(&sender->boundedServices.mutex);

        celixThreadMutex_destroy(&sender->boundedServices.mutex);

        celix_bundleContext_unregisterService(sender->ctx, sender->websockSvcId);

        if (sender->scope != NULL) {
            free(sender->scope);
        }
        free(sender->topic);
        free(sender->url);
        free(sender);
    }
}

long pubsub_websocketTopicSender_serializerSvcId(pubsub_websocket_topic_sender_t *sender) {
    return sender->serializerSvcId;
}

long pubsub_websocketTopicSender_protocolSvcId(pubsub_websocket_topic_sender_t *sender) {
    return sender->protocolSvcId;
}

const char* pubsub_websocketTopicSender_scope(pubsub_websocket_topic_sender_t *sender) {
    return sender->scope;
}

const char* pubsub_websocketTopicSender_topic(pubsub_websocket_topic_sender_t *sender) {
    return sender->topic;
}

const char* pubsub_websocketTopicSender_url(pubsub_websocket_topic_sender_t *sender) {
    return sender->url;
}

bool pubsub_websocketTopicSender_isStatic(pubsub_websocket_topic_sender_t *sender) {
    return sender->isStatic;
}

static int psa_websocket_localMsgTypeIdForMsgType(void* handle, const char* msgType, unsigned int* msgTypeId) {
    psa_websocket_bounded_service_entry_t *entry = (psa_websocket_bounded_service_entry_t *) handle;
    *msgTypeId = (unsigned int)(uintptr_t) hashMap_get(entry->msgTypeIds, msgType);
    return 0;
}

static void* psa_websocket_getPublisherService(void *handle, const celix_bundle_t *requestingBundle, const celix_properties_t *svcProperties __attribute__((unused))) {
    pubsub_websocket_topic_sender_t *sender = handle;
    long bndId = celix_bundle_getId(requestingBundle);

    celixThreadMutex_lock(&sender->boundedServices.mutex);
    psa_websocket_bounded_service_entry_t *entry = hashMap_get(sender->boundedServices.map, (void*)bndId);
    if (entry != NULL) {
        entry->getCount += 1;
    } else {
        entry = calloc(1, sizeof(*entry));
        entry->getCount = 1;
        entry->parent = sender;
        entry->bndId = bndId;
        entry->msgEntries = hashMap_create(NULL, NULL, NULL, NULL);
        entry->msgTypeIds = hashMap_create(utils_stringHash, NULL, utils_stringEquals, NULL);

        int rc = sender->serializer->createSerializerMap(sender->serializer->handle, (celix_bundle_t*)requestingBundle, &entry->msgTypes);
        if (rc == 0) {
            hash_map_iterator_t iter = hashMapIterator_construct(entry->msgTypes);
            while (hashMapIterator_hasNext(&iter)) {
                hash_map_entry_t *hashMapEntry = hashMapIterator_nextEntry(&iter);
                void *key = hashMapEntry_getKey(hashMapEntry);
                psa_websocket_send_msg_entry_t *sendEntry = calloc(1, sizeof(*sendEntry));
                sendEntry->msgSer = hashMapEntry_getValue(hashMapEntry);
                sendEntry->protSer = sender->protocol;
                sendEntry->type = (int32_t) sendEntry->msgSer->msgId;
                int major;
                int minor;
                version_getMajor(sendEntry->msgSer->msgVersion, &major);
                version_getMinor(sendEntry->msgSer->msgVersion, &minor);
                sendEntry->major = (uint8_t) major;
                sendEntry->minor = (uint8_t) minor;
                uuid_copy(sendEntry->originUUID, sender->fwUUID);
                hashMap_put(entry->msgEntries, key, sendEntry);
                hashMap_put(entry->msgTypeIds, strndup(sendEntry->msgSer->msgName, 1024),
                            (void *) (uintptr_t) sendEntry->msgSer->msgId);
            }
            entry->service.handle = entry;
            entry->service.localMsgTypeIdForMsgType = psa_websocket_localMsgTypeIdForMsgType;
            entry->service.send = psa_websocket_topicPublicationSend;
            hashMap_put(sender->boundedServices.map, (void*)bndId, entry);
        } else {
            L_ERROR("Error creating serializer map for websocket TopicSender %s/%s", sender->scope == NULL ? "(null)" : sender->scope, sender->topic);
        }
    }
    celixThreadMutex_unlock(&sender->boundedServices.mutex);

    return &entry->service;
}

static void psa_websocket_ungetPublisherService(void *handle, const celix_bundle_t *requestingBundle, const celix_properties_t *svcProperties __attribute__((unused))) {
    pubsub_websocket_topic_sender_t *sender = handle;
    long bndId = celix_bundle_getId(requestingBundle);

    celixThreadMutex_lock(&sender->boundedServices.mutex);
    psa_websocket_bounded_service_entry_t *entry = hashMap_get(sender->boundedServices.map, (void*)bndId);
    if (entry != NULL) {
        entry->getCount -= 1;
    }
    if (entry != NULL && entry->getCount == 0) {
        //free entry
        hashMap_remove(sender->boundedServices.map, (void*)bndId);
        int rc = sender->serializer->destroySerializerMap(sender->serializer->handle, entry->msgTypes);
        if (rc != 0) {
            L_ERROR("Error destroying publisher service, serializer not available / cannot get msg serializer map\n");
        }

        hash_map_iterator_t iter = hashMapIterator_construct(entry->msgEntries);
        while (hashMapIterator_hasNext(&iter)) {
            psa_websocket_send_msg_entry_t *msgEntry = hashMapIterator_nextValue(&iter);
            if (msgEntry->serializedIoVecOutput)
                free(msgEntry->serializedIoVecOutput);
            msgEntry->serializedIoVecOutput = NULL;
            free(msgEntry);
        }
        hashMap_destroy(entry->msgEntries, false, false);

        hashMap_destroy(entry->msgTypeIds, true, false);
        free(entry);
    }
    celixThreadMutex_unlock(&sender->boundedServices.mutex);
}

static int psa_websocket_topicPublicationSend(void* handle, unsigned int msgTypeId, const void *inMsg, celix_properties_t *metadata) {
    int status = CELIX_SERVICE_EXCEPTION;
    psa_websocket_bounded_service_entry_t *bound = handle;
    pubsub_websocket_topic_sender_t *sender = bound->parent;
    psa_websocket_send_msg_entry_t *entry = hashMap_get(bound->msgEntries, (void *) (uintptr_t) (msgTypeId));

    if (sender->sockConnection != NULL && entry != NULL) {
        delay_first_send_for_late_joiners(sender);

        size_t serializedIoVecOutputLen = 0; //entry->serializedIoVecOutputLen;
        struct iovec *serializedIoVecOutput = NULL;
        status = entry->msgSer->serialize(entry->msgSer->handle, inMsg, &serializedIoVecOutput,
                                          &serializedIoVecOutputLen);
        entry->serializedIoVecOutputLen = MAX(serializedIoVecOutputLen, entry->serializedIoVecOutputLen);

        bool cont = pubsubInterceptorHandler_invokePreSend(sender->interceptorsHandler, entry->msgSer->msgName, msgTypeId, inMsg, &metadata);
        if (status == CELIX_SUCCESS /*ser ok*/ && cont) {
            pubsub_protocol_message_t message;
            message.metadata.metadata = NULL;
            message.payload.payload = NULL;
            message.payload.length = 0;
            if (serializedIoVecOutput) {
                message.payload.payload = serializedIoVecOutput->iov_base;
                message.payload.length = serializedIoVecOutput->iov_len;
            }
            message.header.msgId = msgTypeId;
            message.header.seqNr = entry->seqNr;
            message.header.msgMajorVersion = entry->major;
            message.header.msgMinorVersion = entry->minor;
            message.header.payloadSize = 0;
            message.header.payloadPartSize = 0;
            message.header.payloadOffset = 0;
            message.header.metadataSize = 0;
            if (metadata != NULL)
                message.metadata.metadata = metadata;
            entry->seqNr++;
            bool sendOk = true;
            {
                int rc = pubsub_websocket_handler_write(sender->socketHandler, &message);
                if (rc < 0) {
                    status = -1;
                    sendOk = false;
                }
                pubsubInterceptorHandler_invokePostSend(sender->interceptorsHandler, entry->msgSer->msgName, msgTypeId, inMsg, metadata);
                if (message.metadata.metadata)
                    celix_properties_destroy(message.metadata.metadata);
                if (serializedIoVecOutput) {
                    entry->msgSer->freeSerializeMsg(entry->msgSer->handle,
                                                    serializedIoVecOutput,
                                                    serializedIoVecOutputLen);
                    serializedIoVecOutput = NULL;
                }
            }

            if (!sendOk) {
                L_WARN("[PSA_WEBSOCKET_TS] Error sending msg. %s", strerror(errno));
            }
        } else {
            L_WARN("[PSA_WEBSOCKET_TS] Error serialize message of type %s for scope/topic %s/%s", entry->msgSer->msgName, sender->scope == NULL ? "(null)" : sender->scope, sender->topic);
        }
    } else if (entry == NULL){
        L_WARN("[PSA_WEBSOCKET_TS] Error sending message with msg type id %i for scope/topic %s/%s", msgTypeId, sender->scope == NULL ? "(null)" : sender->scope, sender->topic);
    } else { // when (sender->sockConnection == NULL) we dont have a client, but we do have a valid entry
        status = CELIX_SUCCESS; // Not an error, just nothing to do
    }
    return status;
}

static void delay_first_send_for_late_joiners(pubsub_websocket_topic_sender_t *sender) {

    static bool firstSend = true;

    if (firstSend) {
        L_INFO("PSA_WEBSOCKET_TP: Delaying first send for late joiners...\n");
        sleep(FIRST_SEND_DELAY_IN_SECONDS);
        firstSend = false;
    }
}
