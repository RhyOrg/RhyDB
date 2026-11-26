#include "rhydb/query_engine/saneql/function_definitions.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "rhydb/common/panic.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/saneql/parser.h"

namespace rhydb::query_engine::saneql {

namespace {

/// Whether `expression` refers to no column or table, i.e. contains no identifier.
// NOLINTNEXTLINE(misc-no-recursion)
bool isConstant(const ast::Expression& expression) {
   return std::visit(
      // NOLINTNEXTLINE(misc-no-recursion)
      [](const auto& node) -> bool {
         using Node = std::decay_t<decltype(node)>;
         if constexpr (std::is_same_v<Node, ast::Identifier>) {
            return false;
         } else if constexpr (std::is_same_v<Node, ast::BinaryExpr>) {
            return isConstant(*node.left) && isConstant(*node.right);
         } else if constexpr (std::is_same_v<Node, ast::LogicalExpr>) {
            return std::ranges::all_of(node.operands, [](const auto& operand) {
               return isConstant(*operand);
            });
         } else if constexpr (std::is_same_v<Node, ast::SetLiteral>) {
            return std::ranges::all_of(node.elements, [](const auto& element) {
               return isConstant(*element);
            });
         } else if constexpr (
            std::is_same_v<Node, ast::UnaryNotExpr> || std::is_same_v<Node, ast::TypeCast>
         ) {
            return isConstant(*node.operand);
         } else if constexpr (std::is_same_v<Node, ast::FunctionCall>) {
            return std::ranges::all_of(
                      node.positional_arguments,
                      [](const auto& argument) { return isConstant(*argument.value); }
                   ) &&
                   std::ranges::all_of(node.named_arguments, [](const auto& argument) {
                      return isConstant(*argument.value);
                   });
         } else if constexpr (std::is_same_v<Node, ast::RecordLiteral>) {
            return std::ranges::all_of(node.fields, [](const auto& field) {
               return isConstant(*field.value);
            });
         } else {
            return true;
         }
      },
      expression.value
   );
}

/// Whether `expression` can be a table: a table name or a call of a (pipeline) function.
bool isTableExpression(const ast::Expression& expression) {
   return std::holds_alternative<ast::Identifier>(expression.value) ||
          std::holds_alternative<ast::FunctionCall>(expression.value);
}

/// Throws if `argument` cannot be bound to `parameter` of `definition`.
void checkArgument(
   const std::string& definition,
   const Definition::Parameter& parameter,
   const ast::Expression& argument
) {
   switch (parameter.type) {
      case ParameterType::SCALAR:
         CHECK_RHYDB_QUERY(
            isConstant(argument),
            "{}(): the argument '{}' must be a constant, as it is a scalar, but {} at {} refers to "
            "a column or table; declare the parameter as '{} expression' to pass an expression",
            definition,
            parameter.name,
            argument.toString(),
            argument.location.toString(),
            parameter.name
         );
         return;
      case ParameterType::EXPRESSION:
         return;
      case ParameterType::TABLE:
         CHECK_RHYDB_QUERY(
            isTableExpression(argument),
            "{}(): the argument '{}' must be a table, but is {} at {}",
            definition,
            parameter.name,
            argument.toString(),
            argument.location.toString()
         );
         return;
      case ParameterType::SYMBOL:
         CHECK_RHYDB_QUERY(
            std::holds_alternative<ast::Identifier>(argument.value),
            "{}(): the argument '{}' must be a name, but is {} at {}",
            definition,
            parameter.name,
            argument.toString(),
            argument.location.toString()
         );
         return;
   }
   RHYDB_UNREACHABLE();
}

FunctionSignature signatureOf(const Definition& definition) {
   FunctionSignature signature;
   for (const auto& parameter : definition.parameters) {
      signature.parameters.push_back({
         .name = parameter.name,
         .required = parameter.default_value == nullptr,
         .positional = true,
      });
   }
   return signature;
}

/// The argument bound to a parameter of the definition whose body is being expanded. The argument
/// is final: it was expanded where the call is, and is inserted into the body as it is.
struct Binding {
   ParameterType type;
   const ast::Expression* value;
};

/// Where an expression is expanded.
struct Scope {
   /// The visible definitions: the first `count` definitions of `definitions`, and those of its
   /// enclosing scopes.
   const FunctionDefinitions* definitions;
   size_t count;
   /// The parameters of the definition whose body is expanded, or null outside of any body.
   const std::map<std::string, Binding>* bindings = nullptr;
   /// Within a body, the location of the outermost call, which the nodes of the body take, so
   /// that errors point at the call site.
   std::optional<SourceLocation> location = std::nullopt;

