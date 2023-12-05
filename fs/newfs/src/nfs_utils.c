#include "../include/nfs.h"

extern struct nfs_super nfs_super;
extern struct custom_options nfs_options;


/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* nfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}
/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 
 * @return int 
 */
int nfs_calc_lvl(const char * path) {
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}

// 磁盘交互的封装，方便读写任意长度的数据
/**
 * @brief 驱动读
 *
 * @param offset
 * @param out_content
 * @param size
 * @return int
 */
int nfs_driver_read(int offset, uint8_t *out_content, int size)
{
   int offset_aligned = NFS_ROUND_DOWN(offset, NFS_IO_SZ());    // 下限对齐
   int bias = offset - offset_aligned;                          // 下限多出来的那部分
   int size_aligned = NFS_ROUND_UP((size + bias), NFS_IO_SZ()); // 上限对齐
   uint8_t *temp_content = (uint8_t *)malloc(size_aligned);     // 足够大的一片空间
   uint8_t *cur = temp_content;
   ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);

   // 这里的判断函数不是很精准，可能读多了很多，但是能确保读对
   while (size_aligned != 0)
   {
      ddriver_read(NFS_DRIVER(), cur, NFS_IO_SZ());
      cur += NFS_IO_SZ();
      size_aligned -= NFS_IO_SZ();
   }
   memcpy(out_content, temp_content + bias, size);
   free(temp_content);
   return NFS_ERROR_NONE;
}
/**
 * @brief 驱动写
 *
 * @param offset
 * @param in_content
 * @param size
 * @return int
 */
int nfs_driver_write(int offset, uint8_t *in_content, int size)
{
   int offset_aligned = NFS_ROUND_DOWN(offset, NFS_IO_SZ());
   int bias = offset - offset_aligned;
   int size_aligned = NFS_ROUND_UP((size + bias), NFS_IO_SZ());
   uint8_t *temp_content = (uint8_t *)malloc(size_aligned);
   uint8_t *cur = temp_content;

   // 把上下对齐的空间内的内容读到temp中
   nfs_driver_read(offset_aligned, temp_content, size_aligned);

   // 把要写入的内容拷贝在temp + bias上，也就是说，不改变bias以前的值，只改变bias以后的值
   memcpy(temp_content + bias, in_content, size);

   // 这个时候，temp中的值直接全部写回就是正确的了

   ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
   while (size_aligned != 0)
   {
      ddriver_write(NFS_DRIVER(), cur, NFS_IO_SZ());
      cur += NFS_IO_SZ();
      size_aligned -= NFS_IO_SZ();
   }

   free(temp_content);
   return NFS_ERROR_NONE;
}

/**
 * @brief 为一个inode分配dentry，采用头插法
 *
 * @param inode
 * @param dentry
 * @return int
 */
int nfs_alloc_dentry(struct nfs_inode *inode, struct nfs_dentry *dentry)
{
   if (inode->dentrys == NULL)
   {
      inode->dentrys = dentry;
   }
   else
   {
      dentry->brother = inode->dentrys;
      inode->dentrys = dentry;
   }
   inode->dir_cnt++;
   // 插入时size增加
   inode->size += sizeof(struct nfs_dentry);
   return inode->dir_cnt;
}
/**
 * @brief 将dentry从inode的dentrys中取出
 *
 * @param inode
 * @param dentry
 * @return int
 */
int nfs_drop_dentry(struct nfs_inode *inode, struct nfs_dentry *dentry)
{
   boolean is_find = FALSE;
   struct nfs_dentry *dentry_cursor;
   dentry_cursor = inode->dentrys;

   if (dentry_cursor == dentry)
   {
      inode->dentrys = dentry->brother;
      is_find = TRUE;
   }
   else
   {
      while (dentry_cursor)
      {
         if (dentry_cursor->brother == dentry)
         {
            dentry_cursor->brother = dentry->brother;
            is_find = TRUE;
            break;
         }
         dentry_cursor = dentry_cursor->brother;
      }
   }
   if (!is_find)
   {
      return -NFS_ERROR_NOTFOUND;
   }
   inode->dir_cnt--;
   return inode->dir_cnt;
}
/**
 * @brief 分配一个inode，占用位图
 *
 * @param dentry 该dentry指向分配的inode
 * @return nfs_inode
 */
