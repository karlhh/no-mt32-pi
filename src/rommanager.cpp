//
// rommanager.cpp
//
// mt32-pi - A bare-metal Roland MT-32 emulator for Raspberry Pi
// Copyright (C) 2020  Dale Whinham <daleyo@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include <circle/logger.h>

#include "rommanager.h"

const char ROMManagerName[] = "rommanager";
const char ROMPath[] = "roms";

// Filenames for original ROM loading behaviour
const char MT32ControlROMName[] = "MT32_CONTROL.ROM";
const char MT32PCMROMName[] = "MT32_PCM.ROM";

CROMManager::CROMManager(FATFS& FileSystem)
	: mFileSystem(&FileSystem),
	  mMT32OldControl(nullptr),
	  mMT32NewControl(nullptr),
	  mCM32LControl(nullptr),

	  mMT32PCM(nullptr),
	  mCM32LPCM(nullptr)
{
}

CROMManager::~CROMManager()
{
	const MT32Emu::ROMImage** const roms[] = { &mMT32OldControl, &mMT32NewControl, &mCM32LControl, &mMT32PCM, &mCM32LPCM };
	for (const MT32Emu::ROMImage** rom : roms)
		if (*rom)
			MT32Emu::ROMImage::freeROMImage(*rom);
}

bool CROMManager::ScanROMs()
{
	DIR dir;
	FILINFO fileInfo;
	FRESULT result = f_findfirst(&dir, &fileInfo, ROMPath, "*");

	char path[sizeof(ROMPath) + FF_LFN_BUF];
	strcpy(path, ROMPath);

	// Loop over each file in the directory
	while (result == FR_OK && *fileInfo.fname)
	{
		// Ensure not directory, hidden, or system file
		if (!(fileInfo.fattrib & (AM_DIR | AM_HID | AM_SYS)))
		{
			// Assemble path
			path[sizeof(ROMPath) - 1] = '/';
			strcpy(path + sizeof(ROMPath), fileInfo.fname);

			// Try to open file
			CheckROM(path);
		}

		result = f_findnext(&dir, &fileInfo);
	}

	// Fall back on old ROM loading behavior if we haven't found at least one valid ROM set
	if (!HaveROMSet(TROMSet::Any))
		return CheckROM(MT32ControlROMName) && CheckROM(MT32PCMROMName);

	return true;
}

bool CROMManager::HaveROMSet(TROMSet ROMSet) const
{
	switch (ROMSet)
	{
		case TROMSet::Any:
			return (mMT32OldControl || mMT32NewControl || mCM32LControl) && (mMT32PCM || mCM32LPCM);

		case TROMSet::MT32Old:
			return mMT32OldControl && mMT32PCM;

		case TROMSet::MT32New:
			return mMT32NewControl && mMT32PCM;

		case TROMSet::CM32L:
			return mCM32LControl && mCM32LPCM;
	}

	return false;
}

bool CROMManager::GetROMSet(TROMSet ROMSet, const MT32Emu::ROMImage*& pOutControl, const MT32Emu::ROMImage*& pOutPCM) const
{
	if (!HaveROMSet(ROMSet))
		return false;

	switch (ROMSet)
	{
		case TROMSet::Any:
			if (mMT32OldControl)
				pOutControl = mMT32OldControl;
			else if (mMT32NewControl)
				pOutControl = mMT32NewControl;
			else
				pOutControl = mCM32LControl;

			if (pOutControl == mCM32LControl && mCM32LPCM)
				pOutPCM = mCM32LPCM;
			else
				pOutPCM = mMT32PCM;

			break;

		case TROMSet::MT32Old:
			pOutControl = mMT32OldControl;
			pOutPCM     = mMT32PCM;
			break;

		case TROMSet::MT32New:
			pOutControl = mMT32NewControl;
			pOutPCM     = mMT32PCM;
			break;

		case TROMSet::CM32L:
			pOutControl = mCM32LControl;
			pOutPCM     = mCM32LPCM;
			break;
	}

	return true;
}

bool CROMManager::CheckROM(const char* pPath)
{
	MT32Emu::FileStream* file = new MT32Emu::FileStream();
	if (!file->open(pPath))
	{
		CLogger::Get()->Write(ROMManagerName, LogError, "Couldn't open '%s' for reading", pPath);
		delete file;
		return false;
	}

	// Check ROM and store if valid
	const MT32Emu::ROMImage* rom = MT32Emu::ROMImage::makeROMImage(file);
	if (!StoreROM(*rom))
	{
		MT32Emu::ROMImage::freeROMImage(rom);
		delete file;
		return false;
	}

	return true;
}

bool CROMManager::StoreROM(const MT32Emu::ROMImage& ROMImage)
{
	const MT32Emu::ROMInfo* romInfo = ROMImage.getROMInfo();
	const MT32Emu::ROMImage** romPtr = nullptr;

	// Not a valid ROM file
	if (!romInfo)
		return false;

	if (romInfo->type == MT32Emu::ROMInfo::Type::Control)
	{
		// Is an 'old' MT-32 control ROM
		if (romInfo->shortName[10] == '1' || romInfo->shortName[10] == 'b')
			romPtr = &mMT32OldControl;

		// Is a 'new' MT-32 control ROM
		else if (romInfo->shortName[10] == '2')
			romPtr = &mMT32NewControl;

		// Is a CM-32L control ROM
		else
			romPtr = &mCM32LControl;
	}
	else if (romInfo->type == MT32Emu::ROMInfo::Type::PCM)
	{
		// Is an MT-32 PCM ROM
		if (romInfo->shortName[4] == 'm')
			romPtr = &mMT32PCM;

		// Is a CM-32L PCM ROM
		else
			romPtr = &mCM32LPCM;
	}

	// Ensure we don't already have this ROM
	if (!romPtr || *romPtr)
		return false;

	*romPtr = &ROMImage;
	return true;
}
