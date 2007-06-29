// ----------------------------------------------------------------------------
// PSP Player Emulation Suite
// Copyright (C) 2006 Ben Vanik (noxa)
// Licensed under the LGPL - see License.txt in the project root for details
// ----------------------------------------------------------------------------

#include "StdAfx.h"
#include "R4000Generator.h"
#include "R4000Cpu.h"
#include "R4000Core.h"
#include "R4000Memory.h"
#include "R4000GenContext.h"
#include "R4000Vfpu.h"

#include "CodeGenerator.h"

#include <limits>

using namespace System::Diagnostics;
using namespace Noxa::Emulation::Psp;
using namespace Noxa::Emulation::Psp::Bios;
using namespace Noxa::Emulation::Psp::CodeGen;
using namespace Noxa::Emulation::Psp::Cpu;

#define g context->Generator

// http://wiki.fx-world.org/doku.php?id=start&idx=ops
// http://forums.ps2dev.org/viewtopic.php?t=6929&postdays=0&postorder=asc&start=0
// Invaluable!

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

#define MCP2REG( xr, r )		g->dword_ptr[ xr + CTXCP2REGS + ( r << 2 ) ]

enum VfpuWidth
{
	VSingle,
	VPair,
	VTriple,
	VQuad,
	V2x2,
	V3x3,
	V4x4,
};
static const int _vfpuSizes[ 7 ] = { 1, 2, 3, 4, 4, 9, 16 };
static const float _vfpuConstants[ 8 ] = { 0.0f, 1.0f, 2.0f, 0.5f, 3.0f, 1.0f/3.0f, 1.0f/4.0f, 1.0f/6.0f };
enum VfpuPfx
{
	VPFXS = 0,
	VPFXT = 1,
	VPFXD = 2,
};
enum VfpuConditions
{
	VFPU_FL,	VFPU_EQ,	VFPU_LT,	VFPU_LE,	VFPU_TR,	VFPU_NE,	VFPU_GE,	VFPU_GT,
	VFPU_EZ,	VFPU_EN,	VFPU_EI,	VFPU_ES,	VFPU_NZ,	VFPU_NN,	VFPU_NI,	VFPU_NS,
};
enum VfpuConstants
{
	VFPU_VALUE,	VFPU_HUGE,	VFPU_SQRT2,	VFPU_SQRT1_2,	VFPU_2_SQRTPI,	VFPU_2_PI,
	VFPU_1_PI,	VFPU_PI_4,	VFPU_PI_2,	VFPU_PI,		VFPU_E,			VFPU_LOG2E,	VFPU_LOG10E,
	VFPU_LN2,	VFPU_LN10,	VFPU_2PI,	VFPU_PI_6,		VFPU_LOG10TWO,	VFPU_LOG2TEN,
	VFPU_SQRT3_2,
	VFPU_NUM_CONSTANTS,
};
enum VfpuRwb{		VFPU_WT,	VFPU_WB,	VFPU_RWB3,	VFPU_RWB4,	};
enum VfpuPfxCst{	VFPU_CST_0,	VFPU_CST_1,	VFPU_CST_2,	VFPU_CST_1_2,	VFPU_CST_3,	VFPU_CST_1_3,	VFPU_CST_1_4,	VFPU_CST_1_6,	};
enum VfpuPfxSwz{	VFPU_X,	VFPU_Y,	VFPU_Z,	VFPU_W,	};
enum VfpuPfxSat{	VFPU_SAT0,	VFPU_SAT_0_1,	VFPU_SAT2,	VFPU_SAT_1_1,	};

enum VfpuAttributes
{
	VFPU_NORMAL			= 0x00,
	VFPU_BRANCH			= 0x01,
	VFPU_BRANCH_LIKELY	= 0x02,
	VFPU_PFXS			= 0x04,
	VFPU_PFXT			= 0x08,
	VFPU_PFXD			= 0x10,
	VFPU_PFX			= 0x20,	// All PFX bits
};
typedef int (*VfpuImpl)( R4000Ctx* ctx, uint address, uint code );
typedef bool (*VfpuGen)( R4000GenContext^ context, int address, uint code );
typedef struct VfpuInstruction_t
{
	char*			Name;
	char*			Arguments;
	uint			Opcode;
	uint			Mask;
	uint			Attributes;
	VfpuGen			Generate;
	VfpuImpl		Execute;
} VfpuInstruction;
bool VfpuGenDummy( R4000GenContext^ context, int address, uint code );
int VfpuImplDummy( R4000Ctx* ctx, uint address, uint code );

