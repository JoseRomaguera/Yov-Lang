#include "inc.h"

void assertion_failed(const char* text, const char* file, u32 line)
{
    os_print(Severity_Error, text);
    *((u8*)0) = 0;
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
    foreach(i, countof(yov->scratch_arenas)) {
        yov->scratch_arenas[i] = arena_alloc(GB(16), 8);
    }
}

void shutdown_scratch_arenas() {
    foreach(i, countof(yov->scratch_arenas)) arena_free(yov->scratch_arenas[i]);
}

ScratchArena arena_create_scratch(Arena* conflict0, Arena* conflict1)
{
    u32 index = 0;
    while (yov->scratch_arenas[index] == conflict0 || yov->scratch_arenas[index] == conflict1) index++;
    
    assert(index < countof(yov->scratch_arenas));
    
    ScratchArena scratch;
    scratch.arena = yov->scratch_arenas[index];
    scratch.start_position = scratch.arena->memory_position;
    return scratch;
}

void arena_destroy_scratch(ScratchArena scratch)
{
    arena_pop_to(scratch.arena, scratch.start_position);
}

//- OS UTILS

void file_info_set_path(FileInfo* info, String path)
{
    info->path = path;
    info->folder = {};
    info->name = {};
    info->name_without_extension = {};
    info->extension = {};
    
    if (path.size == 0) return;
    
    i32 start_name = (i32)path.size - 1;
    
    while (start_name > 0) {
        if (path[start_name] == '/') {
            start_name++;
            break;
        }
        start_name--;
    }
    
    info->folder = string_substring(path, 0, start_name);
    info->name = string_substring(path, start_name, path.size - start_name);
    
    i32 start_extension = (i32)info->name.size - 1;
    for (; start_extension >= 0; --start_extension)
    {
        if (info->name[start_extension] == '.') {
            start_extension++;
            break;
        }
    }
    
    if (start_extension >= 0 && start_extension < info->name.size) info->extension = string_substring(info->name, start_extension, info->name.size - start_extension);
    
    if (info->extension.size) info->name_without_extension = string_substring(info->name, 0, info->name.size - info->extension.size - 1);
    else info->name_without_extension = info->name;
}

//- MATH 

