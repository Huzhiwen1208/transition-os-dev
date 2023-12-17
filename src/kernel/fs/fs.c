#include "mod.h"

/**
 * 我们第一版先不做文件系统的一级间接和二级间接。后面会进行补充 TODO
*/

// global variables
static EasyFS easyFileSystem;
static InodeID currentDirectoryInodeID; // 默认为根目录
static String currentPath = NULL; // 默认为根目录

// static methods declaration

static BitMap* newBitMap(u32 startBlockID, u32 blocks);
static DiskInode* newEmptyDiskInode();
static DirectoryEntry* newDirectoryEntry(char* name, InodeID id);
static void writeDiskInode(InodeID id, DiskInode* diskInode);
static DiskInode* readDiskInode(InodeID id);
static InodeID allocateOneInodeID();
static void freeOneInodeID(InodeID id);
/// @brief 申请一个数据块
/// @return BlockID
static u32 allocateOneDataBlockID();
static void freeOneDataBlockID(u32 blockID);
/// @brief 从目录中查找文件名为name的文件的InodeID
/// @param id 目录的InodeID，不存在则返回0
static InodeID findInodeIDByNameFromDirectory(InodeID id, String name);
static Boolean isDirectory(InodeID id);

// public methods

// pwd命令
void PrintfWorkingDirectory() {
    Printf("%s\n", currentPath);
}

