/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, 2015, Oracle and/or its affiliates. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file AstTuner.h
 *
 * Provides utilities to conduct automated tuning of AstStructures.
 *
 ***********************************************************************/

#include "AstTuner.h"
#include "AstProgram.h"
#include "AstVisitor.h"
#include "RamExecutor.h"
#include "RamStatement.h"
#include "RamTranslator.h"

namespace souffle {

namespace {

/**
 * A execution strategy wrapper for the guided interpreter recording
 * execution times and scheduling decisions.
 */
class Profiler {
public:
    /** The type of data to be recorded -- Clauses are identified by ther source location */
    typedef std::map<AstSrcLocation, std::vector<ExecutionSummary>> Data;

private:
    /** The nested scheduler */
    const QueryExecutionStrategy& nested;

    /** The recorded data */
    Data& data;

public:
    Profiler(const QueryExecutionStrategy& strategy, Data& data) : nested(strategy), data(data) {}

    /**
     * Processes the given query by forwarding the call to the nested strategy an
     * recording its performance.
     */
    ExecutionSummary operator()(const RamInsert& insert, RamEnvironment& env, std::ostream* report) const {
        // run nested strategy
        auto res = nested(insert, env, report);

        // record the execution summary
        data[insert.getOrigin().getSrcLoc()].push_back(res);

        // act like a wrapper
        return res;
    }
};
}  // namespace

// splits up a string into a vector given a delimiter
std::vector<std::string> split(std::string s, char delim, int times = -1) {
    std::vector<std::string> res;
    std::stringstream ss(s);
    std::string item;

    while ((times > 0 || times <= -1) && std::getline(ss, item, delim)) {
        res.push_back(item);
        times--;
    }

    return res;
}

// checks whether an element is contained within a set
template <class T>
bool contains(std::set<T> set, T element) {
    return set.find(element) != set.end();
}

// ensures that every relation or rule not specified by
// the auto-schedule option is ignored by the scheduler
void setFixedExecutionPlans(const AstProgram* program) {
    // relation names
    std::set<std::string> relsToSchedule;

    // line numbers
    std::set<int> linesToSchedule;

    // (relation name, line) pairs
    std::set<std::tuple<std::string, int>> rulesToSchedule;

    for (std::string token : split(Global::config().get("auto-schedule"), ',')) {
        std::vector<std::string> vec = split(token, ';');
        if (vec.size() == 2) {
            try {
                rulesToSchedule.insert(std::make_tuple(vec[0], std::stoi(vec[1])));
                continue;
            } catch (const std::invalid_argument&) {}
        }
        try {
            linesToSchedule.insert(std::stoi(token));
        } catch (const std::invalid_argument&) {
            relsToSchedule.insert(token);
        }
    }

    for (AstRelation* rel : program->getRelations()) {
        std::string relName = rel->getName().getNames()[0];
        if (!contains(relsToSchedule, relName)) {
            for (AstClause* clause : rel->getClauses()) {
                // only schedule one rule per line to speed things up
                int line = clause->getSrcLoc().start.line;
                if (contains(linesToSchedule, line)) {
                    linesToSchedule.erase(line);
                } else if (!contains(rulesToSchedule, std::make_tuple(relName, line))) {
                    clause->setFixedExecutionPlan();
                }
            }
        }
    }
}

bool AutoScheduleTransformer::transform(AstTranslationUnit& translationUnit) {
    // if relations or rules were specified, ignore everything other than those specified
    if (!Global::config().get("auto-schedule").empty()) {
        setFixedExecutionPlans(translationUnit.getProgram());
    }

    bool changed = false;
    if (!Global::config().get("debug-report").empty()) {
        std::stringstream report;
        changed = autotune(translationUnit, &report);
        translationUnit.getDebugReport().addSection(
                DebugReporter::getCodeSection("auto-schedule", "Auto Schedule Report", report.str()));
    } else {
        changed = autotune(translationUnit, nullptr);
    }
    return changed;
}

bool AutoScheduleTransformer::autotune(AstTranslationUnit& translationUnit, std::ostream* report) {
    const QueryExecutionStrategy& strategy = ScheduledExecution;
    bool verbose = Global::config().has("verbose");

    // start with status message
    if (verbose) {
        std::cout << "\n";
    }
    if (verbose) {
        std::cout << "----------------- Auto-Scheduling Started -----------------\n";
    }

    // step 1 - translate to RAM program
    if (verbose) {
        std::cout << "[ Converting to RAM Program ...                           ]\n";
    }

    /* translate AST to RAM */
    std::unique_ptr<RamProgram> prog =
            RamTranslator(false, Global::config().has("log-iterations")).translateProgram(translationUnit);
    const RamStatement* stmt = prog->getMain();

    // check whether there is something to tune
    if (!stmt) {
        if (verbose) {
            std::cout << "[                                     No Rules in Program ]\n";
            std::cout << "---------------- Auto-Scheduling Completed ----------------\n";
        }
        return false;
    }

    if (verbose) {
        std::cout << "[                                                    Done ]\n";
    }

    // step 2 - run in interpreted mode, collect decisions
    if (verbose) {
        std::cout << "[ Profiling RAM Program ...                               ]\n";
    }

    Profiler::Data data;
    Profiler profiler(strategy, data);

    // create a copy of the symbol table
    souffle::SymbolTable table = translationUnit.getSymbolTable();

    // create interpreter instance
    RamGuidedInterpreter interpreter(profiler);

    if (report && verbose) {
        SplitStream splitStream(report, &std::cout);
        interpreter.setReportTarget(splitStream);
    } else if (report) {
        interpreter.setReportTarget(*report);
    } else if (verbose) {
        interpreter.setReportTarget(std::cout);
    }

    // run interpreter
    interpreter.execute(table, *prog);

    if (verbose) {
        std::cout << "[                                                    Done ]\n";
    }

    if (verbose) {
        std::cout << "Data:\n";
        for (const auto& cur : data) {
            std::cout << "Clause @ " << cur.first << "\n";
            for (const ExecutionSummary& instance : cur.second) {
                std::cout << "\t" << instance.order << " in " << instance.time << "ms\n";
            }
        }
    }

    // step 3 - process collected data ..
    if (verbose) {
        std::cout << "[ Selecting most significant schedules ...                ]\n";
    }

    std::map<AstSrcLocation, const AstClause*> clauses;
    visitDepthFirst(*translationUnit.getProgram(),
            [&](const AstClause& clause) { clauses[clause.getSrcLoc()] = &clause; });

    std::map<const AstClause*, long> longestTime;
    std::map<const AstClause*, Order> bestOrders;

    // extract best order for each clause
    for (const auto& cur : data) {
        const AstClause* clause = clauses[cur.first];
        assert(clause && "Unknown clause discovered!");
        for (const ExecutionSummary& instance : cur.second) {
            if (longestTime[clause] < instance.time) {
                longestTime[clause] = instance.time;
                bestOrders[clause] = instance.order;
            }
        }
    }

    if (verbose) {
        for (const auto& cur : bestOrders) {
            std::cout << *cur.first << "\n Best Order: " << cur.second
                      << "\n Time: " << longestTime[cur.first] << "\n\n";
        }
    }

    if (verbose) {
        std::cout << "[                                                    Done ]\n";
    }

    // step 4 - apply transformations
    if (verbose) {
        std::cout << "[ Re-scheduling rules ...                                 ]\n";
    }

    bool changed = false;
    for (const auto& cur : bestOrders) {
        AstClause* clause = const_cast<AstClause*>(cur.first);
        bool orderChanged = false;
        const std::vector<unsigned>& newOrder = cur.second.getOrder();

        // Check whether best order is different to the original order
        for (unsigned int i = 0; i < clause->getAtoms().size(); i++) {
            if (newOrder[i] != i) {
                orderChanged = true;
                break;
            }
        }
        if (orderChanged) {
            clause->reorderAtoms(newOrder);
            changed = true;
        }
    }

    if (verbose) {
        std::cout << "[                                                    Done ]\n";
    }

    // end with status message
    if (verbose) {
        std::cout << "---------------- Auto-Scheduling Completed -----------------\n";
    }

    return changed;
}

}  // end of namespace souffle
