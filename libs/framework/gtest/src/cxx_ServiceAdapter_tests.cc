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

class ServiceAdapterTest : public ::testing::Test {
public:
    ServiceAdapterTest() {
        celix::Properties config{};
        config["org.osgi.framework.storage.clean"] = "onFirstInit";
        config["org.osgi.framework.storage"] = "test-cache"; //TODO tmp dir?
        this->fw_ptr = std::unique_ptr<celix::Framework>{celix::FrameworkFactory::newFramework(std::move(config))};
    }

    ~ServiceAdapterTestd(){}

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


/*This service can be used with the DefaulServiceWrapper*/
class IHelloService {
public:
    static constexpr const char * const SERVICE_NAME = "IHelloService";
    static constexpr const char * const SERVICE_VERSION = "1.0.0";

    virtual ~IHelloService() = default;
    virtual std::string hello() = 0;
};

TEST_F(ServiceAdapterTest, RegisterServiceWithWrapperTest) {
    auto &ctx = this->framework().getFrameworkContext();

    class HelloServiceImpl : public IHelloService {
    public:
        virtual ~HelloServiceImpl() = default;
        std::string hello() override {
            return std::string{"hello1"};
        }
    };

    HelloServiceImpl svc{};
    IHelloService* svcPointer = &svc;
    long svcId = ctx.registerService<IHelloService>(svcPointer);

    //use without wrapper
    bool called;
    called = ctx.useService<IHelloService>(IHelloService::SERVICE_NAME, [](IHelloService &svc) {
        ASSERT_EQ(std::string{"hello1"}, svc.hello());
    });
    ASSERT_TRUE(called);

    //use with wrapper
    called = ctx.useService<IHelloService>([](IHelloService &svc) {
        auto result = svc.hello();
        ASSERT_EQ(std::string{"hello1"}, result);
    });
    ASSERT_TRUE(called);

    ctx.unregisterService(svcId);
}

/* Because this service has no SERVICE_NAME/SERVICE_VERSION is can not be wrapped by the DefaultServiceWrapper.
 * Custom wrapper is needed to achieve the same effect
 */
class IHelloServiceNoDefaultWrapper {
public:
    virtual ~IHelloServiceNoDefaultWrapper() = default;
    virtual std::string hello() = 0;
};

class HelloServiceRegistrationAdapter : public celix::IServiceRegistrationAdapterFactory<IHelloServiceNoDefaultWrapper, IHelloServiceNoDefaultWrapper> {
public:
    class ServiceAdapter : public celix::IServiceAdapter<IHelloServiceNoDefaultWrapper> {
    public:
        ServiceAdapter(IHelloServiceNoDefaultWrapper *_svc) : svc{_svc} {}
        virtual ~ServiceAdapter() = default;
        IHelloServiceNoDefaultWrapper* adapt() override { return svc; }
    private:
        IHelloServiceNoDefaultWrapper* svc;
    };

    virtual ~HelloServiceRegistrationAdapter() = default;
    std::string serviceName() override { return "HelloService"; }
    std::string serviceVersion() override { return "1.0.0"; }
    std::string serviceLanguage() override { return celix::Constants::SERVICE_CXX_LANG; }
    celix::IServiceAdapter<IHelloServiceNoDefaultWrapper>* createAdapter(IHelloServiceNoDefaultWrapper *svc) override { return new ServiceAdapter{svc}; }
};

celix::IServiceRegistrationAdapterFactory<IHelloServiceNoDefaultWrapper,IHelloServiceNoDefaultWrapper>& serviceRegistrationAdapterFactoryFor(IHelloServiceNoDefaultWrapper */*dummy*/) {
    static HelloServiceRegistrationAdapter factory{};
    return factory;
}

class HelloServiceUsageAdapter : public celix::IServiceUsageAdapterFactory<IHelloServiceNoDefaultWrapper> {
public:
    class ServiceAdapter : public celix::IServiceAdapter<IHelloServiceNoDefaultWrapper> {
    public:
        ServiceAdapter(void *_svc) : svc{static_cast<IHelloServiceNoDefaultWrapper*>(_svc)} {}
        virtual ~ServiceAdapter() = default;
        IHelloServiceNoDefaultWrapper* adapt() override { return svc; }
    private:
        IHelloServiceNoDefaultWrapper* svc;
    };

