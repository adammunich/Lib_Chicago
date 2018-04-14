/**
* @file hexFile.cpp
*
* @brief Chicago OCM HexFile helper functions
*
* @copyright
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public
* License as published by the Free Software Foundation; either
* version 3.0 of the License, or (at your option) any later version.
*
* @copyright
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* @author Analogix, Inc
*/

//#############################################################################
// Includes
//-----------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "./hexFile.h"

#include "../Debug/debug.h"
#include "../Chicago/chicago_config.h"


//#############################################################################
// Pre-compiler Definitions
//-----------------------------------------------------------------------------
#define MIN_HEX_LINE_SIZE			11
#define HEX_START_CODE_SIZE			1
#define HEX_BYTE_COUNT_SIZE			2
#define HEX_ADDRESS_SIZE			4
#define HEX_RECORD_TYPE_SIZE		2
#define HEX_ONE_DATA_SIZE			2

#define VERSION_ADDR				0x0100


//#############################################################################
// Variable Declarations
//-----------------------------------------------------------------------------
uint8_t const OCM_FW_DATA[]= {
	#include "ocm_hex.h"
};


//#############################################################################
// Function Definitions
//-----------------------------------------------------------------------------
int8_t GetLineData(uint8_t *pLine, uint8_t *pByteCount, uint32_t *pAddress, uint8_t *pRecordType, uint8_t *pData){
	int8_t  start_code;
	uint8_t checksum;
	uint8_t sum;
	uint8_t i;
	
	hex_get_header(pLine, &start_code, pByteCount, pAddress, pRecordType);

	sum = *pByteCount + (*pAddress >> 8) + (*pAddress & 0xFF) + *pRecordType;
	pLine += (HEX_START_CODE_SIZE + HEX_BYTE_COUNT_SIZE + HEX_ADDRESS_SIZE + HEX_RECORD_TYPE_SIZE); /* Start code: 1 character, Byte count: 2 characters, Address: 4 characters, Record type: 2 characters */

	if (start_code != ':'){
		return RETURN_FAILURE_VALUE;  // bad start code
	}

	// only I8HEX files are supported; refer to https://en.wikipedia.org/wiki/Intel_HEX
	if ( (*pRecordType != HEX_RECORD_TYPE_DATA) && (*pRecordType != HEX_RECORD_TYPE_EOF)) {
		return RETURN_FAILURE_VALUE2;  // unsupported record type
	}

	// all other record types are filtered out
	if ( *pRecordType == HEX_RECORD_TYPE_DATA ){
		for (i=0; i<*pByteCount; i++){
			if (hex_get_one_data(pLine, pData) == 0){
				sum += *pData;
				pData++;
				pLine += 2;
			}
			else{
				return RETURN_FAILURE_VALUE3;  // read data error
			}
		}

		if (hex_get_one_data(pLine, &checksum) == 0){
			if ((int8_t)(sum + checksum) == 0){
				return RETURN_NORMAL_VALUE;
			}
			else{
				return RETURN_FAILURE_VALUE4;  // checksum error
			}
		}
		else{
			return RETURN_FAILURE_VALUE5;   // read checksum error
		}
	}
	else{
		return RETURN_NORMAL_VALUE;
	}
}

//-----------------------------------------------------------------------------
void SetLineData(uint8_t *pLine, uint8_t ByteCount, uint32_t Address, uint8_t RecordType, uint8_t *pData){
	uint8_t checksum;

	sprintf((char*)(pLine), ":%02X%04X%02X", ByteCount, Address, RecordType);
	pLine += (1 + 2 + 4 + 2); /* Start code: 1 character, Byte count: 2 characters, Address: 4 characters, Record type: 2 characters */
	checksum = ByteCount + (Address >> 8) + (Address & 0xFF) + RecordType;

	for(; ByteCount;  ByteCount--){
		sprintf((char*)(pLine), "%02X", (int8_t)(*pData));
		checksum += *pData;
		pData++;
		pLine += 2;
	}

	sprintf((char*)(pLine), "%02BX", (int8_t)-checksum);
	pLine += 2;
	*pLine = '\0';
}

//-----------------------------------------------------------------------------
void read_hex_ver(uint8_t *pData){

	#ifdef DEBUG_LEVEL_2
		TRACE1("read_hex_ver(uint8_t *pData=%x)\n", pData);
	#endif

	pData[0] = OCM_FW_DATA[VERSION_ADDR];		// main version
	pData[1] = OCM_FW_DATA[VERSION_ADDR + 1] ;	// minor version
	pData[2] = OCM_FW_DATA[VERSION_ADDR + 2] ;	// build version
	pData[0] = pData[0]&0x0F;
	pData[1] = pData[1]&0x0F;
	
	#ifdef DEBUG_LEVEL_2
		TRACE3("\tOCM version: %01X.%01X.%02X \n", pData[0], pData[1], pData[2]);
	#endif
}

//-----------------------------------------------------------------------------
uint32_t get_hex_size(void){
	return(sizeof(OCM_FW_DATA));
}

//-----------------------------------------------------------------------------
/// @copydoc hex_get_header
static void hex_get_header(uint8_t *pLine, int8_t *start_code, uint8_t *pByteCount, uint32_t *pAddress, uint8_t *pRecordType){
	uint8_t point;
	uint8_t buf[5];
	
	point = 0;
	
	// get start code
	*start_code = pLine[point];
	point += HEX_START_CODE_SIZE;
	
	// get byte count
	memcpy(&buf[0],&pLine[point],HEX_BYTE_COUNT_SIZE);
	buf[HEX_BYTE_COUNT_SIZE] = 0;
	*pByteCount = (uint8_t)strtol((char*)&buf[0], NULL, 16);
	point += HEX_BYTE_COUNT_SIZE;
	
	// get address
	memcpy(&buf[0],&pLine[point],HEX_ADDRESS_SIZE);
	buf[HEX_ADDRESS_SIZE] = 0;
	*pAddress = (uint32_t)strtol((char*)&buf[0], NULL, 16);
	point += HEX_ADDRESS_SIZE;
	
	// get record type
	memcpy(&buf[0],&pLine[point],HEX_RECORD_TYPE_SIZE);
	buf[HEX_RECORD_TYPE_SIZE] = 0;
	*pRecordType = (uint8_t)strtol((char*)&buf[0], NULL, 16);
}

//-----------------------------------------------------------------------------
/// @copydoc hex_get_one_data
static int8_t hex_get_one_data(uint8_t *pLine, uint8_t *pData){
	uint8_t /* xdata */ buf[3];
	
	if((pLine[0]<48)||(pLine[1]<48)){
		return RETURN_FAILURE_VALUE;
	}
	if((pLine[0]>70)||(pLine[1]>70)){
		return RETURN_FAILURE_VALUE;
	}
	if((pLine[0]>57)&&(pLine[0]<65)){
		return RETURN_FAILURE_VALUE;
	}
	if((pLine[1]>57)&&(pLine[1]<65)){
		return RETURN_FAILURE_VALUE;
	}
	
	buf[0]=pLine[0];
	buf[1]=pLine[1];
	buf[2]=0;
	
	*pData = (uint8_t)strtol((char*)&buf[0], NULL, 16);
	
	return RETURN_NORMAL_VALUE;
}


