#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "solar_os_ramfs_portable.h"
#include "solar_os_memory.h"
static unsigned allocations;
static bool fail_alloc;
void *solar_os_memory_alloc(size_t n,solar_os_memory_class_t c,const char *tag) { (void)tag;assert(c==SOLAR_OS_MEMORY_EXTERNAL_REQUIRED);if(fail_alloc)return NULL;void *p=malloc(n);if(p)++allocations;return p; }
void solar_os_memory_free(void *p) { if(p){assert(allocations);--allocations;free(p);} }
void solar_os_memory_get_status(solar_os_memory_status_t *s) {memset(s,0,sizeof(*s));s->external.total=s->external.free=8*1024*1024;}
size_t strlcpy(char *d,const char *s,size_t n){size_t len=strlen(s);if(n){size_t copy=len<n-1?len:n-1;memcpy(d,s,copy);d[copy]=0;}return len;}
int main(void) {
    const solar_os_ramfs_ops_t *o=solar_os_ramfs_ops();
    const char *bad[]={"/","/sd","/flash","/usb","/sd/x","/a/..","/.","//x","relative","/a b"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(solar_os_ramfs_mount(bad[i],4096)!=ESP_OK);
    fail_alloc=true;assert(solar_os_ramfs_mount("/ram",4096)==ESP_ERR_NO_MEM);fail_alloc=false;
    assert(solar_os_ramfs_mount_count()==0 && allocations==0);
    for(unsigned cycle=0;cycle<20;++cycle) {
        assert(solar_os_ramfs_mount("/ram",4096)==ESP_OK);
        assert(solar_os_ramfs_mount("/ram",4096)==ESP_ERR_INVALID_STATE);
        void *c=solar_os_ramfs_context("/ram");assert(c && !solar_os_ramfs_context("/ramble"));
        int f=o->open_p(c,"/hello",O_CREAT|O_RDWR,0666);assert(f>=0);
        assert(o->write_p(c,f,"abc",3)==3);
        assert(o->lseek_p(c,f,8,SEEK_SET)==8);assert(o->write_p(c,f,"z",1)==1);
        assert(o->lseek_p(c,f,0,SEEK_SET)==0);char data[16]={0};assert(o->read_p(c,f,data,16)==9);
        assert(!memcmp(data,"abc\0\0\0\0\0z",9));
        assert(solar_os_ramfs_unmount("/ram")==ESP_ERR_INVALID_STATE);
        assert(o->unlink_p(c,"/hello")==-1 && errno==EBUSY);
        assert(o->close_p(c,f)==0);
        f=o->open_p(c,"/hello",O_WRONLY|O_APPEND,0);assert(f>=0);assert(o->write_p(c,f,"!",1)==1);assert(o->close_p(c,f)==0);
        assert(o->open_p(c,"/hello",O_CREAT|O_EXCL|O_RDWR,0)==-1 && errno==EEXIST);
        int handles[16];
        for(int i=0;i<16;++i){handles[i]=o->open_p(c,"/hello",O_RDONLY,0);assert(handles[i]>=0);}
        assert(o->open_p(c,"/hello",O_WRONLY|O_TRUNC,0)==-1 && errno==EMFILE);
        struct stat preserved;assert(o->fstat_p(c,handles[0],&preserved)==0 && preserved.st_size==10);
        assert(o->ftruncate_p(c,handles[0],0)==-1 && errno==EBADF);
        for(int i=0;i<16;++i)assert(o->close_p(c,handles[i])==0);
        DIR *d=o->opendir_p(c,"/");assert(d);
        solar_os_ramfs_info_t info;assert(solar_os_ramfs_get_info(0,&info) && info.open_count==1);
        assert(solar_os_ramfs_unmount("/ram")==ESP_ERR_INVALID_STATE);
        assert(o->unlink_p(c,"/hello")==-1 && errno==EBUSY);
        assert(o->rename_p(c,"/hello","/other")==-1 && errno==EBUSY);
        assert(!strcmp(o->readdir_p(c,d)->d_name,"hello"));assert(!o->readdir_p(c,d));assert(o->closedir_p(c,d)==0);
        assert(o->mkdir_p(c,"/dir",0777)==0);assert(o->mkdir_p(c,"/dir/sub",0777)==0);
        assert(o->rename_p(c,"/dir","/dir/sub/cycle")==-1 && errno==EINVAL);
        assert(o->rmdir_p(c,"/dir")==-1 && errno==ENOTEMPTY);
        assert(o->rename_p(c,"/hello","/dir/renamed")==0);
        assert(o->unlink_p(c,"/dir/renamed")==0);
        assert(o->rmdir_p(c,"/dir/sub")==0);assert(o->rmdir_p(c,"/dir")==0);
        f=o->open_p(c,"/full",O_CREAT|O_RDWR,0666);assert(f>=0);
        char block[512];memset(block,'Q',sizeof(block));int writes=0;
        while(o->write_p(c,f,block,sizeof(block))==sizeof(block))assert(++writes<8);
        assert(errno==ENOSPC && writes>0);
        struct stat st;assert(o->fstat_p(c,f,&st)==0 && st.st_size==writes*512);
        assert(o->lseek_p(c,f,0,SEEK_SET)==0);assert(o->read_p(c,f,data,sizeof(data))==sizeof(data));assert(data[0]=='Q');
        assert(o->close_p(c,f)==0);assert(o->unlink_p(c,"/full")==0);
        assert(solar_os_ramfs_get_info(0,&info) && info.used_bytes==0 && info.open_count==0 && info.file_count==0);
        assert(solar_os_ramfs_unmount("/ram")==ESP_OK);assert(allocations==0 && solar_os_ramfs_mount_count()==0);
    }
    for(int i=0;i<4;++i){char name[20];snprintf(name,sizeof(name),"/ram%d",i);assert(solar_os_ramfs_mount(name,4096)==ESP_OK);}
    assert(solar_os_ramfs_mount("/fifth",4096)==ESP_ERR_NO_MEM);
    for(int i=0;i<4;++i){char name[20];snprintf(name,sizeof(name),"/ram%d",i);assert(solar_os_ramfs_unmount(name)==ESP_OK);}
    assert(!allocations);puts("PASS: RAMFS quotas, files, append/seek, directories, busy unmount, invalid mounts, rename cycles, ENOSPC integrity and exact allocation recovery");
}
