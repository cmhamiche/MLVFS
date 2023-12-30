/*
 * Copyright (C) 2014 Albert Y. Shih
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

// Originally based off of sample code from the Pismo File Mount Developer Kit (build 171)
//
//----------------------------------------------------------------------------
// Copyright 2006-2013 Joe Lowe
//
// Permission is granted to any person obtaining a copy of this Software,
// to deal in the Software without restriction, including the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and sell copies of
// the Software.
//
// The above copyright and permission notice must be left intact in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS WITHOUT WARRANTY.
//----------------------------------------------------------------------------
// file name:  tempfs.cpp
// created:    2006.12.20
//    Temporary file system application sample. Demonstrates
//    implementing a full read+write file system in a standalone
//    application.
//
//    This sample should compile via MSVC on Windows, and
//    via GCC on Mac and Linux.
//----------------------------------------------------------------------------

#include "portability.cpp"

#include "pfmformatter.h"
#include "pfmmarshaller.h"

#include "files.h"

#undef INT64_C
#undef UINT64_C
#include "index.h"
#include "dng.h"
#include "wav.h"
#include "hdr.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define BUILD64(a,b) (((uint64_t)(a)) << 32 | (b))

static const char mlvfsFormatterName[] = "MLVFS";
static const char mlvFileTypeTag[] = "MLV";

#define PROCESSING_NONE             0
#define PROCESSING_DUAL_ISO         1
#define PROCESSING_VERTICAL_STRIPES 2
#define PROCESSING_CHROMA_SMOOTHING 4

enum Type { TEMPORARY, VIRTUAL, MAPPED };

wchar_t *concatenate(const wchar_t *a, const wchar_t *b, const wchar_t *c = NULL, const wchar_t *d = NULL)
{
    uint32_t len = wcslen(a) + wcslen(b) + (c != NULL ? wcslen(c) : 0) + (d != NULL ? wcslen(d) : 0);
    wchar_t *out = static_cast<wchar_t *>(malloc((len + 1) * sizeof(wchar_t)));
    wcscpy(out, a);
    wcscat(out, b);
    if(c != NULL) wcscat(out, c);
    if(d != NULL) wcscat(out, d);
    return out;
}

struct File;

struct FileList
{
    File* file;
    FileList** prev;
    FileList* next;
    int64_t listId;
    File** position;

    void Iterate(PfmMarshallerListResult* listResult);

    FileList(File* file,int64_t listId);
    ~FileList(void);
};

struct Volume;

struct File
{
    Volume* volume;
    File* parent;
    File** sibPrev;
    File* sibNext;
    File** openPrev;
    File* openNext;
    int64_t openId;
    int64_t openSequence;
    wchar_t* name;
    int8_t fileType;
    uint8_t fileFlags;
    int64_t fileId;
    int64_t createTime;
    int64_t accessTime;
    int64_t writeTime;
    int64_t changeTime;
    static const size_t blockSize = 16384;
    static const uint64_t maxFileSize = static_cast<uint64_t>(blockSize)*INT_MAX;
    union
    {
            // fileType == pfmFileTypeFile
        struct
        {
            uint64_t fileSize;
            size_t blockCount;
            uint8_t** blockList;
        } file;
            // fileType == pfmFileTypeFolder
        struct
        {
            File* firstChild;
            FileList* firstList;
        } folder;
    } data;
    Type type;
    uint8_t *workspace;

#ifdef DEBUG
    void CheckConsistency(void);
#else
    void CheckConsistency(void) { ; }
#endif
    void Opened(PfmOpenAttribs* openAttribs);
    void Open(int64_t newOpenId,PfmOpenAttribs* openAttribs,int64_t* parentFileId,wchar_t** endName);
    void Close(int64_t openSequence);
    void Delete(int64_t writeTime,bool leaveChildren);
    int/*error*/ Move(File* parent,File** sibPrev,const wchar_t* name,int64_t writeTime);
    void Flush(uint8_t fileFlags,int64_t createTime,int64_t accessTime,int64_t writeTime,int64_t changeTime);
    int/*error*/ Read(uint64_t fileOffset,void* data,size_t requestedSize,size_t* outActualSize);
    int/*error*/ PrepSetSize(uint64_t newFileSize);
    int/*error*/ Write(uint64_t fileOffset,const void* data,size_t requestedSize,size_t* outActualSize);
    int/*error*/ SetSize(uint64_t fileSize);
    int/*error*/ ListFactory(int64_t listId,FileList** outList);
    int/*error*/ ListEnd(int64_t listId);

    File(Volume* volume);
    ~File(void);
    int/*error*/ Init(File* parent,File** sibPrev,const wchar_t* name,int8_t fileType,uint8_t fileFlags,int64_t writeTime);
};

struct Volume: PfmFormatterOps
{
    PfmMarshaller* marshaller;
    uint64_t capacity;
    int64_t lastFileId;
    File root;
    File* firstOpenFile;

    wchar_t *mlvBaseFileName;
    wchar_t *mappedDirectory;
    mlv_xref_hdr_t *index;
    uint32_t videoFrameCount, audioFrameCount;
    FILE **chunks;
    uint32_t chunkCount;
    struct frame_headers *frameHeaders;
    mlv_wavi_hdr_t audioHeader;
    uint16_t processing;

#ifdef DEBUG
    void CheckConsistency(void);
#else
    void CheckConsistency(void) { ; }
#endif
    int/*error*/ FileFactory(File* parent,File** sibPrev,const wchar_t* name,int8_t createFileType,uint8_t createFileFlags,int64_t writeTime,File** file);
    int/*error*/ FindFile(const PfmNamePart* nameParts,size_t namePartCount,File** file,File** parent,File*** sibPrev);
    int/*error*/ FindOpenFile(int64_t openId,File** file);

