#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rhydb/query_engine/saneql/ast.h"
#include "rhydb/query_engine/saneql/function_registry.h"

namespace rhydb::query_engine::saneql {

using ast::ParameterType;

/// Expands a call of a native definition: returns the expression that replaces the call, given
/// its arguments (bound and checked against the definition's parameters, and already expanded).
using NativeExpansion = std::function<
   ast::ExpressionPtr(const BoundArguments& arguments, const SourceLocation& call_location)>;

/// A definition: a function from expressions to an expression, which every call is replaced with
/// before the query is converted. This is a pure rewrite of the AST, so definitions add functions
/// to the language without adding anything to the query engine.
///
/// A definition is either written in SaneQL with `let`:
///
///    let name(parameter [type] [:= default], ...) := body,
///    let name := body,
///
/// or native, i.e. implemented in C++ for rewrites the language cannot express (e.g. deriving the
/// name of a table from the name of a column). A native definition only sees the AST of its
/// arguments, never the database.
struct Definition {
   struct Parameter {
      std::string name;
      ParameterType type;
      /// The default value of an optional parameter of a `let` definition.
      const ast::Expression* default_value = nullptr;
      /// Whether a call must bind the parameter. Optional parameters of native definitions have
      /// no default value: their expansion sees them as absent.
      bool required = true;
   };

   std::string name;
   std::vector<Parameter> parameters;
   SourceLocation location;
   /// The `let` definition this was made from, which owns the body and the default values. Null
   /// for a native definition.
   std::shared_ptr<const ast::FunctionDefinition> source;
   /// The expansion of a native definition, empty for a `let` definition.
   NativeExpansion native;

   [[nodiscard]] const ast::Expression& body() const { return *source->body; }
};

/// A scope of definitions. As in SaneQL, a definition only sees the definitions before it (so none
/// can be recursive), and it shadows builtin functions and the definitions of enclosing scopes of
/// the same name. A query's own definitions form a scope enclosed by that of the builtin
/// definitions (builtinDefinitions() in prelude.h).
class FunctionDefinitions {
  public:
   explicit FunctionDefinitions(const FunctionDefinitions* parent = nullptr);

   /// Appends a `let` definition. Throws if this scope already defines its name, if a parameter
   /// name repeats, if a parameter has an unknown type, or if a default value does not fit its
   /// parameter.
   void add(ast::FunctionDefinition definition);

   /// Parses `source`, a sequence of `let` definitions, and appends them.
   void addFromSource(std::string_view source);

   /// Appends a native definition. Its parameters are bound like those of a builtin function
   /// (positionally or by name).
   void addNative(
      std::string name,
      std::vector<Definition::Parameter> parameters,
      NativeExpansion expansion
   );

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