u64 u64_divide_high(u64 n0, u64 n1)
{
    u64 res = n0 / n1;
    if (n0 % n1 != 0) res++;
    return res;
}
u32 u32_divide_high(u32 n0, u32 n1)
{
    u32 res = n0 / n1;
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

b32 string_starts(String str, String with) {
    if (with.size > str.size) return false;
    return string_equals(string_substring(str, 0, with.size), with);
}

b32 string_ends(String str, String with) {
    if (with.size > str.size) return false;
    return string_equals(string_substring(str, str.size - with.size, with.size), with);
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

String string_from_codepoint(Arena* arena, u32 c)
{
    u32 byte_count;
    
    if (c <= 0x7F) {
        byte_count = 1;
    }
    else if (c <= 0x7FF) {
        byte_count = 2;
    }
    else if (c <= 0xFFFF) {
        byte_count = 3;
    }
    else if (c <= 0x10FFFF) {
        byte_count = 4;
    }
    else {
        return {};
    }
    
    String res{};
    res.data = (char*)arena_push(arena, byte_count + 1);
    res.size = byte_count;
    
    if (byte_count == 1) {
        res[0] = (char)c;
    }
    else if (byte_count == 2) {
        res[0] = (char)(0xC0 | (c >> 6));
        res[1] = (char)(0x80 | (c & 0x3F));
    }
    else if (byte_count == 3) {
        res[0] = (char)(0xE0 | (c >> 12));
        res[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        res[2] = (char)(0x80 | (c & 0x3F));
    }
    else if (byte_count == 4) {
        res[0] = (char)(0xF0 | (c >> 18));
        res[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        res[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        res[3] = (char)(0x80 | (c & 0x3F));
    }
    
    return res;
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
        invalid_codepath();
        return {};
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
    u64 cursor = *cursor_ptr;
    DEFER(*cursor_ptr = cursor);
    
    if (cursor >= str.size) return 0;
    
    const char* it = str.data + cursor;
    
    u32 c = 0;
    char b = *it;
    
	u32 byte_count;
    if ((b & 0xF0) == 0xF0) byte_count = 4;
    else if ((b & 0xE0) == 0xE0) byte_count = 3;
    else if ((b & 0xC0) == 0xC0) byte_count = 2;
    else byte_count = 1;
    
    if (cursor + byte_count > str.size) {
        cursor = str.size;
        return 0xFFFD;
    }
    
    if (byte_count == 1) {
        c = (u32)b;
    }
    else if (byte_count == 2) {
        c |= ((u32)(it[0] & 0b00011111)) << 6;
        c |= ((u32)(it[1] & 0b00111111)) << 0;
    }
    else if (byte_count == 3) {
        c |= ((u32)(it[0] & 0b00001111)) << 12;
        c |= ((u32)(it[1] & 0b00111111)) << 6;
        c |= ((u32)(it[2] & 0b00111111)) << 0;
    }
    else {
        c |= ((u32)(it[0] & 0b00000111)) << 18;
        c |= ((u32)(it[1] & 0b00111111)) << 12;
        c |= ((u32)(it[2] & 0b00111111)) << 6;
        c |= ((u32)(it[3] & 0b00111111)) << 0;
    }
    
    cursor += byte_count;
    return c;
}

u32 string_calculate_char_count(String str)
{
    u32 count = 0;
    u64 cursor = 0;
    while (cursor < str.size) {
        string_get_codepoint(str, &cursor);
        count++;
    }
    return count;
}

String escape_string_from_raw_string(Arena* arena, String raw)
{
    SCRATCH(arena);
    
    StringBuilder builder = string_builder_make(scratch.arena);
    
    u64 cursor = 0;
    while (cursor < raw.size) {
        u32 codepoint = string_get_codepoint(raw, &cursor);
        if (codepoint == '\n') append(&builder, "\\n");
        else if (codepoint == '\r') append(&builder, "\\r");
        else if (codepoint == '\t') append(&builder, "\\t");
        else if (codepoint == '\\') append(&builder, "\\\\");
        else if (codepoint == '\"') append(&builder, "\\\"");
        else append_codepoint(&builder, codepoint);
    }
    
    return string_from_builder(arena, &builder);
}

b32 codepoint_is_separator(u32 codepoint) {
    if (codepoint == ' ') return true;
    if (codepoint == '\t') return true;
    if (codepoint == '\r') return true;
    return false;
}

b32 codepoint_is_number(u32 codepoint) {
    return codepoint >= '0' && codepoint <= '9';
}

b32 codepoint_is_text(u32 codepoint) {
    if (codepoint >= 'a' && codepoint <= 'z') return true;
    if (codepoint >= 'A' && codepoint <= 'Z') return true;
    return false;
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

void append_codepoint(StringBuilder* builder, u32 codepoint) {
    SCRATCH(builder->arena);
    append(builder, string_from_codepoint(scratch.arena, codepoint));
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
    if (op == BinaryOperator_Addition) return "+";
    if (op == BinaryOperator_Substraction) return "-";
    if (op == BinaryOperator_Multiplication) return "*";
    if (op == BinaryOperator_Division) return "/";
    if (op == BinaryOperator_Modulo) return "%";
    if (op == BinaryOperator_LogicalNot) return "!";
    if (op == BinaryOperator_LogicalOr) return "||";
    if (op == BinaryOperator_LogicalAnd) return "&&";
    if (op == BinaryOperator_Equals) return "==";
    if (op == BinaryOperator_NotEquals) return "!=";
    if (op == BinaryOperator_LessThan) return "<";
    if (op == BinaryOperator_LessEqualsThan) return "<=";
    if (op == BinaryOperator_GreaterThan) return ">";
    if (op == BinaryOperator_GreaterEqualsThan) return ">=";
    assert(0);
    return "?";
}

b32 binary_operator_is_arithmetic(BinaryOperator op) {
    if (op == BinaryOperator_Addition) return true;
    if (op == BinaryOperator_Substraction) return true;
    if (op == BinaryOperator_Multiplication) return true;
    if (op == BinaryOperator_Division) return true;
    if (op == BinaryOperator_Modulo) return true;
    return false;
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
    TokenKind k = token.kind;
    
    if (k == TokenKind_Separator) return "separator";
    if (k == TokenKind_Identifier) return string_format(arena, "identifier: %S", token.value);
    if (k == TokenKind_IfKeyword) return "if";
    if (k == TokenKind_ElseKeyword) return "else";
    if (k == TokenKind_WhileKeyword) return "while";
    if (k == TokenKind_ForKeyword) return "for";
    if (k == TokenKind_EnumKeyword) return "enum";
    if (k == TokenKind_IntLiteral) return string_format(arena, "Int Literal: %S", token.value);
    if (k == TokenKind_BoolLiteral) { return string_format(arena, "Bool Literal: %S", token.value); }
    if (k == TokenKind_StringLiteral) { return string_format(arena, "String Literal: %S", token.value); }
    if (k == TokenKind_Comment) { return string_format(arena, "Comment: %S", token.value); }
    if (k == TokenKind_Comma) return ",";
    if (k == TokenKind_Dot) return ".";
    if (k == TokenKind_Colon) return ":";
    if (k == TokenKind_OpenBrace) return "{";
    if (k == TokenKind_CloseBrace) return "}";
    if (k == TokenKind_OpenBracket) return "[";
    if (k == TokenKind_CloseBracket) return "]";
    if (k == TokenKind_OpenParenthesis) return "(";
    if (k == TokenKind_CloseParenthesis) return ")";
    if (k == TokenKind_Assignment) {
        if (token.assignment_binary_operator == BinaryOperator_None) return "=";
        return string_format(arena, "%S=", string_from_binary_operator(token.assignment_binary_operator));
    }
    if (k == TokenKind_PlusSign) return "+";
    if (k == TokenKind_MinusSign) return "-";
    if (k == TokenKind_Asterisk) return "*";
    if (k == TokenKind_Slash) return "/";
    if (k == TokenKind_Modulo) return "%";
    if (k == TokenKind_Ampersand) return "&";
    if (k == TokenKind_Exclamation) return "!";
    if (k == TokenKind_LogicalOr) return "||";
    if (k == TokenKind_LogicalAnd) return "&&";
    if (k == TokenKind_CompEquals) return "==";
    if (k == TokenKind_CompNotEquals) return "!=";
    if (k == TokenKind_CompLess) return "<";
    if (k == TokenKind_CompLessEquals) return "<=";
    if (k == TokenKind_CompGreater) return ">";
    if (k == TokenKind_CompGreaterEquals) return ">=";
    if (k == TokenKind_OpenString) return "Open String";
    if (k == TokenKind_NextLine) return "Next Line";
    if (k == TokenKind_CloseString) return "Close String";
    if (k == TokenKind_NextSentence) return ";";
    if (k == TokenKind_Error) return "Error";
    if (k == TokenKind_None) return "None";
    
    return "?";
}

void print_tokens(Array<Token> tokens)
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
    if (kind == OpKind_ParameterList) return sizeof(OpNode_ParameterList);
    if (kind == OpKind_Indexing) return sizeof(OpNode_Indexing);
    if (kind == OpKind_IfStatement) return sizeof(OpNode_IfStatement);
    if (kind == OpKind_WhileStatement) return sizeof(OpNode_WhileStatement);
    if (kind == OpKind_ForStatement) return sizeof(OpNode_ForStatement);
    if (kind == OpKind_ForeachArrayStatement) return sizeof(OpNode_ForeachArrayStatement);
    if (kind == OpKind_ObjectDefinition) return sizeof(OpNode_ObjectDefinition);
    if (kind == OpKind_ObjectType) return sizeof(OpNode_ObjectType);
    if (kind == OpKind_FunctionCall) return sizeof(OpNode_FunctionCall);
    if (kind == OpKind_Return) return sizeof(OpNode_Return);
    if (kind == OpKind_Continue) return sizeof(OpNode);
    if (kind == OpKind_Break) return sizeof(OpNode);
    if (kind == OpKind_ArrayExpresion) return sizeof(OpNode_ArrayExpresion);
    if (kind == OpKind_Binary) return sizeof(OpNode_Binary);
    if (kind == OpKind_Sign) return sizeof(OpNode_Sign);
    if (kind == OpKind_Reference) return sizeof(OpNode_Reference);
    if (kind == OpKind_IntLiteral) return sizeof(OpNode_NumericLiteral);
    if (kind == OpKind_BoolLiteral) return sizeof(OpNode_NumericLiteral);
    if (kind == OpKind_StringLiteral) return sizeof(OpNode_StringLiteral);
    if (kind == OpKind_CodepointLiteral) return sizeof(OpNode_NumericLiteral);
    if (kind == OpKind_MemberValue) return sizeof(OpNode_MemberValue);
    if (kind == OpKind_EnumDefinition) return sizeof(OpNode_EnumDefinition);
    if (kind == OpKind_StructDefinition) return sizeof(OpNode_StructDefinition);
    if (kind == OpKind_ArgDefinition) return sizeof(OpNode_ArgDefinition);
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

per_thread_var Yov* yov;

void yov_initialize(b32 import_core)
{
    yov = (Yov*)os_allocate_heap(sizeof(Yov));
    
    os_setup_memory_info();
    
    yov->static_arena = arena_alloc(GB(32), 8);
    yov->temp_arena = arena_alloc(GB(32), 8);
    initialize_scratch_arenas();
    
    os_initialize();
    yov->timer_start = os_timer_get();
    
    yov->scripts = pooled_array_make<YovScript>(yov->static_arena, 8);
    yov->reports = pooled_array_make<Report>(yov->static_arena, 32);
    
    yov->caller_dir = os_get_working_path(yov->static_arena);
    
    types_initialize(import_core);
}

void yov_shutdown()
{
    if (yov == NULL) return;
    
#if DEV
    if (0)
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
            // TODO(Jose): print_tokens(tokens);
            print_info("\n\n");
        }
        if (0) {
            print_info(STR("// AST\n"));
            // TODO(Jose): log_ast(ast, 0);
            print_info("\n\n");
        }
    }
#endif
    
    if (yov->settings.wait_end) {
        os_console_wait();
    }
    
    shutdown_scratch_arenas();
    os_shutdown();
    
    arena_free(yov->temp_arena);
    arena_free(yov->static_arena);
    yov = NULL;
}

internal_fn Array<ScriptArg> generate_script_args(Arena* arena, Array<String> raw_args)
{
    SCRATCH(arena);
    Array<ScriptArg> args = array_make<ScriptArg>(arena, raw_args.count);
    
    foreach(i, raw_args.count)
    {
        String raw = raw_args[i];
        
        Array<String> split = string_split(scratch.arena, raw, "=");
        
        ScriptArg arg{};
        if (split.count == 1) {
            arg.name = split[0];
            arg.value = "";
        }
        else if (split.count == 2) {
            arg.name = split[0];
            arg.value = split[1];
        }
        else {
            report_error(NO_CODE, "Invalid arg '%S', expected format: name=value\n", raw);
            arg.name = STR("?");
            arg.value = STR("0");
        }
        
        args[i] = arg;
    }
    
    return args;
}

internal_fn String resolve_import_path(Arena* arena, String caller_script_dir, String path)
{
    SCRATCH(arena);
    path = path_resolve(scratch.arena, path);
    
    if (!os_path_is_absolute(path)) {
        path = path_append(scratch.arena, caller_script_dir, path);
    }
    
    return string_copy(arena, path);
}

#include "autogenerated/help.h"

#define YOV_ARG_ANALYZE STR("-analyze")
#define YOV_ARG_TRACE STR("-trace")
#define YOV_ARG_USER_ASSERT STR("-user_assert")
#define YOV_ARG_WAIT_END STR("-wait_end")
#define YOV_ARG_NO_USER STR("-no_user")

void yov_config_from_args()
{
    SCRATCH();
    Array<String> args = os_get_args(scratch.arena);
    
    String path = {};
    i32 script_args_start_index = args.count;
    
    foreach(i, args.count)
    {
        String arg = args[i];
        
        if (arg.size > 0 && arg[0] != '-') {
            path = arg;
            script_args_start_index = i + 1;
            break;
        }
        
        if (string_equals(arg, YOV_ARG_ANALYZE)) yov->settings.analyze_only = true;
        else if (string_equals(arg, YOV_ARG_TRACE)) yov->settings.trace = true;
        else if (string_equals(arg, YOV_ARG_USER_ASSERT)) yov->settings.user_assert = true;
        else if (string_equals(arg, YOV_ARG_WAIT_END)) yov->settings.wait_end = true;
        else if (string_equals(arg, YOV_ARG_NO_USER)) yov->settings.no_user = true;
        else if (string_equals(arg, "-help") || string_equals(arg, "-h")) {
            print_info("Yov Programming Language %S\n", YOV_VERSION);
            print_info("Location: %S\n\n", os_get_executable_path(scratch.arena));
            print_info(YOV_HELP_STR);
            yov->exit_requested = true;
            return;
        }
        else if (string_equals(arg, "-version") || string_equals(arg, "-v")) {
            print_info("Yov Programming Language %S\n", YOV_VERSION);
            yov->exit_requested = true;
            return;
        }
        else {
            report_error(NO_CODE, "Unknown Yov argument '%S'\n", arg);
        }
    }
    
    if (path.size == 0) {
        report_error(NO_CODE, "Script not specified");
        return;
    }
    
    Array<String> script_args_str = array_subarray(args, script_args_start_index, args.count - script_args_start_index);
    Array<ScriptArg> script_args = generate_script_args(scratch.arena, script_args_str);
    
    path = resolve_import_path(scratch.arena, os_get_working_path(scratch.arena), path);
    
    yov_config(path, script_args);
}

void yov_config(String path, Array<ScriptArg> args)
{
    yov->main_script_path = string_copy(yov->static_arena, path);
    yov->args = array_copy(yov->static_arena, args);
    foreach(i, yov->args.count) {
        yov->args[i].name = string_copy(yov->static_arena, args[i].name);
        yov->args[i].value = string_copy(yov->static_arena, args[i].value);
    }
}

void yov_set_exit_code(i64 exit_code)
{
    if (yov->exit_code_is_set) return;
    yov->exit_code_is_set = true;
    yov->exit_code = exit_code;
}

void yov_print_script_help()
{
    SCRATCH();
    StringBuilder builder = string_builder_make(scratch.arena);
    
    // Script description
    {
        ObjectDefinition* global = find_global("script_description");
        
        if (global != NULL) {
            Value value = global->ir.value;
            
            String description;
            if (ct_string_from_value(value, &description)) {
                append(&builder, description);
                append(&builder, "\n\n");
            }
        }
    }
    
    Array<String> headers = array_make<String>(scratch.arena, yov->arg_definitions.count);
    
    u32 longest_header = 0;
    for (auto it = pooled_array_make_iterator(&yov->arg_definitions); it.valid; ++it)
    {
        ArgDefinition* arg = it.value;
        
        b32 show_type = arg->vtype->ID != VTypeID_Bool && arg->vtype->ID > VTypeID_Void;
        
        String space = "    ";
        
        String header;
        if (show_type)
        {
            VariableType* type = arg->vtype;
            
            String type_str;
            if (type->kind == VariableKind_Enum) {
                type_str = "enum";
            }
            else {
                type_str = arg->vtype->name;
            }
            header = string_format(scratch.arena, "%S%S -> %S", space, arg->name, type_str);
        }
        else {
            header = string_format(scratch.arena, "%S%S", space, arg->name);
        }
        
        headers[it.index] = header;
        
        u32 char_count = string_calculate_char_count(header);
        longest_header = MAX(longest_header, char_count);
    }
    
    u32 chars_to_description = longest_header + 4;
    
    appendf(&builder, "Script Arguments:\n");
    for (auto it = pooled_array_make_iterator(&yov->arg_definitions); it.valid; ++it)
    {
        ArgDefinition* arg = it.value;
        String header = headers[it.index];
        
        append(&builder, header);
        
        if (arg->description.size != 0)
        {
            u32 char_count = string_calculate_char_count(header);
            for (u32 i = char_count; i < chars_to_description; ++i) {
                append(&builder, " ");
            }
            
            appendf(&builder, "%S", arg->description);
        }
        appendf(&builder, "\n");
    }
    
    String log = string_from_builder(scratch.arena, &builder);
    print_info(log);
}

void yov_compile(b32 require_args, b32 require_intrinsics)
{
    i32 main_script_id = yov_import_script(yov->main_script_path);
    
    if (main_script_id < 0) {
        yov->exit_requested = true;
        return;
    }
    
    ir_generate(require_args, require_intrinsics);
}


i32 yov_import_script(String path)
{
    SCRATCH();
    assert(os_path_is_absolute(path));
    
    RawBuffer raw_file;
    if (os_read_entire_file(yov->static_arena, path, &raw_file).failed) {
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
    
    script->tokens = lexer_generate_tokens(yov->static_arena, script->text, true, code_location_start_script(script_id));
    script->ast = generate_ast(script->tokens);
    
    Array<OpNode_Import*> imports = get_imports(scratch.arena, script->ast);
    
    foreach(i, imports.count)
    {
        OpNode_Import* import = imports[i];
        
        String import_path = resolve_import_path(scratch.arena, script->dir, import->path);
        
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

ScriptArg* yov_find_arg(String name) {
    foreach(i, yov->args.count) {
        ScriptArg* arg = &yov->args[i];
        if (string_equals(arg->name, name)) return arg;
    }
    return NULL;
}

String yov_get_inherited_args(Arena* arena)
{
    SCRATCH(arena);
    StringBuilder builder = string_builder_make(scratch.arena);
    
    if (yov->settings.analyze_only) appendf(&builder, "%S ", YOV_ARG_ANALYZE);
    if (yov->settings.user_assert) appendf(&builder, "%S ", YOV_ARG_USER_ASSERT);
    if (yov->settings.no_user) appendf(&builder, "%S ", YOV_ARG_NO_USER);
    
    return string_from_builder(arena, &builder);
}

b32 yov_ask_yesno(String title, String message)
{
    if (yov->settings.no_user) return true;
    return os_ask_yesno(title, message);
}

void report_error_ex(CodeLocation code, String text, ...)
{
    SCRATCH();
    
    va_list args;
	va_start(args, text);
    String formatted_text = string_format_with_args(scratch.arena, text, args);
    va_end(args);
    
    String line_sample = yov_get_line_sample(scratch.arena, code);
    formatted_text = string_replace(scratch.arena, formatted_text, "{line}", line_sample);
    
    Report report;
    report.text = string_copy(yov->static_arena, formatted_text);
    report.code = code;
    array_add(&yov->reports, report);
    
    yov->exit_requested = true;
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

String string_from_report(Arena* arena, Report report)
{
    YovScript* script = yov_get_script(report.code.script_id);
    if (script == NULL) return string_format(arena, "%S", report.text);
    else return string_format(arena, "%S(%u): %S", script->path, (u32)report.code.line, report.text);
}

void print_report(Report report)
{
    SCRATCH();
    String str = string_from_report(scratch.arena, report);
    print(Severity_Error, "%S\n", str);
}

