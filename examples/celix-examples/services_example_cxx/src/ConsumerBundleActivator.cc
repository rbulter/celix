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
#include <thread>
#include <atomic>

#include "celix/BundleActivator.h"

#include "ICalc.h"

namespace {
    class BundleActivator : public celix::IBundleActivator {
    public:
        BundleActivator(celix::BundleContext &_ctx) : ctx{_ctx} {
            this->trackerId = ctx.trackServices<example::ICalc>(example::ICalc::NAME,
                 [this](example::ICalc *) {  this->trackCount += 1; },
                 [this](example::ICalc *) {  this->trackCount -= 1; });
        }

        virtual ~BundleActivator() {
            ctx.stopTracker(this->trackerId);
            this->running = false;
            this->useThread.join();
        }

    protected:
        void use() {
                while(running) {
                        int count = 0;
                        double total = 0;
                        ctx.useServices<example::ICalc>(example::ICalc::NAME, [&](example::ICalc &calc, const celix::Properties &, const celix::Bundle&) {
                                count++;
                                total += calc.calc(1);
                        });
                        std::cout << "Called calc " << count << " times. Total is " << total << std::endl;

                        ctx.useService<example::ICalc>(example::ICalc::NAME, [&](example::ICalc &, const celix::Properties &props, const celix::Bundle &bnd){
                           long rank = celix::getProperty(props, celix::Constants::SERVICE_RANKING, -1L);
                           long svcId = celix::getProperty(props, celix::Constants::SERVICE_ID, -1L);
                           long bndId = bnd.getBundleId();
                           std::cout << "Found highest ranking call with rank " << rank << " and service id " << svcId << " from bundle " << bndId << std::endl;
                        });

                        std::cout << "track counter is " << this->trackCount << std::endl;

                        std::this_thread::sleep_for(std::chrono::seconds(5));
                }
        }

    private:
        celix::BundleContext &ctx;

        long trackerId{-1};
        std::thread useThread{[this] { this->use(); }};

        std::atomic<bool> running{true};
        std::atomic<int> trackCount{0};
    };
}

celix::IBundleActivator* celix::createBundleActivator(celix::BundleContext &ctx) {
    return new BundleActivator{ctx};
}

