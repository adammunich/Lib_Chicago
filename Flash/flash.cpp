/**
* @file flash.cpp
*
* @brief Chicago OCM flasher
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

//#############################################################################
// Includes
//-----------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "./flash.h"
#include "./hexFile.h"

#include "../Chicago/chicago_config.h"
#include "../Chicago/chicago.h"
#include "../I2C/i2c.h"
#include "../Debug/debug.h"


//#############################################################################
// Pre-compiler Definitions
//-----------------------------------------------------------------------------

// Flash write protection range
#define  FLASH_PROTECTION_ALL  (BP4 | BP3 | BP2 | BP1 | BP0)
#define  HW_FLASH_PROTECTION_PATTERN   (SRP0 | FLASH_PROTECTION_ALL)
#define  FLASH_PROTECTION_PATTERN_MASK (SRP0 | BP4 | BP3 | BP2 | BP1 | BP0)

// SRP0 = 0
#define  SW_FLASH_PROTECTION_PATTERN   ( FLASH_PROTECTION_ALL )


//#############################################################################
// Variable Declarations
//-----------------------------------------------------------------------------
extern uint8_t g_bFlashWrite;
extern tagFlashRWinfo g_FlashRWinfo;

#ifdef FALSH_READ_BACK
	extern uint8_t g_bFlashResult;
#endif

extern uint8_t g_CmdLineBuf[CMD_LINE_SIZE];

uint8_t const OCM_FW_DATA[]= {
	#include "ocm_hex.h"
};


//#############################################################################
// Function Definitions
//-----------------------------------------------------------------------------
void flash_program(void){
	
	uint8_t WriteDataBuf[MAX_BYTE_COUNT_PER_RECORD_FLASH];
	uint8_t ByteCount;
	uint32_t  Address;
	uint8_t RecordType;
	uint8_t i;  /* counter */
	uint8_t  RegBak1, RegBak2;  // register values back up
	uint8_t  RegVal;			// register value
	int8_t  return_code = 0;

	#ifdef FALSH_READ_BACK
		uint32_t  read_Address;
		uint8_t read_ByteCount;
		uint8_t read_Count;
		uint8_t /* xdata */ ReadDataBuf[FLASH_READ_MAX_LENGTH];
		uint8_t read_result;
	#endif
	
	// stop secure OCM to avoid buffer access conflict
	i2c_read_byte(SLAVEID_DP_IP, ADDR_HDCP2_CTRL, &RegVal);
	RegBak1 = RegVal;
	RegVal &= (~HDCP2_FW_EN);
	i2c_write_byte(SLAVEID_DP_IP, ADDR_HDCP2_CTRL, RegVal);

	// TODO
	// stop main OCM to avoid buffer access conflict
	i2c_read_byte(SLAVEID_SPI, OCM_DEBUG_CTRL, &RegVal);
	RegBak2 = RegVal;
	RegVal |= OCM_RESET;
	i2c_write_byte(SLAVEID_SPI, OCM_DEBUG_CTRL, RegVal);  

	flash_wait_until_flash_SM_done();

	i2c_write_byte(SLAVEID_SPI, R_FLASH_LEN_H, (FLASH_WRITE_MAX_LENGTH - 1) >> 8);
	i2c_write_byte(SLAVEID_SPI, R_FLASH_LEN_L, (FLASH_WRITE_MAX_LENGTH - 1) & 0xFF);

	/* ================================ Ping: accumulates data ================================ */
	if (g_FlashRWinfo.prog_is_Ping){
		return_code = GetLineData(g_CmdLineBuf, &ByteCount, &Address, &RecordType, WriteDataBuf);
		HEX_file_validity_check(Address, ByteCount, return_code);

		/* end of HEX file */
		if (RecordType == HEX_RECORD_TYPE_EOF){
			g_bFlashWrite = 0;

			#ifdef FALSH_READ_BACK
				if(g_bFlashResult == 1){
					TRACE("Flash ERROR!!! read back data was not the same as write data\n");
					TRACE("Please burn again.\n\n");
				}
				else{
			#endif				
			
			TRACE1("\n\n>ping>Flash program done. %lu bytes written.\n\n", g_FlashRWinfo.total_bytes_written);
			TRACE("You MUST power cycle the EVB now.\n\n");
			
			#ifdef FALSH_READ_BACK
				}
			#endif
			
			// TODO: start a timer to check how much time it takes to program the Flash
			flash_HW_write_protection_enable();
			return;
		}

	write_prepare_in_ping:
		flash_write_prepare(Address, (uint8_t)0, ByteCount, &WriteDataBuf[0]);
		
		#ifdef FALSH_READ_BACK
			read_Address = Address;
			flash_writedata_keep(&WriteDataBuf[0], &ReadDataBuf[0], ByteCount);
			read_ByteCount = ByteCount;
		#endif
		
		g_FlashRWinfo.previous_addr = Address;

		g_FlashRWinfo.bytes_accumulated_in_Ping = ByteCount;
		g_FlashRWinfo.prog_is_Ping = 0;

		// We're now in ping, but we have to do something that is normally done in pong (the Address dictates this),
		if ((Address % FLASH_WRITE_MAX_LENGTH) != 0) {
			// so that we can recover the ping-pong cadence.
			
			for (i=0; i<MAX_BYTE_COUNT_PER_RECORD_FLASH; i++){
				i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_0 + i, 0xFF);
			}
			
			flash_write_prepare(Address - MAX_BYTE_COUNT_PER_RECORD_FLASH, (uint8_t)MAX_BYTE_COUNT_PER_RECORD_FLASH, ByteCount, &WriteDataBuf[0]);
			flash_actual_write();
			
			g_FlashRWinfo.total_bytes_written += ByteCount;
			g_FlashRWinfo.bytes_accumulated_in_Ping = 0;
			g_FlashRWinfo.prog_is_Ping = 1;
			g_FlashRWinfo.previous_addr = Address;
		}
		
		return;
	}

	/* ================================ Pong: program Flash ================================ */
	/* note: GetLineData() can ONLY be called ONCE per flash_program() invoke, */
	/* otherwise serial port buffer has no chance to be updated, and the same HEX record is used twice, */
	/* which is incorrect. */
	if (!g_FlashRWinfo.prog_is_Ping){
		
		return_code = GetLineData(g_CmdLineBuf, &ByteCount, &Address, &RecordType, WriteDataBuf);
		HEX_file_validity_check(Address, ByteCount, return_code);

		/* end of HEX file */
		if (RecordType == HEX_RECORD_TYPE_EOF){
			for (i=0; i<MAX_BYTE_COUNT_PER_RECORD_FLASH; i++){
				i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_0 + MAX_BYTE_COUNT_PER_RECORD_FLASH + i, 0xFF);
            }
			
			flash_actual_write();
			
			g_FlashRWinfo.total_bytes_written += g_FlashRWinfo.bytes_accumulated_in_Ping;
			g_bFlashWrite = 0;

			#ifdef FALSH_READ_BACK
				if(1==g_bFlashResult){
					TRACE("Flash ERROR!!! read back data was not the same as write data\n");
					TRACE("Please burn again.\n\n");
				}
				else{
			#endif				
			
			TRACE1("\n\n>pong>Flash program done. %lu bytes written.\n", g_FlashRWinfo.total_bytes_written);
			TRACE("You MUST power cycle the EVB now.\n\n");

			#ifdef FALSH_READ_BACK
				}
			#endif			
			
			// TODO: start a timer to check how much time it takes to program the Flash
			flash_HW_write_protection_enable();
			return;
		}

		if (((Address % FLASH_WRITE_MAX_LENGTH) != 0) && (Address == g_FlashRWinfo.previous_addr + MAX_BYTE_COUNT_PER_RECORD_FLASH)){
		    
			// contiguous address			
			for (i=0; i<ByteCount; i++){
				i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_0 + g_FlashRWinfo.bytes_accumulated_in_Ping + i, WriteDataBuf[i]);			
			}
						
			#ifdef FALSH_READ_BACK
				flash_writedata_keep(&WriteDataBuf[0], &ReadDataBuf[g_FlashRWinfo.bytes_accumulated_in_Ping], ByteCount);
				read_ByteCount += ByteCount;
			#endif
			
			flash_actual_write();
			
			g_FlashRWinfo.total_bytes_written += (g_FlashRWinfo.bytes_accumulated_in_Ping + ByteCount);
			g_FlashRWinfo.bytes_accumulated_in_Ping = 0;
			g_FlashRWinfo.previous_addr = Address;
			g_FlashRWinfo.prog_is_Ping = 1;
		}
		
		else if (((Address % FLASH_WRITE_MAX_LENGTH) != 0) && (Address != g_FlashRWinfo.previous_addr + MAX_BYTE_COUNT_PER_RECORD_FLASH)){
			// address is not contiguous
			for (i=0; i<MAX_BYTE_COUNT_PER_RECORD_FLASH; i++){
				i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_0 + MAX_BYTE_COUNT_PER_RECORD_FLASH + i, 0xFF);
			}
			
			flash_write_enable();
			
			i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_H, g_FlashRWinfo.previous_addr >> 8);
			i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_L, g_FlashRWinfo.previous_addr & 0xFF);
			flash_actual_write();  // write what was received in ping
			g_FlashRWinfo.total_bytes_written += g_FlashRWinfo.bytes_accumulated_in_Ping;
			g_FlashRWinfo.bytes_accumulated_in_Ping = 0;

			for (i=0; i<MAX_BYTE_COUNT_PER_RECORD_FLASH; i++){
				i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_0 + i, 0xFF);
            }
			
			flash_write_prepare(Address - MAX_BYTE_COUNT_PER_RECORD_FLASH, (uint8_t)MAX_BYTE_COUNT_PER_RECORD_FLASH, ByteCount, &WriteDataBuf[0]);
			flash_actual_write();  // write what is received in this pong
			g_FlashRWinfo.total_bytes_written += ByteCount;
			g_FlashRWinfo.previous_addr = Address;
			g_FlashRWinfo.prog_is_Ping = 1;
		}
		
		else if (((Address % FLASH_WRITE_MAX_LENGTH) == 0) && (Address != g_FlashRWinfo.previous_addr + MAX_BYTE_COUNT_PER_RECORD_FLASH)){
			for (i=0; i<MAX_BYTE_COUNT_PER_RECORD_FLASH; i++){
				i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_0 + MAX_BYTE_COUNT_PER_RECORD_FLASH + i, 0xFF);
			}
			
			flash_write_enable();
			
			i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_H, g_FlashRWinfo.previous_addr >> 8);
			i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_L, g_FlashRWinfo.previous_addr & 0xFF);
			
			flash_actual_write();  // write what was received in ping
			g_FlashRWinfo.total_bytes_written += g_FlashRWinfo.bytes_accumulated_in_Ping;
			g_FlashRWinfo.bytes_accumulated_in_Ping = 0;
			goto write_prepare_in_ping;
		}
		
		else{
			TRACE("Internal ERROR in flash_program()\n");
			TRACE("Please power cycle the EVB and check the HEX file!\n");
			while(1);  /* hangs deliberately so that the user can see it */
		}
		
		#ifdef FALSH_READ_BACK
			// read Flash data back
			//flash_wait_until_flash_SM_done();
	
			/* =============== Reads 32 bytes =============== */
			if(read_ByteCount>(MAX_BYTE_COUNT_PER_RECORD_FLASH*2)){
				read_Count = (MAX_BYTE_COUNT_PER_RECORD_FLASH*2);
			}else{
				read_Count=read_ByteCount;
			}

			i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_H, read_Address >> 8);
			i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_L, read_Address & 0xFF);

			i2c_write_byte(SLAVEID_SPI, R_FLASH_LEN_H, 0);
			i2c_write_byte(SLAVEID_SPI, R_FLASH_LEN_L, FLASH_READ_MAX_LENGTH - 1);

			ocm_read_enable();
			flash_wait_until_flash_SM_done();
			
			TRACE("Read:");
			for (i=0; i<read_Count; i++){
				i2c_read_byte(SLAVEID_SPI, FLASH_READ_D0 + i, &read_result);
				
				TRACE1("%B02X",read_result);
				if(read_result!=ReadDataBuf[i]){
					g_bFlashResult = 1;
				}
			}
		#endif
	}

	// restore register value
	i2c_write_byte(SLAVEID_DP_IP, ADDR_HDCP2_CTRL, RegBak1);  
	
	// TODO
	// restore register value
	i2c_write_byte(SLAVEID_SPI, OCM_DEBUG_CTRL, RegBak2);  
}

