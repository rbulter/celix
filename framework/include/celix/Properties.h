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

#ifndef CXX_CELIX_PROPERTIES_H
#define CXX_CELIX_PROPERTIES_H

#include <string>
#include <map>

namespace celix {

    using Properties = std::map<std::string, std::string>;

    inline const std::string& getProperty(const Properties& props, const std::string &key, const std::string &defaultValue) noexcept {
        auto it = props.find(key);
        if (it != props.end()) {
            return props.at(key);
        } else {
            return defaultValue;
        }
    }

    inline int getProperty(const Properties& props, const std::string &key, int defaultValue)  noexcept {
        std::string val = getProperty(props, key, std::to_string(defaultValue));
        return std::stoi(val, nullptr, 10);
    }

    inline unsigned int getProperty(const Properties& props, const std::string &key, unsigned int defaultValue) noexcept {
        std::string val = getProperty(props, key, std::to_string(defaultValue));
        return static_cast<unsigned  int>(std::stoul(val, nullptr, 10)); //NOTE no std::stou ??
    }

    inline long getProperty(const Properties& props, const std::string &key, long defaultValue) noexcept {
        std::string val = getProperty(props, key, std::to_string(defaultValue));
        return std::stol(val, nullptr, 10);
    }

    inline unsigned int getProperty(const Properties& props, const std::string &key, unsigned long defaultValue) noexcept {
        std::string val = getProperty(props, key, std::to_string(defaultValue));
        return std::stoul(val, nullptr, 10);
    }
}

#endif //CXX_CELIX_PROPERTIES_H
