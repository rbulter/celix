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

#ifndef CELIX_PUBSUB_WEBSOCKET_COMMON_H
#define CELIX_PUBSUB_WEBSOCKET_COMMON_H

#include <utils.h>
#include <stdint.h>
#include <hash_map.h>

typedef struct pubsub_websocket_endPointStore {
  celix_thread_mutex_t mutex;
  hash_map_t *map;
} pubsub_websocket_endPointStore_t;

void psa_websocket_setScopeAndTopicFilter(const char* scope, const char *topic, char *filter);
char *psa_websocket_createURI(const char *scope, const char *topic);


#endif //CELIX_PUBSUB_WEBSOCKET_COMMON_H
