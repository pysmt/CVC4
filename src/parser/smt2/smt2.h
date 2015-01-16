/*********************                                                        */
/*! \file smt2.h
 ** \verbatim
 ** Original author: Christopher L. Conway
 ** Major contributors: Morgan Deters
 ** Minor contributors (to current version): Tianyi Liang, Kshitij Bansal
 ** This file is part of the CVC4 project.
 ** Copyright (c) 2009-2014  New York University and The University of Iowa
 ** See the file COPYING in the top-level source directory for licensing
 ** information.\endverbatim
 **
 ** \brief Definitions of SMT2 constants.
 **
 ** Definitions of SMT2 constants.
 **/

#include "cvc4parser_private.h"

#ifndef __CVC4__PARSER__SMT2_H
#define __CVC4__PARSER__SMT2_H

#include "parser/parser.h"
#include "parser/smt1/smt1.h"
#include "theory/logic_info.h"
#include "util/abstract_value.h"
#include "parser/smt2/smt2_input.h"

#include <string>
#include <sstream>
#include <utility>
#include <stack>

namespace CVC4 {

class SExpr;

namespace parser {

class Smt2 : public Parser {
  friend class ParserBuilder;

public:
  enum Theory {
    THEORY_ARRAYS,
    THEORY_BITVECTORS,
    THEORY_CORE,
    THEORY_DATATYPES,
    THEORY_INTS,
    THEORY_REALS,
    THEORY_REALS_INTS,
    THEORY_QUANTIFIERS,
    THEORY_SETS,
    THEORY_STRINGS,
    THEORY_UF,
    THEORY_FP
  };

private:
  bool d_logicSet;
  LogicInfo d_logic;
  std::hash_map<std::string, Kind, StringHashFunction> operatorKindMap;
  std::pair<Expr, std::string> d_lastNamedTerm;
  // this is a user-context stack
  std::stack< std::map<Expr, std::string> > d_unsatCoreNames;
  std::vector<Expr> d_sygusVars, d_sygusConstraints, d_sygusFunSymbols;
  std::vector< std::pair<std::string, Expr> > d_sygusFuns;
  size_t d_nextSygusFun;

protected:
  Smt2(ExprManager* exprManager, Input* input, bool strictMode = false, bool parseOnly = false);

public:
  /**
   * Add theory symbols to the parser state.
   *
   * @param theory the theory to open (e.g., Core, Ints)
   */
  void addTheory(Theory theory);

  void addOperator(Kind k, const std::string& name);

  Kind getOperatorKind(const std::string& name) const;

  bool isOperatorEnabled(const std::string& name) const;

  bool isTheoryEnabled(Theory theory) const;

  bool logicIsSet();

  void reset();

  void resetAssertions();

  /**
   * Sets the logic for the current benchmark. Declares any logic and
   * theory symbols.
   *
   * @param name the name of the logic (e.g., QF_UF, AUFLIA)
   */
  void setLogic(std::string name);

  /**
   * Get the logic.
   */
  const LogicInfo& getLogic() const { return d_logic; }

  bool v2_0() const {
    return getInput()->getLanguage() == language::input::LANG_SMTLIB_V2_0;
  }
  bool v2_5() const {
    return getInput()->getLanguage() == language::input::LANG_SMTLIB_V2_5;
  }
  bool sygus() const {
    return getInput()->getLanguage() == language::input::LANG_SYGUS;
  }

  void setLanguage(InputLanguage lang) {
    ((Smt2Input*) getInput())->setLanguage(lang);
  }

  void setInfo(const std::string& flag, const SExpr& sexpr);

  void setOption(const std::string& flag, const SExpr& sexpr);

  void checkThatLogicIsSet();

  void checkUserSymbol(const std::string& name) {
    if(name.length() > 0 && (name[0] == '.' || name[0] == '@')) {
      std::stringstream ss;
      ss << "cannot declare or define symbol `" << name << "'; symbols starting with . and @ are reserved in SMT-LIB";
      parseError(ss.str());
    }
  }

  void includeFile(const std::string& filename);

  void setLastNamedTerm(Expr e, std::string name) {
    d_lastNamedTerm = std::make_pair(e, name);
  }

  void clearLastNamedTerm() {
    d_lastNamedTerm = std::make_pair(Expr(), "");
  }

  std::pair<Expr, std::string> lastNamedTerm() {
    return d_lastNamedTerm;
  }

  void pushUnsatCoreNameScope() {
    d_unsatCoreNames.push(d_unsatCoreNames.top());
  }

  void popUnsatCoreNameScope() {
    d_unsatCoreNames.pop();
  }

  void registerUnsatCoreName(std::pair<Expr, std::string> name) {
    d_unsatCoreNames.top().insert(name);
  }

  std::map<Expr, std::string> getUnsatCoreNames() {
    return d_unsatCoreNames.top();
  }

  bool isAbstractValue(const std::string& name) {
    return name.length() >= 2 && name[0] == '@' && name[1] != '0' &&
      name.find_first_not_of("0123456789", 1) == std::string::npos;
  }

  Expr mkAbstractValue(const std::string& name) {
    assert(isAbstractValue(name));
    return getExprManager()->mkConst(AbstractValue(Integer(name.substr(1))));
  }

  Expr mkSygusVar(const std::string& name, const Type& type) {
    Expr e = mkBoundVar(name, type);
    d_sygusVars.push_back(e);
    return e;
  }