struct nfs_inode *nfs_alloc_inode(struct nfs_dentry *dentry)
{
   struct nfs_inode *inode;
   int byte_cursor = 0;
   int bit_cursor = 0;
   int ino_cursor = 0;
   
   boolean is_find_free_entry = FALSE;

   for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_inode_blks);
        byte_cursor++)
   {
      for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
      {
         if ((nfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0)
         {
            /* 当前ino_cursor位置空闲 */
            nfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
            is_find_free_entry = TRUE;
            // printf("--------------------------------ino_cursor = %d -----------------------\n", ino_cursor);
            break;
         }
         ino_cursor++;
      }
      if (is_find_free_entry)
      {
         break;
      }
   }

   // 位图满了，分配不了
   if (!is_find_free_entry || ino_cursor == nfs_super.max_ino)
      return -NFS_ERROR_NOSPACE;

   // 根据ino_cursor分配新的inode
   inode = (struct nfs_inode *)malloc(sizeof(struct nfs_inode));
   inode->ino = ino_cursor;
   inode->size = 0;
   /* dentry指向inode */
   dentry->inode = inode;
   dentry->ino = inode->ino;
   /* inode指回dentry */
   inode->dentry = dentry;

   inode->dir_cnt = 0;
   inode->dentrys = NULL;

   // if (NFS_IS_REG(inode))
   // {
      for (int i = 0; i < NFS_DATA_PER_FILE; i++) {
         inode->data[i] = (uint8_t *)malloc(NFS_BLK_SZ());
         inode->blk_no[i] = -1;
      }
   // }

   return inode;
}

/**
 * @brief 分配一个data，占用位图
 *
 * @param dentry 该dentry指向分配的data
 */
int nfs_alloc_datablk(struct nfs_dentry *dentry)
{
   struct nfs_inode *inode;
   int byte_cursor = 0;
   int bit_cursor = 0;
   int data_cursor = 0;
   boolean is_find_free_entry = FALSE;

   for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_data_blks);
        byte_cursor++)
   {
      for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
      {
         if ((nfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0)
         {
            /* 当前data_cursor位置空闲 */
            nfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);
            is_find_free_entry = TRUE;
            break;
         }
         data_cursor++;
      }
      if (is_find_free_entry)
      {
         break;
      }
   }

   // 位图满了，分配不了
   if (!is_find_free_entry || data_cursor == nfs_super.data_blks){
      printf("位图满了，分配不了");
      return -NFS_ERROR_NOSPACE;
   }
      

   // 根据data_cursor分配新的数据块
   inode = dentry->inode;
   
   // 找到没有索引的指针，赋值索引
   int i = 0;
   for (; i < NFS_DATA_PER_FILE; i++) {
      if(inode->blk_no[i] == -1){
         inode->blk_no[i] = data_cursor;
         break;
      }
         
   }
   
   // 最多只能有6个数据块
   if(i == NFS_DATA_PER_FILE){
      printf("最多只能有6个数据块");
      return -NFS_ERROR_NOSPACE;
   }

   return 0;
   
}


/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 *
 * @param inode
 * @return int
 */