        // PfmFormatterOps
    void CCALL ReleaseName(wchar_t* name);
    int/*error*/ CCALL Open(const PfmNamePart* nameParts,size_t namePartCount,int8_t createFileType,uint8_t createFileFlags,int64_t writeTime,int64_t newCreateOpenId,int8_t existingAccessLevel,int64_t newExistingOpenId,uint8_t/*bool*/ * existed,PfmOpenAttribs* openAttribs,int64_t* parentFileId,wchar_t** endName);
    int/*error*/ CCALL Replace(int64_t targetOpenId,int64_t targetParentFileId,const PfmNamePart* targetEndName,uint8_t createFileFlags,int64_t writeTime,int64_t newCreateOpenId,PfmOpenAttribs* openAttribs);
    int/*error*/ CCALL Move(int64_t sourceOpenId,int64_t sourceParentFileId,const PfmNamePart* sourceEndName,const PfmNamePart* targetNameParts,size_t targetNamePartCount,uint8_t/*bool*/ deleteSource,int64_t writeTime,int8_t existingAccessLevel,int64_t newExistingOpenId,uint8_t/*bool*/ * existed,PfmOpenAttribs* openAttribs,int64_t* parentFileId,wchar_t** endName);
    int/*error*/ CCALL MoveReplace(int64_t sourceOpenId,int64_t sourceParentFileId,const PfmNamePart* sourceEndName,int64_t targetOpenId,int64_t targetParentFileId,const PfmNamePart* targetEndName,uint8_t/*bool*/ deleteSource,int64_t writeTime);
    int/*error*/ CCALL Delete(int64_t openId,int64_t parentFileId,const PfmNamePart* endName,int64_t writeTime);
    int/*error*/ CCALL Close(int64_t openId,int64_t openSequence);
    int/*error*/ CCALL FlushFile(int64_t openId,uint8_t flushFlags,uint8_t fileFlags,uint8_t color,int64_t createTime,int64_t accessTime,int64_t writeTime,int64_t changeTime,PfmOpenAttribs* openAttribs);
    int/*error*/ CCALL List(int64_t openId,int64_t listId,PfmMarshallerListResult* listResult);
    int/*error*/ CCALL ListEnd(int64_t openId,int64_t listId);
    int/*error*/ CCALL Read(int64_t openId,uint64_t fileOffset,void* data,size_t requestedSize,size_t* outActualSize);
    int/*error*/ CCALL Write(int64_t openId,uint64_t fileOffset,const void* data,size_t requestedSize,size_t* outActualSize);
    int/*error*/ CCALL SetSize(int64_t openId,uint64_t fileSize);
    int/*error*/ CCALL Capacity(uint64_t* totalCapacity,uint64_t* availableCapacity);
    int/*error*/ CCALL FlushMedia(uint8_t/*bool*/ * mediaClean);
    int/*error*/ CCALL Control(int64_t openId,int8_t accessLevel,int controlCode,const void* input,size_t inputSize,void* output,size_t maxOutputSize,size_t* outputSize);
    int/*error*/ CCALL MediaInfo(int64_t openId,PfmMediaInfo* mediaInfo,wchar_t** mediaLabel);
    int/*error*/ CCALL Access(int64_t openId,int8_t accessLevel,PfmOpenAttribs* openAttribs);

    Volume(void);
    ~Volume(void);
    int/*systemError*/ Init(const wchar_t* mlvFileName);
    size_t ReadVirtualDNG(uint32_t frameNumber, uint64_t fileOffset, File *file, uint8_t *buffer, size_t requestedSize);
    size_t ReadVirtualWAV(uint64_t fileOffset, File *file, uint8_t *buffer, size_t requestedSize);
};

struct MlvFormatter: PfmFormatter
{
    void CCALL Release(void);
    int/*systemError*/ CCALL Identify(HANDLE statusWrite,const wchar_t* mountFileName,HANDLE mountFileHandle,const void* mountFileData,size_t mountFileDataSize);
    int/*systemError*/ CCALL Serve(const wchar_t* mountFileName,int mountFlags,HANDLE read,HANDLE write);
    void CCALL Cancel(void);
};

MlvFormatter mlvFormatter;

#ifdef DEBUG

void File::CheckConsistency(void)
{
    if(this == &(volume->root))
    {
        ASSERT(!sibPrev);
    }
    else
    {
        ASSERT(sibPrev);
        ASSERT(*sibPrev == this);
        if(sibNext)
        {
            ASSERT(sibNext->sibPrev == &sibNext);
        }
    }
    if(openPrev)
    {
        ASSERT(*openPrev == this);
        if(openNext)
        {
            ASSERT(openNext->openPrev == &openNext);
        }
    }
    if(fileType == pfmFileTypeFolder)
    {
        for(File* child = data.folder.firstChild; !!child; child = child->sibNext)
        {
            ASSERT(child->parent == this);
            child->CheckConsistency();
        }
    }
}

void Volume::CheckConsistency(void)
{
    if(firstOpenFile)
    {
        ASSERT(firstOpenFile->openPrev == &firstOpenFile);
    }
    root.CheckConsistency();
}

#endif

void File::Opened(PfmOpenAttribs* openAttribs)
{
    openSequence ++;
    openAttribs->openId = openId;
    openAttribs->openSequence = openSequence;
    openAttribs->accessLevel = pfmAccessLevelWriteData;
    openAttribs->attribs.fileType = fileType;
    openAttribs->attribs.fileFlags = fileFlags;
    openAttribs->attribs.fileId = fileId;
    openAttribs->attribs.fileSize = 0;
    if(fileType == pfmFileTypeFile)
    {
        openAttribs->attribs.fileSize = data.file.fileSize;
    }
    openAttribs->attribs.createTime = createTime;
    openAttribs->attribs.accessTime = accessTime;
    openAttribs->attribs.writeTime = writeTime;
    openAttribs->attribs.changeTime = changeTime;
}

void File::Open(int64_t newOpenId,PfmOpenAttribs* openAttribs,int64_t* parentFileId,wchar_t** endName)
{
    if(!openPrev)
    {
        if(!openId)
        {
            openId = newOpenId;
        }
        openPrev = &(volume->firstOpenFile);
        openNext = *openPrev;
        if(openNext)
        {
            ASSERT(openNext->openPrev == openPrev);
            openNext->openPrev = &openNext;
        }
        *openPrev = this;
        volume->CheckConsistency();
    }
    Opened(openAttribs);
    if(parentFileId && parent)
    {
        *parentFileId = parent->fileId;
    }
    if(endName && name)
    {
        *endName = sswdup(name);
    }
}

void File::Flush(uint8_t newFileFlags,int64_t newCreateTime,int64_t newAccessTime,int64_t newWriteTime,int64_t newChangeTime)
{
    if(newFileFlags != pfmFileFlagsInvalid)
    {
        fileFlags = newFileFlags;
    }
    if(newCreateTime != pfmTimeInvalid)
    {
        createTime = newCreateTime;
    }
    if(newAccessTime != pfmTimeInvalid)
    {
        accessTime = newAccessTime;
    }
    if(newWriteTime != pfmTimeInvalid)
    {
        writeTime = newWriteTime;
    }
    if(newChangeTime != pfmTimeInvalid)
    {
        changeTime = newChangeTime;
    }
}

static size_t get_image_data(struct frame_headers * frame_headers, FILE * file, uint8_t * output_buffer, off_t offset, size_t max_size)
{
    int bpp = frame_headers->rawi_hdr.raw_info.bits_per_pixel;
    uint64_t pixel_start_index = MAX(0, offset) / 2; //lets hope offsets are always even for now
    uint64_t pixel_start_address = pixel_start_index * bpp / 16;
    size_t output_size = max_size - (offset < 0 ? (size_t)(-offset) : 0);
    uint64_t pixel_count = output_size / 2;
    uint64_t packed_size = (pixel_count + 2) * bpp / 16;
    uint16_t * packed_bits = static_cast<uint16_t *>(malloc(packed_size * sizeof(uint16_t)));
    if(packed_bits)
    {
        file_set_pos(file, frame_headers->position + frame_headers->vidf_hdr.frameSpace + sizeof(mlv_vidf_hdr_t) + pixel_start_address * 2, SEEK_SET);
        if(fread(packed_bits, (size_t)packed_size * 2, 1, file))
        {
            dng_get_image_data(frame_headers, packed_bits, output_buffer, offset, max_size);
        }
        else
        {
            fprintf(stderr, "Error reading source data");
        }
        free(packed_bits);
    }
    return max_size;
}