// cd命令，切换当前目录到path下，如果path不存在或者不是目录，则不切换并抛出错误信息
Boolean ChangeDirectory(String path) {
    /*
    * 我们实现了绝对路径和相对路径寻址，其他类型的路径均基于这两种路径进行转换调用
    * 0. 场景0： cd 简单的cd命令，切换到根目录
    * 1. 场景1： cd /home/hzw，绝对路径，只需要一条路走到黑
    * 2. 场景2： cd .. 返回上一级目录，修改currentPath后转换为绝对路径
    * 3. 场景3： cd ../x/y/z 先切换到上一级目录，然后再递归调用cd转换为相对路径
    * 4. 场景4： cd hzw/opt <==> cd ./hzw/opt 相对路径，直接处理
    */
    InodeID backInodeID = currentDirectoryInodeID;
    String backPath = (String)Malloc(StringLength(currentPath));
    MemoryCopy(backPath, currentPath, StringLength(currentPath));

    // 如果只是使用了cd命令，那么默认切换到根目录
    if (StringLength(path) == 0) {
        currentDirectoryInodeID = 0;
        MemoryCopy(currentPath, "/", 2);

        Free(backPath);
        return TRUE;
    }

    // 如果是绝对路径，则需要一直寻找到最后一个目录
    if (path[0] == '/') {
        currentDirectoryInodeID = 0;
        String name = (String)Malloc(28); // 申请文件名大小的空间
        u32 k = 0;
        for (u32 i = 1; path[i]; i++) {
            if (path[i] != '/') {
                name[k++] = path[i];
                continue;
            }

            // 找到一个目录Name, 切换目录InodeID
            currentDirectoryInodeID = findInodeIDByNameFromDirectory(currentDirectoryInodeID, name);
            if (currentDirectoryInodeID == 0) {
                // 回滚
                currentDirectoryInodeID = backInodeID;
                MemoryCopy(currentPath, backPath, StringLength(backPath) + 1);

                PrintWithColor(LIGHT_RED, "cd: %s: No such file or directory\n", path);
                Free(backPath);
                Free(name);
                return FALSE;
            }
            // 重置name
            MemoryFree(name, 28);
            k = 0;
        }
        if (k) {
            // 处理cd /x/y/z这种情况的z
            currentDirectoryInodeID = findInodeIDByNameFromDirectory(currentDirectoryInodeID, name);
            if (currentDirectoryInodeID == 0 || !isDirectory(currentDirectoryInodeID)) {
                // 回滚
                currentDirectoryInodeID = backInodeID;
                MemoryCopy(currentPath, backPath, StringLength(backPath) + 1);

                PrintWithColor(LIGHT_RED, "cd: %s: No such file or directory\n", path);
                Free(backPath);
                Free(name);
                return FALSE;
            }
            MemoryFree(name, 28);
            k = 0;
        }

        // 维护currentPath 一定是不以/结尾的(根目录除外)
        MemoryCopy(currentPath, path, StringLength(path) + 1);
        if (StringLength(currentPath) > 1 && currentPath[StringLength(currentPath) - 1] == '/') {
            currentPath[StringLength(currentPath) - 1] = '\0';
        }

        Free(backPath);
        Free(name);
        return TRUE;
    }

     // ..模式
    if (StringStartWith(path, "..")) {
        // 如果是..模式
        if (StringLength(path) == 2 || StringEqual(path, "../")) {
            // 如果当前目录是根目录，那么就不需要切换了
            if (currentDirectoryInodeID == 0) {
                Free(backPath);
                return TRUE;
            }

            // 如果不是根目录，那么就需要切换到上一级目录
            // 回退currentPath
            u32 len = StringLength(currentPath);
            for (u32 i = len - 1; i >= 0; i--) {
                if (currentPath[i] == '/') {
                    currentPath[i] = '\0';
                    break;
                }
            }

            // 类似/xxx我们需要将其转换成/，而不是空字符串
            if (StringLength(currentPath) == 0) {
                currentPath[0] = '/';
                currentPath[1] = '\0';
            }

            // 递归调用cd，转换成绝对路径
            Boolean result = ChangeDirectory(currentPath);

            Free(backPath);
            return result;
        }

        // 如果是../x/y/z模式
        // 先切换到上一级目录
        ChangeDirectory("..");
        // 再递归调用cd，转换成相对路径
        Boolean result = ChangeDirectory(path + 3);

        Free(backPath);
        return result;
    }

    u32 index = 0;
    // 如果是相对路径的 hzw/opt模式或者./hzw/opt模式, 先转换成统一的hzw/opt模式
    if (StringStartWith(path, "./")) {
        index += 2;
    }

    // 开始处理相对路径的hzw/opt模式
    String name = (String)Malloc(28); // 申请文件名大小的空间
    u32 k = 0;
    for (u32 i = index; path[i]; i++) {
        if (path[i] != '/') {
            name[k++] = path[i];
            continue;
        }

        // 找到一个目录Name, 切换目录InodeID
        currentDirectoryInodeID = findInodeIDByNameFromDirectory(currentDirectoryInodeID, name);
        if (currentDirectoryInodeID == 0) {
            // 回滚
            currentDirectoryInodeID = backInodeID;
            MemoryCopy(currentPath, backPath, StringLength(backPath) + 1);

            PrintWithColor(LIGHT_RED, "cd: %s: No such file or directory\n", path);

            Free(name);
            Free(backPath);
            return FALSE;
        }
        // 重置name
        MemoryFree(name, 28);
        k = 0;
    }
    if (k) {
        // 处理cd /x/y/z这种情况的z
        currentDirectoryInodeID = findInodeIDByNameFromDirectory(currentDirectoryInodeID, name);
        if (currentDirectoryInodeID == 0 || !isDirectory(currentDirectoryInodeID)) {
            // 回滚
            currentDirectoryInodeID = backInodeID;
            MemoryCopy(currentPath, backPath, StringLength(backPath) + 1);

            PrintWithColor(LIGHT_RED, "cd: %s: No such file or directory\n", path);

            Free(name);
            Free(backPath);
            return FALSE;
        }
        MemoryFree(name, 28);
        k = 0;
    }

    // append currentPath with path
    u32 len = StringLength(currentPath);
    if (len > 1 && currentPath[len - 1] != '/') {
        currentPath[len++] = '/';
    }
    MemoryCopy(currentPath + len, path + index, StringLength(path + index) + 1);
    // 确保currentPath不以/结尾
    if (currentPath[StringLength(currentPath) - 1] == '/') {
        currentPath[StringLength(currentPath) - 1] = '\0';
    }

    Free(name);
    Free(backPath);
    return TRUE;
}

