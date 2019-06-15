// Copyright © 2016 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib
/*
 This code is derived from lmdb with the following copyright:

 Copyright 2011-2016 Howard Chu, Symas Corp. All rights reserved.

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

#include "Malterlib_Database_LowLevel.h"
#include "Malterlib_Database_LowLevel_Internal.h"

#ifdef DPlatformFamily_Linux
#include <sys/utsname.h>
#include <sys/vfs.h>
#endif

namespace NMib
{
	NDatabase::CLowLevelSize DMibDatabaseInfrequentSection NDatabase::NPrivate::mdb_fsize(HANDLE fd)
	{
	#ifdef DPlatformFamily_Windows
		LARGE_INTEGER fsize;

		if (!GetFileSizeEx(fd, &fsize))
			DMibError(NStr::fg_Format("Error getting file size: {}", ErrCode()));

		return fsize.QuadPart;
	#else
		struct stat st;

		if (fstat(fd, &st))
			DMibError(NStr::fg_Format("Error getting file size: {}", ErrCode()));

		return st.st_size;
	#endif
	}

	bool DMibDatabaseInfrequentSection NDatabase::NPrivate::fg_BrokenFSync(MDB_env *_pEnvironment, int _FileDescriptor)
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
			struct statfs st;
			fstatfs(_pEnvironment->me_fd, &st);
			while (st.f_type == 0xEF53) {
				struct utsname uts;
				int i;
				uname(&uts);
				if (uts.release[0] < '3') {
					if (!strncmp(uts.release, "2.6.32.", 7)) {
						i = atoi(uts.release+7);
						if (i >= 60)
							break;	/* 2.6.32.60 and newer is OK */
					} else if (!strncmp(uts.release, "2.6.34.", 7)) {
						i = atoi(uts.release+7);
						if (i >= 15)
							break;	/* 2.6.34.15 and newer is OK */
					}
				} else if (uts.release[0] == '3') {
					i = atoi(uts.release+2);
					if (i > 5)
						break;	/* 3.6 and newer is OK */
					if (i == 5) {
						i = atoi(uts.release+4);
						if (i >= 4)
							break;	/* 3.5.4 and newer is OK */
					} else if (i == 2) {
						i = atoi(uts.release+4);
						if (i >= 30)
							break;	/* 3.2.30 and newer is OK */
					}
				} else {	/* 4.x and newer is OK */
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
