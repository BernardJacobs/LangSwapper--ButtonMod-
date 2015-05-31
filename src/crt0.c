/*
 *  LangSwapper--ButtonMod-
 *
 *  Copyright (C) 2014 Omega2058
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <pspkernel.h>
#include <pspthreadman.h>
#include <psputility.h>
#include <psputility_sysparam.h>
#include <pspctrl.h>

// Generic defines.
#define MAKE_CALL(f)							(0x0C000000 | (((u32)(f) >> 2) & 0x03ffffff))
#define NOP										0x00000000
#define SCRATCH_SEGMENT_ADDR					0x00010000

// Defines for sceImposeSetLanguageMode.
#define ASM_RANGE_MAX 							0x84
#define ASM_LANGUAGE_INSTRUCTION 				0x2C83000C 		// sltiu $v1, $a0, 12
#define ASM_BUTTON_INSTRUCTION 					0x2CA70002		// sltiu $a3, $a1, 2
#define ASM_LANGUAGE_PATCHED_INSTRUCTION		0x24040001 		// addiu $a0, zero, $0001
#define ASM_LANGUAGE_PATCHED_INSTRUCTION_BRANCH	0x10000004 		// beq zero, zero, 0x4
#define ASM_BUTTON_PATCHED_INSTRUCTION			0x24050001 		// addiu $a1, zero, $0001
#define ASM_BUTTON_PATCHED_INSTRUCTION_BRANCH	NOP 			// No Operation

// Define for sceUtilitySavedataInitStart.
#define SavedataInitStart_OFFSET				0x18
#define MsgDialogInitStart_OFFSET				0x18

// Define for sceUtilityGetSystemParamInt.
#define PSP_SYSTEMPARAM_ID_INT_LANGUAGE         8

PSP_MODULE_INFO("LangSwapper", PSP_MODULE_KERNEL, 1, 6);

SceUID thid;
u32 _sceImposeSetLanguageMode;
u32 _sceUtilitySavedataInitStart, _sceUtilityMsgDialogInitStart, sd_sub;
int value;

/**
 * Clears the data and instruction cache.
 */
void ClearCaches(void) {
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll();
}

/**
 * Patches sceUtilitySavedataInitStart by hooking into the function and changing the language parameter.
 * Funny stuff happens, so we're using user space for some stuff or some games will break.
 *
 * Huzzah, double pointers~.
 */
void patched_sceUtilitySavedataInitStart(u32 a0, u32 a1) {
	u32 k1 = pspSdkSetK1(0);
	int i, param_struct;
	int ptr = SCRATCH_SEGMENT_ADDR;

	// Get the language pointer and store it in a unused location in scratchpad memory.
	_sw((a1 + 0x4), ptr);

	// Accesses the final location of the structure in scratchpad memory and patches the final values for language.
	param_struct = _lw(ptr);
	_sw(value, param_struct);

	pspSdkSetK1(k1);

	void (*fnc)( u32, u32) = (void *)sd_sub;
	fnc(a0, a1);
}

/**
 * This function searches kernel memory and patches $a0 based on sceUtilityGetSystemParamInt.
 *
 * @ASM_OLD - sltiu $v1, $a0, 12.
 * @ASM_NEW - addiu $a0, zero, $0001
 *
 * Up to 12 modes (0 to 11) exist for the language. With each number representing the language selected.
 *
 * The value is grabbed by sceUtilityGetSystemParamInt and then set to be used by sceImposeSetLanguageMode,
 * resulting in it setting the language set by the system.
 *
 * TLDR: The function below performs voodoo magic, which does things in memory. :)
 */
void patchHomeMenu(u32 addr) {
	int i;
	for (i = 0; i < ASM_RANGE_MAX; i += 4) {
		if (_lw(addr + i) == ASM_LANGUAGE_INSTRUCTION) {
			_sw(ASM_LANGUAGE_PATCHED_INSTRUCTION, addr + i);
			_sb(value, addr + i);
			_sw(ASM_LANGUAGE_PATCHED_INSTRUCTION_BRANCH, (addr + i) + 0x4);
		}
		if (_lw(addr + i) == ASM_BUTTON_INSTRUCTION) {
			_sw(ASM_BUTTON_PATCHED_INSTRUCTION, addr + i);
			_sw(ASM_BUTTON_PATCHED_INSTRUCTION_BRANCH, (addr + i) + 0x4);
		}
	}
}

/**
 * Gets the sub address called by sceUtilitySavedataInitStart and patches it to point to our own sub.
 * Once the patching finishes it continues to run normally.
 */
void patchSaveData(u32 addr, u32 offset) {
	sd_sub = ((*(u32*) (addr + offset) & 0x03FFFFFF) << 2) | 0x80000000;
	_sw(MAKE_CALL(patched_sceUtilitySavedataInitStart), addr + offset);
}

/**
 * Main function that does the black magic.
 */
int mainThread(SceSize args, void *argp) {
	s32 ret;

	// Get the system language beforehand. This should always pass.
	while (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &value)
			!= 0)
		sceKernelDelayThread(10);

	// Find the function in kernel land and patch the Home menu language.
	_sceImposeSetLanguageMode = sctrlHENFindFunction("sceImpose_Driver",
			"sceImpose", 0x36AA6E91);
	if (_sceImposeSetLanguageMode) {
		patchHomeMenu(_sceImposeSetLanguageMode);
	}

	// Find the function in kernel land responsible for handling savedata.
	_sceUtilitySavedataInitStart = sctrlHENFindFunction("sceUtility_Driver",
			"sceUtility", 0x50C4CD57);
	if (_sceUtilitySavedataInitStart) {
		patchSaveData(_sceUtilitySavedataInitStart, SavedataInitStart_OFFSET);
	}

	// Find the function in kernel land responsible for handling the message dialog.
	_sceUtilityMsgDialogInitStart = sctrlHENFindFunction("sceUtility_Driver",
			"sceUtility", 0x2AD8E239);
	if (_sceUtilityMsgDialogInitStart) {
		patchSaveData(_sceUtilityMsgDialogInitStart, MsgDialogInitStart_OFFSET);
	}
	ClearCaches();
	return 0;
}

/**
 * Starts the module.
 */
int module_start(SceSize args, void *argp) {
	thid = sceKernelCreateThread("FunctionThread", &mainThread, 0x18, 0x1000, 0,
			NULL);
	if (thid >= 0)
		sceKernelStartThread(thid, 0, NULL);
	return 0;
}

/**
 * Stops the module.
 */
int module_stop(SceSize args, void *argp) {
	sceKernelTerminateDeleteThread(thid);
	return 0;
}
