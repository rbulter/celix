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

class BundleContextTest : public ::testing::Test {
public:
    BundleContextTest() {
        celix::Properties config{};
        config["org.osgi.framework.storage.clean"] = "onFirstInit";
        config["org.osgi.framework.storage"] = "test-cache"; //TODO tmp dir?
        this->fw_ptr = std::unique_ptr<celix::Framework>{celix::FrameworkFactory::newFramework(std::move(config))};
    }

    ~BundleContextTest(){}

    celix::Framework& framework() { return *(this->fw_ptr); }
private:
    std::unique_ptr<celix::Framework> fw_ptr{nullptr};
};

//Test interface
class ITestSvc {
public:
    static constexpr const char * const NAME = "ITestSvc";

    virtual ~ITestSvc(){};
    virtual int calc(int input) = 0;
};

//Test implementation
class TestImpl : public ITestSvc {
public:
    virtual ~TestImpl(){};
    int calc(int input) override { return input * 42; }
};


TEST_F(BundleContextTest, TestInstallBundle) {
    auto &ctx = this->framework().getFrameworkContext();

    long id;

    //invalid
    id = ctx.installBundle("Invalid loc", false);
    EXPECT_TRUE(id < 0);

    id = ctx.installBundle("bundle1.zip", false);
    EXPECT_TRUE(id > 0);

    long again = ctx.installBundle("bundle1.zip", false);
    EXPECT_EQ(id, again);
}

TEST_F(BundleContextTest, RegisterCServiceTest) {
    struct test_svc {
        void *dummy;
    };

    auto &ctx = this->framework().getFrameworkContext();

    test_svc svc1;

    long svcId = ctx.registerCService(&svc1, "test service");
    EXPECT_TRUE(svcId > 0);
    ctx.unregisterService(svcId);

    long svcId2 = ctx.registerCService(&svc1, "test service");
    EXPECT_TRUE(svcId2 > 0);
    EXPECT_NE(svcId, svcId2); //new registration new id
    ctx.unregisterService(svcId2);

    //NOTE compile error -> cxxSvc is not POD
    //TestImpl cxxSvc{};
    //ctx.registerCService(ITestSvc::NAME, &cxxSvc);
}

TEST_F(BundleContextTest, RegisterServiceTest) {
    auto &ctx = this->framework().getFrameworkContext();

    TestImpl svc1;

    long svcId = ctx.registerService<ITestSvc>(&svc1, ITestSvc::NAME);
    EXPECT_TRUE(svcId > 0);
    ctx.unregisterService(svcId);

    long svcId2 = ctx.registerService<ITestSvc>(&svc1, ITestSvc::NAME);
    EXPECT_TRUE(svcId2 > 0);
    EXPECT_NE(svcId, svcId2); //new registration new id
    ctx.unregisterService(svcId2);
}

TEST_F(BundleContextTest, UseService) {
    auto &ctx = this->framework().getFrameworkContext();

    TestImpl svc1;

    long svcId = ctx.registerService<ITestSvc>(&svc1, ITestSvc::NAME);
    EXPECT_TRUE(svcId > 0);


    int result = -1;
    std::function<void(ITestSvc &svc, const celix::Properties&, const celix::Bundle&)> func = [&result](ITestSvc &svc, const celix::Properties&, const celix::Bundle&) {
        result = svc.calc(1);
    };
    bool called = ctx.useService<ITestSvc>(ITestSvc::NAME, func);
    EXPECT_TRUE(called);
    EXPECT_EQ(result, 42);

//    result = -1;
//    called = ctx.useServiceWithId<ITestSvc>(svcId, ITestSvc::NAME, [&result](ITestSvc &svc, const celix::Properties&, const celix::Bundle&) {
//        result = svc.calc(2);
//    });
//    EXPECT_TRUE(called);
//    EXPECT_EQ(result, 84);

    ctx.unregisterService(svcId);
}

