
//- ARRAY 

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
inline_fn PooledArrayIterator<T> pooled_array_make_iterator(PooledArray<T>* array)
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
inline_fn PooledArrayIterator<T> pooled_array_make_iterator_tail(PooledArray<T>* array)
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

//- LINKED LIST

template<typename T>
inline_fn LinkedList<T> ll_make(Arena* arena) {
    LinkedList<T> ll{};
    ll.arena = arena;
    return ll;
}

template<typename T>
inline_fn T* ll_push(LinkedList<T>* ll)
{
    LLNode* node = (LLNode*)arena_push(ll->arena, sizeof(LLNode) + sizeof(T));
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
inline_fn T* ll_push(LinkedList<T>* ll, const T& data)
{
    T* v = ll_push(ll);
    *v = data;
    return v;
}

template<typename T>
inline_fn void* ll_push_back(LinkedList<T>* ll)
{
    assert(ll->stride > 0);
    LLNode* node = (LLNode*)arena_push(ll->arena, sizeof(LLNode) + sizeof(T));
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
