#define DUCKDB_EXTENSION_MAIN

#include "avro_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <sys/socket.h>

#include "utf8proc_wrapper.hpp"

#include "avro.h"

namespace duckdb {

struct AvroType {
  AvroType() : duckdb_type(LogicalType::INVALID) {}

  AvroType(avro_type_t avro_type_p, LogicalType duckdb_type_p,
           child_list_t<AvroType> children_p = {},
           unordered_map<idx_t, optional_idx> union_child_map_p = {})
      : duckdb_type(duckdb_type_p), avro_type(avro_type_p),
        children(children_p), union_child_map(union_child_map_p) {}
  LogicalType duckdb_type;
  avro_type_t avro_type;
  child_list_t<AvroType> children;
  unordered_map<idx_t, optional_idx> union_child_map;

  bool operator==(const AvroType &other) const {
    return duckdb_type == other.duckdb_type && avro_type == other.avro_type &&
           children == other.children &&
           union_child_map == other.union_child_map;
  }
};

struct AvroBindData : FunctionData {

  string filename;
  AvroType avro_type;
  LogicalType duckdb_type;

  bool Equals(const FunctionData &other_p) const override {
    const AvroBindData &other = static_cast<const AvroBindData &>(other_p);
    return filename == other.filename && avro_type == other.avro_type &&
           duckdb_type == other.duckdb_type;
  }

