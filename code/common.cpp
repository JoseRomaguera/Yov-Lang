#include "common.h"

void AssertionFailed(const char* text, const char* file, U32 line)
{
    PrintEx(PrintLevel_ErrorReport, text);
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

#if DEV
#define PROFILE_ARENA(_arena) do { \
String name = (_arena)->debug_name; \
PROFILE_PLOT(name.data, (_arena)->memory_position); \
} while(0)
#else
#define PROFILE_ARENA(_arena) EmptyFunction()
#endif

Arena* ArenaAlloc(U64 capacity, U32 alignment, String debug_name)
{
    PROFILE_FUNCTION;
    alignment = Max(alignment, 1);
    Arena* arena = (Arena*)OsHeapAllocate(sizeof(Arena));
    arena->reserved_pages = PagesFromBytes(capacity);
    arena->memory = OsReserveVirtualMemory(arena->reserved_pages, false);
    arena->alignment = alignment;
#if DEV
    arena->debug_name = StrHeapCopy(debug_name);
#endif
    return arena;
}

void ArenaFree(Arena* arena) {
    PROFILE_FUNCTION;
    if (arena == NULL) return;
    
    arena->memory_position = 0;
    PROFILE_ARENA(arena);
    
#if DEV_ASAN
    ArenaProtectAndReset(arena);
    return;
#endif
    
    OsReleaseVirtualMemory(arena->memory);
    OsHeapFree(arena);
}

void* ArenaPush(Arena* arena, U64 size)
{
    PROFILE_FUNCTION;
    
    PROFILE_ARENA(arena);
    
    MutexLockGuard(&arena->mutex);
    U64 position = arena->memory_position;
    position = U64DivideHigh(position, arena->alignment) * arena->alignment;
    
    U64 page_size = system_info.page_size;
    U64 commited_size = (U64)arena->commited_pages * page_size;
    U64 reserved_size = (U64)arena->reserved_pages * page_size;
    
    U64 end_position = position + (U64)size;
    
    if (end_position > reserved_size) {
        return NULL; // TODO(Jose): Fatal error, out of memory
    }
    
    if (end_position > commited_size)
    {
        U32 commited_pages_needed = PagesFromBytes(end_position);
        U32 page_count = commited_pages_needed - arena->commited_pages;
        page_count = Max(page_count, PagesFromBytes(Kb(200)));
        OsCommitVirtualMemory(arena->memory, arena->commited_pages, page_count);
        arena->commited_pages += page_count;
    }
    
    arena->memory_position = end_position;
    
    PROFILE_ARENA(arena);
    return (U8*)arena->memory + position;
}

void ArenaPopTo(Arena* arena, U64 position)
{
    PROFILE_FUNCTION;
    PROFILE_ARENA(arena);
    
#if DEV_ASAN
    if (position == 0) ArenaProtectAndReset(arena);
    return;
#endif
    
    Assert(arena->mutex == 0);
    MutexLockGuard(&arena->mutex);
    Assert(position <= arena->memory_position);
    U64 bytes_poped = arena->memory_position - position;
    arena->memory_position = position;
    MemoryZero((U8*)arena->memory + arena->memory_position, bytes_poped);
    
    PROFILE_ARENA(arena);
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
        arena->reserved_pages = PagesFromBytes(Gb(32));
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
    PROFILE_FUNCTION;
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

LaneGroup* LaneGroupStart(Arena* arena, LaneFn* fn, void* user_data, U32 lane_count)
{
    PROFILE_FUNCTION;
    U32 max_lane_count = Max(system_info.logical_cores, 2) - 1;
    
    lane_count = Min(lane_count, max_lane_count);
    
    LaneGroup* group = ArenaPushStruct<LaneGroup>(arena);
    group->arena = arena;
    group->threads = ArrayAlloc<OS_Thread>(arena, lane_count);
    group->fn = fn;
    group->user_data = user_data;
    
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
    PROFILE_FUNCTION;
    foreach(i, group->threads.count) {
        OsThreadWait(group->threads[i], U32_MAX);
    }
}

void LaneBarrierEx(LaneContext* lane, U64 hash)
{
    PROFILE_FUNCTION;
    
    I32 my_sense = 1 - lane->local_sense;
    
    U32 arrived = AtomicIncrement32(&lane->group->threads_arrived);
    
    //PrintF("%u -> %u: s%i\n", arrived, (U32)hash, lane->local_sense);
    
    if (arrived == lane->count)
    {
        MemoryBarrierRelease();
        
        //PrintF("%u **\n", (U32)hash);
        
        AtomicStore32(&lane->group->threads_arrived, 0);
        AtomicStore32(&lane->group->global_sense, my_sense);
    }
    else
    {
        MemoryBarrierAcquire();
        
        U32 spins = 0;
        
        while (lane->group->global_sense != my_sense)
        {
            _mm_pause();
            spins++;
            if (spins > 100) {
                spins = 0;
                OsThreadYield();
            }
        }
        
        MemoryBarrierAcquire();
    }
    
    lane->local_sense = my_sense;
    
    //PrintF("%u <- %u: s%i\n", arrived, (U32)hash, lane->local_sense);
}

B32 LaneNarrow(LaneContext* lane, U32 index)
{
    PROFILE_FUNCTION;
    return index % lane->count == lane->id;
}

void LaneSyncPtr(LaneContext* lane, void** ptr, U32 index)
{
    PROFILE_FUNCTION;
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
    PROFILE_FUNCTION;
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
        B32 success = AtomicCompareExchange32_Full(&group->task_total, last, next) == last;
        if (success) break;
    }
}

B32 LaneTaskFetch(LaneGroup* group, U32* index)
{
    PROFILE_FUNCTION;
    *index = group->task_total;
    while (group->task_next < group->task_total) {
        U32 value = group->task_next;
        B32 success = value < group->task_total && AtomicCompareExchange32_Full(&group->task_next, value, value + 1) == value;
        if (success) {
            *index = value;
            return true;
        }
    }
    return false;
}

B32 LaneDynamicTaskIsBusy(LaneGroup* group)
{
    MemoryBarrierAcquire();
    return group->task_finished < group->task_total;
}

void LaneDynamicTaskFinish(LaneGroup* group)
{
    AtomicIncrement32(&group->task_finished);
}

B32 MutexTryLock(Mutex* mutex)
{
    return AtomicCompareExchange32_Acquire(mutex, 0, 1) == 0;
}

void MutexLock(Mutex* mutex)
{
    PROFILE_FUNCTION;
    
    U32 spins = 0;
    
    while (!MutexTryLock(mutex))
    {
        while (*mutex == 1) { 
            _mm_pause();
            
            spins++;
            if (spins > 100) {
                spins = 0;
                OsThreadYield();
            }
        }
    }
}

B32 MutexIsLocked(Mutex* mutex)
{
    MemoryBarrierAcquire();
    return *mutex != 0;
}

void MutexUnlock(Mutex* mutex)
{
    AtomicStore32(mutex, 0);
}

//- MATH 

U64 U64DivideHigh(U64 n0, U64 n1)
{
    U64 res = n0 / n1;
    if (n0 % n1 != 0) res++;
    return res;
}
U32 U32DivideHigh(U32 n0, U32 n1)
{
    U32 res = n0 / n1;
    if (n0 % n1 != 0) res++;
    return res;
}

U32 PagesFromBytes(U64 bytes) {
    return (U32)U64DivideHigh(bytes, system_info.page_size);
}

// RBUFFER

RBuffer RBufferAlloc(Arena* arena, U64 size) {
    RBuffer dst;
    dst.data = (U8*)ArenaPush(arena, size);
    dst.size = size;
    return dst;
}

RBuffer RBufferCopy(Arena* arena, RBuffer src) {
    RBuffer dst;
    dst.data = (U8*)ArenaPush(arena, src.size);
    dst.size = src.size;
    memcpy(dst.data, src.data, src.size);
    return dst;
}

