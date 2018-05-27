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

#include "gtest/gtest.h"

#include "celix/Framework.h"
#include "celix/FrameworkFactory.h"

class FrameworkTest : public ::testing::Test {
public:
    FrameworkTest() {
        celix::Properties config{};
        config["org.osgi.framework.storage.clean"] = "onFirstInit";
        config["org.osgi.framework.storage"] = "test-cache"; //TODO tmp dir?
        this->fw_ptr = std::unique_ptr<celix::Framework>{celix::FrameworkFactory::newFramework(std::move(config))};
    }

    ~FrameworkTest(){}

    celix::Framework& framework() { return *(this->fw_ptr); }
private:
    std::unique_ptr<celix::Framework> fw_ptr{nullptr};
};

TEST_F(FrameworkTest, TestFrameworkUUID) {
    auto &fw = this->framework();
    std::string uuid = fw.getUUID();
    EXPECT_NE(uuid, "");
}