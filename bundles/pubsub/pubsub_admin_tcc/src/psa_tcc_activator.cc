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
//#include "LogHelper.h"
#include "pubsub_base_admin.h"
//#include "pubsub_nanomsg_admin.h"
#include "pubsub_activator.h"

celix_status_t  celix_bundleActivator_create(celix_bundle_context_t *ctx , void **userData) {
    celix_status_t status = CELIX_SUCCESS;
    auto data = nullptr; //new  (std::nothrow) celix::pubsub::nanomsg::Activator{ctx};
    if (data != NULL) {
        *userData = data;
    } else {
        status = CELIX_ENOMEM;
    }
    return status;
}

celix_status_t celix_bundleActivator_start(void *userData, celix_bundle_context_t *) {
    auto act = static_cast<celix::pubsub::Activator*>(userData);
    return act->start();
}

celix_status_t celix_bundleActivator_stop(void *userData, celix_bundle_context_t *) {
    auto act = static_cast<celix::pubsub::Activator*>(userData);
    return act->stop();
}


celix_status_t celix_bundleActivator_destroy(void *userData, celix_bundle_context_t *) {
    auto act = static_cast<celix::pubsub::Activator*>(userData);
    delete act;
    return CELIX_SUCCESS;
}
