// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

/*
SPDX-License-Identifier: OLDAP-2.8
SPDX-License-Identifier: ISC
This code is derived from lmdb with the following copyright:

Copyright 2011-2019 Howard Chu, Symas Corp. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted only as authorized by the OpenLDAP
Public License.

A copy of this license is available in the file LICENSE.OpenLDAP in the
top-level directory of the distribution or, alternatively, at
<http://www.OpenLDAP.org/license.html>.

This code is derived from btree.c written by Martin Hedenfalk.

Copyright (c) 2009, 2010 Martin Hedenfalk <martin@bzero.se>

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "Malterlib_Database_Database.h"
#include "Malterlib_Database_Database_Internal.h"

#ifdef DPlatformFamily_Linux
#	include <sys/utsname.h>
#	include <sys/vfs.h>
#endif

namespace NMib::NDatabase::NPrivate
{
	CDatabaseOffset DMibDatabaseInfrequentSection fg_FSize(HANDLE _FileHandle) // NOLINT(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
	{
#ifdef DPlatformFamily_Windows
		LARGE_INTEGER FileSize;

		if (!GetFileSizeEx(_FileHandle, &FileSize))
			DMibError(NStr::fg_Format("Error getting file size: {}", DErrCode()));

		return FileSize.QuadPart;
#else
		struct stat Stat;

		if (fstat(_FileHandle, &Stat))
			DMibError(NStr::fg_Format("Error getting file size: {}", DErrCode()));

		return Stat.st_size;
#endif
	}

	// NOLINTNEXTLINE(malterlib-conceptual-prefix): HANDLE is a pointer on Windows and an int elsewhere
	bool DMibDatabaseInfrequentSection fg_BrokenFSync(CMdbEnv *_pEnvironment, HANDLE _FileDescriptor)
	{
		bool bBroken = false;
#ifdef DPlatformFamily_Linux
		/* ext3/ext4 fdatasync is broken on some older Linux kernels.
		 * https://lkml.org/lkml/2012/9/3/83
		 * Kernels after 3.6-rc6 are known good.
		 * https://lkml.org/lkml/2012/9/10/556
		 * See if the DB is on ext3/ext4, then check for new enough kernel
		 * Kernels 2.6.32.60, 2.6.34.15, 3.2.30, and 3.5.4 are also known
		 * to be patched.
		 */
		{
			struct statfs FileSystemStats;
			fstatfs(_pEnvironment->m_FileHandle, &FileSystemStats);
			while (FileSystemStats.f_type == 0xEF53)
			{
				struct utsname SystemName;
				int VersionNumber;
				uname(&SystemName);
				if (SystemName.release[0] < '3')
				{
					if (!strncmp(SystemName.release, "2.6.32.", 7))
					{
						VersionNumber = atoi(SystemName.release + 7);
						if (VersionNumber >= 60)
							break; // 2.6.32.60 and newer is OK
					}
					else if (!strncmp(SystemName.release, "2.6.34.", 7))
					{
						VersionNumber = atoi(SystemName.release + 7);
						if (VersionNumber >= 15)
							break; // 2.6.34.15 and newer is OK
					}
				}
				else if (SystemName.release[0] == '3')
				{
					VersionNumber = atoi(SystemName.release + 2);
					if (VersionNumber > 5)
						break; // 3.6 and newer is OK
					if (VersionNumber == 5)
					{
						VersionNumber = atoi(SystemName.release + 4);
						if (VersionNumber >= 4)
							break; // 3.5.4 and newer is OK
					}
					else if (VersionNumber == 2)
					{
						VersionNumber = atoi(SystemName.release + 4);
						if (VersionNumber >= 30)
							break; // 3.2.30 and newer is OK
					}
				}
				else
				{ // 4.x and newer is OK
					break;
				}
				bBroken = true;
				break;
			}
		}
#endif
		return bBroken;
	}
}
