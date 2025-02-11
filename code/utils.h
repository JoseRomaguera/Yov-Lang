
//- UTILS 

inline_fn u64 u64_divide_high(u64 n0, u64 n1)
{
    u64 res = n0 / n1;
    if (n0 % n1 != 0) res++;
    return res;
}

inline_fn u32 pages_from_bytes(u64 bytes) {
    return (u32)u64_divide_high(bytes, os.page_size);
}

//- ARRAY 

template<typename T>
struct Array {
    T* data;
    u32 count;
    
    inline T& operator[](u32 index) {
        assert(index < count);
        return data[index];
    }
};

template<typename T>
inline_fn Array<T> array_make(T* data, u32 count)
{
    Array<T> array;
    array.data = data;
    array.count = count;
    return array;
}

template<typename T>
inline_fn Array<T> array_make(Arena* arena, u32 count)
{
    Array<T> array;
    array.data = arena_push_struct<T>(arena, count);
    array.count = count;
    return array;
}

template<typename T>
inline_fn Array<T> array_subarray(Array<T> src, u32 offset, u32 count)
{
    assert(offset + count <= src.count);
    Array<T> dst;
    dst.data = src.data + offset;
    dst.count = count;
    return dst;
}

template<typename T>
inline_fn Array<T> array_copy(Arena* arena, Array<T> src)
{
	Array<T> dst = array_make<T>(arena, src.count);
    foreach(i, src.count) dst[i] = src[i];
    return dst;
}

template<typename T>
inline_fn void array_erase(Array<T>* array, u32 index)
{
	assert(index < array->count);
	--array->count;
    
	for (u32 i = index; i < array->count; ++i) 
        array->data[i] = array->data[i + 1];
}

//- POOLED ARRAY 

struct PooledArrayBlock
{
    PooledArrayBlock* next;
    u32 capacity;
    u32 count;
};

struct PooledArrayR
{
    Arena* arena;
    PooledArrayBlock* root;
    PooledArrayBlock* tail;
    PooledArrayBlock* current;
    u64 stride;
    
    u32 default_block_capacity;
    u32 count;
};

template<typename T>
struct PooledArray : PooledArrayR {
    inline T& operator[](u32 index)
    {
        assert(index < count);
        
        PooledArrayBlock* block = root;
        
        while (index >= block->capacity)
        {
            index -= block->capacity;
            block = block->next;
            assert(block != NULL);
        }
        
        T* data = (T*)(block + 1);
        
        return data[index];
    }
};

inline_fn PooledArrayBlock* _pooled_array_create_block(Arena* arena, u32 block_capacity, u64 stride)
{
    PooledArrayBlock* block = (PooledArrayBlock*)arena_push(arena, sizeof(PooledArrayBlock) + block_capacity * stride);
    block->next = NULL;
    block->capacity = block_capacity;
    block->count = 0;
    
    void* data = (void*)(block + 1);
    memory_zero(data, stride * block_capacity);
    
    return block;
}

inline_fn PooledArrayR pooled_array_make(Arena* arena, u64 stride, u32 block_capacity)
{
    assert(block_capacity > 0);
    
    PooledArrayR array{};
    array.default_block_capacity = block_capacity;
    array.root = _pooled_array_create_block(arena, array.default_block_capacity, stride);
    array.tail = array.root;
    array.current = array.root;
    array.arena = arena;
    array.stride = stride;
    return array;
}

inline_fn void array_reset(PooledArrayR* array)
{
    array->count = 0u;
    
    PooledArrayBlock* block = array->root;
    while (block) {
        block->count = 0;
        block = block->next;
    }
    
    array->current = array->root;
}

inline_fn void* array_add(PooledArrayR* array)
{
    assert(array->root != NULL && array->tail != NULL && array->default_block_capacity != 0);
    
    PooledArrayBlock* block = array->current;
    
    while (block->count >= block->capacity)
    {
        if (block == array->tail)
        {
            PooledArrayBlock* new_block = _pooled_array_create_block(array->arena, array->default_block_capacity, array->stride);
            block->next = new_block;
            block = new_block;
            array->tail = new_block;
        }
        else block = block->next;
    }
    
    array->current = block;
    
    u8* ptr = (u8*)(block + 1) + (block->count * array->stride);
    block->count++;
    array->count++;
    return ptr;
}

