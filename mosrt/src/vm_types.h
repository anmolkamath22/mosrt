#ifndef MOSRT_VM_TYPES_H
#define MOSRT_VM_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- Virtual Memory Parameters --- */
#define VM_PAGE_SIZE 256U /* 256 bytes per page */
#define VM_PAGE_MASK (VM_PAGE_SIZE - 1U)
#define VM_PAGE_SHIFT 8U

#define VM_VIRT_ADDR_SPACE_SIZE 65536U /* 16-bit virtual address space (64KB) */
#define VM_NUM_PAGES (VM_VIRT_ADDR_SPACE_SIZE / VM_PAGE_SIZE) /* 256 pages */

/* --- Physical Memory Parameters --- */
#define VM_PHYS_MEM_SIZE 16384U                         /* 16KB physical memory (RAM) */
#define VM_NUM_FRAMES (VM_PHYS_MEM_SIZE / VM_PAGE_SIZE) /* 64 physical frames */

/* --- Swap Space Parameters --- */
#define VM_SWAP_SIZE 65536U                             /* 64KB swap space */
#define VM_NUM_SWAP_SLOTS (VM_SWAP_SIZE / VM_PAGE_SIZE) /* 256 swap slots */

/* --- TLB Parameters --- */
#define VM_TLB_SIZE 16 /* 16 TLB entries */

/* --- Simulated Disk Latency --- */
#define VM_PAGE_FAULT_LATENCY 5U /* Ticks to resolve a page fault from swap */

/* --- Segment Definitions --- */
#define VM_SEG_TEXT_START 0x0000U
#define VM_SEG_TEXT_PAGES 16U /* 4KB */

#define VM_SEG_RODATA_START 0x1000U
#define VM_SEG_RODATA_PAGES 16U /* 4KB */

#define VM_SEG_DATA_START 0x2000U
#define VM_SEG_DATA_PAGES 16U /* 4KB */

#define VM_SEG_BSS_START 0x3000U
#define VM_SEG_BSS_PAGES 16U /* 4KB */

#define VM_SEG_HEAP_START 0x4000U
#define VM_SEG_HEAP_MAX_PAGES 128U /* 32KB max heap */

#define VM_SEG_STACK_START 0xFFFFU /* Grows downward */
#define VM_SEG_STACK_MAX_PAGES 64U /* 16KB max stack */

/* --- Protection Bits --- */
#define VM_PROT_READ 0x01U
#define VM_PROT_WRITE 0x02U
#define VM_PROT_EXEC 0x04U

/* --- Page Table Entry --- */
typedef struct {
    uint8_t frame_num;
    uint8_t permissions;
    bool present;
    bool dirty;
    bool accessed;
    bool referenced;             /* For Clock replacement algorithm */
    uint32_t last_accessed_tick; /* For LRU replacement algorithm */
    bool in_swap;
    uint16_t swap_slot;
} pte_t;

/* --- TLB Entry --- */
typedef struct {
    uint8_t vpn;
    uint8_t pfn;
    uint8_t permissions;
    bool valid;
    bool dirty;
    uint32_t last_used_tick;
} tlb_entry_t;

/* --- Simulated Heap Allocator Block Header --- */
typedef struct vm_block_hdr {
    uint16_t size; /* Size of user payload (block size without header) */
    bool free;
    uint16_t next; /* Offset-based pointers (16-bit) to avoid absolute pointers */
    uint16_t prev; /* Offset-based pointers (16-bit) */
} vm_block_hdr_t;

#define VM_BLOCK_HDR_SIZE (sizeof(vm_block_hdr_t))

#endif /* MOSRT_VM_TYPES_H */
