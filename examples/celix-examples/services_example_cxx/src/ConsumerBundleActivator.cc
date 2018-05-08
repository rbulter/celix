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

#include "celix/BundleActivator.h"

#include "ICalc.h"

namespace {
    class BundleActivator : public celix::IBundleActivator {
    public:
        BundleActivator(celix::BundleContext &_ctx) : ctx{_ctx} {}

        virtual ~BundleActivator() {
                this->useThread.join();
        }

    protected:
        void use() {
                while(this->running) {
                        int count = 0;
                        double total = 0;
                        ctx.useServices<example::ICalc>(example::ICalc::NAME, [&](example::ICalc &calc, const celix::Properties &, const celix::Bundle&) {
                                count++;
                                total += calc.calc(1);
                        });
                        std::cout << "Called calc " << count << " times. Total is " << total << std::endl;
                        std::this_thread::sleep_for(std::chrono::seconds(5));
                }
        }

        void setRunning(bool r) {
                std::lock_guard<std::mutex> lock{this->mutex};
                this->running = r;
        }

        bool isRunning() {
                std::lock_guard<std::mutex> lock{this->mutex};
                return this->running;
        }

    private:
        celix::BundleContext &ctx;
        std::thread useThread{[this] { this->use(); }};

        std::mutex mutex{}; //protects running
        bool running{true};
    };
}

celix::IBundleActivator* celix::createBundleActivator(celix::BundleContext &ctx) {
    return new BundleActivator{ctx};
}