int nfs_sync_inode(struct nfs_inode *inode)
{
   struct nfs_inode_d inode_d;
   struct nfs_dentry *dentry_cursor;
   struct nfs_dentry_d dentry_d;
   int ino = inode->ino;

   // 写回inode
   inode_d.ino = ino;
   inode_d.size = inode->size;
   memcpy(inode_d.target_path, inode->target_path, NFS_MAX_FILE_NAME);
   inode_d.ftype = inode->dentry->ftype;
   inode_d.dir_cnt = inode->dir_cnt;
   for (int i = 0; i< NFS_DATA_PER_FILE; i++) {
      inode_d.blk_no[i] = inode->blk_no[i];
   }

   if (nfs_driver_write(NFS_INO_OFS(ino), (uint8_t *)&inode_d,
                        sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE)
   {
      NFS_DBG("[%s] io error\n", __func__);
      return -NFS_ERROR_IO;
   }

   /* Cycle 1: 写 INODE */
   /* Cycle 2: 写 数据 */
   if (NFS_IS_DIR(inode))
   {
      // 是目录，要把所有的子目录项写回
      dentry_cursor = inode->dentrys;

      int offset;
      int offset_limit;

      for (int i = 0; i < NFS_DATA_PER_FILE; i++) {
         offset = NFS_DATA_OFS(inode->blk_no[i]);
         offset_limit = NFS_DATA_OFS(inode->blk_no[i] + 1);
         while (dentry_cursor != NULL && offset + sizeof(struct nfs_dentry_d) < offset_limit)
         {
            // 写回dentry
            memcpy(dentry_d.fname, dentry_cursor->fname, NFS_MAX_FILE_NAME);
            dentry_d.ftype = dentry_cursor->ftype;
            dentry_d.ino = dentry_cursor->ino;
            if (nfs_driver_write(offset, (uint8_t *)&dentry_d,
                                 sizeof(struct nfs_dentry_d)) != NFS_ERROR_NONE)
            {
               NFS_DBG("[%s] io error\n", __func__);
               return -NFS_ERROR_IO;
            }

            // 递归调用
            if (dentry_cursor->inode != NULL)
            {
               nfs_sync_inode(dentry_cursor->inode);
            }

            dentry_cursor = dentry_cursor->brother;
            offset += sizeof(struct nfs_dentry_d);
         }
      }
      
   }
   else if (NFS_IS_REG(inode))
   {
      for (int j = 0; j < NFS_DATA_PER_FILE; j++) {
         if (inode->blk_no[j] != -1) {
            if (nfs_driver_write(NFS_DATA_OFS(inode->blk_no[j]), (uint8_t *)inode->data[j],
                          NFS_BLK_SZ()) != NFS_ERROR_NONE)
            {
               NFS_DBG("[%s] io error\n", __func__);
               return -NFS_ERROR_IO;
            }
         }
      }
   }
   return NFS_ERROR_NONE;
}

/**
 * @brief 删除内存中的一个inode， 暂时不释放
 * Case 1: Reg File
 *
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Reg Dentry)
 *                       |
 *                      Inode  (Reg File)
 *
 *  1) Step 1. Erase Bitmap
 *  2) Step 2. Free Inode                      (Function of nfs_drop_inode)
 * ------------------------------------------------------------------------
 *  3) *Setp 3. Free Dentry belonging to Inode (Outsider)
 * ========================================================================
 * Case 2: Dir
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Dir Dentry)
 *                       |
 *                      Inode  (Dir)
 *                    /     \
 *                Dentry -> Dentry
 *
 *   Recursive
 * @param inode
 * @return int
 */
int nfs_drop_inode(struct nfs_inode *inode)
{
   struct nfs_dentry *dentry_cursor;
   struct nfs_dentry *dentry_to_free;
   struct nfs_inode *inode_cursor;

   int byte_cursor = 0;
   int bit_cursor = 0;
   int ino_cursor = 0;
   boolean is_find = FALSE;

   if (inode == nfs_super.root_dentry->inode)
   {
      return NFS_ERROR_INVAL;
   }

   if (NFS_IS_DIR(inode))
   {
      dentry_cursor = inode->dentrys;
      /* 递归向下drop */
      while (dentry_cursor)
      {
         inode_cursor = dentry_cursor->inode;
         nfs_drop_inode(inode_cursor);
         nfs_drop_dentry(inode, dentry_cursor);
         dentry_to_free = dentry_cursor;
         dentry_cursor = dentry_cursor->brother;
         free(dentry_to_free);
      }
   }
   else if (NFS_IS_REG(inode) || NFS_IS_SYM_LINK(inode))
   {
      for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_inode_blks);
           byte_cursor++) /* 调整inodemap */
      {
         for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
         {
            if (ino_cursor == inode->ino)
            {
               nfs_super.map_inode[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
               is_find = TRUE;
               break;
            }
            ino_cursor++;
         }
         if (is_find == TRUE)
         {
            break;
         }
      }
      for (int i = 0; i < NFS_DATA_PER_FILE; i++) {    
         free(inode->data[i]);
      }
      free(inode);
   }
   return NFS_ERROR_NONE;
}
/**
 * @brief
 *
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct nfs_inode*
 */
