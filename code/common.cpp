#include "inc.h"

void assertion_failed(const char* text)
{
    os_print(Severity_Error, STR(text));
    *((u8*)0) = 0;
}

void memory_copy(void* dst, const void* src, u64 size)
{
    // TODO(Jose): Optimize
    u64 iteration_stride = 4 * sizeof(size_t);
    u64 iterations = size / iteration_stride;
    
    void* end_dst = (u8*)dst + (iterations * iteration_stride);
    void* end_src = (u8*)src + (iterations * iteration_stride);
    
    size_t* d = (size_t*)dst;
    const size_t* s = (const size_t*)src;
    while (d < end_dst) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
        d += 4;
        s += 4;
    }
    
    u8* byte_dst = (u8*)end_dst;
    const u8* byte_src = (const u8*)end_src;
    
    end_dst = (u8*)dst + size;
    
    while (byte_dst < end_dst) {
        *byte_dst = *byte_src;
        byte_dst++;
        byte_src++;
    }
}

void memory_zero(void* dst, u64 size)
{
    // NOTE(Jose): MSVC with /O2 flag is replacing this function with memset, thats why we are using volatile keyword
    
    u64 iteration_stride = 4 * sizeof(size_t);
    u64 iterations = size / iteration_stride;
    
    void* end_dst = (u8*)dst + (iterations * iteration_stride);
    
    volatile size_t* d = (size_t*)dst;
    while (d < end_dst) {
        d[0] = 0;
        d[1] = 0;
        d[2] = 0;
        d[3] = 0;
        d += 4;
    }
    
    volatile u8* byte_dst = (u8*)end_dst;
    end_dst = (u8*)dst + size;
    
    while (byte_dst < end_dst) {
        *byte_dst = 0;
        byte_dst++;
    }
}

//- ARENA 

Arena* arena_alloc(u64 capacity, u32 alignment)
{
    alignment = MAX(alignment, 1);
    Arena* arena = (Arena*)os_allocate_heap(sizeof(Arena));
    arena->reserved_pages = pages_from_bytes(capacity);
    arena->memory = os_reserve_virtual_memory(arena->reserved_pages, false);
    arena->alignment = alignment;
    return arena;
}

void arena_free(Arena* arena) {
    if (arena == NULL) return;
    os_release_virtual_memory(arena->memory);
    os_free_heap(arena);
}

void* arena_push(Arena* arena, u64 size)
{
    u64 position = arena->memory_position;
    position = u64_divide_high(position, arena->alignment) * arena->alignment;
    
    u64 page_size = yov->os.page_size;
    u64 commited_size = (u64)arena->commited_pages * page_size;
    u64 reserved_size = (u64)arena->reserved_pages * page_size;
    
    u64 end_position = position + (u64)size;
    
    if (end_position > reserved_size) {
        return NULL; // TODO(Jose): Fatal error, out of memory
    }
    
    if (end_position > commited_size)
    {
        u32 commited_pages_needed = pages_from_bytes(end_position);
        u32 page_count = commited_pages_needed - arena->commited_pages;
        page_count = MAX(page_count, pages_from_bytes(KB(200)));
        os_commit_virtual_memory(arena->memory, arena->commited_pages, page_count);
        arena->commited_pages += page_count;
    }
    
    arena->memory_position = end_position;
    return (u8*)arena->memory + position;
}

void arena_pop_to(Arena* arena, u64 position)
{
    assert(position <= arena->memory_position);
    u64 bytes_poped = arena->memory_position - position;
    arena->memory_position = position;
    memory_zero((u8*)arena->memory + arena->memory_position, bytes_poped);
}

void initialize_scratch_arenas()
{
    foreach(i, array_count(yov->scratch_arenas)) {
        yov->scratch_arenas[i] = arena_alloc(GB(16), 8);
    }
}

void shutdown_scratch_arenas() {
    foreach(i, array_count(yov->scratch_arenas)) arena_free(yov->scratch_arenas[i]);
}

ScratchArena arena_create_scratch(Arena* conflict0, Arena* conflict1)
{
    u32 index = 0;
    while (yov->scratch_arenas[index] == conflict0 || yov->scratch_arenas[index] == conflict1) index++;
    
    assert(index < array_count(yov->scratch_arenas));
    
    ScratchArena scratch;
    scratch.arena = yov->scratch_arenas[index];
    scratch.start_position = scratch.arena->memory_position;
    return scratch;
}

void arena_destroy_scratch(ScratchArena scratch)
{
    arena_pop_to(scratch.arena, scratch.start_position);
}