// linux中的ls初步实现，现在只支持-a，例如：ls -a /home/hzw 或者 ls -a ./abc/abdcd，暂不支持../abc/abdcd
void ListFiles(String option, String path) {
    if (StringStartWith(path, "..")) {
        PrintWithColor(LIGHT_RED, "ls: do not support .. mode");
        return;
    }

    if(StringLength(path) == 0 || StringEqual(path, ".")) {
        path = currentPath;
    }

    // 是绝对路径，那么就直接处理
    if (StringStartWith(path, "/")) {
        Boolean all = FALSE;
        if (StringLength(option) > 0) {
            if (StringEqual(option, "-a")) {
                all = TRUE;
            }else {
                Printf("ls: invalid option '%s'\n", option);
                return;
            }
        }

        // 找到最后一个目录或者文件
        String name = (String)Malloc(28);
        u32 k = 0;
        InodeID id = 0;
        for (u32 i = 1; path[i]; i++) {
            if (path[i] != '/') {
                name[k++] = path[i];
                continue;
            }

            // 找到一个目录Name, 切换目录InodeID
            InodeID nextID = findInodeIDByNameFromDirectory(id, name);
            // 如果不存在
            if (nextID == 0) {
                PrintWithColor(LIGHT_RED, "ls: cannot access %s: No such file or directory\n", path);
                Free(name);
                return;
            }

            id = nextID;
            // 重置name
            MemoryFree(name, 28);
            k = 0;
        }
        if (k) {
            // 处理ls /x/y/z这种情况的z
            InodeID nextID = findInodeIDByNameFromDirectory(id, name);
            if (nextID == 0) {
                PrintWithColor(LIGHT_RED, "ls: cannot access %s: No such file or directory\n", path);
                Free(name);
                return;
            }
            id = nextID;
            MemoryFree(name, 28);
            k = 0;
        }

        // 如果是文件，那么直接打印文件名
        if (!isDirectory(id)) {
            Printf("%s\n", name);

            Free(name);
            return;
        }

        // 如果是目录，那么就打印目录下的所有文件名，文件是默认颜色，目录是蓝色
        DiskInode* dirInode = readDiskInode(id);
        u32 size = dirInode->Size;
        u32 blocks = (size + 511) / 512; // 向上取整
        for (u32 i = 0; i < blocks; i++) {
            u32 blockID = dirInode->DirectBlock[i];
            u8 buffer[512];
            DiskCacheRead(blockID, buffer);
            for (u32 j = 0; j < 512; j += sizeof(DirectoryEntry)) {
                DirectoryEntry* entry = (DirectoryEntry*)(buffer + j);
                if (all) {
                    if (entry->InodeID == 0) 
                        continue;
                    if (isDirectory(entry->InodeID)) {
                        PrintWithColor(LIGHT_BLUE, "%s ", entry->Name);
                    }else {
                        Printf("%s ", entry->Name);
                    }
                }else {
                    if (entry->InodeID == 0)
                        continue;
                    
                    if (StringStartWith(entry->Name, "."))
                        continue;

                    if (isDirectory(entry->InodeID)) {
                        PrintWithColor(LIGHT_BLUE, "%s ", entry->Name);
                    }else {
                        Printf("%s ", entry->Name);
                    }
                }
            }
        }
        Printf("\n");

        Free(name);
        Free(dirInode);
        return;
    }

    // 如果是相对路径，那么需要转换成绝对路径
    String temp = (String)Malloc(256);
    if (currentDirectoryInodeID == 0) {
        MemoryCopy(temp, currentPath, 1);
        if (StringStartWith(path, "./")) 
            MemoryCopy(temp + 1, path + 2, StringLength(path+2) + 1);
        else 
            MemoryCopy(temp + 1, path, StringLength(path) + 1);
        ListFiles(option, temp);

        Free(temp);
        return;
    }

    MemoryCopy(temp, currentPath, StringLength(currentPath) + 1);
    u32 len = StringLength(temp);
    if (temp[len - 1] != '/') {
        temp[len++] = '/';
    }
    if (StringStartWith(path, "./")) 
        MemoryCopy(temp + len, path + 2, StringLength(path+2) + 1);
    else
        MemoryCopy(temp + len, path, StringLength(path) + 1);
    ListFiles(option, temp);

    Free(temp);
}

