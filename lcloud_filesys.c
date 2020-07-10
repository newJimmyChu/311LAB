////////////////////////////////////////////////////////////////////////////////
//
//  File           : lcloud_filesys.c
//  Description    : This is the implementation of the Lion Cloud device 
//                   filesystem interfaces.
//
//   Author        : *** INSERT YOUR NAME ***
//   Last Modified : *** DATE ***
//

// Include files
#include <stdlib.h>
#include <string.h>
#include <cmpsc311_log.h>
#include <cmpsc311_util.h>

// Project include files
#include <lcloud_filesys.h>
#include <lcloud_controller.h>
#include "lcloud_cache.h"
#include <lcloud_client.h>
//
// File system interface implementation
#define REGISTER_MASK_B0 (uint64_t)0xf000000000000000
#define REGISTER_MASK_B1 (uint64_t)0x0f00000000000000
#define REGISTER_MASK_C0 (uint64_t)0x00ff000000000000
#define REGISTER_MASK_C1 (uint64_t)0x0000ff0000000000
#define REGISTER_MASK_C2 (uint64_t)0x000000ff00000000
#define REGISTER_MASK_D0 (uint64_t)0x00000000ffff0000
#define REGISTER_MASK_D1 (uint64_t)0x000000000000ffff

#define LCFHANDLE_MASK_ID 		 (uint32_t)0xff000000
#define LCFHANDLE_MASK_HANDLE 	 (uint32_t)0x00ffffff

#define SHIFT_BITS_B0 60
#define SHIFT_BITS_B1 56
#define SHIFT_BITS_C0 48
#define SHIFT_BITS_C1 40
#define SHIFT_BITS_C2 32
#define SHIFT_BITS_D0 16
#define SHIFT_BITS_D1 0
////////////////////////////////////////////////////////////////////////////////

typedef struct LcFileInfo{

	uint32_t filename;              // Integer file name 
	uint32_t handle;                // Check if the file is opening
	uint32_t sector_number;         // Sector location of the file
    uint32_t block_number;          // Block number of the file
    uint32_t device;
    uint32_t length;		        // File length
    uint32_t offset;    	        // location for read and write
    uint32_t currentLength;
    uint32_t start_sector;
    uint32_t start_block;
    char path[64];

} LcFileInfo;

typedef struct LcDeviceInfo{

    uint32_t deviceSectorsSize;
    uint32_t deviceBlocksSize;
    uint32_t deviceFilesSize;
    uint32_t currentCount;
    uint32_t currentSector;
    uint32_t currentBlock;
    uint32_t isFull;
    LcFileInfo **fileInfoArray;

} LcDeviceInfo;

LcDeviceInfo *deviceInfo[16];

uint32_t fileHandleCount = 1;

uint32_t hit = 0;

uint32_t miss = 0;

uint32_t power_on = 0;

uint32_t init = 0;

LCloudRegisterFrame LCRequestFrame(LCloudRegisterFrame requestFrame, 
	uint32_t operation, void *xfer);

LCloudRegisterFrame LCRequestFramePackaging(uint32_t c1, 
	uint32_t c2, uint32_t d0, uint32_t d1);

LcFileInfo* GetFileInfoFromBuffer(char *buffer);

int LCFileInfoToChar(LcFileInfo *fileInfo, char *buffer);

LcDeviceInfo *GetNewLcDeviceInfo(uint32_t smallestDeviceID);

int SetDevicePositionToNext(uint32_t deviceId);

uint32_t GetNextDeviceId(uint32_t deviceId);

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcopen
// Description  : Open the file for for reading and writing
//
// Inputs       : path - the path/filename of the file to be read
// Outputs      : file handle if successful test, -1 if failure