// Contains _vfpuInstructions table, with _vfpuNumInstructions entries
#include "R4000Vfpu_Instructions.h"

GenerationResult Noxa::Emulation::Psp::Cpu::TryEmitVfpu( R4000GenContext^ context, int pass, int address, uint code )
{
	// Look up instruction - slowly
	bool foundInstruction = false;
	VfpuInstruction* instr = ( VfpuInstruction* )&_vfpuInstructions[ 0 ];
	for( int n = 0; n < _vfpuNumInstructions; n++, instr++ )
	{
		if( ( code & instr->Mask ) == instr->Opcode )
		{
			// Found!
			foundInstruction = true;
			break;
		}
	}
	if( foundInstruction == false )
		return GenerationResult::Invalid;

#ifdef _DEBUG
	if( pass == 0 )
	{
		Debug::WriteLine( String::Format( "[0x{0:X8}] {1:X8}\t{2} {3}", address - 4, code, gcnew String( instr->Name ), gcnew String( instr->Arguments ) ) );
	}
#endif

	GenerationResult result = GenerationResult::Success;
	if( ( instr->Attributes & VFPU_BRANCH ) == VFPU_BRANCH )
		result = GenerationResult::Branch;
	else if( ( instr->Attributes & VFPU_BRANCH_LIKELY ) == VFPU_BRANCH_LIKELY )
		result = GenerationResult::BranchAndNullifyDelay;
	bool isBranch = ( result != GenerationResult::Success );

	if( pass == 0 )
	{
		if( isBranch == true )
		{
			int target = address + ( SE( code & 0xFFFF ) << 2 );
			context->DefineBranchTarget( target );
		}
		return result;
	}
	else if( pass == 1 )
	{
		if( isBranch == true )
		{
			int target = address + ( SE( code & 0xFFFF ) << 2 );
			LabelMarker^ targetLabel = context->BranchLabels[ target ];
			Debug::Assert( targetLabel != nullptr );
			context->BranchTarget = targetLabel;
		}

		//g->int3();

		if( instr->Generate != VfpuGenDummy )
		{
			// Emit code
			instr->Generate( context, address, code );
		}
		else
		{
			// Call
			g->push( code );
			g->push( ( uint )address );
			g->push( ( uint )CTX );
			g->call( ( uint )instr->Execute );
			g->add( ESP, 12 );
		}

		// Clear state
		if( ( instr->Attributes & VFPU_PFX ) == VFPU_PFX )
		{
			g->mov( g->dword_ptr[ CTX + CTXCP2PFX ], 0xE4 );
			g->mov( g->dword_ptr[ CTX + CTXCP2PFX + 4 ], 0xE4 );
			g->mov( g->dword_ptr[ CTX + CTXCP2PFX + 8 ], 0xE4 );
			g->mov( g->dword_ptr[ CTX + CTXCP2WM ], 0x0 );
		}
		else
		{
			if( ( instr->Attributes & VFPU_PFXS ) == VFPU_PFXS )
				g->mov( g->dword_ptr[ CTX + CTXCP2PFX ], 0xE4 );
			if( ( instr->Attributes & VFPU_PFXT ) == VFPU_PFXT )
				g->mov( g->dword_ptr[ CTX + CTXCP2PFX + 4 ], 0xE4 );
			if( ( instr->Attributes & VFPU_PFXD ) == VFPU_PFXD )
				g->mov( g->dword_ptr[ CTX + CTXCP2PFX + 8 ], 0x00 );
		}

		if( isBranch == true )
		{
			// EAX contains branch result (0 = no branch, 1 = branch), store it back
			g->mov( MPCVALID( CTX ), EAX );
			if( result == GenerationResult::BranchAndNullifyDelay )
			{
				g->xor( EAX, 0x1 ); // nulldelay = !pcvalid
				g->mov( MNULLDELAY( CTX ), EAX );
			}
		}

		return result;
	}
	else
		return GenerationResult::Invalid;
}

bool VfpuGenDummy( R4000GenContext^ context, int address, uint code )
{
	// ^_^
	return true;
}

