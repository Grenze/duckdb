#include "json_deserializer.hpp"

namespace duckdb {

void JsonDeserializer::SetTag(const char *tag) {
	current_tag = tag;
}

// If inside an object, return the value associated by the current tag (property name)
// If inside an array, return the next element in the sequence
yyjson_val *JsonDeserializer::GetNextValue() {
	auto parent_val = Current();
	yyjson_val *val;
	if (yyjson_is_obj(parent_val)) {
		val = yyjson_obj_get(parent_val, current_tag);
	} else if (yyjson_is_arr(parent_val)) {
		val = yyjson_arr_iter_next(&arr_iter);
	} else {
		// unreachable?
		throw InternalException("Cannot get value from non-array/object");
	}
	if (!val) {
		throw InternalException("Value/Array empty");
	}
	return val;
}

void JsonDeserializer::ThrowTypeError(yyjson_val *val, const char *expected) {
	auto actual = yyjson_get_type_desc(val);
	auto parent = Current();
	if (yyjson_is_obj(parent)) {
		auto msg =
		    StringUtil::Format("property '%s' expected type '%s', but got type: '%s'", current_tag, expected, actual);
	} else if (yyjson_is_arr(parent)) {
		auto msg = StringUtil::Format("Sequence expect child of type '%s', but got type: %s", expected, actual);
	} else {
		// unreachable?
		throw InternalException("cannot get nested value from non object or array-type");
	}
}

void JsonDeserializer::DumpDoc() {
	const char *json = yyjson_write(doc, 0, nullptr);
	printf("json: %s\n", json);
	free((void *)json);
}

void JsonDeserializer::DumpCurrent() {
	const char *json = yyjson_val_write(Current(), 0, nullptr);
	printf("json: %s\n", json);
	free((void *)json);
}

//===--------------------------------------------------------------------===//
// Nested Types Hooks
//===--------------------------------------------------------------------===//
void JsonDeserializer::OnObjectBegin() {
	auto val = GetNextValue();
	if (!yyjson_is_obj(val)) {
		ThrowTypeError(val, "object");
	}
	stack.push_back(val);
}

void JsonDeserializer::OnObjectEnd() {
	stack.pop_back();
}

idx_t JsonDeserializer::OnListBegin() {
	auto val = GetNextValue();
	if (!yyjson_is_arr(val)) {
		ThrowTypeError(val, "array");
	}
	yyjson_arr_iter_init(val, &arr_iter);
	stack.push_back(val);
	return yyjson_arr_size(val);
}

void JsonDeserializer::OnListEnd() {
	stack.pop_back();
}

// Deserialize maps as [ { key: ..., value: ... } ]
idx_t JsonDeserializer::OnMapBegin() {
	auto val = GetNextValue();
	if (!yyjson_is_arr(val)) {
		ThrowTypeError(val, "array");
	}

	yyjson_arr_iter_init(val, &arr_iter);
	stack.push_back(val);
	return yyjson_arr_size(val);
}

void JsonDeserializer::OnMapEntryBegin() {
	auto val = GetNextValue();
	if (!yyjson_is_obj(val)) {
		ThrowTypeError(val, "object");
	}
	stack.push_back(val);
}

void JsonDeserializer::OnMapKeyBegin() {
	SetTag("key");
}

void JsonDeserializer::OnMapValueBegin() {
	SetTag("value");
}

void JsonDeserializer::OnMapEntryEnd() {
	stack.pop_back();
}

void JsonDeserializer::OnMapEnd() {
	stack.pop_back();
}

bool JsonDeserializer::OnOptionalBegin() {
	auto val = GetNextValue();
	if (yyjson_is_null(val)) {
		return false;
	}
	return true;
}

//===--------------------------------------------------------------------===//
// Primitive Types
//===--------------------------------------------------------------------===//
bool JsonDeserializer::ReadBool() {
	auto val = GetNextValue();
	if (!yyjson_is_bool(val)) {
		ThrowTypeError(val, "bool");
	}
	return yyjson_get_bool(val);
}

int8_t JsonDeserializer::ReadSignedInt8() {
	auto val = GetNextValue();
	if (!yyjson_is_sint(val)) {
		ThrowTypeError(val, "int8_t");
	}
	return yyjson_get_sint(val);
}

uint8_t JsonDeserializer::ReadUnsignedInt8() {
	auto val = GetNextValue();
	if (!yyjson_is_uint(val)) {
		ThrowTypeError(val, "uint8_t");
	}
	return yyjson_get_uint(val);
}

int16_t JsonDeserializer::ReadSignedInt16() {
	auto val = GetNextValue();
	if (!yyjson_is_sint(val)) {
		ThrowTypeError(val, "int16_t");
	}
	return yyjson_get_sint(val);
}

uint16_t JsonDeserializer::ReadUnsignedInt16() {
	auto val = GetNextValue();
	if (!yyjson_is_uint(val)) {
		ThrowTypeError(val, "uint16_t");
	}
	return yyjson_get_uint(val);
}

int32_t JsonDeserializer::ReadSignedInt32() {
	auto val = GetNextValue();
	if (!yyjson_is_sint(val)) {
		ThrowTypeError(val, "int32_t");
	}
	return yyjson_get_sint(val);
}

uint32_t JsonDeserializer::ReadUnsignedInt32() {
	auto val = GetNextValue();
	if (!yyjson_is_uint(val)) {
		ThrowTypeError(val, "uint32_t");
	}
	return yyjson_get_uint(val);
}

int64_t JsonDeserializer::ReadSignedInt64() {
	auto val = GetNextValue();
	if (!yyjson_is_sint(val)) {
		ThrowTypeError(val, "int64_t");
	}
	return yyjson_get_sint(val);
}

uint64_t JsonDeserializer::ReadUnsignedInt64() {
	auto val = GetNextValue();
	if (!yyjson_is_uint(val)) {
		ThrowTypeError(val, "uint64_t");
	}
	return yyjson_get_uint(val);
}

float JsonDeserializer::ReadFloat() {
	auto val = GetNextValue();
	if (!yyjson_is_real(val)) {
		ThrowTypeError(val, "float");
	}
	return yyjson_get_real(val);
}

double JsonDeserializer::ReadDouble() {
	auto val = GetNextValue();
	if (!yyjson_is_real(val)) {
		ThrowTypeError(val, "double");
	}
	return yyjson_get_real(val);
}

string JsonDeserializer::ReadString() {
	auto val = GetNextValue();
	if (!yyjson_is_str(val)) {
		ThrowTypeError(val, "string");
	}
	return yyjson_get_str(val);
}

interval_t JsonDeserializer::ReadInterval() {
	auto val = GetNextValue();
	if (!yyjson_is_str(val)) {
		ThrowTypeError(val, "interval");
	}
	auto str = yyjson_get_str(val);
	interval_t result;
	if (Interval::FromString(str, result)) {
		return result;
	} else {
		throw SerializationException("Invalid interval format: %s", str);
	}
}

} // namespace duckdb
