// ----------------------------------------------------------------------------
// PSP Player Emulation Suite
// Copyright (C) 2006 Ben Vanik (noxa)
// Licensed under the LGPL - see License.txt in the project root for details
// ----------------------------------------------------------------------------

#pragma once

#include "VideoCommands.h"

namespace Noxa {
	namespace Emulation {
		namespace Psp {
			namespace Video {
				namespace Native {

					#pragma pack(push)
					#pragma pack(1)
					typedef struct VideoPacket_t
					{
						int			Argument : 24;
						int			Command : 8;
					} VideoPacket;
					#pragma pack(pop)

					#define ARGF( argument ) ( float )( ( void* )( argument << 8 ) )

				}
			}
		}
	}
}