struct nfs_inode *nfs_read_inode(struct nfs_dentry *dentry, int ino)
{
   struct nfs_inode *inode = (struct nfs_inode *)malloc(sizeof(struct nfs_inode));
   struct nfs_inode_d inode_d;
   struct nfs_dentry *sub_dentry;
   struct nfs_dentry_d dentry_d;
   int dir_cnt = 0;
   if (nfs_driver_read(NFS_INO_OFS(ino), (uint8_t *)&inode_d,
                       sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE)
   {
      NFS_DBG("[%s] io error\n", __func__);
      return NULL;
   }
   inode->dir_cnt = 0;
   inode->ino = inode_d.ino;
   inode->size = inode_d.size;
   memcpy(inode->target_path, inode_d.target_path, NFS_MAX_FILE_NAME);
   inode->dentry = dentry;
   inode->dentrys = NULL;
   for (int j = 0; j < NFS_DATA_PER_FILE; j++) {
      inode->blk_no[j] = inode_d.blk_no[j];
   }
   
   if (NFS_IS_DIR(inode))
   {
      dir_cnt = inode_d.dir_cnt;
      int offset, offset_limit;
      for (int j = 0; j < NFS_DATA_PER_FILE; j++) {
         if(dir_cnt == 0) break;

         offset = NFS_DATA_OFS(inode->blk_no[j]);  
         offset_limit = NFS_DATA_OFS(inode->blk_no[j] + 1);
         while(dir_cnt > 0 && offset + sizeof(struct nfs_dentry_d) < offset_limit){

            if (nfs_driver_read(offset, (uint8_t *)&dentry_d,
                              sizeof(struct nfs_dentry_d)) != NFS_ERROR_NONE)
            {
               NFS_DBG("[%s] io error\n", __func__);
               return NULL;
            }

            sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino = dentry_d.ino;
            nfs_alloc_dentry(inode, sub_dentry);
                
            offset += sizeof(struct nfs_dentry_d);
            dir_cnt--;
         }
      }
      
   }
   else if (NFS_IS_REG(inode))
   {
      for (int j = 0; j < NFS_DATA_PER_FILE; j++) {
         inode->data[j] = (uint8_t *)malloc(NFS_BLK_SZ());

         if (nfs_driver_read(NFS_DATA_OFS(inode->blk_no[j]), (uint8_t *)inode->data[j],
                        NFS_BLK_SZ()) != NFS_ERROR_NONE)
         {
            NFS_DBG("[%s] io error\n", __func__);
            return NULL;
         }
         
      }
   }
   return inode;
}
/**
 * @brief
 *
 * @param inode
 * @param dir [0...]
 * @return struct nfs_dentry*
 */
struct nfs_dentry *nfs_get_dentry(struct nfs_inode *inode, int dir)
{
   struct nfs_dentry *dentry_cursor = inode->dentrys;
   int cnt = 0;
   while (dentry_cursor)
   {
      if (dir == cnt)
      {
         return dentry_cursor;
      }
      cnt++;
      dentry_cursor = dentry_cursor->brother;
   }
   return NULL;
}
/**
 * @brief
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 *
 * @param path
 * @return struct nfs_inode*
 */
struct nfs_dentry *nfs_lookup(const char *path, boolean *is_find, boolean *is_root)
{
   struct nfs_dentry *dentry_cursor = nfs_super.root_dentry;
   struct nfs_dentry *dentry_ret = NULL;
   struct nfs_inode *inode;
   int total_lvl = nfs_calc_lvl(path);
   int lvl = 0;
   boolean is_hit;
   char *fname = NULL;
   char *path_cpy = (char *)malloc(sizeof(path));
   *is_root = FALSE;
   strcpy(path_cpy, path);

   if (total_lvl == 0)
   { /* 根目录 */
      *is_find = TRUE;
      *is_root = TRUE;
      dentry_ret = nfs_super.root_dentry;
   }
   fname = strtok(path_cpy, "/");
   while (fname)
   {
      lvl++;
      if (dentry_cursor->inode == NULL)
      { /* Cache机制 */
         nfs_read_inode(dentry_cursor, dentry_cursor->ino);
      }

      inode = dentry_cursor->inode;

      if (NFS_IS_REG(inode) && lvl < total_lvl)
      {
         NFS_DBG("[%s] not a dir\n", __func__);
         dentry_ret = inode->dentry;
         break;
      }
      if (NFS_IS_DIR(inode))
      {
         dentry_cursor = inode->dentrys;
         is_hit = FALSE;

         while (dentry_cursor)
         {
            if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0)
            {
               is_hit = TRUE;
               break;
            }
            dentry_cursor = dentry_cursor->brother;
         }

         if (!is_hit)
         {
            *is_find = FALSE;
            NFS_DBG("[%s] not found %s\n", __func__, fname);
            dentry_ret = inode->dentry;
            break;
         }

         if (is_hit && lvl == total_lvl)
         {
            *is_find = TRUE;
            dentry_ret = dentry_cursor;
            break;
         }
      }
      fname = strtok(NULL, "/");
   }