int/*error*/ File::Read(uint64_t fileOffset,void* inBuffer,size_t requestedSize,size_t* outActualSize)
{
    int error = pfmErrorInvalid;
    uint64_t startOffset = fileOffset;
    uint64_t endOffset = fileOffset;
    uint8_t* buffer = static_cast<uint8_t*>(inBuffer);
    if(fileType == pfmFileTypeFile)
    {
        error = 0;
        if(fileOffset < data.file.fileSize)
        {
            endOffset = fileOffset+requestedSize;
            if(endOffset < startOffset || endOffset > data.file.fileSize)
            {
                endOffset = data.file.fileSize;
            }
        }
        uint64_t offset = startOffset;
        while(offset < endOffset)
        {
            size_t blockIndex = static_cast<size_t>(offset/blockSize);
            size_t blockOffset = static_cast<size_t>(offset%blockSize);
            size_t partSize = blockSize-blockOffset;
            if(offset+partSize > endOffset)
            {
                partSize = static_cast<size_t>(endOffset-offset);
            }
            uint8_t* block = data.file.blockList[blockIndex];
            if(block)
            {
                memcpy(buffer+static_cast<size_t>(offset-startOffset),block+blockOffset,partSize);
            }
            else
            {
                memset(buffer+static_cast<size_t>(offset-startOffset),0,partSize);
            }
            offset += partSize;
        }
    }
    *outActualSize = static_cast<size_t>(endOffset-startOffset);
    return error;
}

int/*error*/ File::PrepSetSize(uint64_t newFileSize)
{
    ASSERT(fileType == pfmFileTypeFile);
    int error = 0;
    if(newFileSize > maxFileSize)
    {
        error = pfmErrorNoSpace;
    }
    else
    {
        size_t newBlockCount = static_cast<size_t>((newFileSize+blockSize-1)/blockSize);
        if(newBlockCount > data.file.blockCount)
        {
            newBlockCount += newBlockCount/4+4;
            uint8_t** newBlockList = static_cast<uint8_t**>(malloc(newBlockCount*sizeof(newBlockList[0])));
            if(!newBlockList)
            {
                error = pfmErrorNoSpace;
            }
            else
            {
                if(data.file.blockList)
                {
                    memcpy(newBlockList,data.file.blockList,data.file.blockCount*sizeof(newBlockList[0]));
                    free(data.file.blockList);
                }
                memset(newBlockList+data.file.blockCount,0,(newBlockCount-data.file.blockCount)*sizeof(newBlockList[0]));
                data.file.blockList = newBlockList;
                data.file.blockCount = newBlockCount;
            }
        }
    }
    return error;
}

int/*error*/ File::Write(uint64_t fileOffset,const void* inBuffer,size_t requestedSize,size_t* outActualSize)
{
    int error = pfmErrorInvalid;
    uint64_t startOffset = fileOffset;
    uint64_t endOffset = fileOffset;
    const uint8_t* buffer = static_cast<const uint8_t*>(inBuffer);
    if(fileType == pfmFileTypeFile)
    {
        error = 0;
        if(startOffset < maxFileSize)
        {
            endOffset = fileOffset+requestedSize;
            if(endOffset > maxFileSize)
            {
                endOffset = maxFileSize;
            }
            if(PrepSetSize(endOffset) != 0)
            {
                endOffset = startOffset;
            }
        }
        uint64_t offset = startOffset;
        while(offset < endOffset)
        {
            size_t blockIndex = static_cast<size_t>(offset/blockSize);
            size_t blockOffset = static_cast<size_t>(offset%blockSize);
            size_t partSize = blockSize-blockOffset;
            if(offset+partSize > endOffset)
            {
                partSize = static_cast<size_t>(endOffset-offset);
            }
            uint8_t* block = data.file.blockList[blockIndex];
            if(!block)
            {
                block = data.file.blockList[blockIndex] = static_cast<uint8_t*>(malloc(blockSize));
                if(block)
                {
                    volume->capacity += blockSize;
                    if(partSize != blockSize)
                    {
                        memset(block,0,blockSize);
                    }
                }
            }
            if(!block)
            {
                endOffset = offset;
            }
            else
            {
                memcpy(block+blockOffset,buffer+static_cast<size_t>(offset-startOffset),partSize);
                offset += partSize;
                if(offset > data.file.fileSize)
                {
                    data.file.fileSize = offset;
                }
            }
        }
    }
    *outActualSize = static_cast<size_t>(endOffset-startOffset);
    return error;
}

int/*error*/ File::SetSize(uint64_t newFileSize)
{
    int error = pfmErrorInvalid;
    if(fileType == pfmFileTypeFile)
    {
        error = PrepSetSize(newFileSize);
        if(!error)
        {
            if(newFileSize < data.file.fileSize)
            {
                    // Zero from new file size to lesser of old file size or the
                    // end of the will be end block.
                uint8_t* block;
                size_t blockIndex = static_cast<size_t>((newFileSize+blockSize-1)/blockSize);
                uint64_t zeroOffset = static_cast<uint64_t>(blockIndex)*blockSize;
                ASSERT(zeroOffset-newFileSize < blockSize);
                if(zeroOffset > newFileSize)
                {
                    block = data.file.blockList[blockIndex-1];
                    if(block)
                    {
                        memset(block+static_cast<size_t>(newFileSize%blockSize),0,static_cast<size_t>(zeroOffset-newFileSize));
                    }
                }
                    // Free any blocks past the new end block.
                size_t blockCount = static_cast<size_t>((data.file.fileSize+blockSize-1)/blockSize);
                while(blockIndex < blockCount)
                {
                    block = data.file.blockList[blockIndex];
                    if(block)
                    {
                        ASSERT(volume->capacity >= blockSize);
                        volume->capacity -= blockSize;
                        free(block);
                        data.file.blockList[blockIndex] = 0;
                    }
                    blockIndex ++;
                }
            }
            data.file.fileSize = newFileSize;
        }
    }
    return error;
}

void FileList::Iterate(PfmMarshallerListResult* listResult)
{
    uint8_t/*bool*/ needMore;
    do
    {
        needMore = false;
        File* file = *position;
        if(!file)
        {
            listResult->NoMore();
        }
        else
        {
            PfmAttribs attribs;
            memset(&attribs,0,sizeof(attribs));
            attribs.fileType = file->fileType;
            attribs.fileFlags = file->fileFlags;
            attribs.fileId = file->fileId;
            if(attribs.fileType == pfmFileTypeFile)
            {
                attribs.fileSize = file->data.file.fileSize;
            }
            attribs.createTime = file->createTime;
            attribs.accessTime = file->accessTime;
            attribs.writeTime = file->writeTime;
            attribs.changeTime = file->changeTime;
            if(listResult->Add(&attribs,file->name,&needMore))
            {
                position = &(file->sibNext);
            }
        }
    } while(needMore);
}

FileList::FileList(File* inFile,int64_t inListId)
{
    file = inFile;
    ASSERT(file->fileType == pfmFileTypeFolder);
    prev = &(file->data.folder.firstList);
    next = *prev;
    if(next)
    {
        ASSERT(next->prev == prev);
        next->prev = &next;
    }
    *prev = this;
    listId = inListId;
    position = &(file->data.folder.firstChild);
}

FileList::~FileList(void)
{
    ASSERT(prev && *prev == this);
    *prev = next;
    if(next)
    {
        ASSERT(next->prev == &next);
        next->prev = prev;
    }
}