   [[nodiscard]] std::optional<FunctionDefinitions::Lookup> find(const std::string& name) const {
      return definitions->find(name, count);
   }

   [[nodiscard]] const Binding* binding(const std::string& name) const {
      if (bindings == nullptr) {
         return nullptr;
      }
      const auto found = bindings->find(name);
      return found == bindings->end() ? nullptr : &found->second;
   }
};

class Expander {
  public:
   explicit Expander(const ExpansionLimits& limits)
       : limits_(limits) {}

   /// Expands the calls `name(...)` of the definitions visible in `scope`, and the references
   /// `name` of those without parameters, and substitutes the bindings of `scope`.
   // NOLINTNEXTLINE(misc-no-recursion)
   ast::ExpressionPtr expand(const ast::Expression& expression, const Scope& scope) {
      const SourceLocation location = scope.location.value_or(expression.location);
      if (const auto* identifier = std::get_if<ast::Identifier>(&expression.value)) {
         if (const auto* binding = scope.binding(identifier->name)) {
            return copy(*binding->value);
         }
         if (
            const auto lookup = scope.find(identifier->name);
            lookup.has_value() && lookup->definition->parameters.empty()
         ) {
            return expandCall(identifier->name, {}, {}, location, *lookup, scope);
         }
      }
      if (const auto* call = std::get_if<ast::FunctionCall>(&expression.value)) {
         if (const auto lookup = scope.find(call->function_name)) {
            return expandCall(
               call->function_name,
               call->positional_arguments,
               call->named_arguments,
               location,
               *lookup,
               scope
            );
         }
      }
      return rebuild(
         expression,
         // NOLINTNEXTLINE(misc-no-recursion)
         [&](const ast::Expression& child) { return expand(child, scope); },
         [&](const std::string& field_name) {
            const auto* binding = scope.binding(field_name);
            if (binding != nullptr && binding->type == ParameterType::SYMBOL) {
               return std::get<ast::Identifier>(binding->value->value).name;
            }
            return field_name;
         },
         location
      );
   }

  private:
   ExpansionLimits limits_;
   size_t nested_calls_ = 0;
   size_t created_nodes_ = 0;

   /// Counts the nested calls while it is alive.
   class NestedCall {
      Expander& expander;

     public:
      NestedCall(Expander& expander, const std::string& name, const SourceLocation& location)
          : expander(expander) {
         CHECK_RHYDB_QUERY(
            expander.nested_calls_ < expander.limits_.max_nested_calls,
            "the expansion of {}() at {} nests more than {} calls of definitions",
            name,
            location.toString(),
            expander.limits_.max_nested_calls
         );
         ++expander.nested_calls_;
      }
      NestedCall(const NestedCall&) = delete;
      NestedCall& operator=(const NestedCall&) = delete;
      NestedCall(NestedCall&&) = delete;
      NestedCall& operator=(NestedCall&&) = delete;
      ~NestedCall() { --expander.nested_calls_; }
   };

   ast::ExpressionPtr make(ast::ExpressionVariant value, const SourceLocation& location) {
      auto expression = ast::makeExpr(std::move(value), location);
      if (nested_calls_ > 0) {
         ++created_nodes_;
         CHECK_RHYDB_QUERY(
            created_nodes_ <= limits_.max_created_nodes,
            "the expansion of the definitions in the query creates more than {} nodes",
            limits_.max_created_nodes
         );
      }
      CHECK_RHYDB_QUERY(
         expression->depth <= limits_.max_depth,
         "the expansion of the definitions in the query at {} is nested too deeply, the maximum "
         "nesting depth is {}",
         location.toString(),
         limits_.max_depth
      );
      return expression;
   }

