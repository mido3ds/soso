#include "systemfs.h"
#include "common.h"
#include "fs.h"
#include "alloc.h"
#include "device.h"
#include "screen.h"
#include "vmm.h"
#include "process.h"

static FileSystemNode* gSystemFsRoot = NULL;


static BOOL systemfs_open(File *file, uint32 flags);
static FileSystemDirent *systemfs_readdir(FileSystemNode *node, uint32 index);
static FileSystemNode *systemfs_finddir(FileSystemNode *node, char *name);

static void createNodes();

static FileSystemDirent gDirent;

static int32 systemfs_read_meminfo_totalpages(File *file, uint32 size, uint8 *buffer);
static int32 systemfs_read_meminfo_usedpages(File *file, uint32 size, uint8 *buffer);
static BOOL systemfs_open_threads_dir(File *file, uint32 flags);
static void systemfs_close_threads_dir(File *file);

void initializeSystemFS()
{
    gSystemFsRoot = kmalloc(sizeof(FileSystemNode));
    memset((uint8*)gSystemFsRoot, 0, sizeof(FileSystemNode));

    gSystemFsRoot->nodeType = FT_Directory;

    FileSystemNode* rootFs = getFileSystemRootNode();

    mkdir_fs(rootFs, "system", 0);

    FileSystemNode* systemNode = finddir_fs(rootFs, "system");

    if (systemNode)
    {
        systemNode->nodeType |= FT_MountPoint;
        systemNode->mountPoint = gSystemFsRoot;
        gSystemFsRoot->parent = systemNode->parent;
        strcpy(gSystemFsRoot->name, systemNode->name);
    }
    else
    {
        PANIC("Could not create /system !");
    }

    gSystemFsRoot->open = systemfs_open;
    gSystemFsRoot->finddir = systemfs_finddir;
    gSystemFsRoot->readdir = systemfs_readdir;

    createNodes();
}

static void createNodes()
{
    FileSystemNode* nodeMemInfo = kmalloc(sizeof(FileSystemNode));

    memset((uint8*)nodeMemInfo, 0, sizeof(FileSystemNode));

    strcpy(nodeMemInfo->name, "meminfo");
    nodeMemInfo->nodeType = FT_Directory;
    nodeMemInfo->open = systemfs_open;
    nodeMemInfo->finddir = systemfs_finddir;
    nodeMemInfo->readdir = systemfs_readdir;
    nodeMemInfo->parent = gSystemFsRoot;

    gSystemFsRoot->firstChild = nodeMemInfo;

    FileSystemNode* nodeMemInfoTotalPages = kmalloc(sizeof(FileSystemNode));
    memset((uint8*)nodeMemInfoTotalPages, 0, sizeof(FileSystemNode));
    strcpy(nodeMemInfoTotalPages->name, "totalpages");
    nodeMemInfoTotalPages->nodeType = FT_File;
    nodeMemInfoTotalPages->open = systemfs_open;
    nodeMemInfoTotalPages->read = systemfs_read_meminfo_totalpages;
    nodeMemInfoTotalPages->parent = nodeMemInfo;

    nodeMemInfo->firstChild = nodeMemInfoTotalPages;

    FileSystemNode* nodeMemInfoUsedPages = kmalloc(sizeof(FileSystemNode));
    memset((uint8*)nodeMemInfoUsedPages, 0, sizeof(FileSystemNode));
    strcpy(nodeMemInfoUsedPages->name, "usedpages");
    nodeMemInfoUsedPages->nodeType = FT_File;
    nodeMemInfoUsedPages->open = systemfs_open;
    nodeMemInfoUsedPages->read = systemfs_read_meminfo_usedpages;
    nodeMemInfoUsedPages->parent = nodeMemInfo;

    nodeMemInfoTotalPages->nextSibling = nodeMemInfoUsedPages;

    //

    FileSystemNode* nodeThreads = kmalloc(sizeof(FileSystemNode));
    memset((uint8*)nodeThreads, 0, sizeof(FileSystemNode));

    strcpy(nodeThreads->name, "threads");
    nodeThreads->nodeType = FT_Directory;
    nodeThreads->open = systemfs_open_threads_dir;
    nodeThreads->close = systemfs_close_threads_dir;
    nodeThreads->finddir = systemfs_finddir;
    nodeThreads->readdir = systemfs_readdir;
    nodeThreads->parent = gSystemFsRoot;

    nodeMemInfo->nextSibling = nodeThreads;

    //

    FileSystemNode* nodePipes = kmalloc(sizeof(FileSystemNode));
    memset((uint8*)nodePipes, 0, sizeof(FileSystemNode));

    strcpy(nodePipes->name, "pipes");
    nodePipes->nodeType = FT_Directory;
    nodePipes->parent = gSystemFsRoot;

    nodeThreads->nextSibling = nodePipes;
}

