#pragma once
#include <stdint.h>

void board_reset(void);
int board_read(void* ctx, int block, uint8_t* buf);
int board_write(void* ctx, int block, const uint8_t* buf);
void board_sleep(void* ctx, int ms);
int fw_constants_match(const int* cmds, int ncmds, const int* results, int nres);

int board_dst(int port, int src);
int board_map_size(int port);
int board_dev(int port);
int board_acc(int port);
void board_set_held(int port, uint32_t word0);
void board_set_present(int on);
int board_stat(int which); /* 0 commits, 1 reverts, 2 sleeps, 3 reads */
