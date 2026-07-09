//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2022
//======================================================================================================================
//	execute process
//----------------------------------------------------------------------------------------------------------------------

#ifndef tools_exec_h_
#define tools_exec_h_

#if !defined(tools_common_h_)
#error "requires include: common/common.h"
#endif

//----------------------------------------------------------------------------------------------------------------------

#include <memory>

//----------------------------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
//#error // use MinGW or WSL to build this, for host compatibility & consistency
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

/*
#if defined(__MINGW32__)
extern "C" FILE *popen(const char *command, const char *mode);
extern "C" void pclose(FILE *pipe);
#endif
*/
//----------------------------------------------------------------------------------------------------------------------

static bool exec(const std::string &cmd, std::string &r_result)
{
	char buffer[4096] = { 0 };
	std::string result;
	std::shared_ptr<FILE> pipe(POPEN(cmd.c_str(), "r"), PCLOSE);

	if (pipe == nullptr)
		return false;

	while (!feof(pipe.get()))
	{
		if (fgets(buffer, (4096-1), pipe.get()) != nullptr)
			result += buffer;
	}
	
	r_result = result;
	
	return true;
}

//----------------------------------------------------------------------------------------------------------------------
#endif // tools_exec_h_
//----------------------------------------------------------------------------------------------------------------------