TEST_F(BundleContextTest, UseServices) {
    auto &ctx = this->framework().getFrameworkContext();

    TestImpl svc;

    long svcId1 = ctx.registerService<ITestSvc>(&svc, "test service");
    EXPECT_TRUE(svcId1 > 0);

    long svcId2 = ctx.registerService<ITestSvc>(&svc, "test service");
    EXPECT_TRUE(svcId2 > 0);


    int result = 0;
    auto func = [&result](ITestSvc &svc, const celix::Properties&, const celix::Bundle&) {
        result += svc.calc(1);
    };
    ctx.useServices<ITestSvc>("test service", func);
    EXPECT_EQ(result, 84); //two times

    ctx.unregisterService(svcId1);

    ctx.useServices<ITestSvc>("test service", func);
    EXPECT_EQ(result, 126); //one time

    ctx.unregisterService(svcId2);
}


TEST_F(BundleContextTest, TrackService) {
    auto &ctx = this->framework().getFrameworkContext();

    int count = 0;

    ITestSvc *svc1 = (ITestSvc*)0x100; //no ranking
    ITestSvc *svc2 = (ITestSvc*)0x200; //no ranking
    ITestSvc *svc3 = (ITestSvc*)0x300; //10 ranking
    ITestSvc *svc4 = (ITestSvc*)0x400; //5 ranking


    auto set = [&](ITestSvc *svc) {
        static int callCount = 0;
        callCount += 1;
        if (callCount == 1) {
            //first time svc1 should be set (oldest service with equal ranking
            EXPECT_EQ(svc1, svc);
        } else if (callCount == 2) {
            EXPECT_EQ(svc3, svc);
            //second time svc3 should be set (highest ranking)
        } else if (callCount == 3) {
            //third time svc4 should be set (highest ranking
            EXPECT_EQ(svc4, svc);
        }

        count = callCount;
    };

    long svcId1 = ctx.registerService(svc1, "NA");
    long svcId2 = ctx.registerService(svc2, "NA");

    //starting tracker should lead to first set call
    long trackerId = ctx.trackService<ITestSvc>("NA", set);
    EXPECT_TRUE(trackerId > 0);

    //register svc3 should lead to second set call
    celix::Properties props3{};
    props3[OSGI_FRAMEWORK_SERVICE_RANKING] = "10";
    long svcId3 = ctx.registerService(svc3, "NA", std::move(props3));

    //register svc4 should lead to no set (lower ranking)
    celix::Properties props4{};
    props4[OSGI_FRAMEWORK_SERVICE_RANKING] = "10";
    long svcId4 = ctx.registerService(svc4, "NA", props4);

    //unregister svc3 should lead to set (new highest ranking)
    ctx.unregisterService(svcId3);

    ctx.stopTracker(trackerId);
    ctx.unregisterService(svcId1);
    ctx.unregisterService(svcId4);
    ctx.unregisterService(svcId2);

    EXPECT_EQ(3, count); //check if the set is called the expected times
}

TEST_F(BundleContextTest, useBundleTest) {
    auto &ctx = this->framework().getFrameworkContext();
    int count = 0;

    ctx.useBundle(0, [&count](const celix::Bundle &bnd) {
        count++;
        long id = bnd.getBundleId();
        EXPECT_EQ(0, id);
    });

    EXPECT_EQ(1, count);
};


TEST_F(BundleContextTest, useBundlesTest) {
    auto &ctx = this->framework().getFrameworkContext();
    int count = 0;

    auto use = [&count](const celix::Bundle &bnd) {
        count++;
        long id = bnd.getBundleId();
        EXPECT_TRUE(id >= 0);
    };

    ctx.useBundles(use);
    EXPECT_EQ(1, count);

    count = 0;
    ctx.installBundle("bundle1.zip", true);
    ctx.useBundles(use);
    EXPECT_EQ(2, count);
};
