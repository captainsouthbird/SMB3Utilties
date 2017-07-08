#include <stdio.h>
#include "NoDiceLib.h"
#include "NoDice.h"

#ifdef _WIN32
#include <windows.h>
#endif


void menu_file_save(void *widget, void *callback_data);

int main(int argc, char *argv[])
{
	gui_boot(argc, argv);

	if(!NoDice_Init())
	{
		gui_display_message(TRUE, NoDice_Error());

		NoDice_Shutdown();

		return 1;
	}

	gui_init();

	ppu_init();

	gui_loop();

	ppu_shutdown();

	NoDice_Shutdown();

	return 0;
}



#ifdef _WIN32
int APIENTRY WinMain( HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine, int nCmdShow)
{
	return main( __argc, __argv );
}
#endif
