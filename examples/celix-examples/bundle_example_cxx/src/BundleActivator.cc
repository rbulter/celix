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

#include <iostream>
#include "celix/BundleActivator.h"
namespace {
    class BundleActivator : public celix::IBundleActivator {
    public:
        virtual ~BundleActivator(){}

        celix_status_t start(celix::BundleContext &ctx) override {
            std::cout << "Hello world from C++ bundle with id " << ctx.getBundle().getBundleId() << std::endl;
            return CELIX_SUCCESS;
        }

        celix_status_t stop(celix::BundleContext &ctx) override {
            std::cout << "Goodbye world from C++ bundle with id " << ctx.getBundle().getBundleId() << std::endl;
            return CELIX_SUCCESS;
        }
    };
}

CELIX_GEN_CXX_BUNDLE_ACTIVATOR(BundleActivator)