//- MATH 

u64 u64_divide_high(u64 n0, u64 n1)
{
    u64 res = n0 / n1;
    if (n0 % n1 != 0) res++;
    return res;
}

u32 pages_from_bytes(u64 bytes) {
    return (u32)u64_divide_high(bytes, yov->os.page_size);
}

//- CSTRING 

u32 cstring_size(const char* str) {
    u32 size = 0;
    while (str[size]) size++;
    return size;
}

u32 cstring_set(char* dst, const char* src, u32 src_size, u32 buff_size)
{
	u32 size = MIN(buff_size - 1u, src_size);
	memory_copy(dst, src, size);
	dst[size] = '\0';
	return (src_size > buff_size - 1u) ? (src_size - buff_size - 1u) : 0u;
}

u32 cstring_copy(char* dst, const char* src, u32 buff_size)
{
	u32 src_size = cstring_size(src);
	return cstring_set(dst, src, src_size, buff_size);
}

u32 cstring_append(char* dst, const char* src, u32 buff_size)
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

void cstring_from_u64(char* dst, u64 value, u32 base)
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

void cstring_from_i64(char* dst, i64 value, u32 base)
{
    if (value < 0)
    {
        dst[0] = '-';
        dst++;
        value = -value;
    }
    
    cstring_from_u64(dst, (u64)value, base);
}

void cstring_from_f64(char* dst, f64 value, u32 decimals)
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

String string_make(const char* cstr, u64 size) {
    String str;
    str.data = (char*)cstr;
    str.size = size;
    return str;
}

String string_make(const char* cstr) {
    return string_make(cstr, cstring_size(cstr));
}

String string_make(String str) { return str; }

String string_make(RawBuffer buffer) {
    String str;
    str.data = (char*)buffer.data;
    str.size = buffer.size;
    return str;
}

String string_copy(Arena* arena, String src) {
    String dst;
    dst.size = src.size;
    dst.data = (char*)arena_push(arena, (u32)dst.size + 1);
    memory_copy(dst.data, src.data, src.size);
    return dst;
}

String string_substring(String str, u64 offset, u64 size) {
    assert(offset + size <= str.size);
    String res{};
    res.data = str.data + offset;
    res.size = size;
    return res;
}

b32 string_equals(String s0, String s1) {
    if (s0.size != s1.size) return false;
    foreach(i, s0.size) {
        if (s0[i] != s1[i]) return false;
    }
    return true;
}

b32 u32_from_string(u32* dst, String str)
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

b32 u32_from_char(u32* dst, char c)
{
    *dst = 0;
	i32 v = c - '0';
    if (v < 0 || v > 9) return false;
    *dst = v;
	return true;
}

b32 i64_from_string(String str, i64* out)
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

b32 i32_from_string(String str, i32* out)
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

String string_from_memory(Arena* arena, u64 bytes)
{
    f64 kb = bytes / 1024.0;
    
    if (kb < 1.0) {
        return string_format(arena, "%l b", bytes);
    }
    
    f64 mb = kb / 1024.0;
    
    if (mb < 1.0) {
        return string_format(arena, "%.2f KB", kb);
    }
    
    f64 gb = mb / 1024.0;
    
    if (gb < 1.0) {
        return string_format(arena, "%.2f MB", mb);
    }
    
    return string_format(arena, "%.2f GB", gb);
}

String string_join(Arena* arena, LinkedList<String> ll)
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

Array<String> string_split(Arena* arena, String str, String separator)
{
    SCRATCH(arena);
    LinkedList<String> ll = ll_make<String>(scratch.arena);
    
    u64 next_offset = 0;
    
    u64 cursor = 0;
    while (cursor < str.size)
    {
        String sub = string_substring(str, cursor, MIN(separator.size, str.size - cursor));
        
        if (string_equals(sub, separator)) {
            
            String next = string_substring(str, next_offset, cursor - next_offset);
            ll_push(&ll, next);
            
            cursor += separator.size;
            next_offset = cursor;
        }
        else cursor++;
    }
    
    if (next_offset < cursor) {
        String next = string_substring(str, next_offset, cursor - next_offset);
        ll_push(&ll, next);
    }
    
    return array_from_ll(arena, ll);
}