// mkdir [-p] path，path可以为相对路径，也可以为绝对路径，但是我们暂时不支持..模式
Boolean MakeDirectory(String path, String option) {
    if (StringStartWith(path, "..")) {
        PrintWithColor(LIGHT_RED, "mkdir: do not support .. mode");
        return FALSE;
    }

    // 是绝对路径，那么就直接处理
    if (StringStartWith(path, "/")) {
        Boolean create = FALSE;
        if (StringLength(option) > 0) {
            if (StringEqual(option, "-p")) {
                create = TRUE;
            }else {
                Printf("mkdir: invalid option '%s'\n", option);
                return FALSE;
            }
        }

        // 逐级创建目录
        String name = (String)Malloc(28);
        u32 k = 0;
        InodeID id = 0;
        for (u32 i = 1; path[i]; i++) {
            if (path[i] != '/') {
                name[k++] = path[i];
                continue;
            }

            // 找到一个目录Name, 切换目录InodeID
            InodeID nextID = findInodeIDByNameFromDirectory(id, name);
            // 如果不存在
            if (nextID == 0) {
                if (!create) {
                    PrintWithColor(LIGHT_RED, "mkdir: cannot create directory %s: No such file or directory\n", path);
                    Free(name);
                    return FALSE;
                }

                // 创建目录
                nextID = CreateFile(id, name, FT_DIRECTORY);
                if (nextID == -1) {
                    PrintWithColor(LIGHT_RED, "mkdir: cannot create directory %s: No such file or directory\n", path);
                    Free(name);
                    return FALSE;
                }
            }

            // 如果存在，那么就切换到下一级目录
            id = nextID;
            // 重置name
            MemoryFree(name, 28);
            k = 0;
        }
        if (k) {
            // 处理mkdir /x/y/z这种情况的z
            InodeID nextID = findInodeIDByNameFromDirectory(id, name);
            if (nextID == 0) {
                if (!create) {
                    PrintWithColor(LIGHT_RED, "mkdir: cannot create directory %s: No such file or directory\n", path);
                    Free(name);
                    return FALSE;
                }

                // 创建目录
                nextID = CreateFile(id, name, FT_DIRECTORY);
                if (nextID == -1) {
                    PrintWithColor(LIGHT_RED, "mkdir: cannot create directory %s: No such file or directory\n", path);
                    Free(name);
                    return FALSE;
                }
            }
            MemoryFree(name, 28);
            k = 0;
        }

        Free(name);
        return TRUE;
    }

    // 如果是相对路径，那么需要转换成绝对路径
    String temp = (String)Malloc(256);
    if (currentDirectoryInodeID == 0) {
        MemoryCopy(temp, currentPath, 1);
        if (StringStartWith(path, "./")) 
            MemoryCopy(temp + 1, path + 2, StringLength(path+2) + 1);
        else 
            MemoryCopy(temp + 1, path, StringLength(path) + 1);
        Boolean result = MakeDirectory(temp, option);
        Free(temp);
        return result;
    }

    MemoryCopy(temp, currentPath, StringLength(currentPath) + 1);
    u32 len = StringLength(temp);
    if (temp[len - 1] != '/') {
        temp[len++] = '/';
    }
    if (StringStartWith(path, "./")) 
        MemoryCopy(temp + len, path + 2, StringLength(path+2) + 1);
    else
        MemoryCopy(temp + len, path, StringLength(path) + 1);
    Boolean result = MakeDirectory(temp, option);

    Free(temp);
    return result;
}

