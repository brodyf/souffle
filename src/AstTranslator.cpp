/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2018, The Souffle Developers. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file AstTranslator.cpp
 *
 * Implementations of a translator from AST to RAM structures.
 *
 ***********************************************************************/

#include "Global.h"
#include "AstTranslator.h"
#include "AstVisitor.h"
#include "LogStatement.h"
#include "Util.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <typeinfo>
#include <utility>
#include <vector>

namespace souffle {

	SymbolMask getSymbolMask(const AstRelation& rel, const TypeEnvironment& typeEnv) {
    auto arity = rel.getArity();
    SymbolMask res(arity);
    for (size_t i = 0; i < arity; i++) {
        res.setSymbol(i, isSymbolType(typeEnv.getType(rel.getAttribute(i)->getTypeName())));
    }
    return res;
}

	/* utility for appending statements */
static void appendStmt(std::unique_ptr<RamStatement>& stmtList, std::unique_ptr<RamStatement> stmt) {
    if (stmt) {
        if (stmtList) {
            RamSequence* stmtSeq;
            if ((stmtSeq = dynamic_cast<RamSequence*>(stmtList.get()))) {
                stmtSeq->add(std::move(stmt));
            } else {
                stmtList = std::make_unique<RamSequence>(std::move(stmtList), std::move(stmt));
            }
        } else {
            stmtList = std::move(stmt);
        }
    }
};


