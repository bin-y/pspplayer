// ----------------------------------------------------------------------------
// PSP Player Emulation Suite
// Copyright (C) 2006 Ben Vanik (noxa)
// Licensed under the LGPL - see License.txt in the project root for details
// ----------------------------------------------------------------------------

using System;
using System.Collections.Generic;
using System.Text;

namespace Noxa.Emulation.Psp
{
	public interface IComponentInstance
	{
		ComponentParameters Parameters
		{
			get;
		}

		IEmulationInstance Emulator
		{
			get;
		}

		Type Factory
		{
			get;
		}

		void Cleanup();
	}
}
