//
// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_QUERY_QUERYABLE_COLUMN_H_
#define THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_QUERY_QUERYABLE_COLUMN_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/base/call_once.h"
#include "absl/strings/str_cat.h"
#include "backend/schema/catalog/column.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {

// A wrapper over backend::Column class which implements googlesql::Column.
class QueryableColumn : public googlesql::Column {
 public:
  // A column without a default or generated expression.
  explicit QueryableColumn(const backend::Column* column)
      : wrapped_column_(column) {}

  // A column whose default or generated expression has already been analyzed.
  QueryableColumn(const backend::Column* column,
                  std::unique_ptr<const googlesql::AnalyzerOutput> output,
                  std::optional<const googlesql::Column::ExpressionAttributes>
                      expression_attributes)
      : wrapped_column_(column),
        output_(std::move(output)),
        column_expression_(expression_attributes) {}

  // A column whose default or generated expression is analyzed on the first
  // call to GetExpression(), against 'catalog' with 'options'. 'catalog' and
  // 'type_factory' must outlive this column; 'options' is shared by all the
  // columns of a table.
  //
  // Analyzing these expressions is the dominant cost of building a Catalog
  // for a schema with many default or generated columns, and the analyzer
  // only asks for them when resolving DML against the column's table. Deferring
  // the analysis makes every other statement skip it entirely.
  QueryableColumn(const backend::Column* column,
                  std::shared_ptr<const googlesql::AnalyzerOptions> options,
                  googlesql::Catalog* catalog,
                  googlesql::TypeFactory* type_factory)
      : wrapped_column_(column),
        options_(std::move(options)),
        catalog_(catalog),
        type_factory_(type_factory) {}

  std::string Name() const override { return wrapped_column_->Name(); }

  std::string FullName() const override { return wrapped_column_->FullName(); }

  const googlesql::Type* GetType() const override {
    return wrapped_column_->GetType();
  }

  bool IsWritableColumn() const override {
    return !wrapped_column_->is_generated();
  }

  bool IsPseudoColumn() const override { return wrapped_column_->hidden(); }

  std::optional<const googlesql::Column::ExpressionAttributes> GetExpression()
      const override {
    if (options_ != nullptr) {
      absl::call_once(analyzed_once_, [this] { Analyze(); });
    }
    return column_expression_;
  }

  const backend::Column* wrapped_column() const { return wrapped_column_; }

 private:
  // Analyzes the column's default or generated expression and fills output_
  // and column_expression_. Crashes on failure, as the eager construction
  // path always did: expressions are validated when the schema is created,
  // so a failure here is an emulator bug.
  void Analyze() const;

  const backend::Column* wrapped_column_;

  // Keeps the resolved expression referenced by column_expression_ alive.
  mutable std::unique_ptr<const googlesql::AnalyzerOutput> output_ = nullptr;
  mutable std::optional<const googlesql::Column::ExpressionAttributes>
      column_expression_;

  // Set only for lazily analyzed columns.
  mutable absl::once_flag analyzed_once_;
  std::shared_ptr<const googlesql::AnalyzerOptions> options_ = nullptr;
  googlesql::Catalog* catalog_ = nullptr;
  googlesql::TypeFactory* type_factory_ = nullptr;
};

}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google

#endif  // THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_QUERY_QUERYABLE_COLUMN_H_
