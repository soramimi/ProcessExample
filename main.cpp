#include <stdio.h>
#include "command.h"

/**
 * @brief Main function to demonstrate command execution
 * @return int Exit status
 */
int main()
{
#ifdef _MSC_VER
	char const *cmd = "cmd.exe /c dir";
#else
	char const *cmd = "ls -l";
#endif
	auto r = command(cmd);
	if (r) {
		printf("Output:\n%s\n", r->c_str());
	} else {
		printf("Failed to execute command.\n");
	}
}
