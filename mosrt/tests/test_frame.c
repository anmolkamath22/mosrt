#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "frame.h"

static void test_frame_alloc_and_free(void) {
    frame_init();
    
    frame_stats_t stats = frame_get_stats();
    assert(stats.allocated_frames == 0);
    assert(stats.free_frames == VM_NUM_FRAMES);

    /* Allocate one frame */
    int f1 = frame_alloc(42, 5);
    assert(f1 >= 0);
    assert(f1 < (int)VM_NUM_FRAMES);
    
    stats = frame_get_stats();
    assert(stats.allocated_frames == 1);
    assert(stats.free_frames == VM_NUM_FRAMES - 1);

    int owner_pid;
    uint8_t owner_vpn;
    assert(frame_get_owner((uint8_t)f1, &owner_pid, &owner_vpn));
    assert(owner_pid == 42);
    assert(owner_vpn == 5);

    /* Allocate all frames */
    for (size_t i = 1; i < VM_NUM_FRAMES; ++i) {
        int f = frame_alloc(42, (uint8_t)i);
        assert(f >= 0);
    }

    /* Next allocation should fail (out of frames) */
    int f_fail = frame_alloc(42, 100);
    assert(f_fail == -1);

    /* Free first frame */
    frame_free((uint8_t)f1);
    stats = frame_get_stats();
    assert(stats.allocated_frames == VM_NUM_FRAMES - 1);

    /* Now we should be able to allocate one */
    int f_retry = frame_alloc(99, 200);
    assert(f_retry == f1); /* Should reuse the freed frame slot */

    frame_shutdown();
}

static void test_frame_read_write(void) {
    frame_init();

    int f = frame_alloc(1, 0);
    assert(f == 0);

    /* Test single byte write/read */
    frame_write_byte(0, 0x55);
    frame_write_byte(128, 0xAA);
    frame_write_byte(255, 0xFF);

    assert(frame_read_byte(0) == 0x55);
    assert(frame_read_byte(128) == 0xAA);
    assert(frame_read_byte(255) == 0xFF);

    /* Test block write/read */
    uint8_t write_block[VM_PAGE_SIZE];
    uint8_t read_block[VM_PAGE_SIZE];
    for (size_t i = 0; i < VM_PAGE_SIZE; ++i) {
        write_block[i] = (uint8_t)(i ^ 0xFF);
    }

    frame_write_block((uint8_t)f, write_block);
    frame_read_block((uint8_t)f, read_block);

    assert(memcmp(write_block, read_block, VM_PAGE_SIZE) == 0);

    frame_shutdown();
}

int main(void) {
    test_frame_alloc_and_free();
    test_frame_read_write();
    printf("test_frame passed\n");
    return 0;
}
