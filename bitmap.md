## 文件系统中的 bitmap 结构

### Q 文件系统中的 bitmap 结构是为了解决什么问题?

bitmap 用尽可能少的信息量存储磁盘数据的使用情况. 

### Q 如何表示?

每个 bit 对应一个 block 的占用情况.

### Q 效率如何?

元数据量/数据量 = bit / block

如果用1整个 block 作为位图,那么能表示的数据空间是 block 个 block.而每个 block 是 4KB, 故共 4K*4K = 16MB.


### Q ucore 是如何表示 bitmap 的?

类似内存分段寻址,每个bit index 的逻辑地址是 word + mask. mask 可以理解为偏移量的掩码表示.


```
bit_status = (*word) & mask
           = block_index/word_bits & mask
```

### Q ucore 实现 bitmap 工具的关键函数是什么?

block 索引到 word-mask的转换函数:

```C
static void
bitmap_translate(struct bitmap *bitmap, uint32_t index, WORD_TYPE **word, WORD_TYPE *mask);
```