int/*error*/ File::ListFactory(int64_t listId,FileList** outList)
{
    int error = 0;
    FileList* list = 0;
    if(fileType != pfmFileTypeFolder)
    {
        error = pfmErrorAccessDenied;
    }
    else
    {
        list = data.folder.firstList;
        while(list && list->listId != listId)
        {
            list = list->next;
        }
        if(!list)
        {
            list = new FileList(this,listId);
            if(!list)
            {
                error = pfmErrorOutOfMemory;
            }
        }
    }
    *outList = list;
    return error;
}

int/*error*/ File::ListEnd(int64_t listId)
{
    int error = pfmErrorNotFound;
    if(fileType == pfmFileTypeFolder)
    {
        FileList* list = data.folder.firstList;
        while(list && list->listId != listId)
        {
            list = list->next;
        }
        if(list)
        {
            error = 0;
            delete list;
        }
    }
    return error;
}

File::File(Volume* inVolume)
{
    volume = inVolume;
    parent = 0;
    sibPrev = 0;
    openPrev = 0;
    openId = 0;
    openSequence = 0;
    name = 0;
    fileType = 0;
    fileFlags = 0;
    memset(&data,0,sizeof(data));
    type = TEMPORARY;
    workspace = NULL;
}

File::~File(void)
{
    ASSERT(!sibPrev && !openPrev);
    free(name);
    switch(fileType)
    {
    case pfmFileTypeFile:
        if(data.file.blockList)
        {
            for(size_t blockIndex = 0; blockIndex < data.file.blockCount; blockIndex ++)
            {
                uint8_t* block = data.file.blockList[blockIndex];
                if(block)
                {
                    ASSERT(volume->capacity >= blockSize);
                    volume->capacity -= blockSize;
                    free(block);
                }
            }
            free(data.file.blockList);
        }
        break;
    case pfmFileTypeFolder:
        ASSERT(!data.folder.firstChild);
        while(data.folder.firstList)
        {
            delete data.folder.firstList;
        }
        break;
    }
    if(workspace) free(workspace);
}

int/*error*/ File::Init(File* inParent,File** inSibPrev,const wchar_t* inName,int8_t inFileType,uint8_t inFileFlags,int64_t inWriteTime)
{
    name = sswdup(inName);
    int error = pfmErrorOutOfMemory;
    if(name)
    {
        error = 0;
        parent = inParent;
        sibPrev = inSibPrev;
        sibNext = *sibPrev;
        if(sibNext)
        {
            ASSERT(sibNext->parent == parent && sibNext->sibPrev == sibPrev);
            sibNext->sibPrev = &sibNext;
        }
        *sibPrev = this;
        volume->CheckConsistency();
        fileType = inFileType;
        fileFlags = inFileFlags;
        fileId = ++(volume->lastFileId);
        createTime = accessTime = writeTime = changeTime = inWriteTime;
        parent->accessTime = parent->writeTime = parent->changeTime = inWriteTime;
    }
    return error;
}

void File::Close(int64_t checkOpenSequence)
{
    if(checkOpenSequence >= openSequence)
    {
        if(workspace)
        {
            free(workspace);
            workspace = NULL;
        }
    }
    if(openPrev && checkOpenSequence >= openSequence)
    {
        ASSERT(*openPrev == this);
        *openPrev = openNext;
        if(openNext)
        {
            ASSERT(openNext->openPrev == &openNext);
            openNext->openPrev = openPrev;
        }
        openPrev = 0;
        volume->CheckConsistency();
    }
    if(!sibPrev && !openPrev && this != &(volume->root))
    {
        delete this;
    }
}

void File::Delete(int64_t writeTime,bool leaveChildren)
{
    if(sibPrev)
    {
        if(!leaveChildren && fileType == pfmFileTypeFolder)
        {
            while(data.folder.firstChild)
            {
                data.folder.firstChild->Delete(writeTime,false/*leaveChildren*/);
            }
        }
            // Make sure no list iterators are referencing this file.
        for(FileList* list = parent->data.folder.firstList; !!list; list = list->next)
        {
            if(list->position == &sibNext)
            {
                list->position = sibPrev;
            }
        }
        ASSERT(*sibPrev == this);
        *sibPrev = sibNext;
        if(sibNext)
        {
            ASSERT(sibNext->sibPrev == &sibNext);
            sibNext->sibPrev = sibPrev;
        }
        if(writeTime != pfmTimeInvalid)
        {
            parent->writeTime = writeTime;
        }
        parent = 0;
        sibPrev = 0;
        volume->CheckConsistency();
        free(name);
        name = 0;
    }
    if(!sibPrev && !openPrev && this != &(volume->root))
    {
        delete this;
    }
}

int/*error*/ File::Move(File* inParent,File** inSibPrev,const wchar_t* inNewName,int64_t writeTime)
{
    int error = 0;
    File* check = parent;
    while(check && check != this)
    {
        check = check->parent;
    }
    if(check)
    {
            // Can't move a folder into itself.
        error = pfmErrorInvalid;
    }
    else
    {
        wchar_t* newName = sswdup(inNewName);
        if(!newName)
        {
            error = pfmErrorOutOfMemory;
        }
        else if(inParent == parent)
        {
                // Simple rename, not changing parent folders.
            free(name);
            name = newName;
        }
        else
        {
            Delete(writeTime,true/*leaveChildren*/);
            name = newName;
            parent = inParent;
            sibPrev = inSibPrev;
            sibNext = *sibPrev;
            if(sibNext)
            {
                ASSERT(sibNext->parent == parent && sibNext->sibPrev == sibPrev);
                sibNext->sibPrev = &sibNext;
            }
            *sibPrev = this;
            volume->CheckConsistency();
        }
        if(!error && writeTime != pfmTimeInvalid)
        {
            parent->writeTime = writeTime;
        }
    }
    return error;
}

int/*error*/ Volume::FileFactory(File* parent,File** sibPrev,const wchar_t* name,int8_t fileType,uint8_t fileFlags,int64_t writeTime,File** outFile)
{
    ASSERT(parent && sibPrev && name);
    File* file = new File(this);
    int error = pfmErrorOutOfMemory;
    if(file)
    {
        error = file->Init(parent,sibPrev,name,fileType,fileFlags,writeTime);
        if(error)
        {
            delete file;
            file = 0;
        }
    }
    *outFile = file;
    return error;
}

int/*error*/ Volume::FindFile(const PfmNamePart* nameParts,size_t namePartCount,File** outFile,File** outParent,File*** outSibPrev)
{
    int error = 0;
    File* file = &root;
    File* parent = 0;
    File** sibPrev = 0;
    size_t namePartIndex = 0;
    while(file && namePartIndex < namePartCount && file->fileType == pfmFileTypeFolder)
    {
        parent = file;
        sibPrev = &(file->data.folder.firstChild);
        const wchar_t* name = nameParts[namePartIndex].name;
        while((file = *sibPrev) != 0 && sswcmpf(name,file->name) != 0)
        {
            sibPrev = &(file->sibNext);
        }
        namePartIndex ++;
    }
    if(namePartIndex < namePartCount)
    {
        error = pfmErrorParentNotFound;
    }
    *outFile = file;
    *outParent = parent;
    *outSibPrev = sibPrev;
    return error;
}