/// @brief 在inodeID指定的目录下创建文件，文件名为name，文件类型为type，如果id==-1, 那么就在当前目录下创建文件
/// @param id 目录的InodeID
/// @param name 文件名
/// @param type 文件类型
/// @return 创建成功返回创建文件的InodeID，否则返回-1
InodeID CreateFile(InodeID id, String name, FileType type) {
    if (CharCount(name, '/')) {
        PrintWithColor(LIGHT_RED, "touch: filename should not contain '/'\n");
        return -1;
    }

    if (id == -1) {
        id = currentDirectoryInodeID;
    }

    DiskInode* dirInode = readDiskInode(id);
    if (dirInode->Type != FT_DIRECTORY) {
        Panic("[CreateFile] current directory is not a directory");
    }

    // 检查是否已经存在同名文件
    if (findInodeIDByNameFromDirectory(id, name) != 0) {
        PrintWithColor(LIGHT_RED, "touch: cannot create %s '%s': File exists\n"
            , type == FT_FILE ? "file" : "directory", name);

        Free(dirInode);
        return -1;
    }

    u32 size = dirInode->Size;
    // TODO: indirect inode
    if (size / 512 >= INODE_DIRECT_BLOCKS) {
        Panic("[CreateFile] indirect inode is not implemented");
    }

    // 如果是正好写满了x块，那么需要申请一块新的块
    if (size % 512 == 0) {
        dirInode->DirectBlock[size / 512] = allocateOneDataBlockID();
    }

    u32 blockID = dirInode->DirectBlock[size / 512];
    u8 buffer[512];
    DiskCacheRead(blockID, buffer);
    InodeID fileID = allocateOneInodeID();
    DirectoryEntry* entry = newDirectoryEntry(name, fileID);
    // append 目录项
    MemoryCopy(buffer + size%512, entry, sizeof(DirectoryEntry));
    DiskCacheWrite(blockID, buffer);

    // 创建diskInode
    DiskInode* fileInode = newEmptyDiskInode();
    fileInode->Type = type;
    fileInode->RefCount = 1;
    writeDiskInode(fileID, fileInode);

    // 更新目录的大小
    dirInode->Size += sizeof(DirectoryEntry);
    writeDiskInode(id, dirInode);

    // 释放内存
    Free(fileInode);
    Free(entry);
    Free(dirInode);
    return fileID;
}

