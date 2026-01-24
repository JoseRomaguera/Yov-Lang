#include "inc.h"

void AssertionFailed(const char* text, const char* file, U32 line)
{
    OsPrint(PrintLevel_ErrorReport, text);
    *((U8*)0) = 0;
}

//- DATE 

Date DateMake(U32 year, U32 month, U32 day, U32 hour, U32 minute, U32 second, U32 milliseconds) {
	Date date;
	date.year = year;
	date.month = month;
	date.day = day;
	date.hour = hour;
	date.minute = minute;
	date.second = second;
	date.millisecond = milliseconds;
	return date;
}

B32 DateEquals(Date d0, Date d1) {
	return d0.year == d1.year &&
		d0.month == d1.month &&
		d0.day == d1.day &&
		d0.hour == d1.hour &&
		d0.minute == d1.minute &&
		d0.second == d1.second &&
		d0.millisecond == d1.millisecond;
}

B32 DateLessThan(Date d0, Date d1) {
	if (d0.year != d1.year) return d0.year < d1.year;
	if (d0.month != d1.month) return d0.month < d1.month;
	if (d0.day != d1.day) return d0.day < d1.day;
	if (d0.hour != d1.hour) return d0.hour < d1.hour;
	if (d0.minute != d1.minute) return d0.minute < d1.minute;
	if (d0.second != d1.second) return d0.second < d1.second;
	if (d0.millisecond != d1.millisecond) return d0.millisecond < d1.millisecond;
	return false;
}

//- ARENA 

Arena* ArenaAlloc(U64 capacity, U32 alignment)
{
    alignment = Max(alignment, 1);
    Arena* arena = (Arena*)OsHeapAllocate(sizeof(Arena));
    arena->reserved_pages = pages_from_bytes(capacity);
    arena->memory = OsReserveVirtualMemory(arena->reserved_pages, false);
    arena->alignment = alignment;
    return arena;
}

void ArenaFree(Arena* arena) {
    if (arena == NULL) return;
    
#if DEV_ASAN
    ArenaProtectAndReset(arena);
    return;
#endif
    
    OsReleaseVirtualMemory(arena->memory);
    OsHeapFree(arena);
}

void* ArenaPush(Arena* arena, U64 size)
{
    mutex_lock_hot_guard(&arena->mutex);
    U64 position = arena->memory_position;
    position = u64_divide_high(position, arena->alignment) * arena->alignment;
    
    U64 page_size = system_info.page_size;
    U64 commited_size = (U64)arena->commited_pages * page_size;
    U64 reserved_size = (U64)arena->reserved_pages * page_size;
    
    U64 end_position = position + (U64)size;
    
    if (end_position > reserved_size) {
        return NULL; // TODO(Jose): Fatal error, out of memory
    }
    
    if (end_position > commited_size)
    {
        U32 commited_pages_needed = pages_from_bytes(end_position);
        U32 page_count = commited_pages_needed - arena->commited_pages;
        page_count = Max(page_count, pages_from_bytes(Kb(200)));
        OsCommitVirtualMemory(arena->memory, arena->commited_pages, page_count);
        arena->commited_pages += page_count;
    }
    
    arena->memory_position = end_position;
    return (U8*)arena->memory + position;
}

void ArenaPopTo(Arena* arena, U64 position)
{
#if DEV_ASAN
    if (position == 0) ArenaProtectAndReset(arena);
    return;
#endif
    
    Assert(arena->mutex == 0);
    mutex_lock_hot_guard(&arena->mutex);
    Assert(position <= arena->memory_position);
    U64 bytes_poped = arena->memory_position - position;
    arena->memory_position = position;
    MemoryZero((U8*)arena->memory + arena->memory_position, bytes_poped);
}

#if DEV
void ArenaProtectAndReset(Arena* arena)
{
    OsProtectVirtualMemory(arena->memory, arena->commited_pages);
    arena->memory = (U8*)arena->memory + arena->commited_pages * system_info.page_size;
    arena->memory_position = 0;
    arena->reserved_pages -= arena->commited_pages;
    arena->commited_pages = 0;
    
    if (arena->reserved_pages * system_info.page_size < Gb(1))
    {
        arena->reserved_pages = pages_from_bytes(Gb(32));
        arena->memory = OsReserveVirtualMemory(arena->reserved_pages, false);
    }
}
#endif

//- OS UTILS

void FileInfoSetPath(FileInfo* info, String path)
{
    info->path = path;
    info->folder = {};
    info->name = {};
    info->name_without_extension = {};
    info->extension = {};
    
    if (path.size == 0) return;
    
    I32 start_name = (I32)path.size - 1;
    
    while (start_name > 0) {
        if (path[start_name] == '/') {
            start_name++;
            break;
        }
        start_name--;
    }
    
    info->folder = StrSub(path, 0, start_name);
    info->name = StrSub(path, start_name, path.size - start_name);
    
    I32 start_extension = (I32)info->name.size - 1;
    for (; start_extension >= 0; --start_extension)
    {
        if (info->name[start_extension] == '.') {
            start_extension++;
            break;
        }
    }
    
    if (start_extension >= 0 && start_extension < info->name.size) info->extension = StrSub(info->name, start_extension, info->name.size - start_extension);
    
    if (info->extension.size) info->name_without_extension = StrSub(info->name, 0, info->name.size - info->extension.size - 1);
    else info->name_without_extension = info->name;
}

F64 TimerNow()
{
    U64 time = OsTimerGet() - system_info.timer_start;
    return time / (F64)system_info.timer_frequency;
}

//- MULTITHREADING 

internal_fn I32 lane_entry_point(void* data)
{
    LaneContext* lane = (LaneContext*)data;
    lane->group->fn(lane);
    return 0;
}

LaneGroup* LaneGroupStart(Arena* arena, LaneFn* fn, U32 lane_count)
{
    U32 max_lane_count = Max(system_info.logical_cores, 2) - 1;
    
    lane_count = Min(lane_count, max_lane_count);
    
    LaneGroup* group = ArenaPushStruct<LaneGroup>(arena);
    group->arena = arena;
    group->threads = array_make<OS_Thread>(arena, lane_count);
    group->fn = fn;
    
    foreach(i, group->threads.count)
    {
        LaneContext lane = {};
        lane.id = i;
        lane.count = group->threads.count;
        lane.group = group;
        
        RBuffer data = { (U8*)&lane, sizeof(lane) };
        group->threads[i] = OsThreadStart(lane_entry_point, data);
    }
    
    return group;
}