static BOOL systemfs_open(File *file, uint32 flags)
{
    return TRUE;
}

static FileSystemDirent *systemfs_readdir(FileSystemNode *node, uint32 index)
{
    int counter = 0;

    FileSystemNode* child = node->firstChild;

    //Screen_PrintF("systemfs_readdir-main:%s index:%d\n", node->name, index);

    while (NULL != child)
    {
        //Screen_PrintF("systemfs_readdir-child:%s\n", child->name);
        if (counter == index)
        {
            strcpy(gDirent.name, child->name);
            gDirent.fileType = child->nodeType;

            return &gDirent;
        }

        ++counter;

        child = child->nextSibling;
    }

    return NULL;
}

static FileSystemNode *systemfs_finddir(FileSystemNode *node, char *name)
{
    //Screen_PrintF("systemfs_finddir-main:%s requestedName:%s\n", node->name, name);

    FileSystemNode* child = node->firstChild;
    while (NULL != child)
    {
        if (strcmp(name, child->name) == 0)
        {
            //Screen_PrintF("systemfs_finddir-found:%s\n", name);
            return child;
        }

        child = child->nextSibling;
    }

    return NULL;
}

static int32 systemfs_read_meminfo_totalpages(File *file, uint32 size, uint8 *buffer)
{
    if (size >= 4)
    {
        if (file->offset == 0)
        {
            int totalPages = getTotalPageCount();

            sprintf(buffer, "%d", totalPages);

            int len = strlen(buffer);

            file->offset += len;

            return len;
        }
        else
        {
            return 0;
        }
    }
    return -1;
}

static int32 systemfs_read_meminfo_usedpages(File *file, uint32 size, uint8 *buffer)
{
    if (size >= 4)
    {
        if (file->offset == 0)
        {
            int usedPages = getUsedPageCount();

            sprintf(buffer, "%d", usedPages);

            int len = strlen(buffer);

            file->offset += len;

            return len;
        }
        else
        {
            return 0;
        }
    }
    return -1;
}

static BOOL systemfs_open_thread_file(File *file, uint32 flags)
{
    return TRUE;
}

static void systemfs_close_thread_file(File *file)
{

}

static int32 systemfs_read_thread_file(File *file, uint32 size, uint8 *buffer)
{
    if (size >= 128)
    {
        if (file->offset == 0)
        {
            int threadId = atoi(file->node->name);
            Thread* thread = getThreadById(threadId);
            if (thread)
            {
                int charIndex = 0;
                charIndex += sprintf(buffer + charIndex, "tid:%d\n", thread->threadId);
                charIndex += sprintf(buffer + charIndex, "userMode:%d\n", thread->userMode);
                char state[10];
                threadStateToString(thread->state, state, 10);
                charIndex += sprintf(buffer + charIndex, "state:%s\n", state);
                charIndex += sprintf(buffer + charIndex, "contextSwitches:%d\n", thread->contextSwitchCount);
                if (thread->owner)
                {
                    charIndex += sprintf(buffer + charIndex, "process:%d\n", thread->owner->pid);
                }
                else
                {
                    charIndex += sprintf(buffer + charIndex, "process:-\n");
                }

                int len = charIndex;

                file->offset += len;

                return len;
            }
        }
        else
        {
            return 0;
        }
    }
    return -1;
}

static void cleanThreadNodes(File *file)
{
    FileSystemNode* node = file->node->firstChild;

    while (node)
    {
        FileSystemNode* next = node->nextSibling;

        kfree(node);

        node = next;
    }
}

static BOOL systemfs_open_threads_dir(File *file, uint32 flags)
{
    char buffer[16];

    cleanThreadNodes(file);

    //And fill again

    FileSystemNode* nodePrevious = NULL;

    Thread* thread = getMainKernelThread();

    while (NULL != thread)
    {
        FileSystemNode* nodeThread = kmalloc(sizeof(FileSystemNode));
        memset((uint8*)nodeThread, 0, sizeof(FileSystemNode));

        sprintf(buffer, "%d", thread->threadId);

        strcpy(nodeThread->name, buffer);
        nodeThread->nodeType = FT_File;
        nodeThread->open = systemfs_open_thread_file;
        nodeThread->close = systemfs_close_thread_file;
        nodeThread->read = systemfs_read_thread_file;
        nodeThread->finddir = systemfs_finddir;
        nodeThread->readdir = systemfs_readdir;
        nodeThread->parent = file->node;

        if (nodePrevious)
        {
            nodePrevious->nextSibling = nodeThread;
        }
        else
        {
            file->node->firstChild = nodeThread;
        }

        nodePrevious = nodeThread;
        thread = thread->next;
    }



    return TRUE;
}

static void systemfs_close_threads_dir(File *file)
{
    //left blank intentionally
}