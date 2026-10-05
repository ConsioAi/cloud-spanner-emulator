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

#include "backend/query/queryable_column.h"

#include <memory>
#include <optional>
#include <string>

#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "backend/schema/catalog/column.h"
#include "common/feature_flags.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {

void QueryableColumn::Analyze() const {
  bool enable_generated_pk =
      EmulatorFeatureFlags::instance().flags().enable_generated_pk;
  bool is_generated_column =
      enable_generated_pk && wrapped_column_->is_generated();
  if (!wrapped_column_->has_default_value() && !is_generated_column) {
    return;
  }

  googlesql::AnalyzerOptions options = *options_;
  if (is_generated_column) {
    for (const Column* dep : wrapped_column_->dependent_columns()) {
      absl::Status status =
          options.AddExpressionColumn(dep->Name(), dep->GetType());
      ABSL_CHECK_OK(status) << "Failed to add dependent column " << dep->Name()
                            << " for generated column : "
                            << wrapped_column_->FullName();  // Crash OK
    }
  }
  std::string expression_type = is_generated_column ? "generated" : "default";

  std::unique_ptr<const googlesql::AnalyzerOutput> output;
  absl::Status status = googlesql::AnalyzeExpressionForAssignmentToType(
      wrapped_column_->expression().value(), options, catalog_, type_factory_,
      wrapped_column_->GetType(), &output);
  ABSL_CHECK_OK(status) << "Failed to analyze " << expression_type
                        << " expression for column "
                        << wrapped_column_->FullName();  // Crash OK

  googlesql::Column::ExpressionAttributes::ExpressionKind expression_kind =
      is_generated_column
          ? googlesql::Column::ExpressionAttributes::ExpressionKind::GENERATED
          : googlesql::Column::ExpressionAttributes::ExpressionKind::DEFAULT;
  output_ = std::move(output);
  column_expression_ = googlesql::Column::ExpressionAttributes(
      expression_kind, wrapped_column_->expression().value(),
      output_->resolved_expr());
}

}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google