   /// A copy of `expression` with its children mapped through `child`, its record field names
   /// through `field_name` and the given location.
   template <typename Child, typename FieldName>
   // NOLINTNEXTLINE(misc-no-recursion)
   ast::ExpressionPtr rebuild(
      const ast::Expression& expression,
      const Child& child,
      const FieldName& field_name,
      const SourceLocation& location
   ) {
      auto value = std::visit(
         // NOLINTNEXTLINE(misc-no-recursion)
         [&](const auto& node) -> ast::ExpressionVariant {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, ast::BinaryExpr>) {
               return ast::BinaryExpr{
                  .op = node.op,
                  .left = child(*node.left),
                  .right = child(*node.right),
               };
            } else if constexpr (std::is_same_v<Node, ast::LogicalExpr>) {
               ast::LogicalExpr result{.op = node.op, .operands = {}};
               result.operands.reserve(node.operands.size());
               for (const auto& operand : node.operands) {
                  result.operands.push_back(child(*operand));
               }
               return result;
            } else if constexpr (std::is_same_v<Node, ast::UnaryNotExpr>) {
               return ast::UnaryNotExpr{.operand = child(*node.operand)};
            } else if constexpr (std::is_same_v<Node, ast::FunctionCall>) {
               ast::FunctionCall result{
                  .function_name = node.function_name,
                  .positional_arguments = {},
                  .named_arguments = {},
               };
               for (const auto& argument : node.positional_arguments) {
                  result.positional_arguments.push_back(
                     {.value = child(*argument.value), .location = argument.location}
                  );
               }
               for (const auto& argument : node.named_arguments) {
                  result.named_arguments.push_back({
                     .name = argument.name,
                     .value = child(*argument.value),
                     .location = argument.location,
                  });
               }
               return result;
            } else if constexpr (std::is_same_v<Node, ast::TypeCast>) {
               return ast::TypeCast{
                  .operand = child(*node.operand),
                  .target_type = node.target_type,
               };
            } else if constexpr (std::is_same_v<Node, ast::SetLiteral>) {
               ast::SetLiteral result;
               result.elements.reserve(node.elements.size());
               for (const auto& element : node.elements) {
                  result.elements.push_back(child(*element));
               }
               return result;
            } else if constexpr (std::is_same_v<Node, ast::RecordLiteral>) {
               ast::RecordLiteral result;
               result.fields.reserve(node.fields.size());
               for (const auto& field : node.fields) {
                  result.fields.push_back(
                     {.name = field_name(field.name), .value = child(*field.value)}
                  );
               }
               return result;
            } else {
               return node;
            }
         },
         expression.value
      );
      return make(std::move(value), location);
   }

   // NOLINTNEXTLINE(misc-no-recursion)
   ast::ExpressionPtr copy(const ast::Expression& expression) {
      return rebuild(
         expression,
         // NOLINTNEXTLINE(misc-no-recursion)
         [&](const ast::Expression& child) { return copy(child); },
         [](const std::string& field_name) { return field_name; },
         expression.location
      );
   }

   /// The value of the unbound `parameter` of `lookup.definition`: its default value, expanded
   /// with the definitions that the definition sees.
   // NOLINTNEXTLINE(misc-no-recursion)
   ast::ExpressionPtr defaultValue(
      const Definition::Parameter& parameter,
      const FunctionDefinitions::Lookup& lookup,
      const SourceLocation& call_location
   ) {
      return expand(
         *parameter.default_value,
         Scope{
            .definitions = lookup.scope,
            .count = lookup.index,
            .bindings = nullptr,
            .location = call_location,
         }
      );
   }