int/*error*/ Volume::FindOpenFile(int64_t openId,File** outFile)
{
    int error = 0;
    File* file = firstOpenFile;
    while(file && file->openId != openId)
    {
        file = file->openNext;
    }
    if(!file)
    {
        error = pfmErrorInvalid;
    }
    *outFile = file;
    return error;
}

void CCALL Volume::ReleaseName(wchar_t* name)
{
    free(name);
}

int/*error*/ CCALL Volume::Open(const PfmNamePart* nameParts,size_t namePartCount,int8_t createFileType,uint8_t createFileFlags,int64_t writeTime,int64_t newCreateOpenId,int8_t existingAccessLevel,int64_t newExistingOpenId,uint8_t/*bool*/ * existed,PfmOpenAttribs* openAttribs,int64_t* parentFileId,wchar_t** endName)
{
    File* file;
    File* parent;
    File** sibPrev;
    int error = FindFile(nameParts,namePartCount,&file,&parent,&sibPrev);
    if(!error)
    {
        if(file)
        {
            *existed = true;
            file->Open(newExistingOpenId,openAttribs,parentFileId,endName);
        }
        else
        {
                // Name must have 1 or more parts or would have found root.
            ASSERT(namePartCount && parent && sibPrev);
            *existed = false;
            if(createFileType == pfmFileTypeNone)
            {
                error = pfmErrorNotFound;
            }
            else
            {
                if((wcsncmp(nameParts[namePartCount-1].name, mlvBaseFileName, wcslen(mlvBaseFileName)) == 0) &&
                   (wcslen(nameParts[namePartCount-1].name) >= 4) &&
                   (wcsncmp(&nameParts[namePartCount-1].name[wcslen(nameParts[namePartCount-1].name) - 4], L".xmp", 4) == 0))
                {
                    wchar_t *mappedFileName = concatenate(mappedDirectory, L"\\", nameParts[namePartCount - 1].name);
                    HANDLE mappedFile;
                    mappedFile = CreateFileW(mappedFileName, 0, 0, NULL, CREATE_ALWAYS, 0, NULL);
                    if(mappedFile != INVALID_HANDLE_VALUE)
                    {
                        FileClose(mappedFile);
                        error = FileFactory(parent,sibPrev,nameParts[namePartCount-1].name,createFileType,createFileFlags,writeTime,&file);
                        file->type = MAPPED;
                    }
                    else
                    {
                        error = pfmErrorFailed;
                    }
                    free(mappedFileName);
                }
                else
                {
                    error = FileFactory(parent,sibPrev,nameParts[namePartCount-1].name,createFileType,createFileFlags,writeTime,&file);
                }
                if(!error)
                {
                    file->Open(newCreateOpenId,openAttribs,parentFileId,endName);
                }
            }
        }
    }
    return error;
}

int/*error*/ CCALL Volume::Replace(int64_t targetOpenId,int64_t targetParentFileId,const PfmNamePart* targetEndName,uint8_t createFileFlags,int64_t writeTime,int64_t newCreateOpenId,PfmOpenAttribs* openAttribs)
{
    File* target;
    int error = FindOpenFile(targetOpenId,&target);
    if(!error)
    {
        if(target == &root)
        {
                // Can't replace root.
            error = pfmErrorAccessDenied;
        }
        else if(!target->name)
        {
            error = pfmErrorDeleted;
        }
        else if(target->type == VIRTUAL)
        {
            error = pfmErrorFailed;
        }
        else if(target->type == MAPPED)
        {
            error = pfmErrorFailed;
        }
        else
        {
            File* file;
            error = FileFactory(target->parent,target->sibPrev,target->name,target->fileType,createFileFlags,writeTime,&file);
            if(!error)
            {
                target->Delete(writeTime,false/*leaveChildren*/);
                file->Open(newCreateOpenId,openAttribs,0,0);
            }
        }
    }
    return error;
}

int/*error*/ CCALL Volume::Move(int64_t sourceOpenId,int64_t sourceParentFileId,const PfmNamePart* sourceEndName,const PfmNamePart* targetNameParts,size_t targetNamePartCount,uint8_t/*bool*/ deleteSource,int64_t writeTime,int8_t existingAccessLevel,int64_t newExistingOpenId,uint8_t/*bool*/ * existed,PfmOpenAttribs* openAttribs,int64_t* parentFileId,wchar_t** endName)
{
    File* file;
    int error = FindOpenFile(sourceOpenId,&file);
    if(!error)
    {
        if(file->type == VIRTUAL) return pfmErrorFailed;
        if(file->type == MAPPED) return pfmErrorFailed;
        File* target;
        File* parent;
        File** sibPrev;
        error = FindFile(targetNameParts,targetNamePartCount,&target,&parent,&sibPrev);
        if(!error)
        {
            if(target && (target->type == VIRTUAL)) return pfmErrorFailed;
            if(target && (target->type == MAPPED)) return pfmErrorFailed;
                // Watch for and allow case change rename. ("FILE.TXT" -> "File.txt")
            if(target && (!targetNamePartCount || target != file))
            {
                *existed = true;
                target->Open(newExistingOpenId,openAttribs,parentFileId,endName);
            }
            else if(file->sibPrev && !deleteSource)
            {
                    // Links are not supported.
                error = pfmErrorInvalid;
            }
            else
            {
                    // Target name must have 1 or more parts or would have found root.
                ASSERT(targetNamePartCount && parent && sibPrev);
                *existed = false;
                error = file->Move(parent,sibPrev,targetNameParts[targetNamePartCount-1].name,writeTime);
                if(!error)
                {
                    file->Open(sourceOpenId,openAttribs,parentFileId,endName);
                }
            }
        }
    }
    return error;
}

int/*error*/ CCALL Volume::MoveReplace(int64_t sourceOpenId,int64_t sourceParentFileId,const PfmNamePart* sourceEndName,int64_t targetOpenId,int64_t targetParentFileId,const PfmNamePart* targetEndName,uint8_t/*bool*/ deleteSource,int64_t writeTime)
{
    File* file;
    int error = FindOpenFile(sourceOpenId,&file);
    if(!error)
    {
        if(file->type == VIRTUAL) return pfmErrorFailed;
        if(file->type == MAPPED) return pfmErrorFailed;
        File* target;
        error = FindOpenFile(targetOpenId,&target);
        if(!error)
        {
            if(target->type == VIRTUAL) return pfmErrorFailed;
            if(target->type == MAPPED) return pfmErrorFailed;
            if(target == &root)
            {
                    // Can't replace root.
                error = pfmErrorAccessDenied;
            }
            else if(!target->name)
            {
                error = pfmErrorDeleted;
            }
            else if(file->sibPrev && !deleteSource)
            {
                    // Links are not supported.
                error = pfmErrorInvalid;
            }
            else if(file == target)
            {
                error = pfmErrorInvalid;
            }
            else
            {
                error = file->Move(target->parent,target->sibPrev,target->name,writeTime);
                if(!error)
                {
                    target->Delete(writeTime,false/*leaveChildren*/);
                }
            }
        }
    }
    return error;
}