void InitializeFileSystem() {
    easyFileSystem.Device = GetDeviceByID(1);
    easyFileSystem.rlock = NewReentrantLock();
    u32 totalBlocks = ((IdeDisk*)easyFileSystem.Device->DevicePtr)->TotalSector;
 
    // 1. 创建Inode 的位图，我们取5个块来存储inode位图，这样可以存储5*4096=20K个inode（文件）
    BitMap* inodeBitMap = newBitMap(1, 5);
    u32 inodeCount = 5 * 512 * 8;
    u32 inodeAreaBlocks = inodeCount / (512 / 128);

    easyFileSystem.InodeBitMap = inodeBitMap;
    easyFileSystem.InodeAreaStartBlockID = 5 + 1;

    /* 2. 创建数据块的位图，位图块数为a，数据块数为b，那么一定满足方程：
    *  a + b = remainBlocks 
    *  a * 512 * 8 = b
    * 所以我们推出 => a = remainBlocks / (512 * 8 + 1)
    */
    u32 remainBlocks = totalBlocks - 5 - 1 - inodeAreaBlocks; // 剩余的块数
    u32 dataBitMapBlocks = remainBlocks / (512 * 8 + 1);
    BitMap* dataBitMap = newBitMap(5 + 1 + inodeAreaBlocks, dataBitMapBlocks);
    u32 dataAreaBlocks = remainBlocks - dataBitMapBlocks;

    easyFileSystem.DataBitMap = dataBitMap;
    easyFileSystem.DataAreaStartBlockID = 5 + 1 + inodeAreaBlocks + dataBitMapBlocks;

    // 3. 创建超级块
    SuperBlock* superBlock = (SuperBlock*)AllocateOnePage(KernelMode);
    superBlock->DataAreaBlocks = dataAreaBlocks;
    superBlock->DataBitMapBlocks = dataBitMapBlocks;
    superBlock->InodeAreaBlocks = inodeAreaBlocks;
    superBlock->InodeBitMapBlocks = 5;
    superBlock->TotalBlocks = totalBlocks;
    superBlock->Magic = 0x20001208;

    // 4. 将超级块写入磁盘
    DiskCacheWrite(0, (void*)superBlock);
    FreeOnePage(superBlock);

    // 5. 创建根目录
    InodeID id = allocateOneInodeID();
    DiskInode* rootInode = newEmptyDiskInode();
    rootInode->Type = FT_DIRECTORY;
    rootInode->RefCount = 1;
    writeDiskInode(id, rootInode);
    Free(rootInode);

    // 6. 初始化当前目录
    currentDirectoryInodeID = id;
    currentPath = (String)Malloc(256);
    MemoryCopy(currentPath, "/", 1);
}


// static methods implementation

static BitMap* newBitMap(u32 startBlockID, u32 blocks) {
    BitMap* bitmap = (BitMap*)Malloc(sizeof(BitMap));
    bitmap->StartBlockID = startBlockID;
    bitmap->Blocks = blocks;
    return bitmap;
}

static DiskInode* newEmptyDiskInode() {
    DiskInode* diskInode = (DiskInode*)Malloc(sizeof(DiskInode));
    diskInode->Type = FT_UNKNOWN;
    diskInode->Size = 0;
    diskInode->RefCount = 0;
    for (u32 i = 0; i < INODE_DIRECT_BLOCKS; i++) {
        diskInode->DirectBlock[i] = 0;
    }
    diskInode->Indirect1 = 0;
    diskInode->Indirect2 = 0;

    return diskInode;
}

static void writeDiskInode(InodeID id, DiskInode* diskInode) {
    u32 blockID = easyFileSystem.InodeAreaStartBlockID + id / (512 / 128);
    u32 offset = (id % (512 / 128)) * 128;

    u8 buffer[512];
    DiskCacheRead(blockID, buffer);
    MemoryCopy(buffer + offset, diskInode, sizeof(DiskInode));
    DiskCacheWrite(blockID, buffer);
}

static DirectoryEntry* newDirectoryEntry(char* name, InodeID id) {
    DirectoryEntry* entry = (DirectoryEntry*)Malloc(sizeof(DirectoryEntry));
    entry->InodeID = id;
    MemoryCopy(entry->Name, name, StringLength(name));
    return entry;
}

static DiskInode* readDiskInode(InodeID id) {
    u32 blockID = easyFileSystem.InodeAreaStartBlockID + id / (512 / 128);
    u32 offset = (id % (512 / 128)) * 128;

    u8 buffer[512];
    DiskCacheRead(blockID, buffer);
    DiskInode* diskInode = (DiskInode*)Malloc(sizeof(DiskInode));
    MemoryCopy(diskInode, buffer + offset, sizeof(DiskInode));
    return diskInode;
}