LcFHandle lcopen( const char *path ) {

	LcFHandle lcFhandle = 0;
	char test_block[LC_DEVICE_BLOCK_SIZE];
	char filepath[64];
	uint32_t isFound = 0;
	memcpy(&filepath[0], path, 64);
	memset (test_block, 0, sizeof (test_block));
	LCloudRegisterFrame requestFrame = 0x0;
	LCloudRegisterFrame respondFrame = 0x0;

	if (!power_on){
		respondFrame = LCRequestFrame(requestFrame, LC_POWER_ON, NULL);
		if (respondFrame < 0) {
			return(-1);
		}
		power_on = 1;
	}

	// Initialize the lcloud cache system
	lcloud_initcache(256);
	// Step 0: Search the DeviceInfoArray to check if the *path is in any of the devices
    for (int i = 0; i < 16; i++) {
        if (deviceInfo[i]) {
            for (int j = 0; j < deviceInfo[i]->currentCount; j++) {
                if (!strcmp(filepath, deviceInfo[i]->fileInfoArray[j]->path)) {
                    if (deviceInfo[i]->fileInfoArray[j]->handle != 0) {
                        return(-1);
                    } else {
                        deviceInfo[i]->fileInfoArray[j]->handle = fileHandleCount++;
                        isFound = 1;
                        lcFhandle = deviceInfo[i]->fileInfoArray[j]->handle;
                        break;
                    }
                }
            }
        }
    }
    

	// Step 1: Probe for the usable device
	// If the file is not in the devices, we need to probe for the usable device
	if (!isFound) {
	    respondFrame = LCRequestFrame(requestFrame, LC_DEVPROBE, NULL);
	    if (respondFrame < 0) {
	    	return(-1);
	    }
	    uint32_t deviceIDs = (respondFrame & REGISTER_MASK_D0) >> SHIFT_BITS_D0;
        //printf("deviceId:  %d\n", deviceIDs);

        // If the devices have not been initialized, initialize them
        if (init == 0) {
            for (int i = 0; i < 16; i++) {
                if ((deviceIDs >> i) & 1){
                    if (deviceInfo[i] == NULL) {
                        // Init device info
                        deviceInfo[i] = GetNewLcDeviceInfo(i);
                    } 
                }    
            }
            init = 1;
        }

	    for (int i = 0; i < 16; i++) {
	    	if ((deviceIDs >> i) & 1) {
	    	    // Check whether device is not init or the device is not full
	    	    if (deviceInfo[i] == NULL) {
	    	        // Init device info
                    deviceInfo[i] = GetNewLcDeviceInfo(i);
	    	    } 
	    	    // Add new file Info to the File Info array
	    	    if (deviceInfo[i]->isFull == 0){
                    deviceInfo[i]->fileInfoArray[deviceInfo[i]->currentCount] = malloc(sizeof(LcFileInfo ));
                    deviceInfo[i]->fileInfoArray[deviceInfo[i]->currentCount]->handle = fileHandleCount++;
                    deviceInfo[i]->fileInfoArray[deviceInfo[i]->currentCount]->sector_number = deviceInfo[i]->currentSector;
                    deviceInfo[i]->fileInfoArray[deviceInfo[i]->currentCount]->block_number = deviceInfo[i]->currentBlock;
                    deviceInfo[i]->fileInfoArray[deviceInfo[i]->currentCount]->start_sector = deviceInfo[i]->currentSector;
                    deviceInfo[i]->fileInfoArray[deviceInfo[i]->currentCount]->start_block = deviceInfo[i]->currentBlock;
                    deviceInfo[i]->fileInfoArray[deviceInfo[i]->currentCount]->device = i;
                    strcpy(deviceInfo[i]->fileInfoArray[deviceInfo[i]->currentCount]->path, filepath);
                    SetDevicePositionToNext(i);
                    // Assign return handle
                    lcFhandle = ((i << 24) & LCFHANDLE_MASK_ID) |
                                (deviceInfo[i]->fileInfoArray[deviceInfo[i]->currentCount]->handle & LCFHANDLE_MASK_HANDLE);
                    deviceInfo[i]->currentCount++;

                    break;
	    	    }
	    	}
	    }
	}

	return( lcFhandle ); 
} 

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcread
// Description  : Read data from the file 
//
// Inputs       : fh - file handle for the file to read from
//                buf - place to put the data
//                len - the length of the read
// Outputs      : number of bytes read, -1 if failure
int lcread( LcFHandle fh, char *buf, size_t len ) {
    //printf("\n---------------Inside file read\n");
	// 1.1 Get the device ID and handle
    uint32_t deviceId = fh >> 24;
    uint32_t fileHandle = fh & LCFHANDLE_MASK_HANDLE;
	char respondFileInfo[LC_DEVICE_BLOCK_SIZE];
	uint32_t remFileLength = 0;
	uint32_t remReadLength = (uint32_t) len;
	uint32_t bufferPosition = 0;
    LcFileInfo *fileInfo = NULL;
	uint32_t isFound = 0;
	uint32_t device, sector, block = 0;
	memset(respondFileInfo, 0, LC_DEVICE_BLOCK_SIZE);
	// Request file info from the device and check if the file is openning
	LCloudRegisterFrame requestFrame = 0x0;
	LCloudRegisterFrame respondFrame = 0x0;

	//memset(buf, '\0', strlen(buf));

	// If the file is currently in any of the devices, get it

	for (int i = 0; i < deviceInfo[deviceId]->currentCount; i++) {
	    if (deviceInfo[deviceId]->fileInfoArray[i]->handle == fileHandle && fileHandle != 0) {
            fileInfo = deviceInfo[deviceId]->fileInfoArray[i];
            isFound = 1;
            break;
	    }
	}
    deviceId = fileInfo->device;


	if (!fileInfo || !fileInfo->handle || !isFound) {
		return (-1);
	}

	// Calculate and update the remaining bytes of the current file
	remFileLength = fileInfo->length - fileInfo->currentLength;
	memset(respondFileInfo, 0, LC_DEVICE_BLOCK_SIZE);
	uint32_t writeBytes = 0;

	if (fileInfo->offset > 0){
        //printf("--------------Inside read if\n");
		if (remReadLength >= LC_DEVICE_BLOCK_SIZE - fileInfo->offset - 12) {
			writeBytes = LC_DEVICE_BLOCK_SIZE - fileInfo->offset - 12;
		}
		else {
			writeBytes = remReadLength;
		}

		char *value = lcloud_getcache(fileInfo->device, fileInfo->sector_number, fileInfo->block_number);

		// If the data is found in the cache system, return data, otherwise add it to the cache
		if (value != NULL){
		    // Update fileInfo (old_sector, old_block) -> (new_sector, new_block)
            memcpy(&respondFileInfo[0], &value[0], LC_DEVICE_BLOCK_SIZE);
            //printf("%s\n", &respondFileInfo[12]);

            hit ++;
		} else {
		    miss ++;
            // 2.1 Read the first block from the device
            requestFrame = LCRequestFramePackaging(fileInfo->device, LC_XFER_READ,
                                                   fileInfo->sector_number, fileInfo->block_number);
            respondFrame = LCRequestFrame(requestFrame, LC_BLOCK_XFER, respondFileInfo);
            if (respondFrame < 0) {
                return (-1);
            }

            lcloud_putcache(fileInfo->device, fileInfo->sector_number, fileInfo->block_number, &respondFileInfo[0]);
        }

		// Get the next sector & block the file points to from the block header
        
        memcpy(&device, &respondFileInfo[0], 4);
		memcpy(&sector, &respondFileInfo[4], 4);
		memcpy(&block, &respondFileInfo[8], 4);
		memcpy(&buf[0], &respondFileInfo[fileInfo->offset + 12], writeBytes);
		if (sector != -1 && block != -1 && remReadLength >= LC_DEVICE_BLOCK_SIZE - fileInfo->offset - 12) {
            fileInfo->device = device;
		    fileInfo->block_number = block;
		    fileInfo->sector_number = sector;
		}

		// Update the file info
		bufferPosition += writeBytes;
		remFileLength -= writeBytes;
		fileInfo->currentLength += writeBytes;

		// If (!endOfFile)
		if (writeBytes == LC_DEVICE_BLOCK_SIZE - fileInfo->offset - 12){
		    fileInfo->offset = 0;
		    remReadLength -= writeBytes;
		} else {
		    fileInfo->offset += writeBytes;
		    remReadLength -= writeBytes;
		}

	}

	// 2.2 Read the remaining file blocks
	while (remReadLength > 0 && remFileLength > 0) {
		//printf("\n----------------------while\n");
        // If the remaining bytes is larger than 256 and the there still
        // have more than 256 bytes to read, transfer a whole block
        memset(respondFileInfo, 0, LC_DEVICE_BLOCK_SIZE);
        char *value = lcloud_getcache(fileInfo->device, fileInfo->sector_number, fileInfo->block_number);

        // If the read block is in the cache, return the cache line.
        if (value != NULL) {
            hit ++;
            memcpy(&respondFileInfo[0], &value[0], LC_DEVICE_BLOCK_SIZE);

        } else {
            miss ++;
            // Send request to the storage system, add cache line to the cache system
            requestFrame = LCRequestFramePackaging(fileInfo->device, LC_XFER_READ,
                                                   fileInfo->sector_number, fileInfo->block_number);
            respondFrame = LCRequestFrame(requestFrame, LC_BLOCK_XFER, &respondFileInfo[0]);
            if (respondFrame < 0) {
                return (-1);
            }

            lcloud_putcache(fileInfo->device, fileInfo->sector_number, fileInfo->block_number, &respondFileInfo[0]);
        }

        if (remFileLength >= LC_DEVICE_BLOCK_SIZE - 12 && remReadLength >= LC_DEVICE_BLOCK_SIZE - 12) {

            memcpy(&device, &respondFileInfo[0], 4);
            memcpy(&sector, &respondFileInfo[4], 4);
            memcpy(&block, &respondFileInfo[8], 4);
            
            if (sector != -1 && block != -1) {
                fileInfo->device = device;
                fileInfo->block_number = block;
                fileInfo->sector_number = sector;
            }


            memcpy(&buf[bufferPosition], &respondFileInfo[12], LC_DEVICE_BLOCK_SIZE - 12);

            bufferPosition += LC_DEVICE_BLOCK_SIZE - 12;
            remFileLength -= LC_DEVICE_BLOCK_SIZE - 12;
            remReadLength -= LC_DEVICE_BLOCK_SIZE - 12;
            fileInfo->currentLength += LC_DEVICE_BLOCK_SIZE - 12;

            // Reset the offset to the beginning of the next block
            fileInfo->offset = 0;

        } else if (remFileLength >= remReadLength) {

            memcpy(&buf[bufferPosition], &respondFileInfo[12], remReadLength);
            bufferPosition += remReadLength;
            fileInfo->offset += remReadLength;
            fileInfo->currentLength += remReadLength;

            break;
        } else {
            return (-1);
        }
    }	

    buf[bufferPosition] = '\0';

	return( bufferPosition );
}
//
// Function     : lcwrite
// Description  : write data to the file
//
// Inputs       : fh - file handle for the file to write to
//                buf - pointer to data to write
//                len - the length of the write
// Outputs      : number of bytes written if successful test, -1 if failure