inline_fn void array_erase(PooledArrayR* array, u32 index)
{
    assert(index < array->count);
    
    PooledArrayBlock* block = array->root;
    
    while (index >= block->capacity)
    {
        index -= block->capacity;
        block = block->next;
        assert(block != NULL);
    }
    
    array->count--;
    block->count--;
    
    u8* data = (u8*)(block + 1);
    
    for (u32 i = index; i < block->count; ++i)
    {
        u64 i0 = (i + 0) * array->stride;
        u64 i1 = (i + 1) * array->stride;
        memory_copy(data + i0, data + i1, array->stride);
    }
    memory_zero(data + block->count * array->stride, array->stride);
    
    PooledArrayBlock* next_block = block->next;
    while (next_block != NULL && next_block->count > 0)
    {
        u8* next_data = (u8*)(next_block + 1);
        
        u64 last_index = block->count * array->stride;
        memory_copy(data + last_index, next_data, array->stride);
        
        block->count++;
        next_block->count--;
        
        for (u32 i = 0; i < next_block->count; ++i)
        {
            u64 i0 = (i + 0) * array->stride;
            u64 i1 = (i + 1) * array->stride;
            memory_copy(next_data + i0, next_data + i1, array->stride);
        }
        memory_zero(next_data + next_block->count * array->stride, array->stride);
        
        block = next_block;
        next_block = next_block->next;
        data = (u8*)(block + 1);
    }
    
    if (array->count) {
        
        if (block->count == 0)
        {
            block = array->root;
            while (block->next->count != 0) block = block->next;
        }
        
        array->current = block;
        assert(array->current->count > 0);
    }
    else {
        array->current = array->root;
    }
}

inline_fn void array_pop(PooledArrayR* array)
{
    if (array->count >= 1) {
        array_erase(array, array->count - 1);
    }
}

inline_fn u32 array_calculate_index(PooledArrayR* array, void* ptr)
{
    u32 index_offset = 0;
    
    PooledArrayBlock* block = array->root;
    while (block != NULL)
    {
        u8* begin_data = (u8*)(block + 1);
        u8* end_data = begin_data + (block->count * array->stride);
        
        if (ptr >= begin_data && ptr < end_data)
        {
            u64 byte_index = (u8*)ptr - begin_data;
            return index_offset + (u32)(byte_index / array->stride);
        }
        
        index_offset += block->count;
        block = block->next;
    }
    return u32_max;
}

template<typename T>
inline_fn PooledArray<T> pooled_array_make(Arena* arena, u32 block_capacity) {
    return *(PooledArray<T>*)&pooled_array_make(arena, sizeof(T), block_capacity); 
}

template<typename T>
inline_fn T* array_add(PooledArray<T>* array, const T& data)
{
    T* res = (T*)array_add(array);
    *res = data;
    return res;
}

template<typename T>
inline_fn T* array_add(PooledArray<T>* array) {
    return (T*)array_add((PooledArrayR*)array);
}

template<typename T>
inline_fn Array<T> array_from_pooled_array(Arena* arena, PooledArray<T> src)
{
    Array<T> dst = array_make<T>(arena, src.count);
    foreach(i, src.count) dst[i] = src[i];
    return dst;
}

template<typename T>
struct PooledArrayIterator
{
    PooledArray<T>* array;
    u32 index;
    T* value;
    
    PooledArrayBlock* block;
    u32 block_index;
    
    b8 valid;
};

template<typename T>
inline_fn PooledArrayIterator<T> pooled_array_create_iterator(PooledArray<T>* array)
{
    PooledArrayIterator<T> it{};
    it.array = array;
    
    if (it.array->count == 0) return it;
    
    it.block = it.array->root;
    it.value = (T*)(it.block + 1);
    it.valid = true;
    return it;
}

template<typename T>
inline_fn PooledArrayIterator<T> pooled_array_create_iterator_tail(PooledArray<T>* array)
{
    PooledArrayIterator<T> it{};
    it.array = array;
    
    if (it.array->count == 0) return it;
    
    it.index = it.array->count - 1;
    it.block = it.array->current;
    it.block_index = it.block->count - 1;
    it.value = (T*)(it.block + 1) + it.block_index;
    it.valid = true;
    return it;
}