    virtual ~HelloServiceUsageAdapter() = default;
    std::string serviceName() override { return "HelloService"; }
    std::string serviceVersionRange() override { return "[1,2)"; }
    std::string serviceLanguage() override { return celix::Constants::SERVICE_CXX_LANG; }
    celix::IServiceAdapter<IHelloServiceNoDefaultWrapper>* createAdapter(void *registeredSvc) override { return new ServiceAdapter(registeredSvc); }
};



celix::IServiceUsageAdapterFactory<IHelloServiceNoDefaultWrapper>& serviceUsageAdapterFactoryFor(IHelloServiceNoDefaultWrapper*) {
    static HelloServiceUsageAdapter factory{};
    return factory;
}

TEST_F(ServiceAdapterTest, RegisterServiceWithCustomWrapperTest) {
    auto &ctx = this->framework().getFrameworkContext();

    class HelloServiceImpl : public IHelloServiceNoDefaultWrapper {
    public:
        virtual ~HelloServiceImpl() = default;
        std::string hello() override {
            return std::string{"hello1"};
        }
    };

    HelloServiceImpl svc{};
    long svcId = ctx.registerService<IHelloServiceNoDefaultWrapper>(&svc);

    //use without wrapper
    bool called = ctx.useService<IHelloServiceNoDefaultWrapper>("HelloService", [](IHelloServiceNoDefaultWrapper &svc) {
        ASSERT_EQ(std::string{"hello1"}, svc.hello());
    });
    ASSERT_TRUE(called);

    //use with wrapper
    called = ctx.useService<IHelloServiceNoDefaultWrapper>([](IHelloServiceNoDefaultWrapper &svc) {
        ASSERT_EQ(std::string{"hello1"}, svc.hello());
    });
    ASSERT_TRUE(called);

    ctx.unregisterService(svcId);
}

#define HELLO_SERVICE_NAME "hello_service"
#define HELLO_SERVICE_VERSION "1.0.0"

//The underlining c service
typedef struct do_service {
    void *handle;
    int (*do_something)(void* handle);
} do_service_t;

//The C++ service to use in the C++ context
class IDoService {
public:
    virtual ~IDoService() = default;
    virtual int do_something() = 0;
};

class DoServiceToCWrapper : public celix::IServiceRegistrationAdapterFactory<IDoService, do_service_t> {
public:
    class ServiceAdapter : public celix::IServiceAdapter<do_service_t> {
    public:
        ServiceAdapter(IDoService *svc) {
            cSvc.handle = static_cast<void*>(svc);
            cSvc.do_something = [](void *handle) -> int {
                auto* s = static_cast<IDoService*>(handle);
                return s->do_something();
            };
        }
        virtual ~ServiceAdapter() = default;
        do_service_t* adapt() override { return &cSvc; }
    private:
        do_service_t cSvc{};
    };

    virtual ~DoServiceToCWrapper() = default;
    std::string serviceName() override { return "do_service"; }
    std::string serviceVersion() override { return "1.0.0"; }
    std::string serviceLanguage() override { return celix::Constants::SERVICE_C_LANG; }
    celix::IServiceAdapter<do_service_t>* createAdapter(IDoService* svc) override { return new ServiceAdapter{svc}; }
};

celix::IServiceRegistrationAdapterFactory<IDoService, do_service_t>& serviceRegistrationAdapterFactoryFor(IDoService */*dummy*/) {
    static DoServiceToCWrapper factory{};
    return factory;
}

class DoServiceFromCWrapper : public celix::IServiceUsageAdapterFactory<IDoService> {
public:
    class ServiceAdapter : public celix::IServiceAdapter<IDoService>, public IDoService {
    public:
        ServiceAdapter(void *_svc) : cSvc{static_cast<do_service_t*>(_svc)} {}
        virtual ~ServiceAdapter() = default;
        IDoService* adapt() override { return this; }

        int do_something() override {
            return cSvc->do_something(cSvc->handle);
        }
    private:
        do_service_t* cSvc;
    };

    virtual ~DoServiceFromCWrapper() = default;
    std::string serviceName() override { return "do_service"; }
    std::string serviceVersionRange() override { return "[1,2)"; }
    std::string serviceLanguage() override { return celix::Constants::SERVICE_C_LANG; }
    celix::IServiceAdapter<IDoService>* createAdapter(void *registeredSvc) override { return new ServiceAdapter{registeredSvc}; }
};

celix::IServiceUsageAdapterFactory<IDoService>& serviceUsageAdapterFactoryFor(IDoService* /*dummy_used_for_infer*/) {
    static DoServiceFromCWrapper factory{};
    return factory;
}

TEST_F(ServiceAdapterTest, RegisterServiceWithCxxToCWrapperTest) {
    auto &ctx = this->framework().getFrameworkContext();

    class DoServiceImpl : public IDoService {
    public:
        virtual ~DoServiceImpl() = default;
        int do_something() override {
            return 42;
        }
    };

    DoServiceImpl svc{};
    long svcId = ctx.registerService<IDoService>(&svc);
    ASSERT_TRUE(svcId >= 0);
    //TODO assert properties has C as svc lang

    bool called = ctx.useService<IDoService>([](IDoService &svc) {
        int result = svc.do_something();
        ASSERT_EQ(42, result);
    });
    ASSERT_TRUE(called);


    called = false;
    long trkId = ctx.trackService<IDoService>([&](IDoService *svc) {
        called = true;
        ASSERT_EQ(42, svc->do_something());
    });
    ASSERT_TRUE(trkId >= 0);
    ASSERT_TRUE(called);
    ctx.stopTracker(trkId);

    ctx.unregisterService(svcId);
}