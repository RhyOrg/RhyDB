#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/query_engine/saneql/function_registry.h"

namespace rhydb::query_engine::saneql {

using ast::ParameterType;

/// A definition: a function from expressions to an expression, which every call is replaced with
/// before the query is converted. This is a pure rewrite of the AST, so definitions add functions
/// to the language without adding anything to the query engine.
///
/// A definition is written in SaneQL with `let`:
///
///    let name(parameter type [:= default], ...) := body,
///    let name := body,
struct Definition {
   struct Parameter {
      std::string name;
      ParameterType type;
      /// The default value of an optional parameter, null if every call must bind it.
      const ast::Expression* default_value = nullptr;
   };

   std::string name;
   std::vector<Parameter> parameters;
   SourceLocation location;
   /// The `let` definition this was made from, which owns the body and the default values.
   std::shared_ptr<const ast::FunctionDefinition> source;

   [[nodiscard]] const ast::Expression& body() const { return *source->body; }
};

/// A scope of definitions. As in SaneQL, a definition only sees the definitions before it (so none
/// can be recursive), and it shadows builtin functions and the definitions of enclosing scopes of
/// the same name.
class FunctionDefinitions {
  public:
   explicit FunctionDefinitions(const FunctionDefinitions* parent = nullptr);

   /// Appends a `let` definition. Throws if this scope already defines its name or if a parameter
   /// name repeats. A default value is expanded and checked against its parameter at every call
   /// that omits the parameter, as it may refer to the parameters before it.
   void add(ast::FunctionDefinition definition);

   /// Parses `source`, a sequence of `let` definitions, and appends them.
   void addFromSource(std::string_view source);

   struct Lookup {
      const Definition* definition;
      /// The scope that holds `definition` and its position therein. The body of `definition` sees
      /// the definitions of `scope` before `index`, and those of the enclosing scopes.
      const FunctionDefinitions* scope;
      size_t index;
   };

   /// Looks up `name` among all definitions of this scope and then in the enclosing ones.
   [[nodiscard]] std::optional<Lookup> find(const std::string& name) const;

   /// Looks up `name` among the first `count` definitions of this scope (the last one wins) and
   /// then in the enclosing ones.
   [[nodiscard]] std::optional<Lookup> find(const std::string& name, size_t count) const;

  private:
   void checkNewName(const std::string& name, const SourceLocation& location) const;

   const FunctionDefinitions* parent_;
   std::vector<Definition> definitions_;
};

/// Bounds on the result of an expansion, which can be much larger than the query: a definition may
/// use a parameter several times, and each call is replaced with a body.
struct ExpansionLimits {
   /// The maximum number of calls of definitions nested in each other (through their bodies).
   size_t max_nested_calls = 100;
   /// The maximum depth (ast::Expression::depth) of the expanded expression.
   uint32_t max_depth = 200;
   /// The maximum number of nodes the expansion of calls may create.
   size_t max_created_nodes = 1'000'000;
};

/// Returns a copy of `expression` in which every call to a function of `definitions` is expanded,
/// transitively. Throws on a call that binds its arguments wrongly, or if the expansion exceeds
/// `limits`.
[[nodiscard]] ast::ExpressionPtr expandFunctionDefinitions(
   const ast::Expression& expression,
   const FunctionDefinitions& definitions,
   const ExpansionLimits& limits = {}
);

/// Parses a query, which may start with its own `let` definitions, and expands all calls to these
/// and to the builtin definitions.
[[nodiscard]] ast::ExpressionPtr parseQuery(std::string_view query);

}  // namespace rhydb::query_engine::saneql