String string_replace(Arena* arena, String str, String old_str, String new_str)
{
    assert(old_str.size > 0);
    
    if (str.size < old_str.size) return str;
    
    SCRATCH(arena);
    LinkedList<String> ll = ll_make<String>(scratch.arena);
    
    u64 next_offset = 0;
    
    u64 cursor = 0;
    while (cursor <= str.size - old_str.size)
    {
        String sub = string_substring(str, cursor, old_str.size);
        if (string_equals(sub, old_str)) {
            
            String next = string_substring(str, next_offset, cursor - next_offset);
            ll_push(&ll, next);
            ll_push(&ll, new_str);
            
            cursor += old_str.size;
            next_offset = cursor;
        }
        else cursor++;
    }
    
    if (next_offset == 0) return str;
    
    if (next_offset < str.size) {
        String next = string_substring(str, next_offset, str.size - next_offset);
        ll_push(&ll, next);
    }
    
    return string_join(arena, ll);
}

String string_format_with_args(Arena* arena, String string, va_list args)
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

String string_format_ex(Arena* arena, String string, ...)
{
    va_list args;
	va_start(args, string);
    String result = string_format_with_args(arena, string, args);
    va_end(args);
    return result;
}

u32 string_get_codepoint(String str, u64* cursor_ptr)
{
    // TODO(Jose):
    u64 cursor = *cursor_ptr;
    u32 codepoint = str[cursor];
    *cursor_ptr = cursor + 1;
    return codepoint;
}

//- PATH 

Array<String> path_subdivide(Arena* arena, String path)
{
    SCRATCH(arena);
    PooledArray<String> list = pooled_array_make<String>(scratch.arena, 32);
    
    u64 last_element = 0;
    u64 cursor = 0;
    while (cursor < path.size)
    {
        if (path[cursor] == '/') {
            String element = string_substring(path, last_element, cursor - last_element);
            if (element.size > 0) array_add(&list, element);
            last_element = cursor + 1;
        }
        
        cursor++;
    }
    
    String element = string_substring(path, last_element, cursor - last_element);
    if (element.size > 0) array_add(&list, element);
    
    return array_from_pooled_array(arena, list);
}

String path_resolve(Arena* arena, String path)
{
    SCRATCH(arena);
    String res = path;
    res = string_replace(scratch.arena, res, STR("\\"), STR("/"));
    
    Array<String> elements = path_subdivide(scratch.arena, res);
    
    {
        i32 remove_prev_element_count = 0;
        
        for (i32 i = (i32)elements.count - 1; i >= 0; --i) {
            if (string_equals(elements[i], STR(".."))) {
                array_erase(&elements, i);
                remove_prev_element_count++;
            }
            else if (string_equals(elements[i], STR(".")) || remove_prev_element_count) {
                array_erase(&elements, i);
                if (remove_prev_element_count) remove_prev_element_count--;
            }
        }
    }
    
    StringBuilder builder = string_builder_make(scratch.arena);
    
    foreach(i, elements.count) {
        String element = elements[i];
        append(&builder, element);
        if (i < elements.count - 1) append_char(&builder, '/');
    }
    
    res = string_from_builder(scratch.arena, &builder);
    if (os_path_is_directory(res)) res = string_format(scratch.arena, "%S/", res);
    
    return string_copy(arena, res);
}

String path_append(Arena* arena, String str0, String str1)
{
    if (os_path_is_absolute(str1)) return string_copy(arena, str0);
    if (str0.size == 0) return string_copy(arena, str1);
    if (str1.size == 0) return string_copy(arena, str0);
    
    if (str0[str0.size - 1] != '/') return string_format(arena, "%S/%S", str0, str1);
    return string_format(arena, "%S%S", str0, str1);
}

String path_get_last_element(String path)
{
    // TODO(Jose): Optimize
    SCRATCH();
    Array<String> array = path_subdivide(scratch.arena, path);
    if (array.count == 0) return {};
    return array[array.count - 1];
}

//- STRING BUILDER

StringBuilder string_builder_make(Arena* arena) {
    StringBuilder builder{};
    builder.ll = ll_make<String>(arena);
    builder.arena = arena;
    builder.buffer_size = 128;
    builder.buffer = (char*)arena_push(arena, builder.buffer_size);
    return builder;
}

inline_fn void _string_builder_push_buffer(StringBuilder* builder) {
    if (builder->buffer_pos == 0) return;
    
    String str = string_make(builder->buffer, builder->buffer_pos);
    str = string_copy(builder->arena, str);
    ll_push(&builder->ll, str);
    
    builder->buffer_pos = 0;
}

void appendf_ex(StringBuilder* builder, String str, ...)
{
    SCRATCH(builder->arena);
    va_list args;
	va_start(args, str);
    String result = string_format_with_args(scratch.arena, str, args);
    va_end(args);
    
    append(builder, result);
}

