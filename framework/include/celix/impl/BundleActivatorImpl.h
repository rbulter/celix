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


namespace celix {
    namespace impl {
        struct ActivatorData {
            celix::BundleContext *ctx;
            celix::IBundleActivator *act;
        };
    }
}


extern "C" celix_status_t bundleActivator_create(bundle_context_t *c_ctx, void **userData) {
    auto *data = new celix::impl::ActivatorData;
    data->ctx = new celix::impl::BundleContextImpl(c_ctx);
    *userData = data;
    return CELIX_SUCCESS;
}

extern "C" celix_status_t bundleActivator_start(void *userData, bundle_context_t *) {
    auto *data = static_cast<celix::impl::ActivatorData*>(userData);
    data->act = celix::createBundleActivator(*data->ctx);
    return CELIX_SUCCESS;
}

extern "C" celix_status_t bundleActivator_stop(void *userData, bundle_context_t *) {
    auto *data = static_cast<celix::impl::ActivatorData*>(userData);
    delete data->act;
    return CELIX_SUCCESS;
}

extern "C" celix_status_t bundleActivator_destroy(void *userData, bundle_context_t*) {
    auto *data = static_cast<celix::impl::ActivatorData*>(userData);
    delete data->ctx;
    delete data;
    return CELIX_SUCCESS;
}
