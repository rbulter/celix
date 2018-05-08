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

#ifndef CXX_CELIX_BUNDLE_H
#define CXX_CELIX_BUNDLE_H

#include "celix/Properties.h"

namespace celix {

    enum class BundleState {
        UNKNOWN,
        UNINSTALLED,
        INSTALLED,
        RESOLVED,
        STARTING,
        STOPPING,
        ACTIVE
    };

    class Bundle {
    public:
        virtual ~Bundle() = default;

        virtual bool isSystemBundle() const noexcept  = 0;

        virtual void * getHandle() const noexcept = 0;

        virtual BundleState getState() const noexcept  = 0;

        virtual long getBundleId() const noexcept  = 0;

        virtual std::string getBundleLocation() const noexcept  = 0;

        virtual std::string getBundleCache() const noexcept  = 0;

        virtual std::string getBundleName() const noexcept = 0;

        virtual std::string getBundleSymbolicName() const noexcept = 0;

        virtual std::string getBundleVersion() const noexcept = 0;

        virtual celix::Properties getManifestAsProperties() const noexcept  = 0;

        virtual void start() noexcept = 0;

        virtual void stop() noexcept = 0;

        virtual void uninstall() noexcept = 0;

    };

}

#endif //CXX_CELIX_BUNDLE_H

#include "celix/impl/BundleImpl.h"
