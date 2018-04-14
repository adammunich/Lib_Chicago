/**
* @file flash.h
*
* @brief Chicago OCM flasher _H
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
* @author Adam Munich
*/

/**
* @defgroup Chicago_flash [Functions] Chicago OCM flasher
* @details
*	These functions control flashing of the OCM and HEX file operations. 
*	In most circumstances, the only one of interest to the end user is 
*	burn_hex_auto(). However, more detailed information can be found in 
*	ANX753X_Programming_Guide.pdf, page 35.
*/


#ifndef __FLASH_H__
	#define __FLASH_H__

	//#############################################################################
	// Pre-compiler Definitions
	//-----------------------------------------------------------------------------
	#define FALSH_READ_BACK
	#define  FLASH_SECTOR_SIZE				(4 * 1024)

	// PARTITION_ID (partition ID)
	#define  MAIN_OCM						0
	#define  SECURE_OCM						1
	#define  HDCP_14_22_KEY					2
	#define  PARTITION_ID_MAX				3

	// Partition address
	#define  MAIN_OCM_FW_ADDR_BASE			0x1000
	#define  MAIN_OCM_FW_ADDR_END			0x8FFF

	#define  SECURE_OCM_FW_ADDR_BASE		0xA000
	#define  SECURE_OCM_FW_ADDR_END			0xCFFF

	#define  HDCP_14_22_KEY_ADDR_BASE		0x9000
	#define  HDCP_14_22_KEY_ADDR_END		0x9FFF

	#define read_status_enable() \
		do{ \
			uint8_t tmp; \
			i2c_read_byte(SLAVEID_SPI, R_DSC_CTRL_0, &tmp); \
			tmp |= READ_STATUS_EN; \
			i2c_write_byte(SLAVEID_SPI, R_DSC_CTRL_0, tmp); \
		}while(0)

	#define write_general_instruction(instruction_type) \
		i2c_write_byte(SLAVEID_SPI, R_FLASH_STATUS_2, instruction_type)

	// READ_DELAY_SELECT = 0, GENERAL_INSTRUCTION_EN = 1;
	#define general_instruction_enable() \
		i2c_write_byte(SLAVEID_SPI, R_FLASH_RW_CTRL, GENERAL_INSTRUCTION_EN)

	#define write_status_register(value) \
		i2c_write_byte(SLAVEID_SPI, R_FLASH_STATUS_0, value)

	// READ_DELAY_SELECT = 0, WRITE_STATUS_EN = 1;
	#define write_status_enable() \
		i2c_write_byte(SLAVEID_SPI, R_FLASH_RW_CTRL, WRITE_STATUS_EN)

	#define flash_write_enable() \
		do{ \
			write_general_instruction(WRITE_ENABLE); \ 
			general_instruction_enable(); \
		}while(0)

	#define flash_write_disable() \
		do{ \
			write_general_instruction(WRITE_DISABLE); \
			general_instruction_enable();\
		}while(0)

	#define flash_write_status_register(value) \
		do{ \
			flash_write_enable(); \
			write_status_register(value); \
			write_status_enable(); \
		}while(0)

	#define flash_address(addr) \
		do{ \ 
			i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_L, addr); \
			i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_H, addr>>8); \
		}while(0)
	
	// FLASH_ERASE_TYPE = R_FLASH_STATUS_3
	#define erase_type(type) \
		i2c_write_byte(SLAVEID_SPI, R_FLASH_STATUS_3, type)

	// READ_DELAY_SELECT = 0, FLASH_ERASE_EN = 1
	#define erase_enable() \
		i2c_write_byte(SLAVEID_SPI, R_FLASH_RW_CTRL, FLASH_ERASE_EN)
	
	#define flash_sector_erase(addr) \
		do{ \
			flash_write_enable(); \
			flash_address(addr); \
			erase_type(SECTOR_ERASE); \    
			erase_enable(); \
		}while(0)

	#define flash_chip_erase() \
		do{ \
			flash_write_enable(); \
			write_general_instruction(CHIP_ERASE_A); \
			general_instruction_enable(); \
		}while(0)

	// READ_DELAY_SELECT = 0, FLASH_READ = 1
	#define ocm_read_enable() \
		i2c_write_byte(SLAVEID_SPI, R_FLASH_RW_CTRL, FLASH_READ)

	// READ_DELAY_SELECT = 0, FLASH_WRITE = 1
	#define ocm_write_enable() \
		i2c_write_byte(SLAVEID_SPI, R_FLASH_RW_CTRL, FLASH_WRITE)


	//#############################################################################
	// Type Definitions
	//-----------------------------------------------------------------------------
	typedef struct
	{
		unsigned long  total_bytes_written;
		uint32_t   previous_addr;
		uint8_t  prog_is_Ping;
		uint8_t  bytes_accumulated_in_Ping;
	} tagFlashRWinfo;


	//#############################################################################
	// Function Prototypes
	//-----------------------------------------------------------------------------
	/**
	 * @brief 
	 *		Program flash with data in a HEX file, 32 bytes at a time.
	 * @details 
	 *		In some special cases, 16 bytes at a time. When the address is not 
	 *		16-byte aligned, simply notify the user and hangs deliberately. The 
	 *		address is always 16-byte aligned, and 16 or 32 bytes are written into 
	 *		Flash at a time, with some special handling, crossing 256-byte (Flash 
	 *		page size) boundary will NEVER happen, thus no need to handle this.
	 * @note 
	 *		This function only programs the Flash; it does NOT automatically 
	 *		erase flash or even backup the data. Erasing Flash properly is the 
	 *		user's responsibility.
	 * @ingroup Chicago_flash
	 * @return void
	 */		
	void flash_program(void);
	
	/**
	 * @brief 
	 *		Erase a flash sector
	 * @ingroup Chicago_flash
	 * @note Command line usage: \\fl_SE (address)
	 * @param Flash_Addr - 16 bit flash address 
	 * @return void
	 */	
	void command_flash_SE(uint32_t Flash_Addr);

	/**
	 * @brief 
	 *		Erase flash partition
	 * @ingroup Chicago_flash
	 * @param part_id - Partition id: MAIN_OCM, SECURE_OCM, HDCP_14_22_KEY
	 * @return void
	 */	
	void command_erase_partition(uint8_t part_id);

	/**
	 * @brief 
	 *		Erase full flash data
	 * @ingroup Chicago_flash
	 * @note Command line usage: \\fl_CE
	 * @return void
	 */		
	void command_flash_CE(void);
	
	/**
	 * @brief 
	 *		Reads flash, and prints the data on the UART console, so that the
	 *		data can be saved in a HEX file.
	 * @ingroup Chicago_flash
	 * @note Command line usage: \\readhex (base_address) (size_to_be_read)
	 * @param Address - Hex Address
	 * @param size_to_be_read - Number of bytes to read
	 * @return void
	 */		
	void command_flash_read(uint32_t Address, uint64_t size_to_be_read);

	/**
	 * @brief 
	 *		Flash programming preparation routine
	 * @ingroup Chicago_flash
	 * @return void
	 */	
	void burn_hex_prepare(void);
	
	/**
	 * @brief 
	 *		Automatically determines whether flash needs updating and burns
	 *		hex file if it does 
	 * @ingroup Chicago_flash
	 * @return uint8_t RETURN_NORMAL_VALUE if success
	 */		
	uint8_t burn_hex_auto(void);
	
	/**
	 * @brief 
	 *		Waits until the flash Write-In-Progress flag is done
	 * @ingroup Chicago_flash
	 * @return void
	 */			
	static void flash_wait_until_WIP_cleared(void);

	/**
	 * @brief 
	 *		Wait until Chicago flash controller hardware state machine returns to idle
	 * @ingroup Chicago_flash
	 * @return void
	 */			
	static void flash_wait_until_flash_SM_done(void);

	/**
	 * @brief 
	 *		Enable flash hardware write protection
	 * @ingroup Chicago_flash
	 * @return void
	 */			
	static void flash_HW_write_protection_enable(void);
	
	/**
	 * @brief 
	 *		Disable flash hardware write protection
	 * @ingroup Chicago_flash
	 * @return void
	 */				
	static void flash_write_protection_disable(void);
	
	/**
	 * @brief 
	 *		Check if HEX file is valid
	 * @ingroup Chicago_flash
	 * @param Address - Hex file address?
	 * @param ByteCount - Number of bytes to check?
	 * @param return_code - MAX_BYTE_COUNT_PER_RECORD_FLASH 
	 * @return void
	 */		
	static void HEX_file_validity_check(uint32_t Address, uint8_t ByteCount, int8_t return_code);
	
	/**
	 * @brief 
	 *		Prepare for flash write
	 * @ingroup Chicago_flash
	 * @param Address - Unknown, ask Analogix
	 * @param offset - Unknown, ask Analogix
	 * @param ByteCount - Unknown, ask Analogix
	 * @param WriteDataBuf - Unknown, ask Analogix 
	 * @return void
	 */		
	static void flash_write_prepare(uint32_t Address, uint8_t offset, uint8_t ByteCount, uint8_t* WriteDataBuf);

	/**
	 * @brief 
	 *		Does something...?
	 * @ingroup Chicago_flash
	 * @param WriteDataBuf - Unknown, ask Analogix
	 * @param ReadDataBuf - Unknown, ask Analogix
	 * @param ByteCount - Unknown, ask Analogix
	 * @return void
	 */	
	static void flash_writedata_keep(uint8_t *WriteDataBuf, uint8_t *ReadDataBuf, uint8_t ByteCount);
	
	/**
	 * @brief 
	 *		Writes to the flash (actually)
	 * @ingroup Chicago_flash
	 * @return void
	 */		
	static void flash_actual_write(void);
	
#endif  /* __FLASH_H__ */