//-----------------------------------------------------------------------------
void command_flash_SE(uint32_t Flash_Addr){
	flash_write_protection_disable();
	flash_sector_erase(Flash_Addr);

	#ifndef  DRY_RUN
		flash_wait_until_WIP_cleared();
	#endif

	flash_wait_until_flash_SM_done();
	
	TRACE2("Sector erase done: 0x%04X ~ 0x%04X\n", (Flash_Addr >> 12) * FLASH_SECTOR_SIZE,
		( (Flash_Addr + FLASH_SECTOR_SIZE) >> 12) * FLASH_SECTOR_SIZE - 1);
		
	flash_HW_write_protection_enable();
}

//-----------------------------------------------------------------------------
void command_erase_partition(uint8_t part_id){
	
	// Flash address, any address inside the sector is a valid address for the Sector Erase (SE) command
	uint32_t Flash_Addr;
	uint32_t base_addr;
	uint32_t end_addr;
	
	char *str[PARTITION_ID_MAX] = {	"Main OCM firmware",
					"Secure OCM firmware",
					"HDCP 1.4 & 2.2 key"};

	if (part_id >= PARTITION_ID_MAX) {
		TRACE("Bad parameter! Partition ID is invalid\n");
		return;
	}

	flash_write_protection_disable();

	switch (part_id) {
		case MAIN_OCM:
			base_addr = MAIN_OCM_FW_ADDR_BASE;
			end_addr = MAIN_OCM_FW_ADDR_END;
			break;
		case SECURE_OCM:
			base_addr = SECURE_OCM_FW_ADDR_BASE;
			end_addr = SECURE_OCM_FW_ADDR_END;
			break;
		case HDCP_14_22_KEY:
			base_addr = HDCP_14_22_KEY_ADDR_BASE;
			end_addr = HDCP_14_22_KEY_ADDR_END;
			break;
		default:
			break;
	}

	for (Flash_Addr = base_addr; Flash_Addr <= end_addr; Flash_Addr += FLASH_SECTOR_SIZE) {
		flash_sector_erase(Flash_Addr);

		#ifndef  DRY_RUN
			flash_wait_until_WIP_cleared();
		#endif

		flash_wait_until_flash_SM_done();
	}

	TRACE1("%s erased.\n", str[part_id]);

	flash_HW_write_protection_enable();
}