void LaneGroupWait(LaneGroup* group)
{
    foreach(i, group->threads.count) {
        OsThreadWait(group->threads[i], U32_MAX);
    }
}

void LaneBarrier(LaneContext* lane)
{
    volatile U32* counter = &lane->group->barrier_counter;
    
    CompilerReadBarrier();
    U32 barrier_index = *counter / lane->count;
    AtomicIncrementU32(counter);
    
    while ((*counter / lane->count) <= barrier_index) {
        OsThreadYield();
    }
}

B32 LaneNarrow(LaneContext* lane, U32 index)
{
    return index % lane->count == lane->id;
}

void LaneSyncPtr(LaneContext* lane, void** ptr, U32 index)
{
    if (LaneNarrow(lane, index)) {
        lane->group->sync.ptr = *ptr;
    }
    LaneBarrier(lane);
    *ptr = lane->group->sync.ptr;
    LaneBarrier(lane);
}

RangeU32 LaneDistributeUniformWork(LaneContext* lane, U32 count)
{
    I32 values_per_lane = count / lane->count;
    I32 leftover = count % lane->count;
    B32 lane_has_leftover = lane->id < leftover;
    
    I32 leftover_offset = lane_has_leftover ? lane->id : leftover;
    U32 start_index = values_per_lane * lane->id + leftover_offset;
    U32 end_index = start_index + values_per_lane + !!lane_has_leftover;
    
    return { start_index, end_index };
}

void LaneTaskStart(LaneContext* lane, U32 count)
{
    if (LaneNarrow(lane)) {
        lane->group->task_total = count;
        lane->group->task_next = 0;
        lane->group->task_finished = 0;
    }
    
    LaneBarrier(lane);
}

void LaneTaskAdd(LaneGroup* group, U32 count)
{
    while (1)
    {
        U32 last = group->task_total;
        U32 next = group->task_total + count;
        B32 success = AtomicExchangeU32(&group->task_total, last, next) == last;
        if (success) break;
    }
}

B32 LaneTaskFetch(LaneGroup* group, U32* index)
{
    *index = group->task_total;
    while (group->task_next < group->task_total) {
        U32 value = group->task_next;
        B32 success = value < group->task_total && AtomicExchangeU32(&group->task_next, value, value + 1) == value;
        if (success) {
            *index = value;
            return true;
        }
    }
    return false;
}

B32 LaneDynamicTaskIsBusy(LaneGroup* group)
{
    CompilerReadBarrier();
    return group->task_finished < group->task_total;
}

void LaneDynamicTaskFinish(LaneGroup* group)
{
    AtomicIncrementU32(&group->task_finished);
}

B32 mutex_try_lock(Mutex* mutex)
{
	if (mutex == NULL) return false;
	
	CompilerReadBarrier();
	if (*mutex != 0) return false;
    
	Mutex last = AtomicExchangeU32(mutex, 0, 1);
    
	CompilerReadBarrier();
	return last == 0 && *mutex == 1;
}

void mutex_lock(Mutex* mutex) {
	if (mutex == NULL) return;
	while (!mutex_try_lock(mutex)) OsThreadYield();
}

void mutex_lock_hot(Mutex* mutex) {
	if (mutex == NULL) return;
	while (!mutex_try_lock(mutex)) {}
}

B32 mutex_is_locked(Mutex* mutex)
{
    if (mutex == NULL) return false;
	
	CompilerReadBarrier();
    return (*mutex != 0);
}

void mutex_unlock(Mutex* mutex)
{
	if (mutex == NULL) return;
    AtomicExchangeU32(mutex, 1, 0);
}

//- MATH 

U64 u64_divide_high(U64 n0, U64 n1)
{
    U64 res = n0 / n1;
    if (n0 % n1 != 0) res++;
    return res;
}
U32 u32_divide_high(U32 n0, U32 n1)
{
    U32 res = n0 / n1;
    if (n0 % n1 != 0) res++;
    return res;
}

U32 pages_from_bytes(U64 bytes) {
    return (U32)u64_divide_high(bytes, system_info.page_size);
}

//- CSTRING 

U32 CStrSize(const char* str) {
    U32 size = 0;
    while (str[size]) size++;
    return size;
}

U32 CStrSet(char* dst, const char* src, U32 src_size, U32 buff_size)
{
	U32 size = Min(buff_size - 1u, src_size);
	MemoryCopy(dst, src, size);
	dst[size] = '\0';
	return (src_size > buff_size - 1u) ? (src_size - buff_size - 1u) : 0u;
}

U32 CStrCopy(char* dst, const char* src, U32 buff_size)
{
	U32 src_size = CStrSize(src);
	return CStrSet(dst, src, src_size, buff_size);
}

U32 CStrAppend(char* dst, const char* src, U32 buff_size)
{
	U32 src_size = CStrSize(src);
	U32 dst_size = CStrSize(dst);
    
	U32 new_size = src_size + dst_size;
    
	U32 overflows = (buff_size < (new_size + 1u)) ? (new_size + 1u) - buff_size : 0u;
    
	U32 append_size = (overflows > src_size) ? 0u : (src_size - overflows);
    
	MemoryCopy(dst + dst_size, src, append_size);
	new_size = dst_size + append_size;
	dst[new_size] = '\0';
	
	return overflows;
}

void CStrFromU64(char* dst, U64 value, U32 base)
{
	Assert(base >= 1);
    
	U32 digits = 0u;
    
	U64 aux = value;
	while (aux != 0) {
		aux /= base;
		++digits;
	}
    
	if (digits == 0u) {
		CStrCopy(dst, "0", 20);
		return;
	}
    
	I32 end = (I32)digits - 1;
    
	for (I32 i = end; i >= 0; --i) {
        
		U64 v = value % base;
        
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
                
                U32 count = ('Z' - 'A') + 1;
                
                U32 char_index = (U32)(v % (U64)count);
                U32 char_case = (U32)(v / (U64)count);
                
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
                    Assert(0);
                }
            }
            break;
            
		}
        
		value /= base;
	}
    
	dst[end + 1] = '\0';
}

void CStrFromI64(char* dst, I64 value, U32 base)
{
    if (value < 0)
    {
        dst[0] = '-';
        dst++;
        value = -value;
    }
    
    CStrFromU64(dst, (U64)value, base);
}