int/*error*/ CCALL Volume::Delete(int64_t openId,int64_t parentFileId,const PfmNamePart* endName,int64_t writeTime)
{
    File* file;
    int error = FindOpenFile(openId,&file);
    if(!error)
    {
        if(file == &root)
        {
                // Can't delete root.
            error = pfmErrorAccessDenied;
        }
        else if(!file->name)
        {
                // Already deleted.
        }
        else if(file->type == VIRTUAL)
        {
            error = pfmErrorFailed;
        }
        else if(file->type == MAPPED)
        {
            wchar_t *mappedFileName = concatenate(mappedDirectory, L"\\", file->name);
            if(!DeleteFileW(mappedFileName))
            {
                error = pfmErrorFailed;
            }
            free(mappedFileName);
            file->Delete(writeTime,false/*leaveChildren*/);
        }
        else if(file->fileType == pfmFileTypeFolder && file->data.folder.firstChild)
        {
                // Don't allow non empty folder to be deleted
            error = pfmErrorNotEmpty;
        }
        else
        {
            file->Delete(writeTime,false/*leaveChildren*/);
        }
    }
    return error;
}

int/*error*/ CCALL Volume::Close(int64_t openId,int64_t openSequence)
{
    File* file;
    int error = FindOpenFile(openId,&file);
    if(!error)
    {
        file->Close(openSequence);
    }
    return error;
}

int/*error*/ CCALL Volume::FlushFile(int64_t openId,uint8_t flushFlags,uint8_t fileFlags,uint8_t color,int64_t createTime,int64_t accessTime,int64_t writeTime,int64_t changeTime,PfmOpenAttribs* openAttribs)
{
    File* file;
    int error = FindOpenFile(openId,&file);
    if(!error)
    {
        file->Flush(fileFlags,createTime,accessTime,writeTime,changeTime);
        if(flushFlags&pfmFlushFlagOpen)
        {
            file->Opened(openAttribs);
        }
    }
    return error;
}

int/*error*/ CCALL Volume::List(int64_t openId,int64_t listId,PfmMarshallerListResult* listResult)
{
    File* file;
    int error = FindOpenFile(openId,&file);
    if(!error)
    {
        FileList* list;
        error = file->ListFactory(listId,&list);
        if(!error)
        {
            list->Iterate(listResult);
        }
    }
    return error;
}

int/*error*/ CCALL Volume::ListEnd(int64_t openId,int64_t listId)
{
    File* file;
    int error = FindOpenFile(openId,&file);
    if(!error)
    {
        error = file->ListEnd(listId);
    }
    return error;
}

size_t Volume::ReadVirtualDNG(uint32_t frameNumber, uint64_t fileOffset, File *file, uint8_t *buffer, size_t requestedSize)
{
    uint64_t read = 0;
    size_t header_size = dng_get_header_size();
    size_t image_size = file->data.file.fileSize - header_size;

    if(fileOffset + requestedSize > file->data.file.fileSize)
    {
        requestedSize = file->data.file.fileSize - fileOffset;
    }

    if(processing == PROCESSING_NONE)
    {
        if(fileOffset >= header_size)
        {
            read += get_image_data(&frameHeaders[frameNumber],
                chunks[frameHeaders[frameNumber].fileNumber],
                buffer,
                fileOffset - header_size,
                requestedSize);
        }
        else
        {
            size_t remaining = MIN(requestedSize, header_size - fileOffset);
            read += dng_get_header_data(&frameHeaders[frameNumber], buffer, fileOffset, remaining);
            if(remaining < requestedSize)
            {
                read += get_image_data(&frameHeaders[frameNumber],
                    chunks[frameHeaders[frameNumber].fileNumber],
                    buffer + remaining,
                    0,
                    requestedSize - remaining);
            }
        }
    }
    else
    {
        if(!file->workspace)
        {
            file->workspace = static_cast<uint8_t *>(malloc(file->data.file.fileSize));
            dng_get_header_data(&frameHeaders[frameNumber], file->workspace, 0, header_size);
            get_image_data(&frameHeaders[frameNumber],
                chunks[frameHeaders[frameNumber].fileNumber],
                file->workspace + header_size,
                0,
                image_size);

            if(processing & PROCESSING_DUAL_ISO)
            {
                marshaller->Printf(L"Processing for dual ISO\n");
                hdr_convert_data(&frameHeaders[frameNumber], (uint16_t *)(file->workspace + header_size), 0, image_size);
            }
            if(processing & PROCESSING_CHROMA_SMOOTHING)
            {
            }
            if(processing & PROCESSING_VERTICAL_STRIPES)
            {
            }
        }
        memcpy(buffer, file->workspace + fileOffset, requestedSize);
        read = requestedSize;
    }

    return static_cast<size_t>(read);
}

size_t Volume::ReadVirtualWAV(uint64_t fileOffset, File *file, uint8_t *buffer, size_t requestedSize)
{
    return wav_get_data_direct(chunks, index, &audioHeader, file->data.file.fileSize, buffer, fileOffset, requestedSize);
}

int/*error*/ CCALL Volume::Read(int64_t openId,uint64_t fileOffset,void* data,size_t requestedSize,size_t* outActualSize)
{
    File* file;
    int error = FindOpenFile(openId,&file);
    if(!error)
    {
        if(file->type == VIRTUAL)
        {
            uint8_t* buffer = static_cast<uint8_t*>(data);
            // By design, virtual files are distinguished by fileId, not name
            //   2 to videoFrameCount+1: DNG frames
            //   videoFrameCount+2: WAV audio (if present)
            uint32_t frameNumber = file->fileId - 2;
            if(frameNumber < videoFrameCount) // DNG frame
            {
                *outActualSize = ReadVirtualDNG(frameNumber, fileOffset, file, buffer, requestedSize);
            }
            else if(frameNumber == videoFrameCount) // WAV audio
            {
                *outActualSize = ReadVirtualWAV(fileOffset, file, buffer, requestedSize);
            }
            else
            {
                return pfmErrorNotFound;
            }
        }
        else if(file->type == MAPPED)
        {
            wchar_t *mappedFileName = concatenate(mappedDirectory, L"\\", file->name);
            HANDLE mappedFile;
            FileOpenRead(mappedFileName, &mappedFile);
            FileSetPointer(mappedFile, fileOffset);
            FileRead(mappedFile, data, requestedSize, outActualSize);
            FileClose(mappedFile);
            free(mappedFileName);
        }
        else
        {
            error = file->Read(fileOffset,data,requestedSize,outActualSize);
        }
    }
    return error;
}

int/*error*/ CCALL Volume::Write(int64_t openId,uint64_t fileOffset,const void* data,size_t requestedSize,size_t* outActualSize)
{
    File* file;
    int error = FindOpenFile(openId,&file);
    if(!error)
    {
        if(file->type == VIRTUAL)
        {
            error = pfmErrorFailed;
        }
        else if(file->type == MAPPED)
        {
            wchar_t *mappedFileName = concatenate(mappedDirectory, L"\\", file->name);
            HANDLE mappedFile;
            FileOpenWrite(mappedFileName, &mappedFile);
            FileSetPointer(mappedFile, fileOffset);
            FileWrite(mappedFile, data, requestedSize, outActualSize);
            FileClose(mappedFile);
            free(mappedFileName);
        }
        else
        {
            error = file->Write(fileOffset,data,requestedSize,outActualSize);
        }
    }
    return error;
}