// Taken from ector's code
#define VFPU_FLOAT16_EXP_MAX	0x1f
#define VFPU_SH_FLOAT16_SIGN	15
#define VFPU_MASK_FLOAT16_SIGN	0x1
#define VFPU_SH_FLOAT16_EXP	10
#define VFPU_MASK_FLOAT16_EXP	0x1f
#define VFPU_SH_FLOAT16_FRAC	0
#define VFPU_MASK_FLOAT16_FRAC	0x3ff
float Float16ToFloat32( ushort l )
{
	union float2int {
		uint	i;
		float	f;
	} float2int;

	unsigned short float16 = l;
	unsigned int sign = (float16 >> VFPU_SH_FLOAT16_SIGN) & VFPU_MASK_FLOAT16_SIGN;
	int exponent = (float16 >> VFPU_SH_FLOAT16_EXP) & VFPU_MASK_FLOAT16_EXP;
	unsigned int fraction = float16 & VFPU_MASK_FLOAT16_FRAC;

	float signf = (sign == 1) ? -1.0f : 1.0f;

	float f;
	if (exponent == VFPU_FLOAT16_EXP_MAX)
	{
		if (fraction == 0)
			f = std::numeric_limits<float>::infinity();
		else
			f = std::numeric_limits<float>::quiet_NaN();
	}
	else if (exponent == 0 && fraction == 0)
	{
		f=0.0f * signf;
	}
	else
	{
		if (exponent == 0)
		{
			do
			{
				fraction <<= 1;
				exponent--;
			}
			while (!(fraction & (VFPU_MASK_FLOAT16_FRAC + 1)));

			fraction &= VFPU_MASK_FLOAT16_FRAC;
		}

		/* Convert to 32-bit single-precision IEEE754. */
		float2int.i = sign << 31;
		float2int.i |= (exponent + 112) << 23;
		float2int.i |= fraction << 13;
		f=float2int.f;
	}
	return f;
}

bool VfpuGenVPFX( R4000GenContext^ context, int address, uint code )
{
	int set = ( code >> 24 ) & 0x3;
	int value = ( code & 0xFFFFF );
	g->mov( g->dword_ptr[ CTX + CTXCP2PFX + ( set << 2 ) ], value );
	return true;
}

//011000 000 ttttttt 0 sssssss 0 ddddddd
#define VRT( code ) ( ( code >> 16 ) & 0x7F )
#define VRS( code ) ( ( code >> 8 ) & 0x7F )
#define VRD( code ) ( code & 0x7F )
#define RS( code ) ( ( code >> 21 ) & 0x1F )
#define RT( code ) ( ( code >> 16 ) & 0x1F )
#define VREG( xr, r ) g->dword_ptr[ xr + CTXCP2REGS + ( ( r ) << 2 ) ]
#define VIMM( code ) ( int )( ( short )( code & 0x0000FFFF ) )
#define VWIDTH( code ) ( VfpuWidth )( ( ( code >> 7 ) & 0x1 ) + ( ( ( code >> 15 ) & 0x1 ) << 1 ) )

// EAX = address in guest space, result in EAX
void EmitAddressLookup( R4000GenContext^ context, int address );

// EmitVfpuRead and EmitVfpuWrite are taken from ector's code - they are pretty nuts :)

