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

#include <memory>

#include "celix/IBundleActivator.h"
#include "celix/Framework.h"

#ifndef CXX_CELIX_BUNDLEACTIVATOR_H
#define CXX_CELIX_BUNDLEACTIVATOR_H

namespace celix {
    /**
     * The BundleActivatorAdapter adapts the C bundle activator calls to a C++ bundle activator.
     * The Type parameter (T) is the C++ bundle activator and needs to support:
     * - A public default constructor.
     * - Implementing the celix::IBundleActivator interface, which contains:
     *      - A public destructor.
     *      - A public 'celix_status_t start(celix::BundleContext &ctx)` method.
     *      - A public 'celix_status_t start(celix::BundleContext &ctx)` method.
     */
    template<typename T>
    class BundleActivatorAdapter {
    private:
        std::unique_ptr<celix::Framework> fw{};
        std::unique_ptr<celix::BundleContext> ctx{};
        std::unique_ptr<celix::IBundleActivator> activator{};
    public:
        BundleActivatorAdapter(bundle_context_t *c_ctx) {
            this->fw = std::unique_ptr<celix::Framework>{new celix::impl::FrameworkImpl{c_ctx}}; \
            this->ctx = std::unique_ptr<celix::BundleContext>{new celix::BundleContext{c_ctx, *this->fw}}; \
            this->activator = nullptr;
        }

        ~BundleActivatorAdapter() {
            this->ctx = nullptr;
            this->fw = nullptr;
            this->activator = nullptr;
        }

        celix_status_t start() noexcept  {
            this->activator = std::unique_ptr<celix::IBundleActivator>{new T};
            celix_status_t status = this->activator->start(*this->ctx);
            if (status == CELIX_SUCCESS) {
                this->ctx->getDependencyManager().start();
            }
            return status;
        }

        celix_status_t stop() noexcept {
            celix_status_t status = this->activator->stop(*this->ctx);
            if (status == CELIX_SUCCESS) {
                this->ctx->getDependencyManager().stop();
            }
            this->activator = nullptr; //implicit delete
            return status;
        }
    };
}

/**
 * This macro generated the required bundle activator symbols, which uses the celix::BundleActivatorAdapter to
 * adapt the C bundle activator calls to the provided C++ bundle activator class.
 */
#define CELIX_GEN_CXX_BUNDLE_ACTIVATOR(clazz)                                                                          \
extern "C" celix_status_t bundleActivator_create(bundle_context_t *c_ctx, void **userData) {                           \
    auto *data = new celix::BundleActivatorAdapter<clazz>{c_ctx};                                                      \
    *userData = data;                                                                                                  \
    return CELIX_SUCCESS;                                                                                              \
}                                                                                                                      \
                                                                                                                       \
extern "C" celix_status_t bundleActivator_start(void *userData, bundle_context_t *) {                                  \
    auto *data = static_cast<celix::BundleActivatorAdapter<clazz>*>(userData);                                         \
    return data->start();                                                                                              \
}                                                                                                                      \
                                                                                                                       \
extern "C" celix_status_t bundleActivator_stop(void *userData, bundle_context_t *) {                                   \
    auto *data = static_cast<celix::BundleActivatorAdapter<clazz>*>(userData);                                         \
    return data->stop();                                                                                               \
}                                                                                                                      \
                                                                                                                       \
extern "C" celix_status_t bundleActivator_destroy(void *userData, bundle_context_t*) {                                 \
    auto *data = static_cast<celix::BundleActivatorAdapter<clazz>*>(userData);                                         \
    delete data;                                                                                                       \
    return CELIX_SUCCESS;                                                                                              \
}                                                                                                                      \

#endif //CXX_CELIX_BUNDLEACTIVATOR_H
