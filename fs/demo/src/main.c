/* main.c源码 */

#define FUSE_USE_VERSION 26
#include <stdio.h>
#include <fuse.h>
#include "../include/ddriver.h"
#include <linux/fs.h>
#include <pwd.h>

#define DEMO_DEFAULT_PERM        0777


/* 超级块 */
struct demo_super
{
    int     driver_fd;  /* 模拟磁盘的fd */

    int     sz_io;      /* 磁盘IO大小，单位B */
    int     sz_disk;    /* 磁盘容量大小，单位B */
    int     sz_blks;    /* 逻辑块大小，单位B */
    int     max_blks;   /* 最多逻辑块数 */
};

struct demo_dentry
{
    char    fname[128];
}; 

struct demo_super super;

#define DEVICE_NAME "ddriver"

/* 挂载文件系统 */
static int demo_mount(){
    // 打开驱动
    char device_path[128] = {0};
    sprintf(device_path, "%s/" DEVICE_NAME, getpwuid(getuid())->pw_dir);
    super.driver_fd = ddriver_open(device_path);

    printf("super.driver_fd: %d\n", super.driver_fd);


    /* TODO: ioctl获取虚拟磁盘参数，并填充super信息 */
    super.sz_io = /* TODO */;
    super.sz_disk = /* TODO */;
    super.sz_blks = /* TODO */;
    super.max_blks = /* TODO */;

    return 0;
}

/* 卸载文件系统 */
static int demo_umount(){
    // 关闭驱动
    ddriver_close(super.driver_fd);
}

/* 遍历目录 */
static int demo_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi)
{
    char filename[128];
    /* TODO: 根据超级块的信息，从第500逻辑块读取一个dentry，ls将只固定显示这个文件名 */
    return filler(buf, filename, NULL, 0);
}

/* 显示文件属性 */
static int demo_getattr(const char* path, struct stat *stbuf)
{
    if(strcmp(path, "/") == 0)
        stbuf->st_mode = DEMO_DEFAULT_PERM | S_IFDIR;            // 根目录是目录文件
    else
        stbuf->st_mode = /* TODO: 显示为普通文件 */;            // 该文件显示为普通文件
    return 0;
}

/* 根据任务1需求 只用实现前四个钩子函数即可完成ls操作 */
static struct fuse_operations ops = {
	.init = demo_mount,						          /* mount文件系统 */		
	.destroy = demo_umount,							  /* umount文件系统 */
	.getattr = demo_getattr,							  /* 获取文件属性 */
	.readdir = demo_readdir,							  /* 填充dentrys */
};

int main(int argc, char *argv[])
{
    int ret = 0;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    ret = fuse_main(argc, argv, &ops, NULL);
    return ret;
}
