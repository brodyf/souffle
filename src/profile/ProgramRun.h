/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2016, The Souffle Developers. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

#pragma once

#include "Relation.h"
#include "StringUtils.h"
#include "Table.h"
#include <chrono>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace souffle {
namespace profile {

/*
 * Stores the relations of the program
 * ProgramRun -> Relations -> Iterations/Rules
 */
class ProgramRun {
private:
    std::unordered_map<std::string, std::shared_ptr<Relation>> relationMap;
    std::chrono::microseconds startTime{0};
    std::chrono::microseconds endTime{0};

public:
    ProgramRun() : relationMap() {}

    inline void setStarttime(std::chrono::microseconds time) {
        startTime = time;
    }

    inline void setEndtime(std::chrono::microseconds time) {
        endTime = time;
    }

    inline void setRelationMap(std::unordered_map<std::string, std::shared_ptr<Relation>>& relationMap) {
        this->relationMap = relationMap;
    }

    std::string toString() {
        std::ostringstream output;
        output << "ProgramRun:" << getRuntime() << "\nRelations:\n";
        for (auto r = relationMap.begin(); r != relationMap.end(); ++r) {
            output << r->second->toString() << "\n";
        }
        return output.str();
    }

    inline const std::unordered_map<std::string, std::shared_ptr<Relation>>& getRelationMap() const {
        return relationMap;
    }

    std::string getRuntime() const {
        if (startTime == endTime) {
            return "--";
        }
        return formatTime(endTime - startTime);
    }

    std::chrono::microseconds getStarttime() const {
        return startTime;
    }

    std::chrono::microseconds getEndtime() const {
        return endTime;
    }

    std::chrono::microseconds getTotalLoadtime() const {
        std::chrono::microseconds result{0};
        for (auto& item : relationMap) {
            result += item.second->getLoadtime();
        }
        return result;
    }

    std::chrono::microseconds getTotalSavetime() const {
        std::chrono::microseconds result{0};
        for (auto& item : relationMap) {
            result += item.second->getSavetime();
        }
        return result;
    }

    size_t getTotalSize() const {
        size_t result = 0;
        for (auto& item : relationMap) {
            result += item.second->size();
        }
        return result;
    }

    size_t getTotalRecursiveSize() const {
        size_t result = 0;
        for (auto& item : relationMap) {
            result += item.second->getTotalRecursiveRuleSize();
        }
        return result;
    }

    std::chrono::microseconds getTotalCopyTime() const {
        std::chrono::microseconds result{0};
        for (auto& item : relationMap) {
            result += item.second->getCopyTime();
        }
        return result;
    }

    std::chrono::microseconds getTotalTime() const {
        std::chrono::microseconds result{0};
        for (auto& item : relationMap) {
            result += item.second->getRecTime();
        }
        return result;
    }

    const Relation* getRelation(const std::string& name) const {
        if (relationMap.find(name) != relationMap.end()) {
            return &(*relationMap.at(name));
        }
        return nullptr;
    }

    std::set<std::shared_ptr<Relation>> getRelationsAtTime(
            std::chrono::microseconds start, std::chrono::microseconds end) const {
        std::set<std::shared_ptr<Relation>> result;
        for (auto& cur : relationMap) {
            if (cur.second->getStarttime() <= end && cur.second->getEndtime() >= start) {
                result.insert(cur.second);
            }
        }
        return result;
    }

    inline std::string formatTime(std::chrono::microseconds runtime) const {
        return Tools::formatTime(runtime);
    }

    inline std::string formatNum(int precision, long number) const {
        return Tools::formatNum(precision, number);
    }

    inline std::vector<std::vector<std::string>> formatTable(Table& table, int precision) const {
        return Tools::formatTable(table, precision);
    }
};

}  // namespace profile
}  // namespace souffle