// Reads a vector from a VFPU register to main memory - EAX = target address in main memory
void EmitVfpuRead( R4000GenContext^ context, VfpuWidth dataWidth, int reg, int outMatrixWidth = 0 )
{
	int mtx = ( reg >> 2 ) & 0x7;
	int idx = reg & 0x3;
	int transpose = ( reg >> 5 ) & 0x1;

	int fsl, k, l;
	switch( dataWidth )
	{
	case VSingle:	fsl = ( reg >> 5 ) & 3;	k = 1;	l = 1;	break;
	case VPair:		fsl = ( reg >> 5 ) & 2;	k = 2;	l = 1;	break;
	case VTriple:	fsl = ( reg >> 6 ) & 1;	k = 3;	l = 1;	break;
	case VQuad:		fsl = ( reg >> 5 ) & 2;	k = 4;	l = 1;	break;
	case V2x2:		fsl = ( reg >> 5 ) & 2;	k = 2;	l = 2;	break;
	case V3x3:		fsl = ( reg >> 6 ) & 1;	k = 3;	l = 3;	break;
	case V4x4:		fsl = ( reg >> 5 ) & 2;	k = 4;	l = 4;	break;
	}

	for( int i = 0; i < k; i++ )
	{
		for( int j = 0; j < l; j++ )
		{
			// EAX is base address in memory

			int sourceRegister;
			if( transpose )
				sourceRegister = ( mtx * 4 + ( ( idx + i ) & 3 ) + ( ( fsl + j ) & 3 ) * 32 );
			else
				sourceRegister = ( mtx * 4 + ( ( idx + j ) & 3 ) + ( ( fsl + i ) & 3 ) * 32 );
			
			g->mov( ECX, VREG( CTX, sourceRegister ) );
			int dest = ( ( j * outMatrixWidth + i ) << 2 );
			if( dest == 0 )
				g->mov( g->dword_ptr[ EAX ], ECX );
			else
			{
				g->lea( EBX, g->dword_ptr[ EAX + dest ] );
				g->mov( g->dword_ptr[ EBX ], ECX );
			}
		}
	}
}
// Writes a vector from main memory to a VFPU register - EAX = source address in main memory
void EmitVfpuWrite( R4000GenContext^ context, VfpuWidth dataWidth, int reg, int inMatrixWidth = 0 )
{
	int mtx = ( reg >> 2 ) & 0x7;
	int col = reg & 0x3;
	int transpose = ( reg >> 5 ) & 0x1;

	int row, k, l;
	switch( dataWidth )
	{
	case VSingle:	row = ( reg >> 5 ) & 3;	k = 1;	l = 1;	break;
	case VPair:		row = ( reg >> 5 ) & 2;	k = 2;	l = 1;	break;
	case VTriple:	row = ( reg >> 6 ) & 1;	k = 3;	l = 1;	break;
	case VQuad:		row = ( reg >> 5 ) & 2;	k = 4;	l = 1;	break;
	case V2x2:		row = ( reg >> 5 ) & 2;	k = 2;	l = 2;	break;
	case V3x3:		row = ( reg >> 6 ) & 1;	k = 3;	l = 3;	break;
	case V4x4:		row = ( reg >> 5 ) & 2;	k = 4;	l = 4;	break;
	}

	for( int i = 0; i < k; i++ )
	{
		for( int j = 0; j < l; j++ )
		{
			// EAX is base address in memory

			//if (!writeMask[i])
			if( true )
			{
				int destRegister;
				if( transpose )
					destRegister = ( mtx * 4 + ( ( col + i ) & 3 ) + ( ( row + j ) & 3 ) * 32 );
				else
					destRegister = ( mtx * 4 + ( ( col + j ) & 3 ) + ( ( row + i ) & 3 ) * 32 );
				int src = ( ( j * inMatrixWidth + i ) << 2 );
				if( src == 0 )
					g->mov( ECX, g->dword_ptr[ EAX ] );
				else
				{
					g->lea( EBX, g->dword_ptr[ EAX + src ] );
					g->mov( ECX, g->dword_ptr[ EBX ] );
				}
				g->mov( VREG( CTX, destRegister ), ECX );
			}
		}
	}
}

bool VfpuGenLVS( R4000GenContext^ context, int address, uint code )
{
	g->mov( EAX, MREG( CTX, RS( code ) ) );
	int imm = ( int )( ( short )( code & 0x0000FFFF ) );
	if( imm != 0 )
		g->add( EAX, imm );
	EmitAddressLookup( context, address );
	// EAX = address of memory start

	// Perform load
	int vt = ( ( code >> 16 ) & 0x1F ) | ( ( code & 0x3 ) << 5 );
	EmitVfpuWrite( context, VSingle, vt, 4 );

	return true;
}

bool VfpuGenLVQ( R4000GenContext^ context, int address, uint code )
{
	g->mov( EAX, MREG( CTX, RS( code ) ) );
	int imm = ( int )( ( short )( code & 0x0000FFFF ) );
	if( imm != 0 )
		g->add( EAX, imm );
	EmitAddressLookup( context, address );
	// EAX = address of memory start

	// Perform load
	int vt = ( ( code >> 16 ) & 0x1F ) | ( ( code & 0x3 ) << 5 );
	EmitVfpuWrite( context, VQuad, vt );

	return true;
}