   if (dentry_ret->inode == NULL)
   {
      dentry_ret->inode = nfs_read_inode(dentry_ret, dentry_ret->ino);
   }

   return dentry_ret;
}

/**
 * @brief 挂载nfs, Layout 如下
 *
 * Layout
 * | Super | Inode Map | Data Map | Inode | Data
 *
 * BLK_SZ = 2 * IO_SZ
 *
 * 每个Inode占用一个Blk
 * @param options
 * @return int
 */
int nfs_mount(struct custom_options options)
{
   int ret = NFS_ERROR_NONE;
   int driver_fd;
   struct nfs_super_d nfs_super_d;
   struct nfs_dentry *root_dentry;
   struct nfs_inode *root_inode;

   boolean is_init = FALSE;

   nfs_super.is_mounted = FALSE;

   driver_fd = ddriver_open(options.device);

   // 如果打不开，返回报错
   if (driver_fd < 0)
   {
      return driver_fd;
   }

   nfs_super.driver_fd = driver_fd;
   ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_SIZE, &nfs_super.sz_disk);
   ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &nfs_super.sz_io);
   nfs_super.sz_blk = 2 * nfs_super.sz_io;
   nfs_super.num_blk = NFS_DISK_SZ() / nfs_super.sz_blk;

   // 从磁盘中读出来超级块
   if (nfs_driver_read(NFS_SUPER_OFS, (uint8_t *)(&nfs_super_d),
                       sizeof(struct nfs_super_d)) != NFS_ERROR_NONE)
   {
      return -NFS_ERROR_IO;
   }

   // 判断是否是第一次加载，方法是 判断是否存在约定的幻数magic_number
   // 如果此前没有挂载过，就要重新估算布局信息

   if (nfs_super_d.magic_num != NFS_MAGIC_NUM)
   {
      // 幻数无，要重新估算各部分大小
      nfs_super_d.sb_offset = 0;
      nfs_super_d.sb_blks = 1;

      nfs_super_d.map_inode_offset = NFS_BLKS_SZ(1);
      nfs_super_d.map_inode_blks = 1;

      nfs_super_d.map_data_offset = NFS_BLKS_SZ(2);
      nfs_super_d.map_data_blks = 1;

      nfs_super_d.inode_blks = NFS_DISK_SZ() / ((NFS_DATA_PER_FILE + NFS_INODE_PER_FILE) * NFS_BLK_SZ());
      nfs_super_d.inode_offset = NFS_BLKS_SZ(3);

      nfs_super_d.data_blks = nfs_super.num_blk - 3 - nfs_super_d.inode_blks;
      nfs_super_d.data_offset = NFS_BLKS_SZ(3)+ NFS_BLKS_SZ(nfs_super_d.inode_blks);

      nfs_super_d.max_ino = nfs_super_d.inode_blks - 3; 
      nfs_super_d.max_file = NFS_DATA_PER_FILE * NFS_BLK_SZ();

      nfs_super_d.sz_usage = 0;
      nfs_super_d.magic_num = NFS_MAGIC_NUM;

      is_init = TRUE;
   }

   // 不管是否重新布局，都要从to-disk 构建 in-mem
   nfs_super.sz_usage = nfs_super_d.sz_usage;
   nfs_super.max_ino = nfs_super_d.max_ino;
   nfs_super.max_file = nfs_super_d.max_file;

   nfs_super.sb_blks = nfs_super_d.sb_blks;
   nfs_super.sb_offset = nfs_super_d.sb_offset;
   nfs_super.map_inode_blks = nfs_super_d.map_inode_blks;
   nfs_super.map_inode_offset = nfs_super_d.map_inode_offset;
   nfs_super.map_data_blks = nfs_super_d.map_data_blks;
   nfs_super.map_data_offset = nfs_super_d.map_data_offset;
   nfs_super.inode_blks = nfs_super_d.inode_blks;
   nfs_super.inode_offset = nfs_super_d.inode_offset;
   nfs_super.data_blks = nfs_super_d.data_blks;
   nfs_super.data_offset = nfs_super_d.data_offset;

   nfs_super.map_inode = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_inode_blks));
   nfs_super.map_data = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_data_blks));

   // 读取索引结点，检验是否能读到，读不到报错。如果是nfs还需要读数据位图
   if (nfs_driver_read(nfs_super_d.map_inode_offset, (uint8_t *)(nfs_super.map_inode),
                       NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE)
   {
      return -NFS_ERROR_IO;
   }

   if (nfs_driver_read(nfs_super_d.map_data_offset, (uint8_t *)(nfs_super.map_data),
                       NFS_BLKS_SZ(nfs_super_d.map_data_blks)) != NFS_ERROR_NONE)
   {
      return -NFS_ERROR_IO;
   }


   // 初始化根目录
   root_dentry = new_dentry("/", NFS_DIR);

   // 如果是第一次挂载才重新创建根目录
   if (is_init)
   { /* 分配根节点 */
      root_inode = nfs_alloc_inode(root_dentry);

      root_dentry->inode = root_inode;
      // 给根节点分配第一个数据块
	   if(nfs_alloc_datablk(root_dentry) < 0)
		   printf("新增数据块失败");

      nfs_sync_inode(root_inode);
   }

   // 读取根目录，生成层级
   root_inode = nfs_read_inode(root_dentry, NFS_ROOT_INO);
   root_dentry->inode = root_inode;
   nfs_super.root_dentry = root_dentry;
   nfs_super.is_mounted = TRUE;

   // 打印索引结点位图，debug用的
   // nfs_dump_map();
   return ret;
}