	/**
 * Converts the given relation identifier into a relation name.
 */
std::string getRelationName(const AstRelationIdentifier& id) {
    return toString(join(id.getNames(), "-"));
}

std::unique_ptr<RamRelation> getRamRelation(const AstRelation* rel, const TypeEnvironment* typeEnv,
        std::string name, size_t arity, const bool istemp = false, const bool hashset = false) {
    // avoid name conflicts for temporary identifiers
    if (istemp) {
        name.insert(0, "@");
    }

    if (!rel) {
        return std::make_unique<RamRelation>(name, arity, istemp, hashset);
    }

    assert(arity == rel->getArity());
    std::vector<std::string> attributeNames;
    std::vector<std::string> attributeTypeQualifiers;
    for (unsigned int i = 0; i < arity; i++) {
        attributeNames.push_back(rel->getAttribute(i)->getAttributeName());
        if (typeEnv) {
            attributeTypeQualifiers.push_back(
                    getTypeQualifier(typeEnv->getType(rel->getAttribute(i)->getTypeName())));
        }
    }

    return std::make_unique<RamRelation>(name, arity, attributeNames, attributeTypeQualifiers,
            getSymbolMask(*rel, *typeEnv), rel->isInput(), rel->isComputed(), rel->isOutput(), rel->isBTree(),
            rel->isRbtset(), rel->isHashset(), rel->isBrie(), rel->isEqRel(), istemp);
}

std::unique_ptr<RamValue> AstTranslator::translateValue(const AstArgument* arg) {
#if 0
    class ValueTranslator : public AstVisitor<std::unique_ptr<RamValue>, const ValueIndex&> {
    public:
        ValueTranslator() = default;

        std::unique_ptr<RamValue> visitVariable(const AstVariable& var, const ValueIndex& index) override {
            ASSERT(index.isDefined(var) && "variable not grounded");
            const Location& loc = index.getDefinitionPoint(var);
            return std::make_unique<RamElementAccess>(loc.level, loc.component, loc.name);
        }

        std::unique_ptr<RamValue> visitUnnamedVariable(const AstUnnamedVariable& var, const ValueIndex& index) override {
            return nullptr;  // utilised to identify _ values
        }

        std::unique_ptr<RamValue> visitConstant(const AstConstant& c, const ValueIndex& index) override {
            return std::make_unique<RamNumber>(c.getIndex());
        }

        /* TODO: Visitors for n-ary functors, see **RamIntrinsic**.
        std::unique_ptr<RamValue> visitUnaryFunctor(const AstUnaryFunctor& uf, const ValueIndex& index) override {
            return std::make_unique<RamUnaryOperator>(uf.getFunction(), translateValue(uf.getOperand(), index));
        }

        std::unique_ptr<RamValue> visitBinaryFunctor(const AstBinaryFunctor& bf, const ValueIndex& index) override {
            return std::make_unique<RamBinaryOperator>(
                bf.getFunction(), translateValue(bf.getLHS(), index), translateValue(bf.getRHS(), index));
        }

        std::unique_ptr<RamValue> visitTernaryFunctor(const AstTernaryFunctor& tf, const ValueIndex& index) override {
            return std::make_unique<RamTernaryOperator>(tf.getFunction(), translateValue(tf.getArg(0), index),
                translateValue(tf.getArg(1), index), translateValue(tf.getArg(2), index));
        }

        std::unique_ptr<RamValue> visitCounter(const AstCounter& cnt, const ValueIndex& index) override {
            return std::make_unique<RamAutoIncrement>();
        }
        */

        std::unique_ptr<RamValue> visitRecordInit(const AstRecordInit& init, const ValueIndex& index) override {
            std::vector<std::unique_ptr<RamValue>> values;
            for (const auto& cur : init.getArguments()) {
                values.push_back(translateValue(cur, index));
            }
            return std::make_unique<RamPack>(std::move(values));
        }

        std::unique_ptr<RamValue> visitAggregator(const AstAggregator& agg, const ValueIndex& index) override {
            // here we look up the location the aggregation result gets bound
            auto loc = index.getAggregatorLocation(agg);
            return std::make_unique<RamElementAccess>(loc.level, loc.component, loc.name);
        }

        std::unique_ptr<RamValue> visitSubroutineArgument(const AstSubroutineArgument& subArg, const ValueIndex& index) override {
            return std::make_unique<RamArgument>(subArg.getNumber());
        }
    };

    return ValueTranslator()(*arg, index);
#endif 
    return nullptr;
}

std::unique_ptr<RamStatement> AstTranslator::translateClause(const AstClause& clause,
        const AstProgram* program, const TypeEnvironment* typeEnv) {
	return nullptr;
}


/** generate RAM code for recursive relations in a strongly-connected component */
std::unique_ptr<RamStatement> AstTranslator::translateRecursiveRelation(
        const std::set<const AstRelation*>& scc, const AstProgram* program,
        const RecursiveClauses* recursiveClauses, const TypeEnvironment& typeEnv) {
#if 0
    // initialize sections
    std::unique_ptr<RamStatement> preamble;
    std::unique_ptr<RamSequence> updateTable(new RamSequence());
    std::unique_ptr<RamStatement> postamble;

    // --- create preamble ---

    // mappings for temporary relations
    std::map<const AstRelation*, std::unique_ptr<RamRelation>> rrel;
    std::map<const AstRelation*, std::unique_ptr<RamRelation>> relDelta;
    std::map<const AstRelation*, std::unique_ptr<RamRelation>> relNew;

    /* Compute non-recursive clauses for relations in scc and push
       the results in their delta tables. */
    for (const AstRelation* rel : scc) {
        std::unique_ptr<RamStatement> updateRelTable;

        /* create two temporary tables for relaxed semi-naive evaluation */
        auto relName = getRelationName(rel->getName());
        rrel[rel] = getRamRelation(rel, &typeEnv, relName, rel->getArity(), false, rel->isHashset());
        relDelta[rel] =
                getRamRelation(rel, &typeEnv, "delta_" + relName, rel->getArity(), true, rel->isHashset());
        relNew[rel] =
                getRamRelation(rel, &typeEnv, "new_" + relName, rel->getArity(), true, rel->isHashset());

        modifiedIdMap[relName] = relName;
        modifiedIdMap[relDelta[rel]->getName()] = relName;
        modifiedIdMap[relNew[rel]->getName()] = relName;

        /* create update statements for fixpoint (even iteration) */
        appendStmt(updateRelTable,
                std::make_unique<RamSequence>(
                        std::make_unique<RamMerge>(std::unique_ptr<RamRelation>(rrel[rel]->clone()),
                                std::unique_ptr<RamRelation>(relNew[rel]->clone())),
                        std::make_unique<RamSwap>(std::unique_ptr<RamRelation>(relDelta[rel]->clone()),
                                std::unique_ptr<RamRelation>(relNew[rel]->clone())),
                        std::make_unique<RamClear>(std::unique_ptr<RamRelation>(relNew[rel]->clone()))));

        /* measure update time for each relation */
        if (Global::config().has("profile")) {
            updateRelTable = std::make_unique<RamLogTimer>(std::move(updateRelTable),
                    LogStatement::cRecursiveRelation(toString(rel->getName()), rel->getSrcLoc()));
        }

        /* drop temporary tables after recursion */
        appendStmt(postamble,
                std::make_unique<RamSequence>(
                        std::make_unique<RamDrop>(std::unique_ptr<RamRelation>(relDelta[rel]->clone())),
                        std::make_unique<RamDrop>(std::unique_ptr<RamRelation>(relNew[rel]->clone()))));

        /* Generate code for non-recursive part of relation */
        appendStmt(preamble, translateNonRecursiveRelation(*rel, program, recursiveClauses, typeEnv));

        /* Generate merge operation for temp tables */
        appendStmt(preamble, std::make_unique<RamMerge>(std::unique_ptr<RamRelation>(relDelta[rel]->clone()),
                                     std::unique_ptr<RamRelation>(rrel[rel]->clone())));

        /* Add update operations of relations to parallel statements */
        updateTable->add(std::move(updateRelTable));
    }

    // --- build main loop ---

    std::unique_ptr<RamParallel> loopSeq(new RamParallel());

    // create a utility to check SCC membership
    auto isInSameSCC = [&](
            const AstRelation* rel) { return std::find(scc.begin(), scc.end(), rel) != scc.end(); };

    /* Compute temp for the current tables */
    for (const AstRelation* rel : scc) {
        std::unique_ptr<RamStatement> loopRelSeq;

        /* Find clauses for relation rel */
        for (size_t i = 0; i < rel->clauseSize(); i++) {
            AstClause* cl = rel->getClause(i);

            // skip non-recursive clauses
            if (!recursiveClauses->recursive(cl)) {
                continue;
            }

            // each recursive rule results in several operations
            int version = 0;
            const auto& atoms = cl->getAtoms();
            for (size_t j = 0; j < atoms.size(); ++j) {
                const AstAtom* atom = atoms[j];
                const AstRelation* atomRelation = getAtomRelation(atom, program);

                // only interested in atoms within the same SCC
                if (!isInSameSCC(atomRelation)) {
                    continue;
                }

                // modify the processed rule to use relDelta and write to relNew
                std::unique_ptr<AstClause> r1(cl->clone());
                r1->getHead()->setName(relNew[rel]->getName());
                r1->getAtoms()[j]->setName(relDelta[atomRelation]->getName());
                r1->addToBody(
                        std::make_unique<AstNegation>(std::unique_ptr<AstAtom>(cl->getHead()->clone())));

                // replace wildcards with variables (reduces indices when wildcards are used in recursive
                // atoms)
                nameUnnamedVariables(r1.get());

                // reduce R to P ...
                for (size_t k = j + 1; k < atoms.size(); k++) {
                    if (isInSameSCC(getAtomRelation(atoms[k], program))) {
                        AstAtom* cur = r1->getAtoms()[k]->clone();
                        cur->setName(relDelta[getAtomRelation(atoms[k], program)]->getName());
                        r1->addToBody(std::make_unique<AstNegation>(std::unique_ptr<AstAtom>(cur)));
                    }
                }

                std::unique_ptr<RamStatement> rule =
                        translateClause(*r1, program, &typeEnv, version, false, rel->isHashset());

                /* add logging */
                if (Global::config().has("profile")) {
                    const std::string& relationName = toString(rel->getName());
                    const SrcLocation& srcLocation = cl->getSrcLoc();
                    const std::string clauseText = stringify(toString(*cl));
                    const std::string logTimerStatement =
                            LogStatement::tRecursiveRule(relationName, version, srcLocation, clauseText);
                    const std::string logSizeStatement =
                            LogStatement::nRecursiveRule(relationName, version, srcLocation, clauseText);
                    rule = std::make_unique<RamSequence>(
                            std::make_unique<RamLogTimer>(std::move(rule), logTimerStatement),
                            std::make_unique<RamLogSize>(
                                    std::unique_ptr<RamRelation>(relNew[rel]->clone()), logSizeStatement));
                }

                // add debug info
                std::ostringstream ds;
                ds << toString(*cl) << "\nin file ";
                ds << cl->getSrcLoc();
                rule = std::make_unique<RamDebugInfo>(std::move(rule), ds.str());

                // add to loop body
                appendStmt(loopRelSeq, std::move(rule));

                // increment version counter
                version++;
            }
            assert(cl->getExecutionPlan() == nullptr || version > cl->getExecutionPlan()->getMaxVersion());
        }

        // if there was no rule, continue
        if (!loopRelSeq) {
            continue;
        }

        // label all versions
        if (Global::config().has("profile")) {
            const std::string& relationName = toString(rel->getName());
            const SrcLocation& srcLocation = rel->getSrcLoc();
            const std::string logTimerStatement = LogStatement::tRecursiveRelation(relationName, srcLocation);
            const std::string logSizeStatement = LogStatement::nRecursiveRelation(relationName, srcLocation);
            loopRelSeq = std::make_unique<RamLogTimer>(std::move(loopRelSeq), logTimerStatement);
            appendStmt(loopRelSeq,
                    std::make_unique<RamLogSize>(
                            std::unique_ptr<RamRelation>(relNew[rel]->clone()), logSizeStatement));
        }

        /* add rule computations of a relation to parallel statement */
        loopSeq->add(std::move(loopRelSeq));
    }

    /* construct exit conditions for odd and even iteration */
    auto addCondition = [](std::unique_ptr<RamCondition>& cond, std::unique_ptr<RamCondition> clause) {
        cond = ((cond) ? std::make_unique<RamAnd>(std::move(cond), std::move(clause)) : std::move(clause));
    };

    std::unique_ptr<RamCondition> exitCond;
    for (const AstRelation* rel : scc) {
        addCondition(
                exitCond, std::make_unique<RamEmpty>(std::unique_ptr<RamRelation>(relNew[rel]->clone())));
    }

    /* construct fixpoint loop  */
    std::unique_ptr<RamStatement> res;
    if (preamble) appendStmt(res, std::move(preamble));
    if (!loopSeq->getStatements().empty() && exitCond && updateTable) {
        appendStmt(res, std::make_unique<RamLoop>(std::move(loopSeq),
                                std::make_unique<RamExit>(std::move(exitCond)), std::move(updateTable)));
    }
    if (postamble) {
        appendStmt(res, std::move(postamble));
    }
    if (res) return res;

    assert(false && "Not Implemented");
#endif
    return nullptr;
}

/** generate RAM code for a non-recursive relation */
std::unique_ptr<RamStatement> AstTranslator::translateNonRecursiveRelation(const AstRelation& rel,
        const AstProgram* program, const RecursiveClauses* recursiveClauses, const TypeEnvironment& typeEnv) {
    /* start with an empty sequence */
    std::unique_ptr<RamStatement> res;

    // the ram table reference
    std::unique_ptr<RamRelation> rrel = getRamRelation(
            &rel, &typeEnv, getRelationName(rel.getName()), rel.getArity(), false, rel.isHashset());

    /* iterate over all clauses that belong to the relation */
    for (AstClause* clause : rel.getClauses()) {
        // skip recursive rules
        if (recursiveClauses->recursive(clause)) {
            continue;
        }

        // translate clause
        std::unique_ptr<RamStatement> rule = translateClause(*clause, program, &typeEnv);

        // add logging
        if (Global::config().has("profile")) {
            const std::string& relationName = toString(rel.getName());
            const SrcLocation& srcLocation = clause->getSrcLoc();
            const std::string clauseText = stringify(toString(*clause));
            const std::string logTimerStatement =
                    LogStatement::tNonrecursiveRule(relationName, srcLocation, clauseText);
            const std::string logSizeStatement =
                    LogStatement::nNonrecursiveRule(relationName, srcLocation, clauseText);
            rule = std::make_unique<RamSequence>(
                    std::make_unique<RamLogTimer>(std::move(rule), logTimerStatement),
                    std::make_unique<RamLogSize>(
                            std::unique_ptr<RamRelation>(rrel->clone()), logSizeStatement));
        }

        // add debug info
        std::ostringstream ds;
        ds << toString(*clause) << "\nin file ";
        ds << clause->getSrcLoc();
        rule = std::make_unique<RamDebugInfo>(std::move(rule), ds.str());

        // add rule to result
        appendStmt(res, std::move(rule));
    }

    // if no clauses have been translated, we are done
    if (!res) {
        return res;
    }

    // add logging for entire relation
    if (Global::config().has("profile")) {
        const std::string& relationName = toString(rel.getName());
        const SrcLocation& srcLocation = rel.getSrcLoc();
        const std::string logTimerStatement = LogStatement::tNonrecursiveRelation(relationName, srcLocation);
        const std::string logSizeStatement = LogStatement::nNonrecursiveRelation(relationName, srcLocation);

        // add timer
        res = std::make_unique<RamLogTimer>(std::move(res), logTimerStatement);

        // add table size printer
        appendStmt(res,
                std::make_unique<RamLogSize>(std::unique_ptr<RamRelation>(rrel->clone()), logSizeStatement));
    }

    // done
    return res;
}

/** Translate AST to a RAM program  */
std::unique_ptr<RamProgram> AstTranslator::translateProgram(const AstTranslationUnit& translationUnit) {
    // obtain type environment from analysis
    const TypeEnvironment& typeEnv =
            translationUnit.getAnalysis<TypeEnvironmentAnalysis>()->getTypeEnvironment();

    // obtain recursive clauses from analysis
    const auto* recursiveClauses = translationUnit.getAnalysis<RecursiveClauses>();

    // obtain strongly connected component (SCC) graph from analysis
    const auto& sccGraph = *translationUnit.getAnalysis<SCCGraph>();

    // obtain some topological order over the nodes of the SCC graph
    const auto& sccOrder = translationUnit.getAnalysis<TopologicallySortedSCCGraph>()->order();

    // obtain the schedule of relations expired at each index of the topological order
    const auto& expirySchedule = translationUnit.getAnalysis<RelationSchedule>()->schedule();

    // start with an empty sequence of ram statements
    std::unique_ptr<RamStatement> res = std::make_unique<RamSequence>();

    // handle the case of an empty SCC graph
    if (sccGraph.getNumberOfSCCs() == 0) {
        return std::make_unique<RamProgram>(std::move(res));
    }

    // maintain the index of the SCC within the topological order
    unsigned index = 0;

    // iterate over each SCC according to the topological order
    for (const auto& scc : sccOrder) {
        // make a new ram statement for the current SCC
        std::unique_ptr<RamStatement> current;

        // find out if the current SCC is recursive
        const auto& isRecursive = sccGraph.isRecursive(scc);

        // make variables for particular sets of relations contained within the current SCC, and, predecessors
        // and successor SCCs thereof
        const auto& allInterns = sccGraph.getInternalRelations(scc);
        const auto& internIns = sccGraph.getInternalInputRelations(scc);
        const auto& internOuts = sccGraph.getInternalOutputRelations(scc);
        const auto& externOutPreds = sccGraph.getExternalOutputPredecessorRelations(scc);
        const auto& externNonOutPreds = sccGraph.getExternalNonOutputPredecessorRelations(scc);
        const auto& internNonOutsWithExternSuccs =
                sccGraph.getInternalNonOutputRelationsWithExternalSuccessors(scc);

        // make a variable for all relations that are expired at the current SCC
        const auto& internExps = expirySchedule.at(index).expired();

        // a function to create relations
        const auto& makeRamCreate = [&](const AstRelation* relation, const std::string relationNamePrefix) {
            appendStmt(current,
                    std::make_unique<RamCreate>(std::unique_ptr<RamRelation>(getRamRelation(relation,
                            &typeEnv, relationNamePrefix + getRelationName(relation->getName()),
                            relation->getArity(), !relationNamePrefix.empty(), relation->isHashset()))));
        };

#if 0
        // a function to load relations
        const auto& makeRamLoad = [&](const AstRelation* relation, const std::string& inputDirectory,
                const std::string& fileExtension) {
            appendStmt(current,
                    std::make_unique<RamLoad>(std::unique_ptr<RamRelation>(getRamRelation(relation, &typeEnv,
                                                      getRelationName(relation->getName()),
                                                      relation->getArity(), false, relation->isHashset())),
                            getInputIODirectives(
                                    relation, Global::config().get(inputDirectory), fileExtension)));
        };
#endif

        // a function to print the size of relations
        const auto& makeRamPrintSize = [&](const AstRelation* relation) {
            appendStmt(current, std::make_unique<RamPrintSize>(std::unique_ptr<RamRelation>(getRamRelation(
                                        relation, &typeEnv, getRelationName(relation->getName()),
                                        relation->getArity(), false, relation->isHashset()))));
        };

#if 0
        // a function to store relations
        const auto& makeRamStore = [&](const AstRelation* relation, const std::string& outputDirectory,
                const std::string& fileExtension) {
            appendStmt(current,
                    std::make_unique<RamStore>(std::unique_ptr<RamRelation>(getRamRelation(relation, &typeEnv,
                                                       getRelationName(relation->getName()),
                                                       relation->getArity(), false, relation->isHashset())),
                            getOutputIODirectives(relation, &typeEnv, Global::config().get(outputDirectory), fileExtension)));
        };
#endif

        // a function to drop relations
        const auto& makeRamDrop = [&](const AstRelation* relation) {
            appendStmt(current, std::make_unique<RamDrop>(getRamRelation(relation, &typeEnv,
                                        getRelationName(relation->getName()), relation->getArity(), false,
                                        relation->isHashset())));
        };

        // create all internal relations of the current scc
        for (const auto& relation : allInterns) {
            makeRamCreate(relation, "");
            // create new and delta relations if required
            if (isRecursive) {
                makeRamCreate(relation, "delta_");
                makeRamCreate(relation, "new_");
            }
        }

#if 0
        // load all internal input relations from the facts dir with a .facts extension
        for (const auto& relation : internIns) {
            makeRamLoad(relation, "fact-dir", ".facts");
        }
#endif

#if 0
        // if a communication engine has been specified...
        if (Global::config().has("engine")) {
            // load all external output predecessor relations from the output dir with a .csv extension
            for (const auto& relation : externOutPreds) {
                makeRamLoad(relation, "output-dir", ".csv");
            }
            // load all external output predecessor relations from the output dir with a .facts extension
            for (const auto& relation : externNonOutPreds) {
                makeRamLoad(relation, "output-dir", ".facts");
            }
        }
#endif

        // compute the relations themselves
        std::unique_ptr<RamStatement> bodyStatement =
                (!isRecursive) ? translateNonRecursiveRelation(*((const AstRelation*)*allInterns.begin()),
                                         translationUnit.getProgram(), recursiveClauses, typeEnv)
                               : translateRecursiveRelation(
                                         allInterns, translationUnit.getProgram(), recursiveClauses, typeEnv);
        appendStmt(current, std::move(bodyStatement));

        // print the size of all printsize relations in the current SCC
        for (const auto& relation : allInterns) {
            if (relation->isPrintSize()) {
                makeRamPrintSize(relation);
            }
        }

#if 0
        // if a communication engine is enabled...
        if (Global::config().has("engine")) {
            // store all internal non-output relations with external successors to the output dir with a
            // .facts extension
            for (const auto& relation : internNonOutsWithExternSuccs) {
                makeRamStore(relation, "output-dir", ".facts");
            }
        }

        // store all internal output relations to the output dir with a .csv extension
        for (const auto& relation : internOuts) {
            makeRamStore(relation, "output-dir", ".csv");
        }
#endif

        // if provenance is not enabled...
        if (!Global::config().has("provenance")) {
            // if a communication engine is enabled...
            if (Global::config().has("engine")) {
                // drop all internal relations
                for (const auto& relation : allInterns) {
                    makeRamDrop(relation);
                }
                // drop external output predecessor relations
                for (const auto& relation : externOutPreds) {
                    makeRamDrop(relation);
                }
                // drop external non-output predecessor relations
                for (const auto& relation : externNonOutPreds) {
                    makeRamDrop(relation);
                }
            } else {
                // otherwise, drop all  relations expired as per the topological order
                for (const auto& relation : internExps) {
                    makeRamDrop(relation);
                }
            }
        }

        if (current) {
            // append the current SCC as a stratum to the sequence
            appendStmt(res, std::make_unique<RamStratum>(std::move(current), index));
            // increment the index of the current SCC
            index++;
        }
    }

    // add main timer if profiling
    if (res && Global::config().has("profile")) {
        res = std::make_unique<RamLogTimer>(std::move(res), LogStatement::runtime());
    }

    // done for main prog
    std::unique_ptr<RamProgram> prog(new RamProgram(std::move(res)));

#if 0
    // add subroutines for each clause
    if (Global::config().has("provenance")) {
        visitDepthFirst(translationUnit.getProgram()->getRelations(), [&](const AstClause& clause) {
            std::stringstream relName;
            relName << clause.getHead()->getName();

            if (relName.str().find("@info") != std::string::npos || clause.getBodyLiterals().empty()) {
                return;
            }

            std::string subroutineLabel =
                    relName.str() + "_" + std::to_string(clause.getClauseNum()) + "_subproof";
            prog->addSubroutine(
                    subroutineLabel, makeSubproofSubroutine(clause, translationUnit.getProgram(), typeEnv));
        });
    }
#endif

    return prog;
}

std::unique_ptr<RamTranslationUnit> AstTranslator::translateUnit(AstTranslationUnit& tu) {

    auto translateStart = std::chrono::high_resolution_clock::now();

    std::unique_ptr<RamProgram> ramProg = translateProgram(tu);

    SymbolTable& symTab = tu.getSymbolTable();
    ErrorReport& errReport = tu.getErrorReport();
    DebugReport& debugReport = tu.getDebugReport();

    if (!Global::config().get("debug-report").empty()) {
        if (ramProg) {
            auto translateEnd = std::chrono::high_resolution_clock::now();
            std::string runtimeStr =
                    "(" + std::to_string(std::chrono::duration<double>(translateEnd - translateStart).count()) + "s)";
            std::stringstream ramProgStr;
            ramProgStr << *ramProg;
            debugReport.addSection(DebugReporter::getCodeSection(
                    "ram-program", "RAM Program " + runtimeStr, ramProgStr.str()));
        }

        if (!debugReport.empty()) {
            std::ofstream debugReportStream(Global::config().get("debug-report"));
            debugReportStream << debugReport;
        }
    }
    return std::make_unique<RamTranslationUnit>(std::move(ramProg), symTab, errReport, debugReport);
   return nullptr;
}

}
