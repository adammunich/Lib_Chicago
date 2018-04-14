/**
* @file hexFile.h
*
* @brief Chicago OCM HexFile helper functions _H
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

#ifndef __HEXFILE_H__
	#define __HEXFILE_H__

	//#############################################################################
	// Pre-compiler Definitions
	//-----------------------------------------------------------------------------
	#define HEX_LINE_SIZE 16

	/* record types; only I8HEX files are supported, so only record types 00 and 01 are used */
	#define  HEX_RECORD_TYPE_DATA   0
	#define  HEX_RECORD_TYPE_EOF    1


	//#############################################################################
	// Type Definitions
	//-----------------------------------------------------------------------------


	//#############################################################################
	// Function Prototypes
	//-----------------------------------------------------------------------------
	/**
	 * @brief 
	 *		Get data from hexfile line
	 * @ingroup Chicago_flash
	 * @param pLine - Line of hex file
	 * @param pByteCount - Unknown, ask analogix
	 * @param pAddress - Unknown, ask analogix
	 * @param pRecordType - Unknown, ask analogix
	 * @param pData - Returned line data
	 * @return RETURN_NORMAL_VALUE if success
	 * @return RETURN_FAILURE_VALUE if bad start code
	 * @return RETURN_FAILURE_VALUE2 if unsupported record type
	 * @return RETURN_FAILURE_VALUE3 if read data error
	 * @return RETURN_FAILURE_VALUE4 if checksum compare error
	 * @return RETURN_FAILURE_VALUE5 if checksum calculate error
	 */		
	int8_t GetLineData(uint8_t *pLine, uint8_t *pByteCount, uint32_t *pAddress, uint8_t *pRecordType, uint8_t *pData);

	/**
	 * @brief 
	 *		Set data in HexFile line
	 * @ingroup Chicago_flash
	 * @param pLine - Line of hex file
	 * @param ByteCount - Unknown, ask analogix
	 * @param Address - Unknown, ask analogix
	 * @param RecordType - Unknown, ask analogix
	 * @param pData - Line hex data
	 * @return void
	 */		
	void SetLineData(uint8_t *pLine, uint8_t ByteCount, uint32_t Address, uint8_t RecordType, uint8_t *pData);
	
	/**
	 * @brief 
	 *		Return the version of the current hex file
	 * @ingroup Chicago_flash
	 * @param pData - Pointer to int8_t array of version # in form [0].[1].[2]
	 * @return void
	 */		
	void read_hex_ver(uint8_t *pData);
	
	/**
	 * @brief 
	 *		Returns the SizeOf OCM hex file data
	 * @ingroup Chicago_flash
	 * @return uint32_t - Size of hex file
	 */		
	uint32_t get_hex_size(void);
	
	/**
	 * @brief 
	 *		Gets header from hex data
	 * @ingroup Chicago_flash
	 * @param pLine - Line of hex file
	 * @param start_code - Unknown, ask analogix
	 * @param pByteCount - Unknown, ask analogix
	 * @param pAddress - Unknown, ask analogix
	 * @param pRecordType - Unknown, ask analogix
	 * @return void
	 */		
	static void hex_get_header(uint8_t *pLine, int8_t *start_code, uint8_t *pByteCount, uint32_t *pAddress, uint8_t *pRecordType);
	
	/**
	 * @brief 
	 *		Get one byte of data from HEX file?
	 * @ingroup Chicago_flash
	 * @param pLine - Line of hex file
	 * @param pData - Unknown, ask analogix
	 * @return void
	 */		
	static int8_t hex_get_one_data(uint8_t *pLine, uint8_t *pData);
	
#endif  /* __HEXFILE_H__ */