/**
 * @brief
 *
 * @return int
 */
int nfs_umount()
{
   struct nfs_super_d nfs_super_d;

   if (!nfs_super.is_mounted)
   {
      return NFS_ERROR_NONE;
   }

   nfs_super_d.magic_num = NFS_MAGIC_NUM;
   nfs_super_d.sz_usage = nfs_super.sz_usage;
   nfs_super_d.max_ino = nfs_super.max_ino;
   nfs_super_d.max_file = nfs_super.max_file;

   nfs_super_d.sb_blks = nfs_super.sb_blks;
   nfs_super_d.sb_offset = nfs_super.sb_offset;
   nfs_super_d.map_inode_blks = nfs_super.map_inode_blks;
   nfs_super_d.map_inode_offset = nfs_super.map_inode_offset;
   nfs_super_d.map_data_blks = nfs_super.map_data_blks;
   nfs_super_d.map_data_offset = nfs_super.map_data_offset;
   nfs_super_d.inode_blks = nfs_super.inode_blks;
   nfs_super_d.inode_offset = nfs_super.inode_offset;
   nfs_super_d.data_blks = nfs_super.data_blks;
   nfs_super_d.data_offset = nfs_super.data_offset;


   if (nfs_driver_write(NFS_SUPER_OFS, (uint8_t *)&nfs_super_d,
                        sizeof(struct nfs_super_d)) != NFS_ERROR_NONE)
   {
      return -NFS_ERROR_IO;
   } else printf("超级块写回");

   if (nfs_driver_write(nfs_super_d.map_inode_offset, (uint8_t *)(nfs_super.map_inode),
                        NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE)
   {
      return -NFS_ERROR_IO;
   } else printf("索引位图写回");

   if (nfs_driver_write(nfs_super_d.map_data_offset, (uint8_t *)(nfs_super.map_data),
                        NFS_BLKS_SZ(nfs_super_d.map_data_blks)) != NFS_ERROR_NONE)
   {
      return -NFS_ERROR_IO;
   } else printf("数据位图写回");
 
   nfs_sync_inode(nfs_super.root_dentry->inode); /* 从根节点向下刷写节点 */
   printf("根节点写回");

   free(nfs_super.map_inode);
   free(nfs_super.map_data);
   ddriver_close(NFS_DRIVER());

   return NFS_ERROR_NONE;
}
