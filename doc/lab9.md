## lab

- Large files
  - 增加一个二级索引以增大一个文件的最大大小
  - 更改数据结构，修改 `bmap` 和 `itrunc` 以支持二级索引
- Symbolic links
  - 符号链接（或软链接）是指按路径名链接的文件；当一个符号链接打开时，内核跟随该链接指向引用的文件
  - 增加一个symlink系统调用，以实现符号链接（软链接）
  - 主要难点是阅读`sys_open` ,`sys_unlink` 等系统调用，学会一些api

在xv6用户态通过`bigfile` , `symlinktest`,  `usertests`  但通过不了`make grade`  