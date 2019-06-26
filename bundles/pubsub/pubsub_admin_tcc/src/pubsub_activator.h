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


#include <stdlib.h>
#include <new>
#include <iostream>
#include "celix_api.h"
#include "pubsub_base_admin.h"

namespace celix { namespace pubsub {
    template<class DataType>
    class Activator {
    public:
        Activator(celix_bundle_context_t *ctx) :
                context{ctx},
                admin(context)
        {}
        Activator(const Activator&) = delete;
        Activator& operator=(const Activator&) = delete;

        ~Activator()  = default;

        celix_status_t  start() {
            admin.start();
            return CELIX_SUCCESS;
        }

        celix_status_t stop() {
            admin.stop();
            return CELIX_SUCCESS;
        };

    private:
      celix_bundle_context_t *context{};
      pubsub_base_admin<DataType> admin;
    };
}}