bool VfpuGenSVS( R4000GenContext^ context, int address, uint code )
{
	g->mov( EAX, MREG( CTX, RS( code ) ) );
	int imm = ( int )( ( short )( code & 0x0000FFFF ) );
	if( imm != 0 )
		g->add( EAX, imm );
	EmitAddressLookup( context, address );
	// EAX = address of memory start

	// Perform store
	int vt = ( ( code >> 16 ) & 0x1F ) | ( ( code & 0x3 ) << 5 );
	EmitVfpuRead( context, VSingle, vt );

	return true;
}

bool VfpuGenSVQ( R4000GenContext^ context, int address, uint code )
{
	g->mov( EAX, MREG( CTX, RS( code ) ) );
	int imm = ( int )( ( short )( code & 0x0000FFFF ) );
	if( imm != 0 )
		g->add( EAX, imm );
	EmitAddressLookup( context, address );
	// EAX = address of memory start

	// Perform store
	int vt = ( ( code >> 16 ) & 0x1F ) | ( ( code & 0x3 ) << 5 );
	EmitVfpuRead( context, VQuad, vt );

	return true;
}

bool VfpuGenMTV( R4000GenContext^ context, int address, uint code )
{
	g->mov( EAX, MREG( CTX, RT( code ) ) );
	g->mov( VREG( CTX, VRD( code ) ), EAX );
	return true;
}

bool VfpuGenMFV( R4000GenContext^ context, int address, uint code )
{
	g->mov( EAX, VREG( CTX, VRD( code ) ) );
	g->mov( MREG( CTX, RT( code ) ), EAX );
	return true;
}

bool VfpuGenVIIM( R4000GenContext^ context, int address, uint code )
{
	// Note that the instruction tables don't have the p/t/d encodings!!
	int vt = VRT( code );
	union imm
	{
		int		Integer;
		float	Float;
	} imm;
	imm.Float = ( float )VIMM( code );
	VfpuWidth width = VWIDTH( code );
	switch( width )
	{
	case VSingle:
		g->mov( VREG( CTX, vt ), imm.Integer );
		break;
	case VPair:
		g->mov( VREG( CTX, vt ), imm.Integer );
		g->mov( VREG( CTX, vt + 1 ), imm.Integer );
		break;
	case VTriple:
		g->mov( VREG( CTX, vt ), imm.Integer );
		g->mov( VREG( CTX, vt + 1 ), imm.Integer );
		g->mov( VREG( CTX, vt + 2 ), imm.Integer );
		break;
	case VQuad:
		g->mov( VREG( CTX, vt ), imm.Integer );
		g->mov( VREG( CTX, vt + 1 ), imm.Integer );
		g->mov( VREG( CTX, vt + 2 ), imm.Integer );
		g->mov( VREG( CTX, vt + 3 ), imm.Integer );
		break;
	}
	return true;
}

bool VfpuGenVFIM( R4000GenContext^ context, int address, uint code )
{
	// Note that the instruction tables don't have the p/t/d encodings!!
	int vt = VRT( code );
	union imm
	{
		int		Integer;
		float	Float;
	} imm;
	imm.Float = Float16ToFloat32( ( ushort )( code & 0x0000FFFF ) );
	VfpuWidth width = VWIDTH( code );
	switch( width )
	{
	case VSingle:
		g->mov( VREG( CTX, vt ), imm.Integer );
		break;
	case VPair:
		g->mov( VREG( CTX, vt ), imm.Integer );
		g->mov( VREG( CTX, vt + 1 ), imm.Integer );
		break;
	case VTriple:
		g->mov( VREG( CTX, vt ), imm.Integer );
		g->mov( VREG( CTX, vt + 1 ), imm.Integer );
		g->mov( VREG( CTX, vt + 2 ), imm.Integer );
		break;
	case VQuad:
		g->mov( VREG( CTX, vt ), imm.Integer );
		g->mov( VREG( CTX, vt + 1 ), imm.Integer );
		g->mov( VREG( CTX, vt + 2 ), imm.Integer );
		g->mov( VREG( CTX, vt + 3 ), imm.Integer );
		break;
	}
	return true;
}

// ----------------------------------- BEGIN IMPL -----------------------------------------------------------
// Everything below here is unmanaged!
#pragma unmanaged

int VfpuImplDummy( R4000Ctx* ctx, uint address, uint code )
{
	// ^_^
	return 0;
}