void CStrFromF64(char* dst, F64 value, U32 decimals)
{
	I64 decimal_mult = 0;
    
	if (decimals > 0)
	{
		U64 d = decimals;
        
		decimal_mult = 10;
		d--;
        
		while (d--)
		{
			decimal_mult *= 10;
		}
	}
    
	B8 minus = value < 0.0;
	value = Abs(value);
    
    I64 integer = (I64)value;
    I64 decimal = (I64)((value - (F64)integer) * (F64)decimal_mult);
    
	if (minus) CStrCopy(dst, "-", 50);
	else CStrCopy(dst, "", 50);
    
    char int_str[50];
	CStrFromU64(int_str, integer, 10);
    
    CStrAppend(dst, int_str, 50);
	CStrAppend(dst, ".", 50);
    
	char raw_decimal_string[100];
	CStrFromU64(raw_decimal_string, decimal, 10);
    
	U32 decimal_size = CStrSize(raw_decimal_string);
	while (decimal_size < decimals) {
		CStrAppend(dst, "0", 20);
		decimal_size++;
	}
    
	CStrAppend(dst, raw_decimal_string, 50);
}

//- STRING 

String StrMake(const char* cstr, U64 size) {
    String str;
    str.data = (char*)cstr;
    str.size = size;
    return str;
}

String StrFromCStr(const char* cstr) {
    return StrMake(cstr, CStrSize(cstr));
}

String StrFromRBuffer(RBuffer buffer) {
    String str;
    str.data = (char*)buffer.data;
    str.size = buffer.size;
    return str;
}

String StringAlloc(Arena* arena, U64 size)
{
    String str;
    str.data = (char*)ArenaPush(arena, size + 1);
    str.size = size;
    return str;
}

String StrCopy(Arena* arena, String src) {
    String dst;
    dst.size = src.size;
    dst.data = (char*)ArenaPush(arena, (U32)dst.size + 1);
    MemoryCopy(dst.data, src.data, src.size);
    return dst;
}

String StrSub(String str, U64 offset, U64 size) {
    Assert(offset + size <= str.size);
    String res{};
    res.data = str.data + offset;
    res.size = size;
    return res;
}

B32 StrEquals(String s0, String s1) {
    if (s0.size != s1.size) return false;
    foreach(i, s0.size) {
        if (s0[i] != s1[i]) return false;
    }
    return true;
}

B32 string_starts(String str, String with) {
    if (with.size > str.size) return false;
    return StrEquals(StrSub(str, 0, with.size), with);
}

B32 string_ends(String str, String with) {
    if (with.size > str.size) return false;
    return StrEquals(StrSub(str, str.size - with.size, with.size), with);
}

B32 u32_from_string(U32* dst, String str)
{
	U32 digits = (U32)str.size;
	*dst = 0u;
    
	if (digits == 0) return false;
    
	U32 mul = 10;
	foreach(i, digits - 1)
        mul *= 10;
    
	foreach(i, digits) {
        
		mul /= 10;
		
		char c = str[i];
		I32 v = c - '0';
        if (v < 0 || v > 9) return false;
        
		v *= mul;
		*dst += v;
	}
    
	return true;
}

B32 u32_from_char(U32* dst, char c)
{
    *dst = 0;
	I32 v = c - '0';
    if (v < 0 || v > 9) return false;
    *dst = v;
	return true;
}

B32 i64_from_string(String str, I64* out)
{
    B32 negative = false;
    if (str.size >= 2 && str[0] == '-') {
        str.data++;
        str.size--;
        negative = true;
    }
    
    U32 digits = (U32)str.size;
	*out = 0;
    
	if (digits == 0) return false;
    
	U64 mul = 10;
	foreach(i, digits - 1) mul *= 10;
    
	foreach(i, digits) 
    {
		mul /= 10;
		
		char c = str[i];
		I64 v = c - '0';
        if (v < 0 || v > 9) return false;
        
		v *= mul;
		*out += v;
	}
    
    if (negative) *out = -(*out);
    
	return true;
}

B32 i32_from_string(String str, I32* out)
{
    U32 digits = (U32)str.size;
	*out = 0;
    
	if (digits == 0) return false;
    
	U32 mul = 10;
	foreach(i, digits - 1) mul *= 10;
    
	foreach(i, digits) 
    {
		mul /= 10;
		
		char c = str[i];
		I32 v = c - '0';
        if (v < 0 || v > 9) return false;
        
		v *= mul;
		*out += v;
	}
    
	return true;
}