  void addSygusFun(const std::string& fun, Expr eval) {
    d_sygusFuns.push_back(std::make_pair(fun, eval));
  }

  void defineSygusFuns() {
    // only define each one once
    while(d_nextSygusFun < d_sygusFuns.size()) {
      std::pair<std::string, Expr> p = d_sygusFuns[d_nextSygusFun];
      std::string fun = p.first;
      Debug("parser-sygus") << "Sygus : define fun " << fun << std::endl;
      Expr eval = p.second;
      FunctionType evalType = eval.getType();
      std::vector<Type> argTypes = evalType.getArgTypes();
      Type rangeType = evalType.getRangeType();
      Debug("parser-sygus") << "...eval type : " << evalType << ", #args=" << argTypes.size() << std::endl;

      // first make the function type
      std::vector<Expr> sygusVars;
      std::vector<Type> funType;
      for(size_t j = 1; j < argTypes.size(); ++j) {
        funType.push_back(argTypes[j]);
        std::stringstream ss;
        ss << fun << "_v_" << j;
        sygusVars.push_back(getExprManager()->mkBoundVar(ss.str(), argTypes[j]));
      }
      Type funt = getExprManager()->mkFunctionType(funType, rangeType);
      Debug("parser-sygus") << "...eval function type : " << funt << std::endl;

      // copy the bound vars
      /*
      std::vector<Expr> sygusVars;
      //std::vector<Type> types;
      for(size_t i = 0; i < d_sygusVars.size(); ++i) {
        std::stringstream ss;
        ss << d_sygusVars[i];
        Type type = d_sygusVars[i].getType();
        sygusVars.push_back(getExprManager()->mkBoundVar(ss.str(), type));
        //types.push_back(type);
      }
      Debug("parser-sygus") << "...made vars, #vars=" << sygusVars.size() << std::endl;
      */

      //Type t = getExprManager()->mkFunctionType(types, rangeType);
      //Debug("parser-sygus") << "...function type : " << t << std::endl;
      
      Expr lambda = mkFunction(fun, funt, ExprManager::VAR_FLAG_DEFINED);
      Debug("parser-sygus") << "...made function : " << lambda << std::endl;
      std::vector<Expr> applyv;
      Expr funbv = getExprManager()->mkBoundVar(std::string("f") + fun, argTypes[0]);
      d_sygusFunSymbols.push_back(funbv);
      applyv.push_back(eval);
      applyv.push_back(funbv);
      for(size_t i = 0; i < sygusVars.size(); ++i) {
        applyv.push_back(sygusVars[i]);
      }
      Expr apply = getExprManager()->mkExpr(kind::APPLY_UF, applyv);
      Debug("parser-sygus") << "...made apply " << apply << std::endl;
      Command* cmd = new DefineFunctionCommand(fun, lambda, sygusVars, apply);
      preemptCommand(cmd);

      ++d_nextSygusFun;
    }
  }

  void addSygusConstraint(Expr constraint) {
    d_sygusConstraints.push_back(constraint);
  }

  Expr getSygusConstraints() {
    switch(d_sygusConstraints.size()) {
    case 0: return getExprManager()->mkConst(bool(true));
    case 1: return d_sygusConstraints[0];
    default: return getExprManager()->mkExpr(kind::AND, d_sygusConstraints);
    }
  }

  const std::vector<Expr>& getSygusVars() {
    return d_sygusVars;
  }

  const std::vector<Expr>& getSygusFunSymbols() {
    return d_sygusFunSymbols;
  }

  /**
   * Smt2 parser provides its own checkDeclaration, which does the
   * same as the base, but with some more helpful errors.
   */
  void checkDeclaration(const std::string& name, DeclarationCheck check,
                        SymbolType type = SYM_VARIABLE,
                        std::string notes = "") throw(ParserException) {
    // if the symbol is something like "-1", we'll give the user a helpful
    // syntax hint.  (-1 is a valid identifier in SMT-LIB, NOT unary minus.)
    if( check != CHECK_DECLARED ||
        name[0] != '-' ||
        name.find_first_not_of("0123456789", 1) != std::string::npos ) {
      this->Parser::checkDeclaration(name, check, type, notes);
      return;
    }

    std::stringstream ss;
    ss << notes
       << "You may have intended to apply unary minus: `(- "
       << name.substr(1)
       << ")'\n";
    this->Parser::checkDeclaration(name, check, type, ss.str());
  }

  void checkOperator(Kind kind, unsigned numArgs) throw(ParserException) {
    Parser::checkOperator(kind, numArgs);
    // strict SMT-LIB mode enables extra checks for some bitvector operators
    // that CVC4 permits as N-ary but the standard requires is binary
    if(strictModeEnabled()) {
      switch(kind) {
      case kind::BITVECTOR_CONCAT:
      case kind::BITVECTOR_AND:
      case kind::BITVECTOR_OR:
      case kind::BITVECTOR_XOR:
      case kind::BITVECTOR_MULT:
      case kind::BITVECTOR_PLUS:
        if(numArgs != 2) {
          parseError("Operator requires exact 2 arguments in strict SMT-LIB compliance mode: " + kindToString(kind));
        }
        break;
      default:
        break; /* no problem */
      }
    }
  }

private:

  void addArithmeticOperators();

  void addBitvectorOperators();

  void addStringOperators();

  void addFloatingPointOperators();

};/* class Smt2 */

}/* CVC4::parser namespace */
}/* CVC4 namespace */

#endif /* __CVC4__PARSER__SMT2_H */