template<typename T>
inline_fn void operator++(PooledArrayIterator<T>& it)
{
    it.index++;
    it.block_index++;
    
    if (it.index >= it.array->count) {
        it.valid = false;
        return;
    }
    
    if (it.block_index >= it.block->count)
    {
        it.block = it.block->next;
        it.block_index = 0;
    }
    
    T* data = (T*)(it.block + 1);
    it.valid = true;
    it.value = data + it.block_index;
}

template<typename T>
inline_fn void operator--(PooledArrayIterator<T>& it)
{
    if (it.index == 0) {
        it.valid = false;
        return;
    }
    
    it.index--;
    
    if (it.block_index > 0) it.block_index--;
    else
    {
        PooledArrayBlock* block = it.array->root;
        u32 index = it.index;
        
        while (index >= block->capacity)
        {
            index -= block->capacity;
            block = block->next;
            assert(block != NULL);
        }
        
        it.block = block;
        it.block_index = index;
    }
    
    assert(it.block_index < it.block->count);
    
    T* data = (T*)(it.block + 1);
    it.valid = true;
    it.value = data + it.block_index;
}

//- C STRING 

inline_fn u32 cstring_size(const char* str) {
    u32 size = 0;
    while (str[size]) size++;
    return size;
}

inline_fn u32 cstring_set(char* dst, const char* src, u32 src_size, u32 buff_size)
{
	u32 size = MIN(buff_size - 1u, src_size);
	memory_copy(dst, src, size);
	dst[size] = '\0';
	return (src_size > buff_size - 1u) ? (src_size - buff_size - 1u) : 0u;
}

inline_fn u32 cstring_copy(char* dst, const char* src, u32 buff_size)
{
	u32 src_size = cstring_size(src);
	return cstring_set(dst, src, src_size, buff_size);
}

inline_fn u32 cstring_append(char* dst, const char* src, u32 buff_size)
{
	u32 src_size = cstring_size(src);
	u32 dst_size = cstring_size(dst);
    
	u32 new_size = src_size + dst_size;
    
	u32 overflows = (buff_size < (new_size + 1u)) ? (new_size + 1u) - buff_size : 0u;
    
	u32 append_size = (overflows > src_size) ? 0u : (src_size - overflows);
    
	memory_copy(dst + dst_size, src, append_size);
	new_size = dst_size + append_size;
	dst[new_size] = '\0';
	
	return overflows;
}

inline_fn void cstring_from_u64(char* dst, u64 value, u32 base)
{
	assert(base >= 1);
    
	u32 digits = 0u;
    
	u64 aux = value;
	while (aux != 0) {
		aux /= base;
		++digits;
	}
    
	if (digits == 0u) {
		cstring_copy(dst, "0", 20);
		return;
	}
    
	i32 end = (i32)digits - 1;
    
	for (i32 i = end; i >= 0; --i) {
        
		u64 v = value % base;
        
		switch (v) {
            
            case 0:
			dst[i] = '0';
			break;
            
            case 1:
			dst[i] = '1';
			break;
            
            case 2:
			dst[i] = '2';
			break;
            
            case 3:
			dst[i] = '3';
			break;
            
            case 4:
			dst[i] = '4';
			break;
            
            case 5:
			dst[i] = '5';
			break;
            
            case 6:
			dst[i] = '6';
			break;
            
            case 7:
			dst[i] = '7';
			break;
            
            case 8:
			dst[i] = '8';
			break;
            
            case 9:
			dst[i] = '9';
			break;
            
            default:
            {
                v -= 10;
                
                u32 count = ('Z' - 'A') + 1;
                
                u32 char_index = (u32)(v % (u64)count);
                u32 char_case = (u32)(v / (u64)count);
                
                if (char_case == 0)
                {
                    dst[i] = (char)(char_index + 'A');
                }
                else if (char_case == 1)
                {
                    dst[i] = (char)(char_index + 'a');
                }
                else
                {
                    assert(0);
                }
            }
            break;
            
		}
        
		value /= base;
	}
    
	dst[end + 1] = '\0';
}

inline_fn void cstring_from_i64(char* dst, i64 value, u32 base = 10)
{
    if (value < 0)
    {
        dst[0] = '-';
        dst++;
        value = -value;
    }
    
    cstring_from_u64(dst, (u64)value, base);
}