  unique_ptr<FunctionData> Copy() const override {
    auto bind_data = make_uniq<AvroBindData>();
    bind_data->filename = filename;
    bind_data->avro_type = avro_type;
    bind_data->duckdb_type = duckdb_type;

    return bind_data;
  }
};

// we use special transformation rules for unions with null:
// 1) the null does not become a union entry and
// 2) if there is only one entry the union disappears and is repaced by its
// child
static LogicalType TransformAvroType(const AvroType &avro_type) {
  child_list_t<LogicalType> children;

  switch (avro_type.duckdb_type.id()) {
  case LogicalTypeId::STRUCT: {
    for (auto &child : avro_type.children) {
      children.push_back(std::pair<std::string, LogicalType>(
          child.first, TransformAvroType(child.second)));
    }
    return LogicalType::STRUCT(std::move(children));
  }
  case LogicalTypeId::LIST:
    return LogicalType::LIST(TransformAvroType(avro_type.children[0].second));
  case LogicalTypeId::MAP: {
    child_list_t<LogicalType> children;
    children.push_back(
        std::pair<std::string, LogicalType>("key", LogicalType::VARCHAR));
    children.push_back(std::pair<std::string, LogicalType>(
        "value", TransformAvroType(avro_type.children[0].second)));
    return LogicalType::MAP(LogicalType::STRUCT(std::move(children)));
  }
  case LogicalTypeId::UNION: {
    for (auto &child : avro_type.children) {
      if (child.second.duckdb_type == LogicalTypeId::SQLNULL) {
        continue;
      }
      children.push_back(std::pair<std::string, LogicalType>(
          child.first, TransformAvroType(child.second)));
    }
    if (children.size() == 1) {
      return children[0].second;
    }
    return LogicalType::UNION(std::move(children));
  }
  default:
    return LogicalType(avro_type.duckdb_type);
  }
}

static AvroType TransformSchema(avro_schema_t &avro_schema) {
  switch (avro_typeof(avro_schema)) {
    case AVRO_NULL:
      return AvroType(AVRO_NULL, LogicalType::SQLNULL);
    case AVRO_BOOLEAN:
      return AvroType(AVRO_BOOLEAN, LogicalType::BOOLEAN);
    case AVRO_INT32:
      return AvroType(AVRO_INT32, LogicalType::INTEGER);
    case AVRO_INT64:
      return AvroType(AVRO_INT64, LogicalType::BIGINT);
    case AVRO_FLOAT:
      return AvroType(AVRO_FLOAT, LogicalType::FLOAT);
    case AVRO_DOUBLE:
      return AvroType(AVRO_DOUBLE, LogicalType::DOUBLE);
    case AVRO_BYTES:
      return AvroType(AVRO_BYTES, LogicalType::BLOB);
    case AVRO_STRING:
      return AvroType(AVRO_STRING, LogicalType::VARCHAR);
    case AVRO_UNION: {
      auto num_children = avro_schema_union_size(avro_schema);
      child_list_t<AvroType> union_children;
      idx_t non_null_child_idx = 0;
      unordered_map<idx_t, optional_idx> union_child_map;
      for (idx_t child_idx = 0; child_idx < num_children; child_idx++) {
        auto child_schema = avro_schema_union_branch(avro_schema, child_idx);
        auto child_type = TransformSchema(child_schema);
        union_children.push_back(std::pair<std::string, AvroType>(
            StringUtil::Format("u%llu", child_idx), std::move(child_type)));
        if (child_type.duckdb_type != LogicalTypeId::SQLNULL) {
          union_child_map[child_idx] = non_null_child_idx++;
        }
      }
      return AvroType(AVRO_UNION, LogicalTypeId::UNION, std::move(union_children),
                      union_child_map);
    }
    case AVRO_RECORD: {
      auto num_children = avro_schema_record_size(avro_schema);
      child_list_t<AvroType> struct_children;
      for (idx_t child_idx = 0; child_idx < num_children; child_idx++) {
        auto child_schema =
            avro_schema_record_field_get_by_index(avro_schema, child_idx);
        auto child_type = TransformSchema(child_schema);
        auto child_name = avro_schema_record_field_name(avro_schema, child_idx);
        if (!child_name || strlen(child_name) == 0) {
          throw InvalidInputException("Empty avro field name");
        }

        struct_children.push_back(
            std::pair<std::string, AvroType>(child_name, std::move(child_type)));
      }

      return AvroType(AVRO_RECORD, LogicalTypeId::STRUCT,
                      std::move(struct_children));
    }
    case AVRO_ENUM: {
      auto size = avro_schema_enum_number_of_symbols(avro_schema);
      Vector levels(LogicalType::VARCHAR, size);
      auto levels_data = FlatVector::GetData<string_t>(levels);
      for (idx_t enum_idx = 0; enum_idx < size; enum_idx++) {
        levels_data[enum_idx] = StringVector::AddString(
            levels, avro_schema_enum_get(avro_schema, enum_idx));
      }
      levels.Verify(size);
      return AvroType(AVRO_ENUM, LogicalType::ENUM(levels, size));
    }
    case AVRO_FIXED: {
      return AvroType(AVRO_FIXED, LogicalType::BLOB);
    }
    case AVRO_ARRAY: {
      auto child_schema = avro_schema_array_items(avro_schema);
      auto child_type = TransformSchema(child_schema);
      child_list_t<AvroType> list_children;
      list_children.push_back(
          std::pair<std::string, AvroType>("list_entry", std::move(child_type)));
      return AvroType(AVRO_ARRAY, LogicalTypeId::LIST, std::move(list_children));
    }
    case AVRO_MAP: {
      auto child_schema = avro_schema_map_values(avro_schema);
      auto child_type = TransformSchema(child_schema);
      child_list_t<AvroType> map_children;
      map_children.push_back(
          std::pair<std::string, AvroType>("list_entry", std::move(child_type)));
      return AvroType(AVRO_MAP, LogicalTypeId::MAP, std::move(map_children));
    }
    case AVRO_LINK:
      throw InvalidInputException("Recursive Avro types are not supported");

  default:
    throw NotImplementedException("Unknown Avro Type %s",
                                  avro_schema_type_name(avro_schema));
  }
}

static unique_ptr<FunctionData>
AvroBindFunction(ClientContext &context, TableFunctionBindInput &input,
                 vector<LogicalType> &return_types, vector<string> &names) {
  auto bind_data = make_uniq<AvroBindData>();
  bind_data->filename = input.inputs[0].ToString();
  avro_file_reader_t reader;
  if (!FileSystem::GetFileSystem(context).FileExists(bind_data->filename)) {
    throw InvalidInputException("Avro file %s not found", bind_data->filename);
  }
  if (avro_file_reader(bind_data->filename.c_str(), &reader)) {
    throw InvalidInputException(avro_strerror());
  }

  auto avro_schema = avro_file_reader_get_writer_schema(reader);
  bind_data->avro_type = TransformSchema(avro_schema);

  bind_data->duckdb_type = TransformAvroType(bind_data->avro_type);
  auto &duckdb_type = bind_data->duckdb_type;

  // special handling for root structs, we pull up the entries
  if (duckdb_type.id() == LogicalTypeId::STRUCT) {
    for (idx_t child_idx = 0;
         child_idx < StructType::GetChildCount(duckdb_type); child_idx++) {
      names.push_back(StructType::GetChildName(duckdb_type, child_idx));
      return_types.push_back(StructType::GetChildType(duckdb_type, child_idx));
    }
  } else {
    names.push_back(avro_schema_type_name(avro_schema));
    return_types.push_back(std::move(duckdb_type));
  }

  avro_schema_decref(avro_schema);
  avro_file_reader_close(reader);

  return bind_data;
}

static void TransformValue(avro_value *avro_val, AvroType &avro_type,
                           Vector &target, idx_t out_idx) {

  switch (avro_type.duckdb_type.id()) {
  case LogicalTypeId::SQLNULL: {
    FlatVector::Validity(target).SetInvalid(out_idx);
    break;
  }
  case LogicalTypeId::BOOLEAN: {
    int bool_val;
    if (avro_value_get_boolean(avro_val, &bool_val)) {
      throw InvalidInputException(avro_strerror());
    }
    FlatVector::GetData<uint8_t>(target)[out_idx] = bool_val != 0;
    break;
  }
  case LogicalTypeId::INTEGER: {
    if (avro_value_get_int(avro_val,
                           &FlatVector::GetData<int32_t>(target)[out_idx])) {
      throw InvalidInputException(avro_strerror());
    }
    break;
  }
  case LogicalTypeId::BIGINT: {
    if (avro_value_get_long(avro_val,
                            &FlatVector::GetData<int64_t>(target)[out_idx])) {
      throw InvalidInputException(avro_strerror());
    }
    break;
  }
  case LogicalTypeId::FLOAT: {
    if (avro_value_get_float(avro_val,
                             &FlatVector::GetData<float>(target)[out_idx])) {
      throw InvalidInputException(avro_strerror());
    }
    break;
  }
  case LogicalTypeId::DOUBLE: {
    if (avro_value_get_double(avro_val,
                              &FlatVector::GetData<double>(target)[out_idx])) {
      throw InvalidInputException(avro_strerror());
    }
    break;
  }
  case LogicalTypeId::BLOB:
    switch (avro_type.avro_type) {
    case AVRO_FIXED: {
      size_t fixed_size;
      const void *fixed_data;
      if (avro_value_get_fixed(avro_val, &fixed_data, &fixed_size)) {
        throw InvalidInputException(avro_strerror());
      }
      FlatVector::GetData<string_t>(target)[out_idx] =
          StringVector::AddStringOrBlob(target, const_char_ptr_cast(fixed_data),
                                        fixed_size);
      break;
    }
    case AVRO_BYTES: {
      avro_wrapped_buffer blob_buf = AVRO_WRAPPED_BUFFER_EMPTY;
      if (avro_value_grab_bytes(avro_val, &blob_buf)) {
        throw InvalidInputException(avro_strerror());
      }
      FlatVector::GetData<string_t>(target)[out_idx] =
          StringVector::AddStringOrBlob(
              target, const_char_ptr_cast(blob_buf.buf), blob_buf.size);
      blob_buf.free(&blob_buf);
      break;
    }
    default:
      throw NotImplementedException("Unknown Avro blob type %s");
    }
    break;

  case LogicalTypeId::VARCHAR: {
    avro_wrapped_buffer str_buf = AVRO_WRAPPED_BUFFER_EMPTY;
    if (avro_value_grab_string(avro_val, &str_buf)) {
      throw InvalidInputException(avro_strerror());
    }
    // avro strings are null-terminated
    D_ASSERT(const_char_ptr_cast(str_buf.buf)[str_buf.size - 1] == '\0');
    if (Utf8Proc::Analyze(const_char_ptr_cast(str_buf.buf), str_buf.size - 1) ==
        UnicodeType::INVALID) {
      throw InvalidInputException("Avro file contains invalid unicode string");
    }
    FlatVector::GetData<string_t>(target)[out_idx] = StringVector::AddString(
        target, const_char_ptr_cast(str_buf.buf), str_buf.size - 1);
    str_buf.free(&str_buf);
    break;
  }
  case LogicalTypeId::STRUCT: {
    size_t child_count;
    if (avro_value_get_size(avro_val, &child_count)) {
      throw InvalidInputException(avro_strerror());
    }
    D_ASSERT(child_count == StructType::GetChildCount(target.GetType()));
    D_ASSERT(child_count == avro_type.children.size());

    for (idx_t child_idx = 0; child_idx < child_count; child_idx++) {
      avro_value child_value;
      if (avro_value_get_by_index(avro_val, child_idx, &child_value, nullptr)) {
        throw InvalidInputException(avro_strerror());
      }
      TransformValue(&child_value, avro_type.children[child_idx].second,
                     *StructVector::GetEntries(target)[child_idx], out_idx);
    }
    break;
  }

  case LogicalTypeId::MAP: {
    size_t entry_count;
    if (avro_value_get_size(avro_val, &entry_count)) {
      throw InvalidInputException(avro_strerror());
    }

    D_ASSERT(avro_type.children.size() == 1);
    auto child_offset = ListVector::GetListSize(target);
    ListVector::SetListSize(target, child_offset + entry_count);

    auto &key_vector = MapVector::GetKeys(target);
    auto &value_vector = MapVector::GetValues(target);

    D_ASSERT(key_vector.GetType().id() == LogicalTypeId::VARCHAR);
    auto string_ptr = FlatVector::GetData<string_t>(key_vector);
    for (idx_t entry_idx = 0; entry_idx < entry_count; entry_idx++) {
      avro_value child_value;
      const char *map_key;
      if (avro_value_get_by_index(avro_val, entry_idx, &child_value,
                                  &map_key)) {
        throw InvalidInputException(avro_strerror());
      }
      D_ASSERT(map_key);
      string_ptr[child_offset + entry_idx] =
          StringVector::AddString(key_vector, map_key);
      TransformValue(&child_value, avro_type.children[0].second, value_vector,
                     child_offset + entry_idx);
    }
    auto list_vector = ListVector::GetData(target);

    list_vector[out_idx].offset = child_offset;
    list_vector[out_idx].length = entry_count;
    break;
  }

  case LogicalTypeId::UNION: {
    int discriminant;
    avro_value union_value;
    if (avro_value_get_discriminant(avro_val, &discriminant) ||
        avro_value_get_current_branch(avro_val, &union_value)) {
      throw InvalidInputException(avro_strerror());
    }
    if (discriminant >= avro_type.children.size()) {
      throw InvalidInputException("Invalid union tag");
    }

    // if the target is a union, we set everything to null to start with
    if (target.GetType().id() == LogicalTypeId::UNION) {
      for (idx_t union_idx = 0;
           union_idx < UnionType::GetMemberCount(target.GetType());
           union_idx++) {
        FlatVector::Validity(UnionVector::GetMember(target, union_idx))
            .SetInvalid(out_idx);
      }
    }
    FlatVector::Validity(target).SetInvalid(out_idx);

    if (avro_type.children[discriminant].second.duckdb_type !=
        LogicalTypeId::SQLNULL) {
      auto duckdb_child_index =
          avro_type.union_child_map[discriminant].GetIndex();
      if (target.GetType().id() == LogicalTypeId::UNION) {
        auto &tags = UnionVector::GetTags(target);
        FlatVector::GetData<union_tag_t>(tags)[out_idx] = duckdb_child_index;
        auto &union_vector = UnionVector::GetMember(target, duckdb_child_index);
        TransformValue(&union_value, avro_type.children[discriminant].second,
                       union_vector, out_idx);
      } else { // directly recurse, we have dissolved the union
        TransformValue(&union_value, avro_type.children[discriminant].second,
                       target, out_idx);
      }
      FlatVector::Validity(target).SetValid(out_idx);
    }

    break;
  }
  case LogicalTypeId::ENUM: {
    auto enum_type = EnumType::GetPhysicalType(target.GetType());
    int enum_val;

    if (avro_value_get_enum(avro_val, &enum_val)) {
      throw InvalidInputException(avro_strerror());
    }
    if (enum_val < 0 || enum_val >= EnumType::GetSize(target.GetType())) {
      throw InvalidInputException("Enum value out of range");
    }

    switch (enum_type) {
    case PhysicalType::UINT8:
      FlatVector::GetData<uint8_t>(target)[out_idx] = enum_val;
      break;
    case PhysicalType::UINT16:
      FlatVector::GetData<uint16_t>(target)[out_idx] = enum_val;
      break;
    case PhysicalType::UINT32:
      FlatVector::GetData<uint32_t>(target)[out_idx] = enum_val;
      break;
    default:
      throw InternalException("Unsupported Enum Internal Type");
    }
    break;
  }

  case LogicalTypeId::LIST: {
    size_t list_len;

    if (avro_value_get_size(avro_val, &list_len)) {
      throw InvalidInputException(avro_strerror());
    }
    auto &child_vector = ListVector::GetEntry(target);
    auto child_offset = ListVector::GetListSize(target);
    ListVector::SetListSize(target, child_offset + list_len);
    for (idx_t child_idx = 0; child_idx < list_len; child_idx++) {
      avro_value_t child_value;
      if (avro_value_get_by_index(avro_val, child_idx, &child_value, nullptr)) {
        throw InvalidInputException(avro_strerror());
      }
      TransformValue(&child_value, avro_type.children[0].second, child_vector,
                     child_offset + child_idx);
    }
    auto list_vector_data = ListVector::GetData(target);
    list_vector_data[out_idx].length = list_len;
    list_vector_data[out_idx].offset = child_offset;

    break;
  }

  default:
    throw NotImplementedException(avro_type.duckdb_type.ToString());
  }
}

struct AvroGlobalState : GlobalTableFunctionState {
  ~AvroGlobalState() {
    avro_value_decref(&value);
    avro_file_reader_close(reader);
  }
  AvroGlobalState(string filename, const LogicalType &duckdb_type) {
    if (avro_file_reader(filename.c_str(), &reader)) {
      throw InvalidInputException(avro_strerror());
    }
    auto schema = avro_file_reader_get_writer_schema(reader);
    auto interface = avro_generic_class_from_schema(schema);
    avro_schema_decref(schema);
    avro_generic_value_new(interface, &value);
    avro_value_iface_decref(interface);
    read_vec = make_uniq<Vector>(duckdb_type);
  }
  avro_file_reader_t reader;
  avro_schema_t schema;
  avro_value_t value;
  unique_ptr<Vector> read_vec;
};

static void AvroTableFunction(ClientContext &context, TableFunctionInput &data,
                              DataChunk &output) {
  auto bind_data = data.bind_data->Cast<AvroBindData>();

  auto &global_state = data.global_state->Cast<AvroGlobalState>();
  idx_t out_idx = 0;

  while (avro_file_reader_read_value(global_state.reader,
                                     &global_state.value) == 0) {
    TransformValue(&global_state.value, bind_data.avro_type,
                   *global_state.read_vec, out_idx++);
    if (out_idx == STANDARD_VECTOR_SIZE) {
      break;
    }
  }

  // pull up root struct into output chunk
  if (bind_data.duckdb_type.id() == LogicalTypeId::STRUCT) {
    for (idx_t col_idx = 0; col_idx < output.ColumnCount(); col_idx++) {
      output.data[col_idx].Reference(
          *StructVector::GetEntries(*global_state.read_vec)[col_idx]);
    }
  }

  output.SetCardinality(out_idx);
}

static unique_ptr<GlobalTableFunctionState>
AvroGlobalInit(ClientContext &context, TableFunctionInitInput &input) {
  auto &bind_data = input.bind_data->Cast<AvroBindData>();
  return make_uniq<AvroGlobalState>(bind_data.filename, bind_data.duckdb_type);
}

static void LoadInternal(DatabaseInstance &instance) {
  // Register a scalar function
  auto avro_read_function =
      TableFunction("read_avro", {LogicalType::VARCHAR}, AvroTableFunction,
                    AvroBindFunction, AvroGlobalInit);
  ExtensionUtil::RegisterFunction(instance, avro_read_function);
}

void AvroExtension::Load(DuckDB &db) { LoadInternal(*db.instance); }
std::string AvroExtension::Name() { return "avro"; }

std::string AvroExtension::Version() const {
#ifdef EXT_VERSION_AVRO
  return EXT_VERSION_AVRO;
#else
  return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_EXTENSION_API void avro_init(duckdb::DatabaseInstance &db) {
  duckdb::DuckDB db_wrapper(db);
  db_wrapper.LoadExtension<duckdb::AvroExtension>();
}

DUCKDB_EXTENSION_API const char *avro_version() {
  return duckdb::DuckDB::LibraryVersion();
}
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