//-----------------------------------------------------------------------------
void command_flash_CE(void){
	
	flash_write_protection_disable();
	flash_chip_erase();
	
	#ifndef  DRY_RUN
		flash_wait_until_WIP_cleared();
	#endif
	
	flash_wait_until_flash_SM_done();
	
	TRACE("Whole Flash chip erased.\n");
	flash_HW_write_protection_enable();
}

//-----------------------------------------------------------------------------
void command_flash_read(uint32_t Address, uint64_t size_to_be_read){
	// Software workaround to deal with issue MIS2-87 (The first byte is wrong 
	// in every 32 bytes) has been applied.
	uint8_t  ReadDataBuf[FLASH_READ_MAX_LENGTH];
	uint8_t  *ReadDataPtr;
	uint8_t  i,j;  // counter
	uint64_t total_bytes_read = 0;
	uint8_t  checksum;
	uint8_t  RegBak1, RegBak2;  // register values back up
	
	uint8_t  RegVal;  // register value

	if (Address%MAX_BYTE_COUNT_PER_RECORD_FLASH != 0){
		TRACE2("ERROR! Address = 0x%04X, not %bu bytes aligned.\n", Address, MAX_BYTE_COUNT_PER_RECORD_FLASH);
		return;
	}

	// stop secure OCM to avoid buffer access conflict
	i2c_read_byte(SLAVEID_DP_IP, ADDR_HDCP2_CTRL, &RegVal);
	
	RegBak1 = RegVal;
	RegVal &= (~HDCP2_FW_EN);
	i2c_write_byte(SLAVEID_DP_IP, ADDR_HDCP2_CTRL, RegVal);

	// TODO
	// stop main OCM to avoid buffer access conflict
	i2c_read_byte(SLAVEID_SPI, OCM_DEBUG_CTRL, &RegVal);
	RegBak2 = RegVal;
	RegVal |= OCM_RESET;
	i2c_write_byte(SLAVEID_SPI, OCM_DEBUG_CTRL, RegVal);  
		
	flash_wait_until_flash_SM_done();

	TRACE2("Start to read HEX from 0x%04X, size 0x%LX\n", Address, size_to_be_read);
		
	while(size_to_be_read!=0){
		i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_H, Address >> 8);
		i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_L, Address & 0xFF);
		i2c_write_byte(SLAVEID_SPI, R_FLASH_LEN_H, 0);
		i2c_write_byte(SLAVEID_SPI, R_FLASH_LEN_L, FLASH_READ_MAX_LENGTH - 1);  // Reads 32 bytes

		ocm_read_enable();
		flash_wait_until_flash_SM_done();

		/* =============== Reads 32 bytes =============== */
		for (i=0; i<FLASH_READ_MAX_LENGTH; i++){
			i2c_read_byte(SLAVEID_SPI, FLASH_READ_D0 + i, &ReadDataBuf[i]);
		}

		ReadDataPtr = &ReadDataBuf[0];
		for(j=0;j<2;j++){
			TRACE3(":%02BX%04X%02BX", MAX_BYTE_COUNT_PER_RECORD_FLASH, Address, HEX_RECORD_TYPE_DATA);

			checksum = MAX_BYTE_COUNT_PER_RECORD_FLASH + (Address>>8) + (Address&0xFF) + HEX_RECORD_TYPE_DATA;
			for (i=0; i<MAX_BYTE_COUNT_PER_RECORD_FLASH; i++){
				TRACE1("%02BX", ReadDataPtr[i]);
				checksum += ReadDataPtr[i];
			}
			
			TRACE1("%02BX\n", -checksum);
				
			if(size_to_be_read>MAX_BYTE_COUNT_PER_RECORD_FLASH){
				Address += MAX_BYTE_COUNT_PER_RECORD_FLASH;
				total_bytes_read += MAX_BYTE_COUNT_PER_RECORD_FLASH;
				size_to_be_read  -= MAX_BYTE_COUNT_PER_RECORD_FLASH;
				ReadDataPtr = &ReadDataBuf[MAX_BYTE_COUNT_PER_RECORD_FLASH];
			}else{
				size_to_be_read=0;
				j = 1;
			}
		}
	}

	// restore register value
	i2c_write_byte(SLAVEID_DP_IP, ADDR_HDCP2_CTRL, RegBak1);  
	
	// TODO
	// restore register value
	i2c_write_byte(SLAVEID_SPI, OCM_DEBUG_CTRL, RegBak2);  
}