inline_fn void cstring_from_f64(char* dst, f64 value, u32 decimals)
{
	i64 decimal_mult = 0;
    
	if (decimals > 0)
	{
		u64 d = decimals;
        
		decimal_mult = 10;
		d--;
        
		while (d--)
		{
			decimal_mult *= 10;
		}
	}
    
	b8 minus = value < 0.0;
	value = ABS(value);
    
    i64 integer = (i64)value;
    i64 decimal = (i64)((value - (f64)integer) * (f64)decimal_mult);
    
	if (minus) cstring_copy(dst, "-", 50);
	else cstring_copy(dst, "", 50);
    
    char int_str[50];
	cstring_from_u64(int_str, integer, 10);
    
    cstring_append(dst, int_str, 50);
	cstring_append(dst, ".", 50);
    
	char raw_decimal_string[100];
	cstring_from_u64(raw_decimal_string, decimal, 10);
    
	u32 decimal_size = cstring_size(raw_decimal_string);
	while (decimal_size < decimals) {
		cstring_append(dst, "0", 20);
		decimal_size++;
	}
    
	cstring_append(dst, raw_decimal_string, 50);
}

//- STRING 

#define STR(x) string_make(x)

inline_fn String string_make(const char* cstr, u64 size) {
    String str;
    str.data = (char*)cstr;
    str.size = size;
    return str;
}

inline_fn String string_make(const char* cstr) {
    return string_make(cstr, cstring_size(cstr));
}

inline_fn String string_make(String str) { return str; }

inline_fn String string_make(RawBuffer buffer) {
    String str;
    str.data = (char*)buffer.data;
    str.size = buffer.size;
    return str;
}

inline_fn String string_copy(Arena* arena, String src) {
    String dst;
    dst.size = src.size;
    dst.data = (char*)arena_push(arena, (u32)dst.size + 1);
    memory_copy(dst.data, src.data, src.size);
    return dst;
}

inline_fn String string_substring(String str, u64 offset, u64 size) {
    assert(offset + size <= str.size);
    String res{};
    res.data = str.data + offset;
    res.size = size;
    return res;
}

inline_fn b32 string_equals(String s0, String s1) {
    if (s0.size != s1.size) return false;
    foreach(i, s0.size) {
        if (s0[i] != s1[i]) return false;
    }
    return true;
}

inline_fn b32 u32_from_string(u32* dst, String str)
{
	u32 digits = (u32)str.size;
	*dst = 0u;
    
	if (digits == 0) return false;
    
	u32 mul = 10;
	foreach(i, digits - 1)
        mul *= 10;
    
	foreach(i, digits) {
        
		mul /= 10;
		
		char c = str[i];
		i32 v = c - '0';
        if (v < 0 || v > 9) return false;
        
		v *= mul;
		*dst += v;
	}
    
	return true;
}

inline_fn b32 u32_from_char(u32* dst, char c)
{
    *dst = 0;
	i32 v = c - '0';
    if (v < 0 || v > 9) return false;
    *dst = v;
	return true;
}

inline_fn b32 i64_from_string(String str, i64* out)
{
    u32 digits = (u32)str.size;
	*out = 0;
    
	if (digits == 0) return false;
    
	u64 mul = 10;
	foreach(i, digits - 1) mul *= 10;
    
	foreach(i, digits) 
    {
		mul /= 10;
		
		char c = str[i];
		i64 v = c - '0';
        if (v < 0 || v > 9) return false;
        
		v *= mul;
		*out += v;
	}
    
	return true;
}

inline_fn b32 i32_from_string(String str, i32* out)
{
    u32 digits = (u32)str.size;
	*out = 0;
    
	if (digits == 0) return false;
    
	u32 mul = 10;
	foreach(i, digits - 1) mul *= 10;
    
	foreach(i, digits) 
    {
		mul /= 10;
		
		char c = str[i];
		i32 v = c - '0';
        if (v < 0 || v > 9) return false;
        
		v *= mul;
		*out += v;
	}
    
	return true;
}

//- LINKED LIST

struct LLNode {
    LLNode* next;
    LLNode* prev;
};

template<typename T>
struct LinkedList {
    LLNode* root;
    LLNode* tail;
    u32 count;
};

template<typename T>
inline_fn LinkedList<T> ll_make() { return {}; }

template<typename T>
inline_fn T* ll_push(Arena* arena, LinkedList<T>* ll)
{
    LLNode* node = (LLNode*)arena_push(arena, sizeof(LLNode) + sizeof(T));
    ll->count++;
    
    if (ll->root == NULL) {
        assert(ll->tail == NULL);
        ll->root = node;
    }
    
    node->prev = ll->tail;
    if (ll->tail) ll->tail->next = node;
    ll->tail = node;
    
    return (T*)(node + 1);
}