String string_from_codepoint(Arena* arena, U32 c)
{
    U32 byte_count;
    
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
    res.data = (char*)ArenaPush(arena, byte_count + 1);
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

String StringFromMemory(U64 bytes)
{
    Arena* arena = context.arena;
    
    F64 kb = bytes / 1024.0;
    
    if (kb < 1.0) {
        return StrFormat(arena, "%l b", bytes);
    }
    
    F64 mb = kb / 1024.0;
    
    if (mb < 1.0) {
        return StrFormat(arena, "%.2f KB", kb);
    }
    
    F64 gb = mb / 1024.0;
    
    if (gb < 1.0) {
        return StrFormat(arena, "%.2f MB", mb);
    }
    
    return StrFormat(arena, "%.2f Gb", gb);
}

String StringFromEllapsedTime(F64 seconds)
{
    Arena* arena = context.arena;
    
    F64 ns = seconds * 1000000000;
    if (ns < 1000.0) return StrFormat(arena, "%.2fns", ns);
    F64 us = seconds * 1000000;
    if (us < 1000.0) return StrFormat(arena, "%.2fus", us);
    F64 ms = seconds * 1000;
    if (ms < 1000.0) return StrFormat(arena, "%.2fms", ms);
    return StrFormat(arena, "%.2fs", seconds);
}

String string_join(Arena* arena, LinkedList<String> ll)
{
    U64 size = 0;
    for (LLNode* node = ll.root; node != NULL; node = node->next) {
        String* str = (String*)(node + 1);
        size += str->size;
    }
    
    char* data = (char*)ArenaPush(arena, size + 1);
    char* it = data;
    
    for (LLNode* node = ll.root; node != NULL; node = node->next) {
        String* str = (String*)(node + 1);
        MemoryCopy(it, str->data, str->size);
        it += str->size;
    }
    
    return StrMake(data, size);
}

Array<String> string_split(Arena* arena, String str, String separator)
{
    LinkedList<String> ll = ll_make<String>(context.arena);
    
    U64 next_offset = 0;
    
    U64 cursor = 0;
    while (cursor < str.size)
    {
        String sub = StrSub(str, cursor, Min(separator.size, str.size - cursor));
        
        if (StrEquals(sub, separator)) {
            
            String next = StrSub(str, next_offset, cursor - next_offset);
            ll_push(&ll, next);
            
            cursor += separator.size;
            next_offset = cursor;
        }
        else cursor++;
    }
    
    if (next_offset < cursor) {
        String next = StrSub(str, next_offset, cursor - next_offset);
        ll_push(&ll, next);
    }
    
    return array_from_ll(arena, ll);
}

String StrReplace(Arena* arena, String str, String old_str, String new_str)
{
    Assert(old_str.size > 0);
    
    if (str.size < old_str.size) return str;
    
    LinkedList<String> ll = ll_make<String>(context.arena);
    
    U64 next_offset = 0;
    
    U64 cursor = 0;
    while (cursor <= str.size - old_str.size)
    {
        String sub = StrSub(str, cursor, old_str.size);
        if (StrEquals(sub, old_str)) {
            
            String next = StrSub(str, next_offset, cursor - next_offset);
            ll_push(&ll, next);
            ll_push(&ll, new_str);
            
            cursor += old_str.size;
            next_offset = cursor;
        }
        else cursor++;
    }
    
    if (next_offset == 0) return str;
    
    if (next_offset < str.size) {
        String next = StrSub(str, next_offset, str.size - next_offset);
        ll_push(&ll, next);
    }
    
    return string_join(arena, ll);
}

String string_format_with_args(Arena* arena, String string, va_list args)
{
    StringBuilder builder = string_builder_make(context.arena);
    
    const U32 default_number_of_decimals = 4;
    
    B8 type_mode = false;
    U32 number_of_decimals = default_number_of_decimals;
    
    B8 invalid_format = false;
    
    U32 cursor = 0;
    while (cursor < string.size && !invalid_format)
    {
        defer(cursor++);
        
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
                I32 n = va_arg(args, I32);
                append_i64(&builder, n);
                type_mode = false;
                continue;
            }
            if (c == 'l')
            {
                I64 n = va_arg(args, I64);
                append_i64(&builder, n);
                type_mode = false;
                continue;
            }
            if (c == '%')
            {
                append(&builder, StrMake(&c, 1));
                type_mode = false;
                continue;
            }
            if (c == 'u')
            {
                U32 n = va_arg(args, U32);
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
                append_f64(&builder, (F64)f, number_of_decimals);
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
        InvalidCodepath();
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

U32 string_get_codepoint(String str, U64* cursor_ptr)
{
    U64 cursor = *cursor_ptr;
    defer(*cursor_ptr = cursor);
    
    if (cursor >= str.size) return 0;
    
    const char* it = str.data + cursor;
    
    U32 c = 0;
    char b = *it;
    
	U32 byte_count;
    if ((b & 0xF0) == 0xF0) byte_count = 4;
    else if ((b & 0xE0) == 0xE0) byte_count = 3;
    else if ((b & 0xC0) == 0xC0) byte_count = 2;
    else byte_count = 1;
    
    if (cursor + byte_count > str.size) {
        cursor = str.size;
        return 0xFFFD;
    }
    
    if (byte_count == 1) {
        c = (U32)b;
    }
    else if (byte_count == 2) {
        c |= ((U32)(it[0] & 0b00011111)) << 6;
        c |= ((U32)(it[1] & 0b00111111)) << 0;
    }
    else if (byte_count == 3) {
        c |= ((U32)(it[0] & 0b00001111)) << 12;
        c |= ((U32)(it[1] & 0b00111111)) << 6;
        c |= ((U32)(it[2] & 0b00111111)) << 0;
    }
    else {
        c |= ((U32)(it[0] & 0b00000111)) << 18;
        c |= ((U32)(it[1] & 0b00111111)) << 12;
        c |= ((U32)(it[2] & 0b00111111)) << 6;
        c |= ((U32)(it[3] & 0b00111111)) << 0;
    }
    
    cursor += byte_count;
    return c;
}

U32 string_calculate_char_count(String str)
{
    U32 count = 0;
    U64 cursor = 0;
    while (cursor < str.size) {
        string_get_codepoint(str, &cursor);
        count++;
    }
    return count;
}

String escape_string_from_raw_string(Arena* arena, String raw)
{
    StringBuilder builder = string_builder_make(context.arena);
    
    U64 cursor = 0;
    while (cursor < raw.size) {
        U32 codepoint = string_get_codepoint(raw, &cursor);
        if (codepoint == '\n') append(&builder, "\\n");
        else if (codepoint == '\r') append(&builder, "\\r");
        else if (codepoint == '\t') append(&builder, "\\t");
        else if (codepoint == '\\') append(&builder, "\\\\");
        else if (codepoint == '\"') append(&builder, "\\\"");
        else append_codepoint(&builder, codepoint);
    }
    
    return string_from_builder(arena, &builder);
}

B32 codepoint_is_separator(U32 codepoint) {
    if (codepoint == ' ') return true;
    if (codepoint == '\t') return true;
    if (codepoint == '\r') return true;
    return false;
}

B32 codepoint_is_number(U32 codepoint) {
    return codepoint >= '0' && codepoint <= '9';
}

B32 codepoint_is_text(U32 codepoint) {
    if (codepoint >= 'a' && codepoint <= 'z') return true;
    if (codepoint >= 'A' && codepoint <= 'Z') return true;
    return false;
}

//- PATH 

Array<String> path_subdivide(Arena* arena, String path)
{
    PooledArray<String> list = pooled_array_make<String>(context.arena, 32);
    
    U64 last_element = 0;
    U64 cursor = 0;
    while (cursor < path.size)
    {
        if (path[cursor] == '/') {
            String element = StrSub(path, last_element, cursor - last_element);
            if (element.size > 0) array_add(&list, element);
            last_element = cursor + 1;
        }
        
        cursor++;
    }
    
    String element = StrSub(path, last_element, cursor - last_element);
    if (element.size > 0) array_add(&list, element);
    
    return array_from_pooled_array(arena, list);
}

String path_resolve(Arena* arena, String path)
{
    String res = path;
    res = StrReplace(context.arena, res, "\\", "/");
    
    Array<String> elements = path_subdivide(context.arena, res);
    
    {
        I32 remove_prev_element_count = 0;
        
        for (I32 i = (I32)elements.count - 1; i >= 0; --i) {
            if (elements[i] == "..") {
                array_erase(&elements, i);
                remove_prev_element_count++;
            }
            else if (elements[i] == "." || remove_prev_element_count) {
                array_erase(&elements, i);
                if (remove_prev_element_count) remove_prev_element_count--;
            }
        }
    }
    
    StringBuilder builder = string_builder_make(context.arena);
    
    foreach(i, elements.count) {
        String element = elements[i];
        append(&builder, element);
        if (i < elements.count - 1) append_char(&builder, '/');
    }
    
    res = string_from_builder(context.arena, &builder);
    if (os_path_is_directory(res)) res = StrFormat(context.arena, "%S/", res);
    
    return StrCopy(arena, res);
}

String path_append(Arena* arena, String str0, String str1)
{
    if (os_path_is_absolute(str1)) return StrCopy(arena, str0);
    if (str0.size == 0) return StrCopy(arena, str1);
    if (str1.size == 0) return StrCopy(arena, str0);
    
    if (str0[str0.size - 1] != '/') return StrFormat(arena, "%S/%S", str0, str1);
    return StrFormat(arena, "%S%S", str0, str1);
}

String path_get_last_element(String path)
{
    // TODO(Jose): Optimize
    Array<String> array = path_subdivide(context.arena, path);
    if (array.count == 0) return {};
    return array[array.count - 1];
}

//- STRING BUILDER

StringBuilder string_builder_make(Arena* arena) {
    StringBuilder builder{};
    builder.ll = ll_make<String>(arena);
    builder.arena = arena;
    builder.buffer_size = 128;
    builder.buffer = (char*)ArenaPush(arena, builder.buffer_size);
    return builder;
}

inline_fn void _string_builder_push_buffer(StringBuilder* builder) {
    if (builder->buffer_pos == 0) return;
    
    String str = StrMake(builder->buffer, builder->buffer_pos);
    str = StrCopy(builder->arena, str);
    ll_push(&builder->ll, str);
    
    builder->buffer_pos = 0;
}

void appendf_ex(StringBuilder* builder, String str, ...)
{
    va_list args;
	va_start(args, str);
    String result = string_format_with_args(context.arena, str, args);
    va_end(args);
    
    append(builder, result);
}

void append(StringBuilder* builder, String str)
{
    if (str.size == 0) return;
    
    if (str.size > builder->buffer_size) {
        _string_builder_push_buffer(builder);
        
        String str_node = StrCopy(builder->arena, str);
        ll_push(&builder->ll, str_node);
    }
    else {
        U64 bytes_left = builder->buffer_size - builder->buffer_pos;
        if (str.size > bytes_left) {
            _string_builder_push_buffer(builder);
        }
        
        MemoryCopy(builder->buffer + builder->buffer_pos, str.data, str.size);
        builder->buffer_pos += str.size;
    }
}

void append_codepoint(StringBuilder* builder, U32 codepoint) {
    append(builder, string_from_codepoint(context.arena, codepoint));
}

void append_i64(StringBuilder* builder, I64 v, U32 base)
{
    char cstr[100];
    CStrFromI64(cstr, v, base);
    append(builder, cstr);
}
void append_i32(StringBuilder* builder, I32 v, U32 base) { append_i64(builder, (I64)v, base); }

void append_u64(StringBuilder* builder, U64 v, U32 base)
{
    char cstr[100];
    CStrFromU64(cstr, v, base);
    append(builder, cstr);
}
void append_u32(StringBuilder* builder, U32 v, U32 base) { append_u64(builder, (U64)v, base); }

void append_f64(StringBuilder* builder, F64 v, U32 decimals)
{
    char cstr[100];
    CStrFromF64(cstr, v, decimals);
    append(builder, cstr);
}

void append_char(StringBuilder* builder, char c) {
    append(builder, StrMake(&c, 1));
}

String string_from_builder(Arena* arena, StringBuilder* builder)
{
    _string_builder_push_buffer(builder);
    return string_join(arena, builder->ll);
}

//- POOLED ARRAY 

inline_fn PooledArrayBlock* _pooled_array_create_block(Arena* arena, U32 block_capacity, U64 stride)
{
    PooledArrayBlock* block = (PooledArrayBlock*)ArenaPush(arena, sizeof(PooledArrayBlock) + block_capacity * stride);
    block->next = NULL;
    block->capacity = block_capacity;
    block->count = 0;
    
    void* data = (void*)(block + 1);
    MemoryZero(data, stride * block_capacity);
    
    return block;
}

PooledArrayR pooled_array_make(Arena* arena, U64 stride, U32 block_capacity)
{
    Assert(block_capacity > 0);
    
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
    Assert(array->root != NULL && array->tail != NULL && array->default_block_capacity != 0);
    
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
    
    U8* ptr = (U8*)(block + 1) + (block->count * array->stride);
    block->count++;
    array->count++;
    return ptr;
}

void array_erase(PooledArrayR* array, U32 index)
{
    Assert(index < array->count);
    
    PooledArrayBlock* block = array->root;
    
    while (index >= block->capacity)
    {
        index -= block->capacity;
        block = block->next;
        Assert(block != NULL);
    }
    
    array->count--;
    block->count--;
    
    U8* data = (U8*)(block + 1);
    
    for (U32 i = index; i < block->count; ++i)
    {
        U64 i0 = (i + 0) * array->stride;
        U64 i1 = (i + 1) * array->stride;
        MemoryCopy(data + i0, data + i1, array->stride);
    }
    MemoryZero(data + block->count * array->stride, array->stride);
    
    PooledArrayBlock* next_block = block->next;
    while (next_block != NULL && next_block->count > 0)
    {
        U8* next_data = (U8*)(next_block + 1);
        
        U64 last_index = block->count * array->stride;
        MemoryCopy(data + last_index, next_data, array->stride);
        
        block->count++;
        next_block->count--;
        
        for (U32 i = 0; i < next_block->count; ++i)
        {
            U64 i0 = (i + 0) * array->stride;
            U64 i1 = (i + 1) * array->stride;
            MemoryCopy(next_data + i0, next_data + i1, array->stride);
        }
        MemoryZero(next_data + next_block->count * array->stride, array->stride);
        
        block = next_block;
        next_block = next_block->next;
        data = (U8*)(block + 1);
    }
    
    if (array->count) {
        
        if (block->count == 0)
        {
            block = array->root;
            while (block->next->count != 0) block = block->next;
        }
        
        array->current = block;
        Assert(array->current->count > 0);
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

U32 array_calculate_index(PooledArrayR* array, void* ptr)
{
    U32 index_offset = 0;
    
    PooledArrayBlock* block = array->root;
    while (block != NULL)
    {
        U8* begin_data = (U8*)(block + 1);
        U8* end_data = begin_data + (block->count * array->stride);
        
        if (ptr >= begin_data && ptr < end_data)
        {
            U64 byte_index = (U8*)ptr - begin_data;
            return index_offset + (U32)(byte_index / array->stride);
        }
        
        index_offset += block->count;
        block = block->next;
    }
    return U32_MAX;
}

//- MISC 

ObjectDefinition ObjDefMake(String name, VType vtype, Location location, B32 is_constant, Value value) {
    ObjectDefinition d{};
    d.name = name;
    d.vtype = vtype;
    d.is_constant = is_constant;
    d.value = value;
    d.location = location;
    return d;
}

String StringFromDefinitionType(DefinitionType type)
{
    if (type == DefinitionType_Function) return "Function";
    if (type == DefinitionType_Struct) return "Struct";
    if (type == DefinitionType_Enum) return "Enum";
    if (type == DefinitionType_Arg) return "Arg";
    InvalidCodepath();
    return "?";
}

Location LocationMake(U64 start, U64 end, I32 script_id)
{
    // TODO(Jose): Check for out of range location
    
    Location location = {};
    location.range = { start, end };
    location.script_id = script_id;
    
#if DEV_LOCATION_INFO
    location.info = LocationInfoMake(script_id, start);
#endif
    
    return location;
}

B32 LocationIsValid(Location location) {
    return location.script_id >= 0;
}

LocationInfo LocationInfoMake(I32 script_id, U64 cursor)
{
    YovScript* script = GetScript(script_id);
    if (script == NULL) return {};
    
    cursor = Min(cursor, script->text.size);
    
    LocationInfo info = {};
    info.line = 1;
    info.column = 1;
    info.start_line_cursor = 0;
    
    U64 it = 0;
    while (it <= cursor) {
        U32 cp = string_get_codepoint(script->text, &it);
        if (cp == '\n') {
            info.line++;
            info.start_line_cursor = it;
            info.column = 1;
        }
        else info.column++;
    }
    
    return info;
}

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
    if (op == BinaryOperator_Is) return "is";
    Assert(0);
    return "?";
}

B32 binary_operator_is_arithmetic(BinaryOperator op) {
    if (op == BinaryOperator_Addition) return true;
    if (op == BinaryOperator_Substraction) return true;
    if (op == BinaryOperator_Multiplication) return true;
    if (op == BinaryOperator_Division) return true;
    if (op == BinaryOperator_Modulo) return true;
    return false;
}

String string_from_tokens(Arena* arena, Array<Token> tokens)
{
    StringBuilder builder = string_builder_make(context.arena);
    
    foreach(i, tokens.count) {
        append(&builder, tokens[i].value);
    }
    
    String res = string_from_builder(context.arena, &builder);
    res = StrReplace(context.arena, res, "\n", "\\n");
    res = StrReplace(context.arena, res, "\r", "");
    res = StrReplace(context.arena, res, "\t", " ");
    
    return StrCopy(arena, res);
}

String debug_info_from_token(Arena* arena, Token token)
{
    TokenKind k = token.kind;
    
    if (k == TokenKind_Separator) return "separator";
    if (k == TokenKind_Identifier) return StrFormat(arena, "identifier: %S", token.value);
    if (k == TokenKind_IfKeyword) return "if";
    if (k == TokenKind_ElseKeyword) return "else";
    if (k == TokenKind_WhileKeyword) return "while";
    if (k == TokenKind_ForKeyword) return "for";
    if (k == TokenKind_EnumKeyword) return "enum";
    if (k == TokenKind_IntLiteral) return StrFormat(arena, "Int Literal: %S", token.value);
    if (k == TokenKind_BoolLiteral) { return StrFormat(arena, "Bool Literal: %S", token.value); }
    if (k == TokenKind_StringLiteral) { return StrFormat(arena, "String Literal: %S", token.value); }
    if (k == TokenKind_Comment) { return StrFormat(arena, "Comment: %S", token.value); }
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
        return StrFormat(arena, "%S=", string_from_binary_operator(token.assignment_binary_operator));
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

BinaryOperator binary_operator_from_token(TokenKind token)
{
    if (token == TokenKind_PlusSign) return BinaryOperator_Addition;
    if (token == TokenKind_MinusSign) return BinaryOperator_Substraction;
    if (token == TokenKind_Asterisk) return BinaryOperator_Multiplication;
    if (token == TokenKind_Slash) return BinaryOperator_Division;
    if (token == TokenKind_Modulo) return BinaryOperator_Modulo;
    if (token == TokenKind_Ampersand) return BinaryOperator_None;
    if (token == TokenKind_Exclamation) return BinaryOperator_LogicalNot;
    if (token == TokenKind_LogicalOr) return BinaryOperator_LogicalOr;
    if (token == TokenKind_LogicalAnd) return BinaryOperator_LogicalAnd;
    if (token == TokenKind_CompEquals) return BinaryOperator_Equals;
    if (token == TokenKind_CompNotEquals) return BinaryOperator_NotEquals;
    if (token == TokenKind_CompLess) return BinaryOperator_LessThan;
    if (token == TokenKind_CompLessEquals) return BinaryOperator_LessEqualsThan;
    if (token == TokenKind_CompGreater) return BinaryOperator_GreaterThan;
    if (token == TokenKind_CompGreaterEquals) return BinaryOperator_GreaterEqualsThan;
    if (token == TokenKind_IsKeyword) return BinaryOperator_Is;
    return BinaryOperator_None;
};

B32 token_is_sign_or_binary_op(TokenKind token)
{
    TokenKind tokens[] = {
        TokenKind_PlusSign,
        TokenKind_MinusSign,
        TokenKind_Asterisk,
        TokenKind_Slash,
        TokenKind_Modulo,
        TokenKind_Ampersand,
        TokenKind_Exclamation,
        
        TokenKind_LogicalOr,
        TokenKind_LogicalAnd,
        
        TokenKind_CompEquals,
        TokenKind_CompNotEquals,
        TokenKind_CompLess,
        TokenKind_CompLessEquals,
        TokenKind_CompGreater,
        TokenKind_CompGreaterEquals,
        
        TokenKind_IsKeyword,
    };
    
    foreach(i, countof(tokens)) {
        if (tokens[i] == token) return true;
    }
    return false;
}

B32 TokenIsFlowModifier(TokenKind token) {
    if (token == TokenKind_IfKeyword) return true;
    if (token == TokenKind_ElseKeyword) return true;
    if (token == TokenKind_WhileKeyword) return true;
    if (token == TokenKind_ForKeyword) return true;
    return false;
}

TokenKind TokenKindFromOpenScope(TokenKind open_token)
{
    if (open_token == TokenKind_OpenParenthesis) return TokenKind_CloseParenthesis;
    if (open_token == TokenKind_OpenBracket) return TokenKind_CloseBracket;
    if (open_token == TokenKind_OpenBrace) return TokenKind_CloseBrace;
    
    InvalidCodepath();
    return TokenKind_Error;
}

Location LocationFromTokens(Array<Token> tokens)
{
    if (tokens.count == 0) return NO_CODE;
    
#if DEV
    foreach(i, tokens.count - 1)
    {
        Token t0 = tokens[i + 0];
        Token t1 = tokens[i + 1];
        Assert(t0.cursor + t0.skip_size == t1.cursor);
        Assert(t0.location.script_id == t1.location.script_id);
    }
#endif
    
    Token first_token = tokens[0];
    Token last_token = tokens[tokens.count - 1];
    
    I32 script_id = first_token.location.script_id;
    return LocationMake(first_token.cursor, last_token.cursor + last_token.skip_size, script_id);
}

//- REPORT 

void PrintEx(PrintLevel level, String str, ...)
{
    va_list args;
	va_start(args, str);
    String result = string_format_with_args(context.arena, str, args);
    va_end(args);
    OsPrint(level, result);
}

void LogInternal(String tag, String str, ...)
{
    va_list args;
    va_start(args, str);
    String result = string_format_with_args(context.arena, str, args);
    va_end(args);
    
    StringBuilder builder = string_builder_make(context.arena);
    append(&builder, "[");
    append(&builder, tag);
    append(&builder, "] ");
    append(&builder, result);
    append(&builder, "\n");
    
    OsPrint(PrintLevel_DevLog, string_from_builder(context.arena, &builder));
}

YovSystemInfo system_info; 
Yov* yov;
per_thread_var YovThreadContext context;

void yov_initialize_thread()
{
    context.arena = ArenaAlloc(Gb(16), 8);
}

void yov_shutdown_thread()
{
    ArenaFree(context.arena);
}

void yov_initialize()
{
    Arena* arena = ArenaAlloc(Gb(32), 8);
    
    yov = ArenaPushStruct<Yov>(arena);
    
    yov->arena = arena;
    
    OsInitialize();
    
    yov->scripts = pooled_array_make<YovScript>(yov->arena, 8);
    yov->reports = pooled_array_make<Report>(yov->arena, 32);
    yov->caller_dir = os_get_working_path(yov->arena);
    
    Interpreter* inter = ArenaPushStruct<Interpreter>(yov->arena);
    yov->inter = inter;
    
    inter->global_scope = ArenaPushStruct<Scope>(yov->arena);
    inter->current_scope = inter->global_scope;
}

void yov_shutdown()
{
    if (yov == NULL) return;
    
    if (yov->settings.wait_end) {
        os_console_wait();
    }
    
    OsShutdown();
    
    ArenaFree(yov->arena);
    yov = NULL;
}

internal_fn Array<ScriptArg> generate_script_args(Arena* arena, Array<String> raw_args)
{
    Array<ScriptArg> args = array_make<ScriptArg>(arena, raw_args.count);
    
    foreach(i, raw_args.count)
    {
        String raw = raw_args[i];
        
        Array<String> split = string_split(context.arena, raw, "=");
        
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
            arg.name = "?";
            arg.value = "0";
        }
        
        args[i] = arg;
    }
    
    return args;
}

#include "autogenerated/help.h"

#define YOV_ARG_ANALYZE STR("-analyze")
#define YOV_ARG_TRACE STR("-trace")
#define YOV_ARG_USER_ASSERT STR("-user_assert")
#define YOV_ARG_WAIT_END STR("-wait_end")
#define YOV_ARG_NO_USER STR("-no_user")

void yov_config_from_args()
{
    Array<String> args = os_get_args(context.arena);
    
    String path = {};
    I32 script_args_start_index = args.count;
    
    YovSettings settings = {};
    
    foreach(i, args.count)
    {
        String arg = args[i];
        
        if (arg.size > 0 && arg[0] != '-') {
            path = arg;
            script_args_start_index = i + 1;
            break;
        }
        
        if (StrEquals(arg, YOV_ARG_ANALYZE)) settings.analyze_only = true;
        else if (StrEquals(arg, YOV_ARG_TRACE)) settings.trace = true;
        else if (StrEquals(arg, YOV_ARG_USER_ASSERT)) settings.user_assert = true;
        else if (StrEquals(arg, YOV_ARG_WAIT_END)) settings.wait_end = true;
        else if (StrEquals(arg, YOV_ARG_NO_USER)) settings.no_user = true;
        else if (StrEquals(arg, "-help") || StrEquals(arg, "-h")) {
            PrintF("Yov Programming Language %S\n", YOV_VERSION);
            PrintF("Location: %S\n\n", os_get_executable_path(context.arena));
            PrintF(YOV_HELP_STR);
            yov->exit_requested = true;
            return;
        }
        else if (StrEquals(arg, "-version") || StrEquals(arg, "-v")) {
            PrintF("Yov Programming Language %S\n", YOV_VERSION);
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
    Array<ScriptArg> script_args = generate_script_args(context.arena, script_args_str);
    
    path = resolve_import_path(context.arena, os_get_working_path(context.arena), path);
    
    yov_config(path, settings, script_args);
}

void yov_config(String path, YovSettings settings, Array<ScriptArg> script_args)
{
    yov->settings = settings;
    yov->main_script_path = StrCopy(yov->arena, path);
    yov->script_args = array_copy(yov->arena, script_args);
    foreach(i, yov->script_args.count) {
        yov->script_args[i].name = StrCopy(yov->arena, script_args[i].name);
        yov->script_args[i].value = StrCopy(yov->arena, script_args[i].value);
    }
    
    ScriptArg* help_arg = yov_find_script_arg("-help");
    
    if (help_arg != NULL && !StrEquals(help_arg->value, "")) {
        report_arg_wrong_value(NO_CODE, help_arg->name, help_arg->value);
    }
}

String resolve_import_path(Arena* arena, String caller_script_dir, String path)
{
    path = path_resolve(context.arena, path);
    
    if (!os_path_is_absolute(path)) {
        path = path_append(context.arena, caller_script_dir, path);
    }
    
    return StrCopy(arena, path);
}

void yov_set_exit_code(I64 exit_code)
{
    if (yov->exit_code_is_set) return;
    yov->exit_code_is_set = true;
    yov->exit_code = exit_code;
}

void yov_print_script_help()
{
    StringBuilder builder = string_builder_make(context.arena);
    
    // Script description
    {
        Global* global = GlobalFromIdentifier("script_description");
        
        if (global != NULL) {
            InvalidCodepath();
#if 0 // TODO(Jose): 
            Value value = def->initialize_ir.value;
            Reference ref = ref_from_value(yov->inter->global_scope, value);
            String description = string_from_ref(context.arena, ref, true);
            
            append(&builder, description);
            append(&builder, "\n\n");
#endif
        }
    }
    
    Array<String> headers = array_make<String>(context.arena, yov->arg_count);
    
    U32 index = 0;
    U32 longest_header = 0;
    foreach(i, yov->definitions.count)
    {
        ArgDefinition* arg = &yov->definitions[i].arg;
        if (arg->type != DefinitionType_Arg) continue;
        
        B32 show_type = arg->vtype != VType_Bool && VTypeValid(arg->vtype);
        
        String space = "    ";
        
        String header;
        if (show_type)
        {
            VType type = arg->vtype;
            
            String type_str;
            if (type.kind == VKind_Enum) {
                type_str = "enum";
            }
            else {
                type_str = VTypeGetName(arg->vtype);
            }
            header = StrFormat(context.arena, "%S%S -> %S", space, arg->name, type_str);
        }
        else {
            header = StrFormat(context.arena, "%S%S", space, arg->name);
        }
        
        headers[index++] = header;
        
        U32 char_count = string_calculate_char_count(header);
        longest_header = Max(longest_header, char_count);
    }
    
    U32 chars_to_description = longest_header + 4;
    
    index = 0;
    appendf(&builder, "Script Arguments:\n");
    foreach(i, yov->definitions.count)
    {
        ArgDefinition* arg = &yov->definitions[i].arg;
        if (arg->type != DefinitionType_Arg) continue;
        
        String header = headers[index++];
        
        append(&builder, header);
        
        if (arg->description.size != 0)
        {
            U32 char_count = string_calculate_char_count(header);
            for (U32 i = char_count; i < chars_to_description; ++i) {
                append(&builder, " ");
            }
            
            appendf(&builder, "%S", arg->description);
        }
        appendf(&builder, "\n");
    }
    
    String log = string_from_builder(context.arena, &builder);
    PrintF(log);
}

YovScript* GetScript(I32 script_id)
{
    if (script_id < 0) return NULL;
    
    if (script_id >= yov->scripts.count) {
        Assert(0);
        return NULL;
    }
    
    return &yov->scripts[script_id];
}

String yov_get_line_sample(Arena* arena, Location location)
{
    if (location.script_id < 0) return {};
    
    LocationInfo info = LocationInfoMake(location.script_id, location.range.min);
    
    String text = GetScript(location.script_id)->text;
    U64 starting_cursor = info.start_line_cursor;
    
    U64 cursor = starting_cursor;
    while (cursor < text.size) {
        U64 next_cursor = cursor;
        U32 codepoint = string_get_codepoint(text, &next_cursor);
        if (codepoint == '\r') break;
        if (codepoint == '\n') break;
        if (cursor == starting_cursor && (codepoint == '\t' || codepoint == ' ')) starting_cursor = next_cursor;
        cursor = next_cursor;
    }
    
    String sample = StrSub(text, starting_cursor, cursor - starting_cursor);
    return StrFormat(arena, "'%S'", sample);
}

ScriptArg* yov_find_script_arg(String name) {
    foreach(i, yov->script_args.count) {
        ScriptArg* arg = &yov->script_args[i];
        if (StrEquals(arg->name, name)) return arg;
    }
    return NULL;
}

String yov_get_inherited_args(Arena* arena)
{
    StringBuilder builder = string_builder_make(context.arena);
    
    if (yov->settings.analyze_only) appendf(&builder, "%S ", YOV_ARG_ANALYZE);
    if (yov->settings.user_assert) appendf(&builder, "%S ", YOV_ARG_USER_ASSERT);
    if (yov->settings.no_user) appendf(&builder, "%S ", YOV_ARG_NO_USER);
    
    return string_from_builder(arena, &builder);
}

B32 yov_ask_yesno(String title, String message)
{
    if (yov->settings.no_user) return true;
    return os_ask_yesno(title, message);
}

void report_error_ex(Location location, String text, ...)
{
    va_list args;
    va_start(args, text);
    String formatted_text = string_format_with_args(context.arena, text, args);
    va_end(args);
    
    String line_sample = yov_get_line_sample(context.arena, location);
    formatted_text = StrReplace(context.arena, formatted_text, "{line}", line_sample);
    
    Report report;
    report.text = StrCopy(yov->arena, formatted_text);
    report.location = location;
    
    mutex_lock_hot(&yov->reports_mutex);
    array_add(&yov->reports, report);
    yov->exit_requested = true;
    mutex_unlock(&yov->reports_mutex);
}

internal_fn I32 report_compare(const void* _0, const void* _1)
{
    const Report* r0 = (const Report*)_0;
    const Report* r1 = (const Report*)_1;
    
    if (r0->location.range.min == r1->location.range.min) return 0;
    return (r0->location.range.min < r1->location.range.min) ? -1 : 1;
}

void yov_print_reports()
{
    Array<Report> reports = array_from_pooled_array(context.arena, yov->reports);
    array_sort(reports, report_compare);
    
    foreach(i, reports.count) {
        print_report(reports[i]);
    }
}

String string_from_report(Arena* arena, Report report)
{
    YovScript* script = GetScript(report.location.script_id);
    if (script == NULL) return StrFormat(arena, "%S", report.text);
    else {
        LocationInfo info = LocationInfoMake(report.location.script_id, report.location.range.min);
        return StrFormat(arena, "%S(%u): %S", script->path, (U32)info.line, report.text);
    }
}

void print_report(Report report)
{
    String str = string_from_report(context.arena, report);
    PrintEx(PrintLevel_ErrorReport, "%S\n", str);
}