int/*error*/ CCALL Volume::SetSize(int64_t openId,uint64_t fileSize)
{
    File* file;
    int error = FindOpenFile(openId,&file);
    if(!error)
    {
        if(file->type == VIRTUAL)
        {
            error = pfmErrorFailed;
        }
        else if(file->type == MAPPED)
        {
            wchar_t *mappedFileName = concatenate(mappedDirectory, L"\\", file->name);
            HANDLE mappedFile;
            FileOpenWrite(mappedFileName, &mappedFile);
            FileSetSize(mappedFile, fileSize);
            FileClose(mappedFile);
            free(mappedFileName);
            file->data.file.fileSize = fileSize;
        }
        else
        {
            error = file->SetSize(fileSize);
        }
    }
    return error;
}

int/*error*/ CCALL Volume::Capacity(uint64_t* totalCapacity,uint64_t* availableCapacity)
{
    *totalCapacity = capacity;
    *availableCapacity = (100000000/File::blockSize)*File::blockSize;
    return 0;
}

int/*error*/ CCALL Volume::FlushMedia(uint8_t/*bool*/ * mediaClean)
{
    return 0;
}

int/*error*/ CCALL Volume::Control(int64_t openId,int8_t accessLevel,int controlCode,const void* input,size_t inputSize,void* output,size_t maxOutputSize,size_t* outputSize)
{
    return pfmErrorInvalid;
}

int/*error*/ CCALL Volume::MediaInfo(int64_t openId,PfmMediaInfo* mediaInfo,wchar_t** mediaLabel)
{
    return 0;
}

int/*error*/ CCALL Volume::Access(int64_t openId,int8_t accessLevel,PfmOpenAttribs* openAttribs)
{
    File* file;
    int error = FindOpenFile(openId,&file);
    if(!error)
    {
        file->Opened(openAttribs);
    }
    return error;
}

Volume::Volume(void)
:
    root(this)
{
    marshaller = 0;
    capacity = 0;
    lastFileId = 1;
    root.fileType = pfmFileTypeFolder;
    root.fileFlags = 0;
    root.fileId = 1;
    root.createTime = root.accessTime = root.writeTime = root.changeTime = 0;
    firstOpenFile = 0;

    mlvBaseFileName = NULL;
    mappedDirectory = NULL;
    index = NULL;
    videoFrameCount = 0;
    audioFrameCount = 0;
    chunks = NULL;
    chunkCount = 0;
    frameHeaders = NULL;
    processing = PROCESSING_NONE;
}

Volume::~Volume(void)
{
    ASSERT(!marshaller);
    while(firstOpenFile)
    {
        firstOpenFile->Close(firstOpenFile->openSequence);
    }
    while(root.data.folder.firstChild)
    {
        root.data.folder.firstChild->Delete(pfmTimeInvalid,false/*leaveChildren*/);
    }

    if(!RemoveDirectoryW(mappedDirectory))
    {
        if(GetLastError() == ERROR_DIR_NOT_EMPTY)
        {
            marshaller->Printf(L"mappedDirectory: not empty\n");
        }
    }

    marshaller->Release();

    if(mlvBaseFileName) free(mlvBaseFileName);
    if(mappedDirectory) free(mappedDirectory);
    if(chunks) close_chunks(chunks, chunkCount);
    if(index) free(index);
    if(frameHeaders) free(frameHeaders);
}