template<typename T>
inline_fn T* ll_push(Arena* arena, LinkedList<T>* ll, const T& data)
{
    T* v = ll_push(arena, ll);
    *v = data;
    return v;
}

template<typename T>
inline_fn void* ll_push_back(Arena* arena, LinkedList<T>* ll)
{
    assert(ll->stride > 0);
    LLNode* node = (LLNode*)arena_push(arena, sizeof(LLNode) + ll->stride);
    ll->count++;
    
    if (ll->tail == NULL) {
        assert(ll->root == NULL);
        ll->tail = node;
    }
    
    node->next = ll->root;
    if (ll->root) ll->root->prev = node;
    ll->root = node;
    
    return node + 1;
}

template<typename T>
inline_fn Array<T> array_from_ll(Arena* arena, LinkedList<T> src) {
    Array<T> dst = array_make<T>(arena, src.count);
    u32 i = 0;
    for (LLNode* node = src.root; node != NULL; node = node->next) {
        dst[i++] = *(T*)(node + 1);
    }
    return dst;
}

//- STRING BUILDER

struct StringBuilder {
    LinkedList<String> ll;
    Arena* arena;
    char* buffer;
    u64 buffer_size;
    u64 buffer_pos;
};

inline_fn StringBuilder string_builder_make(Arena* arena) {
    StringBuilder builder{};
    builder.ll = ll_make<String>();
    builder.arena = arena;
    builder.buffer_size = 128;
    builder.buffer = (char*)arena_push(arena, builder.buffer_size);
    return builder;
}

inline_fn void _string_builder_push_buffer(StringBuilder* builder) {
    if (builder->buffer_pos == 0) return;
    
    String str = string_make(builder->buffer, builder->buffer_pos);
    str = string_copy(builder->arena, str);
    ll_push(builder->arena, &builder->ll, str);
    
    builder->buffer_pos = 0;
}

inline_fn void append(StringBuilder* builder, String str)
{
    if (str.size == 0) return;
    
    if (str.size > builder->buffer_size) {
        _string_builder_push_buffer(builder);
        
        String str_node = string_copy(builder->arena, str);
        ll_push(builder->arena, &builder->ll, str_node);
    }
    else {
        u64 bytes_left = builder->buffer_size - builder->buffer_pos;
        if (str.size > bytes_left) {
            _string_builder_push_buffer(builder);
        }
        
        memory_copy(builder->buffer + builder->buffer_pos, str.data, str.size);
        builder->buffer_pos += str.size;
    }
}

inline_fn void append(StringBuilder* builder, const char* cstr) {
    append(builder, STR(cstr));
}

inline_fn void append_i64(StringBuilder* builder, i64 v, u32 base = 10)
{
    char cstr[100];
    cstring_from_i64(cstr, v, base);
    append(builder, cstr);
}
inline_fn void append_i32(StringBuilder* builder, i32 v, u32 base = 10) { append_i64(builder, (i64)v, base); }

inline_fn void append_u64(StringBuilder* builder, u64 v, u32 base = 10)
{
    char cstr[100];
    cstring_from_u64(cstr, v, base);
    append(builder, cstr);
}
inline_fn void append_u32(StringBuilder* builder, u32 v, u32 base = 10) { append_u64(builder, (u64)v, base); }

inline_fn void append_f64(StringBuilder* builder, f64 v, u32 decimals)
{
    char cstr[100];
    cstring_from_f64(cstr, v, decimals);
    append(builder, cstr);
}

inline_fn void append_char(StringBuilder* builder, char c) {
    append(builder, string_make(&c, 1));
}

inline_fn String string_join(Arena* arena, LinkedList<String> ll)
{
    u64 size = 0;
    for (LLNode* node = ll.root; node != NULL; node = node->next) {
        String* str = (String*)(node + 1);
        size += str->size;
    }
    
    char* data = (char*)arena_push(arena, size + 1);
    char* it = data;
    
    for (LLNode* node = ll.root; node != NULL; node = node->next) {
        String* str = (String*)(node + 1);
        memory_copy(it, str->data, str->size);
        it += str->size;
    }
    
    return string_make(data, size);
}

inline_fn String string_from_builder(Arena* arena, StringBuilder* builder)
{
    _string_builder_push_buffer(builder);
    return string_join(arena, builder->ll);
}

