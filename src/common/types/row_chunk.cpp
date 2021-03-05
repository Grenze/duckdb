#include "duckdb/common/types/row_chunk.hpp"

#include <cfloat>
#include <cstring> // strlen() on Solaris
#include <limits.h>

namespace duckdb {

//! these are optimized and assume a particular byte order
#define BSWAP16(x) ((uint16_t)((((uint16_t)(x)&0xff00) >> 8) | (((uint16_t)(x)&0x00ff) << 8)))

#define BSWAP32(x)                                                                                                     \
	((uint32_t)((((uint32_t)(x)&0xff000000) >> 24) | (((uint32_t)(x)&0x00ff0000) >> 8) |                               \
	            (((uint32_t)(x)&0x0000ff00) << 8) | (((uint32_t)(x)&0x000000ff) << 24)))

#define BSWAP64(x)                                                                                                     \
	((uint64_t)((((uint64_t)(x)&0xff00000000000000ull) >> 56) | (((uint64_t)(x)&0x00ff000000000000ull) >> 40) |        \
	            (((uint64_t)(x)&0x0000ff0000000000ull) >> 24) | (((uint64_t)(x)&0x000000ff00000000ull) >> 8) |         \
	            (((uint64_t)(x)&0x00000000ff000000ull) << 8) | (((uint64_t)(x)&0x0000000000ff0000ull) << 24) |         \
	            (((uint64_t)(x)&0x000000000000ff00ull) << 40) | (((uint64_t)(x)&0x00000000000000ffull) << 56)))

RowChunk::RowChunk(BufferManager &buffer_manager, idx_t block_capacity, idx_t entry_size)
    : buffer_manager(buffer_manager), count(0), block_capacity(block_capacity), entry_size(entry_size) {
	int n = 1;
	//! little endian if true
	if (*(char *)&n == 1) {
		is_little_endian = true;
	} else {
		is_little_endian = false;
	}
}

RowChunk::RowChunk(RowChunk &other)
    : buffer_manager(other.buffer_manager), count(0), block_capacity(other.block_capacity),
      entry_size(other.entry_size), is_little_endian(other.is_little_endian) {
}

static uint8_t FlipSign(uint8_t key_byte) {
	return key_byte ^ 128;
}

static uint32_t EncodeFloat(float x) {
	uint64_t buff;

	//! zero
	if (x == 0) {
		buff = 0;
		buff |= (1u << 31);
		return buff;
	}
	//! infinity
	if (x > FLT_MAX) {
		return UINT_MAX;
	}
	//! -infinity
	if (x < -FLT_MAX) {
		return 0;
	}
	buff = Load<uint32_t>((const_data_ptr_t)&x);
	if ((buff & (1u << 31)) == 0) { //! +0 and positive numbers
		buff |= (1u << 31);
	} else {          //! negative numbers
		buff = ~buff; //! complement 1
	}

	return buff;
}

static uint64_t EncodeDouble(double x) {
	uint64_t buff;
	//! zero
	if (x == 0) {
		buff = 0;
		buff += (1ull << 63);
		return buff;
	}
	//! infinity
	if (x > DBL_MAX) {
		return ULLONG_MAX;
	}
	//! -infinity
	if (x < -DBL_MAX) {
		return 0;
	}
	buff = Load<uint64_t>((const_data_ptr_t)&x);
	if (buff < (1ull << 63)) { //! +0 and positive numbers
		buff += (1ull << 63);
	} else {          //! negative numbers
		buff = ~buff; //! complement 1
	}
	return buff;
}

template <>
void RowChunk::EncodeData(data_t *data, bool value) {
	data[0] = value ? 1 : 0;
}

template <>
void RowChunk::EncodeData(data_t *data, int8_t value) {
	reinterpret_cast<uint8_t *>(data)[0] = value;
	data[0] = FlipSign(data[0]);
}

template <>
void RowChunk::EncodeData(data_t *data, int16_t value) {
	reinterpret_cast<uint16_t *>(data)[0] = is_little_endian ? BSWAP16(value) : value;
	data[0] = FlipSign(data[0]);
}

template <>
void RowChunk::EncodeData(data_t *data, int32_t value) {
	reinterpret_cast<uint32_t *>(data)[0] = is_little_endian ? BSWAP32(value) : value;
	data[0] = FlipSign(data[0]);
}

template <>
void RowChunk::EncodeData(data_t *data, int64_t value) {
	reinterpret_cast<uint64_t *>(data)[0] = is_little_endian ? BSWAP64(value) : value;
	data[0] = FlipSign(data[0]);
}

template <>
void RowChunk::EncodeData(data_t *data, uint8_t value) {
	reinterpret_cast<uint8_t *>(data)[0] = value;
}

template <>
void RowChunk::EncodeData(data_t *data, uint16_t value) {
	reinterpret_cast<uint16_t *>(data)[0] = is_little_endian ? BSWAP16(value) : value;
}

template <>
void RowChunk::EncodeData(data_t *data, uint32_t value) {
	reinterpret_cast<uint32_t *>(data)[0] = is_little_endian ? BSWAP32(value) : value;
}

template <>
void RowChunk::EncodeData(data_t *data, uint64_t value) {
	reinterpret_cast<uint64_t *>(data)[0] = is_little_endian ? BSWAP64(value) : value;
}

template <>
void RowChunk::EncodeData(data_t *data, hugeint_t value) {
	throw NotImplementedException("hugeint_t encoding not implemented");
}

template <>
void RowChunk::EncodeData(data_t *data, float value) {
	uint32_t converted_value = EncodeFloat(value);
	reinterpret_cast<uint32_t *>(data)[0] = is_little_endian ? BSWAP32(converted_value) : converted_value;
}

template <>
void RowChunk::EncodeData(data_t *data, double value) {
	uint64_t converted_value = EncodeDouble(value);
	reinterpret_cast<uint64_t *>(data)[0] = is_little_endian ? BSWAP64(converted_value) : converted_value;
}

template <>
void RowChunk::EncodeData(data_t *data, string_t value) {
	idx_t len = value.GetSize() + 1;
	memcpy(data, value.GetDataUnsafe(), len - 1);
	data[len - 1] = '\0';
}

template <>
void RowChunk::EncodeData(data_t *data, const char *value) {
	EncodeData(data, string_t(value, strlen(value)));
}

template <class T>
void RowChunk::TemplatedSerializeVectorSortable(VectorData &vdata, const SelectionVector &sel, idx_t add_count,
                                                data_ptr_t key_locations[], bool has_null, bool invert) {
	auto source = (T *)vdata.data;
	if (has_null) {
		auto &validity = vdata.validity;
		const data_t valid = invert ? 0 : 1;
		const data_t invalid = invert ? 0 : 1;

		for (idx_t i = 0; i < add_count; i++) {
			auto idx = sel.get_index(i);
			auto source_idx = vdata.sel->get_index(idx);
			// write validity
			key_locations[i][0] = validity.RowIsValid(source_idx) ? valid : invalid;
			key_locations[i]++;
			// write value
			EncodeData(key_locations[i], source[source_idx]);
			key_locations[i] += sizeof(T);
		}
	} else {
		for (idx_t i = 0; i < add_count; i++) {
			auto idx = sel.get_index(i);
			auto source_idx = vdata.sel->get_index(idx);
			// write value
			EncodeData(key_locations[i], source[source_idx]);
			key_locations[i] += sizeof(T);
		}
	}
}

void RowChunk::SerializeVectorSortable(Vector &v, idx_t vcount, const SelectionVector &sel, idx_t ser_count,
                                       data_ptr_t key_locations[], bool has_null, bool invert) {
	VectorData vdata;
	v.Orrify(vcount, vdata);
	switch (v.GetType().InternalType()) {
	case PhysicalType::BOOL:
	case PhysicalType::INT8:
		TemplatedSerializeVectorSortable<int8_t>(vdata, sel, ser_count, key_locations, has_null, invert);
		break;
	case PhysicalType::INT16:
		TemplatedSerializeVectorSortable<int16_t>(vdata, sel, ser_count, key_locations, has_null, invert);
		break;
	case PhysicalType::INT32:
		TemplatedSerializeVectorSortable<int32_t>(vdata, sel, ser_count, key_locations, has_null, invert);
		break;
	case PhysicalType::INT64:
		TemplatedSerializeVectorSortable<int64_t>(vdata, sel, ser_count, key_locations, has_null, invert);
		break;
	case PhysicalType::UINT8:
		TemplatedSerializeVectorSortable<uint8_t>(vdata, sel, ser_count, key_locations, has_null, invert);
		break;
	case PhysicalType::UINT16:
		TemplatedSerializeVectorSortable<uint16_t>(vdata, sel, ser_count, key_locations, has_null, invert);
		break;
	case PhysicalType::UINT32:
		TemplatedSerializeVectorSortable<uint32_t>(vdata, sel, ser_count, key_locations, has_null, invert);
		break;
	case PhysicalType::UINT64:
		TemplatedSerializeVectorSortable<uint64_t>(vdata, sel, ser_count, key_locations, has_null, invert);
		break;
	case PhysicalType::INT128:
		TemplatedSerializeVectorSortable<hugeint_t>(vdata, sel, ser_count, key_locations, has_null, invert);
		break;
	case PhysicalType::FLOAT:
		TemplatedSerializeVectorSortable<float>(vdata, sel, ser_count, key_locations, has_null, invert);
		break;
	case PhysicalType::DOUBLE:
		TemplatedSerializeVectorSortable<double>(vdata, sel, ser_count, key_locations, has_null, invert);
		break;
	case PhysicalType::HASH:
		TemplatedSerializeVectorSortable<hash_t>(vdata, sel, ser_count, key_locations, has_null, invert);
		break;
	case PhysicalType::INTERVAL:
		TemplatedSerializeVectorSortable<interval_t>(vdata, sel, ser_count, key_locations, has_null, invert);
		break;
	case PhysicalType::VARCHAR: {
		// TODO
	}
	default:
		throw NotImplementedException("FIXME: unimplemented deserialize");
	}
}

void RowChunk::SerializeIndices(data_ptr_t key_locations[], idx_t start, idx_t added_count) {
	for (idx_t i = 0; i < added_count; i++) {
		Store(start + i, key_locations[i]);
	}
}

template <class T>
static void TemplatedSerializeVData(VectorData &vdata, const SelectionVector &sel, idx_t count, idx_t col_idx,
                                    data_ptr_t *key_locations, data_ptr_t *validitymask_locations) {
	auto source = (T *)vdata.data;
	if (!validitymask_locations) {
		for (idx_t i = 0; i < count; i++) {
			auto idx = sel.get_index(i);
			auto source_idx = vdata.sel->get_index(idx);

			auto target = (T *)key_locations[i];
			Store<T>(source[source_idx], (data_ptr_t)target);
			key_locations[i] += sizeof(T);
		}
	} else {
		auto byte_offset = col_idx / 8;
		auto offset_in_byte = col_idx % 8;
		for (idx_t i = 0; i < count; i++) {
			auto idx = sel.get_index(i);
			auto source_idx = vdata.sel->get_index(idx);

			auto target = (T *)key_locations[i];
			Store<T>(source[source_idx], (data_ptr_t)target);
			key_locations[i] += sizeof(T);

			// set the validitymask
			if (!vdata.validity.RowIsValid(i)) {
				*(validitymask_locations[i] + byte_offset) &= ~(1UL << offset_in_byte);
			}
		}
	}
}

void RowChunk::SerializeVectorData(VectorData &vdata, PhysicalType type, const SelectionVector &sel, idx_t ser_count,
                                   idx_t col_idx, data_ptr_t key_locations[], data_ptr_t validitymask_locations[]) {
	switch (type) {
	case PhysicalType::BOOL:
	case PhysicalType::INT8:
		TemplatedSerializeVData<int8_t>(vdata, sel, ser_count, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::INT16:
		TemplatedSerializeVData<int16_t>(vdata, sel, ser_count, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::INT32:
		TemplatedSerializeVData<int32_t>(vdata, sel, ser_count, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::INT64:
		TemplatedSerializeVData<int64_t>(vdata, sel, ser_count, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::UINT8:
		TemplatedSerializeVData<uint8_t>(vdata, sel, ser_count, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::UINT16:
		TemplatedSerializeVData<uint16_t>(vdata, sel, ser_count, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::UINT32:
		TemplatedSerializeVData<uint32_t>(vdata, sel, ser_count, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::UINT64:
		TemplatedSerializeVData<uint64_t>(vdata, sel, ser_count, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::INT128:
		TemplatedSerializeVData<hugeint_t>(vdata, sel, ser_count, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::FLOAT:
		TemplatedSerializeVData<float>(vdata, sel, ser_count, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::DOUBLE:
		TemplatedSerializeVData<double>(vdata, sel, ser_count, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::HASH:
		TemplatedSerializeVData<hash_t>(vdata, sel, ser_count, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::INTERVAL:
		TemplatedSerializeVData<interval_t>(vdata, sel, ser_count, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::VARCHAR: {
		auto strings = (string_t *)vdata.data;
		auto byte_offset = col_idx / 8;
		auto offset_in_byte = col_idx % 8;
		for (idx_t i = 0; i < ser_count; i++) {
			auto &string_entry = strings[vdata.sel->get_index(i)];

			// store string size
			Store<uint32_t>(string_entry.GetSize(), key_locations[i]);
			key_locations[i] += string_t::PREFIX_LENGTH;

			// store the string
			memcpy(key_locations[i], string_entry.GetDataUnsafe(), string_entry.GetSize());
			key_locations[i] += string_entry.GetSize();

			// set the validitymask
			if (!vdata.validity.RowIsValid(i)) {
				*(validitymask_locations[i] + byte_offset) &= ~(1UL << offset_in_byte);
			}
		}
		break;
	}
	default:
		throw NotImplementedException("FIXME: unimplemented serialize");
	}
}

void RowChunk::SerializeVector(Vector &v, idx_t vcount, const SelectionVector &sel, idx_t ser_count, idx_t col_idx,
                               data_ptr_t key_locations[], data_ptr_t validitymask_locations[]) {
	VectorData vdata;
	v.Orrify(vcount, vdata);
	SerializeVectorData(vdata, v.GetType().InternalType(), sel, ser_count, col_idx, key_locations,
	                    validitymask_locations);
}

idx_t RowChunk::AppendToBlock(RowDataBlock &block, BufferHandle &handle, vector<BlockAppendEntry> &append_entries,
                              idx_t remaining) {
	idx_t append_count = MinValue<idx_t>(remaining, block.CAPACITY - block.count);
	auto dataptr = handle.node->buffer + block.count * entry_size;
	append_entries.emplace_back(dataptr, append_count);
	block.count += append_count;
	return append_count;
}

idx_t RowChunk::Build(idx_t added_count, data_ptr_t key_locations[]) {
	vector<unique_ptr<BufferHandle>> handles;
	vector<BlockAppendEntry> append_entries;
	idx_t starting_count;

	// first allocate space of where to serialize the keys and payload columns
	idx_t remaining = added_count;
	{
		// first append to the last block (if any)
		lock_guard<mutex> append_lock(rc_lock);
		starting_count = count;
		count += added_count;
		if (!blocks.empty()) {
			auto &last_block = blocks.back();
			if (last_block.count < last_block.CAPACITY) {
				// last block has space: pin the buffer of this block
				auto handle = buffer_manager.Pin(last_block.block);
				// now append to the block
				idx_t append_count = AppendToBlock(last_block, *handle, append_entries, remaining);
				remaining -= append_count;
				handles.push_back(move(handle));
			}
		}
		while (remaining > 0) {
			// now for the remaining data, allocate new buffers to store the data and append there
			RowDataBlock new_block(buffer_manager, block_capacity, entry_size);
			auto handle = buffer_manager.Pin(new_block.block);

			idx_t append_count = AppendToBlock(new_block, *handle, append_entries, remaining);
			remaining -= append_count;

			blocks.push_back(move(new_block));
			handles.push_back(move(handle));
		}
	}
	// now set up the key_locations based on the append entries
	idx_t append_idx = 0;
	for (auto &append_entry : append_entries) {
		idx_t next = append_idx + append_entry.count;
		for (; append_idx < next; append_idx++) {
			key_locations[append_idx] = append_entry.baseptr;
			append_entry.baseptr += entry_size;
		}
	}
	return starting_count;
}

template <class T>
static void TemplatedDeserializeIntoVector(Vector &v, idx_t count, idx_t col_idx, data_ptr_t *key_locations,
                                           data_ptr_t *validitymask_locations) {
	auto target = FlatVector::GetData<T>(v);
	auto byte_offset = col_idx / 8;
	auto offset_in_byte = col_idx % 8;
	auto &validity = FlatVector::Validity(v);
	for (idx_t i = 0; i < count; i++) {
		target[i] = Load<T>(key_locations[i]);
		key_locations[i] += sizeof(T);
		validity.Set(i, *(validitymask_locations[i] + byte_offset) & (1 << offset_in_byte));
	}
}

void RowChunk::DeserializeIntoVectorData(Vector &v, PhysicalType type, idx_t vcount, idx_t col_idx,
                                         data_ptr_t key_locations[], data_ptr_t validitymask_locations[]) {
	switch (type) {
	case PhysicalType::BOOL:
	case PhysicalType::INT8:
		TemplatedDeserializeIntoVector<int8_t>(v, vcount, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::INT16:
		TemplatedDeserializeIntoVector<int16_t>(v, vcount, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::INT32:
		TemplatedDeserializeIntoVector<int32_t>(v, vcount, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::INT64:
		TemplatedDeserializeIntoVector<int64_t>(v, vcount, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::UINT8:
		TemplatedDeserializeIntoVector<uint8_t>(v, vcount, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::UINT16:
		TemplatedDeserializeIntoVector<uint16_t>(v, vcount, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::UINT32:
		TemplatedDeserializeIntoVector<uint32_t>(v, vcount, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::UINT64:
		TemplatedDeserializeIntoVector<uint64_t>(v, vcount, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::INT128:
		TemplatedDeserializeIntoVector<hugeint_t>(v, vcount, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::FLOAT:
		TemplatedDeserializeIntoVector<float>(v, vcount, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::DOUBLE:
		TemplatedDeserializeIntoVector<double>(v, vcount, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::HASH:
		TemplatedDeserializeIntoVector<hash_t>(v, vcount, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::INTERVAL:
		TemplatedDeserializeIntoVector<interval_t>(v, vcount, col_idx, key_locations, validitymask_locations);
		break;
	case PhysicalType::VARCHAR: {
		idx_t len;
		auto target = FlatVector::GetData<string_t>(v);
		auto byte_offset = col_idx / 8;
		auto offset_in_byte = col_idx % 8;
		auto &validity = FlatVector::Validity(v);
		for (idx_t i = 0; i < vcount; i++) {
			// deserialize string length
			len = Load<uint32_t>(key_locations[i]);
			key_locations[i] += string_t::PREFIX_LENGTH;
			// deserialize string
			target[i] = StringVector::AddString(v, (const char *)key_locations[i], len);
			key_locations[i] += len;
			// set validitymask
			validity.Set(i, *(validitymask_locations[i] + byte_offset) & (1 << offset_in_byte));
		}
		break;
	}
	default:
		throw NotImplementedException("FIXME: unimplemented deserialize");
	}
}

void RowChunk::DeserializeIntoVector(Vector &v, const idx_t &vcount, const idx_t &col_idx, data_ptr_t key_locations[],
                                     data_ptr_t validitymask_locations[]) {
	DeserializeIntoVectorData(v, v.GetType().InternalType(), vcount, col_idx, key_locations, validitymask_locations);
}

void RowChunk::SkipOverType(PhysicalType &type, idx_t &vcount, data_ptr_t key_locations[]) {
	if (TypeIsConstantSize(type)) {
		const idx_t size = GetTypeIdSize(type);
		for (idx_t i = 0; i < vcount; i++) {
			key_locations[i] += size;
		}
	} else {
		switch (type) {
		case PhysicalType::VARCHAR: {
			for (idx_t i = 0; i < vcount; i++) {
				key_locations[i] += string_t::PREFIX_LENGTH + Load<uint32_t>(key_locations[i]);
			}
			break;
		}
		default:
			throw NotImplementedException("FIXME: unimplemented SkipOverType");
		}
	}
}

} // namespace duckdb