//-----------------------------------------------------------------------------
void burn_hex_prepare(void){
	
	TRACE("You may send the HEX file now. SecureCRT -> Transfer -> Send ASCII ...\n");
	g_bFlashWrite = 1;

	#ifdef FALSH_READ_BACK
		g_bFlashResult = 0;
	#endif

    g_FlashRWinfo.total_bytes_written = 0;
    g_FlashRWinfo.prog_is_Ping = 1;
    flash_write_protection_disable();

    TRACE("Please make sure line send delay (SecureCRT -> Options -> Session Options -> Terminal\n");
    TRACE("-> Emulation -> Advanced -> Line Send Delay) is set to enough long (Chicago: at least 5ms for 1MHz I2C)\n");
    TRACE("before you select HEX file and transfer it.\n");
}

//-----------------------------------------------------------------------------
uint8_t burn_hex_auto(void){	
	
	uint8_t reg_temp;
	uint8_t current_version[3];
	uint8_t hex_version[3];
	uint8_t update_flag;
	uint8_t i;
	uint32_t hex_index;
	uint32_t hex_lines;
	
	uint8_t WriteDataBuf[MAX_BYTE_COUNT_PER_RECORD_FLASH];
	uint8_t ByteCount;
	uint32_t  Address;
	uint8_t RecordType;
	uint8_t RegBak1, RegBak2;		// register values back up
	uint8_t RegVal;				// register value

	uint32_t  read_Address;
	uint8_t read_ByteCount;
	uint8_t read_Count;
	uint8_t ReadDataBuf[FLASH_READ_MAX_LENGTH];
	uint8_t read_result;

	// RESET chicago first
	chicago_power_onoff(CHICAGO_TURN_ON);
	//delay_ms(100);

	#ifdef DEBUG_LEVEL_2
		TRACE("burn_hex_auto(void)\n");
	#endif
	
	// can read I2C or not
	if(i2c_read_byte(SLAVEID_SPI, R_VERSION, &reg_temp) != 0){
		#ifdef DEBUG_LEVEL_2
			TRACE("I2C can't be read, auto-flash FAIL!!!\n");
		#endif
		chicago_power_onoff(0);
		return -1;
	}
	
	// read current OCM version
	i2c_read_byte(SLAVEID_SPI, OCM_VERSION_MAJOR, &reg_temp);
	current_version[0] = (reg_temp >> 4)&0x0F;
	current_version[1] = (reg_temp)&0x0F;
	i2c_read_byte(SLAVEID_SPI, OCM_BUILD_NUM, &reg_temp);
	current_version[2] = reg_temp;

	read_hex_ver(&hex_version[0]);

	#ifdef DEBUG_LEVEL_2	
		TRACE6("\tCurrent OCM version:%01x.%01x.%02x, HEX version:%01x.%01x.%02x\n",\
				current_version[0],current_version[1],current_version[2],\
				hex_version[0],hex_version[1],hex_version[2]);
	#endif
	
	update_flag = 0;
	
	// check version
	for(i=0; i<3; i++){
		if(current_version[i] != hex_version[i]){
			if(current_version[i] > hex_version[i]){
				update_flag = 0;
				break;
			}
			else{ // current < hex
				update_flag = 1;
				break;
			}
		}
	}


	if(update_flag == 0){	
		#ifdef DEBUG_LEVEL_2
			TRACE("\tCurrent version is the same or later then HEX version, no need to flash\n");
		#endif
		//chicago_power_onoff(0);
		return 1;
	}

	
	// Erase OCM first
	command_erase_partition(MAIN_OCM);

	// prepare Flash
	g_bFlashWrite = 1;

	#ifdef FALSH_READ_BACK
		g_bFlashResult = 0;
	#endif

    g_FlashRWinfo.total_bytes_written = 0;
    g_FlashRWinfo.prog_is_Ping = 1;
    flash_write_protection_disable();

    delay_ms(1000);
	
	hex_index = 0;
	hex_lines = (get_hex_size())/HEX_LINE_SIZE;
	
	TRACE("start to flash");
	
	// stop secure OCM to avoid buffer access conflict
	i2c_read_byte(SLAVEID_DP_IP, ADDR_HDCP2_CTRL, &RegVal);
	RegBak1 = RegVal;
	RegVal &= (~HDCP2_FW_EN);
	i2c_write_byte(SLAVEID_DP_IP, ADDR_HDCP2_CTRL, RegVal);

	// stop main OCM to avoid buffer access conflict
	i2c_read_byte(SLAVEID_SPI, OCM_DEBUG_CTRL, &RegVal);
	RegBak2 = RegVal;
	RegVal |= OCM_RESET;
	i2c_write_byte(SLAVEID_SPI, OCM_DEBUG_CTRL, RegVal);  

	flash_wait_until_flash_SM_done();

	i2c_write_byte(SLAVEID_SPI, R_FLASH_LEN_H, (FLASH_WRITE_MAX_LENGTH - 1) >> 8);
	i2c_write_byte(SLAVEID_SPI, R_FLASH_LEN_L, (FLASH_WRITE_MAX_LENGTH - 1) & 0xFF);

	do {
		if((hex_index % 32) == 0){
			TRACE("\n");
		}
		TRACE(".");
		
		memcpy(&WriteDataBuf[0],&OCM_FW_DATA[hex_index*HEX_LINE_SIZE],HEX_LINE_SIZE);
		if(hex_index==hex_lines){
			RecordType = HEX_RECORD_TYPE_EOF;
			ByteCount = 0;
		}else{
			RecordType = HEX_RECORD_TYPE_DATA;
			ByteCount = HEX_LINE_SIZE;
			
			if(hex_index==(hex_lines-1)){
				Address = MAIN_OCM_FW_ADDR_END-HEX_LINE_SIZE+1;
			}else{
				Address = MAIN_OCM_FW_ADDR_BASE+(hex_index*HEX_LINE_SIZE);
			}
		}
		
		/* ================================ Ping: accumulates data ================================ */
		if (g_FlashRWinfo.prog_is_Ping){
	
			/* end of HEX file */
			if (RecordType == HEX_RECORD_TYPE_EOF){
				g_bFlashWrite = 0;
			
				// check read back flag
				if(1==g_bFlashResult){
					TRACE("\nFlash ERROR!!! read back data was not the same as write data\n");
					TRACE("Please burn again.\n\n");
				}else{
					TRACE1("\n[Ping] Flash program done. %lu bytes written.\n\n", g_FlashRWinfo.total_bytes_written);
				}

				// TODO: start a timer to check how much time it takes to program the Flash
				flash_HW_write_protection_enable();
				break;
			}

		auto_write_prepare_in_ping:
			flash_write_prepare(Address, (uint8_t)0, ByteCount, &WriteDataBuf[0]);

			read_Address = Address;
			flash_writedata_keep(&WriteDataBuf[0], &ReadDataBuf[0], ByteCount);
			read_ByteCount = ByteCount;

			g_FlashRWinfo.previous_addr = Address;

			g_FlashRWinfo.bytes_accumulated_in_Ping = ByteCount;
			g_FlashRWinfo.prog_is_Ping = 0;

			if ( (Address % FLASH_WRITE_MAX_LENGTH) != 0 ) // We're now in ping, but we have to do something that is normally done in pong (the Address dictates this),
			{                                              // so that we can recover the ping-pong cadence.

				for (i=0; i<MAX_BYTE_COUNT_PER_RECORD_FLASH; i++){
					i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_0 + i, 0xFF);
				}
				
				flash_write_prepare(Address - MAX_BYTE_COUNT_PER_RECORD_FLASH, (uint8_t)MAX_BYTE_COUNT_PER_RECORD_FLASH, ByteCount, &WriteDataBuf[0]);
				flash_actual_write();

				g_FlashRWinfo.total_bytes_written += ByteCount;
				g_FlashRWinfo.bytes_accumulated_in_Ping = 0;
				g_FlashRWinfo.prog_is_Ping = 1;
				g_FlashRWinfo.previous_addr = Address;
			}

			hex_index++;
			continue;
		}

		/* ================================ Pong: program Flash ================================ */
		/* note: GetLineData() can ONLY be called ONCE per flash_program() invoke, */
		/* otherwise serial port buffer has no chance to be updated, and the same HEX record is used twice, */
		/* which is incorrect. */
		if (!g_FlashRWinfo.prog_is_Ping){
			
			/* end of HEX file */
			if (RecordType == HEX_RECORD_TYPE_EOF){
				
				for (i=0; i<MAX_BYTE_COUNT_PER_RECORD_FLASH; i++){
					i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_0 + MAX_BYTE_COUNT_PER_RECORD_FLASH + i, 0xFF);
				}
				
				flash_actual_write();
				g_FlashRWinfo.total_bytes_written += g_FlashRWinfo.bytes_accumulated_in_Ping;
				g_bFlashWrite = 0;

				if(g_bFlashResult == 1){
					TRACE("\nFlash ERROR!!! read back data was not the same as write data\n");
					TRACE("Please burn again.\n\n");
				}else{
					TRACE1("\n[Pong] Flash program done. %lu bytes written.\n", g_FlashRWinfo.total_bytes_written);
				}
						
				// TODO: start a timer to check how much time it takes to program the Flash
				flash_HW_write_protection_enable();
				break;
			}

			if (((Address % FLASH_WRITE_MAX_LENGTH) != 0) && (Address == g_FlashRWinfo.previous_addr + MAX_BYTE_COUNT_PER_RECORD_FLASH)){
				
				// contiguous address
				for (i=0; i<ByteCount; i++){
					i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_0 + g_FlashRWinfo.bytes_accumulated_in_Ping + i, WriteDataBuf[i]);
				}
				
				flash_writedata_keep(&WriteDataBuf[0], &ReadDataBuf[g_FlashRWinfo.bytes_accumulated_in_Ping], ByteCount);
				read_ByteCount += ByteCount;

				flash_actual_write();
				
				g_FlashRWinfo.total_bytes_written		+= (g_FlashRWinfo.bytes_accumulated_in_Ping + ByteCount);
				g_FlashRWinfo.bytes_accumulated_in_Ping = 0;
				g_FlashRWinfo.previous_addr				= Address;
				g_FlashRWinfo.prog_is_Ping				= 1;
			}

			else if (((Address % FLASH_WRITE_MAX_LENGTH) != 0) && (Address != g_FlashRWinfo.previous_addr + MAX_BYTE_COUNT_PER_RECORD_FLASH) ){
				
				// address is not contiguous
				for (i=0; i<MAX_BYTE_COUNT_PER_RECORD_FLASH; i++){
					i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_0 + MAX_BYTE_COUNT_PER_RECORD_FLASH + i, 0xFF);
				}
				
				flash_write_enable();
				
				i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_H, g_FlashRWinfo.previous_addr >> 8);
				i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_L, g_FlashRWinfo.previous_addr & 0xFF);
				flash_actual_write();  // write what was received in ping
				
				g_FlashRWinfo.total_bytes_written += g_FlashRWinfo.bytes_accumulated_in_Ping;
				g_FlashRWinfo.bytes_accumulated_in_Ping = 0;

				for (i=0; i<MAX_BYTE_COUNT_PER_RECORD_FLASH; i++){
					i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_0 + i, 0xFF);
				}
				
				flash_write_prepare(Address - MAX_BYTE_COUNT_PER_RECORD_FLASH, (uint8_t)MAX_BYTE_COUNT_PER_RECORD_FLASH, ByteCount, &WriteDataBuf[0]);
				flash_actual_write();  // write what is received in this pong
				
				g_FlashRWinfo.total_bytes_written += ByteCount;
				g_FlashRWinfo.previous_addr = Address;
				g_FlashRWinfo.prog_is_Ping = 1;
			}
			
			else if (((Address % FLASH_WRITE_MAX_LENGTH) == 0) && (Address != g_FlashRWinfo.previous_addr + MAX_BYTE_COUNT_PER_RECORD_FLASH)){
				for (i=0; i<MAX_BYTE_COUNT_PER_RECORD_FLASH; i++){
					i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_0 + MAX_BYTE_COUNT_PER_RECORD_FLASH + i, 0xFF);
				}
				flash_write_enable();
				
				i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_H, g_FlashRWinfo.previous_addr >> 8);
				i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_L, g_FlashRWinfo.previous_addr & 0xFF);
				
				flash_actual_write();  // write what was received in ping
				
				g_FlashRWinfo.total_bytes_written += g_FlashRWinfo.bytes_accumulated_in_Ping;
				g_FlashRWinfo.bytes_accumulated_in_Ping = 0;
				goto auto_write_prepare_in_ping;
			}
			else{
				TRACE("\nInternal ERROR in flash_program()!!!\n");
			}
		
			// read Flash data back
			//flash_wait_until_flash_SM_done();

			/* =============== Reads 32 bytes =============== */
			if(read_ByteCount>(MAX_BYTE_COUNT_PER_RECORD_FLASH*2)){
				read_Count = (MAX_BYTE_COUNT_PER_RECORD_FLASH*2);
			}
			else{
				read_Count = read_ByteCount;
			}

			i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_H, read_Address >> 8);
			i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_L, read_Address & 0xFF);

			i2c_write_byte(SLAVEID_SPI, R_FLASH_LEN_H, 0);
			i2c_write_byte(SLAVEID_SPI, R_FLASH_LEN_L, FLASH_READ_MAX_LENGTH - 1);

			ocm_read_enable();
			
			flash_wait_until_flash_SM_done();
			
			/* =============== Reads 32 bytes =============== */
			for (i=0; i<read_Count; i++){
				i2c_read_byte(SLAVEID_SPI, FLASH_READ_D0 + i, &read_result);
				
				TRACE1("%B02X",read_result);
				
				if(read_result!=ReadDataBuf[i]){
					g_bFlashResult = 1;
				}
			}
		}
		hex_index++;
	}while(1);

	// restore register value
	i2c_write_byte(SLAVEID_DP_IP, ADDR_HDCP2_CTRL, RegBak1);  
	
	// TODO
	// restore register value
	i2c_write_byte(SLAVEID_SPI, OCM_DEBUG_CTRL, RegBak2);  
	delay_ms(100);
	
	// RESET chicago after burn done
	chicago_power_onoff(0);
	delay_ms(100);
	
	chicago_power_supply(0);
	delay_ms(100);
	
	chicago_power_supply(1);

    return RETURN_NORMAL_VALUE;
}

