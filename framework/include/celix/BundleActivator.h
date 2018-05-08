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

#ifndef CXX_CELIX_BUNDLEACTIVATOR_H
#define CXX_CELIX_BUNDLEACTIVATOR_H

#include <memory>

#include "celix/IBundleActivator.h"
#include "celix/Framework.h"

#include "bundle_activator.h"

/**
 * Note & Warning this is a header implementation of the C bundle activator.
 * As result this header can only be included ones or the activator symbols will
 * be duplicate (linking error).
 */

namespace celix {
    /**
     * The celix::createBundleActivator which needs to be implemented by a bundle.
    */
    static celix::IBundleActivator* createBundleActivator(celix::BundleContext &ctx);

    namespace impl {
        struct ActivatorData {
            std::unique_ptr<celix::Framework> fw{};
            std::unique_ptr<celix::BundleContext> ctx{};
            std::unique_ptr<celix::IBundleActivator> act{};
        };
    }
}


extern "C" celix_status_t bundleActivator_create(bundle_context_t *c_ctx, void **userData) {
    auto *data = new celix::impl::ActivatorData;
    data->fw = std::unique_ptr<celix::Framework>{new celix::impl::FrameworkImpl{c_ctx}};
    data->ctx = std::unique_ptr<celix::BundleContext>{new celix::impl::BundleContextImpl{c_ctx, *data->fw}};
    *userData = data;
    return CELIX_SUCCESS;
}

extern "C" celix_status_t bundleActivator_start(void *userData, bundle_context_t *) {
    auto *data = static_cast<celix::impl::ActivatorData*>(userData);
    data->act = std::unique_ptr<celix::IBundleActivator>{celix::createBundleActivator(*data->ctx)};
    data->ctx->getDependencyManager().start();
    return CELIX_SUCCESS;
}

extern "C" celix_status_t bundleActivator_stop(void *userData, bundle_context_t *) {
    auto *data = static_cast<celix::impl::ActivatorData*>(userData);
    data->ctx->getDependencyManager().stop();
    data->act = nullptr;
    return CELIX_SUCCESS;
}

extern "C" celix_status_t bundleActivator_destroy(void *userData, bundle_context_t*) {
    auto *data = static_cast<celix::impl::ActivatorData*>(userData);
    data->ctx = nullptr;
    data->fw = nullptr;
    delete data;
    return CELIX_SUCCESS;
}

#endif //CXX_CELIX_BUNDLEACTIVATOR_H
