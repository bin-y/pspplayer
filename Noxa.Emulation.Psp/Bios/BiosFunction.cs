// ----------------------------------------------------------------------------
// PSP Player Emulation Suite
// Copyright (C) 2006 Ben Vanik (noxa)
// Licensed under the LGPL - see License.txt in the project root for details
// ----------------------------------------------------------------------------

using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using System.Text;

using Noxa.Emulation.Psp.Cpu;

namespace Noxa.Emulation.Psp.Bios
{
	#region NotImplementedAttribute

	/// <summary>
	/// Marks a BIOS function as not being implemented.
	/// </summary>
	[global::System.AttributeUsage( AttributeTargets.Method, Inherited = false, AllowMultiple = false )]
	public sealed class NotImplementedAttribute : Attribute
	{
		/// <summary>
		/// Marks the given method as being not implemented.
		/// </summary>
		public NotImplementedAttribute()
		{
		}
	}

	#endregion

	#region StatelessAttribute

	/// <summary>
	/// Marks a BIOS function as not changing the state of the BIOS.
	/// </summary>
	[global::System.AttributeUsage( AttributeTargets.Method, Inherited = false, AllowMultiple = false )]
	public sealed class StatelessAttribute : Attribute
	{
		/// <summary>
		/// Marks the given method as being stateless.
		/// </summary>
		public StatelessAttribute()
		{
		}
	}

	#endregion

	/// <summary>
	/// Represents a function defined inside the BIOS.
	/// </summary>
	public class BiosFunction
	{
		/// <summary>
		/// Module that contains the function.
		/// </summary>
		public readonly BiosModule Module;

		/// <summary>
		/// Current instance of the module that owns this function.
		/// </summary>
		public readonly IModule ModuleInstance;

		/// <summary>
		/// The NID (unique ID) of the function.
		/// </summary>
		public readonly uint NID;

		/// <summary>
		/// The human-friendly name of the function.
		/// </summary>
		public readonly string Name;

		/// <summary>
		/// <c>true</c> if the function is implemented by the BIOS.
		/// </summary>
		public readonly bool IsImplemented;

		/// <summary>
		/// <c>true</c> if the function does not change the state of the BIOS.
		/// </summary>
		/// <remarks>
		/// If this is <c>false</c>, a context switch will be attempted after the call has been executed.
		/// </remarks>
		public readonly bool IsStateless;

		/// <summary>
		/// The <see cref="MethodInfo"/> for the method that implements the managed version of the function.
		/// </summary>
		public readonly MethodInfo MethodInfo;
		
		/// <summary>
		/// The native pointer for the method that implements the native version of the function.
		/// </summary>
		public readonly IntPtr NativeMethod;

		/// <summary>
		/// The number of parameters taken by the function.
		/// </summary>
		public readonly int ParameterCount;

		/// <summary>
		/// Defines the widths of the input parameters, with 0 for 32-bit and 1 for 64-bit.
		/// </summary>
		public readonly BitArray ParameterWidths;

		/// <summary>
		/// <c>true</c> if the call requires access to the memory system.
		/// </summary>
		public readonly bool UsesMemorySystem;

		/// <summary>
		/// Initializes a new <see cref="BiosFunction"/> instance with the given parameters.
		/// </summary>
		/// <param name="module">Module containing the function.</param>
		/// <param name="moduleInstance">Current instance of the module containing the function.</param>
		/// <param name="nid">Unique ID of the function.</param>
		/// <param name="name">Human-friendly name of the function.</param>
		/// <param name="isImplemented"><c>true</c> if the function is implemented.</param>
		/// <param name="isStateless"><c>true</c> if the function does not change the BIOS state.</param>
		/// <param name="methodInfo"><see cref="MethodInfo"/> of the managed implementation of the function.</param>
		/// <param name="nativePointer">Pointer to the native implementation of the function.</param>
		public BiosFunction( BiosModule module, IModule moduleInstance, uint nid, string name, bool isImplemented, bool isStateless, MethodInfo methodInfo, IntPtr nativePointer )
		{
			this.Module = module;
			this.ModuleInstance = moduleInstance;
			this.NID = nid;
			this.Name = name;
			this.IsImplemented = isImplemented;
			this.IsStateless = isStateless;

			this.MethodInfo = methodInfo;
			this.NativeMethod = nativePointer;
			
			ParameterInfo[] ps = methodInfo.GetParameters();
			if( ps.Length > 0 )
			{
				this.ParameterCount = ps.Length;
				if( ps[ 0 ].ParameterType == typeof( IMemory ) )
				{
					this.ParameterCount--;
					this.UsesMemorySystem = true;
				}

				this.ParameterWidths = new BitArray( this.ParameterCount );
				int offset = ( this.UsesMemorySystem == true ) ? 1 : 0;
				for( int n = 0; n < ( ps.Length - offset ); n++ )
					ParameterWidths[ n ] = ( ps[ n + offset ].ParameterType == typeof( long ) );
#if DEBUG
				// Sanity check to make sure IMemory is always the first argument
				if( this.UsesMemorySystem == false )
				{
					for( int n = 0; n < ps.Length; n++ )
						Debug.Assert( ps[ n ].ParameterType != typeof( IMemory ) );
				}
#endif
			}
		}
	}
}
