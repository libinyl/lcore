## ucore 中的表结构

## 1 基本管理器-链表

通常使用一个链表结构来维护多个同类型的数据.为了提高性能,可能会同时用哈希表维护数据.

```C
struct list_entry {
    struct list_entry *prev, *next;
};

typedef struct list_entry list_entry_t;
```

如果想让某种结构可以被某个管理节点维护,需要在结构中声明节点变量,如

```C
struct proc_struct {
    enum proc_state state;                      // Process state
    int pid;                                    // Process ID
    //省略...
    list_entry_t list_link;                     // Process link list
    list_entry_t hash_link;                     // Process hash list
}
```

最后形成的结构大概如图:

![](https://github.com/libinyl/ucore-study/blob/master/images/%E5%8F%8C%E5%90%91%E9%93%BE%E8%A1%A8%E7%A4%BA%E4%BE%8B.png?raw=1)

当遍历到某个节点后,如何对应到包含此节点的结构体?


```C
// proc.h
#define le2proc(le, member)\
    to_struct((le), struct proc_struct, member)

// def.h
#define to_struct(ptr, type, member)\
    ((type *)((char *)(ptr) - offsetof(type, member)))
```
基于如上定义,典型的查找用例如下.给定`link`地址/结构体类型/link 具体名称 即可找到对应结构体的地址.

```C
// usage
list_entry_t *le = list_next(&(rq->run_list));  // 获取管理节点(的地址)

if (le == &rq->run_list)
    return NULL;
     
struct proc_struct *p = le2proc(le, run_link);
```

原理是,只要给定节点(link)地址,在给定对应的结构体类型,就可以通过`offset`算出此节点在对应结构体内的偏移;而结构体内地址的layout是随成员变量增长的,于是**结构体地址=link成员地址-link成员在结构体内的偏移量**.

## 2 查找加速-哈希表

与链表类似,凡是需要用哈希表管理的,均需要在结构体内声明哈希节点,仍如`proc_struct`所示.

对于每种类型的`struct`,都应有专用的 hash 方法:

```C
// proc.c
#define HASH_SHIFT          10
#define HASH_LIST_SIZE      (1 << HASH_SHIFT)
#define pid_hashfn(x)       (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];
// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc) {
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}
// hash.c
/* 2^31 + 2^29 - 2^25 + 2^22 - 2^19 - 2^16 + 1 */
#define GOLDEN_RATIO_PRIME_32       0x9e370001UL

uint32_t
hash32(uint32_t val, unsigned int bits) {
    uint32_t hash = val * GOLDEN_RATIO_PRIME_32;
    return (hash >> (32 - bits));
}
```
以上是根据进程 id 进行hash 并维护的主要函数和宏定义.

经过哈希处理之后,每个哈希节点都称为表头,随元素增长分别延伸,如图.(注:理想情况下分布应较为均匀)

![](https://github.com/libinyl/ucore-study/blob/master/images/%E5%93%88%E5%B8%8C%E8%A1%A8.png?raw=1)

