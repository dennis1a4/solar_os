#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "flash_policy.h"
static unsigned char media[16][4096], saved[sizeof(media)];
static unsigned writes, erases;
static int fail_read=-1;
static int read_block(const struct lfs_config *c,lfs_block_t b,lfs_off_t o,void *data,lfs_size_t n) {
    (void)c; assert(b<16 && o+n<=4096);
    if ((int)b==fail_read) return LFS_ERR_IO;
    memcpy(data,media[b]+o,n); return 0;
}
static int program(const struct lfs_config *c,lfs_block_t b,lfs_off_t o,const void *data,lfs_size_t n) {
    (void)c; assert(b<16 && o+n<=4096); ++writes;
    const unsigned char *bytes=data;
    for(unsigned i=0;i<n;++i) { assert((media[b][o+i]&bytes[i])==bytes[i]); media[b][o+i]&=bytes[i]; }
    return 0;
}
static int erase_block(const struct lfs_config *c,lfs_block_t b) {
    (void)c; assert(b<16); ++erases; memset(media[b],0xff,4096); return 0;
}
static int sync_media(const struct lfs_config *c) { (void)c; return 0; }
int main(void) {
    struct lfs_config config={.read=read_block,.prog=program,.erase=erase_block,.sync=sync_media,
        .read_size=16,.prog_size=16,.block_size=4096,.block_count=16,.block_cycles=400,
        .cache_size=256,.lookahead_size=16};
    lfs_t fs={0}; memset(media,0xff,sizeof(media));
    // Reproduce begin() fallback: a failed mount must NEVER auto-format.
    assert(lfs_mount(&fs,&config)<0);
    assert(lfs_format(&fs,&config)==LFS_ERR_INVAL);
    assert(writes==0 && erases==0);
    media[15][4095]=0; // Data beyond the superblocks must also prevent init.
    memcpy(saved,media,sizeof(media));
    assert(sk_flash_initialize_blank(&fs,&config,NULL)==LFS_ERR_EXIST);
    assert(!memcmp(saved,media,sizeof(media)) && writes==0 && erases==0);
    memset(media,0xff,sizeof(media)); fail_read=5;
    assert(sk_flash_initialize_blank(&fs,&config,NULL)==LFS_ERR_IO);
    assert(writes==0 && erases==0); fail_read=-1;
    assert(sk_flash_initialize_blank(&fs,&config,NULL)==0);
    assert(writes>0 && erases>0);
    assert(lfs_mount(&fs,&config)==0);
    lfs_file_t file;
    assert(lfs_file_open(&fs,&file,"/keep.txt",LFS_O_WRONLY|LFS_O_CREAT|LFS_O_EXCL)==0);
    assert(lfs_file_write(&fs,&file,"preserve me",11)==11);
    assert(lfs_file_close(&fs,&file)==0);
    assert(lfs_unmount(&fs)==0);
    unsigned before_writes=writes,before_erases=erases;
    memcpy(saved,media,sizeof(media));
    assert(sk_flash_initialize_blank(&fs,&config,NULL)==LFS_ERR_EXIST);
    assert(!memcmp(saved,media,sizeof(media)) && writes==before_writes && erases==before_erases);
    assert(lfs_mount(&fs,&config)==0);
    assert(lfs_file_open(&fs,&file,"/keep.txt",LFS_O_RDONLY)==0);
    char data[12]={0}; assert(lfs_file_read(&fs,&file,data,11)==11); assert(!strcmp(data,"preserve me"));
    assert(lfs_file_close(&fs,&file)==0); assert(lfs_unmount(&fs)==0);
    puts("PASS: no automatic format, full-chip blank check, read-failure refusal, initialization and persistence");
}
