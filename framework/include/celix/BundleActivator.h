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

#include "celix/BundleContext.h"
#include "bundle_activator.h"


/**
 * Note & Warning this is a header implementation of the C bundle activator.
 * As result this header can only be included ones or the activator symbols will
 * be duplicate (linking error).
 */

namespace celix {

    class IBundleActivator {
    public:
        IBundleActivator(){};
        virtual ~IBundleActivator(){};
    };

    /**
     * The celix::createBundleActivator which needs to be implemented by a bundle.
     */
    static celix::IBundleActivator* createBundleActivator(celix::BundleContext &ctx);
}

#include "celix/impl/BundleActivatorImpl.h"

#endif //CXX_CELIX_BUNDLEACTIVATOR_H