//-----------------------------------------------------------------------------
/// @copydoc flash_wait_until_WIP_cleared
static void flash_wait_until_WIP_cleared(void){
	uint8_t  tmp;
	
	do{
		read_status_enable();
		
		// read STATUS_REGISTER
		i2c_read_byte(SLAVEID_SPI, R_FLASH_STATUS_4, &tmp);
	}while((tmp & 1) != 0);
}

//-----------------------------------------------------------------------------
/// @copydoc flash_wait_until_flash_SM_done
static void flash_wait_until_flash_SM_done(void){
	uint8_t  tmp;
	
	do{
		i2c_read_byte(SLAVEID_SPI, R_RAM_CTRL, &tmp);
	}while( (tmp&FLASH_DONE)==0 );
}

//-----------------------------------------------------------------------------
/// @copydoc flash_HW_write_protection_enable
static void flash_HW_write_protection_enable(void){
	uint8_t RegData;

	// 1: flash not wp
	i2c_read_byte(SLAVEID_SPI, GPIO_STATUS_1, &RegData);
	RegData |= FLASH_WP;
	i2c_write_byte(SLAVEID_SPI, GPIO_STATUS_1, RegData);

	RegData = HW_FLASH_PROTECTION_PATTERN;
	flash_wait_until_flash_SM_done();
	flash_write_status_register(RegData);
	
	#ifndef  DRY_RUN
	flash_wait_until_WIP_cleared();
	#endif

	// 0: flash wp, hardware write protected
	i2c_read_byte(SLAVEID_SPI, GPIO_STATUS_1, &RegData);
	RegData &= ~FLASH_WP;
	i2c_write_byte(SLAVEID_SPI, GPIO_STATUS_1, RegData);

	flash_wait_until_flash_SM_done();
	read_status_enable();

	// STATUS_REGISTER
	i2c_read_byte(SLAVEID_SPI, R_FLASH_STATUS_4, &RegData);
	flash_wait_until_flash_SM_done();

	if ((RegData & FLASH_PROTECTION_PATTERN_MASK) == HW_FLASH_PROTECTION_PATTERN){
		TRACE("Flash hardware write protection enabled.\n");
	}
	else{
		TRACE("Enabling Flash hardware write protection FAILED!\n");
	}
}