void VfpuGetVector( R4000Ctx* ctx, VfpuWidth dataWidth, int reg, float* p, int matrixWidth = 0 )
{
	int mtx = ( reg >> 2 ) & 0x7;
	int idx = reg & 0x3;
	int transpose = ( reg >> 5 ) & 0x1;

	int fsl, k, l;
	switch( dataWidth )
	{
	case VSingle:	fsl = ( reg >> 5 ) & 3;	k = 1;	l = 1;	break;
	case VPair:		fsl = ( reg >> 5 ) & 2;	k = 2;	l = 1;	break;
	case VTriple:	fsl = ( reg >> 6 ) & 1;	k = 3;	l = 1;	break;
	case VQuad:		fsl = ( reg >> 5 ) & 2;	k = 4;	l = 1;	break;
	case V2x2:		fsl = ( reg >> 5 ) & 2;	k = 2;	l = 2;	break;
	case V3x3:		fsl = ( reg >> 6 ) & 1;	k = 3;	l = 3;	break;
	case V4x4:		fsl = ( reg >> 5 ) & 2;	k = 4;	l = 4;	break;
	}

	for( int i = 0; i < k; i++ )
	{
		for( int j = 0; j < l; j++ )
		{
			if( transpose )
			{
				int sourceRegister = ( mtx * 4 + ( ( idx + i ) & 3 ) + ( ( fsl + j ) & 3 ) * 32 );
				p[ ( j * matrixWidth + i ) ] = ctx->Cp2Registers[ sourceRegister ];
			}
			else
			{
				int sourceRegister = ( mtx * 4 + ( ( idx + j ) & 3 ) + ( ( fsl + i ) & 3 ) * 32 );
				p[ ( j * matrixWidth + i ) ] = ctx->Cp2Registers[ sourceRegister ];
			}
		}
	}
}

void VfpuSetVector( R4000Ctx* ctx, VfpuWidth dataWidth, int reg, float* p, int matrixWidth = 0 )
{
	int mtx = ( reg >> 2 ) & 0x7;
	int col = reg & 0x3;
	int transpose = ( reg >> 5 ) & 0x1;

	int row, k, l;
	switch( dataWidth )
	{
	case VSingle:	row = ( reg >> 5 ) & 3;	k = 1;	l = 1;	break;
	case VPair:		row = ( reg >> 5 ) & 2;	k = 2;	l = 1;	break;
	case VTriple:	row = ( reg >> 6 ) & 1;	k = 3;	l = 1;	break;
	case VQuad:		row = ( reg >> 5 ) & 2;	k = 4;	l = 1;	break;
	case V2x2:		row = ( reg >> 5 ) & 2;	k = 2;	l = 2;	break;
	case V3x3:		row = ( reg >> 6 ) & 1;	k = 3;	l = 3;	break;
	case V4x4:		row = ( reg >> 5 ) & 2;	k = 4;	l = 4;	break;
	}

	for( int i = 0; i < k; i++ )
	{
		for( int j = 0; j < l; j++ )
		{
			// EAX is base address in memory

			//if (!writeMask[i])
			if( true )
			{
				if( transpose )
				{
					int destRegister = ( mtx * 4 + ( ( col + i ) & 3 ) + ( ( row + j ) & 3 ) * 32 );
					ctx->Cp2Registers[ destRegister ] = p[ ( j * matrixWidth + i ) ];
				}
				else
				{
					int destRegister = ( mtx * 4 + ( ( col + j ) & 3 ) + ( ( row + i ) & 3 ) * 32 );
					ctx->Cp2Registers[ destRegister ] = p[ ( j * matrixWidth + i ) ];
				}
			}
		}
	}
}

