# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

add_library(cxx_services_example_api INTERFACE)
target_include_directories(cxx_services_example_api INTERFACE api)

add_celix_bundle(cxx_foo
        VERSION 1.0.0
        SOURCES
        foo/Foo.cc
        foo/Foo.h
        foo/FooActivator.cc
        foo/FooActivator.h
    )
target_link_libraries(cxx_foo PRIVATE cxx_services_example_api)

add_celix_bundle(cxx_bar
        VERSION 1.0.0
        SOURCES
            bar/Bar.cc
            bar/Bar.h
            bar/BarActivator.cc
            bar/BarActivator.h
)
target_link_libraries(cxx_bar PRIVATE cxx_services_example_api)

add_celix_bundle(cxx_baz
    VERSION 1.0.0
    SOURCES
        baz/Baz.cc
        baz/Baz.h
        baz/BazActivator.cc
        baz/BazActivator.h
)
target_link_libraries(cxx_baz PRIVATE cxx_services_example_api)

add_celix_container(best_practice_example_cxx
    GROUP services_example
    COPY
    BUNDLES
        Celix::shell
        Celix::shell_tui
        cxx_bar
        cxx_foo
        cxx_baz
    PROPERTIES
        example=value
)