void append(StringBuilder* builder, String str)
{
    if (str.size == 0) return;
    
    if (str.size > builder->buffer_size) {
        _string_builder_push_buffer(builder);
        
        String str_node = string_copy(builder->arena, str);
        ll_push(&builder->ll, str_node);
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

void append(StringBuilder* builder, const char* cstr) {
    append(builder, STR(cstr));
}

void append_i64(StringBuilder* builder, i64 v, u32 base)
{
    char cstr[100];
    cstring_from_i64(cstr, v, base);
    append(builder, cstr);
}
void append_i32(StringBuilder* builder, i32 v, u32 base) { append_i64(builder, (i64)v, base); }

void append_u64(StringBuilder* builder, u64 v, u32 base)
{
    char cstr[100];
    cstring_from_u64(cstr, v, base);
    append(builder, cstr);
}
void append_u32(StringBuilder* builder, u32 v, u32 base) { append_u64(builder, (u64)v, base); }

void append_f64(StringBuilder* builder, f64 v, u32 decimals)
{
    char cstr[100];
    cstring_from_f64(cstr, v, decimals);
    append(builder, cstr);
}

void append_char(StringBuilder* builder, char c) {
    append(builder, string_make(&c, 1));
}

String string_from_builder(Arena* arena, StringBuilder* builder)
{
    _string_builder_push_buffer(builder);
    return string_join(arena, builder->ll);
}

//- POOLED ARRAY 

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

PooledArrayR pooled_array_make(Arena* arena, u64 stride, u32 block_capacity)
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

void array_reset(PooledArrayR* array)
{
    array->count = 0u;
    
    PooledArrayBlock* block = array->root;
    while (block) {
        block->count = 0;
        block = block->next;
    }
    
    array->current = array->root;
}

void* array_add(PooledArrayR* array)
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

void array_erase(PooledArrayR* array, u32 index)
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

void array_pop(PooledArrayR* array)
{
    if (array->count >= 1) {
        array_erase(array, array->count - 1);
    }
}

u32 array_calculate_index(PooledArrayR* array, void* ptr)
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

//- MISC 

String string_from_binary_operator(BinaryOperator op) {
    if (op == BinaryOperator_Addition) return STR("+");
    if (op == BinaryOperator_Substraction) return STR("-");
    if (op == BinaryOperator_Multiplication) return STR("*");
    if (op == BinaryOperator_Division) return STR("/");
    if (op == BinaryOperator_Modulo) return STR("%");
    if (op == BinaryOperator_LogicalNot) return STR("!");
    if (op == BinaryOperator_LogicalOr) return STR("||");
    if (op == BinaryOperator_LogicalAnd) return STR("&&");
    if (op == BinaryOperator_Equals) return STR("==");
    if (op == BinaryOperator_NotEquals) return STR("!=");
    if (op == BinaryOperator_LessThan) return STR("<");
    if (op == BinaryOperator_LessEqualsThan) return STR("<=");
    if (op == BinaryOperator_GreaterThan) return STR(">");
    if (op == BinaryOperator_GreaterEqualsThan) return STR(">=");
    assert(0);
    return STR("?");
}

String string_from_tokens(Arena* arena, Array<Token> tokens)
{
    SCRATCH(arena);
    StringBuilder builder = string_builder_make(scratch.arena);
    
    foreach(i, tokens.count) {
        append(&builder, tokens[i].value);
    }
    
    String res = string_from_builder(scratch.arena, &builder);
    res = string_replace(scratch.arena, res, STR("\n"), STR("\\n"));
    res = string_replace(scratch.arena, res, STR("\r"), STR(""));
    res = string_replace(scratch.arena, res, STR("\t"), STR(" "));
    
    return string_copy(arena, res);
}

String debug_info_from_token(Arena* arena, Token token)
{
    if (token.kind == TokenKind_Separator) return STR("separator");
    if (token.kind == TokenKind_Identifier) return string_format(arena, "identifier: %S", token.value);
    if (token.kind == TokenKind_IfKeyword) return STR("if");
    if (token.kind == TokenKind_ElseKeyword) return STR("else");
    if (token.kind == TokenKind_WhileKeyword) return STR("while");
    if (token.kind == TokenKind_ForKeyword) return STR("for");
    if (token.kind == TokenKind_EnumKeyword) return STR("enum");
    if (token.kind == TokenKind_IntLiteral) return string_format(arena, "Int Literal: %S", token.value);
    if (token.kind == TokenKind_BoolLiteral) { return string_format(arena, "Bool Literal: %S", token.value); }
    if (token.kind == TokenKind_StringLiteral) { return string_format(arena, "String Literal: %S", token.value); }
    if (token.kind == TokenKind_Comment) { return string_format(arena, "Comment: %S", token.value); }
    if (token.kind == TokenKind_Comma) return STR(STR(","));
    if (token.kind == TokenKind_Dot) return STR(STR("."));
    if (token.kind == TokenKind_Colon) return STR(STR(":"));
    if (token.kind == TokenKind_OpenBrace) return STR(STR("{"));
    if (token.kind == TokenKind_CloseBrace) return STR(STR("}"));
    if (token.kind == TokenKind_OpenBracket) return STR(STR("["));
    if (token.kind == TokenKind_CloseBracket) return STR(STR("]"));
    if (token.kind == TokenKind_OpenParenthesis) return STR(STR("("));
    if (token.kind == TokenKind_CloseParenthesis) return STR(STR(")"));
    if (token.kind == TokenKind_Assignment) {
        if (token.binary_operator == BinaryOperator_None) return STR("=");
        return string_format(arena, "%S=", string_from_binary_operator(token.binary_operator));
    }
    if (token.kind == TokenKind_BinaryOperator) return string_format(arena, "%S", string_from_binary_operator(token.binary_operator));
    if (token.kind == TokenKind_OpenString) return STR("Open String");
    if (token.kind == TokenKind_NextLine) return STR("Next Line");
    if (token.kind == TokenKind_CloseString) return STR("Close String");
    if (token.kind == TokenKind_NextSentence) return STR(";");
    if (token.kind == TokenKind_Error) return STR("Error");
    if (token.kind == TokenKind_None) return STR("None");
    
    return STR("?");
}

void log_tokens(Array<Token> tokens)
{
    SCRATCH();
    foreach(i, tokens.count) {
        String str = debug_info_from_token(scratch.arena, tokens[i]);
        print_info("-> %S\n", str);
    }
}

u32 get_node_size(OpKind kind) {
    if (kind == OpKind_None) return sizeof(OpNode);
    if (kind == OpKind_Error) return sizeof(OpNode);
    if (kind == OpKind_Block) return sizeof(OpNode_Block);
    if (kind == OpKind_Assignment) return sizeof(OpNode_Assignment);
    if (kind == OpKind_Symbol) return sizeof(OpNode_Symbol);
    if (kind == OpKind_Indexing) return sizeof(OpNode_Indexing);
    if (kind == OpKind_IfStatement) return sizeof(OpNode_IfStatement);
    if (kind == OpKind_WhileStatement) return sizeof(OpNode_WhileStatement);
    if (kind == OpKind_ForStatement) return sizeof(OpNode_ForStatement);
    if (kind == OpKind_ForeachArrayStatement) return sizeof(OpNode_ForeachArrayStatement);
    if (kind == OpKind_ObjectDefinition) return sizeof(OpNode_ObjectDefinition);
    if (kind == OpKind_ObjectType) return sizeof(OpNode_ObjectType);
    if (kind == OpKind_FunctionCall) return sizeof(OpNode_FunctionCall);
    if (kind == OpKind_Return) return sizeof(OpNode_Return);
    if (kind == OpKind_ArrayExpresion) return sizeof(OpNode_ArrayExpresion);
    if (kind == OpKind_Binary) return sizeof(OpNode_Binary);
    if (kind == OpKind_Sign) return sizeof(OpNode_Sign);
    if (kind == OpKind_IntLiteral) return sizeof(OpNode_Literal);
    if (kind == OpKind_StringLiteral) return sizeof(OpNode_Literal);
    if (kind == OpKind_BoolLiteral) return sizeof(OpNode_Literal);
    if (kind == OpKind_MemberValue) return sizeof(OpNode_MemberValue);
    if (kind == OpKind_EnumDefinition) return sizeof(OpNode_EnumDefinition);
    if (kind == OpKind_StructDefinition) return sizeof(OpNode_StructDefinition);
    if (kind == OpKind_FunctionDefinition) return sizeof(OpNode_FunctionDefinition);
    if (kind == OpKind_Import) return sizeof(OpNode_Import);
    assert(0);
    return sizeof(OpNode) + KB(4);
}

Array<OpNode*> get_node_childs(Arena* arena, OpNode* node)
{
    SCRATCH(arena);
    
    PooledArray<OpNode*> nodes = pooled_array_make<OpNode*>(scratch.arena, 8);
    
    if (node->kind == OpKind_Block) {
        auto node0 = (OpNode_Block*)node;
        foreach(i, node0->ops.count) {
            array_add(&nodes, node0->ops[i]);
        }
    }
    else if (node->kind == OpKind_IfStatement) {
        auto node0 = (OpNode_IfStatement*)node;
        array_add(&nodes, node0->expresion);
        array_add(&nodes, node0->success);
        array_add(&nodes, node0->failure);
    }
    else if (node->kind == OpKind_WhileStatement) {
        auto node0 = (OpNode_WhileStatement*)node;
        array_add(&nodes, node0->expresion);
        array_add(&nodes, node0->content);
    }
    else if (node->kind == OpKind_ForStatement) {
        auto node0 = (OpNode_ForStatement*)node;
        array_add(&nodes, node0->initialize_sentence);
        array_add(&nodes, node0->condition_expresion);
        array_add(&nodes, node0->update_sentence);
        array_add(&nodes, node0->content);
    }
    else if (node->kind == OpKind_ForeachArrayStatement) {
        auto node0 = (OpNode_ForeachArrayStatement*)node;
        array_add(&nodes, node0->expresion);
        array_add(&nodes, node0->content);
    }
    else if (node->kind == OpKind_Assignment) {
        auto node0 = (OpNode_Assignment*)node;
        array_add(&nodes, node0->destination);
        array_add(&nodes, node0->source);
    }
    else if (node->kind == OpKind_ObjectDefinition) {
        auto node0 = (OpNode_ObjectDefinition*)node;
        array_add(&nodes, (OpNode*)node0->type);
        array_add(&nodes, node0->assignment);
    }
    else if (node->kind == OpKind_FunctionCall) {
        auto node0 = (OpNode_FunctionCall*)node;
        Array<OpNode*> params = node0->parameters;
        foreach(i, params.count)
            array_add(&nodes, params[i]);
    }
    else if (node->kind == OpKind_ArrayExpresion) {
        auto node0 = (OpNode_ArrayExpresion*)node;
        Array<OpNode*> exps = node0->nodes;
        foreach(i, exps.count)
            array_add(&nodes, exps[i]);
    }
    else if (node->kind == OpKind_Indexing) {
        auto node0 = (OpNode_Indexing*)node;
        array_add(&nodes, node0->value);
        array_add(&nodes, node0->index);
    }
    else if (node->kind == OpKind_Binary) {
        auto node0 = (OpNode_Binary*)node;
        array_add(&nodes, node0->left);
        array_add(&nodes, node0->right);
    }
    else if (node->kind == OpKind_Sign) {
        auto node0 = (OpNode_Sign*)node;
        array_add(&nodes, node0->expresion);
    }
    else if (node->kind == OpKind_MemberValue) {
        auto node0 = (OpNode_MemberValue*)node;
        array_add(&nodes, node0->expresion);
    }
    else if (node->kind == OpKind_ObjectType) {}
    else if (node->kind == OpKind_IntLiteral) {}
    else if (node->kind == OpKind_StringLiteral) {}
    else if (node->kind == OpKind_BoolLiteral) {}
    else if (node->kind == OpKind_Symbol) {}
    else if (node->kind == OpKind_StructDefinition) {
        auto node0 = (OpNode_StructDefinition*)node;
        foreach(i, node0->members.count) array_add(&nodes, (OpNode*)node0->members[i]);
    }
    else if (node->kind == OpKind_FunctionDefinition) {}
    else if (node->kind == OpKind_EnumDefinition) {}
    else if (node->kind == OpKind_None) {}
    else if (node->kind == OpKind_Error) {}
    else {
        assert(0);
    }
    
    return array_from_pooled_array(arena, nodes);
}

//- REPORT 

void print_ex(Severity severity, String str, ...)
{
    SCRATCH();
    
    va_list args;
	va_start(args, str);
    String result = string_format_with_args(scratch.arena, str, args);
    va_end(args);
    os_print(severity, result);
}

Yov* yov;

void yov_initialize()
{
    yov = (Yov*)os_allocate_heap(sizeof(Yov));
    
    os_setup_memory_info();
    
    yov->static_arena = arena_alloc(GB(32), 8);
    yov->temp_arena = arena_alloc(GB(32), 8);
    initialize_scratch_arenas();
    
    os_initialize();
    
    yov->scripts = pooled_array_make<YovScript>(yov->static_arena, 8);
    yov->reports = pooled_array_make<Report>(yov->static_arena, 32);
    
    yov->caller_dir = os_get_working_path(yov->static_arena);
}

void yov_shutdown()
{
    if (yov == NULL) return;
    
#if DEV
    if (1)
    {
        SCRATCH();
        
        print_separator();
        
        u64 committed_static_memory = yov->static_arena->commited_pages * yov->os.page_size;
        u64 committed_temp_memory = yov->temp_arena->commited_pages * yov->os.page_size;
        u64 committed_total_memory = committed_static_memory + committed_temp_memory;
        
        u64 used_static_memory = yov->static_arena->memory_position;
        u64 used_temp_memory = yov->temp_arena->memory_position;
        u64 used_total_memory = used_static_memory + used_temp_memory;
        
        print_info("Committed Static Memory: %S\n", string_from_memory(scratch.arena, committed_static_memory));
        print_info("Committed Temp Memory: %S\n", string_from_memory(scratch.arena, committed_temp_memory));
        print_info("Committed Total Memory: %S\n", string_from_memory(scratch.arena, committed_total_memory));
        print_info("\n");
        print_info("Used Static Memory: %S\n", string_from_memory(scratch.arena, used_static_memory));
        print_info("Used Temp Memory: %S\n", string_from_memory(scratch.arena, used_temp_memory));
        print_info("Used Total Memory: %S\n", string_from_memory(scratch.arena, used_total_memory));
        
        if (0) {
            print_info(STR("\n// TOKENS\n"));
            // TODO(Jose): log_tokens(tokens);
            print_info("\n\n");
        }
        if (0) {
            print_info(STR("// AST\n"));
            // TODO(Jose): log_ast(ast, 0);
            print_info("\n\n");
        }
    }
    
    os_console_wait();
#endif
    
    shutdown_scratch_arenas();
    os_shutdown();
    
    arena_free(yov->temp_arena);
    arena_free(yov->static_arena);
    yov = NULL;
}

void yov_run()
{
    b32 run = yov_read_args();
    
    if (!run || yov->error_count > 0) {
        yov_print_reports();
        return;
    }
    
    i32 main_script_id = yov_import_script(yov->main_script_path);
    
    if (main_script_id < 0) {
        yov_print_reports();
        return;
    }
    
    InterpreterSettings settings{};
    settings.print_execution = yov->settings.trace;
    settings.user_assertion = yov->settings.user_assert;
    
    // Semantic analysis of all the AST before execution
    {
        settings.execute = false;
        interpret(settings);
    }
    
    if (yov->error_count != 0) {
        yov_print_reports();
        return;
    }
    
    // Execute
    if (!yov->settings.analyze_only) {
        settings.execute = true;
        interpret(settings);
    }
    
    if (yov->error_count != 0) {
        // TODO(Jose): This reports should be considered runtime errors
        yov_print_reports();
        return;
    }
}

internal_fn String resolve_import_path(Arena* arena, Yov* yov, String caller_script_dir, String path)
{
    SCRATCH(arena);
    path = path_resolve(scratch.arena, path);
    
    if (!os_path_is_absolute(path)) {
        path = path_append(scratch.arena, caller_script_dir, path);
    }
    
    return string_copy(arena, path);
}

i32 yov_import_script(String path)
{
    SCRATCH();
    assert(os_path_is_absolute(path));
    
    RawBuffer raw_file;
    if (!os_read_entire_file(yov->static_arena, path, &raw_file)) {
        report_error(NO_CODE, "File '%S' not found\n", path);
        return -1;
    }
    
    path = string_copy(yov->static_arena, path);
    
    i32 script_id = yov->scripts.count;
    YovScript* script = array_add(&yov->scripts);
    
    script->path = path;
    script->name = path_get_last_element(path);
    script->dir = path_resolve(yov->static_arena, path_append(scratch.arena, path, STR("..")));;
    script->text = STR(raw_file);
    
    script->tokens = generate_tokens(script->text, true, script_id);
    script->ast = generate_ast(script->tokens, true);
    
    Array<OpNode_Import*> imports = get_imports(scratch.arena, script->ast);
    
    foreach(i, imports.count)
    {
        OpNode_Import* import = imports[i];
        
        String import_path = resolve_import_path(scratch.arena, yov, script->dir, import->path);
        
        b32 imported = false;
        for (auto it = pooled_array_make_iterator(&yov->scripts); it.valid; ++it) {
            if (string_equals(it.value->path, import_path)) {
                imported = true;
                break;
            }
        }
        if (imported) continue;
        
        yov_import_script(import_path);
    }
    
    return script_id;
}

YovScript* yov_get_script(i32 script_id)
{
    if (script_id < 0) return NULL;
    
    if (script_id >= yov->scripts.count) {
        assert(0);
        return NULL;
    }
    
    return &yov->scripts[script_id];
}

String yov_get_line_sample(Arena* arena, CodeLocation code)
{
    if (code.script_id < 0) return {};
    
    String text = yov_get_script(code.script_id)->text;
    u64 starting_cursor = code.start_line_offset;
    
    u64 cursor = starting_cursor;
    while (cursor < text.size) {
        u64 next_cursor = cursor;
        u32 codepoint = string_get_codepoint(text, &next_cursor);
        if (codepoint == '\r') break;
        if (codepoint == '\n') break;
        if (cursor == starting_cursor && (codepoint == '\t' || codepoint == ' ')) starting_cursor = next_cursor;
        cursor = next_cursor;
    }
    
    String sample = string_substring(text, starting_cursor, cursor - starting_cursor);
    return string_format(arena, "'%S'", sample);
}

internal_fn b32 generate_program_args(Array<String> raw_args)
{
    SCRATCH();
    yov->args = array_make<ProgramArg>(yov->static_arena, raw_args.count);
    
    b32 success = true;
    
    foreach(i, raw_args.count)
    {
        String raw = raw_args[i];
        
        Array<String> split = string_split(scratch.arena, raw, STR("="));
        
        ProgramArg arg{};
        if (split.count == 1) {
            arg.name = split[0];
            arg.value = STR("1");
        }
        else if (split.count == 2) {
            arg.name = split[0];
            arg.value = split[1];
        }
        else {
            report_error(NO_CODE, "Invalid arg '%S', expected format: name=value\n", raw);
            success = false;
            arg.name = STR("?");
            arg.value = STR("0");
        }
        
        yov->args[i] = arg;
    }
    
    return success;
}

b32 yov_read_args()
{
    Array<String> args = os_get_args(yov->static_arena);
    
    if (args.count <= 0) {
        report_error(NO_CODE, "Script not specified");
        return false;
    }
    
    if (args.count == 1 && string_equals(args[0], STR("help"))) {
        print_info("TODO\n");
        return false;
    }
    
    if (args.count == 1 && string_equals(args[0], STR("version"))) {
        print_info(YOV_VERSION);
        return false;
    }
    
    i32 script_args_start_index = args.count;
    for (i32 i = 1; i < args.count; ++i) {
        String arg = args[i];
        
        if (string_equals(arg, STR("/"))) {
            script_args_start_index = i + 1;
            break;
        }
        
        if (string_equals(arg, STR("analyze"))) yov->settings.analyze_only = true;
        else if (string_equals(arg, STR("trace"))) yov->settings.trace = true;
        else if (string_equals(arg, STR("user_assert"))) yov->settings.user_assert = true;
        else {
            report_error(NO_CODE, "Unknown interpreter argument '%S'\n", arg);
        }
    }
    
    Array<String> script_args = array_subarray(args, script_args_start_index, args.count - script_args_start_index);
    generate_program_args(script_args);
    
    yov->main_script_path = args[0];
    
    return true;
}

void report_error_ex(CodeLocation code, String text, ...)
{
    SCRATCH();
    
    va_list args;
	va_start(args, text);
    String formatted_text = string_format_with_args(scratch.arena, text, args);
    va_end(args);
    
    String line_sample = yov_get_line_sample(scratch.arena, code);
    formatted_text = string_replace(scratch.arena, formatted_text, STR("{line}"), line_sample);
    
    Report report;
    report.text = string_copy(yov->static_arena, formatted_text);
    report.code = code;
    array_add(&yov->reports, report);
    
    yov->error_count++;
}

internal_fn i32 report_compare(const void* _0, const void* _1)
{
    const Report* r0 = (const Report*)_0;
    const Report* r1 = (const Report*)_1;
    
    if (r0->code.offset == r1->code.offset) return 0;
    return (r0->code.offset < r1->code.offset) ? -1 : 1;
}

void yov_print_reports()
{
    SCRATCH();
    Array<Report> reports = array_from_pooled_array(scratch.arena, yov->reports);
    array_sort(reports, report_compare);
    
    foreach(i, reports.count) {
        print_report(reports[i]);
    }
}

void print_report(Report report)
{
    YovScript* script = yov_get_script(report.code.script_id);
    
    if (script == NULL) print(Severity_Error, "%S\n", report.text);
    else print(Severity_Error, "%S(%u): %S\n", script->path, (u32)report.code.line, report.text);
}

