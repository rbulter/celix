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

#include "celix/FrameworkFactory.h"

class BundleTest : public ::testing::Test {
public:
    BundleTest() {
        celix::Properties config{};
        config["org.osgi.framework.storage.clean"] = "onFirstInit";
        config["org.osgi.framework.storage"] = "test-cache"; //TODO tmp dir?
        this->fw_ptr = std::unique_ptr<celix::Framework>{celix::FrameworkFactory::newFramework(std::move(config))};
    }

    ~BundleTest(){}

    celix::Framework& framework() { return *(this->fw_ptr); }
private:
    std::unique_ptr<celix::Framework> fw_ptr{nullptr};
};

TEST_F(BundleTest, getInfoFromFrameworkBundle) {
    auto &bnd = this->framework().getFrameworkBundle();

    long id = bnd.getBundleId();
    EXPECT_EQ(0, id); //framework bundle is 0

    //TODO FIXME returned name is id
//    std::string name = bnd.getBundleName();
//    EXPECT_EQ("system", name);

    std::string sym = bnd.getBundleSymbolicName();
    EXPECT_EQ("framework", sym);

    std::string loc = bnd.getBundleLocation();
    EXPECT_EQ("System Bundle", loc);

    celix::BundleState state = bnd.getState();
    EXPECT_EQ(celix::BundleState::ACTIVE, state);

    //TODO
//    std::string cache = bnd.getBundleCache(); //TODO make getCacheLoc?
//    EXPECT_TRUE(!cache.emtpy());

    //TODO returns "" should be framework version
//    std::string version = bnd.getBundleVersion();
//    EXPECT_EQ("2.2.0", version);
}