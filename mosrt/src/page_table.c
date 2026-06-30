#include "page_table.h"
#include <string.h>

void page_table_init(page_table_t *pt) {
    if (pt == NULL)
        return;
    memset(pt->entries, 0, sizeof(pt->entries));
    for (uint32_t vpn = 0; vpn < VM_NUM_PAGES; ++vpn) {
        pt->entries[vpn].permissions = page_table_get_segment_default_permissions((uint8_t)vpn);
        pt->entries[vpn].present = false;
        pt->entries[vpn].in_swap = false;
    }
}

void page_table_map(page_table_t *pt, uint8_t vpn, uint8_t pfn, uint8_t permissions) {
    if (pt == NULL)
        return;
    pt->entries[vpn].frame_num = pfn;
    pt->entries[vpn].permissions = permissions;
    pt->entries[vpn].present = true;
    pt->entries[vpn].in_swap = false;
}

void page_table_unmap(page_table_t *pt, uint8_t vpn) {
    if (pt == NULL)
        return;
    pt->entries[vpn].present = false;
    pt->entries[vpn].frame_num = 0;
}

pte_t *page_table_lookup(page_table_t *pt, uint8_t vpn) {
    if (pt == NULL)
        return NULL;
    return &pt->entries[vpn];
}

const pte_t *page_table_lookup_const(const page_table_t *pt, uint8_t vpn) {
    if (pt == NULL)
        return NULL;
    return &pt->entries[vpn];
}

bool page_table_validate_permissions(const page_table_t *pt, uint8_t vpn, uint8_t access_type) {
    if (pt == NULL)
        return false;
    uint8_t permissions = pt->entries[vpn].permissions;
    return (permissions & access_type) == access_type;
}

const char *page_table_get_segment_name(uint8_t vpn) {
    /* Text: 0x0000 - 4KB (16 pages) -> vpn 0 to 15 */
    if (vpn < VM_SEG_TEXT_PAGES) {
        return "TEXT";
    }
    /* Rodata: 0x1000 - 4KB (16 pages) -> vpn 16 to 31 */
    if (vpn < VM_SEG_TEXT_PAGES + VM_SEG_RODATA_PAGES) {
        return "RODATA";
    }
    /* Data: 0x2000 - 4KB (16 pages) -> vpn 32 to 47 */
    if (vpn < VM_SEG_TEXT_PAGES + VM_SEG_RODATA_PAGES + VM_SEG_DATA_PAGES) {
        return "DATA";
    }
    /* BSS: 0x3000 - 4KB (16 pages) -> vpn 48 to 63 */
    if (vpn < VM_SEG_TEXT_PAGES + VM_SEG_RODATA_PAGES + VM_SEG_DATA_PAGES + VM_SEG_BSS_PAGES) {
        return "BSS";
    }

    /* Heap grows from 0x4000 (vpn 64) up to max 128 pages (up to vpn 191) */
    uint32_t heap_start_vpn =
        VM_SEG_TEXT_PAGES + VM_SEG_RODATA_PAGES + VM_SEG_DATA_PAGES + VM_SEG_BSS_PAGES;
    if (vpn >= heap_start_vpn && vpn < heap_start_vpn + VM_SEG_HEAP_MAX_PAGES) {
        return "HEAP";
    }

    /* Stack grows downwards from 0xFFFF (vpn 255) down to max 64 pages (vpn 192 to 255) */
    if (vpn >= VM_NUM_PAGES - VM_SEG_STACK_MAX_PAGES) {
        return "STACK";
    }

    return "UNKNOWN";
}

uint8_t page_table_get_segment_default_permissions(uint8_t vpn) {
    const char *seg = page_table_get_segment_name(vpn);
    if (strcmp(seg, "TEXT") == 0) {
        return VM_PROT_READ | VM_PROT_EXEC;
    }
    if (strcmp(seg, "RODATA") == 0) {
        return VM_PROT_READ;
    }
    if (strcmp(seg, "DATA") == 0 || strcmp(seg, "BSS") == 0 || strcmp(seg, "HEAP") == 0 ||
        strcmp(seg, "STACK") == 0) {
        return VM_PROT_READ | VM_PROT_WRITE;
    }
    return 0;
}

void page_table_dump(const page_table_t *pt, FILE *out) {
    if (pt == NULL || out == NULL)
        return;
    fprintf(out, "%-5s %-8s %-5s %-7s %-5s %-5s %-5s\n", "VPN", "Segment", "PFN", "Present",
            "Dirty", "Access", "Swap");
    for (uint32_t vpn = 0; vpn < VM_NUM_PAGES; ++vpn) {
        const pte_t *e = &pt->entries[vpn];
        if (e->present || e->in_swap) {
            fprintf(out, "0x%02X  %-8s 0x%02X  %-7s %-5s %-5s %-5s (slot:%d)\n", vpn,
                    page_table_get_segment_name((uint8_t)vpn), e->frame_num,
                    e->present ? "yes" : "no", e->dirty ? "yes" : "no", e->accessed ? "yes" : "no",
                    e->in_swap ? "yes" : "no", e->swap_slot);
        }
    }
}