int/*systemError*/ Volume::Init(const wchar_t* mlvFileName)
{
    int error = PfmMarshallerFactory(&marshaller);
    if(!error)
    {
        marshaller->SetTrace(L"MLVFS");

        mappedDirectory = sswdup(mlvFileName);
        wchar_t *loc = NULL;
        if(sswcmpf(loc = wcsrchr(mappedDirectory, '.'), L".mlv") == 0)
        {
            wcsncpy(loc, L".MLD", 4);
        }
        else
        {
            wchar_t *temp = concatenate(mappedDirectory, L".MLD");
            free(mappedDirectory);
            mappedDirectory = temp;
        }
        marshaller->Printf(L"mappedDirectory: %s\n", mappedDirectory);

        // Get the base filename without the path or extension
        const wchar_t *start = wcsrchr(mlvFileName, L'\\') + 1;
        const wchar_t *end = wcsrchr(mlvFileName, L'.');
        mlvBaseFileName = static_cast<wchar_t *>(malloc((end - start + 1) * sizeof(wchar_t)));
        wcsncpy(mlvBaseFileName, start, end - start);
        mlvBaseFileName[end - start] = L'\0';

        // Get the file path as a multi-byte string
        char mlvFileName_mbs[1024];
        size_t count;
        wcstombs_s(&count, mlvFileName_mbs, 1024, mlvFileName, _TRUNCATE);

        // Retrieve the index (does not generate IDX file)
        index = get_new_index(mlvFileName_mbs);

        // Count the number of VIDF frames    
        mlv_xref_t *xrefs = (mlv_xref_t *)&(((uint8_t*)index)[sizeof(mlv_xref_hdr_t)]);
        for(uint32_t block_xref_pos = 0; block_xref_pos < index->entryCount; block_xref_pos++)
        {
            switch(xrefs[block_xref_pos].frameType)
            {
                case MLV_FRAME_VIDF:
                    videoFrameCount++;
                    break;

                case MLV_FRAME_AUDF:
                    audioFrameCount++;
                    break;

                case MLV_FRAME_UNSPECIFIED:
                default:
                    break;
            }
        }

        // Open all of the files
        chunks = load_chunks(mlvFileName_mbs, &chunkCount);

        // Load all the frame headers
        frameHeaders = static_cast<struct frame_headers *>(malloc(videoFrameCount * sizeof(struct frame_headers)));
        memset(&frameHeaders[0], 0, sizeof(struct frame_headers));
        uint32_t vidf_counter = 0;
        mlv_hdr_t mlv_hdr;
        uint32_t hdr_size;
        for(uint32_t block_xref_pos = 0; (block_xref_pos < index->entryCount) && (vidf_counter < videoFrameCount); block_xref_pos++)
        {
            /* get the file and position of the next block */
            uint32_t in_file_num = xrefs[block_xref_pos].fileNumber;
            int64_t position = xrefs[block_xref_pos].frameOffset;

            /* select file */
            FILE *in_file = chunks[in_file_num];

            switch(xrefs[block_xref_pos].frameType)
            {
                case MLV_FRAME_VIDF:
                    //Uses the number in sequence rather than frameNumber in header
                    frameHeaders[vidf_counter].fileNumber = in_file_num;
                    frameHeaders[vidf_counter].position = position;
                    file_set_pos(in_file, position, SEEK_SET);
                    fread(&mlv_hdr, sizeof(mlv_hdr_t), 1, in_file);
                    file_set_pos(in_file, position, SEEK_SET);
                    hdr_size = MIN(sizeof(mlv_vidf_hdr_t), mlv_hdr.blockSize);
                    fread(&frameHeaders[vidf_counter].vidf_hdr, hdr_size, 1, in_file);
                    vidf_counter++;
                    if(vidf_counter < videoFrameCount)
                    {
                        memcpy(&frameHeaders[vidf_counter], &frameHeaders[vidf_counter - 1], sizeof(struct frame_headers));
                    }
                    break;

                case MLV_FRAME_AUDF:
                    break;

                case MLV_FRAME_UNSPECIFIED:
                default:
                    file_set_pos(in_file, position, SEEK_SET);
                    fread(&mlv_hdr, sizeof(mlv_hdr_t), 1, in_file);
                    file_set_pos(in_file, position, SEEK_SET);
                    if(!memcmp(mlv_hdr.blockType, "MLVI", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_file_hdr_t), mlv_hdr.blockSize);
                        fread(&frameHeaders[vidf_counter].file_hdr, hdr_size, 1, in_file);
                    }
                    else if(!memcmp(mlv_hdr.blockType, "RTCI", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_rtci_hdr_t), mlv_hdr.blockSize);
                        fread(&frameHeaders[vidf_counter].rtci_hdr, hdr_size, 1, in_file);
                    }
                    else if(!memcmp(mlv_hdr.blockType, "IDNT", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_idnt_hdr_t), mlv_hdr.blockSize);
                        fread(&frameHeaders[vidf_counter].idnt_hdr, hdr_size, 1, in_file);
                    }
                    else if(!memcmp(mlv_hdr.blockType, "RAWI", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_rawi_hdr_t), mlv_hdr.blockSize);
                        fread(&frameHeaders[vidf_counter].rawi_hdr, hdr_size, 1, in_file);
                    }
                    else if(!memcmp(mlv_hdr.blockType, "EXPO", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_expo_hdr_t), mlv_hdr.blockSize);
                        fread(&frameHeaders[vidf_counter].expo_hdr, hdr_size, 1, in_file);
                    }
                    else if(!memcmp(mlv_hdr.blockType, "LENS", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_lens_hdr_t), mlv_hdr.blockSize);
                        fread(&frameHeaders[vidf_counter].lens_hdr, hdr_size, 1, in_file);
                    }
                    else if(!memcmp(mlv_hdr.blockType, "WBAL", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_wbal_hdr_t), mlv_hdr.blockSize);
                        fread(&frameHeaders[vidf_counter].wbal_hdr, hdr_size, 1, in_file);
                    }
                    else if(!memcmp(mlv_hdr.blockType, "WAVI", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_wavi_hdr_t), mlv_hdr.blockSize);
                        fread(&audioHeader, hdr_size, 1, in_file);
                    }
            }
        }

        if(!CreateDirectoryW(mappedDirectory, NULL))
        {
            if(GetLastError() == ERROR_ALREADY_EXISTS)
            {
                marshaller->Printf(L"mappedDirectory: already exists\n");
            }
        }

        // Determine processing
        if(wcsstr(mlvFileName, L"DUAL") || wcsstr(mlvFileName, L"Dual") || wcsstr(mlvFileName, L"dual"))
        {
            processing |= PROCESSING_DUAL_ISO;
        }

        File *outfile;
        File **sibPrev = &root.data.folder.firstChild;
        wchar_t filename[1024];
        SYSTEMTIME st;
        FILETIME ft, ftl;
        uint64_t time;
        for(uint32_t counter = 0; counter < videoFrameCount; counter++)
        {
            // Create a virtual DNG
            wsprintfW(filename, L"%s_%06d.dng", mlvBaseFileName, counter);

            // Generate timestamp in Windows time
            st.wYear = frameHeaders[counter].rtci_hdr.tm_year + 1900;
            st.wMonth = frameHeaders[counter].rtci_hdr.tm_mon + 1;
            st.wDay = frameHeaders[counter].rtci_hdr.tm_mday;
            st.wHour = frameHeaders[counter].rtci_hdr.tm_hour;
            st.wMinute = frameHeaders[counter].rtci_hdr.tm_min;
            st.wSecond = frameHeaders[counter].rtci_hdr.tm_sec;
            st.wMilliseconds = 0;
            SystemTimeToFileTime(&st, &ft);
            LocalFileTimeToFileTime(&ft, &ftl);
            time = (((uint64_t)ftl.dwHighDateTime) << 32 | ftl.dwLowDateTime) +
                (frameHeaders[counter].vidf_hdr.timestamp - frameHeaders[counter].rtci_hdr.timestamp) * 10;

            FileFactory(&root, sibPrev, filename, pfmFileTypeFile, 0, time, &outfile);
            outfile->type = VIRTUAL;
            outfile->data.file.fileSize = dng_get_size(&frameHeaders[counter]);
            sibPrev = &(outfile->sibNext);
        }

        // Create a virtual WAV
        if(audioFrameCount > 0)
        {
            wsprintfW(filename, L"%s.wav", mlvBaseFileName);
            FileFactory(&root, sibPrev, filename, pfmFileTypeFile, 0, time, &outfile);
            outfile->type = VIRTUAL;
            outfile->data.file.fileSize = wav_get_size(mlvFileName_mbs);
            sibPrev = &(outfile->sibNext);
        }

        // Create mapped XMP files
        wchar_t *searchPath = concatenate(mappedDirectory, L"\\", mlvBaseFileName, L"_*.xmp");
        WIN32_FIND_DATAW FindFileData;
        HANDLE hFind;
        hFind = FindFirstFileW(searchPath, &FindFileData);
        while(hFind != INVALID_HANDLE_VALUE)
        {
            marshaller->Printf(L"%s\n", FindFileData.cFileName);

            time = BUILD64(FindFileData.ftLastWriteTime.dwHighDateTime, FindFileData.ftLastWriteTime.dwLowDateTime);
            FileFactory(&root, sibPrev, FindFileData.cFileName, pfmFileTypeFile, 0, time, &outfile);
            outfile->type = MAPPED;
            outfile->data.file.fileSize = BUILD64(FindFileData.nFileSizeHigh, FindFileData.nFileSizeLow);
            sibPrev = &(outfile->sibNext);

            if(!FindNextFileW(hFind, &FindFileData))
            {
                FindClose(hFind);
                hFind = INVALID_HANDLE_VALUE;
            }
        }

        free(searchPath);
    }
    return error;
}

void CCALL MlvFormatter::Release(void)
{
}

int/*systemError*/ CCALL MlvFormatter::Identify(
    HANDLE statusWrite,
    const wchar_t* mountFileName,
    HANDLE mountFileHandle,
    const void* mountFileData,
    size_t mountFileDataSize)
{
    PfmMarshaller* marshaller;
    int error = PfmMarshallerFactory(&marshaller);
    if(!error)
    {
        // Mount any file with an .mlv extension
        if(sswcmpf(wcsrchr(mountFileName,'.'),L".mlv") != 0)
        {
            // Or mount any file with "MLVI" as the first four bytes
            error = (mountFileDataSize/sizeof(char) < 4 ||
                strncmp(static_cast<const char*>(mountFileData), "MLVI", 4) != 0);
        }
        marshaller->Release();
    }
    return error;
}

int/*systemError*/ CCALL MlvFormatter::Serve(
    const wchar_t* mountFileName,
    int mountFlags,
    HANDLE read,
    HANDLE write)
{
    Volume volume;
    int error = volume.Init(mountFileName);
    if(!error)
    {
        volume.marshaller->ServeReadWrite(&volume,0,mlvfsFormatterName,read,write);
    }
    return error;
}

void CCALL MlvFormatter::Cancel(void)
{
}

extern "C" PT_EXPORT int/*systemError*/ CCALL PfmFormatterFactory1(
    PfmFormatter** formatter)
{
    *formatter = &mlvFormatter;
    return 0;
}
