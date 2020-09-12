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

#ifndef PUBSUB_PSA_WEBSOCKET_CONSTANTS_H_
#define PUBSUB_PSA_WEBSOCKET_CONSTANTS_H_

#define PSA_WEBSOCKET_DEFAULT_QOS_SAMPLE_SCORE        30
#define PSA_WEBSOCKET_DEFAULT_QOS_CONTROL_SCORE       70
#define PSA_WEBSOCKET_DEFAULT_SCORE                   30

#define PSA_WEBSOCKET_QOS_SAMPLE_SCORE_KEY            "PSA_WEBSOCKET_QOS_SAMPLE_SCORE"
#define PSA_WEBSOCKET_QOS_CONTROL_SCORE_KEY           "PSA_WEBSOCKET_QOS_CONTROL_SCORE"
#define PSA_WEBSOCKET_DEFAULT_SCORE_KEY               "PSA_WEBSOCKET_DEFAULT_SCORE"


#define PSA_WEBSOCKET_SUBSCRIBER_CONNECTION_TIMEOUT         "PSA_WEBSOCKET_SUBSCRIBER_CONNECTION_TIMEOUT"
#define PSA_WEBSOCKET_SUBSCRIBER_CONNECTION_DEFAULT_TIMEOUT 250 // 250 ms

#define PUBSUB_WEBSOCKET_VERBOSE_KEY                  "PSA_WEBSOCKET_VERBOSE"
#define PUBSUB_WEBSOCKET_VERBOSE_DEFAULT              true

#define PUBSUB_WEBSOCKET_ADMIN_TYPE                   "websocket"
#define PUBSUB_WEBSOCKET_ADDRESS_KEY                  "websocket.socket_address"
#define PUBSUB_WEBSOCKET_PORT_KEY                     "websocket.socket_port"

/**
 * If set true on the endpoint, the tcp TopicSender bind and/or discovery url is statically configured.
 */
#define PUBSUB_WEBSOCKET_STATIC_CONFIGURED            "websocket.static.configured"

/**
 * The websocket url key for the topic sender endpoints
 */
#define PUBSUB_WEBSOCKET_URL_KEY                      "websocket.url"

/**
 * Can be set in the topic properties to fix a static bind url
 */
#define PUBSUB_WEBSOCKET_BIND_URI              "websocket.bind.uri"

/**
 * The static url which a subscriber should try to connect to.
 * The urls are space separated.
 * Can be set in the topic properties.
 */
#define PUBSUB_WEBSOCKET_STATIC_CONNECT_URLS          "websocket.static.connect.urls"

/**
 * Name of environment variable with space-separated list of ips/urls to connect to
 * e.g. PUBSUB_WEBSOCKET_STATIC_CONNECT_SOCKET_ADDRESSES_FOR_topic_scope="tcp://127.0.0.1:4444 tcp://127.0.0.2:4444"
 */
#define PUBSUB_WEBSOCKET_STATIC_CONNECT_SOCKET_ADDRESSES_FOR "PUBSUB_WEBSOCKET_STATIC_CONNECT_SOCKET_ADDRESSES_FOR_"

/**
 * The static endpoint type which a static endpoint should be configured.
 * Can be set in the topic properties.
 */
#define PUBSUB_WEBSOCKET_STATIC_ENDPOINT_TYPE         "websocket.static.endpoint.type"

#define PUBSUB_WEBSOCKET_STATIC_ENDPOINT_TYPE_SERVER  "server"
#define PUBSUB_WEBSOCKET_STATIC_ENDPOINT_TYPE_CLIENT  "client"

/**
 * Realtime thread prio and scheduling information. This is used to setup the thread prio/sched of the
 * internal TCP threads.
 * Can be set in the topic properties.
 */
#define PUBSUB_WEBSOCKET_THREAD_REALTIME_PRIO         "thread.realtime.prio"
#define PUBSUB_WEBSOCKET_THREAD_REALTIME_SCHED        "thread.realtime.sched"

#endif /* PUBSUB_PSA_WEBSOCKET_CONSTANTS_H_ */
