/* favorites_test: the fork flags byte of the favorites file survives pack / unpack and the VMS wrapper, and a file written before it existed reads
 * as 0. Host build (no console needed), from openMenu/src:
 *   gcc -Wall -Iopenmenu/src -Iopenmenu_shared/include -Iopenmenu_shared/src -o favorites_test openmenu/test/favorites_test.c && ./favorites_test */
#include "favorites.c" /* the module itself, to reach its static pack / unpack */
#include <stdio.h>
const gd_item* list_find_by_hash(unsigned int h){(void)h;return NULL;}
int main(void){
 uint8_t buf[8+4*FAV_MAX]; int bad=0;
 hash_count=2; hashes[0]=0x11223344; hashes[1]=0x55667788; fork_flags=FORK_FLAG_BEAT;
 int len=fav_pack(buf);
 hash_count=0; fork_flags=0;
 if(fav_unpack(buf,len)!=0||hash_count!=2||hashes[1]!=0x55667788||fork_flags!=FORK_FLAG_BEAT){printf("FAIL roundtrip\n");bad++;}
 /* a file from before the flags: byte 5 is 0 */
 buf[5]=0; fork_flags=9; if(fav_unpack(buf,len)!=0||fork_flags!=0){printf("FAIL old file\n");bad++;}
 /* the VMS wrapper keeps it too */
 static uint8_t file[VMS_DATA+8+4*FAV_MAX]; fork_flags=FORK_FLAG_BEAT; int sz=vms_wrap(file); fork_flags=0; hash_count=0;
 if(vms_unwrap(file,sz)!=0||fork_flags!=FORK_FLAG_BEAT){printf("FAIL vms\n");bad++;}
 printf(bad?"%d failed\n":"favorites flags ok\n",bad);return bad!=0;}
