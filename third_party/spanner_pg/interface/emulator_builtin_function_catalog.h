//
// PostgreSQL is released under the PostgreSQL License, a liberal Open Source
// license, similar to the BSD or MIT licenses.
//
// PostgreSQL Database Management System
// (formerly known as Postgres, then as Postgres95)
//
// Portions Copyright © 1996-2020, The PostgreSQL Global Development Group
//
// Portions Copyright © 1994, The Regents of the University of California
//
// Portions Copyright 2023 Google LLC
//
// Permission to use, copy, modify, and distribute this software and its
// documentation for any purpose, without fee, and without a written agreement
// is hereby granted, provided that the above copyright notice and this
// paragraph and the following two paragraphs appear in all copies.
//
// IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
// DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
// LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
// EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.
//
// THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
// FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN
// "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO PROVIDE
// MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
//------------------------------------------------------------------------------

#ifndef INTERFACE_EMULATOR_BUILTIN_FUNCTION_CATALOG_
#define INTERFACE_EMULATOR_BUILTIN_FUNCTION_CATALOG_

#include <memory>
#include <string>

#include "googlesql/public/function.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "backend/query/function_catalog.h"
#include "third_party/spanner_pg/interface/engine_builtin_function_catalog.h"
#include "googlesql/base/status_macros.h"

namespace postgres_translator {
namespace spangres {

// A wrapper around the Emulator catalog so that PG Spanner can access the
// functions that are built into Cloud Spanner.
//
// An instance exists in one of two modes:
//
// - Owning a full FunctionCatalog. The process-wide SpangresSystemCatalog
//   singleton holds one such instance for the lifetime of the process and
//   resolves builtin functions through it.
// - Carrying only the latest schema. Once the singleton exists, every PG
//   analysis only needs to hand the singleton the schema of the database being
//   queried (SpangresSystemCatalog::TryInitializeEngineSystemCatalog copies it
//   and discards the wrapper). Building a FunctionCatalog for that, which
//   loads every GoogleSQL builtin function, costs several milliseconds per
//   statement and its functions are never used.
class EmulatorBuiltinFunctionCatalog : public EngineBuiltinFunctionCatalog {
 public:
  // Owning mode.
  explicit EmulatorBuiltinFunctionCatalog(
      std::unique_ptr<google::spanner::emulator::backend::FunctionCatalog>
          function_catalog)
      : EngineBuiltinFunctionCatalog(),
        function_catalog_(std::move(function_catalog)),
        latest_schema_(function_catalog_ != nullptr
                           ? function_catalog_->GetLatestSchema()
                           : nullptr) {}

  // Schema-carrier mode: owns no FunctionCatalog. Only GetLatestSchema() and
  // SetLatestSchema() may be used on such an instance.
  explicit EmulatorBuiltinFunctionCatalog(
      const google::spanner::emulator::backend::Schema* latest_schema)
      : EngineBuiltinFunctionCatalog(), latest_schema_(latest_schema) {}

  // Creates an owning instance whose FunctionCatalog is built on this
  // wrapper's own TypeFactory, so that the types referenced by the function
  // signatures live exactly as long as the wrapper. This is what the
  // SpangresSystemCatalog singleton must use: a FunctionCatalog built on a
  // caller's TypeFactory would dangle once that caller (a database, or a
  // stack-local factory) goes away.
  static std::unique_ptr<EmulatorBuiltinFunctionCatalog>
  CreateWithOwnedFunctionCatalog(
      const google::spanner::emulator::backend::Schema* latest_schema) {
    auto catalog = std::unique_ptr<EmulatorBuiltinFunctionCatalog>(
        new EmulatorBuiltinFunctionCatalog(latest_schema));
    catalog->function_catalog_ =
        std::make_unique<google::spanner::emulator::backend::FunctionCatalog>(
            catalog->type_factory(),
            google::spanner::emulator::backend::
                kCloudSpannerEmulatorFunctionCatalogName,
            latest_schema);
    return catalog;
  }

  ~EmulatorBuiltinFunctionCatalog() {
  }

  // Returns true in owning mode, false in schema-carrier mode.
  bool has_function_catalog() const { return function_catalog_ != nullptr; }

  absl::StatusOr<const googlesql::Function*> GetFunction(
      const std::string& name) const override {
    ABSL_DCHECK(function_catalog_ != nullptr)
        << "GetFunction called on a schema-carrier "
           "EmulatorBuiltinFunctionCatalog";
    if (function_catalog_ == nullptr) {
      return absl::NotFoundError(absl::StrCat(name, " function not found"));
    }
    const googlesql::Function* function;
    function_catalog_->GetFunction(name, &function);
    if (function == nullptr) {
      return absl::NotFoundError(absl::StrCat(name, " function not found"));
    }
    return function;
  }

  absl::StatusOr<const googlesql::Procedure*> GetProcedure(
      const std::string& name) const override {
    return absl::UnimplementedError("GetProcedure is not supported");
  }

  // TODO: b/313936285 - Add builtin TVF support to the Emulator.
  absl::StatusOr<const googlesql::TableValuedFunction*> GetTableValuedFunction(
      const std::string& name) const override {
    ABSL_DCHECK(function_catalog_ != nullptr)
        << "GetTableValuedFunction called on a schema-carrier "
           "EmulatorBuiltinFunctionCatalog";
    if (function_catalog_ == nullptr) {
      return absl::NotFoundError(
          absl::StrCat(name, " table valued function not found"));
    }
    const googlesql::TableValuedFunction* tvf;
    function_catalog_->GetTableValuedFunction(name, &tvf);
    if (tvf == nullptr) {
      return absl::NotFoundError(
          absl::StrCat(name, " table valued function not found"));
    }
    return tvf;
  }

  absl::Status GetFunctions(
      absl::flat_hash_set<const googlesql::Function*>* output) const override {
    GOOGLESQL_RET_CHECK_NE(output, nullptr);
    GOOGLESQL_RET_CHECK(output->empty());
    ABSL_DCHECK(function_catalog_ != nullptr)
        << "GetFunctions called on a schema-carrier "
           "EmulatorBuiltinFunctionCatalog";
    if (function_catalog_ != nullptr) {
      function_catalog_->GetFunctions(output);
    }
    return absl::OkStatus();
  }

  absl::Status GetProcedures(
      absl::flat_hash_set<const googlesql::Procedure*>* output) const override {
    return absl::OkStatus();
  }

  void SetLatestSchema(
      const google::spanner::emulator::backend::Schema* schema) {
    latest_schema_ = schema;
    if (function_catalog_ != nullptr) {
      function_catalog_->SetLatestSchema(schema);
    }
  }

  const google::spanner::emulator::backend::Schema* GetLatestSchema() const {
    return function_catalog_ != nullptr ? function_catalog_->GetLatestSchema()
                                        : latest_schema_;
  }

 private:
  // The EmulatorBuiltinFunctionCatalog object must own this pointer; otherwise,
  // the returned function pointer from GetFunction() will become invalid and
  // point to an incorrect function. Null in schema-carrier mode.
  std::unique_ptr<google::spanner::emulator::backend::FunctionCatalog>
      function_catalog_;

  // The schema carried in schema-carrier mode. In owning mode the
  // FunctionCatalog's own latest schema is authoritative.
  const google::spanner::emulator::backend::Schema* latest_schema_ = nullptr;
};

}  // namespace spangres
}  // namespace postgres_translator

#endif  // INTERFACE_EMULATOR_BUILTIN_FUNCTION_CATALOG_
