// ----------------------------------------------------------------------------
// PSP Player Emulation Suite
// Copyright (C) 2006 Ben Vanik (noxa)
// Licensed under the LGPL - see License.txt in the project root for details
// ----------------------------------------------------------------------------

using System;
using System.Collections.Generic;
using System.Text;

using Noxa.Emulation.Psp.Debugging;

namespace Noxa.Emulation.Psp.Cpu
{
	public enum ExecutionMode
	{
		Run,
		Step,
		StepN,
		RunUntil,
	}

	public interface ICpu : IComponentInstance
	{
		event EventHandler<BreakpointEventArgs> BreakpointTriggered;

		ICpuCapabilities Capabilities
		{
			get;
		}

		ICpuStatistics Statistics
		{
			get;
		}

		IClock Clock
		{
			get;
		}

		ICpuCore[] Cores
		{
			get;
		}

		ICpuCore this[ int core ]
		{
			get;
		}

		IDmaController Dma
		{
			get;
		}

		IAvcDecoder Avc
		{
			get;
		}

		IMemory Memory
		{
			get;
		}

		byte[] InternalMemory
		{
			get;
		}

		int InternalMemoryBaseAddress
		{
			get;
		}

		ExecutionMode ExecutionMode
		{
			get;
			set;
		}

		int ExecutionParameter
		{
			get;
			set;
		}

		bool DebuggingEnabled
		{
			get;
		}

		void EnableDebugging();

		int RegisterSyscall( uint nid );

		void Resume();
		void Break();

		int ExecuteBlock();

		void PrintStatistics();
	}
}
