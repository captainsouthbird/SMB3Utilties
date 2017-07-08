#include "NoDiceLib.h"
#include <stdlib.h>

#ifdef _WIN32

// Win32 API is of course totally different

// Based on example from http://www.installsetupconfig.com/win32programming/windowsthreadsprocessapis7_18.html

#include <windows.h>
#include <stdio.h>
#include <strsafe.h>

static void FormError(void (*buffer_callback)(const char *))
{
    // Retrieve the system error message for the last-error code

    LPVOID lpMsgBuf;
    DWORD dw = GetLastError();

    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR) &lpMsgBuf,
        0, NULL );

	// Feedback what happened
	buffer_callback(lpMsgBuf);

    LocalFree(lpMsgBuf);
}

int NoDice_exec_build(void (*buffer_callback)(const char *))
{
	int result = 1;

	HANDLE g_hChildStd_IN_Rd = NULL;
	HANDLE g_hChildStd_IN_Wr = NULL;
	HANDLE g_hChildStd_OUT_Rd = NULL;
	HANDLE g_hChildStd_OUT_Wr = NULL;

	SECURITY_ATTRIBUTES saAttr;
	PROCESS_INFORMATION piProcInfo;
	STARTUPINFOA siStartInfo;
	BOOL bSuccess = FALSE;

	DWORD dwRead;
	CHAR chBuf[EXEC_BUF_LINE_LEN];
	HANDLE hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

	// Set the bInheritHandle flag so pipe handles are inherited
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	// Create a pipe for the child process's STDOUT
	if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0))
	  return 0;

	// Ensure the read handle to the pipe for STDOUT is not inherited
	if (!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
	  return 0;


	  // Set up members of the PROCESS_INFORMATION structure
	  ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

	  // Set up members of the STARTUPINFO structure
	  // This structure specifies the STDIN and STDOUT handles for redirection
	  ZeroMemory(&siStartInfo, sizeof(STARTUPINFOA));
	  siStartInfo.cb = sizeof(STARTUPINFOA);
	  siStartInfo.hStdError = g_hChildStd_OUT_Wr;
	  siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
	  siStartInfo.hStdInput = g_hChildStd_IN_Rd;
	  siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

	  // Create the child process
	  bSuccess = CreateProcessA(NULL, // Use szCmdLine
		NoDice_config.buildinfo._build_str,     // command line
		NULL,          // process security attributes
		NULL,          // primary thread security attributes
		TRUE,          // handles are inherited
		CREATE_NO_WINDOW,             // creation flags
		NULL,          // use parent's environment
		NULL,          // use parent's current directory
		&siStartInfo,  // STARTUPINFO pointer
		&piProcInfo);  // receives PROCESS_INFORMATION

	// If an error occurs, report it
	if (!bSuccess)
	{
		FormError(buffer_callback);
		return 0;
	}


	// Read from pipe that is the standard output for child process

	// read end of the pipe, to control child process execution.
	// The pipe is assumed to have enough buffer space to hold the
	// data the child process has already written to it
	if (!CloseHandle(g_hChildStd_OUT_Wr))
		return 0;

	for(;;)
	{
		bSuccess = ReadFile(g_hChildStd_OUT_Rd, chBuf, EXEC_BUF_LINE_LEN-1, &dwRead, NULL);
		if(!bSuccess || dwRead == 0)
			// Probably (hopefully) execution has simply ended
			break;

		// Terminate buffer string
		chBuf[dwRead] = '\0';

		// If we're configured to look for error condition via BUILDERR_TEXTERROR,
		// we must look to see if the word "error" appears in the buffer...
		// This isn't a great test, and I personally prefer by error code, but
		// nesasm does not return non-zero, so this is what we do...
		if(
			// Must be configured to do this check...
			(NoDice_config.buildinfo.builderr == BUILDERR_TEXTERROR) &&

			// Must not have already decided an ill status...
			(result == 1) &&

			// ... and finally, check if "error" is present:
			stristr(chBuf, "error")	)
			{
				// Error found, return zero!
				result = 0;
			}

		// In any case, call callback with buffer
		if(buffer_callback != NULL)
			buffer_callback(chBuf);
	}

	CloseHandle(g_hChildStd_IN_Rd);
	CloseHandle(g_hChildStd_IN_Wr);
	CloseHandle(g_hChildStd_OUT_Rd);

	// If configured for return code, check return code for non-zero
	if(NoDice_config.buildinfo.builderr == BUILDERR_RETURNCODE)
	{
		DWORD code;
		GetExitCodeProcess(piProcInfo.hProcess, &code);
		result = code == 0;
	}

	// Close handles to the child process and its primary thread.
	CloseHandle(piProcInfo.hProcess);
	CloseHandle(piProcInfo.hThread);

	return result;
}

#else

// Linux/Unix pipe/dup2 style redirection

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

int NoDice_exec_build(void (*buffer_callback)(const char *))
{
	int result = 1;
	static char exec_buffer[EXEC_BUF_LINE_LEN];

	int fd[2];	// Descriptors for the pipe

	// Create the pipe in/out
	pipe(fd);

	// Need to fork for the child process
	pid_t pid = fork();
	if(pid == 0)
	{
		// Child

		// Redirect both stdout and stderr into same pipe
		dup2(fd[1], STDOUT_FILENO);
		dup2(fd[1], STDERR_FILENO);

		// Don't need the input end
		close(fd[0]);

		// Run process as configured
		execvp(NoDice_config.buildinfo.build_argv[0], NoDice_config.buildinfo.build_argv);

		// You only get here if the process failed to execute...

		// !!NOTE!! DELIBERATE use of the word "error" in case we're
		// doing trip-on-word-error!  Make sure that remains...
		// This will get piped so it will show up on the parent process.
		perror("Error executing");

		// And otherwise, we must return non-zero to trip in that case
		exit(1);
	}
	else
	{
		// Parent

		int status;
		size_t read_amt;

		dup2(fd[0], STDIN_FILENO);
		close(fd[1]);

		while ((read_amt = fread(exec_buffer, 1, EXEC_BUF_LINE_LEN-1, stdin)))
		{
			// Terminate the string
			exec_buffer[read_amt] = '\0';

			// If we're configured to look for error condition via BUILDERR_TEXTERROR,
			// we must look to see if the word "error" appears in the buffer...
			// This isn't a great test, and I personally prefer by error code, but
			// nesasm does not return non-zero, so this is what we do...
			if(
				// Must be configured to do this check...
				(NoDice_config.buildinfo.builderr == BUILDERR_TEXTERROR) &&

				// Must not have already decided an ill status...
				(result == 1) &&

				// ... and finally, check if "error" is present:
				stristr(exec_buffer, "error")	)
				{
					// Error found, return zero!
					result = 0;
				}

			// In any case, call callback with buffer
			if(buffer_callback != NULL)
				buffer_callback(exec_buffer);
		}

		// Wait for child to finish
		waitpid(pid, &status, 0);


		// If configured for return code, check return code for non-zero
		if(NoDice_config.buildinfo.builderr == BUILDERR_RETURNCODE)
			result = WEXITSTATUS(status) == 0;

	}


	return result;
}

#endif
