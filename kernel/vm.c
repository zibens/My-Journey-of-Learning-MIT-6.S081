#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable; //内核页表

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
//虽然etext和trampoline都是虚拟地址，但是因为执行它们的时候xv6的分页机制是关闭的，所以直接映射
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC 这个大小可以由 0x10000000 - 0x0C00000的带
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // map kernel stacks
  // 映射各个进程的内核栈到内核页表中
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
//其实在调用kvmmake之前在start.c中关闭了分页机制。
//那么什么时候再次打开分页呢，就在这个函数kvminithart里了。
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
/*用软件来模拟MMU查找页表的过程 返回一个pte*/
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  //虚拟地址超过最大地址
  if(va >= MAXVA)
    panic("walk");
  
 

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)]; //因为需要修改原pte并且提高效率所以采用指针
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte); //把这9位所对应的pte所对应的物理地址拿到
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0) //如果缺页的时候不允许提供页 或者 提供页失败，返回0
        return 0;
      memset(pagetable, 0, PGSIZE); // 否则的话， 提供一个页
      *pte = PA2PTE(pagetable) | PTE_V; //将物理地址转成PPN ， 在或一个标志位
    }
  }
  return &pagetable[PX(0, va)]; //返回最后一个9位地址所指向的pte
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);//pte转ppn 但此时还没有加上offset 不知道为什么
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
//内核页表中添加一个映射项 且此函数仅在启动时初始化内核页表时使用
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
//装载一个新的映射关系（可能不止一个页面大小）
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  //a是当前va对应的页
  //last存放的是最后一个应设置的页
  uint64 a, last;
  pte_t *pte;

//要映射的大小为0时，这是不合理的请求
  if(size == 0)
    panic("mappages: size");
  //a下取整 ， last上取整
  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    //设置PTE项，指向对应的物理内存页，并设置标志位permission
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
//取消用户进程页表中指定范围的映射关系 从虚拟地址va开始释放npages个页面
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;
  //va = newsize
  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V) //查找成功但是发现此时pte除了valid位外，其他位均为0
      panic("uvmunmap: not a leaf");//表示这个pte原本不应该出现在叶级页面
    if(do_free){ //如果do_free被置位，那么还要释放掉对应的物理内存
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
//与uvmdealloc函数回收进程的内存相反 ， 它位用户进程申请更多的内存
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz) //扩大内存 new 一定是 大于 old 的
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
//这个函数用来回收用户页表中的页面 ，将用户进程中已分配的空间大小从lodsz修改到newsz
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;
    //新地址应该比旧地址小

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){ //看两个地址中间差了多少个页
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
//专门来回收页表页的内存 ， 因为页表是多级的结构，所以此函数
//的实现用到了递归。在调用函数时应保证叶子级别的页表的映射关系全部解除
//并释放(由uvmunmap函数负责)  因为此函数专门用来回收页表页。
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    //如果有效位为1 且读位，写位，可执行位都是0 说明是一个而高级别pte
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
      // 如果有效位为1，且读位、写位、可执行位有一位为1
    // 表示这是一个叶级PTE，且未经释放，这不符合本函数调用条件，会陷入一个panic
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
//umvunmap用于取消叶级页表的映射关系 ， freewalk用于完全释放页表页，两个结合可以完全释放内存
//free就是将两者结合的一个封装 用于完全释放用户的地址空间
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
//为fork系统调用服务的，他会将父进程的整个地址空间全部复制到子进程中
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
//专门用来清除一个pte的用户使用权限
void
uvmclear(pagetable_t pagetable, uint64 va)//pte置为用户态不可访问
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
//将 src 这里的数据复制到 dstva 所在的物理地址空间  即 copy from kernel to user
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;//每次要复制的字节数确保不夸页复制  

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0); //这两步是为了拿到 当前页面的物理地址
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0); //首先计算出起始地址对于当前页面的偏移量 ， 然后计算起始地址对于页面结束的剩余字节数
    if(n > len) //为了复制不超过len个字节
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);//这个页面的物理地址+偏移量 ，src是要复制的地方的起始地址 ， 复制n个字节

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

void pagetableprint(pagetable_t pagetable , int depth)
{
  
  for(int i =0;i<512;++i){
    pte_t pte = pagetable[i]; //拿到pte 看看是否被使用了
    if((pte & PTE_V) == 0){ //无效的pte
      continue;
    }
    for(int i =0;i<=depth;++i){
      printf(" ..");
    }
    printf("%d: pte %p pa %p\n" , i ,(void*)pte ,(void*)PTE2PA(pte));
    //接着dfs找下一级页表 通过freewalk知道 页表只有 读 写 可执行位都是0 才是高级页表
    if((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0){
      //去指向下一级别页表
      uint64 child = PTE2PA(pte);
      pagetableprint((pagetable_t)child , depth + 1);
    }
  }

}
void vmprint(pagetable_t pagetable){
  printf("page table %p\n" , pagetable);

  pagetableprint(pagetable , 0);


  return;
}
