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
#include <vector>

#include "celix/BundleActivator.h"

#include "ICalc.h"

namespace {
    class CalcImpl : public example::ICalc {
        double calc(double input) override {
            return input * 42.0;
        }
    };

    class BundleActivator {
    public:
        celix_status_t  start(celix::BundleContext &ctx) {
            /*
             * This thread registers calc service to a max of 100, then unregistered the services and repeats.
             */
            th = std::thread{[this, &ctx]{
                std::cout << "Starting service register thread" << std::endl;
                CalcImpl calc{};
                std::vector<long> svcIds{};
                bool up = true;
                while (this->running) {
                    if (up) {
                        celix::Properties props{};
                        props[celix::Constants::SERVICE_RANKING] = std::to_string(std::rand());
                        long svcId = ctx.registerService<example::ICalc>(&calc, example::ICalc::NAME, std::move(props));
                        svcIds.push_back(svcId);
                    } else {
                        long svcId = svcIds.back();
                        svcIds.pop_back();
                        ctx.unregisterService(svcId);
                    }
                    if (up) {
                        up = svcIds.size() < 100;
                    } else {
                        up = svcIds.size() == 0;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(88));
                }
                std::cout << "Exiting service register thread, services count is " << svcIds.size() << std::endl;
                std::for_each(svcIds.begin(), svcIds.end(), [&ctx](long id){ctx.unregisterService(id);});

            }};
            return  CELIX_SUCCESS;
        }

        celix_status_t  stop(celix::BundleContext &) {
            this->running = false;
            th.join();
            return CELIX_SUCCESS;
        }

    private:
        std::thread th{};
        std::atomic<bool> running{true};
    };
}

CELIX_GEN_CXX_BUNDLE_ACTIVATOR(BundleActivator)