RBuffer RBufferFromStr(String str) {
    return { (U8*)str.data, str.size };
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

String StrAlloc(Arena* arena, U64 size)
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

Array<String> StrArrayCopy(Arena* arena, Array<String> src)
{
    Array<String> dst = ArrayAlloc<String>(arena, src.count);
    foreach(i, dst.count) {
        dst[i] = StrCopy(arena, src[i]);
    }
    return dst;
}

String StrHeapCopy(String src)
{
    if (src.size == 0) return {};
    String dst;
    dst.size = src.size;
    dst.data = (char*)OsHeapAllocate(dst.size + 1);
    MemoryCopy(dst.data, src.data, dst.size);
    dst.data[dst.size] = '\0';
    return dst;
}

void StrHeapFree(String* str)
{
    if (str->data != NULL) {
        OsHeapFree(str->data);
    }
    *str = {};
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

B32 StrStarts(String str, String with) {
    if (with.size > str.size) return false;
    return StrEquals(StrSub(str, 0, with.size), with);
}

B32 StrEnds(String str, String with) {
    if (with.size > str.size) return false;
    return StrEquals(StrSub(str, str.size - with.size, with.size), with);
}

B32 U32FromString(U32* dst, String str, U32 base)
{
	U64 v; 
    B32 res = U64FromString(&v, str, base);
    if (v > U32_MAX) return false;
    *dst = (U32)v;
    return res;
}

B32 U64FromString(U64* dst, String str, U32 base)
{
    if (str.size == 0) return false;
    
    U64 value = 0;
    defer (*dst = value);
    
    if (base == 10)
    {
        for (U64 i = 0; i < str.size; ++i)
        {
            char c = str[i];
            if (c < '0' || c > '9') return false;
            
            U64 digit = (U64)(c - '0');
            
            if (value > (U64_MAX - digit) / 10) return false;
            
            value = value * 10 + digit;
        }
    }
    else if (base == 16)
    {
        for (U64 i = 0; i < str.size; ++i)
        {
            char c = str[i];
            U64 digit = 0;
            
            if (c >= '0' && c <= '9') {
                digit = (U64)(c - '0');
            }
            else if (c >= 'a' && c <= 'f') {
                digit = (U64)(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F') {
                digit = (U64)(c - 'A' + 10);
            }
            else {
                return false;
            }
            
            if (value > (U64_MAX >> 4)) return false;
            
            value = (value << 4) | digit; 
        }
    }
    else if (base == 2)
    {
        for (U64 i = 0; i < str.size; ++i)
        {
            char c = str[i];
            if (c < '0' || c > '1') return false;
            
            U64 digit = (U64)(c - '0');
            
            if (value > (U64_MAX >> 1)) return false;
            
            value = (value << 1) | digit;
        }
    }
    else if (base == 8)
    {
        for (U64 i = 0; i < str.size; ++i)
        {
            char c = str[i];
            if (c < '0' || c > '7') return false;
            
            U64 digit = (U64)(c - '0');
            
            if (value > (U64_MAX >> 3)) return false;
            
            value = (value << 3) | digit;
        }
    }
    else {
        return false; 
    }
    
    return true;
}

B32 F64FromString(F64* dst, String str)
{
    if (str.size == 0) return false;
    
    U64 cursor = 0;
    B32 negative = false;
    
    if (str[cursor] == '-') {
        negative = true;
        cursor++;
        if (str.size <= 1) return false;
    }
    
    F64 value = 0.0;
    F64 frac_mul = 0.1;
    B32 seen_dot = false;
    
    defer (*dst = value);
    
    while (cursor < str.size)
    {
        char c = str[cursor++];
        
        if (c == '.') {
            if (seen_dot) return false;
            seen_dot = true;
            continue;
        }
        
        if (c < '0' || c > '9') return false;
        
        F64 digit = (F64)(c - '0');
        
        if (!seen_dot) {
            value = value * 10.0 + digit;
        }
        else {
            value += digit * frac_mul;
            frac_mul *= 0.1;
        }
    }
    
    if (negative) value = -value;
    
    return true;
}

B32 U32FromChar(U32* dst, char c)
{
    *dst = 0;
    I32 v = c - '0';
    if (v < 0 || v > 9) return false;
    *dst = v;
    return true;
}

B32 I64FromString(I64* out, String str)
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

B32 I32FromString(I32* out, String str)
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

String StrFromU64(Arena* arena, U64 value, U32 base)
{
    char buff[32];
    CStrFromU64(buff, value, base);
    return StrCopy(arena,buff);
}

String StrFromI64(Arena* arena, I64 value, U32 base)
{
    char buff[32];
    CStrFromI64(buff, value, base);
    return StrCopy(arena,buff);
}

String StrFromF64(Arena* arena, F64 value, U32 decimals)
{
    char buff[32];
    CStrFromF64(buff, value, decimals);
    return StrCopy(arena,buff);
}

String StringFromCodepoint(Arena* arena, U32 c)
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

String StrJoin(Arena* arena, LinkedList<String> ll)
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

Array<String> StrSplit(Arena* arena, String str, String separator)
{
    LinkedList<String> ll = LLMake<String>(context.arena);
    
    U64 next_offset = 0;
    
    U64 cursor = 0;
    while (cursor < str.size)
    {
        String sub = StrSub(str, cursor, Min(separator.size, str.size - cursor));
        
        if (StrEquals(sub, separator)) {
            
            String next = StrSub(str, next_offset, cursor - next_offset);
            LLPush(&ll, next);
            
            cursor += separator.size;
            next_offset = cursor;
        }
        else cursor++;
    }
    
    if (next_offset < cursor) {
        String next = StrSub(str, next_offset, cursor - next_offset);
        LLPush(&ll, next);
    }
    
    return ArrayFromLL(arena, ll);
}

String StrReplace(Arena* arena, String str, String old_str, String new_str)
{
    Assert(old_str.size > 0);
    
    if (str.size < old_str.size) return str;
    
    LinkedList<String> ll = LLMake<String>(context.arena);
    
    U64 next_offset = 0;
    
    U64 cursor = 0;
    while (cursor <= str.size - old_str.size)
    {
        String sub = StrSub(str, cursor, old_str.size);
        if (StrEquals(sub, old_str)) {
            
            String next = StrSub(str, next_offset, cursor - next_offset);
            LLPush(&ll, next);
            LLPush(&ll, new_str);
            
            cursor += old_str.size;
            next_offset = cursor;
        }
        else cursor++;
    }
    
    if (next_offset == 0) return str;
    
    if (next_offset < str.size) {
        String next = StrSub(str, next_offset, str.size - next_offset);
        LLPush(&ll, next);
    }
    
    return StrJoin(arena, ll);
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
                if (!U32FromChar(&number_of_decimals, num)) invalid_format = true;
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

U32 StrGetCodepoint(String str, U64* cursor_ptr)
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

U32 StrCalculateCharCount(String str)
{
    U32 count = 0;
    U64 cursor = 0;
    while (cursor < str.size) {
        StrGetCodepoint(str, &cursor);
        count++;
    }
    return count;
}

String escape_string_from_raw_string(Arena* arena, String raw)
{
    StringBuilder builder = string_builder_make(context.arena);
    
    U64 cursor = 0;
    while (cursor < raw.size) {
        U32 codepoint = StrGetCodepoint(raw, &cursor);
        if (codepoint == '\n') append(&builder, "\\n");
        else if (codepoint == '\r') append(&builder, "\\r");
        else if (codepoint == '\t') append(&builder, "\\t");
        else if (codepoint == '\\') append(&builder, "\\\\");
        else if (codepoint == '\"') append(&builder, "\\\"");
        else append_codepoint(&builder, codepoint);
    }
    
    return string_from_builder(arena, &builder);
}

B32 CodepointIsSeparator(U32 codepoint) {
    if (codepoint == ' ') return true;
    if (codepoint == '\t') return true;
    if (codepoint == '\r') return true;
    return false;
}

B32 CodepointIsNumber(U32 codepoint) {
    return codepoint >= '0' && codepoint <= '9';
}

B32 CodepointIsText(U32 codepoint) {
    if (codepoint >= 'a' && codepoint <= 'z') return true;
    if (codepoint >= 'A' && codepoint <= 'Z') return true;
    return false;
}

//- PATH 

Array<String> PathSubdivide(Arena* arena, String path)
{
    BArray<String> list = BArrayMake<String>(context.arena, 32);
    
    U64 last_element = 0;
    U64 cursor = 0;
    while (cursor < path.size)
    {
        if (path[cursor] == '/') {
            String element = StrSub(path, last_element, cursor - last_element);
            if (element.size > 0) BArrayAdd(&list, element);
            last_element = cursor + 1;
        }
        
        cursor++;
    }
    
    String element = StrSub(path, last_element, cursor - last_element);
    if (element.size > 0) BArrayAdd(&list, element);
    
    return ArrayFromBArray(arena, list);
}

String PathResolve(Arena* arena, String path)
{
    String res = path;
    res = StrReplace(context.arena, res, "\\", "/");
    
    Array<String> elements = PathSubdivide(context.arena, res);
    
    {
        I32 starting_index = 0;

        while (starting_index < elements.count && elements[starting_index] == "..")
            starting_index++;

        I32 remove_prev_element_count = 0;
        
        for (I32 i = (I32)elements.count - 1; i >= starting_index; --i) {
            if (elements[i] == "..") {
                ArrayErase(&elements, i);
                remove_prev_element_count++;
            }
            else if (elements[i] == "." || remove_prev_element_count) {
                ArrayErase(&elements, i);
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
    if (OsPathIsDirectory(res)) res = StrFormat(context.arena, "%S/", res);
    
    return StrCopy(arena, res);
}

String PathResolveImport(Arena* arena, String caller_script_dir, String path)
{
    path = PathResolve(context.arena, path);
    
    if (!OsPathIsAbsolute(path)) {
        path = PathAppend(context.arena, caller_script_dir, path);
        path = PathResolve(context.arena, path);
    }
    
    return StrCopy(arena, path);
}

String PathAppend(Arena* arena, String str0, String str1)
{
    if (OsPathIsAbsolute(str1)) return StrCopy(arena, str0);
    if (str0.size == 0) return StrCopy(arena, str1);
    if (str1.size == 0) return StrCopy(arena, str0);
    
    if (str0[str0.size - 1] != '/') return StrFormat(arena, "%S/%S", str0, str1);
    return StrFormat(arena, "%S%S", str0, str1);
}

String PathGetLastElement(String path)
{
    // TODO(Jose): Optimize
    Array<String> array = PathSubdivide(context.arena, path);
    if (array.count == 0) return {};
    return array[array.count - 1];
}

String PathGetFolder(String path)
{
    if (path.size == 0) return {};
    I64 cursor = path.size - 1;
    while (cursor > 0 && path[cursor] != '/') cursor--;
    return StrSub(path, 0, cursor);
}

//- STRING BUILDER

StringBuilder string_builder_make(Arena* arena) {
    StringBuilder builder{};
    builder.ll = LLMake<String>(arena);
    builder.arena = arena;
    builder.buffer_size = 128;
    builder.buffer = (char*)ArenaPush(arena, builder.buffer_size);
    return builder;
}

inline_fn void _string_builder_push_buffer(StringBuilder* builder) {
    if (builder->buffer_pos == 0) return;
    
    String str = StrMake(builder->buffer, builder->buffer_pos);
    str = StrCopy(builder->arena, str);
    LLPush(&builder->ll, str);
    
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
        LLPush(&builder->ll, str_node);
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
    append(builder, StringFromCodepoint(context.arena, codepoint));
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
    return StrJoin(arena, builder->ll);
}

//- BUCKET BUFFER 

inline_fn BBufferBlock* BBufferAllocBlock(Arena* arena, U32 block_capacity, U64 stride)
{
    BBufferBlock* block = (BBufferBlock*)ArenaPush(arena, sizeof(BBufferBlock) + block_capacity * stride);
    block->next = NULL;
    block->capacity = block_capacity;
    block->count = 0;
    
    void* data = (void*)(block + 1);
    MemoryZero(data, stride * block_capacity);
    
    return block;
}

BBuffer BBufferMake(Arena* arena, U64 stride, U32 block_capacity)
{
    Assert(block_capacity > 0);
    
    BBuffer buff{};
    buff.default_block_capacity = block_capacity;
    buff.root = BBufferAllocBlock(arena, buff.default_block_capacity, stride);
    buff.tail = buff.root;
    buff.current = buff.root;
    buff.arena = arena;
    buff.stride = stride;
    return buff;
}

void BBufferReset(BBuffer* buffer)
{
    buffer->count = 0u;
    
    BBufferBlock* block = buffer->root;
    while (block) {
        block->count = 0;
        block = block->next;
    }
    
    buffer->current = buffer->root;
}

void* BBufferAdd(BBuffer* buffer)
{
    Assert(buffer->root != NULL && buffer->tail != NULL && buffer->default_block_capacity != 0);
    
    BBufferBlock* block = buffer->current;
    
    while (block->count >= block->capacity)
    {
        if (block == buffer->tail)
        {
            BBufferBlock* new_block = BBufferAllocBlock(buffer->arena, buffer->default_block_capacity, buffer->stride);
            block->next = new_block;
            block = new_block;
            buffer->tail = new_block;
        }
        else block = block->next;
    }
    
    buffer->current = block;
    
    U8* ptr = (U8*)(block + 1) + (block->count * buffer->stride);
    block->count++;
    buffer->count++;
    return ptr;
}

void BBufferErase(BBuffer* buffer, U32 index)
{
    Assert(index < buffer->count);
    
    BBufferBlock* block = buffer->root;
    
    while (index >= block->capacity)
    {
        index -= block->capacity;
        block = block->next;
        Assert(block != NULL);
    }
    
    buffer->count--;
    block->count--;
    
    U8* data = (U8*)(block + 1);
    
    for (U32 i = index; i < block->count; ++i)
    {
        U64 i0 = (i + 0) * buffer->stride;
        U64 i1 = (i + 1) * buffer->stride;
        MemoryCopy(data + i0, data + i1, buffer->stride);
    }
    MemoryZero(data + block->count * buffer->stride, buffer->stride);
    
    BBufferBlock* next_block = block->next;
    while (next_block != NULL && next_block->count > 0)
    {
        U8* next_data = (U8*)(next_block + 1);
        
        U64 last_index = block->count * buffer->stride;
        MemoryCopy(data + last_index, next_data, buffer->stride);
        
        block->count++;
        next_block->count--;
        
        for (U32 i = 0; i < next_block->count; ++i)
        {
            U64 i0 = (i + 0) * buffer->stride;
            U64 i1 = (i + 1) * buffer->stride;
            MemoryCopy(next_data + i0, next_data + i1, buffer->stride);
        }
        MemoryZero(next_data + next_block->count * buffer->stride, buffer->stride);
        
        block = next_block;
        next_block = next_block->next;
        data = (U8*)(block + 1);
    }
    
    if (buffer->count) {
        
        if (block->count == 0)
        {
            block = buffer->root;
            while (block->next->count != 0) block = block->next;
        }
        
        buffer->current = block;
        Assert(buffer->current->count > 0);
    }
    else {
        buffer->current = buffer->root;
    }
}

void BBufferPop(BBuffer* buffer)
{
    if (buffer->count >= 1) {
        BBufferErase(buffer, buffer->count - 1);
    }
}

U32 BBufferCalculateIndex(BBuffer* buffer, void* ptr)
{
    U32 index_offset = 0;
    
    BBufferBlock* block = buffer->root;
    while (block != NULL)
    {
        U8* begin_data = (U8*)(block + 1);
        U8* end_data = begin_data + (block->count * buffer->stride);
        
        if (ptr >= begin_data && ptr < end_data)
        {
            U64 byte_index = (U8*)ptr - begin_data;
            return index_offset + (U32)(byte_index / buffer->stride);
        }
        
        index_offset += block->count;
        block = block->next;
    }
    return U32_MAX;
}

// SERIALIZER

Serializer* SerializerAlloc(Arena* arena)
{
    Serializer* s = ArenaPushStruct<Serializer>(arena);
    s->ll = LLMake<RBuffer>(arena);
    s->arena = arena;
    s->buffer_size = Kb(16);
    s->buffer = (U8*)ArenaPush(arena, s->buffer_size);
    return s;
}

internal_fn void SerializerPushBuffer(Serializer* s) {
    if (s->buffer_pos == 0) return;
    
    RBuffer block = RBufferCopy(s->arena, { s->buffer, s->buffer_pos });
    
    LLPush(&s->ll, block);
    s->buffer_pos = 0;
}

void SerializerWrite(Serializer* s, RBuffer data)
{
    if (data.size == 0) return;
    
    if (data.size > s->buffer_size) {
        SerializerPushBuffer(s);
        
        RBuffer block = RBufferCopy(s->arena, data);
        LLPush(&s->ll, block);
    }
    else {
        U64 bytes_left = s->buffer_size - s->buffer_pos;
        if (data.size > bytes_left) {
            SerializerPushBuffer(s);
        }
        
        memcpy(s->buffer + s->buffer_pos, data.data, data.size);
        s->buffer_pos += data.size;
    }

    s->size += data.size;
}

RBuffer RBufferFromSerializer(Arena* arena, Serializer* s)
{
    SerializerPushBuffer(s);

    RBuffer dst = RBufferAlloc(arena, s->size);

    U64 offset = 0;
    for (LLNode* node = s->ll.root; node != NULL; node = node->next)
    {
        RBuffer block = *(RBuffer*)(node + 1);
        Assert(offset + block.size <= dst.size);
        memcpy(dst.data + offset, block.data, block.size);
        offset += block.size;
    }

    Assert(offset == s->size);
    return dst;
}

void WriteI8(Serializer* s, I8 v) { SerializerWrite(s, bufferof(v)); }
void WriteI16(Serializer* s, I16 v) { SerializerWrite(s, bufferof(v)); }
void WriteI32(Serializer* s, I32 v) { SerializerWrite(s, bufferof(v)); }
void WriteI64(Serializer* s, I64 v) { SerializerWrite(s, bufferof(v)); }
void WriteU8(Serializer* s, U8 v) { SerializerWrite(s, bufferof(v)); }
void WriteU16(Serializer* s, U16 v) { SerializerWrite(s, bufferof(v)); }
void WriteU32(Serializer* s, U32 v) { SerializerWrite(s, bufferof(v)); }
void WriteU64(Serializer* s, U64 v) { SerializerWrite(s, bufferof(v)); }

void WriteF32(Serializer* s, F32 v) { SerializerWrite(s, bufferof(v)); }
void WriteF64(Serializer* s, F64 v) { SerializerWrite(s, bufferof(v)); }

void WriteB8(Serializer* s, B32 v) { B8 v0 = (B8)v; SerializerWrite(s, bufferof(v0)); }

void WriteString(Serializer* s, String v) {
    WriteU32(s, (U32)v.size);
    SerializerWrite(s, RBufferFromStr(v));
}

//- DESERIALIZER

Deserializer* DeserializerAlloc(Arena* arena, RBuffer data)
{
    Deserializer* s = ArenaPushStruct<Deserializer>(arena);
    s->data = data;
    return s;
}

void* DeserializerRead(Deserializer* s, U64 bytes) {
    if (s->failed) return NULL;
    
    if (s->cursor + bytes > s->data.size) {
        DeserializerFailed(s);
        return NULL;
    }

    void* res = s->data.data + s->cursor;
    s->cursor += bytes;
    return res;
}

void DeserializerFailed(Deserializer* s)
{
    s->failed = true;
}

U32 ReadVersionU32(Deserializer* s, U32 min, U32 max)
{
    if (s->failed) return 0;
    U32 version = ReadU32(s);
    if (version < min || version > max) {
        DeserializerFailed(s);
        return 0;
    }
    return version;
}

template<typename T>
inline T _ReadFromDeserializer(Deserializer* s) {
    T* res = (T*)DeserializerRead(s, sizeof(T));
    if (res == NULL) return {};
    return *res;
}

I8 ReadI8(Deserializer* s) { return _ReadFromDeserializer<I8>(s); }
I16 ReadI16(Deserializer* s) { return _ReadFromDeserializer<I16>(s); }
I32 ReadI32(Deserializer* s) { return _ReadFromDeserializer<I32>(s); }
I64 ReadI64(Deserializer* s) { return _ReadFromDeserializer<I64>(s); }
U8 ReadU8(Deserializer* s) { return _ReadFromDeserializer<U8>(s); }
U16 ReadU16(Deserializer* s) { return _ReadFromDeserializer<U16>(s); }
U32 ReadU32(Deserializer* s) { return _ReadFromDeserializer<U32>(s); }
U64 ReadU64(Deserializer* s) { return _ReadFromDeserializer<U64>(s); }

F32 ReadF32(Deserializer* s) { return _ReadFromDeserializer<F32>(s); }
F64 ReadF64(Deserializer* s) { return _ReadFromDeserializer<F64>(s); }

B8 ReadB8(Deserializer* s) { return _ReadFromDeserializer<B8>(s); }

String ReadStringView(Deserializer* s) {
    U32 size = ReadU32(s);
    return StrMake((const char*)DeserializerRead(s, size), size);
}

String ReadString(Arena* arena, Deserializer* s) {
    U32 size = ReadU32(s);
    const char* str = (const char*)DeserializerRead(s, size);
    if (str == NULL) return {};
    return StrCopy(arena, StrMake(str, size));
}

//- LOCATION

Location LocationMake(U64 start, U64 end, I32 script_id)
{
    Location location = {};
    location.range = { start, end };
    location.script_id = script_id;
    return location;
}

B32 LocationIsValid(Location location) {
    return location.script_id >= 0;
}

void WriteLocation(Serializer* s, Location src)
{
    WriteI32(s, src.script_id);
    WriteU64(s, src.range.min);
    WriteU64(s, src.range.max);
}

Location ReadLocation(Deserializer* s)
{
    Location dst = {};
    dst.script_id = ReadI32(s);
    dst.range.min = ReadU64(s);
    dst.range.max = ReadU64(s);
    return dst;
}

//- REPORT 

void PrintEx(PrintLevel level, String str, ...)
{
    va_list args;
    va_start(args, str);
    String result = string_format_with_args(context.arena, str, args);
    va_end(args);
    
    if (level != PrintLevel_UserCode)
    {
        // Discard ansi sequences
        for (U64 i = 0; i < result.size - 1; i++) {
            if (StrStarts(StrSub(result, i, result.size - i), "\x1b[")) {
                result[i] = 'x';
            }
        }
    }
    
    if (system_info.supports_ansi_seq)
    {
        B32 reset = true;
        
        switch (level)
        {
            case PrintLevel_DevLog: OsConsoleWrite(ANSI_FG_YELLOW); break;
            case PrintLevel_WarningReport: OsConsoleWrite(ANSI_FG_GREEN); break;
            case PrintLevel_ErrorReport: OsConsoleWrite(ANSI_FG_RED); break;
            
            case PrintLevel_InfoReport:
            case PrintLevel_UserCode:
            reset = true;
            break;
        }
        
        OsConsoleWrite(result);
        
        if (reset) {
            OsConsoleWrite(ANSI_RESET);
        }
    }
    else {
        OsConsoleWrite(result);
    }
    
    OsConsoleFlush();
}

void LogInternal(String tag, String str, ...)
{
    PROFILE_FUNCTION;
    
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
    
    PrintEx(PrintLevel_DevLog, string_from_builder(context.arena, &builder));
}

YovSystemInfo system_info; 
per_thread_var YovThreadContext context;

void InitializeThread()
{
    context.thread_index = AtomicIncrement32(&system_info.thread_counter);
    char thread_id[32];
    CStrFromU64(thread_id, context.thread_index);
    
    char arena_name[128] = "Arena Thread ";
    CStrAppend(arena_name, thread_id, sizeof(arena_name));
    
    context.arena = ArenaAlloc(Gb(16), 8, arena_name);
}

void ShutdownThread()
{
    ArenaFree(context.arena);
}

Reporter* ReporterAlloc(Arena* arena)
{
    Reporter* reporter = ArenaPushStruct<Reporter>(arena);
    reporter->arena = arena;
    reporter->reports = BArrayMake<Report>(arena, 32);
    return reporter;
}

void ReportEx(Reporter* reporter, ReportLevel level, Location location, U32 line, String path, String text, ...)
{
    va_list args;
    va_start(args, text);
    String formatted_text = string_format_with_args(context.arena, text, args);
    va_end(args);
    
    Report report;
    report.level = level;
    report.text = StrCopy(reporter->arena, formatted_text);
    report.location = location;
    report.line = line;
    report.path = StrCopy(reporter->arena, path);
    
    MutexLock(&reporter->mutex);
    
    report.index = reporter->reports.count;
    BArrayAdd(&reporter->reports, report);

    if (level == ReportLevel_Error) {
        reporter->exit_requested = true;
        if (!reporter->exit_code_is_set) {
            reporter->exit_code = -1;
        }
    }

    PROFILE_LOG(formatted_text);
    MutexUnlock(&reporter->mutex);
}

void ReporterSetExitCode(Reporter* reporter, I64 exit_code)
{
    MutexLockGuard(&reporter->mutex);
    
    if (reporter->exit_code_is_set) return;
    reporter->exit_code_is_set = true;
    reporter->exit_code = exit_code;
    reporter->exit_requested = true;
}

internal_fn I32 ReportCompare(const void* _0, const void* _1)
{
    const Report* r0 = (const Report*)_0;
    const Report* r1 = (const Report*)_1;
    
    if (r0->location.range.min == r1->location.range.min) {
        return (r0->index < r1->index) ? -1 : 1;
    }

    return (r0->location.range.min < r1->location.range.min) ? -1 : 1;
}

void ReporterPrint(Reporter* reporter)
{
    Array<Report> reports = ArrayFromBArray(context.arena, reporter->reports);
    
#if !DEV_UNSORTED_REPORTS
    ArraySort(reports, ReportCompare);
#endif
    
    foreach(i, reports.count) {
        PrintReport(reports[i]);
    }
}

String StringFromReport(Arena* arena, Report report)
{
    if (report.path.size == 0 || report.line == 0) return StrCopy(arena, report.text);
    else {
        return StrFormat(arena, "%S(%u): %S", report.path, (U32)report.line, report.text);
    }
}

void PrintReport(Report report) {
    String str = StringFromReport(context.arena, report);

    PrintLevel level = PrintLevel_ErrorReport;
    if (report.level == ReportLevel_Info) level = PrintLevel_InfoReport;
    else if (report.level == ReportLevel_Warning) level = PrintLevel_WarningReport;
    else if (report.level == ReportLevel_Error) level = PrintLevel_ErrorReport;

    PrintEx(level, "%S\n", str);
}

#include "autogenerated/help.h"

Input* InputFromArgs(Arena* arena, Reporter* reporter)
{
    PROFILE_FRAME_MARK;
    PROFILE_FUNCTION;
    
    Input* input = ArenaPushStruct<Input>(arena);
    input->caller_dir = StrCopy(arena, system_info.working_path);
    
    Array<String> args = OsGetArgs(context.arena);
    I32 script_args_start_index = args.count;
    
    foreach(i, args.count)
    {
        String arg = args[i];
        
        if (arg.size > 0 && arg[0] != '-') {
            input->main_script_path = arg;
            script_args_start_index = i + 1;
            break;
        }
        
        if (StrEquals(arg, LANG_ARG_ANALYZE)) input->settings.analyze_only = true;
        else if (StrEquals(arg, LANG_ARG_TRACE)) input->settings.trace = true;
        else if (StrEquals(arg, LANG_ARG_USER_ASSERT)) input->settings.user_assert = true;
        else if (StrEquals(arg, LANG_ARG_WAIT_END)) input->settings.wait_end = true;
        else if (StrEquals(arg, LANG_ARG_NO_USER)) input->settings.no_user = true;
        else if (StrEquals(arg, "-help") || StrEquals(arg, "-h")) {
            PrintF("Yov Programming Language %S\n", YOV_VERSION);
            PrintF("Location: %S\n\n", system_info.executable_path);
            PrintF(YOV_HELP_STR);
            reporter->exit_requested = true;
            return input;
        }
        else if (StrEquals(arg, "-version") || StrEquals(arg, "-v")) {
            PrintF("Yov Programming Language %S\n", YOV_VERSION);
            reporter->exit_requested = true;
            return input;
        }
        else {
            ReportErrorNoCode("Unknown Yov argument '%S'\n", arg);
        }
    }
    
    if (input->main_script_path.size == 0) {
        ReportErrorNoCode("Script not specified");
        return input;
    }
    
    input->script_args = ArraySub(args, script_args_start_index, args.count - script_args_start_index);
    input->script_args = StrArrayCopy(arena, input->script_args);
    
    input->main_script_path = PathResolveImport(arena, input->caller_dir, input->main_script_path);
    
    return input;
}

I32 InputFindScriptArg(Input* input, String name) {
    foreach(i, input->script_args.count) {
        String arg = input->script_args[i];
        if (StrEquals(arg, name)) return i;
    }
    return -1;
}

String StringFromPrimitive(PrimitiveType type)
{
    switch (type)
    {
        case PrimitiveType_Int: return "Int";
        case PrimitiveType_UInt: return "UInt";
        case PrimitiveType_Bool: return "Bool";
        case PrimitiveType_Float: return "Float";
        case PrimitiveType_String: return "String";
        case PrimitiveType_Type: return "Type";
    }
    
    InvalidCodepath();
    return "?";
}

TypeChild TypeChildMake(Type* type, String name, I32 index, B32 is_property) {
    TypeChild res = {};
    res.type = type;
    res.name = name;
    res.index = index;
    res.is_property = is_property;
    return res;
}

TypeChild TypeChildCopy(Arena* arena, TypeChild src) {
    TypeChild dst = src;
    dst.name = StrCopy(arena, src.name);
    return dst;
}


inline_fn Type _MakeSpecialType(const char* name, VKind kind, U32 id) {
    Type type = {};
    type.name = name;
    type.kind = kind;
    type.id = id;
    return type;
}

inline_fn Type _MakePrimitive(const char* name, PrimitiveType primitive, U32 id) {
    Type type = {};
    type.name = name;
    type.kind = VKind_Primitive;
    type.primitive = primitive;
    type.id = id;
    return type;
}


read_only Type _nil_type = _MakeSpecialType("Nil", VKind_Nil, 0);
read_only Type _void_type = _MakeSpecialType("void", VKind_Void, 1);
read_only Type _any_type = _MakeSpecialType("Any", VKind_Any, 2);

read_only Type _int_type = _MakePrimitive("Int", PrimitiveType_Int, 3);
read_only Type _uint_type = _MakePrimitive("UInt", PrimitiveType_UInt, 4);
read_only Type _bool_type = _MakePrimitive("Bool", PrimitiveType_Bool, 5);
read_only Type _float_type = _MakePrimitive("Float", PrimitiveType_Float, 6);
read_only Type _string_type = _MakePrimitive("String", PrimitiveType_String, 7);
read_only Type _type_type = _MakePrimitive("Type", PrimitiveType_Type, 8);

#define TYPE_ID_BASE 8

Type* nil_type = &_nil_type;
Type* void_type = &_void_type;
Type* any_type = &_any_type;

Type* int_type = &_int_type;
Type* uint_type = &_uint_type;
Type* bool_type = &_bool_type;
Type* float_type = &_float_type;
Type* string_type = &_string_type;
Type* type_type = &_type_type;

TypeSystem* TypeSystemAlloc(Arena* arena)
{
    TypeSystem* tsys = ArenaPushStruct<TypeSystem>(arena);
    tsys->arena = arena;
    tsys->types = BArrayMake<Type>(arena, 128);
    return tsys;
}

B32 TypeIsValid(Type* type) {
    return type->kind > VKind_Any;
}

Type* TypeGetNext(TypeSystem* tsys, Type* type)
{
    PROFILE_FUNCTION;
    
    if (type->kind == VKind_Array || type->kind == VKind_List) {
        return TypeGet(type->element_type_id);
    }
    
    if (type->kind == VKind_Reference) {
        return TypeGet(type->reference_next_id);
    }
    
    return nil_type;
}

Type* TypeGetBase(TypeSystem* tsys, Type* type)
{
    PROFILE_FUNCTION;
    
    Type* next = type;
    while (next != nil_type) {
        type = next;
        next = TypeGetNext(tsys, next);
    }
    
    return type;
}

U32 TypeGetLastID(TypeSystem* tsys)
{
    MutexLockGuard(&tsys->types_mutex);
    return tsys->types.count + TYPE_ID_BASE;
}

internal_fn Type* AllocType(TypeSystem* tsys, VKind kind)
{
    Assert(MutexIsLocked(&tsys->types_mutex));
    Type* type = BArrayAdd(&tsys->types);
    type->kind = kind;
    type->id = tsys->types.count + TYPE_ID_BASE;
    return type;
}

Type* TypeAddStruct(TypeSystem* tsys, String name, U32 definition_index)
{
    Assert(TypeFromName(tsys, name) != NULL);
    
    MutexLockGuard(&tsys->types_mutex);

    Type* type = AllocType(tsys, VKind_Struct);
    type->name = StrCopy(tsys->arena, name);
    type->definition_index = definition_index;
    return type;
}

Type* TypeAddEnum(TypeSystem* tsys, String name, U32 definition_index)
{
    Assert(TypeFromName(tsys, name) != NULL);

    MutexLockGuard(&tsys->types_mutex);

    Type* type = AllocType(tsys, VKind_Enum);
    type->name = StrCopy(tsys->arena, name);
    type->definition_index = definition_index;
    return type;
}

Type* TypeFromID(TypeSystem* tsys, U32 ID)
{
    switch (ID)
    {
    case 0: return nil_type;
    case 1: return void_type;
    case 2: return any_type;
    case 3: return int_type;
    case 4: return uint_type;
    case 5: return bool_type;
    case 6: return float_type;
    case 7: return string_type;
    case 8: return type_type;
    }

    U32 index = ID - (TYPE_ID_BASE + 1);

    MutexLockGuard(&tsys->types_mutex);

    if (index >= tsys->types.count) return nil_type;
    return &tsys->types[index];
}

Type* TypeFromName(TypeSystem* tsys, String name)
{
    if (name == "Any") return any_type;
    if (name == "void") return void_type;
    if (name == "Int") return int_type;
    if (name == "UInt") return uint_type;
    if (name == "Bool") return bool_type;
    if (name == "Float") return float_type;
    if (name == "String") return string_type;
    if (name == "Type") return type_type;
    
    MutexLockGuard(&tsys->types_mutex);
    
    foreach_BArray(it, &tsys->types) {
        Type* t = it.value;
        if (t->name == name) {
            return t;
        }
    }
    return nil_type;
}

Type* TypeFromArray(TypeSystem* tsys, Type* element, U32 dimension)
{
    MutexLockGuard(&tsys->types_mutex);
    
    Type* type = element;
    
    while (dimension > 0)
    {
        element = type;
        
        foreach_BArray(it, &tsys->types)
        {
            Type* t = it.value;
            
            if (t->kind == VKind_Array && t->element_type_id == element->id) {
                return t;
            }
        }
        
        type = AllocType(tsys, VKind_Array);
        type->name = StrFormat(tsys->arena, "Array[%S]", element->name);
        type->element_type_id = element->id;
        
        dimension--;
    }
    
    return type;
}

Type* TypeFromList(TypeSystem* tsys, Type* element, U32 dimension)
{
    MutexLockGuard(&tsys->types_mutex);
    
    Type* type = element;
    
    while (dimension > 0)
    {
        element = type;
        
        foreach_BArray(it, &tsys->types)
        {
            Type* t = it.value;
            
            if (t->kind == VKind_List && t->element_type_id == element->id) {
                return t;
            }
        }
        
        type = AllocType(tsys, VKind_List);
        type->name = StrFormat(tsys->arena, "List[%S]", element->name);
        type->element_type_id = element->id;
        
        dimension--;
    }
    
    return type;
}

Type* TypeFromReference(TypeSystem* tsys, Type* base_type)
{
    MutexLockGuard(&tsys->types_mutex);
    
    foreach_BArray(it, &tsys->types)
    {
        Type* t = it.value;
        
        if (t->kind == VKind_Reference && t->reference_next_id == base_type->id) {
            return t;
        }
    }
    
    Type* type = AllocType(tsys, VKind_Reference);
    type->name = StrFormat(tsys->arena, "%S&", base_type->name);
    type->reference_next_id = base_type->id;
    return type;
}

Type* TypeFromPrimitive(PrimitiveType primitive)
{
    switch (primitive) {
        case PrimitiveType_Int: return int_type;
        case PrimitiveType_UInt: return uint_type;
        case PrimitiveType_Bool: return bool_type;
        case PrimitiveType_Float: return float_type;
        case PrimitiveType_String: return string_type;
        case PrimitiveType_Type: return type_type;
    }
    
    return nil_type;
}

Type* TypeFromStruct(TypeSystem* tsys, U32 definition_index)
{
    MutexLockGuard(&tsys->types_mutex);
    
    foreach_BArray(it, &tsys->types)
    {
        Type* t = it.value;
        
        if (t->kind == VKind_Struct && t->definition_index == definition_index) {
            return t;
        }
    }
    
    return nil_type;
}

Type* TypeFromEnum(TypeSystem* tsys, U32 definition_index)
{
    MutexLockGuard(&tsys->types_mutex);
    
    foreach_BArray(it, &tsys->types)
    {
        Type* t = it.value;
        
        if (t->kind == VKind_Enum && t->definition_index == definition_index) {
            return t;
        }
    }
    
    return nil_type;
}

B32 TypeIsEnum(Type* type) { return type->kind == VKind_Enum; }
B32 TypeIsArray(Type* type) { return type->kind == VKind_Array; }
B32 TypeIsList(Type* type) { return type->kind == VKind_List; }
B32 TypeIsStruct(Type* type) { return type->kind == VKind_Struct; }
B32 TypeIsReference(Type* type) { return type->kind == VKind_Reference; }
B32 TypeIsAnyInt(Type* type) { return type == int_type || type == uint_type; }

U32 RegIndexFromGlobal(U32 global_index) {
    return global_index | Bit(30);
}

U32 RegIndexFromLocal(U32 local_index) {
    return local_index;
}

I32 LocalFromRegIndex(I32 register_index)
{
    if (register_index & Bit(30)) return -1;
    return register_index;
}

I32 GlobalFromRegIndex(I32 register_index)
{
    if (!(register_index & Bit(30))) return -1;
    return register_index & 0xBFFFFFFF;
}

TypeChild string_properties[] = {
    TypeChildMake(uint_type, "size", 0, true)
};

TypeChild array_properties[] = {
    TypeChildMake(uint_type, "count", 0, true)
};

TypeChild enum_properties[] = {
    TypeChildMake(int_type, "index", 0, true),
    TypeChildMake(int_type, "value", 1, true),
    TypeChildMake(string_type, "name", 2, true),
};

TypeChild type_properties[] = {
    TypeChildMake(string_type, "name", 0, true),
};

Array<TypeChild> TypeGetProperties(Type* type)
{
    if (type == string_type) {
        return arrayof(string_properties);
    }

    if (type == type_type) {
        return arrayof(type_properties);
    }
    
    if (TypeIsArray(type)) {
        return arrayof(array_properties);
    }
    
    if (TypeIsEnum(type)) {
        return arrayof(enum_properties);
    }
    
    return {};
}

TypeChild TypeGetProperty(Type* type, String property)
{
    Array<TypeChild> props = TypeGetProperties(type);
    foreach(i, props.count) {
        if (props[i].name == property) return props[i];
    }
    return TypeChildMake(nil_type, "", -1, true);
}

TypeChild TypeGetPropertyAt(Type* type, U32 index)
{
    Array<TypeChild> props = TypeGetProperties(type);
    if (index < props.count) return props[index];
    return TypeChildMake(nil_type, "", -1, true);
}

void WriteType(TypeSystem* tsys, Serializer* s, Type src)
{
    WriteU32(s, 0); // VERSION
    
    WriteU8(s, (U8)src.kind);

    WriteU32(s, src.id);
    WriteString(s, src.name);
    
    switch (src.kind)
    {
    case VKind_Primitive:
    WriteU8(s, (U8)src.primitive);
    break;

    case VKind_Struct:
    case VKind_Enum:
    WriteU32(s, src.definition_index);
    break;

    case VKind_Reference:
    {
        WriteU32(s, src.reference_next_id);
        break;
    }

    case VKind_Array:
    case VKind_List:
    {
        WriteU32(s, src.element_type_id);
        break;
    }

    case VKind_Nil:
    case VKind_Void:
    case VKind_Any:
    break;
    }
}

Type* ReadType(TypeSystem* tsys, Deserializer* s)
{
    U32 version = ReadVersionU32(s, 0, 0);

    VKind kind = (VKind)ReadU8(s);

    MutexLockGuard(&tsys->types_mutex);

    Type* type = AllocType(tsys, kind);

    if (type->id != ReadU32(s)) {
        DeserializerFailed(s);
        return nil_type;
    }

    type->name = ReadString(tsys->arena, s);

    switch (kind)
    {
    case VKind_Primitive:
    {
        type->primitive = (PrimitiveType)ReadU8(s);
        break;
    }

    case VKind_Struct:
    case VKind_Enum:
    {
        type->definition_index = ReadU32(s);
        break;
    }
    
    case VKind_Reference:
    {
        type->reference_next_id = ReadU32(s);
        break;
    }

    case VKind_Array:
    case VKind_List:
    {
        type->element_type_id = ReadU32(s);
        break;
    }

    case VKind_Nil:
    case VKind_Void:
    case VKind_Any:
    break;

    }

    return type;
}

//- VALUES

B32 ValueIsCompiletime(Value value)
{
    if (value.kind == ValueKind_Array)
    {
        foreach(i, value.array.values.count) {
            if (!ValueIsCompiletime(value.array.values[i])) return false;
        }
        return true;
    }
    
    return value.kind == ValueKind_Literal || value.kind == ValueKind_ZeroInit || value.kind == ValueKind_None;
}

I32 ValueGetRegister(Value value) {
    if (value.kind != ValueKind_LValue && value.kind != ValueKind_Register) return -1;
    return value.reg.index;
}

B32 ValueIsRValue(Value value) { return value.kind != ValueKind_None && value.kind != ValueKind_LValue; }

B32 ValueIsNull(Value value) {
    return value.kind == ValueKind_Literal && value.type_id == void_type->id;
}

B32 ValueEquals(Value v0, Value v1)
{
    if (v0.kind != v1.kind) return false;
    
    if (v0.kind == ValueKind_LValue || v0.kind == ValueKind_Register) {
        return v0.reg.index == v1.reg.index && v0.reg.reference_op == v1.reg.reference_op;
    }
    
    return false;
}

Value ValueCopy(Arena* arena, Value src)
{
    Value dst = src;
    
    if (src.kind == ValueKind_Literal && src.type_id == string_type->id) {
        dst.literal_string = StrCopy(arena, src.literal_string);
    }
    else if (src.kind == ValueKind_Array) {
        dst.array.values = ValueArrayCopy(arena, src.array.values);
    }
    else if (src.kind == ValueKind_StringComposition) {
        dst.string_composition = ValueArrayCopy(arena, src.string_composition);
    }
    else if (src.kind == ValueKind_MultipleReturn) {
        dst.multiple_return = ValueArrayCopy(arena, src.multiple_return);
    }
    
    return dst;
}

Array<Value> ValueArrayCopy(Arena* arena, Array<Value> src)
{
    Array<Value> dst = ArrayAlloc<Value>(arena, src.count);
    foreach(i, dst.count) {
        dst[i] = ValueCopy(arena, src[i]);
    }
    return dst;
}

Value ValueNone() {
    Value v{};
    v.type_id = void_type->id;
    v.reg.index = -1;
    return v;
}

Value ValueNull() {
    Value v{};
    v.type_id = void_type->id;
    v.kind = ValueKind_Literal;
    return v;
}

Value ValueFromRegister(I32 index, U32 type_id, B32 is_lvalue) {
    Assert(index >= 0);
    Value v{};
    v.type_id = type_id;
    v.reg.index = index;
    v.reg.reference_op = 0;
    v.kind = is_lvalue ? ValueKind_LValue : ValueKind_Register;
    return v;
}

Value ValueFromReference(TypeSystem* tsys, Value value)
{
    Assert(value.kind == ValueKind_LValue || value.kind == ValueKind_Register);
    Assert(value.reg.reference_op <= 0);
    
    Value v{};
    v.type_id = TypeFromReference(tsys, TypeFromID(tsys, value.type_id))->id;
    v.reg.index = value.reg.index;
    v.reg.reference_op = value.reg.reference_op + 1;
    v.kind = value.kind;
    return v;
}

Value ValueFromDereference(TypeSystem* tsys, Value value)
{
    Type* type = TypeFromID(tsys, value.type_id);

    Assert(value.kind == ValueKind_LValue || value.kind == ValueKind_Register);
    Assert(type->kind == VKind_Reference);
    
    Value v{};
    v.type_id = TypeGetNext(tsys, type)->id;
    v.reg.index = value.reg.index;
    v.reg.reference_op = value.reg.reference_op - 1;
    v.kind = value.kind;
    return v;
}

Value ValueFromInt(I64 value) {
    Value v{};
    v.type_id = int_type->id;
    v.kind = ValueKind_Literal;
    v.literal_sint = value;
    return v;
}

Value ValueFromUInt(U64 value) {
    Value v{};
    v.type_id = uint_type->id;
    v.kind = ValueKind_Literal;
    v.literal_uint = value;
    return v;
}

Value ValueFromBool(B32 value) {
    Value v{};
    v.type_id = bool_type->id;
    v.kind = ValueKind_Literal;
    v.literal_bool = value;
    return v;
}

Value ValueFromFloat(F64 value) {
    Value v{};
    v.type_id = float_type->id;
    v.kind = ValueKind_Literal;
    v.literal_float = value;
    return v;
}

Value ValueFromEnum(Type* type, I64 value) {
    Value v{};
    v.type_id = type->id;
    v.kind = ValueKind_Literal;
    v.literal_sint = value;
    return v;
}

Value ValueFromString(Arena* arena, String value) {
    Value v{};
    v.type_id = string_type->id;
    v.kind = ValueKind_Literal;
    v.literal_string = StrCopy(arena, value);
    return v;
}

Value ValueFromStringArray(Arena* arena, Array<Value> values)
{
    BArray<Value> composition = BArrayMake<Value>(context.arena, 8);
    StringBuilder builder = string_builder_make(context.arena);

    foreach(i, values.count)
    {
        Value value = values[i];

        String ct_str;
        if (StringFromCompiletime(context.arena, &ct_str, value)) {
            append(&builder, ct_str);
        }
        else
        {
            String literal = string_from_builder(context.arena, &builder);
            if (literal.size > 0) {
                builder = string_builder_make(context.arena);
                BArrayAdd(&composition, ValueFromString(arena, literal));
            }
            
            BArrayAdd(&composition, value);
        }
    }

    if (composition.count == 0) {
        return ValueFromString(arena, string_from_builder(context.arena, &builder));
    }

    String literal = string_from_builder(context.arena, &builder);
    if (literal.size > 0) {
        BArrayAdd(&composition, ValueFromString(arena, literal));
    }

    Value v{};
    v.type_id = string_type->id;
    v.kind = ValueKind_StringComposition;
    v.string_composition = ArrayCopy(arena, ArrayFromBArray(context.arena, composition));
    return v;
}

Value ValueFromType(Type* type) {
    Value v{};
    v.type_id = type_type->id;
    v.kind = ValueKind_Literal;
    v.literal_type_id = type->id;
    return v;
}

Value ValueFromArray(Arena* arena, Type* array_type, Array<Value> elements)
{
    Assert(array_type->kind == VKind_Array);
    Value v{};
    v.type_id = array_type->id;
    v.kind = ValueKind_Array;
    v.array.values = ArrayCopy(arena, elements);
    return v;
}

Value ValueFromZero(Type* type)
{
    if (type == int_type) {
        return ValueFromInt(0);
    }
    if (type == uint_type) {
        return ValueFromUInt(0);
    }
    if (type == bool_type) {
        return ValueFromBool(false);
    }
    if (type == float_type) {
        return ValueFromFloat(0.0);
    }
    
    if (type == any_type) {
        return ValueNull();
    }
    
    Value v{};
    v.type_id = type->id;
    v.kind = ValueKind_ZeroInit;
    return v;
}

Value ValueFromGlobal(U32 type_id, U32 global_index)
{
    I32 register_index = RegIndexFromGlobal(global_index);
    return ValueFromRegister(register_index, type_id, true);
}

Value ValueFromReturn(Arena* arena, Array<Value> values)
{
    if (values.count == 0) return ValueNone();
    if (values.count == 1) return values[0];
    
    Value v{};
    v.type_id = any_type->id;
    v.kind = ValueKind_MultipleReturn;
    v.multiple_return = ArrayCopy(arena, values);
    return v;
}

Array<Value> ValuesFromReturn(Arena* arena, Value value, B32 empty_on_void)
{
    if (value.kind == ValueKind_MultipleReturn) return value.multiple_return;
    
    if (value.kind == ValueKind_None) {
        return {};
    }
    
    Array<Value> values = ArrayAlloc<Value>(arena, 1);
    values[0] = value;
    return values;
}

B32 StringFromCompiletime(Arena* arena, String* dst, Value value)
{
    *dst = {};

    if (!ValueIsCompiletime(value)) {
        return false;
    }
    
    if (value.type_id == string_type->id) {
        if (value.kind == ValueKind_Literal) {
            *dst = StrCopy(arena, value.literal_string);
            return true;
        }
        
        if (value.kind == ValueKind_ZeroInit) {
            *dst = {};
            return true;
        }
    }

    if (value.type_id == int_type->id) {
        *dst = StrFromI64(arena, value.literal_sint);
        return true;
    }

    if (value.type_id == uint_type->id) {
        *dst = StrFromU64(arena, value.literal_uint);
        return true;
    }

    if (value.type_id == float_type->id) {
        *dst = StrFromF64(arena, value.literal_float, 8);
        return true;
    }

    if (value.type_id == bool_type->id) {
        *dst = value.literal_bool ? "true" : "false";
        return true;
    }

    return false;
}

B32 B32FromCompiletime(Value value)
{
    if (!ValueIsCompiletime(value)) {
        InvalidCodepath();
        return false;
    }
    
    if (value.type_id != bool_type->id) {
        InvalidCodepath();
        return false;
    }
    
    if (value.kind == ValueKind_Literal) {
        return value.literal_bool;
    }
    
    if (value.kind == ValueKind_ZeroInit) {
        return false;
    }
    
    InvalidCodepath();
    return false;
}

Type* TypeFromCompiletime(TypeSystem* tsys, Value value)
{
    if (!ValueIsCompiletime(value)) {
        InvalidCodepath();
        return void_type;
    }

    Type* type = TypeFromID(tsys, value.type_id);
    
    if (type != type_type) {
        InvalidCodepath();
        return void_type;
    }
    
    if (value.kind == ValueKind_Literal) {
        return TypeFromID(tsys, value.literal_type_id);
    }
    
    if (value.kind == ValueKind_ZeroInit) {
        return void_type;
    }
    
    InvalidCodepath();
    return void_type;
}

B32 CompiletimeEquals(TypeSystem* tsys, Value v0, Value v1)
{
    if (!ValueIsCompiletime(v0) || !ValueIsCompiletime(v1)) {
        InvalidCodepath();
        return false;
    }
    
    if (v0.type_id != v1.type_id) {
        InvalidCodepath();
        return false;
    }
    
    Type* type = TypeFromID(tsys, v0.type_id);
    
    if (type == int_type) {
        return v0.literal_sint == v1.literal_sint;
    }
    if (type == uint_type) {
        return v0.literal_uint == v1.literal_uint;
    }
    if (type == bool_type) {
        return v0.literal_bool == v1.literal_bool;
    }
    if (type == float_type) {
        return v0.literal_float == v1.literal_float;
    }
    if (type == string_type) {
        return v0.literal_string == v1.literal_string;
    }
    if (TypeIsEnum(type)) {
        return v0.literal_sint == v1.literal_sint;
    }
    if (type == type_type) {
        return v0.literal_type_id == v1.literal_type_id;
    }
    
    InvalidCodepath();
    return false;
}

void WriteValue(Serializer* s, Value src)
{
    WriteU32(s, 0); // VERSION

    WriteU8(s, (U8)src.kind);
    WriteU32(s, src.type_id);

    switch (src.kind)
    {
    
    case ValueKind_LValue:
    case ValueKind_Register:
    {
        WriteI32(s, src.reg.index);
        WriteI32(s, src.reg.reference_op);
        break;
    }

    case ValueKind_StringComposition:
    {
        WriteArray(s, src.string_composition, WriteValue);
        break;
    }

    case ValueKind_Array:
    {
        WriteArray(s, src.array.values, WriteValue);
        break;
    }

    case ValueKind_MultipleReturn:
    {
        WriteArray(s, src.multiple_return, WriteValue);
        break;
    }

    case ValueKind_Literal:
    {
        if (src.type_id == int_type->id) WriteI64(s, src.literal_sint);
        else if (src.type_id == uint_type->id) WriteU64(s, src.literal_uint);
        else if (src.type_id == bool_type->id) WriteB8(s, src.literal_bool);
        else if (src.type_id == float_type->id) WriteF64(s, src.literal_float);
        else if (src.type_id == string_type->id) WriteString(s, src.literal_string);
        else if (src.type_id == type_type->id) WriteU32(s, src.literal_type_id);
        else if (src.type_id == void_type->id) {}
        else WriteI64(s, src.literal_sint);
        break;
    }

    case ValueKind_None:
    case ValueKind_ZeroInit:
    break;

    case ValueKind_count: break;
    }
}

Value ReadValue(Arena* arena, Deserializer* s)
{
    U32 version = ReadVersionU32(s, 0, 0);

    Value dst = {};

    dst.kind = (ValueKind)ReadU8(s);

    if (dst.kind >= ValueKind_count) {
        DeserializerFailed(s);
        return {};
    }

    dst.type_id = ReadU32(s);

    switch (dst.kind)
    {
    
    case ValueKind_LValue:
    case ValueKind_Register:
    {
        dst.reg.index = ReadI32(s);
        dst.reg.reference_op = ReadI32(s);
        break;
    }

    case ValueKind_StringComposition:
    {
        dst.string_composition = ReadArrayArena<Value>(arena, s, ReadValue);
        break;
    }

    case ValueKind_Array:
    {
        dst.array.values = ReadArrayArena<Value>(arena, s, ReadValue);
        break;
    }

    case ValueKind_MultipleReturn:
    {
        dst.multiple_return = ReadArrayArena<Value>(arena, s, ReadValue);
        break;
    }

    case ValueKind_Literal:
    {
        if (dst.type_id == int_type->id) dst.literal_sint = ReadI64(s);
        else if (dst.type_id == uint_type->id) dst.literal_uint = ReadU64(s);
        else if (dst.type_id == bool_type->id) dst.literal_bool = ReadB8(s);
        else if (dst.type_id == float_type->id) dst.literal_float = ReadF64(s);
        else if (dst.type_id == string_type->id) dst.literal_string = ReadString(arena, s);
        else if (dst.type_id == type_type->id) dst.literal_type_id = ReadU32(s);
        else if (dst.type_id == void_type->id) {}
        else dst.literal_sint = ReadI64(s);
        break;
    }

    case ValueKind_None:
    case ValueKind_ZeroInit:
    break;

    case ValueKind_count: break;
    }

    return dst;
}

//- UNIT

String StringFromOperatorKind(OperatorKind op) {
    if (op == OperatorKind_Addition) return "+";
    if (op == OperatorKind_Substraction) return "-";
    if (op == OperatorKind_Multiplication) return "*";
    if (op == OperatorKind_Division) return "/";
    if (op == OperatorKind_Modulo) return "%";
    if (op == OperatorKind_LogicalNot) return "!";
    if (op == OperatorKind_LogicalOr) return "||";
    if (op == OperatorKind_LogicalAnd) return "&&";
    if (op == OperatorKind_Equals) return "==";
    if (op == OperatorKind_NotEquals) return "!=";
    if (op == OperatorKind_LessThan) return "<";
    if (op == OperatorKind_LessEqualsThan) return "<=";
    if (op == OperatorKind_GreaterThan) return ">";
    if (op == OperatorKind_GreaterEqualsThan) return ">=";
    if (op == OperatorKind_Is) return "is";
    Assert(0);
    return "?";
}

B32 OperatorKindIsArithmetic(OperatorKind op) {
    if (op == OperatorKind_Addition) return true;
    if (op == OperatorKind_Substraction) return true;
    if (op == OperatorKind_Multiplication) return true;
    if (op == OperatorKind_Division) return true;
    if (op == OperatorKind_Modulo) return true;
    return false;
}

B32 OperatorKindIsComparison(OperatorKind op) {
    if (op == OperatorKind_Equals) return true;
    if (op == OperatorKind_NotEquals) return true;
    if (op == OperatorKind_LessThan) return true;
    if (op == OperatorKind_GreaterThan) return true;
    if (op == OperatorKind_LessEqualsThan) return true;
    if (op == OperatorKind_GreaterEqualsThan) return true;
    if (op == OperatorKind_LogicalAnd) return true;
    if (op == OperatorKind_LogicalOr) return true;
    if (op == OperatorKind_LogicalNot) return true;
    return false;
}

String StringFromUnitKind(Arena* arena, UnitKind unit)
{
    switch (unit)
    {
        case UnitKind_Error: return "error";
        case UnitKind_Copy: return "copy";
        case UnitKind_Store: return "store";
        case UnitKind_FunctionCall: return "call";
        case UnitKind_Return: return "return";
        case UnitKind_Jump: return "jump";
        case UnitKind_Child: return "child";
        case UnitKind_ResultEval: return "eval";
        case UnitKind_Empty: return "empty";
        case UnitKind_Add: return "add";
        case UnitKind_Sub: return "sub";
        case UnitKind_Mul: return "mul";
        case UnitKind_Div: return "div";
        case UnitKind_Mod: return "mod";
        
        case UnitKind_Eql: return "eql";
        
        case UnitKind_Neq: return "neq";
        case UnitKind_Gtr: return "gtr";
        case UnitKind_Lss: return "lss";
        case UnitKind_Geq: return "geq";
        case UnitKind_Leq: return "leq";
        
        case UnitKind_Or: return "or";
        case UnitKind_And: return "and";
        case UnitKind_Not: return "not";
        case UnitKind_Neg: return "neg";
        
        case UnitKind_Cast: return "cast";
        case UnitKind_BitCast: return "bcast";
        
        case UnitKind_Is: return "is";

        case UnitKind_count: break;
    }
    
    InvalidCodepath();
    return "?";
}

Unit UnitCopy(Arena* arena, Unit src)
{
    if (src.kind == UnitKind_Error || src.kind == UnitKind_Empty) return {};
    
    UnitKind kind = src.kind;

    Unit dst = {};
    dst.kind = kind;
    dst.dst_index = src.dst_index;
    dst.src0 = ValueCopy(arena, src.src0);
    dst.src1 = ValueCopy(arena, src.src1);
    dst.op_dst_type = src.op_dst_type;
    
    if (kind == UnitKind_FunctionCall) {
        dst.function_call.header_index = src.function_call.header_index;
        dst.function_call.parameters = ValueArrayCopy(arena, src.function_call.parameters);
    }
    else if (kind == UnitKind_Jump) {
        dst.jump.condition = src.jump.condition;
        dst.jump.offset = src.jump.offset;
    }
    else if (kind == UnitKind_Child) {
        dst.child.child_is_property = src.child.child_is_property;
    }
    
    return dst;
}

void WriteUnit(Serializer* s, Unit src)
{
    WriteU32(s, 0); // VERSION

    WriteU8(s, (U8)src.kind);
    WriteI32(s, src.dst_index);

    switch (src.kind)
    {
    case UnitKind_Copy:
    case UnitKind_Store:
    {
        WriteValue(s, src.src0);
        break;
    }

    case UnitKind_FunctionCall:
    {
        WriteU32(s, src.function_call.header_index);
        WriteArray(s, src.function_call.parameters, WriteValue);
        break;
    }

    case UnitKind_Jump:
    {
        WriteValue(s, src.src0);
        WriteI32(s, src.jump.condition);
        WriteI32(s, src.jump.offset);
        break;
    }

    case UnitKind_Child:
    {
        WriteValue(s, src.src0);
        WriteValue(s, src.src1);
        WriteB8(s, src.child.child_is_property);
        break;
    }

    case UnitKind_ResultEval:
    {
        WriteValue(s, src.src0);
        break;
    }

    case UnitKind_Add:
    case UnitKind_Sub:
    case UnitKind_Mul:
    case UnitKind_Div:
    case UnitKind_Mod:
    case UnitKind_Eql:
    case UnitKind_Neq:
    case UnitKind_Gtr:
    case UnitKind_Lss:
    case UnitKind_Geq:
    case UnitKind_Leq:
    case UnitKind_Or:
    case UnitKind_And:
    {
        WriteValue(s, src.src0);
        WriteValue(s, src.src1);
        WriteU8(s, (U8)src.op_dst_type);
        break;
    }

    case UnitKind_Not:
    case UnitKind_Neg:
    case UnitKind_Cast:
    case UnitKind_BitCast:
    {
        WriteValue(s, src.src0);
        WriteU8(s, (U8)src.op_dst_type);
        break;
    }

    case UnitKind_Is:
    {
        WriteValue(s, src.src0);
        WriteValue(s, src.src1);
        break;
    }

    case UnitKind_Return:
    break;

    case UnitKind_Error:
    case UnitKind_Empty:
    break;

    case UnitKind_count: break;
    }

    WriteU32(s, src.line);
}

Unit ReadUnit(Arena* arena, Deserializer* s)
{
    U32 version = ReadVersionU32(s, 0, 0);

    Unit dst = {};

    dst.kind = (UnitKind)ReadU8(s);

    if (dst.kind >= UnitKind_count) {
        DeserializerFailed(s);
        return {};
    }

    dst.dst_index = ReadI32(s);

    switch (dst.kind)
    {
    case UnitKind_Copy:
    case UnitKind_Store:
    {
        dst.src0 = ReadValue(arena, s);
        break;
    }

    case UnitKind_FunctionCall:
    {
        dst.function_call.header_index = ReadU32(s);
        dst.function_call.parameters = ReadArrayArena<Value>(arena, s, ReadValue);
        break;
    }

    case UnitKind_Jump:
    {
        dst.src0 = ReadValue(arena, s);
        dst.jump.condition = ReadI32(s);
        dst.jump.offset = ReadI32(s);
        break;
    }

    case UnitKind_Child:
    {
        dst.src0 = ReadValue(arena, s);
        dst.src1 = ReadValue(arena, s);
        dst.child.child_is_property = ReadB8(s);
        break;
    }

    case UnitKind_ResultEval:
    {
        dst.src0 = ReadValue(arena, s);
        break;
    }

    case UnitKind_Add:
    case UnitKind_Sub:
    case UnitKind_Mul:
    case UnitKind_Div:
    case UnitKind_Mod:
    case UnitKind_Eql:
    case UnitKind_Neq:
    case UnitKind_Gtr:
    case UnitKind_Lss:
    case UnitKind_Geq:
    case UnitKind_Leq:
    case UnitKind_Or:
    case UnitKind_And:
    {
        dst.src0 = ReadValue(arena, s);
        dst.src1 = ReadValue(arena, s);
        dst.op_dst_type = (PrimitiveType)ReadU8(s);
        break;
    }

    case UnitKind_Not:
    case UnitKind_Neg:
    case UnitKind_Cast:
    case UnitKind_BitCast:
    {
        dst.src0 = ReadValue(arena, s);
        dst.op_dst_type = (PrimitiveType)ReadU8(s);
        break;
    }

    case UnitKind_Is:
    {
        dst.src0 = ReadValue(arena, s);
        dst.src1 = ReadValue(arena, s);
        break;
    }

    case UnitKind_Return:
    break;

    case UnitKind_Error:
    case UnitKind_Empty:
    break;

    case UnitKind_count: break;
    }

    dst.line = ReadU32(s);

    return dst;
}

//- IR

void WriteObjectDefinition(Serializer* s, ObjectDefinition src)
{
    WriteU32(s, 0); // VERSION

    WriteString(s, src.name);
    WriteU32(s, src.type_id);
    WriteB8(s, src.is_constant);

    WriteLocation(s, src.location);
}

void WriteRegister(Serializer* s, Register src)
{
    WriteU8(s, (U8)src.kind);
    WriteU32(s, src.type_id);
    WriteB8(s, (B8)src.is_constant);
}

void WriteIR(Serializer* s, IR src)
{
    WriteU32(s, 0); // VERSION

    WriteB8(s, src.valid);

    if (src.valid)
    {
        WriteArray(s, src.instructions, WriteUnit);
        WriteArray(s, src.local_registers, WriteRegister);
        WriteValue(s, src.output_value);
    }
}

ObjectDefinition ReadObjectDefinition(Arena* arena, Deserializer* s)
{
    U32 version = ReadVersionU32(s, 0, 0);

    ObjectDefinition dst = {};

    dst.name = ReadString(arena, s);
    dst.type_id = ReadU32(s);
    dst.is_constant = ReadB8(s);
    dst.location = ReadLocation(s);

    return dst;
}

Register ReadRegister(Deserializer* s)
{
    Register dst = {};
    dst.kind = (RegisterKind)ReadU8(s);

    if (dst.kind >= RegisterKind_count) {
        DeserializerFailed(s);
        return {};
    }

    dst.type_id = ReadU32(s);
    dst.is_constant = ReadB8(s);
    return dst;
}

IR ReadIR(Arena* arena, Deserializer* s)
{
    IR dst = {};

    U32 version = ReadVersionU32(s, 0, 0);

    dst.valid = ReadB8(s);

    if (dst.valid)
    {
        dst.instructions = ReadArrayArena<Unit>(arena, s, ReadUnit);
        dst.local_registers = ReadArray<Register>(arena, s, ReadRegister);
        dst.output_value = ReadValue(arena, s);
    }

    return dst;
}

//- RUNTIME COMMON

RuntimeSettings RuntimeSettingsCopy(Arena* arena, RuntimeSettings src)
{
    RuntimeSettings dst = {};
    dst.no_user = src.no_user;
    dst.user_assert = src.user_assert;
    dst.caller_dir = StrCopy(arena, src.caller_dir);
    dst.origin_dir = StrCopy(arena, src.origin_dir);
    return dst;
}

//- HIGH LEVEL CALLS

I64 CompileAndRunFromArgs()
{
    PROFILE_FUNCTION;
    
    Arena* arena = ArenaAlloc(Gb(32), 8, "Arena Main");
    defer (ArenaFree(arena));
    
    Reporter* reporter = ReporterAlloc(arena);
    Input* input = InputFromArgs(arena, reporter);
    
    if (!reporter->exit_requested)
    {
        RBuffer binary = YovCompile(arena, reporter, input->main_script_path);
        
        if (!reporter->exit_requested && !input->settings.analyze_only) {
            RuntimeSettings settings = {};
            settings.user_assert = input->settings.user_assert;
            settings.no_user = input->settings.no_user;
            settings.origin_dir = PathGetFolder(input->main_script_path);
            settings.caller_dir = system_info.working_path;
            
            ExecuteProgram(binary, input, reporter, settings);
        }
    }
    
    ReporterPrint(reporter);
    
    if (input->settings.wait_end) {
        OsConsoleWait();
    }
    
    return reporter->exit_code;
}

#if 0 // TODO(Jose)

// TEMP
#include <Windows.h>
I32 StepPressed() {
    if (GetAsyncKeyState('A') & 1) return 1;
    if (GetAsyncKeyState('W') & 1) return 2;
    if (GetAsyncKeyState('D') & 1) return 3;
    if (GetAsyncKeyState(VK_RETURN) & 1) return 4;
    return 0;
}

I64 CompileAndDebugFromArgs()
{
    Arena* arena = ArenaAlloc(Gb(32), 8, "Arena Main");
    defer (ArenaFree(arena));
    
    Reporter* reporter = ReporterAlloc(arena);
    Input* input = InputFromArgs(arena, reporter);
    
    RBuffer binary = YovCompile(arena, reporter, input->main_script_path);
    
    if (!input->settings.analyze_only) {
        RuntimeSettings settings = {};
        settings.user_assert = input->settings.user_assert;
        settings.no_user = input->settings.no_user;
        
        Runtime* runtime = RuntimeAlloc(binary, reporter, settings);
        RuntimeInitializeGlobals(runtime);
        
        if (!reporter->exit_requested)
        {
            RuntimeStart(runtime);
            
            while (1)
            {
                I32 press = StepPressed();
                
                B32 running = true;
                
                if (press == 1) running = RuntimeStepOver(runtime);
                else if (press == 2) running = RuntimeStepInto(runtime);
                else if (press == 3) running = RuntimeStepOut(runtime);
                else if (press == 4) {
                    RuntimeStepAll(runtime);
                    running = false;
                }
                else OsThreadSleep(10);
                
                if (!running) break;
            }
        }
        
        RuntimeFree(runtime);
    }
    
    ReporterPrint(reporter);
    
    if (input->settings.wait_end) {
        OsConsoleWait();
    }
    
    return reporter->exit_code;
}

#endif