inline_fn Array<String> string_split(Arena* arena, String str, String separator)
{
    SCRATCH(arena);
    LinkedList<String> ll = ll_make<String>();
    
    u64 next_offset = 0;
    
    u64 cursor = 0;
    while (cursor < str.size)
    {
        String sub = string_substring(str, cursor, MIN(separator.size, str.size - cursor));
        
        if (string_equals(sub, separator)) {
            
            String next = string_substring(str, next_offset, cursor - next_offset);
            ll_push(scratch.arena, &ll, next);
            
            cursor += separator.size;
            next_offset = cursor;
        }
        else cursor++;
    }
    
    if (next_offset < cursor) {
        String next = string_substring(str, next_offset, cursor - next_offset);
        ll_push(scratch.arena, &ll, next);
    }
    
    return array_from_ll(arena, ll);
}

//- STRING FORMAT

inline_fn String string_replace(Arena* arena, String str, String old_str, String new_str)
{
    assert(old_str.size > 0);
    
    if (str.size < old_str.size) return str;
    
    SCRATCH(arena);
    LinkedList<String> ll = ll_make<String>();
    
    u64 next_offset = 0;
    
    u64 cursor = 0;
    while (cursor <= str.size - old_str.size)
    {
        String sub = string_substring(str, cursor, old_str.size);
        if (string_equals(sub, old_str)) {
            
            String next = string_substring(str, next_offset, cursor - next_offset);
            ll_push(scratch.arena, &ll, next);
            ll_push(scratch.arena, &ll, new_str);
            
            cursor += old_str.size;
            next_offset = cursor;
        }
        else cursor++;
    }
    
    if (next_offset == 0) return str;
    
    if (next_offset < str.size) {
        String next = string_substring(str, next_offset, str.size - next_offset);
        ll_push(scratch.arena, &ll, next);
    }
    
    return string_join(arena, ll);
}

inline_fn String string_format_with_args(Arena* arena, String string, va_list args)
{
    SCRATCH(arena);
    StringBuilder builder = string_builder_make(scratch.arena);
    
    const u32 default_number_of_decimals = 4;
    
    b8 type_mode = false;
    u32 number_of_decimals = default_number_of_decimals;
    
    b8 invalid_format = false;
    
    u32 cursor = 0;
    while (cursor < string.size && !invalid_format)
    {
        DEFER(cursor++);
        
        char c = string[cursor];
        
        if (!type_mode && c == '%')
        {
            number_of_decimals = default_number_of_decimals;
            type_mode = true;
            continue;
        }
        
        if (type_mode)
        {
            if (c == 'i')
            {
                i32 n = va_arg(args, i32);
                append_i64(&builder, n);
                type_mode = false;
                continue;
            }
            if (c == 'l')
            {
                i64 n = va_arg(args, i64);
                append_i64(&builder, n);
                type_mode = false;
                continue;
            }
            if (c == '%')
            {
                append(&builder, string_make(&c, 1));
                type_mode = false;
                continue;
            }
            if (c == 'u')
            {
                u32 n = va_arg(args, u32);
                append_u64(&builder, n);
                type_mode = false;
                continue;
            }
            if (c == 'S')
            {
                String str = va_arg(args, String);
                append(&builder, str);
                type_mode = false;
                continue;
            }
            if (c == 's')
            {
                const char* cstr = va_arg(args, const char*);
                append(&builder, cstr);
                type_mode = false;
                continue;
            }
            if (c == 'f')
            {
                double f = va_arg(args, double);
                append_f64(&builder, (f64)f, number_of_decimals);
                type_mode = false;
                continue;
            }
            if (c == '.')
            {
                cursor++;
                char num = string[cursor];
                if (!u32_from_char(&number_of_decimals, num)) invalid_format = true;
                continue;
            }
            
            invalid_format = true;
            break;
        }
        
        append_char(&builder, c);
    }
    
    if (invalid_format) {
        return string_copy(arena, STR("Invalid Format!!"));
    }
    
    return string_from_builder(arena, &builder);
}

inline_fn String string_format_ex(Arena* arena, String string, ...)
{
    va_list args;
	va_start(args, string);
    String result = string_format_with_args(arena, string, args);
    va_end(args);
    return result;
}

#define string_format(arena, str, ...) string_format_ex(arena, STR(str), __VA_ARGS__)