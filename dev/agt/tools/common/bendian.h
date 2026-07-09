//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2022
//======================================================================================================================
//	planes-to-nibbles conversion
//----------------------------------------------------------------------------------------------------------------------

#ifndef tools_endian_h_
#define tools_endian_h_

#if !defined(tools_common_h_)
#error "requires include: common/common.h"
#endif


//----------------------------------------------------------------------------------------------------------------------

#if defined(_MSC_VER) || defined(__MINGW32__)

// use our own - system includes not consistent
uint32_t agt_htonl(uint32_t hostlong)
{ 
	uint32_t v = hostlong;

	uint32_t vs = 
		((v & 0x000000FF) << 24) | 
		((v & 0x0000FF00) << 8) | 
		((v & 0x00FF0000) >> 8) | 
		((v & 0xFF000000) >> 24)
	;

	return vs;
}
uint32_t agt_ntohl(uint32_t netlong)
{ 
	uint32_t v = netlong;

	uint32_t vs = 
		((v & 0x000000FF) << 24) | 
		((v & 0x0000FF00) << 8) | 
		((v & 0x00FF0000) >> 8) | 
		((v & 0xFF000000) >> 24)
	;

	return vs;
}
uint16_t agt_htons(uint16_t hostword)
{ 
	uint16_t v = hostword;

	uint16_t vs = 
		((v & 0x00FF) << 8) | 
		((v & 0xFF00) >> 8)
	;

	return vs;
}
uint16_t agt_ntohs(uint16_t netword)
{ 
	uint16_t v = netword;

	uint16_t vs = 
		((v & 0x00FF) << 8) | 
		((v & 0xFF00) >> 8)
	;

	return vs;
}

//----------------------------------------------------------------------------------------------------------------------
#else // defined(_MSC_VER) || defined(__MINGW32__)
//----------------------------------------------------------------------------------------------------------------------

#if defined(__linux__)
#include <arpa/inet.h>	// for htonl/ntohl
#endif

// use the platform definitions
#define agt_htonl htonl
#define agt_htons htons
#define agt_ntohl ntohl
#define agt_ntohs ntohs

//----------------------------------------------------------------------------------------------------------------------
#endif // defined(_MSC_VER) || defined(__MINGW32__)
//----------------------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------------------
#endif // tools_endian_h_
//----------------------------------------------------------------------------------------------------------------------