//-----------------------------------------------------------------------------
/// @copydoc flash_write_protection_disable
static void flash_write_protection_disable(void){
	uint8_t RegData;

	// WP# pin of Flash die = high, not hardware write protected
	i2c_read_byte(SLAVEID_SPI, GPIO_STATUS_1, &RegData);
	RegData |= FLASH_WP;
	i2c_write_byte(SLAVEID_SPI, GPIO_STATUS_1, RegData);
	
	RegData = 0;
	flash_wait_until_flash_SM_done();
	flash_write_status_register(RegData);

	#ifndef  DRY_RUN
	flash_wait_until_WIP_cleared();
	#endif

	flash_wait_until_flash_SM_done();
	read_status_enable();
	
	// STATUS_REGISTER
	i2c_read_byte(SLAVEID_SPI, R_FLASH_STATUS_4, &RegData);
	flash_wait_until_flash_SM_done();

	if ((RegData & FLASH_PROTECTION_PATTERN_MASK) == 0 ){
		TRACE("Flash write protection disabled.\n");
	}
	else{
		TRACE1("Disabling Flash write protection FAILED! = 0x%B02X\n",RegData);
	}
}

//-----------------------------------------------------------------------------
/// @copydoc HEX_file_validity_check
static void HEX_file_validity_check(uint32_t Address, uint8_t ByteCount, int8_t return_code){
	if (return_code != 0){
		TRACE2("HEX file error! Address = 0x%04X, error code = %bd\n", Address, return_code);
		TRACE("Please power cycle the EVB and check the HEX file!\n");
		while(1);  /* hangs deliberately so that the user can see it */
	}

	if (ByteCount > MAX_BYTE_COUNT_PER_RECORD_FLASH){
		TRACE2("ERROR! ByteCount = %bu > %bu\n", ByteCount, MAX_BYTE_COUNT_PER_RECORD_FLASH);
		TRACE("Please power cycle the EVB and check the HEX file!\n");
		while(1);  /* hangs deliberately so that the user can see it */
	}

	if ((Address % MAX_BYTE_COUNT_PER_RECORD_FLASH) != 0 ){
		TRACE2("ERROR! Address = 0x%04X, not %bu bytes aligned.\n", Address, MAX_BYTE_COUNT_PER_RECORD_FLASH);
		TRACE("Please power cycle the EVB and check the HEX file!\n");
		while(1);  /* hangs deliberately so that the user can see it */
	}
}