static InodeID allocateOneInodeID() {
    easyFileSystem.rlock->Lock(easyFileSystem.rlock);

    for (u32 i = 0; i < easyFileSystem.InodeBitMap->Blocks; i++) {
        u32 blockID = easyFileSystem.InodeBitMap->StartBlockID + i;
        u8 buffer[512];
        DiskCacheRead(blockID, buffer);
        for (u32 j = 0; j < 512; j++) {
            u8 byte = buffer[j];
            for (u32 k = 0; k < 8; k++) {
                if ((byte & (1 << k)) == 0) {
                    byte |= (1 << k);
                    buffer[j] = byte;
                    DiskCacheWrite(blockID, buffer);
                    easyFileSystem.rlock->Unlock(easyFileSystem.rlock);
                    return i * 512 * 8 + j * 8 + k;
                }
            }
        }
    }

    Panic("allocateOneInodeID: no inode id can be allocated");
}

static void freeOneInodeID(InodeID id) {
    easyFileSystem.rlock->Lock(easyFileSystem.rlock);

    u32 blockID = easyFileSystem.InodeBitMap->StartBlockID + id / (512 * 8);
    u32 offset = (id % (512 * 8)) / 8;
    u8 buffer[512];
    DiskCacheRead(blockID, buffer);
    u8 byte = buffer[offset];
    byte &= ~(1 << (id % 8));
    buffer[offset] = byte;
    DiskCacheWrite(blockID, buffer);

    easyFileSystem.rlock->Unlock(easyFileSystem.rlock);
}

static u32 allocateOneDataBlockID() {
    easyFileSystem.rlock->Lock(easyFileSystem.rlock);

    for (u32 i = 0; i < easyFileSystem.DataBitMap->Blocks; i++) {
        u32 blockID = easyFileSystem.DataBitMap->StartBlockID + i;
        u8 buffer[512];
        DiskCacheRead(blockID, buffer);
        for (u32 j = 0; j < 512; j++) {
            u8 byte = buffer[j];
            for (u32 k = 0; k < 8; k++) {
                if ((byte & (1 << k)) == 0) {
                    byte |= (1 << k);
                    buffer[j] = byte;
                    DiskCacheWrite(blockID, buffer);
                    easyFileSystem.rlock->Unlock(easyFileSystem.rlock);
                    return i * 512 * 8 + j * 8 + k + easyFileSystem.DataAreaStartBlockID;
                }
            }
        }
    }

    Panic("[allocateOneDataBlockID] no data block id can be allocated");
}

static void freeOneDataBlockID(u32 blockID) {
    easyFileSystem.rlock->Lock(easyFileSystem.rlock);

    u32 id = blockID - easyFileSystem.DataAreaStartBlockID;
    u32 blockID2 = easyFileSystem.DataBitMap->StartBlockID + id / (512 * 8);
    u32 offset = (id % (512 * 8)) / 8;
    u8 buffer[512];
    DiskCacheRead(blockID2, buffer);
    u8 byte = buffer[offset];
    byte &= ~(1 << (id % 8));
    buffer[offset] = byte;
    DiskCacheWrite(blockID2, buffer);

    easyFileSystem.rlock->Unlock(easyFileSystem.rlock);
}

static InodeID findInodeIDByNameFromDirectory(InodeID id, String name) {
    DiskInode* dirInode = readDiskInode(id);
    if (dirInode->Type != FT_DIRECTORY) {
        Free(dirInode);
        return 0;
    }

    u32 size = dirInode->Size;
    u32 blocks = (size + 511) / 512; // 向上取整
    for (u32 i = 0; i < blocks; i++) {
        u32 blockID = dirInode->DirectBlock[i];
        u8 buffer[512];
        DiskCacheRead(blockID, buffer);
        for (u32 j = 0; j < 512; j += sizeof(DirectoryEntry)) {
            DirectoryEntry* entry = (DirectoryEntry*)(buffer + j);
            if (StringEqual(entry->Name, name)) {
                Free(dirInode);
                return entry->InodeID;
            }
        }
    }

    Free(dirInode);
    return 0;
}

static Boolean isDirectory(InodeID id) {
    Boolean result = FALSE;
    DiskInode* diskInode = readDiskInode(id);
    result = diskInode->Type == FT_DIRECTORY;

    Free(diskInode);
    return result;
}