   /// Expands a call to `lookup.definition` with the given arguments, made in `scope`. The
   /// arguments are expanded in `scope` and then bound to the parameters, against which the body
   /// is expanded with the definitions before `lookup.definition`. The nodes of the body take the
   /// location of the call.
   // NOLINTNEXTLINE(misc-no-recursion)
   ast::ExpressionPtr expandCall(
      const std::string& function_name,
      const std::vector<ast::PositionalArgument>& call_positional,
      const std::vector<ast::NamedArgument>& call_named,
      const SourceLocation& call_location,
      const FunctionDefinitions::Lookup& lookup,
      const Scope& scope
   ) {
      const NestedCall nested_call{*this, function_name, call_location};
      const auto& definition = *lookup.definition;

      std::vector<ast::PositionalArgument> positional;
      positional.reserve(call_positional.size());
      for (const auto& argument : call_positional) {
         positional.push_back({
            .value = expand(*argument.value, scope),
            .location = argument.location,
         });
      }
      std::vector<ast::NamedArgument> named;
      named.reserve(call_named.size());
      for (const auto& argument : call_named) {
         named.push_back({
            .name = argument.name,
            .value = expand(*argument.value, scope),
            .location = argument.location,
         });
      }
      const auto bound = bindArguments(function_name, signatureOf(definition), positional, named);

      const Scope definition_scope{
         .definitions = lookup.scope,
         .count = lookup.index,
         .bindings = nullptr,
         .location = call_location,
      };

      std::vector<ast::ExpressionPtr> default_values;
      std::map<std::string, Binding> bindings;
      for (const auto& parameter : definition.parameters) {
         const auto* argument = bound.get(parameter.name);
         if (argument != nullptr) {
            checkArgument(function_name, parameter, *argument);
         } else {
            default_values.push_back(defaultValue(parameter, lookup, call_location));
            argument = default_values.back().get();
         }
         bindings.emplace(parameter.name, Binding{.type = parameter.type, .value = argument});
      }
      auto body_scope = definition_scope;
      body_scope.bindings = &bindings;
      return expand(definition.body(), body_scope);
   }
};

}  // namespace

FunctionDefinitions::FunctionDefinitions(const FunctionDefinitions* parent)
    : parent_(parent) {}

void FunctionDefinitions::checkNewName(
   const std::string& name,
   const SourceLocation& location
) const {
   CHECK_RHYDB_QUERY(
      std::ranges::none_of(definitions_, [&](const auto& other) { return other.name == name; }),
      "duplicate let '{}' at {}",
      name,
      location.toString()
   );
}

void FunctionDefinitions::add(ast::FunctionDefinition definition) {
   checkNewName(definition.name, definition.location);
   auto source = std::make_shared<const ast::FunctionDefinition>(std::move(definition));
   Definition result{
      .name = source->name,
      .parameters = {},
      .location = source->location,
      .source = source,
   };
   std::set<std::string> parameter_names;
   for (const auto& parameter : source->parameters) {
      CHECK_RHYDB_QUERY(
         parameter_names.insert(parameter.name).second,
         "duplicate parameter '{}' of let '{}' at {}",
         parameter.name,
         source->name,
         parameter.location.toString()
      );
      const Definition::Parameter result_parameter{
         .name = parameter.name,
         .type = parameter.type,
         .default_value = parameter.default_value.get(),
      };
      if (parameter.default_value != nullptr) {
         checkArgument(source->name, result_parameter, *parameter.default_value);
      }
      result.parameters.push_back(result_parameter);
   }
   definitions_.push_back(std::move(result));
}

void FunctionDefinitions::addFromSource(std::string_view source) {
   for (auto& definition : Parser(source).parseDefinitions()) {
      add(std::move(definition));
   }
}

std::optional<FunctionDefinitions::Lookup> FunctionDefinitions::find(
   const std::string& name
) const {
   return find(name, definitions_.size());
}

std::optional<FunctionDefinitions::Lookup> FunctionDefinitions::find(
   const std::string& name,
   size_t count
) const {
   for (const auto* scope = this; scope != nullptr; scope = scope->parent_) {
      const auto& definitions = scope->definitions_;
      // Only this scope is limited to its first `count` definitions.
      for (size_t index = scope == this ? std::min(count, definitions.size()) : definitions.size();
           index > 0;
           --index) {
         if (definitions[index - 1].name == name) {
            return Lookup{
               .definition = &definitions[index - 1],
               .scope = scope,
               .index = index - 1,
            };
         }
      }
   }
   return std::nullopt;
}

ast::ExpressionPtr expandFunctionDefinitions(
   const ast::Expression& expression,
   const FunctionDefinitions& definitions,
   const ExpansionLimits& limits
) {
   Expander expander{limits};
   return expander.expand(expression, Scope{.definitions = &definitions, .count = SIZE_MAX});
}

ast::ExpressionPtr parseQuery(std::string_view query) {
   auto program = Parser(query).parseProgram();
   FunctionDefinitions definitions;
   for (auto& definition : program.definitions) {
      definitions.add(std::move(definition));
   }
   return expandFunctionDefinitions(*program.body, definitions);
}

}  // namespace rhydb::query_engine::saneql