//-----------------------------------------------------------------------------
/// @copydoc flash_write_prepare
static void flash_write_prepare(uint32_t Address, uint8_t offset, uint8_t ByteCount, uint8_t *WriteDataBuf){
	uint8_t i;  /* counter */

	flash_write_enable();

	i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_H, Address >> 8);
	i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_L, Address & 0xFF);

	for (i=0; i<ByteCount; i++){
		i2c_write_byte(SLAVEID_SPI, R_FLASH_ADDR_0 + offset + i, WriteDataBuf[i]);
	}
}

//-----------------------------------------------------------------------------
/// @copydoc flash_writedata_keep
static void flash_writedata_keep(uint8_t *WriteDataBuf, uint8_t *ReadDataBuf,uint8_t ByteCount){
	uint8_t i;  /* counter */

	for (i=0; i<ByteCount; i++){
		*(ReadDataBuf+i) = *(WriteDataBuf+i);
	}
}

//-----------------------------------------------------------------------------
/// @copydoc flash_actual_write
static void flash_actual_write(void){
	#ifndef  DRY_RUN
	flash_wait_until_WIP_cleared();
	ocm_write_enable();
	flash_wait_until_WIP_cleared();
	#endif

	flash_wait_until_flash_SM_done();
}

//-----------------------------------------------------------------------------
// #if 0
// 	/* basic configurations of the Flash controller, and some global variables initialization  */
// 	/* This is not a debug command, but called by PROC_Main(). */
// 	void flash_basic_config(void){
// 		WriteReg(RX_P0, XTAL_FRQ_SEL, XTAL_FRQ_27M);
// 		flash_wait_until_flash_SM_done();
// 		flash_write_disable();
// 	
// 	#ifndef  DRY_RUN
// 		flash_wait_until_WIP_cleared();
// 	
// 	#endif
// 		flash_wait_until_flash_SM_done();
// 	}
// #endif

