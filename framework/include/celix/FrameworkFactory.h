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

#ifndef CXX_CELIX_FRAMEWORKFACTORY_H
#define CXX_CELIX_FRAMEWORKFACTORY_H

#include "celix/Properties.h"
#include "celix/Framework.h"

//include implementations
#include "celix/impl/BundleImpl.h"
#include "celix/impl/BundleContextImpl.h"
#include "celix/impl/FrameworkImpl.h"

namespace celix {
    class FrameworkFactory {
    public:
        static celix::Framework *newFramework(celix::Properties config = {}) noexcept;

        static void registerEmbeddedBundle(
                std::string id,
                std::function<void(celix::BundleContext &ctx)> start,
                std::function<void(celix::BundleContext &ctx)> stop,
                celix::Properties manifest = {},
                bool autoStart = true
        ) noexcept;

        static void registerEmbeddedBundle(const celix::BundleRegistrationOptions &opts) noexcept;
    };
}

inline celix::Framework* celix::FrameworkFactory::newFramework(celix::Properties config) noexcept {
    return new celix::impl::FrameworkImpl(std::move(config));
}

inline void celix::FrameworkFactory::registerEmbeddedBundle(
        std::string /*id*/,
        std::function<void(celix::BundleContext& ctx)> /*start*/,
        std::function<void(celix::BundleContext& ctx)> /*stop*/,
        celix::Properties /*manifest*/,
        bool /*autoStart*/
) noexcept {
    //TODO
}

inline void celix::FrameworkFactory::registerEmbeddedBundle(const celix::BundleRegistrationOptions &/*opts*/) noexcept {
    //TODO
};

#endif //CXX_CELIX_FRAMEWORKFACTORY_H