int lcwrite( LcFHandle fh, char *buf, size_t len ) {

    //printf("\n-------Begin write\n");
    uint32_t deviceId = fh >> 24;
    uint32_t fileHandle = fh & LCFHANDLE_MASK_HANDLE;
	char respondFileInfo[LC_DEVICE_BLOCK_SIZE];
	uint32_t remWriteLength = (uint32_t) len;
	int32_t remFileLength = 0;
	uint32_t bufferPosition = 0;
	uint32_t device, sector, block = 0;
    uint32_t endOfFile = 0;
    uint32_t overwrite = 0;
    uint32_t old_device = 0;
	memset(respondFileInfo, 0, LC_DEVICE_BLOCK_SIZE);

	// Request file info from the device and check if the file is openning
	LCloudRegisterFrame requestFrame = 0x0;
	LCloudRegisterFrame respondFrame = 0x0;

	LcFileInfo *fileInfo = NULL;

	uint32_t isFound = 0;
    // If the file is currently in any of the devices, get it
    for (int i = 0; i < deviceInfo[deviceId]->currentCount; i++) {
        if (deviceInfo[deviceId]->fileInfoArray[i]->handle == fileHandle && fileHandle != 0) {
            fileInfo = deviceInfo[deviceId]->fileInfoArray[i];
            isFound = 1;
            break;
        }
    }
    //printf("deviceId: %d\n", deviceId);

    if (!fileInfo || !fileInfo->handle || !isFound) {
        return (-1);
    }

	memset(respondFileInfo, 0, LC_DEVICE_BLOCK_SIZE);
	remFileLength = fileInfo->length - fileInfo->currentLength;

    // 2.1 Write the first block to the device
    // Case 1: offset > 0

    
	uint32_t writeBytes = 0;
	if (fileInfo->offset > 0){
        //printf("\n----------Inside if\n");
		if (remWriteLength >= LC_DEVICE_BLOCK_SIZE - fileInfo->offset - 12){
			writeBytes = LC_DEVICE_BLOCK_SIZE - fileInfo->offset - 12;
        } else {
            endOfFile = 1;
			writeBytes = remWriteLength;
		}

        old_device = fileInfo->device;
		char *value = lcloud_getcache(fileInfo->device, fileInfo->sector_number, fileInfo->block_number);
		if (value != NULL) {
            hit ++;
            memcpy(&respondFileInfo[0], &value[0], LC_DEVICE_BLOCK_SIZE);
        } else {
            miss ++;
            // Send request to the storage system, add cache line to the cache system
            requestFrame = LCRequestFramePackaging(fileInfo->device, LC_XFER_READ,
                                                   fileInfo->sector_number, fileInfo->block_number);
            respondFrame = LCRequestFrame(requestFrame, LC_BLOCK_XFER, &respondFileInfo[0]);
            if (respondFrame < 0) {
                return (-1);
            }

            lcloud_putcache(fileInfo->device, fileInfo->sector_number, fileInfo->block_number, &respondFileInfo[0]);
        }
        // Read from the next block to see if the header is not -1
        //requestFrame = LCRequestFramePackaging(deviceId, LC_XFER_READ,
                                               //fileInfo->sector_number, fileInfo->block_number);
        //respondFrame = LCRequestFrame(requestFrame, LC_BLOCK_XFER, respondFileInfo);
        if (respondFrame < 0) {
            return(-1);
        }
        memcpy(&device, &respondFileInfo[0], 4);
        memcpy(&sector, &respondFileInfo[4], 4);
        memcpy(&block, &respondFileInfo[8], 4);
        //printf("------------device: %d, sector: %d, block: %d\n", device, sector, block);

        if ((int)sector > 0 || (int)block > 0) {
        	overwrite = 1;
        }

        //printf("endOfFile: %d, overwrite: %d\n", endOfFile, overwrite);

        // Set the block header -> (sector, block)
        if (endOfFile && !overwrite) {
            device = -1;
        	sector = -1;
            block = -1;
        }
        else {
        	if (overwrite == 0 && ((sector == 0 && block == 0) || (sector <= -1 && block <= -1) ||
                     sector > deviceInfo[fileInfo->device]->deviceSectorsSize ||
                     block > deviceInfo[fileInfo->device]->deviceBlocksSize)) {

                // If the current device is full, put the rest of file data to next avaible device
                if ((deviceInfo[fileInfo->device]->currentSector == deviceInfo[fileInfo->device]->deviceSectorsSize - 1 &&
                        deviceInfo[fileInfo->device]->currentBlock >= deviceInfo[fileInfo->device]->deviceBlocksSize - 1) || 
                        deviceInfo[fileInfo->device]->isFull) {

                    deviceInfo[fileInfo->device]->isFull = 1;
                    device = GetNextDeviceId(fileInfo->device);
                    fileInfo->device = device;
                }
                device = fileInfo->device;
        		sector = deviceInfo[fileInfo->device]->currentSector;
            	block = deviceInfo[fileInfo->device]->currentBlock;
            	SetDevicePositionToNext(fileInfo->device);
        	}
        }
        //printf("deviceInfo[fileInfo->device]->deviceBlocksSize: %d", deviceInfo[fileInfo->device]->deviceBlocksSize);
        // Write to the current block that the file info points to
        
        memcpy(&respondFileInfo[0], (char *)&device, 4);
        memcpy(&respondFileInfo[4], (char *)&sector, 4);
        memcpy(&respondFileInfo[8], (char *)&block, 4);
        memcpy(&respondFileInfo[fileInfo->offset + 12], &buf[0], writeBytes);

		
        requestFrame = LCRequestFramePackaging(old_device, LC_XFER_WRITE, 
			fileInfo->sector_number, fileInfo->block_number);
		respondFrame = LCRequestFrame(requestFrame, LC_BLOCK_XFER, &respondFileInfo[0]);
        //if (fileInfo->offset + writeBytes != 244) {
            lcloud_putcache(old_device, fileInfo->sector_number, fileInfo->block_number, &respondFileInfo[0]);
        //printf("%s\n", &respondFileInfo[12]);
        //} else {
        //    printf("Reach me!!\n");
        //    lcloud_putcache(device, sector, block, &respondFileInfo[0]);
        //}
		
		
		// Update the file info
		bufferPosition += writeBytes;
		remWriteLength -= writeBytes;

		// If the block is full, point to the next block or sector

		if (!endOfFile) {
            fileInfo->device = device;
		    fileInfo->sector_number = sector;
		    fileInfo->block_number = block;
            fileInfo->offset = 0;
		} else {
            fileInfo->offset += writeBytes;
		}

		// Check if the write operation is complete
		if (writeBytes > remFileLength) {
			fileInfo->length += writeBytes - remFileLength;
			fileInfo->currentLength += writeBytes - remFileLength;
		}
		else {
		    fileInfo->currentLength += writeBytes;
			remFileLength -= writeBytes;
		}
		overwrite = 0;
        old_device = 0;
	}

	// Begin writing whole blocks
	// 2.2 Read the remaining file blocks
    endOfFile = 0;
	while (remWriteLength > 0) {
        //printf("\n----------------Inside while\n");
        //printf("------------- did: %d, sec: %d  blk: %d\n", fileInfo->device, fileInfo->sector_number, fileInfo->block_number);
		// If the remaining bytes is larger than 256 and the there still 
		// have more than 256 bytes to read, transfer a whole block
		if (remWriteLength > LC_DEVICE_BLOCK_SIZE - 12){
			writeBytes = LC_DEVICE_BLOCK_SIZE - 12;
		}
		else {
            writeBytes = remWriteLength;
            endOfFile = 1;
		}

        old_device = fileInfo->device;
		// Read from the next block to see if the header is not -1
        char *value = lcloud_getcache(fileInfo->device, fileInfo->sector_number, fileInfo->block_number);
		if (value != NULL) {
            hit ++;
            memcpy(&respondFileInfo[0], &value[0], LC_DEVICE_BLOCK_SIZE);
        } else {
            miss ++;
            // Send request to the storage system, add cache line to the cache system
            requestFrame = LCRequestFramePackaging(fileInfo->device, LC_XFER_READ,
                                                   fileInfo->sector_number, fileInfo->block_number);
            respondFrame = LCRequestFrame(requestFrame, LC_BLOCK_XFER, &respondFileInfo[0]);
            if (respondFrame < 0) {
                return (-1);
            }

            lcloud_putcache(fileInfo->device, fileInfo->sector_number, fileInfo->block_number, &respondFileInfo[0]);
        }

        memcpy(&device, &respondFileInfo[0], 4);
        memcpy(&sector, &respondFileInfo[4], 4);
        memcpy(&block, &respondFileInfo[8], 4);
        //printf("------------device: %d, sector: %d, block: %d\n", device, sector, block);
        //printf("endOfFile: %d, overwrite: %d\n", endOfFile, overwrite);

        if ((int)sector > 0 || (int)block > 0) {
            overwrite = 1;
        }

        // Set the block header -> (sector, block)
        // If write mode is turned to override, the file is not reach the end
        //printf("fileInfo->device: %d\n", fileInfo->device);
        //printf("fileInfo->sector: %d\n", fileInfo->sector_number);
        //printf("fileInfo->block: %d\n", fileInfo->block_number);
        //printf("device: %d, sector: %d, block: %d\n", device, sector, block);

        if (endOfFile && !overwrite) {
            device = -1;
        	sector = -1;
            block = -1;
        }
        else {
        	if (overwrite == 0 && ((sector == 0 && block == 0) || (sector <= -1 && block <= -1) ||
                     sector > deviceInfo[fileInfo->device]->deviceSectorsSize ||
                     block > deviceInfo[fileInfo->device]->deviceBlocksSize)) {
        		
                // If the current device is full, put the rest of file data to next avaible device
                if ((deviceInfo[fileInfo->device]->currentSector == deviceInfo[fileInfo->device]->deviceSectorsSize - 1 &&
                        deviceInfo[fileInfo->device]->currentBlock >= deviceInfo[fileInfo->device]->deviceBlocksSize - 1) ||
                        deviceInfo[fileInfo->device]->isFull) {
                    
                    deviceInfo[fileInfo->device]->isFull = 1;
                    device = GetNextDeviceId(fileInfo->device);
                    fileInfo->device = device;
                }
                device = fileInfo->device;
                sector = deviceInfo[fileInfo->device]->currentSector;
                block = deviceInfo[fileInfo->device]->currentBlock;
                SetDevicePositionToNext(fileInfo->device);
            }
            	
        }

		// Write to the current block that the file info points to
		
        memcpy(&respondFileInfo[0], (char *)&device, 4);
        memcpy(&respondFileInfo[4], (char *)&sector, 4);
        memcpy(&respondFileInfo[8], (char *)&block, 4);
        memcpy(&respondFileInfo[12], &buf[bufferPosition], writeBytes);
        //printf("New device: %d, sector: %d, block: %d\n", device, sector, block);
        //printf("---------------offset: %d\n", fileInfo->offset);

		requestFrame = LCRequestFramePackaging(old_device, LC_XFER_WRITE, 
			fileInfo->sector_number, fileInfo->block_number);
		respondFrame = LCRequestFrame(requestFrame, LC_BLOCK_XFER, respondFileInfo);
		//if (fileInfo->offset + writeBytes != 244) {
            lcloud_putcache(old_device, fileInfo->sector_number, fileInfo->block_number, &respondFileInfo[0]);
            //printf("%s\n", &respondFileInfo[12]);
        //} else {
        //    printf("Reach me!!\n");
        //    lcloud_putcache(device, sector, block, &respondFileInfo[0]);
        //}
        //printf("%s\n", &respondFileInfo[12]);

        

		if (respondFrame < 0) {
			return(-1);
		}

		bufferPosition += writeBytes;
		if (writeBytes > remFileLength) {
			fileInfo->length += writeBytes - remFileLength;
			fileInfo->currentLength = fileInfo->length;
			remFileLength = 0;
		}
		else {
		    fileInfo->currentLength += writeBytes;
			remFileLength -= writeBytes;
		}
		//remFileLength -= writeBytes;
		remWriteLength -= writeBytes;
		
		// Update the file position (sector_num, block_num)
		if (!endOfFile){
            /*if (fileInfo->offset == 244) {
                requestFrame = LCRequestFramePackaging(old_device, LC_XFER_WRITE, 
                    sector, block);
                respondFrame = LCRequestFrame(requestFrame, LC_BLOCK_XFER, respondFileInfo);
                lcloud_putcache(old_device, fileInfo->sector_number, fileInfo->block_number, &respondFileInfo[0]);
            }*/
            fileInfo->device = device;
		    fileInfo->sector_number = sector;
		    fileInfo->block_number = block;
			fileInfo->offset = 0;
		}
		else {
			fileInfo->offset += writeBytes;
		}
		// If the remReadLength is greater than remFileLength, function failed

        endOfFile = 0;
        overwrite = 0;
        old_device = 0;

	}

	if (remFileLength < 0) {
		fileInfo->length -= remFileLength;
		fileInfo->currentLength = fileInfo->length;
	}

	return( bufferPosition );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcseek
// Description  : Seek to a specific place in the file
//
// Inputs       : fh - the file handle of the file to seek in
//                off - offset within the file to seek to
// Outputs      : 0 if successful test, -1 if failure

int lcseek( LcFHandle fh, size_t off ) {
	
    uint32_t deviceId = fh >> 24;
    uint32_t fileHandle = fh & LCFHANDLE_MASK_HANDLE;
	uint32_t sector, block, device = 0;
	char respondFileInfo[LC_DEVICE_BLOCK_SIZE];
	int remLength = (int) off;
	memset(respondFileInfo, 0, LC_DEVICE_BLOCK_SIZE);
    LcFileInfo *fileInfo = NULL;
    uint32_t isFound = 0;

    // If the file is currently in any of the devices, get it
    for (int i = 0; i < deviceInfo[deviceId]->currentCount; i++) {
        if (deviceInfo[deviceId]->fileInfoArray[i]->handle == fileHandle && fileHandle != 0) {
            fileInfo = deviceInfo[deviceId]->fileInfoArray[i];
            isFound = 1;
            break;
        }
    }

    if (!fileInfo || !fileInfo->handle || !isFound) {
        return (-1);
    }
	
    if (fileInfo->length < remLength) {
        return (-1);
    }

    // Request file info from the device and check if the file is openning
	LCloudRegisterFrame requestFrame = 0;
	LCloudRegisterFrame respondFrame = 0;

    device = deviceId;
	sector = fileInfo->start_sector;
	block = fileInfo->start_block;

	// Reset the file offset
	if (off <= LC_DEVICE_BLOCK_SIZE - 12) {
		fileInfo->sector_number = fileInfo->start_sector;
		fileInfo->block_number = fileInfo->start_block;
        fileInfo->device = deviceId;
		fileInfo->offset = off;
		fileInfo->currentLength = off;
    	return(off);
    }


    fileInfo->currentLength = 0;
    
    uint32_t old_device = 0;
	uint32_t old_sector = 0;
	uint32_t old_block = 0;

	while (remLength > 0) {

	    char *value = lcloud_getcache(device, sector, block);
		if (value != NULL) {
            hit ++;
            memcpy(&respondFileInfo[0], &value[0], LC_DEVICE_BLOCK_SIZE);
        } else {
            miss ++;
            // Send request to the storage system, add cache line to the cache system
            requestFrame = LCRequestFramePackaging(device, LC_XFER_READ,
                                                   sector, block);
            respondFrame = LCRequestFrame(requestFrame, LC_BLOCK_XFER, &respondFileInfo[0]);
            if (respondFrame < 0) {
                return (-1);
            }

            lcloud_putcache(device, sector, block, &respondFileInfo[0]);
        }
        old_device = device;
        old_sector = sector;
        old_block = block;


		if (remLength < LC_DEVICE_BLOCK_SIZE - 12) {
            fileInfo->device = device;
            fileInfo->sector_number = sector;
            fileInfo->block_number = block;
            fileInfo->offset = remLength;
            fileInfo->currentLength += remLength;
            return (off);
        }

		remLength -= LC_DEVICE_BLOCK_SIZE - 12;
		fileInfo->currentLength += LC_DEVICE_BLOCK_SIZE - 12;
        //Update the new (sector, block) pointer
        memcpy(&device, &respondFileInfo[0], 4);
        memcpy(&sector, &respondFileInfo[4], 4);
        memcpy(&block, &respondFileInfo[8], 4);
        if ((sector == -1 && block == -1)) {
            device = old_device;
            sector = old_sector;
            block = old_block;
        }
        
	}

	if (remLength == 0) {
        fileInfo->device = device;
		fileInfo->sector_number = sector;
        fileInfo->block_number = block;
        fileInfo->offset = 0;
        fileInfo->currentLength += remLength;
	}
	return (off);

}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcclose
// Description  : Close the file
//
// Inputs       : fh - the file handle of the file to close
// Outputs      : 0 if successful test, -1 if failure

int lcclose( LcFHandle fh ) {

	uint32_t deviceId = fh >> 24;
	uint32_t fileHandle = fh & LCFHANDLE_MASK_HANDLE;
    LcFileInfo *fileInfo = NULL;
    uint32_t isFound = 0;

	// Request file info from the device and check if the file is openning
    //printf("Before close\n");
    for (int i = 0; i < deviceInfo[deviceId]->currentCount; i++) {
        if (deviceInfo[deviceId]->fileInfoArray[i]->handle == fileHandle && fileHandle != 0) {
            fileInfo = deviceInfo[deviceId]->fileInfoArray[i];
            fileInfo->handle = 0;
            isFound = 1;
            break;
        }
    }

    if (!isFound) {
        return (-1);
    }

	return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcshutdown
// Description  : Shut down the filesystem
//
// Inputs       : none
// Outputs      : 0 if successful test, -1 if failure

int lcshutdown( void ) {

	LCloudRegisterFrame requestFrame = 0x0;
	LCloudRegisterFrame respondFrame = LCRequestFrame(requestFrame, LC_DEVPROBE, NULL);
	if (respondFrame < 0) {
		return(-1);
	}

	// Closing all the files
	uint32_t deviceIDs = respondFrame & REGISTER_MASK_D0 >> SHIFT_BITS_D0;
	for (int i = 0; i < 16; i++) {
		if ((deviceIDs >> i) & 1) {
            for (int j = 0; j < deviceInfo[i]->currentCount; j++) {
                deviceInfo[i]->fileInfoArray[j]->handle = 0;
                free(deviceInfo[i]->fileInfoArray[j]);
            }
            free(deviceInfo[i]->fileInfoArray);
            free(deviceInfo[i]);
		}

	}

	requestFrame = 0x0;
    respondFrame = LCRequestFrame(requestFrame, LC_POWER_OFF, NULL);
	if (respondFrame < 0) {
		return (-1);
	}

	printf("The number of cache hit: %d\n", hit);
	printf("The number of cache miss: %d\n", miss);
	printf("The hit / miss rate is: %f\n", (double)(hit / (double)(miss + hit)));
    lcloud_closecache();
	return( 0 );
}

// Copy the fileInfo to the given buffer
int LCFileInfoToChar(LcFileInfo *fileInfo, char *buffer) {

	if (fileInfo != NULL) {
		memcpy(&buffer[0], &fileInfo->filename, sizeof(uint32_t));
		memcpy(&buffer[4], &fileInfo->handle, sizeof(uint32_t));
		memcpy(&buffer[8], &fileInfo->sector_number, sizeof(uint32_t));
		memcpy(&buffer[12], &fileInfo->block_number, sizeof(uint32_t));
		memcpy(&buffer[16], &fileInfo->length, sizeof(uint32_t));
		memcpy(&buffer[20], &fileInfo->offset, sizeof(uint32_t));
		return (0);
	}
	else {
		return (-1);
	}
}


LCloudRegisterFrame LCRequestFrame(LCloudRegisterFrame requestFrame, 
	uint32_t operation, void *xfer) {

	requestFrame = requestFrame | ((uint64_t)operation << SHIFT_BITS_C0);
	LCloudRegisterFrame respondFrame = client_lcloud_bus_request(requestFrame, xfer);
    //printf("---------Succ on io cloud bus\n");
	// Respond failed
	if ((respondFrame & REGISTER_MASK_B1) >> SHIFT_BITS_B1 != LC_SUCCESS) {
		return (-1);
	}		
	return (respondFrame);
}

LCloudRegisterFrame LCRequestFramePackaging(uint32_t c1, 
	uint32_t c2, uint32_t d0, uint32_t d1) {

	LCloudRegisterFrame requestFrame = 0x0;
	requestFrame = requestFrame | ((uint64_t)c1 << SHIFT_BITS_C1)|
	((uint64_t)c2 << SHIFT_BITS_C2) | ((uint64_t)d0 << SHIFT_BITS_D0) | 
	((uint64_t)d1 << SHIFT_BITS_D1);
	return (requestFrame);
}

// Get file info directly from buffer
LcFileInfo* GetFileInfoFromBuffer(char *buffer) {

	LcFileInfo *fileInfo = malloc(sizeof(LcFileInfo));
	memcpy(&(fileInfo->filename), &buffer[0], sizeof(uint32_t));
	memcpy(&(fileInfo->handle), &buffer[4], sizeof(uint32_t));
	memcpy(&(fileInfo->sector_number), &buffer[8], sizeof(uint32_t));
	memcpy(&(fileInfo->block_number), &buffer[12], sizeof(uint32_t));
	memcpy(&(fileInfo->length), &buffer[16], sizeof(uint32_t));
	memcpy(&(fileInfo->offset), &buffer[20], sizeof(uint32_t));

	return (fileInfo);
}

// Generate a new Lc Device Info pointer by using LC_DEVINIT method
LcDeviceInfo *GetNewLcDeviceInfo(uint32_t smallestDeviceID) {

    LcDeviceInfo *info;
    info = malloc(sizeof(LcDeviceInfo ));
    LCloudRegisterFrame requestFrame = LCRequestFramePackaging(smallestDeviceID, 0, 0, 0);
    LCloudRegisterFrame respondFrame = LCRequestFrame(requestFrame, LC_DEVINIT, NULL);
    if (respondFrame < 0) {
        return(NULL);
    }
    info->deviceSectorsSize = (respondFrame & REGISTER_MASK_D0) >> SHIFT_BITS_D0;
    info->deviceBlocksSize = (respondFrame & REGISTER_MASK_D1) >> SHIFT_BITS_D1;
    info->deviceFilesSize = (info->deviceSectorsSize * info->deviceBlocksSize) / 50;
    info->currentCount = 0;
    info->currentBlock = 1;
    info->currentSector = 0;
    info->isFull = 0;
    info->fileInfoArray = (LcFileInfo **)malloc(300 * sizeof(LcFileInfo *));
    return(info);
}

// Set the current device tail pointer to next
int SetDevicePositionToNext(uint32_t deviceId) {
    if (deviceInfo[deviceId]->currentBlock == deviceInfo[deviceId]->deviceBlocksSize - 1
        && deviceInfo[deviceId]->currentSector < deviceInfo[deviceId]->deviceSectorsSize - 1) {
        deviceInfo[deviceId]->currentBlock = 0;
        deviceInfo[deviceId]->currentSector++;
    } else if (deviceInfo[deviceId]->currentSector <= deviceInfo[deviceId]->deviceSectorsSize - 1 &&
                deviceInfo[deviceId]->currentBlock < deviceInfo[deviceId]->deviceBlocksSize - 1) {
        deviceInfo[deviceId]->currentBlock++;
    } else {
        // The device is full, set the pointer to the next sector.
        //printf("Is FUll\n");
        deviceInfo[deviceId]->isFull = 1;
        return (-1);
    }
    return (1);
}

uint32_t GetNextDeviceId(uint32_t deviceId) {
    for (int i = 0; i < 16; i ++) {
        if (deviceInfo[i] != NULL) {
            // If the device is not full
            if (deviceInfo[i]->isFull == 0 && deviceId != i) {
                return (i);
            }
        }
    }
    return(20);
}