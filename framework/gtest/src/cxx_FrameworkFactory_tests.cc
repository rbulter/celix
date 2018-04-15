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

TEST(FrameworkFactoryTest, CreateDestroyTest) {
    celix::Framework *fw1 = celix::FrameworkFactory::newFramework();
    EXPECT_NE(fw1, nullptr);
    delete fw1;

    fw1 = celix::FrameworkFactory::newFramework();
    EXPECT_NE(fw1, nullptr);
    fw1->start(); //NOTE should already be done
    fw1->stop();
    fw1->waitForStop();
    delete fw1;
}

TEST(FrameworkFactoryTest, StartStopTest) {
    celix::Properties config1{};
    config1["org.osgi.framework.storage.clean"] = "onFirstInit";
    config1["org.osgi.framework.storage"] = "test-cache1";

    celix::Properties config2 = config1;
    config2["org.osgi.framework.storage"] = "test-cache2";

    celix::Framework *fw1 = celix::FrameworkFactory::newFramework(config1);
    celix::Framework *fw2 = celix::FrameworkFactory::newFramework(config2);

    EXPECT_NE(fw1, nullptr);
    EXPECT_NE(fw2, nullptr);
    EXPECT_NE(fw1, fw2);

    delete fw1;
    delete fw2;
}