void VfpuApplyPrefix( R4000Ctx* ctx, VfpuPfx pfx, VfpuWidth dataWidth, float* p )
{
	int pfxValue = ctx->Cp2Pfx[ pfx ];
	if( ( pfx == VPFXS ) || ( pfx == VPFXT ) )
	{
		// A value of E4 indicates a passthrough
		if( pfxValue == 0xE4 )
			return;
		for( int n = 0; n < _vfpuSizes[ dataWidth ]; n++ )
		{
			int abs = ( ( pfxValue >> ( 8 + n ) ) & 1 );
			if( ( ( pfxValue >> ( 12 + n ) ) & 1 ) == 0 )
			{
				// Source from input vector - which is already set
				if( abs == 1 )
					p[ n ] = fabs( p[ n ] );
			}
			else
			{
				// Source from constant
				p[ n ] = _vfpuConstants[ ( ( pfxValue >> ( n * 2 ) ) & 3 ) + ( abs << 2 ) ];
			}

			if( ( ( pfxValue >> ( 16 + n ) ) & 1 ) == 1 )
			{
				// Negate result
				p[ n ] = -p[ n ];
			}
		}
	}
	else
	{
		// VPFXD
		// If 0, nothing
		if( pfxValue == 0 )
			return;
		for( int n = 0; n < _vfpuSizes[ dataWidth ]; n++ )
		{
			//int mask = (pfxValue>>(8+i))&1;
			//writeMask[i] = mask ? true : false;
			int sat = ( pfxValue >> ( n * 2 ) ) & 3;
			if( sat == 1 )
			{
				// [0:1]
				p[ n ] = max( 0.0f, min( 1.0f, p[ n ] ) );
			}
			else if( sat == 3 )
			{
				// [-1:1]
				p[ n ] = max( -1.0f, min( 1.0f, p[ n ] ) );
			}
			// If sat == 0, we pass-through
		}
	}
}

int VfpuImplVSCL( R4000Ctx* ctx, uint address, uint code )
{
	VfpuWidth width = VWIDTH( code );
	float s[ 4 ];
	VfpuGetVector( ctx, width, VRS( code ), s );
	VfpuApplyPrefix( ctx, VPFXS, width, s );
	float scale = ctx->Cp2Registers[ VRT( code ) ];
	switch( width )
	{
	case VPair:
		s[ 0 ] *= scale;
		s[ 1 ] *= scale;
		break;
	case VTriple:
		s[ 0 ] *= scale;
		s[ 1 ] *= scale;
		s[ 2 ] *= scale;
		break;
	case VQuad:
		s[ 0 ] *= scale;
		s[ 1 ] *= scale;
		s[ 2 ] *= scale;
		s[ 3 ] *= scale;
		break;
	}
	VfpuApplyPrefix( ctx, VPFXD, width, s );
	VfpuSetVector( ctx, width, VRD( code ), s );
	
	return 0;
}

// A lot of these could be generated inline or at least use SSE for big speedups
#define F_PI_2 ( ( float )1.57079632679489661923132169164 )
#pragma intrinsic( fabs )
int VfpuImplArith( R4000Ctx* ctx, uint address, uint code )
{
	VfpuWidth width = VWIDTH( code );
	float s[ 4 ];
	VfpuGetVector( ctx, width, VRS( code ), s );
	VfpuApplyPrefix( ctx, VPFXS, width, s );
	for( int n = 0; n < _vfpuSizes[ width ]; n++ )
	{
		switch( ( code >> 16 ) & 0x1F )
		{
		/* vmov  */ case 0:		s[ n ] = s[ n ];							break;
		/* vabs  */ case 1:		s[ n ] = fabs( s[ n ] );					break;
		/* vneg  */ case 2:		s[ n ] = -s[ n ];							break;
		/* vsat0 */ case 4:		s[ n ] = max( 0.0f, min( s[ n ], 1.0f ) );	break;
		/* vsat1 */ case 5:		s[ n ] = max( -1.0f, min( s[ n ], 1.0f ) );	break;
		/* vrcp  */ case 16:	s[ n ] = 1.0f / s[ n ];						break;
		/* vrsq  */ case 17:	s[ n ] = 1.0f / sqrt( s[ n ] );				break;
		/* vsin  */ case 18:	s[ n ] = sin( F_PI_2 * s[ n ] );			break;
		/* vcos  */ case 19:	s[ n ] = cos( F_PI_2 * s[ n ] );			break;
		/* vexp2 */ case 20:	s[ n ] = powf( 2.0f, s[ n ] );				break;
		/* vlog2 */ case 21:	s[ n ] = log( s[ n ] ) / log( 2.0f );		break;
		/* vsqrt */ case 22:	s[ n ] = sqrt( s[ n ] );					break;
		default:
			assert( false );
			break;
		}
	}
	VfpuApplyPrefix( ctx, VPFXD, width, s );
	VfpuSetVector( ctx, width, VRD( code ), s );
	return 0;
}

#pragma managed